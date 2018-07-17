// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "../Utilities/Dialog.h"
#include "../Utilities/KeyInput.h"
#include "../Utilities/Joystick.h"
#include "../Utilities/InstanceHandle.h"
#include "../Utilities/Config.h"
#include "../Utilities/InputManagerWithConfig.h"
#include "../Utilities/UtilResource.h"
#include "resource.h"
#include "KeyboardDialog.h"

IMPLEMENT_DYNAMIC(KeyboardDialog, OptionsPage)

KeyboardDialog::KeyboardDialog(int dialogId)
	: OptionsPage(dialogId)
{
	// Enable the "default keys" UI by default.  This displays
	// the designated default key in bold if a command has more
	// than one key assigned, and lets the user designate which
	// key is the default using a little checkmark button that
	// appears in the list row.  For a conventional accelerator
	// key scheme, the default key for a command is the one
	// shown in menu items and toolbar button tool tips.
	bUseDefaultKeys = TRUE;

	// assume alphabetic sorting
	bThreeStateSort = FALSE;

	// set the sort initially to sort by ascending command name
	sortCol = 0;
	sortDir = SortAsc;

	// no accelerator row being edited yet
	accelRow = -1;

	// no hot track row yet
	hotTrackRow = -1;
	hotTrackBtn = -1;
}

KeyboardDialog::~KeyboardDialog()
{
}

void KeyboardDialog::DoDataExchange(CDataExchange* pDX)
{
	DDX_Control(pDX, IDC_FILTER_COMMANDS, filterBox);
	DDX_Control(pDX, IDC_KEY_LIST, keyList);

	// do the base class work
	__super::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(KeyboardDialog, OptionsPage)
	ON_WM_CTLCOLOR()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_RESET_ALL, &OnResetAll)
	ON_EN_CHANGE(IDC_FILTER_COMMANDS, &OnChangeFilter)
	ON_NOTIFY(LVN_COLUMNCLICK, IDC_KEY_LIST, &OnClickCol)
	ON_NOTIFY(NM_CLICK, IDC_KEY_LIST, &OnClickList)
	ON_NOTIFY(LVN_BEGINSCROLL, IDC_KEY_LIST, &OnScrollList)
	ON_NOTIFY(LVN_ENDSCROLL, IDC_KEY_LIST, &OnScrollList)
	ON_NOTIFY(LVN_HOTTRACK, IDC_KEY_LIST, &OnHotTrackList)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_KEY_LIST, &OnCustomDrawList)
END_MESSAGE_MAP()

BOOL KeyboardDialog::OnInitDialog()
{
	// do the base class work
	__super::OnInitDialog();

	// Get the list control's client area width, excluding the scrollbar.
	// We'll divvy this up for the detail view columns.
	CRect rcList;
	keyList.GetClientRect(&rcList);
	int widRem = rcList.Width() - ::GetSystemMetrics(SM_CXVSCROLL) - 1;

	// set the hot tracking color
	hotTrackBg = RGB(240, 240, 240);
	hotTrackTxt = RGB(0, 0, 255);

	// set report mode
	keyList.SetView(LV_VIEW_DETAILS);

	// adjust styles in the list control
	keyList.SetExtendedStyle(LVS_EX_ONECLICKACTIVATE | LVS_EX_FULLROWSELECT);

	// load the list edit icons
	CPngImage iconsPng;
	iconsPng.Load(MAKEINTRESOURCE(IDB_LIST_EDIT_ICONS), G_hInstance);
	icons.Create(16, 16, ILC_COLOR32, 16, 8);
	icons.Add(&iconsPng, RGB(255, 255, 255));

	// set up the list view columns
	int cx = int(widRem * 0.6);
	widRem -= cx;
	keyList.InsertColumn(0, _T("Command"), LVCFMT_LEFT, cx, -1);
	keyList.InsertColumn(1, _T("Key"), LVCFMT_LEFT, widRem, -1);

	// set up the image list
	InitImageList();

	// build our internal database
	BuildDatabase();

	// popuplate the visible command list
	BuildCommandList();

	// set up a timer for removing hot tracking when the mouse leaves
	// the list area
	SetTimer(HotTrackTimer, 100, 0);

	// set focus on the key list
	keyList.SetFocus();
	return FALSE;
}

void KeyboardDialog::BuildCommandList()
{
	// get the current filter text
	CString filter;
	filterBox.GetWindowText(filter);
	filter.MakeLower();

	// clear the command list
	keyList.DeleteAllItems();

	// rebuild the list from our internal command list
	int idx = 0;
	for (auto const& cmdit : commands)
	{
		// get the command
		const Cmd &cmd = cmdit.second;

		// skip commands that don't match the filter
		if (!filter.IsEmpty())
		{
			CString name = cmd.name;
			name.MakeLower();
			if (name.Find(filter) < 0)
				continue;
		}

		// Add the command's keys to the list control.  Each key
		// assignment gets one row.
		for (auto& keyit = cmd.keys.begin(); keyit != cmd.keys.end(); ++keyit)
		{
			// get the Cmd::Key object
			const Cmd::Key *pKey = keyit->get();

			// add a row to the list
			int i = keyList.InsertItem(LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE, idx++,
				cmd.name.GetString(), 0, 0, cmd.imageIndex, (LPARAM)pKey);

			// set the key name in the list
			keyList.SetItemText(i, 1, pKey->keyName);
		}
	}

	// apply the current sorting
	SetSorting(sortCol, sortDir);
}

