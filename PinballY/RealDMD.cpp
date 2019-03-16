// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "../Utilities/Config.h"
#include "../Utilities/FileUtil.h"
#include "../Utilities/FileVersionInfo.h"
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
#include "LogFile.h"


// library inclusion for GetFileVersionInfo et al
#pragma comment(lib, "version.lib")

// DLL name.  In most cases, we load DmdDevice.dll.  When running
// in 64-bit mode, we'll look for DmdDevice64.dll first, and load
// that instead if present, falling back on DmdDevice.dll if that
// doesn't exist.  That allows installing the 32-bit and 64-bit
// versions of the DLL alongside one another for compatibility
// with a mix of 32- and 64-bit clients.
#ifdef _M_X64
#define DMD_DLL64_FILE _T("DmdDevice64.dll")
#endif
#define DMD_DLL_FILE   _T("DmdDevice.dll")

// define the externs for the dmddevice.dll imports
#define DMDDEVICEDLL_DEFINE_EXTERNS
#include "DMDDeviceDll.h"
using namespace DMDDevice;

// -----------------------------------------------------------------------
//
// configuration variables
//
namespace ConfigVars
{
	static const TCHAR *MirrorHorz = _T("RealDMD.MirrorHorz");
	static const TCHAR *MirrorVert = _T("RealDMD.MirrorVert");
	static const TCHAR *Gamma = _T("RealDMD.GrayscaleGamma");
}

// -----------------------------------------------------------------------
// 
// Real DMD implementation
//

// statics
RealDMD *RealDMD::inst = nullptr;
bool RealDMD::dllLoaded = false;
HMODULE RealDMD::hmodDll = NULL;
TSTRING RealDMD::dllPath;
RealDMD::DmdExtInfo RealDMD::dmdExtInfo;
CriticalSection RealDMD::dmdLock;

// default options
const tPMoptions RealDMD::defaultOpts = {
	255, 88, 32,		// monochrome color at 100% - R, G, B
	67, 33, 20,			// monochrome brightness levels 66%, 33%, 0%
	1, 0, 50,			// DMD only, compact mode, antialias
	0,					// colorize mode
	225, 15, 193,		// colorized level 2 (66%) - R, G, B
	6, 0, 214,			// colorized level 1 (33%) - R, G, B
	0, 0, 0				// colorized level 0 (0%) - R, G, B
};

// native device size
static const int dmdWidth = 128, dmdHeight = 32;

RealDMD::RealDMD() :
	curGame(nullptr),
	mirrorHorz(false),
	mirrorVert(false),
	slideShowTimerID(0),
	slideShowPos(slideShow.end()),
	enabled(false)
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

	// initialize gamma from the settings
	UpdateGamma();
}

void RealDMD::UpdateGamma()
{
	// remember the gamma
	float gamma = this->gamma = ConfigManager::GetInstance()->GetFloat(ConfigVars::Gamma, 2.8f);

	// compute the grayscale mapping
	for (int i = 0; i < 256; ++i)
	{
		int l = static_cast<int>(roundf(255.0f * powf(static_cast<float>(i) / 255.0f, gamma)));
		l = max(0, l);
		l = min(l, 255);
		gammaMap[i] = l;
	}
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

	LogGroup();
	Log(_T("Searching for real DMD device DLL\n"));

	// look for the DLL file in a given folder
	auto TryFolder = [](TCHAR *buf /*[MAX_PATH]*/, const TCHAR *folderDesc)
	{
		auto TryFile = [buf, folderDesc](const TCHAR *dllName, const TCHAR *fileDesc)
		{
			// replace the filename in the buffer with the current filename
			PathRemoveFileSpec(buf);
			PathAppend(buf, dllName);

			// check if it exists
			if (FileExists(buf))
			{
				// got it - use this copy
				dllPath = buf;
				Log(_T("+ Found %s in the %s folder: %s\n"), fileDesc, folderDesc, buf);
				return true;
			}

			// not found
			return false;
		};

#ifdef DMD_DLL64_FILE
		// We're in 64-bit mode - try the 64-bit version first
		if (TryFile(DMD_DLL64_FILE, _T("the 64-bit DLL")))
			return true;
#endif

		// Try the normal DmdDevice.dll file
		if (TryFile(DMD_DLL_FILE, _T("the DLL")))
			return true;

		// no luck
		return false;
	};

	// Look for the DLL in our own program folder first.  This allows
	// using a specific version of the DLL with PinballY, without
	// affecting the VPinMAME configuration, simply by copying the
	// desired DLL into the PinballY program folder.
	TCHAR buf[MAX_PATH] = { 0 };
	GetModuleFileName(G_hInstance, buf, countof(buf));
	if (TryFolder(buf, _T("PinballY")))
		return true;

	// Now try the folder containing the VPinMAME COM object DLL.
	// We can find that from its COM InProcServer registration under 
	// its CLSID GUID, {F389C8B7-144F-4C63-A2E3-246D168F9D39}.  Start
	// by querying the length of the value.
	//
	// If we're on a 64-bit Windows system, Windows maintains two 
	// parallel trees of CLSID keys: one under HKCR\Wow3264Node\CLSID 
	// for 32-bit COM objects, and one under HKCR\CLSID for 64-bit 
	// COM objects.  But wait, it gets more complex than that!  If
	// *this process* is running in 32-bit mode on 64-bit Windows,
	// Windows will redirect all registry inquires to HKCR\CLSID to
	// the shadow copy under HKCR\Wow3264Node\CLSID, so that we get
	// the 32-bit COM object pointer automatically.  If the process
	// is in 64-bit mode, it'll gives us the HKCR\CLSID key instead.
	// That means that if we're in 64-bit mode, and 32-bit VPinMAME 
	// is installed, we won't see the VPM key under HKCR\CLSID.  So
	// in this one special case, explicitly try the Wow3264Node
	// shadow copy as a separate query.
	// 
	LONG valLen = 0;
	const TCHAR *vpmKey = nullptr;
	const TCHAR *vpmKey32 = _T("CLSID\\{F389C8B7-144F-4C63-A2E3-246D168F9D39}\\InProcServer32");
	const TCHAR *vpmKey64 = _T("Wow6432Node\\CLSID\\{F389C8B7-144F-4C63-A2E3-246D168F9D39}\\InProcServer32");
	Log(_T("+ No DLL found in the PinballY folder; checking for a VPinMAME folder\n"));
	if (RegQueryValue(HKEY_CLASSES_ROOT, vpmKey = vpmKey32, NULL, &valLen) == ERROR_SUCCESS
		|| RegQueryValue(HKEY_CLASSES_ROOT, vpmKey = vpmKey64, NULL, &valLen) == ERROR_SUCCESS)
	{
		// allocate space and query the value
		++valLen;
		std::unique_ptr<TCHAR> val(new TCHAR[valLen]);
		if (RegQueryValue(HKEY_CLASSES_ROOT, vpmKey, val.get(), &valLen) == ERROR_SUCCESS)
		{
			// got it
			Log(_T("+ VPinMAME COM object registration found at %s\n"), val.get());

			// try this path
			_tcscpy_s(buf, val.get());
			if (TryFolder(buf, _T("VPinMAME")))
				return true;
		}
	}
	else
		Log(_T("+ VPinMAME COM object registration not found in Windows registry\n"));

	// No DLL found
	return false;
}

