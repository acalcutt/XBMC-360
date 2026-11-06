// Include Xbox headers first so _WINSOCKAPI_ is defined, preventing
// curl.h from trying to include winsock2.h (Xbox 360 uses winsockx.h)
#include <xtl.h>
#include <winsockx.h>

// Include curl - rename CURL to CURL_HANDLE to avoid clash with our CURL URL class
#define CURL CURL_HANDLE
#include <curl/curl.h>
#undef CURL

#include "MediaRiffle.h"
#include "Application.h"
#include "FileItem.h"
#include "MediaSource.h"
#include "PlayListPlayer.h"
#include "Settings.h"
#include "URL.h"
#include "filesystem\SpecialProtocol.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\Key.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\XMLUtils.h"
#include "guilib\dialogs\GUIDialogOK.h"
#include "guilib\dialogs\GUIDialogProgress.h"
#include "guilib\tinyxml\tinyxml.h"
#include "utils\JSONVariantParser.h"
#include "utils\Log.h"
#include "utils\SingleLock.h"
#include "utils\StringUtils.h"
#include "utils\SystemInfo.h"
#include "video\VideoInfoTag.h"

CMediaRiffle g_mediaRiffle;

// CCurlFile's: marks each socket libcurl opens so XNet lets it talk to a PC (without it, connecting fails)
extern "C" int xbmc_curl_sockopt_cb(void* clientp, curl_socket_t curlfd, curlsocktype purpose);

#define LINK_FILE      "special://profile/mediariffle.xml"
// How the console asks for its streams: H.264 (no HEVC decoder here) at 720p, which it decodes in time
#define PLAY_QUALITY   "720"
// How often to save where something got to, and to tell the server a paused stream is still wanted
#define REPORT_EVERY   10000
#define ALIVE_EVERY    30000

CMediaRiffle::CMediaRiffle()
{
}

CMediaRiffle::~CMediaRiffle()
{
}

void CMediaRiffle::Start()
{
	// Here, on the main thread, before anything of this asks for a curl handle (the session pool sets it up only when
	// it's first used; this is counted, so the two don't clash)
	curl_global_init(CURL_GLOBAL_ALL);

	LoadLinks();
	Create();

	CSingleLock lock(m_section);
	for (std::map<CStdString, Account>::const_iterator it = m_links.begin(); it != m_links.end(); ++it)
		CLog::Log(LOGNOTICE, "MediaRiffle: linked to %s as %s", it->first.c_str(), it->second.user.c_str());
}

void CMediaRiffle::Stop()
{
	m_bStop = true;
	m_wake.Set();
	StopThread();
	curl_global_cleanup();
}

bool CMediaRiffle::IsMediaRiffle(const CStdString& path)
{
	return path.Left(14).Equals("mediariffle://") || path.Left(15).Equals("mediariffles://");
}

// mediariffle://media01:5080/shows/3/ is on http://media01:5080; mediariffles:// is HTTPS
CStdString CMediaRiffle::ServerOf(const CStdString& path)
{
	CURL url(path);
	if (!IsMediaRiffle(path) || url.GetHostName().IsEmpty())
		return "";

	CStdString server;
	server.Format("%s://%s", url.GetProtocol().Equals("mediariffles") ? "https" : "http", url.GetHostName().c_str());
	if (url.HasPort())
		server.AppendFormat(":%d", url.GetPort());
	server.ToLower();
	return server;
}

CStdString CMediaRiffle::SourceOf(const CStdString& path)
{
	CURL url(path);
	if (!IsMediaRiffle(path) || url.GetHostName().IsEmpty())
		return "";

	CStdString source;
	source.Format("%s://%s", url.GetProtocol().c_str(), url.GetHostName().c_str());
	if (url.HasPort())
		source.AppendFormat(":%d", url.GetPort());
	source.ToLower();
	return source + "/";
}

bool CMediaRiffle::IsLinked(const CStdString& server) const
{
	return !TokenFor(server).IsEmpty();
}

