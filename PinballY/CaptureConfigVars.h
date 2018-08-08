// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Configuration variable names for media capture.  These are
// used in both the playfield view and application classes, so
// we define them in a shared header.

namespace ConfigVars
{
	static const TCHAR *CaptureStartupDelay = _T("Capture.StartupDelay");
	static const TCHAR *CapturePFVideoTime = _T("Capture.PlayfieldVideoTime");
	static const TCHAR *CaptureBGVideoTime = _T("Capture.BackglassVideoTime");
	static const TCHAR *CaptureDMVideoTime = _T("Capture.DMDVideoTime");
	static const TCHAR *CaptureTPVideoTime = _T("Capture.TopperVideoTime");
    static const TCHAR *CaptureTwoPassEncoding = _T("Capture.TwoPassEncoding");
    static const TCHAR *CapturePFAudioTime = _T("Capture.PlayfieldAudioTime");
}
