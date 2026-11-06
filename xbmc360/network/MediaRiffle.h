#ifndef H_CMEDIARIFFLE
#define H_CMEDIARIFFLE

#include "utils\StdString.h"
#include "utils\Variant.h"
#include "utils\CriticalSection.h"
#include "utils\Event.h"
#include "utils\Thread.h"

#include <deque>
#include <string>

class CFileItem;

// A MediaRiffle server: its video libraries in Videos (mediariffle://), played as the HLS streams it makes for the
// console (H.264 at 720p, transcoded on the server), and where each one stopped saved back to it, so it carries on
// from there on any device.
//
// The console is linked to an account once, with a short code someone enters at the server's /link in a browser, and
// from then on signs in with a token of its own (MediaRiffle's docs/devices.md). Linking and the server address are in
// Settings > Network > MediaRiffle; the token is kept apart, in special://profile/mediariffle.xml.
class CMediaRiffle : public CThread
{
public:
	CMediaRiffle();
	virtual ~CMediaRiffle();

	// At startup and shutdown: the link, and the thread that reports progress
	void Start();
	void Stop();

	// The server, as set in Settings (http://host:port, no trailing slash)
	CStdString GetServer() const;
	static CStdString NormalizeServer(const CStdString& typed);
	// Linked to an account on that server; the account's name
	bool IsLinked() const;
	CStdString GetUser() const;

	// Links this console: asks for the server if none is set, shows the code to enter at <server>/link, and waits for
	// it. On the GUI thread. True once linked (Videos then has a MediaRiffle source).
	bool Link();
	// Signs out: the server forgets this console, and so does it
	void Unlink();

	// An API call, with the token: path from the server's root ("/api/libraries"); body as JSON, or none. False when
	// the server couldn't be reached; else its status, and its answer when that's JSON.
	bool Call(const CStdString& method, const CStdString& path, const CVariant* body, long& status, CVariant& answer, int timeout = 20);
	// A GET with the token, as it comes (pictures)
	bool Fetch(const CStdString& path, std::string& data, long& status);

	// Playing: a mediariffle:// item to play, and the stream it plays as (POST /api/play)
	static bool IsMediaRiffle(const CStdString& path);
	bool StartPlayback(const CFileItem& item, CFileItem& stream);
	// From the application while something plays (every frame), and when it stops or ends
	void OnPlaying(const CStdString& currentFile, double seconds, bool paused);
	void OnStopped(bool ended);

protected:
	virtual void Process();

	bool Request(const CStdString& server, const CStdString& token, const CStdString& method, const CStdString& path,
		const std::string* body, long& status, std::string& response, int timeout);
	bool CallServer(const CStdString& server, const CStdString& token, const CStdString& method, const CStdString& path,
		const CVariant* body, long& status, CVariant& answer, int timeout);
	void Queue(const CStdString& method, const CStdString& path, const CVariant* body);
	void LoadLink();
	void SaveLink();
	void AddSource();
	void ShowError(const CStdString& line1, const CStdString& line2 = "", const CStdString& line3 = "");
	// The server couldn't be reached, and libcurl's reason why
	void ShowUnreachable(const CStdString& server);
	static CStdString ErrorOf(const CVariant& answer, long status);

	// The link (special://profile/mediariffle.xml): the server it was made on, its token, and its account
	mutable CCriticalSection m_section;
	CStdString m_linkServer;
	CStdString m_token;
	CStdString m_user;
	// Why the last request that failed did ("Failed to connect to … port …: Connection refused")
	CStdString m_lastError;

	// What's playing from MediaRiffle, to save where it got to
	struct Playing
	{
		Playing() : fileId(0), offset(0), seconds(0), lastReport(0), lastAlive(0) {}
		int fileId;
		CStdString sessionId;
		CStdString stream;   // the URL the player has
		double offset;       // a stream that starts at the resume point: its 0 is this far into the file
		double seconds;      // into the stream, last seen
		DWORD lastReport;
		DWORD lastAlive;
	};
	Playing m_playing;

	// Reports, sent from the thread so the GUI never waits on the network
	struct Report
	{
		CStdString server;
		CStdString token;
		CStdString method;
		CStdString path;
		CVariant body;
		bool hasBody;
	};
	std::deque<Report> m_reports;
	CEvent m_wake;
};

extern CMediaRiffle g_mediaRiffle;

#endif //H_CMEDIARIFFLE
