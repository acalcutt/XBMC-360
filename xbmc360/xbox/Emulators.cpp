#include "Emulators.h"
#include "Application.h"
#include "FileItem.h"
#include "URL.h"
#include "filesystem\Directory.h"
#include "filesystem\SpecialProtocol.h"
#include "filesystem\ZipManager.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\dialogs\GUIDialogOK.h"
#include "guilib\dialogs\GUIDialogProgress.h"
#include "guilib\dialogs\GUIDialogYesNo.h"
#include "utils\Log.h"
#include "utils\StringUtils.h"
#include "utils\TimeUtils.h"
#include "utils\URIUtils.h"
#include "xbox\XBKernalExports.h"
#include <algorithm>

using namespace XFILE;

// Games copied from shares stay on the console for next time, until there
// are this many bytes of them: then the ones copied longest ago go
#define CACHE_LIMIT (8 * (__int64)1024 * 1024 * 1024)

struct GameSystem
{
	const char* name;       // As the system is known
	const char* extensions; // Its own: a file with one of these is its game, wherever it is
	const char* shared;     // Ones other systems or other files use too (.zip, .bin, .md for a README):
	                        // its game when a folder it's in is named for it
	const char* folders;    // Folder names for it, in lower case letters and digits only
	const char* cores;      // Salvia's cores that play it, best first; none when no emulator for it runs on the 360
};

static const GameSystem SYSTEMS[] =
{
	{ "NES", "|.nes|.fds|.unf|.unif|", "|.zip|", "|nes|famicom|nintendoentertainmentsystem|famicomdisksystem|fds|", "|Nestopia|" },
	{ "SNES", "|.sfc|.smc|.fig|.swc|.gd3|.gd7|.dx2|.bsx|", "|.zip|", "|snes|superfamicom|supernintendo|supernes|sfc|", "|Snes9x2010|Snes9x|" },
	{ "Genesis", "|.gen|.smd|.mdx|.68k|.sgd|", "|.zip|.bin|.md|", "|genesis|megadrive|segagenesis|segamegadrive|md|", "|Genesis-Plus-GX|picodrive|" },
	{ "Master System", "|.sms|.sg|.bms|", "|.zip|", "|mastersystem|segamastersystem|sms|sg1000|", "|Genesis-Plus-GX|picodrive|" },
	{ "Game Gear", "|.gg|", "|.zip|", "|gamegear|segagamegear|gg|", "|Genesis-Plus-GX|" },
	{ "Sega CD", "", "|.cue|.chd|.iso|.m3u|", "|segacd|megacd|", "|Genesis-Plus-GX|picodrive|" },
	{ "32X", "|.32x|", "|.zip|", "|32x|sega32x|", "|picodrive|" },
	{ "Game Boy", "|.gb|", "|.zip|.dmg|", "|gb|gameboy|", "|Gambatte|" },
	{ "Game Boy Color", "|.gbc|", "|.zip|", "|gbc|gameboycolor|", "|Gambatte|" },
	{ "Game Boy Advance", "|.gba|", "|.zip|", "|gba|gameboyadvance|", "|vba-next|" },
	{ "Nintendo 64", "|.z64|.n64|.v64|", "|.zip|", "|n64|nintendo64|", "|xbox64|" },
	{ "PlayStation", "|.cbn|", "|.cue|.bin|.img|.iso|.chd|.m3u|.mdf|.pbp|", "|psx|ps1|psone|playstation|playstation1|sonyplaystation|", "|pcsxr-360|" },
	{ "PC Engine", "|.pce|", "|.zip|", "|pce|pcengine|turbografx|turbografx16|tg16|", "|beetle-pce-fast|beetle-pce|" },
	{ "PC Engine CD", "", "|.cue|.chd|.m3u|", "|pcecd|pcenginecd|turbografxcd|tgcd|", "|beetle-pce-fast|beetle-pce|" },
	{ "Neo Geo Pocket", "|.ngp|.ngc|.ngpc|.npc|", "|.zip|", "|ngp|ngpc|neogeopocket|neogeopocketcolor|", "|beetle-ngp|" },
	{ "WonderSwan", "|.ws|.wsc|", "|.zip|", "|wonderswan|wonderswancolor|ws|wsc|", "|beetle-wswan|" },
	{ "Atari Lynx", "|.lnx|.lyx|", "|.zip|", "|lynx|atarilynx|", "|beetle-lynx|" },
	{ "Virtual Boy", "|.vboy|", "|.zip|.vb|", "|virtualboy|vb|", "|beetle-vb|" },

	// No emulator for these runs on the 360 yet. Their games are listed all
	// the same, so that playing one can say so
	{ "Nintendo DS", "|.nds|", "|.zip|", "|ds|nds|nintendods|", "" },
	{ "Nintendo 3DS", "|.3ds|.cia|", "|.zip|", "|3ds|nintendo3ds|", "" },
	{ "PSP", "|.cso|", "|.iso|.zip|", "|psp|playstationportable|", "" },
	{ "PlayStation 2", "", "|.iso|.bin|.cue|.chd|", "|ps2|playstation2|", "" },
	{ "GameCube", "|.gcm|.rvz|", "|.iso|", "|gamecube|nintendogamecube|gc|ngc|", "" },
	{ "Wii", "|.wbfs|", "|.iso|", "|wii|nintendowii|", "" },
	{ "Dreamcast", "|.cdi|.gdi|", "|.chd|", "|dreamcast|segadreamcast|dc|", "" },
	{ "Saturn", "", "|.cue|.chd|.iso|", "|saturn|segasaturn|", "" },
};

