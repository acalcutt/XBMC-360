#include "DiscExport.h"
#include "FileItem.h"
#include "URL.h"
#include "filesystem\Directory.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\dialogs\GUIDialogFileBrowser.h"
#include "guilib\dialogs\GUIDialogKeyboard.h"
#include "guilib\dialogs\GUIDialogOK.h"
#include "guilib\dialogs\GUIDialogProgress.h"
#include "guilib\dialogs\GUIDialogYesNo.h"
#include "utils\Log.h"
#include "utils\StringUtils.h"
#include "utils\TimeUtils.h"
#include "utils\URIUtils.h"
#include "xbox\XBKernalExports.h"

using namespace XFILE;

#define DISC_SECTOR     2048
#define IMAGE_READ_SIZE (512 * DISC_SECTOR) // 1 MB a read

// NT's own values, which the console's disc driver takes as well
#define DISC_OPEN_SYNCHRONOUS          0x00000020 // FILE_SYNCHRONOUS_IO_NONALERT
#define IOCTL_CDROM_GET_DRIVE_GEOMETRY 0x0002404C

// What the 360's drive gives for its geometry: how many sectors there are
// and how big, not NT's DISK_GEOMETRY (read as that, an Xbox 360 game's
// 3567872 sectors of 2048 bytes came out as nonsense). Room to spare after
struct DiscGeometry
{
	DWORD dwSectors;
	DWORD dwBytesPerSector;
	DWORD dwSpare[4];
};

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
		ShowResult(13218, 13183, CURL(strTarget).GetWithoutUserDetails());
	else if (bStopped)
		ShowResult(13218, 13225, CURL(strTarget).GetWithoutUserDetails());
	else
		ShowResult(13218, 13224, CURL(job.m_strFailed).GetWithoutUserDetails());
}

// The drive itself, to read the disc sector by sector
static HANDLE OpenDiscDevice()
{
	HANDLE hDevice = NULL;
	STRING devicePath;
	IO_STATUS_BLOCK ioStatus;
	RtlInitAnsiString(&devicePath, "\\Device\\Cdrom0");
	OBJECT_ATTRIBUTES objAtt = { NULL, &devicePath, OBJ_CASE_INSENSITIVE };

	NTSTATUS status = NtOpenFile(&hDevice, GENERIC_READ | SYNCHRONIZE, &objAtt, &ioStatus, FILE_SHARE_READ | FILE_SHARE_WRITE, DISC_OPEN_SYNCHRONOUS);
	if (!NT_SUCCESS(status))
	{
		CLog::Log(LOGERROR, "%s - couldn't open the drive: status %08X", __FUNCTION__, status);
		return NULL;
	}
	return hDevice;
}

static bool ReadDisc(HANDLE hDevice, __int64 iOffset, BYTE* pBuffer, DWORD dwSize, DWORD& dwRead)
{
	LARGE_INTEGER pos;
	pos.QuadPart = iOffset;
	dwRead = 0;
	return SetFilePointerEx(hDevice, pos, NULL, FILE_BEGIN) && ReadFile(hDevice, pBuffer, dwSize, &dwRead, NULL);
}