// Initialize
bool RealDMD::Init(ErrorHandler &eh)
{
	// log the setup process
	LogGroup();
	Log(_T("Detecting and configuring real DMD device\n"));

	// presume that we're disabled
	enabled = false;

	// try loading the DLL, if we haven't already done so
	if (!LoadDLL(eh))
		return false;

	// Check if the DMD should be enabled.  Note that we will
	// have already tested this in LoadDLL(), but we need a
	// separate test here, because we might be re-initializing
	// due to a change in option settings.  The DLL loading only
	// happens once for the whole process run
	if (!ShouldEnable())
		return false;

	// open the DLL session
	OpenSession();

	// load the mirroring status from the config
	auto cfg = ConfigManager::GetInstance();
	mirrorHorz = cfg->GetBool(ConfigVars::MirrorHorz, false);
	mirrorVert = cfg->GetBool(ConfigVars::MirrorVert, false);

	// Send an initial empty frame.  This clears any leftover display
	// cruft, and also forces the virtual DMD window to open if it's 
	// going to open.  The virtual DMD can have side effects on the
	// display when it first opens, so it's good to get those out of
	// the way synchronously before we go on.  Note that we do this
	// before starting the writer thread so that we don't have to
	// worry about locking.
	Render_16_Shades_(dmdWidth, dmdHeight, emptySlide->pix.get());

	// launch the writer thread
	DWORD tid;
	writerThreadQuit = false;
	hWriterThread = CreateThread(NULL, 0, &SWriterThreadMain, this, 0, &tid);

	// success
	enabled = true;
	return true;
}

