// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once
#include "../Utilities/InputManager.h"
#include "OptionsPage.h"
#include "ListCtrlEx.h"
#include "KeyAssignCtrl.h"


class InputManagerWithConfig;

class KeyboardDialog : public OptionsPage
{
	DECLARE_DYNAMIC(KeyboardDialog)

public:
	KeyboardDialog(int dialogId);
	virtual ~KeyboardDialog();

protected:
	// Build the internal command database.  We call this when
	// we first show the dialog, and again if we reset the data.
	virtual void BuildDatabase() = 0;

	// Reset to the factory configuration
	virtual void FactoryReset() = 0;
	virtual const TSTRING FactoryResetWarningMessage() const = 0;

	// Save changes
	virtual void SaveChanges() = 0;

	// Initialize the image list
	virtual void InitImageList() = 0;

	// Activate/deactivate key entry mode for the given row
	virtual void ActivateKeyEntry(int iItem);
	virtual void DeactivateKeyEntry() = 0;

	// Close the accelerator key entry.  If 'commit' is true, we'll
	// update the command with the new key, otherwise we'll discard
	// any changes.
	void CloseKeyEntry(BOOL commit);

	// command image list, from the toolbar manager
	CImageList cmdImages;

	// Build the command list.  We call this when we first show
	// the dialog, and again whenever the user changes the command
	// filter.  This updates the on-screen command list in the
	// list control to reflect the current filtered set.  This
	// doesn't actually change the internal data.
	void BuildCommandList();

	// Command list.  This is a list of all of the application
	// commands we obtain from the customization services object.
	// This provides the underlying data for the list we display
	// in the dialog.
	struct Cmd
	{
		Cmd(int id, int imageIndex, LPCTSTR name, int uiSortOrder)
			: id(id), imageIndex(imageIndex), name(name), uiSortOrder(uiSortOrder)
		{
			defaultKey = 0;
		}

		struct Key;

		// Add a blank key assignment
		Key *AddKey()
		{
			BlankKey b;
			return AddKey(b);
		}

		// Add a shortcut key
		Key *AddKey(const Key &src)
		{
			// add the key
			keys.emplace_back(src.NewClone(this));
			Key *key = keys.back().get();

			// if this is the only key so far, mark it as the default
			if (keys.size() == 1)
				defaultKey = key;

			// return the new key
			return key;
		}

		// Delete a key
		void DelKey(const Key *key)
		{
			// remove the key from the list
			keys.remove_if([key](std::unique_ptr<Key> &ele) { return ele.get() == key; });

			// if this was the default key, select a new default
			if (key == defaultKey)
				defaultKey = keys.size() > 0 ? keys.front().get() : 0;
		}

		// Count the assigned keys.  This counts the number of
		// non-empty keys in our list.
		int CountAssignedKeys() const
		{
			int n = 0;
			for (auto const& it : keys)
			{
				// count it only if it a non-blank name
				if (!it->keyName.IsEmpty())
					++n;
			}
			return n;
		}

		// Find a key in our list matching the given descriptor
		Key *FindKey(const Key *src) const
		{
			// scan our key list
			for (auto const& key : keys)
			{
				if (key->IsMatch(src))
					return key.get();
			}

			// nothing found
			return 0;
		}

		// Check for conflicts with a given key
		Key *FindConflict(const Key *src) const
		{
			for (auto const& key : keys)
			{
				if (key->IsConflict(src))
					return key.get();
			}

			// nothing found
			return 0;
		}

		// command ID, used in menus, toolbars, and WM_COMMAND messages
		int id;

		// "Natural" UI sort order.  This specifies the relative
		// sort order of this command when the three-state sort
		// is in "sort none" mode.  We use this for the key list 
		// to sort the list into related groups of commands for 
		// easy navigation.
		int uiSortOrder;

		// toolbar image index
		int imageIndex;

		// command display name
		CString name;

		// Key assignment.  This is an abstract base class allowing
		// for different key representations.  We subclass this for
		// keyboard keys and joystick butotns.
		struct Key
		{
			Key(Cmd *cmd) : cmd(cmd) { }
			virtual ~Key() { }

			// Clone the key record, setting the Command pointer
			// in the new object to the given parent.  This must be 
			// overridden per subclass to create a copy of the same 
			// final type.
			virtual Key *NewClone(Cmd *cmd) const = 0;

			// Match the contents of the key
			virtual bool IsMatch(const Key *other) const = 0;

			// Check for a conflict with another key.  In most cases,
			// this is the same as IsMatch(), but in some cases 
			// inexact matches can conflict.  For example, "Joystick
			// Button 1" (which is button 1 on *any* joystick) and
			// "Button 1 on Pinscape Controller" conflict.
			virtual bool IsConflict(const Key *other) const = 0;

