// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class AudioVideoDialog : public OptionsPage
{
	DECLARE_DYNAMIC(AudioVideoDialog)

public:
	AudioVideoDialog(int dialogId);
	virtual ~AudioVideoDialog();

protected:
	// set up the VarMap entries
	virtual void InitVarMap() override;

	class SliderMap : public VarMap
	{
	public:
		SliderMap(const TCHAR *configVar, int sliderControlID, int labelControlID, 
			int minVal, int maxVal, int defVal) :
			VarMap(configVar, sliderControlID, slider),
			labelControlID(labelControlID),
			minVal(minVal), maxVal(maxVal), defVal(defVal)
		{
		}

		CSliderCtrl slider;
		int intVar;

		int minVal, maxVal;
		int defVal;

		CStatic label;
		int labelControlID;

		virtual void CreateExtraControls(CWnd *dlg) override
		{
			label.SubclassDlgItem(labelControlID, dlg);
		}

		virtual void InitControl() override
		{
			slider.SetRange(minVal, maxVal);
		}

		virtual void doDDX(CDataExchange *pDX) override;
		virtual void LoadConfigVar() override;
		virtual void SaveConfigVar() override { ConfigManager::GetInstance()->Set(configVar, intVar); }

		void UpdateLabel()
		{
			if (label.m_hWnd != NULL)
				label.SetWindowText(MsgFmt(_T("%d%%"), intVar));
		}
		
		virtual bool IsModifiedFromConfig() override { return intVar != ConfigManager::GetInstance()->GetInt(configVar, defVal); }
	};

	SliderMap *videoVolumeSlider = nullptr;
	SliderMap *buttonVolumeSlider = nullptr;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
};

