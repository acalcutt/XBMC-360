#include <xtl.h>

#include "Jellyfin.h"
#include "HttpRequest.h"
#include "Application.h"
#include "CompileInfo.h"
#include "FileItem.h"
#include "PlayListPlayer.h"
#include "URL.h"
#include "filesystem\SpecialProtocol.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\XMLUtils.h"
#include "guilib\dialogs\GUIDialogKeyboard.h"
#include "guilib\dialogs\GUIDialogOK.h"
#include "guilib\dialogs\GUIDialogProgress.h"
#include "guilib\tinyxml\tinyxml.h"
#include "utils\JSONVariantParser.h"
#include "utils\Log.h"
#include "utils\SingleLock.h"
#include "utils\StringUtils.h"
#include "video\VideoInfoTag.h"

CJellyfin g_jellyfin;

#define ACCOUNTS_FILE   "special://profile/jellyfin.xml"
// How long a Quick Connect code is shown for, and how often the server is asked whether it's been entered
#define QUICK_CONNECT_FOR   300000
#define QUICK_CONNECT_EVERY 3000
// The most a stream is asked to be, in bits a second: plenty for 720p H.264 on the console's 100 Mb/s
#define MAX_BITRATE         8000000
// How often the server is told where a video has got to
#define REPORT_EVERY        10000

CJellyfin::CJellyfin()
{
}

CJellyfin::~CJellyfin()
{
}

void CJellyfin::Start()
{
	LoadAccounts();

	// The console's own device ID, made once: Jellyfin keeps one sign-in for each device
	if (m_deviceId.IsEmpty())
	{
		m_deviceId.Format("360riffle-%04x%04x%08x", rand() & 0xFFFF, rand() & 0xFFFF, GetTickCount());
		SaveAccounts();
	}
	Create();

	CSingleLock lock(m_section);
	for (std::map<CStdString, Account>::const_iterator it = m_accounts.begin(); it != m_accounts.end(); ++it)
		CLog::Log(LOGNOTICE, "Jellyfin: signed in to %s as %s", it->first.c_str(), it->second.user.c_str());
}

void CJellyfin::Stop()
{
	m_bStop = true;
	m_wake.Set();
	StopThread();
}

bool CJellyfin::IsJellyfin(const CStdString& path)
{
	return path.Left(11).Equals("jellyfin://") || path.Left(12).Equals("jellyfins://");
}

// jellyfin://media01:8096/series/3/ is on http://media01:8096; jellyfins:// is HTTPS, and so are ports 443 and 8920
// (Jellyfin's own HTTPS port) whichever it says
CStdString CJellyfin::ServerOf(const CStdString& path)
{
	CURL url(path);
	if (!IsJellyfin(path) || url.GetHostName().IsEmpty())
		return "";

	bool secure = url.GetProtocol().Equals("jellyfins") || (url.HasPort() && (url.GetPort() == 443 || url.GetPort() == 8920));
	CStdString server;
	server.Format("%s://%s", secure ? "https" : "http", url.GetHostName().c_str());
	if (url.HasPort())
		server.AppendFormat(":%d", url.GetPort());
	server.ToLower();
	return server;
}

CStdString CJellyfin::SourceOf(const CStdString& path)
{
	CURL url(path);
	if (!IsJellyfin(path) || url.GetHostName().IsEmpty())
		return "";

	CStdString source;
	source.Format("%s://%s", url.GetProtocol().c_str(), url.GetHostName().c_str());
	if (url.HasPort())
		source.AppendFormat(":%d", url.GetPort());
	source.ToLower();
	return source + "/";
}

bool CJellyfin::IsSignedIn(const CStdString& server) const
{
	return !TokenFor(server).IsEmpty();
}

CStdString CJellyfin::GetUser(const CStdString& server) const
{
	CSingleLock lock(m_section);
	std::map<CStdString, Account>::const_iterator it = m_accounts.find(server);
	return it == m_accounts.end() ? "" : it->second.user;
}

CStdString CJellyfin::GetUserId(const CStdString& server) const
{
	CSingleLock lock(m_section);
	std::map<CStdString, Account>::const_iterator it = m_accounts.find(server);
	return it == m_accounts.end() ? "" : it->second.userId;
}

CStdString CJellyfin::Describe() const
{
	CSingleLock lock(m_section);
	CStdString lines;
	for (std::map<CStdString, Account>::const_iterator it = m_accounts.begin(); it != m_accounts.end(); ++it)
		lines += it->first + ": " + it->second.user + "\r\n";
	return lines.IsEmpty() ? CStdString("Not signed in to a server\r\n") : lines;
}

