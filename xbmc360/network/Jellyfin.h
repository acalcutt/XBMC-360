#ifndef H_CJELLYFIN
#define H_CJELLYFIN

#include "utils\StdString.h"
#include "utils\Variant.h"
#include "utils\CriticalSection.h"
#include "utils\Event.h"
#include "utils\Thread.h"

#include <deque>
#include <map>

class CFileItem;

// Jellyfin servers (and Emby's, whose API Jellyfin kept): their video libraries as sources (jellyfin://host:port/, or
// jellyfins:// over HTTPS).
//
// A server is added like any network location (Add source > Add network location > Jellyfin server). The first time
// it's opened the console signs in: with Quick Connect, a code entered in Jellyfin where someone's signed in, when the
// server has it on; else with a user name and password. The access token it gets, the user, and this console's device
// ID are kept in special://profile/jellyfin.xml. Where each video got to is reported to the server as it plays, so
// Continue watching and Next up there know it, on any device.
class CJellyfin : public CThread
{
public:
	CJellyfin();
	virtual ~CJellyfin();

	// At startup and shutdown: the accounts, and the thread that sends the reports
	void Start();
	void Stop();

	// A Jellyfin path; its server ("http://host:port", lower case); and its source ("jellyfin://host:port/"), which
	// every path on that server starts with
	static bool IsJellyfin(const CStdString& path);
	static CStdString ServerOf(const CStdString& path);
	static CStdString SourceOf(const CStdString& path);

	// Signed in to a server; as whom (name, and the ID the API wants)
	bool IsSignedIn(const CStdString& server) const;
	CStdString GetUser(const CStdString& server) const;
	CStdString GetUserId(const CStdString& server) const;
	// The servers signed in to, and as whom: a line each, without the tokens (diagnostics)
	CStdString Describe() const;

	// Signs in with Quick Connect, or a name and password where that's off. On the GUI thread. True once signed in.
	bool SignIn(const CStdString& server);
	// Signs out: the server drops this console's token, and so does it
	void SignOut(const CStdString& server);

	// An API call with the server's token: path from the server's root ("/UserViews?userId=…"); body as JSON, or none.
	// False when the server couldn't be reached; else its status, and its answer when that's JSON.
	bool Call(const CStdString& server, const CStdString& method, const CStdString& path, const CVariant* body,
		long& status, CVariant& answer, int timeout = 20);

	// Playing: a jellyfin:// item (<source>play/<item>), and the stream it plays as, an HLS stream the server makes for
	// the console (H.264 at up to 720p), starting where it was stopped
	bool StartPlayback(const CFileItem& item, CFileItem& stream);
	// From the application while something plays (every frame), and when it stops or ends
	void OnPlaying(const CStdString& currentFile, double seconds, bool paused);
	void OnStopped(bool ended);

	// Errors, in a dialog: the server couldn't be reached (and libcurl's reason), or what it answered
	void ShowUnreachable(const CStdString& server);
	void ShowError(const CStdString& line1, const CStdString& line2 = "", const CStdString& line3 = "");
	static CStdString ErrorOf(const CVariant& answer, long status);

protected:
	virtual void Process();

private:
	// The Authorization header Jellyfin (and Emby) want: who's asking, and the token once there is one
	CStdString AuthHeader(const CStdString& token) const;
	// A report for the thread to send, with the token as it is now
	void Queue(const CStdString& server, const CStdString& method, const CStdString& path, const CVariant* body);
	// Where the video playing is, as a session report's body
	CVariant SessionReport(bool paused) const;
	bool CallServer(const CStdString& server, const CStdString& token, const CStdString& method, const CStdString& path,
		const CVariant* body, long& status, CVariant& answer, int timeout);
	int QuickConnect(const CStdString& server, CVariant& signedIn);
	bool SignInWithPassword(const CStdString& server, CVariant& signedIn);
	bool KeepSignIn(const CStdString& server, const CVariant& signedIn);
	CStdString TokenFor(const CStdString& server) const;
	void Forget(const CStdString& server);
	void LoadAccounts();
	void SaveAccounts();

	struct Account
	{
		CStdString token;
		CStdString user;
		CStdString userId;
	};

	// The video playing: its item and the stream made of it (its source and session), and where it's got to
	struct Playing
	{
		Playing() : seconds(0), lastReport(0) {}
		CStdString server;
		CStdString itemId;
		CStdString mediaSourceId;
		CStdString playSessionId;
		CStdString stream;
		double seconds;
		DWORD lastReport;
	};

	struct Report
	{
		Report() : hasBody(false) {}
		CStdString server;
		CStdString token;
		CStdString method;
		CStdString path;
		CVariant body;
		bool hasBody;
	};

	mutable CCriticalSection m_section;
	std::map<CStdString, Account> m_accounts;
	CStdString m_deviceId;
	// Why the last request that failed did ("Failed to connect to … port …: Connection refused")
	CStdString m_lastError;
	Playing m_playing;
	std::deque<Report> m_reports;
	CEvent m_wake;
};

extern CJellyfin g_jellyfin;

#endif //H_CJELLYFIN
