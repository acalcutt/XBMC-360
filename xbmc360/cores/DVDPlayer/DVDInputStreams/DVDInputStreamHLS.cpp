#include "DVDInputStreamHLS.h"
#include "filesystem\CurlFile.h"
#include "URL.h"
#include "utils\Log.h"
#include "utils\SingleLock.h"
#include "utils\StringUtils.h"

using namespace XFILE;

// How long to wait for a growing playlist's next segment, in all, before taking the stream as ended
#define HLS_GROWING_WAIT_MS 60000
// Tries at a segment that fails to open (a server still making it can time out once)
#define HLS_SEGMENT_TRIES   3

CDVDInputStreamHLS::CDVDInputStreamHLS() : CDVDInputStream(DVDSTREAM_TYPE_HLS)
{
	m_current = 0;
	m_segment = NULL;
	m_request = NULL;
	m_eof = true;
	m_aborted = false;
}

CDVDInputStreamHLS::~CDVDInputStreamHLS()
{
	Close();
}

bool CDVDInputStreamHLS::IsHLS(const std::string& file, const std::string& content)
{
	CStdString lower(content);
	lower.ToLower();
	if (lower == "application/vnd.apple.mpegurl" || lower == "application/x-mpegurl")
		return true;

	CStdString path(file);
	path.ToLower();
	if (path.Left(7) != "http://" && path.Left(8) != "https://")
		return false;

	int query = path.Find('?');
	if (query >= 0)
		path = path.Left(query);

	return path.Right(5) == ".m3u8";
}

bool CDVDInputStreamHLS::Open(const char* strFile, const std::string& content)
{
	if (!CDVDInputStream::Open(strFile, content))
		return false;

	m_playlistUrl = strFile;
	m_aborted = false;

	if (!LoadPlaylist())
		return false;

	// A master playlist: its first variant (the server lists the one to start with first)
	if (!m_playlist.variants.empty())
	{
		m_playlistUrl = m_playlist.variants[0];
		CLog::Log(LOGNOTICE, "%s - playing variant %s", __FUNCTION__, m_playlistUrl.c_str());

		if (!LoadPlaylist() || !m_playlist.variants.empty())
		{
			CLog::Log(LOGERROR, "%s - no media playlist at %s", __FUNCTION__, m_playlistUrl.c_str());
			return false;
		}
	}

	if (!m_playlist.supported)
	{
		CLog::Log(LOGERROR, "%s - encrypted or fMP4 segments aren't supported: %s", __FUNCTION__, m_playlistUrl.c_str());
		return false;
	}

	m_current = m_playlist.sequence;
	m_eof = false;
	CLog::Log(LOGNOTICE, "%s - %u segments%s, %.0f s", __FUNCTION__, (unsigned int)m_playlist.segments.size(),
		m_playlist.ended ? "" : " so far", GetTotalTime() / 1000.0);
	return true;
}

void CDVDInputStreamHLS::Close()
{
	CloseSegment();
	CDVDInputStream::Close();
	m_playlist = Playlist();
	m_eof = true;
}

// From the thread stopping the player, while this one may be waiting on the server
void CDVDInputStreamHLS::Abort()
{
	CSingleLock lock(m_section);
	m_aborted = true;
	if (m_request)
		m_request->Interrupt();
}

void CDVDInputStreamHLS::SetRequest(CCurlFile* request)
{
	CSingleLock lock(m_section);
	m_request = request;
	if (m_request && m_aborted)
		m_request->Interrupt();
}

int CDVDInputStreamHLS::Read(BYTE* buf, int buf_size)
{
	while (!m_aborted)
	{
		if (!m_segment && !OpenSegment())
		{
			m_eof = true;
			return 0;
		}

		unsigned int ret = m_segment->Read(buf, buf_size);
		if (ret > 0 && ret != (unsigned int)-1)
			return (int)ret;

		// This segment is done: on to the next
		CloseSegment();
		m_current++;
	}
	return -1;
}

// Not by bytes: by time (SeekTime)
__int64 CDVDInputStreamHLS::Seek(__int64 offset, int whence)
{
	if (whence == SEEK_POSSIBLE)
		return 0;

	return -1;
}

bool CDVDInputStreamHLS::IsEOF()
{
	return m_eof;
}

bool CDVDInputStreamHLS::SeekTime(int ms)
{
	CloseSegment();
	CSingleLock lock(m_section);

	if (m_playlist.segments.empty())
		return false;

	// The segment holding that time (or the last one listed, if a growing playlist hasn't reached it yet)
	double seconds = ms / 1000.0;
	unsigned int index = 0;
	while (index + 1 < m_playlist.segments.size() && m_playlist.segments[index + 1].start <= seconds)
		index++;

	m_current = m_playlist.sequence + (int)index;
	m_eof = false;
	CLog::Log(LOGDEBUG, "%s - %.1f s: segment %d (from %.1f s)", __FUNCTION__, seconds, m_current, m_playlist.segments[index].start);
	return true;
}

int CDVDInputStreamHLS::GetTotalTime()
{
	CSingleLock lock(m_section);

	double total = 0;
	if (!m_playlist.segments.empty())
	{
		const Segment& last = m_playlist.segments.back();
		total = last.start + last.duration;
	}

	// Still growing: its full length, if whoever started it knows it
	if (!m_playlist.ended && m_item.HasProperty("hls.duration"))
	{
		double known = atof(m_item.GetProperty("hls.duration").c_str());
		if (known > total)
			total = known;
	}
	return (int)(total * 1000);
}