static const int NUM_SYSTEMS = sizeof(SYSTEMS) / sizeof(SYSTEMS[0]);

// Folders whose files aren't games even with a game's extension: BIOS
// dumps, and emulators for PCs kept next to the games
static const char* NOT_GAMES = "|bios|firmware|emulator|emulators|emulateur|emulateurs|";

static bool InList(const char* list, const CStdString& strItem)
{
	return !strItem.IsEmpty() && strstr(list, ("|" + strItem + "|").c_str()) != NULL;
}

// The words of a folder's name, in lower case: "SEGA_GENESIS" is sega, genesis
static void GetWords(const CStdString& strFolder, CStdStringArray& words)
{
	CStdString strWord;
	for (unsigned int i = 0; i <= strFolder.size(); i++)
	{
		unsigned char c = i < strFolder.size() ? strFolder[i] : 0;
		if (isalnum(c))
			strWord += (char)tolower(c);
		else if (!strWord.IsEmpty())
		{
			words.push_back(strWord);
			strWord.clear();
		}
	}
}

CEmulators::CEmulators()
{
	m_dlgProgress = NULL;
	m_iTotal = 0;
	m_iDone = 0;
	m_iFileSize = 0;
	m_dwStart = 0;
}

// GetSystem's answer; bExcluded says the file is in a BIOS or emulator folder
static int Classify(const CStdString& strPath, bool& bExcluded)
{
	bExcluded = false;

	// In a zip, the folders the zip is in count, as if it were one more
	CStdString strFile = strPath;
	if (strFile.Left(6).Equals("zip://"))
	{
		CURL url(strPath);
		strFile = url.GetHostName() + "/" + url.GetFileName();
	}

	CStdString strExt = URIUtils::GetExtension(strFile);
	strExt.ToLower();

	bool bShared = false;
	for (int i = 0; i < NUM_SYSTEMS; i++)
	{
		if (InList(SYSTEMS[i].extensions, strExt))
			return i;
		if (InList(SYSTEMS[i].shared, strExt))
			bShared = true;
	}

	if (!bShared)
		return -1;

	// The folders it's in, the nearest first, as far up as the share or
	// drive: each by its whole name, then by two words of it and then one,
	// so that "PlayStation 2" is that before it's "PlayStation"
	CStdString strFolders = strFile;
	strFolders.Replace('\\', '/');
	CStdStringArray parts;
	CStringUtils::SplitString(strFolders, "/", parts);

	for (int i = (int)parts.size() - 2; i >= 0; i--)
	{
		const CStdString& strFolder = parts[i];
		if (strFolder.Find(':') >= 0 || strFolder.Find('@') >= 0) // A drive, or the server and who's signed in
			break;

		CStdStringArray words;
		GetWords(strFolder, words);
		if (words.empty())
			continue;

		for (unsigned int j = 0; j < words.size(); j++)
		{
			if (InList(NOT_GAMES, words[j]))
			{
				bExcluded = true;
				return -1;
			}
		}

		CStdStringArray names;
		CStdString strWhole;
		for (unsigned int j = 0; j < words.size(); j++)
			strWhole += words[j];
		names.push_back(strWhole);
		for (unsigned int j = 0; j + 1 < words.size(); j++)
			names.push_back(words[j] + words[j + 1]);
		for (unsigned int j = 0; j < words.size(); j++)
			names.push_back(words[j]);

		for (unsigned int j = 0; j < names.size(); j++)
		{
			for (int k = 0; k < NUM_SYSTEMS; k++)
			{
				if (InList(SYSTEMS[k].shared, strExt) && InList(SYSTEMS[k].folders, names[j]))
					return k;
			}
		}
	}

	return -1;
}

int CEmulators::GetSystem(const CStdString& strPath)
{
	bool bExcluded;
	return Classify(strPath, bExcluded);
}