CStdString CMediaRiffle::GetUser(const CStdString& server) const
{
	CSingleLock lock(m_section);
	std::map<CStdString, Account>::const_iterator it = m_links.find(server);
	return it == m_links.end() ? "" : it->second.user;
}

CStdString CMediaRiffle::Describe() const
{
	CSingleLock lock(m_section);
	CStdString lines;
	for (std::map<CStdString, Account>::const_iterator it = m_links.begin(); it != m_links.end(); ++it)
		lines += it->first + ": " + it->second.user + "\r\n";
	return lines.IsEmpty() ? CStdString("Not linked to a server\r\n") : lines;
}

CStdString CMediaRiffle::TokenFor(const CStdString& server) const
{
	CSingleLock lock(m_section);
	std::map<CStdString, Account>::const_iterator it = m_links.find(server);
	return it == m_links.end() ? "" : it->second.token;
}

void CMediaRiffle::Forget(const CStdString& server)
{
	{
		CSingleLock lock(m_section);
		m_links.erase(server);
	}
	SaveLinks();
}

bool CMediaRiffle::Link(const CStdString& server)
{
	if (server.IsEmpty())
		return false;

	if (!g_application.getNetwork().IsAvailable())
	{
		ShowError(g_localizeStrings.Get(33510));
		return false;
	}

	// A code for this console
	CVariant ask(CVariant::VariantTypeObject);
	ask["name"] = "Xbox 360";
	long status = 0;
	CVariant answer;
	if (!CallServer(server, "", "POST", "/api/devices/code", &ask, status, answer, 15))
	{
		ShowUnreachable(server);
		return false;
	}
	if (status != 200)
	{
		ShowError(ErrorOf(answer, status), server);
		return false;
	}

	CStdString deviceCode = answer["deviceCode"].asString();
	CStdString userCode = answer["userCode"].asString();
	CStdString where = server + CStdString(answer["link"].asString("/link"));
	DWORD interval = (DWORD)answer["interval"].asInteger(5) * 1000;
	DWORD expires = (DWORD)answer["expiresIn"].asInteger(600) * 1000;

	// Shown until someone enters it in a browser where they're signed in, or it runs out
	CGUIDialogProgress* dialog = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
	if (!dialog)
		return false;

	CStdString enter;
	enter.Format(g_localizeStrings.Get(33513).c_str(), userCode.c_str());
	dialog->SetHeading(33500);
	dialog->SetLine(0, g_localizeStrings.Get(33512));
	dialog->SetLine(1, where);
	dialog->SetLine(2, enter);
	dialog->SetCanCancel(true);
	dialog->ShowProgressBar(false);
	dialog->StartModal();

	DWORD started = GetTickCount();
	DWORD next = started + interval;
	CStdString failure;
	bool linked = false;

	while (!dialog->IsCanceled())
	{
		dialog->Progress();
		::Sleep(50);

		DWORD now = GetTickCount();
		if (now - started > expires)
		{
			failure = g_localizeStrings.Get(33514);
			break;
		}
		if ((long)(now - next) < 0)
			continue;

		next = now + interval;
		CVariant poll(CVariant::VariantTypeObject);
		poll["deviceCode"] = deviceCode.c_str();
		if (!CallServer(server, "", "POST", "/api/devices/token", &poll, status, answer, 15))
			continue;   // a moment the server can't be reached: ask again next time

		if (status == 202)
			continue;   // not entered yet

		if (status == 429)
		{
			interval += 5000;   // asking too often
			continue;
		}

		if (status == 200 && !answer["token"].asString().empty())
		{
			CSingleLock lock(m_section);
			Account& account = m_links[server];
			account.token = answer["token"].asString();
			account.user = answer["user"].asString();
			linked = true;
		}
		else
			failure = status == 410 ? g_localizeStrings.Get(33514) : ErrorOf(answer, status);
		break;
	}
	dialog->Close();

	if (!linked)
	{
		if (!failure.IsEmpty())
			ShowError(failure);
		return false;
	}

	SaveLinks();
	CLog::Log(LOGNOTICE, "MediaRiffle: linked to %s as %s", server.c_str(), GetUser(server).c_str());

	CStdString as;
	as.Format(g_localizeStrings.Get(33515).c_str(), GetUser(server).c_str());
	ShowError(as, server);
	return true;
}

