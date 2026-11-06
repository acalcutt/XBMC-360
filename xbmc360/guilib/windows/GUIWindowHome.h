#ifndef GUILIB_GUIWINDOWHOME_H
#define GUILIB_GUIWINDOWHOME_H

#include "..\GUIWindow.h"

class CFileItemList;

// Home: the tabs, and under the focused one its tiles. Videos, Games, Music and Pictures show a tile for each of their
// sources (Settings > Sources), or one to add a source when there are none; the rest are the skin's.
class CGUIWindowHome : public CGUIWindow
{
public:
	CGUIWindowHome(void);
	virtual ~CGUIWindowHome(void);

	virtual bool OnMessage(CGUIMessage& message);
	virtual void Render();

private:
	void FillSourceRows();

	enum { SOURCE_ROWS = 4 };
	CFileItemList* m_rows[SOURCE_ROWS];
};

#endif //GUILIB_GUIWINDOWHOME_H
