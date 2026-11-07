#ifndef H_CXBLIBSMB2
#define H_CXBLIBSMB2

#include "url.h"
#include "utils\CriticalSection.h"
#include "utils\StdString.h"
extern "C" {
#include "libsmb2.h"
#include "smb2.h"
}

// A connection to one share: a libsmb2 context signed in to the server and connected to the share. There's one per
// server, share and user, from Get(), so a file open on one share keeps working while another share is read. A
// connection is kept while something uses it and for a few minutes after, for the next file or folder.
//
// libsmb2 isn't thread safe: lock the connection (it's a CCriticalSection) around each call, as CSMBConnectionLock
// does.
class CXBLibSMB2 : public CCriticalSection
{
public:
	// The connection to the URL's share, signed in and connected; NULL when it can't be made (why is in
	// GetLastError). Give each one back with Release. Folders are listed on a connection of their own, so a long
	// listing doesn't hold up a file playing from the same share.
	static CXBLibSMB2* Get(const CURL& url, bool bForFolders = false);
	static void Release(CXBLibSMB2* pConnection);

	// Why the last connection couldn't be made, or folder opened, as libsmb2 put it ("Session setup failed with
	// (0xc000006d) STATUS_LOGON_FAILURE"); empty after a folder that opened
	static CStdString GetLastError();

	// A folder's entries (smb2_opendir reads them all); close it with CloseDir
	smb2dir* OpenDir(const CURL& url);
	smb2dirent* ReadDir(smb2dir* pDir);
	void CloseDir(smb2dir* pDir);

	// Files: the caller owns the handle, and closes it with FileClose on this connection
	smb2fh* FileOpen(const CURL& url, UINT64& outFileSize);
	smb2fh* FileOpenForWrite(const CURL& url, bool bOverWrite, UINT64& outFileSize);
	unsigned int FileRead(smb2fh* fh, void* lpBuf, __int64 uiBufSize);
	int FileWrite(smb2fh* fh, const void* lpBuf, __int64 uiBufSize);
	__int64 FileSeek(smb2fh* fh, __int64 iFilePosition, int iWhence);
	__int64 FileGetPosition(smb2fh* fh);
	void FileClose(smb2fh* fh);

	// Operations on a path in the share
	bool Stat(const CURL& url, struct smb2_stat_64& st);
	bool Delete(const CURL& url);
	bool Rename(const CURL& url, const CURL& urlNew);
	bool CreateDir(const CURL& url);
	bool RemoveDir(const CURL& url);

private:
	CXBLibSMB2();
	~CXBLibSMB2();

	bool Connect(const CURL& url);
	bool IsAlive(bool bAsk);
	struct smb2_url* ParseUrl(const CURL& url);

	static void Retire(CXBLibSMB2* pConnection);
	static void CloseIdle();

	struct smb2_context* m_pContext;
	CStdString m_strKey;
	bool m_bConnected;

	// Guarded by the pool's lock
	int m_iUsers;          // Gets not yet released
	DWORD m_dwLastUsed;    // When it was last got or released
	bool m_bRetired;       // Out of the pool (gone dead): deleted when its last user releases it
};

// A share's connection, got and locked for as long as this lasts: for one call, or a few in a row
class CSMBConnectionLock
{
public:
	CSMBConnectionLock(const CURL& url, bool bForFolders = false);
	~CSMBConnectionLock();

	// NULL when the connection couldn't be made
	CXBLibSMB2* Get() const { return m_pConnection; }

private:
	CSMBConnectionLock(const CSMBConnectionLock&);
	CSMBConnectionLock& operator=(const CSMBConnectionLock&);

	CXBLibSMB2* m_pConnection;
};

#endif //H_CXBLIBSMB2
