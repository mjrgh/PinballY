// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

// Options dialog

#pragma once
#include "TreePropSheet/TreePropSheet.h"
#include "OptionsDialogExports.h"


class CMainFrame;
class SystemDialog;
class SysGroupDialog;

// Base options dialog
class OptionsDialog : public TreePropSheet::CTreePropSheet
{
public:
	OptionsDialog(int startPage = DefaultStartPage);
	virtual ~OptionsDialog();

	// Default starting page.  If this is specified as the start
	// page in the constructor, we'll use the page that was open 
	// the last time the same dialog was open.
	static const int DefaultStartPage = -1;

	// initialize
	virtual BOOL OnInitDialog() override;

	// show my help page/a specific help page
	void ShowHelpPage();
	void ShowHelpPage(const TCHAR *page);

protected:
	// Get the dialog ID.  This is an arbitrary, unique key string
	// defined by the subclass.  A good choice is simply the subclass's
	// C++ symbol name.  This is used to keep a table of the default
	// start page for each dialog type.  When the dialog is about to
	// be closed, we remember the page ID of the active page, and 
	// stash it in a static map keyed by the dialog ID.  On the next
	// invocation of the same dialog, if the caller doesn't specify
	// a particular starting page, we'll restore the same page that
	// was last active on the last run.
	virtual const char *GetDialogID() const = 0;

	// default start pages by dialog ID
	static std::unordered_map<std::string, int> defaultStartPages;

	// Add a page.  The constructor should use this to add the
	// dialog pages at creation.
	void AddPage(CPropertyPage *page, int id, const TCHAR *helpFile);

	// show the start page
	virtual void ShowStartPage();

	// get the appropriate help file for the current page
	virtual const TCHAR *GetHelpPage(CPropertyPage *dlgPage);

	// get the default help page
	virtual const TCHAR *DefaultHelpPage() { return 0; }

	// note the active page before existing
	void BeforeClose();

	// Page descriptor
	struct PageDesc
	{
		PageDesc(CPropertyPage *page, int id, const TCHAR *helpFile)
			: page(page), id(id), helpFile(helpFile) { }

		// the sub dialog object implementing the page
		CPropertyPage *page;

		// Page ID.  This is an arbitrary ID assigned by the
		// subclass to identify the page.  This is matched to the
		// start page ID passed in the constructor to select the
		// desired initial page when the dialog is shown.
		int id;

		// Help file.  GetHelpPage() returns this help file for
		// the active page by default.
		const TCHAR *helpFile;
	};

	// Page descriptors
	std::list<PageDesc> pages;

	// starting page index
	int startPage;

	// command handler
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;

	// message handlers
	DECLARE_MESSAGE_MAP()
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnRawInput(UINT inputCode, HRAWINPUT hRawInput);
	afx_msg void OnInputDeviceChange(USHORT what, HANDLE hRawInput);
};

// Player options dialog
class MainOptionsDialog : public OptionsDialog
{
public:
	MainOptionsDialog(
		InitializeDialogPositionCallback initPosCallback,
		bool isAdminHostRunning,
		SetUpAdminAutoRunCallback setUpAdminAutoRunCallback,
		RECT *pFinalDialogRect,
		int startPage = DefaultStartPage);
	~MainOptionsDialog();

	// StartPage IDs.  These are unique identifiers for the property
	// sheet pages.  Note that the order of these isn't significant;
	// the tree is sorted dynamically into localized collation order,
	// so that pages are organized alphabetically even in translated
	// versions.
	static const int CapturePage = 1;
	static const int CoinsPage = 2;
	static const int DMDPage = 3;
	static const int KeysPage = 4;
	static const int InstCardPage = 5;
	static const int PathsPage = 6;
	static const int StartupPage = 7;
	static const int StatuslinePage = 8;
	static const int AttractModePage = 9;
	static const int SysGroupPage = 10;
	static const int AudioVideoPage = 11;
	static const int GameLaunchPage = 12;
	static const int MenuPage = 13;
	static const int GameWheelPage = 14;
	static const int LogFilePage = 15;
	static const int InfoBoxPage = 16;
	static const int FontPage = 17;
	
	// system pages are identified by SystemBasePage + <system number>
	static const int SystemBasePage = 1000;

	virtual BOOL OnInitDialog() override;

	// Add a new system.  This creates a blank page for a new
	// system.
	void AddNewSystem();

	// Receive notification that a system name has changed in a
	// System::<name> property page.  This updates the caption for
	// the page and rebuilds the tree control.
	void OnRenameSystem(SystemDialog *sysDlg);

	// Validate that a system name is unique
	bool IsSystemNameUnique(SystemDialog *sysDlg);

	// Delete a system.  This removes the page from the property
	// sheet and marks the system for deletion in the configuration.
	// This doesn't actually commit the deletion, since that has to
	// wait until the Apply/OK step.
	void DeleteSystem(SystemDialog *sysDlg);

	// is the Admin Host running?
	bool IsAdminHostRunning() const { return isAdminHostRunning; }

	// callback to set up the Admin mode auto-run through the Admin Host
	SetUpAdminAutoRunCallback setUpAdminAutoRunCallback;

protected:
	// callback to set the initial dailog position
	InitializeDialogPositionCallback initPosCallback;

	// is the Admin Host running?
	bool isAdminHostRunning;

	// caller RECT to fill in with final dialog position on closing
	RECT *pFinalDialogRect;

	virtual const char *GetDialogID() const override { return "MainOptionsDialog"; }
	virtual const TCHAR *DefaultHelpPage() { return _T("Options.html"); }

	DECLARE_MESSAGE_MAP()
	afx_msg LRESULT OnDeleteSystemPage(WPARAM wParam, LPARAM lParam);
	afx_msg void OnDestroy();

	// refill the page tree
	virtual void RefillPageTree() override;

	// compare items in tree sort order
	virtual bool TreeItemSorter(const CString &a, const CString &b);

	// system group dialog
	SysGroupDialog *sysGroupDialog;
};
