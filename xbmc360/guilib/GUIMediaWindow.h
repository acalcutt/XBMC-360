#ifndef GUILIB_GUIMEDIAWINDOW_H
#define GUILIB_GUIMEDIAWINDOW_H

#include "guilib\GUIWindow.h"
#include "FileSystem/VirtualDirectory.h"
#include "FileSystem/DirectoryHistory.h"
#include "guilib\GUIViewControl.h"
#include "guilib\dialogs\GUIDialogContextMenu.h"

class CFileItemList;
class CGUIDialogProgress;

// Base class for all media windows
class CGUIMediaWindow : public CGUIWindow
{
public:
	CGUIMediaWindow(int id, const char *xmlFile);
	virtual ~CGUIMediaWindow(void);

	virtual bool OnMessage(CGUIMessage& message);
	virtual bool OnAction(const CAction &action);
	virtual bool OnBack(int actionID);
	virtual void OnWindowLoaded();
	virtual void OnWindowUnload();
	virtual void OnInitWindow();
	virtual bool IsMediaWindow() const { return true; };
	const CFileItemList &CurrentDirectory() const;
	int GetViewContainerID() const { return m_viewControl.GetCurrentControl(); };
	virtual bool HasListItems() const { return true; };
	virtual CFileItemPtr GetCurrentListItem(int offset = 0);
	const CGUIViewState *GetViewState() const;

protected:
	virtual void LoadAdditionalTags(TiXmlElement *root);
	CGUIControl *GetFirstFocusableControl(int id);
	void SetupShares();
	virtual void GoParentFolder();
	virtual bool OnClick(int iItem);
	virtual bool OnPopupMenu(int iItem);
	virtual void GetContextButtons(int itemNumber, CContextButtons &buttons);
	virtual bool OnContextButton(int itemNumber, CONTEXT_BUTTON button);
	virtual void FormatItemLabels(CFileItemList &items, const LABEL_MASKS &labelMasks);
	virtual void UpdateButtons();
	virtual bool GetDirectory(const CStdString &strDirectory, CFileItemList &items);
	virtual bool Update(const CStdString &strDirectory);
	virtual void FormatAndSort(CFileItemList &items);
	virtual void OnPrepareFileItems(CFileItemList &items);
	virtual void OnFinalizeFileItems(CFileItemList &items);

	void ClearFileItems();
	virtual void SortItems(CFileItemList &items);

	// Called on response to a GUI_MSG_FILTER_ITEMS message
	// Filters the current list with the given filter using FilterItems()
	// param filter the filter to use.
	// sa FilterItems
	void OnFilterItems(const CStdString &filter);

	// Retrieve the filtered item list
	// param filter filter to apply
	// param items CFileItemList to filter
	// OnFilterItems
	void GetFilteredItems(const CStdString &filter, CFileItemList &items);

	// Check for a disc or connection
	virtual bool HaveDiscOrConnection(const CStdString& strPath, int iDriveType);
	void ShowShareErrorMessage(CFileItem* pItem);

	void GetDirectoryHistoryString(const CFileItem* pItem, CStdString& strHistoryString);
	void SetHistoryForPath(const CStdString& strDirectory);
	virtual void LoadPlayList(const CStdString& strFileName) {}
	virtual bool OnPlayMedia(int iItem);
	void UpdateFileList();
	virtual void OnDeleteItem(int iItem);
	void OnRenameItem(int iItem);

	// Shuffle and Shuffle with subfolders, at the top of a folder on the console's drives or a share in Videos, Music
	// and Pictures: the first when it has files, the second when it has folders
	void AddShuffleItems(CFileItemList &items);
	// Plays the folder's files in a random order, or with bSubfolders those in its folders and theirs too, as the
	// window's playlist
	virtual void OnShuffle(bool bSubfolders);
	// What Shuffle plays, the files as this window lists them. False when looking through the folders is cancelled
	bool GetShuffleFiles(bool bSubfolders, CFileItemList &files);
	void PlayShuffled(CFileItemList &files);

protected:
	bool AddShuffleFiles(const CFileItemList &items, bool bSubfolders, CFileItemList &files, int iDepth, CGUIDialogProgress *progress);
	bool WaitForNetwork() const;

	// Translate the folder to start in from the given quick path
	// param dir the folder the user wants
	// return the resulting path */
	virtual CStdString GetStartFolder(const CStdString &url);

	XFILE::CVirtualDirectory m_rootDir;
	CGUIViewControl m_viewControl;

	// Current path and history
	CFileItemList* m_vecItems;
	CFileItemList* m_unfilteredItems; // Items prior to filtering using FilterItems()
	CDirectoryHistory m_history;
	std::auto_ptr<CGUIViewState> m_guiState;

	// Save control state on window exit
	int m_iLastControl;
	int m_iSelectedItem;
	CStdString m_startDirectory;
};

#endif //GUILIB_GUIMEDIAWINDOW