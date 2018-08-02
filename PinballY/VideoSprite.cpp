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
		// Shutdown thread.  When we're ready to discard the underlying
		// video, we start a low-priority thread to do the video player
		// shutdown.  We do this on a separate thread to avoid a UI stall
		// while waiting for the playback to stop.  The "stop" call to
		// libvlc can take a noticeable amount of time to return, 
		// presumably because it's explicitly waiting for its own
		// background playback threads to exit.  
		//
		// We want to do the video "stop" call on the background thread,
		// but we don't want to do the actual object deletion there; we
		// want the deletion itself to occur on the main thread.  This
		// is out of an abundance of caution about D3D threading.  The
		// video player probably owns some shader resource view objects,
		// and based on testing, releasing those can trigger implicit
		// calls into the D3D11 Device Context.  DC calls are required
		// to be single-threaded.  My own machines don't actually seem
		// to have a problem with releasing the objects on a separate
		// thread, but I suspect that some configurations might; the
		// degree of thread safety here might be implementation-specific
		// in the D3D11 hardware drivers.  Best not to risk it.  To get
		// the actual object deletion back on the main thread, we use
		// a queue of video players pending deletion.
		class ShutdownThread
		{
		public:
			ShutdownThread(AudioVideoPlayer *vp) : vp(vp) 
			{ 
				// add the player to the pending deletion list
				vp->SetPendingDeletion();

				// create the thread, but don't start it yet
				HandleHolder hThread = CreateThread(NULL, 0, &ShutdownThread::SMain, this, CREATE_SUSPENDED, &tid);
				if (hThread != NULL)
				{
					// set it to low priority and kick it off
					SetThreadPriority(hThread, BELOW_NORMAL_PRIORITY_CLASS);
					ResumeThread(hThread);
				}
				else
				{
					// couldn't start the thread - do the thread work inline
					SMain(this);
				}
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
