#include "DiscDrive.h"
#include "Application.h"
#include "FileItem.h"
#include "filesystem\File.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\dialogs\GUIDialogSelect.h"
#include "guilib\dialogs\GUIDialogYesNo.h"
#include "utils\Log.h"
#include "utils\Util.h"
#include "xbox\GameCopy.h"
#include "xbox\IoSupport.h"
#include "xbox\XBKernalExports.h"

using namespace XFILE;

const char* CDiscDrive::ROOT = "Dvd:\\";

// Written as Play starts the disc's game: its time says when, for CheckPlayed
#define PLAYED_MARK "D:\\cache\\disc-played"

static DWORD ReadBE32(const BYTE* p)
{
	return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | p[3];
}

// The title ID and disc number in an Xbox 360 executable's header: its
// execution info (optional header 0x00040006), which isn't encrypted
static bool ReadXexExecutionInfo(const CStdString& strPath, CDiscDrive::Info& info)
{
	CFile file;
	if (!file.Open(strPath))
		return false;

	bool bFound = false;
	BYTE header[24]; // "XEX2", flags, PE offset, reserved, security offset, optional header count

	if (file.Read(header, sizeof(header)) == sizeof(header) && memcmp(header, "XEX2", 4) == 0)
	{
		DWORD dwHeaders = ReadBE32(header + 20);

		for (DWORD i = 0; i < dwHeaders && i < 64 && !bFound; i++)
		{
			BYTE entry[8]; // Key, then the value or where to find it
			if (file.Read(entry, sizeof(entry)) != sizeof(entry))
				break;

			if (ReadBE32(entry) != 0x00040006)
				continue;

			BYTE exec[20]; // Media ID, version, base version, title ID, platform, type, disc number, disc count
			if (file.Seek(ReadBE32(entry + 4), SEEK_SET) >= 0 && file.Read(exec, sizeof(exec)) == sizeof(exec))
			{
				info.dwTitleId = ReadBE32(exec + 12);
				info.iDiscNumber = exec[18];
				info.iDiscCount = exec[19];
				bFound = true;
			}
		}
	}

	file.Close();
	return bFound;
}

bool CDiscDrive::Detect(Info& info)
{
	info.type = DISC_NONE;
	info.strLabel.Empty();
	info.dwTitleId = 0;
	info.iDiscNumber = 0;
	info.iDiscCount = 0;

	// Nothing to read while the tray is out or on its way
	DWORD dwTray = CIoSupport::GetTrayState();
	if (dwTray == TRAY_OPEN || dwTray == DRIVE_OPEN)
		return false;

	if (!CUtil::FileExists(ROOT))
		return false;

	if (CFile::Exists("Dvd:\\default.xex"))
	{
		info.type = DISC_XBOX360_GAME;
		ReadXexExecutionInfo("Dvd:\\default.xex", info);
	}
	else if (CFile::Exists("Dvd:\\default.xbe"))
		info.type = DISC_XBOX_GAME;
	else if (CFile::Exists("Dvd:\\VIDEO_TS\\VIDEO_TS.IFO"))
		info.type = DISC_DVD_VIDEO;
	else
		info.type = DISC_DATA;

	char szLabel[64] = "";
	char szFileSystem[32] = "";
	if (GetVolumeInformation(ROOT, szLabel, sizeof(szLabel), NULL, NULL, NULL, szFileSystem, sizeof(szFileSystem)))
		info.strLabel = szLabel;

	CLog::Log(LOGNOTICE, "Disc in the drive: %s, label \"%s\", file system %s, title ID %08X, disc %d of %d",
		GetTypeName(info.type).c_str(), info.strLabel.c_str(), szFileSystem, info.dwTitleId, info.iDiscNumber, info.iDiscCount);

	return true;
}

CStdString CDiscDrive::GetTypeName(DiscType type)
{
	switch (type)
	{
		case DISC_XBOX360_GAME: return g_localizeStrings.Get(13213);
		case DISC_XBOX_GAME:    return g_localizeStrings.Get(13214);
		case DISC_DVD_VIDEO:    return g_localizeStrings.Get(13215);
		case DISC_DATA:         return g_localizeStrings.Get(13216);
		default:                return "";
	}
}

// What to call the disc: its label or, for an Xbox 360 game without one,
// its title ID, with which disc it is for games on more than one
CStdString CDiscDrive::GetName(const Info& info)
{
	CStdString strName = info.strLabel;

	if (strName.IsEmpty() && info.dwTitleId)
		strName.Format("%08X", info.dwTitleId);

	if (strName.IsEmpty())
		strName = GetTypeName(info.type);

	if (info.iDiscCount > 1)
	{
		CStdString strDisc;
		strDisc.Format(g_localizeStrings.Get(13217).c_str(), info.iDiscNumber, info.iDiscCount);
		strName += " (" + strDisc + ")";
	}

	return strName;
}

