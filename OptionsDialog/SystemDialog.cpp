// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "SystemDialog.h"
#include "OptionsDialog.h"
#include "../Utilities/Config.h"
#include "../Utilities/FileUtil.h"
#include "../Utilities/PBXUtil.h"
#include "../Utilities/Dialog.h"

#include "../Utilities/std_filesystem.h"
namespace fs = std::filesystem;


IMPLEMENT_DYNAMIC(SystemDialog, OptionsPage)

BEGIN_MESSAGE_MAP(SystemDialog, OptionsPage)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_CK_SHOW_WHEN_RUNNING_BG, OnCustomDraw)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_CK_SHOW_WHEN_RUNNING_DMD, OnCustomDraw)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_CK_SHOW_WHEN_RUNNING_REALDMD, OnCustomDraw)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_CK_SHOW_WHEN_RUNNING_TOPPER, OnCustomDraw)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_CK_SHOW_WHEN_RUNNING_INSTCARD, OnCustomDraw)
END_MESSAGE_MAP()

// System class IDs, as stored in the config file for SystemN.Class
//
// IMPORTANT: The prepopulated list data for the System Class combo
// box in the dialog resource MUST MATCH the order of these entries,
// since we match the displayed combo list item to the config data
// by the index in this list.
const TCHAR *SystemDialog::SysClassMap::configClasses[] = {
	_T("VP"),
	_T("VPX"),
	_T("FP"),
	_T("STEAM"),
	_T("")
};

// default extensions, by system class
const TCHAR *SystemDialog::SysClassMap::defExts[] = {
	_T(".vpt"),
	_T(".vpx"),
	_T(".fpt"),
	nullptr,
	nullptr
};

// config class indices - these must match the list above
namespace SysClass
{
	static const int VP = 0;
	static const int VPX = 1;
	static const int FP = 2;
	static const int STEAM = 3;
	static const int OTHER = 4;
}

void SystemDialog::SysClassMap::LoadConfigVar()
{
	// get the class from the config, in upper-case
	TSTRING cfgval = ConfigManager::GetInstance()->Get(configVar, _T(""));
	std::transform(cfgval.begin(), cfgval.end(), cfgval.begin(), ::_totupper);

	// The in-memory variable value is the index in the class
	// list.  If it's not found, use the last entry, which is
	// the catch-all "Other" entry.
	intVar = countof(configClasses) - 1;
	for (int i = 0; i < intVar; ++i)
	{
		if (cfgval == configClasses[i])
		{
			intVar = i;
			break;
		}
	}
}

void SystemDialog::SysClassMap::SaveConfigVar()
{
	// get the current combo selection index
	int i = combo.GetCurSel();

	// force it into range; if it's invalid, use the last entry,
	// which is the catch-all "Other" entry
	int n = countof(configClasses) - 1;
	if (i < 0 || i > n)
		i = n;

	// save it
	ConfigManager::GetInstance()->Set(configVar, configClasses[i]);
}

bool SystemDialog::SysClassMap::IsModifiedFromConfig()
{
	// get the current combo selection index
	int i = combo.GetCurSel();

	// force it into range; use the first entry ("Show") as the default
	int n = countof(configClasses) - 1;
	if (i < 0 || i > n)
		i = n;

	// get the class from the config, in upper-case
	TSTRING cfgval = ConfigManager::GetInstance()->Get(configVar, _T(""));
	std::transform(cfgval.begin(), cfgval.end(), cfgval.begin(), ::_totupper);

	// check for a match
	return cfgval != configClasses[i];
}

void SystemDialog::SwShowMap::LoadConfigVar()
{
	// get the SW_SHOW value from the config file, in upper-case
	TSTRING cfgval = ConfigManager::GetInstance()->Get(configVar, _T("SW_SHOWMINIMIZED"));
	std::transform(cfgval.begin(), cfgval.end(), cfgval.begin(), ::_totupper);

	// default to the exact text
	strVar = cfgval.c_str();

	// if the combo hasn't been loaded yet, defer the index lookup
	if (combo.GetSafeHwnd() == NULL)
		return;

	// scan for a match to the "(SW_xxx)" portion of a combo item
	for (int i = 0; i < combo.GetCount(); ++i)
	{
		// get this item
		CString s;
		combo.GetLBText(i, s);

		// find the part in parens
		std::match_results<const TCHAR*> m;
		static const std::basic_regex<TCHAR> pat(_T(".+\\((SW_\\w+)\\)"));
		if (std::regex_match(s.GetBuffer(), m, pat) && m[1].str() == cfgval)
		{
			// matched it - use this as the value
			strVar = s;
			combo.SetWindowText(strVar);
			break;
		}
	}
}

void SystemDialog::SwShowMap::SaveConfigVar()
{
	// get the current text
	CString s;
	combo.GetWindowText(s);

	// if there's an SW_XXX portion embedded in the string, save just that
	std::match_results<const TCHAR*> m;
	static const std::basic_regex<TCHAR> pat(_T(".*\\b(SW_\\w+)\\b.*"));
	if (std::regex_match(s.GetBuffer(), m, pat))
		s = m[1].str().c_str();

	// save it
	ConfigManager::GetInstance()->Set(configVar, s.GetBuffer());
}

