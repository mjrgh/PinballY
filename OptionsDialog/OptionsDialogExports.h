// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Options dialog exports.  This defines the public interface exported
// from the DLL.
//
// Important: update PINBALLY_OPTIONS_DIALOG_IFC_VSN whenever making 
// any incompatible change to the binary interface.  The host program
// tests the version before calling anything else in the DLL to
// make sure that the user didn't accidentally leave an old copy of
// the DLL installed when updating the main program.  A mismatched
// DLL version could cause the usual range of difficult-to-diagnose
// crashes due to incorrect function parameters and the like.
//
#pragma once

// Interface version.  The host program can use this to make sure that
// it's talking to the current version of the DLL.  Simply increment it
// whenever making an incompatible change to the DLL binary interface
// (e.g., changing parameters to one of the functions).
//
// Note that this isn't visible to the user; it's purely internal.
// It's not necessary to update this in lock step with the main program
// version or to update it on each public release.  This only has to be
// updated when the binary interface to the DLL changes.
#define PINBALLY_OPTIONS_DIALOG_IFC_VSN  4

// Get the dialog version.  This returns the dialog interface version 
// above.  The host should check this before calling any other functions,
// to ensure that the correct DLL version is installed.
extern "C" DWORD WINAPI GetOptionsDialogVersion();

// Show the options dialog.
//
// 'configSaveCallback' notifies the host that the dialog has saved
// updated settings to the config file.  This is invoked when the Apply
// button is pressed to save changes, or the OK button is pressed when 
// there are unsaved changes.  When this is called, the new settings
// have already been written to the file, so the host can re-load the
// settings file to refresh with the new settings.
//
// 'initPosCallback' lets the host set the initial position of the
// dialog.  This is called during WM_INITDIALOG message processing 
// when the dialog is first opened.
//
// 'finalDialogRect' is filled in on return with the window rect of
// the dialog just before it was closed.  The host can use this to
// save the position of the dialog to restore later.
//
typedef std::function<void(bool succeeded)> ConfigSaveCallback;
typedef std::function<void(HWND hwnd)> InitializeDialogPositionCallback;
typedef std::function<bool()> SetUpAdminAutoRunCallback;
extern "C" void WINAPI ShowOptionsDialog(
	ConfigSaveCallback configSaveCallback, 
	InitializeDialogPositionCallback initPosCallback,
	bool isAdminHostRunning,
	SetUpAdminAutoRunCallback setUpAdminAutoRunCallback,
	/*[OUT]*/ RECT *finalDialogRect);
