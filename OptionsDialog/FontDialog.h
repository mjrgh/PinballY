#pragma once
// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"
#include "FontPreviewCombo/FontPreviewCombo.h"

class FontDialog : public OptionsPage
{
	DECLARE_DYNAMIC(FontDialog)

public:
	FontDialog(int dialogId);
	virtual ~FontDialog();

protected:
	// set up the VarMap entries
	virtual void InitVarMap() override;

	// system font list
	CFontPreviewCombo::Fonts allFonts;

	// combo mapper
	struct FontComboMap : VarMap
	{
		FontComboMap(CFontPreviewCombo::Fonts *allFonts, TCHAR *configVar, int controlID, const TCHAR *defVal) :
			VarMap(configVar, controlID, combo), defVal(defVal),
			allFonts(allFonts)
		{ }

		CString strVar;
		CString defVal;
		CFontPreviewCombo combo;
		CFontPreviewCombo::Fonts *allFonts;

		virtual void InitControl() override
		{
			combo.SetPreviewStyle(CFontPreviewCombo::PreviewStyle::NAME_THEN_SAMPLE);
			combo.Init(allFonts);
			combo.SelectString(0, strVar);
		}

		virtual void doDDX(CDataExchange *pDX) override { DDX_Text(pDX, controlID, strVar); }
		virtual void LoadConfigVar() override { strVar = ConfigManager::GetInstance()->Get(configVar, defVal); }
		virtual void SaveConfigVar() override { ConfigManager::GetInstance()->Set(configVar, strVar.GetString()); }
		virtual bool IsModifiedFromConfig() override { return strVar != ConfigManager::GetInstance()->Get(configVar, defVal); }
	};

	// font definition group
	struct FontVarMap : VarMap
	{
		FontVarMap(CFontPreviewCombo::Fonts *allFonts, const TCHAR *configVar, int fontComboId, int sizeComboId, int weightComboId) :
			VarMap(configVar, fontComboId, fontCombo),
			allFonts(allFonts),
			fontComboId(fontComboId), sizeComboId(sizeComboId), weightComboId(weightComboId)
		{ }

		int fontComboId;
		CFontPreviewCombo fontCombo;
		CString fontVar;
		CFontPreviewCombo::Fonts *allFonts;

		int sizeComboId;
		CComboBox sizeCombo;
		CString sizeVar;

		int weightComboId;
		CComboBox weightCombo;
		CString weightVar;

		virtual void InitControl() override
		{
			fontCombo.SetPreviewStyle(CFontPreviewCombo::PreviewStyle::NAME_THEN_SAMPLE);
			fontCombo.Init(allFonts);
			fontCombo.SelectString(0, fontVar);

			// Windows has the weird notion that owner-drawn combos need
			// to use a different size from regular combos, so the font
			// combo (owner drawn) have a slightly different height from
			// the adjacent size and weight combos.  The contents of the
			// font combo are drawn to look like the default, so we don't
			// need special sizing, and it looks weird to have different
			// heights for adjacent controls that are apparently of the
			// same type.  Fortunately, Windows lets us force the height
			// of a combo's main window to a custom height by setting
			// the "item height" of the pseudo-item -1.  (Note that the
			// naive approach, of setting the window height via, say,
			// SetWindowRect, won't work: combos interpret everything
			// having to do with the window rect as referring to the
			// drop list rect.  Not a great API design, in my opinion,
			// but no one asked me.)
			fontCombo.SetItemHeight(-1, sizeCombo.GetItemHeight(-1));
		}

		virtual void ddxControl(CDataExchange *pDX) override
		{
			DDX_Control(pDX, fontComboId, fontCombo);
			DDX_Control(pDX, sizeComboId, sizeCombo);
			DDX_Control(pDX, weightComboId, weightCombo);
		}

		virtual void doDDX(CDataExchange *pDX) override
		{
			DDX_Text(pDX, fontComboId, fontVar);
			DDX_Text(pDX, sizeComboId, sizeVar);
			DDX_Text(pDX, weightComboId, weightVar);
		}

		// returns tuple<name, size, weight>
		std::tuple<TSTRING, TSTRING, TSTRING> GetConfigVal()
		{
			// try matching the standard format: <size> <weight> <name>
			const TCHAR *val = ConfigManager::GetInstance()->Get(configVar, _T("* * *"));
			static std::basic_regex<TCHAR> pat(_T("\\s*(\\d+(pt)?|\\*)\\s+(\\S+)\\s+(.*)"), std::regex_constants::icase);
			std::match_results<const TCHAR*> m;
			if (std::regex_match(val, m, pat))
			{
				// pull out the matches
				TSTRING size = m[1].str();
				TSTRING weight = m[3].str();
				TSTRING name = m[4].str();

				// strip any "pt" suffix from the size
				if (tstriEndsWith(size.c_str(), _T("pt")))
					size = size.substr(0, size.length() - 2);

				// return the tuple
				return std::make_tuple(name, size, weight);
			}
			else
			{
				// no match - use defaults across the board
				return std::make_tuple<TSTRING, TSTRING, TSTRING>(_T("*"), _T("*"), _T("*"));
			}
		}

		virtual void LoadConfigVar() override
		{
			auto t = GetConfigVal();
			fontVar = std::get<0>(t).c_str();
			sizeVar = std::get<1>(t).c_str();
			weightVar = std::get<2>(t).c_str();
		}

		virtual void SaveConfigVar() override
		{
			TSTRING val;
			if (int sizeVal = _ttoi(sizeVar); sizeVal != 0)
				val = MsgFmt(_T("%dpt "), sizeVal);
			else
				val = _T("* ");

			if (weightVar == _T(""))
				val += _T("* ");
			else
				val += weightVar + _T(" ");

			if (fontVar == _T(""))
				val += _T("*");
			else
				val += fontVar;

			ConfigManager::GetInstance()->Set(configVar, val.c_str());
		}

		virtual bool IsModifiedFromConfig() override
		{
			auto t = GetConfigVal();
			return fontVar != std::get<0>(t).c_str()
				|| sizeVar != std::get<1>(t).c_str()
				|| weightVar != std::get<2>(t).c_str();
		}
	};
};

