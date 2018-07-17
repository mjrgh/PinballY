// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Video Sprite.  This is a subclass of the basic Sprite that adds
// video rendering to the base Sprite type.  This mesh can display
// either a static image like a regular Sprite, or can display video
// playback.

#pragma once
#include "Sprite.h"
#include "AudioVideoPlayer.h"

class Camera;

class VideoSprite : public Sprite
{
public:
	VideoSprite();

	// Load a video.  'width' and 'height' give the size of the sprite
	// in our normalized coordinates, where 1.0 is the height of the
	// window.
	bool LoadVideo(const TSTRING &filename, HWND hwnd, POINTF normalizedSize, 
		ErrorHandler &eh, const TCHAR *descForErrors);

	// Render the video
	virtual void Render(Camera *camera) override;

	// Do we have a video?
	bool IsVideo() const { return videoPlayer != nullptr; }

	// get my player
	AudioVideoPlayer *GetVideoPlayer() const { return videoPlayer; }

	// get my video player cookie
	DWORD GetVideoPlayerCookie() const 
		{ return videoPlayer != nullptr ? videoPlayer->GetCookie() : 0; }

protected:
	virtual ~VideoSprite();

	// Release the video player.  This should be called whenever the
	// video player pointer is about to be changed, since it ensures
	// that we stop playback before releasing the object.  An active
	// video will keep playing even without our object reference if
	// we don't shut the session down explicitly.
	void ReleaseVideoPlayer();

	// video player
	RefPtr<AudioVideoPlayer> videoPlayer;
};