// Load the DLL
bool RealDMD::LoadDLL(ErrorHandler &eh)
{
	// do nothing if we've already loaded the DLL
	if (dllLoaded)
		return IsDllValid();

	// log it
	Log(_T("+ Searching for dmddevice.dll\n"));

	// we've now made the attempt, even if it fails
	dllLoaded = true;

	// check to see if the path exists
	if (!FindDLL())
	{
		eh.Error(LoadStringT(IDS_ERR_DMDNODLL));
		return false;
	}

	// log that we found the DLL
	Log(_T("+ found DMD interface DLL: %s\n"), dllPath.c_str());

	// internal functions to log a system error and return false
	auto Failure2 = [&eh](const TCHAR *desc, WindowsErrorMessage &winErr)
	{
		Log(_T("+ DMD setup failed: %s: Windows error %d, %s\n"), desc, winErr.GetCode(), winErr.Get());
		eh.SysError(LoadStringT(IDS_ERR_DMDSYSERR),
			MsgFmt(_T("%s: Windows error %d, %s"), desc, winErr.GetCode(), winErr.Get()));

		// forget the DLL handle
		hmodDll = NULL;

		// return failure
		return false;
	};
	auto Failure = [&Failure2](const TCHAR *desc)
	{
		WindowsErrorMessage winErr;
		return Failure2(desc, winErr);
	};

	// Before loading the library, check to see if it's the dmd-extensions
	// version.  That's becoming common because it's included in recent
	// (2018) VP distributions, so we can't rely on our original strategy
	// of assuming that the presence of a DMD DLL implies the presence of
	// a DMD device.  Thanks to the VP distribution, the dmd-ext version 
	// could be installed even if the user doesn't need it, doesn't want
	// it, hasn't enabled it, and doesn't know it's there.  Early PinballY
	// testing revealed this when a mysterious second DMD window started
	// showing up on some people's machines, because dmd-ext's default
	// behavior is to show a fake DMD window.  This doesn't happen by
	// default in basic VP and VPinMAME installations because you have
	// to explicitly configure VPM to look for dmddevice.dll; people who
	// don't need and don't want the fake video DMD thus won't know it's
	// there unless they go looking for it.  Or, in early PBY alphas,
	// until they ran PinballY.
	//
	// To sense the dmd-ext DLL, check the product name and copyright
	// strings in the .dll file's VERSION_INFO resource.  The dmd-ext DLL
	// reports (at least currently) "Universal DmdDevice.dll for Visual 
	// PinMAME" as the product string, and "Copyright © 20xx freezy@vpdb.io"
	// as the copyright string.  To be flexible against future changes,
	// we'll only look for some distinctive fragments of these strings (such
	// as "universal" and "freezy") rather than holding out for an exact 
	// match.
	//
	Log(_T("+ retrieving file version info for DLL, to check for special handling\n"), dllPath.c_str());
	FileVersionInfo vs(dllPath.c_str());
	if (vs.valid)
	{
		// log the strings we read
		Log(_T("+ Version Info data: version=%s, product name=\"%s\", comments=\"%s\", copyright=\"%s\"\n"),
			vs.versionStr.c_str(), vs.productName.c_str(), vs.comments.c_str(), vs.legalCopyright.c_str());

		// Look for "universal" in the product string and "freezy" in the
		// copyright string, insensitive to case.
		if (std::regex_search(vs.productName, std::basic_regex<TCHAR>(_T("\\buniversal\\b"), std::regex_constants::icase))
			|| std::regex_search(vs.legalCopyright, std::basic_regex<TCHAR>(_T("\\bfreezy\\b"), std::regex_constants::icase)))
		{
			// It's the dmd-extensions version of the DLL.  So note.
			Log(_T("+ This appears to be the dmd-extensions version of the DLL, based on the product/copyright strings\n"));
			dmdExtInfo.matched = true;

			// VERSION NOTE:  Freezy's original "1.7.2" release (as listed in
			// the GitHub release list) actually had 1.7.3 version stamps on
			// all of the files.  That apparently got fixed at some point;
			// the 1.7.2 on github has 1.7.2 file stamps.

			// Check the product version to see if it's a newer version
			// with the fix for a bug that caused the DLL to crash if we
			// called PM_GameSettings() more than once per process
			// lifetime.  The fix is pull request #122, which is in
			// official releases 1.7.2 and later.
			if (vs.llVersion >= 0x0001000700020000UL
				|| std::regex_search(vs.comments, std::basic_regex<TCHAR>(_T("\\b[Ii]ncludes fix.*\\s#122\\b"))))
			{
				Log(_T("+ Based on the version number, this version has the fix for the PM_GameSettings bug\n"));
				dmdExtInfo.settingsFix = true;
			}
			else
			{
				Log(_T("+ Based on the version number, this version of the DLL has a bug in PM_GameSettings,\n")
					_T("  so we won't call that function; as a result, per-game coloring from your VPinMAME\n")
					_T("  settings won't be used during this session.\n"));
			}

			// Check the product version to see if it has a fix for the
			// Close/Open bug, which makes the DLL crash if we try to close
			// and reopen it more than once per process lifetime.   This is
			// fixed in pull request #127, which is in official releases
			// 1.7.2 and later.
			if (vs.llVersion >= 0x0001000700020000UL
				|| std::regex_search(vs.comments, std::basic_regex<TCHAR>(_T("\\b[Ii]ncludes fix.*\\s#127\\b"))))
			{
				Log(_T("+ Based on the version number, this version has the fix for the Open/Close bug\n"));
				dmdExtInfo.virtualCloseFix = true;
			}
			else
			{
				Log(_T("+ Based on the version number, this version of the DLL has a bug that crashes\n")
					_T("  the process if we try to close and later reopen the DLL session.  As a result,\n")
					_T("  we'll leave the session open permanently once opened.  This may result in the\n")
					_T("  DLL's virtual DMD window remaining visible even if you explicitly disable the\n")
					_T("  real DMD feature in the options.  Close PinballY and restart it to get rid of\n")
					_T("  the extra window.\n"));
			}
		}
	}
	else
	{
		Log(_T("++ DLL version info not available; we must assume this is an old version with known bugs, so some\n")
			_T("   features will be disabled.  Please update your DmdDevice.dll to a current version.\n\n"));
	}

	// If it's the dmd-extensions DLL, check its .ini file before
	// loading it.  If its "virtual DMD" (that is, its on-screen
	// video emulation of a DMD) is enabled, suppress loading the
	// DLL unless the config setting is explicitly ON (not AUTO).
	// Why?  Because "virtual" means that the DLL provides a fake
	// video DMD, not a real DMD.  We have a perfectly nice fake
	// DMD window of our own, thank you very much, and most users
	// find it confusing and ugly to have two fake DMD windows 
	// appear.  The point of the RealDMD module is Real DMD 
	// support; make sure that's what this DLL is actually for.
	if (dmdExtInfo.matched)
	{
		Log(_T("+ Checking if dmd-extensions virtual DMD mode is enabled\n"));

		// The dmd-ext default setting for the fake DMD is "enabled", 
		// so we have to assume it's disabled unless we find an
		// explicit setting saying otherwise.
		dmdExtInfo.virtualEnabled = true;

		// Try loading its dmddevice.ini file.  The .ini file is at
		// the path given by the DMDDEVICE_CONFIG environment variable
		// if set, otherwise in the same folder as the .dll file.
		TCHAR cfgbuf[MAX_PATH];
		size_t cfglen;
		if (_tgetenv_s(&cfglen, cfgbuf, _T("DMDDEVICE_CONFIG")) == 0 && cfglen != 0 && cfglen <= countof(cfgbuf))
		{
			// successfully retrieved the environment variable
			Log(_T("+ DMDDEVICE_CONFIG environment variable found (%s)\n"), cfgbuf);
		}
		else
		{
			// the environment variable isn't defined - try the DLL path
			_tcscpy_s(cfgbuf, dllPath.c_str());
			PathRemoveFileSpec(cfgbuf);
			PathAppend(cfgbuf, _T("DmdDevice.ini"));
			Log(_T("+ Loading DmdDevice.ini from DLL folder (%s)\n"), cfgbuf);
		}

		// try reading the file
		long len;
		std::unique_ptr<WCHAR> ini(ReadFileAsWStr(cfgbuf, SilentErrorHandler(), len,
			ReadFileAsStr_NewlineTerm | ReadFileAsStr_NullTerm));
		if (ini != nullptr)
		{
			// scan for [virtualdmd] enabled=0
			Log(_T("+ DmdDevice.ini successfully loaded; scanning\n"));
			WSTRING sect;
			for (WCHAR *p = ini.get(); *p != 0; )
			{
				// find the end of the line
				WCHAR *eol;
				for (eol = p; *eol != 0 && *eol != '\n' && *eol != '\r'; ++eol);

				// find the start of the next line
				WCHAR *nextLine;
				if (*eol == 0)
					nextLine = eol;
				else if ((*eol == '\n' && *(eol + 1) == '\r') || (*eol == '\r' && *(eol + 1) == '\n'))
					nextLine = eol + 2;
				else
					nextLine = eol + 1;

				// null-terminate the current line
				*eol = 0;

				// check the pattern
				std::match_results<const WCHAR*> m;
				if (std::regex_match(p, std::basic_regex<WCHAR>(L"\\s*;.*")))
				{
					// comment line - ignore it
				}
				else if (std::regex_match(p, m, std::basic_regex<WCHAR>(L"\\s*\\[\\s*([^\\]]*?)\\s*\\]\\s*")))
				{
					// it's a section marker
					sect = m[1].str();
				}
				else if (std::regex_match(p, m, std::basic_regex<WCHAR>(L"\\s*([^=]*?)\\s*=\\s*(.*?)\\s*")))
				{
					// It's a name=value line.  If we're in the [virtualdmd]
					// section, check for enabled=(true|false); if we find 
					// that, it's disabled if the value is false, otherwise
					// it's enabled.  Note that the dmd-ext config reader
					// uses the default for invalid values, and the default
					// for this one is true, so it has to be explicitly
					// "false" to be disabled.
					if (_wcsicmp(sect.c_str(), L"virtualdmd") == 0
						&& _wcsicmp(m[1].str().c_str(), L"enabled") == 0)
					{
						dmdExtInfo.virtualEnabled = (_wcsicmp(m[2].str().c_str(), L"false") != 0);
					}
				}

				// advance to the next line
				p = nextLine;
			}
		}
		else
		{
			Log(_T("+ DmdDevice.ini not found or load failed; assuming default settings (with virtual dmd enabled)\n"));
		}
	}

	// Test to see if the DMD should be enabled.  If not, don't even
	// load the DLL.  This is important for dmd-extensions in virtual
	// mode, because if we load the DLL at all, it'll display its
	// fake on-screen DMD window, and we don't want that to appear
	// at all if we're just going to disable it.  So we need to
	// short-circuit the whole load process in that case.
	if (!ShouldEnable())
	{
		// Pretend that we didn't even try loading the DLL, so that
		// we can try again on a future pass if conditions change.
		// For example, the user might change the DMD-enable option
		// from AUTO to ALWAYS ON, which would change the Should
		// Enable determination.
		dllLoaded = false;

		// fail silently, as though the DLL isn't even installed
		return false;
	}

	// Load the DLL.  Tell the loader to include the DLL's own folder
	// (along with the normal locations) when searching for additional
	// dependencies the DLL itself imports.
	hmodDll = LoadLibraryEx(dllPath.c_str(), NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (hmodDll == NULL)
	{
		// If we're in 64-bit mode, and the error is BAD EXE FORMAT, we're
		// probably trying to load the 32-bit DLL from 64-bit code.  Provide
		// a specific explanation of how to fix this.
		WindowsErrorMessage winErr;
#ifdef _M_X64
		if (winErr.GetCode() == ERROR_BAD_EXE_FORMAT)
		{
			eh.Error(MsgFmt(IDS_ERR_DMD_3264, dllPath.c_str()));
			Log(_T("+ The DLL (%s)\n")
				_T("  can't be loaded because it appears to be a 32-bit DLL, and this\n")
				_T("  is the 64-bit version of PinballY.  Windows doesn't allow mixing\n")
				_T("  32-bit and 64-bit modules.  You'll have to get a copy of the 64-bit\n")
				_T("  version of DmdDevice.dll.  You don't have to replace your current\n")
				_T("  32-bit version, though!  You can install the 64-bit version alongside\n")
				_T("  it and keep both, which you should do to maintain compatibility with\n")
				_T("  any other programs that you're currently using that require the 32-bit\n")
				_T("  DLL.  To install the 32-bit and 64-bit DLLs side by side: download the\n")
				_T("  64-bit DLL, RENAME IT to DmdDevice64.dll, and copy it into the same\n")
				_T("  folder as your current 32-bit DLL.\n"),
				dllPath.c_str());
			return false;
		}
#endif

		// other error - return a generic failure
		return Failure2(MsgFmt(_T("Unable to load %s"), dllPath.c_str()), winErr);
	}

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
#undef DMDDEV_BIND

	// success
	Log(_T("+ dmddevice.dll successfully loaded\n"));
	return true;
}

bool RealDMD::ShouldEnable()
{
	// If it's the dmd-extensions DLL, and and the virtual DMD is enabled,
	// suppress it if we're in AUTO mode.
	if (dmdExtInfo.matched)
	{
		if (dmdExtInfo.virtualEnabled)
		{
			// Unless the config setting is explicitly ON (not AUTO),
			// don't load the DLL.  
			if (auto cv = ConfigManager::GetInstance()->Get(_T("RealDMD"), nullptr);
			cv != nullptr && (_tcsicmp(cv, _T("on")) == 0 || _tcsicmp(cv, _T("enabled")) == 0 || _ttoi(cv) != 0))
			{
				// it's explicitly on - continue with the loading
				Log(_T("+ It looks like virtual dmd mode is enabled in the dmd-extensions DLL.  Your\n")
					_T("  PinballY real DMD setting is \"Always On\", so we're going to use the DLL\n")
					_T("  anyway.  Note that you'll see two simulated DMDs on the screen - one from\n")
					_T("  the DLL, and another from PinballY's built-in DMD window.  If you want to\n")
					_T("  get rid of the one from the DLL, change its virtual dmd setting to disabled\n")
					_T("  in the DLL's DmdDevice.ini file.\n"));
			}
			else
			{
				// It's AUTO (explicitly or by default).  Suppress loading
				// the DLL.
				Log(_T("+ It looks like virtual dmd mode is enabled in the dmd-extensions DLL.  Your\n")
					_T("  PinballY real DMD setting is \"Auto\", so we're NOT using the real DMD\n")
					_T("  for this session, to avoid showing a second on-screen virtual DMD from\n")
					_T("  the DLL in addition to PinballY's built-in DMD simulation.  If you want\n")
					_T("  to use the DLL anyway, change your PinballY real DMD setting to \"Always On\".\n"));
				return false;
			}
		}
		else
		{
			Log(_T("+ It looks like virtual dmd mode is disabled in the dmd-extensions DLL, so we're enabling the DLL.\n"));
		}
	}

	// no disabling conditions found
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

	// shut down the writer thread, if there is one
	if (hWriterThread != NULL)
	{
		// signal for the thread to shut down
		writerThreadQuit = true;
		SetEvent(hWriterEvent);

		// wait for it to exit
		WaitForSingleObject(hWriterThread, 2500);

		// forget the thread handle
		hWriterThread = nullptr;
	}

	// blank the DMD before we detach from it
	if (enabled && Render_16_Shades_ != NULL && emptySlide != nullptr)
	{
		CriticalSectionLocker dmdLocker(dmdLock);
		Render_16_Shades_(dmdWidth, dmdHeight, emptySlide->pix.get());
	}

	// close the session with the underlying device
	CloseSession();

	// unload the DLL
	FreeLibrary(hmodDll);
	hmodDll = NULL;
	dllLoaded = false;
}

void RealDMD::OpenSession()
{
	// open the DLL session
	if (Open_ != NULL)
	{
		CriticalSectionLocker dmdLocker(dmdLock);
		Open_();
	}

	// Set a dummy ROM initially.  dmd-extensions will crash in some
	// cases if we make other calls before setting a game, since it
	// assumes from the VPM usage pattern that a ROM is always set
	// exactly once per session as the second call.
	SetGameSettings("PinballY", defaultOpts);
}

void RealDMD::SetGameSettings(const char *gameName, const tPMoptions &opts)
{
	if (PM_GameSettings_ != NULL)
	{
		// The dmd-extensions version of the DLL had a bug in versions
		// prior to 1.7.2 that causes the DLL to crash if the DLL is in
		// "virtual dmd" mode AND we call this function after making any 
		// other call to the DLL other than Open().  We take care to 
		// call this immediately after each Open() call we make, so the
		// very first call here is always safe.  After that, we can
		// repeat the call unless we're talking to a pre-1.7.2 version
		// of dmd-extensions, in which case we have to skip it.  Skipping
		// the call loses a small amount of functionality, namely the
		// ability of the DLL to apply per-game colorization settings.
		// But that's much better than crashing the whole process.
		static int nCalls = 0;
		bool hasNthCallBug = dmdExtInfo.matched && dmdExtInfo.virtualEnabled && !dmdExtInfo.settingsFix;
		bool safeToCall = nCalls == 0 || !hasNthCallBug;
		if (safeToCall)
		{
			// count the call
			++nCalls;

			// Post a settings object to the writer thread.  Don't do this
			// inline, since the settings call in the DLL might be slow.
			CriticalSectionLocker locker(writeFrameLock);
			writerSettings.reset(new GameSettings(gameName, opts));
			SetEvent(hWriterEvent);
		}
	}
}

void RealDMD::CloseSession()
{
	if (IsDllValid() && enabled && Close_ != NULL)
	{
		// check what kind of DLL we're talking to
		if (dmdExtInfo.matched && dmdExtInfo.virtualEnabled && !dmdExtInfo.virtualCloseFix)
		{
			// It's the dmd-extensions library with its virtual DMD enabled.
			// There's a bug in the DLL that crashes the process if we close
			// the session and re-open it, so we'll have to leave it open.
		}
		else
		{
			// for anything else, it should be safe to close the session,
			// which hopefully will release any USB connection and allow
			// other processes to access the DMD
			CriticalSectionLocker dmdLocker(dmdLock);
			Close_();
		}
	}
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

void RealDMD::BeginRunningGameMode()
{
	// clear media while running
	ClearMedia();
	curGame = nullptr;

	// close the session
	CloseSession();
}

void RealDMD::EndRunningGameMode()
{
	// reopen the session
	OpenSession();

	// reload the current game
	ReloadGame();
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
		videoMode = VideoMode::None;
	}

	// clear out the slide show
	slideShow.clear();
	slideShowPos = slideShow.end();

	// kill any slide show timer
	if (slideShowTimerID != 0)
	{
		KillTimer(NULL, slideShowTimerID);
		slideShowTimerID = 0;
		slideShowTimerRunning = false;
	}

	// send an empty frame to the display
	SendWriterFrame(emptySlide);
}

void RealDMD::ApplyWorkingAudioVolume(int volPct)
{
	if (videoPlayer != nullptr)
		videoPlayer->SetVolume(volPct);
}

void RealDMD::UpdateGame()
{
	// do nothing if the DLL isn't loaded
	if (!(IsDllValid() && enabled))
		return;
		
	// update our game if the selection in the game list has changed
	auto gl = GameList::Get();
	if (auto game = gl->GetNthGame(0); game != curGame)
	{
		// remember the new selection
		curGame = game;

		// note the media volume level
		int volPct = gl->GetAudioVolume(game);

		// Set the DMD color scheme for the game (or the default, if
		// there's no game)
		SetColorScheme(game);

		// Load media.  Search for video first, then a still image.
		// If the physical DMD is color-capable, use color source media
		// ahead of monochrome source material; for a monochrome device,
		// prioritize monochrome source material ahead of color.
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
		bool videosEnabled = Application::Get()->IsEnableVideo();
		TSTRING image, video;
		ColorSpace imageColorSpace = DMD_COLOR_MONO16;
		if (game != nullptr)
		{
			if (videosEnabled && Render_RGB24_ != nullptr
				&& game->GetMediaItem(video, GameListItem::realDMDColorVideoType))
			{
				// Color video source + color device - use color rendering
				videoColorSpace = DMD_COLOR_RGB;
			}
			else if (videosEnabled && game->GetMediaItem(video, GameListItem::realDMDVideoType))
			{
				// Monochrome video source - use monochrome rendering regardless of device type
				videoColorSpace = DMD_COLOR_MONO16;
			}
			else if (videosEnabled && Render_RGB24_ == nullptr
				&& game->GetMediaItem(video, GameListItem::realDMDColorVideoType))
			{
				// Color video source + monochrome device - use monochrome rendering
				videoColorSpace = DMD_COLOR_MONO16;
			}
			else if (Render_RGB24_ != nullptr
				&& game->GetMediaItem(image, GameListItem::realDMDColorImageType))
			{
				// Color image source + color device - use color rendering
				imageColorSpace = DMD_COLOR_RGB;
			}
			else if (game->GetMediaItem(image, GameListItem::realDMDImageType))
			{
				// Monochrome image - use monochrome rendering for any device type
				imageColorSpace = DMD_COLOR_MONO16;
			}
			else if (Render_RGB24_ == nullptr
				&& game->GetMediaItem(image, GameListItem::realDMDColorImageType))
			{
				// Color image + monochrome device - use monochrome rendering
				imageColorSpace = DMD_COLOR_MONO16;
			}
			else if ((videosEnabled && game->GetMediaItem(video, GameListItem::dmdVideoType))
				|| game->GetMediaItem(image, GameListItem::dmdImageType))
			{
				// We have a video or image for the SIMULATED (video screen) DMD.
				// These are in full color, since they're intended for regular
				// video display, so render in RGB if the device supports it.
				videoColorSpace = imageColorSpace = Render_RGB24_ != nullptr ? DMD_COLOR_RGB : DMD_COLOR_MONO16;
			}
		}

		// If we didn't find anything specific to the game, or we simply
		// have no game selected), look for a default video or image.
		if (image.length() == 0 && video.length() == 0)
		{
			// try to find, in order, a default color video, monochrome video, 
			// color image, and monochrome image
			auto gl = GameList::Get();
			TCHAR pathBuf[MAX_PATH];
			if (videosEnabled && gl != nullptr && Render_RGB24_ != nullptr
				&& gl->FindGlobalVideoFile(pathBuf, _T("Videos"), _T("Default Real DMD (color)")))
			{
				// We didn't find any media for this game, but we found a
				// default color DMD video.
				video = pathBuf;
				videoColorSpace = DMD_COLOR_RGB;
			}
			else if (videosEnabled && gl != nullptr
				&& gl->FindGlobalVideoFile(pathBuf, _T("Videos"), _T("Default Real DMD")))
			{
				// default monochrome DMD video
				video = pathBuf;
				videoColorSpace = DMD_COLOR_MONO16;
			}
			else if (gl != nullptr && Render_RGB24_ != nullptr
				&& gl->FindGlobalImageFile(pathBuf, _T("Images"), _T("Default Real DMD (color)")))
			{
				// default color image
				image = pathBuf;
				imageColorSpace = DMD_COLOR_RGB;
			}
			else if (gl != nullptr && gl->FindGlobalImageFile(pathBuf, _T("Images"), _T("Default Real DMD")))
			{
				// default monochrome image
				image = pathBuf;
				imageColorSpace = DMD_COLOR_MONO16;
			}
		}

		// If we found a video, and it's exactly the same video we're
		// already playing, simply leave the current video running.
		bool reload = true;
		if (video.length() != 0 && videoPlayer != nullptr)
		{
			if (auto oldPath = videoPlayer->GetMediaPath();
				oldPath != nullptr && _tcsicmp(oldPath, video.c_str()) == 0)
				reload = false;
		}

		// Load the new media if necessary
		if (reload)
		{
			// clear any previous media
			ClearMedia();

			// try loading the video first, if we found one
			bool ok = false;
			if (video.length() != 0)
				ok = LoadVideo(video.c_str(), true, true, VideoMode::Game, Application::InUiErrorHandler(), volPct);

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
							g2.ScaleTransform(float(dmdWidth) / cx, float(dmdHeight) / cy);

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
								*dst = (UINT8)((src[0] * 19660 + src[1] * 38666 + src[2] * 7209) >> 20);
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
		}

		// generate high-score graphics
		GenerateHighScoreGraphics();
	}
}

void RealDMD::SetColorScheme(GameListItem *game)
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
	if (game != nullptr && VPinMAMEIfc::FindRom(rom, game))
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
	tPMoptions opts = defaultOpts;

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

#undef Query
	}

	// send the settings to the device
	SetGameSettings(TSTRINGToCSTRING(rom).c_str(), opts);

	// remember the base color option
	baseColor = RGB(opts.dmd_red, opts.dmd_green, opts.dmd_blue);
}

