// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "CoinsDialog.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(CoinsDialog, OptionsPage)

// predefined pricing models
const CoinsDialog::PricingModel CoinsDialog::pricingModels[] = 
{
	{ _T("US 25c/play"), { { .25f, 1 } } },
	{ _T("US 50c/play"), { { .25f, .5f }, { .5f, 1 } } },
	{ _T("US 50c/75c/$1"), { { .25f, .5f }, { .5f, 1 }, { .75f, 2 }, { 1.0f, 3 } } },
	{ _T("US 3/$1"), { { .25f, .5f },{ .5f, 1 },{ .75f, 1.5f, },{ 1.0f, 3 } } },
	{ _T("US 75c/$2 x 3"), { { .75f, 1 }, { 2, 3 } } },
	{ _T("US 50c/$2 x 5"), { { .25, .5 }, { .50, 1 }, { .75, 1.5 }, { 1, 2 }, { 1.25f, 2.5f }, { 1.5f, 3 }, { 1.75f, 3.5f }, { 2, 5 } } }
};

CoinsDialog::CoinsDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

CoinsDialog::~CoinsDialog()
{
}

void CoinsDialog::InitVarMap()
{
	varMap.emplace_back(new EditFloatMap(_T("Coin1.Value"), IDC_EDIT_COINVAL1, 0.25f));
	varMap.emplace_back(new EditFloatMap(_T("Coin2.Value"), IDC_EDIT_COINVAL2, 0.25f));
	varMap.emplace_back(new EditFloatMap(_T("Coin3.Value"), IDC_EDIT_COINVAL3, 0.25f));
	varMap.emplace_back(new EditFloatMap(_T("Coin4.Value"), IDC_EDIT_COINVAL4, 1.00f));
	varMap.emplace_back(new SpinIntMap(_T("MaxCreditBalance"), IDC_EDIT_MAX_CREDITS, 10, IDC_SPIN_MAX_CREDITS, 0, 100));
	varMap.emplace_back(new PricingVarMap(_T("PricingModel"), IDC_EDIT_CUSTOM_PRICING, _T("")));
}


BOOL CoinsDialog::OnInitDialog()
{
    // do the base class work
    BOOL result = __super::OnInitDialog();

	// initialize the combo box control
	cbPricing.SubclassDlgItem(IDC_CB_PRICING_MODEL, this);

	// populate the pricing model list
	for (auto &p : pricingModels)
	{
		int idx = cbPricing.AddString(p.name);
		cbPricing.SetItemDataPtr(idx, const_cast<PricingModel*>(&p));
	}

	// add the "Custom" level (with a null data item pointer)
	cbPricing.AddString(LoadStringT(IDS_CUSTOM_PRICING));

	// sync the pricing popup with the text
	SyncPricingPopupWithText();

    // return the base class result
    return result;
}

void CoinsDialog::SyncPricingPopupWithText()
{
	// get the current pricing model
	auto pm = FindPricingModel();

	// select the matching popup item
	for (int idx = 0; idx < cbPricing.GetCount(); ++idx)
	{
		if (cbPricing.GetItemDataPtr(idx) == pm)
		{
			cbPricing.SetCurSel(idx);
			break;
		}
	}
}

BOOL CoinsDialog::OnCommand(WPARAM wParam, LPARAM lParam)
{
	// check for a selection in the pricing model box
	if (wParam == MAKEWPARAM(IDC_CB_PRICING_MODEL, CBN_SELCHANGE))
	{
		// get the new selection
		int idx = cbPricing.GetCurSel();
		if (idx >= 0)
		{
			// If the outgoing model is custom, save it in the 'lastCustom'
			// field so that we can restore it later if they switch back
			// to the "Custom" selection in the combo.
			if (FindPricingModel() == nullptr)
				GetDlgItemText(IDC_EDIT_CUSTOM_PRICING, lastCustom);

			// get the new pricing model
			auto p = static_cast<const PricingModel*>(cbPricing.GetItemDataPtr(idx));
			if (p != nullptr)
			{
				// format it into the field
				SetDlgItemText(IDC_EDIT_CUSTOM_PRICING, p->ToString().c_str());
			}
			else
			{
				// it's the custom field - reinstate the last custom pricing model
				SetDlgItemText(IDC_EDIT_CUSTOM_PRICING, lastCustom);
			}
		}
	}
	else if (wParam == MAKEWPARAM(IDC_EDIT_CUSTOM_PRICING, EN_CHANGE))
	{
		// changed the pricing text list - sync the popup
		SyncPricingPopupWithText();
	}

    // do the normal work
    return __super::OnCommand(wParam, lParam);
}