bool CDVDInputStreamHLS::LoadPlaylist()
{
	CCurlFile http;
	http.SetTimeout(10);
	CStdString text;
	SetRequest(&http);
	bool fetched = http.Get(m_playlistUrl, text);
	SetRequest(NULL);
	if (!fetched)
	{
		if (!m_aborted)
			CLog::Log(LOGERROR, "%s - couldn't fetch %s", __FUNCTION__, m_playlistUrl.c_str());
		return false;
	}

	Playlist playlist;
	if (!ParsePlaylist(text, m_playlistUrl, playlist))
	{
		CLog::Log(LOGERROR, "%s - not an HLS playlist: %s", __FUNCTION__, m_playlistUrl.c_str());
		return false;
	}

	CSingleLock lock(m_section);
	m_playlist = playlist;
	return true;
}

bool CDVDInputStreamHLS::OpenSegment()
{
	// A growing playlist: fetch it again until the segment is listed
	int waited = 0;
	while (m_current - m_playlist.sequence >= (int)m_playlist.segments.size())
	{
		if (m_playlist.ended || waited >= HLS_GROWING_WAIT_MS)
			return false;

		int wait = (int)(m_playlist.targetDuration * 500);
		if (wait < 500)
			wait = 500;

		if (!Wait(wait) || !LoadPlaylist())
			return false;

		waited += wait;
	}

	// Gone from a sliding window: from the oldest still listed
	if (m_current < m_playlist.sequence)
		m_current = m_playlist.sequence;

	const std::string url = m_playlist.segments[m_current - m_playlist.sequence].url;
	for (int tries = 0; tries < HLS_SEGMENT_TRIES && !m_aborted; tries++)
	{
		// Read straight from CCurlFile (no cache in between), so Abort can interrupt it
		m_segment = new CCurlFile();
		SetRequest(m_segment);
		if (m_segment->Open(CURL(url)) && !m_aborted)
			return true;

		if (!m_aborted)
			CLog::Log(LOGWARNING, "%s - couldn't open %s (try %d)", __FUNCTION__, url.c_str(), tries + 1);
		CloseSegment();
	}
	return false;
}

void CDVDInputStreamHLS::CloseSegment()
{
	CCurlFile* segment;
	{
		CSingleLock lock(m_section);
		segment = m_segment;
		m_segment = NULL;
		if (m_request == segment)
			m_request = NULL;
	}

	if (segment)
	{
		segment->Close();
		delete segment;
	}
}

// Sleeps, unless the player stops meanwhile
bool CDVDInputStreamHLS::Wait(int ms)
{
	for (int slept = 0; slept < ms && !m_aborted; slept += 100)
		Sleep(100);

	return !m_aborted;
}

bool CDVDInputStreamHLS::ParsePlaylist(const std::string& text, const std::string& url, Playlist& playlist)
{
	CStdStringArray lines;
	CStdString all(text);
	all.Replace("\r", "");
	CStringUtils::SplitString(all, "\n", lines);

	bool header = false;
	bool variantNext = false;
	double duration = -1;
	double start = 0;

	for (unsigned int i = 0; i < lines.size(); i++)
	{
		CStdString line = lines[i];
		line.Trim();
		if (line.IsEmpty())
			continue;

		if (line.Left(7) == "#EXTM3U")
			header = true;
		else if (line.Left(18) == "#EXT-X-STREAM-INF:")
			variantNext = true;
		else if (line.Left(8) == "#EXTINF:")
			duration = atof(line.Mid(8).c_str());
		else if (line.Left(22) == "#EXT-X-TARGETDURATION:")
			playlist.targetDuration = atof(line.Mid(22).c_str());
		else if (line.Left(22) == "#EXT-X-MEDIA-SEQUENCE:")
			playlist.sequence = atoi(line.Mid(22).c_str());
		else if (line.Left(14) == "#EXT-X-ENDLIST")
			playlist.ended = true;
		else if (line.Left(11) == "#EXT-X-KEY:")
			playlist.supported = playlist.supported && line.Find("METHOD=NONE") >= 0;
		else if (line.Left(11) == "#EXT-X-MAP:")
			playlist.supported = false;
		else if (line[0] == '#')
			continue;
		else if (variantNext)
		{
			playlist.variants.push_back(ResolveUrl(url, line));
			variantNext = false;
		}
		else
		{
			Segment segment;
			segment.start = start;
			segment.duration = duration > 0 ? duration : playlist.targetDuration;
			segment.url = ResolveUrl(url, line);
			playlist.segments.push_back(segment);
			start += segment.duration;
			duration = -1;
		}
	}
	return header;
}

// A playlist's reference to another file: absolute, from the server's root, or beside the playlist
std::string CDVDInputStreamHLS::ResolveUrl(const std::string& base, const std::string& ref)
{
	if (ref.find("://") != std::string::npos)
		return ref;

	std::string path = base.substr(0, base.find('?'));
	if (!ref.empty() && ref[0] == '/')
	{
		size_t host = path.find("://");
		size_t root = host == std::string::npos ? std::string::npos : path.find('/', host + 3);
		return (root == std::string::npos ? path : path.substr(0, root)) + ref;
	}

	size_t slash = path.rfind('/');
	return (slash == std::string::npos ? path + "/" : path.substr(0, slash + 1)) + ref;
}
