#ifndef H_CFLATDIRECTORY
#define H_CFLATDIRECTORY

#include "IDirectory.h"

namespace XFILE
{

// flat://<folder, URL-encoded>/: every video in a folder and in the folders below it, in one list without the folders.
// A DVD's VIDEO_TS folder counts as one video (its VIDEO_TS.IFO, named for the folder it's in). It's listed afresh
// each time, so it shows what's there now.
class CFlatDirectory : public IDirectory
{
public:
	CFlatDirectory(void);
	virtual ~CFlatDirectory(void);

	virtual bool GetDirectory(const CStdString& strPath, CFileItemList &items);
	virtual DIR_CACHE_TYPE GetCacheType(const CStdString& strPath) const { return DIR_CACHE_NEVER; }

	// The flat:// path that lists a folder's videos, and the folder a flat:// path lists
	static CStdString Make(const CStdString& strFolder);
	static CStdString FolderOf(const CStdString& strPath);
	// Folders it can list: on the console's drives or a network share, where looking through every folder is quick
	static bool CanFlatten(const CStdString& strFolder);

private:
	void AddVideos(const CStdString& strFolder, const CStdString& strName, CFileItemList &items, int iDepth);
};

}

#endif //H_CFLATDIRECTORY
