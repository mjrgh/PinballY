// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "CaptureDialog.h"
#include "../Utilities/Config.h"
#include "../Utilities/AudioCapture.h"

IMPLEMENT_DYNAMIC(CaptureDialog, OptionsPage)

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
	
	varMap.emplace_back(new CkBoxMap(_T("Capture.TwoPassEncoding"), IDC_CK_TWO_PASS_CAPTURE, false));

	varMap.emplace_back(new AudioDeviceMap(_T("Capture.AudioDevice"), IDC_CB_AUDIO_CAPTURE));
}

void CaptureDialog::AudioDeviceMap::InitControl()
{
	// populate the audio capture devices
	EnumDirectShowAudioInputDevices([this](const AudioCaptureDeviceInfo *info)
	{
		// add the item to the combo list
		combo.AddString(info->friendlyName);

		// continue the enumeration
		return true;
	});

	// Select the current value from the configuration.  The default is always
	// at the first item in the combo list (index 0); this is rendered as an
	// empty string in the config, but it's rendered in the combo as "(Default)"
	// or some simliar (possibly localized) string defined in the dialog 
	// resource.  So we need to select index 0 explicitly.
	auto cv = ConfigManager::GetInstance()->Get(configVar, _T(""));
	if (cv[0] == 0)
		combo.SetCurSel(0);
	else
		combo.SelectString(1, cv);
}