static bool IsZip(const CStdString& strPath)
{
	// Not one in a zip: those can't be opened
	return URIUtils::GetExtension(strPath).Equals(".zip") && !strPath.Left(6).Equals("zip://");
}

bool CEmulators::IsUnknownZip(const CStdString& strPath)
{
	bool bExcluded;
	return IsZip(strPath) && Classify(strPath, bExcluded) < 0 && !bExcluded;
}

CStdString CEmulators::ZipFolder(const CStdString& strZip)
{
	CStdString strFolder;
	URIUtils::CreateArchivePath(strFolder, "zip", strZip, "");
	return strFolder;
}

// The files in a zip (not its folders), by their paths in it, and their sizes
static void GetZipFiles(const CStdString& strZip, CStdStringArray& names, std::vector<unsigned int>& sizes)
{
	std::vector<SZipEntry> entries;
	if (!g_ZipManager.GetZipList(CEmulators::ZipFolder(strZip), entries))
		return;

	for (unsigned int i = 0; i < entries.size(); i++)
	{
		size_t length = strlen(entries[i].name);
		if (length == 0 || entries[i].name[length - 1] == '/')
			continue;
		names.push_back(entries[i].name);
		sizes.push_back(entries[i].usize);
	}
}

// How well a file in a zip does as the game to start: one with its system's own extension is a game; failing that,
// an .m3u (a game's discs), then a .cue (a disc's tracks), then another its system's games use. 0: none of those
static int GameRank(const CStdString& strName, int iSystem)
{
	CStdString strExt = URIUtils::GetExtension(strName);
	strExt.ToLower();

	if (strExt.Equals(".zip"))
		return 0;
	if (InList(SYSTEMS[iSystem].extensions, strExt))
		return 4;
	if (!InList(SYSTEMS[iSystem].shared, strExt))
		return 0;
	if (strExt.Equals(".m3u"))
		return 3;
	if (strExt.Equals(".cue"))
		return 2;
	return 1;
}

bool CEmulators::HoldsSeveralGames(const CStdString& strPath)
{
	int iSystem = GetSystem(strPath);
	if (!IsZip(strPath) || iSystem < 0)
		return false;

	CStdStringArray names;
	std::vector<unsigned int> sizes;
	GetZipFiles(strPath, names, sizes);

	// Games (own extension) or discs (.cue) more than one, and no .m3u making the discs one game
	int iBest = 0, iCount = 0;
	for (unsigned int i = 0; i < names.size(); i++)
	{
		int iRank = GameRank(names[i], iSystem);
		if (iRank > iBest)
		{
			iBest = iRank;
			iCount = 0;
		}
		if (iRank == iBest)
			iCount++;
	}

	return (iBest == 4 || iBest == 2) && iCount > 1;
}

// The game to start in a zip, as a zip:// path: the file that does best as one (GameRank), the biggest of those;
// failing any, the zip's biggest file. Empty when the zip can't be read or has nothing in it
CStdString CEmulators::MainEntry(const CStdString& strZip, int iSystem)
{
	CStdStringArray names;
	std::vector<unsigned int> sizes;
	GetZipFiles(strZip, names, sizes);

	int iChosen = -1, iBest = -1;
	for (unsigned int i = 0; i < names.size(); i++)
	{
		int iRank = GameRank(names[i], iSystem);
		if (iRank > iBest || (iRank == iBest && sizes[i] > sizes[iChosen]))
		{
			iBest = iRank;
			iChosen = i;
		}
	}

	if (iChosen < 0)
		return "";

	CStdString strEntry;
	URIUtils::CreateArchivePath(strEntry, "zip", strZip, names[iChosen]);
	return strEntry;
}

CStdString CEmulators::GetSystemName(int iSystem)
{
	return iSystem >= 0 && iSystem < NUM_SYSTEMS ? SYSTEMS[iSystem].name : "";
}

CStdString CEmulators::GetExtensions()
{
	static CStdString strExtensions;
	if (!strExtensions.IsEmpty())
		return strExtensions;

	CStdString strAll = "|";
	for (int i = 0; i < NUM_SYSTEMS; i++)
	{
		const char* lists[] = { SYSTEMS[i].extensions, SYSTEMS[i].shared };
		for (int j = 0; j < 2; j++)
		{
			CStdStringArray extensions;
			CStringUtils::SplitString(lists[j], "|", extensions);
			for (unsigned int k = 0; k < extensions.size(); k++)
			{
				if (!extensions[k].IsEmpty() && !InList(strAll.c_str(), extensions[k]))
					strAll += extensions[k] + "|";
			}
		}
	}

	strExtensions = strAll.Mid(1, strAll.size() - 2);
	return strExtensions;
}