void KeyboardDialog::SetSorting(int col, int dir)
{
	// get the list header
	CHeaderCtrl *header = keyList.GetHeaderCtrl();

	// remove the previous sorting marker
	HDITEM hdi;
	hdi.mask = HDI_FORMAT;
	header->GetItem(sortCol, &hdi);
	hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
	header->SetItem(sortCol, &hdi);

	// set the new sorting data
	sortCol = col;
	sortDir = dir;

	// add the new sorting marker
	header->GetItem(sortCol, &hdi);
	hdi.fmt |= (dir == 0 ? 0 : dir == SortAsc ? HDF_SORTUP : HDF_SORTDOWN);
	header->SetItem(sortCol, &hdi);

	// do the sorting
	keyList.SortItems(sortCol == 0 ? &CompareCommands : &CompareKeys, (DWORD_PTR)this);
}

void KeyboardDialog::OnClickCol(NMHDR *nm, LRESULT *result)
{
	// get the column we clicked on
	NMLISTVIEW *nml = (NMLISTVIEW *)nm;
	int col = nml->iSubItem;

	// If it's the current sorting column, reverse the sorting
	// order for the column.  Otherwise sort ascending on the
	// new column.
	if (col == sortCol)
	{
		// Same column.  Reverse the order.  Special case:  if
		// we're on column 0, and we're currently sorting in reverse
		// order, and three-state sorting is enabled, go to sort
		// order "none", which sorts by command ID.  If we're
		// already in "none" mode, the next mode is Ascending.
		if (col == 0 && sortDir == SortDesc && bThreeStateSort)
			SetSorting(col, SortNone);
		else if (sortDir == SortNone)
			SetSorting(col, SortAsc);
		else
			SetSorting(col, -sortDir);
	}
	else
	{
		// Changing column.  Default to command order sorting on
		// column 0 if we're using three-state sorting, otherwise
		// use ascending order.
		SetSorting(col, col == 0 && bThreeStateSort ? SortNone : SortAsc);
	}
}

int CALLBACK KeyboardDialog::CompareCommands(LPARAM la, LPARAM lb, LPARAM ctx)
{
	// cast the LPARAMs to their real types
	KeyboardDialog *self = (KeyboardDialog *)ctx;
	auto a = (const Cmd::Key *)la;
	auto b = (const Cmd::Key *)lb;

	// If the two both have the same command ID, sort within the
	// command as follows:
	// 
	// - If "default key" designation is enabled, sort the default
	//   key for the command to the top
	//
	// - If default keys are disabled or neither is the default
	//   key, sort by key name
	if (a->cmd == b->cmd)
	{
		// for the purposes of key sorting, soft forward
		// if we're in sort state "none"
		int keySortDir = self->sortDir;
		if (keySortDir == 0)
			keySortDir = 1;

		// default keys are enabled
		if (self->bUseDefaultKeys)
		{
			// if 'a' is the default key, put it first
			if (a == a->cmd->defaultKey)
				return -1 * keySortDir;

			// if 'b' is the default key, put it first
			if (b == b->cmd->defaultKey)
				return 1 * keySortDir;
		}

		// the default key didn't distinguish it; sort by key name
		return keySortDir * BasicCompareKeys(la, lb, ctx);
	}

	// if we're using sort state "none", sort based on the natural
	// sort order
	if (self->sortDir == SortNone)
		return a->cmd->uiSortOrder - b->cmd->uiSortOrder;

	// sort based on the command names
	return self->sortDir * a->cmd->name.CompareNoCase(b->cmd->name);
}

int CALLBACK KeyboardDialog::CompareKeys(LPARAM la, LPARAM lb, LPARAM ctx)
{
	KeyboardDialog *self = (KeyboardDialog *)ctx;
	return self->sortDir * BasicCompareKeys(la, lb, ctx);
}

int KeyboardDialog::BasicCompareKeys(LPARAM la, LPARAM lb, LPARAM ctx)
{
	// cast the LPARAMs to their real types
	KeyboardDialog *self = (KeyboardDialog *)ctx;
	auto a = (const Cmd::Key *)la;
	auto b = (const Cmd::Key *)lb;

	// sort based on the relative sort order of the keys
	return a->SortOrder(b);
}

void KeyboardDialog::OnClickList(NMHDR *nm, LRESULT *result)
{
	// close any previous accelerator control
	CloseKeyEntry(FALSE);

	// get the click data
	LPNMITEMACTIVATE nml = (LPNMITEMACTIVATE)nm;

	// find out where we clicked
	int iSubItem;
	int iItem = keyList.PointToItem(nml->ptAction, iSubItem);

	// if we clicked on a valid item's key column, edit the key
	if (iItem >= 0 && iSubItem == 1)
	{
		// check for a button click
		switch (HitTestListIcon(nml->ptAction))
		{
		case 0:
			// Insert.  Add a new blank item to the selected command,
			// and insert a row into the list to match.  Note that
			// this operation doesn't set the "dirty" flag, since it
			// doesn't actually change the dialog data; the blank row
			// by itself has no effect on accelerators.  If the user
			// subsequently assigns a key to the new row, that will
			// cause an actual change, and we'll set the dirty bit at
			// that point as a natural consequence of making a key
			// assignment.
			AddKeyRow(iItem);
			break;

		case 1:
			// Delete button.  Delete this key mapping.
			DeleteKeyRow(iItem);
			break;

		case 2:
			// Set Default button.  This only applies if the default
			// key feature is enabled in this dialog.
			if (bUseDefaultKeys)
			{
				// get the item we're clicking on
				Cmd::Key *cur = keyList.GetItemData(iItem);
				Cmd *cmd = cur->cmd;

				// We can only set a default key if there's actually
				// a key assigned, it's not the current default key,
				// and the command has multiple keys assigned.
				if (!cur->keyName.IsEmpty() && cmd->defaultKey != cur && cmd->CountAssignedKeys() > 1)
				{
					// set it as the default
					cmd->defaultKey = cur;

					// invalidate all items associated with the command,
					// since we need to redraw both this item and the
					// item that's losing its default status
					InvalidateCommandItems(cmd);

					// this changes the settings
					SetDirty();
				}
			}
			break;

		default:
			// No button, so it's a click in the key name area.  Activate
			// the accelerator key entry mode.
			ActivateKeyEntry(iItem);
			break;
		}
	}
}

