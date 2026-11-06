#pragma once

#include "GUIDialogBoxBase.h"
#include "guilib\GUIListItem.h"
#include "guilib\GUIMessage.h"
#include <vector>

class CFileItem;
class CFileItemList;

class CGUIDialogSelect :
	public CGUIDialogBoxBase
{
public:
	CGUIDialogSelect(void);
	virtual ~CGUIDialogSelect(void);
	virtual bool OnMessage(CGUIMessage& message);

	void Reset();
	void Add(const CStdString& strLabel);
	void Add(const CFileItem* pItem);
	void Add(const CFileItemList& items);
	void SetItems(CFileItemList* items);
	int GetSelectedLabel() const;
	const CStdString& GetSelectedLabelText();
	const CFileItem& GetSelectedItem();
	void EnableButton(bool bOnOff);
	void SetButtonLabel(int iString);
	bool IsButtonPressed();
	void Sort(bool bSortOrder = true);
	void SetSelected(int iSelected);

	// Choosing more than one: A ticks an item or takes its tick off, and OK (the button) closes the dialog. After, the
	// ticked items are given by GetSelectedItems, when IsButtonPressed says OK was pressed. Reset ends it
	void SetMultiSelection(bool bMultiSelection);
	void SelectItem(int iItem, bool bSelected = true);
	const std::vector<int>& GetSelectedItems() const;

protected:
	bool m_bButtonEnabled;
	bool m_bButtonPressed;
	bool m_bMultiSelection;
	int m_iSelected;
	std::vector<int> m_selectedItems;

	CFileItem* m_selectedItem;
	CFileItemList* m_vecListInternal;
	CFileItemList* m_vecList;
};