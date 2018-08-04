// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "../Utilities/Config.h"
#include "RealDMD.h"
#include "GameList.h"
#include "HighScores.h"
#include "Application.h"
#include "DOFClient.h"
#include "VLCAudioVideoPlayer.h"
#include "PlayfieldView.h"
#include "VPinMAMEIfc.h"
#include "DMDView.h"
#include "DMDFont.h"


// We access the DMD device through VPinMAME's DLL interface.  That
// interface has been implemented for all of the DMD device types
// used in pin cabs, so it provides good device independence, and
// any pin cab with a DMD will almost certainly have it installed,
// so we can access the DMD without requiring any adidtional setup
// or configuration steps.
//
// The VPM DMD DLL interface is defined in a header file in the 
// VPinMAME source tree (ext/dmddevice/dmddevice.h), but we don't
// want to include that header directly because it creates compile-
// time links to the DLL imports.  We don't want to be hard-wired
// to the DLL like that.  Instead, we reproduce the necessary
// structures here, and then do the DLL loading and entrypoint
// imports explicitly at run-time.
//
// Note that this will have to be kept in sync with any future 
// changes to the VPM DLL ABI, but the nature of DLLs makes it
// difficult to make incompatible changes without breaking lots
// of user installations, so in practical terms the interface is
// more or less frozen anyway.
namespace DMDDevice
{
	// PinMAME options settings struct, to initialize the DLL
	typedef struct tPMoptions {
		int dmd_red, dmd_green, dmd_blue;			// monochrome base color at 100% brightness
		int dmd_perc66, dmd_perc33, dmd_perc0;		// monochrome brightness levels for 4-level display
		int dmd_only, dmd_compact, dmd_antialias;
		int dmd_colorize;							// colorize mode enabled
		int dmd_red66, dmd_green66, dmd_blue66;		// colorized RGB for brightness level 2/66%
		int dmd_red33, dmd_green33, dmd_blue33;		// colorized RGB for brightness level 1/33%
		int dmd_red0, dmd_green0, dmd_blue0;        // colorized RGB for brightness level 0/0%
	} tPMoptions;

	// RGB color
	typedef struct rgb24 {
		unsigned char red;
		unsigned char green;
		unsigned char blue;
	} rgb24;

	// DMD DLL entrypoints.  
	// NOTE: These are just extern declarations that are never actually
	// linked.  DON'T call these directly!  Doing so will cause linker
	// errors, which we DO NOT want to satisfy by binding these to any
	// actual imports, ESPECIALLY NOT DLL imports!  We don't want to
	// statically bind the .exe to DmdDevice.dll because we explicitly
	// want to allow that file to be ENTIRELY MISSING at run-time.  The
	// only reason we include these extern refs at all is for their type
	// declarations, which we use to create the dynamically bound symbols.
	#define DMDDEV extern
	DMDDEV int Open();
	DMDDEV bool Close();
	DMDDEV void Set_4_Colors_Palette(rgb24 color0, rgb24 color33, rgb24 color66, rgb24 color100);
	DMDDEV void Set_16_Colors_Palette(rgb24 *color);
	DMDDEV void PM_GameSettings(const char* GameName, UINT64 HardwareGeneration, const tPMoptions &Options);
	DMDDEV void Render_4_Shades(UINT16 width, UINT16 height, UINT8 *currbuffer);
	DMDDEV void Render_16_Shades(UINT16 width, UINT16 height, UINT8 *currbuffer);
	DMDDEV void Render_RGB24(UINT16 width, UINT16 height, rgb24 *currbuffer);
	DMDDEV void Console_Data(UINT8 data);
}

// DLL name
#ifdef _WIN64
#define DMD_DLL_FILE _T("DmdDevice64.dll")
#else
#define DMD_DLL_FILE _T("DmdDevice.dll")
#endif

using namespace DMDDevice;

// Run-time DMD DLL imports
//
// We call all DLL entrypoints through function pointers that we
// bind at run-time when we first load the DLL.  To set up an
// import, add the following two lines of code:
//
// - a DMDDEV_ENTRYPOINT() line in the list immediately below
//
// - a DMDDEV_BIND() line in the similar list a little later
// 
// When calling these functions, call the Xxx_() version
// instead of the static extern version defined above.

// Define static pointers to the DMD DLL entrypoints we use
#define DMDDEV_ENTRYPOINT(func) static decltype(DMDDevice::func) *func##_;
DMDDEV_ENTRYPOINT(Open)
DMDDEV_ENTRYPOINT(Close)
DMDDEV_ENTRYPOINT(PM_GameSettings)
DMDDEV_ENTRYPOINT(Render_4_Shades)
DMDDEV_ENTRYPOINT(Render_16_Shades)
DMDDEV_ENTRYPOINT(Render_RGB24)


// -----------------------------------------------------------------------
//
// configuration variables
//
namespace ConfigVars
{
	static const TCHAR *MirrorHorz = _T("RealDMD.MirrorHorz");
	static const TCHAR *MirrorVert = _T("RealDMD.MirrorVert");
}

// -----------------------------------------------------------------------
// 
// Real DMD implementation
//

// statics
RealDMD *RealDMD::inst = nullptr;
bool RealDMD::dllLoaded = false;
HMODULE RealDMD::hmodDll = NULL;

