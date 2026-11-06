#include "GUIWindowSources.h"
#include "FileItem.h"
#include "MediaManager.h"
#include "Settings.h"
#include "URL.h"
#include "filesystem\Directory.h"
#include "filesystem\SMBDirectory.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\Key.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\dialogs\GUIDialogFileBrowser.h"
#include "guilib\dialogs\GUIDialogKeyboard.h"
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
				{
					if (ShowAndAddSource())
						Refresh();
				}
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

// The places a source can be
enum Place { PLACE_MEDIARIFFLE, PLACE_JELLYFIN, PLACE_SHARE, PLACE_CONSOLE };

bool CGUIWindowSources::ShowAndAddSource(const CStdString& type)
{
	// Where it is: a server holds videos. Games can be on a share too: an
	// emulator's game is copied to the console to be played
	std::vector<Place> places;
	if (type.IsEmpty() || type.Equals("video"))
	{
		places.push_back(PLACE_MEDIARIFFLE);
		places.push_back(PLACE_JELLYFIN);
	}
	places.push_back(PLACE_SHARE);
	places.push_back(PLACE_CONSOLE);

	Place place = places[0];
	if (places.size() > 1)
	{
		CGUIDialogSelect* select = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
		if (!select)
			return false;

		const int labels[] = { 33530, 33541, 13192, 13193 };
		select->Reset();
		select->SetHeading(13234);
		for (unsigned int i = 0; i < places.size(); i++)
			select->Add(g_localizeStrings.Get(labels[places[i]]));
		select->DoModal();

		int chosen = select->GetSelectedLabel();
		if (chosen < 0 || chosen >= (int)places.size())
			return false;
		place = places[chosen];
	}

	// A server holds videos, and is named for the kind it is
	CStdString path;
	if (place == PLACE_MEDIARIFFLE || place == PLACE_JELLYFIN)
	{
		CStdString name = g_localizeStrings.Get(place == PLACE_MEDIARIFFLE ? 33500 : 33540);
		if (!AskServer(place == PLACE_MEDIARIFFLE, path) || !AskName(name))
			return false;

		AddSource("video", name, path, false);
		return true;
	}

	if (place == PLACE_SHARE ? !AskShare(path) : !AskFolder(path))
		return false;

	CStdString kind = type;
	if (kind.IsEmpty() && !ChooseType(kind))
		return false;

	CStdString name = CURL(path).GetWithoutUserDetails();
	URIUtils::RemoveSlashAtEnd(name);
	name = URIUtils::GetFileName(name);
	if (!AskName(name))
		return false;

	bool allowWrite = CMediaSource::CanWriteTo(path) && CGUIDialogYesNo::ShowAndGetInput(13196, 13197, 0, 0);
	AddSource(kind, name, path, allowWrite);
	return true;
}

bool CGUIWindowSources::AskServer(bool mediaRiffle, CStdString& path)
{
	if (path.IsEmpty())
		path = mediaRiffle ? "mediariffle://" : "jellyfin://";
	if (!CGUIDialogNetworkSetup::ShowAndGetNetworkAddress(path, true))
		return false;

	CStdString server = mediaRiffle ? CMediaRiffle::ServerOf(path) : CJellyfin::ServerOf(path);
	if (server.IsEmpty())
		return false;

	// Linked or signed in already (the same server, changing its name), or straight away
	bool ready = mediaRiffle ? (g_mediaRiffle.IsLinked(server) || g_mediaRiffle.Link(server))
		: (g_jellyfin.IsSignedIn(server) || g_jellyfin.SignIn(server));
	if (!ready)
		return false;

	path = mediaRiffle ? CMediaRiffle::SourceOf(path) : CJellyfin::SourceOf(path);
	return true;
}

// What a share's error means, for whoever typed its details in
static int ShareProblem(const CStdString& error)
{
	CStdString reason = error;
	reason.ToUpper();

	if (reason.Find("LOGON_FAILURE") >= 0 || reason.Find("WRONG_PASSWORD") >= 0 || reason.Find("ACCOUNT_RESTRICTION") >= 0)
		return 13244;
	if (reason.Find("BAD_NETWORK_NAME") >= 0 || reason.Find("NOT_FOUND") >= 0)
		return 13245;
	if (reason.Find("ACCESS_DENIED") >= 0)
		return 13246;
	if (reason.Find("CONNECT") >= 0 || reason.Find("RESOLV") >= 0 || reason.Find("ADDRESS") >= 0)
		return 13247;
	return 13195;
}

