// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "../Utilities/Config.h"
#include "AudioVideoDialog.h"

IMPLEMENT_DYNAMIC(AudioVideoDialog, OptionsPage)

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
}

