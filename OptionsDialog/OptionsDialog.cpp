// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "../Utilities/FileUtil.h"
#include "../Utilities/GlobalConstants.h"
#include "resource.h"
#include "OptionsDialog.h"
#include "KeyboardDialog.h"
#include "AttractModeDialog.h"
#include "CaptureDialog.h"
#include "CaptureFfmpegDialog.h"
#include "CoinsDialog.h"
#include "DMDDialog.h"
#include "DOFDialog.h"
#include "InstCardDialog.h"
#include "GameLaunchDialog.h"
#include "GameWheelDialog.h"
#include "MenuDialog.h"
#include "PathsDialog.h"
#include "StartupDialog.h"
#include "StatuslineDialog.h"
#include "SysGroupDialog.h"
#include "SystemDialog.h"
#include "AudioVideoDialog.h"
#include "LogFileDialog.h"
#include "InfoBoxDialog.h"
#include "FontDialog.h"
#include "MouseDialog.h"
#include "WindowDialog.h"

using namespace TreePropSheet;

// internal application messages
static const UINT MsgDeleteSystemPage = WM_APP + 100;	// delete a system page: WPARAM=page ID

// default start page map
std::unordered_map<std::string, int> OptionsDialog::defaultStartPages;

BEGIN_MESSAGE_MAP(OptionsDialog, CTreePropSheet)
	ON_WM_NCLBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_CAPTURECHANGED()
	ON_WM_SYSCOMMAND()
	ON_WM_INPUT()
	ON_WM_INPUT_DEVICE_CHANGE()
END_MESSAGE_MAP()

OptionsDialog::OptionsDialog(int startPage)
{
	// remember the frame parent and starting page
	this->startPage = startPage;

	// set the title
	SetTitle(_T("Options"));

	// set tree mode
	SetTreeViewMode(true, false, false);

	// use our custom images
	SetButtonImages(IDB_TREE_ARROWS, RGB(255, 255, 255), 6);
}

OptionsDialog::~OptionsDialog()
{
	// delete the pages
	while (pages.size() != 0)
	{
		delete pages.front().page;
		pages.erase(pages.begin());
	}
}

void OptionsDialog::BeforeClose()
{
	// note the final selection
	CPropertyPage *page = GetActivePage();
	if (page != 0)
	{
		PageDesc *pageInfo = findifex(pages, [page](PageDesc &p) { return p.page == page; });
		if (pageInfo != 0)
			defaultStartPages[GetDialogID()] = pageInfo->id;
	}
}

void OptionsDialog::AddPage(CPropertyPage *page, int id, const TCHAR *helpFile)
{
	// add it to the tree control
	__super::AddPage(page);

	// add it to our list
	pages.emplace_back(page, id, helpFile);
}

BOOL OptionsDialog::OnInitDialog()
{
	// do the base class initialization
	BOOL result = __super::OnInitDialog();

	// show the Help button in the frame
	ModifyStyleEx(0, WS_EX_CONTEXTHELP);

	// adjust the tree control styles
	CTreeCtrl *tree = GetPageTreeControl();
	tree->ModifyStyle(TVS_HASLINES, 0);

	// expand all of the top-level items
	for (HTREEITEM ti = tree->GetRootItem();
		ti != NULL; ti = tree->GetNextItem(ti, TVGN_NEXT))
		tree->Expand(ti, TVE_EXPAND);

	// switch to the initial page, if one was specified
	ShowStartPage();

	// done
	return result;
}

