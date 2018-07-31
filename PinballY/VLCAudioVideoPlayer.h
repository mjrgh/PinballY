// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// LibVLC video player.  This implements our AudioVideoPlayer interface
// using libvlc, the core video decoding and playback layer from the VLC
// media player.  The VLC library has a public interface that third-party
// party applications (such as this one) can use for video playback within
// their own user interfaces.  Per our base AudioVideoPlayer interface, we
// have libvlc decode to a D3D texture, which we can then render onto a 3D 
// object.

#pragma once
#include <malloc.h>
#include "AudioVideoPlayer.h"

struct libvlc_instance_t;
struct libvlc_event_t;
struct libvlc_media_t;
struct libvlc_media_player_t;
struct ID3D11Texture2D;
struct ID3D11Resource;
struct ID3D11ShaderResourceView;
class Camera;
class Sprite;
class Shader;

// our VLC player wrapper
class VLCAudioVideoPlayer : public AudioVideoPlayer
{
public:
	VLCAudioVideoPlayer(HWND hwndVideo, HWND hwndEvent, bool audioOnly);

    // Shut down the libvlc subsystem.  The application must call
    // this before exiting, to release global libvlc resources.
    static void OnAppExit();

	// Get the libvlc version number
	static const char *GetLibVersion();

	// Open a file path for playback.  This opens the video with a
	// standard video display target.
	virtual bool Open(const TCHAR *path, ErrorHandler &eh) override
		{ return OpenWithTarget(path, eh, VideoTarget); }

	// DMD device interface
	class DMD
	{
	public:
		virtual ~DMD() { }

		// Present a video frame on the device.  The frame is in
		// I420 format, with separate Y, U, and V buffers.  The
		// frame is a fixed 128x32 pixel format.  Note that the
		// U and V buffers are subsampled in 2x2 blocks, so these
		// contain only 64x16 samples.  All of the buffers are
		// packed with minimal row stride - 128 bytes per row
		// for the Y buffer, 64 bytes per row for U and V.
		virtual void PresentVideoFrame(
			int width, int height,
			const BYTE *y, const BYTE *u, const BYTE *v) = 0;

		// Does the device support RGB display?
		virtual bool SupportsRGBDisplay() const = 0;
	};

	// shut down the session
	virtual void Shutdown() override;

	// Open a file for playback on a real DMD device
	bool OpenDmdTarget(const TCHAR *path, ErrorHandler &eh, DMD *dmd)
	{
		this->dmd = dmd;
		return OpenWithTarget(path, eh, DMDTarget);
	}

	// Start/stop playback
	virtual bool Play(ErrorHandler &eh) override;
	virtual bool Replay(ErrorHandler &eh) override;
	virtual bool Stop(ErrorHandler &eh) override;

	// Is playback running?
	virtual bool IsPlaying() const override { return isPlaying; }

	// Is the first frame ready yet?  Callers can use this in
	// combination with the AVPMsgFirstFrameReady message to delay
	// UI events until the video actually starts playing.  Libvlc
	// loads videos asynchronously in background worker threads,
	// so a video can start loading in the course of other work
	// the caller is doing.
	virtual bool IsFrameReady() const override { return firstFramePresented; }

	// Set looping playback mode
	virtual void SetLooping(bool f) override;

	// Mute audio
	virtual void Mute(bool f) override;

	// Render the current video frame onto a mesh
	virtual bool Render(Camera *camera, Sprite *sprite) override;

protected:
	// reference-counted -> self-destruction only
	virtual ~VLCAudioVideoPlayer();

	// Static: libvlc initialization failed.  We keep track of this
	// statically so that we don't keep showing initialization errors
	// on subsequent attempts; if initialization fails once, it'll
	// probably keep failing.
	static bool initFailed;

	// Target display device types
	enum TargetDevice
	{
		VideoTarget,      // normal video display
		DMDTarget         // real DMD device target
	};

	// Open with the given target
	bool OpenWithTarget(const TCHAR *path, ErrorHandler &eh, TargetDevice target);

	// media path
	TSTRING mediaPath;

	// Real DMD device target, when in DMD playback mode
	DMD *dmd;

	// VLC event callbacks
	static void OnMediaPlayerEndReached(const libvlc_event_t *event, void *opaque);

	// frame decoding callbacks - regular video target mode
	static unsigned int OnVideoSetFormat(void **opaque, char *chroma,
		unsigned *width, unsigned *height, unsigned *pitches, unsigned *lines);
	static void OnVideoFormatCleanup(void *opaque);
	static void *OnVideoFrameLock(void *opaque, void **planes);
	static void OnVideoFrameUnlock(void *opaque, void *picture, void *const *planes);
	static void OnVideoFramePresent(void *opaque, void *picture);

	// frame decoding callbacks - DMD device mode
	static unsigned int OnDMDSetFormat(void **opaque, char *chroma,
		unsigned *width, unsigned *height, unsigned *pitches, unsigned *lines);
	static void OnDMDFrameUnlock(void *opaque, void *picture, void *const *planes);
	static void OnDMDFramePresent(void *opaque, void *picture);


