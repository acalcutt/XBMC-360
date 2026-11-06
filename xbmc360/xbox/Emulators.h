#ifndef H_CEMULATORS
#define H_CEMULATORS

#include "filesystem\File.h"
#include <vector>

class CFileItem;
class CGUIDialogProgress;

// Games for other consoles, played in emulators that run on the Xbox 360.
// Those are Salvia's libretro cores (github.com/danibiker/Salvia): an XEX
// each, which takes the game's path as launch data. They read the
// console's own drives only, so a game on a network share is copied to
// the console first, and kept there for next time.
class CEmulators : public IFileCallback
{
public:
	// The system a file is a game for, from its extension or, for
	// extensions many systems use (.zip, .bin, .iso...), the folders it's
	// in. -1 when it isn't a game
	static int GetSystem(const CStdString& strPath);
	static CStdString GetSystemName(int iSystem);
	// Every extension a game can have, for a file mask
	static CStdString GetExtensions();
	// A game in a listing: its name without the extension, and its system
	static void SetLabels(CFileItem& item);
	// Starts the game in its emulator, or says why it can't
	static void Play(const CFileItem& item);

	virtual bool OnFileCallback(void* pContext, int ipercent, float avgSpeed);

private:
	struct GameFile
	{
		CStdString strFrom;
		CStdString strTo;
		__int64 iSize;
	};

	CEmulators();

	static CStdString FindCore(int iSystem);
	static CStdString ConsolePath(const CStdString& strPath);
	static CStdString ForEmulator(const CStdString& strPath);
	static CStdString SafeName(const CStdString& strName, const CStdString& strFrom);
	static void ShowMessage(int iLine, const CStdString& strDetail);

	bool AddFile(const CStdString& strFrom, const CStdString& strFolder, CStdString& strName, int iDepth);
	bool AddList(const CStdString& strFrom, const CStdString& strFolder, CStdString& strName, int iDepth);
	bool CopyToConsole(const CFileItem& item, int iSystem, CStdString& strGame);
	void MakeRoom(const CStdString& strCache, __int64 iNeeded);
	bool ShowProgress(__int64 iDone);

	std::vector<GameFile> m_files;
	std::vector<std::pair<CStdString, CStdString> > m_lists; // Rewritten .cue and .m3u files: where, and what's in them
	CGUIDialogProgress* m_dlgProgress;
	__int64 m_iTotal;  // Bytes to copy
	__int64 m_iDone;   // Bytes of the files finished so far
	__int64 m_iFileSize;
	CStdString m_strFile;
	DWORD m_dwStart;
};

#endif //H_CEMULATORS
