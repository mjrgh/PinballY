// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "AudioVideoPlayer.h"

// statics
DWORD AudioVideoPlayer::nextCookie = 1;

AudioVideoPlayer::AudioVideoPlayer(HWND hwndVideo, HWND hwndEvent, bool audioOnly) :
	hwndVideo(hwndVideo),
	hwndEvent(hwndEvent),
	audioOnly(audioOnly)
{
	// Assign a cookie
	cookie = InterlockedIncrement(&nextCookie);
}

AudioVideoPlayer::~AudioVideoPlayer()
{
}