bool SystemDialog::SwShowMap::IsModifiedFromConfig()
{
	// get the current text
	CString s;
	combo.GetWindowText(s);
	s.MakeUpper();

	// if there's an SW_XXX portion embedded in the string, extract it;
	// that's what we'd save, so it's what we want to compare to the config
	std::match_results<const TCHAR*> m;
	static const std::basic_regex<TCHAR> pat(_T(".*\\b(SW_\\w+)\\b.*"));
	if (std::regex_match(s.GetBuffer(), m, pat))
		s = m[1].str().c_str();

	// get the value from the config, in upper-case
	TSTRING cfgval = ConfigManager::GetInstance()->Get(configVar, _T("SW_SHOWMINIMIZED"));
	std::transform(cfgval.begin(), cfgval.end(), cfgval.begin(), ::_totupper);

	// check for a match
	return cfgval != s.GetBuffer();
}

void SystemDialog::TerminateByMap::LoadConfigVar()
{
	// get the value from the config file, in upper-case
	TSTRING cfgval = ConfigManager::GetInstance()->Get(configVar, _T("CloseWindow"));
	std::transform(cfgval.begin(), cfgval.end(), cfgval.begin(), ::_totupper);

	// default to the exact text
	strVar = cfgval.c_str();

	// if the combo hasn't been loaded yet, defer the index lookup
	if (combo.GetSafeHwnd() == NULL)
		return;

	// The combo list strings are of the form "Localized Friendly Name (Value)",
	// where Value is the actual text stored in the config file.  The Value
	// strings aren't localized, as these are config values that are primarily
	// for the computer's consumption.
	//
	// Find the combo list item to select by scanning for a match between the
	// config variable value and the "(Value)" portion of a combo string.
	for (int i = 0; i < combo.GetCount(); ++i)
	{
		// get this item
		CString s;
		combo.GetLBText(i, s);

		// find the "(Value)" part and check for a match to the config value
		std::match_results<const TCHAR*> m;
		static const std::basic_regex<TCHAR> pat(_T(".+\\((\\w+)\\)"));
		if (std::regex_match(s.GetBuffer(), m, pat) && _tcsicmp(m[1].str().c_str(), cfgval.c_str()) == 0)
		{
			// matched it - use this as the value
			strVar = s;
			combo.SetWindowText(strVar);
			break;
		}
	}
}

void SystemDialog::TerminateByMap::SaveConfigVar()
{
	// get the current text
	CString s;
	combo.GetWindowText(s);

	// if there's a "(Value)" portion embedded in the string, save just that;
	// otherwise save the exact text
	std::match_results<const TCHAR*> m;
	static const std::basic_regex<TCHAR> pat(_T(".*\\b(\\w+)\\b.*"));
	if (std::regex_match(s.GetBuffer(), m, pat))
		s = m[1].str().c_str();

	// save it
	ConfigManager::GetInstance()->Set(configVar, s.GetBuffer());
}

bool SystemDialog::TerminateByMap::IsModifiedFromConfig()
{
	// get the current text
	CString s;
	combo.GetWindowText(s);
	s.MakeUpper();

	// if there's a "(Value)" portion embedded in the string, extract it;
	// that's what we'd save, so it's what we want to compare to the config
	std::match_results<const TCHAR*> m;
	static const std::basic_regex<TCHAR> pat(_T(".*\\b(\\w+)\\b.*"));
	if (std::regex_match(s.GetBuffer(), m, pat))
		s = m[1].str().c_str();

	// get the value from the config, in upper-case
	TSTRING cfgval = ConfigManager::GetInstance()->Get(configVar, _T("CloseWindow"));
	std::transform(cfgval.begin(), cfgval.end(), cfgval.begin(), ::_totupper);

	// check for a match
	return cfgval != s.GetBuffer();
}

SystemDialog::SystemDialog(int dialogId, int sysNum, bool isNew) :
	OptionsPage(dialogId),
	sysNum(sysNum),
	isNew(isNew),
	sysClassMap(nullptr),
	swShowMap(nullptr),
	terminateByMap(nullptr)
{
}

SystemDialog::~SystemDialog()
{
}