void KeyboardDialog::ActivateKeyEntry(int iItem)
{
	// remember the row we're working on
	accelRow = iItem;
}


void KeyboardDialog::CloseKeyEntry(BOOL commit)
{
	// this only applies if the accelerator is open
	if (accelRow != -1)
	{
		// remember the row and clear it, in case of re-entry
		// from recursive message processing
		int row = accelRow;
		accelRow = -1;

		// hide the UI
		DeactivateKeyEntry();

		// apply changes if desired
		if (commit)
		{
			// get the assigned key
			std::unique_ptr<Cmd::Key> newKey(GetEnteredKey());

			// Make sure there's a key assigned
			if (newKey != nullptr)
			{
				// get the command list entry for the key
				Cmd::Key *oldKey = keyList.GetItemData(row);
				Cmd *cmd = oldKey->cmd;

				// set the same command in the new key
				newKey->cmd = cmd;

				// make sure this is actually a different key from what
				// was already in the row
				if (!newKey->IsMatch(oldKey))
				{
					// Check for redundant key assignments
					std::list<const Cmd::Key *> origKeys;
					bool sameCmd = false;
					for (auto const& cmdit : commands)
					{
						Cmd::Key *key = cmdit.second.FindConflict(newKey.get());
						if (key != nullptr && key != newKey.get())
						{
							// add it to the conflict list
							origKeys.push_back(key);

							// note if it's the same command we're assigning now
							if (key->cmd == cmd)
								sameCmd = true;
						}
					}

					// if we found an existing entry, prompt for confirmation
					bool proceed = true;
					bool removeOld = false;
					if (origKeys.size() != 0)
					{
						// check if we're trying to assign a duplicate key for the same command
						if (sameCmd)
						{
							// same key is already assigned to this command - this is simply an error
							MessageBox(MsgFmt(IDS_ERR_DUP_CMD_KEY,	newKey->keyName.GetString()),
								LoadStringT(IDS_CAPTION_ERROR), MB_OK | MB_ICONINFORMATION);
							proceed = FALSE;
						}
						else
						{
							// The same key is assigned to one or more other commands.  This isn't
							// an error, but it might not be what they intended, so show a dialog
							// that explains the situation and asks how to proceed.
							class PromptDialog : public Dialog
							{
							public:
								PromptDialog(const std::list <const Cmd::Key*> &origKeys, const Cmd::Key *newKey) :
									origKeys(origKeys), newKey(newKey) { }

								virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam) override
								{
									switch (message)
									{
									case WM_INITDIALOG:
										InitStrings();
										break;

									case WM_NOTIFY:
										OnNotify(reinterpret_cast<NMHDR*>(lParam));
										break;
									}

									// return the base class handling
									return __super::Proc(message, wParam, lParam);
								}

								void InitStrings()
								{
									// make a comma-separated list of the original command names
									TSTRING cmds;
									const TCHAR *comma = _T("");
									for (auto &k : origKeys)
									{
										cmds += comma;
										cmds += k->cmd->name;
										comma = _T(", ");
									}

									// substitute the key name and command list into the prompt text
									TCHAR prompt[512];
									::GetDlgItemText(hDlg, IDC_TXT_DUP_CMD_PROMPT, prompt, countof(prompt));
									::SetDlgItemText(hDlg, IDC_TXT_DUP_CMD_PROMPT, MsgFmt(prompt, newKey->keyName.GetString(), cmds.c_str()));
								}

								void OnNotify(NMHDR *nm)
								{
									if (nm->code == NM_RETURN || nm->code == NM_CLICK)
									{
										result = nm->idFrom;
										::EndDialog(hDlg, result);
									}
								}


								const std::list<const Cmd::Key*> &origKeys;
								const Cmd::Key *newKey;
								int result;
							};
							PromptDialog prompt(origKeys, newKey.get());
							prompt.Show(IDD_DUPLICATE_KEY);

							// take the appropriate action
							switch (prompt.result)
							{
							case IDC_LNK_ADD:
								// add the new one, keeping the existing commands - just proceed
								// with the new assignment without changing the old ones
								proceed = true;
								break;

							case IDC_LNK_REPLACE:
								// replace the old ones - proceed after deleteing the old ones
								proceed = true;
								removeOld = true;
								break;

							case IDC_LNK_CANCEL:
								// keep the old ones - don't proceed with the addition
								proceed = false;
								break;
							}
						}
					}

					// assign the key if desired
					if (proceed)
					{
						// delete the old key entry, noting if it was the default
						bool wasDefault = oldKey->cmd->defaultKey == oldKey;
						cmd->DelKey(oldKey);

						// add the new key
						Cmd::Key *cmdKey = cmd->AddKey(*newKey);

						// restore the default status if appropriate
						if (wasDefault)
							cmd->defaultKey = cmdKey;

						// update the list text
						keyList.SetItemData(row, cmdKey);
						keyList.SetItemText(row, 1, cmdKey->keyName);

						// Invalidate all items associated with the command.
						// Assigning a key can have the side effect of making
						// a different key the default (e.g., if we're adding
						// a second key to a command that only had one key),
						// so other rows for the same command can be affected.
						InvalidateCommandItems(cmd);

						// If there was a previous assignment, delete it
						if (removeOld)
						{
							// delete all existing key rows
							for (auto &k : origKeys)
								DeleteKeyRow(FindKeyRow(k));
						}

						// record the change
						SetDirty();
					}
				}
			}
		}
	}
}

