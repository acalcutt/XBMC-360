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
#include "GUISettings.h"
#include "MediaSource.h"
#include "Settings.h"
#include "URL.h"
#include "filesystem\SpecialProtocol.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\Key.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\XMLUtils.h"
#include "guilib\dialogs\GUIDialogKeyboard.h"
#include "guilib\dialogs\GUIDialogOK.h"
#include "guilib\dialogs\GUIDialogProgress.h"
#include "guilib\tinyxml\tinyxml.h"
#include "utils\JSONVariantParser.h"
#include "utils\Log.h"
#include "utils\SingleLock.h"
#include "utils\SystemInfo.h"

CMediaRiffle g_mediaRiffle;

#define LINK_FILE      "special://profile/mediariffle.xml"
#define SOURCE_PATH    "mediariffle://"
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

	LoadLink();
	Create();
	CLog::Log(LOGNOTICE, "MediaRiffle: %s", IsLinked() ? ("linked to " + GetServer() + " as " + GetUser()).c_str() : "not linked");
}

void CMediaRiffle::Stop()
{
	m_bStop = true;
	m_wake.Set();
	StopThread();
	curl_global_cleanup();
}

CStdString CMediaRiffle::GetServer() const
{
	return NormalizeServer(g_guiSettings.GetString("mediariffle.server"));
}

// http:// when no scheme is given, and no trailing slash: "media01:5080/" is "http://media01:5080"
CStdString CMediaRiffle::NormalizeServer(const CStdString& typed)
{
	CStdString server(typed);
	server.Trim();
	if (server.IsEmpty() || server.Equals("http://") || server.Equals("https://"))
		return "";

	if (server.Find("://") < 0)
		server = "http://" + server;

	while (server.Right(1) == "/")
		server = server.Left(server.GetLength() - 1);

	return server;
}

// Linked, to the server set now (a token is only good on the server that gave it)
bool CMediaRiffle::IsLinked() const
{
	CSingleLock lock(m_section);
	return !m_token.IsEmpty() && m_linkServer.Equals(GetServer());
}

CStdString CMediaRiffle::GetUser() const
{
	CSingleLock lock(m_section);
	return IsLinked() ? m_user : "";
}

bool CMediaRiffle::Link()
{
	CStdString server = GetServer();
	if (server.IsEmpty())
	{
		CStdString typed = "http://";
		if (!CGUIDialogKeyboard::ShowAndGetInput(typed, g_localizeStrings.Get(33501), false))
			return false;

		server = NormalizeServer(typed);
		if (server.IsEmpty())
			return false;

		g_guiSettings.SetString("mediariffle.server", server);
		g_settings.Save();
	}

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
			m_linkServer = server;
			m_token = answer["token"].asString();
			m_user = answer["user"].asString();
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

	SaveLink();
	AddSource();
	CLog::Log(LOGNOTICE, "MediaRiffle: linked to %s as %s", server.c_str(), GetUser().c_str());

	CStdString as;
	as.Format(g_localizeStrings.Get(33515).c_str(), GetUser().c_str());
	CGUIDialogOK* ok = (CGUIDialogOK*)g_windowManager.GetWindow(WINDOW_DIALOG_OK);
	if (ok)
	{
		ok->SetHeading(33500);
		ok->SetLine(0, as);
		ok->SetLine(1, g_localizeStrings.Get(33516));
		ok->SetLine(2, "");
		ok->DoModal();
	}
	return true;
}

void CMediaRiffle::Unlink()
{
	// The server forgets the device (best effort: it may be unreachable, and the token is dropped here anyway)
	if (IsLinked())
		Queue("DELETE", "/api/device", NULL);

	{
		CSingleLock lock(m_section);
		m_token.Empty();
		m_user.Empty();
		m_linkServer.Empty();
	}
	SaveLink();
	CLog::Log(LOGNOTICE, "MediaRiffle: unlinked");
}

bool CMediaRiffle::Call(const CStdString& method, const CStdString& path, const CVariant* body, long& status, CVariant& answer, int timeout)
{
	CStdString server, token;
	{
		CSingleLock lock(m_section);
		server = m_linkServer;
		token = m_token;
	}
	if (!IsLinked())
		return false;

	if (!CallServer(server, token, method, path, body, status, answer, timeout))
		return false;

	// Unlinked on the server (from its Profile page, or a password change): link again
	if (status == 401)
	{
		CLog::Log(LOGWARNING, "MediaRiffle: the server no longer knows this console's token");
		{
			CSingleLock lock(m_section);
			m_token.Empty();
			m_user.Empty();
		}
		SaveLink();
	}
	return true;
}

