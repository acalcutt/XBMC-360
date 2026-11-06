#include "TitleNames.h"
#include "filesystem\SpecialProtocol.h"
#include "network\HttpRequest.h"
#include "utils\JSONVariantParser.h"
#include "utils\Log.h"
#include "utils\SingleLock.h"
#include <map>
#include <vector>

// The names found, one a line: title ID=name
#define NAMES_FILE "special://profile/titlenames.txt"
#define LOOKUP_URL "http://xboxunity.net/Resources/Lib/TitleList.php?page=0&count=5&search=%08X&sort=3&direction=1&category=0&filter=0"

// How long after a lookup that got no answer (no network yet, at the start) it's asked again
#define ASK_AGAIN_AFTER (2 * 60 * 1000)

static CCriticalSection s_lock;
static std::map<DWORD, CStdString> s_names;
static std::map<DWORD, DWORD> s_asked; // When each was asked for; 0 once XboxUnity says it doesn't know it
static std::vector<DWORD> s_waiting;   // To ask, in turn
static bool s_asking = false;          // Whether there's a thread asking
static bool s_loaded = false;

// With s_lock held
void CTitleNames::Load()
{
	s_loaded = true;

	FILE* f = fopen(CSpecialProtocol::TranslatePath(NAMES_FILE).c_str(), "r");
	if (!f)
		return;

	char line[512];
	while (fgets(line, sizeof(line), f))
	{
		CStdString strLine = line;
		strLine.TrimRight("\r\n");
		if (strLine.Find('=') == 8)
			s_names[strtoul(strLine.Left(8).c_str(), NULL, 16)] = strLine.Mid(9);
	}
	fclose(f);
}

CStdString CTitleNames::Get(DWORD dwTitleId)
{
	if (!dwTitleId)
		return "";

	CSingleLock lock(s_lock);
	if (!s_loaded)
		Load();

	std::map<DWORD, CStdString>::const_iterator name = s_names.find(dwTitleId);
	if (name != s_names.end())
		return name->second;

	std::map<DWORD, DWORD>::const_iterator asked = s_asked.find(dwTitleId);
	DWORD dwNow = GetTickCount();
	if (asked == s_asked.end() || (asked->second != 0 && dwNow - asked->second > ASK_AGAIN_AFTER))
	{
		s_asked[dwTitleId] = dwNow;
		s_waiting.push_back(dwTitleId);

		// A thread that asks for each in turn, and goes when there are none left
		if (!s_asking)
		{
			s_asking = true;
			CTitleNames* thread = new CTitleNames;
			thread->Create(true);
		}
	}
	return "";
}

CStdString CTitleNames::NameFor(const CStdString& strText)
{
	if (strText.size() != 8 || strText.find_first_not_of("0123456789ABCDEFabcdef") != CStdString::npos)
		return strText;

	CStdString strName = Get(strtoul(strText.c_str(), NULL, 16));
	return strName.IsEmpty() ? strText : strName;
}

void CTitleNames::Process()
{
	for (;;)
	{
		DWORD dwTitleId;
		{
			CSingleLock lock(s_lock);
			if (s_waiting.empty())
			{
				s_asking = false;
				return;
			}
			dwTitleId = s_waiting.front();
			s_waiting.erase(s_waiting.begin());
		}

		LookUp(dwTitleId);
	}
}

void CTitleNames::LookUp(DWORD dwTitleId)
{
	CStdString strId, strUrl;
	strId.Format("%08X", dwTitleId);
	strUrl.Format(LOOKUP_URL, dwTitleId);

	std::vector<CStdString> headers;
	long status = 0;
	std::string response;
	CStdString strError;
	if (!CHttpRequest::Send(strUrl, headers, "GET", NULL, status, response, 15, strError) || status != 200)
	{
		if (strError.IsEmpty())
			strError.Format("HTTP %ld", status);
		CLog::Log(LOGNOTICE, "Couldn't ask XboxUnity for the name of %s (%s)", strId.c_str(), strError.c_str());

		CSingleLock lock(s_lock);
		s_asked[dwTitleId] = GetTickCount();
		return;
	}

	CVariant answer = CJSONVariantParser::Parse(response);
	const CVariant& items = answer["Items"];
	for (unsigned int i = 0; i < items.size(); i++)
	{
		if (!strId.Equals(items[i]["TitleID"].asString().c_str()))
			continue;

		CStdString strName = items[i]["Name"].asString();
		strName.Replace("\r", " ");
		strName.Replace("\n", " ");
		strName.Trim();
		if (strName.IsEmpty())
			break;

		CLog::Log(LOGNOTICE, "XboxUnity: %s is %s", strId.c_str(), strName.c_str());

		CSingleLock lock(s_lock);
		s_names[dwTitleId] = strName;
		FILE* f = fopen(CSpecialProtocol::TranslatePath(NAMES_FILE).c_str(), "a");
		if (f)
		{
			fprintf(f, "%s=%s\n", strId.c_str(), strName.c_str());
			fclose(f);
		}
		return;
	}

	CLog::Log(LOGNOTICE, "XboxUnity doesn't know %s", strId.c_str());
	CSingleLock lock(s_lock);
	s_asked[dwTitleId] = 0;
}
