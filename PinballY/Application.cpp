// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//


#include "stdafx.h"
#include <CommCtrl.h>
#include <winsafer.h>
#include <shellapi.h>
#include <TlHelp32.h>
#include <psapi.h>
#include <dshow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <varargs.h>
#include "../Utilities/InputManagerWithConfig.h"
#include "../Utilities/Config.h"
#include "../Utilities/Joystick.h"
#include "../Utilities/KeyInput.h"
#include "../Utilities/ComUtil.h"
#include "../Utilities/AutoRun.h"
#include "../Utilities/ProcUtil.h"
#include "../Utilities/DateUtil.h"
#include "../Utilities/AudioCapture.h"
#include "../Utilities/GraphicsUtil.h"
#include "Application.h"
#include "Resource.h"
#include "D3D.h"
#include "GameList.h"
#include "D3DView.h"
#include "PlayfieldWin.h"
#include "PlayfieldView.h"
#include "BackglassWin.h"
#include "BackglassView.h"
#include "DMDWin.h"
#include "DMDView.h"
#include "TopperWin.h"
#include "TopperView.h"
#include "InstCardWin.h"
#include "InstCardView.h"
#include "AudioManager.h"
#include "DOFClient.h"
#include "TextureShader.h"
#include "I420Shader.h"
#include "DMDShader.h"
#include "PinscapeDevice.h"
#include "MonitorCheck.h"
#include "HighScores.h"
#include "VLCAudioVideoPlayer.h"
#include "RefTableList.h"
#include "CaptureStatusWin.h"
#include "LogFile.h"
#include "RealDMD.h"

// --------------------------------------------------------------------------
//
// Tell the linker to generate the manifest with common control support
//
#pragma comment(linker, "/manifestdependency:\"type='win32' \
    name='Microsoft.Windows.Common-Controls' \
       version='6.0.0.0' \
       processorArchitecture='*' \
       publicKeyToken='6595b64144ccf1df' \
       language='*'\"")

// include the common control library in the link
#pragma comment(lib, "comctl32.lib")

// include required media foundation libraries
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")

// include the DirectShow class IDs
#pragma comment(lib, "strmiids.lib")



// --------------------------------------------------------------------------
//
// Config variable names
//
namespace ConfigVars
{
	static const TCHAR *MuteVideos = _T("Video.Mute");
	static const TCHAR *VideoVolume = _T("Video.MasterVolume");
	static const TCHAR *MuteTableAudio = _T("TableAudio.Mute");
	static const TCHAR *EnableVideos = _T("Video.Enable");
	static const TCHAR *MuteAttractMode = _T("AttractMode.Mute");
	static const TCHAR *GameTimeout = _T("GameTimeout");
	static const TCHAR *HideTaskbarDuringGame = _T("HideTaskbarDuringGame");
	static const TCHAR *FirstRunTime = _T("FirstRunTime");
	static const TCHAR *HideUnconfiguredGames = _T("GameList.HideUnconfigured");
	static const TCHAR *VSyncLock = _T("VSyncLock");
	static const TCHAR *DOFEnable = _T("DOF.Enable");
	static const TCHAR *MouseHideByMoving = _T("Mouse.HideByMoving");
	static const TCHAR *MouseHideCoors = _T("Mouse.HideCoords");
	static const TCHAR *UnderlayHeightOffset = _T("UnderlayHeightOffset");
}

// include the capture-related variables
#include "CaptureConfigVars.h"


// --------------------------------------------------------------------------
//
// Main application entrypoint
//
int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE /*hPrevInstance*/,
	_In_ LPTSTR lpCmdLine,
	_In_ int nCmdShow)
{
	// enable memory leak debugging at exit, if in debug mode
	IF_DEBUG(_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF));

	// pass control to the application object
	return Application::Main(hInstance, lpCmdLine, nCmdShow);
}

// --------------------------------------------------------------------------
//
// statics
//
Application *Application::inst;
bool Application::isInForeground = true;
bool Application::playVideosInBackground = false;
HCURSOR Application::emptyCursor;


// --------------------------------------------------------------------------
//
// Run the application
//
int Application::Main(HINSTANCE hInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	// remember the instance handle globally
	G_hInstance = hInstance;

	// Initialize COM.  For the sake of our Shockwave Flash sprites,
	// initialize in OLE mode.  This is required for threads that
	// create OLE objects, which we do if we load any Flash media.
	// Note the this sets up the thread in "single-threaded apartment"
	// mode; if we didn't use OLE, we'd prefer to initialize in free-
	// threaded mode via CoInitializeEx(NULL, COINIT_MULTITHREADED).
	HRESULT hr = OleInitialize(NULL);
	if (FAILED(hr))
	{
		LogSysError(EIT_Error, LoadStringT(IDS_ERR_COINIT),
			MsgFmt(_T("CoInitializeEx failed, error %lx"), hr));
		return 0;
	}

	// check for special launch modes
	std::match_results<const TCHAR*> m;
	if (std::regex_match(lpCmdLine, m, std::basic_regex<TCHAR>(_T("\\s*/AutoLaunch=AdminMode,delay=(\\d+)\\s*"))))
	{
		// Extract the delay time
		auto delay = static_cast<DWORD>(_ttol(m[1].str().c_str()));

		// Set up admin mode auto launch.  This sets up auto launch for
		// our "PinballY Admin Mode" executable instead of the regular
		// PinballY executable.
		TCHAR exe[MAX_PATH];
		GetExeFilePath(exe, countof(exe));
		PathAppend(exe, _T("PinballY Admin Mode.exe"));
		bool ok = SetUpAutoRun(true, _T("PinballY"), exe, nullptr, true, delay, InteractiveErrorHandler());

		// indicate success/failure via the exit code
		return ok ? 0 : 2;
	}

	// initialize common controls
	INITCOMMONCONTROLSEX initCtrls;
	initCtrls.dwSize = sizeof(initCtrls);
	initCtrls.dwICC =
		ICC_BAR_CLASSES
		| ICC_COOL_CLASSES
		| ICC_LINK_CLASS
		| ICC_LISTVIEW_CLASSES
		| ICC_STANDARD_CLASSES
		| ICC_TAB_CLASSES
		| ICC_TREEVIEW_CLASSES
		| ICC_USEREX_CLASSES
		| ICC_WIN95_CLASSES;
	InitCommonControlsEx(&initCtrls);

	// Initialize GDI+
	GdiplusIniter gdiplus;

	// load the empty (blank) cursor
	emptyCursor = static_cast<HCURSOR>(LoadImage(hInstance, MAKEINTRESOURCE(IDCSR_EMPTY), IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE));

	// create the application object
	std::unique_ptr<Application> appInst(new Application());

	// run the event loop
	return appInst->EventLoop(nCmdShow);
}

void Application::HideCursor()
{
	// check which hide mode we're in
	auto app = Get();
	if (app->hideCursorByMoving)
	{
		// hide by moving the mouse to a (presumably) hidden parking position
		SetCursorPos(app->hideCursorPos.x, app->hideCursorPos.y);
	}
	else
	{
		// hide by showing our empty cursor
		SetCursor(emptyCursor);
	}
}

int Application::EventLoop(int nCmdShow)
{
	// parse arguments
	for (int i = 1; i < __argc; ++i)
	{
		const TCHAR *argp = __targv[i];
		std::match_results<const TCHAR*> m;

		// AdminHost mode: this means that we're being launched as the
		// child of the Admin Host program.  
		if (std::regex_match(argp, m, std::basic_regex<TCHAR>(_T("/AdminHost:(\\d+)"))))
		{
			// /AdminHost:<pid>
			//
			// Tells us that we were launched under an Admin Host parent
			// process, which is a separate process running elevated (in
			// Admin) specifically so that it can provide elevated process
			// launching services for us.  The host process sets the stdin
			// and stdout handles to pipe ends that we use to communicate
			// with it.
			adminHost.hPipeIn = GetStdHandle(STD_INPUT_HANDLE);
			adminHost.hPipeOut = GetStdHandle(STD_OUTPUT_HANDLE);

			// Get the process ID of the Admin Host process from the option
			// parameters
			adminHost.pid = _ttol(m[1].str().c_str());

			// start the pipe manager thread
			adminHost.StartThread();
		}

		// Javascript Debug mode
		if (std::regex_match(argp, m, std::basic_regex<TCHAR>(_T("/jsdebug(:(.*))?"), std::regex_constants::icase)))
		{
			// enable debugging
			javascriptDebugOptions.enable = true;
			javascriptDebugOptions.serviceName = "PinballY";
			javascriptDebugOptions.serviceDesc = "PinballY";

			// load the favorites icon, if provided
			if (HRSRC hrsrc = ::FindResource(G_hInstance, _T("JSDEBUGGERICON"), _T("ICOFILE")); hrsrc != NULL)
			{
				if (HGLOBAL hglobal = ::LoadResource(G_hInstance, hrsrc); hglobal != NULL)
				{
					javascriptDebugOptions.favIcon = static_cast<const BYTE*>(::LockResource(hglobal));
					javascriptDebugOptions.favIconSize = ::SizeofResource(G_hInstance, hrsrc);
				}
			}

			// scan additional options
			if (m[2].matched && m[2].length() != 0)
			{
				TSTRING subopts = m[2];
				if (std::regex_search(subopts.c_str(), m, std::basic_regex<TCHAR>(_T("\\bport=(\\d+)\\b"), std::regex_constants::icase)))
					javascriptDebugOptions.port = static_cast<uint16_t>(_ttoi(m[1].str().c_str()));

				if (std::regex_search(subopts.c_str(), m, std::basic_regex<TCHAR>(_T("\\bbreak=(.+)\\b"), std::regex_constants::icase)))
				{
					if (_tcsicmp(m[1].str().c_str(), _T("system")) == 0)
						javascriptDebugOptions.initBreak = JavascriptEngine::DebugOptions::SystemCode;
					else if (_tcsicmp(m[1].str().c_str(), _T("user")) == 0)
						javascriptDebugOptions.initBreak = JavascriptEngine::DebugOptions::UserCode;
					else if (_tcsicmp(m[1].str().c_str(), _T("none")) == 0)
						javascriptDebugOptions.initBreak = JavascriptEngine::DebugOptions::None;
				}

				if (std::regex_search(subopts.c_str(), m, std::basic_regex<TCHAR>(_T("\\bwait=(.+)\\b"), std::regex_constants::icase)))
				{
					if (_tcsicmp(m[1].str().c_str(), _T("yes")) == 0)
						javascriptDebugOptions.waitForDebugger = true;
					else if (_tcsicmp(m[1].str().c_str(), _T("no")) == 0)
						javascriptDebugOptions.waitForDebugger = false;
				}
			}
		}
	}

	// initialize the core subsystems and load config settings
	if (!Init() || !LoadConfig(MainConfigFileDesc))
		return 0;

	// Open a dummy window to take focus at startup.  This works around
	// a snag that can happen if we have a RunAtStartup program, and
	// that program takes focus.  We have to run that program, by
	// design, before opening our actual UI windows, but that means
	// that if the RunAtStartup program takes focus at any point, focus
	// won't be able to go to our UI window when the program exits.
	// Windows has to set focus *somewhere* when the child program
	// exits, and if we don't provide a window that can accept it,
	// Windows will use the desktop as the last resort.  That will
	// prevent our main UI window from being able to acquire focus when
	// we get around to opening it later.
	class DummyWindow : public BaseWin
	{
	public:
		DummyWindow() : BaseWin(0) { }
		virtual void UpdateMenu(HMENU, BaseWin*) { }
	};
	RefPtr<DummyWindow> dummyWindow(new DummyWindow());
	dummyWindow->Create(NULL, _T("PinballY"), WS_POPUPWINDOW, SW_SHOW);

	// If desired, check for monitors
	if (const TCHAR *monWaitSpec = ConfigManager::GetInstance()->Get(_T("WaitForMonitors"), _T(""));
		!std::regex_match(monWaitSpec, std::basic_regex<TCHAR>(_T("\\s*"))))
	{
		int extraWait = ConfigManager::GetInstance()->GetInt(_T("WaitForMonitors.ExtraDelay"), 0);
		MonitorCheck::WaitForMonitors(monWaitSpec, extraWait * 1000);
	}

	// Check for a RunBefore program.  Do this after the monitor check
	// has been completed, so that the RunBefore program runs in the
	// stable desktop environment that the monitor wait is intended to
	// guarantee.  But run it before we open any our our UI windows, so
	// that it do any desired preprocessing on our database or media
	// files before we start looking at them.  The one thing we can't
	// let it process first is our config file, since we have to read
	// the config file first in order to find the RunBefore program!
	// (Note: if someone actually does want to mess with the config
	// file at some point, it would be simple enough to re-read the 
	// config file after the RunBefore process finishes.  But for now
	// let's assume this isn't necessary.)
	CheckRunAtStartup();

	// set up DOF before creating the UI
	if (ConfigManager::GetInstance()->GetBool(ConfigVars::DOFEnable, true))
		DOFClient::Init();

	// initialize the game list
	CapturingErrorHandler loadErrs;
	if (!InitGameList(loadErrs, InteractiveErrorHandler()))
		return 0;

	// initialize the Pinscape device list
	PinscapeDevice::FindDevices(pinscapeDevices);

	// create the window objects
	playfieldWin.Attach(new PlayfieldWin());
	backglassWin.Attach(new BackglassWin());
	dmdWin.Attach(new DMDWin());
	topperWin.Attach(new TopperWin());
	instCardWin.Attach(new InstCardWin());

	// get the FFmpeg version by running FFmpeg with no arguments and
	// finding the version string in the stdout results
	{
		// run FFmpeg (32- or 64-bit version, according to our build type)
		// with stdout capture
		TCHAR ffmpeg[MAX_PATH];
		GetDeployedFilePath(ffmpeg, _T("ffmpeg\\ffmpeg.exe"), _T("$(SolutionDir)ffmpeg$(64)\\ffmpeg.exe"));
		CreateProcessCaptureStdout(ffmpeg, _T(""), 5000,
			[this](const BYTE *stdoutContents, long len)
		{
			// find "ffmpeg version <xxx>"
			CSTRING buf((const CHAR *)stdoutContents, (size_t)len);
			std::match_results<CSTRING::const_iterator> m;
			if (std::regex_search(buf, m, std::regex("ffmpeg version (\\S+)", std::regex_constants::icase)))
				ffmpegVersion = m[1].str();
		}, [](const TCHAR *) {});
	}

	// create the high scores reader object
	highScores.Attach(new HighScores());

	// open the UI windows
	bool ok = true;
	if (!playfieldWin->CreateWin(NULL, nCmdShow, _T("PinballY")))
	{
		ok = false;
		PostQuitMessage(1);
	}

	// set up the backglass window
	if (ok && !backglassWin->CreateWin(playfieldWin->GetHWnd(), nCmdShow, _T("PinballY Backglass")))
	{
		ok = false;
		PostQuitMessage(1);
	}

	// set up the DMD window
	if (ok && !dmdWin->CreateWin(playfieldWin->GetHWnd(), nCmdShow, _T("PinballY DMD")))
	{
		ok = false;
		PostQuitMessage(1);
	}

	// set up the topper window
	if (ok && !topperWin->CreateWin(playfieldWin->GetHWnd(), nCmdShow, _T("PinballY Topper")))
	{
		ok = false;
		PostQuitMessage(1);
	}

	// set up the instruction card window
	if (ok && !instCardWin->CreateWin(playfieldWin->GetHWnd(), nCmdShow, _T("PinballY Instruction Card")))
	{
		ok = false;
		PostQuitMessage(1);
	}

	// initialize javascript
	GetPlayfieldView()->InitJavascript();

	// set up raw input through the main playfield window's message loop
	if (ok)
		ok = InputManager::GetInstance()->InitRawInput(playfieldWin->GetHWnd());

	// initialize the high scores object
	highScores->Init();

	// try setting up real DMD support
	if (ok)
		GetPlayfieldView()->InitRealDMD(InUiErrorHandler());

	// Generate a PINemHi version request on behalf of the main window
	highScores->GetVersion(GetPlayfieldView()->GetHWnd());

	// If we got this far, we were able to load at least part of the game
	// list, but there might have been errors or warnings from loading
	// parts of the list.  If there are any errors in the capture list, show 
	// them via a graphical popup.  That's less obtrusive than a system
	// message box, which is appropriate given that things are at least
	// partially working, but still lets the user know that something
	// might need attention.
	if (loadErrs.CountErrors() != 0)
		GetPlayfieldView()->ShowError(EIT_Error, LoadStringT(IDS_ERR_LISTLOADWARNINGS), &loadErrs);

	// wait for DOF initialization to complete
	DOFClient::WaitReady();

	// If we ran into DOF errors, show those
	GetPlayfieldView()->ShowDOFClientInitErrors();

	// bring the main playfield window to the front
	SetForegroundWindow(playfieldWin->GetHWnd());
	SetActiveWindow(playfieldWin->GetHWnd());

	// done with the dummy window
	dummyWindow->SendMessage(WM_CLOSE);
	dummyWindow = nullptr;

	// Start loading the reference game list.  This loads in the background,
	// since it isn't needed until the user runs a Game Setup dialog, which
	// usually won't happen right away.
	refTableList->Init();

	// launch the watchdog process
	watchdog.Launch();

	// run the main window's message loop
	int retcode = D3DView::MessageLoop();

	// if there's a game monitor thread, shut it down
	if (gameMonitor != nullptr)
	{
		InteractiveErrorHandler eh;
		gameMonitor->Shutdown(eh, 5000, true);
		gameMonitor = nullptr;
	}
	
	// Delete any queued launches.  The only reason we have to do this
	// explicitly (rather than letting the destructor take care of it) is
	// that the monitor objects contain D3D window refs, and those will
	// want to access the D3D subsystem in their destructors, so we have
	// to make sure they get cleaned up before we shut down the global
	// D3D object.
	queuedLaunches.clear();

	// If there's a new file scanner thread running, give it a few seconds
	// to finish.
	if (newFileScanThread != nullptr)
		WaitForSingleObject(newFileScanThread->hThread, 5000);

	// save any updates to the config file or game databases
	SaveFiles();

	// if there's an admin host thread, terminate it
	adminHost.Shutdown();

	// make sure any high score image generator threads have exited
	if (auto dmv = GetDMDView(); dmv != nullptr)
		dmv->WaitForHighScoreThreads(5000);

	// close the windows
	DestroyWindow(playfieldWin->GetHWnd());
	DestroyWindow(backglassWin->GetHWnd());
	DestroyWindow(dmdWin->GetHWnd());
	DestroyWindow(topperWin->GetHWnd());
	DestroyWindow(instCardWin->GetHWnd());

	// release the window pointers
	playfieldWin = nullptr;
	backglassWin = nullptr;
	dmdWin = nullptr;
	topperWin = nullptr;
	instCardWin = nullptr;

	// wait for the audio/video player deletion queue to empty
	AudioVideoPlayer::WaitForDeletionQueue(5000);

	// Shut down Javascript.  Do this after saving files, because if we were
	// launched by a debugger (e.g., VS Code), the debugger might kill the
	// debugee child process (that would be us) as soon as we disconnect the
	// debugger socket.  We don't want to be in the middle of any file writes
	// if we get asynchronously terminated like that.
	JavascriptEngine::Terminate();

	// check for a RunAfter program
	CheckRunAtExit();

	// return the Quit message parameter, if we got one
	return retcode;
}

bool Application::LaunchAdminHost(ErrorHandler &eh)
{
	// Get the current program file, and replace the file spec part
	// with the Admin Host program name.
	TCHAR exe[MAX_PATH];
	GetModuleFileName(NULL, exe, countof(exe));
	PathRemoveFileSpec(exe);
	PathAppend(exe, _T("PinballY Admin Mode.exe"));

	// The only way to launch an elevated (Administrator mode) child
	// process from a non-elevated (ordinary user mode) parent is via
	// ShellExecuteEx().  The CreateProcess() variants don't provide
	// any way to launch children at a higher privileged level.
	//
	// Note that we don't need to do anything special in the API call
	// to trigger the elevation, because the privilege level request
	// is contained in the .exe we're launching via its manifest.  If
	// we were trying to launch a program that didn't have the
	// privilege request in its manifest, we could trigger elevation
	// explicitly by using the undocumented lpVerb value "runas",
	// which has the same effect as right-clicking the file in the
	// desktop window and selecting "Run as administrator".  But
	// there's no need for that in this case, so we'll stick to the
	// documented API.
	SHELLEXECUTEINFO shEx;
	ZeroMemory(&shEx, sizeof(shEx));
	shEx.cbSize = sizeof(shEx);
	shEx.fMask = 0;
	shEx.hwnd = 0;
	shEx.lpVerb = _T("open");
	shEx.lpFile = exe;
	shEx.lpParameters = NULL;
	shEx.lpDirectory = NULL;
	shEx.nShow = SW_SHOW;
	shEx.hInstApp = 0;
	if (!ShellExecuteEx(&shEx))
	{
		// If the error is ERROR_CANCELLED, it means the user refused
		// the UAC elevation request.  Simply abort the whole run by
		// returning true to tell the caller to exit.  Don't show any
		// errors in this case, since the cancellation came from the
		// user in the first place, hence they already know why the
		// operation won't proceed.
		WindowsErrorMessage winErr;
		if (winErr.GetCode() == ERROR_CANCELLED)
			return true;

		// show an error
		eh.SysError(LoadStringT(IDS_ERR_LAUNCH_ADMIN_HOST_FAIL),
			MsgFmt(_T("ShellExecuteEx() failed: error %d, %s"), winErr.GetCode(), winErr.Get()));

		// return failure
		return false;
	}

	// success
	return true;
}

// Restart in Admin mode.  This can be called from the UI to
// handle an explicit request from the user to restart in Admin
// mode.  This tries to launch a new elevated instance of the 
// program; on success, we'll shut down the current instance to
// let the new instance take over.
void Application::RestartAsAdmin()
{
	// Save all file and config updates before we launch the new 
	// process, so that it starts up with the same values we have 
	// in memory right now.
	SaveFiles();

	// We only attempt the Admin mode launch on explicit user
	// request, and we only offer that option when a game launch
	// requires it.  So we can create the "Admin Mode Confirmed"
	// marker file to record this explicit user approval and skip 
	// the warning prompt that we'd normally show on the first
	// invocation of the Admin Mode program.
	const TCHAR *confirmFile = _T(".AdminModeConfirmed");
	if (!PathFileExists(confirmFile))
	{
		FILE *fp;
		if (_tfopen_s(&fp, confirmFile, _T("w")))
		{
			_ftprintf(fp, _T("Confirmed\n"));
			fclose(fp);
		}
	}

	// Try launching a new session under the Admin Host
	InUiErrorHandler eh;
	if (LaunchAdminHost(eh))
	{
		// Successfully launched the new instance.  Exit the current
		// session by closing the UI.
		if (auto pfv = GetPlayfieldView(); pfv != nullptr)
			pfv->PostMessage(WM_COMMAND, ID_EXIT);
		else
			PostQuitMessage(0);
	}
}