CStdString CJellyfin::TokenFor(const CStdString& server) const
{
	CSingleLock lock(m_section);
	std::map<CStdString, Account>::const_iterator it = m_accounts.find(server);
	return it == m_accounts.end() ? "" : it->second.token;
}

void CJellyfin::Forget(const CStdString& server)
{
	{
		CSingleLock lock(m_section);
		m_accounts.erase(server);
	}
	SaveAccounts();
}

bool CJellyfin::SignIn(const CStdString& server)
{
	if (server.IsEmpty())
		return false;

	if (!g_application.getNetwork().IsAvailable())
	{
		ShowError(g_localizeStrings.Get(33543));
		return false;
	}

	CVariant signedIn;
	int quick = QuickConnect(server, signedIn);
	if (quick == 0)
		return false;

	// Quick Connect is off there, or the server hasn't got it (Emby, an old Jellyfin): a name and password instead
	if (quick < 0)
	{
		ShowError(g_localizeStrings.Get(33546));
		if (!SignInWithPassword(server, signedIn))
			return false;
	}

	if (!KeepSignIn(server, signedIn))
	{
		ShowError(g_localizeStrings.Get(33557));
		return false;
	}

	CStdString as;
	as.Format(g_localizeStrings.Get(33549).c_str(), GetUser(server).c_str());
	ShowError(as, server);
	return true;
}

void CJellyfin::SignOut(const CStdString& server)
{
	// Best effort: the server may be out of reach, and the token is dropped here anyway
	if (IsSignedIn(server))
		Queue(server, "POST", "/Sessions/Logout", NULL);

	Forget(server);
	CLog::Log(LOGNOTICE, "Jellyfin: signed out of %s", server.c_str());
}

// Quick Connect: a code to enter in Jellyfin where someone's signed in, shown until it's entered or runs out. 1 once
// signed in, 0 when not (cancelled, ran out, unreachable), -1 when the server doesn't do it.
int CJellyfin::QuickConnect(const CStdString& server, CVariant& signedIn)
{
	long status = 0;
	CVariant answer;
	if (!CallServer(server, "", "POST", "/QuickConnect/Initiate", NULL, status, answer, 15))
	{
		ShowUnreachable(server);
		return 0;
	}
	if (status != 200 || answer["Secret"].asString().empty())
		return -1;

	CStdString secret = answer["Secret"].asString();
	CStdString code = answer["Code"].asString();

	CGUIDialogProgress* dialog = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
	if (!dialog)
		return 0;

	CStdString enter;
	enter.Format(g_localizeStrings.Get(33513).c_str(), code.c_str());
	dialog->SetHeading(33540);
	dialog->SetLine(0, g_localizeStrings.Get(33544));
	dialog->SetLine(1, g_localizeStrings.Get(33545));
	dialog->SetLine(2, enter);
	dialog->SetCanCancel(true);
	dialog->ShowProgressBar(false);
	dialog->StartModal();

	DWORD started = GetTickCount();
	DWORD next = started + QUICK_CONNECT_EVERY;
	CStdString failure;
	int result = 0;

	while (!dialog->IsCanceled())
	{
		dialog->Progress();
		::Sleep(50);

		DWORD now = GetTickCount();
		if (now - started > QUICK_CONNECT_FOR)
		{
			failure = g_localizeStrings.Get(33514);
			break;
		}
		if ((long)(now - next) < 0)
			continue;
		next = now + QUICK_CONNECT_EVERY;

		if (!CallServer(server, "", "GET", "/QuickConnect/Connect?secret=" + secret, NULL, status, answer, 15))
			continue;   // a moment the server can't be reached: ask again next time

		// The server forgot the code: it ran out there
		if (status != 200)
		{
			failure = status == 404 ? g_localizeStrings.Get(33514) : ErrorOf(answer, status);
			break;
		}
		if (!answer["Authenticated"].asBoolean())
			continue;   // not entered yet

		CVariant body(CVariant::VariantTypeObject);
		body["Secret"] = secret.c_str();
		if (CallServer(server, "", "POST", "/Users/AuthenticateWithQuickConnect", &body, status, answer, 15) && status == 200)
		{
			signedIn = answer;
			result = 1;
		}
		else
			failure = ErrorOf(answer, status);
		break;
	}
	dialog->Close();

	if (result == 0 && !failure.IsEmpty())
		ShowError(failure);
	return result;
}

