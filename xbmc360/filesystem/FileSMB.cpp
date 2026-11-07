#include "FileSMB.h"
#include "utils\SingleLock.h"
#include "utils\Log.h"
#include <fcntl.h>
#include "XBLibSmb2.h"

using namespace XFILE;

CFileSMB::CFileSMB()
{
	m_pConnection = NULL;
	m_pFileHandle = NULL;
	m_fileSize = 0;
}

CFileSMB::~CFileSMB()
{
	Close();
}

bool CFileSMB::Open(const CURL& strURL)
{
	Close(); // Ensure any previous handle is released

	// The share's own connection, so a file open on another share doesn't take it away
	m_pConnection = CXBLibSMB2::Get(strURL);

	if(m_pConnection)
	{
		CSingleLock lock(*m_pConnection);
		m_pFileHandle = m_pConnection->FileOpen(strURL, m_fileSize);
	}

	if(!m_pFileHandle)
	{
		CLog::Log(LOGERROR, "CFileSMB::Open - Failed to open file");
		Close();
		return false;
	}

	return true;
}

bool CFileSMB::OpenForWrite(const CURL& strURL, bool bOverWrite)
{
	Close(); // Ensure any previous handle is released

	m_pConnection = CXBLibSMB2::Get(strURL);

	if(m_pConnection)
	{
		CSingleLock lock(*m_pConnection);
		m_pFileHandle = m_pConnection->FileOpenForWrite(strURL, bOverWrite, m_fileSize);
	}

	if(!m_pFileHandle)
	{
		CLog::Log(LOGERROR, "CFileSMB::OpenForWrite - Failed to open file");
		Close();
		return false;
	}

	return true;
}

__int64 CFileSMB::GetLength()
{
	return (__int64)m_fileSize;
}

__int64 CFileSMB::GetPosition()
{
	if(!m_pConnection)
		return 0;

	CSingleLock lock(*m_pConnection);

	return m_pConnection->FileGetPosition(m_pFileHandle);
}

void CFileSMB::Close()
{
	if(m_pConnection)
	{
		if(m_pFileHandle)
		{
			CSingleLock lock(*m_pConnection);
			m_pConnection->FileClose(m_pFileHandle);
		}

		CXBLibSMB2::Release(m_pConnection);
		m_pConnection = NULL;
	}
	m_pFileHandle = NULL;
	m_fileSize = 0;
}

unsigned int CFileSMB::Read(void *lpBuf, __int64 uiBufSize)
{
	if(!m_pConnection)
		return 0;

	CSingleLock lock(*m_pConnection);

	return m_pConnection->FileRead(m_pFileHandle, lpBuf, uiBufSize);
}

__int64 CFileSMB::Seek(__int64 iFilePosition, int iWhence)
{
	if(!m_pConnection)
		return -1;

	CSingleLock lock(*m_pConnection);

	return m_pConnection->FileSeek(m_pFileHandle, iFilePosition, iWhence);
}

int CFileSMB::Write(const void* lpBuf, __int64 uiBufSize)
{
	if(!m_pConnection)
		return 0;

	CSingleLock lock(*m_pConnection);

	int iWritten = m_pConnection->FileWrite(m_pFileHandle, lpBuf, uiBufSize);

	// The length was read when opening: keep it up as the file grows
	__int64 iPos = m_pConnection->FileGetPosition(m_pFileHandle);
	if(iPos > (__int64)m_fileSize)
		m_fileSize = iPos;

	return iWritten;
}

int CFileSMB::Stat(const CURL& url, struct __stat64* buffer)
{
	CSMBConnectionLock connection(url);

	struct smb2_stat_64 st;
	if(!connection.Get() || !connection.Get()->Stat(url, st))
		return -1;

	if(buffer)
	{
		memset(buffer, 0, sizeof(struct __stat64));
		buffer->st_mode = (st.smb2_type == SMB2_TYPE_DIRECTORY) ? _S_IFDIR : _S_IFREG;
		buffer->st_nlink = 1;
		buffer->st_size = st.smb2_size;
		buffer->st_atime = st.smb2_atime;
		buffer->st_mtime = st.smb2_mtime;
		buffer->st_ctime = st.smb2_ctime;
	}

	return 0;
}

bool CFileSMB::Exists(const CURL& url)
{
	CSMBConnectionLock connection(url);

	struct smb2_stat_64 st;
	return connection.Get() && connection.Get()->Stat(url, st);
}

bool CFileSMB::Delete(const CURL& url)
{
	CSMBConnectionLock connection(url);

	return connection.Get() && connection.Get()->Delete(url);
}

bool CFileSMB::Rename(const CURL& url, const CURL& urlnew)
{
	CSMBConnectionLock connection(url);

	return connection.Get() && connection.Get()->Rename(url, urlnew);
}
