// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "../Utilities/Config.h"
#include "AudioVideoDialog.h"

IMPLEMENT_DYNAMIC(AudioVideoDialog, OptionsPage)

BEGIN_MESSAGE_MAP(AudioVideoDialog, OptionsPage)
	ON_WM_HSCROLL()
END_MESSAGE_MAP()

AudioVideoDialog::AudioVideoDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

AudioVideoDialog::~AudioVideoDialog()
{
}

void AudioVideoDialog::InitVarMap()
{
	// set up the basic controls
	varMap.emplace_back(new CkBoxMap(_T("Video.Enable"), IDC_CK_ENABLE_VIDEOS, true));
	varMap.emplace_back(new CkBoxMap(_T("Video.Mute"), IDC_CK_MUTE_VIDEOS, false));
	varMap.emplace_back(new CkBoxMap(_T("Buttons.Mute"), IDC_CK_MUTE_BUTTONS, false));
	varMap.emplace_back(new CkBoxMap(_T("Buttons.MuteRepeat"), IDC_CK_MUTE_REPEAT_BUTTONS, false));
	varMap.emplace_back(new CkBoxMap(_T("VSyncLock"), IDC_CK_VSYNC_LOCK, false));
	varMap.emplace_back(new CkBoxMap(_T("Playfield.Stretch"), IDC_CK_STRETCH_PLAYFIELD, false));
	varMap.emplace_back(videoVolumeSlider = new SliderMap(_T("Video.MasterVolume"), IDC_SLIDER_VIDEO_VOL, IDC_TXT_VIDEO_VOL, 0, 100, 100));
	varMap.emplace_back(buttonVolumeSlider = new SliderMap(_T("Buttons.Volume"), IDC_SLIDER_BUTTON_VOL, IDC_TXT_BUTTON_VOL, 0, 100, 100));
}

void AudioVideoDialog::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	// if we're changing any of our volume sliders, update DDX variables
	// and set the dirty-check timer
	if (videoVolumeSlider->slider.GetSafeHwnd() == pScrollBar->GetSafeHwnd()
		|| buttonVolumeSlider->slider.GetSafeHwnd() == pScrollBar->GetSafeHwnd())
	{
		UpdateData(TRUE);
		SetTimer(DirtyCheckTimerId, 500, NULL);
	}

	__super::OnHScroll(nSBCode, nPos, pScrollBar);
}

void AudioVideoDialog::SliderMap::doDDX(CDataExchange *pDX)
{
	DDX_Slider(pDX, controlID, intVar);
	UpdateLabel();
}

void AudioVideoDialog::SliderMap::LoadConfigVar()
{
	intVar = ConfigManager::GetInstance()->GetInt(configVar, defVal);
	UpdateLabel();
}
