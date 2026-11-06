#include "Updater.h"
#include "Application.h"
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

using namespace XFILE;

// Where we're installed, and where an update waits until it's whole
#define INSTALL_DIR "D:\\"
#define STAGING_DIR "D:\\update\\"
#define READY_FILE  "D:\\update\\ready" // There once the update is copied and its default.xex is in place

// Where a build can be on a USB drive: the bundle staged for the softmod's installer, or a folder of its own
static const char* DRIVES[] = { "Usb0:\\", "Usb1:\\", "Usb2:\\" };
static const char* PLACES[] = { "BadUpdatePayload\\SoftmodExtras\\360riffle\\", "360riffle\\" };

static bool IsDots(const char* name)
{
	return !strcmp(name, ".") || !strcmp(name, "..");
}

// A folder and everything in it (strFolder ends with a backslash)
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

// A staged folder's files over the installed ones, with the folders they need. strFrom and strTo end with a backslash
static void MoveTree(const CStdString& strFrom, const CStdString& strTo, int& iMoved, int& iFailed)
{
	CStdString strDir = strTo;
	strDir.TrimRight('\\');
	::CreateDirectory(strDir.c_str(), NULL);

	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile((strFrom + "*").c_str(), &find);
	if (hFind == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (IsDots(find.cFileName))
			continue;

		CStdString strSource = strFrom + find.cFileName;
		CStdString strTarget = strTo + find.cFileName;

		if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			MoveTree(strSource + "\\", strTarget + "\\", iMoved, iFailed);
			continue;
		}

		if (strSource.Equals(READY_FILE))
			continue;

		::DeleteFile(strTarget.c_str());
		if (::MoveFile(strSource.c_str(), strTarget.c_str()))
			iMoved++;
		else
		{
			CLog::Log(LOGERROR, "Update: couldn't put %s in place (error %u)", strTarget.c_str(), GetLastError());
			iFailed++;
		}
	}
	while (FindNextFile(hFind, &find));

	FindClose(hFind);
}

