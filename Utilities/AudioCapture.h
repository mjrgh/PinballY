// Windows Audio Capture Utilities

#pragma once

// Audio capture device enumeration info
struct AudioCaptureDeviceInfo
{
	// Friendly name of the device
	BSTR friendlyName;
};

// Enumerate audio input devices via DirectShow.  Invokes the callback 
// for each audio input device found in the system.  The callback returns
// true to continue the enumeration, false to stop.
void EnumDirectShowAudioInputDevices(std::function<bool(const AudioCaptureDeviceInfo *)> callback);