void CMediaRiffle::Unlink(const CStdString& server)
{
	// The server forgets the device (best effort: it may be unreachable, and the token is dropped here anyway)
	if (IsLinked(server))
		Queue(server, "DELETE", "/api/device", NULL);

	Forget(server);
	CLog::Log(LOGNOTICE, "MediaRiffle: signed out of %s", server.c_str());
}

bool CMediaRiffle::Call(const CStdString& server, const CStdString& method, const CStdString& path, const CVariant* body,
	long& status, CVariant& answer, int timeout)
{
	CStdString token = TokenFor(server);
	if (token.IsEmpty())
		return false;

	if (!CallServer(server, token, method, path, body, status, answer, timeout))
		return false;

	// Unlinked on the server (from its Profile page, or a password change): link again
	if (status == 401)
	{
		CLog::Log(LOGWARNING, "MediaRiffle: %s no longer knows this console's token", server.c_str());
		Forget(server);
	}
	return true;
}

bool CMediaRiffle::Fetch(const CStdString& server, const CStdString& path, std::string& data, long& status)
{
	CStdString token = TokenFor(server);
	if (token.IsEmpty())
		return false;

	return Request(server, token, "GET", path, NULL, status, data, 30);
}

bool CMediaRiffle::CallServer(const CStdString& server, const CStdString& token, const CStdString& method, const CStdString& path,
	const CVariant* body, long& status, CVariant& answer, int timeout)
{
	std::string json;
	if (body)
		json = CJSONVariantWriter::Write(*body);

	std::string response;
	if (!Request(server, token, method, path, body ? &json : NULL, status, response, timeout))
		return false;

	answer = response.empty() ? CVariant() : CJSONVariantParser::Parse(response);
	return true;
}

static size_t WriteToString(char* data, size_t size, size_t count, void* user)
{
	((std::string*)user)->append(data, size * count);
	return size * count;
}

bool CMediaRiffle::Request(const CStdString& server, const CStdString& token, const CStdString& method, const CStdString& path,
	const std::string* body, long& status, std::string& response, int timeout)
{
	status = 0;
	response.clear();

	CURL_HANDLE* h = curl_easy_init();
	if (!h)
		return false;

	CStdString url = server + path;
	char error[CURL_ERROR_SIZE] = "";
	struct curl_slist* headers = NULL;
	if (!token.IsEmpty())
		headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
	headers = curl_slist_append(headers, "Accept: application/json");
	if (body)
		headers = curl_slist_append(headers, "Content-Type: application/json");

	curl_easy_setopt(h, CURLOPT_URL, url.c_str());
	curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_ERRORBUFFER, error);
	curl_easy_setopt(h, CURLOPT_SOCKOPTFUNCTION, xbmc_curl_sockopt_cb);
	// As CCurlFile: the console has no certificate store
	curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(h, CURLOPT_TIMEOUT, (long)timeout);
	curl_easy_setopt(h, CURLOPT_USERAGENT, g_sysinfo.GetUserAgent().c_str());
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, WriteToString);
	curl_easy_setopt(h, CURLOPT_WRITEDATA, &response);

	if (method.Equals("POST"))
	{
		curl_easy_setopt(h, CURLOPT_POST, 1L);
		curl_easy_setopt(h, CURLOPT_POSTFIELDS, body ? body->c_str() : "");
		curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)(body ? body->size() : 0));
	}
	else if (!method.Equals("GET"))
		curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method.c_str());

	CURLcode result = curl_easy_perform(h);
	curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
	curl_slist_free_all(headers);
	curl_easy_cleanup(h);

	if (result != CURLE_OK)
	{
		CStdString reason = error[0] ? error : curl_easy_strerror(result);
		CLog::Log(LOGERROR, "MediaRiffle: %s %s failed: %s", method.c_str(), url.c_str(), reason.c_str());
		CSingleLock lock(m_section);
		m_lastError = reason;
		return false;
	}
	if (status >= 400)
		CLog::Log(LOGWARNING, "MediaRiffle: %s %s: %ld %s", method.c_str(), url.c_str(), status, response.substr(0, 200).c_str());
	return true;
}