void CEmulators::SetLabels(CFileItem& item)
{
	CStdString strName = item.GetLabel();
	int iDot = strName.ReverseFind('.');
	if (iDot > 0)
		strName = strName.Left(iDot);

	item.SetLabel(strName);
	item.SetLabel2(GetSystemName(GetSystem(item.GetPath())));
	item.SetLabelPreformated(true);
	item.SetIconImage("DefaultProgram.png");
}

// Whether an emulator can open the path: it's on the console's hard drive
// or a USB drive
static bool OnConsoleDrive(const CStdString& strPath)
{
	CStdString strDrive = strPath.Left(5);
	return strDrive.Equals("Hdd1:") || strDrive.Equals("Usb0:") || strDrive.Equals("Usb1:") || strDrive.Equals("Usb2:");
}

void CEmulators::Play(const CFileItem& item)
{
	int iSystem = GetSystem(item.GetPath());
	if (iSystem < 0)
		return;

	const char* name = SYSTEMS[iSystem].name;
	if (!*SYSTEMS[iSystem].cores)
	{
		ShowMessage(33561, name);
		return;
	}

	CStdString strCore = FindCore(iSystem);
	if (strCore.IsEmpty())
	{
		CLog::Log(LOGNOTICE, "No emulator for %s games: Salvia wasn't found on the console's drives", name);
		ShowMessage(33562, name);
		return;
	}

	// A zip's game is unzipped onto the console, wherever the zip is: every core can start it then, whatever it
	// makes of zips
	CStdString strFrom = item.GetPath();
	if (IsZip(strFrom))
	{
		strFrom = MainEntry(item.GetPath(), iSystem);
		if (strFrom.IsEmpty())
		{
			ShowMessage(33578, name);
			return;
		}
	}

	CStdString strGame = ConsolePath(strFrom);
	if (!OnConsoleDrive(strGame))
	{
		CEmulators job;
		if (!job.CopyToConsole(strFrom, item.GetLabel(), iSystem, strGame))
			return;

		strGame = ConsolePath(strGame);
		if (!OnConsoleDrive(strGame))
		{
			CLog::Log(LOGERROR, "%s - the copy at %s isn't on a drive the emulator can open", __FUNCTION__, strGame.c_str());
			ShowMessage(33564, item.GetLabel());
			return;
		}
	}

	CStdString strLaunch = ForEmulator(strGame);
	CLog::Log(LOGNOTICE, "Playing the %s game %s in %s", name, strLaunch.c_str(), strCore.c_str());

	g_application.Stop();

	// The core takes the game's path as launch data, and starts it
	XSetLaunchData((PVOID)strLaunch.c_str(), strLaunch.size() + 1);
	XLaunchNewImage(strCore.c_str(), 0);

	// Only back here when the core couldn't be started, with 360 Riffle
	// already stopped: start it again
	CLog::Log(LOGERROR, "%s - couldn't start %s", __FUNCTION__, strCore.c_str());
	XLaunchNewImage(GetLoadedImageName(), 0);
}

// The folders in a folder of the console's drives
static void ListFolders(const CStdString& strFolder, std::vector<CStdString>& folders)
{
	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile((strFolder + "*").c_str(), &find);
	if (hFind == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && find.cFileName[0] != '.')
			folders.push_back(strFolder + find.cFileName + "\\");
	}
	while (FindNextFile(hFind, &find));

	FindClose(hFind);
}

// A core of Salvia's for the system. Salvia is looked for at the top of
// the console's hard drive and USB drives, and in their folders and the
// folders in those (Hdd1:\Salvia, Usb0:\Emulators\Salvia...), then
// remembered for next time
CStdString CEmulators::FindCore(int iSystem)
{
	static CStdString s_strFolder;
	static const char* ROOTS[] = { "Hdd1:\\", "Usb0:\\", "Usb1:\\", "Usb2:\\" };

	CStdStringArray cores;
	CStringUtils::SplitString(SYSTEMS[iSystem].cores, "|", cores);

	std::vector<CStdString> folders;
	if (!s_strFolder.IsEmpty())
		folders.push_back(s_strFolder);

	for (int iPass = 0; iPass < 2; iPass++)
	{
		for (unsigned int i = 0; i < cores.size(); i++)
		{
			if (cores[i].IsEmpty())
				continue;

			for (unsigned int j = 0; j < folders.size(); j++)
			{
				CStdString strCore = folders[j] + cores[i] + ".xex";
				if (GetFileAttributes(strCore.c_str()) != 0xFFFFFFFF)
				{
					s_strFolder = folders[j];
					return strCore;
				}
			}
		}

		if (iPass > 0)
			break;

		// Not where it was last time: look for it everywhere
		folders.clear();
		for (unsigned int i = 0; i < sizeof(ROOTS) / sizeof(ROOTS[0]); i++)
		{
			std::vector<CStdString> top;
			ListFolders(ROOTS[i], top);

			folders.push_back(ROOTS[i]);
			for (unsigned int j = 0; j < top.size(); j++)
			{
				folders.push_back(top[j]);
				ListFolders(top[j], folders);
			}
		}
	}

	return "";
}

