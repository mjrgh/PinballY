// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// LibVLC Audio Video Player.  This is an implementation of our
// generic AudioVideoPlayer interface based on libvlc.
//
// Important:  NEVER call any libvlc export functions directly
// by name.  Instead, always go through our dynamically bound
// function pointer variables, which have the same names as the
// corresponding functions, with a "_" appended.  For example,
// we call libvlc_new_() instead of libvlc_new().  If you get an
// error from the linker claiming that libvlc_xxx() is undefined,
// it means that you tried calling libvlc_xxx() directly instead
// of calling libvlc_xxx_().  Just add the "_" suffix.  If you 
// then get an error from the compiler saying that libvlc_xxx_()
// is undefined, you simply need to add libvlc_xxx to the list 
// of explicitly imported symbols.  That's easy but requires an
// extra step: see HOW TO ADD A NEW FUNCTION BINDING below.
//
// DO NOT ADD A DEPENDENCY ON LIBVLC.LIB.  If you're getting
// linker errors for missing symbols from LIBVLC, it's because
// you're not following the rules above about "_" symbols.  Read
// that section again, and DO NOT ADD LIBVLC.LIB to the link!
// We don't link to LIBVLC.LIB anywhere, either via a #pragma 
// comment(lib) directive or in the list of library dependencies 
// in the project.  This is INTENTIONAL and important.  We must
// load the DLL manually in our own code rather than letting the 
// Windows loader do so automatically from link-time imports in
// the .EXE, for reasons described below under "Libvlc DLL
// Dynamic Loading".

#include "stdafx.h"
#include "PrivateWindowMessages.h"
#include "VLCAudioVideoPlayer.h"
#include "resource.h"
#include "D3D.h"
#include "Sprite.h"
#include "TextureShader.h"
#include "I420Shader.h"
#include "Application.h"


// The VLC public API depends on the Posix type ssize_t ("signed size_t"),
// which isn't defined in the Visual C++ headers.  The Windows SDK headers 
// do define their own equivalent of this, SSIZE_T, so use it to create an
// ssize_t typedef for the VLC headers.
typedef SSIZE_T ssize_t;

// import the main VLC SDK interface
#include <vlc/vlc.h>

// -----------------------------------------------------------------------
//
// Libvlc DLL Dynamic Loading
//
// We load the libvlc DLL dynamically, rather than binding to the DLL
// in the normal fashion via the linker, because we want to control the
// timing of the DLL load.  Binding the DLL through the linker causes
// the DLL to load when the EXE starts up, which doesn't work for us
// because we want to intervene before the DLL loads to fix up the
// environment.
//
// In particular, we need to communicate the correct plugins folder
// path to libvlc.  The way that libvlc takes this information is via
// a system environment variable (VLC_PLUGIN_PATH), so we need to be
// able to set an environment variable prior to the DLL load.  You'd
// think at first glance (I did, anyway) that it would be sufficient
// to set this before calling libvlc_new(), but unfortunately that's
// not the case.  The reason is that the libvlc DLL's C RTL makes a
// snapshot of the Windows system environment within its DllMain() 
// startup.  This means that any environment variable we want to add
// has to be set in the process context before the DLL is loaded.
// So the only way we can communicate environment variable information
// to the DLL is to defer the DLL load until we're running, which
// means loading the DLL dynamically.
//
// Why do we need to pass the extra environment variable information
// in the first place?  It's just a matter of aesthetics.  The VLC
// default plugins folder is "<exe folder>\plugins".  That's great
// for the main VLC player app, but it's kinda sucky for third-party
// applications like this one, because anyone looking at our app
// folder would assume that a "plugins" folder is for something
// that plugs into the app itself, not just a video subsystem.  This
// is especially true because users are accustomed to our friend
// PinballX having its own plugin architecture and, naturally, a
// deployment folder called "plugins".  If users find a "plugins"
// folder in our install folder, they're naturally going to think
// it's for our plugins, and I don't want to have to clear up that
// confusion over and over - better not to create the confusion in
// the first place.  So for our deployment, I want to rename the
// VLC plugins folder to make it clear that it's for VLC plugins,
// not PinballX or PinballY plugins.
// 

// HOW TO ADD A NEW FUNCTION BINDING:
//
// It's easy.  You just need to add two formulaic lines for each
// function you need to import:
//
// - a LIBVLC_ENTRYPOINT() line for the function in the list
//   immediately below
//
// - a LIBVLC_BIND() line in the similar list a little later
//
// Remember to always call these functions via the "_" suffixed
// version of the name: e.g., call libvlc_new_() in place of
// libvlc_new().
//

// Define pointers the libvlc entrypoints we use
#define LIBVLC_ENTRYPOINT(func) static decltype(func) *func##_;
LIBVLC_ENTRYPOINT(libvlc_audio_set_mute)
LIBVLC_ENTRYPOINT(libvlc_audio_set_volume)
LIBVLC_ENTRYPOINT(libvlc_errmsg)
LIBVLC_ENTRYPOINT(libvlc_event_attach)
LIBVLC_ENTRYPOINT(libvlc_get_version)
LIBVLC_ENTRYPOINT(libvlc_media_add_option)
LIBVLC_ENTRYPOINT(libvlc_media_player_event_manager)
LIBVLC_ENTRYPOINT(libvlc_media_player_release)
LIBVLC_ENTRYPOINT(libvlc_media_player_new_from_media)
LIBVLC_ENTRYPOINT(libvlc_media_player_play)
LIBVLC_ENTRYPOINT(libvlc_media_player_set_time)
LIBVLC_ENTRYPOINT(libvlc_media_player_stop)
LIBVLC_ENTRYPOINT(libvlc_media_new_path)
LIBVLC_ENTRYPOINT(libvlc_media_release)
LIBVLC_ENTRYPOINT(libvlc_new)
LIBVLC_ENTRYPOINT(libvlc_release)
LIBVLC_ENTRYPOINT(libvlc_video_set_callbacks)
LIBVLC_ENTRYPOINT(libvlc_video_set_format_callbacks)
LIBVLC_ENTRYPOINT(libvlc_log_set)


