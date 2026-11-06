#include "GUIWindowSources.h"
#include "FileItem.h"
#include "MediaManager.h"
#include "Settings.h"
#include "URL.h"
#include "filesystem\Directory.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\Key.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\dialogs\GUIDialogFileBrowser.h"
#include "guilib\dialogs\GUIDialogKeyboard.h"
#include "guilib\dialogs\GUIDialogMediaSource.h"
#include "guilib\dialogs\GUIDialogNetworkSetup.h"
#include "guilib\dialogs\GUIDialogSelect.h"
#include "guilib\dialogs\GUIDialogYesNo.h"
#include "network\Jellyfin.h"
#include "network\MediaRiffle.h"
#include "utils\URIUtils.h"

using namespace XFILE;

#define CONTROL_LIST 50

// The kinds of source, in the order they're listed
static const char* TYPES[] = { "video", "programs", "music", "pictures" };

CGUIWindowSources::CGUIWindowSources(void)
	: CGUIWindow(WINDOW_SOURCES, "SettingsSources.xml")
{
	m_items = new CFileItemList;
}

CGUIWindowSources::~CGUIWindowSources(void)
{
	delete m_items;
}

int CGUIWindowSources::WindowFor(const CStdString& type)
{
	if (type.Equals("programs"))
		return WINDOW_PROGRAMS;
	if (type.Equals("music"))
		return WINDOW_MUSIC_FILES;
	if (type.Equals("pictures"))
		return WINDOW_PICTURES;
	return WINDOW_VIDEO_FILES;
}

CStdString CGUIWindowSources::TypeName(const CStdString& type)
{
	if (type.Equals("programs"))
		return g_localizeStrings.Get(31961);
	if (type.Equals("music"))
		return g_localizeStrings.Get(31962);
	if (type.Equals("pictures"))
		return g_localizeStrings.Get(31963);
	return g_localizeStrings.Get(31960);
}

// Where a source is, for the line under its name: which server, or the share or folder (never its password), and
// whether things can be saved there
static CStdString Where(const CMediaSource& source)
{
	CStdString where;
	if (CMediaRiffle::IsMediaRiffle(source.strPath))
		where = g_localizeStrings.Get(33530) + " " + CURL(source.strPath).GetHostName();
	else if (CJellyfin::IsJellyfin(source.strPath))
		where = g_localizeStrings.Get(33541) + " " + CURL(source.strPath).GetHostName();
	else
		where = CURL(source.strPath).GetWithoutUserDetails();

	if (source.IsWritable())
		where += ", " + g_localizeStrings.Get(13235);
	return where;
}

bool CGUIWindowSources::OnMessage(CGUIMessage& message)
{
	switch (message.GetMessage())
	{
		case GUI_MSG_WINDOW_INIT:
		{
			CGUIWindow::OnMessage(message);
			Refresh();
			return true;
		}

		case GUI_MSG_CLICKED:
		{
			if (message.GetSenderId() == CONTROL_LIST && message.GetParam1() == ACTION_SELECT_ITEM)
			{
				CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), CONTROL_LIST);
				OnMessage(msg);
				int item = msg.GetParam1();

				// The first is Add a source
				if (item == 0)
					OnAdd();
				else if (item > 0 && item < m_items->Size())
					OnSource(item);
				return true;
			}
		}
		break;
	}
	return CGUIWindow::OnMessage(message);
}

void CGUIWindowSources::Refresh()
{
	CGUIMessage selected(GUI_MSG_ITEM_SELECTED, GetID(), CONTROL_LIST);
	OnMessage(selected);
	int keep = selected.GetParam1();

	CGUIMessage reset(GUI_MSG_LABEL_RESET, GetID(), CONTROL_LIST);
	OnMessage(reset);
	m_items->Clear();

	CFileItemPtr add(new CFileItem(g_localizeStrings.Get(13190)));
	add->SetLabel2(g_localizeStrings.Get(13191));
	m_items->Add(add);

	for (unsigned int t = 0; t < sizeof(TYPES) / sizeof(TYPES[0]); t++)
	{
		VECSOURCES* sources = g_settings.GetSourcesFromType(TYPES[t]);
		for (unsigned int i = 0; sources && i < sources->size(); i++)
		{
			const CMediaSource& source = (*sources)[i];
			CFileItemPtr item(new CFileItem(source.strName));
			item->SetPath(source.strPath);
			item->SetLabel2(TypeName(TYPES[t]) + " - " + Where(source));
			item->SetProperty("type", TYPES[t]);
			m_items->Add(item);
		}
	}

	CGUIMessage bind(GUI_MSG_LABEL_BIND, GetID(), CONTROL_LIST, 0, 0, m_items);
	OnMessage(bind);

	if (keep > 0 && keep < m_items->Size())
	{
		CGUIMessage select(GUI_MSG_ITEM_SELECT, GetID(), CONTROL_LIST, keep);
		OnMessage(select);
	}
}

