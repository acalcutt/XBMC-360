#include "DiscExport.h"
#include "FileItem.h"
#include "MediaManager.h"
#include "MediaSource.h"
#include "Settings.h"
#include "URL.h"
#include "filesystem\Directory.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\dialogs\GUIDialogFileBrowser.h"
#include "guilib\dialogs\GUIDialogKeyboard.h"
#include "guilib\dialogs\GUIDialogOK.h"
#include "guilib\dialogs\GUIDialogProgress.h"
#include "utils\Log.h"
#include "utils\StringUtils.h"
#include "utils\TimeUtils.h"
#include "utils\URIUtils.h"

using namespace XFILE;

CDiscExport::CDiscExport()
{
	m_dlgProgress = NULL;
	m_iTotal = 0;
	m_iDone = 0;
	m_iFileSize = 0;
	m_dwStart = 0;
}

void CDiscExport::CopyFiles()
{
	CDiscDrive::Info disc;
	if (!CDiscDrive::Detect(disc))
	{
		CGUIDialogOK::ShowAndGetInput(13218, 13223, 0, 0);
		return;
	}

	CStdString strFolder, strName;
	if (!ChooseDestination(disc, 13218, strFolder, strName))
		return;

	CStdString strTarget = URIUtils::AddFileToFolder(strFolder, strName);
	CLog::Log(LOGNOTICE, "Copying the disc to %s", CURL(strTarget).GetWithoutUserDetails().c_str());

	CDiscExport job;
	job.StartProgress(13220, 0);
	job.m_iTotal = MeasureFolder(CDiscDrive::ROOT);
	bool bCopied = job.CopyFolder(CDiscDrive::ROOT, strTarget);
	bool bStopped = job.m_dlgProgress->IsCanceled();
	job.m_dlgProgress->Close();

	CLog::Log(LOGNOTICE, "Disc copy %s after %s in %u seconds", bCopied ? "finished" : bStopped ? "stopped" : "failed",
		CStringUtils::SizeToString(job.m_iDone).c_str(), (CTimeUtils::timeGetTime() - job.m_dwStart) / 1000);

	if (bCopied)
		ShowResult(13218, 13183, strTarget);
	else if (bStopped)
		ShowResult(13218, 13225, strTarget);
	else
		ShowResult(13218, 13224, job.m_strFailed);
}

// Where to save the disc and what to call it. Offered are the console's
// hard drive and USB drives, and sources set to allow saving
bool CDiscExport::ChooseDestination(const CDiscDrive::Info& disc, int iHeading, CStdString& strFolder, CStdString& strName)
{
	VECSOURCES shares;
	g_mediaManager.GetLocalDrives(shares);

	const char* types[] = { "programs", "video", "music", "pictures", "files" };
	for (unsigned int i = 0; i < sizeof(types) / sizeof(types[0]); i++)
	{
		VECSOURCES* pSources = g_settings.GetSourcesFromType(types[i]);
		if (!pSources)
			continue;

		for (unsigned int j = 0; j < pSources->size(); j++)
		{
			if ((*pSources)[j].IsWritable())
				AddOrReplace(shares, (*pSources)[j]);
		}
	}

	if (!CGUIDialogFileBrowser::ShowAndGetDirectory(shares, g_localizeStrings.Get(iHeading), strFolder, true))
		return false;

	strName = MakeSafeName(CDiscDrive::GetName(disc));
	if (!CGUIDialogKeyboard::ShowAndGetInput(strName, g_localizeStrings.Get(13222), false))
		return false;

	strName = MakeSafeName(strName);
	return !strName.IsEmpty();
}

// A name the console's FATX drives take, which Windows shares do too:
// letters, digits, spaces and some punctuation, and no more than 42
// characters, less room for ".iso"
CStdString CDiscExport::MakeSafeName(const CStdString& strName)
{
	CStdString strSafe;

	for (unsigned int i = 0; i < strName.size(); i++)
	{
		unsigned char c = strName[i];
		if (isalnum(c) || strchr(" !#$%&'()-.@[]^_`{}~", c))
			strSafe += (char)c;
		else
			strSafe += '_';
	}

	if (strSafe.size() > 38)
		strSafe = strSafe.Left(38);

	strSafe.Trim();
	strSafe.TrimRight(" .");
	return strSafe;
}

