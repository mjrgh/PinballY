// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
//
// DirectShow Audio Player
//
#pragma once
#include <dshow.h>
#include "AudioVideoPlayer.h"

class ErrorHandler;

class DShowAudioPlayer : public AudioVideoPlayer
{
public:
	DShowAudioPlayer(HWND hwndEvent);

	// shut down the player
	virtual void Shutdown() override;

	// open an audio track
	virtual bool Open(const WCHAR *url, ErrorHandler &eh) override;

	// get the media path
	virtual const TCHAR *GetMediaPath() const override { return path.c_str(); }

	// start/stop playback
	virtual bool Play(ErrorHandler &eh) override;
	virtual bool Replay(ErrorHandler &eh) override;
	virtual bool Stop(ErrorHandler &eh) override;

	// is the audio playing?
	virtual bool IsPlaying() const override { return playing; }

	// is a frame ready?
	virtual bool IsFrameReady() const override { return true; }

	// set looping mode
	virtual void SetLooping(bool looping) override;

	// mute
	virtual void Mute(bool mute) override;

	// get/set the volume
	virtual int GetVolume() const override;
	virtual void SetVolume(int volPct) override;

	// Render the video onto the given sprite.  As we're an audio-only
	// player, this does nothing.
	virtual bool Render(Camera*, Sprite*) override { return true; }

	// Process events.  BaseWin::OnAppMessage() calls this when our event
	// window receives a DMsgOnEvent message.
	void OnEvent();

protected:
	virtual ~DShowAudioPlayer();

	virtual bool IsReadyToDelete() const override { return true; }

	// log an error and return false
	bool Error(HRESULT hr, ErrorHandler &eh, const TCHAR *where);

	// file path, mostly for debugging purposes
	TSTRING path;

	// player interfaces
	RefPtr<IMediaControl> pControl;
	RefPtr<IMediaEventEx> pEventEx;
	RefPtr<IBasicAudio> pBasicAudio;
	RefPtr<IMediaSeeking> pSeek;

	// is playback in progress?
	bool playing = false;

	// Current volume level in db from full volume (0 = full volume, -10000 = mute)
	LONG vol = 0;
	bool muted = false;

	// looping mode
	bool looping = false;
};