void CGUIWindowSources::OnAdd()
{
	CGUIDialogSelect* select = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
	if (!select)
		return;

	select->Reset();
	select->SetHeading(13234);
	select->Add(g_localizeStrings.Get(33530));
	select->Add(g_localizeStrings.Get(33541));
	select->Add(g_localizeStrings.Get(13192));
	select->Add(g_localizeStrings.Get(13193));
	select->DoModal();

	int where = select->GetSelectedLabel();
	if (where == 0 || where == 1)
	{
		AddServer(where == 0);
		return;
	}
	if (where != 2 && where != 3)
		return;

	CStdString path;
	if (where == 2)
	{
		// A Windows share: its server, folder and sign-in, until it opens or they give up
		path = "smb://";
		for (;;)
		{
			if (!CGUIDialogNetworkSetup::ShowAndGetNetworkAddress(path, true) || CURL(path).GetHostName().IsEmpty())
				return;

			CFileItemList items;
			if (CDirectory::GetDirectory(path, items, "", false))
				break;
			if (!CGUIDialogYesNo::ShowAndGetInput(13192, 13194, 13195, 0))
				return;
		}
	}
	else
	{
		VECSOURCES drives;
		g_mediaManager.GetLocalDrives(drives);
		if (!CGUIDialogFileBrowser::ShowAndGetDirectory(drives, g_localizeStrings.Get(13193), path))
			return;
	}

	// Games start from the console's own drives, not from a share
	CStdString type;
	if (!ChooseType(where == 3, type))
		return;

	CStdString name = CURL(path).GetWithoutUserDetails();
	URIUtils::RemoveSlashAtEnd(name);
	name = URIUtils::GetFileName(name);
	if (!CGUIDialogKeyboard::ShowAndGetInput(name, g_localizeStrings.Get(13199), false))
		return;

	bool allowWrite = CMediaSource::CanWriteTo(path) && CGUIDialogYesNo::ShowAndGetInput(13196, 13197, 0, 0);
	AddSource(type, name, path, allowWrite);
}

// A MediaRiffle or Jellyfin server: its address, then linking (with a code) or signing in straight away. It's a source
// of videos, named for the server.
bool CGUIWindowSources::AddServer(bool mediaRiffle)
{
	CStdString path = mediaRiffle ? "mediariffle://" : "jellyfin://";
	if (!CGUIDialogNetworkSetup::ShowAndGetNetworkAddress(path, true))
		return false;

	CStdString server = mediaRiffle ? CMediaRiffle::ServerOf(path) : CJellyfin::ServerOf(path);
	CStdString source = mediaRiffle ? CMediaRiffle::SourceOf(path) : CJellyfin::SourceOf(path);
	if (server.IsEmpty())
		return false;

	bool ready = mediaRiffle ? (g_mediaRiffle.IsLinked(server) || g_mediaRiffle.Link(server))
		: (g_jellyfin.IsSignedIn(server) || g_jellyfin.SignIn(server));
	if (!ready)
		return false;

	CStdString name = g_localizeStrings.Get(mediaRiffle ? 33500 : 33540);
	if (!CGUIDialogKeyboard::ShowAndGetInput(name, g_localizeStrings.Get(13199), false))
		return false;

	AddSource("video", name, source, false);
	return true;
}

