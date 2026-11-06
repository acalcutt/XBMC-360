#ifndef H_CSECRETS
#define H_CSECRETS

#include "utils\StdString.h"

// Passwords and keys, kept out of what's written down: the log, and the
// copies of it that diagnostics save
class CSecrets
{
public:
	// Puts ***** for a URL's password (smb://user:password@server/...) and
	// for keys given in a URL or a header (api_key=..., Token="...")
	static void Hide(CStdString& strText);
};

#endif //H_CSECRETS