// A user name and password, typed in
bool CJellyfin::SignInWithPassword(const CStdString& server, CVariant& signedIn)
{
	CStdString user, password;
	if (!CGUIDialogKeyboard::ShowAndGetInput(user, g_localizeStrings.Get(33547), false))
		return false;
	// Jellyfin lets an account have no password
	if (!CGUIDialogKeyboard::ShowAndGetInput(password, g_localizeStrings.Get(33548), true, true))
		return false;

	CVariant body(CVariant::VariantTypeObject);
	body["Username"] = user.c_str();
	body["Pw"] = password.c_str();

	long status = 0;
	CVariant answer;
	if (!CallServer(server, "", "POST", "/Users/AuthenticateByName", &body, status, answer, 20))
	{
		ShowUnreachable(server);
		return false;
	}
	if (status != 200)
	{
		ShowError(status == 401 ? g_localizeStrings.Get(33556) : ErrorOf(answer, status));
		return false;
	}

	signedIn = answer;
	return true;
}

// What signing in answers: { User: { Name, Id }, AccessToken }
bool CJellyfin::KeepSignIn(const CStdString& server, const CVariant& signedIn)
{
	CStdString token = signedIn["AccessToken"].asString();
	CStdString userId = signedIn["User"]["Id"].asString();
	if (token.IsEmpty() || userId.IsEmpty())
		return false;

	{
		CSingleLock lock(m_section);
		Account& account = m_accounts[server];
		account.token = token;
		account.userId = userId;
		account.user = signedIn["User"]["Name"].asString();
	}
	SaveAccounts();

	CLog::Log(LOGNOTICE, "Jellyfin: signed in to %s as %s", server.c_str(), GetUser(server).c_str());
	return true;
}

CStdString CJellyfin::AuthHeader(const CStdString& token) const
{
	CStdString header;
	header.Format("MediaBrowser Client=\"%s\", Device=\"Xbox 360\", DeviceId=\"%s\", Version=\"%s\"",
		APP_NAME, m_deviceId.c_str(), APP_VERSION);
	if (!token.IsEmpty())
		header.AppendFormat(", Token=\"%s\"", token.c_str());
	return header;
}

bool CJellyfin::Call(const CStdString& server, const CStdString& method, const CStdString& path, const CVariant* body,
	long& status, CVariant& answer, int timeout)
{
	CStdString token = TokenFor(server);
	if (token.IsEmpty())
		return false;

	if (!CallServer(server, token, method, path, body, status, answer, timeout))
		return false;

	// Signed out on the server (the device removed from its dashboard, or the password changed): sign in again
	if (status == 401)
	{
		CLog::Log(LOGWARNING, "Jellyfin: %s no longer takes this console's token", server.c_str());
		Forget(server);
	}
	return true;
}

bool CJellyfin::CallServer(const CStdString& server, const CStdString& token, const CStdString& method, const CStdString& path,
	const CVariant* body, long& status, CVariant& answer, int timeout)
{
	std::string json;
	if (body)
		json = CJSONVariantWriter::Write(*body);

	// Jellyfin reads Authorization; Emby, X-Emby-Authorization
	CStdString auth = AuthHeader(token);
	std::vector<CStdString> headers;
	headers.push_back("Authorization: " + auth);
	headers.push_back("X-Emby-Authorization: " + auth);

	CStdString url = server + path;
	std::string response;
	CStdString reason;
	if (!CHttpRequest::Send(url, headers, method, body ? &json : NULL, status, response, timeout, reason))
	{
		CLog::Log(LOGERROR, "Jellyfin: %s %s failed: %s", method.c_str(), url.c_str(), reason.c_str());
		CSingleLock lock(m_section);
		m_lastError = reason;
		return false;
	}
	if (status >= 400)
		CLog::Log(LOGWARNING, "Jellyfin: %s %s: %ld %s", method.c_str(), url.c_str(), status, response.substr(0, 200).c_str());

	answer = response.empty() ? CVariant() : CJSONVariantParser::Parse(response);
	return true;
}

