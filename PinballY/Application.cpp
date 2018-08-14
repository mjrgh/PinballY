// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//


#include "stdafx.h"
#include <CommCtrl.h>
#include <winsafer.h>
#include <shellapi.h>
#include <TlHelp32.h>
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
#include "DateUtil.h"
#include "Application.h"
#include "GraphicsUtil.h"
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

// --------------------------------------------------------------------------
//
// Tell the linker to generate the manifest with common control support
//
#pragma comment(linker, "/manifestdependency:\"type='win32' \
    name='Microsoft.Windows.Common-Controls' \
    ""version='6.0.0.0' \
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
	static const TCHAR *MuteTableAudio = _T("TableAudio.Mute");
	static const TCHAR *EnableVideos = _T("Video.Enable");
	static const TCHAR *MuteAttractMode = _T("AttractMode.Mute");
	static const TCHAR *GameTimeout = _T("GameTimeout");
	static const TCHAR *HideTaskbarDuringGame = _T("HideTaskbarDuringGame");
	static const TCHAR *FirstRunTime = _T("FirstRunTime");
	static const TCHAR *HideUnconfiguredGames = _T("GameList.HideUnconfigured");
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

// --------------------------------------------------------------------------
//
// Run the application
//
int Application::Main(HINSTANCE hInstance, LPTSTR /*lpCmdLine*/, int nCmdShow)
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

	// create the application object
	std::unique_ptr<Application> appInst(new Application());

	// run the event loop
	return appInst->EventLoop(nCmdShow);
}

int Application::EventLoop(int nCmdShow)
{
	// parse arguments
	for (int i = 1; i < __argc; ++i)
	{
		const TCHAR *argp = __targv[i];
		std::match_results<const TCHAR*> m;
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
		MonitorCheck::WaitForMonitors(monWaitSpec);

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
	CapturingErrorHandler dofErrs;
	DOFClient::Init(dofErrs);

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

	// set up raw input through the main playfield window's message loop
	if (ok)
		ok = InputManager::GetInstance()->InitRawInput(playfieldWin->GetHWnd());

	// try setting up real DMD support
	if (ok)
		GetPlayfieldView()->InitRealDMD(InUiErrorHandler());

	// create the high scores reader object
	highScores.Attach(new HighScores());
	highScores->Init();

	// Generate a PINemHi version request on behalf of the main window
	highScores->GetVersion(GetPlayfieldView()->GetHWnd());

	// show the initial game selection in all windows
	SyncSelectedGame();

	// If we got this far, we were able to load at least part of the game
	// list, but there might have been errors or warnings from loading
	// parts of the list.  If there are any errors in the capture list, show 
	// them via a graphical popup.  That's less obtrusive than a system
	// message box, which is appropriate given that things are at least
	// partially working, but still lets the user know that something
	// might need attention.
	if (loadErrs.CountErrors() != 0)
		GetPlayfieldView()->ShowError(EIT_Error, LoadStringT(IDS_ERR_LISTLOADWARNINGS), &loadErrs);

	// If we ran into DOF errors, show those
	if (dofErrs.CountErrors() == 1)
		dofErrs.EnumErrors([this](const ErrorList::Item &item) { GetPlayfieldView()->ShowSysError(item.message.c_str(), item.details.c_str()); });
	else if (dofErrs.CountErrors() > 1)
		GetPlayfieldView()->ShowError(EIT_Error, LoadStringT(IDS_ERR_DOFLOAD), &dofErrs);

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

	// If there's a new file scanner thread running, give it a few seconds
	// to finish.
	if (newFileScanThread != nullptr)
		WaitForSingleObject(newFileScanThread->hThread, 5000);

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

	// save any updates to the config file or game databases
	SaveFiles();

	// check for a RunAfter program
	CheckRunAtExit();

	// Update the registry with our current Auto Launch setting.
	// Do this just before exiting, to avoid the potential problem
	// mentioned in the Windows API docs.  The docs warn that an
	// auto-launched program shouldn't change its own status by
	// writing to the auto-launch registry keys, because doing so
	// can interfere with the launching of other programs under
	// the same key.  Presumably, the issue is that editing a 
	// key's value can interrupt an enumeration of the values 
	// already in progress.  Assuming that's the problem, it
	// really should only be an issue during the early part of
	// our session, maybe the first 60 seconds or so, while the
	// shell is actively working through the auto-launch list.
	// Once the shell is done with that phase, it should be safe
	// to edit the list freely.  Waiting until we're about to
	// quit should almost always get us past that interval
	// where the updates could be problematic.
	SyncAutoLaunchInRegistry(InteractiveErrorHandler());

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
	DOFClient::Shutdown();

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
	GameList::Init();
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

	// delete the game list
	GameList::Shutdown();

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
	muteTableAudio = cfg->GetBool(ConfigVars::MuteTableAudio, false);
	muteAttractMode = cfg->GetBool(ConfigVars::MuteAttractMode, true);
	hideUnconfiguredGames = cfg->GetBool(ConfigVars::HideUnconfiguredGames, false);
}

void Application::SaveFiles()
{
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
	bool wait, HANDLE *phProcess)
{
	// no process handle yet
	if (phProcess != nullptr)
		*phProcess = NULL;

	// set up the startup info
	STARTUPINFO startupInfo;
	ZeroMemory(&startupInfo, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);

	// CreateProcess requires a writable buffer for the command line, so
	// copy it into a local string
	TSTRING cmdStr = cmd;

	// launch the process
	PROCESS_INFORMATION procInfo;
	if (!CreateProcess(NULL, cmdStr.data(), 0, 0, false, 0, 0,
		NULL, &startupInfo, &procInfo))
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

		// the process was successfully launched
		return true;
	}
}