bool RealDMD::LoadVideo(const TCHAR *path, bool looping, bool play, VideoMode mode, ErrorHandler &eh, int volPct)
{
	// create a new video player
	auto pfv = Application::Get()->GetPlayfieldView();
	auto hwndPfv = pfv != nullptr ? pfv->GetHWnd() : NULL;
	videoPlayer.Attach(new VLCAudioVideoPlayer(hwndPfv, hwndPfv, false));

	// set the desired looping mode and audio volume
	videoPlayer->SetLooping(looping);
	videoPlayer->SetVolume(volPct);

	// try loading the video
	bool ok = videoPlayer->OpenDmdTarget(path, eh, this);
	
	// if desired, start playback immediately
	if (ok && play)
		ok = videoPlayer->Play(eh);

	// check the result
	if (ok)
	{
		// success - set the new video mode
		videoMode = mode;
	}
	else
	{
		// failed - forget the video player and clear the video mode
		videoPlayer = nullptr;
		videoMode = VideoMode::None;
	}

	// return the result
	return ok;
}

void RealDMD::StartSlideShow()
{
	// if we don't have any slides, there's nothing to do
	if (slideShow.size() == 0)
	{
		// if there's a video player, and it's not already playing, loop it
		if (videoPlayer != nullptr && !videoPlayer->IsPlaying())
			videoPlayer->Replay(SilentErrorHandler());

		// done
		return;
	}

	// start at the first slide
	slideShowPos = slideShow.begin();

	// if a video is in progress, let it run - it'll start the slide show
	// at its next loop
	if (videoPlayer != nullptr && videoPlayer->IsPlaying())
		return;

	// render the first slide
	RenderSlide();

	// set a timer to advance to the next slide
	SetSlideShowTimer();
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
		inst->slideShowTimerRunning = false;

		// advance to the next slide in the slide show
		inst->NextSlide();
	}
}

