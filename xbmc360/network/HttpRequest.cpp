#include <xtl.h>
#include <winsockx.h>

// Include curl - rename CURL to CURL_HANDLE to avoid clash with our CURL URL class
#define CURL CURL_HANDLE
#include <curl/curl.h>
#undef CURL

#include "HttpRequest.h"
#include "utils\SystemInfo.h"

// CCurlFile's: marks each socket libcurl opens so XNet lets it talk to a PC (without it, connecting fails)
extern "C" int xbmc_curl_sockopt_cb(void* clientp, curl_socket_t curlfd, curlsocktype purpose);

static size_t WriteToString(char* data, size_t size, size_t count, void* user)
{
	((std::string*)user)->append(data, size * count);
	return size * count;
}

bool CHttpRequest::Send(const CStdString& url, const std::vector<CStdString>& headers, const CStdString& method,
	const std::string* body, long& status, std::string& response, int timeout, CStdString& error)
{
	status = 0;
	response.clear();
	error.Empty();

	CURL_HANDLE* h = curl_easy_init();
	if (!h)
	{
		error = "no curl handle";
		return false;
	}

	char reason[CURL_ERROR_SIZE] = "";
	struct curl_slist* lines = NULL;
	for (unsigned int i = 0; i < headers.size(); i++)
		lines = curl_slist_append(lines, headers[i].c_str());
	lines = curl_slist_append(lines, "Accept: application/json");
	if (body)
		lines = curl_slist_append(lines, "Content-Type: application/json");

	curl_easy_setopt(h, CURLOPT_URL, url.c_str());
	curl_easy_setopt(h, CURLOPT_HTTPHEADER, lines);
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_ERRORBUFFER, reason);
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
	{
		curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method.c_str());
		if (body)
		{
			curl_easy_setopt(h, CURLOPT_POSTFIELDS, body->c_str());
			curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)body->size());
		}
	}

	CURLcode result = curl_easy_perform(h);
	curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
	curl_slist_free_all(lines);
	curl_easy_cleanup(h);

	if (result != CURLE_OK)
	{
		error = reason[0] ? reason : curl_easy_strerror(result);
		return false;
	}
	return true;
}
