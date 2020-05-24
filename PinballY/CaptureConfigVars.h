// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Configuration variable names for media capture.  These are
// used in both the playfield view and application classes, so
// we define them in a shared header.

namespace ConfigVars
{
	static const TCHAR *CaptureAudioDevice = _T("Capture.AudioDevice");

	static const TCHAR *CaptureStartupDelay = _T("Capture.StartupDelay");

	static const TCHAR *CapturePFImageStart = _T("Capture.PlayfieldImage.Start");

	static const TCHAR *CapturePFVideoStart = _T("Capture.PlayfieldVideo.Start");
	static const TCHAR *CapturePFVideoStop = _T("Capture.PlayfieldVideo.Stop");
	static const TCHAR *CapturePFVideoTime = _T("Capture.PlayfieldVideo.Time");

	static const TCHAR *CapturePFAudioStart = _T("Capture.PlayfieldAudio.Start");
	static const TCHAR *CapturePFAudioStop = _T("Capture.PlayfieldAudio.Stop");
	static const TCHAR *CapturePFAudioTime = _T("Capture.PlayfieldAudio.Time");

	static const TCHAR *CaptureBGImageStart = _T("Capture.BackglassImage.Start");

	static const TCHAR *CaptureBGVideoStart = _T("Capture.BackglassVideo.Start");
	static const TCHAR *CaptureBGVideoStop = _T("Capture.BackglassVideo.Stop");
	static const TCHAR *CaptureBGVideoTime = _T("Capture.BackglassVideo.Time");

	static const TCHAR *CaptureDMImageStart = _T("Capture.DMDImage.Start");

	static const TCHAR *CaptureDMVideoStart = _T("Capture.DMDVideo.Start");
	static const TCHAR *CaptureDMVideoStop = _T("Capture.DMDVideo.Stop");
	static const TCHAR *CaptureDMVideoTime = _T("Capture.DMDVideo.Time");

	static const TCHAR *CaptureTPImageStart = _T("Capture.TopperImage.Start");

	static const TCHAR *CaptureTPVideoStart = _T("Capture.TopperVideo.Start");
	static const TCHAR *CaptureTPVideoStop = _T("Capture.TopperVideo.Stop");
	static const TCHAR *CaptureTPVideoTime = _T("Capture.TopperVideo.Time");

	static const TCHAR *CaptureTwoPassEncoding = _T("Capture.TwoPassEncoding");
	static const TCHAR *CaptureVideoResLimit = _T("Capture.VideoResolutionLimit");
}