// <source>play/<file> (or <source>movie/<movie>, its first part): a stream of it, from where it stopped
bool CMediaRiffle::StartPlayback(const CFileItem& item, CFileItem& stream)
{
	CStdString server = ServerOf(item.GetPath());
	CStdStringArray parts;
	CStdString rest = CURL(item.GetPath()).GetFileName();
	rest.TrimRight("/");
	CStringUtils::SplitString(rest, "/", parts);
	if (server.IsEmpty() || parts.size() < 2)
		return false;

	CStdString kind = parts[0];
	CStdString id = parts[1];
	int fileId = atoi(id.c_str());

	if (!IsLinked(server) && !Link(server))
		return false;

	long status = 0;
	CVariant answer;
	if (kind.Equals("movie"))
	{
		if (!Call(server, "GET", "/api/movies/" + id, NULL, status, answer) || status != 200 || answer["parts"].size() == 0)
		{
			ShowError(ErrorOf(answer, status));
			return false;
		}
		fileId = (int)answer["parts"][0u]["id"].asInteger();
	}

	CVariant play(CVariant::VariantTypeObject);
	play["fileId"] = fileId;
	play["burn"] = false;
	play["quality"] = PLAY_QUALITY;
	play["hevc"] = false;

	// Transcoding has to start before it answers
	if (!Call(server, "POST", "/api/play", &play, status, answer, 90))
	{
		ShowUnreachable(server);
		return false;
	}
	if (status != 200)
	{
		ShowError(ErrorOf(answer, status));
		return false;
	}

	CStdString mode = answer["mode"].asString();
	bool adaptive = answer["adaptive"].asBoolean();
	double duration = answer["duration"].asDouble();
	double seekTo = answer["seekTo"].asDouble();

	stream = item;
	stream.SetPath(server + CStdString(answer["url"].asString()));
	stream.SetMimeType(mode.Equals("Direct") ? CStdString(answer["contentType"].asString()) : "video/mp2t");
	// The whole file (adaptive, or sent as it is) starts where it stopped by seeking; a remuxed one starts there
	stream.m_lStartOffset = (long)(seekTo * 75.0);

	// Something else of MediaRiffle's was playing: it's done (the player switches without saying it stopped)
	OnStopped(false);

	{
		CSingleLock lock(m_section);
		m_playing = Playing();
		m_playing.server = server;
		m_playing.fileId = fileId;
		m_playing.sessionId = answer["sessionId"].asString();
		m_playing.stream = stream.GetPath();
		m_playing.offset = adaptive || mode.Equals("Direct") ? 0 : answer["startSeconds"].asDouble();
		m_playing.lastReport = m_playing.lastAlive = GetTickCount();
	}
	// A remuxed stream's playlist grows as it's made: its length is what's left of the file
	if (duration > m_playing.offset)
	{
		CStdString length;
		length.Format("%.3f", duration - m_playing.offset);
		stream.SetProperty("hls.duration", length);
	}

	CLog::Log(LOGNOTICE, "MediaRiffle: playing file %d, %s (%s), from %.0f s", fileId, mode.c_str(),
		answer["reason"].asString().c_str(), m_playing.offset + seekTo);
	return true;
}

