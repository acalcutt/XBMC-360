#include "MediaRiffleFile.h"
#include "URL.h"
#include "network\MediaRiffle.h"
#include "utils\Log.h"

#include <sys/stat.h>

using namespace XFILE;

CMediaRiffleFile::CMediaRiffleFile()
{
	m_position = 0;
}

CMediaRiffleFile::~CMediaRiffleFile()
{
	Close();
}

bool CMediaRiffleFile::Open(const CURL& url)
{
	Close();

	if (!Exists(url))
		return false;

	long status = 0;
	CStdString server = CMediaRiffle::ServerOf(url.Get());
	CStdString path = "/api/images/" + url.GetFileName().Mid(6) + url.GetOptions();
	if (!g_mediaRiffle.Fetch(server, path, m_data, status) || status != 200 || m_data.empty())
	{
		CLog::Log(LOGDEBUG, "%s - no picture at %s%s (%ld)", __FUNCTION__, server.c_str(), path.c_str(), status);
		m_data.clear();
		return false;
	}
	return true;
}

// <source>image/<kind>/<id>. Whether it's there is only known by fetching it: Open says
bool CMediaRiffleFile::Exists(const CURL& url)
{
	return url.GetFileName().Left(6).Equals("image/") && !url.GetHostName().IsEmpty();
}

int CMediaRiffleFile::Stat(const CURL& url, struct __stat64* buffer)
{
	if (!Exists(url))
		return -1;

	if (buffer)
	{
		memset(buffer, 0, sizeof(struct __stat64));
		buffer->st_mode = _S_IFREG;
	}
	return 0;
}

unsigned int CMediaRiffleFile::Read(void* lpBuf, int64_t uiBufSize)
{
	if (m_position >= (int64_t)m_data.size())
		return 0;

	int64_t left = (int64_t)m_data.size() - m_position;
	unsigned int count = (unsigned int)(uiBufSize < left ? uiBufSize : left);
	memcpy(lpBuf, m_data.data() + m_position, count);
	m_position += count;
	return count;
}

int64_t CMediaRiffleFile::Seek(int64_t iFilePosition, int iWhence)
{
	int64_t position = iFilePosition;
	if (iWhence == SEEK_CUR)
		position += m_position;
	else if (iWhence == SEEK_END)
		position += (int64_t)m_data.size();
	else if (iWhence != SEEK_SET)
		return -1;

	if (position < 0 || position > (int64_t)m_data.size())
		return -1;

	m_position = position;
	return m_position;
}

void CMediaRiffleFile::Close()
{
	m_data.clear();
	m_position = 0;
}

int64_t CMediaRiffleFile::GetPosition()
{
	return m_position;
}

int64_t CMediaRiffleFile::GetLength()
{
	return (int64_t)m_data.size();
}