void SystemDialog::InitVarMap()
{
	// build a variable name based on the system number and a suffix
	auto cv = [this](const CHAR *suffix = "") -> TSTRINGEx
	{
		TSTRINGEx s;
		s.Format(_T("System%d%hs"), sysNum, suffix);
		return s;
	};

	// set up the basic controls
	varMap.emplace_back(new EditStrMap(cv(), IDC_EDIT_SYS_NAME, _T("")));
	varMap.emplace_back(new CkBoxMap(cv(".Enabled"), IDC_CK_ENABLE, true));
	varMap.emplace_back(new EditStrMap(cv(".MediaDir"), IDC_EDIT_MEDIA_FOLDER, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".DatabaseDir"), IDC_EDIT_DB_FOLDER, _T("")));
	varMap.emplace_back(sysClassMap = new SysClassMap(cv(".Class"), IDC_CB_SYS_CLASS));
	varMap.emplace_back(new EditStrMap(cv(".Exe"), IDC_EDIT_EXE, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".Parameters"), IDC_EDIT_PARAMS, _T("")));
	varMap.emplace_back(swShowMap = new SwShowMap(cv(".ShowWindow"), IDC_CB_SHOW_WINDOW));
	varMap.emplace_back(terminateByMap = new TerminateByMap(cv(".TerminateBy"), IDC_CB_TERMINATE_BY));
	varMap.emplace_back(new EditStrMap(cv(".Environment"), IDC_EDIT_ENVIRONMENT, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".Process"), IDC_EDIT_PROC, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".StartupKeys"), IDC_EDIT_STARTUP_KEYS, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".TablePath"), IDC_EDIT_TABLE_FOLDER, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".NVRAMPath"), IDC_EDIT_NVRAM_FOLDER, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".DefExt"), IDC_EDIT_DEFEXT, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".DOFTitlePrefix"), IDC_EDIT_DOF_PREFIX, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".RunBeforePre"), IDC_EDIT_RUN_BEFORE1, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".RunBefore"), IDC_EDIT_RUN_BEFORE2, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".RunAfter"), IDC_EDIT_RUN_AFTER1, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".RunAfterPost"), IDC_EDIT_RUN_AFTER2, _T("")));

	// set up the Keep Window Open controls
	TSTRING showWindowsVar = cv(".ShowWindowsWhileRunning");
	varMap.emplace_back(new KeepWindowCkMap(showWindowsVar.c_str(), _T("bg"), IDC_CK_SHOW_WHEN_RUNNING_BG, true));
	varMap.emplace_back(new KeepWindowCkMap(showWindowsVar.c_str(), _T("dmd"), IDC_CK_SHOW_WHEN_RUNNING_DMD, true));
	varMap.emplace_back(new KeepWindowCkMap(showWindowsVar.c_str(), _T("realdmd"), IDC_CK_SHOW_WHEN_RUNNING_REALDMD, true));
	varMap.emplace_back(new KeepWindowCkMap(showWindowsVar.c_str(), _T("topper"), IDC_CK_SHOW_WHEN_RUNNING_TOPPER, true));
	varMap.emplace_back(new KeepWindowCkMap(showWindowsVar.c_str(), _T("instcard"), IDC_CK_SHOW_WHEN_RUNNING_INSTCARD, true));
}

BOOL SystemDialog::OnInitDialog()
{
	// do the base class initialization
	BOOL ret = __super::OnInitDialog();

	// set up the browse buttons
	btnDbFolder.SubclassDlgItem(IDC_BTN_DB_FOLDER, this);
	btnMediaFolder.SubclassDlgItem(IDC_BTN_MEDIA_FOLDER, this);
	btnExe.SubclassDlgItem(IDC_BTN_EXE, this);
	btnTableFolder.SubclassDlgItem(IDC_BTN_TABLE_FOLDER, this);
	btnNvramFolder.SubclassDlgItem(IDC_BTN_NVRAM_FOLDER, this);
	
	// set up the folder icon buttons
	folderIcon.Load(MAKEINTRESOURCE(IDB_FOLDER_ICON));
	btnDbFolder.SetBitmap(folderIcon);
	btnMediaFolder.SetBitmap(folderIcon);
	btnExe.SetBitmap(folderIcon);
	btnTableFolder.SetBitmap(folderIcon);
	btnNvramFolder.SetBitmap(folderIcon);

	// Explicitly re-load the Show Window and Terminate By combos.  We have to
	// defer these until now because the control isn't loaded when we set up 
	// the VarMap entry, which is where these initializations are normally done.
	// We need the control loaded first to do the initialization properly, since 
	// we need to scan its string list loaded from the dialog resource.
	swShowMap->LoadConfigVar();
	terminateByMap->LoadConfigVar();

	// return the base class result
	return ret;
}

BOOL SystemDialog::ValidateSubfolder(int ctlId, int pathTypeId, const TCHAR *val)
{
	// if the proposed value is null, use the current value
	CString s;
	if (val == nullptr)
	{
		GetDlgItemText(ctlId, s);
		val = s;
	}

	// check for special filename characters
	if (std::regex_search(val, std::basic_regex<TCHAR>(_T("[\\\\/:*?|\"<>]"))))
	{
		MessageBox(MsgFmt(IDS_ERR_BAD_SUBFOLDER, LoadStringT(pathTypeId).c_str(), LoadStringT(IDS_WARN_CAPTION)));
		return false;
	}

	// valid 
	return true;
}

