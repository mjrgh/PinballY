// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "OptionsPage.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(OptionsPage, CPropertyPageEx)

BEGIN_MESSAGE_MAP(OptionsPage, CPropertyPageEx)
	ON_WM_TIMER()
END_MESSAGE_MAP()

OptionsPage::OptionsPage(int dialogId) :
	CPropertyPageEx(dialogId),
	m_bIsDirty(false)
{
}

OptionsPage::~OptionsPage()
{
}

BOOL OptionsPage::OnInitDialog()
{
	// do the base class work
	BOOL result = __super::OnInitDialog();

	// set up the variable map
	InitVarMap();

	// create additional controls
	for (auto &v : varMap)
		v->CreateExtraControls(this);

	// load config variables
	auto cm = ConfigManager::GetInstance();
	for (auto &v : varMap)
		v->LoadConfigVar();

	// update control values with the loaded values
	UpdateData(false);

	// initialize controls
	for (auto &v : varMap)
		v->InitControl();

	// return the base class result
	return result;
}

void OptionsPage::DoDataExchange(CDataExchange *pDX)
{
	// do data exchange for all of our variable mappings
	for (auto &v : varMap)
	{
		// set up the control mapping
		v->ddxControl(pDX);

		// get/set values
		v->doDDX(pDX);
	}
}

BOOL OptionsPage::OnApply()
{
	// do the base class work
	__super::OnApply();

	// sync controls
	UpdateData(true);

	// update the configuration
	for (auto &v : varMap)
		v->SaveConfigVar();

	// clear our internal dirty flag
	m_bIsDirty = false;

	// changes accepted
	return true;
}

void OptionsPage::SetDirty(BOOL dirty)
{
	// set our internal dirty bit, and update the property page 
	// modified status
	SetModified(m_bIsDirty = dirty);
}

BOOL OptionsPage::OnCommand(WPARAM wParam, LPARAM lParam)
{
	// check for checkbox/radio button state changes, and list
	// box selection changes, and text field changes
	const DWORD ckStyles = BS_AUTO3STATE | BS_AUTOCHECKBOX | BS_AUTORADIOBUTTON | BS_CHECKBOX | BS_RADIOBUTTON;
	if (HIWORD(wParam) == CBN_SELCHANGE
		|| (HIWORD(wParam) == BN_CLICKED
			&& (GetWindowLongPtr((HWND)lParam, GWL_STYLE) & ckStyles) != 0)
		|| HIWORD(wParam) == EN_CHANGE)
	{
		// Windows edit controls send EN_CHANGE messages when the text
		// changes by way of user edits OR programmatic changes.  The
		// latter is a design error on Microsoft's part, which they 
		// eventually fixed in multi-line edits, but as with all Windows
		// API misfeatures, the original single-line edit behavior is
		// locked in forever for the sake of backward compatibility.
		// Anyway, the problem with the EN_CHANGE on programmatic 
		// updates is that a change notification could be coming from
		// either initialization code or from some other control change
		// event that we're in the process of handling, so responding
		// to it as though it's strictly coming from the user can cause
		// recursive loops and similar problems.  There's no good way
		// to tell whether it's user-generated or programmatic, either.
		//
		// What we *want* to do with this update notification is to 
		// check for unsaved changes made by the user.  In other words,
		// the update event is telling us that the dialog is possibly
		// out of sync with the saved version now, so we should check
		// if it's really out of sync and set the Apply/Save controls
		// accordingly.  But if the update is programmatic, it might
		// be premature to make that check, since other related changes
		// might still be pending.  So rather than looking at the state
		// now, let's set a timer to check again after the current
		// window message has been completed.  
		//
		// The point of the timer isn't really to delay the check for 
		// a given amount of time.  It's just to sequence it after the
		// current window message handler has returned, with the
		// expectation that any group of programmatic updates will all
		// be done within a single message handler.  However, the timer
		// does have a nice side effect, which is that we can use it to
		// defer the update check until after a batch of keystrokes if
		// the user is actively typing in data.  The update check might
		// be slightly time-consuming, so this will keep the check from
		// becoming a drag on responsiveness while typing.
		SetTimer(DirtyCheckTimerId, 500, NULL);
	}

	// do the normal work
	return __super::OnCommand(wParam, lParam);
}

void OptionsPage::OnTimer(UINT_PTR id)
{
	switch (id)
	{
	case DirtyCheckTimerId:
		// this is a one-shot
		KillTimer(id);

		// check if we're modified from the configuration
		if (bool mod = IsModFromConfig(); mod != IsDirty())
		{
			// load the current control settings into member variables
			UpdateData(true);

			// enable/disable the Apply button according to the dirty status
			SetDirty(mod);
		}
		break;
	}

	__super::OnTimer(id);
}