Application::Application()
{
	// initialize variables
	muteVideos = false;
	videoVolume = 100;
	muteTableAudio = false;
	muteAttractMode = true;
	enableVideos = true;

	// remember the global instance pointer
	if (inst == 0)
		inst = this;

	// Create the reference table list object.  Don't actually start
	// loading the table file yet, as that consumes CPU time that could
	// slow down startup, and we won't need the data until the user
	// navigates to somewhere in the UI that uses it, such as the Game
	// Setup dialog.  (All of the consumers need to be aware of the
	// asynchronous loading, so that they're tolerant of running before
	// the loading is completed.)
	refTableList.reset(new RefTableList());
}

bool Application::Init()
{
	// load the app title string
	Title.Load(IDS_APP_TITLE);

	// initialize the log file - do this first, so that other subsystems
	// can log messages during initialization if desired
	LogFile::Init();

	// Set up the config manager.  Do this as the first thing after
	// setting up the log file.
	ConfigManager::Init();

	// let the log file load any config data it needs
	LogFile::Get()->InitConfig();

	// initialize the media type list
	GameListItem::InitMediaTypeList();

	// initialize D3D
	if (!D3D::Init())
		return false;

	// create the texture shader
	textureShader.reset(new TextureShader());
	if (!textureShader->Init())
		return false;

	// create the DMD shader
	dmdShader.reset(new DMDShader());
	if (!dmdShader->Init())
		return false;

	// create the I420 shader
	i420Shader.reset(new I420Shader());
	if (!i420Shader->Init())
		return false;

	// initialize the audio manager
	AudioManager::Init();

	// start Media Foundation
	MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);

	// initialize the input manager
	if (!InputManagerWithConfig::Init())
		return false;

	// success
	return true;
}

Application::~Application()
{
	// shut down the DOF client
	DOFClient::Shutdown(true);

	// delete the game list
	GameList::Shutdown();

	// shut down libvlc
	VLCAudioVideoPlayer::OnAppExit();

	// clean up the input subsystem
	InputManager::Shutdown();

	// shut down the audio manager
	AudioManager::Shutdown();

	// shut down D3D
	D3D::Shutdown();

	// clean up the config manager
	ConfigManager::Shutdown();

	// close the log file
	LogFile::Shutdown();

	// forget the global instance pointer
	if (inst == this)
		inst = 0;

	// shut down media foundation
	MFShutdown();

	// shut down COM/OLE before we exit
	OleUninitialize();
}

bool Application::LoadConfig(const ConfigFileDesc &fileDesc)
{
	// load the configuration
	if (!ConfigManager::GetInstance()->Load(fileDesc))
		return false;

	// If the "first run" timestamp hasn't been set, set it to the
	// current time.
	TSTRING firstRunTime = ConfigManager::GetInstance()->Get(ConfigVars::FirstRunTime, _T(""));
	if (firstRunTime.length() == 0)
	{
		// get the current date/time
		firstRunTime = DateTime().ToString();

		// save it
		ConfigManager::GetInstance()->Set(ConfigVars::FirstRunTime, firstRunTime.c_str());
	}

	// remember the first run time
	this->firstRunTime = DateTime(firstRunTime.c_str());

	// load our own config variables
	OnConfigChange();

	// success
	return true;
}

bool Application::InitGameList(CapturingErrorHandler &loadErrs, ErrorHandler &fatalErrorHandler)
{
	GameList::Create();
	GameList::Get()->Init(loadErrs);
	if (!GameList::Get()->Load(loadErrs))
	{
		MultiErrorList meh;
		meh.Add(&loadErrs);
		meh.Report(EIT_Error, fatalErrorHandler, LoadStringT(IDS_ERR_GAMELISTLOAD).c_str());
		return false;
	}

	// restore the current game selection and filter setting
	GameList::Get()->RestoreConfig();

	// success
	return true;
}

bool Application::ReloadConfig()
{
	// the UI should be running when this is called, so show any
	// errors via the in-UI mechanism
	InUiErrorHandler uieh;

	// clear media in all windows
	ClearMedia();

	// re-create the game list
	GameList::ReCreate();

	// load the settings file
	if (!LoadConfig(MainConfigFileDesc))
		return false;

	// reset the game list
	CapturingErrorHandler loadErrs;
	if (!InitGameList(loadErrs, uieh))
		return false;

	// update the selection in the main playfield window (which will
	// trigger updates in the other windows)
	if (auto pfv = GetPlayfieldView(); pfv != nullptr)
		pfv->OnGameListRebuild();

	// reload DMD support
	GetPlayfieldView()->InitRealDMD(uieh);

	// show any non-fatal game list load errors
	if (loadErrs.CountErrors() != 0)
		GetPlayfieldView()->ShowError(EIT_Error, LoadStringT(IDS_ERR_LISTLOADWARNINGS), &loadErrs);

	// success
	return true;
}

void Application::OnConfigChange()
{
	// load application-level variables
	ConfigManager *cfg = ConfigManager::GetInstance();
	enableVideos = cfg->GetBool(ConfigVars::EnableVideos, true);
	muteVideos = cfg->GetBool(ConfigVars::MuteVideos, false);
	videoVolume = cfg->GetInt(ConfigVars::VideoVolume, 100);
	muteTableAudio = cfg->GetBool(ConfigVars::MuteTableAudio, false);
	muteAttractMode = cfg->GetBool(ConfigVars::MuteAttractMode, true);
	hideUnconfiguredGames = cfg->GetBool(ConfigVars::HideUnconfiguredGames, false);

	// update the video sync mode
	D3DWin::vsyncMode = cfg->GetBool(ConfigVars::VSyncLock, false) ? 1 : 0;

	// If the DOF mode has changed since we last checked, create or destroy
	// the DOF client.
	DOFClient::WaitReady();
	bool dofWasActive = DOFClient::Get() != nullptr;
	bool dofIsActive = cfg->GetBool(ConfigVars::DOFEnable, true);
	if (dofWasActive != dofIsActive)
	{
		// if DOF is newly enabled, initialize it; if it's newly disabled, shut it down
		if (dofIsActive)
			DOFClient::Init();
		else
			DOFClient::Shutdown(false);
	}

	// update the mouse hiding mode
	hideCursorByMoving = cfg->GetBool(ConfigVars::MouseHideByMoving, false);
	if (hideCursorByMoving)
	{
		const TCHAR *txt = cfg->Get(ConfigVars::MouseHideCoors, _T("1920,540"));
		_stscanf_s(txt, _T("%ld,%ld"), &hideCursorPos.x, &hideCursorPos.y);
	}
}

void Application::SaveFiles()
{
	// Skip this if the options dialog is showing.  The options dialog
	// also accesses the config file, so give it exclusive access while
	// it's running.
	if (auto pfv = inst->GetPlayfieldView(); pfv != nullptr && pfv->IsSettingsDialogOpen())
		return;

	// save any statistics database updates
	GameList::Get()->SaveStatsDb();

	// save the current game selection and game list filter
	GameList::Get()->SaveConfig();

	// save change to game database XML files
	GameList::Get()->SaveGameListFiles();

	// save any config setting updates
	ConfigManager::GetInstance()->SaveIfDirty();
}

void Application::CheckRunAtStartup()
{
	if (const TCHAR *cmd = ConfigManager::GetInstance()->Get(_T("RunAtStartup"), _T("")); 
	    !std::regex_match(cmd, std::basic_regex<TCHAR>(_T("\\s*"))))
		RunCommand(cmd, InteractiveErrorHandler(), IDS_ERR_RUNATSTARTUP);
}

void Application::CheckRunAtExit()
{
	if (const TCHAR *cmd = ConfigManager::GetInstance()->Get(_T("RunAtExit"), _T(""));
		!std::regex_match(cmd, std::basic_regex<TCHAR>(_T("\\s*"))))
		RunCommand(cmd, InteractiveErrorHandler(), IDS_ERR_RUNATEXIT);
}