BOOL SystemDialog::OnApply()
{
	// Make sure the system name is non-empty
	CString s;
	GetDlgItemText(IDC_EDIT_SYS_NAME, s);
	if (std::regex_match(s.GetString(), std::basic_regex<TCHAR>(_T("\\s*"))))
	{
		s = MsgFmt(_T("New System %d"), sysNum);
		SetDlgItemText(IDC_EDIT_SYS_NAME, s);
	}

	// Make sure the system name is unique.  Only do this validation if
	// our new setting has been changed from the configuration.
	MsgFmt sysvar(_T("System%d"), sysNum);
	if (s != ConfigManager::GetInstance()->Get(sysvar, _T("")))
	{
		if (auto ps = dynamic_cast<MainOptionsDialog*>(GetParent()); ps != nullptr)
		{
			if (!ps->IsSystemNameUnique(this))
			{
				MessageBox(LoadStringT(IDS_ERR_SYS_NAME_NOT_UNIQUE), LoadStringT(IDS_CAPTION_ERROR));
				return OnApplyFail(GetDlgItem(IDC_EDIT_SYS_NAME));
			}
		}
	}

	// Check the subfolders to make sure they look like valid folder names
	if (!ValidateSubfolder(IDC_EDIT_MEDIA_FOLDER, IDS_PATHTYPE_MEDIA))
		return OnApplyFail(GetDlgItem(IDC_EDIT_MEDIA_FOLDER));
	if (!ValidateSubfolder(IDC_EDIT_DB_FOLDER, IDS_PATHTYPE_DB))
		return OnApplyFail(GetDlgItem(IDC_EDIT_DB_FOLDER));

	// Do the base class work
	if (__super::OnApply())
	{
		// success - we're no longer "new", since we're in the config now
		isNew = false;

		// apply changes to Keep Window Open checkboxes
		KeepWindowCkMap::OnApply(varMap);

		// success
		return true;
	}
	else
	{
		// failed/rejected
		return false;
	}
}

BOOL SystemDialog::OnCommand(WPARAM wParam, LPARAM lParam)
{
	auto cfg = ConfigManager::GetInstance();
	auto StdFolder = [cfg](const TCHAR *cfgVar, const TCHAR *defaultSubFolder) -> TSTRING
	{
		// get the config var
		TCHAR result[MAX_PATH];
		TSTRING v = cfg->Get(cfgVar, _T(""));

		// expand the "[PinballX]" substitution variable
		static const auto pbxPat = std::basic_regex<TCHAR>(_T("\\[pinballx\\]"), std::regex::icase);
		if (std::regex_search(v, pbxPat))
			v = std::regex_replace(v, pbxPat, IfNull(GetPinballXPath(), _T("C:\\PinballX_Not_Installed")));

		// if the path is relative, get the full path relative to the install folder
		if (v.length() == 0 || PathIsRelative(v.c_str()))
		{
			// It's specified in the config as a relative path, so it's
			// relative to our install folder.
			GetDeployedFilePath(result, v.c_str(), _T(""));
			return result;
		}

		// return the path as given
		return v;
	};

	switch (LOWORD(wParam))
	{
	case IDC_EDIT_SYS_NAME:
		// on a name change, notify the parent that it needs to update the
		// tab control title for this page and rebuild the tree control
		if (HIWORD(wParam) == EN_CHANGE)
		{
			if (auto ps = dynamic_cast<MainOptionsDialog*>(GetParent()); ps != nullptr)
				ps->OnRenameSystem(this);
		}
		break;

	case IDC_BTN_SYS_DELETE:
		DeleteSystem();
		break;

	case IDC_BTN_DB_FOLDER:
		BrowseSubfolder(IDC_EDIT_DB_FOLDER, IDS_PATHTYPE_DB, StdFolder(_T("TableDatabasePath"), _T("Databases")).c_str());
		break;

	case IDC_BTN_MEDIA_FOLDER:
		BrowseSubfolder(IDC_EDIT_MEDIA_FOLDER, IDS_PATHTYPE_MEDIA, StdFolder(_T("MediaPath"), _T("Media")).c_str());
		break;

	case IDC_BTN_EXE:
		BrowseExe();
		break;

	case IDC_BTN_TABLE_FOLDER:
		BrowseFolder(IDC_EDIT_TABLE_FOLDER);
		break;

	case IDC_BTN_NVRAM_FOLDER:
		BrowseFolder(IDC_EDIT_NVRAM_FOLDER);
		break;

	case IDC_CB_SYS_CLASS:
		if (HIWORD(wParam) == CBN_SELCHANGE)
			OnSysClassChange();
		break;
	}

	// use the base class handling
	return __super::OnCommand(wParam, lParam);
}

