#ifndef H_CJELLYFIN
#define H_CJELLYFIN

#include "utils\StdString.h"
#include "utils\Variant.h"
#include "utils\CriticalSection.h"

#include <map>

// Jellyfin servers (and Emby's, whose API Jellyfin kept): their video libraries as sources (jellyfin://host:port/, or
// jellyfins:// over HTTPS).
//
// A server is added like any network location (Add source > Add network location > Jellyfin server). The first time
// it's opened the console signs in: with Quick Connect, a code entered in Jellyfin where someone's signed in, when the
// server has it on; else with a user name and password. The access token it gets, the user, and this console's device
// ID are kept in special://profile/jellyfin.xml.
class CJellyfin
{
public:
	CJellyfin();
	virtual ~CJellyfin();

	// At startup: the accounts
	void Start();

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

	// An API call with the server's token: path from the server's root ("/UserViews?userId=…"); body as JSON, or none.
	// False when the server couldn't be reached; else its status, and its answer when that's JSON.
	bool Call(const CStdString& server, const CStdString& method, const CStdString& path, const CVariant* body,
		long& status, CVariant& answer, int timeout = 20);

	// Errors, in a dialog: the server couldn't be reached (and libcurl's reason), or what it answered
	void ShowUnreachable(const CStdString& server);
	void ShowError(const CStdString& line1, const CStdString& line2 = "", const CStdString& line3 = "");
	static CStdString ErrorOf(const CVariant& answer, long status);

private:
	// The Authorization header Jellyfin (and Emby) want: who's asking, and the token once there is one
	CStdString AuthHeader(const CStdString& token) const;
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

	mutable CCriticalSection m_section;
	std::map<CStdString, Account> m_accounts;
	CStdString m_deviceId;
	// Why the last request that failed did ("Failed to connect to … port …: Connection refused")
	CStdString m_lastError;
};

extern CJellyfin g_jellyfin;

#endif //H_CJELLYFIN