bool Application::RunCommand(const TCHAR *cmd,
	ErrorHandler &eh, int friendlyErrorStringId,
	bool wait, HANDLE *phProcess, DWORD *pPid, UINT nShowCmd)
{
	// no process handle yet
	if (phProcess != nullptr)
		*phProcess = NULL;

	// set up the startup info
	STARTUPINFO startupInfo;
	ZeroMemory(&startupInfo, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESHOWWINDOW;
	startupInfo.wShowWindow = nShowCmd;

	// CreateProcess requires a writable buffer for the command line, so
	// copy it into a local string
	TSTRING cmdStr = cmd;

	// If the command is specified with an absolute path, pull out the
	// path and use it as the working directory.
	const TCHAR *workingDir = nullptr;
	TSTRING appName;
	GetAppNameFromCommandLine(appName, cmdStr.c_str());
	if (!PathIsRelative(appName.c_str()))
	{
		PathRemoveFileSpec(appName.data());
		workingDir = appName.c_str();
	}
	
	// launch the process
	PROCESS_INFORMATION procInfo;
	if (!CreateProcess(NULL, cmdStr.data(), NULL, NULL, false, 0, NULL,
		workingDir, &startupInfo, &procInfo))
	{
		// failed to launch - show an error and abort
		WindowsErrorMessage sysErr;
		eh.SysError(LoadStringT(friendlyErrorStringId),
			MsgFmt(_T("CreateProcess(%s) failed; system error %d: %s"), cmd, sysErr.GetCode(), sysErr.Get()));		
		return false;
	}

	// we don't need the thread handle for anything - close it immediately
	CloseHandle(procInfo.hThread);

	// If we're waiting, wait for the process to exit
	if (wait)
	{
		// wait for the process to finish
		if (WaitForSingleObject(procInfo.hProcess, INFINITE) == WAIT_OBJECT_0)
		{
			// success - close the handle and return success
			CloseHandle(procInfo.hProcess);
			return true;
		}
		else
		{
			// wait failed - show an error and return failure
			WindowsErrorMessage sysErr;
			eh.SysError(LoadStringT(friendlyErrorStringId),
				MsgFmt(_T("Error waiting for child process to exit; system error %d: %s"), sysErr.GetCode(), sysErr.Get()));
			return false;
		}
	}
	else
	{
		// They don't want to wait for the process to finish.  If they want
		// the handle returned, return it, otherwise close it.
		if (phProcess != nullptr)
			*phProcess = procInfo.hProcess;
		else
			CloseHandle(procInfo.hProcess);

		// pass back the PID if desired
		if (pPid != nullptr)
			*pPid = procInfo.dwProcessId;

		// the process was successfully launched
		return true;
	}
}

bool Application::LoadStartupVideos()
{

	// Call a function for each view.  Continues as long as each
	// callback returns true; returns the AND combination of all of 
	// the callback results.
	auto ForEachView = [this](std::function<bool(BaseView*)> func)
	{
		return func(GetPlayfieldView())
			&& func(GetBackglassView())
			&& func(GetDMDView())
			&& func(GetInstCardView())
			&& func(GetTopperView());
	};

	// try loading a video in each window
	bool found = false;
	ForEachView([&found](BaseView *view) 
	{ 
		// we have a startup video if there's a video in any window
		found |= view->LoadStartupVideo();
		return true; 
	});

	// try loading a video in the real DMD as well
	auto dmd = GetPlayfieldView() != nullptr ? GetPlayfieldView()->GetRealDMD() : nullptr;
	if (dmd != nullptr)
		found |= dmd->LoadStartupVideo();

	// if we found any videos, start them playing
	if (found)
	{
		// start them, noting if they all succeed
		bool ok = ForEachView([](BaseView *view) { return view->PlayStartupVideo(); });
		if (dmd != nullptr)
			ok &= dmd->PlayStartupVideo();

		// if we ran into any errors, cancel them all
		if (!ok)
		{
			ForEachView([](BaseView *view) { view->EndStartupVideo(); return true; });
			if (dmd != nullptr)
				dmd->EndStartupVideo();

			found = false;
		}
	}

	// tell the caller whether or not any videos were found
	return found;
}

void Application::SyncSelectedGame()
{
	if (backglassWin != 0 && backglassWin->GetView() != 0)
		backglassWin->GetView()->SendMessage(WM_COMMAND, ID_SYNC_GAME);
	if (dmdWin != 0 && dmdWin->GetView() != 0)
		dmdWin->GetView()->SendMessage(WM_COMMAND, ID_SYNC_GAME);
	if (topperWin != 0 && topperWin->GetView() != 0)
		topperWin->GetView()->SendMessage(WM_COMMAND, ID_SYNC_GAME);
	if (instCardWin != 0 && instCardWin->GetView() != 0)
		instCardWin->GetView()->SendMessage(WM_COMMAND, ID_SYNC_GAME);
}

void Application::InitDialogPos(HWND hDlg, const TCHAR *configVar)
{
	// get the dialog's default location
	RECT winrc;
	GetWindowRect(hDlg, &winrc);

	// note its size
	int winwid = winrc.right - winrc.left, winht = winrc.bottom - winrc.top;

	// look for a saved location
	RECT savedrc;
	if (savedrc = ConfigManager::GetInstance()->GetRect(configVar); !IsRectEmpty(&savedrc))
	{
		// We have a saved position - restore it, with one adjustment.
		// The saved rect might be from an earlier version where the
		// dialog size was different, so the position might be a bit
		// off when applied to the new dialog.  So intead of using
		// the upper left coordinates of the saved position, use the
		// center coordinates.  That is, center the new dialog on the
		// center position of the old dialog.  In cases where the new
		// and old dialog sizes are the same, this will yield exactly
		// the same position; when the size has changed, this should
		// yield a position that looks the same to the eye.  In any
		// case, we further adjust the position below to ensure that
		// the final window position is within the viewable screen
		// area, so if our screen layout has changed since the rect
		// was saved, or the new size adjustment pushes it out of
		// bounds, we'll correct for that.
		winrc.left = (savedrc.left + savedrc.right)/2 - winwid/2;
		winrc.top = (savedrc.top + savedrc.bottom)/2 - winht/2;
		winrc.right = winrc.left + winwid;
		winrc.bottom = winrc.top + winht;
	}
	else
	{
		// There's no saved position.  Look for an open window that's not 
		// rotated and that's big enough to contain the dialog.  If we find
		// one, position the dialog centered over that window.
		auto TestWin = [hDlg, &winrc, winwid, winht](D3DView *view)
		{
			// don't use this window if it's not open
			HWND hwndView = view->GetHWnd();
			HWND hwndPar = GetParent(hwndView);
			if (!IsWindow(hwndPar) || !IsWindowVisible(hwndPar) || IsIconic(hwndPar))
				return false;

			// don't use this window if it's rotated
			if (view->GetRotation() != 0)
				return false;

			// don't use this window if it's too small to contain the dialog
			RECT parrc;
			GetWindowRect(hwndPar, &parrc);
			int parwid = parrc.right - parrc.left, parht = parrc.bottom - parrc.top;
			if (parwid < winwid || parht < winht)
				return false;

			// looks good - center it over this window
			int left = parrc.left + (parwid - winwid)/2;
			int top = parrc.top + (parht - winht)/2;
			SetRect(&winrc, left, top, left + winwid, top + winht);
			return true;
		};
		
		// try each window in turn; if we don't find a suitable destination
		// window, simply leave the dialog at its default position
		if (!TestWin(GetPlayfieldView())
			&& !TestWin(GetBackglassView())
			&& !TestWin(GetDMDView())
			&& !TestWin(GetTopperView())
			&& !TestWin(GetInstCardView()))
			return;
	}

	// force the final location into view
	ForceRectIntoWorkArea(winrc, false);

	// set the location
	SetWindowPos(hDlg, NULL, winrc.left, winrc.top, -1, -1,
		SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
}

void Application::SaveDialogPos(HWND hDlg, const TCHAR *configVar)
{
	RECT rc;
	GetWindowRect(hDlg, &rc);
	ConfigManager::GetInstance()->Set(configVar, rc);
}

void Application::ShowWindow(FrameWin *win)
{
	// If the window is already visible and isn't minimized, check
	// if its current location is usably within a valid monitor.  If
	// the user is telling us to show a window that should already be
	// visible, it might be because the window is positioned somewhere
	// where the user can't see it.
	HWND hWnd = win->GetHWnd();
	if (IsWindowVisible(hWnd) && !IsIconic(hWnd))
	{
		// get the window layout
		RECT rc;
		GetWindowRect(hWnd, &rc);

		// make sure the window is at a usable minimum size
		bool repos = false;
		if (rc.right - rc.left < 200)
		{
			rc.right = rc.left + 200;
			repos = true;
		}
		if (rc.bottom - rc.top < 150)
		{
			rc.bottom = rc.top + 150;
			repos = true;
		}

		// check the window's location
		if (!IsWindowPosUsable(rc, 200, 100))
		{
			// force the window into the work area
			ForceRectIntoWorkArea(rc, false);
			repos = true;
		}

		// if we resized or moved the window, effect the changes
		if (repos)
			SetWindowPos(hWnd, NULL, 
				rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
				SWP_NOZORDER | SWP_NOACTIVATE);
	}
	else
	{
		// if the window is currently hidden, restore it to visibility
		win->ShowHideFrameWindow(true);

		// if it's minimized, restore it
		if (IsIconic(hWnd))
			SendMessage(hWnd, WM_SYSCOMMAND, SC_RESTORE, 0);
	}

	// make sure it's in front
	BringWindowToTop(hWnd);
}

void Application::EnumFrameWindows(std::function<void(FrameWin*)> func)
{
	if (playfieldWin != nullptr) func(playfieldWin);
	if (backglassWin != nullptr) func(backglassWin);
	if (dmdWin != nullptr) func(dmdWin);
	if (instCardWin != nullptr) func(instCardWin);
	if (topperWin != nullptr) func(topperWin);
}

void Application::CheckForegroundStatus()
{
	// if one of our main windows is active, we're in the foreground
	bool fg = (playfieldWin != nullptr && playfieldWin->IsNcActive())
		|| (backglassWin != nullptr && backglassWin->IsNcActive())
		|| (dmdWin != nullptr && dmdWin->IsNcActive())
		|| (instCardWin != nullptr && instCardWin->IsNcActive())
		|| (topperWin != nullptr && topperWin->IsNcActive());

	// check for a change
	if (fg != isInForeground)
	{
		// remember the new status
		isInForeground = fg;

		// notify the playfield view
		if (auto pfv = GetPlayfieldView(); pfv != 0)
			pfv->OnAppActivationChange(fg);
	}
}

void Application::OnActivateApp(BaseWin* /*win*/, bool activating, DWORD /*otherThreadId*/)
{
	// check for a change in status
	if (activating != isInForeground)
	{
		// remember the new status
		isInForeground = activating;

		// notify the UI windiows
		auto Visit = [activating](FrameWin *win)
		{
			if (win != nullptr)
				win->OnAppActivationChange(activating);
		};
		Visit(playfieldWin);
		Visit(backglassWin);
		Visit(dmdWin);
		Visit(topperWin);
		Visit(instCardWin);

		// if we're switching to the foreground, do some extra work
		if (activating)
		{
			// Launch a file scan thread, if one isn't already in progress.
			// This looks for new game files that were added since we last 
			// checked, so that we can dynamically incorporate newly 
			// downloaded games into the UI without having to restart the 
			// program.
			if (!IsNewFileScanRunning())
			{
				// Create and launch a new file scanner thread.  If the
				// launch succeeds, stash it in our thread pointer so that
				// we can check its progress later (as we just did above).
				RefPtr<NewFileScanThread> t(new NewFileScanThread());
				if (t->Launch())
					newFileScanThread = t;
			}
		}
	}
}

void Application::EnableSecondaryWindows(bool enabled)
{
	auto Visit = [enabled](BaseWin *win) 
	{
		if (win != nullptr)
		{
			HWND hwnd = win->GetHWnd();
			if (IsWindow(hwnd) && IsWindowVisible(hwnd))
				EnableWindow(hwnd, enabled);
		}
	};

	Visit(backglassWin);
	Visit(dmdWin);
	Visit(topperWin);
	Visit(instCardWin);
}

void Application::ClearMedia()
{
	if (auto pfv = GetPlayfieldView(); pfv != nullptr)
		pfv->ClearMedia();
	if (auto bgv = GetBackglassView(); bgv != nullptr)
		bgv->ClearMedia();
	if (auto dmv = GetDMDView(); dmv != nullptr)
		dmv->ClearMedia();
	if (auto tpv = GetTopperView(); tpv != nullptr)
		tpv->ClearMedia();
	if (auto ic = GetInstCardView(); ic != nullptr)
		ic->ClearMedia();
}

void Application::BeginRunningGameMode(GameListItem *game, GameSystem *system)
{
	// Assume we won't continue to play videos in the background
	playVideosInBackground = false;

	// Put the backglass, DMD, and topper windows into running-game mode.  
	// Note that it's not necessary to notify the playfield window, since 
	// it initiates this process.
	auto bgv = GetBackglassView();
	bool bgvideo = false, dmvideo = false, fpvideo = false, icvideo = false;
	if (bgv != nullptr)
		bgv->BeginRunningGameMode(game, system, bgvideo);
	if (auto dmv = GetDMDView(); dmv != nullptr)
		dmv->BeginRunningGameMode(game, system, dmvideo);
	if (auto tpv = GetTopperView(); tpv != nullptr)
		tpv->BeginRunningGameMode(game, system, fpvideo);
	if (auto ic = GetInstCardView(); ic != nullptr)
		ic->BeginRunningGameMode(game, system, icvideo);

	// note if any of the windows shows video in the background, so
	// that the message loop will know that we need full-speed updates
	playVideosInBackground = bgvideo || dmvideo || fpvideo || icvideo;

	// Now start the media sync process for the secondary windows, by
	// syncing the backglass window.  Each window will forward the
	// request to the next window in the chain after it finishes with
	// its own media loading.  Note that the secondary windows 
	// understand "current" to mean the running game when in running
	// game mode.
	if (bgv != nullptr)
		bgv->SyncCurrentGame();
}

void Application::EndRunningGameMode()
{
	// End running game mode in the backglass, DMD, and topper windows
	if (auto bgv = GetBackglassView(); bgv != nullptr)
		bgv->EndRunningGameMode();
	if (auto dmv = GetDMDView(); dmv != nullptr)
		dmv->EndRunningGameMode();
	if (auto tpv = GetTopperView(); tpv != nullptr)
		tpv->EndRunningGameMode();
	if (auto ic = GetInstCardView(); ic != nullptr)
		ic->EndRunningGameMode();

	// clear the videos-in-background flag, as we're no longer 
	// running a game
	playVideosInBackground = false;

	// Restore the saved pre-game window positions, in case Windows
	// repositioned any of our windows in response to monitor layout
	// changes.
	EnumFrameWindows([](FrameWin *win) { win->RestorePreRunPlacement(); });
}

bool Application::Launch(int cmd, DWORD launchFlags,
	GameListItem *game, GameSystem *system,
	const std::list<LaunchCaptureItem> *captureList, int captureStartupDelay,
	ErrorHandler &eh)
{
	// prepare a new game monitor object
	RefPtr<GameMonitorThread> mon(new GameMonitorThread());
	mon->Prepare(cmd, launchFlags, game, system, captureList, captureStartupDelay);

	// launch it
	return Launch(mon, eh);
}

bool Application::Launch(GameMonitorThread *mon, ErrorHandler &eh)
{
	// if there's already a game monitor thread, shut it down
	if (gameMonitor != nullptr)
	{
		// shut it down
		gameMonitor->Shutdown(eh, 500, false);

		// forget it
		gameMonitor = nullptr;
	}

	// save the pre-run window position for each frame window
	EnumFrameWindows([](FrameWin *win) { win->SavePreRunPlacement(); });

	// make the new one current
	gameMonitor = mon;

	// launch it
	return mon->Launch(eh);
}

bool Application::LaunchNextQueuedGame(ErrorHandler &eh)
{
	// if there's nothing in the queue, we're done
	if (queuedLaunches.size() == 0)
		return false;

	// take the next item off the queue
	RefPtr<GameMonitorThread> mon(queuedLaunches.front().Detach());
	queuedLaunches.pop_front();

	// launch it
	return Launch(mon, eh);
}

void Application::QueueLaunch(int cmd, DWORD launchFlags,
	GameListItem *game, GameSystem *system,
	const std::list<LaunchCaptureItem> *captureList, int captureStartupDelay,
	const BatchCaptureInfo *bci)
{
	// prepare a new game monitor object
	RefPtr<GameMonitorThread> mon(new GameMonitorThread());
	mon->Prepare(cmd, launchFlags, game, system, captureList, captureStartupDelay, bci);

	// queue it, handing over our reference count to the list
	queuedLaunches.emplace_back(mon.Detach());
}

bool Application::GetNextQueuedGame(QueuedGameInfo &info) const
{
	// if the queue is empty, there's no game
	if (queuedLaunches.size() == 0)
		return false;

	// get the next item
	auto &q = queuedLaunches.front();

	// return the info struct
	info = { q->cmd, q->gameId, q->gameSys.configIndex };
	return true;
}

void Application::SetNextQueuedGameOverride(const CHAR *prop, const TSTRING &val)
{
	// if a game is available, add the property to its override map
	if (queuedLaunches.size() != 0)
		queuedLaunches.front()->overrides.emplace(prop, val);
}

void Application::RemoveNextQueuedGame()
{
	queuedLaunches.pop_front();
}

TSTRING Application::ExpandGameSysVars(TSTRING &str, GameSystem *system, GameListItem *game)
{
	// set up a dummy monitor object
	RefPtr<GameMonitorThread> mon(new GameMonitorThread());
	mon->Prepare(ID_PLAY_GAME, LaunchFlags::StdPlayFlags, game, system, nullptr, 0);

	// resolve the game file
	TCHAR gameFileWithPath[MAX_PATH];
	mon->GetGameFileWithPath(gameFileWithPath);
	mon->ResolveGameFile(gameFileWithPath, false);

	// apply the substitutions
	return mon->SubstituteVars(str);
}

void Application::KillGame()
{
	// make sure the process is still running
	if (gameMonitor != nullptr)
		gameMonitor->CloseGame();
}

void Application::ResumeGame()
{
	// make sure the process is still running
	if (gameMonitor != nullptr)
		gameMonitor->BringToForeground();
}

void Application::StealFocusFromGame()
{
	if (gameMonitor != nullptr)
	{
		HWND hwnd = GetPlayfieldWin()->GetHWnd();
		if (gameMonitor->IsAdminMode() && adminHost.IsAvailable())
		{
			// admin mode - we have to proxy this through the admin host
			TCHAR hwndAsStr[32];
			_stprintf_s(hwndAsStr, _T("%ld"), (long)(INT_PTR)hwnd);
			const TCHAR *req[] = { _T("stealFocus"), hwndAsStr };
			adminHost.PostRequest(req, countof(req));
		}
		else
		{
			// it's not in admin mode - we should be able to take focus directly
			gameMonitor->StealFocusFromGame(hwnd);
		}
	}
}

void Application::ManualCaptureGo()
{
	if (gameMonitor != nullptr)
		gameMonitor->ManualCaptureGo();
}

void Application::BatchCaptureCancelPrompt(bool show)
{
	if (gameMonitor != nullptr)
	{
		if (CaptureStatusWin *statusWin = gameMonitor->capture.statusWin; statusWin != nullptr)
			statusWin->BatchCaptureCancelPrompt(show);
	}
}

void Application::ShowCaptureCancel()
{
	if (gameMonitor != nullptr)
	{
		if (CaptureStatusWin *statusWin = gameMonitor->capture.statusWin; statusWin != nullptr)
			statusWin->ShowCaptureCancel();
	}
}

void Application::CleanGameMonitor()
{
	// if the game monitor thread has exited, remove our reference
	if (gameMonitor != nullptr && !gameMonitor->IsThreadRunning())
		gameMonitor = nullptr;
}

void Application::EnableVideos(bool enable)
{
	// update the status if it's changing
	if (enable != enableVideos)
	{
		// remember the new setting
		enableVideos = enable;

		// save it in the config
		ConfigManager::GetInstance()->SetBool(ConfigVars::EnableVideos, enable);

		// update the status for current sprites
		UpdateEnableVideos();
	}
}

void Application::UpdateEnableVideos()
{
	// update each window that hosts videos
	GetPlayfieldView()->OnEnableVideos(enableVideos);
	GetBackglassView()->OnEnableVideos(enableVideos);
	GetDMDView()->OnEnableVideos(enableVideos);
	GetTopperView()->OnEnableVideos(enableVideos);
}

void Application::MuteVideos(bool mute)
{
	// update the status if it's changing
	if (mute != muteVideos)
	{
		// remember the new setting
		muteVideos = mute;

		// save it in the config
		ConfigManager::GetInstance()->SetBool(ConfigVars::MuteVideos, mute);

		// update the muting status for running videos
		UpdateVideoVolume();
	}
}

void Application::SetVideoVolume(int pctVol)
{
	// update playing videos if it's changing
	if (pctVol != videoVolume)
	{
		// remember the new setting
		videoVolume = pctVol;

		// save it in the config
		ConfigManager::GetInstance()->Set(ConfigVars::VideoVolume, pctVol);

		// update the volume for running videos
		UpdateVideoVolume();
	}
}

void Application::MuteTableAudio(bool mute)
{
	// update the status if it's changing
	if (mute != muteTableAudio)
	{
		// remember the new setting
		muteTableAudio = mute;

		// save it in the config
		ConfigManager::GetInstance()->SetBool(ConfigVars::MuteTableAudio, mute);

		// update muting status in the playfield
		if (auto pfv = GetPlayfieldView(); pfv != nullptr)
			pfv->MuteTableAudio(mute);
	}
}

void Application::MuteAttractMode(bool mute)
{
	// update the setting if it's changing
	if (muteAttractMode != mute)
	{
		// remember the new setting, locally and in the config file
		muteAttractMode = mute;
		ConfigManager::GetInstance()->SetBool(ConfigVars::MuteAttractMode, mute);

		// update the muting status for running videos
		UpdateVideoVolume();
	}
}

void Application::UpdateVideoVolume()
{
	// get the active muting status
	bool mute = IsMuteVideosNow();
	int vol = videoVolume;

	// update any playing videos in the windows that host them
	auto Update = [mute, vol](D3DView *view)
	{
		auto callback = [mute, vol](Sprite *sprite)
		{
			if (auto video = dynamic_cast<VideoSprite*>(sprite); video != nullptr)
			{
				if (auto player = video->GetVideoPlayer(); player != nullptr)
				{
					player->Mute(mute);
					if (!vol)
						player->SetVolume(vol);
				}
			}
		};
		if (view != nullptr)
			view->ForDrawingList(callback);
	};
	Update(GetPlayfieldView());
	Update(GetBackglassView());
	Update(GetDMDView());
	Update(GetTopperView());
}

bool Application::IsMuteVideosNow() const
{
	// Start with the global muting status
	bool mute = muteVideos;

	// If Attract Mode is active, and attract mode is set to mute, apply
	// muting even if muting isn't normally in effect.
	if (auto pfv = GetPlayfieldView(); pfv != nullptr && pfv->IsAttractMode() && muteAttractMode)
		mute = true;

	// return the result
	return mute;
}

bool Application::UpdatePinscapeDeviceList()
{
	// update the device list
	PinscapeDevice::FindDevices(pinscapeDevices);

	// indicate whether or not any devices were found
	return pinscapeDevices.begin() != pinscapeDevices.end();
}

bool Application::GetPinscapeNightMode(bool &nightMode)
{
	// presume we're not in night mode
	nightMode = false;

	// scan the devices to see if any are in night mode
	for (auto it = pinscapeDevices.begin(); it != pinscapeDevices.end(); ++it)
	{
		// check night mode on this device
		if (it->IsNightMode())
		{
			// indicate that night mode is active
			nightMode = true;
			return true;
		}
	}
	
	// return true if there are any devices
	return pinscapeDevices.begin() != pinscapeDevices.end();
}

void Application::SetPinscapeNightMode(bool nightMode)
{
	// set the new mode in all attached devices
	for (auto &d : pinscapeDevices)
		d.SetNightMode(nightMode);
}

void Application::TogglePinscapeNightMode()
{
	bool nightMode;
	if (GetPinscapeNightMode(nightMode))
		SetPinscapeNightMode(!nightMode);
}

void Application::SendPinVol(const WCHAR *fmt, ...)
{
	// If we don't have a mail slot handle yet, try creating one.  Note that
	// we repeat this each time we want to send a message, since PinVol could
	// be newly started at any time while we're running.
	if (pinVolMailSlot == NULL || pinVolMailSlot == INVALID_HANDLE_VALUE)
		pinVolMailSlot = CreateFile(_T("\\\\.\\mailslot\\Pinscape.PinVol"),
			GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	// if we have a mail slot, try sending the message
	if (pinVolMailSlot != INVALID_HANDLE_VALUE)
	{ 
		// Prepare the message: "game <filename>|<title>", in WCHAR
		// (16-bit unicode) characters.
		va_list ap;
		va_start(ap, fmt);
		WSTRINGEx msg;
		msg.FormatV(fmt, ap);
		va_end(ap);

		// Write the message to the mailslot.  If the write fails, close
		// the mail slot and retry - the old server might have shut down
		// and a new one might have started, in which case we'll need to
		// reopen the handle.
		for (int tries = 0; tries < 2; ++tries)
		{
			DWORD actual;
			if (!WriteFile(pinVolMailSlot, msg.c_str(), (DWORD)(msg.length() * sizeof(WCHAR)), &actual, NULL))
			{
				// try re-opening the slot
				pinVolMailSlot = CreateFile(_T("\\\\.\\mailslot\\Pinscape.PinVol"),
					GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

				// if that failed, there's no need to retry the write
				if (pinVolMailSlot == INVALID_HANDLE_VALUE)
					break;
			}
		}
	}
}

// -----------------------------------------------------------------------
//
// Game monitor thread
//

Application::GameMonitorThread::GameMonitorThread() :
	isAdminMode(false),
	hideTaskbar(false),
	closedGameProc(false)
{
	// create the manual start/stop, shutdown, and close-game event objects
	startStopEvent = CreateEvent(0, TRUE, FALSE, 0);
	shutdownEvent = CreateEvent(0, TRUE, FALSE, 0);
	closeEvent = CreateEvent(0, TRUE, FALSE, 0);

	// keep references to the game views
	playfieldView = Application::Get()->GetPlayfieldView();
	backglassView = Application::Get()->GetBackglassView();
	dmdView = Application::Get()->GetDMDView();
	topperView = Application::Get()->GetTopperView();
	instCardView = Application::Get()->GetInstCardView();
}

Application::GameMonitorThread::~GameMonitorThread()
{
}

bool Application::GameMonitorThread::IsThreadRunning()
{
	return hThread != NULL && WaitForSingleObject(hThread, 0) == WAIT_TIMEOUT;
}

bool Application::GameMonitorThread::IsGameRunning() const
{
	return hGameProc != NULL && WaitForSingleObject(hGameProc, 0) == WAIT_TIMEOUT;
}

void Application::GameMonitorThread::CloseGame()
{
	// if the game is running, close its windows
	if (IsGameRunning())
	{
		// flag that we've tried closing the game
		closedGameProc = true;

		// check for admin mode
		if (isAdminMode)
		{
			static const TCHAR *const request[] = { _T("killgame") };
			Application::Get()->PostAdminHostRequest(request, countof(request));
		}
		else
		{
			// Normal launch - we can do the close ourselves

			// Try bringing our main window to the foreground before 
			// closing the game program's window(s), so that the taskbar 
			// doesn't reappear between closing the game and activating
			// our window, assuming we're in full-screen mode.  Explorer 
			// normally hides the taskbar when a full-screen window is
			// in front, but only when it's in front.
			if (auto pfw = Application::Get()->GetPlayfieldWin(); pfw != nullptr)
			{
				// inject a call to the child process to set our window
				// as the foreground
				DWORD tid;
				HandleHolder hRemoteThread = CreateRemoteThread(
					hGameProc, NULL, 0,
					(LPTHREAD_START_ROUTINE)&SetForegroundWindow, pfw->GetHWnd(),
					0, &tid);

				// explicitly set our foreground window
				SetForegroundWindow(pfw->GetHWnd());
			}

			// Check the termination mode
			if (_tcsicmp(GetLaunchParam("terminateBy", gameSys.terminateBy).c_str(), _T("KillProcess")) == 0)
			{
				// KillProcess mode.  Don't try to close windows; just terminate
				// the process by fiat.
				TerminateProcess(hGameProc, 0);
			}
			else
			{
				// Close Window mode, or anything else (this is the default,
				// which we'll use if there's no setting or some other setting
				// we don't recognize).
				//
				// Try closing one game window at a time.  Repeat until we
				// don't find any windows to close, or we reach a maximum
				// retry limit (so that we don't get stuck if the game 
				// refuses to close).
				for (int tries = 0; tries < 20; ++tries)
				{
					// look for a window to close
					struct CloseContext
					{
						std::list<HWND> windows;
					} closeCtx;
					EnumThreadWindows(tidMainGameThread, [](HWND hWnd, LPARAM lParam)
					{
						// get the context
						auto ctx = reinterpret_cast<CloseContext*>(lParam);

						// Only include windows that are visible and enabled.  VP will
						// crash if we try to close disabled windows while a dialog is
						// showing.
						if (IsWindowVisible(hWnd) && IsWindowEnabled(hWnd))
							ctx->windows.push_back(hWnd);

						// continue the enumeration
						return TRUE;
					}, reinterpret_cast<LPARAM>(&closeCtx));

					// if we didn't find any windows to close, stop trying to
					// close windows
					if (closeCtx.windows.size() == 0)
						break;

					// try closing each window we found
					for (auto hWnd : closeCtx.windows)
					{
						// try closing this window
						SendMessage(hWnd, WM_SYSCOMMAND, SC_CLOSE, 0);

						// If it's still there, send it an ordinary WM_CLOSE as well.
						// Some windows don't response to SC_CLOSE.
						if (IsWindow(hWnd) && IsWindowVisible(hWnd) && IsWindowEnabled(hWnd))
							SendMessage(hWnd, WM_CLOSE, 0, 0);
					}

					// pause briefly between iterations to give the program a chance
					// to update its windows; stop if the process exits
					if (hGameProc == NULL || WaitForSingleObject(hGameProc, 100) != WAIT_TIMEOUT)
						break;
				}
			}

			// If the game is still running, resort to stronger measures:
			// attempt to kill it at the process level.  It's not unheard
			// of for VP to crash, which makes it futile to try to kill it
			// by closing windows, and The Pinball Arcade seems very prone
			// to going into an unresponsive state rather than terminating
			// when we close its window.
			if (hGameProc != NULL && WaitForSingleObject(hGameProc, 0) == WAIT_TIMEOUT)
				TerminateProcess(hGameProc, 0);
		}
	}

	// signal the close-game event to the monitor thread
	SetCloseEvent();
}

void Application::GameMonitorThread::BringToForeground()
{
	if (IsGameRunning())
	{
		// find the other app's first window
		struct context {
			context(DWORD pid, DWORD tid) : pid(pid), tid(tid) { }
			DWORD pid, tid;
			HWND hwnd = NULL;
		} ctx(pid, tidMainGameThread);
		EnumThreadWindows(tidMainGameThread, [](HWND hwnd, LPARAM lparam)
		{
			// only consider visible windows with no owner
			if (IsWindowVisible(hwnd) && GetWindowOwner(hwnd) == NULL)
			{
				// remember this window and stop the enumeration
				reinterpret_cast<context*>(lparam)->hwnd = hwnd;
				return FALSE;
			}

			// continue the enumeration otherwise
			return TRUE;
		}, reinterpret_cast<LPARAM>(&ctx));

		// If we didn't find a window for the main thread, try again,
		// looking for any top-level window belonging to the process.  
		// EnumThreadWindows() won't find the console window for a
		// console-mode application, for example.
		if (ctx.hwnd == NULL)
		{
			EnumWindows([](HWND hwnd, LPARAM lparam)
			{
				// only consider visible windows with no owner
				if (IsWindowVisible(hwnd) && GetWindowOwner(hwnd) == NULL)
				{
					// get the process information for the window
					DWORD tid, pid;
					tid = GetWindowThreadProcessId(hwnd, &pid);

					// check if it matches our process and/or thread ID
					auto ctx = reinterpret_cast<context*>(lparam);
					if (pid == ctx->pid)
					{
						// provisionally set this window as a match
						ctx->hwnd = hwnd;

						// If it's on the process's main thread, accept it as
						// as the winner and stop the enumeration.  If it's 
						// not on the main thread, continue the enumeration,
						// in case we find a more likely window.  In most
						// applications, the UI is on the main thread, so
						// this is usually the best bet for the main window.
						if (tid == ctx->tid)
							return FALSE;
					}
				}

				// continue the enumeration otherwise
				return TRUE;
			}, reinterpret_cast<LPARAM>(&ctx));
		}

		// if we found a window, bring it to the front
		if (ctx.hwnd != NULL)
			BringWindowToTop(ctx.hwnd);
	}
}

bool Application::GameMonitorThread::Launch(
	int cmd, DWORD launchFlags, GameListItem *game, GameSystem *system,
	const std::list<LaunchCaptureItem> *captureList, int captureStartupDelay,
	ErrorHandler &eh)
{
	// prepare the object
	Prepare(cmd, launchFlags, game, system, captureList, captureStartupDelay, nullptr);

	// do the launch
	return Launch(eh);
}

void Application::GameMonitorThread::Prepare(
	int cmd, DWORD launchFlags, GameListItem *game,
	GameSystem *system,
	const std::list<LaunchCaptureItem> *captureList, int captureStartupDelay,
	const BatchCaptureInfo *bci)
{
	// save the game information
	this->cmd = cmd;
	this->launchFlags = launchFlags;
	this->game = *game;
	this->gameId = game->internalID;
	this->gameSys = *system;
	this->elevationApproved = system->elevationApproved;

	// if we're in a batch capture, save the batch info
	if (bci != nullptr)
		this->batchCaptureInfo = *bci;

	// initially assume the game filename is the full name
	this->gameFileWithExt = game->filename;

	// get config settings needed during the launch
	auto cfg = ConfigManager::GetInstance();
	this->hideTaskbar = cfg->GetBool(ConfigVars::HideTaskbarDuringGame, true);
	this->gameInactivityTimeout.Format(_T("%ld"), cfg->GetInt(ConfigVars::GameTimeout, 0) * 1000);

	// log the launch start
	LogFile::Get()->Group(LogFile::TableLaunchLogging);
	LogFile::Get()->Write(LogFile::TableLaunchLogging,
		_T("Table launch: %s, table file %s, system %s\n"),
		game->title.c_str(), game->filename.c_str(), system->displayName.c_str());

	// If the launch is for the sake of capturing screenshots of the
	// running game, pre-figure the capture details for all of the
	// requested capture items.  We store all of the details in the
	// monitor object so that the background thread doesn't have to
	// access any outside objects to do the captures, thus avoiding
	// the need for any cross-thread synchronization for the game
	// list item or windows.
	if ((launchFlags & LaunchFlags::Capturing) != 0 && captureList != nullptr)
	{
		// Keep a running total of the capture time as we go.  Start
		// with some fixed overhead for our own initialization.
		const DWORD initTime = 3000;
		capture.totalTime = initTime;

		// remember the startup delay
		auto cfg = ConfigManager::GetInstance();
		capture.startupDelay = captureStartupDelay * 1000;
		capture.totalTime += capture.startupDelay;

		// remember the two-pass encoding option
		capture.twoPassEncoding = cfg->GetBool(ConfigVars::CaptureTwoPassEncoding, false);

		// build our local list of capture items
		bool audioNeeded = false;
		for (auto &cap : *captureList)
		{
			// create a capture item in our local list
			auto &item = capture.items.emplace_back(cap.mediaType, cap.videoWithAudio);

			// get the media file name - use "for capture" mode, since
			// we just want the default name, and don't need to search
			// for an existing file
			game->GetMediaItem(item.filename, item.mediaType, true);

			// set the capture time, if specified, converting to milliseconds
			if (auto cfgvar = item.mediaType.captureTimeConfigVar; cfgvar != nullptr)
				item.captureTime = cfg->GetInt(cfgvar, 30) * 1000;

			// set the manual start/stop modes
			if (auto cfgvar = item.mediaType.captureStartConfigVar; cfgvar != nullptr)
				item.manualStart = _tcsicmp(cfg->Get(cfgvar, _T("auto")), _T("manual")) == 0;
			if (auto cfgvar = item.mediaType.captureStopConfigVar; cfgvar != nullptr)
				item.manualStop = _tcsicmp(cfg->Get(cfgvar, _T("auto")), _T("manual")) == 0;

			// Add it to the total time, plus a couple of seconds of overhead 
			// for launching ffmpeg.  Note that there's no way to guess how long
			// the capture will actually run if we're in manual stop mode, so
			// we can only make a wild guess, but we'll still use the configured
			// fixed capture time (as our wild guess, in this case), since that
			// should at least be on the right order of magnitude.
			capture.totalTime += item.captureTime + 2000;

			// If we're doing two-pass encoding, add an estimate of the second
			// pass encoding time.  This option is normally used only on a machine
			// that can't keep up with real-time encoding, so it's a good bet that
			// the encoding time will exceed the capture time - by how much, though,
			// is pretty much impossible to estimate without more knowledge of the
			// local machine than we can be bothered to gather.  So we'll just make
			// a wild guess.  It's hard to run VP successfully on *too* slow a 
			// machine; the slowest machines capable of good VP operation are
			// probably only borderline too slow for real-time encoding, so let's
			// assume that a factor of two (times the video running time) is a 
			// decent upper bound.  And of course we've already established that 
			// a factor of one is a good lower bound if we're using this mode.
			// So let's just split the difference and call it 1.5x.
			if (capture.twoPassEncoding
				&& (item.mediaType.format == MediaType::Format::SilentVideo
					|| item.mediaType.format == MediaType::Format::VideoWithAudio))
				capture.totalTime += item.captureTime * 3 / 2;

			// get the source window's rotation
			item.windowRotation = cap.win->GetRotation();
			item.windowMirrorVert = cap.win->IsMirrorVert();
			item.windowMirrorHorz = cap.win->IsMirrorHorz();

			// remember the desired rotation for the stored image
			item.mediaRotation = cap.mediaType.rotation;

			// get the client area of the view window, adjusted to
			// screen coordinates
			HWND hwndView = cap.win->GetHWnd();
			GetClientRect(hwndView, &item.rc);
			POINT pt = { 0, 0 };
			ClientToScreen(hwndView, &pt);
			OffsetRect(&item.rc, pt.x, pt.y);

			// note if audio is required
			if ((item.mediaType.format == MediaType::VideoWithAudio && item.enableAudio)
				|| item.mediaType.format == MediaType::Audio)
				audioNeeded = true;
		}

		// If audio is required, figure the audio device
		if (audioNeeded)
		{
			// start with the config setting
			audioCaptureDevice = cfg->Get(ConfigVars::CaptureAudioDevice, _T(""));

			// if there's no config setting, search for a "Stereo Mix" device by default
			if (audioCaptureDevice.length() == 0)
			{
				// friendly name pattern we're scanning for
				static const std::basic_regex<WCHAR> stmixPat(L"\\bstereo mix\\b", std::regex_constants::icase);

				// search for a device with a name containing "stereo mix"
				EnumDirectShowAudioInputDevices([this](const AudioCaptureDeviceInfo *info)
				{
					// check if the name matches our pattern
					if (std::regex_search(info->friendlyName, stmixPat))
					{
						// use this source
						audioCaptureDevice = info->friendlyName;

						// stop searching
						return false;
					}

					// not a match - keep looking
					return true;
				});
			}
		}
	}
}

bool Application::GameMonitorThread::Launch(ErrorHandler &eh)
{
	// check if we're in capture mode
	if ((launchFlags & LaunchFlags::Capturing) != 0 && capture.items.size() != 0)
	{
		// create the status window
		capture.statusWin.Attach(new CaptureStatusWin());
		capture.statusWin->Create(NULL, _T("PinballY"), WS_POPUP, SW_SHOWNOACTIVATE);
		capture.statusWin->SetTotalTime(capture.totalTime);
		capture.statusWin->SetBatchInfo(batchCaptureInfo.nCurGame, batchCaptureInfo.nGames, 
			batchCaptureInfo.remainingTime*1000, batchCaptureInfo.totalTime*1000);
		capture.statusWin->SetCaptureStatus(LoadStringT(IDS_CAPSTAT_INITING), CaptureInfo::initTime);
	}

	// If PinVol is running, send it a message on its mailslot with the
	// game file and title.  This lets it show the game's real title in
	// its on-screen display text, rather than just the game's filename.
	// PinVol infers which game is running from the window title of the 
	// foreground app, and the apps usually only include the filename
	// there.
	Application::Get()->SendPinVol(L"game %s|%s",
		TSTRINGToWSTRING(gameFileWithExt).c_str(),
		TSTRINGToWSTRING(game.title).c_str());

	// Add a reference to myself on behalf of the thread.  This will 
	// keep the object alive as long as the thread is running.
	AddRef();

	// launch the game monitor thread
	hThread = CreateThread(0, 0, &SMain, this, 0, 0);
	if (hThread == 0)
	{
		// flag the error
		WindowsErrorMessage sysErr;
		LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ failed to create monitor thread: %s\n"), sysErr.Get());
		eh.SysError(LoadStringT(IDS_ERR_LAUNCHGAME), MsgFmt(_T("Monitor thread creation failed: %s"), sysErr.Get()));
		
		// remove the thread's reference, since there's no thread
		Release();

		// return failure
		return false;
	}

	// Look up the game object by its internal ID.  (We do it this way
	// rather than by storing a pointer to the game to ensure that the
	// pointer doesn't go stale between the time the monitor object
	// was prepared and the time it was launched.)
	GameList *gl = GameList::Get();
	GameListItem *pgame = gl->GetByInternalID(this->gameId);
	if (pgame != nullptr)
	{
		// If desired, update the game's last launch time and play count.  
		// (This flag is usually only set for regular play sessions, not
		// media capture launches.)
		if ((launchFlags & LaunchFlags::UpdateStats) != 0)
		{
			gl->SetLastPlayedNow(pgame);
			gl->SetPlayCount(pgame, gl->GetPlayCount(pgame) + 1);
		}
	}

	// success - the monitor thread will take it from here
	return true;
}

DWORD WINAPI Application::GameMonitorThread::SMain(LPVOID lpParam)
{
	// the parameter is the 'this' object
	auto self = (GameMonitorThread *)lpParam;

	// invoke the member function for the main thread entrypoint
	DWORD result = self->Main();

	// Regardless of how we exited, tell the main window that the game
	// monitor thread is exiting.
	if (self->playfieldView != 0)
	{
		PlayfieldView::LaunchReport report(self->cmd, self->launchFlags, self->gameId, self->gameSys.configIndex);
		self->playfieldView->SendMessage(PFVMsgLaunchThreadExit, 0, reinterpret_cast<LPARAM>(&report));
	}

	// The caller (in the main thread) adds a reference to the 'this'
	// object on behalf of the thread, to ensure that the object can't
	// be deleted as long as the thread is running.  Now that the
	// thread is just about to exit, release our reference.
	self->Release();

	// return the exit code from the main thread handler
	return result;
}

TSTRING Application::GameMonitorThread::SubstituteVars(const TSTRING &str)
{
	static const std::basic_regex<TCHAR> pat(_T("\\[(\\w+)\\]"));
	return regex_replace(str, pat, [this](const std::match_results<TSTRING::const_iterator> &m) -> TSTRING
	{
		// get the variable name in all caps
		TSTRING var = m[1].str();
		std::transform(var.begin(), var.end(), var.begin(), ::_totupper);

		// check for known substitution variable names
		if (var == _T("TABLEPATH"))
		{
			// the table path
			return gameSys.tablePath;
		}
		else if (var == _T("TABLEFILE"))
		{
			// the table file, resolved to an existing file
			return gameFileWithExt;
		}
		else if (var == _T("TABLEFILEBASE"))
		{
			// the resolved table file, stripped of its extension
			static const std::basic_regex<TCHAR> extPat(_T("\\.[^.\\\\]+$"));
			return std::regex_replace(gameFileWithExt, extPat, _T(""));
		}
		else if (var == _T("TABLEFILEORIG"))
		{
			// the original table file, exactly as it appears in the database
			return game.filename;
		}
		else if (var == _T("PINBALLY"))
		{
			// the PinballY progrma folder
			TCHAR exePath[MAX_PATH];
			GetExeFilePath(exePath, countof(exePath));
			return exePath;
		}
		else if (var == _T("LB"))
		{
			// a literal "[" (left bracket)
			return _T("[");
		}
		else if (var == _T("RB"))
		{
			// a literal "]" (right bracket)
			return _T("]");
		}
		else
		{
			// not matched - return the full original string unchanged
			return m[0].str();
		}
	});
}

void Application::GameMonitorThread::ManualCaptureGo()
{
	// set the manual capture event to let the capture thread know it
	// can proceed with the next step or finish the current step
	SetEvent(startStopEvent);
}

void Application::GameMonitorThread::ResolveGameFile(TCHAR gameFileWithPath[MAX_PATH], bool logging)
{
	// Check if the file exists.  If not, try adding the default extension.
	if (!FileExists(gameFileWithPath) && gameSys.defExt.length() != 0)
	{
		// The file doesn't exist.  Try adding the default extension.
		TCHAR gameFileWithPathExt[MAX_PATH];
		_stprintf_s(gameFileWithPathExt, _T("%s%s"), gameFileWithPath, gameSys.defExt.c_str());

		// log the attempt
		if (logging)
			LogFile::Get()->Write(LogFile::TableLaunchLogging,
				_T("+ table launch: table file %s doesn't exist; try adding extension -> %s\n"),
				gameFileWithPath, gameFileWithPathExt);

		// if the file + extension exists, use that instead of the original
		if (FileExists(gameFileWithPathExt))
		{
			// log it
			if (logging)
				LogFile::Get()->Write(LogFile::TableLaunchLogging,
					_T("+ table launch: file + extension (%s) exists, using it\n"), gameFileWithPathExt);

			// use the path + extension version, and also add the extension
			// to the base game file name
			_tcscpy_s(gameFileWithPath, MAX_PATH, gameFileWithPathExt);
			gameFileWithExt.append(gameSys.defExt);
		}
		else
		{
			// log that neither file exists
			if (logging)
				LogFile::Get()->Write(LogFile::TableLaunchLogging,
					_T("+ table launch: file + extension (%s) doesn't exist either; sticking with original name (%s)\n"),
					gameFileWithPathExt, gameFileWithPath);
		}
	}
}

void Application::GameMonitorThread::GetGameFileWithPath(TCHAR gameFileWithPath[MAX_PATH])
{
	if (PathIsRelative(gameFileWithExt.c_str()))
		PathCombine(gameFileWithPath, gameSys.tablePath.c_str(), gameFileWithExt.c_str());
	else
		_tcscpy_s(gameFileWithPath, MAX_PATH, gameFileWithExt.c_str());
}

const TSTRING &Application::GameMonitorThread::GetLaunchParam(const CHAR *propname, const TSTRING &defaultVal)
{
	if (auto it = overrides.find(propname); it != overrides.end())
		return it->second;
	else
		return defaultVal;
}

int Application::GameMonitorThread::GetLaunchParamInt(const CHAR *propname, int defaultVal)
{
	if (auto it = overrides.find(propname); it != overrides.end())
		return _ttoi(it->second.c_str());
	else
		return defaultVal;
}

DWORD Application::GameMonitorThread::Main()
{
	// Get the game filename from the database, and build the full path
	// (unless the filename already has an absolute path).
	TCHAR gameFileWithPath[MAX_PATH];
	GetGameFileWithPath(gameFileWithPath);
	LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ launch: full table path %s\n"), gameFileWithPath);

	// Get the centerpoint of the various windows.  If we need to
	// send a synthesized mouse click targeted to a specific window, 
	// this will give us the location of the click.
	auto WinPt = [](BaseWin *win, int x, int y)
	{
		POINT pt = { x, y };
		if (win != nullptr && IsWindowVisible(win->GetHWnd()) && !IsIconic(win->GetHWnd()))
		{
			RECT rc;
			GetWindowRect(win->GetHWnd(), &rc);
			pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
		}
		return pt;
	};
	POINT ptPlayfieldCenter = WinPt(Application::Get()->GetPlayfieldWin(), 810, 540);
	POINT ptBackglassCenter = WinPt(Application::Get()->GetBackglassWin(), 950, 540);
	POINT ptDMDCenter = WinPt(Application::Get()->GetDMDWin(), 320, 650);
	POINT ptTopperCenter = WinPt(Application::Get()->GetTopperWin(), 950, 650);

	// Set up a rotation manager on the stack, so that it'll automatically
	// undo any outstanding rotations when we return.
	RotationManager rotationManager(this);

	// RunBefore/RunAfter option flag parser
	class RunBeforeAfterParser
	{
	public:
		RunBeforeAfterParser(GameMonitorThread *monitor, RotationManager &rotationManager,
			const TCHAR *desc, int launchErrorId, const TSTRING &command, bool continueAfterClose) :
			monitor(monitor),
			rotationManager(rotationManager),
			desc(desc),
			launchErrorId(launchErrorId),
			returnStatusOnClose(continueAfterClose),
			nowait(false),
			terminate(false),
			hide(false),
			minimize(false),
			admin(false),
			executed(false),
			canceled(false),
			pid(0)
		{
			static const std::basic_regex<TCHAR> flagsPat(_T("\\s*\\[([^\\]]+)\\]\\s*(.*)"));
			std::match_results<TSTRING::const_iterator> m;
			if (std::regex_match(command, m, flagsPat))
			{
				// extract the flags
				const TSTRING &flags = m[1].str();

				// Pull out the actual command string, stripped of the option flags
				this->command = m[2].str();

				// match the individual flags
				const TCHAR *start = flags.c_str();
				for (const TCHAR *p = start; ; )
				{
					// skip spaces
					for (; _istspace(*p); ++p);
					if (*p == 0)
						break;

					// find the end of this segment
					for (start = p; *p != 0 && !_istspace(*p); ++p);

					// extract the token and convert to lower-case
					TSTRING tok(start, p - start);
					std::transform(tok.begin(), tok.end(), tok.begin(), ::_totlower);

					// match this token
					static const std::basic_regex<TCHAR> rotatePat(_T("rotate\\((\\w+,\\d+)\\)"));
					std::match_results<TSTRING::const_iterator> m2;
					if (tok == _T("nowait"))
						nowait = true;
					else if (tok == _T("terminate"))
						terminate = true;
					else if (tok == _T("hide"))
						hide = true;
					else if (tok == _T("min") || tok == _T("minimize"))
						minimize = true;
					else if (std::regex_match(tok, m2, rotatePat))
						rotate.emplace_back(m2[1].str());
					else if (tok == _T("admin"))
						admin = true;
					else
					{
						// invalid token - append to the invalid token list
						if (invalOptTok.length() != 0) invalOptTok += _T(" ");
						invalOptTok += tok;
					}
				}
			}
			else
			{
				// no flags - use the command string as-is
				this->command = command;
			}
		}

		~RunBeforeAfterParser()
		{
			// if we haven't executed the command yet, do so now
			if (!executed && !canceled)
				Run();

			// If we have a process handle at this point, it means that we're
			// in NOWAIT TERMINATE mode.  The time has come for that TERMINATE
			// bit.
			if (hProc != NULL && WaitForSingleObject(hProc, 0) == WAIT_TIMEOUT)
			{
				// if we're in Admin mode, clean up the process via the admin host
				if (admin)
				{
					MsgFmt spid(_T("%ld"), pid);
					const TCHAR *req[] = {
						_T("killpid"),
						spid.Get()
					};
					Application::Get()->PostAdminHostRequest(req, countof(req));
				}
				else
				{
					// normal user mode - kill the process 
					SaferTerminateProcess(hProc);
				}
			}
		}

		// Cancel the command.  Once the command object is instantiated, it's
		// guaranteed to execute, UNLESS it's explicitly canceled.  The guaranteed
		// execution occurs when the object is destroyed, if an explicit call to
		// Run() wasn't made earlier.  This is designed so that the caller has
		// control over the timing of the execution when everything goes to plan,
		// but can be assured that the command will execute if the caller exits 
		// prematurely due to an error.
		void Cancel() { canceled = true; }

		// Run the command.  Returns true on success, false if a fatal
		// error occurred (fatal meaning we should abort the rest of the
		// game launch process).
		bool Run()
		{
			AsyncErrorHandler aeh;

			// if the command is empty, there's nothing to do - simply return success
			if (command.length() == 0)
				return true;

			// the command has now been executed (the attempt counts as 
			// execution, whether or not it succeeds)
			executed = true;

			// log the command
			LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ %s:\n> %s\n"), desc.c_str(), command.c_str());

			// Check for invalid option tokens.  We defer errors on option parsing
			// until we run the command, since we parse and set up some of the
			// commands long before we actually run them.  It's more sensible to
			// the user to have any parsing errors show up at the point in the 
			// sequence where we try to run the command.
			if (invalOptTok.length() != 0)
			{
				aeh.Error(MsgFmt(_T("%s %s"), LoadStringT(launchErrorId).c_str(),
					MsgFmt(IDS_ERR_RUNBEFOREAFTEROPT, invalOptTok.c_str()).Get()));
				LogFile::Get()->Write(_T("+ %s: Invalid prefix option(s) [%s]\n"), desc.c_str(), invalOptTok.c_str());
				return false;
			}

			// Reset the Close event.  The Close event can be used to terminate
			// any step of the launch sequence, including Run Before and Run After
			// commands.  But it only cancels the step in effect when it was used;
			// it doesn't cancel any subsequent steps.  So count this as the start
			// of the current step, and therefore clear any Close event that was
			// previously signaled.
			monitor->ResetCloseEvent();

			// apply window rotations
			for (auto const &s : rotate)
			{
				std::match_results<TSTRING::const_iterator> m;
				if (std::regex_match(s, m, std::basic_regex<TCHAR>(_T("(\\w+),(90|180|270)"))))
				{
					TSTRING windowName = m[1].str();
					int theta = _ttoi(m[2].str().c_str());
					rotationManager.Rotate(windowName, theta);
				}
			}

			// apply substitution variables to the command
			TSTRING expCommand = monitor->SubstituteVars(command);
			
			// launch in the appropriate mode
			if (admin)
			{
				// Admin mode - launch it through the admin proxy.  If no
				// proxy is available, Admin launch isn't allowed.
				auto app = Application::Get();
				if (!app->IsAdminHostAvailable())
				{
					aeh.Error(MsgFmt(_T("%s %s"), LoadStringT(launchErrorId).c_str(), LoadStringT(IDS_ERR_ADMINHOSTREQ).c_str()));
					LogFile::Get()->Write(_T("+ %s: [ADMIN] flag was specified, but Administrator mode launching\n")
						_T("isn't available.  Please run \"PinballY Admin Mode.exe\" instead of the normal\n")
						_T("PinballY program to make Administrator launching available.\n"), desc.c_str());
					return false;
				}

				// Set up the admin mode request.
				//
				// For the launch mode, use "keep" or "detach", according to whether
				// we'll need the handle later.  If we're in [NOWAIT] mode (without
				// TERMINATE), we're going to set the process loose and never look
				// back, so we don't need the handle for anything.  In other modes,
				// we'll want to be able to terminate the process when the game
				// exits, so tell the host to keep the handle for later cleanup.
				const TCHAR *req[] = {
					_T("run"),             // admin request verb
					_T(""),                // path to executable
					_T(""),                // working directory
					expCommand.c_str(),    // command line
					_T(""),                // environment strings
					_T("0"),               // inactivity timeout - 0 means no timeout
					hide ? _T("SW_HIDE") : minimize ? _T("SW_SHOWMINIMIZED") : _T("SW_SHOW"),  // ShowWindow mode
					nowait && !terminate ? _T("detach") : _T("keep"),  // process handle retention mode
					_T("")                 // termination mode - ignored for non-game launches
				};

				// launch it
				std::vector<TSTRING> reply;
				TSTRING errDetails;
				bool adminOk = app->SendAdminHostRequest(req, countof(req), reply, errDetails);

				// validate the response format on a successful reply
				if (adminOk && reply.size() < 2)
				{
					adminOk = false;
					errDetails = _T("Invalid response format from host: ");
					for (auto const &r : reply)
						errDetails += _T(" \"") + r + _T("\"");
				}

				// check for errors
				if (!adminOk)
				{
					LogFile::Get()->Write(LogFile::TableLaunchLogging,
						_T("+ %s:\n> [ADMIN] command execution failed: %s; aborting launch\n"), desc.c_str(), errDetails.c_str());
					return false;
				}

				// The reply is "ok <pid> <tid>".  We just need the process ID.
				pid = (DWORD)_ttol(reply[1].c_str());

				// Open a handle on the process.  For an Admin process, we're
				// restricted in the rights we can ask for, but SYNCHRONIZE is
				// one of the allowed rights, and that's all we need for 'wait'
				// operations on the process.
				hProc = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
			}
			else
			{
				// Normal user mode - launch it directly.  Don't wait here, as
				// we might not want to wait for it at all, and even if we do,
				// we'll need to do the wait ourselves since we have to monitor
				// other events besides the spawned process while waiting.
				if (!Application::RunCommand(expCommand.c_str(), aeh, launchErrorId, false, &hProc, &pid,
					hide ? SW_HIDE : minimize ? SW_SHOWMINIMIZED : SW_SHOW))
				{
					// failed - abort the launch
					LogFile::Get()->Write(LogFile::TableLaunchLogging,
						_T("+ %s:\n> command execution failed; aborting launch\n"), desc.c_str());
					return false;
				}
			}

			// Now wait for it, if it's not in NOWAIT mode.  Note that we
			// have to wait explicitly here, rather than letting RunCommand
			// handle the wait, because we need to also stop waiting if we
			// get a shutdown signal.  If there's no process handle, assume
			// that the process has already exited and hence that waiting
			// isn't necessary.
			if (nowait || hProc == NULL)
			{
				// NOWAIT mode.  We can simply leave the process running.
				// If TERMINATE mode is set, leave the process handle in
				// hRunBeforeProc, so that the thread object destructor
				// will know to terminate the process when the monitor
				// thread exits.  If TERMINATE mode isn't set, though, 
				// the user wants us to simply launch the process and
				// leave it running, so we can close the process handle
				// now and let the process run independently from now on.
				LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ %s: [NOWAIT] specified, continuing\n"), desc.c_str());
				if (!terminate)
					hProc = NULL;
			}
			else
			{
				// Wait mode.  Wait for the process to exit, or for a 
				// close-game or application-wide shutdown signal.
				LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ %s: waiting for command to finish\n"), desc.c_str());
				HANDLE waitEvents[] = { hProc, monitor->shutdownEvent, monitor->closeEvent };
				switch (WaitForMultipleObjects(countof(waitEvents), waitEvents, FALSE, INFINITE))
				{
				case WAIT_OBJECT_0:
					// The RunBefore process exited.  This is what we were
					// hoping for; proceed to run the game.  Close the child
					// process handle and continue.
					LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ %s: command finished normally\n"), desc.c_str());
					hProc = NULL;
					break;

				case WAIT_OBJECT_0 + 1:
					// The Shutdown event fired.  Shut down immediately without
					// processing any remaining steps in the launch.
					LogFile::Get()->Write(LogFile::TableLaunchLogging,
						_T("+ %s: command interrupted because PinballY is exiting; aborting launch\n"), desc.c_str());
					return false;

				case WAIT_OBJECT_0 + 2:
					// The Close event fired.  Terminate the current step, and return
					// the caller's desired return status on close.
					LogFile::Get()->Write(LogFile::TableLaunchLogging,
						_T("+ %s: command interrupted by Exit Game event\n"), desc.c_str());
					return returnStatusOnClose;

				default:
					// An error occurred in the wait.  Abort the launch.
					LogFile::Get()->Write(LogFile::TableLaunchLogging,
						_T("+ %s: wait failed; aborting launch\n"), desc.c_str());
					return false;
				}
			}

			// Success
			return true;
		}

		// the game monitor thread object
		GameMonitorThread *monitor;

		// rotation manager object
		RotationManager &rotationManager;

		// description for log messages
		TSTRING desc;

		// error string ID to use for launch errors
		int launchErrorId;

		// Run() return status on Close event
		bool returnStatusOnClose;

		// child process handle holder and PID
		HandleHolder hProc;
		DWORD pid;

		// final command string, with all flags parsed out and substitutions applied
		TSTRING command;

		// option flags
		bool nowait;
		bool terminate;
		bool hide;
		bool minimize;
		bool admin;

		// if we find an invalid option token, we'll store it here for reporting 
		// when we try to execute the command
		TSTRING invalOptTok;

		// has the command been executed yet?
		bool executed;

		// has the command been canceled?
		bool canceled;

		// rotation command list
		std::list<TSTRING> rotate;
	};

	// Do an initial check to see if we need to add the default extension
	// to the game file.  We try this first before the Run Before commands,
	// so that the Run Before commands get the benefit of the adjusted
	// filename if available.
	ResolveGameFile(gameFileWithPath);

	// If desired, hide the taskbar while the game is running
	class TaskbarHider
	{
	public:
		TaskbarHider() { Show(SW_HIDE); }
		~TaskbarHider() { Show(SW_SHOW); }

		void Show(int nCmdShow)
		{
			// notify the watchdog process
			Application::Get()->watchdog.Notify(nCmdShow == SW_HIDE ? "Hide Taskbar" : "Restore Taskbar");

			// hide/show all top-level windows with a given class name
			auto ShowTopLevelWindows = [nCmdShow](const TCHAR *className)
			{
				for (HWND hWnd = FindWindowEx(NULL, NULL, className, NULL);
					hWnd != NULL;
					hWnd = FindWindowEx(NULL, hWnd, className, NULL))
				{
					::ShowWindow(hWnd, nCmdShow);
					::UpdateWindow(hWnd);
				}
			};

			// show/hide all taskbar and secondary taskbar windows, and
			// "Button" windows for the Start button
			ShowTopLevelWindows(_T("Shell_TrayWnd"));
			ShowTopLevelWindows(_T("Shell_SecondaryTrayWnd"));
			ShowTopLevelWindows(_T("Button"));
		}
	};
	std::unique_ptr<TaskbarHider> taskbarHider;
	if (hideTaskbar)
		taskbarHider.reset(new TaskbarHider());

	// Once RunBeforePre runs, we wish to guarantee that RunAfterPost runs,
	// so that it can undo the RunBeforePre operation.  Instantiate the 
	// RunAfterPost command, which will guarantee execution if we exit
	// prematurely due to another error in the course of the game launch.
	//
	// A Close event in an After command only cancels the current step, so
	// continue if a close event occurs.
	RunBeforeAfterParser runAfterPostCmd(this, rotationManager,
		_T("RunAfterPost (final post-game exit command)"), IDS_ERR_GAMERUNAFTERPOST, 
		GetLaunchParam("runAfterPost", gameSys.runAfterPost), true);

	// Run the RunBeforePre command, if any.  This command is executed with
	// a completely blank playfield window, before we show the "Launching 
	// Game" message.  This lets the command make system-wide monitor layout
	// changes with a minimum of visual fuss, since monitor changes won't
	// usually cause any visible effect on a blank black screen.
	//
	// A Close event in a Before command cancels the game launch, so don't
	// continue if Close is signaled.
	RunBeforeAfterParser runBeforePreCmd(this, rotationManager,
		_T("RunBeforePre (initial pre-launch command)"), IDS_ERR_GAMERUNBEFOREPRE, 
		GetLaunchParam("runBeforePre", gameSys.runBeforePre), false);
	if (!runBeforePreCmd.Run())
		return 0;

	// Display the "Launching Game" message in the main window, and run
	// Javascript scripts.  Stop if the Javascript handlers cancel the launch.
	if (playfieldView != nullptr)
	{
		PlayfieldView::LaunchReport report(cmd, launchFlags, gameId, gameSys.configIndex);
		if (!playfieldView->SendMessage(PFVMsgGameRunBefore, 0, reinterpret_cast<LPARAM>(&report)))
			return 0;
	}

	// Set up guaranteed execution for the RunAfter command, now that
	// we're about to fire RunBefore.
	RunBeforeAfterParser runAfterCmd(this, rotationManager,
		_T("RunAfter (post-game exit command)"), IDS_ERR_GAMERUNAFTER, 
		GetLaunchParam("runAfter", gameSys.runAfter), true);

	// Run the RunBefore command, if any
	RunBeforeAfterParser runBeforeCmd(this, rotationManager,
		_T("RunBefore (pre-launch command)"), IDS_ERR_GAMERUNBEFORE,
		GetLaunchParam("runBefore", gameSys.runBefore), false);
	if (!runBeforeCmd.Run())
		return 0;

	// Do another check for adding a default extension to the game file.
	// We already did this once, before the Run Before commands, because
	// we wanted to give the Run Before commands visibility into the
	// adjusted name, if available.  However, the Run Before commands
	// could conceivably be used to move or rename the game file into
	// place in preparation for the game, so the file might not have
	// actually existed until after those commands finished.  So do
	// a second check here, in case the file has come into existence.
	ResolveGameFile(gameFileWithPath);

	// Note the starting time.  We use this to figure the total time the
	// game was running, for the total play time statistics.  We'll update
	// the launch time below to the time when the new game process is
	// actually running, for a more accurate count that doesn't include
	// the time it takes to start the process, but it's best to get a
	// provisional starting time now just in case we don't all the way
	// through the launch process.  That way we'll at least have a valid
	// starting time if anyone should try to access this value before
	// we get the more accurate starting time.
	launchTime = GetTickCount64();

	// Get the current system time in FILETIME format, in case we need
	// it to look for a recently launched process in the two-stage launch
	// used by Steam (see below).
	FILETIME t0;
	GetSystemTimeAsFileTime(&t0);

	// get the program executable
	const TSTRING &exe = GetLaunchParam("exe", gameSys.exe);

	// Replace substitution variables in the command-line parameters
	const TSTRING &rawParams = GetLaunchParam("params", gameSys.params);
	TSTRING expandedParams = SubstituteVars(rawParams);
	LogFile::Get()->Write(LogFile::TableLaunchLogging,
		_T("+ table launch: executable: %s\n")
		_T("+ table launch: applying command line variable substitutions:\n+ Original> %s\n+ Final   > %s\n"),
		exe.c_str(), rawParams.c_str(), expandedParams.c_str());

	// Build the full command line: "exe" params
	TSTRING cmdline = _T("\"");
	cmdline += exe;
	cmdline += _T("\" ");
	cmdline += expandedParams;

	// set up the startup information struct
	STARTUPINFO startupInfo;
	ZeroMemory(&startupInfo, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESHOWWINDOW;
	startupInfo.wShowWindow = GetLaunchParamInt("swShow", gameSys.swShow);

	// process creation flags
	DWORD createFlags = 0;

	// If the system has environment variables to add, build a merged
	// environment.
	WCHAR *lpEnvironment = nullptr;
	std::unique_ptr<WCHAR> mergedEnvironment;
	const TSTRING &envVarsParam = GetLaunchParam("envVars", gameSys.envVars);
	if (envVarsParam.length() != 0)
	{
		// create the merged environment from the flattened ';'-delimited list
		CreateMergedEnvironment(mergedEnvironment, envVarsParam.c_str());

		// use the merged environment, noting in the create flags that it's Unicode
		lpEnvironment = mergedEnvironment.get();
		createFlags |= CREATE_UNICODE_ENVIRONMENT;
	}

	// Try launching the new process
	const TSTRING &workingPath = GetLaunchParam("workingPath", gameSys.workingPath);
	PROCESS_INFORMATION procInfo;
	ZeroMemory(&procInfo, sizeof(procInfo));
	if (!CreateProcess(NULL, cmdline.data(), NULL, NULL, false, createFlags,
		lpEnvironment, workingPath.c_str(), &startupInfo, &procInfo))
	{
		// failed - get the error
		WindowsErrorMessage sysErr;
		LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch failed: %s\n"), sysErr.Get());

		// If it's "elevation required", we have an exe that's marked as
		// requesting or requiring elevated privileges.  CreateProcess()
		// can't launch such programs because the UAC UI has to get
		// involved to ask the user permission.  
		if (sysErr.GetCode() == ERROR_ELEVATION_REQUIRED)
		{
			// CreateProcess() fails with ELEVATION REQUIRED even if the
			// program only *requests* elevation via the "highestAvailable"
			// setting in its manifest.  Such a program is declaring that
			// it's capable of running in either mode but will take admin
			// privileges when available.  CreateProcess() interprets that
			// to mean that admin mode MUST be used if the user account is
			// capable, and returns this error.
			//
			// For our purposes, though, we want to consider the "highest
			// available" privileges to be the privileges we actually have
			// in this process, which we know must be in regular user mode,
			// since we wouldn't have gotten an elevation error if we were
			// already in admin mode.  So try the launch again, this time
			// explicitly coercing the process to run "As Invoker".  If
			// the program requested "highest available", this will start
			// the new process without elevation and return success.  If
			// the program actually requires administrator mode (which it
			// can declare via the "requireAdministrator" setting in its
			// manifest), then the "as invoker" attempt will fail with
			// another ELEVATION REQUIRED error, since in this case
			// elevation is truly required.
			LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: retrying launch As Invoker\n"));
			if (!CreateProcessAsInvoker(
				exe.c_str(), cmdline.data(), NULL, NULL, false, createFlags,
				lpEnvironment, workingPath.c_str(), &startupInfo, &procInfo))
			{
				// get the new error code
				sysErr.Reset();
			}
		}

		// If elevation is still required, this program must require
		// administrator mode (via "requireAdministrator" in its manifest),
		// rather than merely requesting it.  If we're running under an 
		// Admin Host, we're in luck:  we can launch the program with 
		// elevated privileges via the Admin Host process.
		//
		// Note that we require the user to approve elevation per system
		// during each session, so only proceed if the user has approved 
		// elevation for this system previously.
		if (procInfo.hProcess == NULL
			&& sysErr.GetCode() == ERROR_ELEVATION_REQUIRED
			&& Application::Get()->adminHost.IsAvailable()
			&& elevationApproved)
		{
			// The Admin Host is running - we can proxy the request
			// to launch an Administrator mode process through it.
			auto& adminHost = Application::Get()->adminHost;
			LogFile::Get()->Write(LogFile::TableLaunchLogging,
				_T("+ table launch: re-launching in Administrator mode via PinballY Admin Mode host\n"));

			// flag that we're in admin mode
			isAdminMode = true;

			// set up the request parameters
			MsgFmt swShowStr(_T("%d"), GetLaunchParamInt("swShow", gameSys.swShow));
			const TCHAR *request[] = {
				_T("run"),                        // admin request verb
				exe.c_str(),                      // full path to executable
				workingPath.c_str(),              // working directory for new process
				cmdline.c_str(),                  // command line 
				envVarsParam.c_str(),             // environment variable list
				gameInactivityTimeout.c_str(),    // inactivity timeout, in seconds
				swShowStr,                        // ShowWindow mode
				_T("game"),                       // process handle retention mode
				GetLaunchParam("terminateBy", gameSys.terminateBy).c_str()  // termination mode
			};

			// Allow the admin host to set the foreground window when the
			// new game starts
			AllowSetForegroundWindow(adminHost.pid);

			// Send the request
			std::vector<TSTRING> reply;
			TSTRING errDetails;
			bool adminOk = adminHost.SendRequest(request, countof(request), reply, errDetails);

			// if the basic request succeeded, check the response format
			if (adminOk && reply.size() < 3)
			{
				adminOk = false;
				errDetails = _T("Invalid response format from host: ");
				for (auto const &r : reply)
					errDetails += _T(" \"") + r + _T("\"");
			}

			// check the results
			if (adminOk)
			{
				// Successful launch.  The first parameter item in the
				// reply is the process ID of the new process; the second
				// is the thread ID.  We can use the process ID to open a
				// handle to the process.  This is allowed even though the
				// new process is elevated; a non-elevated process is 
				// allowed to open a handle to an elevated process, with
				// restrictions on what types of access we can request. 
				// SYNCHRONIZE (to wait for the process to exit) is one of
				// the allowed access rights, as is "query limited 
				// information".
				// 
				// Plug the process handle into the PROCESS_INFORMATION
				// struct that we'd normally get back from CreateProcess(),
				// to emulate normal process creation.  We don't need the
				// thread handle for anything, so leave it null.
				//
				LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: Admin mode launch succeeded\n"));
				procInfo.dwProcessId = (DWORD)_ttol(reply[1].c_str());
				procInfo.dwThreadId = (DWORD)_ttol(reply[2].c_str());
				procInfo.hProcess = OpenProcess(
					SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, procInfo.dwProcessId);
				procInfo.hThread = NULL;
			}
			else
			{
				// Error launching
				LogFile::Get()->Write(LogFile::TableLaunchLogging,
					_T("+ table launch: Admin launch failed: %s\n"), errDetails.c_str());

				// send the error to the playfield view for display
				if (playfieldView != nullptr)
				{
					PlayfieldView::LaunchErrorReport report(cmd, launchFlags, gameId, gameSys.configIndex, errDetails.c_str());
					playfieldView->SendMessage(PFVMsgGameLaunchError, 0, reinterpret_cast<LPARAM>(&report));
				}

				// return failure
				return 0;
			}
		}

		// Check to see if we finally managed to create a process
		if (procInfo.hProcess == NULL)
		{
			// launch failed
			LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch failed: %s\n"), sysErr.Get());
				
			// Report the error.  Call out "elevation required" as a
			// separate error, since we can offer special workarounds
			// for that error (namely re-launching our own process in
			// elevated mode, which will enable us to launch elevated
			// child processes).
			if (playfieldView != nullptr)
			{
				switch (sysErr.GetCode())
				{
				case ERROR_ELEVATION_REQUIRED:
					// elevation is required - offer options
					playfieldView->SendMessage(PFVMsgPlayElevReqd,
						reinterpret_cast<WPARAM>(gameSys.displayName.c_str()),
						static_cast<LPARAM>(gameId));
					break;

				default:
					// use the generic error message for anything else
					{
						PlayfieldView::LaunchErrorReport report(cmd, launchFlags, gameId, gameSys.configIndex, sysErr.Get());
						playfieldView->SendMessage(PFVMsgGameLaunchError, 0, reinterpret_cast<LPARAM>(&report));
					}
					break;
				}
			}

			// abort the thread
			return 0;
		}
	}

	// We don't need the thread handle - close it immediately
	if (procInfo.hThread != NULL)
		CloseHandle(procInfo.hThread);

	// remember the process ID and main thread ID for the new process
	pid = procInfo.dwProcessId;
	tidMainGameThread = procInfo.dwThreadId;

	// remember the first-stage process handle
	HANDLE hProcFirstStage = procInfo.hProcess;

	// wait for the process to start up
	WaitForStartup(exe.c_str(), hProcFirstStage);

	// Some games, such as Steam-based systems or Future Pinball + BAM,
	// are set up where the program we launch is actually just another
	// launcher itself.  The program we run isn't the actual game; it
	// launches the actual game as a separate (third) process.  Some
	// of these launchers processes exit after launching the actual 
	// game process, so our normal technique of monitoring the process
	// we launch to determine when the game is done won't work:  since
	// the launcher will exit immediately, its termination doesn't tell
	// us anything about the game process's lifetime.  In these cases,
	// we have a separate parameter for the game system, "Process name", 
	// which tells us the EXE name of the actual game process.  
	// 
	const TSTRING &secondaryProcessName = GetLaunchParam("processName", gameSys.process);
	if (secondaryProcessName.length() == 0)
	{
		// Single-stage launch - remember the process handle
		hGameProc = hProcFirstStage;
	}
	else
	{
		// Two-stage launch
		LogFile::Get()->Write(LogFile::TableLaunchLogging, 
			_T("+ table launch: waiting for secondary process %s to start\n"), secondaryProcessName.c_str());

		// keep going until the process launches, the launcher process
		// dies, or we get an abort signal
		HandleHolder snapshot;
		for (int triesSinceFirstStageExited = 0; ; )
		{
			// get a snapshot of running processes
			snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
			if (snapshot == 0)
			{
				// get the error and log it
				WindowsErrorMessage sysErr;
				LogFile::Get()->Write(LogFile::TableLaunchLogging,
					_T("+ table launch: error getting process snapshot: %s\n"), sysErr.Get());

				// display it in the playfield view if possible
				if (playfieldView != nullptr)
				{
					MsgFmt msg(_T("Error getting process snapshot: %s"), sysErr.Get());
					PlayfieldView::LaunchErrorReport report(cmd, launchFlags, gameId, gameSys.configIndex, msg.Get());
					playfieldView->SendMessage(PFVMsgGameLaunchError, 0, reinterpret_cast<LPARAM>(&report));
				}

				// abort the launch
				return 0;
			}

			// scan processes
			PROCESSENTRY32 procInfo;
			ZeroMemory(&procInfo, sizeof(procInfo));
			procInfo.dwSize = sizeof(procInfo);
			bool found = false;
			if (Process32First(snapshot, &procInfo))
			{
				do
				{				
					// check for a match to our name
					if (_tcsicmp(secondaryProcessName.c_str(), procInfo.szExeFile) == 0)
					{
						// Check to see if was launched after the first stage - we don't
						// want to match old instances that were already running.
						FILETIME createTime, exitTime, kernelTime, userTime;
						HandleHolder newProc = OpenProcess(
							PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, 
							false, procInfo.th32ProcessID);
						if (newProc != 0
							&& GetProcessTimes(newProc, &createTime, &exitTime, &kernelTime, &userTime)
							&& CompareFileTime(&createTime, &t0) > 0)
						{
							LogFile::Get()->Write(LogFile::TableLaunchLogging,
								_T("+ table launch: found matching process %d\n"), (int)procInfo.th32ProcessID);

							// It has the right name and was created after we launched
							// the first stage, so assume it's the one we're looking
							// for.  Replace the monitor process handle with this new
							// process handle.
							LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ using this process\n"));
							hGameProc = newProc.Detach();

							// wait for the program to enter its event loop
							TCHAR exepath[MAX_PATH];
							GetModuleFileNameEx(hGameProc, NULL, exepath, countof(exepath));
							WaitForStartup(exepath, hGameProc);

							// Find the thread with the UI window(s) for the new process.
							// As with waiting for startup, it might take a while for the
							// new process to open its main window.  So retry until we 
							// find the window we're looking for, encounter an error, or
							// receive an Application Shutdown or Close Game signal.
							while (FindMainWindowForProcess(procInfo.th32ProcessID, &tidMainGameThread) == NULL)
							{
								// Pause for a bit, exiting the thread if we get a Shutdown
								// signal.  Don't stop on a Close event, though:  that would
								// leave the second-stage process running.  The two-stage
								// launch programs generally don't have any UI in the first
								// stage, so it's best to treat the whole launch as an atomic
								// operation for the purposes of the Close signal.
								HANDLE waitHandles[] = { shutdownEvent };
								if (WaitForMultipleObjects(countof(waitHandles), waitHandles, false, 500) != WAIT_TIMEOUT)
								{
									LogFile::Get()->Write(LogFile::TableLaunchLogging,
										_T("+ table launch: interrupted waiting for first child process window to open; aborting launch\n"));
									return 0;
								}
							}

							// Process search success - exit the process search loop
							found = true;
							break;
						}						
						else
						{
							// log why we're skipping it
							LogFile::Get()->Write(LogFile::TableLaunchLogging,
								_T("+ table launch: found matching process name %d, but process was pre-existing; skipping\n"), 
								(int)procInfo.th32ProcessID);
						}
					}
				} while (Process32Next(snapshot, &procInfo));
			}

			// if we found what we were looking for, stop waiting
			if (found)
				break;

			// If the first stage process has exited, count the iteration.  The
			// second stage should have launched before the first stage exits,
			// so we really shouldn't have to go more than one iteration after
			// it exits to see the new process.  But just in case Windows is a
			// little slow updating its process list, give it a few tries.
			if (WaitForSingleObject(hProcFirstStage, 0) == WAIT_OBJECT_0
				&& ++triesSinceFirstStageExited > 10)
			{
				// It's been too long; we can probably assume the new process
				// isn't going to start.
				if (playfieldView != nullptr)
				{
					MsgFmt msg(_T("Launcher process exited, target process %s hasn't started"), secondaryProcessName.c_str());
					PlayfieldView::LaunchErrorReport report(cmd, launchFlags, gameId, gameSys.configIndex, msg.Get());
					playfieldView->SendMessage(PFVMsgGameLaunchError, 0, reinterpret_cast<LPARAM>(&report));
				}

				// abort the launch
				LogFile::Get()->Write(LogFile::TableLaunchLogging,
					_T("+ table launch: launcher process exited, target process %s hasn't started;")
					_T(" assuming failure and aborting launch\n"),
					secondaryProcessName.c_str());
				return 0;
			}

			// Do a brief pause, unless a Shutdown event fired.  Don't stop
			// on a Close event, since we want to treat the two-stage launch
			// as atomic for the purposes of the Exit Game command; aborting
			// here would leave the actual game process running indefinitely,
			// since we haven't identified the process yet and thus can't
			// explicitly shut it down yet.
			HANDLE waitHandles[] = { shutdownEvent };
			if (WaitForMultipleObjects(countof(waitHandles), waitHandles, false, 1000) != WAIT_TIMEOUT)
			{
				// shutting down the app; abort immediately
				LogFile::Get()->Write(LogFile::TableLaunchLogging,
					_T("+ table launch: interrupted waiting for target process to start; aborting launch\n"));
				return 0;
			}
		}
	}

	// Successful launch!
	LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: process launch succeeded\n"));

	// Count this as the starting time for the actual game session
	launchTime = GetTickCount64();

	// switch the playfield view to Running mode
	if (playfieldView != nullptr)
	{
		PlayfieldView::LaunchReport report(cmd, launchFlags, gameId, gameSys.configIndex);
		playfieldView->PostMessage(PFVMsgGameLoaded, 0, reinterpret_cast<LPARAM>(&report));
	}

	// If the game system has a startup key sequence, send it
	if (gameSys.startupKeys.length() != 0)
	{
		LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ sending startup key sequence\n"));

		// Key names for use in the startupKeys list
		struct KbKey
		{
			const TCHAR *name;
			int scanCode;
			bool extended;
		};
		static const KbKey keys[] =
		{
			{ _T("esc"), 0x01 },
			{ _T("f1"),  0x3b },
			{ _T("f2"),  0x3c },
			{ _T("f3"),  0x3d },
			{ _T("f4"),  0x3e },
			{ _T("f5"), 0x3f },
			{ _T("f6"), 0x40 },
			{ _T("f7"), 0x41 },
			{ _T("f8"), 0x42 },
			{ _T("f9"), 0x43 },
			{ _T("f10"), 0x44 },
			{ _T("f11"), 0x57 },
			{ _T("f12"), 0x58 },
			{ _T("tilde"), 0x29 },
			{ _T("1"), 0x02 },
			{ _T("2"), 0x03 },
			{ _T("3"), 0x04 },
			{ _T("4"), 0x05 },
			{ _T("5"), 0x06 },
			{ _T("6"), 0x07 },
			{ _T("7"), 0x08 },
			{ _T("8"), 0x09 },
			{ _T("9"), 0x0A },
			{ _T("0"), 0x0B },
			{ _T("dash"), 0x0c },
			{ _T("plus"), 0x0D },
			{ _T("backslash"), 0x2B },
			{ _T("backspace"), 0x0E },
			{ _T("tab"), 0x0F },
			{ _T("q"), 0x10 },
			{ _T("w"), 0x11 },
			{ _T("e"), 0x12 },
			{ _T("r"), 0x13 },
			{ _T("t"), 0x14 },
			{ _T("y"), 0x15 },
			{ _T("u"), 0x16 },
			{ _T("i"), 0x17 },
			{ _T("o"), 0x18 },
			{ _T("p"), 0x19 },
			{ _T("lbracket"), 0x1A },
			{ _T("rbracket"), 0x1B },
			{ _T("capslock"), 0x3A },
			{ _T("a"), 0x1e },
			{ _T("s"), 0x1f },
			{ _T("d"), 0x20 },
			{ _T("f"), 0x21 },
			{ _T("g"), 0x22 },
			{ _T("h"), 0x23 },
			{ _T("j"), 0x24 },
			{ _T("k"), 0x25 },
			{ _T("l"), 0x26 },
			{ _T("colon"), 0x27 },
			{ _T("quote"), 0x28 },
			{ _T("enter"), 0x1c },
			{ _T("lshift"), 0x2a },
			{ _T("z"), 0x2c },
			{ _T("x"), 0x2d },
			{ _T("c"), 0x2e },
			{ _T("v"), 0x2f },
			{ _T("b"), 0x30 },
			{ _T("n"), 0x31 },
			{ _T("m"), 0x32 },
			{ _T("comma"), 0x33 },
			{ _T("period"), 0x34 },
			{ _T("slash"), 0x35 },
			{ _T("rshift"), 0x36 },
			{ _T("lctrl"), 0x1D },
			{ _T("lalt"), 0x38 },
			{ _T("space"), 0x39 },
			{ _T("ralt"), 0x38, true },
			{ _T("rctrl"), 0x1D, true },
			{ _T("ins"), 0x52, true },
			{ _T("home"), 0x47, true },
			{ _T("pageup"), 0x49, true },
			{ _T("del"), 0x53, true },
			{ _T("end"), 0x4f, true },
			{ _T("pagedown"), 0x51, true },
			{ _T("up"), 0x48, true },
			{ _T("left"), 0x4b, true },
			{ _T("down"), 0x50, true },
			{ _T("right"), 0x4d, true },
			{ _T("numlock"), 0x45 },
			{ _T("kpenter"), 0x1c, true },
			{ _T("kp0"), 0x52 },
			{ _T("decimal"), 0x53 },
			{ _T("kp1"), 0x4F },
			{ _T("kp2"), 0x50 },
			{ _T("kp3"), 0x51 },
			{ _T("kp4"), 0x4B },
			{ _T("kp5"), 0x4C },
			{ _T("kp6"), 0x4D },
			{ _T("kp7"), 0x47 },
			{ _T("kp8"), 0x48 },
			{ _T("kp9"), 0x49 },
			{ _T("add"), 0x4E },
			{ _T("subtract"), 0x4A },
			{ _T("divide"), 0x35, true },
			{ _T("times"), 0x37 }
		};

		// Send a Make/Break key event pair for a given scan code
		int delayBetweenKeys = 50;
		auto Send = [&delayBetweenKeys](const TCHAR *key)
		{
			auto Set = [](INPUT &ii, const KbKey *key, bool up)
			{
				ZeroMemory(&ii, sizeof(ii));
				ii.type = INPUT_KEYBOARD;
				ii.ki.wScan = key->scanCode;
				ii.ki.dwFlags = KEYEVENTF_SCANCODE;
				if (key->extended) ii.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
				if (up) ii.ki.dwFlags |= KEYEVENTF_KEYUP;
			};

			// look up the key
			for (size_t i = 0; i < countof(keys); ++i)
			{
				if (_tcscmp(keys[i].name, key) == 0)
				{
					// build the make-break pair for the key
					INPUT iMake, iBreak;
					Set(iMake, &keys[i], false);
					Set(iBreak, &keys[i], true);

					// send the 'make' event
					SendInput(1, &iMake, sizeof(INPUT));

					// Pause to let the receiver recognize the input.  DirectInput
					// games in particular poll the input periodically and thus will
					// only see a keystroke if the key is held down for the polling
					// interval.  We can only guess about the interval, since we
					// want this to work for different receiver programs that might
					// use different DirectInput versions or different input layers
					// entirely, but the common USB polling interval of 10ms is a
					// good lower bound, and 30ms or so is a good upper bound since
					// a game program has to be at least that responsive to avoid
					// showing obvious keyboard lag.
					Sleep(20);

					// send the 'break' event
					SendInput(1, &iBreak, sizeof(INPUT));

					// pause for the delay time between keys
					Sleep(delayBetweenKeys);
				}
			}
		};

		// The startupKeys setting is a list of space-delimited tokens.  
		// Each token is the name of a keyboard key taken from the set
		// above, or a special directive:
		//
		// { comment text, ignored } 
		//
		// [pace <milliseconds>] - set the delay between keys
		//
		// [pause <seconds>] - pause for the given time in seconds
		//
		// [click], [rclick] - left or right mouse click at current
		// mouse position
		//
		// [gridpos <down> <right>] - send a sequence of <right> and
		// <down> keys according to the gridPos setting (from the game
		// database entry) for the game we're launching.  For example, 
		// if gridPos is "2x5" (row 2, column 5), we send one <down>
		// key and four <right> keys.  The <down> and <right> keys
		// use the key names from the list above.
		//   
		const TCHAR *p = gameSys.startupKeys.c_str();
		while (*p != 0)
		{
			// find the next token - stop if there are no more tokens
			std::basic_regex<TCHAR> tokPat(_T("(^\\s*([^\\s\\[\\]]+|\\[[^\\]]+\\]|\\{[^}]+\\})\\s*).*"));
			std::basic_regex<TCHAR> clickPat(_T("\\[r?click\\b\\s*(.*)\\]"), std::regex_constants::icase);
			std::match_results<const TCHAR*> m;
			std::match_results<TSTRING::const_iterator> ms;
			if (!std::regex_match(p, m, tokPat))
				break;

			// skip the token in the source
			p += m[1].length();

			// pull out the token and convert to lower-case
			TSTRING tok = m[2].str();
			std::transform(tok.begin(), tok.end(), tok.begin(), ::_totlower);

			// check what we have
			if (tok[0] == '{')
			{
				// comment - just ignore the whole thing
			}
			else if (tstrStartsWith(tok.c_str(), _T("[pause ")))
			{
				// Pause for the given interval.  Don't just Sleep(); rather,
				// wait for our various termination events, with the given pause
				// time as the timeout.  If one of the termination events fires
				// before the timeout expires, stop sending keys, since we're
				// apparently aborting the whole launch.  If we time out, that's
				// exactly what we wanted to do, so just keep going.
				HANDLE h[] = { hGameProc, shutdownEvent, closeEvent };
				if (WaitForMultipleObjects(countof(h), h, FALSE, _ttoi(tok.c_str() + 7) * 1000) != WAIT_TIMEOUT)
					break;
			}
			else if (tstrStartsWith(tok.c_str(), _T("[pace ")))
			{
				// set the delay time between keys
				delayBetweenKeys = _ttoi(tok.c_str() + 6);
			}
			else if (std::regex_match(tok, ms, clickPat))
			{
				// figure whether it's a left or right click
				bool right = (tok[1] == 'r');

				// set up the base mouse input struct
				INPUT i;
				ZeroMemory(&i, sizeof(i));
				i.type = INPUT_MOUSE;
				DWORD baseFlags = 0;

				// If a target window is specified, add absolute positioning for the 
				// center of that window's bounds.  If no target is specified, the 
				// default will be relatively positioned at a zero offset, which is 
				// simply the current pointer position.
				if (ms[1].matched)
				{
					auto Test = [&ms, &i, &baseFlags](const TCHAR *name, const POINT &pt)
					{
						if (ms[1].str() == name)
						{
							baseFlags |= MOUSEEVENTF_ABSOLUTE;
							i.mi.dx = pt.x;
							i.mi.dy = pt.y;
					 		return true;
						}
						return false;
					};
					Test(_T("playfield"), ptPlayfieldCenter)
						|| Test(_T("backglass"), ptBackglassCenter)
						|| Test(_T("dmd"), ptDMDCenter)
						|| Test(_T("topper"), ptTopperCenter);
				}

				// synthesize a button-down event for the desired button
				i.mi.dwFlags = baseFlags | (right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN);
				SendInput(1, &i, sizeof(INPUT));

				// pause, then send the corresponding button-up event
				Sleep(20);
				i.mi.dwFlags = baseFlags | (right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP);
				SendInput(1, &i, sizeof(INPUT));

				// pause for the inter-key delay
				Sleep(delayBetweenKeys);
			}
			else if (tstrStartsWith(tok.c_str(), _T("[gridpos ")))
			{
				// Send a sequence of keys to move to the row/columns position
				// given by the gridPos database entry for this game.  First,
				// we need to pull out the <down> and <right> key names from 
				// the [gridpos <down> <right>] syntax.
				std::basic_regex<TCHAR> gpPat(_T("\\s*(\\S+)\\s+([^\\s\\]]+).*"));
				std::match_results<const TCHAR*> gm;
				if (std::regex_match(tok.c_str() + 9, gm, gpPat))
				{
					// Send the <down> keys to move to the target row.  Note that 
					// we start from row 1 column 1, so we send (target row - 1)
					// <down> keys.
					for (int row = 1; row < game.gridPos.row; ++row)
						Send(gm[1].str().c_str());

					// Send the <right> keys to move to the target column
					for (int col = 1; col < game.gridPos.col; ++col)
						Send(gm[2].str().c_str());
				}
			}
			else
			{
				// anything else should be a key name - send the key
				Send(tok.c_str());
			}
		}
	}

	// Reduce our process priority while the game is running, to minimize
	// the amount of CPU time we take away from the game while we're in
	// the background.  This should only be considered a secondary way of
	// reducing our CPU impact; the primary strategy always has to be to
	// actual reduction of the ongoing work we're doing, which we try to
	// do by disabling UI elements and features while a game is running.
	// For example, we discard all video objects, stop animations in the
	// UI windows, and turn off most event timers.  But a Windows GUI
	// program will always receive a steady stream of events from the
	// system even if it's just idling, so we can't become completely
	// quiescent without terminating the process entirely, which we don't
	// want to do because of the overhead incurred on reloading.  The
	// priority reduction is just another little tweak to minimize the
	// CPU time we receive for handling background idle messages, and
	// especially to reduce the chances that our idle processing will
	// interrupt the game when it has foreground work to do.
	struct PrioritySetter
	{
		PrioritySetter() :
			origPriorityClass(0),
			hCurProc(OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId()))
		{
			if (hCurProc != 0)
			{
				// remember the old priority so that we can reset it on thread exit
				origPriorityClass = GetPriorityClass(hCurProc);

				// set the new priority to "below normal"
				SetPriorityClass(hCurProc, BELOW_NORMAL_PRIORITY_CLASS);
			}
		}
		~PrioritySetter()
		{
			// restore the old priority class if possible
			if (hCurProc != 0 && origPriorityClass != 0)
				SetPriorityClass(hCurProc, origPriorityClass);
		}
		HandleHolder hCurProc;
		DWORD origPriorityClass;
	}
	prioritySetter;

	// If we're capturing screenshots of the running game, start
	// the capture process
	if ((launchFlags & LaunchFlags::Capturing) != 0)
	{
		// Collect a list of results for the items.  (Note that it's just
		// a weird coincidence that we're using a CapturingErrorHandler
		// here: it's not because that error handler has anything special
		// to do with screen captures!  It's so named because it collects
		// error messages in a list.  We're capturing errors about 
		// capturing screen shots.)
		CapturingErrorHandler statusList;

		// the capture is okay so far
		bool captureOkay = true;
		bool abortCapture = false;

		// overall capture status
		TSTRINGEx curStatus;
		int overallStatusMsgId = 0;

		// count of media items attempted/succeeded
		int nMediaItemsAttempted = 0;
		int nMediaItemsOk = 0;

		// do the initial startup wait, to allow the game to boot up
		{
			// set up the wait handles for each step requiring a wait
			HANDLE h[] = { hGameProc, shutdownEvent, closeEvent };

			// set the capture status message
			capture.statusWin->SetCaptureStatus(LoadStringT(IDS_CAPSTAT_STARTING), capture.startupDelay);

			// Wait for the initial startup time.  If any events fire
			// (that is, we don't time out), something happened that
			// interrupted the capture, so stop immediately.
			if (WaitForMultipleObjects(countof(h), h, FALSE, capture.startupDelay) != WAIT_TIMEOUT)
			{
				overallStatusMsgId = IDS_ERR_CAP_GAME_EXITED;
				captureOkay = false;
				abortCapture = true;
			}
		}

		// Get the path to ffmpeg.exe.  Note that this is always called 
		// ffmpeg\\ffmpeg.exe in a deployed system, but the development
		// build system has separate 32-bit and 64-bit copies.
		TCHAR ffmpeg[MAX_PATH];
		GetDeployedFilePath(ffmpeg, _T("ffmpeg\\ffmpeg.exe"), _T("$(SolutionDir)ffmpeg$(64)\\ffmpeg.exe"));

		// Capture one item.  Returns true to continue capturing
		// additional items, false to end the capture process.
		// A true return doesn't necessarily mean that the 
		// individual capture succeeded; it just means that we
		// didn't run into a condition that ends the whole
		// process, such as the game exiting prematurely.
		for (auto &item: capture.items)
		{
			// count the item attempted
			nMediaItemsAttempted += 1;

			// get the descriptor for the item, for status messages
			TSTRINGEx itemDesc;
			itemDesc.Load(item.mediaType.nameStrId);

			// If the game has already exited, or a shutdown or close event
			// is already pending, abort this capture before it starts
			{
				HANDLE h[] = { hGameProc, shutdownEvent, closeEvent };
				if (WaitForMultipleObjects(countof(h), h, FALSE, 0) != WAIT_TIMEOUT)
				{
					abortCapture = true;
					captureOkay = false;
				}
			}

			// if we've already decided to abort, just add a status message
			// for this item saying so
			if (abortCapture)
			{
				statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), LoadStringT(IDS_ERR_CAP_ITEM_NOT_STARTED).c_str()));
				break;
			}

			// If we're capturing audio for this item, and we haven't found
			// the audio capture device yet, find it now.  We use FFMPEG's
			// DirectShow (dshow) audio capture capability, so we have to 
			// find the device using the dshow API to make sure we see the
			// same device name that FFMPEG will see when it scans for a
			// device.  Note that Windows has multiple media APIs that can
			// access the same audio devices, but it's important to use the
			// same API that FFMPEG uses, since the different APIs can use
			// different names for the same devices.  For example, dshow 
			// truncates long device names in different ways on different
			// Windows versions.
			bool hasAudio =	(item.mediaType.format == MediaType::VideoWithAudio && item.enableAudio)
				|| item.mediaType.format == MediaType::Audio;
			if (hasAudio && audioCaptureDevice.length() == 0)
			{
				// Audio capture is needed, but audio isn't available.  Note
				// that the item didn't complete successfully.
				captureOkay = false;

				// If this is video with audio, disable audio for the item and
				// continue with the capture; we can at least still capture a
				// silent video for it.  If it's pure audio, there's no point,
				// so just skip the item.
				if (item.mediaType.format == MediaType::VideoWithAudio)
				{
					// disable the audio, proceed with the capture (but log an
					// error to alert the user to the reason the video doesn't
					// have the audio they requested)
					statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), LoadStringT(IDS_ERR_CAP_NO_AUDIO_DEV_VIDEO).c_str()));
					item.enableAudio = false;
				}
				else
				{
					// pure audio - skip the item entirely
					statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), LoadStringT(IDS_ERR_CAP_NO_AUDIO_DEV).c_str()));
					continue;
				}
			}

			// If this item is in Manual Start mode, wait for the start signal
			if (item.manualStart)
			{
				// Move the status window over the playfield window, even if we're
				// going to capture the playfield.  We're not actually capturing
				// anything while waiting for the Go signal, so it doesn't matter
				// if we put the status window in front of the window we're about
				// to capture, hence we can put it anywhere.  The window is more
				// than just status in this case - it's showing a prompt - so we
				// want it to be as conspicuous as possible.  The playfield window
				// is the best place to make the user notice it.
				capture.statusWin->PositionOver(Application::Get()->GetPlayfieldWin());

				// put the status window in waiting mode
				capture.statusWin->SetCaptureStatus(MsgFmt(IDS_CAPSTAT_MANUAL_START, itemDesc.c_str()), item.captureTime);
				capture.statusWin->SetManualStartMode(true);

				// clear any previous manual start/stop signal
				ResetEvent(startStopEvent);

				// Wait for the start/stop event
				HANDLE h[] = { startStopEvent, hGameProc, shutdownEvent, closeEvent };
				static const TCHAR *hDesc[] = { 
					_T("Started"), _T("game exited"), _T("PinballY shutting down"), _T("user pressed Exit Game button") 
				};
				switch (DWORD result = WaitForMultipleObjects(countof(h), h, FALSE, INFINITE))
				{
				case WAIT_OBJECT_0:
					// The user pressed the Go button combo.  Ready to proceed
					break;

				case WAIT_OBJECT_0 + 1:
				case WAIT_OBJECT_0 + 2:
				case WAIT_OBJECT_0 + 3:
					// The game process exited
					captureOkay = false;
					abortCapture = true;
					LogFile::Get()->Write(LogFile::CaptureLogging, 
						MsgFmt(_T("+ Capture aborted: %s while waiting for manual start\n"), hDesc[result - WAIT_OBJECT_0]));
					statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), LoadStringT(IDS_ERR_CAP_ITEM_INTERRUPTED).c_str()));
					break;
					
				default:
					// error waiting
					captureOkay = false;
					abortCapture = true;
					{
						WindowsErrorMessage err;
						LogFile::Get()->Write(LogFile::CaptureLogging,
							MsgFmt(_T("+ Capture aborted: error waiting: %s\n"), err.Get()));
					}
					statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), LoadStringT(IDS_ERR_CAP_ITEM_INTERRUPTED)));
					break;
				}
			}

			// stop if the capture was aborted
			if (abortCapture)
				break;

			// ready to go - set the status window message
			curStatus.Format(LoadStringT(IDS_CAPSTAT_ITEM), itemDesc.c_str());
			capture.statusWin->SetCaptureStatus(curStatus, item.captureTime);
			capture.statusWin->SetManualStartMode(false);

			// Move the status window over the playfield window when capturing
			// in any other window, and move it over the backglass window when
			// capturing the playfield.
			switch (item.mediaType.nameStrId)
			{
			case IDS_MEDIATYPE_PFPIC:
			case IDS_MEDIATYPE_PFVID:
				capture.statusWin->PositionOver(Application::Get()->GetBackglassWin());
				break;

			default:
				capture.statusWin->PositionOver(Application::Get()->GetPlayfieldWin());
				break;
			}

			// save (by renaming) any existing files of the type we're about to capture
			TSTRING oldName;
			if (FileExists(item.filename.c_str())
				&& !item.mediaType.SaveBackup(item.filename.c_str(), oldName, statusList))
			{
				// backup rename failed - skip this file
				captureOkay = false;
				continue;
			}

			// if the file still exists, skip the item
			if (FileExists(item.filename.c_str()))
			{
				statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), LoadStringT(IDS_ERR_CAP_ITEM_EXISTS).c_str()));
				captureOkay = false;
				continue;
			}

			// if the directory doesn't exist, try creating it
			TCHAR dir[MAX_PATH];
			_tcscpy_s(dir, item.filename.c_str());
			PathRemoveFileSpec(dir);
			if (!DirectoryExists(dir))
			{
				LogFile::Get()->Write(LogFile::CaptureLogging, _T("+ Media folder doesn't exist, creating it: %s\n"), dir);
				if (!CreateSubDirectory(dir, _T(""), NULL))
				{
					WindowsErrorMessage winErr;
					LogFile::Get()->Write(LogFile::CaptureLogging, _T("+ Media folder creation failed: %s, error %s\n"), dir, winErr.Get());
					statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), winErr.Get()));
					captureOkay = false;
					continue;
				}
			}

			// Figure the ffmpeg transforms to apply to the captured screen
			// images to get the final video in the correct orientation.  We
			// need to invert the transformations we apply to our display
			// images, so that an image captured from a screen that appears
			// as we display it gets stored in a format that yields that same
			// screen display appearance after applying our display transforms.
			// We apply our mirror/flip transforms last, so apply the reverse
			// mirror/flip transforms first in the capture.  (The mirror/flip
			// transforms are mutually commutative, so it doesn't matter
			// which one goes first.)
			TSTRING transforms;
			auto AddTransform = [&transforms, &item](const TCHAR *t) 
			{
				// add visual transforms for visual media only
				if (item.mediaType.format != MediaType::Audio)
				{
					// add the -vf switch before the first item; add commas
					// before subsequent items
					if (transforms.length() == 0)
						transforms = _T("-vf \"");
					else
						transforms += _T(",");

					// add the new transform
					transforms += t;
				}
			};
			if (item.windowMirrorVert)
				AddTransform(_T("vflip"));
			if (item.windowMirrorHorz)
				AddTransform(_T("hflip"));

			// Now add the rotation transform.  We need to figure the total
			// rotation as the difference between the normal rotation for the
			// media type and the window rotation.  And then we have to apply
			// that rotation in the opposite direction.  Our display transforms
			// are all clockwise, so we need counter-clockwise rotations for
			// the reversals.
			int rotate = item.mediaRotation - item.windowRotation;
			switch ((rotate + 360) % 360)
			{
			case 90:
				AddTransform(_T("transpose=2"));  // 90 degrees counter-clockwise
				break;

			case 180:
				AddTransform(_T("transpose=1,transpose=1"));  // 90 degrees twice -> 180 degrees
				break;

			case 270:
				AddTransform(_T("transpose=1"));  // 90 degrees clockwise
				break;
			}

			// add the close quote to the transforms if applicable
			if (transforms.length() != 0)
				transforms += _T("\"");

			// set up the image format options, if we're capturing a still
			// image or a video
			TSTRINGEx imageOpts;
			switch (item.mediaType.format)
			{
			case MediaType::Image:
			case MediaType::SilentVideo:
			case MediaType::VideoWithAudio:
				imageOpts.Format(
					_T(" -f gdigrab")
					_T(" -framerate 30")
					_T(" -offset_x %d -offset_y %d -video_size %dx%d -i desktop"),
					item.rc.left, item.rc.top, item.rc.right - item.rc.left, item.rc.bottom - item.rc.top);
				break;
			}

			// if we're on a 64-bit build, use a very large realtime input 
			// buffer to reduce the chance dropped frames
			TSTRINGEx rtbufsizeOpts(IF_32_64(_T(""), _T("-rtbufsize 2000M")));

			// set up format-dependent options
			TSTRINGEx audioOpts;
			TSTRINGEx timeLimitOpt;
			TSTRINGEx acodecOpts;
			bool isVideo = false;
			switch (item.mediaType.format)
			{
			case MediaType::Image:
				// image capture - capture one frame only (-vframes 1)
				timeLimitOpt = _T("-vframes 1");
				break;

			case MediaType::SilentVideo:
				// video capture, no audio
				isVideo = true;
				if (!item.manualStop)
					timeLimitOpt.Format(_T("-t %d"), item.captureTime / 1000);
				audioOpts = _T("-c:a none");
				break;

			case MediaType::VideoWithAudio:
				// video capture with optional audio
				isVideo = true;
				if (!item.manualStop)
					timeLimitOpt.Format(_T("-t %d"), item.captureTime / 1000);
				if (item.enableAudio)
				{
					acodecOpts.Format(_T("-c:a aac -b:a 128k"));
					audioOpts.Format(_T("-f dshow -i audio=\"%s\""), audioCaptureDevice.c_str());
				}
				else
					audioOpts = _T("-c:a none");
				break;

			case MediaType::Audio:
				// audio only
				if (!item.manualStop)
					timeLimitOpt.Format(_T("-t %d"), item.captureTime / 1000);
				audioOpts.Format(_T("-f dshow -i audio=\"%s\""), audioCaptureDevice.c_str());
				break;
			}

			// Build the FFMPEG command line for either normal one-pass mode or 
			// two-pass video mode.
			TSTRINGEx cmdline1;
			TSTRINGEx cmdline2;
			TSTRINGEx tmpfile;
			if (isVideo && capture.twoPassEncoding)
			{
				// Two-pass encoding.  Capture the video with the lossless h264
				// code in the fastest mode, with no rotation, to a temp file.
				// We'll re-encode to the actual output file and apply rotations
				// in the second pass.
				tmpfile = std::regex_replace(item.filename, std::basic_regex<TCHAR>(_T("\\.([^.]+)$")), _T(".tmp.mkv"));
				cmdline1.Format(_T("\"%s\" -y -loglevel warning -thread_queue_size 32")
					_T(" %s %s %s")
					_T(" -probesize 30M")
					_T(" %s -c:v libx264 %s -threads 8 -qp 0 -preset ultrafast")
					_T(" \"%s\""),
					ffmpeg, 
					imageOpts.c_str(), audioOpts.c_str(), timeLimitOpt.c_str(), 
					rtbufsizeOpts.c_str(),
					acodecOpts.c_str(),
					tmpfile.c_str());

				// Format the command line for the second pass while we're here
				cmdline2.Format(_T("\"%s\" -y -loglevel warning")
					_T(" -i \"%s\"")
					_T(" %s -c:a copy -max_muxing_queue_size 1024")
					_T(" \"%s\""),
					ffmpeg,
					tmpfile.c_str(),
					transforms.c_str(), 
					item.filename.c_str());
			}
			else
			{
				// normal one-pass encoding - include all options and encode
				// directly to the desired output file
				cmdline1.Format(_T("\"%s\" -y -loglevel warning -probesize 30M -thread_queue_size 32")
					_T(" %s %s")
					_T(" %s %s %s %s")
					_T(" \"%s\""),
					ffmpeg, 
					imageOpts.c_str(), audioOpts.c_str(), acodecOpts.c_str(),
					transforms.c_str(), timeLimitOpt.c_str(), rtbufsizeOpts.c_str(),
					item.filename.c_str());
			}

			// Run the capture.  'logSuccess' indicates whether or not we'll log
			// a successful completion; this should be false until the last pass
			// if we're doing a multi-pass capture, so that we don't roll out the
			// "mission accomplished" banner prematurely.  'isCapturePass' is true
			// on the first pass where we actually the capture, and false on
			// subsequent passes.  This is used for Manual Stop mode: we only pay 
			// attention to Manual Stop mode on the actual capture pass, not on
			// subsequent encoding passes.
			auto RunFFMPEG = [this, &statusList, &curStatus, &item, &itemDesc, &captureOkay, &abortCapture, &nMediaItemsOk]
				(TSTRINGEx &cmdline, bool logSuccess, bool isCapturePass)
			{
				// presume failure
				bool result = false;

				// Log the command for debugging purposes, as there's a lot that
				// can go wrong here and little information back from ffmpeg that
				// we can analyze mechanically.
				auto LogCommandLine = [&curStatus, &cmdline](bool log)
				{
					if (log)
					{
						LogFile::Get()->Group();
						LogFile::Get()->Write(_T("Media capture: %s: launching FFMPEG\n> %s\n"),
							curStatus.c_str(), cmdline.c_str());
					}
				};

				// log the command line information if logging is enabled
				LogCommandLine(LogFile::Get()->IsFeatureEnabled(LogFile::CaptureLogging));

				// Set up an "inheritable handle" security attributes struct,
				// for creating the stdin and stdout/stderr handles for the
				// child process.  These need to be inheritable so that we 
				// can open the files and pass the handles to the child.
				SECURITY_ATTRIBUTES sa;
				sa.nLength = sizeof(sa);
				sa.lpSecurityDescriptor = NULL;
				sa.bInheritHandle = TRUE;

				// Create a pipe for the ffmpeg stdin.  This will let us send
				// a "q" key to cancel the capture prematurely if necessary.
				HandleHolder hStdinRead, hStdinWrite;
				if (CreatePipe(&hStdinRead, &hStdinWrite, &sa, 1024))
				{
					// don't let the child inherit our end of the pipe
					SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);
				}
				else
				{
					// failed to create the pipe - just pass the NUL device
					hStdinRead = CreateFile(_T("NUL"), GENERIC_READ, 0, &sa, OPEN_EXISTING, 0, NULL);

					// we absolutely need the pipe in Manual Stop mode, since it's
					// the way we tell ffmpeg to stop the capture
					if (item.manualStop && isCapturePass)
					{
						statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), LoadStringT(IDS_ERR_CAP_MANUAL_STOP_NO_PIPE).c_str()));
						LogFile::Get()->Write(LogFile::CaptureLogging,
							_T("+ Manual Stop isn't possible for this item because an error occurred\n")
							_T("  trying to create a pipe to send the stop command to ffmpeg; capture aborted\n"));
						captureOkay = false;
						abortCapture = true;
						return false;
					}
				}

				// Set up a temp file to capture output from FFmpeg, so that we can
				// copy it to the log file after the capture is done.  Do this whether
				// or not capture logging is enabled; if the capture fails due to an
				// FFmpeg error, we'll log the FFmpeg output regardless of the log
				// settings, to give the user a chance to see what went wrong even
				// if they weren't anticipating anything going wrong.
				HandleHolder hStdOut;
				TSTRING fnameStdOut;
				{
					// create the temp file
					TCHAR tmpPath[MAX_PATH] = _T("<no temp path>"), tmpName[MAX_PATH] = _T("<no temp name>");
					GetTempPath(countof(tmpPath), tmpPath);
					GetTempFileName(tmpPath, _T("PBYCap"), 0, tmpName);
					hStdOut = CreateFile(tmpName, GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

					// log an error if that failed, but continue with the capture
					if (hStdOut == NULL)
					{
						// log the error
						WindowsErrorMessage err;
						LogFile::Get()->Write(LogFile::CaptureLogging,
							_T("+ Unable to log FFMPEG output: error opening temp file %s (error %d: %s)\n"),
							tmpName, err.GetCode(), err.Get());

						// direct FFmpeg output to NUL
						hStdOut = CreateFile(_T("NUL"), GENERIC_WRITE, 0, &sa, OPEN_EXISTING, 0, NULL);
					}
					else
					{
						// successfully opened the file - remember its name
						fnameStdOut = tmpName;
					}
				}

				// Set up the startup info.  Use Show-No-Activate to try to keep
				// the game window activated and in the foreground, since VP (and
				// probably others) stop animations when in the background.
				STARTUPINFO startupInfo;
				ZeroMemory(&startupInfo, sizeof(startupInfo));
				startupInfo.cb = sizeof(startupInfo);
				startupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
				startupInfo.wShowWindow = SW_SHOWNOACTIVATE;
				startupInfo.hStdInput = hStdinRead;
				startupInfo.hStdOutput = hStdOut;
				startupInfo.hStdError = hStdOut;

				// launch the process
				PROCESS_INFORMATION procInfo;
				if (CreateProcess(NULL, cmdline.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, 
					NULL, NULL, &startupInfo, &procInfo))
				{
					// ffmpeg launched successfully.  Put the handles in holders
					// so that we auto-close the handles when done with them.
					HandleHolder hFfmpegProc(procInfo.hProcess);
					HandleHolder hFfmpegThread(procInfo.hThread);

					// close our copy of the child's stdin read handle
					hStdinRead = NULL;

					// copy the ffmpeg output log file to our log, if capturing a log
					auto CopyOutputToLog = [&hStdOut, &fnameStdOut, &LogCommandLine](bool force)
					{
						// close our copy of the output file handle to make sure the
						// file is really closed
						hStdOut = nullptr;

						// check if we're include capture logging
						if (!LogFile::Get()->IsFeatureEnabled(LogFile::CaptureLogging))
						{
							// Capture logging is disabled, so we're not logging this by
							// default.  Check for a 'force' override.
							if (force)
							{
								// We're forcing the output, due to an error in the capture.
								// In this case, we won't have logged the command line earlier,
								// so do so now.
								LogCommandLine(true);
							}
							else
							{
								// capture logging disabled, not forcing it; don't copy the output
								return;
							}
						}

						// if there's a log file, copy it
						if (fnameStdOut.length() != 0)
						{
							// read the file
							long len;
							std::unique_ptr<BYTE> txt(ReadFileAsStr(fnameStdOut.c_str(), SilentErrorHandler(),
								len, ReadFileAsStr_NewlineTerm | ReadFileAsStr_NullTerm));

							// copy it to the log file
							if (txt != nullptr)
							{
								// in case the log file contains null bytes, write it piecewise
								// in null-terminated chunks
								const BYTE *endp = txt.get() + len;
								for (const BYTE *p = txt.get(); p < endp; )
								{
									// find the end of this null-terminated chunk
									const BYTE *q;
									for (q = p; q != endp && *q != 0; ++q);

									// write this chunk
									LogFile::Get()->WriteStrA((const char *)p);

									// skip the null byte
									p = q + 1;
								}
							}

							// delete the temp file
							DeleteFile(fnameStdOut.c_str());
						}
					};

					// Wait for the process to finish, or for a shutdown or
					// close-game event to interrupt it.  Also include the
					// start/stop event as the last handle, but don't count
					// it just yet - we'll only include it in the actual wait
					// if we're in manual stop mode.
					HANDLE h[] = { hFfmpegProc, hGameProc, shutdownEvent, closeEvent, startStopEvent };
					static const TCHAR *waitName[] = {
						_T("ffmpeg exited"), _T("game exited"), _T("app shutdown"), _T("user Exit Game command"), _T("Manual Stop")
					};
					DWORD nWaitHandles = countof(h) - 1;

					// Check for Manual Stop mode.  This only applies on the
					// capture pass (not on subsequent encode/compress passes).
					if (item.manualStop && isCapturePass)
					{
						// include the manual stop event in the wait list
						nWaitHandles += 1;

						// set the capture status window to reflect manual stop mode
						capture.statusWin->SetManualStopMode(true);

						// clear any past manual start/stop signal
						ResetEvent(startStopEvent);
					}

				WaitForFfmpeg:
					// wait for the capture to finish
					const TCHAR *waitResultName = nullptr;
					switch (DWORD waitResult = WaitForMultipleObjects(nWaitHandles, h, FALSE, INFINITE))
					{
					case WAIT_OBJECT_0 + 4:
						// The user pressed the Manual Stop button to terminate a manually
						// timed capture.  Send ffmpeg the "Q" key on its stdin to stop
						// the capture.
						if (hStdinWrite != NULL)
						{
							static const char msg[] = "q\n";
							DWORD actual;
							WriteFile(hStdinWrite, msg, sizeof(msg) - 1, &actual, NULL);
						}

						// Now go back for another wait pass, this time removing the
						// Manual Stop event from the wait list.  This gives ffmpeg a
						// chance to exit before we proceed.
						nWaitHandles -= 1;
						goto WaitForFfmpeg;

					case WAIT_OBJECT_0:
						// The ffmpeg process finished successfully
						{
							// retrieve the process exit code
							DWORD exitCode;
							GetExitCodeProcess(hFfmpegProc, &exitCode);

							// Copy the output to the log.  If the FFmpeg exit code was non-zero,
							// log it even if capture logging is turned off in the options, since
							// the error information is too useful to discard just because the
							// user wasn't anticipating an error.
							CopyOutputToLog(exitCode != 0);

							// log the process exit code
							LogFile::Get()->Write(LogFile::CaptureLogging,
								_T("\n+ FFMPEG completed: process exit code %d\n"), (int)exitCode);

							// consider this a success if the exit code was 0, otherwise consider
							// it an error
							if (exitCode == 0)
							{
								// success
								result = true;

								// log successful completion if desired
								if (logSuccess)
								{
									// log the success
									statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), LoadStringT(IDS_ERR_CAP_ITEM_OK).c_str()));

									// count the success
									nMediaItemsOk += 1;
								}
							}
							else
							{
								// log the error
								statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(),
									MsgFmt(IDS_ERR_CAP_ITEM_FFMPEG_ERR_LOGGED, (int)exitCode).Get()));
								captureOkay = false;
							}
						}
						break;

					default:
						// Error/unexpected wait result
						waitResultName = _T("Error waiting for ffmpeg to exit");
						goto Interruption;

					case WAIT_OBJECT_0 + 1:
					case WAIT_OBJECT_0 + 2:
					case WAIT_OBJECT_0 + 3:
						waitResultName = waitName[waitResult - WAIT_OBJECT_0];

					Interruption:
						// Shutdown event, close event, or premature game termination,
						// or another error.  Count this as an interrupted capture.
						statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), LoadStringT(IDS_ERR_CAP_ITEM_INTERRUPTED).c_str()));
						captureOkay = false;
						abortCapture = true;

						// log it
						CopyOutputToLog(false);
						LogFile::Get()->Write(LogFile::CaptureLogging, _T("\n+ capture interrupted (%s)\n"), waitResultName);

						// Send ffmpeg a "Q" key press on its stdin to try to shut
						// it down immediately
						if (hStdinWrite != NULL)
						{
							static const char msg[] = "q\n";
							DWORD actual;
							WriteFile(hStdinWrite, msg, sizeof(msg) - 1, &actual, NULL);
						}
						break;
					}
				}
				else
				{
					// Error launching ffmpeg.  It's likely that all subsequent
					// ffmpeg launch attempts will fail, because the problem is
					// probably something permanent (e.g., ffmpeg.exe isn't
					// installed where we expect it to be installed, or there's
					// a file permissions problem).  So skip any remaining items
					// by setting the 'abort' flag.
					WindowsErrorMessage err;
					LogFile::Get()->Write(LogFile::CaptureLogging, _T("+ FFMPEG launch failed: Win32 error %d, %s\n"), err.GetCode(), err.Get());
					statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(),	MsgFmt(IDS_ERR_CAP_ITEM_FFMPEG_LAUNCH, err.Get()).Get()));
					captureOkay = false;
					abortCapture = true;
				}

				// add a blank line to the log after the FFMPEG output, for readability 
				LogFile::Get()->Group(LogFile::CaptureLogging);

				// we're done with manual stop mode, if it was ever in effect
				capture.statusWin->SetManualStopMode(false);

				// return the operation status
				return result;
			};

			// Run the first pass.  Only show the success status for the first pass
			// if there will be no second pass, since we won't know if the overall
			// operation is successful until after the second pass, if there is one.
			// Pass 'capturePass' as true on this pass, since this is the actual
			// screen capture phase, regardless of whether we're doing the capture 
			// in one pass or two.
			bool twoPass = (cmdline2.length() != 0);
			if (RunFFMPEG(cmdline1, !twoPass, true))
			{
				// success - if there's a second pass, run it
				if (twoPass)
				{
					curStatus.Format(LoadStringT(IDS_CAPSTAT_ENCODING_ITEM), itemDesc.c_str());
					capture.statusWin->SetCaptureStatus(curStatus.c_str(), item.captureTime*3/2);
					RunFFMPEG(cmdline2, true, false);
				}
			}

			// if there's a temp file, delete it
			if (tmpfile.length() != 0 && FileExists(tmpfile.c_str()))
				DeleteFile(tmpfile.c_str());
		}

		// We're done with the capture process, either because we finished
		// capturing all of the selected items or because another event
		// interrupted the capture.  In either case, if the game is still
		// running, terminate it.
		capture.statusWin->SetCaptureStatus(LoadStringT(IDS_CAPSTAT_ENDING).c_str(), 0);
		if (WaitForSingleObject(hGameProc, 0) == WAIT_TIMEOUT)
			CloseGame();

		// close the capture status window
		capture.statusWin->PostMessage(WM_CLOSE);

		// Display the results to the main window
		if (playfieldView != nullptr)
		{
			// set the overall group message, if we don't already have one
			if (overallStatusMsgId == 0)
				overallStatusMsgId = captureOkay ? IDS_ERR_CAP_SUCCESS : IDS_ERR_CAP_FAILED;

			// notify the playfield window of the capture status
			PlayfieldView::CaptureDoneReport report(gameId, captureOkay, IsCloseEventSet(),
				overallStatusMsgId, statusList, nMediaItemsAttempted, nMediaItemsOk);
			playfieldView->SendMessage(PFVMsgCaptureDone, reinterpret_cast<WPARAM>(&report));
		}
	}

	// wait until the game exits, or we get a shutdown/close signal
	HANDLE h[] = { hGameProc, shutdownEvent, closeEvent };
	switch (WaitForMultipleObjects(countof(h), h, FALSE, INFINITE))
	{
	case WAIT_OBJECT_0:
		// The running game process exited.
		LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: child process exited normally\n"));
		break;

	case WAIT_OBJECT_0 + 1:
		// The shutdown event triggered - the program is exiting.  Simply
		// exit the thread so that the program can terminate normally.
		LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: interrupted by PinballY shutdown\n"));
		break;

	case WAIT_OBJECT_0 + 2:
		// The Close Game event has triggered
		LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: Close Game command received\n"));

		// If we didn't already do so, try terminating the game process
		// explicitly.  The main UI will normally do this as soon as the
		// Exit Game command is issued (which is what triggers the Close 
		// event in the first place), but in some cases it might not be
		// able to do so at the time of the command.  For example, if 
		// this game uses a two-stage launch, the user might have pressed
		// the Close button during the first phase, at which point the
		// game process ID wasn't yet known.
		if (!closedGameProc)
		{
			LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: trying to close the game process\n"));
			CloseGame();
		}

		// give the game a few seconds to terminate
		{
			HANDLE h2[] = { hGameProc, shutdownEvent };
			switch (WaitForMultipleObjects(countof(h2), h2, FALSE, 5000))
			{
			case WAIT_OBJECT_0:
				// the game process exited
				LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: game exited normally\n"));
				break;

			case WAIT_OBJECT_0 + 1:
				// the shutdown event fired
				LogFile::Get()->Write(LogFile::TableLaunchLogging,
					_T("+ table launch: application shutting down; aborting without waiting for game to exit\n"));
				break;

			case WAIT_TIMEOUT:
				// timeout - the game didn't exit
				LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: timed out waiting for game to exit\n"));
				break;

			default:
				// error/interruption
				LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: error waiting for game to exit\n"));
				break;
			}
		}
		break;

	case WAIT_TIMEOUT:
	case WAIT_ABANDONED:
	case WAIT_FAILED:
	default:
		// Error, abandoned handle, or other.  Something must be wrong;
		// simply exit the thread.
		LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: error waiting for child process to exit\n"));
		break;
	}

	// note the exit time
	exitTime = GetTickCount64();

	// let the main window know that the game child process has exited
	if (playfieldView != nullptr)
	{
		PlayfieldView::GameOverReport report(cmd, launchFlags, gameId, gameSys.configIndex, exitTime - launchTime);
		playfieldView->SendMessage(PFVMsgGameOver, 0, reinterpret_cast<LPARAM>(&report));
	}

	// run the RunAfter command, if any
	if (!runAfterCmd.Run())
		return 0;

	// remove the "game exiting" message
	if (playfieldView != nullptr)
	{
		PlayfieldView::LaunchReport report(cmd, launchFlags, gameId, gameSys.configIndex);
		playfieldView->SendMessage(PFVMsgGameRunAfter, 0, reinterpret_cast<LPARAM>(&report));
	}

	// run the RunAfterPost command, if any
	if (!runAfterPostCmd.Run())
		return 0;

	// done
	LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch finished successfully\n"));
	return 0;
}

