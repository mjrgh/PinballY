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

IMPLEMENT_DYNAMIC(SystemDialog, OptionsPage)

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

	// force it into range; use the last entry ("Other") as the fallback
	int n = countof(configClasses) - 1;
	if (i < 0 || i > n)
		i = n;

	// get the class from the config, in upper-case
	TSTRING cfgval = ConfigManager::GetInstance()->Get(configVar, _T(""));
	std::transform(cfgval.begin(), cfgval.end(), cfgval.begin(), ::_totupper);

	// check for a match
	return cfgval != configClasses[i];
}

SystemDialog::SystemDialog(int dialogId, int sysNum, bool isNew) :
	OptionsPage(dialogId),
	sysNum(sysNum),
	isNew(isNew),
	sysClassMap(nullptr)
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
	varMap.emplace_back(new EditStrMap(cv(".Process"), IDC_EDIT_PROC, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".StartupKeys"), IDC_EDIT_STARTUP_KEYS, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".TablePath"), IDC_EDIT_TABLE_FOLDER, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".NVRAMPath"), IDC_EDIT_NVRAM_FOLDER, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".DefExt"), IDC_EDIT_DEFEXT, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".DOFTitlePrefix"), IDC_EDIT_DOF_PREFIX, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".RunBefore"), IDC_EDIT_RUN_BEFORE, _T("")));
	varMap.emplace_back(new EditStrMap(cv(".RunAfter"), IDC_EDIT_RUN_AFTER, _T("")));
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

	// return the base class result
	return ret;
}

BOOL SystemDialog::OnApply()
{
	// Make sure the system name is non-empty
	CString s;
	GetDlgItemText(IDC_EDIT_SYS_NAME, s);
	if (std::regex_match(s.GetString(), std::basic_regex<TCHAR>(_T("\\s*"))))
		SetDlgItemText(IDC_EDIT_SYS_NAME, MsgFmt(_T("New System %d"), sysNum));

	// Do the base class work
	if (__super::OnApply())
	{
		// success - we're no longer "new", since we're in the config now
		isNew = false;
		return true;
	}
	else
		return false;
}

BOOL SystemDialog::OnCommand(WPARAM wParam, LPARAM lParam)
{
	auto cfg = ConfigManager::GetInstance();
	auto StdFolder = [cfg](const TCHAR *cfgVar, const TCHAR *defaultSubFolder) -> TSTRING
	{
		// get the config var
		TCHAR result[MAX_PATH];
		const TCHAR *v = cfg->Get(cfgVar, _T(""));
		if (*v == 0)
		{
			// It's empty/missing, so the default is to use the default
			// subfolder of the PinballX program folder, if it's installed, 
			// or our own program folder.
			const TCHAR *pbxPath = GetPinballXPath();
			if (pbxPath != nullptr)
			{
				// PBX is installed - use the default subfolder there
				PathCombine(result, pbxPath, defaultSubFolder);
				return result;
			}
			else
			{
				// PBX isn't installed - use our own default subfolder
				GetDeployedFilePath(result, defaultSubFolder, _T(""));
				return result;
			}
		}
		else if (PathIsRelative(v))
		{
			// It's specified in the config as a relative path, so it's
			// relative to our install folder.
			GetDeployedFilePath(result, defaultSubFolder, _T(""));
			return result;
		}
		else
		{
			// it's an absolute path - return it exactly as given
			return v;
		}
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
		BrowseSubfolder(IDC_EDIT_DB_FOLDER, StdFolder(_T("TableDatabasePath"), _T("Databases")).c_str());
		break;

	case IDC_BTN_MEDIA_FOLDER:
		BrowseSubfolder(IDC_EDIT_MEDIA_FOLDER, StdFolder(_T("MediaPath"), _T("Media")).c_str());
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

void SystemDialog::BrowseSubfolder(int editID, const TCHAR *parent)
{
	// find the edit control's mapping entry in the list
	if (auto edit = GetEditVarMap(editID); edit != nullptr)
	{
		// combine the parent path and the current subfolder
		CString s;
		edit->GetWindowText(s);
		TCHAR buf[MAX_PATH];
		PathCombine(buf, parent, s);
		TSTRING path = buf;

		// run the folder browser
		if (::BrowseForFolder(path, GetParent()->GetSafeHwnd(), LoadStringT(IDS_BROWSE_FOLDER), BFF_OPT_ALLOW_MISSING_PATH))
		{
			// find the last '\' path separator
			const TCHAR *s = _tcsrchr(path.c_str(), '\\');

			// store only the subfolder name
			edit->SetWindowText(s != nullptr ? s + 1 : path.c_str());
		}
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
		TSTRING steamExe;
		{
			TCHAR buf[MAX_PATH];
			DWORD len = countof(buf);
			if (SUCCEEDED(AssocQueryString(ASSOCF_NONE, ASSOCSTR_EXECUTABLE,
				_T("steam"), _T("Open"), buf, &len)))
				steamExe = buf;
		}

		// Figure out where the file is, depending on the value and
		// the system class:
		//
		// - If the value is [STEAM], substitute the Steam executable
		//   from the class registration
		//
		// - If the value is an absolute path, simply start there
		//
		// - If it's a relative path, and we have an extension, use
		//   the path to the executable associated with the extension.
		//
		// - If it's blank, and there's a filename extension, get the
		//   executable associated with the extension.
		//
		if (path.CompareNoCase(_T("[steam]")) == 0 && steamExe.length() != 0)
		{
			// This is shorthand for the Steam executable as specified
			// in the registry, under the "Steam" program ID.
			path = steamExe.c_str();
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

