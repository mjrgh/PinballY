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

VideoSprite *VideoSprite::LoadVideo(const TSTRING &filename, HWND hwnd,
	POINTF normalizedSize, ErrorHandler &eh, const TCHAR *descForErrors,
	bool play, int volumePct)
{
	RefPtr<VideoSprite> vs(new VideoSprite());
	if (vs->_LoadVideo(filename, hwnd, normalizedSize, eh, descForErrors, play, volumePct))
		return vs.Detach();

	return nullptr;
}

bool VideoSprite::_LoadVideo(
	const TSTRING &filename, HWND hwnd, POINTF sz,
	ErrorHandler &eh, const TCHAR *descForErrors,
	bool play, int volumePct)
{
	// Check for GIF files.  Perversely, libvlc can't play animated
	// GIFs, but our regular image sprite loader can!  Libvlc actually
	// can *load* animated GIFs, it won't animate them - it just shows
	// the first frame.  And even more weirdly, libvlc actually has
	// the code to play back animated GIFs, but it's disabled, because
	// the libvlc media type list has GIF entered as a still image
	// format.  This misfeature has been there for years and years
	// (there are some old reports of it on the Web), so they don't
	// seem interested in fixing it; maybe there are complications
	// beyond just changing the media type that make it impractical,
	// or maybe they just don't want to bother testing it.  In any
	// case, there's no way to work around it in the libvlc API.  But
	// we *can* work around it by using our own image sprite loader
	// instead when we detect a GIF file.  If it turns out to be a
	// still GIF, that's fine, too, since our image loader happily
	// handles those.
	if (ImageFileDesc desc; GetImageFileInfo(filename.c_str(), desc, false)
		&& desc.imageType == ImageFileDesc::GIF)
		return __super::LoadGIF(filename.c_str(), sz, desc.dispSize, eh);

	// create a new video player
	RefPtr<AudioVideoPlayer> v(new VLCAudioVideoPlayer(hwnd, hwnd, false));

	// set looping mode
	v->SetLooping(true);

	// set the audio volume
	v->SetVolume(volumePct);

	// set the initial mute mode according to the current global status
	v->Mute(Application::Get()->IsMuteVideosNow());

	// try opening the video 
	if (!v->Open(TSTRINGToWSTRING(filename).c_str(), eh))
		return false;

	// if desired, start it playing
	if (play && !v->Play(eh))
		return false;

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