// Whether two files are the same, byte for byte
static bool SameFile(const CStdString& strA, const CStdString& strB)
{
	HANDLE hA = CreateFile(strA.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	HANDLE hB = CreateFile(strB.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	bool bSame = hA != INVALID_HANDLE_VALUE && hB != INVALID_HANDLE_VALUE && GetFileSize(hA, NULL) == GetFileSize(hB, NULL);

	static BYTE a[64 * 1024], b[64 * 1024];
	while (bSame)
	{
		DWORD dwA = 0, dwB = 0;
		if (!ReadFile(hA, a, sizeof(a), &dwA, NULL) || !ReadFile(hB, b, sizeof(b), &dwB, NULL) || dwA != dwB)
			bSame = false;
		else if (dwA == 0)
			break;
		else
			bSame = memcmp(a, b, dwA) == 0;
	}

	if (hA != INVALID_HANDLE_VALUE)
		CloseHandle(hA);
	if (hB != INVALID_HANDLE_VALUE)
		CloseHandle(hB);
	return bSame;
}

// When a file was last written, to tell builds apart: 2026-11-06 10:48
static CStdString FileDate(const CStdString& strFile)
{
	CStdString strDate;
	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile(strFile.c_str(), &find);
	if (hFind == INVALID_HANDLE_VALUE)
		return strDate;
	FindClose(hFind);

	SYSTEMTIME time;
	if (FileTimeToSystemTime(&find.ftLastWriteTime, &time))
		strDate.Format("%04u-%02u-%02u %02u:%02u", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
	return strDate;
}

CUpdater::CUpdater()
{
	m_dlgProgress = NULL;
	m_iTotal = 0;
	m_iDone = 0;
	m_iFileSize = 0;
	m_dwStart = 0;
}

// The first USB drive with a build of 360 Riffle in one of the places it's looked for: a default.xex with our
// skin beside it (not some other app's default.xex)
CStdString CUpdater::FindBuild()
{
	for (unsigned int i = 0; i < sizeof(DRIVES) / sizeof(DRIVES[0]); i++)
	{
		for (unsigned int j = 0; j < sizeof(PLACES) / sizeof(PLACES[0]); j++)
		{
			CStdString strFolder = CStdString(DRIVES[i]) + PLACES[j];
			if (GetFileAttributes((strFolder + "default.xex").c_str()) != 0xFFFFFFFF &&
				GetFileAttributes((strFolder + "skins\\360 Riffle\\skin.xml").c_str()) != 0xFFFFFFFF)
				return strFolder;
		}
	}
	return "";
}

void CUpdater::ShowMessage(int iLine, const CStdString& strDetail)
{
	CStdString strLine;
	strLine.Format(g_localizeStrings.Get(iLine).c_str(), strDetail.c_str());

	CGUIDialogOK* dialog = (CGUIDialogOK*)g_windowManager.GetWindow(WINDOW_DIALOG_OK);
	if (dialog)
	{
		dialog->SetHeading(33568);
		dialog->SetLine(0, strLine);
		dialog->SetLine(1, "");
		dialog->SetLine(2, "");
		dialog->DoModal();
	}
}

void CUpdater::UpdateFromUsb()
{
	CStdString strBuild = FindBuild();
	if (strBuild.IsEmpty())
	{
		CGUIDialogOK::ShowAndGetInput(33568, 33569, 33570, 0);
		return;
	}

	// Ours by its own name, which the new one takes
	CStdString strOurs = CStdString(INSTALL_DIR) + URIUtils::GetFileName(GetLoadedImageName());
	CStdString strXex = strBuild + "default.xex";
	CStdString strDate = FileDate(strXex);

	if (SameFile(strXex, strOurs))
	{
		ShowMessage(33571, strDate);
		return;
	}

	CGUIDialogYesNo* dialog = (CGUIDialogYesNo*)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
	if (!dialog)
		return;

	CStdString strQuestion, strWhere = strBuild;
	strQuestion.Format(g_localizeStrings.Get(33572).c_str(), strDate.c_str());
	strWhere.TrimRight('\\');
	dialog->SetHeading(33568);
	dialog->SetLine(0, strQuestion);
	dialog->SetLine(1, strWhere);
	dialog->SetLine(2, g_localizeStrings.Get(33573));
	dialog->DoModal();
	if (!dialog->IsConfirmed())
		return;

	// Copied beside ours first, from scratch: what an earlier try left goes
	DeleteTree(STAGING_DIR);
	::DeleteFile((strOurs + ".old").c_str());

	CUpdater job;
	job.m_iTotal = MeasureTree(strBuild);

	ULARGE_INTEGER free;
	if (GetDiskFreeSpaceEx(INSTALL_DIR, &free, NULL, NULL) && (__int64)free.QuadPart < job.m_iTotal + 32 * 1024 * 1024)
	{
		ShowMessage(33576, CStringUtils::SizeToString(job.m_iTotal));
		return;
	}

	CLog::Log(LOGNOTICE, "Update: copying the build from %s on %s, %s", strDate.c_str(), strWhere.c_str(),
		CStringUtils::SizeToString(job.m_iTotal).c_str());

	job.m_dwStart = CTimeUtils::timeGetTime();
	job.m_dlgProgress = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
	job.m_dlgProgress->SetHeading(33577);
	job.m_dlgProgress->SetLine(0, "");
	job.m_dlgProgress->SetLine(1, "");
	job.m_dlgProgress->SetLine(2, "");
	job.m_dlgProgress->SetPercentage(0);
	job.m_dlgProgress->StartModal();
	job.m_dlgProgress->ShowProgressBar(true);
	job.m_dlgProgress->SetCanCancel(true);
	job.m_dlgProgress->Progress();

	bool bCopied = job.CopyFolder(strBuild, STAGING_DIR) && SameFile(strXex, CStdString(STAGING_DIR) + "default.xex");
	bool bStopped = job.m_dlgProgress->IsCanceled();
	job.m_dlgProgress->Close();

	if (!bCopied)
	{
		DeleteTree(STAGING_DIR);
		CLog::Log(bStopped ? LOGNOTICE : LOGERROR, "Update: %s", bStopped ? "stopped" : "couldn't copy the build");
		if (!bStopped)
			ShowMessage(33574, job.m_strFailed.IsEmpty() ? strXex : job.m_strFailed);
		return;
	}

	// The new default.xex in place of ours, which can go while it runs as it's loaded whole. Ours goes aside
	// first, and back if the new one can't be put in its place
	CStdString strStaged = CStdString(STAGING_DIR) + "default.xex";
	if (!::MoveFile(strOurs.c_str(), (strOurs + ".old").c_str()))
	{
		CLog::Log(LOGERROR, "Update: couldn't move %s aside (error %u)", strOurs.c_str(), GetLastError());
		DeleteTree(STAGING_DIR);
		CGUIDialogOK::ShowAndGetInput(33568, 33575, 0, 0);
		return;
	}
	if (!::MoveFile(strStaged.c_str(), strOurs.c_str()))
	{
		CLog::Log(LOGERROR, "Update: couldn't put the new %s in place (error %u)", strOurs.c_str(), GetLastError());
		::MoveFile((strOurs + ".old").c_str(), strOurs.c_str());
		DeleteTree(STAGING_DIR);
		CGUIDialogOK::ShowAndGetInput(33568, 33575, 0, 0);
		return;
	}

	// The rest goes in place when the new build starts
	HANDLE hReady = CreateFile(READY_FILE, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hReady != INVALID_HANDLE_VALUE)
		CloseHandle(hReady);

	CLog::Log(LOGNOTICE, "Update: the new default.xex is in place; starting it");
	g_application.getApplicationMessenger().RestartApp();
}

void CUpdater::FinishUpdate()
{
	if (GetFileAttributes(READY_FILE) == 0xFFFFFFFF)
	{
		// An update that didn't get as far as its default.xex has nothing to keep
		if (GetFileAttributes(STAGING_DIR) != 0xFFFFFFFF)
		{
			CLog::Log(LOGNOTICE, "Update: removing an unfinished one");
			DeleteTree(STAGING_DIR);
		}
		return;
	}

	CLog::Log(LOGNOTICE, "Update: putting the rest of the new build in place");
	int iMoved = 0, iFailed = 0;
	MoveTree(STAGING_DIR, INSTALL_DIR, iMoved, iFailed);
	DeleteTree(STAGING_DIR);
	::DeleteFile((CStdString(INSTALL_DIR) + URIUtils::GetFileName(GetLoadedImageName()) + ".old").c_str());

	if (iFailed)
		CLog::Log(LOGERROR, "Update: %d files in place, %d couldn't be", iMoved, iFailed);
	else
		CLog::Log(LOGNOTICE, "Update: %d files in place", iMoved);
}

// Shows how far the copy's got. False once it's been cancelled
bool CUpdater::ShowProgress(__int64 iDone)
{
	CStdString strDone;
	strDone.Format(g_localizeStrings.Get(13227).c_str(), CStringUtils::SizeToString(iDone).c_str(), CStringUtils::SizeToString(m_iTotal).c_str());

	m_dlgProgress->SetLine(0, m_strFile);
	m_dlgProgress->SetLine(1, strDone);
	m_dlgProgress->SetPercentage(m_iTotal > 0 ? (int)(iDone * 100 / m_iTotal) : 0);
	m_dlgProgress->Progress();

	return !m_dlgProgress->IsCanceled();
}

bool CUpdater::OnFileCallback(void* pContext, int ipercent, float avgSpeed)
{
	return ShowProgress(m_iDone + m_iFileSize * ipercent / 100);
}

// A folder and the folders in it, file by file. strFrom and strTo end with a backslash
bool CUpdater::CopyFolder(const CStdString& strFrom, const CStdString& strTo)
{
	CStdString strDir = strTo;
	strDir.TrimRight('\\');
	if (!::CreateDirectory(strDir.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
	{
		CLog::Log(LOGERROR, "Update: couldn't make the folder %s (error %u)", strDir.c_str(), GetLastError());
		m_strFailed = strDir;
		return false;
	}

	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile((strFrom + "*").c_str(), &find);
	if (hFind == INVALID_HANDLE_VALUE)
		return true;

	bool bOK = true;
	do
	{
		if (IsDots(find.cFileName))
			continue;

		CStdString strSource = strFrom + find.cFileName;
		CStdString strTarget = strTo + find.cFileName;

		if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			bOK = CopyFolder(strSource + "\\", strTarget + "\\");
			continue;
		}

		m_strFile = find.cFileName;
		m_iFileSize = ((__int64)find.nFileSizeHigh << 32) | find.nFileSizeLow;

		if (!ShowProgress(m_iDone) || !CFile::Cache(strSource, strTarget, this))
		{
			if (!m_dlgProgress->IsCanceled())
			{
				CLog::Log(LOGERROR, "Update: couldn't copy %s", strSource.c_str());
				m_strFailed = strSource;
			}
			bOK = false;
		}
		else
			m_iDone += m_iFileSize;
	}
	while (bOK && FindNextFile(hFind, &find));

	FindClose(hFind);
	return bOK;
}
