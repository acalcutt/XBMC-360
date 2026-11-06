#include "GUIWindowHome.h"
#include "FileItem.h"
#include "Settings.h"
#include "URL.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\Key.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\windows\GUIWindowSources.h"
#include "network\Jellyfin.h"
#include "network\MediaRiffle.h"

// The tabs whose tiles are their sources: the tile row's list, and the kind of source
static const struct
{
	int list;
	const char* type;
} ROWS[] = { { 9101, "video" }, { 9102, "programs" }, { 9103, "music" }, { 9104, "pictures" } };

CGUIWindowHome::CGUIWindowHome(void) : CGUIWindow(WINDOW_HOME, "Home.xml")
{
	m_loadOnDemand = false;
	for (int i = 0; i < SOURCE_ROWS; i++)
		m_rows[i] = new CFileItemList;
}

CGUIWindowHome::~CGUIWindowHome(void)
{
	for (int i = 0; i < SOURCE_ROWS; i++)
		delete m_rows[i];
}

bool CGUIWindowHome::OnMessage(CGUIMessage& message)
{
	switch (message.GetMessage())
	{
		case GUI_MSG_WINDOW_INIT:
		{
			bool result = CGUIWindow::OnMessage(message);
			FillSourceRows();
			return result;
		}

		case GUI_MSG_CLICKED:
		{
			for (int i = 0; i < SOURCE_ROWS; i++)
			{
				if (message.GetSenderId() != ROWS[i].list)
					continue;

				CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), ROWS[i].list);
				OnMessage(msg);
				int item = msg.GetParam1();
				if (item < 0 || item >= m_rows[i]->Size())
					return true;

				CFileItemPtr pItem = m_rows[i]->Get(item);
				if (pItem->GetPropertyBOOL("addsource"))
					g_windowManager.ActivateWindow(WINDOW_SOURCES);
				else
					g_windowManager.ActivateWindow(CGUIWindowSources::WindowFor(ROWS[i].type), pItem->GetPath());
				return true;
			}
		}
		break;
	}
	return CGUIWindow::OnMessage(message);
}

// Where a source is, short enough for a tile: which kind of server, the share's server, or this console
static CStdString Where(const CStdString& path)
{
	if (CMediaRiffle::IsMediaRiffle(path))
		return g_localizeStrings.Get(33530);
	if (CJellyfin::IsJellyfin(path))
		return g_localizeStrings.Get(33541);

	CURL url(path);
	if (!url.GetHostName().IsEmpty())
		return url.GetHostName();
	return g_localizeStrings.Get(13242);
}

// Each tab's sources, a tile each, refilled whenever Home is shown (sources may have changed)
void CGUIWindowHome::FillSourceRows()
{
	for (int i = 0; i < SOURCE_ROWS; i++)
	{
		CGUIMessage reset(GUI_MSG_LABEL_RESET, GetID(), ROWS[i].list);
		OnMessage(reset);
		m_rows[i]->Clear();

		VECSOURCES* sources = g_settings.GetSourcesFromType(ROWS[i].type);
		for (unsigned int s = 0; sources && s < sources->size(); s++)
		{
			const CMediaSource& source = (*sources)[s];
			CFileItemPtr item(new CFileItem(source.strName));
			item->SetPath(source.strPath);
			item->SetLabel2(Where(source.strPath));
			m_rows[i]->Add(item);
		}

		if (m_rows[i]->IsEmpty())
		{
			CFileItemPtr add(new CFileItem(g_localizeStrings.Get(13190)));
			add->SetLabel2(g_localizeStrings.Get(13189));
			add->SetProperty("addsource", true);
			m_rows[i]->Add(add);
		}

		CGUIMessage bind(GUI_MSG_LABEL_BIND, GetID(), ROWS[i].list, 0, 0, m_rows[i]);
		OnMessage(bind);
	}
}

void CGUIWindowHome::Render()
{
	CGUIWindow::Render();
}