// Have we attempted to load the libvlc DLL yet?
static bool libvlcLoaded = false;

// Was the load successful?
static bool libvlcOk = false;

// libvlc DLL handle
static HMODULE hmoduleLibvlc = NULL;

#ifdef _WIN64
#define VLC_ROOT_DIR _T("VLC64")
#else
#define VLC_ROOT_DIR _T("VLC")
#endif

// Import the libvlc entrypoints
static bool LoadLibvlc(ErrorHandler &eh)
{
	// do nothing if we've already loaded VLC
	if (libvlcLoaded)
		return hmoduleLibvlc != NULL;

	// we've now made the attempt (even if it doesn't succeed)
	libvlcLoaded = true;

	// internal function to log a system error and return false
	auto Failure = [&eh](const TCHAR *desc)
	{
		WindowsErrorMessage winErr;
		eh.SysError(LoadStringT(IDS_ERR_VIDEOPLAYERSYSERR),
			MsgFmt(_T("%s: Windows error %d, %s"), desc, winErr.GetCode(), winErr.Get()));
		return false;
	};

	// Before we load the DLL, set the plugins path.  This is done
	// via an environment variable.  It's essential to do this before
	// loading the libvlc DLL for the first time, because the DLL makes 
	// a private snapshot of the environment as it exists when the DLL
	// is first loaded.  That means that we can't communicate anything
	// to it via environment variables after this point.  Note also that
	// we're setting the environment variable in the Windows system
	// environment, NOT the C++ runtime environment (via putenv() and
	// the like).  That's because the DLL has no access to our C++
	// environment - its snapshot comes strictly from the Windows
	// system settings.
	TCHAR pluginsPath[MAX_PATH];
	GetDeployedFilePath(pluginsPath, VLC_ROOT_DIR _T("\\plugins"), _T(""));
	SetEnvironmentVariable(_T("VLC_PLUGIN_PATH"), pluginsPath);

	// set minimum verbosity (to try to reduce the copious
	// OutputDebugString garbage that libvlc generates; doesn't
	// actually seem to reduce it by much)
	SetEnvironmentVariable(_T("VLC_VERBOSE"), _T("-1"));

	// Load libvlccore first, so that it's in memory when libvlc.dll
	// tries to bind to it statically.
	TCHAR libvlccorePath[MAX_PATH];
	GetDeployedFilePath(libvlccorePath, VLC_ROOT_DIR _T("\\libvlccore.dll"), _T(""));
	if (LoadLibrary(libvlccorePath) == NULL)
		return Failure(MsgFmt(_T("Unable to load %s"), libvlccorePath));

	// load libvlc
	TCHAR libvlcPath[MAX_PATH];
	GetDeployedFilePath(libvlcPath, VLC_ROOT_DIR _T("\\libvlc.dll"), _T(""));
	if ((hmoduleLibvlc = LoadLibrary(libvlcPath)) == NULL)
		return Failure(MsgFmt(_T("Unable to load %s"), libvlcPath));

	// Bind the entrypoints we access
#define LIBVLC_BIND(func) \
    if ((func##_ = reinterpret_cast<decltype(func)*>(GetProcAddress(hmoduleLibvlc, #func))) == nullptr) \
		return Failure(_T("Unable to bind libvlc function ") _T(#func) _T("()"));

	LIBVLC_BIND(libvlc_audio_set_mute)
	LIBVLC_BIND(libvlc_audio_set_volume)
    LIBVLC_BIND(libvlc_errmsg)
    LIBVLC_BIND(libvlc_event_attach)
	LIBVLC_BIND(libvlc_get_version)
	LIBVLC_BIND(libvlc_media_add_option)
    LIBVLC_BIND(libvlc_media_player_event_manager)
    LIBVLC_BIND(libvlc_media_player_release)
    LIBVLC_BIND(libvlc_media_player_new_from_media)
    LIBVLC_BIND(libvlc_media_player_play)
    LIBVLC_BIND(libvlc_media_player_set_time)
    LIBVLC_BIND(libvlc_media_player_stop)
    LIBVLC_BIND(libvlc_media_new_path)
    LIBVLC_BIND(libvlc_media_release)
    LIBVLC_BIND(libvlc_new)
    LIBVLC_BIND(libvlc_release)
    LIBVLC_BIND(libvlc_video_set_callbacks)
    LIBVLC_BIND(libvlc_video_set_format_callbacks)
	LIBVLC_BIND(libvlc_log_set)

	// success
	libvlcOk = true;
	return true;
}

// -----------------------------------------------------------------------
//
// VLC Audio Video Player class
//


// statics
libvlc_instance_t *VLCAudioVideoPlayer::vlcInst = nullptr;
bool VLCAudioVideoPlayer::initFailed = false;

const char *VLCAudioVideoPlayer::GetLibVersion()
{
	// load the libvlc DLLs if we haven't already
	if (!LoadLibvlc(Application::InUiErrorHandler()))
		return nullptr;

	// retrieve the version string
	return libvlc_get_version_();
}

VLCAudioVideoPlayer::VLCAudioVideoPlayer(HWND hwndVideo, HWND hwndEvent, bool audioOnly) :
	AudioVideoPlayer(hwndVideo, hwndEvent, audioOnly),
	isPlaying(false),
	looping(false),
	muted(false),
	player(nullptr),
	media(nullptr),
	firstFramePresented(false),
	shader(nullptr),
	dmd(nullptr),
	nPlanes(0),
	volume(100)
{
	// load the libvlc DLLs if we haven't already
	LoadLibvlc(Application::InUiErrorHandler());

	// Note that audioOnly is accepted but ignored.  We just go ahead 
	// and decode the video into our D3D11 textures anyway, and the 
	// client can ignore it by not drawing the textures anywhere.
	// It would be more efficient if we could find a way to tell VLC
	// not to decode the video at all, but I don't think it has a way
	// to do that.  Perhaps we could at least save some memory by
	// creating small textures instead of decoding at full size, but
	// that might actually be worse for overall performance because it
	// would VLC to rescale the images.
}

void VLCAudioVideoPlayer::OnAppExit()
{
    // if we created a libvlc instance, release it
    if (vlcInst != nullptr)
    {
        libvlc_release_(vlcInst);
        vlcInst = nullptr;
    }
}

VLCAudioVideoPlayer::~VLCAudioVideoPlayer()
{
	// Shut down VLC
	Shutdown();
}

void VLCAudioVideoPlayer::Shutdown()
{
	// stop playback
	if (player != nullptr)
		Stop(SilentErrorHandler());

	// release the VLC objects
	if (player != nullptr)
	{
		CriticalSectionLocker lock(playerLock);
		libvlc_media_player_release_(player);
		player = nullptr;
	}
	if (media != nullptr)
	{
		libvlc_media_release_(media);
		media = nullptr;
	}
}

bool VLCAudioVideoPlayer::OpenWithTarget(const TCHAR *path, ErrorHandler &eh, TargetDevice target)
{
	// remember the media object
	mediaPath = path;

	// make sure libvlc is available
	if (!libvlcOk)
		return false;

	// release any existing media player
	if (player != nullptr)
	{
		CriticalSectionLocker lock(playerLock);
		libvlc_media_player_release_(player);
		player = nullptr;
	}

	// release any existing media object
	if (media != nullptr)
	{
		libvlc_media_release_(media);
		media = nullptr;
	}

	// presume failure
	bool ok = false;
	do
	{
		// create the VLC instance if we haven't already
		if (vlcInst == nullptr)
		{
			// Set some special options:
			//
			// --no-lua - disable LUA support.  LUA is a scripting language,
			// which we have no use for.  Disabling it speeds up the DLL 
			// loading.
			//
			// --deinterlace=0 - disable the de-interlacing filter.  It
			// would be nicer if we could leave this enabled, but VLC's
			// deinterlacing filter currently (as of 3.0.8) has a huge
			// limitation, which is that it doesn't handle any formats with
			// alpha channel (transparency) information.  Alpha support is
			// necessary for video layering.  Interlacing is commonly used
			// for broadcast media, but is rare for computer media, so I
			// don't think it'll be a significant limitation to remove the
			// filter.  If anyone runs into problems with unplayable videos
			// that turns out to be due to interlacing, they could run them
			// through ffmpeg to deinterlace them, or if that's a problem
			// for some reason, we could add a global program option to
			// enable this.
			//
			// --verbose=0 --quiet - disable as much logging as we can.
			// libvlc generates tons of OutputDebugString messages, which
			// waste CPU time and clutter the debugger console in dev
			// builds.  There's no way to disable most of them, but
			// these options are supposed to at least reduce them.  In
			// practice, unfortunately, not by much.
			// 
			static const char *args[] = {
				"--no-lua",
				"--deinterlace=0",
				"--verbose=-1",
				"--quiet",
			};
			if ((vlcInst = libvlc_new_(countof(args), args)) == nullptr)
			{
				// VLC init failed.  If this has happened before, don't
				// bother showing another message; just fail silently.
				// One initialization failure usually means we'll never
				// be able to initialize, so there's no benefit in showing
				// the same error every time we try to load a video.
				if (!initFailed)
				{
					// remember the initialization failure in case we try again
					initFailed = true;

					// Show an error.  We usually can't get more details from
					// VLC when we can't load VLC in the first place, but give
					// it a shot on the off chance.
					const char *errmsg = libvlc_errmsg_();
					eh.SysError(LoadStringT(IDS_ERR_VIDEOPLAYERSYSERR),
						errmsg != nullptr ? MsgFmt(_T("Error initializing libvlc: %hs"), errmsg) :
						_T("Error initializing libvlc"));
				}
				break;
			}
		}

		// Create a media item from the file path
		if ((media = libvlc_media_new_path_(vlcInst, WideToAnsi(path, CP_UTF8).c_str())) == nullptr)
		{
			eh.SysError(LoadStringT(IDS_ERR_VIDEOPLAYERSYSERR),
				MsgFmt(_T("Creating media item for %s: %hs"), path, libvlc_errmsg_()));
			break;
		}

		// create a media player for the media item
		if ((player = libvlc_media_player_new_from_media_(media)) == nullptr)
		{
			eh.SysError(LoadStringT(IDS_ERR_VIDEOPLAYERSYSERR),
				MsgFmt(_T("Creating media player for %s: %hs"), path, libvlc_errmsg_()));
			break;
		}

		// set the initial volume
		libvlc_audio_set_volume_(player, volume);

		// register for events
		libvlc_event_attach_(libvlc_media_player_event_manager_(player), libvlc_MediaPlayerEndReached, &OnMediaPlayerEndReached, this);

		// Set up the decoding callbacks.  Choose the set according to the
		// target device type.
		switch (target)
		{
		case VideoTarget:
			libvlc_video_set_callbacks_(player, &OnVideoFrameLock, &OnVideoFrameUnlock, &OnVideoFramePresent, this);
			libvlc_video_set_format_callbacks_(player, &OnVideoSetFormat, &OnVideoFormatCleanup);
			break;

		case DMDTarget:
			libvlc_video_set_callbacks_(player, &OnVideoFrameLock, &OnDMDFrameUnlock, &OnDMDFramePresent, this);
			libvlc_video_set_format_callbacks_(player, &OnDMDSetFormat, &OnVideoFormatCleanup);
			break;
		}

		// success
		ok = true;

	} while (false);

	// on failure, delete any half-baked objects we created
	if (!ok)
	{
		if (player != nullptr)
		{
			CriticalSectionLocker lock(playerLock);
			libvlc_media_player_release_(player);
			player = nullptr;
		}
		if (media != nullptr)
		{
			libvlc_media_release_(media);
			media = nullptr;
		}
	}

	// return the status
	return ok;
}

bool VLCAudioVideoPlayer::Play(ErrorHandler &eh)
{
	// proceed only if there's a player
	if (player == nullptr)
	{
		eh.SysError(LoadStringT(IDS_ERR_VIDEOPLAYERSYSERR),
			_T("VLCAudioVideoPlayer::Play() called with no media player object"));
		return false;
	}

	// if we're already playing, there's nothing to do
	if (isPlaying)
		return true;

	// the first frame hasn't been presented yet
	firstFramePresented = false;

	// Set muting mode and volume.  The libvlc documentation says that the muting
	// function is unreliable, so we'll just set the volume to zero insetad.
	libvlc_audio_set_volume_(player, muted ? 0 : volume);

	// start playback
	libvlc_media_player_play_(player);

	// playback started
	isPlaying = true;

	// The libvlc bug that affects audio volume on replays might also be
	// causing volume problems on *first* plays on some machines, where
	// we seem to have muted initial plays in some cases.  I'm thinking
	// that the anamolous volume reset that we know occurs on replay might
	// also occur on the first play, and in that case it might be using
	// uninitialized data that on some machines manifests as a muted
	// first play.  So we'll do our explicit volume setting the first
	// time through as well.
	LaunchVolInitThread();

	// success
	return true;
}

bool VLCAudioVideoPlayer::Replay(ErrorHandler &eh)
{
	// proceed only if there's a player
	if (player == nullptr)
	{
		eh.SysError(LoadStringT(IDS_ERR_VIDEOPLAYERSYSERR),
			_T("VLCAudioVideoPlayer::Replay() called with no media player object"));
		return false;
	}

	// rewind
	libvlc_media_player_stop_(player);
	libvlc_media_player_set_time_(player, 0);

	// start playback
	libvlc_media_player_play_(player);

	// playback (re-)started
	isPlaying = true;

	// libvlc has a truly egregious bug with looped video.  When we restart
	// playback, libvlc will forget the audio volume and mute status, resetting
	// to unmuted full volume.  It would be one thing if we could just restore
	// the audio settings here, but it's worse than that: the reset happens in
	// the playback thread, asynchronously, some time after playback resumes.
	// Empirically this takes about 30ms on my machine, but that undoubtedly
	// varies from machine to machine and by phase of the moon.  It's not
	// acceptble to take a 30-50ms delay here, as that would stall the UI for
	// a noticeable period.  Instead, set up a background thread to do the 
	// work after a suitable delay.
	LaunchVolInitThread();

	// success
	return true;
}

void VLCAudioVideoPlayer::LaunchVolInitThread()
{
	auto RestoreThread = [](LPVOID param) -> DWORD
	{
		// get my self-reference from the parameter
		RefPtr<VLCAudioVideoPlayer> self(static_cast<VLCAudioVideoPlayer*>(param));

		// Restore the audio settings on a delay.  Do this several times to
		// account for the inherent unpredictability of when the background
		// thread actually wakes up.  Since we're only restoring the desired
		// current settings each time, it's harmless to do this redundantly.
		for (int tries = 0; tries < 6; ++tries)
		{
			// pause to let the playback thread start up
			Sleep(10);

			// critical section
			{
				// if we're not still playing, abort
				CriticalSectionLocker lock(self->playerLock);
				if (!self->isPlaying || self->player == nullptr)
					break;

				// reset the audio status
				libvlc_audio_set_volume_(self->player, self->muted ? 0 : self->volume);
			}
		}

		// done
		return 0;
	};
	DWORD tid;

	// add a reference on behalf of the thread, and start the thread
	AddRef();
	HandleHolder hThread(CreateThread(NULL, 0, RestoreThread, this, 0, &tid));

	// if the thread failed, forget its added reference
	if (hThread == NULL)
		Release();
}

bool VLCAudioVideoPlayer::Stop(ErrorHandler &eh)
{
	// proceed only if there's a player
	if (player == nullptr)
	{
		eh.SysError(LoadStringT(IDS_ERR_VIDEOPLAYERSYSERR),
			_T("VLCAudioVideoPlayer::Stop() called with no media player object"));
		return false;
	}

	// if we're not playing, there's nothing to do
	if (!isPlaying)
		return true;

	// stop playback
	isPlaying = false;
	libvlc_media_player_stop_(player);

	// success
	return true;
}

void VLCAudioVideoPlayer::Mute(bool f)
{
	// remember the new muting mode internally
	muted = f;

	// Set muting on the player, if present.  Note that the libvlc muting
	// function (libvlc_audio_set_mute) isn't reliable (the documentation
	// says so and experience bears this out; it sometimes works but often
	// doesn't).  Setting the volume to zero seems more reliable.
	if (player != nullptr)
		libvlc_audio_set_volume_(player, muted ? 0 : volume);
}

void VLCAudioVideoPlayer::SetVolume(int pctVol)
{
	volume = pctVol;
	if (player != nullptr)
		libvlc_audio_set_volume_(player, muted ? 0 : volume);
}

void VLCAudioVideoPlayer::SetLooping(bool f)
{
	// remember the new looping mode
	looping = f;
}

unsigned int VLCAudioVideoPlayer::OnVideoSetFormat(void **opaque, char *chroma,
	unsigned *width, unsigned *height, unsigned *pitches, unsigned *lines)
{
	// get the 'this' pointer
	auto self = reinterpret_cast<VLCAudioVideoPlayer*>(*opaque);

	// plane descriptions, to be set according to the format
	int nPlanes = 0;
	FrameBuffer::Plane planes[4];

	// shader, to be chosen according to the format
	Shader *shader = nullptr;

	// Choose a format based on the format proposed by the caller.
	// 
	// NOTE: The code below explicitly skips the I444 case, and funnels
	// everything into I420 format regardless of the proposed format.
	// In principle, we'd want to choose the closest format to the one
	// proposed, as this would presumably give us the shortest code
	// path through vlc and the best fidelity.  I444 in particular
	// should be slightly better than I420 because of its higher chroma
	// resolution (full chroma sampling for I444, vs 2x2 sub-sampling 
	// for I420).  But in practice, vlc actually seems to work better
	// using I420 unconditionally.  By work better, I mean that it
	// achieves real-time playback more consistently, with fewer late
	// or missed frames.  The smaller buffer sizes in I420 (thanks to
	// the chroma sub-sampling) must outweigh any conversion cost.
	// 
	// Note also that there are a few video formats that use an RGB 
	// color space.  These are rare, mostly old defunct Microsoft
	// formats that (ironically) the Windows media layers no longer 
	// support, but you see them occasionally in old HyperPin media
	// files (e.g., from vpforums.org).  And you'd think that it
	// would be a huge win to decode these to RGB rather than having
	// libvlc convert RGB to YUV just so we can convert it back for
	// rendering.  But surprisingly - maybe exactly because they're
	// mostly very old files that were originally created for much
	// slower hardware - even these play back quite happily in I420
	// mode.  Given that and given their rarity and obsolescence, I 
	// don't see any good reason to add any RGB targets here.
	if (/* DISABLED */false && memcmp(chroma, "I444", 4) == 0)
	{
		// I444 uses 8 bits per pixel in three planes (Y U V).
		nPlanes = 3;
		pitches[0] = pitches[1] = pitches[2] = *width;
		lines[0] = lines[1] = lines[2] = *height;

		// set up the plane texture descriptors
		planes[0].textureDesc = planes[1].textureDesc = planes[2].textureDesc = CD3D11_TEXTURE2D_DESC(
			DXGI_FORMAT_R8_UNORM, pitches[0], lines[0], 1, 1,
			D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE, 0, 1, 0, 0);
		
		// use the I420/I444 shader
		shader = Application::Get()->i420Shader.get();
	}
	else
	{
		// For anything else, use YUV 4:2:0 (FOURCC code 'I420').
		//
		// YUV 4:2:0 decodes to three separate planes, with 8 bits per 
		// pixel in each plane.  The Y plane has one byte per image pixel, 
		// and the U and V planes are sub-sampled in 2x2 blocks, so they're
		// half the width and height of the Y plane.
		//
		// Adjust the row pitches to multiples of 128.  Some alignments
		// are more efficient on some hardware; I don't think there's a
		// single ideal alignment, but the hardware-specific ideals are
		// virtually always powers of 2.  A higher power of 2 will be
		// aligned at any smaller power of 2 as well, so picking a fairly
		// large power of 2 for our generic alignment should work well
		// across a range of hardware.
		nPlanes = 3;
		pitches[0] = (*width + 127) / 128 * 128;
		pitches[1] = pitches[2] = ((*width + 1) / 2 + 127) / 128 * 128;
		lines[0] = *height;
		lines[1] = lines[2] = (*height + 1) / 2;

		// set up the plane texture descriptors
		planes[0].textureDesc = CD3D11_TEXTURE2D_DESC(
			DXGI_FORMAT_R8_UNORM, *width, lines[0], 1, 1,
			D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE, 0, 1, 0, 0);

		planes[1].textureDesc = planes[2].textureDesc = CD3D11_TEXTURE2D_DESC(
			DXGI_FORMAT_R8_UNORM, (*width + 1) / 2, lines[1], 1, 1,
			D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE, 0, 1, 0, 0);

		// If the source format uses alpha transparency, use YUVA 4:2:0
		// instead.  This format is exactly like YUV 4:2:0, but it adds 
		// a fourth plane with the alpha channel, using the same 8-bit
		// format as the Y channel.
		if (memcmp(chroma, "RGBA", 4) == 0
			|| memcmp(chroma, "ARGB", 4) == 0
			|| memcmp(chroma, "BGRA", 4) == 0
			|| memcmp(chroma, "RGA0", 4) == 0
			|| memcmp(chroma, "RGA4", 4) == 0
			|| memcmp(chroma, "YUVA", 4) == 0
			|| memcmp(chroma, "I40A", 4) == 0
			|| memcmp(chroma, "I42A", 4) == 0)
		{
			// Add the fourth plane for alpha.  This is identical to
			// the first plane.
			nPlanes = 4;
			pitches[3] = pitches[0];
			lines[3] = lines[0];
			planes[3].textureDesc = planes[0].textureDesc;

			// set the libvlc output format to YUVA 4:2:0
			memcpy(chroma, "I40A", 4);

			// use the YUVA 4:2:0:4 shader
			shader = Application::Get()->i420AShader.get();
		}
		else if (memcmp(chroma, "YA0L", 4) == 0  // YUVA 4:4:4 10 bits per pixel, little-endian
			|| memcmp(chroma, "YA0B", 4) == 0)   // YUVA 4:4:4 10 bits per pixel, big-endian
		{
			// YUVA 4:4:4:4, 10 bits per pixel per channel.  This requires
			// special handling, because libvlc doesn't have a converter of
			// its own that can translate between this and 420A.  If we ask
			// for 420A output, libvlc will instead downgrade to 420, losing
			// the alpha.  If we want the alpha, we have to let libvlc pass
			// the yuva444p10le format straight through to our buffers.
			//
			// It's worth the extra trouble to support this format because 
			// it's one of the few codec/format combinations that supports
			// alpha at all, and might be the only one at the moment that
			// supports it well.  The other alternatives all seem to be low-
			// compression codecs like PNG and RLE, which work but make for
			// gigantic files.  Apple's prores_ks might be the only extant
			// video codec that handles alpha.  (If you read through Apple's
			// whitepaper on prores, you can see why - integrating alpha
			// into a video codec is extremely tricky because lapha has very 
			// different compression characteristics from the chroma and 
			// luma components.  You basically have to use a lossless
			// compression algorithm for alpha to recover a usable signal.
			// That's tricky in video because you still want to compress
			// the other components using traditional lossy encodings, so
			// you have to be able to mix lossy and lossless encoding in
			// a single stream.)

			// First off, this is a 4-plane format like regular YUVA
			nPlanes = 4;

			// It's a 4:4:4:4 format with 10 bits per pixel.  libvlc will
			// unpack each 10-bit pixel into a byte pair, so we need two
			// bytes per pixel.  All planes are the same size.
			pitches[0] = pitches[1] = pitches[2] = pitches[3] = (*width * 2 + 127) / 128 * 128;

			// one pixel per line in all planes
			lines[0] = lines[1] = lines[2] = lines[3] = *height;

			// Set up the texture descriptors.  All planes use the same 
			// format, so we just copy the same descriptor to all planes.  
			//
			// Here's where things get a bit tricky.  The format we'd really
			// like here is xxx_R10_UNORM, for a 10-bit normalized int format.
			// DXGI dosen't have such a thing.  Its closest equivalent is
			// xxx_R16_UNORM.  R16 is a good match because it also works with
			// two bytes per pixel and interprets them as little-endian ints.
			// It's not a perfect match, though, in that it normalizes to a
			// 16-bit space when it passes the pixels to the shader.  The
			// atcual pixels are all 10-bit values, though.  
			// 
			// There's nothing too magical about the normalization, though.
			// All that really happens is that the shader will see each byte
			// pair as a 16-bit "fixed point float" value, with the decimal
			// point immediately before the most significant bit.  Another
			// (easier) way to look at this is that each byte pair is
			// interpreted as
			//
			//     static_cast<float>(byte_pair)/65535.0f
			//
			// What we WANT is to do a similar normalization based on the
			// 10-bit pixels that libvlc will hand us, which means we want
			//
			//     static_cast<float>(byte_pair)/1023.0f
			//
			// Once you look at it that way, it becomes pretty easy to see
			// how to deal with this.  One way would be to do a 6-bit left
			// shift on all of the byte pairs before creating the DXGI
			// texture.  But that would be extremely CPU-intensive.  A
			// better way would be to do the adjustment in the shader, where
			// we can take advantage of the high parallelism of the GPU to
			// do the renormalization.  A little trivial arithmetic tells us
			// that the shader just has to multiply each 'float' value that
			// it receives by 64.0f.
			//
			// So: start by setting up the 16_UNORM texture descs...
			planes[0].textureDesc = planes[1].textureDesc = planes[2].textureDesc = planes[3].textureDesc =
				CD3D11_TEXTURE2D_DESC(
				DXGI_FORMAT_R16_UNORM, *width, lines[0], 1, 1,
				D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE, 0, 1, 0, 0);

			// ...and then compensate for the wrong normalization by using
			// our special shader that's aware of the 10-bit pixels.  Our
			// I420A10 shader has the extra processing to renormalize all 
			// of the pixel values at render time.
			shader = Application::Get()->i444A10Shader.get();

			// Finally, force the conversion to little-endian, if it isn't
			// already.  YA0B is the big-endian version of this format, 
			// which is identical except that it reverses the order of the
			// bytes in the per-pixel byte pairs.  We could fix that up in 
			// the shader (like the normalization), but in this case I'm
			// going to ask libvlc to do it in the CPU, since I don't want
			// to get into byte-order twiddling in the shader.  That could
			// create some subtle hardware dependencies.  I've tested that
			// libvlc can do YA0B -> YA0L without losing the alpha, so
			// let's not add another shader for this.
			memcpy(chroma, "YA0L", 4);
		}
		else
		{
			// Regular non-alpha format.  Force the output format to
			// YUV 4:2:0.
			memcpy(chroma, "I420", 4);

			// use the YUV shader
			shader = Application::Get()->i420Shader.get();
		}
	}

	// calculate the buffer size
	size_t bufsize = 0;
	for (int i = 0; i < nPlanes; ++i)
	{
		planes[i].rowPitch = pitches[i];
		planes[i].bufOfs = bufsize;

		bufsize += pitches[i] * lines[i];
	}

	// send a format update to the event window
	FormatDesc formatDesc;
	formatDesc.width = *width;
	formatDesc.height = *height;
	SendMessage(self->hwndEvent, AVPMsgSetFormat, static_cast<WPARAM>(self->cookie), reinterpret_cast<LPARAM>(&formatDesc));

	// lock the object while updating its fields
	CriticalSectionLocker locker(self->lock);

	// allocate the frame buffers
	for (auto &f : self->frame)
	{
		// allocate a new frame
		f.Attach(new FrameBuffer());

		// remember the frame dimensions
		f->dims.cx = *width;
		f->dims.cy = *height;

		// remember the shader
		f->shader = shader;

		// remember the plane descriptors
		f->nPlanes = nPlanes;
		for (int i = 0; i < nPlanes; ++i)
		{
			f->planes[i].textureDesc = planes[i].textureDesc;
			f->planes[i].bufOfs = planes[i].bufOfs;
			f->planes[i].rowPitch = planes[i].rowPitch;
		}

		// allocate the pixel array
		f->pixBuf.reset(static_cast<BYTE*>(_mm_malloc(bufsize, 128)));
		if (f->pixBuf == nullptr)
			return 0;
	}

	// return the buffer count
	return countof(self->frame);
}

void VLCAudioVideoPlayer::OnVideoFormatCleanup(void *opaque)
{
	// get the 'this' pointer and lock it while working
	auto self = reinterpret_cast<VLCAudioVideoPlayer*>(opaque);
	CriticalSectionLocker locker(self->lock);

	// Free buffers
	for (int i = 0; i < countof(self->frame); ++i)
		self->frame[i] = nullptr;
}

void *VLCAudioVideoPlayer::OnVideoFrameLock(void *opaque, void **planes)
{
	// get the 'this' pointer and lock it while working
	auto self = reinterpret_cast<VLCAudioVideoPlayer*>(opaque);
	CriticalSectionLocker locker(self->lock);

	// keep going until we can satisfy the request
	for (;;)
	{
		// look for a free frame
		for (auto &f : self->frame)
		{
			// use the buffer if it's free
			if (f->status == FrameBuffer::Free)
			{
				// lock this frame
				f->status = FrameBuffer::Locked;

				// Return the pixel buffer for each plane.  Recall that
				// the three planes are packed into a single byte array,
				// so can find each plane's memory address by adding its
				// offset to the base buffer address.
				BYTE *p = f->pixBuf.get();
				for (int i = 0; i < f->nPlanes; ++i)
					planes[i] = p + f->planes[i].bufOfs;

				// Add a reference to the frame on behalf of libvlc.
				// This will ensure that the frame stays alive as long
				// as libvlc is using it.
				f->AddRef();

				// the raw frame buffer object pointer is the frame ID
				return f.Get();
			}
		}

		// We failed to find a free frame buffer, so wait until one 
		// becomes available.  Old frames are freed up as new frames 
		// are presented, so we just have to wait a bit for the 
		// presentation clock to catch up with the decoder.  While
		// waiting, we have to release the object lock, so that the 
		// presentation thread can update the frame status variables.
		locker.Unlock();

		// pause briefly
		Sleep(5);

		// lock the object again and continue searching
		locker.Lock(self->lock);
	}
}

void VLCAudioVideoPlayer::OnVideoFrameUnlock(void *opaque, void *pictureId, void *const *planes)
{
	// Note: we don't have to hold the video player object lock
	// at any point in this routine, even though we're accessing
	// the buffer object.  The buffer's status==Locked makes it
	// off-limits for any other threads to touch, so it's already
	// protected.  And we don't have to access anything else in 
	// the video player object itself.

	// do nothing if the picture ID is null
	if (pictureId == nullptr)
		return;

	// the "picture ID" is actually our frame buffer pointer
	FrameBuffer *f = reinterpret_cast<FrameBuffer*>(pictureId);

	// the buffer now has a valid decoded frame
	f->status = FrameBuffer::Valid;
}

void VLCAudioVideoPlayer::OnVideoFramePresent(void *opaque, void *pictureId)
{
	// do nothing if the picture ID is null
	if (pictureId == nullptr)
		return;

	// The "picture ID" is actually our frame buffer pointer.  Take
	// over libvlc's reference on the frame object.
	RefPtr<FrameBuffer> f(reinterpret_cast<FrameBuffer*>(pictureId));

	// get the 'this' pointer
	auto self = reinterpret_cast<VLCAudioVideoPlayer*>(opaque);

	// hold the render resource lock while updating presentedFrame
	{
		// acquire the lock
		CriticalSectionLocker renderLocker(self->renderLock);

		// If another frame was previously presented, that frame is
		// now free.  Note that it's okay to free the frame currenty
		// locked by the renderer, because merely updating the buffer
		// status won't affect the frame data.  Instead, we check
		// in the 'lock' routine to make sure that we don't try to
		// re-use a frame that's currently being used for rendering.
		// We get finer resource access granularity and thus less
		// contention by deferring that check until we actually need
		// to write into a buffer.
		if (self->presentedFrame != nullptr && self->presentedFrame != f)
			self->presentedFrame->status = FrameBuffer::Free;

		// this is now the presented frame
		self->presentedFrame = f;

		// advance its state to 'presented'
		f->status = FrameBuffer::Presented;
	}

	// if this the first frame we've presented, notify the event window
	CriticalSectionLocker locker(self->lock);
	if (!self->firstFramePresented)
	{
		// send the 'first frame' message
		PostMessage(self->hwndEvent, AVPMsgFirstFrameReady, (WPARAM)self->cookie, 0);

		// we've now presented the first frame
		self->firstFramePresented = true;
	}
}

void VLCAudioVideoPlayer::OnMediaPlayerEndReached(const libvlc_event_t *event, void *opaque)
{
	// get the 'this' pointer
	auto self = reinterpret_cast<VLCAudioVideoPlayer*>(opaque);

	// if we're in looping mode, restart the video; otherwise notify
	// the event window that playback has finished
	if (self->looping)
	{
		// Tell the event window that it needs to restart the playback
		// for us.  Unfortunately, it doesn't seem to work to do the
		// rewind and replay in the event handler context - this seems
		// to be an undocumented (as far as I can tell) limitation in
		// libvlc.
		PostMessage(self->hwndEvent, AVPMsgLoopNeeded, (WPARAM)self->cookie, 0);
	}
	else
	{
		// not looping - notify the event window that playback is done
		PostMessage(self->hwndEvent, AVPMsgEndOfPresentation, (WPARAM)self->cookie, 0);

		// no longer looping
		self->isPlaying = false;
	}
}

//
// Render the current frame onto a sprite
//
bool VLCAudioVideoPlayer::Render(Camera *camera, Sprite *sprite)
{
	// Lock the current presentation frame.  Note that we only have
	// to hold the object lock while manipulating the internal render
	// frame variables; once we've marked the frame as locked, the
	// background threads will respect our ownership of the frame and
	// won't overwrite its contents until we release it.  This allows
	// the VLC background threads to carry on other work with the
	// frame buffers (such as decoding into other frame buffers)
	// concurrently while we're doing the rendering.
	RefPtr<FrameBuffer> newFrame;
	{
		// lock against concurrent access by the VLC background threads
		CriticalSectionLocker locker(renderLock);

		// If there's a presented frame, take over the reference
		if (presentedFrame != nullptr)
			newFrame.Attach(presentedFrame.Detach());
	}

	// If we have a new presented frame, copy it to GPU memory
	if (newFrame != nullptr)
	{
		// delete the the previous shader resource views
		for (int i = 0; i < nPlanes; ++i)
			shaderResourceView[i] = nullptr;

		// use the shader from the frame
		shader = newFrame->shader;

		// set up the shader resource view descriptor for the frame
		D3D11_SHADER_RESOURCE_VIEW_DESC srvd;
		srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvd.Texture2D.MipLevels = 1;
		srvd.Texture2D.MostDetailedMip = 0;

		// create the shader resource views for each plane
		D3D11_SUBRESOURCE_DATA srd;
		srd.SysMemSlicePitch = 0;
		nPlanes = newFrame->nPlanes;
		for (int i = 0; i < nPlanes; ++i)
		{
			// get the plane
			auto &plane = newFrame->planes[i];

			// create the new texture and view
			shaderResourceView[i] = nullptr;
			srd.pSysMem = newFrame->pixBuf.get() + plane.bufOfs;
			srd.SysMemPitch = plane.rowPitch;
			srvd.Format = plane.textureDesc.Format;
			D3D::Get()->CreateTexture2D(&plane.textureDesc, &srd, &srvd, &shaderResourceView[i], NULL);
		}

		// this frame can now be reused for a new decoded frame
		newFrame->status = FrameBuffer::Free;
	}

	// if there's no shader yet, there's nothing to render
	if (shader == nullptr)
		return false;

	// populate the resource view list to bind to the shader
	ID3D11ShaderResourceView *rv[4];
	int n = 0;
	for (; n < nPlanes && n < countof(shaderResourceView) && shaderResourceView[n] != nullptr; ++n)
		rv[n] = shaderResourceView[n];
	
	// we can't proceed if there were no shader resource views
	if (n == 0)
		return false;

	// bind the shader resource views to the shader
	D3D::Get()->PSSetShaderResources(0, n, rv);

	// prepare the shader for rendering
	shader->PrepareForRendering(camera);
	shader->SetAlpha(sprite->alpha);

	// Do the basic sprite rendering.  This renders the video frame
	// onto the sprite's 3D object.
	sprite->RenderMesh();

	// success
	return true;
}

// -----------------------------------------------------------------------
//
// Real DMD support
// 
// For real DMD playback, we decode into a simple memory buffer in I420
// format, and pass the buffer to the DMD callback object to send to the
// device each time a frame is presented.


unsigned int VLCAudioVideoPlayer::OnDMDSetFormat(void **opaque, char *chroma,
	unsigned *width, unsigned *height, unsigned *pitches, unsigned *lines)
{
	// get the 'this' pointer
	auto self = reinterpret_cast<VLCAudioVideoPlayer*>(*opaque);

	// Real DMD devices use a fixed size of 128x32 pixels, so that's
	// generally our target size for the output.
	//
	// Special case:  if the video is exactly double size (256x64),
	// we'll assume that this is in the special format sometimes
	// used for PinballX real DMD videos, where the DMD pixel
	// structure is mapped onto one pixel per 2x2 block in the
	// video.  This makes the video look like a DMD when played
	// back on a video monitor, but it makes decoding for true
	// DMD playback just a little tricky.  The problem is that
	// the scaling algorithm in the vlc decoder would normally
	// want to average all of those blank pixels, but the blank
	// pixels are meant to represent actual blank spaces in the
	// DMD, and thus don't need to be interpolated into the frame.
	// To get this right, we have to let vlc decode at the input
	// size, keeping the blank pixels, and then *we* have to
	// discard the blank pixels in our rendering step.  The DMD
	// device callback knows to do that if presented with a
	// 256x64 video.
	if (*width == 256 && *height == 64)
	{
		// special case for PinballX double-size media - decode
		// at this same size
	}
	else
	{
		// use the native DMD device 128x32 sizing
		*width = 128;
		*height = 32;
	}

	// set up to decide in I420 mode at the native video size
	memcpy(chroma, "I420", 4);
	pitches[0] = *width;
	pitches[1] = pitches[2] = (*width + 1) / 2;
	lines[0] = *height;
	lines[1] = lines[2] = (*height + 1) / 2;

	// lock the object while updating its fields
	CriticalSectionLocker locker(self->lock);

	// allocate the frame buffers
	for (auto &f : self->frame)
	{
		// allocate the new frame
		f.Attach(new FrameBuffer());

		// remember the frame dimensions
		f->dims.cx = *width;
		f->dims.cy = *height;

		// set up the plane descriptors
		f->nPlanes = 3;
		UINT ofs = 0;
		for (int i = 0; i < 3; ++i)
		{
			f->planes[i].bufOfs = ofs;
			f->planes[i].rowPitch = pitches[i];
			ofs += pitches[i] * lines[i];
		}

		// allocate the pixel buffer
		f->pixBuf.reset(static_cast<BYTE*>(_mm_malloc(ofs, 16)));
		if (f->pixBuf == nullptr)
			return 0;
	}

	// return the buffer count
	return countof(self->frame);
}

void VLCAudioVideoPlayer::OnDMDFrameUnlock(void *opaque, void *pictureId, void *const *planes)
{
	// do nothing if the picture ID is null
	if (pictureId == nullptr)
		return;

	// the "picture ID" is actually our frame buffer pointer
	FrameBuffer *f = reinterpret_cast<FrameBuffer*>(pictureId);

	// the buffer now has a valid decoded frame
	f->status = FrameBuffer::Valid;
}

void VLCAudioVideoPlayer::OnDMDFramePresent(void *opaque, void *pictureId)
{
	// do nothing if the picture ID is null
	if (pictureId == nullptr)
		return;

	// the "picture ID" is actually our frame buffer pointer
	RefPtr<FrameBuffer> f(reinterpret_cast<FrameBuffer*>(pictureId));

	// get the 'this' pointer
	auto self = reinterpret_cast<VLCAudioVideoPlayer*>(opaque);

	// send it to the DMD device
	const BYTE *pix = f->pixBuf.get();
	self->dmd->PresentVideoFrame(f->dims.cx, f->dims.cy, 
		pix, pix + f->planes[1].bufOfs, pix + f->planes[2].bufOfs);

	// this frame is now free
	f->status = FrameBuffer::Free;

	// if this the first frame we've presented, notify the event window
	CriticalSectionLocker locker(self->lock);
	if (!self->firstFramePresented)
	{
		// send the 'first frame' message
		PostMessage(self->hwndEvent, AVPMsgFirstFrameReady, (WPARAM)self->cookie, 0);

		// we've now presented the first frame
		self->firstFramePresented = true;
	}
}
