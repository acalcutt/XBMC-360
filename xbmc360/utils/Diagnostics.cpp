#include "Diagnostics.h"
#include "Application.h"
#include "CompileInfo.h"
#include "GUISettings.h"
#include "MediaSource.h"
#include "Settings.h"
#include "URL.h"
#include "filesystem\Drive.h"
#include "filesystem\File.h"
#include "guilib\GUIInfoManager.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\dialogs\GUIDialogOK.h"
#include "network\MediaRiffle.h"
#include "utils\Log.h"
#include "utils\StringUtils.h"

using namespace XFILE;

#define DIAGNOSTICS_FOLDER "360Riffle-diagnostics"

// The first USB drive plugged in ("Usb0:\"), or none
CStdString CDiagnostics::FindUsbDrive()
{
	std::vector<CDrive* const> drives;
	g_application.getDriveManager().getMountedDrives(&drives);
	for (unsigned int i = 0; i < drives.size(); i++)
	{
		eDriveType type = drives[i]->GetDriveType();
		if (type == DEVICE_USB0 || type == DEVICE_USB1 || type == DEVICE_USB2)
			return drives[i]->getRootPath();
	}
	return "";
}

// A log, while the logger still has it open for writing (CFile opens with read sharing only, which that refuses)
static bool CopyLog(const CStdString& from, const CStdString& to)
{
	HANDLE in = CreateFile(from.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (in == INVALID_HANDLE_VALUE)
		return false;

	HANDLE out = CreateFile(to.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (out == INVALID_HANDLE_VALUE)
	{
		CloseHandle(in);
		return false;
	}

	static char buffer[64 * 1024];
	DWORD read = 0, written = 0;
	bool ok = true;
	while (ok && ReadFile(in, buffer, sizeof(buffer), &read, NULL) && read > 0)
		ok = WriteFile(out, buffer, read, &written, NULL) && written == read;

	CloseHandle(out);
	CloseHandle(in);
	return ok;
}

static void AddLine(CStdString& report, const CStdString& name, const CStdString& value)
{
	report += name + ": " + value + "\r\n";
}

static void AddInfo(CStdString& report, const char* name, int info)
{
	AddLine(report, name, g_infoManager.GetLabel(info));
}

// What System info shows, the drives' space, the sources (without passwords) and the media servers linked (without
// their tokens)
CStdString CDiagnostics::SystemReport()
{
	CStdString report;
	SYSTEMTIME now;
	GetLocalTime(&now);
	report.Format("%s diagnostics, %04d-%02d-%02d %02d:%02d:%02d\r\n\r\n", APP_NAME, now.wYear, now.wMonth, now.wDay,
		now.wHour, now.wMinute, now.wSecond);

	AddInfo(report, "Build date", SYSTEM_BUILD_DATE);
	AddInfo(report, "Kernel", SYSTEM_KERNEL_VERSION);
	AddInfo(report, "Screen", SYSTEM_SCREEN_RESOLUTION);
	AddInfo(report, "Video standard and region", SYSTEM_VIDEO_REGION);
	AddInfo(report, "Memory used", SYSTEM_USED_MEMORY);
	AddInfo(report, "Memory free", SYSTEM_FREE_MEMORY);
	AddInfo(report, "Memory total", SYSTEM_TOTAL_MEMORY);
	AddInfo(report, "Uptime", SYSTEM_UPTIME);
	AddInfo(report, "CPU temperature", SYSTEM_CPU_TEMPERATURE);
	AddInfo(report, "GPU temperature", SYSTEM_GPU_TEMPERATURE);
	AddInfo(report, "Memory temperature", SYSTEM_MEMORY_TEMPERATURE);
	AddInfo(report, "Board temperature", SYSTEM_BOARD_TEMPERATURE);
	AddLine(report, "Skin", g_guiSettings.GetString("lookandfeel.skin"));
	AddLine(report, "Console keyboard", g_guiSettings.GetBool("lookandfeel.consolekeyboard") ? "on" : "off");

	report += "\r\nNetwork\r\n";
	AddInfo(report, "Link", NETWORK_LINK_STATE);
	AddInfo(report, "Type", NETWORK_IS_DHCP);
	AddInfo(report, "IP address", NETWORK_IP_ADDRESS);
	AddInfo(report, "MAC address", NETWORK_MAC_ADDRESS);
	AddInfo(report, "Gateway", NETWORK_GATEWAY_ADDRESS);
	AddInfo(report, "DNS", NETWORK_DNS1_ADDRESS);
	AddInfo(report, "Internet", SYSTEM_INTERNET_STATE);

	report += "\r\nDrives\r\n";
	std::vector<CDrive* const> drives;
	g_application.getDriveManager().getMountedDrives(&drives);
	for (unsigned int i = 0; i < drives.size(); i++)
	{
		CStdString root = drives[i]->getRootPath();
		ULARGE_INTEGER available, total, free;
		CStdString space = "mounted";
		if (GetDiskFreeSpaceEx(root.c_str(), &available, &total, &free) && total.QuadPart)
			space = CStringUtils::SizeToString(free.QuadPart) + " free of " + CStringUtils::SizeToString(total.QuadPart);
		AddLine(report, root, space);
	}

	report += "\r\nSources\r\n";
	const char* types[] = { "video", "music", "pictures", "programs", "files" };
	for (int t = 0; t < 5; t++)
	{
		VECSOURCES* sources = g_settings.GetSourcesFromType(types[t]);
		for (unsigned int i = 0; sources && i < sources->size(); i++)
			AddLine(report, CStdString(types[t]) + " " + (*sources)[i].strName, CURL((*sources)[i].strPath).GetWithoutUserDetails());
	}

	report += "\r\nMediaRiffle\r\n";
	report += g_mediaRiffle.Describe();
	return report;
}

bool CDiagnostics::SaveToUsb()
{
	CStdString usb = FindUsbDrive();
	if (usb.IsEmpty())
	{
		CGUIDialogOK::ShowAndGetInput(13182, 13184, 0, 0);
		return false;
	}

	SYSTEMTIME now;
	GetLocalTime(&now);
	CStdString folder, stamp;
	stamp.Format("%04d-%02d-%02d_%02d%02d%02d", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
	folder = usb + DIAGNOSTICS_FOLDER;
	CreateDirectory(folder.c_str(), NULL);
	folder += "\\" + stamp;
	CreateDirectory(folder.c_str(), NULL);

	bool saved = false;
	CFile report;
	if (report.OpenForWrite(folder + "\\system.txt", true))
	{
		CStdString text = SystemReport();
		saved = report.Write(text.c_str(), text.size()) == text.size();
		report.Close();
	}

	// The logs: this run's, and the run before (each start moves xbmc.log to xbmc.old.log)
	const char* logs[] = { "xbmc.log", "xbmc.old.log" };
	for (int i = 0; i < 2; i++)
		CopyLog(g_settings.m_logFolder + logs[i], folder + "\\" + logs[i]);

	CLog::Log(saved ? LOGNOTICE : LOGERROR, "Diagnostics: %s %s", saved ? "saved to" : "couldn't be saved to", folder.c_str());

	CGUIDialogOK* dialog = (CGUIDialogOK*)g_windowManager.GetWindow(WINDOW_DIALOG_OK);
	if (dialog)
	{
		CStdString line;
		line.Format(g_localizeStrings.Get(saved ? 13183 : 13185).c_str(), folder.c_str());
		dialog->SetHeading(13182);
		dialog->SetLine(0, line);
		dialog->SetLine(1, "");
		dialog->SetLine(2, "");
		dialog->DoModal();
	}
	return saved;
}