// How big the disc is: what the drive says or, failing that, what an ISO
// 9660 volume descriptor says; 0 when neither does. Each answer is logged,
// and whether an Xbox game's file system header is where it would be at
// the start of a game partition, to learn what the drive gives us
static __int64 GetDiscSize(HANDLE hDevice, BYTE* pBuffer)
{
	__int64 iSize = 0;

	DiscGeometry geometry;
	memset(&geometry, 0, sizeof(geometry));
	IO_STATUS_BLOCK ioStatus;
	NTSTATUS status = NtDeviceIoControlFile(hDevice, NULL, NULL, NULL, &ioStatus, IOCTL_CDROM_GET_DRIVE_GEOMETRY, NULL, 0, &geometry, sizeof(geometry));
	if (NT_SUCCESS(status))
	{
		CLog::Log(LOGNOTICE, "Disc image: the drive reports %u sectors of %u bytes", geometry.dwSectors, geometry.dwBytesPerSector);
		if (geometry.dwBytesPerSector == DISC_SECTOR)
			iSize = (__int64)geometry.dwSectors * DISC_SECTOR;
	}
	else
		CLog::Log(LOGNOTICE, "Disc image: the drive didn't give its size (status %08X)", status);

	// The size of what's on the disc, as its file system gives it
	ULARGE_INTEGER total;
	if (GetDiskFreeSpaceEx(CDiscDrive::ROOT, NULL, &total, NULL))
	{
		CLog::Log(LOGNOTICE, "Disc image: its file system is %I64u bytes", total.QuadPart);
		if (iSize == 0)
			iSize = total.QuadPart;
	}

	DWORD dwRead = 0;

	// ISO 9660's primary volume descriptor is sector 16; its sizes are given
	// both ways round, little-endian then big-endian
	if (ReadDisc(hDevice, 16 * DISC_SECTOR, pBuffer, DISC_SECTOR, dwRead) && dwRead == DISC_SECTOR &&
		pBuffer[0] == 1 && memcmp(pBuffer + 1, "CD001", 5) == 0)
	{
		__int64 iBlocks = ((DWORD)pBuffer[84] << 24) | ((DWORD)pBuffer[85] << 16) | ((DWORD)pBuffer[86] << 8) | pBuffer[87];
		__int64 iBlockSize = (pBuffer[130] << 8) | pBuffer[131];
		CLog::Log(LOGNOTICE, "Disc image: an ISO 9660 volume of %I64d blocks of %I64d bytes", iBlocks, iBlockSize);

		if (iSize == 0)
			iSize = iBlocks * iBlockSize;
	}
	else
		CLog::Log(LOGNOTICE, "Disc image: no ISO 9660 volume descriptor at sector 16");

	if (ReadDisc(hDevice, 32 * DISC_SECTOR, pBuffer, DISC_SECTOR, dwRead) && dwRead == DISC_SECTOR)
		CLog::Log(LOGNOTICE, "Disc image: %s Xbox media header at sector 32", memcmp(pBuffer, "MICROSOFT*XBOX*MEDIA", 20) == 0 ? "an" : "no");
	else
		CLog::Log(LOGNOTICE, "Disc image: couldn't read sector 32 (error %u)", GetLastError());

	return iSize;
}

