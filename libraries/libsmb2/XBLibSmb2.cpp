#include "XBLibSmb2.h"
#include "utils\Log.h"
#include "utils\SingleLock.h"
#include <fcntl.h>
#include <map>
#include <vector>

// A connection nothing has used for this long is closed
#define SMB_IDLE_CLOSE    (5 * 60 * 1000)
// A connection left idle for this long is checked with an echo before it's used again: a server drops idle
// sessions (Windows after 15 minutes), and one that restarted has forgotten them
#define SMB_IDLE_CHECK    (30 * 1000)
// How long, in seconds, the echo (and signing off) waits for a server: one that's gone away doesn't answer at all
#define SMB_SHORT_TIMEOUT 5

// The connections by KeyOf, and why the last one couldn't be made or folder opened
static CCriticalSection s_poolLock;
static std::map<CStdString, CXBLibSMB2*> s_pool;
static CStdString s_strLastError;

static void KeepError(const CStdString& strError)
{
	CSingleLock lock(s_poolLock);
	s_strLastError = strError;
}

// Who to sign in as. A plain name is an account on the server itself, as before (domain "workgroup"); a domain
// account can be DOMAIN\name, or name@domain, whose domain is in the name, so NTLM is sent none beside it; and an
// smb://domain;name@server/ URL gives its domain apart.
static void SetCredentials(struct smb2_context* smb2, const CURL& url)
{
	CStdString user = url.GetUserName();
	CStdString domain = url.GetDomain();

	int iSlash = user.Find('\\');
	if (domain.IsEmpty() && iSlash > 0)
	{
		domain = user.Left(iSlash);
		user = user.Mid(iSlash + 1);
	}
	if (domain.IsEmpty() && user.Find('@') < 0)
		domain = "workgroup";

	smb2_set_user(smb2, user.c_str());
	smb2_set_password(smb2, url.GetPassWord().c_str());
	smb2_set_domain(smb2, domain.c_str());
}

// Which connection a URL uses: one per server, share and sign-in, and whether it's for listing folders. The password
// is in it too, so a source whose password was changed signs in again; the key is never logged.
static CStdString KeyOf(const CURL& url, bool bForFolders)
{
	CStdString strHost = url.GetHostName();
	CStdString strShare = url.GetShareName();
	strHost.ToLower();
	strShare.ToLower();

	CStdString strKey;
	strKey.Format("%s/%s|%s;%s:%s|%s", strHost.c_str(), strShare.c_str(), url.GetDomain().c_str(),
		url.GetUserName().c_str(), url.GetPassWord().c_str(), bForFolders ? "folders" : "files");
	return strKey;
}

CXBLibSMB2::CXBLibSMB2()
{
	m_pContext = NULL;
	m_bConnected = false;
	m_iUsers = 0;
	m_dwLastUsed = GetTickCount();
	m_bRetired = false;
}

CXBLibSMB2::~CXBLibSMB2()
{
	if(m_pContext)
	{
		// Signing off waits on the server, so not for long: it not answering may be why this one's going
		smb2_set_timeout(m_pContext, SMB_SHORT_TIMEOUT);
		if(m_bConnected && smb2_get_fd(m_pContext) != (t_socket)-1)
			smb2_disconnect_share(m_pContext);
		smb2_destroy_context(m_pContext);
	}
}

CXBLibSMB2* CXBLibSMB2::Get(const CURL& url, bool bForFolders)
{
	CStdString strKey = KeyOf(url, bForFolders);

	// A connection kept for the share that has gone dead (the server restarted, or dropped it) is retired and a new
	// one made
	for(int iTry = 0; iTry < 2; iTry++)
	{
		CloseIdle();

		CXBLibSMB2* pConnection;
		bool bAsk;
		{
			CSingleLock lock(s_poolLock);

			std::map<CStdString, CXBLibSMB2*>::iterator it = s_pool.find(strKey);
			if(it != s_pool.end())
				pConnection = it->second;
			else
			{
				pConnection = new CXBLibSMB2();
				pConnection->m_strKey = strKey;
				s_pool[strKey] = pConnection;
			}

			// One in use by another file just now is known to work; one left idle a while is asked
			bAsk = pConnection->m_iUsers == 0 && GetTickCount() - pConnection->m_dwLastUsed > SMB_IDLE_CHECK;
			pConnection->m_iUsers++;
		}

		bool bWasConnected, bReady;
		{
			CSingleLock lock(*pConnection);
			bWasConnected = pConnection->m_bConnected;
			bReady = bWasConnected ? pConnection->IsAlive(bAsk) : pConnection->Connect(url);
		}

		if(bReady)
			return pConnection;

		Retire(pConnection);
		Release(pConnection);

		// It couldn't connect: why is kept for GetLastError
		if(!bWasConnected)
			return NULL;
	}

	return NULL;
}

