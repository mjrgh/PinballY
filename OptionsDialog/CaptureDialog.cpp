// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "CaptureDialog.h"
#include "../Utilities/Config.h"

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
}