int KeyboardDialog::FindKeyRow(const Cmd::Key *key)
{
	// search the list for the item for this key
	for (int i = 0; i < keyList.GetItemCount(); ++i)
	{
		if (keyList.GetItemData(i) == key)
			return i;
	}

	// not found
	return -1;
}

void KeyboardDialog::AddKeyRow(int iItem)
{
	// get the item we're clicking on
	Cmd::Key *cur = keyList.GetItemData(iItem);
	Cmd *cmd = cur->cmd;

	// add a new blank key to the item's command
	Cmd::Key *added = cmd->AddKey();

	// insert the new row
	keyList.InsertItem(LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE, iItem,
		cmd->name.GetString(), 0, 0, cmd->imageIndex, (LPARAM)added);

	// scroll the new item into view
	keyList.EnsureVisible(iItem, FALSE);

	// select the new row
	keyList.SelectItem(iItem);
}

void KeyboardDialog::DeleteKeyRow(int iItem)
{
	// ignore invalid rows
	if (iItem == -1)
		return;

	// get the item we're clicking on
	Cmd::Key *cur = keyList.GetItemData(iItem);
	Cmd *cmd = cur->cmd;

	// If this is the only command for the item, don't delete
	// the row; simply clear the key assignment, leaving the
	// row with a blank key field.  This leaves the command in
	// play so that the user can assign a new key if desired.
	// If the command has other rows, we can simply delete
	// this row entirely, since the command will still be
	// addressable via one of its other rows.
	if (cmd->keys.size() == 1)
	{
		// It's an only child.  Delete the key in the 
		// database, but add back a blank key entry to
		// serve as the placeholder for the newly blank 
		// row in the UI.
		cmd->DelKey(cur);
		cur = cmd->AddKey();

		// update the row to use the new blank key
		keyList.SetItemData(iItem, cur);
		keyList.SetItemText(iItem, 1, _T(""));
	}
	else
	{
		// There are other rows for this command, so we can 
		// delete this row.  First delete the row itself.
		keyList.DeleteItem(iItem);

		// now remove the key from the command list
		cmd->DelKey(cur);

		// invalidate all items associated with the command, 
		// since deleting a row can change the default key
		// (meaning we might have to update one of the other
		// items for this command)
		InvalidateCommandItems(cmd);
	}

	// this modifies the dialog data
	SetDirty();
}

void KeyboardDialog::OnHotTrackList(NMHDR *nm, LRESULT *result)
{
	// find out where the mouse is
	auto nml = (LPNMLISTVIEW)nm;
	SetHotTrackRow(nml->ptAction);
}

void KeyboardDialog::SetHotTrackRow(CPoint mousePos)
{
	// figure out which row we're over
	int iSubItem;
	int iItem = keyList.PointToItem(mousePos, iSubItem);

	// check for a hot-track button change
	int b = HitTestListIcon(mousePos);
	if (b != hotTrackBtn)
	{
		// invalidate the button area
		CRect rc;
		keyList.GetSubItemRect(hotTrackRow, 1, LVIR_BOUNDS, rc);
		rc.left = rc.right - ICON_SIZE * NUM_LIST_BUTTONS;
		keyList.InvalidateRect(&rc);

		// set the new button
		hotTrackBtn = b;
	}

	// set the hot track row
	SetHotTrackRow(iItem);
}

void KeyboardDialog::SetHotTrackRow(int row)
{
	if (row != hotTrackRow)
	{
		// invalidate the outgoing row
		if (hotTrackRow != -1)
			keyList.InvalidateRowRect(hotTrackRow);

		// set the new row and invalidate its area
		hotTrackRow = row;
		if (row != -1)
			keyList.InvalidateRowRect(row);
	}
}

void KeyboardDialog::OnTimer(UINT_PTR idTimer)
{
	if (idTimer == HotTrackTimer)
	{
		// if we're hot-tracking a row, check if we're still in it
		if (hotTrackRow != -1)
		{
			// get the current mouse location in list view coordinates
			POINT pt;
			GetCursorPos(&pt);
			keyList.ScreenToClient(&pt);

			// update the hot-track row
			SetHotTrackRow(pt);
		}

		// handled
		return;
	}
	else if (idTimer == FilterChangeTimer)
	{
		KillTimer(FilterChangeTimer);
		BuildCommandList();
		return;
	}

	// use the default handling
	__super::OnTimer(idTimer);
}

BOOL KeyboardDialog::OnEraseBkgnd(CDC* pDC)
{
	// get the dialog window dimensions
	CRect rc;
	GetClientRect(&rc);

	// fill the background
	pDC->FillRect(rc, &GetGlobalData()->brWindow);

	// done
	return TRUE;
}

HBRUSH KeyboardDialog::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	switch (nCtlColor)
	{
	case CTLCOLOR_STATIC:
	case CTLCOLOR_BTN:
		// draw these with a transparent background
		pDC->SetBkMode(TRANSPARENT);
		return 0;

	default:
		// use the default for other controls
		return __super::OnCtlColor(pDC, pWnd, nCtlColor);
	}
}


