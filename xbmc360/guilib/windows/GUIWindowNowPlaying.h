#ifndef GUILIB_GUIWINDOWNOWPLAYING_H
#define GUILIB_GUIWINDOWNOWPLAYING_H

#include "guilib\GUIWindow.h"

class CFileItemList;

// Now playing (MyMusicPlaylist.xml, the music playlist's window id): what the playlist playing has, music or videos, in
// its order with the one playing marked, and the player's controls (previous, play or pause, stop, next, full screen).
// Play all and Shuffle open it, and so do Home's now playing card and Start. A on an item plays it.
class CGUIWindowNowPlaying : public CGUIWindow
{
public:
	CGUIWindowNowPlaying(void);
	virtual ~CGUIWindowNowPlaying(void);
	virtual bool OnMessage(CGUIMessage& message);
	virtual void FrameMove();

private:
	void Fill();
	void MarkPlaying();

	CFileItemList* m_items;
	int m_iPlaylist; // The playlist shown
	int m_iSize;     // Its size and the item playing when they were shown: the playlist playing, its size or the
	int m_iPlaying;  // item playing changing shows them again
};

#endif //GUILIB_GUIWINDOWNOWPLAYING_H