// A path of ours as it is on the console's drives: D:\ and game:\ are
// where 360 Riffle is, which an emulator would take for its own folder
CStdString CEmulators::ConsolePath(const CStdString& strPath)
{
	CStdString strReal = CSpecialProtocol::TranslatePath(strPath);

	int iRoot = 0;
	if (strReal.Left(2).Equals("D:"))
		iRoot = 2;
	else if (strReal.Left(5).Equals("game:"))
		iRoot = 5;

	if (iRoot == 0)
		return strReal;

	// Our XEX was started from \Device\Harddisk0\Partition1\...: the drive
	// mounted from that device has it
	CStdString strImage = GetLoadedImageName();
	std::vector<CDrive* const> drives;
	g_application.getDriveManager().getMountedDrives(&drives);

	for (unsigned int i = 0; i < drives.size(); i++)
	{
		CStdString strDevice = drives[i]->getSystemPath() + "\\";
		if (!strImage.Left(strDevice.size()).Equals(strDevice))
			continue;

		CStdString strHome = drives[i]->getRootPath() + strImage.Mid(strDevice.size());
		CStdString strRest = strReal.Mid(iRoot);
		strRest.TrimLeft('\\');
		return strHome.Left(strHome.ReverseFind('\\') + 1) + strRest;
	}

	return strReal;
}

// The path as Salvia knows it: it names the hard drive Hdd:, and USB drives
// as we do
CStdString CEmulators::ForEmulator(const CStdString& strPath)
{
	if (strPath.Left(5).Equals("Hdd1:"))
		return "Hdd:" + strPath.Mid(5);
	return strPath;
}

// What a file is called on the console: FATX takes names of up to 42
// characters, and fewer kinds of character than a share does. A name
// that has to be cut short gets a mark of where it came from, so that two
// games whose names start the same don't become one
CStdString CEmulators::SafeName(const CStdString& strName, const CStdString& strFrom)
{
	CStdString strSafe;
	for (unsigned int i = 0; i < strName.size(); i++)
	{
		unsigned char c = strName[i];
		strSafe += (isalnum(c) || strchr(" !#$%&'()-.@[]^_`{}~", c)) ? (char)c : '_';
	}

	if (strSafe.size() <= 42)
		return strSafe;

	CStdString strExt = URIUtils::GetExtension(strSafe);
	if (strExt.size() > 8)
		strExt.clear();

	CStdString strWhere = CURL(strFrom).GetWithoutUserDetails();
	DWORD dwHash = 5381;
	for (unsigned int i = 0; i < strWhere.size(); i++)
		dwHash = dwHash * 33 + (unsigned char)tolower(strWhere[i]);

	CStdString strMark;
	strMark.Format("~%08X", dwHash);

	CStdString strBase = strSafe.Left(42 - strExt.size() - strMark.size());
	strBase.TrimRight(" .");
	return strBase + strMark + strExt;
}

void CEmulators::ShowMessage(int iLine, const CStdString& strDetail)
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

// Adds a file to copy, under the name it gets on the console. A .cue or
// .m3u names other files, which are added too
bool CEmulators::AddFile(const CStdString& strFrom, const CStdString& strFolder, CStdString& strName, int iDepth)
{
	strName = SafeName(URIUtils::GetFileName(strFrom), strFrom);

	CStdString strExt = URIUtils::GetExtension(strFrom);
	strExt.ToLower();
	if ((strExt.Equals(".cue") || strExt.Equals(".m3u")) && iDepth < 2)
		return AddList(strFrom, strFolder, strName, iDepth);

	struct __stat64 from;
	if (CFile::Stat(strFrom, &from) != 0)
	{
		CLog::Log(LOGERROR, "%s - couldn't find %s", __FUNCTION__, CURL(strFrom).GetWithoutUserDetails().c_str());
		return false;
	}

	GameFile file;
	file.strFrom = strFrom;
	file.strTo = URIUtils::AddFileToFolder(strFolder, strName);
	file.iSize = from.st_size;

	// Already copied last time
	struct __stat64 to;
	if (CFile::Stat(file.strTo, &to) == 0 && to.st_size == from.st_size)
		file.iSize = -1;
	else
		m_iTotal += file.iSize;

	m_files.push_back(file);
	return true;
}

