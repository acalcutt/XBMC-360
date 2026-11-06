#include "GameCopy.h"
#include "Application.h"
#include "FileItem.h"
#include "URL.h"
#include "filesystem\Directory.h"
#include "filesystem\SpecialProtocol.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\dialogs\GUIDialogOK.h"
#include "guilib\dialogs\GUIDialogProgress.h"
#include "guilib\dialogs\GUIDialogSelect.h"
#include "guilib\dialogs\GUIDialogYesNo.h"
#include "utils\DiscExport.h"
#include "utils\Log.h"
#include "utils\StringUtils.h"
#include "utils\TimeUtils.h"
#include "utils\URIUtils.h"
#include "utils\XEXUtils.h"
#include "xbox\Emulators.h"
#include "xbox\GameMove.h"
#include "xbox\TitleNames.h"
#include <algorithm>

using namespace XFILE;

// In each copy: its name, where it came from, the XEX it starts, and the files that came from there with their sizes
#define LIST_FILE "360riffle-copy.txt"

// The list's last line once everything from the share or disc is in the copy. A copy stopped part way, or one getting
// files that are new on its share, has its files' lines after it until it's complete again
#define LIST_DONE "done"

// Space a copy leaves on the drive, when the games played longest ago can make it
#define SPARE ((__int64)2 * 1024 * 1024 * 1024)

static bool IsDots(const char* name)
{
	return !strcmp(name, ".") || !strcmp(name, "..");
}

// A folder of the console's and everything in it (strFolder ends with a backslash)
static void DeleteTree(const CStdString& strFolder)
{
	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile((strFolder + "*").c_str(), &find);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (IsDots(find.cFileName))
				continue;

			CStdString strPath = strFolder + find.cFileName;
			if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				DeleteTree(strPath + "\\");
			else
				::DeleteFile(strPath.c_str());
		}
		while (FindNextFile(hFind, &find));
		FindClose(hFind);
	}

	CStdString strDir = strFolder;
	strDir.TrimRight('\\');
	::RemoveDirectory(strDir.c_str());
}