			// Get the sort order relative to another key
			virtual int SortOrder(const Key *other) const
			{
				// Try sorting by sort group.  If they're in different
				// groups, order by their groups.
				int d = SortGroup() - other->SortGroup();
				if (d != 0)
					return d;

				// They're in the same group, so sort within the group
				// by command name.
				return cmd->name.CompareNoCase(other->cmd->name);
			}

			// Sort class.  This allows sorting a list that mixes multiple
			// classes of keys, such as keyboard keys and joystick buttons.
			// When two unlike objects are compared, we compare them based 
			// on the class group.  This puts all of the objects together 
			// in a group, and sorts within the group based on the
			// individual item contents.
			virtual int SortGroup() const { return 0; }

			// we match on pointer address
			BOOL operator==(const Key &other) const { return this == &other; }

			// parent command object
			Cmd *cmd;

			// User-friendly name for the key.  Subclasses fill
			// this in when setting a key value.
			CString keyName;
		};

		// Blank key - key with no command
		struct BlankKey : Key
		{
			BlankKey(Cmd *cmd = 0) : Key(cmd) { }
			virtual Key *NewClone(Cmd *cmd) const override { return new BlankKey(cmd); }
			virtual bool IsMatch(const Key *other) const override { return false; }
			virtual bool IsConflict(const Key *other) const override { return false; }
			virtual int SortGroup() const { return INT_MAX; } // sort to the end
		};

		// Default key.  A command with multiple keys assigned has
		// one key designated as the default.  This will be set first
		// in the accelerator list so that it's the one listed in
		// menus and tool tips.
		Key *defaultKey;

		// Keyboard shortcuts associated with the command.  A command
		// can have any number of shortcuts (including zero).
		std::list<std::unique_ptr<Key>> keys;
	};
	std::unordered_map<int, Cmd> commands;

	// Get the key entered in the key entry field.  This creates
	// a Cmd::Key object of the appropriate concrete subclass to
	// represent the type of key entered.  The caller is responsible
	// for deleting the object when done.  Returns null if no key
	// was entered.
	virtual Cmd::Key *GetEnteredKey() = 0;

	// Set the sorting column and direction
	static const int SortAsc = 1;
	static const int SortDesc = -1;
	static const int SortNone = 0;
	void SetSorting(int col, int dir);

	// timer event for hot tracking
	static const int HotTrackTimer = 1001;

	// filter change timer
	static const int FilterChangeTimer = 1002;

	// current sorting settings
	int sortCol;
	int sortDir;

	// Three-state sorting enabled.  For the key dialog, we allow
	// sorting in a canonical order.  This is enabled by a third sort
	// state represented by direction "0" in the name column.  No arrow
	// icon is shown in this state.
	BOOL bThreeStateSort;

	// sorting comparators
	static int CALLBACK CompareCommands(LPARAM a, LPARAM b, LPARAM ctx);
	static int CALLBACK CompareKeys(LPARAM a, LPARAM b, LPARAM ctx);

	// basic key comparison - does the comparison without applying the
	// sort direction
	static int BasicCompareKeys(LPARAM la, LPARAM lb, LPARAM ctx);

	// DDX/DDV support
	virtual void DoDataExchange(CDataExchange* pDX);

public:
	// subclass the key list to forward accelerator entry control
	// notifications to the parent
	class KeyListCtrl : public CListCtrlEx
	{
	public:
		// command handler
		virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;

		// Get/set item data.  Each item in this type of list
		// is associated with a Cmd::Key* value.
		void SetItemData(int iItem, Cmd::Key *val) { __super::SetItemData(iItem, (LPARAM)val); }
		Cmd::Key *GetItemData(int iItem) { return (Cmd::Key *)__super::GetItemData(iItem); }

		// select an item
		void SelectItem(int iItem);

		// message handlers
		DECLARE_MESSAGE_MAP()
		afx_msg void OnKeyDown(UINT, UINT, UINT);
	};

protected:
	// Check for changes.  We simply keep a flag that tracks when we
	// make any change to the key list, and consider the dialog dirty
	// if that flag has ever been set.  This isn't as nice as our
	// usual approach of testing to see if the dialog is currently
	// dirty - in that case, making a change and then changing it
	// back would remove themodified flag.  But it's time-consuming
	// to test the whole key list, so we'll keep it simple.
	virtual bool IsModFromConfig() override;
	bool wasEverModified = false;

	// does this dialog type allow assigning default keys?
	BOOL bUseDefaultKeys;

	// Delete a key row in the list, and associated internal data
	void DeleteKeyRow(int iItem);

	// Add a new row that copies the given item
	void AddKeyRow(int iItem);

	// Find the row for a given command key
	int FindKeyRow(const Cmd::Key *key);

	// active accelerator assignment row
	int accelRow;

	// hot tracking row
	int hotTrackRow;
	void SetHotTrackRow(int row);
	void SetHotTrackRow(CPoint mousePos);  // keyList-local coordinates

										   // hot tracking row button, from the right (0=rightmost, 1=next, etc)
	int hotTrackBtn;

