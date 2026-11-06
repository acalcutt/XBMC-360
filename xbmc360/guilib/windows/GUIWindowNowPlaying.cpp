#include "GUIWindowNowPlaying.h"
#include "FileItem.h"
#include "PlayListPlayer.h"
#include "guilib\GUIWindowManager.h"
#include "playlists\PlayList.h"

using namespace PLAYLIST;

// The playlist. The controls above it are the skin's, the player's own commands
#define CONTROL_LIST 50

CGUIWindowNowPlaying::CGUIWindowNowPlaying(void) : CGUIWindow(WINDOW_MUSIC_PLAYLIST, "MyMusicPlaylist.xml")
{
	m_items = new CFileItemList;
	m_iPlaylist = PLAYLIST_NONE;
	m_iSize = -1;
	m_iPlaying = -1;
}

CGUIWindowNowPlaying::~CGUIWindowNowPlaying(void)
{
	delete m_items;
}

bool CGUIWindowNowPlaying::OnMessage(CGUIMessage& message)
{
	switch (message.GetMessage())
	{
		case GUI_MSG_WINDOW_INIT:
		{
			bool result = CGUIWindow::OnMessage(message);
			Fill();
			return result;
		}

		case GUI_MSG_WINDOW_DEINIT:
		{
			CGUIMessage reset(GUI_MSG_LABEL_RESET, GetID(), CONTROL_LIST);
			OnMessage(reset);
			m_items->Clear();
		}
		break;

		case GUI_MSG_CLICKED:
		{
			if (message.GetSenderId() == CONTROL_LIST)
			{
				CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), CONTROL_LIST);
				OnMessage(msg);
				int iItem = msg.GetParam1();
				if (m_iPlaylist != PLAYLIST_NONE && iItem >= 0 && iItem < m_items->Size())
				{
					g_playlistPlayer.SetCurrentPlaylist(m_iPlaylist);
					g_playlistPlayer.Play(iItem);
				}
				return true;
			}
		}
		break;
	}
	return CGUIWindow::OnMessage(message);
}

// The playlist playing, each item as it's named, the one playing in view
void CGUIWindowNowPlaying::Fill()
{
	CGUIMessage reset(GUI_MSG_LABEL_RESET, GetID(), CONTROL_LIST);
	OnMessage(reset);
	m_items->Clear();

	m_iPlaylist = g_playlistPlayer.GetCurrentPlaylist();
	if (m_iPlaylist != PLAYLIST_NONE)
	{
		CPlayList& playlist = g_playlistPlayer.GetPlaylist(m_iPlaylist);
		for (int i = 0; i < playlist.size(); i++)
		{
			CFileItemPtr item(new CFileItem(*playlist[i]));
			if (!item->IsLabelPreformated())
				item->RemoveExtension();
			m_items->Add(item);
		}
	}
	m_iSize = m_items->Size();
	MarkPlaying();

	CGUIMessage bind(GUI_MSG_LABEL_BIND, GetID(), CONTROL_LIST, 0, 0, m_items);
	OnMessage(bind);

	if (m_iPlaying >= 0)
	{
		CGUIMessage select(GUI_MSG_ITEM_SELECT, GetID(), CONTROL_LIST, m_iPlaying);
		OnMessage(select);
	}
}

// The item playing is the list's selected one, as the skin shows it
void CGUIWindowNowPlaying::MarkPlaying()
{
	m_iPlaying = g_playlistPlayer.GetCurrentPlaylist() == m_iPlaylist ? g_playlistPlayer.GetCurrentSong() : -1;
	for (int i = 0; i < m_items->Size(); i++)
		m_items->Get(i)->Select(i == m_iPlaying);
}

void CGUIWindowNowPlaying::FrameMove()
{
	// What plays changes as it goes: another playlist, or the next item in this one
	int iPlaylist = g_playlistPlayer.GetCurrentPlaylist();
	if (iPlaylist != m_iPlaylist || (iPlaylist != PLAYLIST_NONE && g_playlistPlayer.GetPlaylist(iPlaylist).size() != m_iSize))
		Fill();
	else if (g_playlistPlayer.GetCurrentSong() != m_iPlaying)
		MarkPlaying();

	CGUIWindow::FrameMove();
}