	// is playback running?
	bool isPlaying;

	// do we loop playback?
	bool looping;

	// audio muted
	bool muted;

	// Frame buffers for the video decoder and renderer.  These are
	// the memory buffers that we return to libvlc from our "lock
	// buffer" callback.  Libvlc decodes video frames directly into
	// these buffers.  Each buffer holds one frame.
	//
	// We maintain a small pool of these buffers.  At any given time,
	// a buffer can be in one of three states:  free, meaning it's
	// not currently in use and is available for the next "lock"
	// request from libvlc; locked, meaning that libvlc is actively
	// decoding a frame into the buffer's memory; and valid, which
	// means that libvlc has finished decoding a frame into this
	// buffer.  Multiple valid frames might exist at any given time, 
	// since libvlc can work ahead to decode future frames before 
	// it's time to display them.  Libvlc tells us via our "present
	// frame" callback precisely when it's time to display a frame.
	// It only calls this with valid frames.
	//
	// Note that the usual D3D11 method for video playback is "usage
	// dynamic" textures, which are optimized for streaming data 
	// from the CPU to GPU.  We don't use this mechanism, though,
	// because the shared CPU/GPU memory where dynamic textures have
	// to be allocated is too scarce on many systems to allow for
	// the kind of multi-stream playback we need to do.  Instead, we
	// stream data to the GPU simply by creating a new texture for
	// each frame.  This is less efficient than dynamic textures,
	// but in testing it's more reliable.  (The really big problem
	// with dynamic textures is that D3D seems to have bugs that
	// make it crash on exhausting dynamic texture memory rather 
	// than returning errors to callers as it should.  That makes
	// it impossible to gracefully handle these conditions in the
	// app code.)
	class FrameBuffer
	{
	public:
		FrameBuffer() : 
			status(Free)
		{ 
		}

		~FrameBuffer()
		{
			// make sure our worker thread has exited
			if (hThread != nullptr)
				WaitForSingleObject(hThread, INFINITE);
		}

		// frame status
		enum
		{
			// Free: this frame buffer is available for a new decoded frame.
			Free,

			// Locked: VLC has locked this frame for writing, and is in the
			// process of decoding frame data into it.
			Locked,

			// Valid:  VLC has finished rendering a frame into this buffer.
			Valid
		} status;

		// Frame dimensions in pixels
		SIZE dims;

		// Pixel buffer.  This is allocated in our libvlc "set format"
		// callback, which tells us the size and pixel format of the frame
		// so that we can allocate buffers.
		std::unique_ptr<BYTE> pixBuf;

		// Shader to use for rendering this frame
		Shader *shader;

		// Pixel plane layout.  Some formats (e.g., I420 or NV12) divide
		// the image into multiple planes.  We create a texture and
		// shader resource view per plane.
		struct Plane
		{
			// texture descriptor for this plane's data
			D3D11_TEXTURE2D_DESC textureDesc;

			// Offset in pixBuf of the start of this plane's data.
			// (We pack all of a plane's frames into a single pixel
			// buffer, end on end, so this tells us the byte offset
			// of the start of this plane's data.)
			size_t bufOfs;

			// row pitch of this plane
			UINT rowPitch;

			// shader resource view - created by thread launched at
			// Unlock time
			RefPtr<ID3D11ShaderResourceView> shaderResourceView;
		};
		Plane planes[3];

		// number of planes in this format
		int nplanes;

		// Texture creation thread handle.  This is the thread we 
		// launch at Unlock time to create the texture objects for
		// the buffer.  The renderer must wait on this before
		// rendering the frame.
		HandleHolder hThread;
	};

	// Frame buffers
	FrameBuffer frame[5];

	// Shader for current rendered frame
	Shader *shader;

	// Shader resource views for the current frame we're rendering
	RefPtr<ID3D11ShaderResourceView> shaderResourceView[3];

	// Critical section locker for the rendering pointers.  This
	// controls access to presentedFrame and renderFrame.  We use
	// this separate lock for these two items, because they're the
	// only items that the renderer needs to access.  Isolating
	// them with their own lock minimizes contention between the
	// renderer and decoder threads.  That helps avoid blocking
	// the UI, as the renderer runs in the main UI thread.
	CriticalSection renderLock;

	// Current presented frame
	FrameBuffer *presentedFrame;

	// has the first frame been presented yet?
	bool firstFramePresented;

	// Critical section lock, for protecting items that can be
	// accessed by background threads
	CriticalSection lock;

	// media object representing the video file
	libvlc_media_t *media;

	// VLD media player instance.  This is the interface to our
	// video decoder/player.
	libvlc_media_player_t *player;

    // Global libvlc instance.  This is the top-level context 
    // for VLC operations.  We create this on demand on the first
    // use, and retain it until application termination.
    static libvlc_instance_t *vlcInst;
};
