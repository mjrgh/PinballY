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
		ErrorHandler &eh, const TCHAR *descForErrors, 
		bool play = true, int volumePct = 100);

	// is the first frame ready?
	virtual bool IsFrameReady() const { return videoPlayer != nullptr && videoPlayer->IsFrameReady(); }

	// Clear resources
	virtual void Clear() override;

	// Clear the video
	void ClearVideo() { ReleaseVideoPlayer(); }

	// Render the video
	virtual void Render(Camera *camera) override;

	// Do we have a video?
	bool IsVideo() const { return videoPlayer != nullptr; }

	// Get/set the looping status
	virtual bool IsLooping() const override
		{ return videoPlayer != nullptr ? videoPlayer->IsLooping() : __super::IsLooping(); }
	virtual void SetLooping(bool f) override
	{
		if (videoPlayer != nullptr)
			videoPlayer->SetLooping(f);
		__super::SetLooping(f);
	}

	// Play/stop the video
	virtual void Play(ErrorHandler &eh) override
	{
		if (videoPlayer != nullptr)
			videoPlayer->Play(eh);
		__super::Play(eh);
	}
	virtual void Stop(ErrorHandler &eh) override
	{
		if (videoPlayer != nullptr)
			videoPlayer->Stop(eh);
		__super::Stop(eh);
	}

	// get my player
	AudioVideoPlayer *GetVideoPlayer() const { return videoPlayer; }

	// get my video player cookie
	virtual DWORD GetMediaCookie() const override
		{ return videoPlayer != nullptr ? videoPlayer->GetCookie() : __super::GetMediaCookie(); }

	// service an AVPMsgLoopNeeded message
	virtual void ServiceLoopNeededMessage(ErrorHandler &eh) override
	{
		if (videoPlayer != nullptr)
			videoPlayer->Replay(eh);
	}


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