// What the console can play, for the server to make its stream to: nothing as it is (every file is transcoded, as
// MediaRiffle's are), and HLS in MPEG-TS of H.264 at up to 720p and level 4.1 with stereo AAC, MP3 or AC-3
static CVariant DeviceProfile()
{
	CVariant hls(CVariant::VariantTypeObject);
	hls["Container"] = "ts";
	hls["Type"] = "Video";
	hls["VideoCodec"] = "h264";
	hls["AudioCodec"] = "aac,mp3,ac3";
	hls["Protocol"] = "hls";
	hls["Context"] = "Streaming";
	hls["MaxAudioChannels"] = "2";
	hls["BreakOnNonKeyFrames"] = true;

	const char* limits[][2] = { { "Width", "1280" }, { "Height", "720" }, { "VideoLevel", "41" } };
	CVariant conditions(CVariant::VariantTypeArray);
	for (unsigned int i = 0; i < sizeof(limits) / sizeof(limits[0]); i++)
	{
		CVariant condition(CVariant::VariantTypeObject);
		condition["Condition"] = "LessThanEqual";
		condition["Property"] = limits[i][0];
		condition["Value"] = limits[i][1];
		conditions.push_back(condition);
	}
	CVariant h264(CVariant::VariantTypeObject);
	h264["Type"] = "Video";
	h264["Codec"] = "h264";
	h264["Conditions"] = conditions;

	CVariant profile(CVariant::VariantTypeObject);
	profile["Name"] = APP_NAME;
	profile["MaxStreamingBitrate"] = MAX_BITRATE;
	profile["DirectPlayProfiles"] = CVariant(CVariant::VariantTypeArray);
	profile["TranscodingProfiles"] = CVariant(CVariant::VariantTypeArray);
	profile["TranscodingProfiles"].push_back(hls);
	profile["CodecProfiles"] = CVariant(CVariant::VariantTypeArray);
	profile["CodecProfiles"].push_back(h264);
	profile["SubtitleProfiles"] = CVariant(CVariant::VariantTypeArray);
	return profile;
}

bool CJellyfin::StartPlayback(const CFileItem& item, CFileItem& stream)
{
	CStdString server = ServerOf(item.GetPath());
	CStdStringArray parts;
	CStdString rest = CURL(item.GetPath()).GetFileName();
	rest.TrimRight("/");
	CStringUtils::SplitString(rest, "/", parts);
	if (server.IsEmpty() || parts.size() < 2 || !parts[0].Equals("play"))
		return false;

	if (!IsSignedIn(server) && !SignIn(server))
		return false;

	CStdString id = parts[1];
	CStdString userId = GetUserId(server);

	// Where it was stopped (on 10.9 and later /Items/{id}, before that under the user)
	long status = 0;
	CVariant details;
	if (Call(server, "GET", "/Items/" + id + "?userId=" + userId, NULL, status, details) && status == 404)
		Call(server, "GET", "/Users/" + userId + "/Items/" + id, NULL, status, details);
	double resume = status == 200 ? details["UserData"]["PlaybackPositionTicks"].asDouble() / 10000000.0 : 0;

	CVariant info(CVariant::VariantTypeObject);
	info["DeviceProfile"] = DeviceProfile();
	CStdString query;
	query.Format("/Items/%s/PlaybackInfo?userId=%s&isPlayback=true&autoOpenLiveStream=true&maxStreamingBitrate=%d",
		id.c_str(), userId.c_str(), MAX_BITRATE);

	CVariant answer;
	if (!Call(server, "POST", query, &info, status, answer, 60))
	{
		ShowUnreachable(server);
		return false;
	}
	if (status != 200 || answer["MediaSources"].size() == 0)
	{
		ShowError(ErrorOf(answer, status));
		return false;
	}

	// The stream's URL carries a key of its own, so the player needs no token for it (and it's kept out of the log)
	CStdString url = answer["MediaSources"][0u]["TranscodingUrl"].asString();
	if (url.IsEmpty())
	{
		ShowError(g_localizeStrings.Get(33558));
		return false;
	}

	stream = item;
	stream.SetPath(server + url);
	stream.SetMimeType("application/vnd.apple.mpegurl");
	stream.m_lStartOffset = (long)(resume * 75.0);

	// Something else of Jellyfin's was playing: it's done (the player switches without saying it stopped)
	OnStopped(false);

	{
		CSingleLock lock(m_section);
		m_playing = Playing();
		m_playing.server = server;
		m_playing.itemId = id;
		m_playing.mediaSourceId = answer["MediaSources"][0u]["Id"].asString();
		m_playing.playSessionId = answer["PlaySessionId"].asString();
		m_playing.stream = stream.GetPath();
		m_playing.seconds = resume;
		m_playing.lastReport = GetTickCount();

		CVariant started = SessionReport(false);
		Queue(server, "POST", "/Sessions/Playing", &started);
	}

	CLog::Log(LOGNOTICE, "Jellyfin: playing %s from %.0f s", id.c_str(), resume);
	return true;
}