void OptionsDialog::ShowStartPage()
{
	// If they want to use the default page, start on the same page 
	// that was selected just before the same dialog was closed on its
	// last appearance.
	if (startPage == DefaultStartPage)
	{
		// See if there's an entry for this dialog in the default start 
		// page table.  This records the last page that was active for
		// each dialog type.
		auto it = defaultStartPages.find(GetDialogID());
		if (it != defaultStartPages.end())
			startPage = it->second;
	}

	// If we didn't find an explicit start page, use the first page in
	// the tree list.  The tree sorts by page name, so this won't 
	// necessarily be the first page in creation order.
	if (startPage == DefaultStartPage)
	{
		// get the first item as shown in the tree control
		auto tree = GetPageTreeControl();
		auto hItem = tree->GetRootItem();

		// Find the first item with a valid page.  Some items aren't
		// associated with pages at all, since we create page-less
		// parent items for nested items.  E.g., there could be
		// "Environment::Keyboard" and "Environment::Mouse", but
		// no "Environment" page.  The tree will still have an item
		// for "Environment", though; it just won't be associated
		// with a page.
		int tabPage = GetTabPageNum(hItem);
		while (hItem != NULL && tabPage < 0)
		{
			hItem = tree->GetChildItem(hItem);
			if (hItem != NULL)
				tabPage = GetTabPageNum(hItem);
		}

		// find the page descriptor for indexed item
		auto it = pages.begin();
		for (int i = 0; i < tabPage && it != pages.end(); ++i, ++it);

		// make this the default start page
		if (it != pages.end())
			this->startPage = it->id;
	}

	// Select the page, if one was specified (or we found a previous page
	// to re-select)
	if (startPage != DefaultStartPage)
	{
		// find the page matching the target ID
		PageDesc *page = findifex(pages, [this](PageDesc &page) { return page.id == startPage; });
		if (page != 0)
			SetActivePage(page->page);
	}
}

const TCHAR *OptionsDialog::GetHelpPage(CPropertyPage *dlgPage)
{
	PageDesc *page = findifex(pages, [dlgPage](PageDesc &page) { return page.page == dlgPage; });
	if (page != 0 && page->helpFile != 0)
		return page->helpFile;
	else
		return DefaultHelpPage();
}

BOOL OptionsDialog::OnCommand(WPARAM wParam, LPARAM lParam)
{
	// if it looks like one of the close buttons, note the
	// current page before exiting
	UINT nID = LOWORD(wParam);
	int nCode = HIWORD(wParam);
	HWND hWndCtrl = (HWND)lParam;
	if (hWndCtrl != 0 && nCode == BN_CLICKED && (nID == IDOK || nID == IDCANCEL))
		BeforeClose();

	// pass it along to the base class handler
	BOOL result = __super::OnCommand(wParam, lParam);

	// On Apply, invoke the default window proc ourselves rather than
	// letting the caller handle it.  The default window proc for the
	// Apply button sends PSN_APPLY notifications to all of the child
	// windows, which invokes the OnApply handlers for pages with
	// outstanding changes, which update the in-memory config object.
	// When that process is finished, we want to save changes to the
	// in-memory config object to the on-disk config file to make the
	// changes permanent, which is the point of Apply.  There's no
	// separate notification for when the PSN_APPLY processing is
	// done, so the only way to hook into its completion is to call
	// that process explicitly as a subroutine, which we can do by
	// invoking the default window proc here.  That normally happens
	// in our caller (in MFC code), but we can prevent our caller
	// from redundantly calling it by returning TRUE, which means
	// that we've fully handled the message and don't want the def
	// window proc called.
	if (nCode == BN_CLICKED && nID == ID_APPLY_NOW)
	{
		// Apply - invoke the default window proc
		DefWindowProc(WM_COMMAND, wParam, lParam);

		// All dirty pages have now been applied.  Save any changes
		// to the in-memory config.
		ConfigManager::GetInstance()->SaveIfDirty();

		// this message has been fully processed - return TRUE to
		// tell MFC not to call the def window proc (as we've just
		// done that ourselves)
		return TRUE;
	}

	// return the result from the base class handler
	return result;
}

void OptionsDialog::ShowHelpPage()
{
	ShowHelpPage(GetHelpPage(GetActivePage()));
}

void OptionsDialog::ShowHelpPage(const TCHAR *helpFile)
{
	// look in the help/ folder
	TCHAR relPath[MAX_PATH];
	PathCombine(relPath, _T("help"), helpFile);

	// get the html file path
	TCHAR path[MAX_PATH];
	GetDeployedFilePath(path, relPath, _T(""));

	// open the file
	ShellExecute(NULL, _T("open"), path, NULL, NULL, SW_SHOW);
}

void OptionsDialog::OnSysCommand(UINT nID, LPARAM lParam)
{
	// on pressing the frame context help button, show the
	// help section for the currently selected page
	switch (nID)
	{
	case SC_CONTEXTHELP:
		// show help for the current page
		ShowHelpPage();

		// Skip the standard system processing, which switches to the
		// "?" cursor for asking for help on an individual control.  We
		// just show help for the whole page, so we don't want to enter
		// per-control help mode.
		return;

	case SC_CLOSE:
		// capture the current active page before closing
		BeforeClose();
		break;
	}

	// inherit the default handling
	__super::OnSysCommand(nID, lParam);
}