BOOL KeyboardDialog::OnApply()
{
	// note if we have uncommited changes - do this before doing
	// the base class data exchange update, as that clears the
	// dirty flag when it's done
	bool isDirty = m_bIsDirty;

	// do the base class work (does the data exchange update)
	__super::OnApply();

	// if there are changes, save them
	if (isDirty)
		SaveChanges();

	// changes accepted
	return true;
}

BOOL KeyboardDialog::OnCommand(WPARAM wParam, LPARAM lParam)
{
	// on accelerator key entries, commit updates and close the
	// accelerator entry control
	if (wParam == MAKEWPARAM(IDC_ACCEL_ASSIGNER, KeyAssignCtrl::EN_ACCEL_SET))
		CloseKeyEntry(TRUE);

	// close the accelerator control if it loses focus or the user
	// cancels the entry with the Escape key
	if (wParam == MAKEWPARAM(IDC_ACCEL_ASSIGNER, EN_KILLFOCUS))
		CloseKeyEntry(FALSE);

	// check for list box or checkbox changes
	if (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == BN_CLICKED)
	{
		// load the current control settings into member variables
		UpdateData(true);

		// enable/disable OK according to the current dirty status
		SetModified(IsDirty());
	}

	// do the normal work
	return __super::OnCommand(wParam, lParam);
}

void KeyboardDialog::InvalidateCommandItems(Cmd *cmd)
{
	// invalidate all rows matching the command
	keyList.InvalidateRowsIf([this, cmd](int row) {
		return keyList.GetItemData(row)->cmd == cmd;
	});
}

void KeyboardDialog::OnScrollList(NMHDR *nm, LRESULT *result)
{
	// Overlying the text field on the list screws up scrolling
	// for both objects, so just get rid of it any time we're
	// about to scroll.  To be a bit fancier, we could hide it 
	// at the start of the scroll, move it, and restore it at 
	// the end, but I don't think anyone's going to be seriously
	// bothered if it just disappears on scroll - it's such an
	// ephemeral control to start with, given that it only takes
	// a single key, and it auto-hides on just about any other
	// mouse event anyway.
	CloseKeyEntry(FALSE);
}

void KeyboardDialog::OnCustomDrawList(NMHDR *nm, LRESULT *result)
{
	// assume we'll use the default handling
	*result = CDRF_DODEFAULT;

	// see what phase we're in
	auto nmd = (LPNMLVCUSTOMDRAW)nm;
	switch (nmd->nmcd.dwDrawStage)
	{
	case CDDS_PREPAINT:
		// ask for item-level drawing
		*result |= CDRF_NOTIFYITEMDRAW;
		break;

	case CDDS_ITEMPREERASE:
		if (nmd->nmcd.dwItemSpec == hotTrackRow)
		{
			CDC *pDC = CDC::FromHandle(nmd->nmcd.hdc);
			CBrush br(hotTrackBg);
			pDC->FillRect(&nmd->nmcd.rc, &br);
		}
		*result = 0;
		break;

	case CDDS_ITEMPREPAINT:
		// if it's the hot-track row, do some extra work
		if (nmd->nmcd.dwItemSpec == hotTrackRow)
		{
			// set the hot-track colors
			nmd->clrText = hotTrackTxt;
			nmd->clrTextBk = hotTrackBg;

			// ask for a post-paint notification so we can add the buttons
			*result |= CDRF_NOTIFYPOSTPAINT;
		}

		// if this is the default item for a command with multiple items,
		// show it in bold
		if (bUseDefaultKeys)
		{
			// If we don't have the bold fonts yet, create it
			if (boldFont.GetSafeHandle() == 0)
			{
				// get a description of the current font in the DC - this is
				// the original font the control normally uses
				LOGFONT lf;
				HGDIOBJ origFont = ::GetCurrentObject(nmd->nmcd.hdc, OBJ_FONT);
				::GetObject(origFont, sizeof(lf), &lf);

				// Boldify it by increasing the weight to the next step up.  If
				// it's already heavier than semibold, go to "black", otherwise
				// go to "bold".
				lf.lfWeight = (lf.lfWeight < FW_SEMIBOLD ? FW_BOLD : FW_BLACK);

				// create the new font
				boldFont.CreateFontIndirect(&lf);
			}

			Cmd::Key *key = keyList.GetItemData((int)nmd->nmcd.dwItemSpec);
			if (key->cmd->CountAssignedKeys() > 1 && key == key->cmd->defaultKey)
			{
				::SelectObject(nmd->nmcd.hdc, boldFont.GetSafeHandle());
				*result |= CDRF_NEWFONT;
			}
		}
		break;

	case CDDS_ITEMPOSTPAINT:
		// draw the list icons for the active row
		if (nmd->nmcd.dwItemSpec == hotTrackRow)
		{
			CDC *pDC = CDC::FromHandle(nmd->nmcd.hdc);
			DrawListIcons(*pDC);
		}
		break;
	}
}