void CXBLibSMB2::Release(CXBLibSMB2* pConnection)
{
	if(!pConnection)
		return;

	bool bDelete;
	{
		CSingleLock lock(s_poolLock);
		pConnection->m_iUsers--;
		pConnection->m_dwLastUsed = GetTickCount();
		bDelete = pConnection->m_bRetired && pConnection->m_iUsers == 0;
	}

	if(bDelete)
		delete pConnection;
}

CStdString CXBLibSMB2::GetLastError()
{
	CSingleLock lock(s_poolLock);
	return s_strLastError;
}

// Out of the pool, so the next Get makes a new connection; deleted when its last user releases it
void CXBLibSMB2::Retire(CXBLibSMB2* pConnection)
{
	CSingleLock lock(s_poolLock);

	if(pConnection->m_bRetired)
		return;

	pConnection->m_bRetired = true;
	std::map<CStdString, CXBLibSMB2*>::iterator it = s_pool.find(pConnection->m_strKey);
	if(it != s_pool.end() && it->second == pConnection)
		s_pool.erase(it);
}

// Connections nothing has used for a while are closed
void CXBLibSMB2::CloseIdle()
{
	std::vector<CXBLibSMB2*> idle;
	{
		CSingleLock lock(s_poolLock);

		DWORD dwNow = GetTickCount();
		std::map<CStdString, CXBLibSMB2*>::iterator it = s_pool.begin();
		while(it != s_pool.end())
		{
			CXBLibSMB2* pConnection = it->second;
			if(pConnection->m_iUsers == 0 && dwNow - pConnection->m_dwLastUsed > SMB_IDLE_CLOSE)
			{
				pConnection->m_bRetired = true;
				idle.push_back(pConnection);
				s_pool.erase(it++);
			}
			else
				++it;
		}
	}

	// Not under the pool's lock: signing off waits on the server
	for(unsigned int i = 0; i < idle.size(); i++)
		delete idle[i];
}

// Signs in to the server and connects to the share, on a context of its own (a new one after a failed try)
bool CXBLibSMB2::Connect(const CURL& url)
{
	if(m_pContext)
		smb2_destroy_context(m_pContext);

	m_pContext = smb2_init_context();

	if(m_pContext == NULL)
	{
		KeepError("Failed to init context");
		CLog::Log(LOGERROR, "CXBLibSMB2::Connect - Failed to init context");
		return false;
	}

	SetCredentials(m_pContext, url);
	smb2_set_security_mode(m_pContext, SMB2_NEGOTIATE_SIGNING_ENABLED);

	struct smb2_url* pUrl = ParseUrl(url);

	if(!pUrl)
	{
		KeepError(smb2_get_error(m_pContext));
		return false;
	}

	int rc = smb2_connect_share(m_pContext, pUrl->server, pUrl->share, pUrl->user);
	smb2_destroy_url(pUrl);

	if(rc != 0)
	{
		CStdString strError = smb2_get_error(m_pContext);
		KeepError(strError);
		CLog::Log(LOGERROR, "CXBLibSMB2::Connect - smb2_connect_share failed. %s", strError.c_str());
		return false;
	}

	m_bConnected = true;
	return true;
}

// Whether a connection kept from before still works: its socket is still open and, when asked to, the server answers
// an echo
bool CXBLibSMB2::IsAlive(bool bAsk)
{
	if(smb2_get_fd(m_pContext) == (t_socket)-1)
	{
		CLog::Log(LOGNOTICE, "CXBLibSMB2 - A share's connection was closed, making a new one");
		return false;
	}

	if(!bAsk)
		return true;

	smb2_set_timeout(m_pContext, SMB_SHORT_TIMEOUT);
	bool bAlive = smb2_echo(m_pContext) == 0;
	smb2_set_timeout(m_pContext, 0);

	if(!bAlive)
		CLog::Log(LOGNOTICE, "CXBLibSMB2 - A share's connection stopped answering (%s), making a new one", smb2_get_error(m_pContext));

	return bAlive;
}

// The URL as libsmb2 sees it: server, share and the path within the share.
// The caller frees it with smb2_destroy_url().
struct smb2_url* CXBLibSMB2::ParseUrl(const CURL& url)
{
	struct smb2_url* pUrl = smb2_parse_url(m_pContext, "smb://"+url.GetHostName()+":445/"+url.GetShareName());