bool CGUIWindowSources::ChooseType(bool allowGames, CStdString& type)
{
	CGUIDialogSelect* select = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
	if (!select)
		return false;

	const char* types[] = { "video", "music", "pictures", "programs" };
	unsigned int count = allowGames ? 4 : 3;

	select->Reset();
	select->SetHeading(13198);
	for (unsigned int i = 0; i < count; i++)
		select->Add(TypeName(types[i]));
	select->DoModal();

	int chosen = select->GetSelectedLabel();
	if (chosen < 0 || chosen >= (int)count)
		return false;

	type = types[chosen];
	return true;
}

void CGUIWindowSources::AddSource(const CStdString& type, const CStdString& name, const CStdString& path, bool allowWrite)
{
	// A name of its own among that kind's sources
	CStdString unique = name;
	for (int n = 2; Find(type, unique, ""); n++)
		unique.Format("%s (%d)", name.c_str(), n);

	std::vector<CStdString> paths;
	paths.push_back(path);

	CMediaSource source;
	source.FromNameAndPaths(type, unique, paths);
	source.m_allowWrite = allowWrite;
	g_settings.AddShare(type, source);

	Refresh();
}

// A source of a kind by its name, and its path when that's given
CMediaSource* CGUIWindowSources::Find(const CStdString& type, const CStdString& name, const CStdString& path)
{
	VECSOURCES* sources = g_settings.GetSourcesFromType(type);
	for (unsigned int i = 0; sources && i < sources->size(); i++)
	{
		CMediaSource& source = (*sources)[i];
		if (source.strName.Equals(name) && (path.IsEmpty() || source.strPath.Equals(path)))
			return &source;
	}
	return NULL;
}

// What to do with a source: open it, change it, remove it, or for a linked MediaRiffle server sign out
void CGUIWindowSources::OnSource(int item)
{
	CFileItemPtr pItem = m_items->Get(item);
	CStdString type = pItem->GetProperty("type");
	CMediaSource* found = Find(type, pItem->GetLabel(), pItem->GetPath());
	if (!found)
		return;
	CMediaSource source = *found;

	bool server = CMediaRiffle::IsMediaRiffle(source.strPath) || CJellyfin::IsJellyfin(source.strPath);
	CStdString mediaRiffle = CMediaRiffle::ServerOf(source.strPath);
	bool canSignOut = !mediaRiffle.IsEmpty() && g_mediaRiffle.IsLinked(mediaRiffle);

	CGUIDialogSelect* select = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
	if (!select)
		return;

	select->Reset();
	select->SetHeading(source.strName);
	select->Add(g_localizeStrings.Get(13231));
	select->Add(g_localizeStrings.Get(1027));
	select->Add(g_localizeStrings.Get(522));
	if (canSignOut)
		select->Add(g_localizeStrings.Get(33506));
	select->DoModal();

	switch (select->GetSelectedLabel())
	{
		case 0:
			g_windowManager.ActivateWindow(WindowFor(type), source.strPath);
			return;

		case 1:
		{
			// A server's source has just its name to change
			if (server)
			{
				CStdString name = source.strName;
				if (CGUIDialogKeyboard::ShowAndGetInput(name, g_localizeStrings.Get(13199), false) && !name.Equals(source.strName))
				{
					CMediaSource renamed = source;
					renamed.strName = name;
					g_settings.UpdateShare(type, source.strName, renamed);
				}
			}
			else
				CGUIDialogMediaSource::ShowAndEditMediaSource(type, source);
			break;
		}

		case 2:
		{
			CGUIDialogYesNo* dialog = (CGUIDialogYesNo*)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
			if (!dialog)
				return;

			CStdString question;
			question.Format(g_localizeStrings.Get(13232).c_str(), source.strName.c_str());
			dialog->SetHeading(522);
			dialog->SetLine(0, question);
			dialog->SetLine(1, g_localizeStrings.Get(13233));
			dialog->SetLine(2, "");
			dialog->DoModal();
			if (dialog->IsConfirmed())
				g_settings.DeleteSource(type, source.strName, source.strPath);
			break;
		}

		case 3:
			if (canSignOut)
				g_mediaRiffle.Unlink(mediaRiffle);
			break;

		default:
			return;
	}
	Refresh();
}