bool CMediaRiffle::Fetch(const CStdString& path, std::string& data, long& status)
{
	CStdString server, token;
	{
		CSingleLock lock(m_section);
		server = m_linkServer;
		token = m_token;
	}
	if (!IsLinked())
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

bool CMediaRiffle::IsMediaRiffle(const CStdString& path)
{
	return path.Left(14).Equals(SOURCE_PATH);
}

// mediariffle://play/<file> (or mediariffle://movie/<movie>, its first part): a stream of it, from where it stopped
bool CMediaRiffle::StartPlayback(const CFileItem& item, CFileItem& stream)
{
	CURL url(item.GetPath());
	CStdString kind = url.GetHostName();
	CStdString id = url.GetFileName();
	id.TrimRight("/");
	int fileId = atoi(id.c_str());

	if (!IsLinked() && !Link())
		return false;

	long status = 0;
	CVariant answer;
	if (kind.Equals("movie"))
	{
		if (!Call("GET", "/api/movies/" + id, NULL, status, answer) || status != 200 || answer["parts"].size() == 0)
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
	if (!Call("POST", "/api/play", &play, status, answer, 90))
	{
		ShowUnreachable(GetServer());
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
	stream.SetPath(GetServer() + CStdString(answer["url"].asString()));
	stream.SetMimeType(mode.Equals("Direct") ? CStdString(answer["contentType"].asString()) : "video/mp2t");
	// The whole file (adaptive, or sent as it is) starts where it stopped by seeking; a remuxed one starts there
	stream.m_lStartOffset = (long)(seekTo * 75.0);

	// Something else of MediaRiffle's was playing: it's done (the player switches without saying it stopped)
	OnStopped(false);

	{
		CSingleLock lock(m_section);
		m_playing = Playing();
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
		Queue("POST", "/api/progress", &progress);
	}

	// Paused: nothing is fetched, so the server is told it's still wanted
	if (paused && now - m_playing.lastAlive >= ALIVE_EVERY)
	{
		m_playing.lastAlive = now;
		Queue("GET", "/stream/" + m_playing.sessionId + "/alive", NULL);
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
	Queue("POST", "/api/progress", &progress);
	Queue("DELETE", "/api/play/" + m_playing.sessionId, NULL);

	m_playing = Playing();
}

// Sent with the link as it is now (an unlink's DELETE still carries the token it's dropping)
void CMediaRiffle::Queue(const CStdString& method, const CStdString& path, const CVariant* body)
{
	Report report;
	report.method = method;
	report.path = path;
	report.hasBody = body != NULL;
	if (body)
		report.body = *body;

	CSingleLock lock(m_section);
	if (m_token.IsEmpty())
		return;

	report.server = m_linkServer;
	report.token = m_token;
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

void CMediaRiffle::LoadLink()
{
	TiXmlDocument doc;
	if (!doc.LoadFile(_P(LINK_FILE)))
		return;

	TiXmlElement* root = doc.RootElement();
	if (!root || strcmp(root->Value(), "mediariffle") != 0)
		return;

	CSingleLock lock(m_section);
	XMLUtils::GetString(root, "server", m_linkServer);
	XMLUtils::GetString(root, "token", m_token);
	XMLUtils::GetString(root, "user", m_user);
}

void CMediaRiffle::SaveLink()
{
	TiXmlDocument doc;
	TiXmlElement element("mediariffle");
	TiXmlNode* root = doc.InsertEndChild(element);
	if (!root)
		return;

	{
		CSingleLock lock(m_section);
		XMLUtils::SetString(root, "server", m_linkServer);
		XMLUtils::SetString(root, "token", m_token);
		XMLUtils::SetString(root, "user", m_user);
	}
	if (!doc.SaveFile(_P(LINK_FILE)))
		CLog::Log(LOGERROR, "MediaRiffle: couldn't save %s", LINK_FILE);
}

// Videos gets a MediaRiffle source, once
void CMediaRiffle::AddSource()
{
	VECSOURCES* sources = g_settings.GetSourcesFromType("video");
	if (!sources)
		return;

	for (IVECSOURCES it = sources->begin(); it != sources->end(); ++it)
	{
		if (it->strPath.Equals(SOURCE_PATH))
			return;
	}

	CMediaSource source;
	source.strName = "MediaRiffle";
	source.strPath = SOURCE_PATH;
	source.m_iDriveType = CMediaSource::SOURCE_TYPE_REMOTE;
	source.vecPaths.push_back(source.strPath);
	g_settings.AddShare("video", source);
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
