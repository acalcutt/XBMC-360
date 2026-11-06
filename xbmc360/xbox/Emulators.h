#ifndef H_CEMULATORS
#define H_CEMULATORS

#include "filesystem\File.h"
#include <vector>

class CFileItem;
class CFileItemList;
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
	// Starts the game in its emulator, or says why it can't. A game in a zip is unzipped on the console first
	static void Play(const CFileItem& item);

	// A .zip whose system its folders don't say (and not in a BIOS or emulator folder): it opens like a folder, and
	// what's in it says what it is
	static bool IsUnknownZip(const CStdString& strPath);
	// A game's .zip with more than one game in it (several ROMs, or several discs and no .m3u), to open like a folder
	// and choose one
	static bool HoldsSeveralGames(const CStdString& strPath);
	// The zip:// folder a .zip opens as
	static CStdString ZipFolder(const CStdString& strZip);
	// A path of ours as it is on the console's drives, for another program: D:\ (and game:\) is where 360 Riffle
	// is, which another program would take for its own folder
	static CStdString ConsolePath(const CStdString& strPath);

	// The games copied to the console, a file each by its name (the files a .cue or .m3u names are part of it), for On
	// this console. They play from there without their share
	static void GetCopied(CFileItemList& items);
	// Whether a path is a game copied to the console
	static bool IsCopy(const CStdString& strPath);
	// Asks, and deletes a game copied to the console, with the files it names. True when it's gone
	static bool RemoveCopy(const CFileItem& item);
	// Moves a game copied to the console, with the files it names, to a drive or share (CGameMove): in a folder named
	// for its system where the place's folders don't say it. True when it's gone from the console
	static bool MoveCopy(const CFileItem& item);

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
	static CStdString MainEntry(const CStdString& strZip, int iSystem);
	static CStdString ForEmulator(const CStdString& strPath);
	static CStdString SafeName(const CStdString& strName, const CStdString& strFrom);
	static void ShowMessage(int iLine, const CStdString& strDetail);

	bool AddFile(const CStdString& strFrom, const CStdString& strFolder, CStdString& strName, int iDepth);
	bool AddList(const CStdString& strFrom, const CStdString& strFolder, CStdString& strName, int iDepth);
	bool CopyToConsole(const CStdString& strFrom, const CStdString& strLabel, int iSystem, CStdString& strGame);
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