	if(!pUrl)
		CLog::Log(LOGERROR, "CXBLibSMB2::ParseUrl - Failed to parse url: %s", smb2_get_error(m_pContext));

	return pUrl;
}

// The path within the share as SMB names it: empty for the share itself
// (libsmb2 leaves it NULL) and without the trailing slash a folder URL has
static CStdString PathInShare(const struct smb2_url* pUrl)
{
	CStdString strPath(pUrl->path ? pUrl->path : "");
	strPath.TrimRight('/');
	return strPath;
}

smb2dir* CXBLibSMB2::OpenDir(const CURL& url)
{
	struct smb2_url* pUrl = ParseUrl(url);

	if(!pUrl)
		return NULL;

	struct smb2dir* pDir = smb2_opendir(m_pContext, pUrl->path);
	smb2_destroy_url(pUrl);

	if(!pDir)
	{
		CStdString strError = smb2_get_error(m_pContext);
		KeepError(strError);
		CLog::Log(LOGERROR, "smb2_opendir failed. %s", strError.c_str());
		return NULL;
	}

	KeepError("");
	return pDir;
}

smb2dirent* CXBLibSMB2::ReadDir(smb2dir* pDir)
{
	return smb2_readdir(m_pContext, pDir);
}

void CXBLibSMB2::CloseDir(smb2dir* pDir)
{
	if(pDir)
		smb2_closedir(m_pContext, pDir);
}

smb2fh* CXBLibSMB2::FileOpen(const CURL& url, UINT64& outFileSize)
{
	outFileSize = 0;

	struct smb2_url* pUrl = ParseUrl(url);

	if(!pUrl)
		return NULL;

	struct smb2fh* fh = smb2_open(m_pContext, pUrl->path, O_RDONLY | O_BINARY);
	smb2_destroy_url(pUrl);

	if(fh == NULL)
		return NULL;

	outFileSize = smb2_lseek(m_pContext, fh, 0, SEEK_END, NULL);
	smb2_lseek(m_pContext, fh, 0, SEEK_SET, NULL);

	return fh;
}

unsigned int CXBLibSMB2::FileRead(smb2fh* fh, void* lpBuf, __int64 uiBufSize)
{
	if(fh == NULL || m_pContext == NULL)
		return 0;

	int bytesRead = smb2_read(m_pContext, fh, static_cast<uint8_t*>(lpBuf), (unsigned int)uiBufSize);

	if(bytesRead < 0)
	{
		CLog::Log(LOGERROR, __FUNCTION__" - smb2_read returned error %i", errno);
		return 0;
	}

	return (unsigned int)bytesRead;
}

__int64 CXBLibSMB2::FileSeek(smb2fh* fh, __int64 iFilePosition, int iWhence)
{
	if(fh == NULL || m_pContext == NULL)
		return -1;

	INT64 iPos = smb2_lseek(m_pContext, fh, iFilePosition, iWhence, NULL);

	if(iPos < 0)
		return -1;

	return (__int64)iPos;
}

__int64 CXBLibSMB2::FileGetPosition(smb2fh* fh)
{
	if(fh == NULL || m_pContext == NULL)
		return 0;

	__int64 iPos = smb2_lseek(m_pContext, fh, 0, SEEK_CUR, NULL);

	if(iPos < 0)
		return 0;

	return iPos;
}

void CXBLibSMB2::FileClose(smb2fh* fh)
{
	if(fh != NULL && m_pContext != NULL)
		smb2_close(m_pContext, fh);
}

smb2fh* CXBLibSMB2::FileOpenForWrite(const CURL& url, bool bOverWrite, UINT64& outFileSize)
{
	outFileSize = 0;

	struct smb2_url* pUrl = ParseUrl(url);

	if(!pUrl)
		return NULL;

	// As CFileHD does: create the file if it's missing, and empty it only when overwriting
	int flags = O_RDWR | O_CREAT | O_BINARY;
	if(bOverWrite)
		flags |= O_TRUNC;

	struct smb2fh* fh = smb2_open(m_pContext, pUrl->path, flags);
	smb2_destroy_url(pUrl);

	if(fh == NULL)
	{
		CLog::Log(LOGERROR, "CXBLibSMB2::FileOpenForWrite - smb2_open failed: %s", smb2_get_error(m_pContext));
		return NULL;
	}

	outFileSize = smb2_lseek(m_pContext, fh, 0, SEEK_END, NULL);
	smb2_lseek(m_pContext, fh, 0, SEEK_SET, NULL);

	return fh;
}

