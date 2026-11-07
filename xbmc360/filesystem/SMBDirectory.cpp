#include "SMBDirectory.h"
#include "utils\SingleLock.h"
#include "utils\Log.h"
#include "utils\Util.h"
#include "XBLibSmb2.h"
#include "FileItem.h"

using namespace XFILE;

CSMBDirectory::CSMBDirectory(void)
{
} 

CSMBDirectory::~CSMBDirectory(void)
{
}

bool CSMBDirectory::GetDirectory(const CStdString& strPath, CFileItemList &items)
{
	// We accept smb://[[[domain;]user[:password@]]server[/share[/path[/file]]]]

	// We need an url to do proper escaping
	CURL url(strPath);

	// The share's connection for folders, locked as LibSMB2 isn't thread safe
	CSMBConnectionLock connection(url, true);
	CXBLibSMB2* pConnection = connection.Get();

	if(!pConnection)
		return false;

	// Separate roots for the authentication and the containing items to allow browsing to work correctly
	CStdString strRoot = strPath;

	struct smb2dir* pDir = pConnection->OpenDir(url);

	if(!pDir)
		return false;

	struct smb2dirent* dirEnt = NULL;
	CStdString strFile;

	if(!CUtil::HasSlashAtEnd(strRoot)) strRoot += "/";

	while(dirEnt = pConnection->ReadDir(pDir))
	{
		strFile = dirEnt->name;

		if(strFile == ".." || strFile == ".")
			continue;

		// When it was last changed, in seconds since 1970 as the server says
		time_t modified = (time_t)dirEnt->st.smb2_mtime;

		if(dirEnt->st.smb2_type == SMB2_TYPE_DIRECTORY)
		{
			CFileItemPtr pItem(new CFileItem(strFile));
			pItem->m_strPath = strRoot;
/*
			// Needed for network / workgroup browsing
			// skip if root if we are given a server
			if(dirEnt->smbc_type == SMBC_SERVER)
			{
				// Create url with same options, user, pass.. but no filename or host
				CURL rooturl(strRoot);
				rooturl.SetFileName("");
				rooturl.SetHostName("");
				pItem->m_strPath = smb.URLEncode(rooturl);
			}
*/
			pItem->m_strPath += dirEnt->name;

			if(!CUtil::HasSlashAtEnd(pItem->m_strPath))
				pItem->m_strPath += '/';

			pItem->m_bIsFolder = true;
			pItem->m_dateTime = modified;
//			vecCacheItems.Add(pItem);
			items.Add(pItem);
		}
		else if(dirEnt->st.smb2_type == SMB2_TYPE_FILE)
		{
			CFileItemPtr pItem(new CFileItem(strFile));
			pItem->m_strPath = strRoot + dirEnt->name;
			pItem->m_bIsFolder = false;
			pItem->m_dwSize = dirEnt->st.smb2_size;
			pItem->m_dateTime = modified;

//			vecCacheItems.Add(pItem);
			if(IsAllowed(dirEnt->name))
				items.Add(pItem);
		}
	}

	pConnection->CloseDir(pDir);

	return true;
}

CStdString CSMBDirectory::GetLastError()
{
	return CXBLibSMB2::GetLastError();
}

bool CSMBDirectory::Create(const char* strPath)
{
	CURL url(strPath);
	CSMBConnectionLock connection(url, true);

	return connection.Get() && connection.Get()->CreateDir(url);
}

bool CSMBDirectory::Remove(const char* strPath)
{
	CURL url(strPath);
	CSMBConnectionLock connection(url, true);

	return connection.Get() && connection.Get()->RemoveDir(url);
}

bool CSMBDirectory::Exists(const char* strPath)
{
	CURL url(strPath);
	CSMBConnectionLock connection(url, true);

	struct smb2_stat_64 st;
	return connection.Get() && connection.Get()->Stat(url, st) && st.smb2_type == SMB2_TYPE_DIRECTORY;
}