bool CMediaRiffle::IsQueue(const CStdString& path)
{
	if (ServerOf(path).IsEmpty())
		return false;

	CStdString rest = CURL(path).GetFileName();
	return rest.Left(6).Equals("queue/") || rest.Left(8).Equals("shuffle/");
}

bool CMediaRiffle::PlayQueue(const CFileItem& item)
{
	CStdString server = ServerOf(item.GetPath());
	CStdString source = SourceOf(item.GetPath());
	CStdStringArray parts;
	CStdString rest = CURL(item.GetPath()).GetFileName();
	rest.TrimRight("/");
	CStringUtils::SplitString(rest, "/", parts);
	if (server.IsEmpty() || parts.size() < 3)
		return false;

	CStdString what = parts[1];
	if (!what.Equals("library") && !what.Equals("show") && !what.Equals("tag") && !what.Equals("list"))
		return false;

	if (!IsLinked(server) && !Link(server))
		return false;

	// The value is URL-encoded already (a tag or a list's name, as its folder's path has it)
	bool shuffle = parts[0].Equals("shuffle");
	CStdString query;
	query.Format("/api/queue?%s=%s&shuffle=%s", what.c_str(), parts[2].c_str(), shuffle ? "true" : "false");

	long status = 0;
	CVariant answer;
	if (!Call(server, "GET", query, NULL, status, answer, 30))
	{
		ShowUnreachable(server);
		return false;
	}
	if (status != 200)
	{
		ShowError(ErrorOf(answer, status));
		return false;
	}

	// Its videos (music has its own place)
	CFileItemList videos;
	for (unsigned int i = 0; i < answer.size(); i++)
	{
		if (answer[i]["audio"].asBoolean())
			continue;

		CStdString title = answer[i]["title"].asString();
		CStdString path;
		path.Format("%splay/%d", source.c_str(), (int)answer[i]["id"].asInteger());

		CFileItemPtr video(new CFileItem(title));
		video->SetPath(path);
		video->SetMimeType("video/mp2t");
		video->SetLabelPreformated(true);
		video->GetVideoInfoTag()->m_strTitle = title;
		videos.Add(video);
	}
	if (videos.IsEmpty())
		return false;

	CLog::Log(LOGNOTICE, "MediaRiffle: playing %d videos in a row (%s %s%s)", videos.Size(), what.c_str(), parts[2].c_str(),
		shuffle ? ", shuffled" : "");
	g_playlistPlayer.ClearPlaylist(PLAYLIST_VIDEO);
	g_playlistPlayer.Reset();
	g_playlistPlayer.Add(PLAYLIST_VIDEO, videos);
	g_playlistPlayer.SetCurrentPlaylist(PLAYLIST_VIDEO);
	g_playlistPlayer.Play();
	return true;
}

void CMediaRiffle::OnPlaying(const CStdString& currentFile, double seconds, bool paused)
{
	CSingleLock lock(m_section);
	if (m_playing.fileId == 0 || !currentFile.Equals(m_playing.stream))
		return;

	m_playing.seconds = seconds;
	DWORD now = GetTickCount();

	if (!paused && now - m_playing.lastReport >= REPORT_EVERY)
	{
		m_playing.lastReport = now;
		CVariant progress(CVariant::VariantTypeObject);
		progress["fileId"] = m_playing.fileId;
		progress["position"] = m_playing.offset + seconds;
		Queue(m_playing.server, "POST", "/api/progress", &progress);
	}

	// Paused: nothing is fetched, so the server is told it's still wanted
	if (paused && now - m_playing.lastAlive >= ALIVE_EVERY)
	{
		m_playing.lastAlive = now;
		Queue(m_playing.server, "GET", "/stream/" + m_playing.sessionId + "/alive", NULL);
	}
	else if (!paused)
		m_playing.lastAlive = now;
}