void KeyboardDialog::DrawListIcons(CDC &dc)
{
	// there's nothing to do if there's no hot-track row
	if (hotTrackRow == -1)
		return;

	// get the key item bounds
	CRect rc;
	keyList.GetSubItemRect(hotTrackRow, 1, LVIR_BOUNDS, rc);

	// put the icons at the right edge, centered vertically
	rc.left = rc.right - ICON_SIZE;
	rc.top = (rc.top + rc.bottom - ICON_SIZE) / 2;

	// draw the Add button - all controls have this button
	icons.Draw(&dc, hotTrackBtn == 0 ? 9 : 10, rc.TopLeft(), ILD_TRANSPARENT);
	rc.OffsetRect(-ICON_SIZE, 0);

	// Draw the Delete button.  This applies if the entry has
	// a non-empty key name, OR the same command has at least
	// one other key associated with it.
	CString keyName = keyList.GetItemText(hotTrackRow, 1);
	Cmd::Key *key = keyList.GetItemData(hotTrackRow);
	if (!keyName.IsEmpty() || key->cmd->keys.size() > 1)
		icons.Draw(&dc, hotTrackBtn == 1 ? 3 : 4, rc.TopLeft(), ILD_TRANSPARENT);
	rc.OffsetRect(-ICON_SIZE, 0);

	// Draw the Default button.  This applies if the command
	// has multiple keys assigned, and we're using the default
	// key feature in this dialog.
	if (bUseDefaultKeys
		&& !keyName.IsEmpty()
		&& key->cmd->CountAssignedKeys() > 1)
		icons.Draw(&dc, hotTrackBtn == 2 || key->cmd->defaultKey == key ? 12 : 13, rc.TopLeft(), ILD_TRANSPARENT);
}

int KeyboardDialog::HitTestListIcon(CPoint mouse)
{
	// get the key item bounds
	CRect rc;
	keyList.GetSubItemRect(hotTrackRow, 1, LVIR_BOUNDS, rc);

	// the icons go at the right edge, centered vertically
	rc.left = rc.right - ICON_SIZE;
	rc.top = (rc.top + rc.bottom - ICON_SIZE) / 2;

	// test the buttons
	for (int i = 0; i < NUM_LIST_BUTTONS; ++i, rc.OffsetRect(-ICON_SIZE, 0))
	{
		if (rc.PtInRect(mouse))
			return i;
	}

	// no hit
	return -1;
}

void KeyboardDialog::OnChangeFilter()
{
	// Kill any previous timer, then start a new timer to update
	// the list with the new filter in a few moments.  Don't do it
	// immediately so that we respond more quickly if the user
	// types several characters quickly.
	KillTimer(FilterChangeTimer);
	SetTimer(FilterChangeTimer, 500, 0);
}

void KeyboardDialog::OnResetAll()
{
	int result = MessageBox(FactoryResetWarningMessage().c_str(),
		LoadStringT(IDS_RESET_WARNING_CAPTION), MB_YESNO | MB_ICONWARNING);

	if (result == IDYES)
	{
		// clear the display list
		keyList.DeleteAllItems();

		// do the factory reset
		FactoryReset();

		// reset the filter and redisplay the command list
		filterBox.SetWindowText(_T(""));
		BuildCommandList();

		// note the changes
		SetDirty(TRUE);
	}
}

// -----------------------------------------------------------------------
//
// Keyboard dialog CListCtrl specialization
//

typedef KeyboardDialog::KeyListCtrl KeyboardDialogListCtrl;
BEGIN_MESSAGE_MAP(KeyboardDialogListCtrl, CListCtrlEx)
	ON_WM_KEYDOWN()
END_MESSAGE_MAP()

BOOL KeyboardDialog::KeyListCtrl::OnCommand(WPARAM wParam, LPARAM lParam)
{
	// forard accelerator-related command messages to the parent
	if (LOWORD(wParam) == IDC_ACCEL_ASSIGNER && GetParent() != 0)
		return (BOOL)GetParent()->SendMessage(WM_COMMAND, wParam, lParam);

	// pass anything else to the base class
	return __super::OnCommand(wParam, lParam);
}

void KeyboardDialog::KeyListCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// pass it to the base class
	__super::OnKeyDown(nChar, nRepCnt, nFlags);

	// get the current selected row 
	int iItem = -1;
	if (GetSelectedCount() == 1)
	{
		// get the item number
		POSITION pos = GetFirstSelectedItemPosition();
		iItem = GetNextSelectedItem(pos);
	}

	// get the parent dialog
	KeyboardDialog *dlg = dynamic_cast<KeyboardDialog *>(GetParent());

	// Check for special editing keys
	switch (nChar)
	{
	case VK_SPACE:
	case VK_RETURN:
		// open the key editor on the current row, if any
		if (dlg != 0 && iItem != -1)
			dlg->ActivateKeyEntry(iItem);
		break;

	case VK_DELETE:
	case VK_BACK:
		// delete/backspace - delete the selected key mapping
		if (dlg != 0 && iItem != -1)
		{
			// delete the item
			dlg->DeleteKeyRow(iItem);

			// select the next item (which now has the same index
			// as the item we deleted)
			SelectItem(iItem);
		}
		break;

	case VK_INSERT:
		if (dlg != 0 && iItem != -1)
			dlg->AddKeyRow(iItem);
		break;
	}
}

void KeyboardDialog::KeyListCtrl::SelectItem(int iItem)
{
	// clear the current selection
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != 0; )
	{
		int ip = GetNextSelectedItem(pos);
		SetItemState(ip, 0, LVIS_SELECTED);
	}

	// select the new item
	if (iItem >= 0)
		SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}


// --------------------------------------------------------------------------
//
// Main keyboard dialog
//

BEGIN_MESSAGE_MAP(MainKeyboardDialog, KeyboardDialog)
	ON_BN_CLICKED(IDC_REMEMBER_JOYSTICKS, OnClickRememberJoysticks)
END_MESSAGE_MAP()