void OptionsDialog::OnRawInput(UINT inputCode, HRAWINPUT hRawInput)
{
	// process it through the input manager
	InputManager::GetInstance()->ProcessRawInput(inputCode, hRawInput);

	// do the base class work
	__super::OnRawInput(inputCode, hRawInput);
}

void OptionsDialog::OnInputDeviceChange(USHORT what, HANDLE hDevice)
{
	// process it through the input manager
	InputManager::GetInstance()->ProcessDeviceChange(what, hDevice);
}

// -----------------------------------------------------------------------
// 
// Main PinballY options dialog
//


BEGIN_MESSAGE_MAP(MainOptionsDialog, OptionsDialog)
	ON_MESSAGE(MsgDeleteSystemPage, OnDeleteSystemPage)
	ON_WM_DESTROY()
END_MESSAGE_MAP()


MainOptionsDialog::MainOptionsDialog(
	InitializeDialogPositionCallback initPosCallback, 
	bool isAdminHostRunning,
	SetUpAdminAutoRunCallback setUpAdminAutoRunCallback,
	RECT *pFinalDialogRect,
	int startPage)
	: OptionsDialog(startPage),
	initPosCallback(initPosCallback),
	isAdminHostRunning(isAdminHostRunning),
	setUpAdminAutoRunCallback(setUpAdminAutoRunCallback),
	pFinalDialogRect(pFinalDialogRect)
{
	// Create the pages.  Note that the order of page creation doesn't
	// affect the display order, since we sort the tree dynamically into
	// localized collated order by page name.  That ensures that the tree 
	// is shown in a sane order even if we rename pages, and even in a
	// translated version.
	AddPage(new AudioVideoDialog(IDD_AUDIO_VIDEO), AudioVideoPage, _T("AudioVideoOptions.html"));
	AddPage(new MainKeyboardDialog(IDD_KEYS), KeysPage, _T("ButtonOptions.html"));
	AddPage(new CaptureDialog(IDD_CAPTURE), CapturePage, _T("CaptureOptions.html"));
	AddPage(new CaptureFfmpegDialog(IDD_CAPTURE_FFMPEG), CaptureFfmpegPage, _T("CaptureFfmpegOptions.html"));
	AddPage(new AttractModeDialog(IDD_ATTRACT_MODE), AttractModePage, _T("AttractModeOptions.html"));
	AddPage(new CoinsDialog(IDD_COINS), CoinsPage, _T("CoinOptions.html"));
	AddPage(new DMDDialog(IDD_DMD), DMDPage, _T("RealDMDOptions.html"));
	AddPage(new DOFDialog(IDD_DOF), DOFPage, _T("DOFOptions.html"));
	AddPage(new InstCardDialog(IDD_INST_CARD), InstCardPage, _T("InstCardOptions.html"));
	AddPage(new GameLaunchDialog(IDD_LAUNCH), GameLaunchPage, _T("GameLaunchOptions.html"));
	AddPage(new GameWheelDialog(IDD_GAME_WHEEL), GameWheelPage, _T("GameWheelOptions.html"));
	AddPage(new MenuDialog(IDD_MENUS), MenuPage, _T("MenuOptions.html"));
	AddPage(new PathsDialog(IDD_PATHS), PathsPage, _T("PathOptions.html"));
	AddPage(new StartupDialog(IDD_STARTUP), StartupPage, _T("StartupOptions.html"));
	AddPage(new StatuslineDialog(IDD_STATUSLINE), StatuslinePage, _T("StatuslineOptions.html"));
	AddPage(new LogFileDialog(IDD_LOGGING), LogFilePage, _T("LogFileOptions.html"));
	AddPage(sysGroupDialog = new SysGroupDialog(IDD_SYSTEM_GROUP), SysGroupPage, _T("SystemOptions.html"));
	AddPage(new InfoBoxDialog(IDD_INFOBOX), InfoBoxPage, _T("InfoBoxOptions.html"));
	AddPage(new FontDialog(IDD_FONTS), FontPage, _T("FontOptions.html"));
	AddPage(new MouseDialog(IDD_MOUSE), MousePage, _T("MouseOptions.html"));
	AddPage(new WindowDialog(IDD_WINDOWS), WindowPage, _T("WindowOptions.html"));

	// Add pages for the systems
	auto cfg = ConfigManager::GetInstance();
	for (int i = 1; i < PinballY::Constants::MaxSystemNum; ++i)
	{
		// if this system is populated in the config, add a page for it
		if (cfg->Get(MsgFmt(_T("System%d"), i), nullptr) != nullptr)
			AddPage(new SystemDialog(IDD_SYSTEM, i), SystemBasePage + i, _T("SystemOptions.html"));
	}

	// Set the tree panel to be wide enough for a sample system
	// name.  We don't want to size it based on the actual widest
	// system name, since the user could rename a system to
	// something even longer.  Instead, use a longish name that's
	// within the bounds of what's likely to occur in practice.
	// If this isn't wide enough for the actual data, the tree 
	// panel has a scrollbar, so the user can still see what's
	// there.  But it's nicer not to have to use that in the
	// "typical" case.
	CDC dc;
	dc.CreateCompatibleDC(NULL);
	CFont font;
	font.CreatePointFont(8, _T("MS Shell Dlg"), &dc);
	SetTreeWidth(dc.GetTextExtent(_T("XXX(1) SamplePinballSys 10.0")).cx);
}

