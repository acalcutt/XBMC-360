#ifndef H_CGAMECOPY
#define H_CGAMECOPY

#include "filesystem\File.h"
#include <vector>

class CFileItem;
class CGUIDialogProgress;

// Xbox 360 games and apps on a network share. The console only starts a game from its own drives, and 360 Riffle's
// share support is gone once the game starts, so the game's folder is copied to the console (special://temp/games/
// Xbox 360/) and started from there.
//
// The copy stays for next time: playing again copies only what's new on the share, never over a file the copy has.
// Saves a game keeps in its folder stay with the copy, and aren't written back to the share. Each copy lists the files
// that came from the share (360riffle-copy.txt), so that freeing a game's space deletes those and keeps the rest, its
// saves; playing it again copies them back. That's done by itself for the games played longest ago when a copy would
// leave less than 2 GB, and from System info > Games on the console, which also removes a copy with its saves.
class CGameCopy : public IFileCallback
{
public:
	// Copies the XEX's folder to the console, or what's new in it, and starts the copy
	static void Play(const CFileItem& item);
	// Whether the XEX's folder has a copy on the console (its game, or only its saves)
	static bool HasCopy(const CStdString& strXex);
	// What can be done with the XEX's copy: free its game's space, or remove it with its saves
	static void ManageCopy(const CStdString& strXex);
	// System info > Games on the console: every copy with its size, the emulators' games too, to free or remove
	static void ShowCopies();

	virtual bool OnFileCallback(void* pContext, int ipercent, float avgSpeed);

	// A copy on the console, as the list shows it
	struct Copy
	{
		CStdString strFolder;   // D:\cache\games\Xbox 360\<name>~<mark>, with a backslash at the end
		CStdString strName;     // The game's name
		__int64 iGameBytes;     // Its files from the share
		__int64 iOtherBytes;    // The rest, its saves
		unsigned __int64 iPlayed; // When it was last played, as a FILETIME
	};

private:
	struct GameFile
	{
		CStdString strFrom;
		CStdString strTo;
		__int64 iSize;
	};

	CGameCopy();

	static CStdString CopyFolder(const CStdString& strXex);
	static CStdString CopiesFolder();
	static void GetCopies(std::vector<Copy>& copies);
	static bool ReadCopy(const CStdString& strFolder, Copy& copy);
	static void FreeGameData(const CStdString& strFolder);
	static bool MakeRoom(__int64 iNeeded, const CStdString& strExcept);
	static bool ManageFolder(const Copy& copy);
	static void ShowMessage(int iLine, const CStdString& strDetail);

	bool AddFolder(const CStdString& strFrom, const CStdString& strTo);
	bool ShowProgress(__int64 iDone);

	std::vector<CStdString> m_folders; // To make, parents first
	std::vector<GameFile> m_files;     // To copy
	CGUIDialogProgress* m_dlgProgress;
	__int64 m_iTotal;    // Bytes to copy
	__int64 m_iDone;     // Bytes of the files finished so far
	__int64 m_iFileSize; // The file being copied
	CStdString m_strFile;
	DWORD m_dwStart;
};

#endif //H_CGAMECOPY
