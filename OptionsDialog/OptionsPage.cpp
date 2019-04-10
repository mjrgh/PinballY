// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "OptionsPage.h"
#include "OptionsDialog.h"
#include "resource.h"
#include "../Utilities/Config.h"
#include "../Utilities/PngUtil.h"


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

BOOL OptionsPage::OnApplyFail(HWND ctl)
{
	// Select this page, to direct the user's attention to the locus
	// of the validation error
	if (auto mainDlg = dynamic_cast<MainOptionsDialog*>(GetParent()); mainDlg != nullptr)
		mainDlg->SetActivePage(this);

	// set focus on the control, if possible
	if (ctl != NULL)
	{
		::SetFocus(ctl);
		::SendMessage(ctl, EM_SETSEL, 0, -1);
	}

	// Since the Apply failed, consider the entire save operation to
	// have failed atomically, so roll back to the last saved copy
	// of the configuration
	ConfigManager::GetInstance()->Reload();

	// mark the page as dirty: whatever change triggered an Apply in
	// the first place is still outstanding
	SetDirty(true);

	// return FALSE, so that an OnApply override can return our
	// return value on its way out
	return FALSE;
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

bool OptionsPage::CkBoxEnumMap::GetConfigVar()
{
	// check to see if the config file value matches one of the enumerated values
	if (const TCHAR *s = ConfigManager::GetInstance()->Get(configVar, nullptr); s != nullptr)
	{
		if (_tcsicmp(s, checkedVal.c_str()) == 0)
			return true;
		else if (_tcsicmp(s, uncheckedVal.c_str()) == 0)
			return false;
	}

	// we didn't match an enumerated value, so apply the default value
	return defVal;
}

void OptionsPage::CkBoxEnumMap::SaveConfigVar()
{
	ConfigManager::GetInstance()->Set(configVar, intVar == 0 ? uncheckedVal.c_str() : checkedVal.c_str());
}

bool OptionsPage::CkBoxEnumMap::IsModifiedFromConfig()
{
	bool checked = (ckbox.GetCheck() == BST_CHECKED);
	return checked != GetConfigVar();
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


// -----------------------------------------------------------------------
//
// Special checkbox mapper for the "Keep Windows Open" checkboxes
//

Gdiplus::Bitmap *OptionsPage::KeepWindowCkMap::bmpKeepWinCkbox = nullptr;
int OptionsPage::KeepWindowCkMap::bmpRefs = 0;

OptionsPage::KeepWindowCkMap::KeepWindowCkMap(const TCHAR *configVar, const TCHAR *windowID, int controlID, bool triState) :
	CkBoxMap(configVar, controlID, false), windowID(windowID), triState(triState)
{
	// load the config variable value
	InitConfigVal();

	// if we're a tri-state checkbox, make sure the custom image is loaded
	if (triState)
	{
		++bmpRefs;
		if (bmpKeepWinCkbox == nullptr)
			bmpKeepWinCkbox = GPBitmapFromPNG(IDB_KEEP_WIN_CKBOX);
	}
}

OptionsPage::KeepWindowCkMap::~KeepWindowCkMap()
{
	// if we're a tri-state checkbox, release the custom image 
	if (triState)
	{
		if (--bmpRefs == 0)
		{
			delete bmpKeepWinCkbox;
			bmpKeepWinCkbox = nullptr;
		}
	}
}

void OptionsPage::KeepWindowCkMap::InitConfigVal()
{
	// presume 'unchecked' for a regular checkbox, or 'indeterminate' for
	// a tri-state checkbox
	configVal = triState ? BST_INDETERMINATE : BST_UNCHECKED;

	// get the config var and scan its space-delimited tokens
	const TCHAR *p = ConfigManager::GetInstance()->Get(configVar, _T(""));
	size_t idLen = windowID.length();
	while (*p != 0)
	{
		// skip leading spaces
		for (; _istspace(*p); ++p);

		// find the end of the token
		const TCHAR *tok = p;
		for (; *p != 0 && !_istspace(*p); ++p);

		// if this term is found, the value will be BST_CHECKED or BST_UNCHECKED,
		// according to the presence or absence of a '-' negation marker
		int val = BST_CHECKED;
		if (*tok == '-')
			val = (++tok, BST_UNCHECKED);

		// check for a match to our token
		if (p - tok == idLen && _tcsnicmp(tok, windowID.c_str(), idLen) == 0)
		{
			// it's our token - the value is BST_CHECKED or BST_UNCHECKED, according
			// to the negation status
			configVal = val;
			break;
		}
	}
}

void OptionsPage::KeepWindowCkMap::LoadConfigVar()
{
	intVar = configVal;
}

void OptionsPage::KeepWindowCkMap::SaveConfigVar()
{
	configVal = intVar;
}

bool OptionsPage::KeepWindowCkMap::IsModifiedFromConfig()
{
	return ckbox.GetCheck() != configVal;
}

LRESULT OptionsPage::KeepWindowCkMap::OnCustomDraw(CWnd *dlg, NMHDR *pnmhdr)
{
	// check the custom draw stage
	auto nm = reinterpret_cast<NMCUSTOMDRAW*>(pnmhdr);
	switch (nm->dwDrawStage)
	{
	case CDDS_PREPAINT:
		return CDRF_NOTIFYPOSTPAINT;

	case CDDS_POSTPAINT:
		if (auto ctl = pnmhdr->hwndFrom; ctl != nullptr)
		{
			// get the square at the left of the checkbox area
			RECT rc = nm->rc;
			rc.right = rc.left + rc.bottom - rc.top;

			// erase it by filling it with the parent background color
			DrawThemeParentBackground(ctl, nm->hdc, &rc);

			// figure the current state
			UINT state = dlg->IsDlgButtonChecked(static_cast<int>(pnmhdr->idFrom));
			bool checked = state == BST_CHECKED;
			bool indet = state == BST_INDETERMINATE;
			bool hot = (nm->uItemState & CDIS_HOT) != 0;
			bool clicked = hot && (GetKeyState(VK_LBUTTON) < 0);

			// Figure the offset based on the state.  Each cell in the source
			// image is 32x32 pixels.  The cells are arranged horizontally,
			// in groups of Normal/Hot/Clicked, in order, Checked, Default,
			// Unchecked.
			int xSrc = (checked ? 0 : indet ? 96 : 192) + (clicked ? 64 : hot ? 32 : 0);

			// draw the bitmap
			Gdiplus::Graphics g(nm->hdc);
			g.DrawImage(bmpKeepWinCkbox,
				Gdiplus::Rect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top),
				xSrc, 0, 32, 32, Gdiplus::UnitPixel);
		}
	}

	// use the default handling
	return CDRF_DODEFAULT;
}

void OptionsPage::KeepWindowCkMap::OnApply(std::list<std::unique_ptr<VarMap>> &varMap)
{
	// Scan the control map list for our instances.  Note that
	// we assume that any given dialog has only one set of these
	// controls.  In particular, we assume there's only a single
	// variable name shared by all of the controls.  If we wanted
	// to add multiple sets to a single dialog, we'd have to 
	// partition the results by variable name.
	TSTRING val;
	const TCHAR *configVar = nullptr;
	for (auto &v : varMap)
	{
		// check if this is one of ours
		if (auto w = dynamic_cast<KeepWindowCkMap*>(v.get()); w != nullptr)
		{
			// Assert that the variable name isn't changing.  This
			// enforces our assumption that we only have one set of
			// controls based on a single config variable.  If that
			// assumption is ever broken, this will catch it quickly
			// so that no one has to puzzle over it too long.
			assert(configVar == nullptr || configVar == w->configVar);

			// If we didn't know the config variable name yet, we do now
			configVar = w->configVar;

			// Figure the value to add to the list
			TSTRING ele;
			if (w->configVal == BST_CHECKED)
			{
				// it's checked - add the window keyword as a positive term
				ele = w->windowID;
			}
			else if (w->configVal == BST_UNCHECKED && w->triState)
			{
				// it's unchecked, and this is a tri-state checkbox, so this
				// explicitly disables the window; add the window keyword as
				// a negative term ("-dmd")
				ele = _T("-");
				ele += w->windowID;
			}

			// if the term is non-empty, add it
			if (ele.length() != 0)
			{
				if (val.length() != 0) val += _T(" ");
				val += ele;
			}
		}
	}

	// save the final value
	ConfigManager::GetInstance()->Set(configVar, val.c_str());
}