bool OptionsPage::IsModFromConfig()
{
	// refresh variables from the dialog controls
	UpdateData(true);
	
	// check each control for a diff from the config data
	for (auto &v : varMap)
	{
		if (v->controlWnd.m_hWnd != NULL && v->IsModifiedFromConfig())
			return true;
	}

	// no changes detected
	return false;
}

bool OptionsPage::CkBoxMap::IsModifiedFromConfig()
{
	bool checked = (ckbox.GetCheck() == BST_CHECKED);
	return checked != ConfigManager::GetInstance()->GetBool(configVar, defVal);
}

bool OptionsPage::EditStrMap::IsModifiedFromConfig()
{
	// Canonicalize the config value for comparison purposes, by
	// converting it to the display format and then back to the
	// config format.
	TSTRING cfgVal = ToConfig(FromConfig(ConfigManager::GetInstance()->Get(configVar, defVal)).c_str());

	// now convert the current dialog value to a config value, and
	// see if it matches the canonicalized config value
	return (ToConfig(strVar) != cfgVal.c_str());
}

bool OptionsPage::EditIntMap::IsModifiedFromConfig()
{
	// get the new text
	return (intVar != ConfigManager::GetInstance()->GetInt(configVar, defVal));
}

void OptionsPage::EditFloatMap::doDDX(CDataExchange *pDX)
{
	if (pDX->m_bSaveAndValidate)
	{
		// Saving.  Use custom handling to suppress error messages
		// if the format is wrong; just treat it as a zero value.
		HWND hWndCtrl;
		pDX->PrepareEditCtrl(controlID);
		pDX->m_pDlgWnd->GetDlgItem(controlID, &hWndCtrl);
		const int TEXT_BUFFER_SIZE = 400;
		TCHAR szBuffer[TEXT_BUFFER_SIZE];
		::GetWindowText(hWndCtrl, szBuffer, _countof(szBuffer));
		if (_sntscanf_s(szBuffer, _countof(szBuffer), _T("%f"), &floatVar) != 1)
			floatVar = 0.0f;
	}
	else
	{
		// use the default handling
		DDX_Text(pDX, controlID, floatVar);
	}
}

bool OptionsPage::EditFloatMap::IsModifiedFromConfig()
{
	// get the new text
	return (floatVar != ConfigManager::GetInstance()->GetFloat(configVar, defVal));
}

void OptionsPage::RadioStrMap::LoadConfigVar()
{
	// presume we won't find a match
	intVar = -1;

	// get the value from the config
	const TCHAR *val = ConfigManager::GetInstance()->Get(configVar, defVal.c_str());

	// find the matching string value in our list
	for (size_t i = 0; i < nVals; ++i)
	{
		if (_tcsicmp(val, vals[i]) == 0)
		{
			intVar = (int)i;
			break;
		}
	}

	// if we didn't find a match, use the default
	if (intVar < 0)
		SetDefault(val);
}

void OptionsPage::RadioStrMap::SaveConfigVar()
{
	const TCHAR *strVal = intVar >= 0 && (size_t)intVar < nVals ? vals[intVar] : defVal.c_str();
	ConfigManager::GetInstance()->Set(configVar, strVal);
}

bool OptionsPage::RadioStrMap::IsModifiedFromConfig()
{
	const TCHAR *dlgVal = intVar >= 0 && (size_t)intVar < nVals ? vals[intVar] : defVal.c_str();
	const TCHAR *cfgVal = ConfigManager::GetInstance()->Get(configVar, defVal.c_str());
	return _tcsicmp(dlgVal, cfgVal) != 0;
}

TSTRING OptionsPage::StatusMessageMap::FromConfig(const TCHAR *str)
{
	// The config file format uses "|" to separate messages, and "||" as a
	// literal "|".  The edit box shows one message per line instead.  Replace
	// single "|" separators with newlines, and replace stuttered "||" with "|".
	return regex_replace(
		TSTRING(str),
		std::basic_regex<TCHAR>(_T("\\|\\|?")),
		[](const std::match_results<TSTRING::const_iterator> &m) -> TSTRING {
		return (m[0].str() == _T("||")) ? _T("|") : _T("\r\n");
	}).c_str();
}

TSTRING OptionsPage::StatusMessageMap::ToConfig(const TCHAR *str)
{
	// Put it back into our "|" delimited format.  First, remove any
	// trailing newline, so that we don't end up with a trailing "|".
	TSTRING tmp = std::regex_replace(str, std::basic_regex<TCHAR>(_T("(\r\n|\n)$")), _T(""));

	// Insert a space into any blank lines
	tmp = std::regex_replace(tmp, std::basic_regex<TCHAR>(_T("(\r\n|\n)(?=\r\n|\n)")), _T("$1 "));

	// Convert "|" to "||"
	tmp = std::regex_replace(tmp, std::basic_regex<TCHAR>(_T("\\|")), _T("||"));

	// Convert newlines to "|"
	return std::regex_replace(tmp, std::basic_regex<TCHAR>(_T("\r\n|\n")), _T("|")).c_str();
}