// native device size
static const int dmdWidth = 128, dmdHeight = 32;

RealDMD::RealDMD() :
	curGame(nullptr),
	mirrorHorz(false),
	mirrorVert(false),
	slideShowTimerID(0),
	slideShowPos(slideShow.end())
{
	// if there's no singleton instance yet, we're it
	if (inst == nullptr)
		inst = this;

	// create the writer thread event
	hWriterEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	// create an empty slide
	size_t emptyBufSize = dmdWidth * dmdHeight;
	std::unique_ptr<BYTE> emptyBuf(new BYTE[emptyBufSize]);
	ZeroMemory(emptyBuf.get(), emptyBufSize);
	emptySlide.Attach(new Slide(DMD_COLOR_MONO16, emptyBuf.release(), 0, Slide::EmptySlide));
}

RealDMD::~RealDMD()
{
	// shut down the DLL subsystem
	Shutdown();

	// if I'm the singleton instance, clear the pointer
	if (inst == this)
		inst = nullptr;
}

// Find the DLL
bool RealDMD::FindDLL()
{
	// if we've already found the DLL, we're set
	if (dllPath.length() != 0)
		return true;

	// find the library path
	TCHAR buf[MAX_PATH] = { 0 };

	// The DMD DLL should be in the same folder as the VPinMAME DLL.
	// We can find that from its COM InProcServer registration under 
	// its CLSID GUID, {F389C8B7-144F-4C63-A2E3-246D168F9D39}.  Start
	// by querying the length of the value.
	LONG valLen = 0;
	const TCHAR *vpmKey = _T("CLSID\\{F389C8B7-144F-4C63-A2E3-246D168F9D39}\\InProcServer32");
	if (RegQueryValue(HKEY_CLASSES_ROOT, vpmKey, NULL, &valLen) == ERROR_SUCCESS)
	{
		// allocate space and query the value
		++valLen;
		std::unique_ptr<TCHAR> val(new TCHAR[valLen]);
		if (RegQueryValue(HKEY_CLASSES_ROOT, vpmKey, val.get(), &valLen) == ERROR_SUCCESS)
		{
			// got it - remove the DLL file spec to get its path
			PathRemoveFileSpec(val.get());

			// combine the path and the DLL name
			PathCombine(buf, val.get(), DMD_DLL_FILE);

			// if the file exists, use this path
			if (FileExists(buf)) 
			{
				dllPath = buf;
				return true;
			}
		}
	}

	// We didn't find the DLL via the VPinMAME COM object registration.
	// Try loading it from our own program folder instead.  Get our .EXE
	// full path, and replace the file spec with the DLL name.
	GetModuleFileName(G_hInstance, buf, countof(dllPath));
	PathRemoveFileSpec(buf);
	PathAppend(buf, DMD_DLL_FILE);

	// Use this path if it exists
	if (FileExists(buf))
	{
		dllPath = buf;
		return true;
	}

	// No DLL found
	return false;
}

// Initialize
bool RealDMD::Init(ErrorHandler &eh)
{
	// try loading the DLL, if we haven't already done so
	if (!LoadDLL(eh))
		return false;

	// open the DLL session
	Open_();

	// load the mirroring status from the config
	auto cfg = ConfigManager::GetInstance();
	mirrorHorz = cfg->GetBool(ConfigVars::MirrorHorz, false);
	mirrorVert = cfg->GetBool(ConfigVars::MirrorVert, false);

	// launch the writer thread
	DWORD tid;
	writerThreadQuit = false;
	hWriterThread = CreateThread(NULL, 0, &SWriterThreadMain, this, 0, &tid);

	// success
	return true;
}