// The folders in a folder that are empty, and then those left empty by that (the folder itself stays)
static void RemoveEmptyFolders(const CStdString& strFolder)
{
	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile((strFolder + "*").c_str(), &find);
	if (hFind == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (!IsDots(find.cFileName) && (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			CStdString strSub = strFolder + find.cFileName;
			RemoveEmptyFolders(strSub + "\\");
			::RemoveDirectory(strSub.c_str()); // Only empty ones go
		}
	}
	while (FindNextFile(hFind, &find));
	FindClose(hFind);
}

// The bytes in a folder and the folders in it
static __int64 MeasureTree(const CStdString& strFolder)
{
	__int64 iSize = 0;
	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile((strFolder + "*").c_str(), &find);
	if (hFind == INVALID_HANDLE_VALUE)
		return 0;

	do
	{
		if (IsDots(find.cFileName))
			continue;

		if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			iSize += MeasureTree(strFolder + find.cFileName + "\\");
		else
			iSize += ((__int64)find.nFileSizeHigh << 32) | find.nFileSizeLow;
	}
	while (FindNextFile(hFind, &find));

	FindClose(hFind);
	return iSize;
}

// A file's size, and when it was last written; false when it isn't there
static bool GetFileInfo(const CStdString& strFile, __int64& iSize, unsigned __int64& iWritten)
{
	WIN32_FILE_ATTRIBUTE_DATA data;
	if (!GetFileAttributesEx(strFile.c_str(), GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		return false;

	iSize = ((__int64)data.nFileSizeHigh << 32) | data.nFileSizeLow;
	iWritten = ((unsigned __int64)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
	return true;
}

// The XEX at the top of a copy: default.xex, or failing that the first one there. Empty when there's none
static CStdString FindXex(const CStdString& strFolder)
{
	if (GetFileAttributes((strFolder + "default.xex").c_str()) != 0xFFFFFFFF)
		return "default.xex";

	CStdString strXex;
	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile((strFolder + "*.xex").c_str(), &find);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		strXex = find.cFileName;
		FindClose(hFind);
	}
	return strXex;
}

// A copy's list: its name, where it came from, the XEX it starts, and its files from there (path in the copy | size).
// Without files, only its first lines are read
static void ReadList(const CStdString& strFolder, CStdString& strName, CStdString& strFrom, CStdString& strXex, CStdStringArray* files)
{
	FILE* f = fopen((strFolder + LIST_FILE).c_str(), "r");
	if (f)
	{
		char line[1024];
		while (fgets(line, sizeof(line), f))
		{
			CStdString strLine = line;
			strLine.TrimRight("\r\n");
			if (strLine.Left(5).Equals("name="))
				strName = strLine.Mid(5);
			else if (strLine.Left(5).Equals("from="))
				strFrom = strLine.Mid(5);
			else if (strLine.Left(4).Equals("xex="))
				strXex = strLine.Mid(4);
			else if (strLine.Left(5).Equals("file="))
			{
				if (!files)
					break;
				files->push_back(strLine.Mid(5));
			}
		}
		fclose(f);
	}

	// A list from before the XEX was written in it
	if (strXex.IsEmpty())
		strXex = FindXex(strFolder);
}

static void WriteListHeader(const CStdString& strFolder, const CStdString& strName, const CStdString& strFrom, const CStdString& strXex)
{
	FILE* f = fopen((strFolder + LIST_FILE).c_str(), "w");
	if (!f)
		return;
	fprintf(f, "name=%s\r\nfrom=%s\r\nxex=%s\r\n", strName.c_str(), strFrom.c_str(), strXex.c_str());
	fclose(f);
}

static void AddListLine(const CStdString& strFolder, const char* line)
{
	FILE* f = fopen((strFolder + LIST_FILE).c_str(), "a");
	if (!f)
		return;
	fprintf(f, "%s\r\n", line);
	fclose(f);
}

// Whether everything from a copy's share or disc is in it: its list ends with LIST_DONE
static bool IsDone(const CStdString& strFolder)
{
	FILE* f = fopen((strFolder + LIST_FILE).c_str(), "rb");
	if (!f)
		return false;

	char tail[17];
	size_t iRead = 0;
	if (fseek(f, -16, SEEK_END) == 0)
		iRead = fread(tail, 1, 16, f);
	fclose(f);

	tail[iRead] = 0;
	CStdString strTail = tail;
	strTail.TrimRight("\r\n");
	return strTail.Right(5).Equals("\n" LIST_DONE);
}

// Whether a copy starts as it is, without its share or disc: complete, with its XEX
static bool IsPlayable(const CStdString& strFolder, const CStdString& strXex)
{
	return !strXex.IsEmpty() && IsDone(strFolder) && GetFileAttributes((strFolder + strXex).c_str()) != 0xFFFFFFFF;
}

// Marks a copy as played now: its list's time is when it was last played
static void MarkPlayed(const CStdString& strFolder)
{
	HANDLE hFile = CreateFile((strFolder + LIST_FILE).c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return;

	FILETIME now;
	GetSystemTimeAsFileTime(&now);
	SetFileTime(hFile, NULL, NULL, &now);
	CloseHandle(hFile);
}

// Starts a copy's XEX, by its path on the console's drives: D:\ is ours only
static void StartCopy(const CStdString& strCopy, const CStdString& strXex, const CStdString& strName)
{
	MarkPlayed(strCopy);

	CStdString strRun = CEmulators::ConsolePath(strCopy + strXex);
	CLog::Log(LOGNOTICE, "Starting %s from its copy on the console, %s", strName.c_str(), strRun.c_str());
	CXEXUtils::RunXex(strRun);
}

CGameCopy::CGameCopy()
{
	m_dlgProgress = NULL;
	m_iTotal = 0;
	m_iDone = 0;
	m_iFileSize = 0;
	m_dwStart = 0;
}

// D:\cache\games\Xbox 360\, the copies' folder
CStdString CGameCopy::CopiesFolder()
{
	CStdString strCache = URIUtils::AddFileToFolder(CSpecialProtocol::TranslatePath("special://temp/"), "games");
	return URIUtils::AddFileToFolder(strCache, "Xbox 360") + "\\";
}

// Where the copy of a game's folder goes: in the copies' folder under its folder's name and a mark of where it came
// from, so that two games' folders of the same name stay apart. As a FATX name: 42 characters at most, and fewer
// kinds of character than a share takes. Ends with a backslash
CStdString CGameCopy::CopyFolder(const CStdString& strXex)
{
	CStdString strFolder;
	URIUtils::GetDirectory(strXex, strFolder);

	CStdString strName = strFolder;
	URIUtils::RemoveSlashAtEnd(strName);
	strName = URIUtils::GetFileName(strName);

	CStdString strSafe;
	for (unsigned int i = 0; i < strName.size(); i++)
	{
		unsigned char c = strName[i];
		strSafe += (isalnum(c) || strchr(" !#$%&'()-.@[]^_`{}~", c)) ? (char)c : '_';
	}

	CStdString strWhere = CURL(strFolder).GetWithoutUserDetails();
	DWORD dwHash = 5381;
	for (unsigned int i = 0; i < strWhere.size(); i++)
		dwHash = dwHash * 33 + (unsigned char)tolower(strWhere[i]);

	CStdString strMark;
	strMark.Format("~%08X", dwHash);
	strSafe = strSafe.Left(42 - strMark.size());
	strSafe.TrimRight(" .");

	return CopiesFolder() + strSafe + strMark + "\\";
}

bool CGameCopy::HasCopy(const CStdString& strXex)
{
	CStdString strFolder = CopyFolder(strXex);
	strFolder.TrimRight('\\');
	return GetFileAttributes(strFolder.c_str()) != 0xFFFFFFFF;
}

bool CGameCopy::IsCopy(const CStdString& strPath)
{
	CStdString strCopies = CopiesFolder();
	return strPath.size() > strCopies.size() && strPath.Left(strCopies.size()).Equals(strCopies);
}

// The copy a path in the copies' folder is in, with a backslash at the end
CStdString CGameCopy::CopyOf(const CStdString& strPath)
{
	int iSlash = strPath.Find('\\', CopiesFolder().size());
	return iSlash > 0 ? strPath.Left(iSlash + 1) : strPath + "\\";
}

void CGameCopy::GetPlayable(CFileItemList& items)
{
	CStdString strCopies = CopiesFolder();
	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile((strCopies + "*").c_str(), &find);
	if (hFind == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (IsDots(find.cFileName) || !(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;

		CStdString strFolder = strCopies + find.cFileName + "\\";
		CStdString strName, strFrom, strXex;
		ReadList(strFolder, strName, strFrom, strXex, NULL);
		if (!IsPlayable(strFolder, strXex))
			continue;

		// Named for its title ID (a disc without a label, or a share's folder named so): its game's name once known
		CFileItemPtr item(new CFileItem(CTitleNames::NameFor(strName)));
		item->SetPath(strFolder + strXex);
		item->SetLabel2("Xbox 360");
		item->SetLabelPreformated(true);
		item->SetIconImage("DefaultProgram.png");

		// When it was last played: its list's time
		__int64 iListSize;
		unsigned __int64 iPlayed;
		if (GetFileInfo(strFolder + LIST_FILE, iListSize, iPlayed))
		{
			FILETIME played;
			played.dwLowDateTime = (DWORD)iPlayed;
			played.dwHighDateTime = (DWORD)(iPlayed >> 32);
			item->m_dateTime = played;
		}
		items.Add(item);
	}
	while (FindNextFile(hFind, &find));
	FindClose(hFind);
}

void CGameCopy::PlayCopy(const CStdString& strXex)
{
	CStdString strCopy = CopyOf(strXex);
	CStdString strName, strFrom, strListed;
	ReadList(strCopy, strName, strFrom, strListed, NULL);
	StartCopy(strCopy, strXex.Mid(strCopy.size()), strName);
}

// A copy as the list shows it: its game's files that are still as they came (the rest is its saves), and when it
// was played
bool CGameCopy::ReadCopy(const CStdString& strFolder, Copy& copy)
{
	copy.strFolder = strFolder;
	copy.iGameBytes = 0;
	copy.iOtherBytes = 0;
	copy.iPlayed = 0;

	CStdString strFrom;
	CStdStringArray files;
	ReadList(strFolder, copy.strName, strFrom, copy.strXex, &files);
	copy.bPlayable = IsPlayable(strFolder, copy.strXex);

	for (unsigned int i = 0; i < files.size(); i++)
	{
		int iBar = files[i].ReverseFind('|');
		__int64 iSize;
		unsigned __int64 iWritten;
		if (iBar > 0 && GetFileInfo(strFolder + files[i].Left(iBar), iSize, iWritten) && iSize == _atoi64(files[i].Mid(iBar + 1).c_str()))
			copy.iGameBytes += iSize;
	}

	__int64 iListSize = 0;
	GetFileInfo(strFolder + LIST_FILE, iListSize, copy.iPlayed);
	copy.iOtherBytes = MeasureTree(strFolder) - copy.iGameBytes - iListSize;
	if (copy.iOtherBytes < 0)
		copy.iOtherBytes = 0;

	// A copy without its list goes by its folder's name, less the mark
	if (copy.strName.IsEmpty())
	{
		copy.strName = strFolder;
		URIUtils::RemoveSlashAtEnd(copy.strName);
		copy.strName = URIUtils::GetFileName(copy.strName);
		int iMark = copy.strName.ReverseFind('~');
		if (iMark > 0)
			copy.strName = copy.strName.Left(iMark);
	}

	copy.strName = CTitleNames::NameFor(copy.strName);
	return true;
}

static bool PlayedLater(const CGameCopy::Copy& a, const CGameCopy::Copy& b)
{
	return a.iPlayed > b.iPlayed;
}

// Every copy, the one played last first
void CGameCopy::GetCopies(std::vector<Copy>& copies)
{
	CStdString strCopies = CopiesFolder();
	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile((strCopies + "*").c_str(), &find);
	if (hFind == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (IsDots(find.cFileName) || !(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;

		Copy copy;
		if (ReadCopy(strCopies + find.cFileName + "\\", copy))
			copies.push_back(copy);
	}
	while (FindNextFile(hFind, &find));
	FindClose(hFind);

	std::sort(copies.begin(), copies.end(), PlayedLater);
}

// Deletes a copy's files from the share that are still as they came, keeping the rest: its saves. Playing it again
// copies them back. A copy left with nothing goes
void CGameCopy::FreeGameData(const CStdString& strFolder)
{
	CStdString strName, strFrom, strXex;
	CStdStringArray files;
	ReadList(strFolder, strName, strFrom, strXex, &files);

	for (unsigned int i = 0; i < files.size(); i++)
	{
		int iBar = files[i].ReverseFind('|');
		__int64 iSize;
		unsigned __int64 iWritten;
		CStdString strFile = strFolder + files[i].Left(iBar);
		if (iBar > 0 && GetFileInfo(strFile, iSize, iWritten) && iSize == _atoi64(files[i].Mid(iBar + 1).c_str()))
			::DeleteFile(strFile.c_str());
	}

	RemoveEmptyFolders(strFolder);
	WriteListHeader(strFolder, strName, strFrom, strXex);

	__int64 iListSize = 0;
	unsigned __int64 iWritten;
	GetFileInfo(strFolder + LIST_FILE, iListSize, iWritten);
	if (MeasureTree(strFolder) <= iListSize)
		DeleteTree(strFolder);
}

// Room on the drive for iNeeded more bytes, leaving SPARE: the game files of the copies played longest ago go
// first (their saves stay). False when even then there isn't room for iNeeded
bool CGameCopy::MakeRoom(__int64 iNeeded, const CStdString& strExcept)
{
	ULARGE_INTEGER free;
	if (!GetDiskFreeSpaceEx(CopiesFolder().c_str(), &free, NULL, NULL) && !GetDiskFreeSpaceEx("D:\\", &free, NULL, NULL))
		return true; // Can't tell: try

	std::vector<Copy> copies;
	GetCopies(copies);

	for (int i = (int)copies.size() - 1; i >= 0 && (__int64)free.QuadPart < iNeeded + SPARE; i--)
	{
		if (copies[i].strFolder.Equals(strExcept) || copies[i].iGameBytes == 0)
			continue;

		CLog::Log(LOGNOTICE, "Making room: freeing %s of %s, played longest ago", CStringUtils::SizeToString(copies[i].iGameBytes).c_str(),
			copies[i].strName.c_str());
		FreeGameData(copies[i].strFolder);
		GetDiskFreeSpaceEx("D:\\", &free, NULL, NULL);
	}

	return (__int64)free.QuadPart >= iNeeded + 64 * 1024 * 1024;
}

void CGameCopy::ShowMessage(int iLine, const CStdString& strDetail)
{
	CStdString strLine;
	strLine.Format(g_localizeStrings.Get(iLine).c_str(), strDetail.c_str());

	CGUIDialogOK* dialog = (CGUIDialogOK*)g_windowManager.GetWindow(WINDOW_DIALOG_OK);
	if (dialog)
	{
		dialog->SetHeading(33560);
		dialog->SetLine(0, strLine);
		dialog->SetLine(1, "");
		dialog->SetLine(2, "");
		dialog->DoModal();
	}
}

// What to do with a copy: play it as it is, move it to a drive or share, free its game's space and keep its saves, or
// remove it, saves and all. True when it changed
bool CGameCopy::ManageFolder(const Copy& copy)
{
	CGUIDialogSelect* select = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
	if (!select)
		return false;

	CStdString strFree;
	strFree.Format(g_localizeStrings.Get(33583).c_str(), CStringUtils::SizeToString(copy.iGameBytes).c_str());

	select->Reset();
	select->SetHeading(copy.strName);

	int iOptions = 0, iPlay = -1, iMove = -1, iFree = -1;
	if (copy.bPlayable)
	{
		select->Add(g_localizeStrings.Get(208));
		iPlay = iOptions++;
		select->Add(g_localizeStrings.Get(33597));
		iMove = iOptions++;
	}
	if (copy.iGameBytes > 0)
	{
		select->Add(strFree);
		iFree = iOptions++;
	}
	select->Add(g_localizeStrings.Get(33584));
	select->DoModal();

	int iChosen = select->GetSelectedLabel();
	if (iChosen < 0)
		return false;

	if (iChosen == iPlay)
	{
		StartCopy(copy.strFolder, copy.strXex, copy.strName);
		return false;
	}

	if (iChosen == iMove)
		return MoveFolder(copy);

	if (iChosen == iFree)
	{
		CLog::Log(LOGNOTICE, "Freeing the game files of the console's copy of %s, %s", copy.strName.c_str(), copy.strFolder.c_str());
		FreeGameData(copy.strFolder);
	}
	else
	{
		CLog::Log(LOGNOTICE, "Removing the console's copy of %s, %s", copy.strName.c_str(), copy.strFolder.c_str());
		DeleteTree(copy.strFolder);
	}
	return true;
}

bool CGameCopy::ManageCopy(const CStdString& strXex)
{
	Copy copy;
	return ReadCopy(IsCopy(strXex) ? CopyOf(strXex) : CopyFolder(strXex), copy) && ManageFolder(copy);
}

bool CGameCopy::MoveCopy(const CStdString& strXex)
{
	Copy copy;
	return ReadCopy(IsCopy(strXex) ? CopyOf(strXex) : CopyFolder(strXex), copy) && copy.bPlayable && MoveFolder(copy);
}

// A complete copy, game and saves, to a folder named for its game wherever it's wanted (not its list, which is the
// console's), and then off the console
bool CGameCopy::MoveFolder(const Copy& copy)
{
	CStdString strPlace;
	if (!CGameMove::ChoosePlace(copy.strName, strPlace))
		return false;

	std::vector<CStdString> files, folders;
	CGameMove::ListFolder(copy.strFolder, LIST_FILE, files, folders);

	CStdString strTo = URIUtils::AddFileToFolder(strPlace, CDiscExport::MakeSafeName(copy.strName));
	if (!CGameMove::Move(copy.strName, copy.strFolder, files, folders, strTo, true))
		return false;

	DeleteTree(copy.strFolder);
	return true;
}

// The emulators' games copied to the console: the files in the other folders beside the copies' (one per system).
// Deleted too when bDelete
static int EmulatorGames(__int64& iBytes, bool bDelete)
{
	CStdString strGames = CSpecialProtocol::TranslatePath("special://temp/");
	strGames = URIUtils::AddFileToFolder(strGames, "games") + "\\";

	int iCount = 0;
	iBytes = 0;

	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile((strGames + "*").c_str(), &find);
	if (hFind == INVALID_HANDLE_VALUE)
		return 0;

	do
	{
		if (IsDots(find.cFileName) || !(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !_stricmp(find.cFileName, "Xbox 360"))
			continue;

		CStdString strSystem = strGames + find.cFileName + "\\";
		WIN32_FIND_DATA game;
		HANDLE hGame = FindFirstFile((strSystem + "*").c_str(), &game);
		if (hGame == INVALID_HANDLE_VALUE)
			continue;

		do
		{
			if (game.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			iCount++;
			iBytes += ((__int64)game.nFileSizeHigh << 32) | game.nFileSizeLow;
			if (bDelete)
				::DeleteFile((strSystem + game.cFileName).c_str());
		}
		while (FindNextFile(hGame, &game));
		FindClose(hGame);
	}
	while (FindNextFile(hFind, &find));

	FindClose(hFind);
	return iCount;
}

void CGameCopy::ShowCopies()
{
	CGUIDialogSelect* select = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
	if (!select)
		return;

	for (;;)
	{
		std::vector<Copy> copies;
		GetCopies(copies);
		__int64 iRomBytes = 0;
		int iRoms = EmulatorGames(iRomBytes, false);

		if (copies.empty() && iRoms == 0)
		{
			CGUIDialogOK::ShowAndGetInput(33580, 33586, 0, 0);
			return;
		}

		select->Reset();
		select->SetHeading(33580);

		// Remove all, first, when there's more than one thing to remove
		__int64 iAllGames = iRomBytes, iAll = iRomBytes;
		for (unsigned int i = 0; i < copies.size(); i++)
		{
			iAllGames += copies[i].iGameBytes;
			iAll += copies[i].iGameBytes + copies[i].iOtherBytes;
		}
		int iFirst = 0;
		if (copies.size() + (iRoms > 0 ? 1 : 0) > 1)
		{
			select->Add(g_localizeStrings.Get(33607) + ": " + CStringUtils::SizeToString(iAll));
			iFirst = 1;
		}

		for (unsigned int i = 0; i < copies.size(); i++)
		{
			CStdString strLine = copies[i].strName + ": ";
			CStdString strSaves;
			strSaves.Format(g_localizeStrings.Get(33581).c_str(), CStringUtils::SizeToString(copies[i].iOtherBytes).c_str());
			if (copies[i].iGameBytes > 0)
				strLine += CStringUtils::SizeToString(copies[i].iGameBytes) + (copies[i].iOtherBytes > 0 ? ", " + strSaves : "");
			else
				strLine += strSaves;
			select->Add(strLine);
		}
		if (iRoms > 0)
		{
			CStdString strRoms;
			strRoms.Format(g_localizeStrings.Get(33582).c_str(), iRoms);
			select->Add(strRoms + ": " + CStringUtils::SizeToString(iRomBytes));
		}
		select->DoModal();

		int iChosen = select->GetSelectedLabel();
		if (iChosen < 0)
			return;

		if (iChosen < iFirst)
		{
			RemoveAll(copies, iAllGames, iRoms);
			continue;
		}
		iChosen -= iFirst;

		if (iChosen < (int)copies.size())
		{
			ManageFolder(copies[iChosen]);
			continue;
		}

		CGUIDialogYesNo* dialog = (CGUIDialogYesNo*)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
		if (!dialog)
			return;

		CStdString strQuestion;
		strQuestion.Format(g_localizeStrings.Get(33585).c_str(), iRoms);
		dialog->SetHeading(33580);
		dialog->SetLine(0, strQuestion);
		dialog->SetLine(1, "");
		dialog->SetLine(2, "");
		dialog->DoModal();
		if (dialog->IsConfirmed())
		{
			CLog::Log(LOGNOTICE, "Removing the %d emulator games copied to the console, %s", iRoms, CStringUtils::SizeToString(iRomBytes).c_str());
			EmulatorGames(iRomBytes, true);
		}
	}
}

// Remove all: every copy's game files, keeping their saves, or every copy with its saves (which asks again). The
// emulators' games go either way, as they keep no saves there. iGameBytes: what freeing them all frees
void CGameCopy::RemoveAll(const std::vector<Copy>& copies, __int64 iGameBytes, int iRoms)
{
	CGUIDialogSelect* select = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
	if (!select)
		return;

	CStdString strFree;
	strFree.Format(g_localizeStrings.Get(33608).c_str(), CStringUtils::SizeToString(iGameBytes).c_str());

	select->Reset();
	select->SetHeading(33607);
	select->Add(strFree);
	select->Add(g_localizeStrings.Get(33609));
	select->DoModal();

	int iChosen = select->GetSelectedLabel();
	if (iChosen < 0)
		return;

	bool bSaves = iChosen == 1;
	if (bSaves)
	{
		CGUIDialogYesNo* dialog = (CGUIDialogYesNo*)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
		if (!dialog)
			return;

		CStdString strQuestion;
		strQuestion.Format(g_localizeStrings.Get(33610).c_str(), (int)copies.size() + iRoms);
		dialog->SetHeading(33580);
		dialog->SetLine(0, strQuestion);
		dialog->SetLine(1, g_localizeStrings.Get(33611));
		dialog->SetLine(2, "");
		dialog->DoModal();
		if (!dialog->IsConfirmed())
			return;
	}

	CLog::Log(LOGNOTICE, "%s the %u game copies on the console and its %d emulator games", bSaves ? "Removing" : "Freeing",
		copies.size(), iRoms);
	for (unsigned int i = 0; i < copies.size(); i++)
	{
		if (bSaves)
			DeleteTree(copies[i].strFolder);
		else if (copies[i].iGameBytes > 0)
			FreeGameData(copies[i].strFolder);
	}

	__int64 iRomBytes;
	EmulatorGames(iRomBytes, true);
}

// What's in a folder on the share that its copy doesn't have yet: the folders to make and the files to copy. A file
// the copy has stays as it is, a save the game made perhaps. False when the share's folder can't be read
bool CGameCopy::AddFolder(const CStdString& strFrom, const CStdString& strTo)
{
	m_folders.push_back(strTo);

	CStdString strName = strFrom;
	URIUtils::RemoveSlashAtEnd(strName);
	m_dlgProgress->SetLine(1, URIUtils::GetFileName(strName));
	m_dlgProgress->Progress();
	if (m_dlgProgress->IsCanceled())
		return false;

	CFileItemList items;
	if (!CDirectory::GetDirectory(strFrom, items, "", false))
	{
		CLog::Log(LOGERROR, "%s - couldn't list %s", __FUNCTION__, CURL(strFrom).GetWithoutUserDetails().c_str());
		return false;
	}

	for (int i = 0; i < items.Size(); i++)
	{
		CFileItemPtr pItem = items[i];
		if (pItem->IsParentFolder())
			continue;

		CStdString strTarget = strTo + pItem->GetLabel();
		if (pItem->m_bIsFolder)
		{
			if (!AddFolder(pItem->GetPath(), strTarget + "\\"))
				return false;
			continue;
		}

		if (GetFileAttributes(strTarget.c_str()) != 0xFFFFFFFF)
			continue;

		GameFile file;
		file.strFrom = pItem->GetPath();
		file.strTo = strTarget;
		file.iSize = pItem->m_dwSize;
		m_files.push_back(file);
		m_iTotal += file.iSize;
	}

	return true;
}

void CGameCopy::Play(const CFileItem& item)
{
	CStdString strXex = item.GetPath();
	CStdString strFolder;
	URIUtils::GetDirectory(strXex, strFolder);

	CStdString strName = item.GetLabel();
	if (URIUtils::GetExtension(strName).Equals(".xex"))
		URIUtils::RemoveExtension(strName);

	CopyAndStart(strFolder, URIUtils::GetFileName(strXex), CopyFolder(strXex), strName);
}

// A copy of this disc of the game with this title ID: its own (<title ID>~disc<n>), or for a game's first disc a
// share's copy of a folder named for the title ID, as Copy disc to a folder names a disc without a label (other discs
// of a game get "(Disc 2 of 2)" after it). The copies' folder, or empty
CStdString CGameCopy::FindTitle(DWORD dwTitleId, int iDisc)
{
	CStdString strId, strDisc;
	strId.Format("%08X", dwTitleId);
	strDisc.Format("%s~disc%d", strId.c_str(), iDisc > 0 ? iDisc : 1);

	CStdString strCopies = CopiesFolder();
	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile((strCopies + strId + "*").c_str(), &find);
	if (hFind == INVALID_HANDLE_VALUE)
		return "";

	CStdString strFound;
	do
	{
		CStdString strName = find.cFileName;
		if (!(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;

		bool bShare = strName.Equals(strId) || (strName.Left(9).Equals(strId + "~") && !strName.Mid(9, 4).Equals("disc"));
		if (strName.Equals(strDisc) || (bShare && iDisc <= 1))
			strFound = strCopies + strName + "\\";
	}
	while (strFound.IsEmpty() && FindNextFile(hFind, &find));

	FindClose(hFind);
	return strFound;
}

void CGameCopy::PlayDisc(const CStdString& strName, DWORD dwTitleId, int iDisc)
{
	CStdString strCopy = FindTitle(dwTitleId, iDisc);
	if (strCopy.IsEmpty())
		strCopy.Format("%s%08X~disc%d\\", CopiesFolder().c_str(), dwTitleId, iDisc > 0 ? iDisc : 1);

	// A disc's files don't change: a complete copy starts without the disc being read again
	if (IsPlayable(strCopy, "default.xex"))
	{
		StartCopy(strCopy, "default.xex", strName);
		return;
	}

	CopyAndStart("Dvd:\\", "default.xex", strCopy, strName);
}

bool CGameCopy::HasDiscCopy(DWORD dwTitleId, int iDisc)
{
	CStdString strCopy = FindTitle(dwTitleId, iDisc);
	return !strCopy.IsEmpty() && GetFileAttributes((strCopy + "default.xex").c_str()) != 0xFFFFFFFF;
}

// Copies what strCopy doesn't have of strFolder, the game's folder, and starts strXex from the copy
void CGameCopy::CopyAndStart(const CStdString& strFolder, const CStdString& strXex, const CStdString& strCopy, const CStdString& strName)
{
	CGameCopy job;
	job.m_dlgProgress = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
	job.m_dlgProgress->SetHeading(33563);
	job.m_dlgProgress->SetLine(0, g_localizeStrings.Get(33579));
	job.m_dlgProgress->SetLine(1, "");
	job.m_dlgProgress->SetLine(2, "");
	job.m_dlgProgress->SetPercentage(0);
	job.m_dlgProgress->StartModal();
	job.m_dlgProgress->ShowProgressBar(false);
	job.m_dlgProgress->SetCanCancel(true);
	job.m_dlgProgress->Progress();

	// What the copy doesn't have yet: everything, the first time
	if (!job.AddFolder(strFolder, strCopy))
	{
		bool bStopped = job.m_dlgProgress->IsCanceled();
		job.m_dlgProgress->Close();
		if (bStopped)
			return;

		// Its share can't be reached, or the disc read: a complete copy starts as it is
		if (IsPlayable(strCopy, strXex))
		{
			CLog::Log(LOGNOTICE, "Couldn't look in %s for what's new: starting the copy as it is", CURL(strFolder).GetWithoutUserDetails().c_str());
			StartCopy(strCopy, strXex, strName);
			return;
		}

		ShowMessage(33564, strName);
		return;
	}

	if (!job.m_files.empty())
	{
		if (job.m_iTotal > 0 && !MakeRoom(job.m_iTotal, strCopy))
		{
			job.m_dlgProgress->Close();
			ShowMessage(33565, CStringUtils::SizeToString(job.m_iTotal));
			return;
		}

		CLog::Log(LOGNOTICE, "Copying %s to the console, %u files, %s", CURL(strFolder).GetWithoutUserDetails().c_str(),
			job.m_files.size(), CStringUtils::SizeToString(job.m_iTotal).c_str());

		// Its folder and the ones it's in (D:\cache\games\Xbox 360), then the game's own
		for (int iSlash = strCopy.Find('\\', 3); iSlash > 0; iSlash = strCopy.Find('\\', iSlash + 1))
			::CreateDirectory(strCopy.Left(iSlash).c_str(), NULL);
		for (unsigned int i = 0; i < job.m_folders.size(); i++)
			::CreateDirectory(job.m_folders[i].Left(job.m_folders[i].size() - 1).c_str(), NULL);

		// Its list: what it is, then each file as it's copied, then that it's complete
		if (GetFileAttributes((strCopy + LIST_FILE).c_str()) == 0xFFFFFFFF)
			WriteListHeader(strCopy, strName, CURL(strFolder).GetWithoutUserDetails(), strXex);
		FILE* list = fopen((strCopy + LIST_FILE).c_str(), "a");

		job.m_dwStart = CTimeUtils::timeGetTime();
		job.m_dlgProgress->ShowProgressBar(true);

		for (unsigned int i = 0; i < job.m_files.size(); i++)
		{
			const GameFile& file = job.m_files[i];
			job.m_strFile = URIUtils::GetFileName(file.strTo);
			job.m_iFileSize = file.iSize;

			if (!job.ShowProgress(job.m_iDone) || !CFile::Cache(file.strFrom, file.strTo, &job))
			{
				// What was half copied goes; the rest stays for next time, which copies only what's missing
				::DeleteFile(file.strTo.c_str());
				if (list)
					fclose(list);
				bool bStopped = job.m_dlgProgress->IsCanceled();
				job.m_dlgProgress->Close();

				CLog::Log(bStopped ? LOGNOTICE : LOGERROR, "%s %s after %s", bStopped ? "Stopped copying" : "Couldn't copy",
					CURL(file.strFrom).GetWithoutUserDetails().c_str(), CStringUtils::SizeToString(job.m_iDone).c_str());
				if (!bStopped)
					ShowMessage(33564, job.m_strFile);
				return;
			}

			if (list)
				fprintf(list, "file=%s|%I64d\r\n", file.strTo.Mid(strCopy.size()).c_str(), file.iSize);
			job.m_iDone += file.iSize;
		}

		if (list)
		{
			fprintf(list, "%s\r\n", LIST_DONE);
			fclose(list);
		}
		CLog::Log(LOGNOTICE, "Copied it in %u seconds", (CTimeUtils::timeGetTime() - job.m_dwStart) / 1000);
	}
	else if (GetFileAttributes((strCopy + LIST_FILE).c_str()) != 0xFFFFFFFF && !IsDone(strCopy))
	{
		// Nothing missing: a copy from before lists said when they were complete
		AddListLine(strCopy, LIST_DONE);
	}

	job.m_dlgProgress->Close();
	StartCopy(strCopy, strXex, strName);
}

// Shows how far the copy's got. False once it's been cancelled
bool CGameCopy::ShowProgress(__int64 iDone)
{
	CStdString strDone;
	strDone.Format(g_localizeStrings.Get(13227).c_str(), CStringUtils::SizeToString(iDone).c_str(), CStringUtils::SizeToString(m_iTotal).c_str());

	DWORD dwSeconds = (CTimeUtils::timeGetTime() - m_dwStart) / 1000;
	if (dwSeconds > 0)
		strDone += "  (" + CStringUtils::SizeToString(iDone / dwSeconds) + "/s)";

	m_dlgProgress->SetLine(0, m_strFile);
	m_dlgProgress->SetLine(1, strDone);
	m_dlgProgress->SetPercentage(m_iTotal > 0 ? (int)(iDone * 100 / m_iTotal) : 0);
	m_dlgProgress->Progress();

	return !m_dlgProgress->IsCanceled();
}

bool CGameCopy::OnFileCallback(void* pContext, int ipercent, float avgSpeed)
{
	return ShowProgress(m_iDone + m_iFileSize * ipercent / 100);
}
