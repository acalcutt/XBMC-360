#ifndef H_DVDINPUTSTREAMHLS
#define H_DVDINPUTSTREAMHLS

#include "DVDInputStream.h"
#include "utils\CriticalSection.h"

#include <vector>

namespace XFILE { class CCurlFile; }

// An HLS stream (an .m3u8 playlist over HTTP), read as one stream: its MPEG-TS segments one after the other, which
// the FFmpeg demuxer takes like any transport stream (this FFmpeg is built without its own HLS demuxer and network
// protocols). A master playlist plays its first variant. Seeking is by time, to the segment that holds it. A playlist
// still growing (no #EXT-X-ENDLIST, as while a server is remuxing) is fetched again as its segments run out.
class CDVDInputStreamHLS
	: public CDVDInputStream
	, public CDVDInputStream::ISeekTime
	, public CDVDInputStream::IDisplayTime
	, public CDVDInputStream::ISeekable
{
public:
	CDVDInputStreamHLS();
	virtual ~CDVDInputStreamHLS();
	virtual bool Open(const char* strFile, const std::string &content);
	virtual void Close();
	virtual int Read(BYTE* buf, int buf_size);
	virtual __int64 Seek(__int64 offset, int whence);
	virtual bool Pause(double dTime) { return false; };
	virtual bool IsEOF();
	virtual __int64 GetLength() { return -1; }
	virtual void Abort();

	// ISeekTime
	virtual bool SeekTime(int ms);

	// IDisplayTime: the length only. The time is the player's own, from the segments' timestamps (-1 says so)
	virtual int GetTotalTime();
	virtual int GetTime() { return -1; }

	// ISeekable
	virtual bool CanSeek() { return true; }
	virtual bool CanPause() { return true; }

	// Whether a file is an HLS playlist: an http(s) URL ending in .m3u8, or one that says it's one
	static bool IsHLS(const std::string& file, const std::string& content);

	// A playlist, as read: either a master playlist's variants, or a media playlist's segments
	struct Segment
	{
		double start;     // seconds, from the first segment listed
		double duration;
		std::string url;
	};
	struct Playlist
	{
		Playlist() : sequence(0), targetDuration(0), ended(false), supported(true) {}
		std::vector<std::string> variants;
		std::vector<Segment> segments;
		int sequence;           // the first segment's number (#EXT-X-MEDIA-SEQUENCE)
		double targetDuration;
		bool ended;             // #EXT-X-ENDLIST: no more segments to come
		bool supported;         // false for encrypted or fMP4 segments, which this can't read
	};
	static bool ParsePlaylist(const std::string& text, const std::string& url, Playlist& playlist);
	static std::string ResolveUrl(const std::string& base, const std::string& ref);

protected:
	bool LoadPlaylist();
	bool OpenSegment();
	void CloseSegment();
	void SetRequest(XFILE::CCurlFile* request);
	bool Wait(int ms);

	CCriticalSection m_section;
	std::string m_playlistUrl;
	Playlist m_playlist;
	int m_current;              // the segment being read, by its number
	XFILE::CCurlFile* m_segment;
	// The request under way (a playlist or a segment), for Abort to interrupt from the thread stopping the player:
	// a server making a segment can take a while to answer. Set and cleared under m_section.
	XFILE::CCurlFile* m_request;
	bool m_eof;
	volatile bool m_aborted;
};

#endif //H_DVDINPUTSTREAMHLS