// The file a line of a .cue (FILE "Game (Track 1).bin" BINARY) or an .m3u (a
// disc's file) names, and for a .cue what follows the name. False for a line
// that names none
static bool NamedFile(const CStdString& strLine, bool bCue, CStdString& strFile, CStdString& strRest)
{
	CStdString strTrimmed = strLine;
	strTrimmed.Trim();

	if (bCue && strTrimmed.Left(5).Equals("FILE ", false))
	{
		strFile = strTrimmed.Mid(5);
		strFile.TrimLeft();
		int iEnd = strFile[0] == '"' ? strFile.Find('"', 1) : strFile.Find(' ');
		if (strFile[0] == '"' && iEnd > 0)
		{
			strRest = strFile.Mid(iEnd + 1);
			strFile = strFile.Mid(1, iEnd - 1);
		}
		else if (iEnd > 0)
		{
			strRest = strFile.Mid(iEnd);
			strFile = strFile.Left(iEnd);
		}
	}
	else if (!bCue && !strTrimmed.IsEmpty() && strTrimmed[0] != '#')
		strFile = strTrimmed;

	return !strFile.IsEmpty();
}

// A .cue (a disc's tracks) or an .m3u (a game's discs): the files it names
// are added, and it's kept to be written out naming them as they're named
// on the console
bool CEmulators::AddList(const CStdString& strFrom, const CStdString& strFolder, CStdString& strName, int iDepth)
{
	CFile file;
	if (!file.Open(strFrom))
		return false;

	CStdString strText;
	__int64 iLength = file.GetLength();
	if (iLength > 0 && iLength < 64 * 1024)
	{
		char* buffer = new char[(unsigned int)iLength + 1];
		int iRead = file.Read(buffer, iLength);
		buffer[iRead > 0 ? iRead : 0] = 0;
		strText = buffer;
		delete[] buffer;
	}
	file.Close();

	if (strText.IsEmpty())
		return false;

	CStdString strDir;
	URIUtils::GetDirectory(strFrom, strDir);
	bool bCue = URIUtils::GetExtension(strFrom).Equals(".cue", false);

	CStdStringArray lines;
	strText.Replace("\r", "");
	CStringUtils::SplitString(strText, "\n", lines);

	CStdString strOut;
	for (unsigned int i = 0; i < lines.size(); i++)
	{
		CStdString strLine = lines[i];
		CStdString strFile, strRest;
		if (NamedFile(strLine, bCue, strFile, strRest))
		{
			CStdString strNew;
			if (!AddFile(URIUtils::AddFileToFolder(strDir, strFile), strFolder, strNew, iDepth + 1))
				return false;

			strLine = bCue ? "FILE \"" + strNew + "\"" + strRest : strNew;
		}

		strOut += strLine + "\r\n";
	}

	m_lists.push_back(std::make_pair(URIUtils::AddFileToFolder(strFolder, strName), strOut));
	return true;
}

// Copies the game from where it is (a share, a disc, a zip) to the console's
// drive, with its other files. strLabel: what to call it if it can't be.
// strGame: where it is on the console after
bool CEmulators::CopyToConsole(const CStdString& strFrom, const CStdString& strLabel, int iSystem, CStdString& strGame)
{
	CStdString strTemp = CSpecialProtocol::TranslatePath("special://temp/");
	CStdString strCache = URIUtils::AddFileToFolder(strTemp, "games");
	CStdString strFolder = URIUtils::AddFileToFolder(strCache, SYSTEMS[iSystem].name);

	const CStdString* folders[] = { &strTemp, &strCache, &strFolder };
	for (int i = 0; i < 3; i++)
	{
		if (!CDirectory::Exists(*folders[i]) && !CDirectory::Create(*folders[i]))
		{
			CLog::Log(LOGERROR, "%s - couldn't make the folder %s", __FUNCTION__, folders[i]->c_str());
			ShowMessage(13185, *folders[i]);
			return false;
		}
	}

	CStdString strName;
	if (!AddFile(strFrom, strFolder, strName, 0))
	{
		ShowMessage(33564, strLabel);
		return false;
	}

	strGame = URIUtils::AddFileToFolder(strFolder, strName);

	if (m_iTotal > 0)
	{
		MakeRoom(strCache, m_iTotal);

		ULARGE_INTEGER free;
		if (GetDiskFreeSpaceEx(strFolder.c_str(), &free, NULL, NULL) && (__int64)free.QuadPart < m_iTotal + 16 * 1024 * 1024)
		{
			ShowMessage(33565, CStringUtils::SizeToString(m_iTotal));
			return false;
		}

		CLog::Log(LOGNOTICE, "Copying the game %s to the console, %s", CURL(strFrom).GetWithoutUserDetails().c_str(),
			CStringUtils::SizeToString(m_iTotal).c_str());

		m_dwStart = CTimeUtils::timeGetTime();
		m_dlgProgress = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
		m_dlgProgress->SetHeading(33563);
		m_dlgProgress->SetLine(0, "");
		m_dlgProgress->SetLine(1, "");
		m_dlgProgress->SetLine(2, "");
		m_dlgProgress->SetPercentage(0);
		m_dlgProgress->StartModal();
		m_dlgProgress->ShowProgressBar(true);
		m_dlgProgress->SetCanCancel(true);
		m_dlgProgress->Progress();

		for (unsigned int i = 0; i < m_files.size(); i++)
		{
			const GameFile& file = m_files[i];
			if (file.iSize < 0)
				continue;

			m_strFile = URIUtils::GetFileName(file.strTo);
			m_iFileSize = file.iSize;

			if (!ShowProgress(m_iDone) || !CFile::Cache(file.strFrom, file.strTo, this))
			{
				bool bStopped = m_dlgProgress->IsCanceled();
				m_dlgProgress->Close();
				CFile::Delete(file.strTo);

				if (bStopped)
					return false;

				CLog::Log(LOGERROR, "%s - couldn't copy %s", __FUNCTION__, CURL(file.strFrom).GetWithoutUserDetails().c_str());
				ShowMessage(33564, URIUtils::GetFileName(file.strFrom));
				return false;
			}

			m_iDone += file.iSize;
		}

		m_dlgProgress->Close();
		CLog::Log(LOGNOTICE, "Copied the game in %u seconds", (CTimeUtils::timeGetTime() - m_dwStart) / 1000);
	}

	for (unsigned int i = 0; i < m_lists.size(); i++)
	{
		CFile list;
		const CStdString& strText = m_lists[i].second;
		if (!list.OpenForWrite(m_lists[i].first, true) || list.Write(strText.c_str(), strText.size()) != (int)strText.size())
		{
			list.Close();
			ShowMessage(13185, m_lists[i].first);
			return false;
		}
		list.Close();
	}

	return true;
}