void Application::GameMonitorThread::StealFocusFromGame(HWND hwnd)
{
	// inject a call to the child process to set our window
	// as the foreground
	DWORD tid;
	HandleHolder hRemoteThread = CreateRemoteThread(
		hGameProc, NULL, 0,
		(LPTHREAD_START_ROUTINE)&SetForegroundWindow, hwnd,
		0, &tid);

	// explicitly set ourselves as the foreground window here, too,
	// for good measure
	SetForegroundWindow(hwnd);
}

bool Application::GameMonitorThread::WaitForStartup(const TCHAR *exepath, HANDLE hProc)
{
	// Determine the executable type
	DWORD_PTR exeinfo;
	SHFILEINFO shinfo;
	if ((exeinfo = SHGetFileInfo(exepath, 0, &shinfo, sizeof(shinfo), SHGFI_EXETYPE)) != 0)
	{
		// If it's a console-mode program, WaitForInputIdle will always
		// fail, so there's no point in calling it.  There's no conceptual
		// equivalent for console-mode programs, either; they don't have
		// a message loop, so there's no way in general to tell when
		// they're ready.  All we can do is return success.
		if (HIWORD(exeinfo) == 0 
			&& (LOWORD(exeinfo) == 0x5a4d // 'MZ' -> MS-DOS .exe or .com file
				|| LOWORD(exeinfo) == 0x4550)) // 'PE' -> Console application or .bat file
		{
			LogFile::Get()->Write(LogFile::TableLaunchLogging, 
				_T("+ table launch: note: this is a DOS/console-mode program; skipping the usual startup wait\n"));
			return true;
		}
	}

	// keep trying until the process is ready, or we run into a problem
	for (int tries = 0; tries < 20; ++tries)
	{
		// wait for "input idle" state
		DWORD result = WaitForInputIdle(hProc, 1000);

		// if it's ready, return success
		if (result == 0)
			return true;

		// If the wait failed, pause briefly and try again.  For reasons
		// unknown, the wait sometimes fails when called immediately on a 
		// new process launched with ShellExecuteEx(), but will work if
		// we give it a couple of seconds.
		if (result == WAIT_FAILED)
		{
			Sleep(100);
			continue;
		}

		// if the wait timed out, check if the exit event was signalled;
		// if so, terminate the thread immediately
		if (WaitForSingleObject(shutdownEvent, 0) == WAIT_OBJECT_0)
			return false;
	}

	// too many retries - fail
	LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: error waiting for the new process to start up (WaitForInputIdle failed)\n"));
	return false;
}