MainKeyboardDialog::MainKeyboardDialog(int dialogId)
	: KeyboardDialog(dialogId)
{
	// we don't use the "default keys" feature in this dialog
	bUseDefaultKeys = FALSE;

	// Enable three-state sorting - state 0 on column 0 represents sorting
	// in canonical order, by button index.
	bThreeStateSort = TRUE;
	sortDir = 0;
}

BOOL MainKeyboardDialog::OnInitDialog()
{
	// do the base class work
	BOOL result = __super::OnInitDialog();

	// hide the accelerator entry control initially, and move it into
	// the list control so that it's clipped to the list's window
	plKeyAssigner.ShowWindow(SW_HIDE);
	plKeyAssigner.SetParent(&keyList);

	// set the initial "remember joysticks" mode
	buttonsByJoystick = ConfigManager::GetInstance()->GetInt(JoystickManager::cv_RememberJSButtonSource);
	UpdateData(false);

	// return the base class result
	return result;
}

void MainKeyboardDialog::DoDataExchange(CDataExchange* pDX)
{
	DDX_Control(pDX, IDC_ACCEL_ASSIGNER, plKeyAssigner);
	DDX_Control(pDX, IDC_REMEMBER_JOYSTICKS, ckButtonsByJoystick);
	DDX_Check(pDX, IDC_REMEMBER_JOYSTICKS, buttonsByJoystick);
	__super::DoDataExchange(pDX);
}

void MainKeyboardDialog::BuildDatabase()
{
	// Build the database from the current command set
	BuildDatabase(false);
}

void MainKeyboardDialog::BuildDatabase(bool bResetToFactory)
{
	// clear the command list
	commands.clear();

	// add the commands
	InputManager::GetInstance()->EnumCommands([this, bResetToFactory](const InputManager::Command &cmdDesc)
	{
		// add the command
		Cmd &cmd = commands.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(cmdDesc.idx),
			std::forward_as_tuple(cmdDesc.idx, cmdDesc.idx, cmdDesc.name, cmdDesc.uiSortOrder))
			.first->second;

		// check if we're resetting to defaults
		if (bResetToFactory)
		{
			if (cmdDesc.defaultKey != -1)
				cmd.AddKey(KBKey(&cmd, cmdDesc.defaultKey));
		}
		else
		{
			// use the configured keys
			for (auto const& button : cmdDesc.buttons)
			{
				switch (button.devType)
				{
				case InputManager::Button::TypeNone:
					// empty placeholder - skip it
					break;

				case InputManager::Button::TypeJS:
					// joystick button
					cmd.AddKey(JSKey(&cmd, button.unit, button.code));
					break;

				case InputManager::Button::TypeKB:
					// keyboard key
					cmd.AddKey(KBKey(&cmd, button.code));
					break;
				}
			}
		}

		// if we didn't add any keys, add a blank placeholder
		if (cmd.keys.size() == 0)
			cmd.AddKey();
	});
}

void MainKeyboardDialog::FactoryReset()
{
	// Rebuild the database from the default keys
	BuildDatabase(true);

	// switch to "don't remember buttons by joystick" mode by default

}

void MainKeyboardDialog::SaveChanges()
{
	// visit each command in our database
	auto im = dynamic_cast<InputManagerWithConfig*>(InputManager::GetInstance());
	for (auto const& cmd : commands)
	{
		// clear the existing keys in the configuration
		int commandIndex = cmd.first;
		im->ClearCommandKeys(commandIndex);

		// add each button from our database
		for (auto const& key : cmd.second.keys)
		{
			// get our key maping object, and use it to construct
			// the button description for the input manager
			KeyMapping *pk = dynamic_cast<KeyMapping *>(&*key);
			if (pk != 0)
				im->AddCommandKey(commandIndex, pk->IMButton());
		}
	}

	// save the new joystick memory flag
	ConfigManager *cm = ConfigManager::GetInstance();
	cm->Set(JoystickManager::cv_RememberJSButtonSource, buttonsByJoystick);

	// Save updates to the configuration
	im->StoreConfig();
	cm->Save();
}

void MainKeyboardDialog::InitImageList()
{
	// Load the image list.  Load it from a PNG so that we can
	// retain alpha transparency.
	CPngImage png;
	png.Load(MAKEINTRESOURCE(IDB_BUTTON_ICONS), G_hInstance);
	cmdImages.Create(24, 24, ILC_COLOR32, 16, 8);
	cmdImages.Add(&png, RGB(255, 255, 255));
	keyList.SetImageList(&cmdImages, LVSIL_SMALL);
}

void MainKeyboardDialog::ActivateKeyEntry(int iItem)
{
	// Activate accelerator key entry mode by placing the entry 
	// field over the key name subitem in the list.  Note that 
	// merely opening a row for editing doesn't actually change
	// the key assignment, so this doesn't affect the dirty bit.

	// the key code is in column 1
	const int iSubItem = 1;

	// store the current key name in the entry control
	plKeyAssigner.Reset(keyList.GetItemText(iItem, iSubItem));

	// position the control over the list item
	CRect rc;
	keyList.GetSubItemRect(iItem, iSubItem, LVIR_BOUNDS, rc);
	plKeyAssigner.SetWindowPos(0, rc.left, rc.top, rc.Width(), rc.Height(), SWP_FRAMECHANGED);

	// show it
	plKeyAssigner.ShowWindow(SW_SHOW);

	// set focus on it
	plKeyAssigner.SetFocus();

	// do the base class work
	__super::ActivateKeyEntry(iItem);
}

void MainKeyboardDialog::DeactivateKeyEntry()
{
	// remove the control
	plKeyAssigner.ShowWindow(SW_HIDE);
}

