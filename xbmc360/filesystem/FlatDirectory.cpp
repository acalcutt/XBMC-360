#include "FlatDirectory.h"
#include "Directory.h"
#include "File.h"
#include "FileItem.h"
#include "Settings.h"
#include "URL.h"
#include "utils\Log.h"
#include "utils\URIUtils.h"

using namespace XFILE;

// Enough for any library, short of a folder that leads back into itself
#define FLAT_MAX_DEPTH 16
#define FLAT_MAX_ITEMS 5000

CFlatDirectory::CFlatDirectory(void)
{
}

CFlatDirectory::~CFlatDirectory(void)
{
}

CStdString CFlatDirectory::Make(const CStdString& strFolder)
{
	CStdString strEncoded = strFolder;
	CURL::Encode(strEncoded);
	return "flat://" + strEncoded + "/";
}

CStdString CFlatDirectory::FolderOf(const CStdString& strPath)
{
	if (!strPath.Left(7).Equals("flat://"))
		return "";

	CStdString strFolder = strPath.Mid(7);
	URIUtils::RemoveSlashAtEnd(strFolder);
	CURL::Decode(strFolder);
	return strFolder;
}

bool CFlatDirectory::CanFlatten(const CStdString& strFolder)
{
	if (URIUtils::IsMultiPath(strFolder))
		return true;

	CStdString strProtocol = CURL(strFolder).GetProtocol();
	return strProtocol.Equals("smb") || (strProtocol.IsEmpty() && URIUtils::IsHD(strFolder));
}

bool CFlatDirectory::GetDirectory(const CStdString& strPath, CFileItemList &items)
{
	CStdString strFolder = FolderOf(strPath);
	if (strFolder.IsEmpty() || !CanFlatten(strFolder))
		return false;

	CStdString strName = strFolder;
	URIUtils::RemoveSlashAtEnd(strName);
	AddVideos(strFolder, URIUtils::GetFileName(strName), items, 0);

	if (items.Size() >= FLAT_MAX_ITEMS)
		CLog::Log(LOGWARNING, "%s - stopped at %d videos in %s", __FUNCTION__, FLAT_MAX_ITEMS, CURL(strFolder).GetWithoutUserDetails().c_str());

	return true;
}

// A folder's videos and those in the folders in it. strName is the folder's own name, for a DVD in it.
void CFlatDirectory::AddVideos(const CStdString& strFolder, const CStdString& strName, CFileItemList &items, int iDepth)
{
	CFileItemList listing;
	if (!CDirectory::GetDirectory(strFolder, listing, g_settings.GetVideoExtensions(), false))
		return;

	for (int i = 0; i < listing.Size() && items.Size() < FLAT_MAX_ITEMS; i++)
	{
		CFileItemPtr item = listing[i];

		if (!item->m_bIsFolder)
		{
			items.Add(item);
			continue;
		}

		// A DVD: its VIDEO_TS.IFO plays the disc, named for the folder it's in
		if (item->GetLabel().Equals("VIDEO_TS"))
		{
			CStdString strIFO = URIUtils::AddFileToFolder(item->GetPath(), "VIDEO_TS.IFO");
			if (CFile::Exists(strIFO))
			{
				CFileItemPtr dvd(new CFileItem(strName.IsEmpty() ? item->GetLabel() : strName));
				dvd->SetPath(strIFO);
				dvd->m_bIsFolder = false;
				items.Add(dvd);
			}
			continue;
		}

		if (iDepth < FLAT_MAX_DEPTH)
			AddVideos(item->GetPath(), item->GetLabel(), items, iDepth + 1);
	}
}