void CDiscExport::SaveImage()
{
	CDiscDrive::Info disc;
	if (!CDiscDrive::Detect(disc))
	{
		CGUIDialogOK::ShowAndGetInput(13219, 13223, 0, 0);
		return;
	}

	CStdString strFolder, strName;
	if (!ChooseDestination(disc, 13219, strFolder, strName))
		return;

	CStdString strTarget = URIUtils::AddFileToFolder(strFolder, strName + ".iso");
	if (CFile::Exists(strTarget) && !CGUIDialogYesNo::ShowAndGetInput(13219, 13230, 0, 0))
		return;

	HANDLE hDevice = OpenDiscDevice();
	BYTE* pBuffer = hDevice ? (BYTE*)VirtualAlloc(NULL, IMAGE_READ_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : NULL;
	if (!pBuffer)
	{
		if (hDevice)
			NtClose(hDevice);
		CGUIDialogOK::ShowAndGetInput(13219, 13229, 0, 0);
		return;
	}

	__int64 iSize = GetDiscSize(hDevice, pBuffer);

	// The console's own drives (FATX, or FAT32 on USB) take files of up to 4 GB
	bool bToShare = CURL(strTarget).GetProtocol().Equals("smb");
	if (iSize > 0xFFFFFFFF && !bToShare)
	{
		VirtualFree(pBuffer, 0, MEM_RELEASE);
		NtClose(hDevice);
		ShowResult(13219, 13228, CStringUtils::SizeToString(iSize));
		return;
	}

	CFile file;
	if (!file.OpenForWrite(strTarget, true))
	{
		VirtualFree(pBuffer, 0, MEM_RELEASE);
		NtClose(hDevice);
		ShowResult(13219, 13185, CURL(strTarget).GetWithoutUserDetails());
		return;
	}

	CLog::Log(LOGNOTICE, "Saving the disc image to %s", CURL(strTarget).GetWithoutUserDetails().c_str());

	CDiscExport job;
	job.StartProgress(13221, iSize);

	bool bReadFailed = false;
	bool bWriteFailed = false;
	__int64 iOffset = 0;
	DWORD dwChunk = IMAGE_READ_SIZE;
	DWORD dwShown = 0;

	while (iSize == 0 || iOffset < iSize)
	{
		DWORD dwWant = dwChunk;
		if (iSize > 0 && iSize - iOffset < dwWant)
			dwWant = (DWORD)(iSize - iOffset);

		// A scratch can take more than one go
		DWORD dwRead = 0;
		bool bRead = false;
		for (int iTry = 0; iTry < 3 && !bRead; iTry++)
			bRead = ReadDisc(hDevice, iOffset, pBuffer, dwWant, dwRead) && dwRead > 0;

		if (!bRead)
		{
			// Without a size, a read past the end fails whole: find the end a sector at a time
			if (iSize == 0 && dwChunk > DISC_SECTOR)
			{
				dwChunk = DISC_SECTOR;
				continue;
			}

			// With no size to go by, the end of the disc is where reading stops
			if (iSize > 0 || iOffset == 0)
			{
				CLog::Log(LOGERROR, "%s - couldn't read the disc at %I64d (error %u)", __FUNCTION__, iOffset, GetLastError());
				bReadFailed = true;
			}
			break;
		}

		if (file.Write(pBuffer, dwRead) != (int)dwRead)
		{
			CLog::Log(LOGERROR, "%s - couldn't write the image at %I64d", __FUNCTION__, iOffset);
			bWriteFailed = true;
			break;
		}

		iOffset += dwRead;

		// Drawing the dialog for every read would slow the copy
		if (CTimeUtils::timeGetTime() - dwShown >= 250)
		{
			dwShown = CTimeUtils::timeGetTime();
			if (!job.ShowProgress(iOffset, strName + ".iso"))
				break;
		}
	}

	bool bStopped = job.m_dlgProgress->IsCanceled();
	job.m_dlgProgress->Close();
	file.Close();
	VirtualFree(pBuffer, 0, MEM_RELEASE);
	NtClose(hDevice);

	CLog::Log(LOGNOTICE, "Disc image %s after %s in %u seconds", (bReadFailed || bWriteFailed) ? "failed" : bStopped ? "stopped" : "finished",
		CStringUtils::SizeToString(iOffset).c_str(), (CTimeUtils::timeGetTime() - job.m_dwStart) / 1000);

	if (bReadFailed || bWriteFailed || bStopped)
	{
		CFile::Delete(strTarget);

		if (bWriteFailed)
			ShowResult(13219, 13185, CURL(strTarget).GetWithoutUserDetails());
		else if (bReadFailed)
			ShowResult(13219, 13229, "");
		else
			ShowResult(13219, 13226, "");
	}
	else
		ShowResult(13219, 13183, CURL(strTarget).GetWithoutUserDetails());
}

// Where to save the disc and what to call it. Offered are the console's
// hard drive and USB drives, and sources set to allow saving
bool CDiscExport::ChooseDestination(const CDiscDrive::Info& disc, int iHeading, CStdString& strFolder, CStdString& strName)
{
	if (!CGUIDialogFileBrowser::ShowAndGetWritableDirectory(g_localizeStrings.Get(iHeading), strFolder))
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

void CDiscExport::ShowResult(int iHeading, int iLine, const CStdString& strDetail)
{
	CStdString strLine;
	strLine.Format(g_localizeStrings.Get(iLine).c_str(), strDetail.c_str());

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
