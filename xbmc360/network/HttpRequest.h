#ifndef H_CHTTPREQUEST
#define H_CHTTPREQUEST

#include "utils\StdString.h"

#include <string>
#include <vector>

// One HTTP request as the media server clients make them (MediaRiffle, Jellyfin): JSON in and out, the socket marked
// so XNet lets it reach a PC, and certificates taken as they come (the console has no store to check them against).
class CHttpRequest
{
public:
	// method is GET, POST, PUT or DELETE; headers are whole lines ("Authorization: …"); body, when there is one, is
	// sent as JSON. False when there was no answer at all, with libcurl's reason in error; else the status and what
	// came back.
	static bool Send(const CStdString& url, const std::vector<CStdString>& headers, const CStdString& method,
		const std::string* body, long& status, std::string& response, int timeout, CStdString& error);
};

#endif //H_CHTTPREQUEST