MainOptionsDialog::~MainOptionsDialog()
{
}

BOOL MainOptionsDialog::OnInitDialog()
{
	// do the base class initialization
	BOOL result = __super::OnInitDialog();

	// let the caller select an initial window position
	initPosCallback(GetSafeHwnd());

	// If raw input isn't initialized, handle messages ourselves.
	InputManager *im = InputManager::GetInstance();
	if (!im->IsRawInputInitialized())
		im->InitRawInput(m_hWnd);

	// If we have any system dialog items, the system tabs will all have
	// the same generic title from the dialog template.  We need to update
	// them with the actual system names.
	auto cfg = ConfigManager::GetInstance();
	auto tabCtrl = GetTabControl();
	auto treeCtrl = GetPageTreeControl();
	int nPages = tabCtrl->GetItemCount();
	for (int i = 0; i < nPages; ++i)
	{
		// if this is a system page, update its tab title
		if (auto sysPage = dynamic_cast<SystemDialog*>(GetPage(i)); sysPage != nullptr)
		{
			// get the system name from the config
			int sysNum = sysPage->GetSysNum();
			auto name = cfg->Get(MsgFmt(_T("System%d"), sysNum), _T("Untitled"));

			// get the old tab title
			TCITEM ti;
			TCHAR oldName[256];
			ZeroMemory(&ti, sizeof(ti));
			ti.mask = TCIF_TEXT;
			ti.cchTextMax = countof(oldName);
			ti.pszText = oldName;
			tabCtrl->GetItem(i, &ti);

			// Keep the part up to the "::" separator, so that we keep the
			// localized resource text for the group name ("Systems" in the
			// English version).  Just search for the first colon and slap
			// in a null terminator in its place.
			if (TCHAR *colon = _tcschr(oldName, ':'); colon != nullptr)
				*colon = 0;

			// set the new tab title
			MsgFmt tabTitle(_T("%s::(%d) %s"), oldName, sysPage->GetSysNum(), name);
			ti.pszText = const_cast<TCHAR*>(tabTitle.Get());
			tabCtrl->SetItem(i, &ti);
		}
	}

	// rebuild the tree for the newly retitled tabs
	RefillPageTree();

	// Re-show the start page.  The base class does this, but the tree
	// rebuild loses track of it.
	ShowStartPage();

	// return the base class result
	return result;
}