// Load the DLL
bool RealDMD::LoadDLL(ErrorHandler &eh)
{
	// do nothing if we've already loaded the DLL
	if (dllLoaded)
		return hmodDll != NULL;

	// we've now made the attempt, even if it fails
	dllLoaded = true;

	// check to see if the path exists
	if (!FindDLL())
	{
		eh.Error(LoadStringT(IDS_ERR_DMDNODLL));
		return false;
	}

	// internal function to log a system error and return false
	auto Failure = [&eh](const TCHAR *desc)
	{
		// log the error
		WindowsErrorMessage winErr;
		eh.SysError(LoadStringT(IDS_ERR_DMDSYSERR),
			MsgFmt(_T("%s: Windows error %d, %s"), desc, winErr.GetCode(), winErr.Get()));

		// forget the DLL handle
		hmodDll = NULL;

		// return failure
		return false;
	};

	// Now load the DLL.  Tell the loader to include the DLL's own folder
	// (along with the normal locations) when searching for additional
	// dependencies the DLL itself imports.
	hmodDll = LoadLibraryEx(dllPath.c_str(), NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (hmodDll == NULL)
		return Failure(MsgFmt(_T("Unable to load %s"), dllPath.c_str()));

	// Bind the entrypoints we access
#define DMDDEV_BIND(func, required) \
    if ((func##_ = reinterpret_cast<decltype(DMDDevice::func)*>(GetProcAddress(hmodDll, #func))) == nullptr && (required)) \
		return Failure(_T("Unable to bind dmddevice.dll function ") _T(#func) _T("()"));

	DMDDEV_BIND(Open, true)
	DMDDEV_BIND(Close, true)
	DMDDEV_BIND(PM_GameSettings, true)
	DMDDEV_BIND(Render_4_Shades, true)
	DMDDEV_BIND(Render_16_Shades, true)
	DMDDEV_BIND(Render_RGB24, false)

	// success
	return true;
}

void RealDMD::Shutdown()
{
	// shut down any playing video
	if (videoPlayer != nullptr)
	{
		videoPlayer->Stop(SilentErrorHandler());
		videoPlayer = nullptr;
	}

	// shut down the writer thread
	writerThreadQuit = true;
	SetEvent(hWriterEvent);
	WaitForSingleObject(hWriterThread, 250);

	// close the underlying device
	if (hmodDll != NULL)
		Close_();
}

void RealDMD::SetMirrorHorz(bool f)
{
	if (mirrorHorz != f)
	{
		// set the new status, and save it in the config
		mirrorHorz = f;
		ConfigManager::GetInstance()->SetBool(ConfigVars::MirrorHorz, f);

		// reload media to make sure we update the display
		ReloadGame();
	}
}

void RealDMD::SetMirrorVert(bool f)
{
	if (mirrorVert != f)
	{
		// set the new status, and save it in the config
		mirrorVert = f;
		ConfigManager::GetInstance()->SetBool(ConfigVars::MirrorVert, f);

		// reload media to make sure we update the display
		ReloadGame();
	}
}

void RealDMD::ReloadGame()
{
	curGame = nullptr;
	UpdateGame();
}

void RealDMD::ClearMedia()
{
	// discard any playing video
	if (videoPlayer != nullptr)
	{
		videoPlayer->Stop(SilentErrorHandler());
		videoPlayer = nullptr;
	}

	// clear out the slide show
	slideShow.clear();
	slideShowPos = slideShow.end();

	// there's now no game loaded
	curGame = nullptr;

	// kill any slide show timer
	if (slideShowTimerID != 0)
	{
		KillTimer(NULL, slideShowTimerID);
		slideShowTimerID = 0;
	}

	// send an empty frame to the display
	SendWriterFrame(emptySlide);
}

void RealDMD::UpdateGame()
{
	// do nothing if the DLL isn't loaded
	if (hmodDll == NULL)
		return;
		
	// update our game if the selection in the game list has changed
	if (auto game = GameList::Get()->GetNthGame(0); game != curGame)
	{
		// discard any previous media
		ClearMedia();

		// remember the new selection
		curGame = game;

		// if there's a new game, load its media
		if (game != nullptr)
		{
			// If the game has any saved VPM config settings, load the
			// DMD-related settings from the VPM config and send them
			// to the device.  This helps ensure that the device looks
			// the same as it would when actually playing this game;
			// e.g., this should restore the color scheme for an RGB
			// device.
			TSTRING rom;
			HKEYHolder hkey;
			bool keyOk = false;
			if (VPinMAMEIfc::FindRom(rom, game))
			{
				// open the registry key for the game
				MsgFmt romkey(_T("%s\\%s"), VPinMAMEIfc::configKey, rom.c_str());
				keyOk = (RegOpenKey(HKEY_CURRENT_USER, romkey, &hkey) == ERROR_SUCCESS);
			}

			// if we didn't get a key that way, try the VPM "default"
			// key, which contains default settings for new tables
			if (!keyOk)
			{
				MsgFmt dfltkey(_T("%s\\default"), VPinMAMEIfc::configKey);
				keyOk = (RegOpenKey(HKEY_CURRENT_USER, dfltkey, &hkey) == ERROR_SUCCESS);
			}

			// set up the default device settings, in case we didn't get
			// a key at all, or for any missing values in the registry
			tPMoptions opts = {
				255, 88, 32,		// monochrome color at 100% - R, G, B
				67, 33, 20,			// monochrome brightness levels 66%, 33%, 0%
				1, 0, 50,			// DMD only, compact mode, antialias
				0,					// colorize mode
				225, 15, 193,		// colorized level 2 (66%) - R, G, B
				6, 0, 214,			// colorized level 1 (33%) - R, G, B
				0, 0, 0				// colorized level 0 (0%) - R, G, B
			};

			// if we got the key, load the registry values
			if (keyOk)
			{
				auto queryf = [&hkey](const TCHAR *valName, int *pval)
				{
					DWORD val, typ, siz = sizeof(val);
					if (RegQueryValueEx(hkey, valName, NULL, &typ, (BYTE*)&val, &siz) == ERROR_SUCCESS
						&& typ == REG_DWORD)
						*pval = val;
				};
				#define Query(item) queryf(_T("dmd_") _T(#item), &opts.dmd_##item)

				// Load the basic values.  Note that we disable "colorize" mode 
				// regardless of the media type, so there's no need to read any of 
				// the values associated with colorization.  Colorization is purely
				// for VPM's use in generating graphics from live ROM output.  The
				// colorization scheme doesn't work well with captured video because
				// it's keyed to the four-level grayscale quantization used in the
				// original pinball hardware.  Captured video can't accurately
				// reproduce that quantization, so colorizing it would produce
				// terrible results most of the time.  It's much better to apply
				// the colorization when capturing the video in the first place,
				// and capture it with the desired RGB colors; then we can simply
				// play it back with the captured colors.  And of course this whole
				// topic is moot for monochrome DMDs.
				Query(red);
				Query(green);
				Query(blue);
				Query(perc66);
				Query(perc33);
				Query(perc0);
				Query(only);
				Query(compact);
				Query(antialias);
			}

			// Send the settings to the device.  Note that the generation
			// is chosen for the way we're going to use the device, not for
			// how the associated game would use it, since the game isn't
			// actually sending the device data - we are.  For our purposes,
			// the WPC95 generation is suitable.  (Note that if we didn't
			// load any registry settings, we'll still have valid defaults
			// to send from initializing the struct earlier.)
			const UINT64 GEN_WPC95 = 0x0000000000080LL;
			PM_GameSettings_(TSTRINGToCSTRING(rom).c_str(), GEN_WPC95, opts);

			// remember the base color option
			baseColor = RGB(opts.dmd_red, opts.dmd_green, opts.dmd_blue);

			// Load media.  Look for, in order, a real DMD color video, 
			// real DMD monochrome video, real DMD color image, real DMD
			// monochrome image, simulated DMD video, simulated DMD image.
			//
			// For each media type, figure the appropriate color space to
			// use for rendering.  We have to consider both the color space
			// of the source material and the capabilities of the physical
			// display device to generate the right mapping.
			//
			// The source material is all in ordinary computer video and 
			// graphics formats, so the source media is in full color at
			// the format level.  However, the actual graphics in the files
			// can be in full color or in black-and-white grayscale.  We
			// can't tell which is which from the format, but we follow
			// the PinballX convention of determining this by folder
			// location:
			//
			//  "Real DMD Image/Video" -> monochrome source
			//  "Real DMD Color Image/Video" -> RGB source
			//  "DMD Image/Video" (i.e., for the simulated DMD window) -> RGB source
			//
			// The device's capabilities can be determined from the DLL's
			// exports.  If the DLL exports the Render_RGB_24() entrypoint,
			// the device is capable of full-color images; if not, it's
			// only capable of monochrome images.  All of the monochrome
			// devices (as far as I can see) support 2-bit (4-shade) and
			// 4-bit (16-shade) grayscale.
			//
			// So combining the source type and device capabilities, we 
			// determine the rendering type:
			//
			//   Monochrome device + Monochrome source -> 16-shade grayscale rendering
			//   Monochrome device + RGB source -> 16-shade grayscale rendering
			//   RGB device + Monochrome source -> 16-shade grayscale rendering
			//   RGB device + RGB source -> RGB rendering
			//
			TSTRING image, video;
			ColorSpace imageColorSpace = DMD_COLOR_MONO16;
			if (game->GetMediaItem(video, GameListItem::realDMDColorVideoType))
			{
				// Real DMD color video - use RGB rendering if the device supports it
				videoColorSpace = Render_RGB24_ != nullptr ? DMD_COLOR_RGB : DMD_COLOR_MONO16;
			}
			else if (game->GetMediaItem(video, GameListItem::realDMDVideoType))
			{
				// Real DMD monochrome video - use monochrome rendering
				videoColorSpace = DMD_COLOR_MONO16;
			}
			else if (game->GetMediaItem(image, GameListItem::realDMDColorImageType))
			{
				// Real DMD color image - use RGB mode if the device supports it
				imageColorSpace = Render_RGB24_ != nullptr ? DMD_COLOR_RGB : DMD_COLOR_MONO16;
			}
			else if (game->GetMediaItem(image, GameListItem::realDMDImageType))
			{
				// Real DMD monochrome image - use monochrome rendering
				imageColorSpace = DMD_COLOR_MONO16;
			}
			else if (game->GetMediaItem(video, GameListItem::dmdVideoType)
				|| game->GetMediaItem(image, GameListItem::dmdImageType))
			{
				// We have a video or image for the simulated (video screen) DMD.
				// These are in full color, since they're intended for regular
				// video display, so render in RGB if the device supports it.
				videoColorSpace = Render_RGB24_ != nullptr ? DMD_COLOR_RGB : DMD_COLOR_MONO16;
			}
			else
			{
				// we couldn't find any media for this game - use the default 
				// real DMD image
				TCHAR path[MAX_PATH];
				GetDeployedFilePath(path, _T("assets\\DefaultRealDMD.png"), _T(""));
				image = path;

				// the default image is a monochrome source
				imageColorSpace = DMD_COLOR_MONO16;
			}

			// Try loading the video first, if we found one
			bool ok = false;
			if (video.length() != 0)
			{
				// create a new video player
				auto pfv = Application::Get()->GetPlayfieldView();
				auto hwndPfv = pfv != nullptr ? pfv->GetHWnd() : NULL;
				videoPlayer.Attach(new VLCAudioVideoPlayer(hwndPfv, hwndPfv, false));

				// Try loading the video.  Always play DMD videos muted.
				Application::InUiErrorHandler uieh;
				videoPlayer->Mute(true);
				videoPlayer->SetLooping(true);
				ok = videoPlayer->OpenDmdTarget(video.c_str(), uieh, this) && videoPlayer->Play(uieh);

				// if that failed, forget the video player
				if (!ok)
					videoPlayer = nullptr;
			}

			// If we didn't manage to load a video, try loading the image
			if (!ok && image.length() != 0)
			{
				// load the image
				std::unique_ptr<Gdiplus::Bitmap> bmp(Gdiplus::Bitmap::FromFile(image.c_str()));
				if (bmp != nullptr)
				{
					// If it's not already at 128x32, rescale it.  The PinDMD drivers
					// are pretty inflexible about the size.  We also need to apply
					// transforms for mirroring, if those are enabled.
					UINT cx = bmp->GetWidth(), cy = bmp->GetHeight();
					if (cx != dmdWidth || cy != dmdHeight || mirrorVert || mirrorHorz)
					{
						// create a 128x32 bitmap to hold the rescaled image
						std::unique_ptr<Gdiplus::Bitmap> bmp2(new Gdiplus::Bitmap(dmdWidth, dmdHeight));

						// set up a GDI+ context on the bitmap
						Gdiplus::Graphics g2(bmp2.get());

						// appply the scaling transform if needed
						if (cx != dmdWidth || cy != dmdHeight)
							g2.ScaleTransform(float(dmdWidth)/cx, float(dmdHeight)/cy);

						// set up mirror transforms as needed
						if (mirrorHorz)
						{
							Gdiplus::Matrix hz(-1, 0, 0, 1, (float)dmdWidth, 0);
							g2.MultiplyTransform(&hz);
						}
						if (mirrorVert)
						{
							Gdiplus::Matrix vt(1, 0, 0, -1, 0, (float)dmdHeight);
							g2.MultiplyTransform(&vt);
						}

						// draw it with the selected transforms
						g2.DrawImage(bmp.get(), 0, 0);

						// replace the original image with the new image
						bmp.reset(bmp2.release());

						// flush the GDI+ context
						g2.Flush();
					}

					// Set up a pixel descriptor to fetch the bits in 24-bit RGB mode,
					// with packed rows (that is, the stride is exactly the pixel width
					// of a row, at 24 bits == 3 bytes per pixel).
					Gdiplus::BitmapData bits;
					bits.Height = bmp->GetHeight();
					bits.Width = bmp->GetWidth();
					bits.PixelFormat = PixelFormat24bppRGB;
					bits.Reserved = 0;
					bits.Stride = bits.Width * 3;

					// set up our 24bpp pixel buffer
					std::unique_ptr<BYTE> buf(new BYTE[bits.Height * bits.Width * 3]);
					bits.Scan0 = buf.get();

					// lock the bits
					bmp->LockBits(nullptr,
						Gdiplus::ImageLockMode::ImageLockModeRead | Gdiplus::ImageLockMode::ImageLockModeUserInputBuf, 
						PixelFormat24bppRGB, &bits);

					// figure out which rendering mode we're using
					DWORD imageDisplayTime = 7000;
					switch (imageColorSpace)
					{
					case DMD_COLOR_MONO16:
						// 16-color grayscale mode.  Reformat the pixels into 4-bit
						// grayscale, with one pixel per byte.
						{
							// create the buffer
							const int dmdBytes = dmdWidth * dmdHeight;
							std::unique_ptr<UINT8> gray(new UINT8[dmdBytes]);

							// copy bits
							const BYTE *src = buf.get();
							UINT8 *dst = gray.get();
							for (int i = 0; i < dmdBytes; ++i, src += 3, ++dst)
							{
								// Figure luma = 0.3R + 0.59G + 0.11B.
								//
								// This calculation uses integer arithmetic, using the machine
								// int as a fixed-point type with 16 bits after the decimal
								// point.  The nominal int values are thus all multiplied by
								// 65536.  The inputs (R, G, B from the image) are all 8-bit
								// ints, so there's no chance of overflow in the fixed-point
								// representation, and the final sum will be on a 0..255 scale,
								// in our fixed-point format.  To convert the fixed-point luma
								// result back to a regular int, shift right 16 bits.  But we
								// then want to further convert that to a 4-bit value, which
								// is a simple matter of shifting right by another 4 bits. 
								// So that gives us a total final shift of 20 bits.
								*dst = (UINT8)((src[0]*19660 + src[1]*38666 + src[2]*7209) >> 20);
							}

							// add it to the slide show, and start playback
							slideShow.emplace_back(new Slide(imageColorSpace, gray.release(), 
								imageDisplayTime, Slide::MediaSlide));
							StartSlideShow();
						}
						break;

					case DMD_COLOR_RGB:
						// RGB mode - we have the bits in exactly the right format.
						// Add the 24bpp buffer to the slide show.
						slideShow.emplace_back(new Slide(imageColorSpace, buf.release(), 
							imageDisplayTime, Slide::MediaSlide));
						StartSlideShow();
						break;
					}

					// unlock the bits
					bmp->UnlockBits(&bits);
				}
			}

			// generate high-score graphics
			GenerateHighScoreGraphics();
		}
	}
}

void RealDMD::StartSlideShow()
{
	// if we don't have any slides, there's nothing to do
	if (slideShow.size() == 0)
		return;

	// start at the first slide
	slideShowPos = slideShow.begin();

	// render the first slide
	RenderSlide();

	// set a timer to advance to the next slide
	slideShowTimerID = SetTimer(NULL, 0, (*slideShowPos)->displayTime, SlideTimerProc);
}

VOID CALLBACK RealDMD::SlideTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	// this is a one-shot timer, so remove it
	KillTimer(hwnd, idEvent);

	// The windowless form of WM_TIMER doesn't have any way to provide
	// a context to the callback, so we can't tell from anything in the
	// message which instance set the timer.  Fortunately, there should
	// only be one global singleton instance, so this question isn't
	// shrouded in such impenetrable mystery after all...
	if (inst != nullptr)
	{
		// we just killed the timer, so forget our record of its ID
		inst->slideShowTimerID = 0;

		// advance to the next slide in the slide show
		inst->NextSlide();
	}
}

void RealDMD::NextSlide()
{
	if (slideShow.size() != 0)
	{
		// if we're not already at the end of the list, advance to
		// the next slide
		if (slideShowPos != slideShow.end())
			++slideShowPos;

		// If we've finished with the last slide, loop
		if (slideShowPos == slideShow.end())
		{
			// If there's a video, restart video playback, so that
			// we alternate between the video and the slide show.
			if (videoPlayer != nullptr)
			{
				videoPlayer->Replay(SilentErrorHandler());
				return;
			}

			// there's no video - start over at the first slide
			slideShowPos = slideShow.begin();
		}

		// show the current slide
		RenderSlide();

		// set a timer to advance to the next slide
		slideShowTimerID = SetTimer(NULL, 0, (*slideShowPos)->displayTime, SlideTimerProc);
	}
}

void RealDMD::RenderSlide()
{
	// render the current slide
	if (slideShow.size() != 0 && slideShowPos != slideShow.end())
		SendWriterFrame(*slideShowPos);
}

void RealDMD::SendWriterFrame(Slide *slide)
{
	// Set the writer frame to the slide.  Note that this will add a
	// reference to the slide, so it'll survive even if we clear media
	// out of the slide show list before the write thread gets around
	// to displaying it.
	CriticalSectionLocker locker(writeFrameLock);
	writerFrame = slide;

	// wake up the writer thread
	SetEvent(hWriterEvent);
}

DWORD RealDMD::WriterThreadMain()
{
	// keep going until the 'quit' event is signaled
	for (;;)
	{
		// wait for an event
		switch (WaitForSingleObject(hWriterEvent, INFINITE))
		{
		case WAIT_OBJECT_0:
		case WAIT_ABANDONED:
			break;

		default:
			// wait error - abandon the thread
			return 0;
		}

		// if 'quit' is signaled, we're done
		if (writerThreadQuit)
			break;

		// send frames to the device
		for (;;)
		{
			// Pull the latest frame off the queue
			RefPtr<Slide> frame;
			{
				// lock the queue
				CriticalSectionLocker locker(writeFrameLock);

				// if there's no frame, there's nothing to do
				if (writerFrame == nullptr)
					break;

				// grab the pending frame, taking over its reference count
				frame.Attach(writerFrame.Detach());
			}

			// send the frame to the device
			switch (frame->colorSpace)
			{
			case DMD_COLOR_MONO4:
				Render_4_Shades_(dmdWidth, dmdHeight, frame->pix.get());
				break;

			case DMD_COLOR_MONO16:
				Render_16_Shades_(dmdWidth, dmdHeight, frame->pix.get());
				break;

			case DMD_COLOR_RGB:
				Render_RGB24_(dmdWidth, dmdHeight, reinterpret_cast<DMDDevice::rgb24*>(frame->pix.get()));
				break;
			}
		}
	}

	// done (thread return value isn't used)
	return 0;
}

void RealDMD::OnUpdateHighScores(GameListItem *game)
{
	// if this is the current game, rebuild our high score graphics
	if (game == curGame)
		GenerateHighScoreGraphics();
}

void RealDMD::GenerateHighScoreGraphics()
{
	// remove any existing high-score slides
	for (auto it = slideShow.begin(); it != slideShow.end(); )
	{
		// Remember this slide, and advance to the next one.  Do this
		// first in case we delete the current slide (and thus invalidate
		// the current iterator position).
		auto cur = it;
		++it;

		// if it's a high score slide, delete it
		if ((*cur)->slideType == Slide::HighScoreSlide)
			slideShow.erase(cur);
	}

	// if we have a game, and it has high scores, generate the graphics
	if (curGame != nullptr && curGame->highScores.size() != 0)
	{
		// Check the game's high score style setting.  If it's "none",
		// suppress the generated graphics entirely.  Note that we
		// otherwise ignore the style, since everything on a DMD will
		// end up looking like a DMD anyway.
		const TCHAR *style = GameList::Get()->GetHighScoreStyle(curGame);
		if (style != nullptr && _tcsicmp(style, _T("none")) == 0)
			return;

		// generate the graphics for each high score text group
		curGame->DispHighScoreGroups([this](const std::list<const TSTRING*> &group)
		{
			// allocate a buffer for the image and clear it to all "off" pixels
			const int dmdBytes = dmdWidth * dmdHeight;
			std::unique_ptr<BYTE> pix(new BYTE[dmdBytes]);
			memset(pix.get(), 0, dmdBytes);

			// Pick a font, using the algorithm from the DMDView window.  This
			// selects the largest DMD font that will fit the message into the
			// 128x32 space.
			const DMDFont *font = DMDView::PickHighScoreFont(group);

			// figure the starting y offset, centering the text vertically
			int nLines = (int)group.size();
			int totalTextHeight = font->cellHeight * nLines;
			int y = (dmdHeight - totalTextHeight)/2;

			// draw each line
			for (auto it = group.begin(); it != group.end(); ++it)
			{
				// measure the string
				const TCHAR *str = (*it)->c_str();
				SIZE sz = font->MeasureString(str);

				// draw it centered horizontally
				font->DrawString4(str, pix.get(), (dmdWidth - sz.cx)/2, y);

				// advance to the next line
				y += font->cellHeight;
			}

			// if necessary, mirror and/or flip the display
			if (mirrorHorz || mirrorVert)
			{
				// create a new buffer for the updated image
				std::unique_ptr<BYTE> newpix(new BYTE[dmdBytes]);

				// Set up source pointers.  To make a simple copy, we'd
				// start at the first row and column.  If we're flipping
				// vertically, we start at the left end of the last row
				// and work from bottom to top.  If we're mirroring
				// horizontally, start at the right end of whichever row 
				// we decided goes first (based on the vertical flip), 
				// and work from right to left.
				const BYTE *rowp = pix.get();
				int rowInc = dmdWidth;
				int colInc = 1;
				if (mirrorVert)
					rowp += dmdWidth * (dmdHeight - 1), rowInc = -dmdWidth;
				if (mirrorHorz)
					rowp += dmdWidth - 1, colInc = -1;

				// copy rows
				BYTE *dst = newpix.get();
				for (int row = 0; row < dmdHeight; ++row, rowp += rowInc)
				{
					const BYTE *src = rowp;
					for (int col = 0; col < dmdWidth; ++col, src += colInc)
						*dst++ = *src;
				}

				// replace the original buffer with the new buffer
				pix.reset(newpix.release());
			}

			// add this screen to our list, transferring ownership of the pixel
			// buffer to the list
			slideShow.emplace_back(new Slide(DMD_COLOR_MONO16, pix.release(), 3500, Slide::HighScoreSlide));
		});
	}

	// reset the slide show pointer
	slideShowPos = slideShow.end();
}

bool RealDMD::SupportsRGBDisplay() const
{
	return Render_RGB24_ != nullptr;
}

// Our decoder will always call us with one of the following
// frame sizes:
//
// 256x64:  If the source video's frame size is 256x64, the
// decoder will decode at that size and pass the frames to us
// at that size.  Videos in this format are assumed to use a
// pixel structure where every 2x2 block contains exactly one
// DMD pixel.  The other three pixels in each 2x2 block are
// expected to be black (zero brightness), so that the video
// reproduces the DMD pixel structure visually when played
// back on a regular video display.  For DMD playback, the
// black pixels correspond to the space between pixels in
// the physical DMD, so we don't want to display them at all.
// To play back this format, we examine each 2x2 block and
// pick out the brightest pixel, ignoring the rest.
//
// 128x32:  If the source video frame size isn't one of the
// special cases listed above, the decoder scales the frame
// to 128x32 and passes us the 128x32 buffer.  This is the
// same size as the native DMD, so we simply map the frame
// pixels to DMD pixels one-to-one.
//
void RealDMD::PresentVideoFrame(int width, int height, const BYTE *y, const BYTE *u, const BYTE *v)
{
	// Figure the output buffer pointers according to the mirroring
	// settings.
	int dstStartRow = 0, dstStartCol = 0, dstRowInc = 1, dstColInc = 1;
	if (mirrorVert)
		dstStartRow = 31, dstRowInc = -1;
	if (mirrorHorz)
		dstStartCol = 127, dstColInc = -1;

	// prepare the buffer according to the device color space we're
	// rendering to
	switch (videoColorSpace)
	{
	case DMD_COLOR_MONO16:
		// 16-bit monochrome mode.  The Y plane is conveniently in
		// 8-bit luma format, at the native device size of 128x32, so
		// all we have to do is shift all of the pixel luma values 
		// right by four bits to get 4-bit luma.  We can ignore the U 
		// and V planes in this mode.
		if (width == 256 && height == 64)
		{
			// Double-size frame.  These should follow the convention
			// where each DMD pixel is stored as a 2x2 block of video
			// pixels, so that the video has the same visible pixel
			// structure as a DMD when played back on a video device.
			// Look for the maximum pixel value in each block, and
			// render that in 16-shade grayscale.
			UINT8 gray[dmdWidth * dmdHeight];
			UINT8 *dst;
			for (int row = 0; row < dmdHeight; ++row, y += dmdWidth*2)
			{
				dst = gray + dstStartRow*dmdWidth + dstStartCol;
				dstStartRow += dstRowInc;
				for (int col = 0; col < dmdWidth; ++col, y += 2)
				{
					// get the 2x2 pixel block at this position
					BYTE a = y[0], b = y[1], c = y[dmdWidth*2], d = y[dmdWidth*2 + 1];

					// take the maximum of these values
					if (b > a) a = b;
					if (c > a) a = c;
					if (d > a) a = d;

					// downconvert from 8 bits to 4 bits, clamp to 0..15, and store it
					a >>= 4;
					*dst = min(a, 15);
					dst += dstColInc;
				}
			}

			// display it
			Render_16_Shades_(dmdWidth, dmdHeight, gray);
		}
		else if (width == dmdWidth && height == dmdHeight)
		{
			// native size frame - convert from 8-bit luma to 4-bit luma
			UINT8 gray[dmdWidth * dmdHeight];
			UINT8 *dst;
			for (int row = 0; row < dmdHeight; ++row)
			{
				dst = gray + dstStartRow*dmdWidth + dstStartCol;
				dstStartRow += dstRowInc;
				for (int col = 0; col < dmdWidth; ++col, y += 2)
				{
					BYTE b = (*y++) >> 4;
					*dst = min(b, 15);
					dst += dstColInc;
				}
			}

			// display it
			Render_16_Shades_(dmdWidth, dmdHeight, gray);
		}
		break;

	case DMD_COLOR_RGB:
		if (width == dmdWidth*2 && height == dmdHeight*2)
		{
			// Double-size frame.  Pick out the brightest pixel from
			// each 2x2 block.
			rgb24 rgb[dmdWidth * dmdHeight];
			rgb24 *dst;
			for (int row = 0; row < dmdHeight; ++row, y += dmdWidth*2)
			{
				dst = rgb + dstStartRow*dmdWidth + dstStartCol;
				dstStartRow += dstRowInc;
				for (int col = 0; col < dmdWidth; ++col, y += 2, ++u, ++v)
				{
					// get the 2x2 pixel block at this position
					BYTE a = y[0], b = y[1], c = y[dmdWidth*2], d = y[dmdWidth*2 + 1];
					int ofs = 0;

					// take the maximum of these values
					if (b > a) a = b;
					if (c > a) a = c;
					if (d > a) a = d;

					// By some amazing coincidence, the U and V planes are 
					// already subsampled in 2x2 blocks, so whichever pixel
					// we just picked out, the U and V samples are the same.
					// Calculate the RGB value using the standard formula:
					//
					//  Y' = 1.164*(Y-16)
					//  U' = U - 128
					//  V' = V - 128
					//
					//  R = Y' + 1.596*V'
					//  G = Y' - 0.813*V' - 0.391*U'
					//  B = Y' + 2.018*U'
					//
					// For efficiency, do the calculations in base-65536
					// fixed-point representation.
					int yp = (a - 16)*76284;
					int up = (*u - 128);
					int vp = (*v - 128);
					int rr = (yp + 104595*vp) >> 16;
					int gg = (yp - 53281*vp - 25625*up) >> 16;
					int bb = (yp + 132252*up) >> 16;

					// Clamp the results to 0..255
					rr = max(rr, 0);
					gg = max(gg, 0);
					bb = max(bb, 0);
					dst->red = min(rr, 255);
					dst->green = min(gg, 255);
					dst->blue = min(bb, 255);

					dst += dstColInc;
				}
			}

			// display it
			Render_RGB24_(dmdWidth, dmdHeight, rgb);
		}
		else if (width == dmdWidth && height == dmdHeight)
		{
			// native size frame - convert from YUV to RGB
			rgb24 rgb[dmdWidth * dmdHeight];
			rgb24 *dst;
			for (int row = 0; row < dmdHeight; ++row)
			{
				dst = rgb + dstStartRow*dmdWidth + dstStartCol;
				dstStartRow += dstRowInc;
				for (int col = 0; col < dmdWidth; ++col)
				{
					// Get the Y, U and V values for this pixel.  The U and V
					// planes are subsampled in 2x2 blocks, so we need to figure
					// the U/V index accordingly.
					int yy = *y++;
					int uvIdx = (row/2)*dmdWidth/2 + col/2;
					int uu = u[uvIdx];
					int vv = v[uvIdx];

					// do the YUV -> RGB conversion
					int yp = (yy - 16)*76284;
					int up = (uu - 128);
					int vp = (vv - 128);
					int rr = (yp + 104595*vp) >> 16;
					int gg = (yp - 53281*vp - 25625*up) >> 16;
					int bb = (yp + 132252*up) >> 16;

					// clamp the results to 0..255 and store the RGB pixel
					rr = max(rr, 0);
					gg = max(gg, 0);
					bb = max(bb, 0);
					dst->red = min(rr, 255);
					dst->green = min(gg, 255);
					dst->blue = min(bb, 255);

					dst += dstColInc;
				}
			}

			// display it
			Render_RGB24_(dmdWidth, dmdHeight, rgb);
		}
		break;
	}
}

void RealDMD::VideoLoopNeeded(WPARAM cookie)
{
	// if the request is for our current video, restart playback
	if (videoPlayer != nullptr && videoPlayer->GetCookie() == cookie)
	{
		// If we have slides, start the slide show instead of looping the
		// video.  The slide show will replay the video when it finishes
		// with the last slide.  If there's no slide show, just loop the
		// video immediately.
		if (slideShow.size() != 0)
			StartSlideShow();
		else
			videoPlayer->Replay(SilentErrorHandler());
	}
}