	// hot tracking colors
	COLORREF hotTrackBg, hotTrackTxt;

	// list edit icons
	CImageList icons;
	const int ICON_SIZE = 16;
	const int ICON_SPACING = 0;
	const int NUM_LIST_BUTTONS = 3;

	// bold list item font, for showing default command items
	CFont boldFont;

	// draw the list icons for the current hot-track row
	void DrawListIcons(CDC &dc);

	// Hit-test the list icon buttons.  If we hit a button, it's
	// numbered from 0 for the rightmost.  If there's no hit, we
	// return -1.
	int HitTestListIcon(CPoint mouse);

	// Invalidate list items associated with a command
	void InvalidateCommandItems(Cmd *cmd);

	// DDX variables
	CEdit filterBox;
	KeyListCtrl keyList;

public:
	virtual BOOL OnInitDialog() override;
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;
	virtual BOOL OnApply() override;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnTimer(UINT_PTR idEvent);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnChangeFilter();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnClickCol(NMHDR *nm, LRESULT *result);
	afx_msg void OnClickList(NMHDR *nm, LRESULT *result);
	afx_msg void OnScrollList(NMHDR *nm, LRESULT *result);
	afx_msg void OnHotTrackList(NMHDR *nm, LRESULT *result);
	afx_msg void OnCustomDrawList(NMHDR *nm, LRESULT *result);
	afx_msg void OnResetAll();
};

// Keyboard dialog subclass for game-style keys
class MainKeyboardDialog : public KeyboardDialog
{
public:
	MainKeyboardDialog(int dialogId);

	virtual BOOL OnInitDialog() override;

protected:

	// Base class for our key mappings types.  Each key mapping
	// has to know how to construct an InputManager::Button object
	// to describe itself.
	struct KeyMapping : Cmd::Key
	{
		KeyMapping(Cmd *cmd) : Cmd::Key(cmd) { }

		// get the input manager button representation
		virtual InputManager::Button IMButton() const = 0;
	};

	// Joystick button mapping
	struct JSKey : KeyMapping
	{
		JSKey(Cmd *cmd, int unitNo, int button);

		virtual Key *NewClone(Cmd *cmd) const override { return new JSKey(cmd, unitNo, button); }
		virtual bool IsMatch(const Key *other) const override;
		virtual bool IsConflict(const Key *other) const override;
		virtual int SortOrder(const Key *other) const override;
		virtual int SortGroup() const { return 2; }

		virtual InputManager::Button IMButton() const override
		{
			return InputManager::Button(InputManager::Button::TypeJS, unitNo, button);
		}


		// Joystick unit number.  This is the unit number as defined
		// in the core library JoystickManager, which is essentially
		// shorthand for the device GUID.  The unit number is an index
		// into a vector of device GUIDs found during the current
		// program session, so it's stable for the entire session
		// (even if joysticks are connected and disconnected during
		// the session) and lets us look up the full details of the
		// joystick.  It's not permanent across sessions, though, so
		// any persistently stored reference should use the actual
		// GUID instead, since that's (more or less) permanent.
		int unitNo;

		// Unit name.  We get this from the joystick manager if
		// the joystick is connected.
		CString unitName;

		// Button number on the joystick.  This is the 0-based 
		// button number.
		int button;
	};

	// Keybaord key mapping
	struct KBKey : KeyMapping
	{
		KBKey(Cmd *cmd, int vk);

		virtual Key *NewClone(Cmd *cmd) const override { return new KBKey(cmd, vk); }
		virtual bool IsMatch(const Key *other) const override;
		virtual bool IsConflict(const Key *other) const override { return IsMatch(other); }
		virtual int SortOrder(const Key *other) const override;
		virtual int SortGroup() const { return 1; }

		virtual InputManager::Button IMButton() const override
		{
			return InputManager::Button(InputManager::Button::TypeKB, 0, vk);
		}

		// Virtual key code (a standard Windows VK_ code, or our
		// extended VKE_ code)
		int vk;
	};

	// DDX/DDV support
	virtual void DoDataExchange(CDataExchange* pDX);

	// message handler
	DECLARE_MESSAGE_MAP()
	afx_msg void OnClickRememberJoysticks();

	// buttons by joystick status
	CButton ckButtonsByJoystick;
	int buttonsByJoystick;

	// key assigner
	KeyAssignCtrl plKeyAssigner;

	// build the database, optionally resetting to factory default keys
	void BuildDatabase(bool bResetToFactory);

	virtual void BuildDatabase() override;
	virtual void FactoryReset() override;
	virtual void SaveChanges() override;
	virtual void InitImageList() override;
	virtual void ActivateKeyEntry(int iItem) override;
	virtual void DeactivateKeyEntry() override;
	virtual Cmd::Key *GetEnteredKey() override;

	virtual const TSTRING FactoryResetWarningMessage() const
	{
		return LoadStringT(IDS_FACTORY_RESET_WARNING);
	}
};
