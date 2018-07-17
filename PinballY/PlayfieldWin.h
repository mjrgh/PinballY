// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Playfield window

#pragma once

#include "stdafx.h"
#include "FrameWin.h"

class PlayfieldView;

// Playfield frame window
class PlayfieldWin : public FrameWin
{
public:
	// construction
	PlayfieldWin();

protected:
	// destruction - called internally when the reference count reaches zero
	~PlayfieldWin();

	// create my view window
	virtual BaseView *CreateViewWin();

	// handle raw input
	virtual void OnRawInput(UINT rawInputCode, HRAWINPUT hRawInput) override;

	// Process a raw input device change event (WM_INPUT_DEVICE_CHANGED)
	virtual void OnRawInputDeviceChange(USHORT what, HANDLE hDevice) override;

	// terminate the application on closing the main window
	virtual bool OnNCDestroy() override;
};
