// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <dshow.h>
#include "DShowAudioPlayer.h"
#include "PrivateWindowMessages.h"
#include "../Utilities/LogError.h"

// Include the library where the DirectShow IIDs are defined
#pragma comment(lib, "strmiids.lib") 


// statics
CriticalSection DShowAudioPlayer::lock;
LONG_PTR DShowAudioPlayer::nextCallbackID = 1;
std::unordered_map<LONG_PTR, DShowAudioPlayer*> DShowAudioPlayer::callbackIDMap;

DShowAudioPlayer::DShowAudioPlayer(HWND hwndEvent) : 
	AudioVideoPlayer(NULL, hwndEvent, true)
{
	// lock the static resources while working
	CriticalSectionLocker locker(lock);

	// assign an event callback identifier
	callbackID = nextCallbackID++;

	// add our callback ID to the live object map
	callbackIDMap.emplace(callbackID, this);
}

DShowAudioPlayer::~DShowAudioPlayer()
{
	// remove myself from the live object map
	CriticalSectionLocker locker(lock);
	callbackIDMap.erase(callbackID);
}

void DShowAudioPlayer::SetLooping(bool looping)
{
	this->looping = looping;
}

bool DShowAudioPlayer::Error(HRESULT hr, ErrorHandler &eh, const TCHAR *where)
{
	WindowsErrorMessage winErr(hr);
	eh.SysError(LoadStringT(IDS_ERR_AUDIOPLAYERSYSERR),
			MsgFmt(_T("Opening audio file %s: %s: %s"), path.c_str(), where, winErr.Get()));
		return false;
}

bool DShowAudioPlayer::Open(const WCHAR *path, ErrorHandler &eh)
{
	// remember the file path
	this->path = WideToTSTRING(path);

	// create the graph manager
	RefPtr<IGraphBuilder> pGraph;
	HRESULT hr;
	if (!SUCCEEDED(hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void **)&pGraph)))
		return Error(hr, eh, _T("Creating filter graph"));

	// query interfaces
	if (!SUCCEEDED(hr = pGraph->QueryInterface(&pControl)))
		return Error(hr, eh, _T("Querying media control interface"));
	if (!SUCCEEDED(hr = pGraph->QueryInterface(&pEventEx)))
		return Error(hr, eh, _T("Querying media event interface"));
	if (!SUCCEEDED(hr = pGraph->QueryInterface(&pBasicAudio)))
		return Error(hr, eh, _T("Querying basic audio interface"));
	if (!SUCCEEDED(hr = pGraph->QueryInterface(&pSeek)))
		return Error(hr, eh, _T("Querying seek interface"));

	// set up the event callback
	pEventEx->SetNotifyWindow(reinterpret_cast<OAHWND>(hwndEvent), DSMsgOnEvent, callbackID);

	// render the file
	if (!SUCCEEDED(hr = pGraph->RenderFile(path, NULL)))
		return Error(hr, eh, _T("Rendering file"));

	// set the initial muting and volume level in the player
	if (pBasicAudio != nullptr)
		pBasicAudio->put_Volume(muted ? -10000 : vol);

	// success
	return true;
}

void DShowAudioPlayer::OnEvent(LPARAM lParam)
{
	// target object, which we'll look up from the lParam
	RefPtr<DShowAudioPlayer> self;

	{
		// Lock static resources while accessing the live object table
		CriticalSectionLocker locker(lock);

		// The LPARAM is the callback ID of the target object.  Look it
		// up in the live object table.
		if (auto it = callbackIDMap.find(static_cast<UINT64>(lParam)); it != callbackIDMap.end())
		{
			// got it - the object is live
			self = it->second;
		}
		else
		{
			// not found - the object has already been deleted; ignore
			// any remaining event messages targeting it
			return;
		}
	}

	// process events until the queue is empty
	long eventCode;
	LONG_PTR lParam1, lParam2;
	while (SUCCEEDED(self->pEventEx->GetEvent(&eventCode, &lParam1, &lParam2, 0)))
	{
		// check what we have
		switch (eventCode)
		{
		case EC_COMPLETE:
			// notify the event window that playback is finished
			PostMessage(self->hwndEvent, self->looping ? AVPMsgLoopNeeded : AVPMsgEndOfPresentation, 
				static_cast<WPARAM>(self->cookie), 0);
			break;
		}

		// clean up the event parameters
		self->pEventEx->FreeEventParams(eventCode, lParam1, lParam2);
	}
}

bool DShowAudioPlayer::Play(ErrorHandler &eh)
{
	// start playback
	if (HRESULT hr; !SUCCEEDED(hr = pControl->Run()))
		return Error(hr, eh, _T("IMediaControl::Run"));

	// flag that playback is running
	playing = true;

	// success
	return true;
}

bool DShowAudioPlayer::Stop(ErrorHandler &eh)
{
	// stop playback
	if (HRESULT hr; !SUCCEEDED(hr = pControl->Stop()))
		return Error(hr, eh, _T("IMediaControl::Stop"));

	// flag that playback is no longer running
	playing = false;

	// success
	return true;
}

bool DShowAudioPlayer::Replay(ErrorHandler &eh)
{
	// stop playback
	if (!Stop(eh))
		return false;

	// seek to the start
	LONGLONG cur = 0;
	if (HRESULT hr; !SUCCEEDED(hr = pSeek->SetPositions(&cur, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning)))
		return Error(hr, eh, _T("IMediaSeek::SetPositions"));

	// resume/restart playback
	return Play(eh);
}

void DShowAudioPlayer::Mute(bool mute)
{
	// if we have a player, set the new status in the player
	if (pBasicAudio != nullptr)
		pBasicAudio->put_Volume(mute ? -10000 : vol);

	// remember the new status internally
	muted = mute;
}

int DShowAudioPlayer::GetVolume() const
{
	// reverse the log calculation we do in SetVolume
	return static_cast<int>(100.0f * powf(10.0f, static_cast<float>(vol) / 2000.0f));
}

void DShowAudioPlayer::SetVolume(int pct)
{
	// override muting
	if (muted)
		muted = false;

	// Figure the new volume, converting from our 0%-100% scale to 
	// DShow's -100db to 0db scale.
	//
	// 100% = 0db (reference volume, same as recorded level).
	// 0% is really "minus infinity", but treat it as a special case at -100db.
	//
	// In between, calculate it on a log scale, with 1% set to -40db.
	// That makes the log factor 20db.
	// 
	// Note that the DShow scale is 100x the nominal db level.
	vol = pct < 1 ? -10000 :
		static_cast<long>(2000.0f * log10f(static_cast<float>(pct) / 100.0f));

	// set the new volume in the underlying interface, if available
	if (pBasicAudio != nullptr)
		pBasicAudio->put_Volume(vol);
}

void DShowAudioPlayer::Shutdown()
{
	Stop(SilentErrorHandler());
}
