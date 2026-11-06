#include "DiscDrive.h"
#include "Application.h"
#include "FileItem.h"
#include "filesystem\File.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\LocalizeStrings.h"
#include "utils\Log.h"
#include "utils\Util.h"
#include "xbox\IoSupport.h"

using namespace XFILE;

const char* CDiscDrive::ROOT = "Dvd:\\";

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
	if (info.type == DISC_XBOX360_GAME || info.type == DISC_XBOX_GAME)
	{
		// The system starts what's in the drive, as its own dashboard does
		CLog::Log(LOGNOTICE, "Starting the %s in the drive", GetTypeName(info.type).c_str());
		g_application.Stop();
		XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
	}
	else if (info.type == DISC_DVD_VIDEO)
	{
		CFileItem item("Dvd:\\VIDEO_TS\\VIDEO_TS.IFO", false);
		item.SetLabel(GetName(info));
		g_application.PlayFile(item);
	}
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