KeyboardDialog::Cmd::Key *MainKeyboardDialog::GetEnteredKey()
{
	// check for a keyboard key
	int vk = plKeyAssigner.GetKey();
	if (vk != -1)
		return new KBKey(0, vk);

	// check for a joystick key
	int jsUnit;
	int jsButton = plKeyAssigner.GetJS(jsUnit);
	if (jsButton != -1)
	{
		// If we're in "remember joystick source" mode, store
		// the button with the actual logical unit we got from
		// the key press.  Otherwise, we're in "all joysticks
		// are the same" mode, so set the unit to -1 to indicate
		// that the command will be triggered by the same button
		// on any joystick unit.
		if (!buttonsByJoystick)
			jsUnit = -1;

		// return the joystick button descriptor
		return new JSKey(0, jsUnit, jsButton);
	}

	// no key assignment found
	return 0;
}

MainKeyboardDialog::JSKey::JSKey(Cmd *cmd, int unitNo, int button)
	: KeyMapping(cmd)
{
	// store the device data
	this->unitNo = unitNo;
	this->button = button;

	// Unit number -1 means that the button will match input
	// from any joystick.  Other values mean it only matches
	// input from the logical joystick with that index.
	if (unitNo == -1)
	{
		// Unit -1 means that the command will match this same
		// button from any joystick.  Name it simply "Joystick
		// Button N" to indicate that it's generic.
		unitName = _T("*");
		keyName.Format(_T("Joystick button %d"), button);
	}
	else
	{
		// The button is tied to a particular device, so include
		// the device name in the button description.

		// look up the joystick
		JoystickManager::LogicalJoystick *j =
			JoystickManager::GetInstance()->GetLogicalJoystick(unitNo);

		// generate the name
		unitName = j != 0 ? j->prodName.c_str() : _T("Unknown Device");
		keyName.Format(_T("Button %d - %s"), button, unitName.GetString());
	}
}

bool MainKeyboardDialog::JSKey::IsMatch(const Key *other) const
{
	// if it's not a joystick button, it's not a match
	const JSKey *a = dynamic_cast<const JSKey *>(other);
	if (a == 0)
		return false;

	// match the unit number and button number
	return unitNo == a->unitNo && button == a->button;
}

bool MainKeyboardDialog::JSKey::IsConflict(const Key *other) const
{
	// if it's not a joystick button, it doesn't conflict
	const JSKey *a = dynamic_cast<const JSKey *>(other);
	if (a == 0)
		return false;

	// Two joystick buttons conflict if EITHER:
	//
	//  (a) they refer to the same button number and the same unit number, OR
	//  (b) they refer to the same button number, and one refers to "any unit" (-1)
	//
	return button == a->button
		&& (unitNo == a->unitNo || unitNo == -1 || a->unitNo == -1);
}

int MainKeyboardDialog::JSKey::SortOrder(const Key *other) const
{
	// if it's not a joystick button, use the default ordering
	const JSKey *a = dynamic_cast<const JSKey *>(other);
	if (a == 0)
		return __super::SortOrder(other);

	// If they're in different units, sort by unit name.  If 
	// they're the same unit, sort by button number.
	if (unitNo == a->unitNo)
		return button - a->button;
	else
		return unitName.CompareNoCase(a->unitName);
}

MainKeyboardDialog::KBKey::KBKey(Cmd *cmd, int vk)
	: KeyMapping(cmd)
{
	// store the device data
	this->vk = vk;

	// get the key name from the input manager's table
	const TCHAR *f = vk > 0 && vk <= VKE_LAST ? KeyInput::keyName[vk].friendlyName : 0;
	if (f != 0)
		keyName = f;
	else
		keyName.Format(_T("Key code %d"), vk);
}

bool MainKeyboardDialog::KBKey::IsMatch(const Key *other) const
{
	// if it's not a DI key, it's not a match
	const KBKey *a = dynamic_cast<const KBKey *>(other);
	if (a == 0)
		return false;

	// match the key code
	return vk == a->vk;
}

int MainKeyboardDialog::KBKey::SortOrder(const Key *other) const
{
	// if it's not a DI key, use the default ordering
	const KBKey *a = dynamic_cast<const KBKey *>(other);
	if (a == 0)
		return __super::SortOrder(other);

	// use the UI sort key from the DI key list
	int s1 = vk >= 0 && vk <= VKE_LAST ? KeyInput::keyName[vk].sortKey : 100000;
	int s2 = a->vk >= 0 && a->vk <= VKE_LAST ? KeyInput::keyName[a->vk].sortKey : 100000;
	return s1 - s2;
}

void MainKeyboardDialog::OnClickRememberJoysticks()
{
	// show the warning if we just turned this on, and the user
	// hasn't suppressed the warning
	const TCHAR *warningVar = _T("SuppressWarning[RememberButtonsByJoystick]");
	ConfigManager *cm = ConfigManager::GetInstance();
	if (!cm->GetInt(warningVar))
	{
		// show the warning
		MessageBoxWithCheckbox mb(EIT_Information,
			LoadStringT(IDS_REMEMBER_JOYSTICKS_WARNING),
			LoadStringT(IDS_SKIP_WARNING));
		mb.Show(IDD_MSGBOX_WITH_CHECKBOX);

		// if desired, suppress it in the future
		if (mb.IsCheckboxChecked())
			ConfigManager::GetInstance()->Set(warningVar, 1);
	}

	// load the new value from the control, and note the change
	UpdateData(true);
	SetDirty();
}