bool CJellyfin::IsQueue(const CStdString& path)
{
	if (ServerOf(path).IsEmpty())
		return false;

	CStdString rest = CURL(path).GetFileName();
	return rest.Left(6).Equals("queue/") || rest.Left(8).Equals("shuffle/");
}

bool CJellyfin::PlayQueue(const CFileItem& item)
{
	CStdString server = ServerOf(item.GetPath());
	CStdString source = SourceOf(item.GetPath());
	CStdStringArray parts;
	CStdString rest = CURL(item.GetPath()).GetFileName();
	rest.TrimRight("/");
	CStringUtils::SplitString(rest, "/", parts);
	if (server.IsEmpty() || parts.size() < 2)
		return false;

	if (!IsSignedIn(server) && !SignIn(server))
		return false;

	// In episode order (movies have none, so they go by name), or in the server's random order; enough for an evening
	bool shuffle = parts[0].Equals("shuffle");
	CStdString userId = GetUserId(server);
	CStdString query;
	query.Format("parentId=%s&recursive=true&includeItemTypes=Movie,Episode,Video,MusicVideo&sortBy=%s&limit=500",
		parts[1].c_str(), shuffle ? "Random" : "ParentIndexNumber,IndexNumber,SortName");

	long status = 0;
	CVariant answer;
	if (!Call(server, "GET", "/Items?userId=" + userId + "&" + query, NULL, status, answer, 30))
	{
		ShowUnreachable(server);
		return false;
	}
	if (status == 404)
		Call(server, "GET", "/Users/" + userId + "/Items?" + query, NULL, status, answer, 30);
	if (status != 200)
	{
		ShowError(ErrorOf(answer, status));
		return false;
	}

	CFileItemList videos;
	const CVariant& list = answer["Items"];
	for (unsigned int i = 0; i < list.size(); i++)
	{
		const CVariant& entry = list[i];
		CStdString title = entry["Name"].asString();
		if (!entry["SeriesName"].isNull())
			title = CStdString(entry["SeriesName"].asString()) + " - " + title;

		CFileItemPtr video(new CFileItem(title));
		video->SetPath(source + "play/" + CStdString(entry["Id"].asString()));
		video->SetMimeType("video/mp2t");
		video->SetLabelPreformated(true);
		video->GetVideoInfoTag()->m_strTitle = title;
		videos.Add(video);
	}
	if (videos.IsEmpty())
		return false;

	CLog::Log(LOGNOTICE, "Jellyfin: playing %d videos in a row from %s%s", videos.Size(), parts[1].c_str(), shuffle ? ", shuffled" : "");
	g_playlistPlayer.ClearPlaylist(PLAYLIST_VIDEO);
	g_playlistPlayer.Reset();
	g_playlistPlayer.Add(PLAYLIST_VIDEO, videos);
	g_playlistPlayer.SetCurrentPlaylist(PLAYLIST_VIDEO);
	g_playlistPlayer.Play();
	return true;
}

// What Jellyfin's session reports carry: the item, its stream (source and session), and where it's got to in ticks
CVariant CJellyfin::SessionReport(bool paused) const
{
	CVariant report(CVariant::VariantTypeObject);
	report["ItemId"] = m_playing.itemId.c_str();
	report["MediaSourceId"] = m_playing.mediaSourceId.c_str();
	report["PlaySessionId"] = m_playing.playSessionId.c_str();
	report["PositionTicks"] = (int64_t)(m_playing.seconds * 10000000.0);
	report["IsPaused"] = paused;
	report["PlayMethod"] = "Transcode";
	report["CanSeek"] = true;
	return report;
}

void CJellyfin::OnPlaying(const CStdString& currentFile, double seconds, bool paused)
{
	CSingleLock lock(m_section);
	if (m_playing.itemId.IsEmpty() || !currentFile.Equals(m_playing.stream))
		return;

	m_playing.seconds = seconds;

	// Every few seconds, paused too: the server keeps a session that's reported, and its transcode with it
	DWORD now = GetTickCount();
	if (now - m_playing.lastReport >= REPORT_EVERY)
	{
		m_playing.lastReport = now;
		CVariant progress = SessionReport(paused);
		Queue(m_playing.server, "POST", "/Sessions/Playing/Progress", &progress);
	}
}

