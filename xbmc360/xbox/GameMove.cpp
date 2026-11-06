#include "GameMove.h"
#include "URL.h"
#include "filesystem\Directory.h"
#include "filesystem\SpecialProtocol.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\dialogs\GUIDialogFileBrowser.h"
#include "guilib\dialogs\GUIDialogOK.h"
#include "guilib\dialogs\GUIDialogProgress.h"
#include "utils\Log.h"
#include "utils\StringUtils.h"
#include "utils\TimeUtils.h"
#include "utils\URIUtils.h"
#include "xbox\Emulators.h"

using namespace XFILE;

CGameMove::CGameMove()
{
	m_dlgProgress = NULL;
	m_iTotal = 0;
	m_iDone = 0;
	m_iFileSize = 0;
	m_dwStart = 0;
}

bool CGameMove::ChoosePlace(const CStdString& strName, CStdString& strPlace)
{
	CStdString strHeading;
	strHeading.Format(g_localizeStrings.Get(33598).c_str(), strName.c_str());
	return CGUIDialogFileBrowser::ShowAndGetWritableDirectory(strHeading, strPlace);
}

void CGameMove::ListFolder(const CStdString& strFolder, const CStdString& strSkip, std::vector<CStdString>& files, std::vector<CStdString>& folders)
{
	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile((strFolder + "*").c_str(), &find);
	if (hFind == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (!strcmp(find.cFileName, ".") || !strcmp(find.cFileName, ".."))
			continue;

		CStdString strPath = strFolder + find.cFileName;
		if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			folders.push_back(strPath + "\\");
			ListFolder(strPath + "\\", strSkip, files, folders);
		}
		else if (!strSkip.Equals(find.cFileName))
			files.push_back(strPath);
	}
	while (FindNextFile(hFind, &find));

	FindClose(hFind);
}

// Where a path on the console goes: the same place in strTo as it has in strFrom
static CStdString Target(const CStdString& strPath, const CStdString& strFrom, const CStdString& strTo)
{
	CStdStringArray parts;
	CStringUtils::SplitString(strPath.Mid(strFrom.size()), "\\", parts);

	CStdString strTarget = strTo;
	for (unsigned int i = 0; i < parts.size(); i++)
	{
		if (!parts[i].IsEmpty())
			strTarget = URIUtils::AddFileToFolder(strTarget, parts[i]);
	}
	return strTarget;
}