void SystemDialog::DeleteSystem()
{
	// prompt for confirmation
	class ConfirmDialog : public Dialog
	{
	public:
		int result;

	protected:
		virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam) override
		{
			// check for dismiss buttons
			switch (message)
			{
			case WM_INITDIALOG:
				::SetDlgItemText(hDlg, IDC_TXT_CONFIRM, LoadStringT(IDS_CONFIRM_DEL_SYS));
				break;

			case WM_COMMAND:
				switch (LOWORD(wParam))
				{
				case IDC_BTN_DELETE:
				case IDC_BTN_DISABLE:
				case IDCANCEL:
					result = LOWORD(wParam);
					::EndDialog(hDlg, IDOK);
					break;
				}
			}

			// call the base class handler
			return __super::Proc(message, wParam, lParam);
		}	
	};
	ConfirmDialog dlg;
	dlg.Show(IDD_CONFIRM_DELETE_SYS);

	// check the result
	switch (dlg.result) 
	{
	case IDC_BTN_DELETE:
		// tell the parent to delete the system
		if (auto parDlg = dynamic_cast<MainOptionsDialog*>(GetParent()); parDlg != nullptr)
			parDlg->DeleteSystem(this);
		break;

	case IDC_BTN_DISABLE:
		// un-check the DISABLE button
		CheckDlgButton(IDC_CK_ENABLE, BST_UNCHECKED);
		break;
	}
}

void SystemDialog::OnSysClassChange()
{
	// populate an edit control with a default if it's currently empty
	auto Put = [this](int editID, const TCHAR *val)
	{
		// get the current value
		CString s;
		GetDlgItemText(editID, s);

		// if it's blank, set the new default value
		if (s.GetLength() == 0)
			SetDlgItemText(editID, val);
	};

	// get the new system
	int i = sysClassMap->combo.GetCurSel();
	switch (i)
	{
	case SysClass::VP:
		Put(IDC_EDIT_DEFEXT, _T(".vpt"));
		Put(IDC_EDIT_TABLE_FOLDER, _T("Tables"));
		Put(IDC_EDIT_PARAMS, _T("/play -\"[TABLEPATH]\\[TABLEFILE]\""));
		break;

	case SysClass::VPX:
		Put(IDC_EDIT_DEFEXT, _T(".vpx"));
		Put(IDC_EDIT_TABLE_FOLDER, _T("Tables"));
		Put(IDC_EDIT_PARAMS, _T("/play -\"[TABLEPATH]\\[TABLEFILE]\""));
		break;

	case SysClass::FP:
		Put(IDC_EDIT_DEFEXT, _T(".fpt"));
		Put(IDC_EDIT_DOF_PREFIX, _T("FP"));
		Put(IDC_EDIT_TABLE_FOLDER, _T("Tables"));
		Put(IDC_EDIT_PARAMS, _T("/open \"[TABLEPATH]\\[TABLEFILE]\" /play /exit /arcaderender"));
		break;

	case SysClass::STEAM:
		// For Steam-based games, populate the EXE field with "[STEAM]",
		// and supply a template for the launch parameters.
		Put(IDC_EDIT_EXE, _T("[STEAM]"));
		Put(IDC_EDIT_PARAMS, _T("-applaunch <put app ID number here>"));
		Put(IDC_EDIT_PROC, _T("<put app .exe name here>"));
		break;
	}
}

CEdit *SystemDialog::GetEditVarMap(int editID)
{
	// searc the var map for a matching control
	for (auto &v : varMap)
	{
		if (v->controlID == editID)
			return dynamic_cast<CEdit*>(&v->controlWnd);
	}

	// not found
	return nullptr;
}

