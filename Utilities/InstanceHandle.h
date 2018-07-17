// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Instance handle global variable
//
// The "instance handle" is handle to the loaded .EXE or .DLL
// containing the running code.  Windows passes the instance
// handle to the program's entrypoint (WinMain for an application,
// DllMain for a DLL).  The instance handle is needed mostly for
// loading resources.
//
// Note that you can always get the instance handle for the
// running application by calling the Win32 API GetModuleHandle
// with a null module handle argument (GetModuleHandle(0)). 
// However, that always gives you the *application* instance
// handle.  If the loaded code is running as a DLL, that's not
// the handle you want to load resources, since it'll load
// resources from the EXE that loaded the DLL, not from the
// DLL.  Code that's running in a DLL usually wants to be able
// to access its own resources, not those of the containing
// process.  That's why it's important to hold on to the handle
// that Windows passes to the startup code. 

#pragma once
#include <Windows.h>

// Instance handle global.  The application is responsible for
// setting this in its WinMain or DllMain entrypoint code.
extern HINSTANCE G_hInstance;