void CJellyfin::OnStopped(bool ended)
{
	CSingleLock lock(m_section);
	if (m_playing.itemId.IsEmpty())
		return;

	// Where it stopped; the server marks it watched when that's near enough the end
	CVariant stopped = SessionReport(false);
	Queue(m_playing.server, "POST", "/Sessions/Playing/Stopped", &stopped);

	// And the transcode for it can stop
	CStdString encodings;
	encodings.Format("/Videos/ActiveEncodings?deviceId=%s&playSessionId=%s", m_deviceId.c_str(), m_playing.playSessionId.c_str());
	Queue(m_playing.server, "DELETE", encodings, NULL);

	CLog::Log(LOGNOTICE, "Jellyfin: %s %s at %.0f s", ended ? "finished" : "stopped", m_playing.itemId.c_str(), m_playing.seconds);
	m_playing = Playing();
}

// Sent with the sign-in as it is now (a sign-out's request still carries the token it's dropping)
void CJellyfin::Queue(const CStdString& server, const CStdString& method, const CStdString& path, const CVariant* body)
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

void CJellyfin::Process()
{
	SetName("Jellyfin");
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

void CJellyfin::ShowError(const CStdString& line1, const CStdString& line2, const CStdString& line3)
{
	CGUIDialogOK* ok = (CGUIDialogOK*)g_windowManager.GetWindow(WINDOW_DIALOG_OK);
	if (!ok)
		return;

	ok->SetHeading(33540);
	ok->SetLine(0, line1);
	ok->SetLine(1, line2);
	ok->SetLine(2, line3);
	ok->DoModal();
}

void CJellyfin::ShowUnreachable(const CStdString& server)
{
	CStdString reason;
	{
		CSingleLock lock(m_section);
		reason = m_lastError;
	}
	ShowError(g_localizeStrings.Get(33543), server, reason);
}

// What the server said went wrong (a problem's title), or what its status means
CStdString CJellyfin::ErrorOf(const CVariant& answer, long status)
{
	if (answer.isObject() && answer.isMember("title"))
		return answer["title"].asString();

	if (status == 401)
		return g_localizeStrings.Get(33554);

	CStdString error;
	error.Format(g_localizeStrings.Get(33511).c_str(), status);
	return error;
}

// <jellyfin><deviceid/><account><server/><token/><user/><userid/></account>…</jellyfin>, an account for each server
void CJellyfin::LoadAccounts()
{
	TiXmlDocument doc;
	if (!doc.LoadFile(_P(ACCOUNTS_FILE)))
		return;

	TiXmlElement* root = doc.RootElement();
	if (!root || strcmp(root->Value(), "jellyfin") != 0)
		return;

	CSingleLock lock(m_section);
	XMLUtils::GetString(root, "deviceid", m_deviceId);
	for (TiXmlElement* element = root->FirstChildElement("account"); element; element = element->NextSiblingElement("account"))
	{
		CStdString server;
		Account account;
		XMLUtils::GetString(element, "server", server);
		XMLUtils::GetString(element, "token", account.token);
		XMLUtils::GetString(element, "user", account.user);
		XMLUtils::GetString(element, "userid", account.userId);
		server.ToLower();
		if (!server.IsEmpty() && !account.token.IsEmpty() && !account.userId.IsEmpty())
			m_accounts[server] = account;
	}
}

void CJellyfin::SaveAccounts()
{
	TiXmlDocument doc;
	TiXmlElement element("jellyfin");
	TiXmlNode* root = doc.InsertEndChild(element);
	if (!root)
		return;

	{
		CSingleLock lock(m_section);
		XMLUtils::SetString(root, "deviceid", m_deviceId);
		for (std::map<CStdString, Account>::const_iterator it = m_accounts.begin(); it != m_accounts.end(); ++it)
		{
			TiXmlElement accountElement("account");
			TiXmlNode* account = root->InsertEndChild(accountElement);
			if (!account)
				continue;
			XMLUtils::SetString(account, "server", it->first);
			XMLUtils::SetString(account, "token", it->second.token);
			XMLUtils::SetString(account, "user", it->second.user);
			XMLUtils::SetString(account, "userid", it->second.userId);
		}
	}
	if (!doc.SaveFile(_P(ACCOUNTS_FILE)))
		CLog::Log(LOGERROR, "Jellyfin: couldn't save %s", ACCOUNTS_FILE);
}