void SystemDialog::BrowseSubfolder(int editID, int folderTypeID, const TCHAR *parentFolder)
{
	// find the edit control's mapping entry in the list
	if (auto edit = GetEditVarMap(editID); edit != nullptr)
	{
		// get the old value
		CString oldVal;
		edit->GetWindowText(oldVal);

		// get the system name
		CString sysName;
		GetDlgItemText(IDC_EDIT_SYS_NAME, sysName);

		// if it's empty, change it to "System %n"
		if (sysName.GetLength() == 0)
			sysName.Format(_T("System %d"), sysNum);

		// set up the dialog
		class SubfolderDialog : public Dialog
		{
		public:
			SubfolderDialog(SystemDialog *sysdlg, int editID, int folderTypeID, const TCHAR *parentFolder, const TCHAR *sysName, const CString &oldVal) :
				result(0),
				sysdlg(sysdlg),
				editID(editID),
				folderTypeID(folderTypeID),
				parentFolder(parentFolder),
				sysName(sysName),
				oldVal(oldVal)
			{
			}

			int result;
			CString newVal;

			SystemDialog *sysdlg;
			int editID;
			int folderTypeID;
			const TCHAR *parentFolder;
			TSTRING sysName;
			const CString &oldVal;

			virtual INT_PTR Proc(UINT msg, WPARAM wParam, LPARAM lParam) override 
			{
				switch (msg)
				{
				case WM_INITDIALOG:
					Init();
					break;

				case WM_COMMAND:
					if (int ctl = LOWORD(wParam); ctl == IDOK || ctl == IDCANCEL)
					{
						// note the result
						result = ctl;

						// if it's "OK", save the new value
						if (ctl == IDOK)
						{
							// retrieve the new value
							TCHAR buf[MAX_PATH];
							::GetDlgItemText(hDlg, IDC_FLD_SUBFOLDER, buf, countof(buf));

							// validate it
							if (!sysdlg->ValidateSubfolder(editID, folderTypeID, buf))
								return 0;

							// store it
							newVal = buf;
						}
					}
					else if (ctl == IDHELP)
					{
						if (auto od = dynamic_cast<OptionsDialog*>(sysdlg->GetParent()); od != nullptr)
							od->ShowHelpPage(_T("SystemOptionsBrowseSubfolder.html"));
					}
					break;

				case WM_NOTIFY:
					if (auto nm = reinterpret_cast<NMHDR*>(lParam); nm->idFrom == IDC_LIST_FOLDERS)
					{
						switch (nm->code)
						{
						case NM_DBLCLK:
						case NM_CLICK:
							// set the new selected item text
							{
								// get the clicked item's text
								LVITEM lvi;
								TCHAR buf[MAX_PATH];
								ZeroMemory(&lvi, sizeof(lvi));
								lvi.mask = LVIF_TEXT;
								lvi.iItem = reinterpret_cast<NMITEMACTIVATE*>(nm)->iItem;
								lvi.pszText = buf;
								lvi.cchTextMax = countof(buf);
								ListView_GetItem(GetDlgItem(IDC_LIST_FOLDERS), &lvi);

								// store it in the text box
								::SetDlgItemText(hDlg, IDC_FLD_SUBFOLDER, buf);
							}

							// on double-click, send an OK to the parent
							if (nm->code == NM_DBLCLK)
								::PostMessage(hDlg, WM_COMMAND, IDOK, 0);
							break;
						}
					}
					break;
				}

				// use the base class handling
				return __super::Proc(msg, wParam, lParam);
			}

			CImageList images;

			void Init()
			{
				// get the list view size
				HWND lv = GetDlgItem(IDC_LIST_FOLDERS);
				RECT rclv;
				::GetClientRect(lv, &rclv);

				// load the image list for the folder listview
				images.Create(16, 15, ILC_COLOR24 | ILC_MASK, 2, 1);
				CBitmap ilbmp;
				ilbmp.LoadBitmap(IDB_FOLDER_BROWSER_IMAGES);
				images.Add(&ilbmp, RGB(255, 0, 255));
				ListView_SetImageList(lv, images.GetSafeHandle(), LVSIL_SMALL);

				// initialize the column list
				LVCOLUMN col;
				ZeroMemory(&col, sizeof(col));
				col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
				col.fmt = LVCFMT_LEFT;
				col.cx = rclv.right - rclv.left - GetSystemMetrics(SM_CXVSCROLL) - 1;
				col.iSubItem = 0;
				TSTRING colhdr = LoadStringT(IDS_COLHDR_FOLDER);
				col.pszText = colhdr.data();
				ListView_InsertColumn(lv, 0, &col);

				// update the prompt strings with the folder type and system name
				auto folderType = LoadStringT(folderTypeID);
				FormatWindowText(hDlg, folderType.c_str());
				FormatDlgItemText(IDC_TXT_SELECT_SUBFOLDER, folderType.c_str(), sysName.c_str());
				FormatDlgItemText(IDC_TXT_SELECT_SUBFOLDER2, folderType.c_str());
				::SetDlgItemText(hDlg, IDC_TXT_MAIN_FOLDER, parentFolder);

				// populate the list
				int idx = 0;
				std::error_code ec;
				for (auto &file : fs::directory_iterator(parentFolder, ec))
				{
					// only include folders
					if (file.status().type() == fs::file_type::directory)
					{
						// get the name
						TSTRING name = file.path().filename();

						// set up the list item definition
						LVITEM lvi;
						ZeroMemory(&lvi, sizeof(lvi));
						lvi.mask = LVIF_IMAGE | LVIF_TEXT | LVIF_STATE;
						lvi.iItem = idx++;
						lvi.state = 0;
						lvi.stateMask = LVIS_SELECTED;
						lvi.iImage = 1;
						lvi.pszText = name.data();

						// select it, if this is the current name
						if (_tcsicmp(name.c_str(), oldVal) == 0)
							lvi.state |= LVIS_SELECTED;

						// add it to the list
						ListView_InsertItem(lv, &lvi);
					}
				}

				// fill in the current folder name
				::SetDlgItemText(hDlg, IDC_FLD_SUBFOLDER, oldVal);
			}
		};

		// show the dialog
		SubfolderDialog dlg(this, editID, folderTypeID, parentFolder, sysName, oldVal);
		dlg.Show(IDD_BROWSE_SYS_SUBFOLDER);

		// update the dialog value if they selected a new folder
		if (dlg.result == IDOK)
			edit->SetWindowText(dlg.newVal);
	}
}