void RealDMD::NextSlide()
{
	// if there are slides, advance to the next one
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
				// loop it if it's not already playing
				if (!videoPlayer->IsPlaying())
					videoPlayer->Replay(SilentErrorHandler());

				// we're done until the video finishes this playback iteration
				return;
			}

			// there's no video - start over at the first slide
			slideShowPos = slideShow.begin();
		}

		// show the current slide
		RenderSlide();

		// set the slide show timer
		SetSlideShowTimer();
	}
	else
	{
		// no slides - if there's a video player, and it's not already
		// playing, loop it
		if (videoPlayer != nullptr && !videoPlayer->IsPlaying())
			videoPlayer->Replay(SilentErrorHandler());
	}
}

void RealDMD::SetSlideShowTimer()
{
	// set a timer to advance to the next slide
	if (!slideShowTimerRunning)
	{
		slideShowTimerID = SetTimer(NULL, 0, (*slideShowPos)->displayTime, SlideTimerProc);
		slideShowTimerRunning = true;
	}
}

void RealDMD::RenderSlide()
{
	// if a slide is current, and a video isn't playing, render the current slide
	if (slideShow.size() != 0 && slideShowPos != slideShow.end()
		&& !(videoPlayer != nullptr && videoPlayer->IsPlaying()))
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
			// get the latest video frame and settings data
			RefPtr<Slide> frame;
			std::unique_ptr<GameSettings> settings;
			{
				// lock the queue
				CriticalSectionLocker frameLocker(writeFrameLock);

				// if we have no work to do, we're done for this round
				if (writerFrame == nullptr && writerSettings == nullptr)
					break;

				// grab the pending video frame, taking over its reference count
				frame.Attach(writerFrame.Detach());

				// grab the pending settings
				settings.reset(writerSettings.release());
			}

			// send the settings to the device
			if (settings != nullptr)
			{
				// Send the settings to the device.  Note that the generation
				// is chosen for the way we're going to use the device, not for
				// how the associated game would use it, since the game isn't
				// actually sending the device data - we are.  For our purposes,
				// the WPC95 generation is suitable.  (Note that if we didn't
				// load any registry settings, we'll still have valid defaults
				// to send from initializing the struct earlier.)
				CriticalSectionLocker dmdLocker(dmdLock);
				PM_GameSettings_(settings->gameName.c_str(), GEN_WPC95, settings->opts);
			}

			// send the video frame to the device
			if (frame != nullptr)
			{
				CriticalSectionLocker dmdLocker(dmdLock);
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
		int nSlides = 0;
		curGame->DispHighScoreGroups([this, &nSlides](const std::list<const TSTRING*> &group)
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

			// count the slides we added
			++nSlides;
		});

		// if we only generated one slide, extend its display time
		if (nSlides == 1)
			slideShow.back()->displayTime += 2000;
	}

	// reset the slide show pointer
	slideShowPos = slideShow.end();

	// restart the slide show
	StartSlideShow();
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

					// downconvert from 8 bits to 4 bits
					*dst = (gammaMap[a] >> 4) & 0x0F;
					dst += dstColInc;
				}
			}

			// display it
			CriticalSectionLocker dmdLocker(dmdLock);
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
				for (int col = 0; col < dmdWidth; ++col)
				{
					*dst = (gammaMap[*y++] >> 4) & 0x0F;
					dst += dstColInc;
				}
			}

			// display it
			CriticalSectionLocker dmdLocker(dmdLock);
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
			CriticalSectionLocker dmdLocker(dmdLock);
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
			CriticalSectionLocker dmdLocker(dmdLock);
			Render_RGB24_(dmdWidth, dmdHeight, rgb);
		}
		break;
	}
}