bool Application::GameMonitorThread::Shutdown(ErrorHandler &eh, DWORD timeout, bool force)
{
	// set the shutdown event to tell background threads to exit
	SetEvent(shutdownEvent);

	// wait for the thread to exit, but not too long
	DWORD result = WaitForSingleObject(hThread, timeout);
	if (result == WAIT_OBJECT_0)
		return true;

	// the wait failed - report the error
	WindowsErrorMessage msg;
	eh.SysError(LoadStringT(IDS_ERR_MONTHREADEXIT),
		result == WAIT_TIMEOUT ? _T("wait timed out") : MsgFmt(_T("Wait failed: %s"), msg.Get()).Get());

	// if desired, terminate the thread forcibly
	if (force)
		TerminateThread(hThread, 0);

	// return failure, since the thread didn't terminate on its own
	return false;
}

void Application::GameMonitorThread::RotationManager::RotateNoStore(const TSTRING &winName, int theta)
{
	// find the window by name
	D3DView *pwnd = nullptr;
	if (winName == _T("playfield"))
		pwnd = monitor->playfieldView;
	else if (winName == _T("backglass"))
		pwnd = monitor->backglassView;
	else if (winName == _T("dmd"))
		pwnd = monitor->dmdView;
	else if (winName == _T("topper"))
		pwnd = monitor->topperView;
	else if (winName == _T("instructions"))
		pwnd = monitor->instCardView;

	// if we found it, apply the rotation
	if (pwnd != nullptr)
		pwnd->SetRotation((pwnd->GetRotation() + theta + 360) % 360);
}