struct CachedGame
{
	CStdString strPath;
	__int64 iSize;
	unsigned __int64 iTime;
};

static bool CopiedEarlier(const CachedGame& a, const CachedGame& b)
{
	return a.iTime < b.iTime;
}

// Makes room for iNeeded more bytes of games under CACHE_LIMIT, by deleting
// the games copied longest ago. Never this game's own files
void CEmulators::MakeRoom(const CStdString& strCache, __int64 iNeeded)
{
	std::vector<CStdString> folders;
	ListFolders(strCache + "\\", folders);

	std::vector<CachedGame> games;
	__int64 iTotal = 0;

	for (unsigned int i = 0; i < folders.size(); i++)
	{
		WIN32_FIND_DATA find;
		HANDLE hFind = FindFirstFile((folders[i] + "*").c_str(), &find);
		if (hFind == INVALID_HANDLE_VALUE)
			continue;

		do
		{
			if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;

			CachedGame game;
			game.strPath = folders[i] + find.cFileName;
			game.iSize = ((__int64)find.nFileSizeHigh << 32) | find.nFileSizeLow;
			game.iTime = ((unsigned __int64)find.ftLastWriteTime.dwHighDateTime << 32) | find.ftLastWriteTime.dwLowDateTime;
			iTotal += game.iSize;
			games.push_back(game);
		}
		while (FindNextFile(hFind, &find));

		FindClose(hFind);
	}

	if (iTotal + iNeeded <= CACHE_LIMIT)
		return;

	std::sort(games.begin(), games.end(), CopiedEarlier);

	for (unsigned int i = 0; i < games.size() && iTotal + iNeeded > CACHE_LIMIT; i++)
	{
		bool bOurs = false;
		for (unsigned int j = 0; j < m_files.size() && !bOurs; j++)
			bOurs = m_files[j].strTo.Equals(games[i].strPath);
		for (unsigned int j = 0; j < m_lists.size() && !bOurs; j++)
			bOurs = m_lists[j].first.Equals(games[i].strPath);
		if (bOurs)
			continue;

		if (::DeleteFile(games[i].strPath.c_str()))
		{
			CLog::Log(LOGNOTICE, "Made room for the game: deleted the copy of %s", games[i].strPath.c_str());
			iTotal -= games[i].iSize;
		}
	}
}

// Where games are copied to: D:\cache\games\, with a folder for each system (and Xbox 360 games')
static CStdString CacheFolder()
{
	return URIUtils::AddFileToFolder(CSpecialProtocol::TranslatePath("special://temp/"), "games") + "\\";
}

// The files a .cue or .m3u on the console names
static void ReadNamedFiles(const CStdString& strList, CStdStringArray& files)
{
	FILE* f = fopen(strList.c_str(), "r");
	if (!f)
		return;

	bool bCue = URIUtils::GetExtension(strList).Equals(".cue", false);
	char line[1024];
	while (fgets(line, sizeof(line), f))
	{
		CStdString strLine = line;
		strLine.TrimRight("\r\n");

		CStdString strFile, strRest;
		if (NamedFile(strLine, bCue, strFile, strRest))
			files.push_back(strFile);
	}
	fclose(f);
}

