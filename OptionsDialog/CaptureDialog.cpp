// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "OptionsDialog.h"
#include "CaptureDialog.h"
#include "../Utilities/Config.h"
#include "../Utilities/AudioCapture.h"

IMPLEMENT_DYNAMIC(CaptureDialog, OptionsPage)

BEGIN_MESSAGE_MAP(CaptureDialog, OptionsPage)
END_MESSAGE_MAP()


CaptureDialog::CaptureDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

CaptureDialog::~CaptureDialog()
{
}

void CaptureDialog::InitVarMap()
{
	// set up the basic controls
	varMap.emplace_back(new SpinIntMap(_T("Capture.StartupDelay"), IDC_EDIT_CAP_STARTUP_DELAY, 10, IDC_SPIN_CAP_STARTUP_DELAY, 0, 120));
	
	static const TCHAR *stopVals[] = { _T("auto"), _T("manual") };
	
	varMap.emplace_back(new CkBoxEnumMap(_T("Capture.PlayfieldImage.Start"), IDC_CK_PF_IMG_MANUAL_START, _T("auto"), _T("manual"), false));

	varMap.emplace_back(new CkBoxEnumMap(_T("Capture.PlayfieldVideo.Start"), IDC_CK_PF_VID_MANUAL_START, _T("auto"), _T("manual"), false));
	varMap.emplace_back(new RadioStrMap(_T("Capture.PlayfieldVideo.Stop"), IDC_RB_PF_VID_TIMED_STOP, _T("timed"), stopVals, 2));
	varMap.emplace_back(new SpinIntMap(_T("Capture.PlayfieldVideo.Time"), IDC_EDIT_CAP_PF_TIME, 30, IDC_SPIN_CAP_PF_TIME, 1, 120));

	varMap.emplace_back(new CkBoxEnumMap(_T("Capture.PlayfieldAudio.Start"), IDC_CK_PF_AUD_MANUAL_START, _T("auto"), _T("manual"), false));
	varMap.emplace_back(new RadioStrMap(_T("Capture.PlayfieldAudio.Stop"), IDC_RB_PF_AUD_TIMED_STOP, _T("timed"), stopVals, 2));
	varMap.emplace_back(new SpinIntMap(_T("Capture.PlayfieldAudio.Time"), IDC_EDIT_CAP_PF_AUD_TIME, 30, IDC_SPIN_CAP_PF_AUD_TIME, 1, 120));

	varMap.emplace_back(new CkBoxEnumMap(_T("Capture.BackglassImage.Start"), IDC_CK_BG_IMG_MANUAL_START, _T("auto"), _T("manual"), false));

	varMap.emplace_back(new CkBoxEnumMap(_T("Capture.BackglassVideo.Start"), IDC_CK_BG_VID_MANUAL_START, _T("auto"), _T("manual"), false));
	varMap.emplace_back(new RadioStrMap(_T("Capture.BackglassVideo.Stop"), IDC_RB_BG_VID_TIMED_STOP, _T("timed"), stopVals, 2));
	varMap.emplace_back(new SpinIntMap(_T("Capture.BackglassVideo.Time"), IDC_EDIT_CAP_BG_TIME, 30, IDC_SPIN_CAP_BG_TIME, 1, 120));
	
	varMap.emplace_back(new CkBoxEnumMap(_T("Capture.DMDImage.Start"), IDC_CK_DMD_IMG_MANUAL_START, _T("auto"), _T("manual"), false));

	varMap.emplace_back(new CkBoxEnumMap(_T("Capture.DMDVideo.Start"), IDC_CK_DMD_VID_MANUAL_START, _T("auto"), _T("manual"), false));
	varMap.emplace_back(new RadioStrMap(_T("Capture.DMDVideo.Stop"), IDC_RB_DMD_VID_TIMED_STOP, _T("timed"), stopVals, 2));
	varMap.emplace_back(new SpinIntMap(_T("Capture.DMDVideo.Time"), IDC_EDIT_CAP_DMD_TIME, 30, IDC_SPIN_CAP_DMD_TIME, 1, 120));
	
	varMap.emplace_back(new CkBoxEnumMap(_T("Capture.TopperImage.Start"), IDC_CK_TP_IMG_MANUAL_START, _T("auto"), _T("manual"), false));

	varMap.emplace_back(new CkBoxEnumMap(_T("Capture.TopperVideo.Start"), IDC_CK_TP_VID_MANUAL_START, _T("auto"), _T("manual"), false));
	varMap.emplace_back(new RadioStrMap(_T("Capture.TopperVideo.Stop"), IDC_RB_TP_VID_TIMED_STOP, _T("timed"), stopVals, 2));
	varMap.emplace_back(new SpinIntMap(_T("Capture.TopperVideo.Time"), IDC_EDIT_CAP_TOPPER_TIME, 30, IDC_SPIN_CAP_TOPPER_TIME, 1, 120));
		
	varMap.emplace_back(new ManualStartButtonMap(_T("Capture.ManualStartStopButton"), IDC_CB_MANUAL_START_BUTTON));
}

const TCHAR *CaptureDialog::ManualStartButtonMap::buttonNames[] = {
	_T("flippers"),
	_T("magnasave"),
	_T("launch"),
	_T("info"),
	_T("instructions")
};

void CaptureDialog::ManualStartButtonMap::LoadConfigVar()
{
	// get the config setting, in lower-case
	TSTRING cfgval = ConfigManager::GetInstance()->Get(configVar, _T("flippers"));
	std::transform(cfgval.begin(), cfgval.end(), cfgval.begin(), ::_totlower);

	// find it in list of valid values to get the popup list index;
	// use the default value at index 0 if we don't find a match
	intVar = 0;
	for (int i = 0; i < countof(buttonNames); ++i)
	{
		if (cfgval == buttonNames[i])
		{
			intVar = i;
			break;
		}
	}
}

void CaptureDialog::ManualStartButtonMap::SaveConfigVar()
{
	// get the current combo selection index
	int i = combo.GetCurSel();

	// force it into range; use the first item as the default if it's out of range
	if (i < 0 || i >= countof(buttonNames))
		i = 0;

	// save it
	ConfigManager::GetInstance()->Set(configVar, buttonNames[i]);
}

bool CaptureDialog::ManualStartButtonMap::IsModifiedFromConfig()
{
	// get the current combo selection index and force it into the valid range
	int i = combo.GetCurSel();
	if (i < 0 || i >= countof(buttonNames))
		i = 0;

	// get the button name from the config
	TSTRING cfgval = ConfigManager::GetInstance()->Get(configVar, _T("flippers"));
	std::transform(cfgval.begin(), cfgval.end(), cfgval.begin(), ::_totlower);

	// check for a match
	return cfgval != buttonNames[i];
}
