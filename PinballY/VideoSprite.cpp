// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "VideoSprite.h"
#include "Application.h"
#include "AudioVideoPlayer.h"
#include "VLCAudioVideoPlayer.h"

VideoSprite::VideoSprite()
{
}

VideoSprite::~VideoSprite()
{
	ReleaseVideoPlayer();
}

void VideoSprite::ReleaseVideoPlayer()
{
	if (videoPlayer != nullptr)
	{
		// shutdown thread
		class ShutdownThread
		{
		public:
			ShutdownThread(AudioVideoPlayer *vp) : vp(vp) 
			{ 
				HandleHolder hThread = CreateThread(
					NULL, 0, &ShutdownThread::SMain, this, 0, &tid);
			}

		protected:
            // Thread entrypoint
			static DWORD WINAPI SMain(LPVOID lParam)
            {
                // get 'self'
                auto self = static_cast<ShutdownThread*>(lParam);
                
                // explicitly shut down the video player
				self->vp->Shutdown();
                
				// delete 'self'
				delete self;
				return 0;
			}

			// the video player we're disposing of
			RefPtr<AudioVideoPlayer> vp;

			// my thread ID
			DWORD tid;
		};

		// transfer the video player to a shutdown thread object
		new ShutdownThread(videoPlayer.Detach());
	}
}

bool VideoSprite::LoadVideo(
	const TSTRING &filename, HWND hwnd, POINTF sz,
	ErrorHandler &eh, const TCHAR *descForErrors)
{
	// create a new video player
	RefPtr<AudioVideoPlayer> v(new VLCAudioVideoPlayer(hwnd, hwnd, false));

	// set looping mode
	v->SetLooping(true);

	// set the initial mute mode according to the current global status
	v->Mute(Application::Get()->IsMuteVideosNow());

	// try opening the video and starting it playing
	if (!v->Open(TSTRINGToWSTRING(filename).c_str(), eh)
		|| !v->Play(eh))
	{
		// we couldn't get the video loaded or playing - return failure
		return false;
	}

	// discard any previous video player and store the new one
	ReleaseVideoPlayer();
	videoPlayer = v;

	// create the mesh
	CreateMesh(sz, eh, descForErrors);

	// success 
	return true;
}

// Render the video
void VideoSprite::Render(Camera *camera)
{
	// update the fade
	UpdateFade();

	// If we have a video, try rendering through the video player
	if (videoPlayer != nullptr && videoPlayer->Render(camera, this))
		return;

	// No video or no video frame - render the static image instead,
	// if we have one
	__super::Render(camera);
}