static bool IsList(const CStdString& strFile)
{
	CStdString strExt = URIUtils::GetExtension(strFile);
	strExt.ToLower();
	return strExt.Equals(".cue") || strExt.Equals(".m3u");
}

bool CEmulators::IsCopy(const CStdString& strPath)
{
	CStdString strCache = CacheFolder();
	return strPath.size() > strCache.size() && strPath.Left(strCache.size()).Equals(strCache) &&
		!strPath.Mid(strCache.size(), 9).Equals("Xbox 360\\");
}

void CEmulators::GetCopied(CFileItemList& items)
{
	CStdString strCache = CacheFolder();

	for (int iSystem = 0; iSystem < NUM_SYSTEMS; iSystem++)
	{
		CStdString strFolder = strCache + SYSTEMS[iSystem].name + "\\";

		std::vector<WIN32_FIND_DATA> files;
		WIN32_FIND_DATA find;
		HANDLE hFind = FindFirstFile((strFolder + "*").c_str(), &find);
		if (hFind == INVALID_HANDLE_VALUE)
			continue;

		do
		{
			if (!(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				files.push_back(find);
		}
		while (FindNextFile(hFind, &find));
		FindClose(hFind);

		// A file a .cue or .m3u names is part of that game (a disc's track, a game's disc), not one of its own
		CStdString strNamed = "|";
		for (unsigned int i = 0; i < files.size(); i++)
		{
			if (!IsList(files[i].cFileName))
				continue;

			CStdStringArray named;
			ReadNamedFiles(strFolder + files[i].cFileName, named);
			for (unsigned int j = 0; j < named.size(); j++)
				strNamed += named[j].ToLower() + "|";
		}

		for (unsigned int i = 0; i < files.size(); i++)
		{
			CStdString strName = files[i].cFileName;
			CStdString strPath = strFolder + strName;
			if (InList(strNamed.c_str(), CStdString(strName).ToLower()) || GetSystem(strPath) != iSystem)
				continue;

			// Its name: the file's, less its extension and the mark a name that was too long gets
			CStdString strLabel = strName;
			int iDot = strLabel.ReverseFind('.');
			if (iDot > 0)
				strLabel = strLabel.Left(iDot);
			int iMark = strLabel.ReverseFind('~');
			if (iMark > 0 && (int)strLabel.size() - iMark == 9)
				strLabel = strLabel.Left(iMark);

			CFileItemPtr item(new CFileItem(strLabel));
			item->SetPath(strPath);
			item->SetLabel2(SYSTEMS[iSystem].name);
			item->SetLabelPreformated(true);
			item->SetIconImage("DefaultProgram.png");
			item->m_dwSize = ((__int64)files[i].nFileSizeHigh << 32) | files[i].nFileSizeLow;
			item->m_dateTime = files[i].ftLastWriteTime;
			items.Add(item);
		}
	}
}

// A game's file in the cache, and for a .cue or .m3u the files it names
static void DeleteGame(const CStdString& strFile, int iDepth)
{
	if (IsList(strFile) && iDepth < 2)
	{
		CStdString strFolder;
		URIUtils::GetDirectory(strFile, strFolder);

		CStdStringArray named;
		ReadNamedFiles(strFile, named);
		for (unsigned int i = 0; i < named.size(); i++)
			DeleteGame(URIUtils::AddFileToFolder(strFolder, named[i]), iDepth + 1);
	}

	::DeleteFile(strFile.c_str());
}

bool CEmulators::RemoveCopy(const CFileItem& item)
{
	CGUIDialogYesNo* dialog = (CGUIDialogYesNo*)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
	if (!dialog)
		return false;

	CStdString strQuestion;
	strQuestion.Format(g_localizeStrings.Get(33595).c_str(), item.GetLabel().c_str());
	dialog->SetHeading(33580);
	dialog->SetLine(0, strQuestion);
	dialog->SetLine(1, "");
	dialog->SetLine(2, "");
	dialog->DoModal();
	if (!dialog->IsConfirmed())
		return false;

	CLog::Log(LOGNOTICE, "Removing the %s game %s from the console", item.GetLabel2().c_str(), item.GetPath().c_str());
	DeleteGame(item.GetPath(), 0);
	return true;
}

// Shows how far the copy's got. False once it's been cancelled
bool CEmulators::ShowProgress(__int64 iDone)
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

bool CEmulators::OnFileCallback(void* pContext, int ipercent, float avgSpeed)
{
	return ShowProgress(m_iDone + m_iFileSize * ipercent / 100);
}
