#ifndef H_CUPDATER
#define H_CUPDATER

#include "filesystem\File.h"

class CGUIDialogProgress;

// Installs a build of 360 Riffle from a USB drive over this one, without the softmod's installer: the bundle staged
// for SoftmodExtras (<USB>\BadUpdatePayload\SoftmodExtras\360riffle), or a 360riffle folder at the top of the drive.
//
// Its files are copied to D:\update\ and checked first, so a build that can't be read whole changes nothing. Then
// its default.xex takes the place of ours and 360 Riffle starts again; the new build puts the rest in place as it
// starts, before anything of ours is open (the skin's textures and fonts are, while it runs).
class CUpdater : public IFileCallback
{
public:
	// From System info, on the GUI thread: finds a build, says which it is, and installs it if that's wanted
	static void UpdateFromUsb();
	// At start, before the splash, skin or language load: the rest of an update UpdateFromUsb staged goes in place
	static void FinishUpdate();

	virtual bool OnFileCallback(void* pContext, int ipercent, float avgSpeed);

private:
	CUpdater();

	static CStdString FindBuild();
	static void ShowMessage(int iLine, const CStdString& strDetail);

	bool CopyFolder(const CStdString& strFrom, const CStdString& strTo);
	bool ShowProgress(__int64 iDone);

	CGUIDialogProgress* m_dlgProgress;
	__int64 m_iTotal;       // Bytes to copy
	__int64 m_iDone;        // Bytes of the files finished so far
	__int64 m_iFileSize;    // The file being copied
	CStdString m_strFile;   // Its name, for the dialog
	CStdString m_strFailed; // What couldn't be copied
	DWORD m_dwStart;
};

#endif //H_CUPDATER