void CMediaRiffle::OnStopped(bool ended)
{
	CSingleLock lock(m_section);
	if (m_playing.fileId == 0)
		return;

	CVariant progress(CVariant::VariantTypeObject);
	progress["fileId"] = m_playing.fileId;
	progress["position"] = m_playing.offset + m_playing.seconds;
	if (ended)
		progress["finished"] = true;
	Queue(m_playing.server, "POST", "/api/progress", &progress);
	Queue(m_playing.server, "DELETE", "/api/play/" + m_playing.sessionId, NULL);

	m_playing = Playing();
}

// Sent with the link as it is now (a sign-out's DELETE still carries the token it's dropping)
void CMediaRiffle::Queue(const CStdString& server, const CStdString& method, const CStdString& path, const CVariant* body)
{
	Report report;
	report.server = server;
	report.token = TokenFor(server);
	report.method = method;
	report.path = path;
	report.hasBody = body != NULL;
	if (body)
		report.body = *body;
	if (report.token.IsEmpty())
		return;

	CSingleLock lock(m_section);
	m_reports.push_back(report);
	m_wake.Set();
}

void CMediaRiffle::Process()
{
	SetName("MediaRiffle");
	while (!m_bStop)
	{
		m_wake.WaitMSec(1000);

		for (;;)
		{
			Report report;
			{
				CSingleLock lock(m_section);
				if (m_reports.empty())
					break;
				report = m_reports.front();
				m_reports.pop_front();
			}

			long status = 0;
			CVariant answer;
			CallServer(report.server, report.token, report.method, report.path, report.hasBody ? &report.body : NULL, status, answer, 15);
		}
	}
}

// <mediariffle><link><server/><token/><user/></link>…</mediariffle>, a link for each server
void CMediaRiffle::LoadLinks()
{
	TiXmlDocument doc;
	if (!doc.LoadFile(_P(LINK_FILE)))
		return;

	TiXmlElement* root = doc.RootElement();
	if (!root || strcmp(root->Value(), "mediariffle") != 0)
		return;

	{
		CSingleLock lock(m_section);
		for (TiXmlElement* link = root->FirstChildElement("link"); link; link = link->NextSiblingElement("link"))
		{
			CStdString server;
			Account account;
			XMLUtils::GetString(link, "server", server);
			XMLUtils::GetString(link, "token", account.token);
			XMLUtils::GetString(link, "user", account.user);
			server.ToLower();
			if (!server.IsEmpty() && !account.token.IsEmpty())
				m_links[server] = account;
		}
	}

	// The first version's file had the one server, whose source was mediariffle://: the link is kept, and the source
	// moved to that server
	CStdString server, token, user;
	XMLUtils::GetString(root, "server", server);
	XMLUtils::GetString(root, "token", token);
	XMLUtils::GetString(root, "user", user);
	server.ToLower();
	if (server.IsEmpty() || token.IsEmpty())
		return;

	{
		CSingleLock lock(m_section);
		m_links[server].token = token;
		m_links[server].user = user;
	}
	SaveLinks();

	CStdString source;
	if (server.Left(8).Equals("https://"))
		source = "mediariffles://" + server.Mid(8) + "/";
	else if (server.Left(7).Equals("http://"))
		source = "mediariffle://" + server.Mid(7) + "/";

	VECSOURCES* sources = g_settings.GetSourcesFromType("video");
	for (unsigned int i = 0; sources && !source.IsEmpty() && i < sources->size(); i++)
	{
		if (!(*sources)[i].strPath.Equals("mediariffle://"))
			continue;

		CMediaSource moved = (*sources)[i];
		moved.strPath = source;
		moved.vecPaths.clear();
		moved.vecPaths.push_back(source);
		g_settings.UpdateShare("video", moved.strName, moved);
		CLog::Log(LOGNOTICE, "MediaRiffle: the %s source is now %s", moved.strName.c_str(), source.c_str());
		break;
	}
}

