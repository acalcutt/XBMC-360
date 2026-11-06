#ifndef H_CMEDIARIFFLE
#define H_CMEDIARIFFLE

#include "utils\StdString.h"
#include "utils\Variant.h"
#include "utils\CriticalSection.h"
#include "utils\Event.h"
#include "utils\Thread.h"

#include <deque>
#include <map>
#include <string>

class CFileItem;

// MediaRiffle servers: their video libraries as sources (mediariffle://host:port/, or mediariffles:// over HTTPS),
// played as the HLS streams a server makes for the console (H.264 at 720p, transcoded there), and where each one
// stopped saved back to it, so it carries on from there on any device.
//
// A server is added like any network location (Add source > Add network location > MediaRiffle server). The console is
// linked to an account on it once, the first time it's opened, with a short code someone enters at the server's /link
// in a browser, and from then on signs in with a token of its own (MediaRiffle's docs/devices.md). The tokens are kept
// apart, in special://profile/mediariffle.xml; signing out is on the source's context menu.
class CMediaRiffle : public CThread
{
public:
	CMediaRiffle();
	virtual ~CMediaRiffle();

	// At startup and shutdown: the links, and the thread that reports progress
	void Start();
	void Stop();

	// A MediaRiffle path; its server ("http://host:port", lower case); and its source ("mediariffle://host:port/"),
	// which every path on that server starts with. Empty for mediariffle:// itself, which lists the sources.
	static bool IsMediaRiffle(const CStdString& path);
	static CStdString ServerOf(const CStdString& path);
	static CStdString SourceOf(const CStdString& path);

	// Linked to an account on a server; the account's name
	bool IsLinked(const CStdString& server) const;
	CStdString GetUser(const CStdString& server) const;
	// The servers this console is linked to, and as whom: a line each, without the tokens (diagnostics)
	CStdString Describe() const;

	// Links this console to an account on the server: shows the code to enter at <server>/link, and waits for it. On
	// the GUI thread. True once linked.
	bool Link(const CStdString& server);
	// Signs out: the server forgets this console, and so does it
	void Unlink(const CStdString& server);

	// An API call, with the server's token: path from the server's root ("/api/libraries"); body as JSON, or none.
	// False when the server couldn't be reached; else its status, and its answer when that's JSON.
	bool Call(const CStdString& server, const CStdString& method, const CStdString& path, const CVariant* body,
		long& status, CVariant& answer, int timeout = 20);
	// A GET with the token, as it comes (pictures)
	bool Fetch(const CStdString& server, const CStdString& path, std::string& data, long& status);

	// Playing: a mediariffle:// item to play, and the stream it plays as (POST /api/play)
	bool StartPlayback(const CFileItem& item, CFileItem& stream);
	// Play all and Shuffle: <source>queue/<what>/<value> or <source>shuffle/<what>/<value>, for a library, a show, a
	// category (tag) or a list. What the server lists to play in a row (/api/queue) becomes the video playlist, each
	// video a stream asked for when its turn comes.
	static bool IsQueue(const CStdString& path);
	bool PlayQueue(const CFileItem& item);
	// From the application while something plays (every frame), and when it stops or ends
	void OnPlaying(const CStdString& currentFile, double seconds, bool paused);
	void OnStopped(bool ended);

	// Errors, in a dialog: the server couldn't be reached (and libcurl's reason), or what it answered
	void ShowUnreachable(const CStdString& server);
	void ShowError(const CStdString& line1, const CStdString& line2 = "", const CStdString& line3 = "");
	static CStdString ErrorOf(const CVariant& answer, long status);

protected:
	virtual void Process();

	bool Request(const CStdString& server, const CStdString& token, const CStdString& method, const CStdString& path,
		const std::string* body, long& status, std::string& response, int timeout);
	bool CallServer(const CStdString& server, const CStdString& token, const CStdString& method, const CStdString& path,
		const CVariant* body, long& status, CVariant& answer, int timeout);
	CStdString TokenFor(const CStdString& server) const;
	void Forget(const CStdString& server);
	void Queue(const CStdString& server, const CStdString& method, const CStdString& path, const CVariant* body);
	void LoadLinks();
	void SaveLinks();

	// The links (special://profile/mediariffle.xml): the account on each server, and the console's token there
	struct Account
	{
		CStdString token;
		CStdString user;
	};
	mutable CCriticalSection m_section;
	std::map<CStdString, Account> m_links;
	// Why the last request that failed did ("Failed to connect to … port …: Connection refused")
	CStdString m_lastError;

	// What's playing from MediaRiffle, to save where it got to
	struct Playing
	{
		Playing() : fileId(0), offset(0), seconds(0), lastReport(0), lastAlive(0) {}
		CStdString server;
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