void SystemDialog::BrowseFolder(int editID)
{
	// find the edit control's mapping entry in the list
	if (auto edit = GetEditVarMap(editID); edit != nullptr)
	{
		// start with the parent path
		CString txt;
		edit->GetWindowText(txt);
		TSTRING path = txt;
		if (::BrowseForFolder(path, GetParent()->GetSafeHwnd(), LoadStringT(IDS_BROWSE_FOLDER)))
			edit->SetWindowText(path.c_str());
	}
}


void SystemDialog::BrowseExe()
{
	// find the edit control's mapping entry in the list
	if (auto edit = GetEditVarMap(IDC_EDIT_EXE); edit != nullptr)
	{
		// get the current value
		CString path;
		edit->GetWindowText(path);
		path.Trim();

		// Get the system class
		int sysClass = sysClassMap->combo.GetCurSel();

		// Get the current filename extension setting.  If there's an
		// entry in the extension field, use that.  Otherwise infer the
		// extension from the system class, if possible.
		CString ext;
		GetDlgItemText(IDC_EDIT_DEFEXT, ext);
		if (ext.GetLength() == 0)
		{
			if (sysClass >= 0 && sysClass < countof(sysClassMap->defExts) && sysClassMap->defExts[sysClass] != nullptr)
				ext = sysClassMap->defExts[sysClass];
		}

		// If we have a default extension, get the registered program
		// for the extension.
		TSTRING registeredExe;
		GetProgramForExt(registeredExe, ext);

		// get the registered Steam executable
		TSTRING steamExe, steamDir;
		{
			TCHAR buf[MAX_PATH];
			DWORD len = countof(buf);
			if (SUCCEEDED(AssocQueryString(ASSOCF_NONE, ASSOCSTR_EXECUTABLE,
				_T("steam"), _T("Open"), buf, &len)))
			{
				// save the full Steam executable name
				steamExe = buf;

				// save the path portion
				PathRemoveFileSpec(buf);
				steamDir = buf;
			}
		}

		// Figure out where the file is, depending on the value and
		// the system class:
		//
		// - If the value is [STEAM], substitute the Steam executable
		//   from the class registration
		//
		// - If the value starts with [STEAMDIR], substitute the Steam
		//   program directory path
		//
		// - If the value is an absolute path, simply start there
		//
		// - If it's a relative path, and we have an extension, use
		//   the path to the executable associated with the extension.
		//
		// - If it's blank, and there's a filename extension, get the
		//   executable associated with the extension.
		//
		static const std::basic_regex<TCHAR> steamDirPat(_T("\\[steamdir\\]"), std::regex_constants::icase);
		if (steamExe.length() != 0 && path.CompareNoCase(_T("[steam]")) == 0)
		{
			// This is shorthand for the Steam executable as specified
			// in the registry, under the "Steam" program ID.
			path = steamExe.c_str();
		}
		else if (steamDir.length() != 0 && std::regex_search(path.GetBuffer(), steamDirPat))
		{
			// Substitute the Steam path for the [steamdir] portion
			path = std::regex_replace(path.GetBuffer(), steamDirPat, steamDir).c_str();
		}
		else if ((path.GetLength() == 0 || PathIsRelative(path)) && registeredExe.length() != 0)
		{
			// If no program name was specified, use the registered
			// program as found.  If a partial program name was given,
			// take the PATH portion of the registered program, and
			// combine it with the relative filename given in the
			// config.
			if (path.GetLength() == 0)
			{
				// no program specified - use the registered program
				// name in its entirety
				path = registeredExe.c_str();
			}
			else
			{
				// partial program name specified - combine the path 
				// from the registered program with the filename from
				// the config
				TCHAR buf[MAX_PATH];
				_tcscpy_s(buf, registeredExe.c_str());
				PathRemoveFileSpec(buf);
				PathAppend(buf, path);
				path = buf;
			}
		}

		// run the file browser
		TSTRING spath = path;
		if (::BrowseForFile(spath, GetParent()->GetSafeHwnd(), LoadStringT(IDS_BROWSE_FILE)))
		{
			// Radio button dialog.  This is a simple dialog subclass
			// where the dialog consists of radio buttons and an OK/Cancel
			// button pair.  The 'result' will be the ID of whichever radio
			// button is selected when OK is clicked, otherwise IDCANCEL.
			class RadioButtonDialog : public Dialog
			{
			public:
				RadioButtonDialog(int initButtonID) :
					curButtonID(initButtonID), result(IDCANCEL)
				{
				}

				// result: the selected radio button if OK was pressed,
				// or IDCANCEL if Cancel was pressed
				int result;

				// currently selected radio button
				int curButtonID;

				virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam)
				{
					switch (message)
					{
					case WM_INITDIALOG:
						::CheckDlgButton(hDlg, curButtonID, BST_CHECKED);
						break;

					case WM_COMMAND:
						switch (LOWORD(wParam))
						{
						case IDCANCEL:
							result = IDCANCEL;
							break;

						case IDOK:
							result = curButtonID;
							break;

						default:
							if (HIWORD(wParam) == BN_CLICKED
								&& (GetWindowLong(GetDlgItem(LOWORD(wParam)), GWL_STYLE) & BS_AUTORADIOBUTTON) != 0)
								curButtonID = LOWORD(wParam);
							break;
						}
					}

					// inherit the default handling
					return __super::Proc(message, wParam, lParam);
				}
			};

			// Now see if we can reverse the "defaulting" process:
			//
			// - If they selected the registered Steam executable, offer to use
			//   "[Steam]" as the result instead of the absolute file path
			//
			// - If the path is in the Steam folder, offer to substitute
			//   "[SteamDir]" for the initial portion of the path
			//
			// - If they selected the registered executable, offer to leave it
			//   blank to use the default, or to use a relative path
			//
			// - If they selected an exectuable in the same folder as the program
			//   associated with the file extension, but it's not the same exe,
			//   offer to use the relative path
			//
			//
			if (steamExe.length() != 0 && _tcsicmp(steamExe.c_str(), spath.c_str()) == 0)
			{
				// they selected the Steam executable - offer to use "[STEAM]" 
				// as the setting
				RadioButtonDialog dlg(IDC_RB_STEAM);
				dlg.Show(IDD_STEAM_DEFAULT);
				if (dlg.result == IDC_RB_STEAM)
				{
					// use [STEAM] as the path
					spath = _T("[STEAM]");
				}
				else if (dlg.result == IDCANCEL)
				{
					// cancel - abort the whole thing
					return;
				}
			}
			else if (steamDir.length() != 0
				&& _tcsicmp(steamDir.c_str(), spath.substr(0, steamDir.length()).c_str()) == 0
				&& spath[steamDir.length()] == '\\')
			{
				// offer to replace the initial portion of the path with [STEAMDIR]
				RadioButtonDialog dlg(IDC_RB_STEAMDIR);
				dlg.Show(IDD_STEAMDIR_DEFAULT);
				if (dlg.result == IDC_RB_STEAMDIR)
				{
					// replace the path prefix with [STEAMDIR]
					spath = TSTRING(_T("[STEAMDIR]")) + spath.substr(steamDir.length());
				}
				else if (dlg.result == IDCANCEL)
				{
					// cancel - abort the whole thing
					return;
				}
			}
			else if (registeredExe.length() != 0 && _tcsicmp(registeredExe.c_str(), spath.c_str()) == 0)
			{
				// they selected the registered program for the file extension -
				// offer options for default and relative paths
				RadioButtonDialog dlg(IDC_RB_DEFAULT_PATH);
				dlg.Show(IDD_REL_OR_DFLT_EXE_PATH);
				if (dlg.result == IDC_RB_DEFAULT_PATH)
				{
					// leave it entirely blank to use the default EXE
					spath = _T("");
				}
				else if (dlg.result == IDC_RB_REL_PATH)
				{
					// use the relative path only
					TSTRING relPath = PathFindFileName(spath.c_str());
					spath = relPath;
				}
				else if (dlg.result == IDCANCEL)
				{
					// cancel - abort the whole thing
					return;
				}
			}
			else
			{
				// Check if the selected executable is in the same folder or a
				// subfolder of the registered executable.  Start by getting the 
				// folder containing the registered exe.
				TCHAR registeredExePath[MAX_PATH];
				_tcscpy_s(registeredExePath, registeredExe.c_str());
				PathRemoveFileSpec(registeredExePath);

				// check if the registered exe's folder is a path prefix of the
				// selected executable path
				size_t registeredExePathLen = _tcslen(registeredExePath);
				if (registeredExePathLen != 0 
					&& registeredExePathLen < spath.length()
					&& spath[registeredExePathLen] == '\\')
				{
					// yes, it's a path prefix - offer to use relative notation
					RadioButtonDialog dlg(IDC_RB_REL_PATH);
					dlg.Show(IDD_REL_EXE_PATH);
					if (dlg.result == IDC_RB_REL_PATH)
					{
						// use the relative path only
						spath = spath.substr(registeredExePathLen + 1);
					}
					else if (dlg.result == IDCANCEL)
					{
						// cancel - abort the whole thing
						return;
					}
				}
			}

			// set the new file
			edit->SetWindowText(spath.c_str());
		}
	}
}

void SystemDialog::BrowseFile(int editID)
{
	// find the edit control's mapping entry in the list
	if (auto edit = GetEditVarMap(editID); edit != nullptr)
	{
		// start with the parent path
		CString txt;
		edit->GetWindowText(txt);
		TSTRING path = txt;
		if (::BrowseForFile(path, GetParent()->GetSafeHwnd(), LoadStringT(IDS_BROWSE_FILE)))
			edit->SetWindowText(path.c_str());
	}
}

bool SystemDialog::IsModFromConfig()
{
	// if we're new since the last Apply, we're modified; otherwise
	// report whatever the base class reports
	return isNew || __super::IsModFromConfig();
}

void SystemDialog::OnCustomDraw(NMHDR *pnmhdr, LRESULT *plResult)
{
	*plResult = KeepWindowCkMap::OnCustomDraw(this, pnmhdr);
}
