#ifndef H_CGAMEMOVE
#define H_CGAMEMOVE

#include "filesystem\File.h"
#include <vector>

class CGUIDialogProgress;

// Moves a game copied to the console (On this console, System info > Games on the console) to a folder that can be
// written to: a share, a USB drive, a folder on the hard drive. Its files are copied there, and deleted from the console
// once they all are; when that can't be done, or is cancelled, what was copied goes again and the console keeps it.
class CGameMove : public IFileCallback
{
public:
	// Asks where to move strName to. False when that's cancelled
	static bool ChoosePlace(const CStdString& strName, CStdString& strPlace);
	// The files in a folder on the console and the folders in it, each folder before what's in it, less files named
	// strSkip. strFolder ends with a backslash, and so do the folders
	static void ListFolder(const CStdString& strFolder, const CStdString& strSkip, std::vector<CStdString>& files, std::vector<CStdString>& folders);
	// Moves files on the console (in strFrom, or folders in it) to the same places in strTo, making strTo and the folders
	// first. bNew: strTo is the game's own folder, which mustn't be there yet. True when they're moved
	static bool Move(const CStdString& strName, const CStdString& strFrom, const std::vector<CStdString>& files,
		const std::vector<CStdString>& folders, const CStdString& strTo, bool bNew);

	virtual bool OnFileCallback(void* pContext, int ipercent, float avgSpeed);

private:
	CGameMove();

	static void ShowMessage(int iLine, const CStdString& strDetail, const CStdString& strMore = "");
	bool ShowProgress(__int64 iDone);

	CGUIDialogProgress* m_dlgProgress;
	__int64 m_iTotal;    // Bytes to copy
	__int64 m_iDone;     // Bytes of the files finished so far
	__int64 m_iFileSize; // The file being copied
	CStdString m_strFile;
	DWORD m_dwStart;
};

#endif //H_CGAMEMOVE
