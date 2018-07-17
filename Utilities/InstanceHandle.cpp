// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Global instance handle

#include "stdafx.h"
#include "InstanceHandle.h"


// Instance handle global variable.  The application is
// responsible for setting this in its WinMain() or DllMain()
// entrypoint code.
HINSTANCE G_hInstance;

