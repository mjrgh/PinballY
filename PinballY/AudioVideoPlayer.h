// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Audio/Video Player interface.
//
// This is an abstract interface for our audio/video player,
// which we use to play back media files.  The player works with
// our DirectX rendering scheme to render video onto Sprite
// objects.  (It could also be easily adapted to render video 
// onto any DX 3D mesh, since it simply renders video to DX11
// 2D texture objects that can be mapped as shader resource
// views, but we only use it for simple 2D Sprite rendering.)


#pragma once
#include "PrivateWindowMessages.h"

class Camera;
class Sprite;

// Abstract Audio/Video player interface
class AudioVideoPlayer : public RefCounted
{
public:
	// Create the player.  
	//
	// hwndVideo is the window where the video will be presented. 
	// The player object itself DOESN'T render anything into this
	// window - we render onto D3D textures, which the caller can
	// use for any desired display rendering.  However, some player
	// implementations might need to know the window for their own
	// internal purposes, such as resource allocation (e.g., a
	// player based on Media Foundation would need this for object
	// activation purposes).
	//
	// hwndEvent is a window handle where AVPMsgXxx events will be
	// sent during playback.
	//
	// audioOnly is true if you want to create an audio-only player.
	// This type of player can be used to play back compressed audio
	// (e.g., mp3 files).
	//
	AudioVideoPlayer(HWND hwndVideo, HWND hwndEvent, bool audioOnly);

	// explicitly shut down the player
	virtual void Shutdown() = 0;

	// Mark the player as pending deletion.  This adds the player
	// to a deletion queue, which the UI loop can check periodically
	// for objects that are ready to delete.
	//
	// Players pending deletion won't be removed until they report
	// that they're ready, meaning that playback has been terminated
	// and there are no references to the object apart from the one
	// we keep from our own queue.
	void SetPendingDeletion();

	// Process the pending deletion queue.  This deletes any players
	// that are marked as pending deletion and are ready to be
	// deleted.  This should be called from time to time from the
	// main UI thread, to clean up dead player objects.  This must 
	// only be called from the main UI thread, since the whole point
	// of the queue is to ensure that any implicit D3D11 resource
	// releasing is done on the main UI thread.  Releasing a D3D
	// resource can sometimes trigger an implicit call into the
	// D3D Device Context, which isn't thread-safe.
	//
	// Returns true if any objects are left in the queue on return,
	// false if the queue is now empty.
	static bool ProcessDeletionQueue();

	// Wait for the deletion queue to empty
	static void WaitForDeletionQueue(DWORD timeout = INFINITE);

	// Get this playback session's cookie.  This is an ID for the
	// object, generated at construction, that's meant to be unique 
	// over the lifetime of the process.  (It's not unique across 
	// program runs, so it's not useful as an external ID in config 
	// files or other outside storage.)  Callers that need to refer 
	// to playback sessions asynchronously should use the cookie
	// instead of an object pointer to keep track of the session,
	// because C++ can reuse the same memory for a new session after 
	// a previous session has been deleted, thus you can't always 
	// tell by the pointer alone that you're referring to the same
	// object.  This is particularly important for event callbacks
	// via posted window messages, because an object could be
	// asynchronously deleted before 
	DWORD GetCookie() const { return cookie; }

	// open a URL for playback
	virtual bool Open(const WCHAR *url, ErrorHandler &eh) = 0;

	// get the media file
	virtual const TCHAR *GetMediaPath() const = 0;

	// start/stop playback
	virtual bool Play(ErrorHandler &eh) = 0;
	virtual bool Stop(ErrorHandler &eh) = 0;

	// replay from the beginning
	virtual bool Replay(ErrorHandler &eh) = 0;

	// Is playback running?  This returns true after the first
	// "session started" event fires.
	virtual bool IsPlaying() const = 0;

	// Is a frame ready yet?
	virtual bool IsFrameReady() const = 0;

	// Set looping playback.  When set, we'll automatically
	// restart the video from the beginning whenever we reach
	// the end.
	virtual void SetLooping(bool f) = 0;

	// Mute audio
	virtual void Mute(bool f) = 0;

	// Set the current audio volume of playback for this track, as
	// a percentage (0..100) of the nominal recorded volume.
	virtual void SetVolume(int volPct) = 0;

	// Render the video onto the given sprite
	virtual bool Render(Camera *camera, Sprite *sprite) = 0;

	// Format descriptor.  During video playback, when the video frame
	// format is first detected or when it changes during playback, the
	// player sends the event window an AVPMsgSetFormat message with a
	// pointer to this struct in the LPARAM.
	struct FormatDesc
	{
		UINT width;
		UINT height;
	};

protected:
	// reference counted -> protected destructor
	virtual ~AudioVideoPlayer();

	// Is this object ready to delete?  This returns true if playback
	// has been successfully terminated and there's only one reference
	// to the object (presumably the caller's).
	virtual bool IsReadyToDelete() const = 0;

	// video window
	HWND hwndVideo;

	// event window
	HWND hwndEvent;

	// audio-only mode?
	bool audioOnly;

	// Player cookie.  This is a unique ID provided by the caller to identify
	// the object in event callbacks.  We use this rather than an object
	// pointer because a caller-generated ID lets the caller control the
	// lifetime and reuse of the IDs.
	DWORD cookie;

	// Next available cookie.  We use this to assign a cookie each
	// time we create a new player session.
	static DWORD nextCookie;

	// Pending deletion queue
	static std::list<RefPtr<AudioVideoPlayer>> pendingDeletion;

	// deletion queue resource lock
	static CriticalSection pendingDeletionLock;
};