void RealDMD::VideoEndOfPresentation(WPARAM cookie)
{
	// check if this is for our current video
	if (videoPlayer != nullptr && videoPlayer->GetCookie() == cookie)
	{
		// check what mode we're in
		switch (videoMode)
		{
		case VideoMode::Startup:
			// startup video - clear the media and notify the main window 
			// that our startup video has finished
			curGame = nullptr;
			ClearMedia();
			if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != nullptr)
				pfv->OnEndExtStartupVideo();

			break;
		}
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
		SilentErrorHandler seh;
		if (slideShow.size() != 0)
		{
			videoPlayer->Stop(seh);
			StartSlideShow();
		}
		else
			videoPlayer->Replay(seh);
	}
}

void RealDMD::Log(const TCHAR *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	LogFile::Get()->WriteV(false, LogFile::DmdLogging, msg, ap);
	va_end(ap);
}

void RealDMD::LogGroup()
{
	LogFile::Get()->Group(LogFile::DmdLogging);
}

bool RealDMD::LoadStartupVideo()
{
	// Check for a startup video.  Try for a color video first,
	// then a monochrome video
	auto TryVideo = [this](const TCHAR *name, bool isColor)
	{
		// try this file
		TCHAR startupVideo[MAX_PATH];
		auto gl = GameList::Get();
		if (gl != nullptr && gl->FindGlobalVideoFile(startupVideo, _T("Startup Videos"), name))
		{
			// Got it.  Clear any previous media and set the default color scheme.
			curGame = nullptr;
			ClearMedia();
			SetColorScheme(nullptr);

			// Try loading the video.
			if (LoadVideo(startupVideo, false, false, VideoMode::Startup, LogFileErrorHandler(), 100))
			{
				// use a 24-bit color space if this is a color video and the device
				// supports full color, otherwise use 16-shade monochrome
				videoColorSpace = isColor && Render_RGB24_ != nullptr ? DMD_COLOR_RGB : DMD_COLOR_MONO16;

				// success
				return true;
			}
		}

		// not found or failed to load
		return false;
	};

	// if we're on a color device, try a color video first
	if (Render_RGB24_ != nullptr && TryVideo(_T("Startup Video (realdmd color)"), true))
		return true;

	// try the monochrome video next
	if (TryVideo(_T("Startup Video (realdmd)"), false))
		return true;

	// if we're on a monochrome device, try color video last
	if (Render_RGB24_ == nullptr && TryVideo(_T("Startup Video (realdmd color)"), true))
		return true;

	// no media found
	return false;
}

bool RealDMD::PlayStartupVideo() 
{
	// if there's a video, start it playing
	if (videoPlayer != nullptr && videoMode == VideoMode::Startup)
		return videoPlayer->Play(LogFileErrorHandler());
	
	// there's no video to play, so this is a successful no-op
	return true;
}

void RealDMD::EndStartupVideo()
{
	// end the startup video
	if (videoPlayer != nullptr && videoMode == VideoMode::Startup)
	{
		curGame = nullptr;
		ClearMedia();
	}
}

bool RealDMD::IsStartupVideoPlaying() const
{
	return videoMode == Startup && videoPlayer != nullptr && videoPlayer->IsPlaying();
}