// A Windows share: its server, folder and sign-in, until it opens or they give up. When it doesn't, why, and back to
// the details as they were typed, to change what's wrong.
bool CGUIWindowSources::AskShare(CStdString& path)
{
	if (path.IsEmpty())
		path = "smb://";

	for (;;)
	{
		if (!CGUIDialogNetworkSetup::ShowAndGetNetworkAddress(path, true) || CURL(path).GetHostName().IsEmpty())
			return false;

		CFileItemList items;
		if (CDirectory::GetDirectory(path, items, "", false))
			return true;

		if (!CGUIDialogYesNo::ShowAndGetInput(13192, 13194, ShareProblem(CSMBDirectory::GetLastError()), 0, 222, 13243))
			return false;
	}
}

bool CGUIWindowSources::AskFolder(CStdString& path)
{
	VECSOURCES drives;
	g_mediaManager.GetLocalDrives(drives);
	return CGUIDialogFileBrowser::ShowAndGetDirectory(drives, g_localizeStrings.Get(13193), path);
}

bool CGUIWindowSources::AskName(CStdString& name)
{
	return CGUIDialogKeyboard::ShowAndGetInput(name, g_localizeStrings.Get(13199), false);
}

// A server's source can move to another server (linking or signing in there); a share's server, folder and sign-in
// can change, or a folder; then its name, and whether it may be saved to
bool CGUIWindowSources::ShowAndEditSource(const CStdString& type, const CMediaSource& source)
{
	CStdString path = source.strPath;
	bool mediaRiffle = CMediaRiffle::IsMediaRiffle(path);
	bool server = mediaRiffle || CJellyfin::IsJellyfin(path);

	if (server)
	{
		if (!AskServer(mediaRiffle, path))
			return false;
	}
	else if (CURL(path).GetProtocol().Equals("smb"))
	{
		if (!AskShare(path))
			return false;
	}
	else if (!URIUtils::IsMultiPath(path) && !AskFolder(path))
		return false;

	CStdString name = source.strName;
	if (!AskName(name))
		return false;

	std::vector<CStdString> paths;
	paths.push_back(path);

	CMediaSource changed = source;
	changed.FromNameAndPaths(type, UniqueName(type, name, source.strName), paths);
	changed.m_allowWrite = !server && CMediaSource::CanWriteTo(changed.strPath) && CGUIDialogYesNo::ShowAndGetInput(13196, 13197, 0, 0);
	return g_settings.UpdateShare(type, source.strName, changed);
}

bool CGUIWindowSources::ChooseType(CStdString& type)
{
	CGUIDialogSelect* select = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
	if (!select)
		return false;

	const char* types[] = { "video", "music", "pictures", "programs" };
	unsigned int count = sizeof(types) / sizeof(types[0]);

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
	std::vector<CStdString> paths;
	paths.push_back(path);

	CMediaSource source;
	source.FromNameAndPaths(type, UniqueName(type, name, ""), paths);
	source.m_allowWrite = allowWrite;
	g_settings.AddShare(type, source);
}

// A name of its own among that kind's sources (except is the one it's taking over from, when a source is changed)
CStdString CGUIWindowSources::UniqueName(const CStdString& type, const CStdString& name, const CStdString& except)
{
	CStdString unique = name;
	for (int n = 2; !unique.Equals(except) && Find(type, unique, ""); n++)
		unique.Format("%s (%d)", name.c_str(), n);
	return unique;
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

// What to do with a source: open it, change it, remove it, or sign out of the MediaRiffle or Jellyfin server it's on
void CGUIWindowSources::OnSource(int item)
{
	CFileItemPtr pItem = m_items->Get(item);
	CStdString type = pItem->GetProperty("type");
	CMediaSource* found = Find(type, pItem->GetLabel(), pItem->GetPath());
	if (!found)
		return;
	CMediaSource source = *found;

	CStdString mediaRiffle = CMediaRiffle::ServerOf(source.strPath);
	CStdString jellyfin = CJellyfin::ServerOf(source.strPath);
	bool canSignOut = (!mediaRiffle.IsEmpty() && g_mediaRiffle.IsLinked(mediaRiffle)) ||
		(!jellyfin.IsEmpty() && g_jellyfin.IsSignedIn(jellyfin));

	CGUIDialogSelect* select = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
	if (!select)
		return;

	select->Reset();
	select->SetHeading(source.strName);
	select->Add(g_localizeStrings.Get(13231));
	select->Add(g_localizeStrings.Get(1027));
	select->Add(g_localizeStrings.Get(522));
	if (canSignOut)
		select->Add(g_localizeStrings.Get(mediaRiffle.IsEmpty() ? 33550 : 33506));
	select->DoModal();

	switch (select->GetSelectedLabel())
	{
		case 0:
			g_windowManager.ActivateWindow(WindowFor(type), source.strPath);
			return;

		case 1:
			ShowAndEditSource(type, source);
			break;

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
			if (canSignOut && !mediaRiffle.IsEmpty())
				g_mediaRiffle.Unlink(mediaRiffle);
			else if (canSignOut)
				g_jellyfin.SignOut(jellyfin);
			break;

		default:
			return;
	}
	Refresh();
}