void MainOptionsDialog::OnRenameSystem(SystemDialog *sysDlg)
{
	// find the page in my list
	auto tabCtrl = GetTabControl();
	auto treeCtrl = GetPageTreeControl();
	int nPages = tabCtrl->GetItemCount();
	for (int i = 0; i < nPages; ++i)
	{
		// check if this is the page of interest
		if (GetPage(i) == sysDlg)
		{
			// get the new system name from the dialog
			CString newSysName;
			sysDlg->GetDlgItemText(IDC_EDIT_SYS_NAME, newSysName);

			// get the old tab title
			TCITEM ti;
			TCHAR oldTabName[256];
			ZeroMemory(&ti, sizeof(ti));
			ti.mask = TCIF_TEXT;
			ti.cchTextMax = countof(oldTabName);
			ti.pszText = oldTabName;
			tabCtrl->GetItem(i, &ti);

			// pull out the prefix, which won't change: "System::"
			if (TCHAR *colon = _tcschr(oldTabName, ':'); colon != nullptr)
				*colon = 0;

			// set the new tab title
			MsgFmt newTabName(_T("%s::(%d) %s"), oldTabName, sysDlg->GetSysNum(), newSysName.GetString());
			ti.pszText = const_cast<TCHAR*>(newTabName.Get());
			tabCtrl->SetItem(i, &ti);

			// update the tree control title - use the part after the "::"
			const TCHAR *colon = _tcschr(newTabName.Get(), ':');
			TVITEM tvi;
			tvi.mask = TVIF_HANDLE | TVIF_TEXT;
			tvi.hItem = GetPageTreeItem(i);
			tvi.pszText = const_cast<TCHAR*>(colon != nullptr ? colon + 2 : newTabName.Get());
			treeCtrl->SetItem(&tvi);

			// no need to keep searching
			break;
		}
	}
}

bool MainOptionsDialog::IsSystemNameUnique(SystemDialog *sysDlg)
{
	// get the name of the system of interest
	CString sysName;
	sysDlg->GetDlgItemText(IDC_EDIT_SYS_NAME, sysName);

	// check the other system pages for conflicting names
	auto tabCtrl = GetTabControl();
	int nPages = tabCtrl->GetItemCount();
	for (int i = 0; i < nPages; ++i)
	{
		// if this is another system dialog (but not the one we're testing), check it
		if (auto page = dynamic_cast<SystemDialog*>(GetPage(i)); page != sysDlg && page != nullptr)
		{
			// Get this other system's name.  If the dialog window has been created,
			// get the name from the edit control.  Otherwise use the name from the
			// config, since we obviously haven't edited anything if its window
			// hasn't been created yet.
			CString otherSysName;
			if (page->GetSafeHwnd() != NULL)
			{
				// the window has been created - use the live edit control data
				page->GetDlgItemText(IDC_EDIT_SYS_NAME, otherSysName);
			}
			else
			{
				// not loaded yet - get the name from the configuration
				MsgFmt sysvar(_T("System%d"), page->GetSysNum());
				otherSysName = ConfigManager::GetInstance()->Get(sysvar, _T(""));
			}

			// if it matches our new name, it's not unique
			if (otherSysName == sysName)
				return false;
		}
	}

	// we didn't find any matching names, so it's unique
	return true;
}

void MainOptionsDialog::AddNewSystem()
{
	// Assign a system number for the new system, by scanning for one
	// that isn't currently used in the configuration.
	auto cfg = ConfigManager::GetInstance();
	for (int i = 1; i < PinballY::Constants::MaxSystemNum; ++i)
	{
		// if this system is populated in the config, add a page for it
		if (cfg->Get(MsgFmt(_T("System%d"), i), nullptr) == nullptr)
		{
			// create the new page
			SystemDialog *sd = new SystemDialog(IDD_SYSTEM, i, true);
			AddPage(sd, SystemBasePage + i, _T("SystemOptions.html"));

			// rebuild the tree
			RefillPageTree();

			// switch to the new page
			SetActivePage(sd);

			// set the new system's name
			sd->SetDlgItemText(IDC_EDIT_SYS_NAME, MsgFmt(_T("New System #%d"), i));

			// success
			return;
		}
	}

	// There are too many systems!
	MessageBox(LoadStringT(IDS_ERR_TOO_MANY_SYSTEMS), LoadStringT(IDS_CAPTION_ERROR), MB_OK);
}