bool Application::SyncAutoLaunchInRegistry(ErrorHandler &eh)
{
	LONG err;
	auto ReturnError = [&err, &eh](const TCHAR *where)
	{
		WindowsErrorMessage sysErr(err);
		eh.SysError(LoadStringT(IDS_ERR_SYNCAUTOLAUNCHREG),
			MsgFmt(_T("%s: system error %d: %s"), where, err, sysErr.Get()));
		return false;
	};

	// get the current auto-launch status
	bool autoLaunch = IsAutoLaunch();

	// If auto-launch is on, figure the new launch command.
	TSTRINGEx launchCmd;
	if (autoLaunch)
	{
		// get the executable path
		TCHAR exe[MAX_PATH];
		GetModuleFileName(G_hInstance, exe, MAX_PATH);

		// build the command string
		launchCmd.Format(_T("\"%s\""), exe);
	}

	// open the relevant registry key
	HKEYHolder hkey;
	const TCHAR *keyName = HKLM_SOFTWARE_Microsoft_Windows _T("\\Run");		
	if ((err = RegOpenKey(HKEY_CURRENT_USER, keyName, &hkey)) != ERROR_SUCCESS)
		return ReturnError(MsgFmt(_T("Opening %s"), keyName));

	// presume we'll need to update the value
	bool needUpdate = true;

	// query the current value
	DWORD typ;
	DWORD len;
	const TCHAR *valName = _T("PinballY");
	err = RegQueryValueEx(hkey, valName, NULL, &typ, NULL, &len);
	if (err == ERROR_SUCCESS)
	{
		// The value is present.  If auto-launch is turned off, simply delete
		// the value. 
		if (!autoLaunch)
		{
			// delete the key
			if ((err = RegDeleteValue(hkey, valName)) != ERROR_SUCCESS)
				return ReturnError(MsgFmt(_T("Deleting %s[%s]"), keyName, valName));

			// success
			return true;
		}
		else
		{
			// The key is present, so determine if it already has the correct value. 
			// If it's not a string value, it's definitely wrong; otherwise, retrieve
			// the string and compare it to the new setting.
			if (typ == REG_SZ)
			{
				// allocate space and retrieve the value
				std::unique_ptr<TCHAR> oldval((TCHAR*)new BYTE[len]);
				if ((err = RegQueryValueEx(hkey, valName, NULL, &typ, (LPBYTE)oldval.get(), &len)) != ERROR_SUCCESS)
					return ReturnError(MsgFmt(_T("Value query for %s[%s]"), keyName, valName));

				// we need an update if the value doesn't match the new command
				needUpdate = _tcsicmp(oldval.get(), launchCmd.c_str()) != 0;
			}
		}
	}
	else if (err == ERROR_FILE_NOT_FOUND)
	{
		// The key doesn't exist.  We'll need to update it if auto-launch
		// is turned on.
		needUpdate = autoLaunch;
	}
	else
		return ReturnError(MsgFmt(_T("Initial value query for %s[%s]"), keyName, valName));

	// If auto-launch is turned on and a registry update is needed, 
	// write the new value
	if (autoLaunch && needUpdate)
	{
		// write the value
		if ((err = RegSetValueEx(hkey, valName, 0, REG_SZ, (BYTE*)launchCmd.c_str(),
			(DWORD)((launchCmd.length() + 1) * sizeof(TCHAR)))) != ERROR_SUCCESS)
			return ReturnError(MsgFmt(_T("Updating %s[%s] to %s"), keyName, valName, launchCmd.c_str()));
	}

	// success
	return true;
}

bool Application::IsAutoLaunch() const
{
	return ConfigManager::GetInstance()->GetBool(_T("AutoLaunch"), false);
}