bool CGameMove::Move(const CStdString& strName, const CStdString& strFrom, const std::vector<CStdString>& files,
	const std::vector<CStdString>& folders, const CStdString& strTo, bool bNew)
{
	// Not into the copies' own folder, which would take the game with it when it goes
	CStdString strCopies = CEmulators::ConsolePath(CSpecialProtocol::TranslatePath("special://temp/games/"));
	if (CEmulators::ConsolePath(strTo).Left(strCopies.size()).Equals(strCopies))
	{
		ShowMessage(33604, "");
		return false;
	}

	bool bThere = CDirectory::Exists(strTo);
	if (bNew && bThere)
	{
		CStdString strFolder = strTo;
		URIUtils::RemoveSlashAtEnd(strFolder);
		ShowMessage(33600, URIUtils::GetFileName(strFolder));
		return false;
	}

	// What goes where, how much there is, and that nothing there is in its way
	std::vector<CStdString> targets;
	std::vector<__int64> sizes;
	__int64 iTotal = 0;
	for (unsigned int i = 0; i < files.size(); i++)
	{
		targets.push_back(Target(files[i], strFrom, strTo));
		if (bThere && CFile::Exists(targets[i]))
		{
			ShowMessage(33600, URIUtils::GetFileName(targets[i]));
			return false;
		}

		WIN32_FILE_ATTRIBUTE_DATA data;
		__int64 iSize = 0;
		if (GetFileAttributesEx(files[i].c_str(), GetFileExInfoStandard, &data))
			iSize = ((__int64)data.nFileSizeHigh << 32) | data.nFileSizeLow;
		sizes.push_back(iSize);
		iTotal += iSize;
	}

	// Room for it on a console drive (a share says so when it's full)
	if (CURL(strTo).GetProtocol().IsEmpty())
	{
		ULARGE_INTEGER free;
		CStdString strDrive = strTo.Left(strTo.Find(':') + 1) + "\\";
		if (GetDiskFreeSpaceEx(strDrive.c_str(), &free, NULL, NULL) && (__int64)free.QuadPart < iTotal + 16 * 1024 * 1024)
		{
			ShowMessage(33602, CStringUtils::SizeToString(iTotal));
			return false;
		}
	}

	CStdString strWhere = CURL(strTo).GetWithoutUserDetails();
	CLog::Log(LOGNOTICE, "Moving %s to %s, %u files, %s", strName.c_str(), strWhere.c_str(), files.size(),
		CStringUtils::SizeToString(iTotal).c_str());

	CStdString strHeading;
	strHeading.Format(g_localizeStrings.Get(33599).c_str(), strName.c_str());

	CGameMove job;
	job.m_iTotal = iTotal;
	job.m_dwStart = CTimeUtils::timeGetTime();
	job.m_dlgProgress = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
	job.m_dlgProgress->SetHeading(strHeading);
	job.m_dlgProgress->SetLine(0, "");
	job.m_dlgProgress->SetLine(1, "");
	job.m_dlgProgress->SetLine(2, "");
	job.m_dlgProgress->SetPercentage(0);
	job.m_dlgProgress->StartModal();
	job.m_dlgProgress->ShowProgressBar(true);
	job.m_dlgProgress->SetCanCancel(true);
	job.m_dlgProgress->Progress();

	// Its folders, then its files. The folders made are kept track of, to take away again if it can't all be done
	std::vector<CStdString> made;
	CStdString strFailed;

	std::vector<CStdString> dirs;
	if (!bThere)
		dirs.push_back(strTo);
	for (unsigned int i = 0; i < folders.size(); i++)
		dirs.push_back(Target(folders[i], strFrom, strTo));

	for (unsigned int i = 0; i < dirs.size() && strFailed.IsEmpty(); i++)
	{
		if (CDirectory::Create(dirs[i]))
			made.push_back(dirs[i]);
		else
			strFailed = dirs[i];
	}

	unsigned int iCopied = 0;
	while (strFailed.IsEmpty() && iCopied < files.size())
	{
		job.m_strFile = URIUtils::GetFileName(files[iCopied]);
		job.m_iFileSize = sizes[iCopied];
		if (!job.ShowProgress(job.m_iDone) || !CFile::Cache(files[iCopied], targets[iCopied], &job))
		{
			strFailed = files[iCopied];
			break;
		}

		job.m_iDone += sizes[iCopied];
		iCopied++;
	}

	bool bStopped = job.m_dlgProgress->IsCanceled();
	job.m_dlgProgress->Close();

	if (!strFailed.IsEmpty())
	{
		// What was copied goes again (the file it stopped at too), then the folders made for it
		for (unsigned int i = 0; i <= iCopied && i < targets.size(); i++)
			CFile::Delete(targets[i]);
		for (int i = (int)made.size() - 1; i >= 0; i--)
			CDirectory::Remove(made[i]);

		CLog::Log(bStopped ? LOGNOTICE : LOGERROR, "%s moving %s, at %s", bStopped ? "Stopped" : "Failed",
			strName.c_str(), CURL(strFailed).GetWithoutUserDetails().c_str());
		if (!bStopped)
			ShowMessage(33601, strName);
		return false;
	}

	for (unsigned int i = 0; i < files.size(); i++)
		::DeleteFile(files[i].c_str());

	CLog::Log(LOGNOTICE, "Moved it in %u seconds", (CTimeUtils::timeGetTime() - job.m_dwStart) / 1000);
	ShowMessage(33603, strName, strWhere);
	return true;
}

void CGameMove::ShowMessage(int iLine, const CStdString& strDetail, const CStdString& strMore)
{
	CStdString strLine;
	strLine.Format(g_localizeStrings.Get(iLine).c_str(), strDetail.c_str(), strMore.c_str());

	CGUIDialogOK* dialog = (CGUIDialogOK*)g_windowManager.GetWindow(WINDOW_DIALOG_OK);
	if (dialog)
	{
		dialog->SetHeading(33580);
		dialog->SetLine(0, strLine);
		dialog->SetLine(1, "");
		dialog->SetLine(2, "");
		dialog->DoModal();
	}
}

// Shows how far it's got. False once it's been cancelled
bool CGameMove::ShowProgress(__int64 iDone)
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

bool CGameMove::OnFileCallback(void* pContext, int ipercent, float avgSpeed)
{
	return ShowProgress(m_iDone + m_iFileSize * ipercent / 100);
}
