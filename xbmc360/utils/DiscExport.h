#ifndef H_CDISCEXPORT
#define H_CDISCEXPORT

#include "filesystem\File.h"
#include "xbox\DiscDrive.h"

class CGUIDialogProgress;

// Keeps what's on the disc in the drive: its files copied to a folder on
// the console's drives or a network share
class CDiscExport : public IFileCallback
{
public:
	static void CopyFiles();

	virtual bool OnFileCallback(void* pContext, int ipercent, float avgSpeed);

private:
	CDiscExport();

	static bool ChooseDestination(const CDiscDrive::Info& disc, int iHeading, CStdString& strFolder, CStdString& strName);
	static CStdString MakeSafeName(const CStdString& strName);
	static __int64 MeasureFolder(const CStdString& strFolder);
	static void ShowResult(int iHeading, int iLine, const CStdString& strPath);

	void StartProgress(int iHeading, __int64 iTotal);
	bool ShowProgress(__int64 iDone, const CStdString& strLine);
	bool CopyFolder(const CStdString& strFrom, const CStdString& strTo);

	CGUIDialogProgress* m_dlgProgress;
	__int64 m_iTotal;       // Bytes to copy, 0 when not known
	__int64 m_iDone;        // Bytes of the files finished so far
	__int64 m_iFileSize;    // The file being copied
	CStdString m_strFile;   // Its name, for the dialog
	CStdString m_strFailed; // What couldn't be copied
	DWORD m_dwStart;        // When copying started, for the speed
};

#endif //H_CDISCEXPORT
