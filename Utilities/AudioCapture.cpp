#include "stdafx.h"
#include <functional>
#include <dshow.h>
#include "Pointers.h"
#include "ComUtil.h"
#include "AudioCapture.h"

// include the DirectShow class IDs
#pragma comment(lib, "strmiids.lib")


// Enumerate audio input devices via DirectShow
void EnumDirectShowAudioInputDevices(std::function<bool(const AudioCaptureDeviceInfo *)> callback)
{
	// create the audio device enumerator
	RefPtr<ICreateDevEnum> pCreateDevEnum;
	RefPtr<IEnumMoniker> pEnumMoniker;
	LPMALLOC coMalloc = nullptr;
	if (SUCCEEDED(CoGetMalloc(1, &coMalloc))
		&& SUCCEEDED(CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pCreateDevEnum)))
		&& pCreateDevEnum->CreateClassEnumerator(CLSID_AudioInputDeviceCategory, &pEnumMoniker, 0) == S_OK
		&& pEnumMoniker != nullptr)
	{
		// scan through the audio devices
		for (;;)
		{
			// get the next device in the enumeration
			RefPtr<IMoniker> m;
			if (pEnumMoniker->Next(1, &m, NULL) != S_OK)
				break;

			// get the friendly name from the object's properties
			RefPtr<IBindCtx> bindCtx;
			RefPtr<IPropertyBag> propertyBag;
			VARIANTEx v(VT_BSTR);
			if (SUCCEEDED(CreateBindCtx(0, &bindCtx))
				&& SUCCEEDED(m->BindToStorage(bindCtx, NULL, IID_PPV_ARGS(&propertyBag)))
				&& SUCCEEDED(propertyBag->Read(L"FriendlyName", &v, NULL)))
			{
				// invoke the callback; if it returns false, stop the enumeration
				AudioCaptureDeviceInfo info{ v.bstrVal };
				if (!callback(&info))
					break;
			}
		}
	}
}