int CXBLibSMB2::FileWrite(smb2fh* fh, const void* lpBuf, __int64 uiBufSize)
{
	if(fh == NULL || m_pContext == NULL)
		return 0;

	// A request carries no more than the server's max write size, so
	// smb2_write can write less than it's given: go on until it's all written
	const uint8_t* pBuf = static_cast<const uint8_t*>(lpBuf);
	__int64 iWritten = 0;

	while(iWritten < uiBufSize)
	{
		int bytesWritten = smb2_write(m_pContext, fh, pBuf + iWritten, (uint32_t)(uiBufSize - iWritten));

		if(bytesWritten <= 0)
		{
			CLog::Log(LOGERROR, __FUNCTION__" - smb2_write failed: %s", smb2_get_error(m_pContext));
			break;
		}

		iWritten += bytesWritten;
	}

	return (int)iWritten;
}

bool CXBLibSMB2::Stat(const CURL& url, struct smb2_stat_64& st)
{
	struct smb2_url* pUrl = ParseUrl(url);

	if(!pUrl)
		return false;

	// Not logged when it fails: asking whether something exists is often expected to
	int rc = smb2_stat(m_pContext, PathInShare(pUrl).c_str(), &st);
	smb2_destroy_url(pUrl);

	return rc == 0;
}

bool CXBLibSMB2::Delete(const CURL& url)
{
	struct smb2_url* pUrl = ParseUrl(url);

	if(!pUrl)
		return false;

	int rc = smb2_unlink(m_pContext, PathInShare(pUrl).c_str());
	smb2_destroy_url(pUrl);

	if(rc != 0)
	{
		CLog::Log(LOGERROR, "CXBLibSMB2::Delete - smb2_unlink failed: %s", smb2_get_error(m_pContext));
		return false;
	}

	return true;
}

bool CXBLibSMB2::Rename(const CURL& url, const CURL& urlNew)
{
	struct smb2_url* pUrl = ParseUrl(url);
	struct smb2_url* pUrlNew = ParseUrl(urlNew);
	bool bRenamed = false;

	if(pUrl && pUrlNew)
	{
		// SMB renames within a share; moving elsewhere is a copy
		if(CStdString(pUrl->server).CompareNoCase(pUrlNew->server) != 0 || CStdString(pUrl->share).CompareNoCase(pUrlNew->share) != 0)
			CLog::Log(LOGERROR, "CXBLibSMB2::Rename - Can't rename across shares");
		else if(smb2_rename(m_pContext, PathInShare(pUrl).c_str(), PathInShare(pUrlNew).c_str()) != 0)
			CLog::Log(LOGERROR, "CXBLibSMB2::Rename - smb2_rename failed: %s", smb2_get_error(m_pContext));
		else
			bRenamed = true;
	}

	if(pUrl)
		smb2_destroy_url(pUrl);
	if(pUrlNew)
		smb2_destroy_url(pUrlNew);

	return bRenamed;
}

bool CXBLibSMB2::CreateDir(const CURL& url)
{
	struct smb2_url* pUrl = ParseUrl(url);

	if(!pUrl)
		return false;

	int rc = smb2_mkdir(m_pContext, PathInShare(pUrl).c_str());
	smb2_destroy_url(pUrl);

	if(rc != 0)
	{
		CLog::Log(LOGERROR, "CXBLibSMB2::CreateDir - smb2_mkdir failed: %s", smb2_get_error(m_pContext));
		return false;
	}

	return true;
}

bool CXBLibSMB2::RemoveDir(const CURL& url)
{
	struct smb2_url* pUrl = ParseUrl(url);

	if(!pUrl)
		return false;

	int rc = smb2_rmdir(m_pContext, PathInShare(pUrl).c_str());
	smb2_destroy_url(pUrl);

	if(rc != 0)
	{
		CLog::Log(LOGERROR, "CXBLibSMB2::RemoveDir - smb2_rmdir failed: %s", smb2_get_error(m_pContext));
		return false;
	}

	return true;
}

CSMBConnectionLock::CSMBConnectionLock(const CURL& url, bool bForFolders)
{
	m_pConnection = CXBLibSMB2::Get(url, bForFolders);
	if(m_pConnection)
		::EnterCriticalSection(*m_pConnection);
}

CSMBConnectionLock::~CSMBConnectionLock()
{
	if(m_pConnection)
	{
		::LeaveCriticalSection(*m_pConnection);
		CXBLibSMB2::Release(m_pConnection);
	}
}