void CDiscDrive::Play(const Info& info)
{
	if (info.type == DISC_XBOX360_GAME)
	{
		// Peer Pressure won't start a game from the disc itself, but its files run from the hard drive. A copy made
		// before (from the disc, or from a share's copy of it) starts at once
		if (CGameCopy::HasDiscCopy(info.dwTitleId, info.iDiscNumber))
		{
			CGameCopy::PlayDisc(GetName(info), info.dwTitleId, info.iDiscNumber);
			return;
		}

		CGUIDialogSelect* select = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
		if (!select)
			return;

		// The copy says how big it is as it goes: the disc's size isn't its game's, and adding up its files takes a
		// while on a disc. Not Microsoft's dashboard: Peer Pressure starts its homebrew dashboard, us, in its place
		select->Reset();
		select->SetHeading(GetName(info));
		select->Add(g_localizeStrings.Get(33590));
		select->Add(g_localizeStrings.Get(33592));
		select->DoModal();

		switch (select->GetSelectedLabel())
		{
			case 0: // Copied to the hard drive and started there, kept for next time
				CGameCopy::PlayDisc(GetName(info), info.dwTitleId, info.iDiscNumber);
				return;

			case 1: // From the disc, as other softmods allow
				break;

			default:
				return;
		}
	}

	if (info.type == DISC_XBOX360_GAME || info.type == DISC_XBOX_GAME)
	{
		// The game's own executable, by the drive's device name. Not the
		// default app, which would start what's in the drive on a retail
		// console: a softmod makes that its homebrew dashboard, which is us
		const char* game = info.type == DISC_XBOX360_GAME ? "\\Device\\Cdrom0\\default.xex" : "\\Device\\Cdrom0\\default.xbe";
		CLog::Log(LOGNOTICE, "Starting the %s in the drive, %s", GetTypeName(info.type).c_str(), game);

		// When this was, for CheckPlayed at our next start
		::CreateDirectory("D:\\cache", NULL);
		HANDLE hMark = CreateFile(PLAYED_MARK, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hMark != INVALID_HANDLE_VALUE)
			CloseHandle(hMark);

		g_application.Stop();
		XLaunchNewImage(game, 0);

		// Only back here when it couldn't be started, with 360 Riffle
		// already stopped: start it again
		CLog::Log(LOGERROR, "%s - couldn't start %s", __FUNCTION__, game);
		XLaunchNewImage(GetLoadedImageName(), 0);
	}
	else if (info.type == DISC_DVD_VIDEO)
	{
		CFileItem item("Dvd:\\VIDEO_TS\\VIDEO_TS.IFO", false);
		item.SetLabel(GetName(info));
		g_application.PlayFile(item);
	}
}

void CDiscDrive::CheckPlayed()
{
	static bool s_bChecked = false;
	if (s_bChecked || g_windowManager.GetActiveWindow() != WINDOW_HOME || g_windowManager.HasModalDialog())
		return;
	s_bChecked = true;

	WIN32_FILE_ATTRIBUTE_DATA mark;
	if (!GetFileAttributesEx(PLAYED_MARK, GetFileExInfoStandard, &mark))
		return;
	::DeleteFile(PLAYED_MARK);

	// Back within a minute: the game didn't start (or was left at once)
	FILETIME now;
	GetSystemTimeAsFileTime(&now);
	unsigned __int64 iNow = ((unsigned __int64)now.dwHighDateTime << 32) | now.dwLowDateTime;
	unsigned __int64 iThen = ((unsigned __int64)mark.ftLastWriteTime.dwHighDateTime << 32) | mark.ftLastWriteTime.dwLowDateTime;
	if (iNow < iThen || iNow - iThen > (unsigned __int64)60 * 10000000)
		return;

	CLog::Log(LOGNOTICE, "Back %u seconds after starting the disc's game: it didn't start", (unsigned int)((iNow - iThen) / 10000000));

	// An Xbox 360 game runs from a copy on the hard drive
	Info info;
	if (!Detect(info) || info.type != DISC_XBOX360_GAME || !CGUIDialogYesNo::ShowAndGetInput(13238, 33588, 33589, 0))
		return;

	CGameCopy::PlayDisc(GetName(info), info.dwTitleId, info.iDiscNumber);
}

void CDiscDrive::Open(const Info& info)
{
	if (info.type == DISC_DATA)
		g_windowManager.ActivateWindow(WINDOW_VIDEO_FILES, ROOT);
	else
		Play(info);
}

void CDiscDrive::Eject()
{
	CIoSupport::ToggleTray();
}

// What HasDisc last found, and when it last asked and looked
static CDiscDrive::Info s_current;
static bool s_hasDisc = false;
static DWORD s_trayState = 0xFFFFFFFF;
static DWORD s_trayAsked = 0;
static DWORD s_trayChanged = 0;
static DWORD s_discRead = 0;

#define TRAY_ASK_EVERY   1000
#define DISC_READ_EVERY  3000
#define DISC_READ_FOR    30000

bool CDiscDrive::HasDisc()
{
	DWORD now = GetTickCount();
	if (s_trayState != 0xFFFFFFFF && now - s_trayAsked < TRAY_ASK_EVERY)
		return s_hasDisc;
	s_trayAsked = now;

	DWORD tray = CIoSupport::GetTrayState();
	if (tray != s_trayState)
	{
		s_trayState = tray;
		s_trayChanged = now;
		s_discRead = 0;
		s_hasDisc = false;
	}

	// Closed: read the disc, again while it spins up, until it's found or it's been long enough to say there's none
	bool closed = tray == TRAY_CLOSED_NO_MEDIA || tray == TRAY_CLOSED_MEDIA_PRESENT;
	if (!s_hasDisc && closed && now - s_trayChanged < DISC_READ_FOR && (s_discRead == 0 || now - s_discRead >= DISC_READ_EVERY))
	{
		s_discRead = now;
		s_hasDisc = Detect(s_current);
	}
	return s_hasDisc;
}

const CDiscDrive::Info& CDiscDrive::Current()
{
	return s_current;
}