// -----------------------------------------------------------------------
//
// In-UI error handler.  This is a variation on the interactive error
// handler that displays errors in a graphical popup box in the main
// playfield window.
//

void Application::InUiErrorHandler::Display(ErrorIconType icon, const TCHAR *msg)
{
	// check if we have a playfield view available
	if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != 0 && pfv->GetHWnd() != 0)
	{
		// there's a playfield view - show the error through the D3D UI
		pfv->ShowError(icon, msg, 0);
	}
	else
	{
		// no playfield view - use the system default error box
		LogError(icon, msg);
	}
}

void Application::InUiErrorHandler::GroupError(ErrorIconType icon, const TCHAR *summary, const ErrorList &geh)
{
	// check if we have a playfield view available
	if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != 0 && pfv->GetHWnd() != 0)
	{
		// there's a playfield view - show the error through the D3D UI
		pfv->ShowError(icon, summary, &geh);
	}
	else
	{
		// no playfield view - use the system default error box
		InteractiveErrorHandler ieh;
		ieh.GroupError(icon, summary, geh);
	}
}

// -----------------------------------------------------------------------
//
// Async version of the in-UI error handler.  This uses window messages
// to handle the display operations, making it usable from background
// threads.
//

void Application::AsyncErrorHandler::SysError(const TCHAR *friendly, const TCHAR *details)
{
	// check if we have a playfield view available
	HWND hwnd;
	if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != 0 && (hwnd = pfv->GetHWnd()) != NULL)
	{
		// there's a playfield view - show the error through the D3D UI
		::SendMessage(hwnd, PFVMsgShowSysError, (WPARAM)friendly, (LPARAM)details);
	}
	else
	{
		// no playfield view - use the system default error box
		LogSysError(EIT_Error, friendly, details);
	}
}