void MainOptionsDialog::DeleteSystem(SystemDialog *sysDlg)
{
	// Switch to the group dialog page.  This serves dual purposes:
	// first, so that we land somewhere sensible after the page we're
	// on gets deleted; and second, more subtly, to make sure that the
	// group dialog page actually has an extant window object.  The
	// property sheet container only loads pages when they're displayed,
	// so this page might not have a window yet; and if it doesn't have
	// a window, its "dirty" bit (which keeps track of the unsaved 
	// change represented by the system deletion) won't stick.
	SetActivePage(sysGroupDialog);

	// queue the system for deletion in the System Group page
	sysGroupDialog->MarkForDeletion(sysDlg);

	// Post a message to self to delete the page.  We need to defer
	// this because the UI event that triggered the deletion is coming
	// from a button on the page to be deleted.  Deleting the page will
	// delete the button, which we can't do here because this function
	// call is nested inside a handler function attached to the button;
	// deleting the button could cause problems as we unwind the stack
	// back out of the calling handler.
	PostMessage(MsgDeleteSystemPage, sysDlg->GetSysNum());
}

LRESULT MainOptionsDialog::OnDeleteSystemPage(WPARAM wParam, LPARAM lParam)
{
	// the WPARAM gives the system to be deleted - find it
	int nPages = GetTabControl()->GetItemCount();
	for (int i = 0; i < nPages; ++i)
	{
		// is this a system page?
		if (auto sysPage = dynamic_cast<SystemDialog*>(GetPage(i)); sysPage != nullptr && sysPage->GetSysNum() == wParam)
		{
			// delete the page from the tab control
			RemovePage(sysPage);

			// rebuild the tree
			RefillPageTree();

			// no need to keep looking
			break;
		}
	}

	return 0;
}

void MainOptionsDialog::RefillPageTree()
{
	// do the base class rebuild
	__super::RefillPageTree();

	// find the "System" parent item, and make sure it's expanded
	auto treeCtrl = GetPageTreeControl();
	int nPages = GetTabControl()->GetItemCount();
	for (int i = 0; i < nPages; ++i)
	{
		// is this a system page?
		if (auto sysPage = dynamic_cast<SystemDialog*>(GetPage(i)); sysPage != nullptr)
		{
			// it's a system page - expand its parent item
			treeCtrl->Expand(treeCtrl->GetParentItem(GetPageTreeItem(i)), TVE_EXPAND);

			// we only need to do this for one system, since all of the systems
			// share a common parent; so we can stop looking now
			break;
		}
	}
}

bool MainOptionsDialog::TreeItemSorter(const CString &a, const CString &b)
{
	// compare the items in '::' chunks
	const TCHAR *pa = a.GetString();
	const TCHAR *pb = b.GetString();

	auto GetSeg = [](const TCHAR *&p)
	{
		// find the end of the current segment
		TSTRING segment;
		if (const TCHAR *colon = _tcschr(p, ':'); colon != nullptr)
		{
			segment.assign(p, colon - p);
			p = colon + 1;
			if (*p == ':')
				++p;
		}
		else
		{
			segment = p;
			p += _tcslen(p);
		}

		return segment;
	};
	auto GetNum = [](const TSTRING &s, int &n)
	{
		if (const TCHAR *p = s.c_str(); *p == '(')
		{
			int nDigits = 0;
			int acc = 0;
			for (++p; _istdigit(*p); ++nDigits, ++p)
				acc = acc*10 + (*p - '0');

			if (nDigits > 0 && *p == ')')
			{
				n = acc;
				return true;
			}
		}

		return false;
	};

	for (;;)
	{
		// get the current segments
		TSTRING sa = GetSeg(pa);
		TSTRING sb = GetSeg(pb);

		// if we've reached the end of both strings, they're equal
		if (sa.length() == 0 && sb.length() == 0)
			return false;

		// if both have "(number)" prefixes, sort by the number as an
		// integer value instead of lexically
		int ia, ib;
		if (GetNum(sa, ia) && GetNum(sa, ib))
		{
			// if the numbers differ, compare based on the numbers
			if (ia < ib)
				return true;
			if (ia > ib)
				return false;
		}
		else
		{
			// compare them lexically
			int d = lstrcmpi(sa.c_str(), sb.c_str());
			if (d < 0)
				return true;
			if (d > 0)
				return false;
		}
	}
}

void MainOptionsDialog::OnDestroy()
{
	// pass the final window rect back to the host
	if (pFinalDialogRect != nullptr)
		GetWindowRect(pFinalDialogRect);

	// do the base class work
	__super::OnDestroy();
}