__int64 CDiscExport::MeasureFolder(const CStdString& strFolder)
{
	CFileItemList items;
	if (!CDirectory::GetDirectory(strFolder, items, "", false))
		return 0;

	__int64 iSize = 0;
	for (int i = 0; i < items.Size(); i++)
	{
		if (items[i]->m_bIsFolder)
			iSize += MeasureFolder(items[i]->GetPath());
		else
			iSize += items[i]->m_dwSize;
	}
	return iSize;
}

void CDiscExport::ShowResult(int iHeading, int iLine, const CStdString& strPath)
{
	CStdString strLine;
	strLine.Format(g_localizeStrings.Get(iLine).c_str(), CURL(strPath).GetWithoutUserDetails().c_str());

	CGUIDialogOK* dialog = (CGUIDialogOK*)g_windowManager.GetWindow(WINDOW_DIALOG_OK);
	if (dialog)
	{
		dialog->SetHeading(iHeading);
		dialog->SetLine(0, strLine);
		dialog->SetLine(1, "");
		dialog->SetLine(2, "");
		dialog->DoModal();
	}
}

void CDiscExport::StartProgress(int iHeading, __int64 iTotal)
{
	m_iTotal = iTotal;
	m_iDone = 0;
	m_dwStart = CTimeUtils::timeGetTime();

	m_dlgProgress = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
	m_dlgProgress->SetHeading(iHeading);
	m_dlgProgress->SetLine(0, "");
	m_dlgProgress->SetLine(1, "");
	m_dlgProgress->SetLine(2, "");
	m_dlgProgress->SetPercentage(0);
	m_dlgProgress->StartModal();
	m_dlgProgress->ShowProgressBar(true);
	m_dlgProgress->SetCanCancel(true);
	m_dlgProgress->Progress();
}

// Shows how far along it is: what's being done, how much of the total and
// how fast. False once it's been cancelled
bool CDiscExport::ShowProgress(__int64 iDone, const CStdString& strLine)
{
	CStdString strDone = CStringUtils::SizeToString(iDone);
	if (m_iTotal > 0)
		strDone.Format(g_localizeStrings.Get(13227).c_str(), CStringUtils::SizeToString(iDone).c_str(), CStringUtils::SizeToString(m_iTotal).c_str());

	DWORD dwSeconds = (CTimeUtils::timeGetTime() - m_dwStart) / 1000;
	if (dwSeconds > 0)
		strDone += "  (" + CStringUtils::SizeToString(iDone / dwSeconds) + "/s)";

	m_dlgProgress->SetLine(0, strLine);
	m_dlgProgress->SetLine(1, strDone);
	m_dlgProgress->SetPercentage(m_iTotal > 0 ? (int)(iDone * 100 / m_iTotal) : 0);
	m_dlgProgress->Progress();

	return !m_dlgProgress->IsCanceled();
}

bool CDiscExport::OnFileCallback(void* pContext, int ipercent, float avgSpeed)
{
	return ShowProgress(m_iDone + m_iFileSize * ipercent / 100, m_strFile);
}

bool CDiscExport::CopyFolder(const CStdString& strFrom, const CStdString& strTo)
{
	if (!CDirectory::Exists(strTo) && !CDirectory::Create(strTo))
	{
		CLog::Log(LOGERROR, "%s - couldn't make the folder %s", __FUNCTION__, CURL(strTo).GetWithoutUserDetails().c_str());
		m_strFailed = strTo;
		return false;
	}

	CFileItemList items;
	if (!CDirectory::GetDirectory(strFrom, items, "", false))
	{
		CLog::Log(LOGERROR, "%s - couldn't list %s", __FUNCTION__, strFrom.c_str());
		m_strFailed = strFrom;
		return false;
	}

	for (int i = 0; i < items.Size(); i++)
	{
		CFileItemPtr pItem = items[i];
		CStdString strDest = URIUtils::AddFileToFolder(strTo, pItem->GetLabel());

		if (pItem->m_bIsFolder)
		{
			if (!CopyFolder(pItem->GetPath(), strDest))
				return false;
			continue;
		}

		m_strFile = pItem->GetLabel();
		m_iFileSize = pItem->m_dwSize;

		if (!ShowProgress(m_iDone, m_strFile))
			return false;

		if (!CFile::Cache(pItem->GetPath(), strDest, this))
		{
			if (!m_dlgProgress->IsCanceled())
			{
				CLog::Log(LOGERROR, "%s - couldn't copy %s to %s", __FUNCTION__, pItem->GetPath().c_str(), CURL(strDest).GetWithoutUserDetails().c_str());
				m_strFailed = pItem->GetPath();
			}
			return false;
		}

		m_iDone += m_iFileSize;
	}

	return true;
}