void Application::AsyncErrorHandler::Display(ErrorIconType icon, const TCHAR *msg)
{
	// check if we have a playfield view available
	HWND hwnd;
	if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != 0 && (hwnd = pfv->GetHWnd()) != NULL)
	{
		// there's a playfield view - show the error through the D3D UI
		PFVMsgShowErrorParams ep(icon, msg);
		::SendMessage(hwnd, PFVMsgShowError, 0, reinterpret_cast<LPARAM>(&ep));
	}
	else
	{
		// no playfield view - use the system default error box
		LogError(icon, msg);
	}
}

void Application::AsyncErrorHandler::GroupError(ErrorIconType icon, const TCHAR *summary, const ErrorList &geh)
{
	// check if we have a playfield view available
	HWND hwnd;
	if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != 0 && (hwnd = pfv->GetHWnd()) != NULL)
	{
		// there's a playfield view - show the error through the D3D UI
		PFVMsgShowErrorParams ep(icon, summary, &geh);
		::SendMessage(hwnd, PFVMsgShowError, 0, reinterpret_cast<LPARAM>(&ep));
	}
	else
	{
		// no playfield view - use the system default error box
		InteractiveErrorHandler ieh;
		ieh.GroupError(icon, summary, geh);
	}
}

void Application::AsyncErrorHandler::FlashError(const ErrorList &geh)
{
	// check if we have a playfield view available
	HWND hwnd;
	if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != 0 && (hwnd = pfv->GetHWnd()) != NULL)
	{
		// send it to the playfield view
		::SendMessage(hwnd, PFVMsgShowFlashError, 0, reinterpret_cast<LPARAM>(&geh));
	}
	else
	{
		// no playfield view - show the error through a regular dialog box
		InteractiveErrorHandler ieh;
		ieh.GroupError(EIT_Error, nullptr, geh);
	}
}

