#ifndef H_GUILIB_CGUIWINDOWSOURCES
#define H_GUILIB_CGUIWINDOWSOURCES

#include "guilib\GUIWindow.h"
#include "MediaSource.h"

class CFileItemList;

// Settings > Sources: every source in one place, with what's on it and where it is; adding, changing and removing them.
// Adding asks where the media is (a MediaRiffle or Jellyfin server, a Windows share or a folder on the console) and
// only what that needs: a server's address, after which the console links or signs in there and then; a share's
// server, folder and sign-in, checked by opening it; or a folder to pick. A share or folder is then asked what's on it,
// a name, and whether things can be saved to it.
class CGUIWindowSources : public CGUIWindow
{
public:
	CGUIWindowSources(void);
	virtual ~CGUIWindowSources(void);
	virtual bool OnMessage(CGUIMessage& message);

	// The window a kind of source is opened in, and that kind's name ("video": Videos)
	static int WindowFor(const CStdString& type);
	static CStdString TypeName(const CStdString& type);

	// Adding a source, as described above. Given a kind (from a media window's Add source), only the places that kind
	// can be on are offered, and what's on it isn't asked. True once one's added.
	static bool ShowAndAddSource(const CStdString& type = "");
	// Changing a source: the same steps, each starting from what it is. True once it's changed.
	static bool ShowAndEditSource(const CStdString& type, const CMediaSource& source);

private:
	void Refresh();
	void OnSource(int item);

	// The steps a source is made from, each starting from what path is (empty for a new one) and giving what it
	// becomes: a server's address, and linking or signing in; a share's server, folder and sign-in, checked by opening
	// it; a folder on the console
	static bool AskServer(bool mediaRiffle, CStdString& path);
	static bool AskShare(CStdString& path);
	static bool AskFolder(CStdString& path);
	static bool AskName(CStdString& name);
	static bool ChooseType(CStdString& type);
	static void AddSource(const CStdString& type, const CStdString& name, const CStdString& path, bool allowWrite);
	static CStdString UniqueName(const CStdString& type, const CStdString& name, const CStdString& except);
	static CMediaSource* Find(const CStdString& type, const CStdString& name, const CStdString& path);

	CFileItemList* m_items;
};

#endif //H_GUILIB_CGUIWINDOWSOURCES