const CoinsDialog::PricingModel *CoinsDialog::FindPricingModel()
{
	// get the current field value
	CString txt;
	GetDlgItemText(IDC_EDIT_CUSTOM_PRICING, txt);

	// parse it into a level array
	PricingModel m;
	m.FromString(txt);

	// look for a matching level descriptor among the predefined levels
	for (auto &pm : pricingModels)
	{
		if (memcmp(pm.levels, m.levels, sizeof(m.levels)) == 0)
			return &pm;
	}

	// not found
	return nullptr;
}

void CoinsDialog::PricingModel::FromString(const TCHAR *str)
{
	// start with all zeroes in the level array
	ZeroMemory(&levels, sizeof(levels));

	// parse the level list into the level array
	int i = 0;
	for (const TCHAR *p = str; *p != 0 && i < countof(levels); ++i)
	{
		// find the next delimiter
		const TCHAR *start = p;
		for (; *p != 0 && *p != '\n' && *p != 'r'; ++p);

		// parse this level
		TSTRING cur(start, p - start);
		if (_stscanf_s(cur.c_str(), _T("%f %f"), &levels[i].value, &levels[i].credits) != 2)
			break;

		// skip delimiters
		for (; *p == '\n' || *p == '\r'; ++p);
	}
}

TSTRING CoinsDialog::PricingModel::ToString() const
{
	auto FloatToStr = [](float f) -> TSTRING
	{
		// format it as a float
		MsgFmt s(_T("%f"), f);

		// strip trailing zeroes after the decimal point
		TSTRING tmp = std::regex_replace(
			s.Get(), std::basic_regex<TCHAR>(_T("(\\.[123456789]*)0+$")), _T("$1"));

		// if that leaves us with a trailing decimal point, remove it
		return std::regex_replace(tmp, std::basic_regex<TCHAR>(_T("\\.$")), _T(""));
	};

	// add each level on a new line
	TSTRING result;
	for (auto &level : levels)
	{
		// stop if we reach a level with a zero value
		if (level.value == 0)
			break;

		// add this item to the result
		result += FloatToStr(level.value) + _T(" ") + FloatToStr(level.credits) + _T("\r\n");
	}

	return result;
}

TSTRING CoinsDialog::PricingVarMap::FromConfig(const TCHAR *str)
{
	// convert the comma delimiters to newline delimiters
	return std::regex_replace(str, std::basic_regex<TCHAR>(_T(",\\s*")), _T("\r\n"));
}

TSTRING CoinsDialog::PricingVarMap::ToConfig(const TCHAR *str)
{
	// strip any trailing newlines
	TSTRING tmp = std::regex_replace(str, std::basic_regex<TCHAR>(_T("[\r\n]+$")), _T(""));

	// convert the newline delimiters to comma delimiters
	return std::regex_replace(tmp, std::basic_regex<TCHAR>(_T("[\r\n]+")), _T(", "));
}

bool CoinsDialog::PricingVarMap::IsModifiedFromConfig()
{
	// parse the config var into a level list
	PricingModel mCfg;
	mCfg.FromString(FromConfig(ConfigManager::GetInstance()->Get(configVar, defVal)).c_str());

	// parse our current value into a level list
	PricingModel mDlg;
	mDlg.FromString(strVar);

	// compare their level lists
	return memcmp(mCfg.levels, mDlg.levels, sizeof(mDlg.levels)) != 0;
}