// -----------------------------------------------------------------------
//
// Admin Host interface
//

bool Application::AdminHost::StartThread()
{
	// create the 'quit' event object, which the main UI thread uses
	// to signal that it's time to shut down
	hQuitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (hQuitEvent == NULL)
		return false;

	// Create the queue wait event
	hRequestEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (hRequestEvent == NULL)
		return false;

	// launch the thread
	hThread = CreateThread(NULL, 0, &SThreadMain, this, 0, &tid);
	if (hThread == NULL)
		return false;

	// success
	return true;
}

// Semi-generic value mapper function.  The 'to' type has to provide
// a reserve() method.
template<typename TypeFrom, typename TypeTo>
void MapValues(const TypeFrom &from, TypeTo &to, std::function<typename TypeTo::value_type(const typename TypeFrom::value_type&)> f)
{
	to.reserve(from.size() + to.size());
	std::transform(from.begin(), from.end(), std::back_inserter(to), f);
}

void Application::AdminHost::PostRequest(const std::vector<TSTRING> &request)
{
	// create a new vector of <const TCHAR*> elements pointing to the strings in place
	std::vector<const TCHAR*> requestp;
	MapValues(request, requestp, [](const TSTRING &ele) { return ele.c_str(); });

	// post the request using vector's underlying array storage
	PostRequest(&requestp[0], requestp.size());
}

void Application::AdminHost::PostRequest(const TCHAR *const *request, size_t nItems)
{
	// create the request object; no wait is required for a posted request
	RefPtr<Request> requestObj(new Request(request, nItems, false));

	// Enqueue the request, holding the object lock while manipulating
	// the queue.  Note that the counted reference object is a little
	// tricky to deal with in the list: we have to emplace a null pointer
	// and then assign the pointer to the newly created slot, because
	// RefPtr construction assumes ownership of an existing reference
	// rather than counting a new one, and we want to count a new one
	// in this case.
	{
		CriticalSectionLocker locker(lock);
		requests.emplace_back(nullptr);
		requests.back() = requestObj;
	}

	// wake up the pipe manager thread
	SetEvent(hRequestEvent);
}


void Application::PostAdminHostRequest(const TCHAR *const *request, size_t nItems)
{
	if (adminHost.IsAvailable())
		adminHost.PostRequest(request, nItems);
}

bool Application::SendAdminHostRequest(const TCHAR *const *request, size_t nItems, 
	std::vector<TSTRING> &reply, TSTRING &errDetails)
{
	if (adminHost.IsAvailable())
		return adminHost.SendRequest(request, nItems, reply, errDetails);
	else
		return false;
}

bool Application::AdminHost::SendRequest(const TCHAR *const *request, size_t nItems, 
	std::vector<TSTRING> &reply, TSTRING &errDetails)
{
	// create the request object with waiting enabled
	RefPtr<Request> requestObj(new Request(request, nItems, true));

	// Enqueue the request, holding the object lock while manipulating 
	// the queue.  But ONLY that long; we don't want to continue holding
	// the object lock while awaiting the reply, since we'd lock the
	// pipe thread out of being able to read the queue and thus would
	// deadlock against it, as we need it to read the queue to process
	// our request and respond.  Note also that emplacing the counted
	// reference requires a two-step procedure to make the list's ref
	// add a count: we have to emplace null, then assign the pointer.
	// Emplacing directly would invoke the RefPtr constructor, which
	// assumes an existing reference rather than adding one.
	{
		CriticalSectionLocker locker(lock);
		requests.emplace_back(nullptr);
		requests.back() = requestObj;
	}

	// wake up the pipe manager thread
	SetEvent(hRequestEvent);

	// Now await the reply, or a shutdown event
	HANDLE waitHandles[] = { requestObj->hEvent, hQuitEvent };
	for (;;)
	{
		switch (WaitForMultipleObjects(countof(waitHandles), waitHandles, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:
			// The request completed.  The reply uses the same format
			// as the request, with one or more strings separated by
			// null characters.  Parse the result.
			reply.clear();
			if (requestObj->success)
			{
				// copy the strings from the reply buffer into the vector
				const TCHAR *p = requestObj->reply.get();
				const TCHAR *start = p;
				const TCHAR *endp = p + requestObj->replyCharLen;
				for (; p != endp; ++p)
				{
					if (*p == 0)
					{
						reply.emplace_back(start, p - start);
						start = p + 1;
					}
				}
				
				// if there's a non-null-terminated final fragment, add it
				if (start != endp)
					reply.emplace_back(start, p - start);

				// interpret the results
				if (reply.size() >= 1 && reply[0] == _T("ok"))
				{
					// success response from host
					return true;
				}
				else if (reply.size() >= 2 && reply[0] == _T("error"))
				{
					// host returned an error message - use that as our error detail message
					errDetails = reply[1];
					return false;
				}
				else
				{
					// unexpected response - say so, and include the full response in the reply
					errDetails = _T("Unexpected response from Admin host:");
					for (auto const &r : reply)
						errDetails += _T(" \"") + r + _T("\"");

					return false;
				}

				// success
				return true;
			}
			else
			{
				// request failed
				return false;
			}

		case WAIT_OBJECT_0 + 1:
			// Shutdown event - abandon the request and return failure
			errDetails = _T("Interrupted - PinballY is closing");
			return false;

		case WAIT_TIMEOUT:
		case WAIT_ABANDONED:
			// ignore these - just go back for another try
			break;

		default:
			// error - abandon the request and return failure
			errDetails = _T("Error waiting for Admin host reply");
			return false;
		}
	}
}

void Application::SendKeysToAdminHost(const std::list<TSTRING> &keys)
{
	// we only need to do this if the Admin Host is running
	if (adminHost.IsAvailable())
	{
		// start the request vector with the "keys" verb
		std::vector<const TCHAR*> req;
		req.emplace_back(_T("keys"));

		// add the keys
		MapValues(keys, req, [](const TSTRING &ele) { return ele.c_str(); });

		// post the request - this request has no reply
		adminHost.PostRequest(&req[0], req.size());
	}
}

Application::AdminHost::Request::Request(const TCHAR *const *request, size_t nItems, bool wait) :
	success(false)
{
	// The message that we send through the pipe needs to go in a single
	// buffer.  We'll format the array of strings into a flat buffer by
	// packing them back-to-back, with a null character separating each
	// string from the next.  Start by summing up the string lengths.
	size_t totalCharLen = 0;
	for (size_t i = 0; i < nItems; ++i)
		totalCharLen += _tcslen(request[i]) + 1;

	// allocate space for the buffer
	TCHAR *buf = new TCHAR[totalCharLen];
	this->request.reset(buf);
	this->requestCharLen = totalCharLen;

	// copy the request strings into the buffer
	for (size_t i = 0; i < nItems; ++i)
	{
		size_t len = _tcslen(request[i]) + 1;
		memcpy(buf, request[i], len * sizeof(TCHAR));
		buf += len;
	}

	// if the caller wants to wait for a reply, create the event object
	if (wait)
		hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
}

DWORD Application::AdminHost::ThreadMain()
{
	// set up the OVERLAPPED struct for reading the pipe
	hReadEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	ZeroMemory(&ovRead, sizeof(ovRead));
	ovRead.hEvent = hReadEvent;

	// keep going until we get a 'quit' event
	for (bool done = false; !done; )
	{
		// wait for something interesting to happen
		HANDLE waitHandles[] = { hRequestEvent, hQuitEvent };
		switch (WaitForMultipleObjects(countof(waitHandles), waitHandles, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:
			// request - process it
			ProcessRequests();
			break;

		case WAIT_OBJECT_0 + 1:
			// quit event
			done = true;
			break;

		case WAIT_TIMEOUT:
		case WAIT_ABANDONED:
			// timeout/abandoned - ignore these
			continue;

		default:
			// error - abort
			done = true;
			break;
		}
	}

	// exit
	return 0;
}

void Application::AdminHost::ProcessRequests()
{
	// keep going until we've emptied the queue
	for (;;)
	{
		// grab the next request from the queue
		RefPtr<Request> req;
		{
			// acquire the object lock while manipulating the queue
			CriticalSectionLocker locker(lock);

			// if the queue is empty, we're done
			if (requests.size() == 0)
				return;

			// pull the next request off the queue
			req = requests.front();
			requests.pop_front();
		}

		// write the request to the pipe
		const TCHAR *writeData = req->request.get();
		DWORD writeLen = (DWORD)(req->requestCharLen * sizeof(TCHAR));
		DWORD actual;
		if (!WriteFile(hPipeOut, writeData, writeLen, &actual, NULL) || actual != writeLen)
		{
			// We failed to send the request properly; mark the request
			// as finished with no reply.  (It would be good do some kind
			// of error reporting here, perhaps via logging as there's no
			// clean way to present it in the UI given our context.)
			if (req->hEvent != NULL)
				SetEvent(req->hEvent);

			// we're done with this request
			continue;
		}

		// Successful write.  If the request has a wait event, the
		// caller who enqueued the request expects a reply, so read
		// the pipe to get the reply.
		if (req->hEvent != NULL)
		{
			// read the reply in non-blocking mode
			TCHAR readBuf[4096];
			if (!ReadFile(hPipeIn, readBuf, sizeof(readBuf), NULL, &ovRead)
				&& GetLastError() != ERROR_IO_PENDING)
			{
				// The read failed.  Simply mark the request as done
				// so that the caller doesn't get stuck.  (As above, it
				// would be good to do some error logging here.)
				SetEvent(req->hEvent);
				continue;
			}

			// Wait for the read to complete, or the 'quit' signal
			for (bool completed = false; !completed; )
			{
				HANDLE waitHandles[] = { hReadEvent, hQuitEvent };
				switch (WaitForMultipleObjects(countof(waitHandles), waitHandles, FALSE, INFINITE))
				{
				case WAIT_OBJECT_0:
					// Read event - the read completed.  Read the result.
					if (GetOverlappedResult(hPipeIn, &ovRead, &actual, FALSE))
					{
						// successful completion - copy the data to the reply
						// slot in the request object
						TCHAR *buf = new TCHAR[actual / sizeof(TCHAR)];
						memcpy(buf, readBuf, actual);
						req->reply.reset(buf);
						req->replyCharLen = actual / sizeof(TCHAR);
						req->success = true;
					}
					else
					{
						// read failed - as above, we should log an error somehow
					}

					// mark the request as completed
					SetEvent(req->hEvent);
					completed = true;
					break;

				case WAIT_OBJECT_0 + 1:
					// Quit signal - abort.  Mark the request as complete
					// before we return so that the caller doesn't get stuck
					// waiting for a reply that will never come.
					SetEvent(req->hEvent);
					return;

				case WAIT_TIMEOUT:
				case WAIT_ABANDONED:
					// ignore these cases; go back and try another wait
					break;

				default:
					// Error/other - abort.  (It might be good to log an error here.)
					SetEvent(req->hEvent);
					completed = true;
					break;
				}
			}
		}
	}
}

void Application::AdminHost::Shutdown() 
{
	// if there's a thread, terminate it
	if (hThread != NULL)
	{
		// tell the thread to exit
		SetEvent(hQuitEvent);

		// Give the thread a few moments to exit gracefully; if that fails,
		// try forcing it to exit.
		if (WaitForSingleObject(hThread, 5000) != WAIT_OBJECT_0)
			TerminateThread(hThread, 0);
	}
}

// -----------------------------------------------------------------------
//
// New file scan thread
//

Application::NewFileScanThread::NewFileScanThread() :
	hwndPlayfieldView(NULL)
{
}

Application::NewFileScanThread::~NewFileScanThread()
{
}

bool Application::NewFileScanThread::Launch()
{
	// do nothing if the playfield view is already closed
	auto pfv = Application::Get()->GetPlayfieldView();
	if (pfv == nullptr || !IsWindow(hwndPlayfieldView = pfv->GetHWnd()))
		return false;

	// add a self-reference on behalf of the new thread
	AddRef();

	// launch the thread - launch suspended so that we can complete
	// initialization before it executes
	DWORD tid;
	hThread = CreateThread(NULL, 0, &SMain, this, CREATE_SUSPENDED, &tid);

	// if that failed, drop the self-reference and fail
	if (hThread == nullptr)
	{
		Release();
		return false;
	}

	// reduce the thread's priority to minimize UI impact
	SetThreadPriority(hThread, BELOW_NORMAL_PRIORITY_CLASS);

	// Copy the table file set information from the game list.
	// We make a private copy of this to avoid any complications
	// from accessing the game list data from a thread.
	GameList::Get()->EnumTableFileSets([this](const TableFileSet &t) { dirs.emplace_back(t); });

	// let the thread start executing
	ResumeThread(hThread);

	// remember the active thread object in the application singleton
	Application::Get()->newFileScanThread = this;

	// success
	return true;
}

Application::NewFileScanThread::Directory::Directory(const TableFileSet &t) :
	path(t.tablePath),
	ext(t.defExt)
{
	// copy the file list
	for (auto &f : t.files)
		oldFiles.emplace(f.first);
}

DWORD WINAPI Application::NewFileScanThread::SMain(LPVOID lParam)
{
	// The lParam is our thread object.  Assume the thread's counted
	// reference into a local RefPtr, so that we'll automatically
	// release the thread's reference when we return.
	RefPtr<NewFileScanThread> th(static_cast<NewFileScanThread*>(lParam));

	// run the thread
	return th->Main();
}

// 'filesystem' access, for our directory scan
#include <filesystem>
namespace fs = std::experimental::filesystem;

DWORD Application::NewFileScanThread::Main()
{
	// log the scan
	GameList::LogGroup();
	GameList::Log(_T("Re-scanning for all systems' table files due to application activation\n"));
		
	// scan each directory in our list
	for (auto &d : dirs)
	{
		// scan this folder for files matching the extension for this set
		const TCHAR *ext = d.ext.c_str();
		TableFileSet::ScanFolder(d.path.c_str(), d.ext.c_str(), [&d](const TCHAR *filename)
		{
			// make the key by converting the name to lower-case
			TSTRING key(filename);
			std::transform(key.begin(), key.end(), key.begin(), ::_totlower);

			// if it's not in the old file set, add it to the new file list
			if (d.oldFiles.find(key) == d.oldFiles.end())
			{
				GameList::Log(_T("+ New file found: %s\n"), filename);
				d.newFiles.emplace_back(filename);
			}
		});
	}

	// If we found any new files, load them into the UI.  Do this on
	// the main UI thread rather than in the background thread, to
	// ensure that there are no conflicts with concurrent access to
	// the global game list.
	CallOnMainThread(hwndPlayfieldView, [this]() -> LRESULT
	{
		// Add all of the new files we found in each directory
		auto gl = GameList::Get();
		int nAdded = 0;
		for (auto &d : dirs)
			nAdded += gl->AddNewFiles(d.path, d.ext, d.newFiles);

		// If we added any new files, finalize the updates
		if (nAdded != 0)
		{
			// rebuild the title index to add the new entries
			gl->BuildTitleIndex();

			// rebuild the current filter to incorporate any new items
			// it selects
			gl->RefreshFilter();

			// update the filter and selection in the playfield view,
			// so that the new files are included in the wheel if
			// appropriate
			if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != nullptr)
				pfv->OnNewFilesAdded();
		}

		// the thread is now done with its work, so we can remove the
		// reference from the application object
		Application::Get()->newFileScanThread = nullptr;

		// done
		return 0;
	});

	// done (the thread return value isn't used)
	return 0;
}

// -----------------------------------------------------------------------
//
// Watchdog process interface
//



void Application::Watchdog::Launch()
{
	// create the pipes for communicating with the watchdog process
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;
	HandleHolder hChildInRead, hChildOutWrite;
	if (!CreatePipe(&hPipeRead, &hChildOutWrite, &sa, 1024)
		|| !CreatePipe(&hChildInRead, &hPipeWrite, &sa, 1024))
		return;

	// turn off handle inheritance for our ends of the pipes
	SetHandleInformation(hPipeRead, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(hPipeWrite, HANDLE_FLAG_INHERIT, 0);

	// build the watchdog exe name
	TCHAR exe[MAX_PATH];
	GetExeFilePath(exe, countof(exe));
	PathAppend(exe, _T("PinballY Watchdog.exe"));

	// set up the command line
	TSTRINGEx cmdline;
	cmdline.Format(_T(" -pid=%d"), GetCurrentProcessId());

	// set up the startup info
	STARTUPINFO si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_FORCEOFFFEEDBACK | STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	si.hStdInput = hChildInRead;
	si.hStdOutput = hChildOutWrite;
	si.hStdError = hChildOutWrite;

	// launch the process
	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof(pi));
	if (!CreateProcess(exe, cmdline.data(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
	{
		hPipeRead = NULL;
		hPipeWrite = NULL;
		return;
	}

	// remember the process handle, forget the thread handle
	hProc = pi.hProcess;
	CloseHandle(pi.hThread);
}

void Application::Watchdog::Notify(const char *msg)
{
	if (hPipeWrite != NULL)
	{
		DWORD actual;
		WriteFile(hPipeWrite, msg, (DWORD)strlen(msg) + 1, &actual, NULL);
	}
}