void Application::SetAutoLaunch(bool f)
{
	ConfigManager::GetInstance()->SetBool(_T("AutoLaunch"), f);
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

		// notify the playfield view
		if (auto pfv = GetPlayfieldView(); pfv != 0)
			pfv->OnAppActivationChange(activating);

		// If we're newly in the foreground, and a new file scanner
		// thread isn't already running, launch one.  This looks for
		// new game files that were added since we last checked, so
		// that we can dynamically incorporate newly downloaded games
		// into the UI without having to restart the program.
		if (activating && !IsNewFileScanRunning())
		{
			// Creaet and launch a new file scanner thread.  If the
			// launch succeeds, stash it in our thread pointer so that
			// we can check its progress later (as we just did above).
			RefPtr<NewFileScanThread> t(new NewFileScanThread());
			if (t->Launch())
				newFileScanThread = t;
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

void Application::BeginRunningGameMode()
{
	// Put the backglass, DMD, and topper windows into running-game mode.  
	// Note that it's not necessary to notify the playfield window, since 
	// it initiates this process.
	if (auto bgv = GetBackglassView(); bgv != nullptr)
		bgv->BeginRunningGameMode();
	if (auto dmv = GetDMDView(); dmv != nullptr)
		dmv->BeginRunningGameMode();
	if (auto tpv = GetTopperView(); tpv != nullptr)
		tpv->BeginRunningGameMode();
	if (auto ic = GetInstCardView(); ic != nullptr)
		ic->BeginRunningGameMode();
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
}

bool Application::Launch(int cmd, GameListItem *game, GameSystem *system, 
	const std::list<LaunchCaptureItem> *capture, int captureStartupDelay,
	ErrorHandler &eh)
{
	// if there's already a game monitor thread, shut it down
	if (gameMonitor != 0)
	{
		// shut it down
		gameMonitor->Shutdown(eh, 500, false);

		// forget it
		gameMonitor = 0;
	}

	// create a new monitor thread
	gameMonitor.Attach(new GameMonitorThread());

	// launch it
	return gameMonitor->Launch(cmd, game, system, capture, captureStartupDelay, eh);
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

void Application::CleanGameMonitor()
{
	// if the game monitor thread has exited, remove our reference
	if (gameMonitor != nullptr && !gameMonitor->IsThreadRunning())
	{
		// Update the run time for the game, assuming it was a normal
		// "play" run (don't count media capture runs as plays).
		if (gameMonitor->cmd == ID_PLAY_GAME)
		{
			// figure the total run time in seconds
			int seconds = (int)((gameMonitor->exitTime - gameMonitor->launchTime) / 1000);

			// add the time to the game's row in the stats database
			GameList *gl = GameList::Get();
			int row = gl->GetStatsDbRow(gameMonitor->gameId.c_str(), true);
			gl->playTimeCol->Set(row, gl->playTimeCol->GetInt(row, 0) + seconds);
		}

		// forget the game monitor thread
		gameMonitor = nullptr;
	}
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
		UpdateVideoMuting();
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
		UpdateVideoMuting();
	}
}

void Application::UpdateVideoMuting()
{
	// get the active muting status
	bool mute = IsMuteVideosNow();

	// update any playing videos in the windows that host them
	auto DoMute = [mute](D3DView *view)
	{
		auto callback = [mute](Sprite *sprite)
		{
			if (auto video = dynamic_cast<VideoSprite*>(sprite); video != nullptr)
				if (auto player = video->GetVideoPlayer(); player != nullptr)
					player->Mute(mute);
		};
		if (view != nullptr)
			view->ForDrawingList(callback);
	};
	DoMute(GetPlayfieldView());
	DoMute(GetBackglassView());
	DoMute(GetDMDView());
	DoMute(GetTopperView());
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

// -----------------------------------------------------------------------
//
// Game monitor thread
//

Application::GameMonitorThread::GameMonitorThread() :
	isAdminMode(false),
	hideTaskbar(false)
{
	// create the shutdown and close-game event objects
	shutdownEvent = CreateEvent(0, TRUE, FALSE, 0);
	closeEvent = CreateEvent(0, TRUE, FALSE, 0);

	// keep a reference on the playfield view, since we send it messages
	// about our status
	playfieldView = Application::Get()->GetPlayfieldView();
}

Application::GameMonitorThread::~GameMonitorThread()
{
	// If we have a handle to the RunBefore process, it means that it uses
	// [NOWAIT TERMINATE] mode, which means that we left the program running
	// while playing the game, and that we're meant to terminate the program
	// when the game terminates.  
	if (hRunBeforeProc != NULL)
		SaferTerminateProcess(hRunBeforeProc);

	// Likewise, if we have an outstanding RunAfter process handle, kill it.
	// A RunAfter process will only be left running if we launched it in
	// "wait" mode and the wait failed, either due to an error or due to
	// the user canceling the game launch.
	if (hRunAfterProc != NULL)
		SaferTerminateProcess(hRunAfterProc);
}

bool Application::GameMonitorThread::IsThreadRunning()
{
	return hThread != 0 && WaitForSingleObject(hThread, 0) == WAIT_TIMEOUT;
}

bool Application::GameMonitorThread::IsGameRunning()
{
	return hGameProc != 0 && WaitForSingleObject(hGameProc, 0) == WAIT_TIMEOUT;
}

void Application::GameMonitorThread::CloseGame()
{
	// signal the close-game event
	SetEvent(closeEvent);

	// if the game is running, close its windows
	if (IsGameRunning())
	{
		// Try closing one game window at a time.  Repeat until we
		// don't find any windows to close, or we reach a maximum
		// retry limit (so that we don't get stuck if the game 
		// refuses to close).
		for (int tries = 0; tries < 20; ++tries)
		{
			// look for a window to close
			struct CloseContext
			{
				CloseContext(HANDLE hGameProc) : found(false), hGameProc(hGameProc) { }
				bool found;
				HANDLE hGameProc;
			} closeCtx(hGameProc);
			EnumThreadWindows(tidMainGameThread, [](HWND hWnd, LPARAM lParam)
			{
				// get the context
				auto ctx = reinterpret_cast<CloseContext*>(lParam);

				// Try bringing our main window to the foreground before 
				// closing the game window, so that the taskbar doesn't
				// reappear between closing the game window and activating
				// our window, assuming we're in full-screen mode.  Explorer 
				// normally hides the taskbar when a full-screen window is
				// in front, but only when it's in front.
				if (auto pfw = Application::Get()->GetPlayfieldWin(); pfw != nullptr)
				{
					// inject a call to the child process to set our window
					// as the foreground
					DWORD tid;
					HandleHolder hRemoteThread = CreateRemoteThread(
						ctx->hGameProc, NULL, 0,
						(LPTHREAD_START_ROUTINE)&SetForegroundWindow, pfw->GetHWnd(),
						0, &tid);

					// explicitly set our foreground window
					SetForegroundWindow(pfw->GetHWnd());
				}

				// If the window is visible and enabled, close it.  Don't try 
				// to close hidden or disabled windows; doing so can crash VP
				// if it's showing a dialog.
				if (IsWindowVisible(hWnd) && IsWindowEnabled(hWnd))
				{
					// this window looks safe to close - try closing it
					SendMessage(hWnd, WM_SYSCOMMAND, SC_CLOSE, 0);

					// note that we found something to close, and stop the
					// enumeration
					ctx->found = true;
					return FALSE;
				}

				// continue the enumeration
				return TRUE;
			}, reinterpret_cast<LPARAM>(&closeCtx));

			// if we didn't find any windows to close on this pass, stop
			// looping
			if (!closeCtx.found)
				break;

			// pause briefly between iterations to give the program a chance
			// to update its windows; stop if the process exits
			if (hGameProc == NULL || WaitForSingleObject(hGameProc, 1000) != WAIT_TIMEOUT)
				break;
		}

		// If the game is still running, resort to stronger measures:
		// attempt to kill it at the process level.  It's not unheard
		// of for VP to crash, which makes it futile to try to kill it
		// by closing windows, and The Pinball Arcade seems very prone
		// to going into an unresponsive state rather than terminating
		// when we close its window.
		if (hGameProc != NULL && WaitForSingleObject(hGameProc, 0) == WAIT_TIMEOUT)
			SaferTerminateProcess(hGameProc);
	}
}

void Application::GameMonitorThread::BringToForeground()
{
	if (IsGameRunning())
	{
		// find the other app's first window
		EnumThreadWindows(tidMainGameThread, [](HWND hWnd, LPARAM lparam)
		{
			// only consider visible windows with no owner
			if (IsWindowVisible(hWnd) && GetWindowOwner(hWnd) == 0)
			{
				// bring it to the front
				BringWindowToTop(hWnd);

				// stop the enumeration
				return FALSE;
			}

			// continue the enumeration otherwise
			return TRUE;
		}, (LPARAM)0);
	}
}

bool Application::GameMonitorThread::Launch(
	int cmd, GameListItem *game, GameSystem *system, 
	const std::list<LaunchCaptureItem> *captureList, int captureStartupDelay,
	ErrorHandler &eh)
{
	// save the game information
	this->cmd = cmd;
	this->game = *game;
	this->gameId = game->GetGameId();
	this->gameSys = *system;
	this->elevationApproved = system->elevationApproved;

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
	if (cmd == ID_CAPTURE_GO && captureList != nullptr)
	{
		// Keep a running total of the capture time as we go.  Start
		// with some fixed overhead for our own initialization.
		const DWORD initTime = 3000;
		DWORD totalTime = initTime;

		// remember the startup delay
		auto cfg = ConfigManager::GetInstance();
		capture.startupDelay = captureStartupDelay * 1000;
		totalTime += capture.startupDelay;

		// remember the two-pass encoding option
		capture.twoPassEncoding = cfg->GetBool(ConfigVars::CaptureTwoPassEncoding, false);

		// build our local list of capture items
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

			// add it to the total time, plus a couple of seconds of
			// overhead launching the capture program
			totalTime += item.captureTime + 2000;

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
				totalTime += item.captureTime*3/2;

			// get the source window's rotation
			item.windowRotation = cap.win->GetRotation();

			// remember the desired rotation for the stored image
			item.mediaRotation = cap.mediaType.rotation;

			// get the client area of the view window, adjusted to
			// screen coordinates
			HWND hwndView = cap.win->GetHWnd();
			GetClientRect(hwndView, &item.rc);
			POINT pt = { 0, 0 };
			ClientToScreen(hwndView, &pt);
			OffsetRect(&item.rc, pt.x, pt.y);
		}

		// create the status window
		capture.statusWin.Attach(new CaptureStatusWin());
		capture.statusWin->Create(NULL, _T("PinballY"), WS_POPUP, SW_SHOWNOACTIVATE);
		capture.statusWin->SetTotalTime(totalTime);
		capture.statusWin->SetCaptureStatus(LoadStringT(IDS_CAPSTAT_INITING), initTime);
	}

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

	// update the last launch time for the game
	GameList *gl = GameList::Get();
	gl->SetLastPlayedNow(game);

	// update the play count for the game
	gl->SetPlayCount(game, gl->GetPlayCount(game) + 1);

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
		self->playfieldView->PostMessage(PFVMsgGameOver);

	// The caller (in the main thread) adds a reference to the 'this'
	// object on behalf of the thread, to ensure that the object can't
	// be deleted as long as the thread is running.  Now that the
	// thread is just about to exit, release our reference.
	self->Release();

	// return the exit code from the main thread handler
	return result;
}

DWORD Application::GameMonitorThread::Main()
{
	// Get the game filename from the database, and build the full path
	TSTRING gameFile = game.filename;
	TCHAR gameFileWithPath[MAX_PATH];
	PathCombine(gameFileWithPath, gameSys.tablePath.c_str(), gameFile.c_str());
	LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ launch: full table path %s\n"), gameFileWithPath);

	// If PinVol is running, send it a message on its mailslot with the
	// game file and title.  This lets it show the title in its on-screen
	// display text rather than the filename.  PinVol infers which game
	// is running from the window title of the foreground app, and the
	// apps usually only include the filename there.
	if (HandleHolder mailslot(CreateFile(_T("\\\\.\\mailslot\\Pinscape.PinVol"),
		GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
		mailslot != NULL && mailslot != INVALID_HANDLE_VALUE)
	{
		// Prepare the message: "game <filename>|<title>", in WCHAR
		// (16-bit unicode) characters.
		WSTRINGEx msg;
		msg.Format(L"game %s|%s", TSTRINGToWSTRING(gameFile).c_str(), TSTRINGToWSTRING(game.title).c_str());

		// Write the message to the mailslot.  Ignore errors, as the only
		// harm if we fail is that PinVol won't have the title to display.
		DWORD actual;
		WriteFile(mailslot, msg.c_str(), (DWORD)(msg.length() * sizeof(WCHAR)), &actual, NULL);
	}

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

	// Substitute parameter variables in a command line
	auto SubstituteVars = [this, &gameFile](const TSTRING &str) -> TSTRING
	{
		std::basic_regex<TCHAR> pat(_T("\\[(\\w+)\\]"));
		return regex_replace(str, pat, [this, &gameFile](const std::match_results<TSTRING::const_iterator> &m) -> TSTRING
		{
			// get the variable name in all caps
			TSTRING var = m[1].str();
			std::transform(var.begin(), var.end(), var.begin(), ::_totupper);

			// check for known substitution variable names
			if (var == _T("TABLEPATH"))
			{
				return gameSys.tablePath;
			}
			else if (var == _T("TABLEFILE"))
			{
				return gameFile;
			}
			else
			{
				// not matched - return the full original string unchanged
				return m[0].str();
			}
		});
	};

	// RunBefore/RunAfter option flag parser
	class RunOptions
	{
	public:
		RunOptions(const TSTRING &command) : 
			nowait(false), 
			terminate(false)
		{
			std::basic_regex<TCHAR> flagsPat(_T("\\s*\\[((NOWAIT|TERMINATE)(\\s+(NOWAIT|TERMINATE))*)\\]\\s*(.*)"));
			std::match_results<TSTRING::const_iterator> m;
			if (std::regex_match(command, m, flagsPat))
			{
				// extract the flags
				const TSTRING &flags = m[1].str();

				// Pull out the actual command string, minus the option flags
				this->command = m[5].str();

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

					// match this token
					size_t len = p - start;
					if (_tcsncmp(start, _T("NOWAIT"), len) == 0)
						nowait = true;
					else if (_tcsncmp(start, _T("TERMINATE"), len) == 0)
						terminate = true;
				}
			}
			else
			{
				// no flags - use the command string as-is
				this->command = command;
			}
		}

		// updated command string
		TSTRING command;

		// option flags
		bool nowait;
		bool terminate;
	};

	// Before we launch the game, check for a RunBefore command
	if (gameSys.runBefore.length() != 0)
	{
		// Parse option flags
		RunOptions options(gameSys.runBefore);

		// log the launch
		LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ run before launch:\n> %s\n"), options.command.c_str());

		// Launch the program without waiting
		AsyncErrorHandler aeh;
		if (!Application::RunCommand(SubstituteVars(options.command).c_str(), 
			aeh, IDS_ERR_GAMERUNBEFORE, false, &hRunBeforeProc))
			return 0;

		// Now wait for it, if it's not in NOWAIT mode.  Note that we
		// have to wait explicitly here, rather than letting RunCommand
		// handle the wait, because we need to also stop waiting if we
		// get a shutdown signal.
		if (options.nowait)
		{
			// NOWAIT mode.  We can simply leave the process running.
			// If TERMINATE mode is set, leave the process handle in
			// hRunBeforeProc, so that the thread object destructor
			// will know to terminate the process when the monitor
			// thread exits.  If TERMINATE mode isn't set, though, 
			// the user wants us to simply launch the process and
			// leave it running, so we can close the process handle
			// now and let the process run independently from now on.
			LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ run before launch: [NOWAIT] specified, continuing\n"));
			if (!options.terminate)
				hRunBeforeProc = NULL;
		}
		else
		{
			// Wait mode.  Wait for the process to exit, or for a 
			// close-game or application-wide shutdown signal.
			LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ run before launch: waiting for command to finish\n"));
			HANDLE waitEvents[] = { hRunBeforeProc, shutdownEvent, closeEvent };
			switch (WaitForMultipleObjects(countof(waitEvents), waitEvents, FALSE, INFINITE))
			{
			case WAIT_OBJECT_0:
				// The RunBefore process exited.  This is what we were
				// hoping for; proceed to run the game.  Close the child
				// process handle and continue.
				LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ run before launch: command finished\n"));
				hRunBeforeProc = NULL;
				break;

			case WAIT_OBJECT_0 + 1:
			case WAIT_OBJECT_0 + 2:
			default:
				// The shutdown event fired, the "close game" event fired, or 
				// an error occurred in the wait.  In any of these cases, shut
				// down the monitor thread immediately, without proceeding to 
				// the game launch.  
				//
				// What should we do about the RunBefore process?  Given that
				// the user wanted us to wait for the process, it's highly 
				// likely that the process is supposed to be something quick 
				// that does some small amount of work and exits immediately.
				// The user presumably wouldn't have configured it for waiting 
				// if it were something long-running.  In any case, by telling
				// us to wait in the first place, the user told us that the
				// program was to finish before the game was launched, and by
				// implication, before we return to the wheel UI.  So if the
				// process hasn't exited on its own, the reasonable thing to
				// do is to terminate it explicitly, to meet the user's
				// expectation that the program is done when we get back to
				// the wheel UI.  In fact, one reason we might be in this
				// situation at all is that the RunBefore program might have
				// gotten stuck, prompting the user to cancel the launch from
				// the UI, which would have fired the shutdown event and 
				// landed us right here.  In any case, we can ensure that the
				// RunBefore process gets terminated explicitly by leaving its
				// process handle in hRunBeforeProc.  The game monitor thread
				// object destructor will use that to kill the process if it's
				// still running, as soon as the thread exits.
				LogFile::Get()->Write(LogFile::TableLaunchLogging, 
					_T("+ run before launch: Run Before command interrupted; aborting launch\n"));
				return 0;
			}
		}		
	}

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
	const TCHAR *exe = gameSys.exe.c_str();

	// Check if the file exists.  If not, add the default extension.
	if (!FileExists(gameFileWithPath) && gameSys.defExt.length() != 0)
	{
		// The file doesn't exist.  Try adding the default extension.
		TCHAR gameFileWithPathExt[MAX_PATH];
		_stprintf_s(gameFileWithPathExt, _T("%s%s"), gameFileWithPath, gameSys.defExt.c_str());

		// log the attempt
		LogFile::Get()->Write(LogFile::TableLaunchLogging,
			_T("+ table launch: table file %s doesn't exist; try adding extension -> %s\n"),
			gameFileWithPath, gameFileWithPathExt);

		// if the file + extension exists, use that instead of the original
		if (FileExists(gameFileWithPathExt))
		{
			// log it
			LogFile::Get()->Write(LogFile::TableLaunchLogging, 
				_T("+ table launch: file + extension (%s) exists, using it\n"), gameFileWithPathExt);

			// use the path + extension version, and also add the extension
			// to the base game file name
			_tcscpy_s(gameFileWithPath, gameFileWithPathExt);
			gameFile.append(gameSys.defExt);
		}
		else
		{
			// log that neither file exists
			LogFile::Get()->Write(LogFile::TableLaunchLogging,
				_T("+ table launch: file + extension (%s) doesn't exist either; sticking with original name (%s)\n"), 
					gameFileWithPathExt, gameFileWithPath);
		}
	}

	// Replace substitution variables in the command-line parameters
	TSTRING cmdline = SubstituteVars(gameSys.params);
	LogFile::Get()->Write(LogFile::TableLaunchLogging,
		_T("+ table launch: executable: %s\n")
		_T("+ table launch: applying command line variable substitutions:\n+ Original> %s\n+ Final   > %s\n"),
		exe, gameSys.params.c_str(), cmdline.c_str());

	// set up the startup information struct
	STARTUPINFO startupInfo;
	ZeroMemory(&startupInfo, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESHOWWINDOW;
	startupInfo.wShowWindow = SW_SHOWMINIMIZED;

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

	// Try launching the new process
	PROCESS_INFORMATION procInfo;
	ZeroMemory(&procInfo, sizeof(procInfo));
	if (!CreateProcess(exe, cmdline.data(), 0, 0, false, 0, NULL,
		gameSys.workingPath.c_str(), &startupInfo, &procInfo))
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
				exe, cmdline.data(), 0, 0, false, 0, 0,
				gameSys.workingPath.c_str(), &startupInfo, &procInfo))
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
			const TCHAR *request[] = {
				_T("run"),
				exe,
				gameSys.workingPath.c_str(),
				cmdline.c_str(),
				gameInactivityTimeout.c_str()
			};

			// Allow the admin host to set the foreground window when the
			// new game starts
			AllowSetForegroundWindow(adminHost.pid);

			// Send the request
			std::vector<TSTRING> reply;
			if (adminHost.SendRequest(request, countof(request), reply))
			{
				// successfully sent the launch request - parse the reply
				if (reply[0] == _T("ok") && reply.size() >= 2)
				{
					// Successful launch.  The first parameter item in the
					// reply is the process ID of the new process.  We can use
					// this to open a handle to the process.  Note that this
					// is allowed even though the new process is elevated: a
					// non-elevated process is allowed to open a handle to an
					// elevated process, but there are restrictions on what
					// types of access we can request.  SYNCHRONIZE (to wait
					// for the process to exit) is one of the allowed access
					// rights, as is "query limited information".
					// 
					// Plug the process handle into the PROCESS_INFORMATION
					// struct that we'd normally get back from CreateProcess(),
					// to emulate normal process creation.  Leave the thread
					// handle empty.
					//
					LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: Admin mode launch succeeded\n"));
					procInfo.dwProcessId = (DWORD)_ttol(reply[1].c_str());
					procInfo.hProcess = OpenProcess(
						SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 
						procInfo.dwProcessId);
					procInfo.dwThreadId = 0;
					procInfo.hThread = NULL;
				}
				else if (reply[0] == _T("error") && reply.size() >= 2)
				{
					// Error, with technical error text in the first parameter
					const TCHAR *errmsg = reply[1].c_str();
					LogFile::Get()->Write(LogFile::TableLaunchLogging,
						_T("+ table launch: Admin launch failed: %s\n"), errmsg);
 
					// send the error to the playfield view for display
					if (playfieldView != nullptr)
						playfieldView->SendMessage(PFVMsgGameLaunchError, 0, reinterpret_cast<LPARAM>(errmsg));

					// return failure
					return 0;
				}
				else
				{
					// Unknown response
					const TCHAR *unk = reply[0].c_str();
					LogFile::Get()->Write(LogFile::TableLaunchLogging,
						_T("+ table launch: Admin launch failed: unexpected response from Admin Host \"%s\"\n"), unk);

					// send the error to the playfield view for display
					if (playfieldView != nullptr)
						playfieldView->SendMessage(PFVMsgGameLaunchError, 0,
							reinterpret_cast<LPARAM>(MsgFmt(
								_T("Unexpected response from Admin Host: \"%s\""), unk).Get()));

					// return failure
					return 0;
				}
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
						reinterpret_cast<LPARAM>(gameId.c_str()));
					break;

				default:
					// use the generic error message for anything else
					playfieldView->SendMessage(PFVMsgGameLaunchError, 
						reinterpret_cast<WPARAM>(gameId.c_str()),
						reinterpret_cast<LPARAM>(sysErr.Get()));
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

	// remember the new process's handle and main thread ID
	hGameProc = procInfo.hProcess;
	tidMainGameThread = procInfo.dwThreadId;

	// wait for the process to start up
	if (!WaitForStartup())
		return 0;

	// if we don't know the main thread ID yet, find it
	while (tidMainGameThread == 0 && FindMainWindowForProcess(GetProcessId(hGameProc), &tidMainGameThread) == NULL)
	{
		// pause for a bit, exiting the thread if we get a Shutdown
		// or Close Game signal
		HANDLE waitHandles[] = { shutdownEvent, closeEvent };
		if (WaitForMultipleObjects(countof(waitHandles), waitHandles, false, 500) != WAIT_TIMEOUT)
		{
			LogFile::Get()->Write(LogFile::TableLaunchLogging,
				_T("+ table launch interrupted (waiting for first window in child process to open)\n"));
			return 0;
		}
	}

	// The Steam-based systems use a staged launch, where we launch
	// Steam.exe, and that in turn launches the actual program.  At the
	// moment, Steam is the only thing that works this way, but for the
	// sake of generality, we handle this with a "Process" parameter in
	// the game system configuration, which tells us that we need to
	// monitor a different process from the one we actually launched.
	if (gameSys.process.length() != 0)
	{
		// we're going to wait for a second process
		LogFile::Get()->Write(LogFile::TableLaunchLogging, 
			_T("+ table launch: waiting for secondary process %s to start\n"), gameSys.process.c_str());

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
					playfieldView->SendMessage(PFVMsgGameLaunchError, 0,
					(LPARAM)MsgFmt(_T("Error getting process snapshot: %s"), sysErr.Get()).Get());

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
					if (_tcsicmp(gameSys.process.c_str(), procInfo.szExeFile) == 0)
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

							// make sure this process has finished starting up
							if (!WaitForStartup())
								return false;

							// Find the thread with the UI window(s) for the new process.
							// As with waiting for startup, it might take a while for the
							// new process to open its main window.  So retry until we 
							// find the window we're looking for, encounter an error, or
							// receive an Application Shutdown or Close Game signal.
							while (FindMainWindowForProcess(procInfo.th32ProcessID, &tidMainGameThread) == NULL)
							{
								// pause for a bit, exiting the thread if we get a Shutdown
								// or Close Game signal
								HANDLE waitHandles[] = { shutdownEvent, closeEvent };
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
			if (WaitForSingleObject(hGameProc, 0) == WAIT_OBJECT_0
				&& ++triesSinceFirstStageExited > 10)
			{
				// It's been too long; we can probably assume the new process
				// isn't going to start.
				if (playfieldView != nullptr)
					playfieldView->SendMessage(PFVMsgGameLaunchError, 0,
					(LPARAM)MsgFmt(_T("Launcher process exited, target process %s hasn't started"), gameSys.process.c_str()).Get());

				// abort the launch
				LogFile::Get()->Write(LogFile::TableLaunchLogging,
					_T("+ table launch: launcher process exited, target process %s hasn't started;")
					_T(" assuming failure and aborting launch\n"),
					gameSys.process.c_str());
				return 0;
			}

			// do a brief pause, unless a Shutdown or Close Game event fired
			HANDLE waitHandles[] = { shutdownEvent, closeEvent };
			if (WaitForMultipleObjects(countof(waitHandles), waitHandles, false, 1000) != WAIT_TIMEOUT)
			{
				// uh oh - one of the exit events has fired; abort immediately
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
	if (playfieldView != 0)
		playfieldView->PostMessage(PFVMsgGameLoaded, (WPARAM)cmd);

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
	if (cmd == ID_CAPTURE_GO)
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
		TSTRINGEx overallStatus;

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
				overallStatus.Load(IDS_ERR_CAP_GAME_EXITED);
				captureOkay = false;
				abortCapture = true;
			}
		}

		// Get the path to ffmpeg.exe
		TCHAR ffmpeg[MAX_PATH];
		GetDeployedFilePath(ffmpeg, _T("ffmpeg\\ffmpeg.exe"), _T(""));

		// Audio capture device name, to pass to ffmpeg.  We populate
		// this the first time we need it.
		TSTRING audioCaptureDevice;

		// Capture one item.  Returns true to continue capturing
		// additional items, false to end the capture process.
		// A true return doesn't necessarily mean that the 
		// individual capture succeeded; it just means that we
		// didn't run into a condition that ends the whole
		// process, such as the game exiting prematurely.
		for (auto &item: capture.items)
		{
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

			// set the status window message
			curStatus.Format(LoadStringT(IDS_CAPSTAT_ITEM), itemDesc.c_str());
			capture.statusWin->SetCaptureStatus(curStatus, item.captureTime);

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
				// friendly name pattern we're scanning for
				std::basic_regex<WCHAR> stmixPat(L"\\bstereo mix\\b", std::regex_constants::icase);

				// create the audio device enumerator
				RefPtr<ICreateDevEnum> pCreateDevEnum;
				RefPtr<IEnumMoniker> pEnumMoniker;
				LPMALLOC coMalloc = nullptr;
				if (SUCCEEDED(CoGetMalloc(1, &coMalloc))
					&& SUCCEEDED(CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pCreateDevEnum)))
					&& SUCCEEDED(pCreateDevEnum->CreateClassEnumerator(CLSID_AudioInputDeviceCategory, &pEnumMoniker, 0)))
				{
					// scan through the audio devices
					RefPtr<IMoniker> m;
					while (pEnumMoniker->Next(1, &m, NULL) == S_OK)
					{
						// get the friendly name from the object's properties
						RefPtr<IBindCtx> bindCtx;
						RefPtr<IPropertyBag> propertyBag;
						VARIANTEx v(VT_BSTR);
						if (SUCCEEDED(CreateBindCtx(0, &bindCtx))
							&& SUCCEEDED(m->BindToStorage(bindCtx, NULL, IID_PPV_ARGS(&propertyBag)))
							&& SUCCEEDED(propertyBag->Read(L"FriendlyName", &v, NULL)))
						{
							// check if the name matches our pattern
							if (std::regex_search(v.bstrVal, stmixPat))
							{
								// use this source
								audioCaptureDevice = v.bstrVal;
								break;
							}
						}
					}
				}

				// if a capture device isn't available, skip this item
				if (audioCaptureDevice.length() == 0)
				{
					statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), LoadStringT(IDS_ERR_CAP_NO_AUDIO_DEV).c_str()));
					captureOkay = false;
					continue;
				}
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

			// if the file still exists, skip it
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
			if (!DirectoryExists(dir) && !CreateSubDirectory(dir, _T(""), NULL))
			{
				WindowsErrorMessage winErr;
				statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), winErr.Get()));
				captureOkay = false;
				continue;
			}

			// Figure the required image/video rotation parameter for ffmpeg.
			// Note that this doesn't apply to audio-only capture.
			int rotate = item.mediaRotation - item.windowRotation;
			const TCHAR *rotateOpt = _T("");
			if (item.mediaType.format != MediaType::Audio)
			{
				switch ((rotate + 360) % 360)
				{
				case 90:
					rotateOpt = _T("-vf \"transpose=1\"");  // 90 degrees clockwise
					break;

				case 180:
					rotateOpt = _T("-vf \"hflip,vflip\"");  // mirror both axes
					break;

				case 270:
					rotateOpt = _T("-vf \"transpose=2\"");	// 90 degrees counterclockwise
					break;
				}
			}

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

			// set up format-dependent options
			TSTRINGEx audioOpts;
			TSTRINGEx timeLimitOpt;
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
				timeLimitOpt.Format(_T("-t %d"), item.captureTime / 1000);
				audioOpts = _T("-c:a none");
				break;

			case MediaType::VideoWithAudio:
				// video capture with optional audio
				isVideo = true;
				timeLimitOpt.Format(_T("-t %d"), item.captureTime / 1000);
				if (item.enableAudio)
					audioOpts.Format(_T("-f dshow -i audio=\"%s\""), audioCaptureDevice.c_str());
				else
					audioOpts = _T("-c:a none");
				break;

			case MediaType::Audio:
				// audio only
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
				// Two-pass encoding.  Capture the video with the lossless h265
				// code in the fastest mode, with no rotation, to a temp file.
				// We'll re-encode to the actual output file and apply rotations
				// in the second pass.
				tmpfile = std::regex_replace(item.filename, std::basic_regex<TCHAR>(_T("\\.([^.]+)$")), _T(".tmp.$1"));
				cmdline1.Format(_T("\"%s\" -loglevel error")
					_T(" %s %s %s %s -c:v libx264 -crf 0 -preset ultrafast \"%s\""),
					ffmpeg, 
					imageOpts.c_str(), audioOpts.c_str(), timeLimitOpt.c_str(), tmpfile.c_str());

				// Format the command line for the second pass while we're here
				cmdline2.Format(_T("\"%s\" -loglevel error")
					_T(" -i \"%s\"")
					_T(" %s -c:a copy -max_muxing_queue_size 1024")
					_T(" \"%s\""),
					ffmpeg,
					tmpfile.c_str(),
					rotateOpt, 
					item.filename.c_str());
			}
			else
			{
				// normal one-pass encoding - include all options and encode
				// directly to the desired output file
				cmdline1.Format(_T("\"%s\" -loglevel error")
					_T(" %s %s")
					_T(" %s %s")
					_T(" \"%s\""),
					ffmpeg, 
					imageOpts.c_str(), audioOpts.c_str(), 
					rotateOpt, timeLimitOpt.c_str(), 
					item.filename.c_str());
			}

			auto RunFFMPEG = [this, &statusList, &curStatus, &itemDesc, &captureOkay, &abortCapture](TSTRINGEx &cmdline, bool logSuccess)
			{
				// presume failure
				bool result = false;

				// Log the command for debugging purposes, as there's a lot that
				// can go wrong here and little information back from ffmpeg that
				// we can analyze mechanically.
				LogFile::Get()->Group(LogFile::CaptureLogging);
				LogFile::Get()->WriteTimestamp(LogFile::CaptureLogging, 
					_T("Media capture for %s: launching FFMPEG\n> %s\n"), 
					curStatus.c_str(), cmdline.c_str());

				// open the NUL file as stdin for the child
				SECURITY_ATTRIBUTES sa;
				sa.nLength = sizeof(sa);
				sa.lpSecurityDescriptor = NULL;
				sa.bInheritHandle = TRUE;
				HandleHolder hNulIn = CreateFile(_T("NUL"), GENERIC_READ, 0, &sa, OPEN_EXISTING, 0, NULL);

				// Set up a temp file to capture output from FFMPEG, so that
				// we can then copy it to the log file.  Only do this if logging
				// is enabled; if not, discard output by sending it to NUL.
				HandleHolder hStdOut;
				TSTRING fnameStdOut;
				if (LogFile::Get()->IsFeatureEnabled(LogFile::CaptureLogging))
				{
					// we're logging it - capture to a temp file
					TCHAR tmpPath[MAX_PATH] = _T("<no temp path>"), tmpName[MAX_PATH] = _T("<no temp name>");
					GetTempPath(countof(tmpPath), tmpPath);
					GetTempFileName(tmpPath, _T("PBYCap"), 0, tmpName);
					hStdOut = CreateFile(tmpName, GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

					// log an error if that failed, but continue with the capture
					if (hStdOut == NULL)
					{
						WindowsErrorMessage err;
						LogFile::Get()->Write(LogFile::CaptureLogging,
							_T("+ Unable to log FFMPEG output: error opening temp file %s (error %d: %s)\n"),
							tmpName, err.GetCode(), err.Get());
					}
					else
					{
						// successfully opened the file - remember its name
						fnameStdOut = tmpName;
					}
				}

				// if we didn't open an output file, discard output by sending it to NUL
				if (hStdOut == NULL)
					hStdOut = CreateFile(_T("NUL"), GENERIC_WRITE, 0, &sa, OPEN_EXISTING, 0, NULL);

				// Set up the startup info.  Use Show-No-Activate to try to keep
				// the game window activated and in the foreground, since VP (and
				// probably others) stop animations when in the background.
				STARTUPINFO startupInfo;
				ZeroMemory(&startupInfo, sizeof(startupInfo));
				startupInfo.cb = sizeof(startupInfo);
				startupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
				startupInfo.wShowWindow = SW_SHOWNOACTIVATE;
				startupInfo.hStdInput = hNulIn;
				startupInfo.hStdOutput = hStdOut;

				// launch the process
				PROCESS_INFORMATION procInfo;
				if (CreateProcess(NULL, cmdline.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, 
					NULL, NULL, &startupInfo, &procInfo))
				{
					// ffmpeg launched successfully.  Put the handles in holders
					// so that we auto-close the handles when done with them.
					HandleHolder hFfmpegProc(procInfo.hProcess);
					HandleHolder hFfmpegThread(procInfo.hThread);

					// wait for the process to finish, or for a shutdown or
					// close-game event to interrupt it
					HANDLE h[] = { hFfmpegProc, hGameProc, shutdownEvent, closeEvent };
					switch (WaitForMultipleObjects(countof(h), h, FALSE, INFINITE))
					{
					case WAIT_OBJECT_0:
						// The ffmpeg process finished
						{
							// Make sure the main thread exited.  We seem to get exit
							// code 259 (STILL_ACTIVE) in some cases even after the
							// process handle has become signalled (which is the only
							// way we get here).
							WaitForSingleObject(hFfmpegThread, 5000);

							// retrieve the process exit code
							DWORD exitCode;
							GetExitCodeProcess(hGameProc, &exitCode);
							LogFile::Get()->Write(LogFile::CaptureLogging,
								_T("+ FFMPEG completed: process exit code %d\n"), (int)exitCode);

							// consider this a success
							result = true;

							// log successful completion if desired
							if (logSuccess)
								statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), LoadStringT(IDS_ERR_CAP_ITEM_OK).c_str()));
						}
						break;

					case WAIT_OBJECT_0 + 1:
					case WAIT_OBJECT_0 + 2:
					case WAIT_OBJECT_0 + 3:
					default:
						// Shutdown event, close event, or premature game termination,
						// or another error.  Count this as an interrupted capture.
						LogFile::Get()->Write(LogFile::CaptureLogging, _T("+ capture interrupted\n"));
						statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), LoadStringT(IDS_ERR_CAP_ITEM_INTERRUPTED).c_str()));
						captureOkay = false;
						abortCapture = true;
						break;
					}

					// Whatever happened, we managed to launch the process, so
					// there might be at least some information in the log file
					// indicating what went wrong.  
					hStdOut = nullptr;
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
					LogFile::Get()->Write(LogFile::CaptureLogging, 
						_T("+ error lauching FFMPEG: error %d, %s\n"), err.GetCode(), err.Get());
					statusList.Error(MsgFmt(_T("%s: %s"), itemDesc.c_str(), LoadStringT(IDS_ERR_CAP_ITEM_NOT_STARTED).c_str()));
					captureOkay = false;
					abortCapture = true;
				}

				// add a blank line to the log after the FFMPEG output, for readability 
				LogFile::Get()->Group(LogFile::CaptureLogging);

				// return the status
				return result;
			};

			// Run the first pass.  Only show the success status for the first pass
			// if there will be no second pass, since we won't know if the overall
			// operation is successful until after the second pass, if there is one.
			if (RunFFMPEG(cmdline1, cmdline2.length() == 0))
			{
				// success - if there's a second pass, run it
				if (cmdline2.length() != 0)
				{
					curStatus.Format(LoadStringT(IDS_CAPSTAT_ENCODING_ITEM), itemDesc.c_str());
					capture.statusWin->SetCaptureStatus(curStatus.c_str(), item.captureTime*3/2);
					RunFFMPEG(cmdline2, true);
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
			// load the overall group message, if we don't already have one
			if (overallStatus.length() == 0)
				overallStatus.Load(captureOkay ? IDS_ERR_CAP_SUCCESS : IDS_ERR_CAP_FAILED);

			// show the results
			PFVMsgShowErrorParams ep(captureOkay ? EIT_Information : EIT_Error, overallStatus.c_str(), &statusList);
			playfieldView->SendMessage(PFVMsgShowError, 0, reinterpret_cast<LPARAM>(&ep));
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
		// The Close Game event has triggered.  The program should be
		// exiting shortly, as we should have sent the necessary Close
		// Window commands to the game when we triggered the Close.
		// Give the game some time to finish, but don't wait too long
		// this time.  Also stop immediately if we get an application
		// Shutdown event: that means the user has quit out of the
		// program, so we'll leave it to them to finish cleaning up
		// any processes that are still running.
		LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: Close Game command received\n"));
		{
			HANDLE h2[] = { hGameProc, shutdownEvent };
			switch (WaitForMultipleObjects(countof(h2), h2, FALSE, 5000))
			{
			case WAIT_OBJECT_0:
				LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: game exited normally\n"));
				break;

			case WAIT_OBJECT_0 + 1:
				LogFile::Get()->Write(LogFile::TableLaunchLogging,
					_T("+ table launch: application shutting down; not waiting for game to exit\n"));
				break;

			default:
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

	// Check for a RunAfter command
	if (gameSys.runAfter.length() != 0)
	{
		// Parse option flags
		RunOptions options(gameSys.runAfter);
		LogFile::Get()->Write(LogFile::TableLaunchLogging, 
			_T("+ table launch: Run After command:\n> %s\n"), options.command.c_str());

		// run the command with no waiting
		AsyncErrorHandler aeh;
		if (!Application::RunCommand(SubstituteVars(options.command).c_str(),
			aeh, IDS_ERR_GAMERUNBEFORE, false, &hRunAfterProc))
			return 0;

		// if desired, wait for the process to exit on its own
		if (options.nowait)
		{
			// [NOWAIT] was specified, so we're meant to just launch the
			// process and leave it running.  Forget the process handle,
			// so that we don't try to kill the process when the monitor
			// thread exits.
			hRunAfterProc = NULL;
		}
		else
		{
			// There's no [NOWAIT], so the default is to wait for the 
			// process to exit on its own.  Also stop if the 'shutdown' or
			// 'close' events fire.
			//
			// Before doing the wait, reset the Close event.  If we got here
			// by way of our own Terminate Game command, the Close event will
			// be set.  But the RunAfter command is a brand new program launch
			// and a brand new wait, so we want to treat this as a separate
			// operation.  If the RunAfter command itself gets stuck, this 
			// gives the user a way to cancel it.  Don't reset the Shutdown 
			// event, though, as that's a separate matter of quitting out of
			// our application.
			ResetEvent(closeEvent);
			HANDLE waitEvents[] = { hRunAfterProc, shutdownEvent, closeEvent };
			switch (WaitForMultipleObjects(countof(waitEvents), waitEvents, FALSE, INFINITE))
			{
			case WAIT_OBJECT_0:
				// Tun RunAfter process exited.  Close the process handle
				// and proceed.
				LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: Run After command finished\n"));
				hRunAfterProc = NULL;
				break;

			case WAIT_OBJECT_0 + 1:
			case WAIT_OBJECT_0 + 2:
			default:
				// The shutdown or close event fired, or an error occurred in 
				// the wait.  In either case, shut down the monitor thread 
				// immediately.  Leave the process handle in hRunAfterProc 
				// so that the monitor thread object destructor takes care of
				// terminating the process.  That's desirable in this case 
				// because we didn't finish up normally.  The conditions that
				// would normally make the RunAfter program exit on its own
				// might not exist, so it seems safest to let the thread
				// cleanup code terminate the process explicitly.
				LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch: Run After command interrupted\n"));
				return 0;
			}
		}
	}

	// done
	LogFile::Get()->Write(LogFile::TableLaunchLogging, _T("+ table launch finished successfully\n"));
	return 0;
}

bool Application::GameMonitorThread::WaitForStartup()
{
	// keep trying until the process is ready, or we run into a problem
	for (int tries = 0; tries < 20; ++tries)
	{
		// wait for "input idle" state
		DWORD result = WaitForInputIdle(hGameProc, 1000);

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

bool Application::AdminHost::SendRequest(const TCHAR *const *request, size_t nItems, std::vector<TSTRING> &reply)
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
			return false;

		case WAIT_TIMEOUT:
		case WAIT_ABANDONED:
			// ignore these - just go back for another try
			break;

		default:
			// error - abandon the request and return failure
			return false;
		}
	}
}

void Application::SendExitGameKeysToAdminHost(const std::list<TSTRING> &keys)
{
	// we only need to do this if the Admin Host is running
	if (adminHost.IsAvailable())
	{
		// start the command vector with the EXIT GAME KEYS verb
		std::vector<const TCHAR*> req;
		req.emplace_back(_T("exitGameKeys"));

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
