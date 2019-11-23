// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "../Utilities/Config.h"
#include "Resource.h"
#include "Application.h"
#include "PlayfieldView.h"
#include "PlayfieldWin.h"

namespace ConfigVars
{
	const TCHAR *PlayfieldWinVarPrefix = _T("PlayfieldWindow");
}

// construction
PlayfieldWin::PlayfieldWin() : FrameWin(ConfigVars::PlayfieldWinVarPrefix, _T("Playfield"), IDI_MAINICON, IDI_MAINICON_GRAY)
{
}

// destruction
PlayfieldWin::~PlayfieldWin()
{
}

BaseView *PlayfieldWin::CreateViewWin() 
{
	// create our view
	PlayfieldView *pfView = new PlayfieldView();
	if (!pfView->Create(hWnd))
	{
		pfView->Release();
		return 0;
	}

	// set focus on the view
	SetFocus(pfView->GetHWnd());

	// return the new window
	return pfView;
}

void PlayfieldWin::OnAppActivationChange(bool activating)
{
	if (auto pfv = dynamic_cast<PlayfieldView*>(GetView()); pfv != nullptr)
		pfv->OnAppActivationChange(activating);
}

void PlayfieldWin::OnRawInput(UINT rawInputCode, HRAWINPUT hRawInput)
{
	// send the event to the input manager
	InputManager::GetInstance()->ProcessRawInput(rawInputCode, hRawInput);
}

void PlayfieldWin::OnRawInputDeviceChange(USHORT what, HANDLE hDevice)
{
	// send the event to the input manager
	InputManager::GetInstance()->ProcessDeviceChange(what, hDevice);
}

bool PlayfieldWin::OnNCDestroy()
{
	// terminate the application on closing the main window
	PostQuitMessage(0);

	// do the base class handling
	return __super::OnNCDestroy();
}
