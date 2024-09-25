// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "OptionsDialog.h"
#include "CaptureFfmpegDialog.h"
#include "../PinballY/CaptureConfigVars.h"
#include "../Utilities/Config.h"
#include "../Utilities/AudioCapture.h"

IMPLEMENT_DYNAMIC(CaptureFfmpegDialog, OptionsPage)

BEGIN_MESSAGE_MAP(CaptureFfmpegDialog, OptionsPage)
	ON_NOTIFY(NM_CLICK, IDC_LINK_AUDIO_HELP, OnClickAudioHelp)
	ON_NOTIFY(NM_CLICK, IDC_LINK_FFMPEG_OPTS_HELP, OnClickOptsHelp)
END_MESSAGE_MAP()

CaptureFfmpegDialog::CaptureFfmpegDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

CaptureFfmpegDialog::~CaptureFfmpegDialog()
{
}

BOOL CaptureFfmpegDialog::OnInitDialog()
{
	// do the base class initialization
	BOOL ret = __super::OnInitDialog();

	// set up the browser button
	folderIcon.Load(MAKEINTRESOURCE(IDB_FOLDER_ICON));
	btnTempFolder.SubclassDlgItem(IDC_BTN_TEMPFOLDER, this);
	btnTempFolder.SetBitmap(folderIcon);

	// return the result from the base class
	return ret;
}

void CaptureFfmpegDialog::InitVarMap()
{
	varMap.emplace_back(new CkBoxMap(ConfigVars::CaptureTwoPassEncoding, IDC_CK_TWO_PASS_CAPTURE, false));
	varMap.emplace_back(new EditStrMap(ConfigVars::CaptureTempFolder, IDC_EDIT_TEMPFOLDER, _T("")));
	varMap.emplace_back(new AudioDeviceMap(ConfigVars::CaptureAudioDevice, IDC_CB_AUDIO_CAPTURE));
	varMap.emplace_back(new CkBoxEnumMap(ConfigVars::CaptureVideoResLimit, IDC_CK_LIMIT_TO_HD, _T("none"), _T("hd"), false));
	varMap.emplace_back(new EditStrMap(ConfigVars::CaptureVideoCodecPass1, IDC_EDIT_VCODECPASS1, _T("")));
	varMap.emplace_back(new EditStrMap(ConfigVars::CaptureCustomVideoSource, IDC_EDIT_VIDEO_SOURCE_OPTS, _T("")));
	varMap.emplace_back(new EditStrMap(ConfigVars::CaptureCustomVideoCodec, IDC_EDIT_VIDEO_CODEC_OPTS, _T("")));
	varMap.emplace_back(new EditStrMap(ConfigVars::CaptureCustomImageCodec, IDC_EDIT_IMAGE_CODEC_OPTS, _T("")));
	varMap.emplace_back(new EditStrMap(ConfigVars::CaptureCustomAudioSource, IDC_EDIT_AUDIO_SOURCE_OPTS, _T("")));
	varMap.emplace_back(new EditStrMap(ConfigVars::CaptureCustomAudioCodec, IDC_EDIT_AUDIO_CODEC_OPTS, _T("")));
	varMap.emplace_back(new EditStrMap(ConfigVars::CaptureCustomGlobalOptions, IDC_EDIT_GLOBAL_OPTS, _T("")));
}

BOOL CaptureFfmpegDialog::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (LOWORD(wParam))
	{
	case IDC_BTN_TEMPFOLDER:
		BrowseFolder(IDC_EDIT_TEMPFOLDER);
		break;
	}

	// use the base class handling
	return __super::OnCommand(wParam, lParam);
}


afx_msg void CaptureFfmpegDialog::OnClickAudioHelp(NMHDR *pNMHDR, LRESULT *pResult)
{
	if (auto par = dynamic_cast<OptionsDialog*>(GetParent()); par != nullptr)
		par->ShowHelpPage(_T("CaptureOptions_AudioDevice.html"));

	*pResult = 0;
}

afx_msg void CaptureFfmpegDialog::OnClickOptsHelp(NMHDR *pNMHDR, LRESULT *pResult)
{
	if (auto par = dynamic_cast<OptionsDialog*>(GetParent()); par != nullptr)
		par->ShowHelpPage(_T("CaptureOptions_CommandLine.html"));

	*pResult = 0;
}

void CaptureFfmpegDialog::AudioDeviceMap::InitControl()
{
	// get the current config setting
	auto cv = ConfigManager::GetInstance()->Get(configVar, _T(""));

	// Populate the audio capture devices, and note along the way if we
	// come across the current setting from the configuration.
	bool foundCur = false;
	std::vector<TSTRING> devices;
	EnumDirectShowAudioInputDevices([cv, &foundCur, &devices](const AudioCaptureDeviceInfo *info)
	{
		// add it to the list of known devices
		devices.emplace_back(info->friendlyName);

		// note if it's the current setting in the config
		if (_tcsicmp(cv, info->friendlyName) == 0)
			foundCur = true;

		// continue the enumeration
		return true;
	});

	// If we didn't find the current config value, add it to the combo list.
	// This allows the user to keep the current value even though it's not an
	// active device, which could be desirable if the user only temporarily
	// removed the device from the system for some reason.
	if (cv[0] != 0 && !foundCur)
		devices.emplace_back(cv);

	// Sort the list alphabetically before building the combo.  Note that we
	// do the sorting explicitly rather than relying on the combo to do the
	// sorting, since this lets us keep the pre-populated "(Default)" item
	// at the head of the list without having to mess with the string sort
	// order.  "(Default)" stays at the top because it started at the top,
	// and all of the new items get inserted after it.
	std::sort(devices.begin(), devices.end(), [](const TSTRING& a, const TSTRING& b) {
		return lstrcmpi(a.c_str(), b.c_str()) < 0; });

	// add the items to the combo
	for (auto &dev : devices)
		combo.AddString(dev.c_str());

	// Select the current value from the configuration.  The default is always
	// at the first item in the combo list (index 0); this is rendered as an
	// empty string in the config, but it's rendered in the combo as "(Default)"
	// or some simliar (possibly localized) string defined in the dialog 
	// resource.  So we need to select index 0 explicitly.
	if (cv[0] == 0)
		combo.SetCurSel(0);
	else
		combo.SelectString(1, cv);
}