void CMediaRiffle::SaveLinks()
{
	TiXmlDocument doc;
	TiXmlElement element("mediariffle");
	TiXmlNode* root = doc.InsertEndChild(element);
	if (!root)
		return;

	{
		CSingleLock lock(m_section);
		for (std::map<CStdString, Account>::const_iterator it = m_links.begin(); it != m_links.end(); ++it)
		{
			TiXmlElement linkElement("link");
			TiXmlNode* link = root->InsertEndChild(linkElement);
			if (!link)
				continue;
			XMLUtils::SetString(link, "server", it->first);
			XMLUtils::SetString(link, "token", it->second.token);
			XMLUtils::SetString(link, "user", it->second.user);
		}
	}
	if (!doc.SaveFile(_P(LINK_FILE)))
		CLog::Log(LOGERROR, "MediaRiffle: couldn't save %s", LINK_FILE);
}

// What the server calls a path, to hide it: a show (show/<id>/), a movie (movie/<id>) or an episode (play/<id>, a
// "file"). A season can't be hidden on its own.
static bool HiddenKindOf(const CStdString& path, CStdString& kind, int& id)
{
	if (CMediaRiffle::ServerOf(path).IsEmpty())
		return false;

	CStdString rest = CURL(path).GetFileName();
	rest.TrimRight("/");

	CStdStringArray parts;
	CStringUtils::SplitString(rest, "/", parts);
	if (parts.size() != 2 || (id = atoi(parts[1].c_str())) <= 0)
		return false;

	if (parts[0].Equals("show"))
		kind = "show";
	else if (parts[0].Equals("movie"))
		kind = "movie";
	else if (parts[0].Equals("play"))
		kind = "file";
	else
		return false;

	return true;
}

bool CMediaRiffle::CanHide(const CStdString& path)
{
	CStdString kind;
	int id;
	return HiddenKindOf(path, kind, id);
}

bool CMediaRiffle::SetHidden(const CStdString& path, bool hidden)
{
	CStdString kind;
	int id;
	if (!HiddenKindOf(path, kind, id))
		return false;

	CStdString server = ServerOf(path);
	CStdString api;
	api.Format("/api/items/%s/%d/hidden", kind.c_str(), id);

	CVariant body;
	body["hidden"] = hidden;

	long status = 0;
	CVariant answer;
	if (!Call(server, "PUT", api, &body, status, answer))
	{
		ShowUnreachable(server);
		return false;
	}
	if (status != 200)
	{
		CLog::Log(LOGERROR, "MediaRiffle: %s%s: %ld", server.c_str(), api.c_str(), status);
		ShowError(g_localizeStrings.Get(33535), ErrorOf(answer, status));
		return false;
	}

	CLog::Log(LOGNOTICE, "MediaRiffle: %s %s %d", hidden ? "hid" : "showed again", kind.c_str(), id);
	return true;
}

void CMediaRiffle::ShowError(const CStdString& line1, const CStdString& line2, const CStdString& line3)
{
	CGUIDialogOK* ok = (CGUIDialogOK*)g_windowManager.GetWindow(WINDOW_DIALOG_OK);
	if (!ok)
		return;

	ok->SetHeading(33500);
	ok->SetLine(0, line1);
	ok->SetLine(1, line2);
	ok->SetLine(2, line3);
	ok->DoModal();
}

void CMediaRiffle::ShowUnreachable(const CStdString& server)
{
	CStdString reason;
	{
		CSingleLock lock(m_section);
		reason = m_lastError;
	}
	ShowError(g_localizeStrings.Get(33510), server, reason);
}

// What the server said went wrong ({ "error": "…" }), or what its status means
CStdString CMediaRiffle::ErrorOf(const CVariant& answer, long status)
{
	if (answer.isObject() && answer.isMember("error"))
		return answer["error"].asString();

	if (status == 401)
		return g_localizeStrings.Get(33517);

	CStdString error;
	error.Format(g_localizeStrings.Get(33511).c_str(), status);
	return error;
}
