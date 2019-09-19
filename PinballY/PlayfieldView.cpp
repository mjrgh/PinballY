// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Playfield View

#include "stdafx.h"
#include <mmsystem.h>
#include <filesystem>
#include <Dwmapi.h>
#include <d3d11_1.h>
#include <gdiplus.h>
#include <DirectXMath.h>
#include <VersionHelpers.h>
#include <Shellapi.h>
#include <Shlobj.h>
#include <Vsstyle.h>
#include <Vssym32.h>
#include "../Utilities/Config.h"
#include "../Utilities/FileVersionInfo.h"
#include "../Utilities/DateUtil.h"
#include "../Utilities/FileUtil.h"
#include "../Utilities/GraphicsUtil.h"
#include "PlayfieldView.h"
#include "Resource.h"
#include "DialogResource.h"
#include "D3D.h"
#include "D3DWin.h"
#include "BackglassView.h"
#include "DMDView.h"
#include "TopperView.h"
#include "InstCardView.h"
#include "Camera.h"
#include "TextDraw.h"
#include "VersionInfo.h"
#include "Sprite.h"
#include "Application.h"
#include "MouseButtons.h"
#include "AudioManager.h"
#include "DOFClient.h"
#include "AudioVideoPlayer.h"
#include "DShowAudioPlayer.h"
#include "VLCAudioVideoPlayer.h"
#include "HighScores.h"
#include "RefTableList.h"
#include "VPFileReader.h"
#include "MediaDropTarget.h"
#include "SevenZipIfc.h"
#include "RealDMD.h"
#include "VPinMAMEIfc.h"
#include "DialogWithSavedPos.h"
#include "LogFile.h"
#include "../OptionsDialog/OptionsDialogExports.h"
#include "JavascriptEngine.h"

// Option setting: should we notify PinVol (if running) when we select
// a new game in the wheel UI?
//
// By default, this is disabled.
//
// I implemented this to address an enhancement request (issue #9 on github)
// asking for per-game audio volume level adjustments for the background
// videos and table audio tracks.  The rationale is that some tables have
// much louder soundtracks than others, so it would be nice to be able to
// equalize the audio level of the background media from game to game with
// a per-game volume adjustment in PinballY.  At first glance, I thought
// this would be ideal to handle through PinVol, since game-to-game volume
// equalization is what it's all about.  If we notify PinVol of our game
// selection, it can treat us as "PinballY + Selected Game" rather than
// just "PinballY", and can thereby store independent volume levels per
// selection.  But after implementing it and trying it out, I realized
// that this was the wrong way to do it.  The snag is that you only want
// to adjust the game media volume - NOT the global effects like button
// sounds.  PinVol can only adjust the volume across the board, so if you
// turn down the volume for a loud game with PinVol, you also turn down 
// the button sounds.  So in the name of equalizing sound levels for the
// media items, we inadvertantly also dis-equalize levels for the button
// sounds.  The effect is not pleasing.
//
// So I disabled it, both in PinVol and here.  Note that it would be
// harmless functionally to leave the notifications intact, since PinVol
// will just ignore them as long as the feature is disabled there, but I
// figure it's better to remove them entirely as long as they're not
// being used to avoid the added overhead of the mailslot transmission.
// Simply change this to 'true' to re-enable the notifications on our end.
// (Turning them back on here won't re-activate the feature in PinVol,
// though, as it's separately disabled there.  You'd have to re-enable
// it in both places to bring back the feature.)
//
const bool NOTIFY_PINVOL_ON_WHEEL_SELECTION = false;

using namespace DirectX;

namespace ConfigVars
{
	static const TCHAR *AttractModeEnabled = _T("AttractMode.Enabled");
	static const TCHAR *AttractModeIdleTime = _T("AttractMode.IdleTime");
	static const TCHAR *AttractModeSwitchTime = _T("AttractMode.SwitchTime");
	static const TCHAR *AttractModeHideWheelImages = _T("AttractMode.HideWheelImages");
	static const TCHAR *PlayfieldWinPrefix = _T("PlayfieldWindow");
	static const TCHAR *GameTimeout = _T("GameTimeout");
	static const TCHAR *ExitKeyMode = _T("ExitMenu.ExitKeyMode");
	static const TCHAR *ExitMenuEnabled = _T("ExitMenu.Enabled");
	static const TCHAR *ShowOpMenuInExitMenu = _T("ExitMenu.ShowOperatorMenu");
	static const TCHAR *MuteButtons = _T("Buttons.Mute");
	static const TCHAR *MuteRepeatButtons = _T("Buttons.MuteRepeat");
	static const TCHAR *ButtonVolume = _T("Buttons.Volume");
	static const TCHAR *InstCardLoc = _T("InstructionCardLocation");
	static const TCHAR *InstCardEnableFlash = _T("InstructionCards.EnableFlash");
	static const TCHAR *CoinSlotValue = _T("Coin%d.Value");
	static const TCHAR *PricingModel = _T("PricingModel");
	static const TCHAR *CreditBalance = _T("CreditBalance");
	static const TCHAR *MaxCreditBalance = _T("MaxCreditBalance");
	static const TCHAR *RealDMD = _T("RealDMD");
	static const TCHAR *GameInfoDialogPos = _T("EditGameInfoDialog.Position");
	static const TCHAR *CategoryDialogPos = _T("CategoryDialog.Position");
	static const TCHAR *CatNameDialogPos = _T("CategoryNameDialog.Position");
	static const TCHAR *OptsDialogPos = _T("OptionsDialog.Position");
	static const TCHAR *SplashScreen = _T("SplashScreen");

	static const TCHAR *PlayfieldStretch = _T("Playfield.Stretch");

	static const TCHAR *InfoBoxShow = _T("InfoBox.Show");
	static const TCHAR *InfoBoxTitle = _T("InfoBox.Title");
	static const TCHAR *InfoBoxGameLogo = _T("InfoBox.GameLogo");
	static const TCHAR *InfoBoxManufacturer = _T("InfoBox.Manufacturer");
	static const TCHAR *InfoBoxManufacturerLogo = _T("InfoBox.ManufacturerLogo");
	static const TCHAR *InfoBoxYear = _T("InfoBox.Year");
	static const TCHAR *InfoBoxSystem = _T("InfoBox.System");
	static const TCHAR *InfoBoxSystemLogo = _T("InfoBox.SystemLogo");
	static const TCHAR *InfoBoxTableType = _T("InfoBox.TableType");
	static const TCHAR *InfoBoxTableTypeAbbr = _T("InfoBox.TableTypeAbbr");
	static const TCHAR *InfoBoxRating = _T("InfoBox.Rating");
	static const TCHAR *InfoBoxTableFile = _T("InfoBox.TableFile");

	static const TCHAR *DefaultFontFamily = _T("DefaultFontFamily");
	static const TCHAR *MenuFont = _T("MenuFont");
	static const TCHAR *MenuHeaderFont = _T("MenuHeaderFont");
	static const TCHAR *PopupFont = _T("PopupFont");
	static const TCHAR *PopupTitleFont = _T("PopupTitleFont");
	static const TCHAR *PopupSmallerFont = _T("PopupSmallerFont");
	static const TCHAR *PopupDetailFont = _T("PopupDetailFont");
	static const TCHAR *MediaDetailFont = _T("MediaDetailFont");
	static const TCHAR *WheelFont = _T("WheelFont");
	static const TCHAR *HighScoreFont = _T("HighScoreFont");
	static const TCHAR *InfoBoxTitleFont = _T("InfoBoxTitleFont");
	static const TCHAR *InfoBoxFont = _T("InfoBoxFont");
	static const TCHAR *InfoBoxDetailFont = _T("InfoBoxDetailFont");
	static const TCHAR *StatusFont = _T("StatusFont");
	static const TCHAR *CreditsFont = _T("CreditsFont");

	static const TCHAR *DOFEnable = _T("DOF.Enable");

	static const TCHAR *CaptureSkipLayoutMessage = _T("Capture.SkipLayoutMessage");
	static const TCHAR *CaptureManualStartStopButtons = _T("Capture.ManualStartStopButton");

	static const TCHAR *StatusLineEnable = _T("StatusLine.Enable");
};

// include the capture-related variables
#include "CaptureConfigVars.h"

// Wheel animation time
static const DWORD wheelTime = 260;

// "No Command" 
const PlayfieldView::KeyCommand PlayfieldView::NoCommand(_T("NoOp"), &PlayfieldView::CmdNone);

// construction
PlayfieldView::PlayfieldView() : 
	BaseView(IDR_PLAYFIELD_CONTEXT_MENU, ConfigVars::PlayfieldWinPrefix),
	playfieldLoader(this)
{
	// clear variables, reset modes
	fpsDisplay = false;
	popupType = PopupNone;
	isAnimTimerRunning = false;
	popupAnimMode = PopupAnimNone;
	wheelAnimMode = WheelAnimNone;
	menuAnimMode = MenuAnimNone;
	muteButtons = false;
	muteRepeatButtons = false;
	buttonVolume = 100;
	lastDOFEventTime = 0;
	coinBalance = 0.0f;
	bankedCredits = 0.0f;
	maxCredits = 0.0f;
	lastInputEventTime = GetTickCount();
	settingsDialogOpen = false;
	mediaDropTargetGame = nullptr;
	runningGameMode = RunningGameMode::None;
	
	// note the exit key mode
	TSTRING exitMode = ConfigManager::GetInstance()->Get(ConfigVars::ExitKeyMode, _T("select"));
	std::transform(exitMode.begin(), exitMode.end(), exitMode.begin(), ::_totlower);
	exitMenuExitKeyIsSelectKey = exitMode == _T("select");

	// populate the command handler table
	auto add = [this](const TCHAR *name, KeyCommandFunc func)
	{
		commandsByName.emplace(std::piecewise_construct,
			std::forward_as_tuple(name),
			std::forward_as_tuple(name, func));
	};

	add(_T("Select"), &PlayfieldView::CmdSelect);
	add(_T("Exit"), &PlayfieldView::CmdExit);
	add(_T("Next"), &PlayfieldView::CmdNext);
	add(_T("Prev"), &PlayfieldView::CmdPrev);
	add(_T("NextPage"), &PlayfieldView::CmdNextPage);
	add(_T("PrevPage"), &PlayfieldView::CmdPrevPage);
	add(_T("CoinDoor"), &PlayfieldView::CmdCoinDoor);
	add(_T("Service1"), &PlayfieldView::CmdService1);
	add(_T("Service2"), &PlayfieldView::CmdService2);
	add(_T("Service3"), &PlayfieldView::CmdService3);
	add(_T("Service4"), &PlayfieldView::CmdService4);
	add(_T("FrameCounter"), &PlayfieldView::CmdFrameCounter);
	add(_T("FullScreen"), &PlayfieldView::CmdFullScreen);
	add(_T("Settings"), &PlayfieldView::CmdSettings);
	add(_T("RotateMonitor"), &PlayfieldView::CmdRotateMonitorCW);
	add(_T("Coin1"), &PlayfieldView::CmdCoin1);
	add(_T("Coin2"), &PlayfieldView::CmdCoin2);
	add(_T("Coin3"), &PlayfieldView::CmdCoin3);
	add(_T("Coin4"), &PlayfieldView::CmdCoin4);
	add(_T("Launch"), &PlayfieldView::CmdLaunch);
	add(_T("ExitGame"), &PlayfieldView::CmdExitGame);
	add(_T("Information"), &PlayfieldView::CmdGameInfo);
	add(_T("Instructions"), &PlayfieldView::CmdInstCard);
	add(_T("PauseGame"), &PlayfieldView::CmdPauseGame);

	// populate the table of commands with menu associations
	commandNameToMenuID.emplace(_T("RotateMonitor"), ID_ROTATE_CW);
	commandNameToMenuID.emplace(_T("FullScreen"), ID_FULL_SCREEN);
	commandNameToMenuID.emplace(_T("Settings"), ID_OPTIONS);
	commandNameToMenuID.emplace(_T("FrameCounter"), ID_FPS);

	// populate the table type name map
	tableTypeNameMap.emplace(_T("SS"), LoadStringT(IDS_GAMEINFO_TYPE_SS));
	tableTypeNameMap.emplace(_T("EM"), LoadStringT(IDS_GAMEINFO_TYPE_EM));
	tableTypeNameMap.emplace(_T("ME"), LoadStringT(IDS_GAMEINFO_TYPE_ME));

	// subscribe for joystick events
	JoystickManager::GetInstance()->SubscribeJoystickEvents(this);

	// subscribe for raw input events
	InputManager::GetInstance()->SubscribeRawInput(this);

	// process the initial configuration settings
	OnConfigChange();

	// subscribe for future config updates
	ConfigManager::GetInstance()->Subscribe(this);

	// load the rating stars image
	stars.reset(GPBitmapFromPNG(IDB_STARS));
}

// destruction
PlayfieldView::~PlayfieldView()
{
	// commit any coin balance to a credit balance
	ResetCoins();

	// Explicitly clear any outstanding high score requests.  These can
	// contain Javascript objects, so we want to make sure these get deleted
	// before we destroy the Javascript context they belong to.
	highScoresReadyList.clear();

	// The user-defined filter list also contains Javscript object references,
	// so clear it explicitly.
	javascriptFilters.clear();
}

// Create our window
bool PlayfieldView::Create(HWND parent)
{
	// do the base class creation
	if (!BaseView::Create(parent, _T("Playfield")))
		return false;

	// set the context menu's key shortcuts
	UpdateMenuKeys(GetSubMenu(hContextMenu, 0));

	// set the real DMD enabling items to radio button style
	MENUITEMINFO mii;
	ZeroMemory(&mii, sizeof(mii));
	mii.cbSize = sizeof(mii);
	mii.fMask = MIIM_FTYPE;
	mii.fType = MFT_RADIOCHECK;
	SetMenuItemInfo(hContextMenu, ID_REALDMD_AUTO_ENABLE, FALSE, &mii);
	SetMenuItemInfo(hContextMenu, ID_REALDMD_ENABLE, FALSE, &mii);
	SetMenuItemInfo(hContextMenu, ID_REALDMD_DISABLE, FALSE, &mii);

	// success
	return true;
}

PlayfieldView::RealDMDStatus PlayfieldView::GetRealDMDStatus() const
{
	// Get the RealDMD configuration setting.  Possible settings are:
	//
	// AUTO -> enable if the DLL is found, disable (with no error) if not
	// ON/ENABLE/1 -> enable unconditionally (show an error if the DLL isn't found)
	// OFF/DISABLE/0/other -> disable unconditionally
	//
	auto dmdvar = ConfigManager::GetInstance()->Get(ConfigVars::RealDMD, _T("auto"));
	return _tcsicmp(dmdvar, _T("auto")) == 0 ? RealDMDAuto :
		_tcsicmp(dmdvar, _T("on")) == 0 || _tcsicmp(dmdvar, _T("enable")) == 0 || _ttoi(dmdvar) != 0 ? RealDMDEnable :
		RealDMDDisable;
}

void PlayfieldView::SetRealDMDStatus(RealDMDStatus newStat)
{
	// if the status is changing, update it
	if (newStat != GetRealDMDStatus())
	{
		// store the new status in the config
		ConfigManager::GetInstance()->Set(ConfigVars::RealDMD,
			newStat == RealDMDAuto ? _T("auto") :
			newStat == RealDMDEnable ? _T("on") : _T("off"));

		// If we're not currently running a game, dynamically attach
		// to or detach from the DMD device.  Don't do this when a
		// game is running, as the game owns the DMD device for the
		// duration of the run; we'll reattach if appropriate when
		// the game exits and we take over the UI again.
		if (runningGamePopup == nullptr)
		{
			// shut down any existing DMD session
			if (realDMD != nullptr)
			{
				realDMD->ClearMedia();
				realDMD.reset();
			}

			// Activate or deactivate the DMD according to the new status
			if (newStat != RealDMDDisable)
			{
				// enabling or auto-sensing - try initializing if it's not
				// already running
				InitRealDMD(Application::InUiErrorHandler());
			}

			// load media, if the DMD is now active
			if (realDMD != nullptr)
				realDMD->UpdateGame();
		}
	}
}

void PlayfieldView::InitRealDMD(ErrorHandler &eh)
{
	// Delete any previous DMD support object
	realDMD.reset(nullptr);

	// if we're in ON or AUTO mode, try loading the DMD DLL
	auto mode = GetRealDMDStatus();
	if (mode == RealDMDEnable || mode == RealDMDAuto)
	{
		// presume failure
		bool ok = false;

		// create the interface object
		realDMD.reset(new RealDMD());

		// if we're in AUTO mode, check first to see if the DLL
		// exists, and fail silently if not (since AUTO means that
		// the DMD is enabled according to whether or not the DLL
		// can be found)
		if (mode == RealDMDAuto && !realDMD->FindDLL())
		{
			// auto mode, no DLL -> fail silently
		}
		else
		{
			// try initializing the DLL
			ok = realDMD->Init(eh);
		}

		// check what happened
		if (!ok)
		{
			// failed or not attempted - forget the DMD interface
			realDMD.reset(nullptr);
		}
	}
}

// Initialize the window
const DWORD statusLineTimerInterval = 16;
const DWORD attractModeTimerInterval = 1000;
bool PlayfieldView::InitWin()
{
	// do the base class work
	if (!__super::InitWin())
		return false;

	// Register for idle events.   We only want the first one, so that
	// we can start the UI timers running after all of the initial
	// setup has been completed.
	D3DView::SubscribeIdleEvents(this);

	// register for Capture Manual Go notifications from the Admin Host, if present
	if (auto app = Application::Get(); app->IsAdminHostAvailable())
	{
		MsgFmt shwnd(_T("%ld"), (long)(INT_PTR)hWnd);
		MsgFmt smsg(_T("%d"), PFVMsgManualGo);
		const TCHAR *req[] = {
			_T("regStartStopMsg"),
			shwnd,
			smsg
		};
		app->PostAdminHostRequest(req, countof(req));
	}

	// success
	return true;
}

void PlayfieldView::InitJavascript()
{
	LogFileErrorHandler eh(_T("Initializing Javascript: "), LogFile::JSLogging);

	// Check for the main javascript extensions file.  If it exists, set up
	// the javascript engine and load the script.
	TCHAR jsmain[MAX_PATH];
	GetDeployedFilePath(jsmain, _T("scripts\\main.js"), _T(""));
	LogFile::Get()->Group(LogFile::JSLogging);
	LogFile::Get()->Write(LogFile::JSLogging, _T("Checking for Javascript main script file %s\n"), jsmain);
	if (FileExists(jsmain))
	{
		// if initialization fails, shut down javascript
		struct Cleanup
		{
			Cleanup() : success(false) { }
			~Cleanup() { if (!success) JavascriptEngine::Terminate(); }
			bool success;
		} cleanup;

		// message window settings
		JavascriptEngine::MessageWindow messageWindow;
		messageWindow.hwnd = hWnd;
		messageWindow.timerId = javascriptTimerID;
		messageWindow.debugEventMessageId = PFVMsgJsDebugMessage;

		// debug options
		auto &debugOpts = Application::Get()->javascriptDebugOptions;

		// create and initialize the javascript engine
		LogFile::Get()->Write(LogFile::JSLogging, _T(". Main script file exists; initializing Javascript engine\n"));
		if (!JavascriptEngine::Init(eh, messageWindow, &debugOpts))
		{
			LogFile::Get()->Write(LogFile::JSLogging, _T(". Javascript engine initialization failed; Javascript disabled for this session\n"));
			return;
		}

		auto js = JavascriptEngine::Get();
		try
		{
			const TCHAR *where;
			JsErrorCode err;

			// set up our callbacks
			if (!js->DefineGlobalFunc("alert", &PlayfieldView::JsAlert, this, eh)
				|| !js->DefineGlobalFunc("message", &PlayfieldView::JsMessage, this, eh)
				|| !js->DefineGlobalFunc("OutputDebugString", &PlayfieldView::JsOutputDebugString, this, eh)
				|| !js->DefineGlobalFunc("setTimeout", &PlayfieldView::JsSetTimeout, this, eh)
				|| !js->DefineGlobalFunc("clearTimeout", &PlayfieldView::JsClearTimeout, this, eh)
				|| !js->DefineGlobalFunc("setInterval", &PlayfieldView::JsSetInterval, this, eh)
				|| !js->DefineGlobalFunc("clearInterval", &PlayfieldView::JsClearInterval, this, eh))
			{
				LogFile::Get()->Write(LogFile::JSLogging, _T(". Error setting up Javascript native callbacks; Javascript disabled for this session\n"));
				return;
			}

			// Load our system scripts.  These define the basic class framework 
			// for the system objects, as far as we can in pure Javascript.  We'll
			// still have to attach native code functions to some of the object
			// methods after everything is set up.
			auto LoadSysScript = [&js, &eh](const TCHAR *name)
			{
				// get the full path
				TCHAR path[MAX_PATH];
				GetDeployedFilePath(path, name, _T(""));
				LogFile::Get()->Write(LogFile::JSLogging, _T(". Loading system script file %s\n"), path);

				// get the file:/// URL for the path
				WSTRING url = js->GetFileUrl(path);

				// load the file
				long len;
				std::unique_ptr<WCHAR> contents(ReadFileAsWStr(path, eh, len, ReadFileAsStr_NullTerm));
				if (contents == nullptr)
					return false;

				// evaluate the script
				return js->EvalScript(contents.get(), url.c_str(), nullptr, eh);
			};

			if (!LoadSysScript(_T("scripts\\system\\CParser.js"))
				|| !LoadSysScript(_T("scripts\\system\\SystemClasses.js")))
			{
				LogFile::Get()->Write(LogFile::JSLogging, _T(". Error loading system scripts; Javascript disabled for this session\n"));
				return;
			}

			// create a system info object with basic system details
			MsgFmt sysInfo(_T("this.systemInfo = {")
				_T("programName:\"PinballY\",")
				_T("platform:\"") IF_32_64(_T("x86"), _T("x64")) _T("\",")
				_T("version:{")
				_T("display:\"%hs\",")
				_T("semantic:\"%hs\",")
				_T("basic:\"") _T(PINBALLY_VERSION) _T("\",")
				_T("status:\"%hs\",")
				_T("build:%d,")
				_T("buildDate:new Date(%I64d)}")
				_T("};"),

				G_VersionInfo.fullVerWithStat,
				G_VersionInfo.semVer,
				G_VersionInfo.fullVer,
				G_VersionInfo.buildNo,
				G_VersionInfo.unix_date * 1000
			);
			js->EvalScript(sysInfo.Get(), _T("system:sysinfo"), nullptr, eh);

			// Install the dllImport callbacks.  Note that this happens AFTER the system
			// scripts are loaded, since the callbacks are installed on objects created in
			// the system scripts.
			if (!js->BindDllImportCallbacks(eh))
				return;

			// get the global objects we need to reference
			auto GetObj = [&js](JsValueRef &jsval, const char *name)
			{
				// get the global property by name
				const TCHAR *where;
				JsErrorCode err;
				if ((err = js->GetGlobProp(jsval, name, where)) != JsNoError)
				{
					LogFile::Get()->Write(LogFile::JSLogging, _T(". %hs object missing; Javascript disabled for this session\n"), name);
					return false;
				}

				// add a reference
				JsAddRef(jsval, nullptr);
				return true;
			};
			if (!GetObj(jsCommandButtonDownEvent, "CommandButtonDownEvent")
				|| !GetObj(jsCommandButtonUpEvent, "CommandButtonUpEvent")
				|| !GetObj(jsCommandButtonBgDownEvent, "CommandButtonBgDownEvent")
				|| !GetObj(jsCommandButtonBgUpEvent, "CommandButtonBgUpEvent")
				|| !GetObj(jsKeyDownEvent, "KeyDownEvent")
				|| !GetObj(jsKeyUpEvent, "KeyUpEvent")
				|| !GetObj(jsKeyBgDownEvent, "KeyBgDownEvent")
				|| !GetObj(jsKeyBgUpEvent, "KeyBgUpEvent")
				|| !GetObj(jsJoystickButtonDownEvent, "JoystickButtonDownEvent")
				|| !GetObj(jsJoystickButtonUpEvent, "JoystickButtonUpEvent")
				|| !GetObj(jsJoystickButtonBgDownEvent, "JoystickButtonBgDownEvent")
				|| !GetObj(jsJoystickButtonBgUpEvent, "JoystickButtonBgUpEvent")
				|| !GetObj(jsPreLaunchEvent, "PreLaunchEvent")
				|| !GetObj(jsPostLaunchEvent, "PostLaunchEvent")
				|| !GetObj(jsLaunchErrorEvent, "LaunchErrorEvent")
				|| !GetObj(jsGameStartedEvent, "GameStartedEvent")
				|| !GetObj(jsGameOverEvent, "GameOverEvent")
				|| !GetObj(jsRunBeforePreEvent, "RunBeforePreEvent")
				|| !GetObj(jsRunBeforeEvent, "RunBeforeEvent")
				|| !GetObj(jsRunAfterEvent, "RunAfterEvent")
				|| !GetObj(jsRunAfterPostEvent, "RunAfterPostEvent")
				|| !GetObj(jsGameSelectEvent, "GameSelectEvent")
				|| !GetObj(jsFilterSelectEvent, "FilterSelectEvent")
				|| !GetObj(jsCommandEvent, "CommandEvent")
				|| !GetObj(jsMenuOpenEvent, "MenuOpenEvent")
				|| !GetObj(jsMenuCloseEvent, "MenuCloseEvent")
				|| !GetObj(jsAttractModeStartEvent, "AttractModeStartEvent")
				|| !GetObj(jsAttractModeEndEvent, "AttractModeEndEvent")
				|| !GetObj(jsWheelModeEvent, "WheelModeEvent")
				|| !GetObj(jsPopupOpenEvent, "PopupOpenEvent")
				|| !GetObj(jsPopupCloseEvent, "PopupCloseEvent")
				|| !GetObj(jsSettingsReloadEvent, "SettingsReloadEvent")
				|| !GetObj(jsSettingsPreSaveEvent, "SettingsPreSaveEvent")
				|| !GetObj(jsSettingsPostSaveEvent, "SettingsPostSaveEvent")
				|| !GetObj(jsStatusLineEvent, "StatusLineEvent")
				|| !GetObj(jsHighScoresRequestEvent, "HighScoresRequestEvent")
				|| !GetObj(jsHighScoresReadyEvent, "HighScoresReadyEvent")
				|| !GetObj(jsConsole, "console")
				|| !GetObj(jsLogfile, "logfile")
				|| !GetObj(jsGameList, "gameList")
				|| !GetObj(jsGameInfo, "GameInfo")
				|| !GetObj(jsGameSysInfo, "GameSysInfo")
				|| !GetObj(jsFilterInfo, "FilterInfo")
				|| !GetObj(jsOptionSettings, "optionSettings"))
				return;

			// Initialize generic properties for a js window object.  Note that this has
			// to wait until after binding the DLL import subsystem, since we depend upon
			// the native HANDLE object type.
			auto InitWinObj = [&js, &GetObj, &eh](FrameWin *frame, JsValueRef &jswinobj, const CHAR *name)
			{
				// get the window object
				if (!GetObj(jswinobj, name))
					return false;

				// get the view
				auto view = dynamic_cast<D3DView*>(frame->GetView());

				// set up the HWND properties
				JsErrorCode err;
				const TCHAR *where;
				JsValueRef propval;
				if ((err = js->NewHWNDObj(propval, view->GetHWnd(), where)) != JsNoError
					|| (err = js->SetReadonlyProp(jswinobj, "hwndView", propval, where)) != JsNoError
					|| (err = js->NewHWNDObj(propval, GetParent(view->GetHWnd()), where)) != JsNoError
					|| (err = js->SetReadonlyProp(jswinobj, "hwndFrame", propval, where)) != JsNoError)
				{
					LogFile::Get()->Write(LogFile::JSLogging, _T(". error setting hwnd properties: %s\n"), where);
					return false;
				}
				if (!js->DefineGetterSetter(jswinobj, name, "fullScreenMode", &FrameWin::IsFullScreen, &FrameWin::SetFullScreen, frame, eh)
					|| !js->DefineGetterSetter(jswinobj, name, "borderlessMode", &FrameWin::IsBorderless, &FrameWin::SetBorderless, frame, eh)
					|| !js->DefineObjPropFunc(jswinobj, name, "showWindow", &FrameWin::ShowHideFrameWindow, frame, eh)
					|| !js->DefineObjPropFunc(jswinobj, name, "setWindowPos", &FrameWin::JsSetWindowPos, frame, eh)
					|| !js->DefineObjPropFunc(jswinobj, name, "setWindowState", &FrameWin::JsSetWindowState, frame, eh))
					return false;

				// success 
				return true;
			};
			if (!InitWinObj(Application::Get()->GetPlayfieldWin(), jsMainWindow, "mainWindow")
				|| !InitWinObj(Application::Get()->GetBackglassWin(), jsBackglassWindow, "backglassWindow")
				|| !InitWinObj(Application::Get()->GetDMDWin(), jsDMDWindow, "dmdWindow")
				|| !InitWinObj(Application::Get()->GetTopperWin(), jsTopperWindow, "topperWindow")
				|| !InitWinObj(Application::Get()->GetInstCardWin(), jsInstCardWindow, "instCardWindow"))
				return;

			// set up the console methods
			if (!js->DefineObjPropFunc(jsConsole, "console", "_log", &PlayfieldView::JsConsoleLog, this, eh))
				return;

			// set up logfile methods
			if (!js->DefineObjPropFunc(jsLogfile, "logfile", "_log", &PlayfieldView::JsLog, this, eh))
				return;

			// set up the optionSettings methods
			if (!js->DefineObjPropFunc(jsOptionSettings, "optionSettings", "get", &PlayfieldView::JsSettingsGet<const TCHAR*, &ConfigManager::ToStr>, this, eh)
				|| !js->DefineObjPropFunc(jsOptionSettings, "optionSettings", "getBool", &PlayfieldView::JsSettingsGet<bool, &ConfigManager::ToBool>, this, eh)
				|| !js->DefineObjPropFunc(jsOptionSettings, "optionSettings", "getInt", &PlayfieldView::JsSettingsGet<int, &ConfigManager::ToInt>, this, eh)
				|| !js->DefineObjPropFunc(jsOptionSettings, "optionSettings", "getFloat", &PlayfieldView::JsSettingsGet<float, &ConfigManager::ToFloat>, this, eh)
				|| !js->DefineObjPropFunc(jsOptionSettings, "optionSettings", "getRect", &PlayfieldView::JsSettingsGet<RECT, &ConfigManager::ToRect>, this, eh)
				|| !js->DefineObjPropFunc(jsOptionSettings, "optionSettings", "set", &PlayfieldView::JsSettingsSet, this, eh)
				|| !js->DefineObjPropFunc(jsOptionSettings, "optionSettings", "isDirty", &PlayfieldView::JsSettingsIsDirty, this, eh)
				|| !js->DefineObjPropFunc(jsOptionSettings, "optionSettings", "save", &PlayfieldView::JsSettingsSave, this, eh)
				|| !js->DefineObjPropFunc(jsOptionSettings, "optionSettings", "reload", &PlayfieldView::JsSettingsReload, this, eh)
				|| !js->SetProp(jsOptionSettings, "filename", ConfigManager::GetInstance()->GetFilename()))
				return;

			// set up mainWindow methods
			if (!js->DefineObjPropFunc(jsMainWindow, "mainWindow", "message", &PlayfieldView::JsMessage, this, eh)
				|| !js->DefineObjPropFunc(jsMainWindow, "mainWindow", "getUIMode", &PlayfieldView::JsGetUIMode, this, eh)
				|| !js->DefineObjPropFunc(jsMainWindow, "mainWindow", "getActiveWindow", &PlayfieldView::JsGetActiveWindow, this, eh)
				|| !js->DefineObjPropFunc(jsMainWindow, "mainWindow", "doCommand", &PlayfieldView::JsDoCommand, this, eh)
				|| !js->DefineObjPropFunc(jsMainWindow, "mainWindow", "doButtonCommand", &PlayfieldView::JsDoButtonCommand, this, eh)
				|| !js->DefineObjPropFunc(jsMainWindow, "mainWindow", "showMenu", &PlayfieldView::JsShowMenu, this, eh)
				|| !js->DefineObjPropFunc(jsMainWindow, "mainWindow", "showPopup", &PlayfieldView::JsShowPopup, this, eh)
				|| !js->DefineObjPropFunc(jsMainWindow, "mainWindow", "playGame", &PlayfieldView::JsPlayGame, this, eh)
				|| !js->DefineObjPropFunc(jsMainWindow, "mainWindow", "startAttractMode", &PlayfieldView::JsStartAttractMode, this, eh)
				|| !js->DefineObjPropFunc(jsMainWindow, "mainWindow", "endAttractMode", &PlayfieldView::JsEndAttractMode, this, eh)
				|| !js->DefineObjPropFunc(jsMainWindow, "mainWindow", "playButtonSound", &PlayfieldView::JsPlayButtonSound, this, eh)
				|| !js->DefineObjPropFunc(jsMainWindow, "mainWindow", "getKeyCommand", &PlayfieldView::JsGetKeyCommand, this, eh))
				return;

			// Get the status lines
			auto GetStatusLine = [this, js](const char *propName, StatusLine &statusLine)
			{
				// get the statusLines object, then retrieve the property for this status line
				const TCHAR *where;
				JsErrorCode err;
				JsValueRef sl;
				if ((err = js->GetProp(sl, jsMainWindow, "statusLines", where)) != JsNoError
					|| (err = js->GetProp(statusLine.jsobj, sl, propName, where)) != JsNoError)
				{
					LogFile::Get()->Write(LogFile::JSLogging, _T(". error getting mainWindow.statusLines.%hs: %s\n"), propName, where);
					return false;
				}

				// got it - keep an external reference on the object and return success
				JsAddRef(statusLine.jsobj, nullptr);
				return true;
			};
			if (!GetStatusLine("upper", upperStatus)
				|| !GetStatusLine("lower", lowerStatus)
				|| !GetStatusLine("attract", attractModeStatus))
				return;

			// get the StatusLine prototype object
			JsValueRef statusLineObj, statusLineProto;
			if ((err = js->GetGlobProp(statusLineObj, "StatusLine", where)) != JsNoError
				|| (err = js->GetProp(statusLineProto, statusLineObj, "prototype", where)) != JsNoError)
			{
				LogFile::Get()->Write(LogFile::JSLogging, _T(". error getting StatusLine prototype object: %s\n"), where);
				return;
			}

			// set up the StatusLine prototype methods
			if (!js->DefineObjMethod(statusLineProto, "StatusLine", "getText",
				&PlayfieldView::JsStatusLineMethod<decltype(&StatusLine::JsGetText), &StatusLine::JsGetText, JsValueRef>, this, eh)
				|| !js->DefineObjMethod(statusLineProto, "StatusLine", "getCur",
					&PlayfieldView::JsStatusLineMethod<decltype(&StatusLine::JsGetCur), &StatusLine::JsGetCur, int>, this, eh)
				|| !js->DefineObjMethod(statusLineProto, "StatusLine", "setText",
					&PlayfieldView::JsStatusLineMethod<decltype(&StatusLine::JsSetText), &StatusLine::JsSetText, void, int, TSTRING>, this, eh) 
				|| !js->DefineObjMethod(statusLineProto, "StatusLine", "add",
					&PlayfieldView::JsStatusLineMethod<decltype(&StatusLine::JsAdd), &StatusLine::JsAdd, void, TSTRING, JsValueRef>, this, eh)
				|| !js->DefineObjMethod(statusLineProto, "StatusLine", "remove",
					&PlayfieldView::JsStatusLineMethod<decltype(&StatusLine::JsRemove), &StatusLine::JsRemove, void, int>, this, eh)
				|| !js->DefineObjMethod(statusLineProto, "StatusLine", "show",
					&PlayfieldView::JsStatusLineMethod<decltype(&StatusLine::JsShow), &StatusLine::JsShow, void, TSTRING>, this, eh))
				return;

			// create the DrawingContext prototype and populate its methods
			if (!js->CreateObj(jsDrawingContextProto)
				|| JsAddRef(jsDrawingContextProto, nullptr) != JsNoError
				|| !js->DefineObjPropFunc(jsDrawingContextProto, "DrawingContext", "drawText", &PlayfieldView::JsDrawDrawText, this, eh)
				|| !js->DefineObjPropFunc(jsDrawingContextProto, "DrawingContext", "setFont", &PlayfieldView::JsDrawSetFont, this, eh)
				|| !js->DefineObjPropFunc(jsDrawingContextProto, "DrawingContext", "setTextColor", &PlayfieldView::JsDrawSetTextColor, this, eh)
				|| !js->DefineObjPropFunc(jsDrawingContextProto, "DrawingContext", "setTextAlign", &PlayfieldView::JsDrawSetTextAlign, this, eh)
				|| !js->DefineObjPropFunc(jsDrawingContextProto, "DrawingContext", "drawImage", &PlayfieldView::JsDrawDrawImage, this, eh)
				|| !js->DefineObjPropFunc(jsDrawingContextProto, "DrawingContext", "getImageSize", &PlayfieldView::JsDrawGetImageSize, this, eh)
				|| !js->DefineObjPropFunc(jsDrawingContextProto, "DrawingContext", "setTextArea", &PlayfieldView::JsDrawSetTextArea, this, eh)
				|| !js->DefineObjPropFunc(jsDrawingContextProto, "DrawingContext", "setTextOrigin", &PlayfieldView::JsDrawSetTextOrigin, this, eh)
				|| !js->DefineObjPropFunc(jsDrawingContextProto, "DrawingContext", "getTextOrigin", &PlayfieldView::JsDrawGetTextOrigin, this, eh)
				|| !js->DefineObjPropFunc(jsDrawingContextProto, "DrawingContext", "measureText", &PlayfieldView::JsDrawMeasureText, this, eh)
				|| !js->DefineObjPropFunc(jsDrawingContextProto, "DrawingContext", "fillRect", &PlayfieldView::JsDrawFillRect, this, eh)
				|| !js->DefineObjPropFunc(jsDrawingContextProto, "DrawingContext", "frameRect", &PlayfieldView::JsDrawFrameRect, this, eh)
				|| !js->DefineObjPropFunc(jsDrawingContextProto, "DrawingContext", "getSize", &PlayfieldView::JsDrawGetSize, this, eh))
				return;

			// Set up the game list methods.  These are nominally on the gameList Javascript
			// object, but the actual implementations are still PlayfieldView:: methods.  We
			// implement the methods here because the actual GameList instance can be deleted
			// and re-created dynamically during a session.  The PlayfieldView instance has
			// session lifetime, so implementing the methods on PlayfieldView makes it easier
			// to manage the references.  It doesn't otherwise really matter where these
			// methods are implemented, since everything's just a global anyway.  The only
			// reason we bother with different locations for the Javascript methods is to
			// create a more coherent organization from the scripting user's perspective.
			if (!js->DefineObjPropFunc(jsGameList, "gameList", "getGameInfo", &PlayfieldView::JsGetGameInfo, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "getGame", &PlayfieldView::JsGetGame, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "getAllGames", &PlayfieldView::JsGetAllGames, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "getGameCount", &PlayfieldView::JsGetGameCount, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "getWheelGame", &PlayfieldView::JsGetWheelGame, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "getAllWheelGames", &PlayfieldView::JsGetAllWheelGames, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "getWheelCount", &PlayfieldView::JsGetWheelCount, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "getCurFilter", &PlayfieldView::JsGetCurFilter, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "setCurFilter", &PlayfieldView::JsSetCurFilter, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "refreshFilter", &PlayfieldView::JsRefreshFilter, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "getFilterInfo", &PlayfieldView::JsGetFilterInfo, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "getAllFilters", &PlayfieldView::JsGetAllFilters, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "createFilter", &PlayfieldView::JsCreateFilter, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "createMetaFilter", &PlayfieldView::JsCreateMetaFilter, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "removeMetaFilter", &PlayfieldView::JsRemoveMetaFilter, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "getAllCategories", &PlayfieldView::JsGetAllCategories, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "createCategory", &PlayfieldView::JsCreateCategory, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "renameCategory", &PlayfieldView::JsRenameCategory, this, eh)
				|| !js->DefineObjPropFunc(jsGameList, "gameList", "deleteCategory", &PlayfieldView::JsDeleteCategory, this, eh))
				return;

			// Set up the GameInfo methods
			if (!js->DefineObjMethod(jsGameInfo, "GameInfo", "getHighScores", &PlayfieldView::JsGetHighScores, this, eh)
				|| !js->DefineObjMethod(jsGameInfo, "GameInfo", "setHighScores", &PlayfieldView::JsSetHighScores, this, eh)
				|| !js->DefineObjMethod(jsGameInfo, "GameInfo", "resolveGameFile", &PlayfieldView::JsResolveGameFile, this, eh)
				|| !js->DefineObjMethod(jsGameInfo, "GameInfo", "resolveMedia", &PlayfieldView::JsResolveMedia, this, eh)
				|| !js->DefineObjMethod(jsGameInfo, "GameInfo", "resolveROM", &PlayfieldView::JsResolveROM, this, eh)
				|| !js->DefineObjMethod(jsGameInfo, "GameInfo", "update", &PlayfieldView::JsGameInfoUpdate, this, eh)
				|| !js->DefineObjMethod(jsGameInfo, "GameInfo", "renameMediaFiles", &PlayfieldView::JsGameInfoRenameMediaFiles, this, eh)
				|| !js->DefineObjMethod(jsGameInfo, "GameInfo", "erase", &PlayfieldView::JsGameInfoErase, this, eh))
				return;

			// Some convenience definitions for the GameInfo getters
			using JE = JavascriptEngine;
			using JsObj = JE::JsObj;
#define JsUndef (JavascriptEngine::Get()->GetUndefVal())

			// GameInfo "categories" getter (it's a bit large to define inline)
			static auto GetCategories = [](GameListItem *game)
			{
				// get the category list
				std::list<const GameCategory*> catList;
				GameList::Get()->GetCategoryList(game, catList);

				// translate it to a javascript array
				auto arr = JsObj::CreateArray();
				for (auto cat : catList)
					arr.Push(cat->name);

				return arr.jsobj;
			};

			// Set up the GameInfo getters
			if (!AddGameInfoGetter<TSTRING>("configId", [](GameListItem *game) { return game->GetGameId(); }, eh)
				|| !AddGameInfoGetter<TSTRING>("displayName", [](GameListItem *game) { return game->GetDisplayName(); }, eh)
				|| !AddGameInfoGetter<TSTRING>("title", [](GameListItem *game) { return game->title; }, eh)
				|| !AddGameInfoGetter<TSTRING>("ipdbId", [](GameListItem *game) { return game->ipdbId; }, eh)
				|| !AddGameInfoGetter<JsValueRef>("rom",
					[](GameListItem *game) { return game->rom.length() != 0 ? JE::NativeToJs(game->rom) : JsUndef; }, eh)
				|| !AddGameInfoGetter<TSTRING>("mediaName", [](GameListItem *game) { return game->mediaName; }, eh)
				|| !AddGameInfoGetter<JsValueRef>("year", [](GameListItem *game) { return game->year != 0 ? JE::NativeToJs(game->year) : JsUndef; }, eh)
				|| !AddGameInfoGetter<JsValueRef>("tableType",
					[](GameListItem *game) { return game->tableType.length() != 0 ? JE::NativeToJs(game->tableType) : JsUndef; }, eh)
				|| !AddGameInfoGetter<JsValueRef>("filename",
					[](GameListItem *game) { return game->filename.length() != 0 ? JE::NativeToJs(game->filename) : JsUndef; }, eh)
				|| !AddGameInfoGetter<JsValueRef>("path",
					[](GameListItem *game) { return game->tableFileSet != nullptr ? JE::NativeToJs(game->tableFileSet->tablePath) : JsUndef; }, eh)
				|| !AddGameInfoGetter<JsValueRef>("gridPos",
					[](GameListItem *game) { auto gp = JsObj::CreateObject(); gp.Set("row", game->gridPos.row); gp.Set("column", game->gridPos.col); return gp.jsobj; }, eh)
				|| !AddGameInfoGetter<JsValueRef>("manufacturer",
					[](GameListItem *game) { return game->manufacturer != nullptr ? JE::NativeToJs(game->manufacturer->manufacturer) : JsUndef; }, eh)
				|| !AddGameInfoGetter<JsValueRef>("system",
					[](GameListItem *game) { return Application::Get()->GetPlayfieldView()->BuildGameSysInfo(game->system); }, eh)
				|| !AddGameInfoGetter<JsValueRef>("dbFile",
					[](GameListItem *game) { return game->dbFile != nullptr ? JE::NativeToJs(game->dbFile->filename) : JsUndef; }, eh)
				|| !AddGameInfoGetter<bool>("isConfigured", [](GameListItem *game) { return game->isConfigured; }, eh)
				|| !AddGameInfoGetter<bool>("isHidden", [](GameListItem *game) { return game->IsHidden(); }, eh)
				|| !AddGameInfoStatsGetter<JsValueRef>("lastPlayed",
					[](GameListItem *game) { auto d = GameList::Get()->GetLastPlayed(game); return d != nullptr && d[0] != 0 ? JE::NativeToJs(DateTime(d)) : JsUndef; }, eh)
				|| !AddGameInfoStatsGetter<JsValueRef>("dateAdded",
					[](GameListItem *game) { auto d = GameList::Get()->GetDateAdded(game); return d != nullptr && d[0] != 0 ? JE::NativeToJs(DateTime(d)) : JsUndef; }, eh)
				|| !AddGameInfoStatsGetter<JsValueRef>("highScoreStyle",
					[](GameListItem *game) { auto hs = GameList::Get()->GetHighScoreStyle(game); return hs != nullptr ? JE::NativeToJs(hs) : JsUndef; }, eh)
				|| !AddGameInfoStatsGetter<double>("playCount", [](GameListItem *game) { return static_cast<double>(GameList::Get()->GetPlayCount(game)); }, eh)
				|| !AddGameInfoStatsGetter<double>("playTime", [](GameListItem *game) { return static_cast<double>(GameList::Get()->GetPlayTime(game)); }, eh)
				|| !AddGameInfoStatsGetter<bool>("isFavorite", [](GameListItem *game) { return GameList::Get()->IsFavorite(game); }, eh)
				|| !AddGameInfoStatsGetter<double>("rating", [](GameListItem *game) { return static_cast<double>(GameList::Get()->GetRating(game)); }, eh)
				|| !AddGameInfoStatsGetter<bool>("isMarkedForCapture", [](GameListItem *game) { return GameList::Get()->IsMarkedForCapture(game); }, eh)
				|| !AddGameInfoStatsGetter<int>("audioVolume", [](GameListItem *game) { return GameList::Get()->GetAudioVolume(game); }, eh)
				|| !AddGameInfoStatsGetter<JsValueRef>("categories", GetCategories, eh))
				return;

#undef JsUndef

			// GameSysInfo.dbFiles
			auto dbFilesGetter = [](GameSystem *sys)
			{
				// build an array of descriptor objects
				auto arr = JavascriptEngine::JsObj::CreateArray();
				for (auto &f : sys->dbFiles)
				{
					// build the database file descriptor object
					auto obj = JavascriptEngine::JsObj::CreateObject();
					obj.Set("filename", f->filename);
					if (f->category != nullptr)
					{
						obj.Set("category", f->category->name);
						obj.Set("categoryFilter", Application::Get()->GetPlayfieldView()->BuildFilterInfo(f->category));
					}

					// add the object to the array
					arr.Push(obj);
				}
				return arr.jsobj;
			};

			// Set up the GameSysInfo getters
			if (!AddGameSysInfoGetter<WSTRING>("displayName", [](GameSystem *sys) { return sys->displayName; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("systemClass", [](GameSystem *sys) { return sys->systemClass; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("mediaDir", [](GameSystem *sys) { return sys->mediaDir; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("databaseDir", [](GameSystem *sys) { return sys->databaseDir; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("exe", [](GameSystem *sys) { return sys->exe; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("tablePath", [](GameSystem *sys) { return sys->tablePath; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("nvramPath", [](GameSystem *sys) { return sys->nvramPath; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("defExt", [](GameSystem *sys) { return sys->defExt; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("params", [](GameSystem *sys) { return sys->params; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("workingPath", [](GameSystem *sys) { return sys->workingPath; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("processName", [](GameSystem *sys) { return sys->process; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("startupKeys", [](GameSystem *sys) { return sys->startupKeys; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("envVars", [](GameSystem *sys) { return sys->envVars; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("terminateBy", [](GameSystem *sys) { return sys->terminateBy; }, eh)
				|| !AddGameSysInfoGetter<int>("swShow", [](GameSystem *sys) { return static_cast<int>(sys->swShow); }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("dofTitlePrefix", [](GameSystem *sys) { return sys->dofTitlePrefix; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("runBeforePre", [](GameSystem *sys) { return sys->runBeforePre; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("runBefore", [](GameSystem *sys) { return sys->runBefore; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("runAfter", [](GameSystem *sys) { return sys->runAfter; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("runAfterPost", [](GameSystem *sys) { return sys->runAfterPost; }, eh)
				|| !AddGameSysInfoGetter<WSTRING>("genericDbFile", [](GameSystem *sys) { return sys->genericDbFilename; }, eh)
				|| !AddGameSysInfoGetter<JsValueRef>("dbFiles", dbFilesGetter, eh))
				return;

			// set up GameSysInfo methods
			if (!js->DefineObjMethod(jsGameSysInfo, "GameSysInfo", "expand", &PlayfieldView::JsExpandSysVar, this, eh))
				return;

			// Set up the FilterInfo methods
			if (!js->DefineObjMethod(jsFilterInfo, "FilterInfo", "getGames", &PlayfieldView::JsFilterInfoGetGames, this, eh)
				|| !js->DefineObjMethod(jsFilterInfo, "FilterInfo", "testGame", &PlayfieldView::JsFilterInfoTestGame, this, eh))
				return;

			// set up command IDs in the Command object
			JsValueRef jsCommand;
			if (js->GetGlobProp(jsCommand, "command", where) == JsNoError)
			{
#define C(name, id) js->SetProp(jsCommand, #name, id)
				C(MenuReturn, ID_MENU_RETURN);
				C(PlayGame, ID_PLAY_GAME);
				C(Flyer, ID_FLYER);
				C(GameInfo, ID_GAMEINFO);
				C(HighScores, ID_HIGH_SCORES);
				C(Instructions, ID_INSTRUCTIONS);
				C(AboutBox, ID_ABOUT);
				C(Help, ID_HELP);
				C(Quit, ID_EXIT);
				C(PowerOff, ID_SHUTDOWN);
				C(PowerOffConfirm, ID_SHUTDOWN_CONFIRM);
				C(MuteVideos, ID_MUTE_VIDEOS);
				C(MuteTableAudio, ID_MUTE_TABLE_AUDIO);
				C(MuteButtons, ID_MUTE_BUTTONS);
				C(MuteRepeatButtons, ID_MUTE_REPEAT_BUTTONS);
				C(MuteAttractMode, ID_MUTE_ATTRACTMODE);
				C(PinscapeNightMode, ID_PINSCAPE_NIGHT_MODE);
				C(Options, ID_OPTIONS);
				C(KillGame, ID_KILL_GAME);
				C(PauseGame, ID_PAUSE_GAME);
				C(ResumeGame, ID_RESUME_GAME);
				C(RealDmdAutoEnable, ID_REALDMD_AUTO_ENABLE);
				C(RealDmdEnable, ID_REALDMD_ENABLE);
				C(RealDmdDisable, ID_REALDMD_DISABLE);
				C(RealDmdMirrorHorz, ID_REALDMD_MIRROR_HORZ);
				C(RealDmdMirrorVert, ID_REALDMD_MIRROR_VERT);
				C(AddFavorite, ID_ADD_FAVORITE);
				C(RemoveFavorite, ID_REMOVE_FAVORITE);
				C(RateGame, ID_RATE_GAME);
				C(FilterByEra, ID_FILTER_BY_ERA);
				C(FilterByManufacturer, ID_FILTER_BY_MANUF);
				C(FilterBySystem, ID_FILTER_BY_SYS);
				C(FilterByRating, ID_FILTER_BY_RATING);
				C(FilterByCategory, ID_FILTER_BY_CATEGORY);
				C(FilterByRecency, ID_FILTER_BY_RECENCY);
				C(FilterByAdded, ID_FILTER_BY_ADDED);
				C(ClearCredits, ID_CLEAR_CREDITS);
				C(ShowOperatorMenu, ID_OPERATOR_MENU);
				C(BatchCaptureStep1, ID_BATCH_CAPTURE_STEP1);
				C(BatchCaptureAll, ID_BATCH_CAPTURE_ALL);
				C(BatchCaptureFilter, ID_BATCH_CAPTURE_FILTER);
				C(BatchCaptureMarked, ID_BATCH_CAPTURE_MARKED);
				C(BatchCaptureStep3, ID_BATCH_CAPTURE_STEP3);
				C(BatchCaptureStep4, ID_BATCH_CAPTURE_STEP4);
				C(BatchCaptureView, ID_BATCH_CAPTURE_VIEW);
				C(BatchCaptureGo, ID_BATCH_CAPTURE_GO);
				C(EditGameInfo, ID_EDIT_GAME_INFO);
				C(DeleteGameInfo, ID_DEL_GAME_INFO);
				C(ConfirmDeleteGameInfo, ID_CONFIRM_DEL_GAME_INFO);
				C(SetCategories, ID_SET_CATEGORIES);
				C(MenuPageUp, ID_MENU_PAGE_UP);
				C(MenuPageDown, ID_MENU_PAGE_DOWN);
				C(SaveCategories, ID_SAVE_CATEGORIES);
				C(EditCategories, ID_EDIT_CATEGORIES);
				C(CaptureMediaSetup, ID_CAPTURE_MEDIA);
				C(CaptureGo, ID_CAPTURE_GO);
				C(CaptureLayoutSkip, ID_CAPTURE_LAYOUT_SKIP);
				C(CaptureLayoutOk, ID_CAPTURE_LAYOUT_OK);
				C(MarkForBatchCapture, ID_MARK_FOR_BATCH_CAPTURE);
				C(ShowFindMediaMenu, ID_FIND_MEDIA);
				C(FindMediaGo, ID_MEDIA_SEARCH_GO);
				C(ShowMediaFiles, ID_SHOW_MEDIA_FILES);
				C(AdjustAudioVolume, ID_ADJUST_AUDIO_VOLUME);
				C(DeleteMediaFile, ID_DEL_MEDIA_FILE);
				C(HideGame, ID_HIDE_GAME);
				C(EnableVideos, ID_ENABLE_VIDEO_GLOBAL);
				C(RestartAsAdmin, ID_RESTART_AS_ADMIN);
				C(MediaDropPhase2, ID_MEDIA_DROP_PHASE2);
				C(MediaDropGo, ID_MEDIA_DROP_GO);
				C(AdjustCaptureDelay, ID_CAPTURE_ADJUSTDELAY);
				C(FilterFirst, ID_FILTER_FIRST);
				C(FilterLast, ID_FILTER_LAST);
				C(UserFilterGroupFirst, ID_USER_FILTER_GROUP_FIRST);
				C(UserFilterGroupLast, ID_USER_FILTER_GROUP_LAST);
				C(PickSysFirst, ID_PICKSYS_FIRST);
				C(PickSysLast, ID_PICKSYS_LAST);
				C(CaptureFirst, ID_CAPTURE_FIRST);
				C(CaptureLast, ID_CAPTURE_LAST);
				C(MediaDropFirst, ID_MEDIADROP_FIRST);
				C(MediaDropLast, ID_MEDIADROP_LAST);
				C(ShowGameSetupMenu, ID_GAME_SETUP);
				C(ShowMainMenu, ID_SHOW_MAIN_MENU);
				C(ShowExitMenu, ID_SHOW_EXIT_MENU);
				C(UserFirst, ID_USER_FIRST);
				C(UserLast, ID_USER_LAST);
				C(ViewBackglass, ID_VIEW_BACKGLASS);
				C(ViewDMD, ID_VIEW_DMD);
				C(ViewPlayfield, ID_VIEW_PLAYFIELD);
				C(ViewTopper, ID_VIEW_TOPPER);
				C(ViewInstCard, ID_VIEW_INSTCARD);
				C(ToggleFrameCounter, ID_FPS);
				C(ToggleFullScreenMode, ID_FULL_SCREEN);
				C(HideWindow, ID_HIDE);
				C(ToggleWindowBorders, ID_WINDOW_BORDERS);
				C(RotateWindowCW, ID_ROTATE_CW);
				C(RotateWindowCCW, ID_ROTATE_CCW);
				C(MirrorWindowHorz, ID_MIRROR_HORZ);
				C(MirrorWindowVert, ID_MIRROR_VERT);
				C(SWFErrorDisable, ID_SWF_ERROR_DISABLE);
				C(SWFErrorSuppress, ID_SWF_ERROR_SUPPRESS);
				C(SWFErrorHelp, ID_SWF_ERROR_HELP);
#undef C

				// initialize the ID-to-name table
				js->CallMethod<void>(jsCommand, "_init");
			}

			// Execute the user script.  This sets up event handlers for
			// any events the script wants to be notified about.
			LogFile::Get()->Write(LogFile::JSLogging, _T(". Loading main script file %s\n"), jsmain);
			if (!js->LoadModule(jsmain, eh))
				return;

			// Allow imported modules to run.  Imports are deferred, similar
			// to promises, so imported code won't run until the next task
			// processing phase.  We want to allow imports to initialize now
			// rather than later, so that imported modules can receive events
			// generated during startup, such as the initial game selection.
			//
			// Repeat until there's nothing to run.  Imports could trigger 
			// other imports dynamically, and we want to descend the whole 
			// tree until everything is loaded and initialized.  But make
			// sure we don't get stuck in an infinite loop with something
			// pathological, like zero-length setTimeout()'s that keep
			// resetting themselves: so stop after an arbitrary (large)
			// number of iterations.
			for (int iters = 0; iters < 100 && js->RunTasks(); ++iters);

			// We successfully initialized the javascript engine and loaded
			// the user script.
			cleanup.success = true;
		}
		catch (JavascriptEngine::CallException exc)
		{
			exc.Log(_T(". error initializing Javascript"), &eh);
		}
	}
}

static const TCHAR *jsbool(bool b) { return b ? _T("true") : _T("false"); }

bool PlayfieldView::FireCommandEvent(int cmd)
{
	bool ret = true;
	if (auto js = JavascriptEngine::Get(); js != nullptr)
		ret = js->FireEvent(jsMainWindow, jsCommandEvent, cmd);

	return ret;
}

bool PlayfieldView::FireMenuEvent(bool open, Menu *menu, int pageno)
{
	bool ret = true;
	if (auto js = JavascriptEngine::Get(); js != nullptr)
	{
		if (open && menu != nullptr)
		{
			// menu open event
			try
			{
				// build the Javascript menu descriptor
				using JsObj = JavascriptEngine::JsObj;
				JsObj arr = JsObj::CreateArray();
				for (auto &m : menu->descs)
				{
					// create en element object and add it to the array
					JsObj ele = JsObj::CreateObject();
					arr.Push(ele);

					// populate it
					ele.Set("title", m.text);
					ele.Set("cmd", m.cmd);
					if (m.selected) ele.Set("selected", true);
					if (m.checked) ele.Set("checked", true);
					if (m.radioChecked) ele.Set("radio", true);
					if (m.hasSubmenu) ele.Set("hasSubmenu", true);
					if (m.stayOpen) ele.Set("stayOpen", true);
				}

				// set up the options object
				JsObj options = JsObj::CreateObject();
				if ((menu->flags & SHOWMENU_IS_EXIT_MENU) != 0) options.Set("isExitMenu", true);
				if ((menu->flags & SHOWMENU_NO_ANIMATION) != 0) options.Set("noAmination", true);
				if ((menu->flags & SHOWMENU_DIALOG_STYLE) != 0) options.Set("dialogStyle", true);
				options.Set("pageNo", pageno);

				// Fire the event, retrieving the 
				JsValueRef eventObjVal;
				ret = js->FireAndReturnEvent(eventObjVal, jsMainWindow, jsMenuOpenEvent, menu->id.c_str(), arr, options);

				// If the "menuUpdated" property is set in the event object, the caller
				// modified the menu array.  Show the modified menu instead of the original.
				JavascriptEngine::JsObj eventObj(eventObjVal);
				if (eventObj.Get<bool>("menuUpdated"))
				{
					// show the menu as though via Javascript mainWindow.showMenu()
					JsShowMenu(
						eventObj.Get<WSTRING>("id"),
						eventObj.Get<std::vector<JsValueRef>>("items"),
						eventObj.Get<JavascriptEngine::JsObj>("options"));

					// Tell the caller not to proceed with the native menu, since
					// we're showing this one instead.
					return false;
				}
			}
			catch (...)
			{
				// ignore errors
			}
		}
		else if (curMenu != nullptr)
		{
			// menu close event
			ret = js->FireEvent(jsMainWindow, jsMenuCloseEvent, curMenu->id.c_str());
		}
	}

	return ret;
}

bool PlayfieldView::FirePopupEvent(bool open, const WCHAR *name)
{
	bool ret = true;
	if (auto js = JavascriptEngine::Get(); js != nullptr)
		ret = js->FireEvent(jsMainWindow, open ? jsPopupOpenEvent : jsPopupCloseEvent, name);

	return ret;
}

bool PlayfieldView::FireCommandButtonEvent(const QueuedKey &key)
{
	bool ret = true;
	if (key.cmd != nullptr)
	{
		if (auto js = JavascriptEngine::Get(); js != nullptr)
		{
			// figure the event type
			JsValueRef event = key.bg ?
				(key.mode ? jsCommandButtonBgDownEvent : jsCommandButtonBgUpEvent) :
				(key.mode ? jsCommandButtonDownEvent : jsCommandButtonUpEvent);

			// fire it
			ret = js->FireEvent(jsMainWindow, event, key.cmd->name, key.mode == KeyRepeat || key.mode == KeyBgRepeat);
		}
	}

	return ret;
}

bool PlayfieldView::FireKeyEvent(int vkey, bool down, bool repeat, bool bg)
{
	bool ret = true;
	if (auto js = JavascriptEngine::Get(); js != nullptr)
	{
		// get the key label information from the key manager
		auto const& label = KeyInput::keyName[KeyInput::IsValidKeyCode(vkey) ? vkey : 0];

		// Figure the javascript event 'key' value.  If the key name contains
		// a '|' character, it has an alternate shifted meaning.
		auto jsKey = label.jsEventKey;
		auto jsKeyLen = _tcslen(jsKey);
		if (auto bar = _tcschr(jsKey, '|'); bar != nullptr)
		{
			// Get the Shift key state.  Use the input state we've deduced
			// from the raw input stream - see the comments above the struct
			// definition for RawShiftKeyState for a lengthy explanation of
			// and tale of woe about why we're not using GetKeyState(VK_SHIFT),
			// which any good Windows programmer would *think* we should be
			// using here.  (Short version: there's a really, really, REALLY
			// bizarre misfeature in that function that we have to work around.)
			bool shifted = rawShiftKeyState.left || rawShiftKeyState.right;

			// numeric keypad keys (location '3') are special
			if (label.jsEventLocation == 3)
			{
				// get the NumLock state
				bool numLock = ((GetKeyState(VK_NUMLOCK) & 0x0001) != 0);

				// For keypad keys, the "unshifted|shifted" names are set up for NumLock mode,
				// with the numeric key in the unshifted part and the cursor key in the shifted
				// part.  When we're in NumLock mode, the shift key has exactly the effect of
				// selecting one of these.  When we're NOT in NumLock mode, the keypad simply
				// acts like the shift key is always in effect (that is, the cursor key is
				// always used).  So if NumLock is off, simply select the shifted meaning in
				// all cases, otherwise go by the current shift key state.
				if (!numLock)
					shifted = true;
			}

			// alphabetic keys are special: the actual shift status for alphabetic
			// keys is Shift XOR Caps Lock
			if (vkey >= 'A' && vkey <= 'Z')
			{
				bool capsLock = ((GetKeyState(VK_CAPITAL) & 0x0001) != 0);
				shifted ^= capsLock;
			}

			// if unshifted, use the part before the '|', otherwise use the part after
			if (shifted)
			{
				jsKeyLen -= (bar + 1 - jsKey);
				jsKey = bar + 1;
			}
			else
				jsKeyLen = bar - jsKey;
		}

		// figure the javascript event type
		JsValueRef eventType = bg ? 
			(down ? jsKeyBgDownEvent : jsKeyBgUpEvent) :
			(down ? jsKeyDownEvent : jsKeyUpEvent);

		// dispatch the javsacript event
		ret = js->FireEvent(jsMainWindow, eventType, vkey,
			TSTRING(jsKey, jsKeyLen).c_str(), label.jsEventCode, label.jsEventLocation, 
			repeat, bg);
	}
	return ret;
}

bool PlayfieldView::FireJoystickEvent(int unit, int button, bool down, bool repeat, bool bg)
{
	bool ret = true;
	if (auto js = JavascriptEngine::Get(); js != nullptr)
	{
		JsValueRef eventType = bg ?
			(down ? jsJoystickButtonBgDownEvent : jsJoystickButtonBgUpEvent) :
			(down ? jsJoystickButtonDownEvent : jsJoystickButtonUpEvent);

		ret = js->FireEvent(jsMainWindow, eventType, unit, button, repeat, bg);
	}
	return ret;
}

bool PlayfieldView::FireAttractModeEvent(bool starting)
{
	bool ret = true;
	if (auto js = JavascriptEngine::Get(); js != nullptr)
		ret = js->FireEvent(jsMainWindow, starting ? jsAttractModeStartEvent : jsAttractModeEndEvent);

	return ret;
}

void PlayfieldView::UpdateJsUIMode()
{
	// figure the new mode
	JSUIMode newMode;
	if (curMenu != nullptr)
		newMode = jsuiMenu;
	else if (popupSprite != nullptr)
		newMode = jsuiPopup;
	else if (runningGameMode != None)
		newMode = jsuiRun;
	else if (attractMode.active)
		newMode = jsuiAttract;
	else
		newMode = jsuiWheel;

	// if this is a different mode, update it
	if (newMode != jsuiMode)
	{
		// note the new mode
		jsuiMode = newMode;

		// On switching to wheel mode, fire an event.  This isn't necessary
		// on other events, as we have other more event-specific handling
		// for the rest.
		if (newMode == jsuiWheel)
			FireWheelModeEvent();
	}
}

void PlayfieldView::FireWheelModeEvent()
{
	if (auto js = JavascriptEngine::Get(); js != nullptr)
		js->FireEvent(jsMainWindow, jsWheelModeEvent);
}

void PlayfieldView::FireGameSelectEvent(GameListItem *game)
{
	LONG id = IsGameValid(game) ? game->internalID : 0;
	if (id != jsLastGameSelectReport)
	{
		jsLastGameSelectReport = id;
		if (auto js = JavascriptEngine::Get(); js != nullptr)
			js->FireEvent(jsGameList, jsGameSelectEvent, BuildJsGameInfo(game));
	}
}

void PlayfieldView::JsAlert(TSTRING msg)
{
	MessageBox(GetParent(hWnd), msg.c_str(), _T("PinballY"), MB_OK | MB_ICONINFORMATION);
}

void PlayfieldView::JsMessage(TSTRING msg, TSTRING typ)
{
	ErrorIconType iconType =
		_tcsicmp(typ.c_str(), _T("error")) == 0 ? EIT_Error :
		_tcsicmp(typ.c_str(), _T("warning")) == 0 ? EIT_Warning :
		EIT_Information;

	ShowError(iconType, msg.c_str());
}

void PlayfieldView::JsLog(TSTRING msg)
{
	LogFile::Get()->Write(_T("[Script] %s\n"), msg.c_str());
}

void PlayfieldView::JsOutputDebugString(TSTRING msg)
{
	OutputDebugString(msg.c_str());
}

double PlayfieldView::JsSetTimeout(JsValueRef func, double dt)
{
	auto task = new JavascriptEngine::TimeoutTask(func, dt);
	JavascriptEngine::Get()->AddTask(task);
	return task->id;
}

void PlayfieldView::JsClearTimeout(double id)
{
	JavascriptEngine::Get()->EnumTasks([id](JavascriptEngine::Task *task)
	{
		if (auto tt = dynamic_cast<JavascriptEngine::TimeoutTask*>(task); tt != nullptr && tt->id == id)
		{
			tt->cancelled = true;
			return false;
		}
		return true;
	});
}

double PlayfieldView::JsSetInterval(JsValueRef func, double dt)
{
	auto task = new JavascriptEngine::IntervalTask(func, dt);
	JavascriptEngine::Get()->AddTask(task);
	return task->id;
}

void PlayfieldView::JsClearInterval(double id)
{
	JavascriptEngine::Get()->EnumTasks([id](JavascriptEngine::Task *task)
	{
		if (auto it = dynamic_cast<JavascriptEngine::IntervalTask*>(task); it != nullptr && it->id == id)
		{
			it->cancelled = true;
			return false;
		}
		return true;
	});
}

void PlayfieldView::JsConsoleLog(TSTRING level, TSTRING message)
{
	OutputDebugString(MsgFmt(_T("console.log(%s): %s\n"), level.c_str(), message.c_str()));
	if (auto js = JavascriptEngine::Get(); js != nullptr)
		js->DebugConsoleLog(level.c_str(), message.c_str());
}

JsValueRef PlayfieldView::JsGetUIMode()
{
	JsValueRef obj = JS_INVALID_REFERENCE;
	if (auto js = JavascriptEngine::Get(); js != nullptr)
	{
		// create the mode descriptor object
		if (js->CreateObj(obj))
		{
			// figure the main mode, and set sub-mode information as appropriate
			const WCHAR *mode = nullptr;
			if (curMenu != nullptr)
			{
				mode = L"menu";
				js->SetProp(obj, "menuID", curMenu->id.c_str());
			}
			else if (popupSprite != nullptr)
			{
				mode = L"popup";
				js->SetProp(obj, "popupID", popupName);
			}
			else if (runningGameMode != None)
			{
				mode = L"running";
			}
			else if (attractMode.active)
			{
				mode = L"attract";
			}
			else
			{
				mode = L"wheel";
			}

			// if a game is running, report the current running state
			if (runningGameMode != None)
			{
				js->SetProp(obj, "runMode",
					runningGameMode == Starting ? L"starting" :
					runningGameMode == Running ? L"running" :
					runningGameMode == Exiting ? L"exiting" :
					L"other");

				if ((lastPlayGameLaunchFlags & Application::LaunchFlags::Capturing) != 0)
					js->SetProp(obj, "capture", batchCaptureMode.active ? L"batch" : L"single");
			}

			// if a game is selected, report the selection
			if (auto game = GameList::Get()->GetNthGame(0); IsGameValid(game))
				js->SetProp(obj, "game", BuildJsGameInfo(game));
		
			// set the main mode string
			js->SetProp(obj, "mode", mode);
		}
	}

	return obj;
}

JsValueRef PlayfieldView::JsGetActiveWindow()
{
	// test a window to see if it's the active window; if so, sets jsobj to
	// the Javascript object representing the window and returns true
	JsValueRef jsobj = JavascriptEngine::Get()->GetNullVal();
	auto W = [&jsobj](FrameWin *win, JsValueRef obj)
	{
		if (win != nullptr && win->IsNcActive())
		{
			jsobj = obj;
			return true;
		}
		return false;
	};

	// try each window, stopping when we find the active one
	auto app = Application::Get();
	W(app->GetPlayfieldWin(), jsMainWindow)
		|| W(app->GetBackglassWin(), jsBackglassWindow)
		|| W(app->GetDMDWin(), jsDMDWindow)
		|| W(app->GetInstCardWin(), jsInstCardWindow)
		|| W(app->GetTopperWin(), jsTopperWindow);

	// return whatever we found
	return jsobj;
}

void PlayfieldView::JsPlayButtonSound(WSTRING name)
{
	PlayButtonSound(name.c_str());
}

JsValueRef PlayfieldView::JsGetKeyCommand(JavascriptEngine::JsObj desc)
{
	auto js = JavascriptEngine::Get();
	try
	{
		// get the key type
		WSTRING type = desc.Get<WSTRING>("type");
		std::list<const KeyCommand*> *list = nullptr;
		if (type == L"key")
		{
			// keyboard key - look it up by virtual key code or 
			int vkey = 0;
			if (desc.Has(L"vkey"))
			{
				// .vkey was specified
				vkey = desc.Get<int>("vkey");
			}
			else if (desc.Has(L"code"))
			{
				// .key was specified - get the key name
				WSTRING key = desc.Get<WSTRING>("code");

				// look up the vkey for the key name
				vkey = KeyInput::GetInstance()->KeyByJsKeyCode(key.c_str());
			}

			// look up the command list by vkey
			if (auto it = vkeyToCommand.find(vkey); it != vkeyToCommand.end())
				list = &it->second;
		}
		else if (type == L"joystick")
		{
			// joystick button - get the joystick unit and button
			int unit = desc.Get<int>("unit");
			int button = desc.Get<int>("button");
			if (auto it = jsCommands.find(JsCommandKey(unit, button)); it != jsCommands.end())
				list = &it->second;
		}

		// If we found a list, return it as an array of command name strings.
		// If not, simply return an empty array.
		auto arr = JavascriptEngine::JsObj::CreateArray();
		if (list != nullptr)
		{
			// build the array of command name strings
			for (auto cmd : *list)
				arr.Push(cmd->name);
		}

		// return the array
		return arr.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

void PlayfieldView::UpdateMenuKeys(HMENU hMenu)
{
	// make a table of the key assignments, keyed by command ID
	std::unordered_map<int, UINT> cmdToVkey;
	InputManager::GetInstance()->EnumButtons([this, &cmdToVkey](const InputManager::Command &cmd, const InputManager::Button &btn)
	{
		// check if this internal command has an associated menu command
		auto it = commandNameToMenuID.find(cmd.configID);
		if (it != commandNameToMenuID.end())
		{
			// if this is a keyboard key, and there's not already a key
			// mapping for this command, add it to the list
			if (btn.devType == InputManager::Button::TypeKB
				&& cmdToVkey.find(it->second) == cmdToVkey.end())
			{
				// add the mapping
				cmdToVkey.emplace(it->second, btn.code);
			}
		}
	});

	// visit each menu item
	for (int i = 0; ; i++)
	{
		// get this menu item
		MENUITEMINFO mii;
		TCHAR buf[256];
		mii.dwTypeData = buf;
		mii.cch = countof(buf);
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING;
		if (!GetMenuItemInfo(hMenu, i, TRUE, &mii))
			break;

		// skip system menu items
		if (mii.wID >= SC_SIZE && mii.wID <= SC_CONTEXTHELP)
			continue;

		// if it's a string item, update its key association
		if (mii.fType == MFT_STRING)
		{
			// see if there's a key association for the command
			const TCHAR *keyName = 0;
			if (auto it = cmdToVkey.find(mii.wID); it != cmdToVkey.end())
				keyName = KeyInput::keyName[it->second].friendlyName;

			// find the key name portion of the current menu title, or the
			// end of the title if there's no key portion
			TCHAR *tab = _tcschr(buf, '\t');
			if (tab == 0)
				tab = buf + _tcslen(buf);

			// strip the existing key name
			*tab = '\0';

			// if there's a new key name, add it
			if (keyName != 0)
				_stprintf_s(tab, countof(buf) - (tab - buf), _T("\t%s"), keyName);

			// update the menu name
			SetMenuItemInfo(hMenu, i, TRUE, &mii);
		}
	}
}

JsValueRef PlayfieldView::JsGetGameInfo(WSTRING id)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();

	// Look up the object.  The ID can be provided as a numeric internal ID, or
	// as a config ID.
	GameListItem *game;
	if ((game = gl->GetGameByID(WSTRINGToTSTRING(id).c_str())) == nullptr)
	{
		// no luck that way - if it's numeric, try it as an internal ID
		if (auto nId = _wtoi(id.c_str()); nId != 0)
			game = gl->GetByInternalID(nId);
	}

	// if there's no such game, return null
	if (game == nullptr)
		return js->GetNullVal();

	try
	{
		// build and return the game info object
		return BuildJsGameInfo(game);
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

// Launch override property names
static const CHAR *const launchOverrideProps[] = {
	"envVars",
	"exe",
	"params",
	"processName",
	"runAfter",
	"runAfterPost",
	"runBefore",
	"runBeforePre",
	"terminateBy",
	"workingPath",
	"swShow"
};

void PlayfieldView::JsPlayGame(JsValueRef gameval, JsValueRef optsval)
{
	// Look up the game object by self.id
	auto js = JavascriptEngine::Get();
	try
	{
		// get the game object from val.id
		JavascriptEngine::JsObj gameobj(gameval);
		auto game = GameList::Get()->GetByInternalID(gameobj.Get<int>("id"));
		if (!IsGameValid(game))
			return js->Throw(_T("GameInfo object is no longer valid")), static_cast<void>(0);

		// make sure conditions allow launching a game
		if (Application::Get()->IsGameQueuedForLaunch()
			|| Application::Get()->IsGameRunning()
			|| batchCaptureMode.active)
			return js->Throw(_T("Can't launch now because a game is already running")), static_cast<void>(0);

		// set default options
		int cmd = ID_PLAY_GAME;
		DWORD launchFlags = Application::LaunchFlags::StdPlayFlags;

		// Figure the default system to use for the launch.  If the game has
		// an assigned system, use that; if it's associated with a table file
		// set that has a unique system association, use that; otherwise we
		// can't infer the system automatically, and it must be specified as
		// an option.
		GameSystem *system = nullptr;
		if (game->system != nullptr)
			system = game->system;
		else if (game->tableFileSet != nullptr && game->tableFileSet->systems.size() == 1)
			system = game->tableFileSet->systems.front();

		// get the options
		JavascriptEngine::JsObj options(optsval);
		std::list<std::pair<CSTRING, TSTRING>> overrides;
		if (!options.IsNull())
		{
			// get the launch command code
			if (options.Has(L"command"))
				cmd = options.Get<int>("command");

			// if a system is specified, use it
			if (options.Has(L"system"))
			{
				// get the system
				system = GameList::Get()->GetSystem(options.Get<JavascriptEngine::JsObj>("system").Get<int>("index"));
				if (system == nullptr)
					return js->Throw(_T("GameSysInfo object is no longer valid")), static_cast<void>(0);
			}

			// check for overrides
			if (options.Has(L"overrides"))
			{
				JavascriptEngine::JsObj ov(options.Get<JsValueRef>("overrides"));
				for (auto p : launchOverrideProps)
				{
					if (ov.Has(p))
						overrides.emplace_back(std::make_pair(p, ov.Get<TSTRING>(p)));
				}
			}
		}

		// make sure a system was either inferred or specified explicitly
		if (system == nullptr)
			return js->Throw(_T("This game doesn't have a unique system associated with it, so a system must be specified")), static_cast<void>(0);

		// make sure we're in the base UI mode
		attractMode.Reset(this);
		CloseMenusAndPopups();

		// launch the game in play mode
		PlayGame(cmd, launchFlags, game, system, &overrides);
	}
	catch (JavascriptEngine::CallException exc)
	{
		js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

template<typename T> 
T PlayfieldView::JsGameInfoGetter(T (*func)(GameListItem*), JsValueRef self)
{
	// Look up the game object by self.id
	auto js = JavascriptEngine::Get();
	try
	{
		// get the object from self.id
		JavascriptEngine::JsObj selfobj(self);
		auto game = GameList::Get()->GetByInternalID(selfobj.Get<int>("id"));
		if (game == nullptr)
		{
			js->Throw(_T("GameInfo object is no longer valid"));
			return JavascriptEngine::DefaultVal<T>();
		}

		// retrieve and return the result
		return func(game);
	}
	catch (JavascriptEngine::CallException exc)
	{
		js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
		return JavascriptEngine::DefaultVal<T>();
	}
}

template<typename T>
bool PlayfieldView::AddGameInfoGetter(const CHAR *propName, T (*func)(GameListItem*), ErrorHandler &eh)
{
	typedef T funcType(GameListItem*);
	return JavascriptEngine::Get()->DefineGetterSetter(jsGameInfo, "GameInfo", propName,
		JavascriptEngine::Get()->CreateAndSaveMethodWrapper<funcType, T>(&PlayfieldView::JsGameInfoGetter, func), nullptr, eh);
}

template<typename T>
JsValueRef PlayfieldView::JsGameInfoStatsGetter(T (*func)(GameListItem*), JsValueRef self)
{
	// Look up the game object by self.id
	auto js = JavascriptEngine::Get();
	try
	{
		// get the object from self.id
		JavascriptEngine::JsObj selfobj(self);
		auto gl = GameList::Get();
		auto game = gl->GetByInternalID(selfobj.Get<int>("id"));
		if (game == nullptr)
			return js->Throw(_T("GameInfo object is no longer valid"));

		// retrieve and return the result
		return JavascriptEngine::NativeToJs(func(game));
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

template<typename T>
bool PlayfieldView::AddGameInfoStatsGetter(const CHAR *propName, T (*func)(GameListItem*), ErrorHandler &eh)
{
	typedef T funcType(GameListItem *);
	return JavascriptEngine::Get()->DefineGetterSetter(jsGameInfo, "GameInfo", propName,
		JavascriptEngine::Get()->CreateAndSaveMethodWrapper<funcType, JsValueRef>(&PlayfieldView::JsGameInfoStatsGetter, func), nullptr, eh);
}

JsValueRef PlayfieldView::BuildJsGameInfo(GameListItem *game)
{
	// if the game is null or invalid, reutrn a null javascript object
	if (!IsGameValid(game))
		return JavascriptEngine::Get()->GetNullVal();

	// create a GameInfo object for the results
	auto obj = JavascriptEngine::JsObj::CreateObjectWithPrototype(jsGameInfo);

	// populate the properties
	obj.Set("id", game->internalID);

	// return the populated object
	return obj.jsobj;
}

template<typename T>
T PlayfieldView::JsGameSysInfoGetter(T (*func)(GameSystem*), JsValueRef self)
{
	auto js = JavascriptEngine::Get();
	try
	{
		// get the object from self.index
		JavascriptEngine::JsObj selfobj(self);
		auto sys = GameList::Get()->GetSystem(selfobj.Get<int>("index"));
		if (sys == nullptr)
		{
			js->Throw(_T("GameSysInfo object is no longer valid"));
			return JavascriptEngine::DefaultVal<T>();
		}

		// retrieve and return the result
		return func(sys);
	}
	catch (JavascriptEngine::CallException exc)
	{
		js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
		return JavascriptEngine::DefaultVal<T>();
	}
}

template<typename T>
bool PlayfieldView::AddGameSysInfoGetter(const CHAR *propName, T (*func)(GameSystem*), ErrorHandler &eh)
{
	typedef T funcType(GameSystem*);
	return JavascriptEngine::Get()->DefineGetterSetter(jsGameSysInfo, "GameSysInfo", propName,
		JavascriptEngine::Get()->CreateAndSaveMethodWrapper<funcType, T>(&PlayfieldView::JsGameSysInfoGetter, func), nullptr, eh);
}

JsValueRef PlayfieldView::BuildGameSysInfo(GameSystem *system)
{
	// if the game is null or invalid, reutrn a null javascript object
	if (system == nullptr)
		return JavascriptEngine::Get()->GetNullVal();

	// create a GameSysInfo object for the results
	auto obj = JavascriptEngine::JsObj::CreateObjectWithPrototype(jsGameSysInfo);

	// populate the properties
	obj.Set("index", system->configIndex);

	// return the populated object
	return obj.jsobj;
}

WSTRING PlayfieldView::JsExpandSysVar(JsValueRef self, WSTRING str, JsValueRef game)
{
	auto js = JavascriptEngine::Get();
	try
	{
		// get the object from self.index
		JavascriptEngine::JsObj selfobj(self);
		auto sys = GameList::Get()->GetSystem(selfobj.Get<int>("index"));
		if (sys == nullptr)
		{
			js->Throw(_T("GameSysInfo object is no longer valid"));
			return L"";
		}

		// get the game object
		JavascriptEngine::JsObj gameobj(game);
		auto game = GameList::Get()->GetByInternalID(gameobj.Get<int>("id"));
		if (game == nullptr)
		{
			js->Throw(_T("GameInfo object is no longer valid"));
			return L"";
		}

		// expand the string and return the result
		return Application::Get()->ExpandGameSysVars(str, sys, game);
	}
	catch (JavascriptEngine::CallException exc)
	{
		js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
		return L"";
	}
}

JsValueRef PlayfieldView::JsGetHighScores(JsValueRef self)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();

	try
	{
		// get the game from the id in 'self'
		JavascriptEngine::JsObj selfobj(self);
		auto id = selfobj.Get<int>("id");
		auto game = gl->GetByInternalID(id);
		if (game == nullptr)
			return js->Throw(_T("Invalid game ID"));

		// set up the notifier callback
		class Handler : public HighScoresReadyCallback
		{
		public:
			Handler(LONG gameID, JavascriptEngine::Promise *promise) :
				HighScoresReadyCallback(gameID),
				promise(promise)
			{ }

			virtual void Ready(bool success, const WCHAR *source) override
			{
				if (success)
				{
					// recover the game object
					if (auto game = GameList::Get()->GetByInternalID(gameID); game != nullptr)
					{
						// create a Javascript array with the high score text, one element per line
						auto arr = JavascriptEngine::JsObj::CreateArray();
						for (auto &l : game->highScores)
							arr.Push(l);

						// set the source, as the .source property of the array
						arr.Set("source", source);

						// resolve the promise
						promise->Resolve(arr.jsobj);
					}
					else
						promise->Reject(L"Game no longer exists");
				}
				else
				{
					// reject the promise
					try
					{
						auto e = JavascriptEngine::JsObj::CreateError(L"High scores not available");
						e.Set("source", source);
						promise->Reject(e.jsobj);
					}
					catch (JavascriptEngine::CallException exc)
					{
						exc.Log(_T("GameInfo.getHighScores()"));
					}
				}
			}

			std::unique_ptr<JavascriptEngine::Promise> promise;
		};

		// create the Javascript Promise
		std::unique_ptr<JavascriptEngine::Promise> promise(JavascriptEngine::Promise::Create());

		// the return value will be the Javascript promise object, no matter what happens
		JsValueRef jspromise = promise->GetPromise();

		// create our handler object and add it to the ready list
		highScoresReadyList.emplace_back(new Handler(id, promise.release()));

		// If the high score system is ready, send the request.  If not,
		// just leave the callback enqueued; we automatically process all
		// backlogged requests when we get the first ready notification.
		if (hiScoreSysReady)
			RequestHighScores(game, true);

		// return the Javascript Promise object
		return jspromise;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

void PlayfieldView::JsSetHighScores(JsValueRef self, JsValueRef scoresJsObj)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();
	try
	{
		// get the game 
		JavascriptEngine::JsObj selfobj(self);
		auto game = gl->GetByInternalID(selfobj.Get<int>("id"));
		if (game == nullptr)
			return js->Throw(_T("GameSysInfo object is no longer valid")), static_cast<void>(0);

		// clear the game's prior high score list
		bool hadScores = game->highScores.size() != 0;
		game->highScores.clear();

		// clear the high scores rather than setting new ones
		auto ClearHighScores = [game, this]()
		{
			// If the high score status isn't "requested", set it to "init"
			// to indicate that we need new high scores.  Don't do this for
			// "requested" status, as that means a PinEMHi process is running,
			// and in that case we do want to keep its results when it finishes.
			if (game->highScoreStatus != GameListItem::HighScoreStatus::Requested)
				game->highScoreStatus = GameListItem::HighScoreStatus::Init;
		};

		// If the new high score value is null/undefined, simply clear
		// the existing high scores.  
		if (js->IsUndefinedOrNull(scoresJsObj))
			return ClearHighScores();

		// get the scores array as a string list
		JavascriptEngine::JsObj scoresObj(scoresJsObj);
		int n = scoresObj.Get<int>("length");
		for (int i = 0; i < n; ++i)
			game->highScores.emplace_back(scoresObj.GetAtIndex<TSTRING>(i));

		// the game now has cached high score data
		game->highScoreStatus = GameListItem::HighScoreStatus::Received;

		// resolve any promises waiting for the high scores
		OnHighScoresReady(game->internalID, true, L"javascript");

		// apply the new high scores
		ApplyHighScores(game, hadScores);
	}
	catch (JavascriptEngine::CallException exc)
	{
		js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

int PlayfieldView::JsGetGameCount()
{
	return GameList::Get()->GetAllGamesCount();
}

JsValueRef PlayfieldView::JsGetGame(int n)
{
	auto js = JavascriptEngine::Get();
	try
	{
		if (auto game = GameList::Get()->GetAllGamesAt(n); game != nullptr)
			return BuildJsGameInfo(game);
		else
			return js->GetNullVal();
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

JsValueRef PlayfieldView::JsGetAllGames()
{
	auto js = JavascriptEngine::Get();
	try
	{
		// create an array
		auto arr = JavascriptEngine::JsObj::CreateArray();

		// populate it with GameInfo objects for each entry in the master game list
		GameList::Get()->EnumGames([&arr, this](GameListItem *game) { arr.Push(BuildJsGameInfo(game)); });

		// return the array
		return arr.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

int PlayfieldView::JsGetWheelCount()
{
	return GameList::Get()->GetCurFilterCount();
}

JsValueRef PlayfieldView::JsGetWheelGame(int n)
{
	auto js = JavascriptEngine::Get();
	try
	{
		if (auto game = GameList::Get()->GetNthGame(n); game != nullptr)
			return BuildJsGameInfo(game);
		else
			return js->GetNullVal();
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

JsValueRef PlayfieldView::JsGetAllWheelGames()
{
	auto js = JavascriptEngine::Get();
	try
	{
		// create an array
		auto arr = JavascriptEngine::JsObj::CreateArray();

		// populate it from the wheel, starting at the current game
		auto gl = GameList::Get();
		for (int i = 0, n = gl->GetCurFilterCount(); i < n; ++i)
			arr.Push(BuildJsGameInfo(gl->GetNthGame(i)));

		// return the array
		return arr.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

JsValueRef PlayfieldView::JsResolveGameFile(JsValueRef self)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();

	try
	{
		// get the game from the ID in self.id
		JavascriptEngine::JsObj selfobj(self);
		auto id = selfobj.Get<int>("id");
		auto game = gl->GetByInternalID(id);
		if (game == nullptr)
			return js->Throw(_T("Invalid game ID"));

		// resolve the game file
		GameListItem::ResolvedFile rf;
		game->ResolveFile(rf);

		// create an object for the result
		auto obj = JavascriptEngine::JsObj::CreateObject();

		// populate it
		obj.Set("exists", rf.exists);
		obj.Set("path", rf.path);
		obj.Set("folder", rf.folder);
		obj.Set("filename", rf.file);

		// return the object
		return obj.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

JsValueRef PlayfieldView::JsResolveMedia(JsValueRef self, WSTRING type, bool mustExist)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();

	try
	{
		// get the game from the ID in self.id
		JavascriptEngine::JsObj selfobj(self);
		auto id = selfobj.Get<int>("id");
		auto game = gl->GetByInternalID(id);
		if (game == nullptr)
			return js->Throw(_T("Invalid game ID"));

		// find the type
		const MediaType *mediaType = nullptr;
		if (auto mtit = GameListItem::jsMediaTypes.find(type); mtit != GameListItem::jsMediaTypes.end())
			mediaType = mtit->second;
		else
			return js->Throw(_T("Invalid media type"));

		// set up the option flags
		DWORD flags = 0;
		if (mustExist) flags |= GameListItem::GMI_EXISTS;

		// resolve the media
		std::list<TSTRING> filenames;
		game->GetMediaItems(filenames, *mediaType, flags);

		// create an array for the results
		auto arr = JavascriptEngine::JsObj::CreateArray();

		// populate the array
		for (auto &f : filenames)
			arr.Push(f);

		// return the array
		return arr.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

JsValueRef PlayfieldView::JsResolveROM(JsValueRef self)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();

	try
	{
		// get the game from the ID in self.id
		JavascriptEngine::JsObj selfobj(self);
		auto id = selfobj.Get<int>("id");
		auto game = gl->GetByInternalID(id);
		if (game == nullptr)
			return js->Throw(_T("Invalid game ID"));

		// get the VPM ROM
		TSTRING vpmRom;
		bool hasVpmRom = VPinMAMEIfc::FindRom(vpmRom, game);

		// if DOF is active, get the DOF ROM
		const WCHAR *dofRom = nullptr;
		if (auto dofClient = DOFClient::Get(); dofClient != nullptr && DOFClient::IsReady())
			dofRom = dofClient->GetRomForTable(game);

		// try getting the NVRAM file
		TSTRING nvramFile, nvramPath;
		bool hasNv = false;
		if (auto &hs = Application::Get()->highScores; hs != nullptr)
			hasNv = hs->GetNvramFile(nvramPath, nvramFile, game);

		// create an object for the result
		auto obj = JavascriptEngine::JsObj::CreateObject();

		// set VPM ROM name, if available
		if (hasVpmRom)
		{
			// set the ROM name
			obj.Set("vpmRom", vpmRom);

			// build the full folder path
			TSTRING vpmRomDir;
			if (VPinMAMEIfc::GetRomDir(vpmRomDir))
			{
				// build the full name: path\rom.zip
				TCHAR romfile[MAX_PATH];
				PathCombine(romfile, vpmRomDir.c_str(), vpmRom.c_str());
				_tcscat_s(romfile, _T(".zip"));

				// if the file exists, add it to the results
				if (FileExists(romfile))
					obj.Set("vpmRomPath", romfile);
			}
		}

		// set the DOF ROM name, if availalbe
		if (dofRom != nullptr && dofRom[0] != 0)
			obj.Set("dofRom", dofRom);

		// set the NVRAM file, if available
		if (hasNv)
		{
			TCHAR nv[MAX_PATH];
			PathCombine(nv, nvramPath.c_str(), nvramFile.c_str());
			obj.Set("nvramPath", nv);
		}

		// return the object
		return obj.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

// Descriptor value encapsulation for JsGameInfoUpdate.  This
// captures a property value and whether or not it's present at
// all in the descriptor.
template<typename T> struct GameInfoDescItem
{
	GameInfoDescItem() : isDefined(false) { }
	GameInfoDescItem(JavascriptEngine::JsObj &desc, const CHAR *prop) { From(desc, prop); }

	void From(JavascriptEngine::JsObj &desc, const CHAR *prop)
	{
		if ((this->isDefined = desc.Has(prop)) == true)
			this->value = desc.Get<T>(prop);
	}

	void From(T value)
	{
		this->isDefined = true;
		this->value = value;
	}

	bool isDefined;
	T value;
};

JsValueRef PlayfieldView::JsGameInfoUpdate(JsValueRef self, JsValueRef descval, JsValueRef optsval)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();

	try
	{
		// create an object for the return value
		auto retobj = JavascriptEngine::JsObj::CreateObject();

		// get the game from the ID in self.id
		JavascriptEngine::JsObj selfobj(self);
		auto id = selfobj.Get<int>("id");
		auto game = gl->GetByInternalID(id);
		if (game == nullptr)
			return js->Throw(_T("Invalid game ID"));

		// get the descriptor object
		JavascriptEngine::JsObj desc(descval);

		// Retrieve all of the values before changing anything.  This
		// will avoid leaving the game in an inconsistent state if any
		// of the Javascript data operations throw errors.
#define Prop(T, name) GameInfoDescItem<T> name(desc, #name)

		Prop(JsValueRef, categories);
		Prop(TSTRING, title);
		Prop(TSTRING, rom);
		Prop(int, year);
		Prop(TSTRING, tableType);
		Prop(TSTRING, highScoreStyle);
		Prop(TSTRING, manufacturer);
		Prop(JsValueRef, gridPos);
		Prop(JsValueRef, system);
		Prop(double, rating);
		Prop(bool, isFavorite);
		Prop(bool, isHidden);
		Prop(bool, isMarkedForCapture);
		Prop(DateTime, lastPlayed);
		Prop(DateTime, dateAdded);
		Prop(int, playTime);
		Prop(int, playCount);
		Prop(TSTRING, ipdbId);
		Prop(int, audioVolume);
		Prop(JsValueRef, mediaName);

		// if a mediaName is specified, validate it
		bool useMediaName = false;
		TSTRING mediaNameStr;
		if (mediaName.isDefined && !js->IsUndefinedOrNull(mediaName.value))
		{
			// get the string
			GameInfoDescItem<TSTRING> s(desc, "mediaName");
			mediaNameStr = s.value;
			useMediaName = true;

			// make sure the name pattern is valid
			static const std::basic_regex<TCHAR> invalMediaNamePat(
				_T(".*[\\\\/:<>\"|?*].*|con|prn|aux|nul|com\\d|lpt\\d"), std::regex_constants::icase);
			if (std::regex_match(mediaNameStr, invalMediaNamePat))
				return js->Throw(_T("mediaName must be a valid filename string, with no path portion"));
		}

		// if gridPos is specified, get its components {row, column}
		GameInfoDescItem<int> gridPosRow, gridPosColumn;
		if (gridPos.isDefined)
		{
			JavascriptEngine::JsObj gridPosObj(desc.Get<JsValueRef>("gridPos"));
			if (!gridPosObj.IsNull())
			{
				gridPosRow.From(gridPosObj, "row");
				gridPosColumn.From(gridPosObj, "column");
			}
		}

		// if a category array is present, convert it to C++ string list
		std::list<const GameCategory*> catList;
		if (categories.isDefined)
		{
			JavascriptEngine::JsObj catArr(categories.value);
			int length = catArr.Get<int>("length");
			for (int i = 0; i < length; ++i)
				catList.emplace_back(gl->FindOrCreateCategory(catArr.GetAtIndex<TSTRING>(i).c_str()));
		}

		// if system is specified, get the GameSystem object, and validate
		// the change
		GameInfoDescItem<GameSystem*> psystem;
		if (system.isDefined)
		{
			JavascriptEngine::JsObj sysobj(system.value);
			if (sysobj.IsNull())
			{
				// it's explicitly undefined/null - set to the null system
				psystem.From(nullptr);
			}
			else
			{
				// it's a GameSysInfo object - look it up by config index
				psystem.From(gl->GetSystem(sysobj.Get<int>("index")));

				// validate that we found a system
				if (psystem.value == nullptr)
					return js->Throw(_T("GameSysInfo is invalid"));

				// We can only move it to another system associated with the same table
				// file set containing the game.  We assume in many places that all of
				// a system's table files are in the table folder, and we make this
				// assumption largely because practically everything else in the pin cab
				// ecosystem makes that assumption, particularly the two main freeware
				// systems, VP and FP.  I would have preferred to design PinballY with
				// no such assumption, since I'd much rather organize my own game files
				// in a saner way than dumping everything into one giant flat directory,
				// but that would have been folly because the one system/one table folder
				// assumption is so deeply baked into all of the other programs.  Better
				// to live with the crappy status quo on this one.  But anyway, it means
				// that it would be a bad idea to allow moving a game to a system that's
				// not associated with the game file's folder.
				//
				// If the game didn't come from a table file, it's an XML entry with no
				// installed game backing it up.  Allow moving these between systems,
				// since that just moves the XML entry to a new file.
				if (game->tableFileSet != nullptr)
				{
					// We have a table file set, so this game entry is associated with
					// an extant file.  The new system must share the same table folder
					// to be valid, which means it has to be in the system list for the
					// table file set.
					auto &systems = game->tableFileSet->systems;
					if (std::find(systems.begin(), systems.end(), psystem.value) == systems.end())
						return js->Throw(_T("Game's system cannot be changed to one with a different table folder"));
				}
				else
				{
					// The game has no table file set, so this is an abstract entry that
					// came from an XML file, with no extant game file to back it up.
					// Don't allow moving these records.
					return js->Throw(_T("A game with no associated file cannot be moved to a different system"));
				}
			}
		}

#undef Prop

		// Get the options
		bool renameMediaOption = true;
		JavascriptEngine::JsObj opts(optsval);
		if (!opts.IsNull())
		{
			if (opts.Has(L"renameMedia"))
				renameMediaOption = opts.Get<bool>("renameMedia");
		}

		// do we need to rebuild the XML data?
		bool rebuildDb = false;

		// update the rating
		if (rating.isDefined)
			gl->SetRating(game, static_cast<float>(rating.value));

		// update the favorites flag
		if (isFavorite.isDefined)
			gl->SetIsFavorite(game, isFavorite.value);

		// update the hidden status
		if (isHidden.isDefined)
			gl->SetHidden(game, isHidden.value);

		// update the capture flag
		if (isMarkedForCapture.isDefined)
			gl->MarkForCapture(game, isMarkedForCapture.value);

		// update the added date
		if (dateAdded.isDefined)
			gl->SetDateAdded(game, dateAdded.value);

		// update the last-played date
		if (lastPlayed.isDefined)
			gl->SetLastPlayed(game, lastPlayed.value);

		// update the play count
		if (playCount.isDefined)
			gl->SetPlayCount(game, playCount.value);

		// update the play time
		if (playTime.isDefined)
			gl->SetPlayTime(game, playTime.value);

		// update the audio volume
		if (audioVolume.isDefined)
		{
			// set it in the metadata
			gl->SetAudioVolume(game, audioVolume.value);

			// if this is the current game in the wheel, apply the working volume change
			if (game == gl->GetNthGame(0))
			{
				workingAudioVolume = audioVolume.value;
				ApplyWorkingAudioVolume();
			}
		}

		// update the title, if present
		if (title.isDefined)
		{
			game->title = title.value;
			rebuildDb = true;
		}

		// update the IPDB ID, if present
		if (ipdbId.isDefined)
		{
			game->ipdbId = ipdbId.value;
			rebuildDb = true;
		}
		
		// update the ROM, if present
		if (rom.isDefined)
		{
			game->rom = rom.value;
			rebuildDb = true;
		}
		
		// update the year, if present; if necessary, create an era filter covering the year
		if (year.isDefined)
		{
			gl->FindOrAddDateFilter(game->year = year.value);
			rebuildDb = true;
		}

		// update the table type
		if (tableType.isDefined)
		{
			game->tableType = tableType.value;
			rebuildDb = true;
		}

		// update the high score style
		if (highScoreStyle.isDefined)
			gl->SetHighScoreStyle(game, highScoreStyle.value.c_str());

		// update the manufacturer
		if (manufacturer.isDefined)
		{
			game->manufacturer = gl->FindOrAddManufacturer(manufacturer.value.c_str());
			rebuildDb = true;
		}

		// update the grid position
		if (gridPosRow.isDefined)
		{
			game->gridPos.row = gridPosRow.value;
			rebuildDb = true;
		}
		if (gridPosColumn.isDefined)
		{
			game->gridPos.col = gridPosColumn.value;
			rebuildDb = true;
		}

		// Update the system.  This is the last step for the regular metadata items,
		// because it might require moving the XML database entry between files.
		if (psystem.isDefined)
		{
			gl->ChangeSystem(game, psystem.value);
			rebuildDb = true;
		}

		// Update categories.  This can also require moving between database files,
		// so (like the system) save it for the end.
		if (categories.isDefined)
			gl->SetCategories(game, catList);

		// if a mediaName property was specified, update the media name
		if (useMediaName)
		{
			// Update the name.  If the media name property was specified
			// as an empty string, it has the special meaning of using the
			// default media name format, "Title (Manufacturer Year)".
			// Otherwise use the string exactly as given.
			std::list<std::pair<TSTRING, TSTRING>> mediaRenameList;
			bool nameChanged = game->UpdateMediaName(&mediaRenameList, mediaNameStr.c_str());
			bool renamedFiles = false;
			if (nameChanged && mediaRenameList.size() != 0)
			{
				// The name changed, and there are existing files matching
				// the old media name, so we might need to rename them. 
				// Whether or not we're going to rename them here, return
				// the rename list in the results object.
				try
				{
					auto renameArr = JavascriptEngine::JsObj::CreateArray();
					for (auto &m : mediaRenameList)
					{
						auto ele = JavascriptEngine::JsObj::CreateObject();
						ele.Set("oldName", m.first);
						ele.Set("newName", m.second);
						renameArr.Push(ele.jsobj);
					}
					retobj.Set("renamedMediaFiles", renameArr.jsobj);
				}
				catch (JavascriptEngine::CallException exc)
				{
					exc.Log(_T("GameInfo.update()"));
				}

				// if desired, rename the files here
				if (renameMediaOption)
				{
					try
					{
						renamedFiles = true;
						JsRenameMediaHelper(game, mediaRenameList, retobj);
					}
					catch (JavascriptEngine::CallException exc)
					{
						exc.Log(_T("GameInfo.update()"));
					}
				}
			}

			// If the media name changed, but we didn't rename files, and
			// this game is current, clear the current playing media to
			// ensure that we immediately reflect the change in the UI.
			if (nameChanged && !renamedFiles && game == gl->GetNthGame(0))
				Application::Get()->ClearMedia();
		}

		// Apply the changes to the game database if necessary
		if (rebuildDb)
		{
			// apply the XML changes
			ApplyGameChangesToDatabase(game);
		}

		// Refresh the filter and update everything in the UI that could be affected.
		// Do this on a zero-delay timer, so that we only do the refresh once even if
		// multiple games get updated in this script.
		SetTimer(hWnd, fullRefreshTimerID, 0, NULL);

		// return the results object
		return retobj.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

void PlayfieldView::JsGameInfoErase(JsValueRef self)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();

	try
	{
		// get the game from the ID in self.id
		JavascriptEngine::JsObj selfobj(self);
		auto id = selfobj.Get<int>("id");
		auto game = gl->GetByInternalID(id);
		if (game == nullptr)
			return js->Throw(_T("Invalid game ID")), (void)0;

		// delete the game's XML entry
		gl->DeleteXml(game);
	
		// do a full UI refresh, in case this affects the current game display
		// or filter selection
		SetTimer(hWnd, fullRefreshTimerID, 0, NULL);
	}
	catch (JavascriptEngine::CallException exc)
	{
		js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

JsValueRef PlayfieldView::JsGameInfoRenameMediaFiles(JsValueRef self, JsValueRef renameArrayVal)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();
	try
	{
		// get the game from the ID in self.id
		JavascriptEngine::JsObj selfobj(self);
		auto id = selfobj.Get<int>("id");
		auto game = gl->GetByInternalID(id);
		if (game == nullptr)
			return js->Throw(_T("Invalid game ID"));

		// get the renamed items array
		JavascriptEngine::JsObj renameArray(renameArrayVal);

		// convert it to a list of TSTRING,TSTRING pairs
		std::list<std::pair<TSTRING, TSTRING>> renameList;
		for (int i = 0, length = renameArray.Get<int>("length"); i < length; ++i)
		{
			auto ele = renameArray.GetAtIndex<JavascriptEngine::JsObj>(i);
			renameList.emplace_back(ele.Get<TSTRING>("oldName"), ele.Get<TSTRING>("newName"));
		}

		// create an object for the results
		auto retobj = JavascriptEngine::JsObj::CreateObject();

		// do the renaming
		JsRenameMediaHelper(game, renameList, retobj);

		// return the results object
		return retobj.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

void PlayfieldView::JsRenameMediaHelper(
	GameListItem *game,
	const std::list<std::pair<TSTRING, TSTRING>> &renameList, 
	JavascriptEngine::JsObj &retobj)
{
	CapturingErrorHandler ceh;
	if (!ApplyGameChangesRenameMediaFiles(game, renameList, ceh))
	{
		// renaming errors occurred - report them in the results object
		auto errorArr = JavascriptEngine::JsObj::CreateArray();
		ceh.EnumErrors([&errorArr](const ErrorList::Item &err)
		{
			auto ele = JavascriptEngine::JsObj::CreateObject();
			ele.Set("message", err.message);
			if (err.details.length() != 0)
				ele.Set("details", err.details);
			errorArr.Push(ele.jsobj);
		});

		// set it in the results object
		retobj.Set("mediaRenameErrors", errorArr.jsobj);
	}
}

JsValueRef PlayfieldView::JsGetAllCategories()
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();
	try
	{
		// create a Javascript array for the category list
		auto arr = JavascriptEngine::JsObj::CreateArray();

		// get the category list
		for (auto cat : gl->GetAllCategories())
			arr.Push(cat->name);

		// return the array object
		return arr.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

void PlayfieldView::JsCreateCategory(WSTRING name)
{
	GameList::Get()->NewCategory(name.c_str());
}

void PlayfieldView::JsRenameCategory(WSTRING oldName, WSTRING newName)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();
	try
	{
		// make sure the new name isn't already in use
		if (gl->CategoryExists(newName.c_str()))
			return js->Throw(_T("New category name already exists")), (void)0;

		// get the existing category under its old name
		auto cat = gl->GetCategoryByName(oldName.c_str());
		if (cat == nullptr)
			return js->Throw(_T("Category doesn't exist")), (void)0;

		// rename it
		gl->RenameCategory(cat, newName.c_str());
	}
	catch (JavascriptEngine::CallException exc)
	{
		js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

void PlayfieldView::JsDeleteCategory(WSTRING name)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();
	try
	{
		// find the category to be deleted
		auto cat = gl->GetCategoryByName(name.c_str());
		if (cat == nullptr)
			return js->Throw(_T("Category doesn't exist")), (void)0;

		// delete it
		gl->DeleteCategory(cat);
	}
	catch (JavascriptEngine::CallException exc)
	{
		js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}


JsValueRef PlayfieldView::JsGetCurFilter()
{
	auto js = JavascriptEngine::Get();
	try
	{
		return BuildFilterInfo(GameList::Get()->GetCurFilter()->GetFilterId());
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

JsValueRef PlayfieldView::JsGetAllFilters()
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();
	try
	{
		// create the result array
		auto arr = JavascriptEngine::JsObj::CreateArray();

		// populate it with FilterInfo objects for all of the filters
		for (auto f : gl->GetFilters())
			arr.Push(BuildFilterInfo(f->GetFilterId()));

		// return the array
		return arr.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

JsValueRef PlayfieldView::JsGetFilterInfo(WSTRING id)
{
	auto js = JavascriptEngine::Get();
	try
	{
		return BuildFilterInfo(id);
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

JsValueRef PlayfieldView::BuildFilterInfo(const WSTRING &id)
{
	return BuildFilterInfo(GameList::Get()->GetFilterById(id.c_str()));
}

JsValueRef PlayfieldView::BuildFilterInfo(GameListFilter *filter)
{
	auto gl = GameList::Get();
	if (filter != nullptr)
	{
		// create a FilterInfo object
		auto obj = JavascriptEngine::JsObj::CreateObjectWithPrototype(jsFilterInfo);

		// populate the filter-specific properties
		obj.Set("id", filter->GetFilterId());
		obj.Set("title", filter->GetFilterTitle());
		obj.Set("cmd", filter->cmd);
		if (filter->menuGroup.length() != 0)
		{
			obj.Set("group", filter->menuGroup);
			if (auto groupCmd = gl->GetFilterGroupCommand(filter->menuGroup.c_str()); groupCmd != 0)
				obj.Set("groupCmd", groupCmd);
		}

		// return the filter descriptor
		return obj.jsobj;
	}
	else
	{
		// No filter - return null.  There should always be a filter, so
		// this really should be impossible, but just in case.
		return JavascriptEngine::Get()->GetNullVal();
	}

}

void PlayfieldView::JsSetCurFilter(WSTRING id)
{
	// Try looking up the filter.  Try first by name; if that fails,
	// and the ID can be parsed as a number in the filter command range, 
	// look it up by command ID.
	auto gl = GameList::Get();
	GameListFilter *filter;
	int cmd;
	if ((filter = gl->GetFilterById(id.c_str())) != nullptr
		|| ((cmd = _ttoi(id.c_str())) >= ID_FILTER_FIRST && cmd <= ID_FILTER_LAST && (filter = gl->GetFilterByCommand(cmd)) != nullptr))
	{
		// set the filter
		GameList::Get()->SetFilter(filter);

		// update the selection, in case the current game is filtered out
		// by the new filter
		UpdateSelection();

		// update the status text, since it might mention the filter name
		UpdateAllStatusText();
	}
}

void PlayfieldView::JsRefreshFilter()
{
	// refresh the current filter
	GameList::Get()->RefreshFilter();

	// update the selection and status text
	UpdateSelection();
	UpdateAllStatusText();
}

int PlayfieldView::JsCreateFilter(JavascriptEngine::JsObj desc)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();
	try
	{
		// get the filter properties from the descriptor
		auto id = desc.Get<WSTRING>("id");
		auto title = desc.Get<WSTRING>("title");
		auto menuTitle = desc.Get<WSTRING>("menuTitle");
		auto group = desc.Get<WSTRING>("group");
		auto select = desc.Get<JsValueRef>("select");
		bool includeHidden = desc.Get<bool>("includeHidden");
		bool includeUnconfig = desc.Get<bool>("includeUnconfig");
		auto sortKey = desc.Get<WSTRING>("sortKey");
		auto before = desc.Get<JsValueRef>("before");
		auto after = desc.Get<JsValueRef>("after");
		
		// use the title as the default sort key and menu title
		if (sortKey.length() == 0)
			sortKey = title;
		if (menuTitle.length() == 0)
			menuTitle = title;

		// if there's an existing filter with the same ID, delete it
		if (auto it = javascriptFilters.find(id); it != javascriptFilters.end())
		{
			// it already exists - delete the live filter in the game list
			gl->DeleteUserDefinedFilter(&it->second);

			// delete it from our map
			javascriptFilters.erase(it);
		}

		// if before/after are undefined or null, store as JS_INVALID_REFERENCE
		if (js->IsFalsy(before))
			before = JS_INVALID_REFERENCE;
		if (js->IsFalsy(after))
			after = JS_INVALID_REFERENCE;

		// add it to our map 
		JavascriptFilter *filter = &javascriptFilters.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(id),
			std::forward_as_tuple(select, id, title, menuTitle, group, sortKey, includeHidden, includeUnconfig, before, after)).first->second;

		// create the live filter
		if (gl->AddUserDefinedFilter(filter))
		{
			// This is the filter that was saved in the configuration that we
			// recently loaded.  Make it current.
			PostMessage(WM_COMMAND, filter->cmd);
		}

		// return the new filter's command ID
		return filter->cmd;
	}
	catch (JavascriptEngine::CallException exc)
	{
		js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
		return 0;
	}
}

void PlayfieldView::JavascriptFilter::BeforeScan()
{
	if (beforeScanFunc != JS_INVALID_REFERENCE)
	{
		try
		{
			JsValueRef argv[] = { JavascriptEngine::Get()->GetGlobalObject() }, result;
			JsCallFunction(beforeScanFunc, argv, static_cast<unsigned short>(countof(argv)), &result);
		}
		catch (JavascriptEngine::CallException exc)
		{
			exc.Log(_T("User-defined filter before()"));
		}
	}
}

void PlayfieldView::JavascriptFilter::AfterScan()
{
	if (afterScanFunc != JS_INVALID_REFERENCE)
	{
		try
		{
			JsValueRef argv[] = { JavascriptEngine::Get()->GetGlobalObject() }, result;
			JsCallFunction(afterScanFunc, argv, static_cast<unsigned short>(countof(argv)), &result);
		}
		catch (JavascriptEngine::CallException exc)
		{
			exc.Log(_T("User-defined filter after()"));
		}
	}
}

bool PlayfieldView::JavascriptFilter::Include(GameListItem *game)
{
	auto js = JavascriptEngine::Get();
	try
	{
		// get the javascript version of the game info object
		auto pfv = Application::Get()->GetPlayfieldView();
		JsValueRef jsgame = pfv->BuildJsGameInfo(game);

		// call the callback function
		JsValueRef argv[] = { js->GetGlobalObject(), jsgame }, result, boolval;
		bool b;
		if (JsCallFunction(func, argv, static_cast<unsigned short>(countof(argv)), &result) == JsNoError
			&& JsConvertValueToBoolean(result, &boolval) == JsNoError
			&& JsBooleanToBool(boolval, &b) == JsNoError)
			return b;

		return false;
	}
	catch (JavascriptEngine::CallException exc)
	{
		// on error, simply filter out the game
		exc.Log(_T("User-defined filter select()"));
		return false;
	}
}

JsValueRef PlayfieldView::JsFilterInfoGetGames(JsValueRef self)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();
	try
	{
		// get the filter ID from self
		JavascriptEngine::JsObj selfobj(self);
		auto id = selfobj.Get<WSTRING>("id");

		// look up the filter
		auto filter = gl->GetFilterById(id.c_str());
		if (filter == nullptr)
			return js->GetNullVal();

		// create an array for the results
		auto arr = JavascriptEngine::JsObj::CreateArray();

		// populate it with games passing the filter
		gl->EnumGames([this, &arr](GameListItem *game) { arr.Push(BuildJsGameInfo(game)); }, filter);

		// return the array
		return arr.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

bool PlayfieldView::JsFilterInfoTestGame(JsValueRef self, JsValueRef game)
{
	auto js = JavascriptEngine::Get();
	auto gl = GameList::Get();
	try
	{
		// get the filter ID from self
		JavascriptEngine::JsObj selfobj(self);
		auto id = selfobj.Get<WSTRING>("id");

		// look up the filter
		auto filter = gl->GetFilterById(id.c_str());
		if (filter == nullptr)
			return js->GetNullVal();

		// get the game
		JavascriptEngine::JsObj gameobj(game);
		auto game = gl->GetByInternalID(gameobj.Get<int>("id"));
		if (game == nullptr)
			return js->GetNullVal();

		// test the game
		return gl->FilterIncludes(filter, game) ? js->GetTrueVal() : js->GetFalseVal();
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

int PlayfieldView::JsCreateMetaFilter(JavascriptEngine::JsObj desc)
{
	auto js = JavascriptEngine::Get();
	try
	{
		// create the new filter object from the descriptor
		auto &mf = javascriptMetaFilters.emplace_back(new JavascriptMetafilter(
			desc.Get<JsValueRef>("before"),
			desc.Get<JsValueRef>("select"),
			desc.Get<JsValueRef>("after"),
			desc.Get<int>("priority"),
			desc.Get<bool>("includeExcluded")));

		// assign an ID 
		mf->id = nextMetaFilterId++;

		// add it to the active metafilter list in the game list
		auto gl = GameList::Get();
		gl->AddMetaFilter(mf.get());

		// update the selection and status text for the filter change
		gl->RefreshFilter();
		UpdateSelection();
		UpdateAllStatusText();

		// return the new filter ID
		return mf->id;
	}
	catch (JavascriptEngine::CallException exc)
	{
		js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
		return 0;
	}
}

void PlayfieldView::JsRemoveMetaFilter(int id)
{
	// find the filter in the list
	for (auto &it : javascriptMetaFilters)
	{
		if (it->id == id)
		{
			// got it - remove it from the active metafilter list
			auto gl = GameList::Get();
			gl->RemoveMetaFilter(it.get());

			// delete it from our storage list
			javascriptMetaFilters.remove(it);

			// update the selection and status text for the filter change
			gl->RefreshFilter();
			UpdateSelection();
			UpdateAllStatusText();
			
			// stop searching
			break;
		}
	}
}

void PlayfieldView::JavascriptMetafilter::Before()
{
	auto js = JavascriptEngine::Get();
	try
	{
		if (!js->IsUndefinedOrNull(before))
			js->CallFunc<void>(before);
	}
	catch (JavascriptEngine::CallException exc)
	{
		exc.Log(_T("User-defined metafilter before()"));
	}
}

void PlayfieldView::JavascriptMetafilter::After()
{
	auto js = JavascriptEngine::Get();
	try
	{
		if (!js->IsUndefinedOrNull(after))
			js->CallFunc<void>(after);
	}
	catch (JavascriptEngine::CallException exc)
	{
		exc.Log(_T("User-defined metafilter after()"));
	}
}

bool PlayfieldView::JavascriptMetafilter::Include(GameListItem *game, bool include)
{
	auto js = JavascriptEngine::Get();
	auto pfv = Application::Get()->GetPlayfieldView();
	try
	{
		return js->CallFunc<bool>(select, pfv->BuildJsGameInfo(game), include);
	}
	catch (JavascriptEngine::CallException exc)
	{
		exc.Log(_T("User-defined metafilter select()"));
		return false;
	}
}

void PlayfieldView::OnAppActivationChange(bool foreground)
{
	// kill any keyboard/joystick auto-repeat action whenever we 
	// switch modes
	StopAutoRepeat();

	// turn off all DOF keyboard effects if switching to the background
	if (!foreground)
		dof.KeyEffectsOff();

	// reset attract mode
	attractMode.Reset(this);

	// if we're in running game mode, show/hide the pause menu
	if (runningGameMode == RunningGameMode::Running)
	{
		if (foreground)
		{
			// We're activating.  If the game is still running, show the
			// pause menu.  Skip this if the game has already exited: Windows
			// will usually activate us by default (as the next application in
			// the desktop Z order) when the game exits, and this might occur
			// before our monitor thread wakes up and detects that the child
			// has exited.  If the child is in fact no longer running, there's
			// no need for the pause menu, as we're just returning to normal
			// operation.
			if (Application::Get()->IsGameRunning())
				ShowPauseMenu(false);
		}
		else
		{
			// We're deactivating - remove any active UI elements
			CloseMenusAndPopups();
		}
	}
}

bool PlayfieldView::OnCreate(CREATESTRUCT *cs)
{
	// inherit the default handling
	bool ret = __super::OnCreate(cs);

	// Set a timer to do some extra initialization in a moment, to
	// allow the UI to stabilize.
	SetTimer(hWnd, startupTimerID, 1000, 0);

	// return the inherited result
	return ret;
}

// Idle event handler.  We use this to detect when the window is
// ready at program startup.
void PlayfieldView::OnIdleEvent()
{
	// We only want a one-shot idle notification, for when the event
	// queue becomes idle after our initial application startup is
	// finished.  Now that we've received the one notification, remove
	// our idle event subscription.
	D3DView::UnsubscribeIdleEvents(this);

	// check for startup videos
	if (Application::Get()->LoadStartupVideos())
	{
		// note that a startup video is playing
		startupVideoPlaying = true;
	}
	else
	{
		// No startup videos found.  Go to the normal wheel UI.
		ShowInitialUI(true);
	}

	// set the DOF context to startup video mode
	dof.SetUIContext(L"PBYStartupVideo");

	// Hide the cursor while playing the startup videos
	Application::HideCursor();

	// check for a startup audio track
	TCHAR startupAudio[MAX_PATH];
	auto gl = GameList::Get();
	if (gl != nullptr && gl->FindGlobalAudioFile(startupAudio, _T("Startup Sounds"), _T("Startup Audio")))
	{
		// create a player and load the audio track
		LogFileErrorHandler eh(_T("Startup audio: "));
		RefPtr<AudioVideoPlayer> player(new DShowAudioPlayer(hWnd));
		if (player->Open(startupAudio, eh))
		{
			// set the initial volume to the video volume level
			int vol = Application::Get()->GetVideoVolume();
			player->SetVolume(vol);

			// start playback
			if (player->Play(eh))
			{
				// success - add the player to the active audio playback list,
				// which will let us automatically clean up it when it finishes
				// playback
				DWORD cookie = player->GetCookie();
				activeAudio.emplace(std::piecewise_construct,
					std::forward_as_tuple(cookie),
					std::forward_as_tuple(player.Detach(), ActiveAudio::StartupAudio, vol));
			}
		}
	}
}

bool PlayfieldView::CancelStartupVideo()
{
	// if the startup video isn't playing, there's nothing to do
	if (!startupVideoPlaying)
		return false;

	// start the fade-out timer
	SetTimer(hWnd, startupVideoFadeTimerID, 20, NULL);

	// indicate that a startup video was playing and is being canceled
	return true;
}

void PlayfieldView::UpdateStartupVideoFade()
{
	// fade the video in all windows
	auto Fade = [](BaseView *view)
	{
		if (view != nullptr)
			view->FadeStartupVideo(.1f);
	};
	auto app = Application::Get();
	Fade(this);
	Fade(app->GetBackglassView());
	Fade(app->GetDMDView());
	Fade(app->GetTopperView());
	Fade(app->GetInstCardView());
	if (realDMD != nullptr)
		realDMD->FadeStartupVideo(.1f);
}

void PlayfieldView::OnEndExtStartupVideo()
{
	// The startup video in one of the windows has finished.
	// Check to see if they're *all* finished.
	auto IsDone = [](BaseView *view)
	{
		return view == nullptr || !view->IsStartupVideoPlaying();
	};
	auto app = Application::Get();
	if (IsDone(this)
		&& IsDone(app->GetBackglassView())
		&& IsDone(app->GetDMDView())
		&& IsDone(app->GetTopperView())
		&& IsDone(app->GetInstCardView())
		&& (realDMD == nullptr || !realDMD->IsStartupVideoPlaying()))
	{
		// All startup videos are done - flag it.
		startupVideoPlaying = false;

		// kill any fade timer
		KillTimer(hWnd, startupVideoFadeTimerID);
		
		// Show the initial wheel UI.  Skip the about box splash, since the
		// intro video serves the purpose of the transition into the UI.
		ShowInitialUI(false);
	}
}

void PlayfieldView::ShowInitialUI(bool showAboutBox)
{
	// initialize the status lines
	InitStatusLines();

	// load the initial selection
	UpdateSelection();

	// Hide the cursor initially.  This makes the display a little 
	// cleaner and more video-game like.  The mouse cursor isn't
	// normally needed, since the main UI is designed to be operated
	// entirely through the keyboard.  However, we don't want to
	// make the mouse impossible to find, so we don't actually use
	// an empty mouse cursor for the window background.  Hiding it
	// now will take it off the screen until the user moves it, at
	// which point it will revert to the normal arrow cursor, which
	// makes it easy for the user to spot the cursor location if
	// necessary.  We'll keep hiding the cursor again any time a
	// keyboard key is pressed.
	Application::HideCursor();

	// start the status line timer
	SetTimer(hWnd, statusLineTimerID, statusLineTimerInterval, 0);

	// set the attract mode timer
	SetTimer(hWnd, attractModeTimerID, attractModeTimerInterval, 0);

	// start the cleanup timer
	SetTimer(hWnd, cleanupTimerID, 1000, 0);

	// Show the about box for a few seconds, as a sort of splash screen.
	// Skip this if an error message is showing, or if the caller didn't
	// want to show the about box, or if the initial about box is disabled
	// in the config settings.
	if (showAboutBox
		&& popupType != PopupErrorMessage
		&& ConfigManager::GetInstance()->GetBool(ConfigVars::SplashScreen, true))
	{
		ShowAboutBox();
		SetTimer(hWnd, endSplashTimerID, 5000, 0);
	}

	// set the DOF context to "wheel" mode
	dof.SetUIContext(L"PBYWheel");
}

void PlayfieldView::InitStatusLines()
{
	// get the enabled/disabled setting
	statusLineEnabled = ConfigManager::GetInstance()->GetBool(ConfigVars::StatusLineEnable, true);

	// initialize the status lines from the config
	upperStatus.Init(this, 75, 0, 6, _T("UpperStatus"), IDS_DEFAULT_STATUS_UPPER);
	lowerStatus.Init(this, 0, 0, 6, _T("LowerStatus"), IDS_DEFAULT_STATUS_LOWER);
	attractModeStatus.Init(this, 32, 0, 6, _T("AttractMode.StatusLine"), IDS_DEFAULT_STATUS_ATTRACTMODE);

	// reset the drawing list, as the sprites might have changed
	UpdateDrawingList();
}

bool PlayfieldView::OnTimer(WPARAM timer, LPARAM callback)
{
	switch (timer)
	{
	case startupTimerID:
		// done with the startup timer
		KillTimer(hWnd, timer);
		return true;

	case animTimerID:
		// animation update timer
		UpdateAnimation();
		return true;

	case audioFadeoutTimerID:
		UpdateAudioFadeout();
		return true;

	case startupVideoFadeTimerID:
		UpdateStartupVideoFade();
		return true;

	case pfTimerID:
		// Playfield switch timer.  This is a one-shot, so cancel repeats.
		KillTimer(hWnd, pfTimerID);

		// sync the playfield to the game list selection
		SyncPlayfield(SyncByTimer);
		return true;

	case infoBoxFadeTimerID:
		// Info box fade timer - update the fade animation
		UpdateInfoBoxAnimation();
		return true;

	case infoBoxSyncTimerID:
		// Info box sync timer 
		SyncInfoBox();
		return true;

	case statusLineTimerID:
		// Status line timer
		upperStatus.TimerUpdate(this);
		lowerStatus.TimerUpdate(this);
		return true;

	case attractModeStatusLineTimerID:
		// Attract mode status line timer
		attractModeStatus.TimerUpdate(this);
		return true;

	case killGameTimerID:
		// Remove the running game overlay
		EndRunningGameMode();

		// this is a one-shot timer
		KillTimer(hWnd, killGameTimerID);
		return true;

	case jsRepeatTimerID:
		// process a joystick button auto-repeat
		OnJsAutoRepeatTimer();
		return true;

	case kbRepeatTimerID:
		OnKbAutoRepeatTimer();
		return true;

	case attractModeTimerID:
		attractMode.OnTimer(this);
		return true;

	case dofPulseTimerID:
		OnDOFTimer();
		return true;

	case creditsDispTimerID:
		OnCreditsDispTimer();
		return true;

	case gameTimeoutTimerID:
		OnGameTimeout();
		return true;

	case endSplashTimerID:
		// if the splash screen is showing, remove it
		if (popupType == PopupAboutBox)
			ClosePopup();

		// this is a one-shot
		KillTimer(hWnd, timer);
		return true;

	case restoreDOFAndDMDTimerID:
		// If DOF is enabled, start reinitializing the DOF client.  This fires 
		// off a background thread, so DOF won't be ready immediately.
		if (ConfigManager::GetInstance()->GetBool(ConfigVars::DOFEnable, true))
			DOFClient::Init();

		// start polling for when DOF is ready
		SetTimer(hWnd, dofReadyTimerID, 250, NULL);

		// reinstate the real DMD
		InitRealDMD(SilentErrorHandler());
		if (realDMD != nullptr)
			realDMD->EndRunningGameMode();

		// this is a one-shot
		KillTimer(hWnd, timer);
		return true;

	case dofReadyTimerID:
		// Check if DOF has finished initializing
		if (DOFClient::IsReady())
		{
			// show any DOF client errors
			ShowDOFClientInitErrors();

			// signal end of game
			QueueDOFPulse(L"PBYEndGame");

			// send any deferred context settings to DOF
			dof.OnDOFReady();

			// sync the current game context
			dof.SyncSelectedGame();

			// we're done waiting for DOF startup
			KillTimer(hWnd, timer);
		}
		return true;

	case cleanupTimerID:
		// process pending audio/video player deletions
		AudioVideoPlayer::ProcessDeletionQueue();
		return true;
		
	case mediaDropTimerID:
		// continue media drop processing
		KillTimer(hWnd, timer);
		MediaDropGo();
		return true;

	case autoDismissMsgTimerID:
		// if there's a message popup, remove it
		KillTimer(hWnd, timer);
		if (popupType == PopupErrorMessage)
			ClosePopup();
		return true;

	case batchCaptureCancelTimerID:
		// clear the pending cancellation
		KillTimer(hWnd, timer);
		batchCaptureMode.cancelPending = false;
		Application::Get()->BatchCaptureCancelPrompt(false);
		return true;

	case javascriptTimerID:
		// process javascript events
		if (auto js = JavascriptEngine::Get(); js != nullptr)
			js->RunTasks();
		return true;

	case fullRefreshTimerID:
		// Do a full UI update:  redo the filter, update the wheel selection,
		// update the status text, and update the info box.
		GameList::Get()->RefreshFilter();
		UpdateSelection();
		UpdateAllStatusText();
		infoBox.game = nullptr;

		// this is a one-shot timer
		KillTimer(hWnd, timer);
		break;

	case overlayFadeoutTimerID:
		// remove the video overlay
		videoOverlay = nullptr;
		UpdateDrawingList();

		// this is a one-shot
		KillTimer(hWnd, timer);
		break;
	}

	// use the default handling
	return __super::OnTimer(timer, callback);
}

bool PlayfieldView::OnCommand(int cmd, int source, HWND hwndControl)
{
	// some commands are more of the nature of internal events; these bypass javascript
	switch (cmd)
	{
	case ID_SYNC_BACKGLASS:
		// sync the backglass, and do a deferred sync on the DMD
		if (auto bg = Application::Get()->GetBackglassView(); bg != nullptr)
			bg->SyncCurrentGame();
		return true;

	case ID_SYNC_DMD:
		// sync the simulated DMD window
		if (auto dmd = Application::Get()->GetDMDView(); dmd != nullptr)
			dmd->SyncCurrentGame();

		// while we're at it, sync the real DMD, if we're using one
		if (realDMD != nullptr)
			realDMD->UpdateGame();

		return true;

	case ID_SYNC_TOPPER:
		// sync the topper window
		if (auto topper = Application::Get()->GetTopperView(); topper != nullptr)
			topper->SyncCurrentGame();
		return true;

	case ID_SYNC_INSTCARD:
		// sync the instruction card window
		if (auto inst = Application::Get()->GetInstCardView(); inst != nullptr)
			inst->SyncCurrentGame();
		return true;

	case ID_APPROVE_ELEVATION:
		// The user has approved launching the current game in Admin mode.
		// Make sure we have a game to launch...
		if (auto game = GameList::Get()->GetNthGame(0); game != nullptr)
		{
			// Find the system we're going to run the game with.  If
			// the game has a configured system, we'll always use that.
			// Otherwise, it must be an unconfigured file, so the system
			// comes from the matching systems for its database file.
			// There can be multiple of these, but since we already
			// tried to launch the game, we know it's the one at the
			// game's "recent system index" in the list.
			GameSystem *system = game->system;
			if (system == nullptr && game->tableFileSet != nullptr)
			{
				// find the system in the list
				int n = 0;
				for (auto s : game->tableFileSet->systems)
				{
					if (n++ == game->recentSystemIndex)
					{
						system = s;
						break;
					}
				}
			}

			// only proceed if we found a system
			if (system != nullptr)
			{
				// Elevation approval is only required once per system per
				// session, so this implicitly approves future launches on
				// this same system.
				system->elevationApproved = true;

				// Try launching the game again.  Use the same command and the
				// same system index as the original launch attempt (the most
				// recent launch attempt), since elevation approval is always
				// a follow-on step to a launch command.
				PlayGame(lastPlayGameCmd, lastPlayGameLaunchFlags, game->recentSystemIndex);
			}
		}
		return true;
	}

	// Fire the Javascript command event.  If it blocks the system action,
	// stop now and return "handled".
	if (!FireCommandEvent(cmd))
		return true;

	// carry out the command
	return OnCommandImpl(cmd, source, hwndControl);
}

bool PlayfieldView::JsDoCommand(int cmd)
{
	return OnCommandImpl(cmd, 0, NULL);
}

void PlayfieldView::JsDoButtonCommand(WSTRING cmd, bool down, bool repeat)
{
	// look up the command
	if (auto it = commandsByName.find(cmd); it != commandsByName.end())
	{
		// get the foreground/background status
		bool bg = !Application::Get()->IsInForeground();

		// figure the key mode
		KeyPressType mode = (bg ?
			(down ? repeat ? KeyBgRepeat : KeyBgDown : KeyUp) :
			(down ? repeat ? KeyRepeat : KeyDown : KeyUp));

		// make a single-element list with the command
		std::list<const KeyCommand*> commands;
		commands.emplace_back(&it->second);

		// queue the key
		ProcessKeyPress(hWnd, mode, bg, true, commands);
	}
}

bool PlayfieldView::OnCommandImpl(int cmd, int source, HWND hwndControl)
{
	switch (cmd)
	{
	case ID_SHOW_MAIN_MENU:
		ShowMainMenu();
		return true;

	case ID_SHOW_EXIT_MENU:
		ShowExitMenu();
		return true;
		
	case ID_PLAY_GAME:
		PlayGame(cmd, Application::LaunchFlags::StdPlayFlags);
		return true;

	case ID_BATCH_CAPTURE_NEXT_GAME:
		BatchCaptureNextGame();
		return true;

	case ID_FLYER:
		ShowFlyer();
		return true;

	case ID_GAMEINFO:
		ShowGameInfo();
		return true;

	case ID_HIGH_SCORES:
		ShowHighScores();
		return true;

	case ID_INSTRUCTIONS:
		ShowInstructionCard();
		return true;

	case ID_ABOUT:
		ShowAboutBox();
		return true;

	case ID_HELP:
		ShowHelp();
		return true;

	case ID_EXIT:
		::SendMessage(GetParent(hWnd), WM_CLOSE, 0, 0);
		return true;

	case ID_SHUTDOWN:
		AskPowerOff();
		return true;

	case ID_SHUTDOWN_CONFIRM:
		PowerOff();
		return true;

	case ID_MUTE_VIDEOS:
		Application::Get()->ToggleMuteVideos();
		return true;

	case ID_MUTE_TABLE_AUDIO:
		Application::Get()->ToggleMuteTableAudio();
		return true;

	case ID_MUTE_BUTTONS:
		muteButtons = !muteButtons;
		ConfigManager::GetInstance()->SetBool(ConfigVars::MuteButtons, muteButtons);
		return true;

	case ID_MUTE_REPEAT_BUTTONS:
		muteRepeatButtons = !muteRepeatButtons;
		ConfigManager::GetInstance()->SetBool(ConfigVars::MuteRepeatButtons, muteRepeatButtons);
		return true;

	case ID_MUTE_ATTRACTMODE:
		Application::Get()->ToggleMuteAttractMode();
		return true;

	case ID_PINSCAPE_NIGHT_MODE:
		Application::Get()->TogglePinscapeNightMode();
		return true;

	case ID_OPTIONS:
		ShowSettingsDialog();
		return true;

	case ID_KILL_GAME:
		// check if a game is running
		if (Application::Get()->IsGameRunning())
		{
			// If we're in batch mode, require two cancel commands before we
			// actually carry it out, to avoid accidental interruption of
			// what could be a lengthy process.
			if (batchCaptureMode.active)
			{
				// if no cancel is pending, put up the prompt
				if (!batchCaptureMode.cancelPending)
				{
					// mark the cancel as pending
					batchCaptureMode.cancelPending = true;

					// put up the prompt
					Application::Get()->BatchCaptureCancelPrompt(true);

					// set a timer to clear the pending status after a few seconds
					SetTimer(hWnd, batchCaptureCancelTimerID, 3000, NULL);

					// that's it for this command cycle
					return true;
				}

				// mark the batch as cancelled
				batchCaptureMode.cancel = true;
			}

			// switch to the "exiting game" message
			runningGameMode = RunningGameMode::Exiting;
			ShowRunningGameMessage(LoadStringT(IDS_GAME_EXITING));

			// switch to "cancelling" mode
			Application::Get()->ShowCaptureCancel();

			// send the terminate command to the game
			Application::Get()->KillGame();

			// REMOVED: This timer is really meant to be a last resort in case
			// the game monitor thread gets stuck.  Let's try to make sure that
			// can't happen instead - it's cleaner to make sure the thread exits
			// properly.
			// Set a timer to end running game mode if we don't detect the game
			// exiting on its own in a reasonable time.  
			// SetTimer(hWnd, killGameTimerID, 10000, 0);
		}
		else
		{
			// No game is running, so there's no game to exit.  Just in case
			// we're out of sync somehow and the running game popup is showing
			// despite no game running, explicitly exit running game mode in
			// the UI.  That hopefully gives the user a way to get things back
			// in sync if the UI gets stuck in running game mode.
			EndRunningGameMode();
		}
		return true;

	case ID_PAUSE_GAME:
		// ignore this unless a game is running
		if (Application::Get()->IsGameRunning())
		{
			// try to grab focus from the running game
			Application::Get()->StealFocusFromGame();

			// process a Select command
			DoSelect(false);
		}
		return true;

	case ID_RESUME_GAME:
		// immediately complete any menu animation, since we'll freeze
		// screen updates once we're back in the background
		AccelerateCloseMenu();
		InvalidateRect(hWnd, 0, false);

		// bring the game to the foreground
		Application::Get()->ResumeGame();
		return true;

	case ID_REALDMD_AUTO_ENABLE:
		SetRealDMDStatus(RealDMDAuto);
		return true;

	case ID_REALDMD_ENABLE:
		SetRealDMDStatus(RealDMDEnable);
		return true;

	case ID_REALDMD_DISABLE:
		SetRealDMDStatus(RealDMDDisable);
		return true;

	case ID_REALDMD_MIRROR_HORZ:
		if (realDMD != nullptr)
			realDMD->SetMirrorHorz(!realDMD->IsMirrorHorz());
		return true;

	case ID_REALDMD_MIRROR_VERT:
		if (realDMD != nullptr)
			realDMD->SetMirrorVert(!realDMD->IsMirrorVert());
		return true;

	case ID_ADD_FAVORITE:
		if (auto game = GameList::Get()->GetNthGame(0); game != nullptr)
			GameList::Get()->SetIsFavorite(game, true);
		return true;

	case ID_REMOVE_FAVORITE:
		if (auto game = GameList::Get()->GetNthGame(0); 
		    IsGameValid(game) && GameList::Get()->IsFavorite(game))
		{
			// un-set the favorite flag on the game
			GameList *gl = GameList::Get();
			gl->SetIsFavorite(game, false);

			// If we're currently filtering for favorites, this game is now
			// filtered out, so we need rebuild the filter list and update
			// the selection.
			if (gl->GetCurFilter() == gl->GetFavoritesFilter())
			{
				gl->SetFilter(gl->GetFavoritesFilter());
				UpdateSelection();
				UpdateAllStatusText();
			}
		}
		return true;

	case ID_RATE_GAME:
		RateGame();
		return true;

	case ID_FILTER_BY_ERA:
		ShowFilterSubMenu(cmd, _T("[Era]"), L"filter by era");
		return true;

	case ID_FILTER_BY_MANUF:
		ShowFilterSubMenu(cmd, _T("[Manuf]"), L"filter by manuf");
		return true;

	case ID_FILTER_BY_SYS:
		ShowFilterSubMenu(cmd, _T("[Sys]"), L"filter by system");
		return true;

	case ID_FILTER_BY_RATING:
		ShowFilterSubMenu(cmd, _T("[Rating]"), L"filter by rating");
		return true;

	case ID_FILTER_BY_CATEGORY:
		ShowFilterSubMenu(cmd, _T("[Cat]"), L"filter by category");
		return true;

	case ID_FILTER_BY_RECENCY:
		ShowRecencyFilterMenu(_T("[Played]"), _T("[!Played]"), _T("[!!Played]"), L"filter by when played", IDS_PLAYED_WITHIN, IDS_NOT_PLAYED_WITHIN);
		return true;

	case ID_FILTER_BY_ADDED:
		ShowRecencyFilterMenu(_T("[Added]"), _T("[!Added]"), nullptr, L"filter by when added", IDS_ADDED_WITHIN, IDS_NOT_ADDED_WITHIN);
		return true;

	case ID_CLEAR_CREDITS:
		coinBalance = 0.0f;
		SetCredits(0.0f);
		UpdateAllStatusText();
		return true;

	case ID_OPERATOR_MENU:
		ShowOperatorMenu();
		return true;

	case ID_GAME_SETUP:
		ShowGameSetupMenu();
		return true;

	case ID_ADJUST_AUDIO_VOLUME:
		ShowAudioVolumeDialog();
		return true;

	case ID_CAPTURE_MEDIA:
	case ID_BATCH_CAPTURE_STEP1:
		// Start a single/batch capture.  The first step in both processes is
		// the layout information message.
		CaptureLayoutPrompt(cmd, false);
		return true;

	case ID_CAPTURE_LAYOUT_SKIP:
		ConfigManager::GetInstance()->SetBool(ConfigVars::CaptureSkipLayoutMessage,
			!ConfigManager::GetInstance()->GetBool(ConfigVars::CaptureSkipLayoutMessage, false));
		CaptureLayoutPrompt(0, true);
		return true;
		break;

	case ID_CAPTURE_LAYOUT_OK:
		switch (origCaptureCmd)
		{
		case ID_CAPTURE_MEDIA:
			CaptureMediaSetup();
			return true;

		case ID_BATCH_CAPTURE_STEP1:
			BatchCaptureStep1();
			return true;
		}
		break;

	case ID_CAPTURE_GO:
		CaptureMediaGo();
		return true;

	case ID_MARK_FOR_BATCH_CAPTURE:
		if (auto game = GameList::Get()->GetNthGame(0); game != nullptr)
			GameList::Get()->ToggleMarkedForCapture(game);
		return true;

	case ID_BATCH_CAPTURE_ALL:
	case ID_BATCH_CAPTURE_FILTER:
	case ID_BATCH_CAPTURE_MARKED:
		BatchCaptureStep2(cmd);
		return true;

	case ID_BATCH_CAPTURE_STEP3:
		BatchCaptureStep3();
		return true;

	case ID_BATCH_CAPTURE_STEP4:
		BatchCaptureStep4();
		return true;

	case ID_BATCH_CAPTURE_VIEW:
		BatchCaptureView();
		return true;

	case ID_BATCH_CAPTURE_GO:
		BatchCaptureGo();
		return true;

	case ID_EDIT_GAME_INFO:
		EditGameInfo();
		return true;

	case ID_DEL_GAME_INFO:
		DelGameInfo(false);
		return true;

	case ID_CONFIRM_DEL_GAME_INFO:
		DelGameInfo(true);
		return true;

	case ID_SET_CATEGORIES:
		ShowGameCategoriesMenu();
		return true;

	case ID_MENU_PAGE_UP:
		MenuPageUpDown(-1);
		return true;

	case ID_MENU_PAGE_DOWN:
		MenuPageUpDown(1);
		return true;

	case ID_SAVE_CATEGORIES:
		SaveCategoryEdits();
		return true;

	case ID_EDIT_CATEGORIES:
		EditCategories();
		return true;

	case ID_FIND_MEDIA:
		ShowMediaSearchMenu();
		return true;

	case ID_MEDIA_SEARCH_GO:
		LaunchMediaSearch();
		return true;

	case ID_SHOW_MEDIA_FILES:
		showMedia.ResetDialog();
		ShowMediaFiles(0);
		return true;

	case ID_DEL_MEDIA_FILE:
		DelMediaFile();
		return true;

	case ID_HIDE_GAME:
		ToggleHideGame();
		return true;

	case ID_ENABLE_VIDEO_GLOBAL:
		Application::Get()->ToggleEnableVideos();
		return true;

	case ID_RESTART_AS_ADMIN:
		Application::Get()->RestartAsAdmin();
		return true;

	case ID_MEDIA_DROP_PHASE2:
		MediaDropPhase2();
		return true;

	case ID_MEDIA_DROP_GO:
		MediaDropGo();
		return true;

	case ID_CAPTURE_ADJUSTDELAY:
		ShowCaptureDelayDialog(false);
		return true;

	case ID_SWF_ERROR_DISABLE:
		// disable Flash in the config
		ConfigManager::GetInstance()->SetBool(ConfigVars::InstCardEnableFlash, false);

		// save and reload the configuration to effect the change immediately
		ConfigManager::GetInstance()->Save();
		Application::Get()->ReloadConfig();

		// display the confirmation
		ShowError(EIT_Information, LoadStringT(IDS_SWF_DISABLED));

		// done
		return true;


	case ID_SWF_ERROR_SUPPRESS:
		// flag that we should ignore Flash errors for this sessionj
		showFlashErrors = false;
		return true;

	case ID_SWF_ERROR_HELP:
		ShowHelp(_T("SWF"));
		return true;

	default:
		// check for game filters
		if (cmd >= ID_FILTER_FIRST && cmd <= ID_FILTER_LAST)
		{
			// If a game category selection menu is active, a category filter
			// command toggles the checkmark status for the category in the 
			// game's edit list.  We can tell if this is the case by checking 
			// to see if there's an edit list.  Otherwise, a filter menu item 
			// makes that filter current for the wheel UI.
			if (categoryEditList != nullptr)
			{
				// game category editing in progress - toggle the selected
				// category in the edit list
				ToggleCategoryInEditList(cmd);
			}
			else
			{
				// fire the filter change event
				auto gl = GameList::Get();
				if (FireFilterSelectEvent(gl->GetFilterByCommand(cmd)))
				{
					// set the new filter
					GameList::Get()->SetFilter(cmd);

					// refresh the current game selection and wheel images
					UpdateSelection();

					// update status line text
					UpdateAllStatusText();
				}
			}

			// handled
			return true;
		}

		// check for user-defined filter groups
		if (cmd >= ID_USER_FILTER_GROUP_FIRST && cmd < ID_USER_FILTER_GROUP_LAST)
		{
			// Get the group title.  This also serves as the group ID.
			if (auto title = GameList::Get()->GetUserDefinedFilterGroup(cmd); title != nullptr)
				ShowFilterSubMenu(cmd, title, TCHARToWCHAR(title));

			// handled
			return true;
		}

		// check for launch-with-system commands
		if (cmd >= ID_PICKSYS_FIRST && cmd <= ID_PICKSYS_LAST)
		{
			// Launch using the last PlayGame() command.  System selection
			// is always triggered by a launch attempt, so when the user
			// makes the selection, it just retries the same command as
			// last time using the additional information provided by the
			// system selection.
			PlayGame(lastPlayGameCmd, lastPlayGameLaunchFlags, cmd - ID_PICKSYS_FIRST);
			return true;
		}

		// check for capture items
		if (cmd >= ID_CAPTURE_FIRST && cmd <= ID_CAPTURE_LAST)
		{
			AdvanceCaptureItemState(cmd);
			return true;
		}

		// check for media drop items
		if (cmd >= ID_MEDIADROP_FIRST && cmd <= ID_MEDIADROP_LAST)
		{
			InvertMediaDropState(cmd);
			return true;
		}
		break;
	}

	// not handled
    return __super::OnCommand(cmd, source, hwndControl);
}

bool PlayfieldView::HandleSysKeyEvent(BaseWin *win, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// translate the key to an extended key code and check what we have
	switch (KeyInput::TranslateExtKeys(msg, wParam, lParam))
	{
	case VK_LMENU:
		// Left Alt.  If there's a keyboard command assigned to Left Alt,
		// or Alt is used as a mouse modifier key, suppress menu activation.
		if (leftAltHasCommand)
			return HandleKeyEvent(win, msg == WM_SYSKEYDOWN ? WM_KEYDOWN : WM_KEYUP, wParam, lParam);
		else if (altHasMouseCommand)
			return true;
		break;

	case VK_RMENU:
		// Right Alt.  Same deal as with Left Alt.
		if (rightAltHasCommand)
			return HandleKeyEvent(win, msg == WM_SYSKEYDOWN ? WM_KEYDOWN : WM_KEYUP, wParam, lParam);
		else if (altHasMouseCommand)
			return true;
		break;

	case VK_F10:
		// F10.  Skip the menu activation if there's a command mapped.
		if (f10HasCommand)
		{
			// F10 is only sent as a system key event, so explicitly process
			// it as though it's ordinary key event.
			return HandleKeyEvent(win, msg == WM_SYSKEYDOWN ? WM_KEYDOWN : WM_KEYUP, wParam, lParam);
		}
		break;
	}

	// not processed
	return false;
}

bool PlayfieldView::HandleSysCharEvent(BaseWin *win, WPARAM wParam, LPARAM lParam)
{
	// If the Alt keys are claimed for command use, suppress the normal
	// keyboard shortcut behavior.  The test is a bit subtle: if Left Alt
	// is claimed or it's up (unpressed), it shouldn't activate the menu
	// (and thus we should 'return 0' to skip the default handling).  
	// Likewise, if Right Alt is  claimed or up, it shouldn't activate
	// the menu.  The only way to activate the menu is either with the
	// Left Alt or Right Alt, so if *both* of these are either claimed
	// or up, there's nothing to activate the menu and we should return.
	if ((leftAltHasCommand || (GetKeyState(VK_LMENU) & 0x8000) == 0)
		&& (rightAltHasCommand || (GetKeyState(VK_RMENU) & 0x8000) == 0))
		return true;

	// an unclaimed Alt key is down, so allow the standard system menu handling
	return false;
}

bool PlayfieldView::HandleKeyEvent(BaseWin *win, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// update the attract mode key event timer
	attractMode.OnKeyEvent(this);

	// get the original vkey from the message - we need this to test
	// the state in auto-repeat
	int vkeyOrig = (int)wParam;

	// translate the key code
	int vkey = KeyInput::TranslateExtKeys(msg, wParam, lParam);

	// Determine the key press mode according to the Windows message type
	// and flags.  If it's a WM_KEYUP event, it's always KeyUp mode.  If 
	// it's a WM_KEYDOWN event, it could be either a first press (KeyDown)
	// or an auto-repeat event (KeyRepeat).  We can distinguish those by 
	// bit 30 of the lParam.
	KeyPressType mode;
	bool down;
	if (msg == WM_KEYUP)
	{
		// key up event
		mode = KeyUp;
		down = false;

		// stop any auto-repeat in effect
		StopAutoRepeat();
	}
	else if ((lParam & (1 << 30)) != 0)
	{
		// Bit 30 means that the key was already down on the last event,
		// so this is an auto-repeat.  Skip these: we synthesize our own
		// repeat events instead using a timer, to make the handling more
		// consistent across different keyboard types.  Simply treat the
		// event as handled.
		return true;
	}
	else
	{
		// first key-down event for a key press
		mode = KeyDown;
		down = true;

		// start a new auto-repeat timer
		KbAutoRepeatStart(vkey, vkeyOrig, KeyRepeat);
	}

	// fire a javascript key event; if that says to ignore it, we're done
	if (!FireKeyEvent(vkey, down, false, false))
		return false;

	// determine if we have a handler
	if (auto it = vkeyToCommand.find(vkey); it != vkeyToCommand.end())
	{
		// We found a handler for the key.  Process the key press.
		ProcessKeyPress(win->GetHWnd(), mode, false, false, it->second);

		// the key event was handled
		return true;
	}

	// not processed 
	return false;
}

// Add a key press to the queue and process it
void PlayfieldView::ProcessKeyPress(HWND hwndSrc, KeyPressType mode, bool bg, bool scripted, std::list<const KeyCommand*> cmds)
{
	// add each command to the key queue
	for (auto c : cmds)
	{
		// queue the command
		keyQueue.emplace_back(hwndSrc, mode, bg, scripted, c);

		// Immediately process any DOF effects associated with the key
		if (c->func == &PlayfieldView::CmdNext)
			dof.SetKeyEffectState(_T("PBYFlipperRight"), (mode & KeyDown) != 0);
		else if (c->func == &PlayfieldView::CmdPrev)
			dof.SetKeyEffectState(_T("PBYFlipperLeft"), (mode & KeyDown) != 0);
		else if (c->func == &PlayfieldView::CmdNextPage)
			dof.SetKeyEffectState(_T("PBYMagnaRight"), (mode & KeyDown) != 0);
		else if (c->func == &PlayfieldView::CmdPrevPage)
			dof.SetKeyEffectState(_T("PBYMagnaLeft"), (mode & KeyDown) != 0);
	}

	// If a wheel animation is in progress, skip directly to the end
	// of the animation on any new key-down event.  This makes the
	// UI more responsive by not forcing the user to wait through 
	// each wheel animation step.
	if (mode == KeyDown && wheelAnimMode == WheelAnimNormal)
		wheelAnimStartTime = GetTickCount() - wheelTime;

	// process the command queue
	ProcessKeyQueue();
}

void PlayfieldView::ShowHelp(const TCHAR *section)
{
	// Build the full help file name - <install folder>\Help\section.html
	TCHAR helpFile[MAX_PATH];
	GetDeployedFilePath(helpFile, MsgFmt(_T("Help\\%s.html"), section), _T(""));

	// open the help file in a browser window
	ShellExec(helpFile);
}

void PlayfieldView::ShowAboutBox()
{
	// fire an event first, abort on cancel
	const WCHAR *popupName = L"about box";
	if (!FirePopupEvent(true, popupName))
		return;

	// load the About Box background image
    std::unique_ptr<Gdiplus::Bitmap> bkg(GPBitmapFromPNG(IDB_ABOUTBOX));
	
	// create the sprite
	popupSprite.Attach(new Sprite());
    Application::InUiErrorHandler eh;
	bool ok = popupSprite->Load(bkg->GetWidth(), bkg->GetHeight(), [&bkg, this](HDC hdc, HBITMAP bmp)
	{
		// set up GDI+ on the memory DC
		Gdiplus::Graphics g(hdc);

		// draw the background
		if (bkg != nullptr)
            g.DrawImage(bkg.get(), 0, 0);

		// figure the bounding box for the text
		const float margin = 20.0f;
		Gdiplus::RectF bbox(margin, margin, bkg->GetWidth() - 2.0f*margin - 215.0f, bkg->GetHeight() - 2.0f*margin);

		// draw the title
		const TCHAR *title = Application::Get()->Title;
		Gdiplus::SolidBrush br(Gdiplus::Color(0xff, 0x40, 0x40, 0x40));
		std::unique_ptr<Gdiplus::Font> titleFont(CreateGPFont(_T("Segoe UI"), 48, 400));
		Gdiplus::PointF origin(margin, margin);
		GPDrawStringAdv(g, title, titleFont.get(), &br, origin, bbox);

		// add a margin after the title
		origin.Y += 8;

		// draw the version string
		std::unique_ptr<Gdiplus::Font> verFont(CreateGPFont(_T("Segoe UI"), 24, 400));
		std::unique_ptr<Gdiplus::Font> smallFont(CreateGPFont(_T("Segoe UI"), 14, 400));
		GPDrawStringAdv(g, MsgFmt(_T("Version %hs"), G_VersionInfo.fullVerWithStat),
			verFont.get(), &br, origin, bbox);
		GPDrawStringAdv(g, MsgFmt(_T("Build %d (%s, %hs)"),
			G_VersionInfo.buildNo, IF_32_64(_T("x86"), _T("x64")), G_VersionInfo.date),
			smallFont.get(), &br, origin, bbox);

		// add the DOF version if present
		std::unique_ptr<Gdiplus::Font> smallerFont(CreateGPFont(_T("Segoe UI"), 12, 400));
		if (DOFClient *dof = DOFClient::Get(); dof != nullptr && DOFClient::IsReady())
			GPDrawStringAdv(g, MsgFmt(_T("DirectOutput Framework %s"), dof->GetDOFVersion()),
				smallerFont.get(), &br, origin, bbox);

		// add the PINemHi version if present
		if (pinEmHiVersion.length() != 0)
			GPDrawStringAdv(g, MsgFmt(_T("PINemHi version %s by Dna Disturber"), pinEmHiVersion.c_str()),
				smallerFont.get(), &br, origin, bbox);

		// add the libvlc version if available
		if (const char *libvlcVer = VLCAudioVideoPlayer::GetLibVersion(); libvlcVer != nullptr)
			GPDrawStringAdv(g, MsgFmt(_T("Libvlc version %hs"), libvlcVer),
				smallerFont.get(), &br, origin, bbox);

		// add the ffmpeg version if available
		if (const char *ffmpegVer = Application::Get()->GetFFmpegVersion(); ffmpegVer != nullptr)
			GPDrawStringAdv(g, MsgFmt(_T("FFmpeg version %hs"), ffmpegVer),
				smallerFont.get(), &br, origin, bbox);
		
		// add the ChakraCore version if we're using Javascript
		if (auto js = JavascriptEngine::Get(); js != nullptr)
		{
			// retrieve the ChakraCore DLL file version resource data
			TCHAR ccdll[MAX_PATH];
			GetExeFilePath(ccdll, countof(ccdll));
			PathAppend(ccdll, _T("ChakraCore.dll"));
			FileVersionInfo ccv(ccdll);
			if (ccv.valid)
			{
				GPDrawStringAdv(g, MsgFmt(_T("Microsoft Chakra Core version %s"), ccv.versionStr.c_str()),
					smallerFont.get(), &br, origin, bbox);
			}
		}

		// draw the copyright details, bottom-justified
		MsgFmt cprMsg(IDS_APP_CPR, G_VersionInfo.copyrightDates, PINBALLY_COPYRIGHT_OWNERS);
		Gdiplus::RectF measured;
		g.MeasureString(cprMsg.Get(), -1, smallFont.get(), bbox, Gdiplus::StringFormat::GenericTypographic(), &measured);
		origin.Y = bbox.GetBottom() - measured.Height;
		GPDrawStringAdv(g, cprMsg, smallFont.get(), &br, origin, bbox);
	
		// flush our drawing to the pixel buffer
		g.Flush();

	}, eh, _T("About Box"));

	// open the popup, positioning it in the upper half of the screen
	popupSprite->offset.y = .2f;
	popupSprite->UpdateWorld();
	StartPopupAnimation(PopupAboutBox, popupName, true);
	UpdateDrawingList();
}

void PlayfieldView::PlayGame(int cmd, DWORD launchFlags, int systemIndex)
{
	// Remember the command that triggered the launch.  We might have
	// to ask the user for additional information (such as selecting
	// a system or approving privilege elevation), which will require
	// us to abort this PlayGame() attempt and pop up a new menu asking
	// for the additional info.  If the user makes a selection from
	// that menu, that will fill in the missing information and make a
	// new call here.  But that new call will have to know the command
	// that triggered the launch in the first place.  So that's what
	// we're storing here: the command that triggered this launch
	// attempt in case it has to be repeated.
	lastPlayGameCmd = cmd;
	lastPlayGameLaunchFlags = launchFlags;

	// get the current game
	if (GameListItem *game = GameList::Get()->GetNthGame(0); IsGameValid(game))
	{
		// If the game doesn't have a system, it must be an unconfigured
		// file.  All unconfigured files are loaded from table file sets,
		// which are associated with one or more systems.  If the file
		// set has exactly one associated system, we can infer that this
		// game uses that system, otherwise we have to ask.
		GameSystem *system = game->system;
		if (system == nullptr && game->tableFileSet != nullptr)
		{
			// if there's exactly one system, use that implicitly; if
			// there are more than one, we have to ask
			if (size_t nSystems = game->tableFileSet->systems.size(); nSystems == 0)
			{
				// there's nothing to choose from
			}
			else if (nSystems == 1)
			{
				// There's exactly one associated system, so it's a safe
				// bet that this is the right system for the game.
				system = game->tableFileSet->systems.front();
			}
			else if (systemIndex >= 0 && (size_t)systemIndex < nSystems)
			{
				// A system index was explicitly specified, meaning that
				// the user selected a system from the system picker menu
				// we presented in the code below in a prior pass.  Run
				// the game with that system selected.
				size_t n = 0;
				for (auto s : game->tableFileSet->systems)
				{
					if (n++ == systemIndex)
					{
						system = s;
						break;
					}
				}
			}
			else
			{
				// The game came from a table file set that's associated
				// with multiple systems.  This can happen when multiple
				// versions of a player are installed and they all use the
				// same table folder and extension.  This is common with
				// VP, since VP has broken backwards compatibility several
				// times over its evolution and thus forces people to keep
				// multiple versions installed.  Since the system is
				// ambiguous, we have to ask the user which to use.				

				// set up the menu - start with the prompt message
				std::list<MenuItemDesc> md;
				md.emplace_back(LoadStringT(IDS_MENU_PICK_SYSTEM), -1);
				md.emplace_back(_T(""), -1);

				// add the system options
				int n = 0;
				for (auto s : game->tableFileSet->systems)
				{
					// select if it this is the one we selected the last time
					// this menu was displayed
					UINT flags = 0;
					if (n == game->recentSystemIndex)
						flags |= MenuSelected;

					// Add the system item.  The command is in the ID_PICKSYS_FIRST
					// to _LAST range, indexed by the system index in the list.
					md.emplace_back(s->displayName.c_str(), ID_PICKSYS_FIRST + n);

					// count it
					++n;
				}

				// add a "cancel" item
				md.emplace_back(_T(""), -1);
				md.emplace_back(LoadStringT(IDS_MENU_CXL_PICK_SYSTEM), ID_MENU_RETURN);

				// show the menu
				ShowMenu(md, L"play pick system", SHOWMENU_DIALOG_STYLE);

				// return - the user can initiate the launch again using
				// the menu selections
				return;
			}
		}

		// if we didn't find a system, we can't launch
		if (system == nullptr)
		{
			// This table file set has no associated systems; we can't
			// play this game.
			ShowError(EIT_Error, LoadStringT(IDS_ERR_NOSYSNOPLAY));
			return;
		}

		// Record the selected system index from this launch.  This will
		// be used to select the default item the next time we have to
		// display the system selection menu, and for re-launching if we
		// have to go through an Admin approval menu.
		game->recentSystemIndex = systemIndex;

		// launch the game with the selected system
		PlayGame(cmd, launchFlags, game, system);
	}
}

void PlayfieldView::PlayGame(int cmd, DWORD launchFlags, GameListItem *game, GameSystem *system,
	const std::list<std::pair<CSTRING, TSTRING>> *overrides)
{
	// If desired, collect a credit on launch
	if ((launchFlags & Application::LaunchFlags::ConsumeCredit) != 0)
	{
		// reset the coin balance (converting to credits)
		ResetCoins();

		// Deduct a credit, if there's at least one credit available. 
		// Note that this only provides a novelty implementation; if we
		// wanted to implement some kind of real coin/credit management,
		// we wouldn't charge a credit here; instead, we'd charge as
		// usual in the games themselves, but we'd transfer the credit
		// balance back and forth between the games and the menu system.
		// Charging here is kind of pointless because it lets the player 
		// play as many rounds on the launched table as they want for
		// one credit here, assuming that the tables are set on free
		// play.  (And if the tables aren't set on free play, it makes
		// even less sense to collect a toll here, as we're going to
		// charge the player again in the game.)  But transfering the
		// credits to and from the games is difficult, because most
		// tables are VPinMAME ROM-based games; we'd have to patch the
		// VPM NVRAM files with our credit balance on launch, and then
		// read the updated value back out on return.  That would take
		// another program along the lines of PINemHi (the high-score
		// NVRAM parser), which I don't think anyone has built.  So
		// anyway, our whole coin credit system is just here for the
		// entertainment value, and the only reason to charge a credit
		// here is to make sure we charge credits *somewhere*, so that
		// the balance doesn't just keep rising on coin insertions.
		if (bankedCredits >= 1.0f)
			SetCredits(bankedCredits - 1.0f);
	}

	// Save any in-memory changes to config/stats files.  Launching an
	// external program always runs the risk of crashing the system, so
	// this would be a good time to make sure we've committed changes.
	// Program launchs also tend to be fairly slow, so this is also a
	// good reason to do a save now - the few milliseconds needed to
	// write our files won't be noticeable against the backdrop of a
	// whole process launch.
	Application::SaveFiles();

	// Clear any cached high score information, in case the user
	// sets a new high score on this run.  That will ensure that
	// we fetch fresh data from the NVRAM file the next time we
	// want to display the high scores.
	game->ClearCachedHighScores();

	// If we're launching for media capture, set up the capture list
	std::list<Application::LaunchCaptureItem> launchCaptureList;
	if ((launchFlags & Application::LaunchFlags::Capturing) != 0)
	{
		// include each item with capture enabled
		for (auto &s : captureList)
		{
			switch (s.mode)
			{
			case IDS_CAPTURE_CAPTURE:
			case IDS_CAPTURE_SILENT:
				// capturing in default mode, no audio
				launchCaptureList.emplace_back(s.win, s.mediaType, false);
				break;

			case IDS_CAPTURE_WITH_AUDIO:
				// capturing video with audio
				launchCaptureList.emplace_back(s.win, s.mediaType, true);
				break;

			case IDS_CAPTURE_KEEP:
			case IDS_CAPTURE_SKIP:
				// not capturing this item
				break;
			}
		}

		// if nothing was selected for capture, say so and skip the launch
		if (launchCaptureList.size() == 0)
		{
			ShowError(EIT_Information, LoadStringT(IDS_CAPSTAT_NONE_SELECTED));
			return;
		}
	}

	// queue the game for launch, replacing any prior launch queue
	auto app = Application::Get();
	app->ClearLaunchQueue();
	app->QueueLaunch(cmd, launchFlags, game, system, &launchCaptureList, captureStartupDelay);

	// apply overrides
	if (overrides != nullptr)
	{
		for (auto &p : *overrides)
			app->SetNextQueuedGameOverride(p.first.c_str(), p.second);
	}

	// launch it
	LaunchQueuedGame();
}

void PlayfieldView::LaunchQueuedGame()
{
	// Find the next launchable game in the queue.  Keep going until we
	// find a game we can launch, or we exhaust the queue.
	GameListItem *game = nullptr;
	GameSystem *sys = nullptr;
	for (;;)
	{
		// Get the next queued game.  If the queue is empty, we're done.
		Application::QueuedGameInfo info;
		if (!Application::Get()->GetNextQueuedGame(info))
			return;

		// Fire a Javascript pre-launch event, to give user scripts
		// a chance to reject the launch.  If the event isn't canceled
		// (that is, FireLaunchEvent returns true), we can proceed with
		// the launch.
		game = GameList::Get()->GetByInternalID(info.gameId);
		sys = GameList::Get()->GetSystem(info.sysConfigIndex);
		JavascriptEngine::JsObj overrides(JS_INVALID_REFERENCE);
		if (game != nullptr && FireLaunchEvent(&overrides, jsPreLaunchEvent, game, info.cmd))
		{
			// Cleared for launch.  Check for overrides.
			if (!overrides.IsNull())
			{
				try
				{
					for (auto p : launchOverrideProps)
					{
						if (overrides.Has(p))
							Application::Get()->SetNextQueuedGameOverride(p, overrides.Get<TSTRING>(p));
					}
				}
				catch (JavascriptEngine::CallException exc)
				{
					exc.Log(_T("Applying game launch overrides"));
				}
			}
			
			// proceed with this game
			break;
		}
		else
		{
			// Javascript canceled the launch.  Discard this queued
			// launch request and continue with the next queued item.
			Application::Get()->RemoveNextQueuedGame();
		}
	}

	// Before launching, shut down our DOF interface, so that the game
	// can take it over while running.
	dof.SetRomContext(_T(""));
	dof.SetUIContext(L"");
	DOFClient::Shutdown(false);

	// Also shut down the real DMD, so that the game can take it over
	if (realDMD != nullptr)
	{
		realDMD->BeginRunningGameMode();
		realDMD.reset(nullptr);
	}

	// kill any pending return-from-game timers, as we're going into a new game
	KillTimer(hWnd, restoreDOFAndDMDTimerID);

	// try launching the game
	Application::InUiErrorHandler eh;
	if (Application::Get()->LaunchNextQueuedGame(eh))
	{
		// show the "game running" popup in the main window
		BeginRunningGameMode(game, sys);

		// play the generic Launch button sound
		PlayButtonSound(_T("Launch"));

		// check for an audio clip to play on launching the game
		TSTRING audio;
		if (game->GetMediaItem(audio, GameListItem::launchAudioType))
		{
			// Got a clip - set up an Audio Video player for it.  Note that
			// we need to use an A/V player for it instead of the low-level
			// Sound Manager, because the latter can only handle uncompressed
			// PCM formats like WAV.  Launch audio clips are typically MP3s.
			SilentErrorHandler eh;
			RefPtr<AudioVideoPlayer> player(new DShowAudioPlayer(hWnd));
			if (player->Open(audio.c_str(), eh))
			{
				// set the volume level to the global video level
				int vol = Application::Get()->GetVideoVolume();
				player->SetVolume(vol);

				// start playback
				if (player->Play(eh))
				{
					// Playback started.  We'll need to keep this object alive
					// until playback finishes, then delete it.  The object will
					// notify us when playback ends via an AVPMsgEndOfPresentation
					// message to the window.  Keep a reference to the object
					// until then in our active audio table.
					DWORD cookie = player->GetCookie();
					activeAudio.emplace(std::piecewise_construct,
						std::forward_as_tuple(cookie),
						std::forward_as_tuple(player.Detach(), ActiveAudio::LaunchAudio, vol));
				}
			}
		}

		// start the audio fade timer for any non-launch audios currently playing
		SetTimer(hWnd, audioFadeoutTimerID, 20, 0);
	}
	else
	{
		// launch failed - reinstate the DOF client
		SetTimer(hWnd, restoreDOFAndDMDTimerID, 100, NULL);
	}
}

bool PlayfieldView::FireLaunchEvent(JsValueRef type, LONG gameId, int cmd, const TCHAR *errorMessage)
{
	bool ret = true;
	if (auto game = GameList::Get()->GetByInternalID(gameId); game != nullptr)
		ret = FireLaunchEvent(type, game, cmd, errorMessage);

	return ret;
}

bool PlayfieldView::FireLaunchEvent(JsValueRef type, GameListItem *game, int cmd, const TCHAR *errorMessage)
{
	return FireLaunchEvent(nullptr, type, game, cmd, errorMessage);
}

bool PlayfieldView::FireLaunchEvent(JavascriptEngine::JsObj *overrides, JsValueRef type, GameListItem *game, int cmd, const TCHAR *errorMessage)
{
	bool ret = true;
	if (auto js = JavascriptEngine::Get(); js != nullptr)
	{
		// pass the error message as a js string if provided, otherwise use 'undefined'
		JsValueRef errorVal = errorMessage != nullptr ? js->NativeToJs(errorMessage) : js->GetUndefVal();

		// fire the event
		JsValueRef eventObj;
		ret = js->FireAndReturnEvent(eventObj, jsMainWindow, type, BuildJsGameInfo(game), cmd, errorVal);

		// if the caller wants the 'overrides' object, retrieve it
		if (overrides != nullptr)
		{
			try
			{
				JavascriptEngine::JsObj event(eventObj);
				overrides->jsobj = event.Get<JsValueRef>("overrides");
			}
			catch (JavascriptEngine::CallException exc)
			{
				exc.Log(_T("Game launch event"));
			}
		}
	}

	return ret;
}

void PlayfieldView::ResetGameTimeout()
{
	// If a game is running, and the timeout interval is non-zero, set 
	// the timer.  This replaces any previous timer, so it effectively 
	// resets the interval if a timer was already set.
	if (runningGamePopup != nullptr && gameTimeout != 0)
		SetTimer(hWnd, gameTimeoutTimerID, gameTimeout, NULL);
}

void PlayfieldView::OnGameTimeout()
{
	// If the game is running in Admin mode, ignore timeouts on the
	// user-mode side.  We can't get accurate inactivity information
	// because Windows blocks a user-mode program (us) from observing
	// any keyboard or other input events while a privileged program
	// (an Admin mode game) is in the foreground.  As far as we're
	// concerned, there are simply no keyboard events going on, so
	// we'll be tricked into thinking that the user hasn't been
	// playing the game.  Our Admin Mode proxy does its own keyboard
	// monitoring in this scenario, so our only job is to stand by.
	if (Application::Get()->IsGameInAdminMode())
		return;

	// check to see if the last input event happened within the timeout 
	// interval
	DWORD dt = GetTickCount() - lastInputEventTime;
	if (dt < gameTimeout)
	{
		// It hasn't been long enough since the last input event to
		// shut down the game.  Reset the timer to fire again after
		// the remaining time on the new timeout interval (starting
		// at the last input event time) expires.
		SetTimer(hWnd, gameTimeoutTimerID, gameTimeout - dt, NULL);
	}
	else
	{
		// The last input event preceded the current timeout period,
		// so we've been inactive long enough to terminate the game.
		// Fire a Kill Game command.
		PostMessage(WM_COMMAND, ID_KILL_GAME);
	}
}

bool PlayfieldView::InstructionCardExists(GameListItem *game)
{
	// figure the media search flags
	DWORD gmiFlags = GameListItem::GMI_EXISTS;
	if (!instCardEnableFlash)
		gmiFlags |= GameListItem::GMI_NO_SWF;

	// load the instruction card list; return true if it contains any items
	std::list<TSTRING> cards;
	return game->GetMediaItems(cards, GameListItem::instructionCardImageType, gmiFlags) && cards.size() != 0;
}

void PlayfieldView::ShowInstructionCard(int cardNumber)
{
	// fire an event first, abort on cancel
	const WCHAR *popupName = L"instructions";
	if (!FirePopupEvent(true, popupName))
		return;

	// get the current game; proceed only if it's valid
	GameListItem *game = GameList::Get()->GetNthGame(0);
	if (!IsGameValid(game))
		return;

	// figure the media search flags
	DWORD gmiFlags = GameListItem::GMI_EXISTS;
	if (!instCardEnableFlash)
		gmiFlags |= GameListItem::GMI_NO_SWF;

	// load the instruction card list
	std::list<TSTRING> cards;
	if (!game->GetMediaItems(cards, GameListItem::instructionCardImageType, gmiFlags))
		return;

	// if the selected page is out of range, wrap it
	if (cardNumber < 0)
		cardNumber = (int)cards.size() - 1;
	else if (cardNumber >= (int)cards.size())
		cardNumber = 0;

	// find the selected card
	TSTRING *fname = nullptr;
	int i = 0;
	for (auto &it : cards)
	{
		if (i++ == cardNumber)
		{
			fname = &it;
			break;
		}
	}
	if (fname == nullptr)
		return;

	// Check which window is going to display the card
	bool displayHere = true;
	BackglassBaseView *destView = nullptr;
	if (instCardLoc == _T("backglass"))
		destView = Application::Get()->GetBackglassView();
	else if (instCardLoc == _T("topper"))
		destView = Application::Get()->GetTopperView();

	// If there's a designated destination window, and it's open
	// and visible, try showing the instruction card there
	bool ok = false;
	if (destView != nullptr 
		&& IsWindow(destView->GetHWnd())
		&& IsWindowVisible(destView->GetHWnd())
		&& !IsIconic(destView->GetHWnd()))
	{
		// the window is visible - try showing the instruction card there
		displayHere = false;
		ok = destView->ShowInstructionCard(fname->c_str());
	}

	// If we didn't end up displaying the instruction card somewhere else,
	// display it in the main window.
	if (displayHere)
	{
		// load the instruction card into our popup sprite
		popupSprite.Attach(PrepInstructionCard(fname->c_str()));
		ok = (popupSprite != nullptr);
	}

	if (ok)
	{
		// If we're displaying the card in another window, we still
		// need to display a fake popup in our own window so that we
		// act like we're in popup mode as long a the card is up. 
		// Just set up a blank sprite.
		if (!displayHere)
			popupSprite.Attach(new Sprite());

		// remember which card we're showing (there might be more than one)
		instCardPage = cardNumber;

		// if we're switching to instruction card mode, animate the popup
		StartPopupAnimation(PopupInstructions, popupName, true);

		// update the drawing list for the new sprite
		UpdateDrawingList();

		// Signal an Instruction Card event in DOF
		QueueDOFPulse(L"PBYInstructions");
	}
}

void PlayfieldView::ShowFlyer(int pageNumber)
{
	// fire an event first, abort on cancel
	const WCHAR *popupName = L"flyer";
	if (!FirePopupEvent(true, popupName))
		return;

	// get the current game; proceed only if it's valid
	GameListItem *game = GameList::Get()->GetNthGame(0);
	if (!IsGameValid(game))
		return;

	// load the flyer file list
	std::list<TSTRING> flyers;
	if (!game->GetMediaItems(flyers, GameListItem::flyerImageType))
		return;

	// if the selected page is out of range, wrap it
	if (pageNumber < 0)
		pageNumber = (int)flyers.size() - 1;
	else if (pageNumber >= (int)flyers.size())
		pageNumber = 0;

	// find the selected page
	TSTRING *flyer = nullptr;
	int i = 0;
	for (auto &it : flyers)
	{
		if (i++ == pageNumber)
		{
			flyer = &it;
			break;
		}
	}
	if (flyer == nullptr)
		return;

	// get the file dimensions
	ImageFileDesc imageDesc;
	GetImageFileInfo(flyer->c_str(), imageDesc, true);

	// Figure the sprite dimensions.  The scale of these is arbitrary,
	// because we automatically rescale this in ScaleSprites() according
	// to the window proportions.  All that matters is matching the
	// original image's aspect ratio.  So use a fixed height of 1.0,
	// and a proportional width.
	float aspect = imageDesc.dispSize.cy == 0 ? 1.0f : float(imageDesc.dispSize.cx) / float(imageDesc.dispSize.cy);
	POINTF normalizedSize = { aspect, 1.0f };

	// figure the corresponding pixel size
	SIZE pixSize = { (int)(normalizedSize.x * szLayout.cx), (int)(normalizedSize.y * szLayout.cy) };

	// load the image at the calculated size
    Application::InUiErrorHandler eh;
	popupSprite.Attach(new Sprite());
	if (!popupSprite->Load(flyer->c_str(), normalizedSize, pixSize, eh))
	{
		popupSprite = nullptr;
		UpdateDrawingList();
		ShowQueuedError();
		return;
	}

	// remember the new flyer page
	flyerPage = pageNumber;

	// if we're switching to flyer mode, animate the popup
	StartPopupAnimation(PopupFlyer, popupName, true);

	// put the new sprite in the drawing list
	UpdateDrawingList();

	// Signal a Flyer event in DOF
	QueueDOFPulse(L"PBYFlyer");
}

void PlayfieldView::RateGame()
{
	// ignore it if there's no game selection
	GameList *gl = GameList::Get();
	GameListItem *game = gl->GetNthGame(0);
	if (!IsGameValid(game))
		return;

	// load the current game's rating from the database into the
	// working rating for the dialog
	workingRating = gl->GetRating(game);

	// show the dialog
	UpdateRateGameDialog();
}

void PlayfieldView::UpdateRateGameDialog()
{
	// fire an event first, abort on cancel
	const WCHAR *popupName = L"rate game";
	if (!FirePopupEvent(true, popupName))
		return;

	// ignore it if there's no game selection
	GameList *gl = GameList::Get();
	GameListItem *game = gl->GetNthGame(0);
	if (!IsGameValid(game))
		return;

	// Force the working rating to a valid value.  We use -1 to represent "no 
	// rating".  Negative ratings are otherwise meaningless, so normalize any
	// negative value to -1.  At the other extreme, the maximum rating is 5 stars.
	if (workingRating < 0.0f)
		workingRating = -1.0f;
	else if (workingRating > 5.0f)
		workingRating = 5.0f;

	// set up the info box
	const int width = 600, height = 480;
	Application::InUiErrorHandler eh;
	popupSprite.Attach(new Sprite());
	if (!popupSprite->Load(width, height, [gl, game, this, width, height](HDC hdc, HBITMAP)
	{
		// set up a GDI+ drawing context
		Gdiplus::Graphics g(hdc);

		// draw the background
		Gdiplus::SolidBrush bkgBr(Gdiplus::Color(0xd0, 0x00, 0x00, 0x00));
		g.FillRectangle(&bkgBr, 0, 0, width, height);

		// draw the border
		const int borderWidth = 2;
		Gdiplus::Pen pen(Gdiplus::Color(0xe0, 0xff, 0xff, 0xff), float(borderWidth));
		g.DrawRectangle(&pen, borderWidth / 2, borderWidth / 2, width - borderWidth, height - borderWidth);

		// margin for our content area
		const float margin = 16.0f;

		// centered string formatter
		Gdiplus::StringFormat centerFmt;
		centerFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
		centerFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

		// draw the game wheel icon, if available, or just the game title
		TSTRING wheelFile;
		Gdiplus::SolidBrush textBr(Gdiplus::Color(0xFF, 0xFF, 0xFF, 0xFF));
		if (game->GetMediaItem(wheelFile, GameListItem::wheelImageType))
		{
			// load the image
			std::unique_ptr<Gdiplus::Bitmap> wheelBmp(Gdiplus::Bitmap::FromFile(wheelFile.c_str()));
			if (wheelBmp != nullptr)
			{
				// draw the image centered at the top, with a maximum height
				float imgWidth = (float)wheelBmp->GetWidth(), imgHeight = (float)wheelBmp->GetHeight();
				float drawWidth = ((float)width - margin * 4.0f);
				float drawHeight = imgWidth != 0.0f ? drawWidth * (imgHeight / imgWidth) : drawWidth * .25f;
				float maxHeight = (float)height/3.0f - margin*2.0f;
				if (drawHeight > maxHeight)
				{
					drawHeight = maxHeight;
					drawWidth = imgHeight != 0.0f ? drawHeight * (imgWidth / imgHeight) : imgHeight * 4.0f;
				}
				Gdiplus::RectF wrc(
					((float)width - drawWidth) / 2.0f, (maxHeight - drawHeight)/2.0f + margin,
					drawWidth, drawHeight);
				g.DrawImage(wheelBmp.get(), wrc);
			}
		}
		else
		{
			// no wheel icon - just draw the title
			Gdiplus::RectF rcTitle(0.0f, 0.0f, (float)width, (float)height / 3.0f);
			g.DrawString(game->title.c_str(), -1, popupTitleFont, rcTitle, &centerFmt, &textBr);
		}

		// draw the stars
		int cxStar = 0, cyStar = 0;
		if (stars != nullptr)
		{
			// draw the stars
			cxStar = stars.get()->GetWidth() / 3;
			cyStar = stars.get()->GetHeight();
			float x = (float)width / 2.0f - (float)cxStar*2.5f;
			float y = ((float)(height - cyStar)) / 2.0f;
			DrawStars(g, x, y, workingRating);
		}

		// show the current rating as text
		Gdiplus::RectF rcStars(0.f, (float)height / 2.0f + (float)cyStar, (float)width, (float)cyStar);
		FontPref &starsFont = popupDetailFont;
		g.DrawString(MsgFmt(_T("(%s)"), StarsAsText(workingRating).c_str()), -1, starsFont, rcStars, &centerFmt, &textBr);

		// draw the prompt text
		FontPref &promptFont = popupFont;
		Gdiplus::RectF rcPrompt(0.0f, (float)height*2.0f/3.0f, (float)width, (float)height/3.0f);
		g.DrawString(LoadStringT(IDS_RATE_GAME_PROMPT), -1, promptFont, rcPrompt, &centerFmt, &textBr);

		// flush GDI+ drawing to the bitmap
		g.Flush();

	}, eh, _T("Rate Game Dialog")))
	{
		popupSprite = nullptr;
		UpdateDrawingList();
		return;
	}

	// adjust it to the canonical popup position
	AdjustSpritePosition(popupSprite);

	// if we're switching to flyer mode, animate the popup
	StartPopupAnimation(PopupRateGame, popupName, true);

	// put the new sprite in the drawing list
	UpdateDrawingList();
}

TSTRING PlayfieldView::StarsAsText(float rating)
{
	// get the whole and fractional stars
	float wholeStars = floorf(rating), fracStars = rating - wholeStars;

	// figure the number in text form, rounding to the nearest 1/2 star
	// (Unicode 0x00BD is the 1/2 fraction symbol)
	MsgFmt num(_T("%d%s"), (int)wholeStars, fracStars > 0.25f ? _T("\xBD") : _T(""));

	// format the string
	return rating < 0 ? LoadStringT(IDS_RATE_GAME_UNRATED) :
		wholeStars == 0 && fracStars > 0.25f ? LoadStringT(IDS_RATE_GAME_HALFSTAR) :
		rating == 1 ? LoadStringT(IDS_RATE_GAME_1STAR) :
		MsgFmt(IDS_RATE_GAME_STARS, num);
}

TSTRING PlayfieldView::PlayTimeAsText(int seconds)
{
	int minutes = seconds / 60;
	seconds %= 60;
	int hours = minutes / 60;
	minutes %= 60;

	if (hours > 1 || (hours == 1 && minutes > 0))
		return MsgFmt(IDS_N_HOURS, hours, minutes).Get();
	else if (hours == 1 && minutes == 0)
		return LoadStringT(IDS_1_HOUR);
	else if (minutes > 1)
		return MsgFmt(IDS_N_MINUTES, minutes).Get();
	else if (minutes == 1)
		return LoadStringT(IDS_1_MINUTE);
	else
		return MsgFmt(IDS_N_MINUTES, 0).Get();
}

void PlayfieldView::DrawStars(Gdiplus::Graphics &g, float x, float y, float rating)
{
	if (stars != nullptr)
	{
		int cxStar = stars.get()->GetWidth() / 3;
		int cyStar = stars.get()->GetHeight();
		for (float i = 1.0f; i <= 5.0f; ++i, x += (float)cxStar)
		{
			// Figure out which cell to draw.  The stars image has three
			// cells: empty star, half star, full star.
			int cell = rating > i - 0.25f ? 2 : rating > i - 0.75f ? 1 : 0;

			// draw the star cell
			g.DrawImage(stars.get(), x, y,
				(float)(cxStar*cell), 0.0f, (float)cxStar, (float)cyStar,
				Gdiplus::UnitPixel);
		}
	}
}

void PlayfieldView::AdjustRating(float delta)
{
	// ignore if we're not running the Rate Game popup
	if (popupSprite == nullptr || popupType != PopupRateGame)
		return;

	// ignore it if there's no game selection
	GameList *gl = GameList::Get();
	GameListItem *game = gl->GetNthGame(0);
	if (!IsGameValid(game))
		return;

	// Adjust the rating.  If we're adjusting upwards from any negative
	// value, adjust to zero stars; otherwise just add the value.
	if (workingRating < 0 && delta > 0)
		workingRating = 0.0f;
	else
		workingRating += delta;

	// update the popup
	UpdateRateGameDialog();
}

void PlayfieldView::ShowAudioVolumeDialog()
{
	// get the game and make sure we have a valid selection
	auto gl = GameList::Get();
	auto game = gl->GetNthGame(0);
	if (IsGameValid(game))
	{
		// set the working audio volume to the game's current database value
		workingAudioVolume = gl->GetAudioVolume(game);

		// display the dialog
		UpdateAudioVolumeDialog();
	}
}

void PlayfieldView::UpdateAudioVolumeDialog()
{
	// fire an event first, abort on cancel
	const WCHAR *popupName = L"game audio volume";
	if (!FirePopupEvent(true, popupName))
		return;

	// ignore it if there's no game selection
	GameList *gl = GameList::Get();
	GameListItem *game = gl->GetNthGame(0);
	if (!IsGameValid(game))
		return;

	// update all playing media with the current volume
	ApplyWorkingAudioVolume();

	// set up the dialog box display
	int width = 800, height = 800;
	auto Draw = [gl, game, this, width, &height](HDC hdc, HBITMAP)
	{
		// set up a GDI+ drawing context
		Gdiplus::Graphics g(hdc);

		// draw the background
		Gdiplus::SolidBrush bkgBr(Gdiplus::Color(0xd0, 0x00, 0x00, 0x00));
		g.FillRectangle(&bkgBr, 0, 0, width, height);

		// draw the border
		const int borderWidth = 2;
		Gdiplus::Pen pen(Gdiplus::Color(0xe0, 0xff, 0xff, 0xff), float(borderWidth));
		g.DrawRectangle(&pen, borderWidth / 2, borderWidth / 2, width - borderWidth, height - borderWidth);

		// margin for our content area
		const float margin = 16.0f;
		const float inner = margin + borderWidth;

		// set up a string drawer
		GPDrawString gds(g, Gdiplus::RectF(inner, inner, width - 2*inner, height - 2*inner));

		// draw the main caption and instructions text
		Gdiplus::SolidBrush br(Gdiplus::Color(0xff, 0xff, 0xff));
		gds.DrawString(LoadStringT(IDS_ADJUST_AUDIO_CAPTION), popupFont, &br, true, 0);
		gds.DrawString(LoadStringT(IDS_ADJUST_AUDIO_INSTR), popupSmallerFont, &br, true, 0);

		// add some vertical space
		gds.curOrigin.Y += 20.0f;

		// set up the bounding box for the volume bar
		const float barHeight = popupSmallerFont->GetHeight(&g) + 4.0f;
		Gdiplus::RectF rc(gds.bbox.X, gds.curOrigin.Y, gds.bbox.Width, barHeight);
		gds.curOrigin.Y += barHeight;

		// draw the volume bar
		const float onWidth = static_cast<float>(workingAudioVolume) / 100.0f * rc.Width;
		Gdiplus::RectF rcOn = rc, rcOff = rc;
		rcOn.Width = onWidth;
		rcOff.X += onWidth;
		rcOff.Width -= onWidth;
		Gdiplus::SolidBrush brOn(Gdiplus::Color(0x00, 0xff, 0x00));
		Gdiplus::SolidBrush brOff(Gdiplus::Color(0x00, 0x70, 0x00));
		g.FillRectangle(&brOn, rcOn);
		g.FillRectangle(&brOff, rcOff);

		// superimpose the volume level
		Gdiplus::StringFormat ctr(Gdiplus::StringFormat::GenericTypographic());
		ctr.SetAlignment(Gdiplus::StringAlignmentCenter);
		g.DrawString(MsgFmt(_T("%d%%"), workingAudioVolume).Get(), -1, popupSmallerFont, rc, &ctr, &br);

		// set the actual needed height
		height = static_cast<int>(gds.curOrigin.Y + inner);

		// flush GDI+ drawing to the bitmap
		g.Flush();
	};

	// draw once off-screen to figure the height
	DrawOffScreen(width, height, [&](HDC hdc, HBITMAP hbmp, const void*, const BITMAPINFO&) { Draw(hdc, hbmp); });

	// draw it for real
	Application::InUiErrorHandler eh;
	popupSprite.Attach(new Sprite());
	if (!popupSprite->Load(width, height, Draw, eh, _T("Game Audio Volume Dialog")))
	{
		popupSprite = nullptr;
		UpdateDrawingList();
		return;
	}

	// adjust it to the canonical popup position
	AdjustSpritePosition(popupSprite);

	// if we're switching to flyer mode, animate the popup
	StartPopupAnimation(PopupGameAudioVolume, popupName, true);

	// put the new sprite in the drawing list
	UpdateDrawingList();
}

void PlayfieldView::AdjustWorkingAudioVolume(int delta)
{
	// set the new volume
	workingAudioVolume += delta;

	// redisplay the dialog
	UpdateAudioVolumeDialog();
}

void PlayfieldView::ApplyWorkingAudioVolume()
{
	// clamp the working audio volume to the valid range
	workingAudioVolume = max(0, workingAudioVolume);
	workingAudioVolume = min(workingAudioVolume, 100);

	// figure the actual volume by combining the working volume and the
	// global video volume setting
	int vol = workingAudioVolume * Application::Get()->GetVideoVolume() / 100;

	// update our video sprites
	auto Update = [vol](GameMedia<VideoSprite> &media)
	{
		// update its video player, if it has one
		if (media.sprite != nullptr && media.sprite->IsVideo())
		{
			if (auto vp = media.sprite->GetVideoPlayer(); vp != nullptr)
				vp->SetVolume(vol);
		}

		// update its audio player, if it has one
		if (media.audio != nullptr)
			media.audio->SetVolume(vol);
	};
	Update(incomingPlayfield);
	Update(currentPlayfield);

	// update the secondary windows
	auto Update2 = [vol](SecondaryView *view)
	{
		if (view != nullptr)
			view->ApplyWorkingAudioVolume(vol);
	};
	auto app = Application::Get();
	Update2(app->GetBackglassView());
	Update2(app->GetDMDView());
	Update2(app->GetTopperView());
	Update2(app->GetInstCardView());

	// update the real DMD video
	if (realDMD != nullptr)
		realDMD->ApplyWorkingAudioVolume(vol);
}

void PlayfieldView::JsShowPopup(JavascriptEngine::JsObj contents)
{
	auto js = JavascriptEngine::Get();
	try
	{
		// If a drawing context is already set up, this must be a recursive
		// call (that is, they're trying to show a popup from within their
		// popup drawing callback).  That's not allowed.
		if (jsDC != nullptr)
			return js->Throw(_T("Recursive call to showPopup isn't allowed")), static_cast<void>(0);

		// set defaults
		float opacity = 0.9f;
		DWORD bkColor = 0x202020;
		DWORD txtColor = 0xffffff;
		DWORD borderColor = 0xffffff;
		int borderWidth = 2;
		float width = -1.0f, height = -1.0f;
		float xOfs = -1.0f, yOfs = -1.0f;
		TSTRING bkImage;
		TSTRING id = _T("UserDefined");
		JsValueRef drawFunc = JS_INVALID_REFERENCE;

		// get the actual values specified in the 'contents' object
		if (contents.Has("opacity")) opacity = contents.Get<float>("opacity");
		if (contents.Has("backgroundColor")) bkColor = contents.Get<int>("backgroundColor");
		if (contents.Has("textColor")) txtColor = contents.Get<int>("textColor");
		if (contents.Has("borderColor")) borderColor = contents.Get<int>("borderColor");
		if (contents.Has("borderWidth")) borderWidth = contents.Get<int>("borderWidth");
		if (contents.Has("width")) width = contents.Get<float>("width");
		if (contents.Has("height")) height = contents.Get<float>("height");
		if (contents.Has("x")) xOfs = contents.Get<float>("x");
		if (contents.Has("y")) yOfs = contents.Get<float>("y");
		if (contents.Has("backgroundImage")) bkImage = contents.Get<TSTRING>("backgroundImage");
		if (contents.Has("id")) id = contents.Get<TSTRING>("id");
		if (contents.Has("draw")) drawFunc = contents.Get<JsValueRef>("draw");

		// If a background image was specified, and the path is relative, get
		// the full path relative to the program folder.
		const TCHAR *bkImagePath = bkImage.c_str();
		TCHAR bkImagePathBuf[MAX_PATH];
		if (bkImagePath[0] != 0 && PathIsRelative(bkImagePath))
		{
			GetDeployedFilePath(bkImagePathBuf, bkImagePath, _T(""));
			bkImagePath = bkImagePathBuf;
		}

		// figure the pixel dimensions from the percentage-based dimensions
		int pixWidth = static_cast<int>(roundf(width*1920.0f)) + borderWidth*2;
		int pixHeight = static_cast<int>(roundf(height*1920.0f)) + borderWidth*2;

		// If the size isn't specified, and there's a background image, use the
		// image size, normalized to 1920H x 1080W.
		if (width < 0.0f && height < 0.0f && bkImagePath[0] != 0)
		{
			ImageFileDesc desc;
			if (GetImageFileInfo(bkImagePath, desc, true))
			{
				// figure the image size relative to the normalized layout height
				pixWidth = desc.dispSize.cx + borderWidth*2;
				pixHeight = desc.dispSize.cy + borderWidth*2;

				// refigure the percentage dimensions from the pixel sizes
				width = static_cast<float>(pixWidth) / 1920.0f;
				height = static_cast<float>(pixHeight) / 1920.0f;
			}
		}

		// If we still don't have a width, apply a default of 80% of the 
		// normalized window width.  We're using units of window height,
		// and the normalized size is 1080W x 1920H, so 80% of 1080px ==
		// 864px == 45% of 1920, so .45 in height units.
		if (width < 0.0f)
		{
			width = 0.45f;
			pixWidth = 864;
		}

		// set up the draw function
		int contentHeight = -1;
		auto Draw = [&](Gdiplus::Graphics &g)
		{
			// RGB as packed int value from javascript to Gdiplus color
			static auto GdiColor = [](int color) {
				return Gdiplus::Color(
					static_cast<BYTE>((color >> 16) & 0xff),
					static_cast<BYTE>((color >> 8) & 0xff),
					static_cast<BYTE>((color) & 0xff));
			};

			// draw the background image, if applicable
			if (bkImagePath[0] != 0)
			{
				// draw the image
				Gdiplus::Image image(bkImagePath);
				g.DrawImage(&image, borderWidth, borderWidth);
			}
			else
			{
				// fill the background with the background color
				Gdiplus::SolidBrush br(Gdiplus::Color(
					static_cast<BYTE>(opacity*255.0f),
					static_cast<BYTE>((bkColor >> 16) & 0xff),
					static_cast<BYTE>((bkColor >> 8) & 0xff),
					static_cast<BYTE>(bkColor & 0xff)));

				g.FillRectangle(&br, 0.0f, 0.0f, static_cast<float>(pixWidth), static_cast<float>(pixHeight));
			}

			// if the javascript caller provided a draw callback function, call it
			if (drawFunc != JS_INVALID_REFERENCE)
			{
				// set up a drawing context for the callback
				jsDC.reset(new JsDrawingContext(g,
					static_cast<float>(pixWidth), static_cast<float>(pixHeight),
					static_cast<float>(borderWidth)));

				// Create the Javascript drawing context.  This is just an object with
				// prototype <jsDrawingContext>, which encapsulates the drawing methods.
				// The context object doesn't *actually* capture any host state, even
				// though it represents the host drawing state conceptually.  The real 
				// host state is managed by the jsPopupDC static, which is fine for our
				// purposes here since it only has to exist for the duration of the
				// callback.  If in the future we wanted to create multiple host drawing
				// contexts for javascript use that could exist simultaneously with
				// overlapping lifetimes, we'd make the concept match the reality by
				// putting a reference to the true host state object in the js object;
				// but for now we don't have any practical need for that, so keep it
				// simple by using the static.
				auto jsdc = JavascriptEngine::JsObj::CreateObjectWithPrototype(jsDrawingContextProto);

				// call the callback:  func(drawingContext)
				JsValueRef argv[] = { js->GetGlobalObject(), jsdc.jsobj }, result;
				if (JsErrorCode err = JsCallFunction(drawFunc, argv, static_cast<unsigned short>(countof(argv)), &result); err != JsNoError)
					js->Throw(err, _T("mainWindow.showPopup draw callback"));

				// if the callback returned a value, it's the height
				if (result != js->GetUndefVal())
					js->ToInt(contentHeight, result);

				// the drawing context is valid only for the duration of the callback
				jsDC.reset();
			}

			// draw the border
			Gdiplus::SolidBrush borderBrush(GdiColor(borderColor));
			Gdiplus::Pen borderPen(&borderBrush, static_cast<float>(borderWidth));
			g.DrawRectangle(&borderPen, borderWidth/2, borderWidth/2, pixWidth - borderWidth, pixHeight - borderWidth);
		};

		// If the height is still to be determined, do an off-screen drawing pass
		// to determine the content height.
		if (height < 0.0f)
		{
			// draw into a dummy off-screen bitmap to figure the content height
			DrawOffScreen(pixWidth, 1, [&](HDC hdc, HBITMAP, const void *dibits, const BITMAPINFO &bi)
			{ 
				Gdiplus::Graphics g(hdc);
				Draw(g);
			});

			// if the content height wasn't set, it's an error
			if (contentHeight < 0)
				return js->Throw(_T("Popup height must be returned from drawing function")), static_cast<void>(0);

			// use the content height
			height = static_cast<float>(contentHeight) / 1920.0f;
			pixHeight = static_cast<int>(height*1920.0f) + borderWidth*2;
		}

		// create the sprite
		popupSprite.Attach(new Sprite());
		popupSprite->Load(pixWidth, pixHeight, Draw, SilentErrorHandler(), _T("jsShowPopup"));

		// set the canonical popup position
		AdjustSpritePosition(popupSprite);

		// if an explicit position was specified, apply it
		if (xOfs >= 0)
			popupSprite->offset.x = xOfs;
		if (yOfs >= 0)
			popupSprite->offset.y = yOfs;

		// show the popup
		StartPopupAnimation(PopupUserDefined, id.c_str(), true);

		// put the new sprite in the drawing list
		UpdateDrawingList();
	}
	catch (JavascriptEngine::CallException exc)
	{
		js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

void PlayfieldView::JsDrawingContext::InitFont()
{
	// create a font object if we don't already have one
	if (font == nullptr)
		font.reset(CreateGPFont(fontName.c_str(), fontPtSize, fontWeight));

	// If there's still no font, create a default font. 
	if (font == nullptr)
	{
		// range-check the point size and weight, substituting reasonable defaults if
		// the user-specified ones look crazy
		int ptSize = fontPtSize >= 4 && fontPtSize < 400 ? fontPtSize : 24;
		int weight = fontWeight >= 100 && fontWeight <= 900 ? fontWeight : 400;

		// create the font
		font.reset(CreateGPFont(_T("Tahoma"), ptSize, weight));
	}

	// create a brush if we don't already have one
	if (textBrush == nullptr)
		textBrush.reset(new Gdiplus::SolidBrush(textColor));
}

void PlayfieldView::JsDrawDrawText(TSTRING text)
{
	// validate the drawing context
	auto js = JavascriptEngine::Get();
	if (jsDC == nullptr)
		return js->Throw(_T("Drawing operation is not valid now")), static_cast<void>(0);

	// initialize drawing resources
	jsDC->InitFont();

	// set up a text formatter with the current alignment
	Gdiplus::StringFormat f = Gdiplus::StringFormat::GenericTypographic();
	f.SetAlignment(jsDC->textAlignHorz);
	f.SetLineAlignment(jsDC->textAlignVert);
	f.SetFormatFlags(
		(f.GetFormatFlags() | Gdiplus::StringFormatFlags::StringFormatFlagsMeasureTrailingSpaces)
		& ~Gdiplus::StringFormatFlagsLineLimit);

	// Figure the layout area
	Gdiplus::RectF rcLayout(
		jsDC->textOrigin.X,
		jsDC->textOrigin.Y,
		jsDC->textBounds.GetRight() - jsDC->textOrigin.X,
		jsDC->textBounds.GetBottom() - jsDC->textOrigin.Y);

	// if the text ends in a newline, note it, but don't include it in the text
	// we send to DrawString
	INT len = static_cast<INT>(text.length());
	bool newline;
	if ((newline = (len > 0 && text[len-1] == '\n')) != false)
		--len;

	// draw the text
	jsDC->g.DrawString(text.c_str(), len, jsDC->font.get(), rcLayout, &f, jsDC->textBrush.get());

	// Advance the text origin.  If the text ended in a newline, advance the
	// vertical offset by the line height and move the horizontal offset to
	// the left of the text layout box.  Otherwise, advance the horizontal
	// offset by the text width.
	Gdiplus::RectF bbox;
	jsDC->g.MeasureString(text.c_str(), len, jsDC->font.get(), rcLayout, &bbox);
	if (newline)
	{
		jsDC->textOrigin.X = jsDC->textBounds.X;
		jsDC->textOrigin.Y += bbox.Height;
	}
	else
	{
		jsDC->textOrigin.X += bbox.Width;
	}
}

void PlayfieldView::JsDrawSetFont(JsValueRef name, JsValueRef pointSize, JsValueRef weight)
{
	// validate the drawing context
	auto js = JavascriptEngine::Get();
	if (jsDC == nullptr)
		return js->Throw(_T("Drawing operation is not valid now")), static_cast<void>(0);

	// set each value that's specified; leave undefined values unchanged
	JsErrorCode err;
	if (name != js->GetUndefVal() && (err = js->ToString(jsDC->fontName, name)) != JsNoError)
		return js->Throw(err), static_cast<void>(0);
	if (pointSize != js->GetUndefVal() && (err = js->ToInt(jsDC->fontPtSize, pointSize)) != JsNoError)
		return js->Throw(err), static_cast<void>(0);
	if (weight != js->GetUndefVal() && (err = js->ToInt(jsDC->fontWeight, weight)) != JsNoError)
		return js->Throw(err), static_cast<void>(0);

	// clear the previous font
	jsDC->font.reset();
}

void PlayfieldView::JsDrawSetTextColor(int rgb)
{
	// validate the drawing context
	auto js = JavascriptEngine::Get();
	if (jsDC == nullptr)
		return js->Throw(_T("Drawing operation is not valid now")), static_cast<void>(0);

	// set the new color
	jsDC->textColor = Gdiplus::Color(
		static_cast<BYTE>((rgb >> 16) & 0xff),
		static_cast<BYTE>((rgb >> 8) & 0xff),
		static_cast<BYTE>(rgb & 0xff));

	// clear the previous text brush
	jsDC->textBrush.reset();
}

void PlayfieldView::JsDrawSetTextAlign(JsValueRef horz, JsValueRef vert)
{
	auto js = JavascriptEngine::Get();
	if (jsDC == nullptr)
		return js->Throw(_T("Drawing operation is not valid now")), static_cast<void>(0);

	// set the values; leave undefined values unchanged
	auto Xlat = [js](JsValueRef val, Gdiplus::StringAlignment &align)
	{
		JsErrorCode err;
		int i;
		if (val != js->GetUndefVal() && (err = js->ToInt(i, val)) != JsNoError)
		{
			js->Throw(err);
			return false;
		}
		align = i < 0 ? Gdiplus::StringAlignmentNear :
			i > 0 ? Gdiplus::StringAlignmentFar :
			Gdiplus::StringAlignmentCenter;
		return true;
	};
	Xlat(horz, jsDC->textAlignHorz) || Xlat(vert, jsDC->textAlignVert);
}

void PlayfieldView::JsDrawDrawImage(TSTRING filename, float x, float y, JsValueRef widthVal, JsValueRef heightVal)
{
	// validate the drawing context
	auto js = JavascriptEngine::Get();
	if (jsDC == nullptr)
		return js->Throw(_T("Drawing operation is not valid now")), static_cast<void>(0);

	// if the path is relative, make it relative to the program folder
	const TCHAR *path = filename.c_str();
	TCHAR pathBuf[MAX_PATH];
	if (PathIsRelative(path))
	{
		GetDeployedFilePath(pathBuf, path, _T(""));
		path = pathBuf;
	}

	// adjust to from js interior coordinates to global coordinates
	x += jsDC->borderWidth;
	y += jsDC->borderWidth;

	// load the file
	std::unique_ptr<Gdiplus::Image> image(new Gdiplus::Image(path));
	if (image == nullptr)
		return js->Throw(_T("Unable to load image file")), static_cast<void>(0);

	// Figure the drawing width and height.  If both dimensions are unspecified,
	// use the native image size.  If one dimension is unspecified, figure the
	// unspecified dimension such that it preserves the image's native aspect
	// ratio given the specified dimension.  If both are specified, use the 
	// exact dimensions given.
	UINT imageWidth = image->GetWidth();
	UINT imageHeight = image->GetHeight();
	float drawWidth = static_cast<float>(imageWidth);
	float drawHeight = static_cast<float>(imageHeight);
	JsErrorCode err;
	if (widthVal != js->GetUndefVal() && heightVal != js->GetUndefVal())
	{
		// use the exact dimensions specified
		if ((err = js->ToFloat(drawWidth, widthVal)) != JsNoError || (err = js->ToFloat(drawHeight, heightVal)) != JsNoError)
			return js->Throw(err), static_cast<void>(0);
	}
	else if (widthVal != js->GetUndefVal())
	{
		// width was specified, height was not - use the width, and figure the
		// height that preserves the original image's aspect ratio at that width
		if ((err = js->ToFloat(drawWidth, widthVal)) != JsNoError)
			return js->Throw(err), static_cast<void>(0);

		if (imageWidth != 0)
			drawHeight *= drawWidth / static_cast<float>(imageWidth);
	}
	else if (heightVal != js->GetUndefVal())
	{
		// height was specified, width was not
		if ((err = js->ToFloat(drawHeight, heightVal)) != JsNoError)
			return js->Throw(err), static_cast<void>(0);

		if (imageHeight != 0)
			drawWidth *= drawHeight / static_cast<float>(imageHeight);
	}

	// draw the image
	jsDC->g.DrawImage(image.get(), Gdiplus::RectF(x, y, drawWidth, drawHeight), 
		0.0f, 0.0f, static_cast<float>(imageWidth), static_cast<float>(imageHeight),
		Gdiplus::Unit::UnitPixel);
}

JsValueRef PlayfieldView::JsDrawGetImageSize(TSTRING filename)
{
	// validate the drawing context
	auto js = JavascriptEngine::Get();
	if (jsDC == nullptr)
		return js->Throw(_T("Drawing operation is not valid now"));

	// if the path is relative, make it relative to the program folder
	const TCHAR *path = filename.c_str();
	TCHAR pathBuf[MAX_PATH];
	if (PathIsRelative(path))
	{
		GetDeployedFilePath(pathBuf, path, _T(""));
		path = pathBuf;
	}

	// get the image size
	ImageFileDesc desc;
	if (!GetImageFileInfo(path, desc))
		return js->Throw(_T("Image file can't be loaded"));

	// return an object with the width and height
	try
	{
		auto ret = JavascriptEngine::JsObj::CreateObject();
		ret.Set("width", desc.size.cx);
		ret.Set("height", desc.size.cy);
		return ret.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

void PlayfieldView::JsDrawSetTextArea(float x, float y, float width, float height)
{
	// validate the drawing context
	auto js = JavascriptEngine::Get();
	if (jsDC == nullptr)
		return js->Throw(_T("Drawing operation is not valid now")), static_cast<void>(0);

	// The Javascript caller works in terms of the area inside the borders,
	// whereas we work with the raw drawing surface including the borders,
	// so adjust the top left by the border width;
	x += jsDC->borderWidth;
	y += jsDC->borderWidth;

	// set the new text clip area
	jsDC->textBounds = Gdiplus::RectF(x, y, width, height);
}

void PlayfieldView::JsDrawSetTextOrigin(float x, float y)
{
	// validate the drawing context
	auto js = JavascriptEngine::Get();
	if (jsDC == nullptr)
		return js->Throw(_T("Drawing operation is not valid now")), static_cast<void>(0);

	// The Javascript caller works in terms of the area inside the borders,
	// whereas we work with the raw drawing surface including the borders,
	// so adjust the top left by the border width;
	x += jsDC->borderWidth;
	y += jsDC->borderWidth;

	// set the new origin
	jsDC->textOrigin = Gdiplus::PointF(x, y);
}

JsValueRef PlayfieldView::JsDrawGetTextOrigin()
{
	// validate the drawing context
	auto js = JavascriptEngine::Get();
	if (jsDC == nullptr)
		return js->Throw(_T("Drawing operation is not valid now"));

	// return the current text origin as an object with x,y properties
	try
	{
		auto ret = JavascriptEngine::JsObj::CreateObject();
		ret.Set("x", jsDC->textOrigin.X - jsDC->borderWidth);
		ret.Set("y", jsDC->textOrigin.Y - jsDC->borderWidth);
		return ret.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}
	
JsValueRef PlayfieldView::JsDrawMeasureText(TSTRING text)
{
	// validate the drawing context
	auto js = JavascriptEngine::Get();
	if (jsDC == nullptr)
		return js->Throw(_T("Drawing operation is not valid now"));

	// initialize drawing resources
	jsDC->InitFont();

	// measure the text
	Gdiplus::StringFormat f = Gdiplus::StringFormat::GenericTypographic();
	f.SetFormatFlags(f.GetFormatFlags() | Gdiplus::StringFormatFlags::StringFormatFlagsMeasureTrailingSpaces);
	Gdiplus::RectF bbox;
	jsDC->g.MeasureString(text.c_str(), static_cast<INT>(text.length()), jsDC->font.get(), jsDC->textOrigin, &f, &bbox);

	// return the bounding rectangle, adjusting from our global coordinates
	// to the interior of the border area
	try
	{
		auto ret = JavascriptEngine::JsObj::CreateObject();
		ret.Set("left", bbox.GetLeft() - jsDC->borderWidth);
		ret.Set("top", bbox.GetTop() - jsDC->borderWidth);
		ret.Set("right", bbox.GetRight() - jsDC->borderWidth);
		ret.Set("bottom", bbox.GetBottom() - jsDC->borderWidth);
		ret.Set("width", bbox.Width);
		ret.Set("height", bbox.Height);
		return ret.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

void PlayfieldView::JsDrawFillRect(float x, float y, float width, float height, int rgb)
{
	// validate the drawing context
	auto js = JavascriptEngine::Get();
	if (jsDC == nullptr)
		return js->Throw(_T("Drawing operation is not valid now")), static_cast<void>(0);

	// adjust from border-relative to surface-relative coordinates
	x += jsDC->borderWidth;
	y += jsDC->borderWidth;

	// create a brush
	Gdiplus::SolidBrush br(Gdiplus::Color(
		static_cast<BYTE>((rgb >> 16) & 0xff),
		static_cast<BYTE>((rgb >> 8) & 0xff),
		static_cast<BYTE>(rgb & 0xff)));

	// fill the rectangle
	jsDC->g.FillRectangle(&br, x, y, width, height);
}

void PlayfieldView::JsDrawFrameRect(float x, float y, float width, float height, float frameWidth, int rgb)
{
	// validate the drawing context
	auto js = JavascriptEngine::Get();
	if (jsDC == nullptr)
		return js->Throw(_T("Drawing operation is not valid now")), static_cast<void>(0);

	// adjust to from js interior coordinates to global coordinates
	x += jsDC->borderWidth;
	y += jsDC->borderWidth;

	// create a pen
	Gdiplus::Pen pen(Gdiplus::Color(
		static_cast<BYTE>((rgb >> 16) & 0xff),
		static_cast<BYTE>((rgb >> 8) & 0xff),
		static_cast<BYTE>(rgb & 0xff)), frameWidth);

	// draw the frame
	jsDC->g.DrawRectangle(&pen, x, y, width, height);
}

JsValueRef PlayfieldView::JsDrawGetSize()
{
	// validate the drawing context
	auto js = JavascriptEngine::Get();
	if (jsDC == nullptr)
		return js->Throw(_T("Drawing operation is not valid now"));

	// return an object with the width and height
	try
	{
		auto ret = JavascriptEngine::JsObj::CreateObject();
		ret.Set("width", jsDC->width - 2.0f*jsDC->borderWidth);
		ret.Set("height", jsDC->height - 2.0f*jsDC->borderWidth);
		return ret.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

void PlayfieldView::StartPopupAnimation(PopupType popupType, const WCHAR *popupName, bool opening, const PopupDesc *replaceTypes)
{
	// If we're opening a new popup, and there's an existing popup in the
	// 'replace types' list, skip the animation.
	if (opening && this->popupType != PopupNone)
	{
		// If there's a replace list, search it.  If there's no replace
		// list, the default is a list with the new popup type as the
		// sole entry.
		bool replacing = false;
		if (replaceTypes == nullptr)
		{
			// no explicit list - we can replace a popup of the same type only
			replacing = (popupType == this->popupType);
		}
		else
		{
			// search the provided list of replaceable types
			for (const PopupDesc *p = replaceTypes; p->type != PopupNone; ++p)
			{
				if (p->type == popupType
					&& (p->name == nullptr || popupName == nullptr || _wcsicmp(p->name, popupName) == 0))
				{
					replacing = true;
					break;
				}
			}
		}

		// If we're replacing an existing popup, skip the animation.
		if (replacing)
		{
			// Fire a Javascript event to signal that the old popup is being
			// closed.  This would normally happen at the end of the closing
			// animation, but the old popup doesn't get any closing animation,
			// since we're just doing a "cut" from one popup to the next.
			FirePopupEvent(false, this->popupName.c_str());

			// set the new popup type
			this->popupType = popupType;
			this->popupName = popupName;

			// we're done - skip the rest of the animation work
			return;
		}
	}

	// if showing a new popup, close any menu
	if (opening && curMenu != nullptr)
		StartMenuAnimation(false);

	// if we were previously showing an instruction card, remove it
	if (this->popupType == PopupInstructions && popupType != PopupInstructions)
		RemoveInstructionsCard();

	// remember the popup type
	this->popupType = popupType;
	if (popupName != nullptr)
		this->popupName = popupName;

	// start the animation timer
	StartAnimTimer(popupAnimStartTime);

	// check if we're opening a new sprite
	if (opening)
	{
		// if an info box is showing, hide it
		HideInfoBox();

		// hide the sprite initially
		popupSprite->alpha = 0.0f;

		// set opening mode
		popupAnimMode = PopupAnimOpen;
	}
	else
	{
		popupAnimMode = PopupAnimClose;
	}

	// update the javascript UI mode
	UpdateJsUIMode();
}

void PlayfieldView::ClosePopup()
{
	if (popupSprite != nullptr)
	{
		// if an instruction card is showing in the backglass or topper view, 
		// close it
		if (popupType == PopupInstructions)
			RemoveInstructionsCard();

		// for the audio volume popup, restore running media to the original volume
		if (popupType == PopupGameAudioVolume)
		{
			auto gl = GameList::Get();
			workingAudioVolume = gl->GetAudioVolume(gl->GetNthGame(0));
			ApplyWorkingAudioVolume();
		}

		// start the fade-out animation
		StartPopupAnimation(popupType, nullptr, false);
	}
}

void PlayfieldView::RemoveInstructionsCard()
{
	if (auto bg = Application::Get()->GetBackglassView(); bg != nullptr)
		bg->RemoveInstructionCard();
	if (auto t = Application::Get()->GetTopperView(); t != nullptr)
		t->RemoveInstructionCard();
}

// Draw the common portion of the Game Info and High Scores box.  This draws
// the background, outline, title string, and wheel image.  On return, 'gds'
// is filled in with the bounding box and starting position for additional
// text.
void PlayfieldView::DrawInfoBoxCommon(const GameListItem *game,
	Gdiplus::Graphics &g, int width, int height, float margin, GPDrawString &gds)
{
	// draw the background
	Gdiplus::SolidBrush bkgBr(Gdiplus::Color(0xd0, 0x00, 0x00, 0x00));
	g.FillRectangle(&bkgBr, 0, 0, width, height);

	// draw the border
	const int borderWidth = 2;
	Gdiplus::Pen pen(Gdiplus::Color(0xe0, 0xff, 0xff, 0xff), float(borderWidth));
	g.DrawRectangle(&pen, borderWidth / 2, borderWidth / 2, width - borderWidth, height - borderWidth);

	// initially allocate the entire box, minus the margins, for the title layout
	Gdiplus::RectF titleBox(margin, margin, (float)width - 2.0f*margin, (float)height - 2.0f*margin);

	// draw the game wheel icon at top right
	TSTRING wheelFile;
	Gdiplus::PointF pt(margin, margin);
	if (game->GetMediaItem(wheelFile, GameListItem::wheelImageType))
	{
		// load the image
		std::unique_ptr<Gdiplus::Bitmap> wheelBmp(Gdiplus::Bitmap::FromFile(wheelFile.c_str()));
		if (wheelBmp != nullptr)
		{
			// draw the image in the top right corner, using 1/3 of the box width
			const float imgWidth = (float)wheelBmp->GetWidth(), imgHeight = (float)wheelBmp->GetHeight();
			const float drawWidth = ((float)width - margin * 2.0f) * 1.0f / 3.0f;
			const float drawHeight = imgWidth != 0.0f ? drawWidth * (imgHeight / imgWidth) : drawWidth * .25f;
			Gdiplus::RectF wrc((float)width - margin - drawWidth, margin, drawWidth, drawHeight);
			g.DrawImage(wheelBmp.get(), wrc);

			// deduct the width from the title layout area
			titleBox.Width -= drawWidth - margin;
		}
	}

	// draw the title
	Gdiplus::SolidBrush textBr(Gdiplus::Color(0xFF, 0xFF, 0xFF, 0xFF));
	Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
	g.DrawString(game->title.c_str(), -1, popupTitleFont, titleBox, &fmt, &textBr);

	// measure the fit
	Gdiplus::RectF bbox;
	g.MeasureString(game->title.c_str(), -1, popupTitleFont, titleBox, &fmt, &bbox);
	if (bbox.GetBottom() > pt.Y)
		pt.Y = bbox.GetBottom();

	// set up the drawing area
	Gdiplus::RectF rcLayout(margin, pt.Y, (float)width - 2.0f*margin, (float)height - margin - pt.Y);
	gds.bbox = rcLayout;
	gds.curOrigin = { rcLayout.X, rcLayout.Y };
}

// create a font for drawing arrows
struct ArrowFont
{
	ArrowFont(int ptSize) : font(CreateGPFont(_T("Wingdings 3, Webdings"), ptSize, 400))
	{
		// determine which font we actually loaded
		Gdiplus::FontFamily family;
		font->GetFamily(&family);
		family.GetFamilyName(fontName);

		// set up the arrow characters based on the selected font
		if (_tcscmp(fontName, _T("Wingdings 3")) == 0)
		{
			menuArrowUp = _T("\x81");
			menuArrowDown = _T("\x82");
			menuArrowLeft = _T("\x74");
			menuArrowRight = _T("\x75");
			subMenuArrow = _T("\x7d");
		}
		else if (_tcscmp(fontName, _T("Webdings")) == 0)
		{
			menuArrowUp = _T("\x35");
			menuArrowDown = _T("\x36");
			menuArrowLeft = _T("\x33");
			menuArrowRight = subMenuArrow = _T("\x34");
		}
		else
		{
			menuArrowLeft = _T("\u02c2");
			menuArrowRight = subMenuArrow = _T("\u02c3");
			menuArrowUp = _T("\u02c4");
			menuArrowDown = _T("\u02c5");
		}
	}

	Gdiplus::Font *get() { return font.get(); }

	std::unique_ptr<Gdiplus::Font> font;
	TCHAR fontName[LF_FACESIZE];

	const TCHAR *menuArrowUp, *menuArrowDown, *menuArrowLeft, *menuArrowRight;
	TCHAR *subMenuArrow;
};

void PlayfieldView::ShowGameInfo()
{
	// fire an event first, abort on cancel
	const WCHAR *popupName = L"game info";
	if (!FirePopupEvent(true, popupName))
		return;

	// ignore it if there's no game selection
	GameList *gl = GameList::Get();
	GameListItem *game = gl->GetNthGame(0);
	if (!IsGameValid(game))
		return;

	// Request high score information for this game.  This is an
	// asynchronous operation; we'll rebuild the sprite with the
	// high score data when we receive it.
	RequestHighScores(game, true);

	// info box drawing function
	int width = 972, height = 2000;
	int pass = 1;
	auto Draw = [gl, game, this, width, &height, &pass](HDC hdc, HBITMAP)
	{
		// set up a GDI+ drawing context
		Gdiplus::Graphics g(hdc);

		// margin for our content area
		const float margin = 16.0f;

		// draw the common area
		GPDrawString gds(g);
		DrawInfoBoxCommon(game, g, width, height, margin, gds);

		// set up fonts and colors
		Gdiplus::SolidBrush textBr(Gdiplus::Color(0xFF, 0xFF, 0xFF, 0xFF));
		FontPref &textFont = popupFont;
		FontPref &smallerTextFont = popupSmallerFont;
		FontPref &detailsFont = popupDetailFont;
		std::unique_ptr<Gdiplus::Font> symFont(CreateGPFont(_T("Wingdings"), detailsFont.ptSize, 400));
		ArrowFont arrowFont(18);
		
		//
		// Main bibliographic details section
		//

		// Manufacturer and/or year - "Manufacturer, Year" or just one or the other.
		// Draw this in the left 2/3 minus a little margin, to avoid overlapping the
		// wheel icon at upper right.
		auto origWidth = gds.bbox.Width;
		gds.bbox.Width = gds.bbox.Width*2/3 - 16;
		if (game->manufacturer != nullptr)
			gds.DrawString(MsgFmt(_T("%s, %d"), game->manufacturer->manufacturer.c_str(), game->year), textFont, &textBr);
		else if (game->year != 0)
			gds.DrawString(MsgFmt(_T("%d"), game->year), textFont, &textBr);

		// restore the bounding box
		gds.bbox.Width = origWidth;

		// table type
		if (auto tt = tableTypeNameMap.find(game->tableType); tt != tableTypeNameMap.end())
			gds.DrawString(tt->second.c_str(), smallerTextFont, &textBr);

		// system
		if (game->system != nullptr)
			gds.DrawString(game->system->displayName.c_str(), smallerTextFont, &textBr);

		// show the personal rating
		float rating = gl->GetRating(game);
		if (stars != nullptr && rating >= 0.0f)
		{
			// note the starting Y offset
			float y0 = gds.curOrigin.Y;

			// get the font metrics
			LOGFONTW lf;
			detailsFont->GetLogFontW(&g, &lf);

			// Figure the star position.  Draw at the current position,
			// unless the font is taller than the star graphics, in which
			// case push the stars down by the difference so that they
			// have the same baseline.
			float starHt = (float)stars->GetHeight();
			float dh = fabsf((float)lf.lfHeight) - starHt;
			float yStars = gds.curOrigin.Y + fmaxf(0.0f, dh);

			// draw the stars and skip past them horizontally
			DrawStars(g, gds.curOrigin.X, yStars, rating);
			gds.curOrigin.X += (float)stars->GetWidth() / 3.0f*5.0f + 16.0f;

			// Draw the text version of the rating.  If the star graphics are 
			// taller than the font, advance by the difference, so that the text
			// baseline matches the star graphics baseline.
			gds.curOrigin.Y += fmaxf(0.0f, -dh);
			gds.DrawString(MsgFmt(_T("(%s)"), StarsAsText(rating).c_str()), detailsFont, &textBr, true);

			// make sure we moved past the stars vertically
			gds.curOrigin.Y = fmaxf(y0 + starHt, gds.curOrigin.Y);
		}

		//
		// Statistics section
		//

		// add the play date/count/time statistics, if it's ever been played
		gds.VertSpace(16.0f);
		int playCount = gl->GetPlayCount(game);
		if (playCount != 0)
		{
			// show the date/time of last run
			DateTime d(gl->GetLastPlayed(game));
			if (d.IsValid())
			{
				gds.DrawString(MsgFmt(IDS_LAST_PLAYED_DATE, d.FormatLocalDateTime(DATE_LONGDATE, TIME_NOSECONDS).c_str()),
					detailsFont, &textBr);
			}
			else
				gds.DrawString(LoadStringT(IDS_LAST_PLAYED_NEVER).c_str(), detailsFont, &textBr);

			// add the number of times played
			gds.DrawString(MsgFmt(IDS_TIMES_PLAYED, playCount), detailsFont, &textBr);

			// add the total play time
			gds.DrawString(MsgFmt(IDS_TOTAL_PLAY_TIME, PlayTimeAsText(gl->GetPlayTime(game)).c_str()), detailsFont, &textBr);
		}
		else
		{
			// never played - just say so, without all of the zeroed statistics
			gds.DrawString(LoadStringT(IDS_LAST_PLAYED_NEVER).c_str(), detailsFont, &textBr);
		}

		// mention if it's in the favorites
		if (gl->IsFavorite(game))
			gds.DrawString(LoadStringT(IDS_GAMEINFO_FAV), detailsFont, &textBr);

		//
		// Technical details section
		//
		gds.VertSpace(16.0f);
		Gdiplus::SolidBrush detailsBr(Gdiplus::Color(0xff, 0xA0, 0xA0, 0xA0));

		// date added
		if (DateTime dateAdded = gl->GetDateAdded(game); dateAdded.IsValid())
			gds.DrawString(MsgFmt(IDS_DATE_ADDED, dateAdded.FormatLocalDate().c_str()), detailsFont, &detailsBr);

		// add the game file, if present
		if (game->filename.length() != 0)
			gds.DrawString(MsgFmt(IDS_GAMEINFO_FILENAME, game->filename.c_str()), detailsFont, &detailsBr);

		// add the media file name
		if (game->mediaName.length() != 0)
			gds.DrawString(MsgFmt(IDS_GAMEINFO_MEDIANAME, game->mediaName.c_str()), detailsFont, &detailsBr);

		// add the DOF ROM, if present
		if (auto dofClient = DOFClient::Get(); dofClient != nullptr && DOFClient::IsReady())
		{
			if (const WCHAR *rom = dofClient->GetRomForTable(game); rom != 0 && rom[0] != 0)
				gds.DrawString(MsgFmt(IDS_GAMEINFO_DOF_ROM, rom), detailsFont, &detailsBr);
		}

		// add the NVRAM file, if present
		TSTRING nvramPath, nvramFile;
		if (Application::Get()->highScores->GetNvramFile(nvramPath, nvramFile, game))
			gds.DrawString(MsgFmt(IDS_GAMEINFO_NVRAM, nvramFile.c_str()), detailsFont, &detailsBr);

		// if we have high scores, add a navigation hint at the bottom right
		if (game->highScores.size() != 0)
		{
			// add some vertical spacing
			gds.curOrigin.Y += margin * 2;

			// measure the strings, so we can align them at the right
			TSTRING hs = LoadStringT(IDS_MENU_HIGH_SCORES);
			Gdiplus::RectF bbox1, bbox2;
			g.MeasureString(hs.c_str(), -1, detailsFont, Gdiplus::PointF(0, 0), &bbox1);
			g.MeasureString(arrowFont.menuArrowRight, -1, arrowFont.font.get(), Gdiplus::PointF(0, 0), &bbox2);

			// if this is the second pass, bottom-justify the nav hints (don't do this
			// on the first pass, since we're just measuring the space we need; we can't
			// bottom-justify when we don't know the final height yet)
			if (pass > 1)
				gds.curOrigin.Y = height - margin - max(bbox1.Height, bbox2.Height);

			// draw them
			gds.curOrigin.X = width - margin - bbox1.Width - bbox2.Width;
			gds.DrawString(hs.c_str(), detailsFont, &textBr, false);
			gds.curOrigin.Y += (bbox1.Height - bbox2.Height) / 2.0f;
			gds.DrawString(arrowFont.menuArrowRight, arrowFont.get(), &textBr);
		}

		// set the final height that we actually used, and count the drawing pass
		height = (int)(gds.curOrigin.Y + margin);
		++pass;

		// close out the drawing context
		g.Flush();
	};

	// Draw the info box to a dummy context first, to measure the height.
	// This will set height to the actual height used.
	MemoryDC memdc;
	Draw(memdc, NULL);

	// set a minimum height
	height = max(500, height);

	// If a high scores popup is showing, we must be switching "pages"
	// between high scores and game info.  Try to keep the popup size the
	// same by using the existing popup's height as a minimum height for
	// the new one.
	if (popupType == PopupHighScores)
		height = max(height, (int)(popupSprite->loadSize.y * 1920.0f));

	// Set up the info box at the computed height
	Application::InUiErrorHandler eh;
	popupSprite.Attach(new Sprite());
	if (!popupSprite->Load(width, height, Draw, eh, _T("Game Info box")))
	{
		popupSprite = nullptr;
		UpdateDrawingList();
		ShowQueuedError();
		return;
	}

	// adjust it to the canonical popup position
	AdjustSpritePosition(popupSprite);

	// Start the animation.  We can do a direct switch between Game Info
	// and High Scores without animation.
	static const PopupDesc replaceTypes[] = { 
		{ PopupGameInfo },
		{ PopupHighScores },
		{ PopupNone }
	};
	StartPopupAnimation(PopupGameInfo, popupName, true, replaceTypes);

	// put the new sprite in the drawing list
	UpdateDrawingList();

	// Signal a Game Information event in DOF
	QueueDOFPulse(L"PBYGameInfo");
}

void PlayfieldView::ShowHighScores()
{
	// fire an event first, abort on cancel
	const WCHAR *popupName = L"high scores";
	if (!FirePopupEvent(true, popupName))
		return;

	// ignore it if there's no game selection
	GameList *gl = GameList::Get();
	GameListItem *game = gl->GetNthGame(0);
	if (!IsGameValid(game))
		return;

	// Request high score information for this game.  This is an
	// asynchronous operation; we'll rebuild the sprite with the
	// high score data when we receive it.
	RequestHighScores(game, true);

	// set up the default font
	int textFontPts = highScoreFont.ptSize;
	std::unique_ptr<Gdiplus::Font> textFont(CreateGPFont(highScoreFont.family.c_str(), textFontPts, highScoreFont.weight));

	// drawing function
	int width = 972, height = 2000;
	int pass = 1;
	auto Draw = [gl, game, this, width, &height, &pass, &textFont](HDC hdc, HBITMAP)
	{
		// set up a GDI+ drawing context
		Gdiplus::Graphics g(hdc);

		// margin for our content area
		const float margin = 16.0f;

		// draw the common area
		GPDrawString gds(g);
		DrawInfoBoxCommon(game, g, width, height, margin, gds);

		// write the high score information
		Gdiplus::SolidBrush textBr(Gdiplus::Color(0xFF, 0xFF, 0xFF, 0xFF));
		for (auto const &txt : game->highScores)
			gds.DrawString(txt.length() == 0 ? _T(" ") : txt.c_str(), textFont.get(), &textBr);

		// add some vertical whitespace
		gds.curOrigin.Y += margin * 2;

		// measure the "back to info box" navigation hint text
		FontPref &linkFont = popupDetailFont;
		ArrowFont arrowFont(linkFont.ptSize);
		TSTRING info = LoadStringT(IDS_MENU_INFO);
		const TCHAR *arrow = arrowFont.menuArrowLeft;
		Gdiplus::RectF bbox1, bbox2;
		g.MeasureString(arrow, -1, arrowFont.get(), Gdiplus::PointF(0, 0), &bbox1);
		g.MeasureString(info.c_str(), -1, linkFont, Gdiplus::PointF(0, 0), &bbox2);

		// if this is the second pass, bottom-justify the nav hints (don't do this
		// on the first pass, since we're just measuring the space we need; we can't
		// bottom-justify when we don't know the final height yet)
		if (pass > 1)
			gds.curOrigin.Y = height - margin - max(bbox1.Height, bbox2.Height);

		// draw them
		gds.DrawString(arrow, arrowFont.get(), &textBr, false);
		gds.curOrigin.Y += (bbox1.Height - bbox2.Height) / 2.0f;
		gds.DrawString(info.c_str(), linkFont, &textBr);

		// set the final height and count the drawing pass
		height = (int)(gds.curOrigin.Y + margin);

		// close out the drawing context
		g.Flush();
	};

	// Keep going until we find a font size that makes all of the text fit.
	// Some games (I'm looking at you, Medieval Madness) have insanely long
	// high score lists that need to be squeezed a bit to fit a reasonable
	// window height.
	while (textFontPts > 12)
	{
		// Draw it to a dummy DC to figure the required height.  This will
		// adjust 'height' to the actual required height.
		MemoryDC memdc;
		Draw(memdc, NULL);

		// if it fits 80% of our reference window height, it's a fit
		if (height < 1536)
			break;

		// reduce the font size slightly and try again
		textFontPts -= 4;
		textFont.reset(CreateGPFont(highScoreFont.family.c_str(), textFontPts, highScoreFont.weight));
	}

	// set a minimum height, so that the box doesn't look too squat for
	// older games with one-liner high scores
	height = max(500, height);

	// If there's a game info popup currently showing, we must be switching
	// "pages" between game info and high scores, so use the height of the
	// existing popup as the minimum for the new popup.  This makes for a
	// slightly smoother presentation if we're switching back and forth
	// between the two, by keeping the outline size the same and just
	// updating the contents, like two pages of a multi-tab dialog.
	if (popupType == PopupGameInfo)
		height = max(height, (int)(popupSprite->loadSize.y * 1920.0f));

	// Now create the sprite and draw it for real at the final height
	++pass;
	Application::InUiErrorHandler eh;
	popupSprite.Attach(new Sprite());
	if (!popupSprite->Load(width, height, Draw, eh, _T("High Scores box")))
	{
		popupSprite = 0;
		UpdateDrawingList();
		ShowQueuedError();
		return;
	}

	// adjust it to the canonical popup position
	AdjustSpritePosition(popupSprite);

	// Start the animation.  We can do a direct switch between high scores
	// and Game Info with no animation.
	static const PopupDesc replaceTypes[] = { 
		{ PopupHighScores },
		{ PopupGameInfo },
		{ PopupNone }
	};
	StartPopupAnimation(PopupHighScores, popupName, true, replaceTypes);

	// put the new sprite in the drawing list
	UpdateDrawingList();

	// Signal a Game Information event in DOF
	QueueDOFPulse(L"PBYHighScores");
}

bool PlayfieldView::FireHighScoresRequestEvent(GameListItem *game)
{
	bool ret = true;
	if (auto js = JavascriptEngine::Get(); js != nullptr)
		ret = js->FireEvent(jsGameList, jsHighScoresRequestEvent, BuildJsGameInfo(game));

	return ret;
}

void PlayfieldView::FireHighScoresReadyEvent(GameListItem *game, bool success, const TCHAR *source)
{
	if (auto js = JavascriptEngine::Get(); js != nullptr)
	{
		try
		{
			// build an array of strings from the string list
			auto arr = JavascriptEngine::JsObj::CreateArray();
			for (auto &s : game->highScores)
				arr.Push(s);

			// fire the event
			js->FireEvent(jsGameList, jsHighScoresReadyEvent, BuildJsGameInfo(game), success, arr, source);
		}
		catch (JavascriptEngine::CallException exc)
		{
			exc.Log(_T("High scores ready event"));
		}
	}
}

void PlayfieldView::RequestHighScores(GameListItem *game, bool notifyJavascript)
{
	// If the current game is valid, and we don't already have the
	// high scores for it, send the request.
	if (IsGameValid(game))
	{
		switch (game->highScoreStatus)
		{
		case GameListItem::HighScoreStatus::Init:
			// If desired, fire a Javascript event for the high score request
			if (notifyJavascript)
			{
				// try invoking the javascript event
				if (!FireHighScoresRequestEvent(game))
				{
					// Javascript canceled the event, so don't request high
					// scores.  If the game is still in the "Init" state, set
					// it to the "Failed" state.  Otherwise, leave it in its
					// current state, as the javascript code must have explicitly
					// set the scores in the course of handling the event.
					if (game->highScoreStatus == GameListItem::HighScoreStatus::Init)
					{
						// treat it as a failure
						game->highScoreStatus = GameListItem::HighScoreStatus::Failed;
						OnHighScoresReady(game->internalID, false, L"javascript");
					}

					// we're done with the high score request
					break;
				}
			}

			// Not yet requested.  Request the high scores if the high score system
			// is ready.  If not, do nothing; the caller will either queue the request
			// to the highScoresReadyList, or in the case of anything involving the
			// current wheel selection, the caller will rely on the fact that we 
			// automatically load high scores for the current game as soon as we get
			// notification that the high score system is ready.
			if (hiScoreSysReady)
			{
				// request the high scores for the game
				if (Application::Get()->highScores->GetScores(game, hWnd, new HighScoreRequestContext(notifyJavascript)))
				{
					// note that the request has been sent in the game object
					game->highScoreStatus = GameListItem::HighScoreStatus::Requested;
				}
				else
				{
					// request failed - reject any outstanding callbacks
					game->highScoreStatus = GameListItem::HighScoreStatus::Failed;
					OnHighScoresReady(game->internalID, false, L"pinemhi");

					// We didn't initiate an asynchronous request for this game's data,
					// so there will be no further reply.  Fire the js failure event now
					// if this is a notifiable request.
					if (notifyJavascript)
						FireHighScoresReadyEvent(game, false, L"pinemhi");
				}
			}
			break;

		case GameListItem::HighScoreStatus::Requested:
			// request already sent - no need to repeat
			break;

		case GameListItem::HighScoreStatus::Received:
			// request already completed - immediately fulfill any outstanding callbacks
			OnHighScoresReady(game->internalID, true, L"cache");
			break;

		case GameListItem::HighScoreStatus::Failed:
			// request already completed with error - reject any outstanding callbacks
			OnHighScoresReady(game->internalID, false, L"cache");
			break;
		}
	}
}

void PlayfieldView::ReceiveHighScores(const HighScores::NotifyInfo *ni)
{
	switch (ni->queryType)
	{
	case HighScores::Initialized:
		// the high scores system has finished initializing - flag it
		hiScoreSysReady = true;

		// If there's anything in the high score request queue, send a request
		// for each item now.  We need to do this with a separate list of the
		// game IDs, since the original list can be modified in place by
		// RequestHighScores().
		{
			// build a safe copy of the list
			std::vector<LONG> ids;
			ids.reserve(highScoresReadyList.size());
			for (auto &c : highScoresReadyList)
				ids.push_back(c->gameID);

			// Request high scores for each list entry.  Don't notify Javascript,
			// as whoever added this item to the list in the first place should
			// have notified Javascript at that point.
			for (auto id : ids)
				RequestHighScores(GameList::Get()->GetByInternalID(id), false);
		}

		// Request high scores for the current game, now that it's possible
		RequestHighScores(GameList::Get()->GetNthGame(0), true);
		break;

	case HighScores::ProgramVersionQuery:
		// if the query was successful, find the version number string
		// in the results and store it away for About Box use
		if (ni->status == HighScores::NotifyInfo::Success)
		{
			std::basic_regex<TCHAR> verPat(_T("\\bversion\\s+([\\d.]+)"), std::regex_constants::icase);
			std::match_results<const TCHAR *> m;
			if (std::regex_search(ni->results.c_str(), m, verPat))
				pinEmHiVersion = m[1].str();
		}
		break;

	case HighScores::HighScoreQuery:
		// High score query results.  
		{
			// note the result status
			bool success = ni->status == HighScores::NotifyInfo::Success;

			// Retrieve the game object from the ID.  Proceed only if it's 
			// valid; game objects can be deleted, so the one from the original 
			// request might have been deleted since the time we sent the request.
			if (auto game = GameList::Get()->GetByInternalID(ni->gameID); game != nullptr)
			{
				// note the old status
				auto oldStatus = game->highScoreStatus;

				// update the game's high score status
				game->highScoreStatus = (ni->status == HighScores::NotifyInfo::Success ?
					GameListItem::HighScoreStatus::Received : GameListItem::HighScoreStatus::Failed);

				// If the reply was successful, and we haven't already set high
				// scores in this game, update the game with the new high score 
				// data from the reply.  Ignore the new data if we already have
				// high scores, since Javascript might have intervened while the
				// request was outstanding.
				if (success && oldStatus != GameListItem::HighScoreStatus::Received)
					ApplyHighScores(game, ni->results.c_str());

				// fire the High Scores Ready event
				if (auto ctx = dynamic_cast<HighScoreRequestContext*>(ni->context); ctx == nullptr || ctx->notifyJavascript)
					FireHighScoresReadyEvent(game, success, L"pinemhi");
			}

			// update any notification callbacks
			OnHighScoresReady(ni->gameID, success, L"pinemhi");
		}
		break;
	}
}

void PlayfieldView::ApplyHighScores(GameListItem *game, const TCHAR *scores)
{
	// note whether or not we had any score data previously
	bool hadScores = game->highScores.size() != 0;

	// clear any previous high score data
	game->highScores.clear();

	// break the new text into lines and populate the list
	const TCHAR *start = scores;
	for (;;)
	{
		// find the end of this line
		const TCHAR *p = start;
		for (; *p != 0 && *p != '\n' && *p != '\r'; ++p);

		// add this line to the list
		game->highScores.emplace_back(start, p - start);

		// if this is the last line, we're done
		if (*p == 0)
			break;

		// skip newline sequences - single \n or \r, or \n\r or \r\n pairs
		if ((*p == '\n' && *(p + 1) == '\r') || (*p == '\r' && *(p + 1) == '\n'))
			++p;
		++p;

		// this is the start of the next line
		start = p;
	}

	// apply the scores
	ApplyHighScores(game, hadScores);
}

void PlayfieldView::ApplyHighScores(GameListItem *game, bool hadScores)
{
	// if this is the current game, update some display items
	if (game == GameList::Get()->GetNthGame(0))
	{
		// If we didn't have high scores previously and we do now,
		// and we're displaying the game info popup, update it to
		// reflect that we now have high scores.
		if (!hadScores && game->highScores.size() != 0 && popupType == PopupGameInfo)
			ShowGameInfo();

		// If we're currently displaying the high scores popup, show 
		// it again to update it with the new data.
		if (popupType == PopupHighScores)
			ShowHighScores();

		// notify the DMD window of the update
		if (auto dv = Application::Get()->GetDMDView(); dv != nullptr)
			dv->OnUpdateHighScores(game);

		// notify the real DMD of the update
		if (realDMD != nullptr)
			realDMD->OnUpdateHighScores(game);
	}
}

void PlayfieldView::OnHighScoresReady(LONG gameID, bool success, const WCHAR *source)
{
	// notify any callbacks waiting for this game's data
	for (auto it = highScoresReadyList.begin(); it != highScoresReadyList.end(); )
	{
		// get the next item, in case we delete this one
		auto nxt = it;
		nxt++;

		// if this item matches the game, invoke its callback
		if (it->get()->gameID == gameID)
		{
			// invoke the callback
			it->get()->Ready(success, source);

			// we're done with this item now
			highScoresReadyList.erase(it);
		}

		// move on to the next item
		it = nxt;
	}
}

void PlayfieldView::AskPowerOff()
{
	// show the confirmation menu
	std::list<MenuItemDesc> md;
	md.emplace_back(LoadStringT(IDS_MENU_SHUTDOWN_CONFIRM), ID_SHUTDOWN_CONFIRM);
	md.emplace_back(LoadStringT(IDS_MENU_SHUTDOWN_CANCEL), ID_MENU_RETURN, MenuSelected);
	ShowMenu(md, L"power off", SHOWMENU_IS_EXIT_MENU);
}

void PlayfieldView::PowerOff()
{
	// We need SeShutdownName privileges to do this.  Get our security
	// token so that we can request additional privileges.
	HandleHolder hToken;
	BOOL ok = OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, TRUE, &hToken);
	DWORD err;
	if (!ok)
	{
		err = GetLastError();
		if (err == ERROR_NO_TOKEN && ImpersonateSelf(SecurityImpersonation))
		{
			ok = OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, TRUE, &hToken);
			if (!ok)
				err = GetLastError();
		}
	}

	if (!ok)
	{
		WindowsErrorMessage winErr(err);
		ShowError(EIT_Error, MsgFmt(IDS_ERR_SHUTDN_TOKEN, err, winErr.Get()));
		return;
	}

	// get the local ID for SeShutdownName
	LUID luid;
	if (!LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &luid))
	{
		WindowsErrorMessage winErr;
		ShowError(EIT_Error, MsgFmt(IDS_ERR_SHUTDN_PRIVLK, (long)winErr.GetCode(), winErr.Get()));
		return;
	}

	// enable the privilege
	TOKEN_PRIVILEGES tp;
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL))
	{
		WindowsErrorMessage winErr;
		ShowError(EIT_Error, MsgFmt(IDS_ERR_SHUTDN_PRIVADJ, (long)winErr.GetCode(), winErr.Get()));
		return;
	}

	// Figure the shutdown mode.  We want to shut down windows and then power
	// off the system if possible.  If we're on Windows 8 or later, we can add
	// "hybrid shutdown" mode to make the next boot faster.
	UINT shutdownMode = EWX_POWEROFF | EWX_SHUTDOWN;
	if (IsWindows8OrGreater())
		shutdownMode |= EWX_HYBRID_SHUTDOWN;

	// privileges elevated successfully - request the shutdown
	if (!ExitWindowsEx(shutdownMode, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED))
	{
		WindowsErrorMessage winErr;
		ShowError(EIT_Error, MsgFmt(IDS_ERR_SHUTDN_FAILED, (long)winErr.GetCode(), winErr.Get()));
		return;
	}

	// shutdown initiated - exit the program immediately
	::SendMessage(GetParent(hWnd), WM_CLOSE, 0, 0);
}

void PlayfieldView::ProcessKeyQueue()
{
	// process keys until the queue is empty or we enter animation mode
	while (keyQueue.size() != 0)
	{
		// if we're in an animation, defer until done
		if (isAnimTimerRunning)
			return;

		// pull the head item from the list
		QueuedKey key = keyQueue.front();
		keyQueue.pop_front();

		// Run it through the Javascript handler.  Skip this if it came from
		// a script in the first place.
		if (key.scripted || FireCommandButtonEvent(key))
		{
			// process the command
			(this->*key.cmd->func)(key);
		}

		// this counts as a key event for attract mode idle purposes
		attractMode.OnKeyEvent(this);
	}

	// sync the playfield when idle
	SetTimer(hWnd, pfTimerID, 100, 0);
}

bool PlayfieldView::IsGameValid(const GameListItem *game)
{
	return game != nullptr && game != GameList::Get()->noGame.get();
}

void PlayfieldView::OnGameListRebuild()
{
	UpdateSelection();
}

void PlayfieldView::UpdateSelection()
{
	// Get the current selection
	GameListItem *curGame = GameList::Get()->GetNthGame(0);

	// fire the javascript game selection event
	FireGameSelectEvent(curGame);

	// get the playfield image into the incoming item
	LoadIncomingPlayfieldMedia(curGame);

	// Load the adjacent wheel images
	wheelImages.clear();
	animAddedToWheel = 0;
	animFirstInWheel = -2;
	for (int i = -2; i <= 2; ++i)
	{
		// look up the wheel image media
		Sprite *s = LoadWheelImage(GameList::Get()->GetNthGame(i));

		// add it to the wheel image list
		wheelImages.emplace_back(s);

		// set its initial position
		SetWheelImagePos(s, i, 0.0f);
	}

	// refresh the sprite list with the new wheel images
	UpdateDrawingList();
}

void PlayfieldView::LoadIncomingPlayfieldMedia(GameListItem *game)
{
	// If this game is already loaded in the incoming playfield,
	// do nothing.
	if (game == incomingPlayfield.game)
		return;

	// we haven't loaded anything yet
	bool ok = false;

	// remember the new game
	incomingPlayfield.game = game;

	// if there's a game, try loading its playfield media
	Application::InUiErrorHandler uieh;
	TSTRING video, image, audio;
	bool videosEnabled = Application::Get()->IsEnableVideo();
	int volumePct = 100;
	if (IsGameValid(game))
	{
		// Retrieve the playfield video path and static image path.
		// We'll use the video if present and fall back on the static
		// image if it's not available or we can't load it.
		if (videosEnabled)
			game->GetMediaItem(video, GameListItem::playfieldVideoType);
		game->GetMediaItem(image, GameListItem::playfieldImageType);

		// look for an audio track for the table
		game->GetMediaItem(audio, GameListItem::playfieldAudioType);

		// note the new game's audio volume level in the settings
		volumePct = GameList::Get()->GetAudioVolume(game);
	}

	// combine the game-specific volume with the global video volume level
	volumePct = volumePct * Application::Get()->GetVideoVolume() / 100;

	// If the outgoing game has a database record but is still marked
	// as unconfigured, the game details must have been added just now.
	// The nominally unconfigured status lasts as long as it's the
	// current selection, even after entering the details, so that it
	// doesn't abruptly vanish from the UI even if the current filter 
	// hides unconfigured games, and so that the "Game Setup" command
	// doesn't abruptly disappear from the main menu.  But as soon as
	// we switch to a new game by whatever means, that special temporary
	// UI status no longer applies, so we want its "configured" flag to
	// match its true status.
	if (auto oldGame = currentPlayfield.game;
		IsGameValid(oldGame) && !oldGame->isConfigured && oldGame->dbFile != nullptr)
		oldGame->isConfigured = true;

	// stop any previous playfield audio
	if (currentPlayfield.audio != nullptr)
		currentPlayfield.audio->Stop(SilentErrorHandler());

	// Load the audio
	if (audio.length() != 0)
	{
		incomingPlayfield.audio.Attach(new DShowAudioPlayer(hWnd));
		if (incomingPlayfield.audio->Open(audio.c_str(), uieh))
		{
			// set the game-specific media volume level
			incomingPlayfield.audio->SetVolume(volumePct);

			// set the muting mode to match playfield video
			if (Application::Get()->IsMuteVideos())
				incomingPlayfield.audio->Mute(true);

			// start playback
			incomingPlayfield.audio->SetLooping(true);
			incomingPlayfield.audio->Play(uieh);
		}
	}

	// If there's no incoming game, and the new media file matches the media
	// file for the current sprite, leave the current one as-is.  This can
	// happen when both the current and incoming games use the same default
	// background.  Only bother with this in the case of video; for images,
	// a re-load won't be visible.  With videos, a re-load would start the
	// video over from the beginning, so it's nicer to leave it running
	// uninterrupted.
	bool isSameVideo = false;
	if (videosEnabled
		&& incomingPlayfield.sprite == nullptr
		&& currentPlayfield.sprite != nullptr
		&& currentPlayfield.sprite->GetVideoPlayer() != nullptr)
	{
		// get the current video path
		if (const TCHAR *oldPath = currentPlayfield.sprite->GetVideoPlayer()->GetMediaPath(); oldPath != nullptr)
		{
			// figure out which new video we're going to use
			const TCHAR *newPath = nullptr;
			TCHAR defaultVideo[MAX_PATH];
			if (video.length() != 0)
				newPath = video.c_str();
			else if (image.length() != 0)
				newPath = nullptr;  // we're using the image, not a video
			else if (GameList::Get()->FindGlobalVideoFile(defaultVideo, _T("Videos"), _T("Default Playfield")))
				newPath = defaultVideo;

			// check if they're the same
			if (newPath != nullptr && _tcsicmp(newPath, oldPath) == 0)
				isSameVideo = true;
		}
	}

	// if we're not staying with the same video, load the new one
	if (!isSameVideo)
	{
		// Asynchronous loader function
		HWND hWnd = this->hWnd;
		SIZE szLayout = this->szLayout;
		auto load = [hWnd, video, image, szLayout, videosEnabled, volumePct](VideoSprite *sprite)
		{
			// nothing loaded yet
			bool ok = false;

			// initialize it to fully transparent so we can cross-fade into it
			sprite->alpha = 0;

			// First try loading a playfield video.  Load it at the full window
			// height (1.0) and width.  We'll scale the video when we get its format.
			Application::AsyncErrorHandler eh;
			if (video.length() != 0
				&& sprite->LoadVideo(video, hWnd, { 1.0f, 1.0f }, eh, _T("Playfield Video"), true, volumePct))
				ok = true;

			// If there's no video, try a static image
			auto LoadImage = [szLayout, &sprite, &eh](const TCHAR *path)
			{
				// Get the image's native size, and figure the aspect
				// ratio.  Playfield images are always stored "sideways",
				// so the nominal width is the display height.  We display
				// playfield images at 1.0 times the viewport height, so
				// we just need to figure the relative width.
				ImageFileDesc imageDesc;
				GetImageFileInfo(path, imageDesc, true);
				float cx = imageDesc.dispSize.cx != 0 ? float(imageDesc.dispSize.cy) / float(imageDesc.dispSize.cx) : 0.5f;
				POINTF normSize = { 1.0f, cx };

				// figure the corresponding pixel size
				SIZE pixSize = { (int)(normSize.y * szLayout.cy), (int)(normSize.x * szLayout.cx) };

				// load the image into a new sprite
				return sprite->Load(path, normSize, pixSize, eh);
			};
			if (!ok && image.length() != 0)
				ok = LoadImage(image.c_str());

			// if we didn't find any media to load, and videos are enabled, try the
			// default playfield video
			TCHAR defaultVideo[MAX_PATH];
			if (!ok && videosEnabled && GameList::Get()->FindGlobalVideoFile(defaultVideo, _T("Videos"), _T("Default Playfield")))
				ok = sprite->LoadVideo(defaultVideo, hWnd, { 1.0f, 1.0f }, eh, _T("Playfield Default Video"), true, volumePct);

			// if we *still* didn't find anything, try the default playfield image
			TCHAR defaultImage[MAX_PATH];
			if (!ok && GameList::Get()->FindGlobalImageFile(defaultImage, _T("Images"), _T("Default Playfield")))
				ok = LoadImage(defaultImage);

			// HyperPin/PBX playfield images are oriented sideways, with the bottom at
			// the left.  Rotate 90 degrees counter-clockwise to orient it vertically.
			// The actual display will of course orient it according to the camera
			// view, but it makes things easier to think about if we orient all
			// graphics the "normal" way internally.  (Note that CCW is positive on
			// the Z axis, since D3D coordinates are left-handed.)
			sprite->rotation.z = XM_PI / 2.0f;
			sprite->UpdateWorld();
		};

		// Asynchronous loader completion
		auto done = [this](VideoSprite *sprite) { IncomingPlayfieldMediaDone(sprite); };

		// Kick off the asynchronous load
		playfieldLoader.AsyncLoad(false, load, done);
	}

	// update the status line text, in case it mentions the current game selection
	UpdateAllStatusText();

	// if desired, notify PinVol (it it's running) when we change the wheel selection
	if constexpr (NOTIFY_PINVOL_ON_WHEEL_SELECTION)
	{
		if (IsGameValid(game))
			Application::Get()->SendPinVol(L"PinballY Select %s\n%s", game->GetGameId().c_str(), game->title.c_str());
		else
			Application::Get()->SendPinVol(L"PinballY SelectNone");
	}

	// request high scores if we don't already have them
	RequestHighScores(game, true);
}

void PlayfieldView::MuteTableAudio(bool mute)
{
	if (incomingPlayfield.audio != nullptr)
		incomingPlayfield.audio->Mute(mute);
	if (currentPlayfield.audio != nullptr)
		currentPlayfield.audio->Mute(mute);
}

void PlayfieldView::IncomingPlayfieldMediaDone(VideoSprite *sprite)
{
	// set the new sprite
	incomingPlayfield.sprite = sprite;
	incomingPlayfieldLoadTime = GetTickCount();

	// update the drawing list
	UpdateDrawingList();

	// Start the cross-fade.  Exception: if there's a video, and it hasn't 
	// started playing yet, defer the cross-fade until we get notification
	// that playback has started.
	if (sprite->GetVideoPlayer() == nullptr || sprite->GetVideoPlayer()->IsFrameReady())
		StartPlayfieldCrossfade();
}

void PlayfieldView::OnEnableVideos(bool enable)
{
	// Clear the playfield sprites (current and incoming), then reload.
	// If video is becoming disabled, we only need to reload if we actually
	// have a video currently showing.  If video is becoming enabled, reload
	// under all circumstances.
	bool reload = false;
	auto Check = [enable, &reload](GameMedia<VideoSprite> &item)
	{
		// we only need to reload this item if it has a sprite loaded
		if (item.sprite != nullptr)
		{
			// reload the item if we're newly enabling video, or the item
			// is currently showing a video
			if (enable || item.sprite->GetVideoPlayer() != nullptr)
			{
				// enabling - reload unconditionally
				item.Clear();
				reload = true;
			}
		}
	};

	Check(currentPlayfield);
	Check(incomingPlayfield);

	// reload the media if necessary
	if (reload)
	{
		UpdateDrawingList();
		LoadIncomingPlayfieldMedia(GameList::Get()->GetNthGame(0));
	}
}

Sprite *PlayfieldView::LoadWheelImage(const GameListItem *game)
{
	// create the sprite
	Sprite *sprite = new Sprite();

	// get the path for the wheel image
	TSTRING path;
	bool ok = false;
    Application::InUiErrorHandler eh;
	if (IsGameValid(game) && game->GetMediaItem(path, GameListItem::wheelImageType))
	{
		// Get the image's native size.  Figure the sprite size based on
		// a fixed width, scaling as always to the height, using 1920 pixels
		// as the reference height.
		ImageFileDesc imageDesc;
		GetImageFileInfo(path.c_str(), imageDesc, true);
		float aspect = imageDesc.dispSize.cx != 0 ? float(imageDesc.dispSize.cy) / float(imageDesc.dispSize.cx) : 1.0f;
		float width = 0.44f;
		float height = width * aspect;

		// If that makes the image too tall, scale it down to limit the height
		if (height > 0.25f)
		{
			height = 0.25f;
			width = height / (aspect > .01f ? aspect : 1.0f);
		}
		POINTF normSize = { width, height };

		// figure the corresponding pixel size
		SIZE pixSize = { (int)(width * szLayout.cx), (int)(height * szLayout.cy) };

		// Load the image
		ok = sprite->Load(path.c_str(), normSize, pixSize, eh);
	}

	// if we didn't load a sprite, synthesize a default image
	if (!ok)
	{
		// synthesize a default image based on the table title
		int width = 844, height = 240;
		sprite->Load(width, height, [this, game, width, height](HDC hdc, HBITMAP)
		{
			// get the title string
			TSTRINGEx title;
			if (game != nullptr)
				title = game->title;
			else
				title.Load(IDS_NO_GAME_TITLE);

			// get up a graphic context and a font
			Gdiplus::Graphics g(hdc);

			// measure the title string and figure the origin for centering it
			std::unique_ptr<Gdiplus::Font> font;
			Gdiplus::RectF rcLayout(0, 0, float(width), 0);
			Gdiplus::RectF bbox;
			for (int ptsize = wheelFont.ptSize; ptsize >= 40; ptsize -= 8)
			{
				// create the font at this size
				font.reset(CreateGPFont(wheelFont.family.c_str(), ptsize, wheelFont.weight));

				// measure it
				g.MeasureString(title, -1, font.get(), rcLayout, &bbox);

				// if it fits, use this font size
				if (bbox.Height <= height)
					break;
			}

			// center it
			rcLayout.X = float(width - bbox.Width) / 2.0f;
			rcLayout.Y = float(height - bbox.Height) / 2.0f;
			rcLayout.Width = bbox.Width;
			rcLayout.Height = bbox.Height;

			// draw a drop shadow
			Gdiplus::SolidBrush shadow(Gdiplus::Color(192, 0, 0, 0));
			Gdiplus::StringFormat fmt;
			fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
			g.DrawString(title, -1, font.get(), rcLayout, &fmt, &shadow);

			// draw the text
			rcLayout.X -= 3;
			rcLayout.Y -= 3;
			Gdiplus::SolidBrush br(Gdiplus::Color(255, 255, 255, 255));
			g.DrawString(title, -1, font.get(), rcLayout, &fmt, &br);
			
			// make sure updates are flushed
			g.Flush();
		}, eh, _T("default wheel image"));
	}

	// return the new sprite
	return sprite;
}

// Update a wheel image position.  'n' is the position on the wheel,
// with 0 representing the center position.  'progress' is the position
// in the animation sequence; 0.0f represents the idle state or the
// start of an animation, +1.0f represents the end of the transition
// to the next game to the right, and -1.0f represent the end of the
// transition to the prior game to the left.
void PlayfieldView::SetWheelImagePos(Sprite *image, int n, float progress)
{
	// wheel layout parameters
	const float r = 943.0f / 1980.0f;			// wheel radius
	const float y = -1580.0f / 1980.0f;		// vertical location of wheel 
	const float dTheta = 0.25f;				// angle between games
	const float y0 = -0.07135f;				// center image offset at idle
	const float targetWidth = 0.14f;        // target width of wheel image

	// set the scale so that the image width comes out to the target width
	float ratio = image->loadSize.x == 0.0f ? 1.0f : 0.14f / image->loadSize.x;
	image->scale.x = image->scale.y = ratio;

	// calculate the angle for this game
	float theta = float(n) * dTheta;

	// adjust for the travel distance
	theta -= progress * dTheta * fabs(float(animWheelDistance));

	// calculate the new position
	image->offset.x = r * sinf(theta);
	image->offset.y = y + r * cosf(theta);

	// For images at the center or transitioning to/from the center spot,
	// adjust the position and scale.  The center image is shown at (0,y0)
	// and at scale factor 1.0; the adjacent images are shown at their
	// natural wheel positions at idle.  We adjust the position and scale
	// on a ramp between the standard and special positions according to
	// the progress.
	float ramp = fabs(progress)*progress*progress;
	if (n == 0)
	{
		// Outgoing center image
		image->scale.x = image->scale.y = 1.0f - (1.0f - ratio)*ramp;
		image->offset.y = y0 - (y0 - image->offset.y)*ramp;
	}
	else if (n == animWheelDistance)
	{
		// Animation target - incoming center image
		image->scale.x = image->scale.y = ratio + (1.0f - ratio)*ramp;
		image->offset.y += (y0 - image->offset.y)*ramp;
	}

	// update the world transform for the image
	image->UpdateWorld();
}

void PlayfieldView::SwitchToGame(int n, bool fast, bool byUserCommand)
{
	// ignore switches to the same game
	if (n == 0)
		return;

	// note the next step direction
	int dn = n > 0 ? 1 : -1;

	// If the switch is being made by a user command (as opposed to
	// something automatic, like attract mode), and we've newly
	// configured the current game, promote the game to configured
	// status.  The user can create a database entry to a previously
	// unconfigured game through the Game Setup menu commands, so 
	// it's possible for a game to become newly configured while
	// selected.  However, we don't change its status until the user
	// explicitly switches to another game in the UI, so that we
	// continue to give it the special UI treatment for a new game
	// as long as it's selected.  That allows the user to complete
	// a series of setup actions on the same game - filling in the
	// bibliographic data, capturing screen shots, downloading
	// media - via the same Game Setup commands in the main menu.
	// Once we leave the game, we'll promote it to "configured"
	// status so that it acts like any other game that was already
	// set up.  Note that you can still access all of the setup
	// commands for a configured game, so promoting the status
	// doesn't actually disable any commands; it simply makes the
	// UI path to access the commands different, in that they only
	// appears in the operator menu for a configured game, whereas
	// they also appear in the main menu for an unconfigured game.
	if (byUserCommand)
	{
		// if the outgoing game is marked as unconfigured but now 
		// has a database entry, mark it as configured
		if (auto game = GameList::Get()->GetNthGame(0);
			IsGameValid(game) && !game->isConfigured && game->dbFile != nullptr)
			game->isConfigured = true;
	}

	// Load the new wheel images coming into view.  The wheel shows
	// two games to either side of the current game, so we'll need
	// to load enough images such that there are two more beyond the
	// target game in the direction we're going:
	//
	// - If we're moving by one game notch only, the target game is 
	//   already in the list.  We just need to load the one new game
	//   that will come into view on the wheel.
	//
	// - If we're moving two notches, the target game is also already
	//   in the list, so we just need to load the two new games
	//   coming into view.
	//
	// - If we're moving three notches, the target game isn't in the
	//   list yet, so we need to load it plus the next two games.
	//
	// - If we're moving four notches, the target isn't in the list
	//   yet, nor is the game to its left, so we need to load the
	//   third, fourth, fifth, and sixth games.
	//
	// - If we're moving five or more notches, things get tricker.
	//   We *could* just load all of the intermediate games, but we
	//   could be moving such a distance that the animation might get
	//   jerky if we populated all of the games in between.  (It would
	//   get jerky becuase we use a fixed time for the animation, no
	//   matter how far we're moving, hence the distance moved on each
	//   frame increases as the span increases.)  So instead of loading
	//   all intermediate games, load a fresh slate of five games
	//   centered on the new game, and act like we're only moving five
	//   notches for animation purposes.  The animation goes by so fast
	//   that the user won't be able to see that the games in between
	//   we're dropped, and the animation effect will at least be no
	//   worse than the five-game-span effect.
	int firstToAdd = dn * 3;
	int lastToAdd = firstToAdd + (abs(n) - 1)*dn;
	animWheelDistance = n;
	if (n > 4 || n < -4)
	{
		// add the five games centered on the new selection
		if (dn > 0)
		{
			firstToAdd = n - 2;
			lastToAdd = n + 2;
		}
		else
		{
			firstToAdd = n + 2;
			lastToAdd = n - 2;
		}

		// move by five wheel spots
		animWheelDistance = dn * 5;
	}

	// If we don't already have five images in the wheel, something
	// must be out of sync - repopulate the list so that we're starting
	// from the correct baseline.
	if (wheelImages.size() < 5)
		UpdateSelection();

	// add the selected items to the wheel
	animAddedToWheel = 0;
	animFirstInWheel = -2;
	TSTRING path;
	for (int i = firstToAdd; ; i += dn)
	{
		// Load the next wheel image 
		Sprite *s = LoadWheelImage(GameList::Get()->GetNthGame(i));

		// make sure it's off the screen initially
		s->offset.y = -5.0f;
		s->scale.x = s->scale.y = 0.0f;
		s->UpdateWorld();

		// add it to the appropriate end of the list for the direction
		// we're moving
		if (dn < 0)
		{
			wheelImages.emplace_front(s);
			animFirstInWheel--;
		}
		else
		{
			wheelImages.emplace_back(s);
		}

		// count it so that we can remove it when the animation is done
		animAddedToWheel++;

		// stop if this is the last one we're adding
		if (i == lastToAdd)
			break;
	}

	// update the drawing list for the sprite changes
	UpdateDrawingList();

	// set the new selection in the game list
	GameList::Get()->SetGame(n);

	// enter wheel animation mode
	StartWheelAnimation(fast);

	// update javascript
	FireGameSelectEvent(GameList::Get()->GetNthGame(0));
}

// Start a wheel animation
void PlayfieldView::StartWheelAnimation(bool fast)
{
	// if an info box is showing, hide it
	HideInfoBox();

	// set the new mode
	wheelAnimMode = fast ? WheelAnimFast : WheelAnimNormal;

	// start the timer
	StartAnimTimer(wheelAnimStartTime);
}

void PlayfieldView::ClearMedia()
{
	// remove the playfield images
	currentPlayfield.Clear();
	incomingPlayfield.Clear();

	// clear the info box
	infoBox.Clear();

	// remove all wheel images
	wheelImages.clear();
	animAddedToWheel = 0;

	// update the drawing list for the change
	UpdateDrawingList();

	// clear media on the real DMD if present
	if (realDMD != nullptr)
		realDMD->ClearMedia();
}

void PlayfieldView::OnNewFilesAdded()
{
	// update the selection, to rebuild the wheel list
	UpdateSelection();
}

// Show the "game running" popup
void PlayfieldView::BeginRunningGameMode(GameListItem *game, GameSystem *)
{
	// remember the running game's ID
	runningGameID = game != nullptr ? game->internalID : 0;

	// fire a DOF Launch Game event
	QueueDOFPulse(L"PBYLaunchGame");

	// show the initial blank screen
	runningGameMode = RunningGameMode::Starting;
	ShowRunningGameMessage(nullptr);

	// animate the popup opening
	runningGamePopup->alpha = 0;
	StartAnimTimer(runningGamePopupStartTime);
	runningGamePopupMode = RunningGamePopupOpen;

	// turn off the status line updates while running
	DisableStatusLine();

	// cancel any key/joystick auto-repeat
	StopAutoRepeat();

	// don't monitor for attract mode while running
	KillTimer(hWnd, attractModeTimerID);

	// set up the game inactivity timer
	ResetGameTimeout();

	// update the javascript UI mode
	UpdateJsUIMode();
}

void PlayfieldView::ShowRunningGameMessage(const TCHAR *msg)
{
	// create the sprite
	runningGamePopup.Attach(new Sprite());
	const int width = NormalizedWidth(), height = 1920;
	Application::InUiErrorHandler eh;
	runningGamePopup->Load(width, height, [width, height, msg, this](Gdiplus::Graphics &g)
	{
		// fill the background
		Gdiplus::SolidBrush bkg(Gdiplus::Color(255, 30, 30, 30));
		g.FillRectangle(&bkg, 0, 0, width, height);

		// If there's a message, draw the wheel image and the message.
		// If the message is null, leave the screen blank.
		if (msg != nullptr)
		{
			// load the wheel image, if available
			std::unique_ptr<Gdiplus::Bitmap> wheelImage;
			SIZE wheelImageSize = { 0, 0 };
			auto game = GameList::Get()->GetByInternalID(runningGameID);
			TSTRING wheelFile;
			if (IsGameValid(game) && game->GetMediaItem(wheelFile, GameListItem::wheelImageType))
				wheelImage.reset(Gdiplus::Bitmap::FromFile(wheelFile.c_str()));

			// draw the wheel image, if available
			if (wheelImage != nullptr)
			{
				wheelImageSize = { (LONG)wheelImage->GetWidth(), (LONG)wheelImage->GetHeight() };
				float aspect = wheelImageSize.cx != 0 ? float(wheelImageSize.cy) / float(wheelImageSize.cx) : 1.0f;
				int dispWidth = 844, dispHeight = int(dispWidth * aspect);
				g.DrawImage(wheelImage.get(), Gdiplus::Rect((width - dispWidth) / 2, (height - dispHeight) / 2, dispWidth, dispHeight));
			}

			// draw the text, centered above the wheel image
			Gdiplus::SolidBrush fg(Gdiplus::Color(255, 255, 255, 255));
			Gdiplus::RectF bbox;
			g.MeasureString(msg, -1, popupTitleFont, Gdiplus::PointF(0, 0), &bbox);
			g.DrawString(msg, -1, popupTitleFont, Gdiplus::PointF(
				(float)(width - bbox.Width) / 2.0f,
				(float)(height - wheelImageSize.cy) / 2.0f - bbox.Height - 60),
				&fg);
		}
	}, eh, _T("Game Running Popup"));

	// udpate the drawing list with the new sprite
	UpdateDrawingList();
}

void PlayfieldView::EndRunningGameMode()
{
	// clear the running game mode flag
	runningGameMode = RunningGameMode::None;

	// resume background updates
	freezeBackgroundRendering = false;

	// Only proceed if we're in running game mode
	if (runningGamePopup == nullptr)
		return;

	// Clear the keyboard queue
	keyQueue.clear();

	// sync the playfield
	SyncPlayfield(SyncEndGame);

	// Restore the other windows
	Application::Get()->EndRunningGameMode();

	// Remove any kill-game timer.  This timer is set when the user
	// explicitly asks us to terminate the running game, as a fallback
	// in case the game doesn't exit properly on its own (or in case
	// we fail to detect its termination).  The timer ensures that the
	// running game overlay comes down even if the game is still running,
	// to return the UI to normal.
	KillTimer(hWnd, killGameTimerID);

	// Remove any game inactivity timeout timer
	KillTimer(hWnd, gameTimeoutTimerID);

	// remove the popup
	StartAnimTimer(runningGamePopupStartTime);
	runningGamePopupMode = RunningGamePopupClose;

	// restore status line updates
	EnableStatusLine();

	// restart the attract mode timer
	attractMode.Reset(this);
	SetTimer(hWnd, attractModeTimerID, attractModeTimerInterval, 0);

	// make sure I'm in the foreground
	SetForegroundWindow(GetParent(hWnd));

	// Set a timer to reinstate our DOF client after a short delay.
	// Don't do this immediately, because DOF doesn't do anything to
	// serialize access from multiple processes, and many of the USB
	// devices that DOF accesses have protocols that depend on packet
	// sequencing, which for obvious reasons will get confused if 
	// multiple processes are sending packets at once.  DOF resets
	// devices when an attached process exits, so the exiting game
	// will have just sent a set of USB packets to each device to 
	// turn off its ports.  We want to allow time for those packets
	// to make it through the Windows driver buffers and down the
	// wire before we start sending our own DOF packets, to avoid
	// the packet sequencing problems mentioned above.  So set a
	// short timer for doing our DOF re-connect.
	SetTimer(hWnd, restoreDOFAndDMDTimerID, 500, NULL);

	// cancel any keyboard/joystick auto-repeat
	StopAutoRepeat();

	// update the javascript UI mode
	UpdateJsUIMode();
}

bool PlayfieldView::OnUserMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case PFVMsgGameRunBefore:
		// The RunBeforePre external command has finished.  Fire the Javascript
		// "runbeforepre" event, show the "Loading" message, and fire the 
		// Javascript "runbefore" event.
		{
			// get the launch report
			auto report = reinterpret_cast<LaunchReport*>(lParam);

			// presume that we'll return TRUE in the LRESULT to continue the launch
			curMsg->lResult = TRUE;

			// fire Javascript "runbeforepre"; abort the launch if the event is canceled
			if (!FireLaunchEvent(jsRunBeforePreEvent, report->gameInternalID, report->launchCmd))
			{
				curMsg->lResult = FALSE;
				return true;
			}

			// Show the "Launching Game" message while the game is loading, and
			// while we're running the RunBefore javascript and external commands.
			ShowRunningGameMessage(LoadStringT((report->launchFlags & Application::LaunchFlags::Capturing) != 0 ? 
				IDS_CAPTURE_LOADING : IDS_GAME_LOADING));

			// fire Javascript "runbefore"
			if (!FireLaunchEvent(jsRunBeforeEvent, report->gameInternalID, report->launchCmd))
			{
				curMsg->lResult = FALSE;
				return true;
			}
		}

		// done
		return true;

	case PFVMsgGameRunAfter:
		// We're finished the RunAfter external command.  Fire the Javascript
		// "runafter" event, clear the "Exiting Game" message, and fire the
		// Javascript "runafterpost" event.  These Javascript events aren't 
		// cancelable.
		{
			// get the launch report
			auto report = reinterpret_cast<LaunchReport*>(lParam);

			// Fire "runafter"
			FireLaunchEvent(jsRunAfterEvent, report->gameInternalID, report->launchCmd);

			// clear the screen
			ShowRunningGameMessage(nullptr);

			// fire "runafter post"
			FireLaunchEvent(jsRunAfterPostEvent, report->gameInternalID, report->launchCmd);
		}

		// done
		return true;

	case PFVMsgGameLoaded:
		{
			// get the launch report
			auto report = reinterpret_cast<LaunchReport*>(lParam);

			// switch to "Running" mode in the UI
			runningGameMode = RunningGameMode::Running;
			ShowRunningGameMessage(LoadStringT((report->launchFlags & Application::LaunchFlags::Capturing) != 0 ?
				IDS_CAPTURE_RUNNING : IDS_GAME_RUNNING));

			// If a menu is closing, remove it immediately.  Most games take
			// a couple of seconds to launch, so the menu that launches the
			// game is usually already gone by the time we get here, the
			// closing animation having started when the user selected Play.  
			// But in some cases we won't have much of a delay, such as when
			// launching a small console-mode program, so the animation might
			// still be in progress, in which case it'll never finish by
			// itself since we're about to freeze the display.  So finish the
			// animation by fiat now, so that we leave the screen with just
			// the running game message.
			AccelerateCloseMenu();

			// get the game we're launching
			auto game = GameList::Get()->GetByInternalID(report->gameInternalID);
			auto system = GameList::Get()->GetSystem(report->systemConfigIndex);

			// Reset the game inactivity timer now that the game has actually
			// started running.  This effectively removes however long the game
			// needed to start from the timeout period.  Note that we start the
			// timeout when first trying to load the game, just in case we fail
			// to figure out when the game has finished loading; that way the
			// timeout will still trigger.
			ResetGameTimeout();

			// set running game mode in the other windows
			Application::Get()->BeginRunningGameMode(game, system);

			// Fire the Javascript "gamestarted" event
			FireLaunchEvent(jsGameStartedEvent, report->gameInternalID, report->launchCmd);

			// We're presumably running in the background at this point, since
			// the game player app should be in front now, so we're not doing
			// the usual D3D redraw-on-idle that we do in the foreground.  We
			// need to do an old-fashioned InvalidateRect() to trigger a
			// WM_PAINT to show the running game popup.  That remains static
			// as long as the game is running, so we won't need further manual
			// updates, but we at least need one now for the initial display.
			InvalidateRect(hWnd, 0, false);

			// freeze idle-time rendering while we're in the background, to
			// minimize GPU usage
			freezeBackgroundRendering = true;
		}
		return true;

	case PFVMsgGameOver:
		{
			// get the launch report
			auto report = reinterpret_cast<GameOverReport*>(lParam);

			// The current running game's child process has exited.  If we're
			// not already in "exiting game" mode, the process must have exited
			// on its own rather than via a Kill Game message, so update the UI
			// to "exiting game" mode now.
			if (runningGameMode != RunningGameMode::Exiting)
			{
				runningGameMode = RunningGameMode::Exiting;
				ShowRunningGameMessage(LoadStringT(IDS_GAME_EXITING));
			}

			// Update the run time for the game, if statistics updates were requested
			// when the game was launched.  (This flag is typically set when launching
			// a regular play sessions, but not when launching for media capture.)
			if ((report->launchFlags & Application::LaunchFlags::UpdateStats) != 0)
			{
				// find the game
				GameList *gl = GameList::Get();
				if (auto game = gl->GetByInternalID(report->gameInternalID); game != nullptr)
				{
					// figure the total run time in seconds for this session
					int seconds = static_cast<int>(report->runTime_ms / 1000);

					// add it to the cumulative play time in the database
					gl->SetPlayTime(game, gl->GetPlayTime(game) + seconds);
				}
			}

			// fire the javascript "gameover" event
			FireLaunchEvent(jsGameOverEvent, report->gameInternalID, report->launchCmd);
		}

		// handled
		return true;

	case PFVMsgLaunchThreadExit:
		{
			// get the launch report
			auto report = reinterpret_cast<LaunchReport*>(lParam);
		
			// the game launch thread has exited - exit running game mode in the UI
			EndRunningGameMode();

			// clean up the thread monitor in the application
			Application::Get()->CleanGameMonitor();

			// Fire the Javascript "postlaunch" event
			FireLaunchEvent(jsPostLaunchEvent, report->gameInternalID, report->launchCmd);

			// launch the next queued game, if in batch capture mode
			if (batchCaptureMode.active)
				PostMessage(WM_COMMAND, ID_BATCH_CAPTURE_NEXT_GAME);
		}

		// handled
		return true;

	case PFVMsgCaptureDone:
		// capture done report
		OnCaptureDone(reinterpret_cast<const CaptureDoneReport*>(wParam));
		return true;

	case PFVMsgGameLaunchError:
		// game launch failed
		{
			// get the error descriptor
			auto report = reinterpret_cast<const LaunchErrorReport*>(lParam);

			// Notify javascript.  If Javascript cancels the event, it means that we should
			// skip showing the error message.
			if (FireLaunchEvent(jsLaunchErrorEvent, report->gameInternalID, report->launchCmd, report->errorMessage))
			{
				// show the error
				ShowSysError(LoadStringT(IDS_ERR_LAUNCHGAME), report->errorMessage);
			}

			// exit running game mode
			EndRunningGameMode();

		}
		return true;

	case PFVMsgShowError:
		// Show in-UI error; LPARAM = const PFVMsgShowErrorParams*
		{
			auto ep = reinterpret_cast<const PFVMsgShowErrorParams*>(lParam);
			ShowError(ep->iconType, ep->summary, ep->errList);
		}
		return true;

	case PFVMsgShowFlashError:
		// Show in-UI Flash loading error; LPARAM = const ErrorList*
		ShowFlashError(*reinterpret_cast<const ErrorList*>(lParam));
		return true;

	case PFVMsgShowSysError:
		// Show in-UI system error: WPARAM = const TCHAR *friendly, LPARAM = const TCHAR *details
		ShowSysError((const TCHAR *)wParam, (const TCHAR *)lParam);
		return true;

	case PFVMsgPlayElevReqd:
		// The game we were trying to run failed to launch because the
		// program requires Admin privileges.  This can be triggered in
		// two different ways:
		//
		// 1. If we're not currently running under the Admin Host program,
		//    we can't launch ANY games in Admin mode, since we don't have
		//    an elevated proxy to do the elevated launches for us.  In
		//    this case, we can offer the user an option to relaunch with
		//    the Admin Host.
		//
		// 2. If the Admin Host is already running, we still require the
		//    user to explicitly approve each game system before we'll
		//    run it elevated.  In this case, we ask if they want to
		//    approve the current system.
		//
		// In any case, skip the whole thing if the current game doesn't
		// match the ID in the message.  The launch process is asynchronous,
		// so it's possible that the selection has changed in the UI.  If
		// so, treat that as user cancellation of the original launch.
		if (auto game = GameList::Get()->GetNthGame(0);
			game == nullptr || game->internalID != static_cast<LONG>(lParam))
		{
			// the game selection has changed in the UI - treat it as
			// a cancellation of the launch, so skip the menu
			return true;
		}

		// Check whether we need an Admin Host or just need approval
		// for the current system
		if (Application::Get()->IsAdminHostAvailable())
		{
			// The Admin Host is running, so the issue is that the game
			// system hasn't been approved for elevation yet.  Show the
			// menu asking for approval for this system.

			// Get the name of the system from the WPARAM
			auto sysName = reinterpret_cast<const TCHAR*>(wParam);

			// Remember the game ID, so that we can be sure to launch the
			// same game on approval.  The current game selection in the
			// UI could have changed since the launch attempt, since the
			// launch process runs asynchronously.

			// set up the menu
			std::list<MenuItemDesc> md;
			md.emplace_back(MsgFmt(IDS_ERR_NEED_ELEVATION, sysName), -1);
			md.emplace_back(_T(""), -1);
			md.emplace_back(LoadStringT(IDS_MENU_RUN_GAME_ADMIN), ID_APPROVE_ELEVATION);
			md.emplace_back(LoadStringT(IDS_MENU_CXL_RUN_GAME_ADMIN), ID_MENU_RETURN, MenuSelected);

			// show the menu in "dialog" mode
			ShowMenu(md, L"approve elevation", SHOWMENU_DIALOG_STYLE);
		}
		else
		{
			// The Admin Host isn't running.  Offer to start it.

			// Get the name of the system from the WPARAM
			auto sysName = reinterpret_cast<const TCHAR*>(wParam);
		
			// set up the menu
			std::list<MenuItemDesc> md;
			md.emplace_back(MsgFmt(IDS_ERR_NEED_ADMIN_HOST, sysName), -1);
			md.emplace_back(_T(""), -1);
			md.emplace_back(LoadStringT(IDS_MENU_RUN_ADMIN_HOST), ID_RESTART_AS_ADMIN);
			md.emplace_back(LoadStringT(IDS_MENU_CXL_RUN_AS_ADMIN), ID_MENU_RETURN, MenuSelected);

			// show the menu in "dialog" mode
			ShowMenu(md, L"elevation required", SHOWMENU_DIALOG_STYLE);
		}
		return true;

	case PFVMsgManualGo:
		// Manual Capture Go notification - the Admin Host sends this when
		// it detects the Next+Prev key combination.
		Application::Get()->ManualCaptureGo();
		return true;

	case PFVMsgJsDebugMessage:
		// We've just received a socket request from an attached debugger UI
		// (e.g., VS Code).  These requests are received asynchronously (on
		// background threads) but must be processed on the main thread.
		// Process the incoming command queue now.
		if (auto js = JavascriptEngine::Get(); js != nullptr)
			js->OnDebugMessageQueued();
		break;
	}

	// inherit the default handling
	return __super::OnUserMessage(msg, wParam, lParam);
}

bool PlayfieldView::OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case AVPMsgSetFormat:
		// Video frame format detection/change.
		{
			auto UpdateFormat = [this, wParam, lParam](VideoSprite *sprite)
			{
				if (sprite != nullptr && sprite->GetVideoPlayerCookie() == wParam)
				{
					// Update the sprite's load size to match the actual video frame size.
					// Note that playfield videos are by convention rotated 90 degrees CW,
					// so the nominal 'x' dimension is actually the height.
					auto desc = reinterpret_cast<const AudioVideoPlayer::FormatDesc*>(lParam);
					if (desc->height != 0)
						sprite->loadSize.y = static_cast<float>(desc->height) / static_cast<float>(desc->width);

					// re-create the mesh at the new aspect ratio
					sprite->ReCreateMesh();

					// tell the caller this sprite matched the cookie
					return true;
				}

				// these aren't the droids you're looking for
				return false;
			};
			if (UpdateFormat(incomingPlayfield.sprite) || UpdateFormat(currentPlayfield.sprite))
				ScaleSprites();
		}
		break;

	case AVPMsgFirstFrameReady:
		// First frame of a video is ready to display.  If this is the
		// incoming playfield video player, start the cross-fade for the
		// new playfield.  The WPARAM gives the player cookie, so check
		// that against the incoming playfield's video cookie to make 
		// sure this notification is for the current playfield.  This is
		// asynchronous, so it's possible that we could have already
		// switched to a new playfield by the time this notification
		// arrives.
		if (incomingPlayfield.sprite != nullptr 
			&& incomingPlayfield.sprite->GetVideoPlayerCookie() == wParam)
			StartPlayfieldCrossfade();
		break;

	case AVPMsgEndOfPresentation:
		// End of playback for an audio/video clip.  The WPARAM is the
		// cookie identifying the media player instance.
		//
		// * If it's audio clip in our active audio table, remove it from the 
		//   table, which will release the reference and delete the object.
		//
		// * If it's the real DMD video, pass along the notification to the
		//   real DMD object
		//
		{
			auto cookie = static_cast<DWORD>(wParam);
			if (auto it = activeAudio.find(cookie); it != activeAudio.end())
			{
				// audio playback - remove it from the audio list
				activeAudio.erase(it);
			}
			else if (realDMD != nullptr)
				realDMD->VideoEndOfPresentation(cookie);
		}
		break;

	case AVPMsgLoopNeeded:
		// Loop needed for a video.  The base class handles this for
		// video sprites, but we need to handle table audio looping, and
		// we need to pass the notification to the real DMD if present.
		// Note that each media item has a unique cookie, so if we find
		// a match in one of the objects, we can skip the others.
		if (auto a = currentPlayfield.audio.Get(); a != nullptr && a->GetCookie() == wParam)
			a->Replay(SilentErrorHandler());
		else if (auto a = incomingPlayfield.audio.Get(); a != nullptr && a->GetCookie() == wParam)
			a->Replay(SilentErrorHandler());
		else if (realDMD != nullptr)
			realDMD->VideoLoopNeeded(wParam);

		// proceed to the default handling
		break;

	case HSMsgHighScores:
		// Show the high scores for the given game
		ReceiveHighScores(reinterpret_cast<const HighScores::NotifyInfo*>(lParam));
		return true;
	}

	// inherit the default handling
	return __super::OnAppMessage(msg, wParam, lParam);
}

void PlayfieldView::ShowContextMenu(POINT pt)
{
	// reset the screen saver timer
	attractMode.Reset(this);

	// use the normal handling
	__super::ShowContextMenu(pt);
}

// Show a system error
void PlayfieldView::ShowSysError(const TCHAR *msg, const TCHAR *details)
{
	ShowError(EIT_Error, MsgFmt(IDS_ERR_MSGANDDETAILS, msg, details));
}

// Show an error message using a popup in the main UI window.  This
// is less obtrusive than a system message box since it uses the
// same video game-style interface as the rest of the UI.
void PlayfieldView::ShowError(ErrorIconType iconType, const TCHAR *groupMsg, const ErrorList *list)
{
	ShowErrorAutoDismiss(INFINITE, iconType, groupMsg, list);
}

void PlayfieldView::ShowErrorAutoDismiss(DWORD timeout, ErrorIconType iconType, const TCHAR *groupMsg, const ErrorList *list)
{
	// queue the error
	queuedErrors.emplace_back(timeout, iconType, groupMsg, list);

	// If there's not already an error display in progress, show the new
	// error.  Otherwise just leave it in the queue for display when the
	// user dismisses the current error popup.
	if (popupSprite == nullptr || popupType != PopupErrorMessage)
		ShowQueuedError();
}

void PlayfieldView::ShowQueuedError()
{
	// if there are no errors, there's nothing to do
	if (queuedErrors.size() == 0)
		return;

	// fire an event first, abort on cancel
	const WCHAR *popupName = L"message";
	if (!FirePopupEvent(true, popupName))
		return;

	// remove any previous auto-dismiss timer
	KillTimer(hWnd, autoDismissMsgTimerID);

	// get the first error
	const QueuedError &err = queuedErrors.front();

	// if this message has a timeout, set a timer for it
	if (err.timeout != INFINITE)
		SetTimer(hWnd, autoDismissMsgTimerID, err.timeout, NULL);

	// build the message list
	std::list<TSTRING> messages;

	// add the group message if present
	if (err.groupMsg.length() != 0)
		messages.emplace_back(err.groupMsg);

	// add the list items
	err.list.EnumErrors([&messages](const ErrorList::Item &i)
	{
		if (i.details.length() != 0)
			messages.emplace_back(MsgFmt(IDS_ERR_MSGANDDETAILS, i.message.c_str(), i.details.c_str()));
		else
			messages.emplace_back(i.message.c_str());
	});

	// fixed drawing box dimensions
	const int headerHeight = 60;
	const int margins = 16;
	const int outline = 4;
	const int bottomSpacing = 8;  // to compensate for GDI putting all leading at the top of the line
	const int layoutWidth = 900;

	// set up a font for the error text
	MemoryDC memdc;
	Gdiplus::Graphics g(memdc);
	std::unique_ptr<Gdiplus::Font> font(CreateGPFont(_T("Segoe UI"), 22, 400));

	// figure the height of the error list
	int ht = 0;
	Gdiplus::RectF layoutRect(float(margins), 0.0f, float(layoutWidth - margins*2 - outline*2), 600.0f);
	Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
	format.SetFormatFlags(format.GetFormatFlags() & ~Gdiplus::StringFormatFlagsLineLimit);
	for (auto const &m: messages)
	{
		// measure the text
		Gdiplus::RectF bbox;
		g.MeasureString(m.c_str(), -1, font.get(), layoutRect, &format, &bbox);
		ht += (int)bbox.Height;
	};

	// add interline spacing
	int spacing = 12;
	ht += ((int)messages.size() - 1) * spacing;

	// if anything goes wrong while loading the graphics, ignore that
	// problem and show the original error message in a system error box
	class FallbackHandler : public ErrorHandler
	{
	public:
		FallbackHandler(const QueuedError &err)
			: err(err), displayed(false) { }

		const QueuedError &err;
		bool displayed;

		virtual void Display(ErrorIconType, const TCHAR *)
		{
			if (!displayed)
			{
				displayed = true;
				InteractiveErrorHandler ieh;
				if (err.list.CountErrors() != 0)
					ieh.GroupError(EIT_Error, err.groupMsg.c_str(), err.list);
				else
					ieh.Error(err.groupMsg.c_str());
			}
		}
	};
	FallbackHandler eh(err);

	// set up the popup
	popupSprite.Attach(new Sprite());
	int layoutHeight = headerHeight + 2*margins + outline + ht + bottomSpacing;
	popupSprite->Load(layoutWidth, layoutHeight, 
		[&err, &messages, layoutWidth, layoutHeight, headerHeight, spacing, margins, outline, &font]
		(HDC hdc, HBITMAP)
	{
		// set up the graphics context
		Gdiplus::Graphics g(hdc);

		// figure the icon and frame color according to the error type
		int iconId;
		Gdiplus::Color frameColor;
		switch (err.iconType)
		{
		case EIT_Error:
			iconId = IDB_ERROR_BOX_BAR;
			frameColor = Gdiplus::Color(192, 255, 0, 0);
			break;

		case EIT_Warning:
			iconId = IDB_WARNING_BOX_BAR;
			frameColor = Gdiplus::Color(255, 255, 127, 0);
			break;

		case EIT_Information:
			iconId = IDB_INFO_BOX_BAR;
			frameColor = Gdiplus::Color(255, 0, 160, 0);
			break;
		}

		// fill the background
		Gdiplus::SolidBrush bkg(Gdiplus::Color(255, 220, 220, 220));
		g.FillRectangle(&bkg, 0, 0, layoutWidth, layoutHeight);

		// load the top bar graphic
		std::unique_ptr<Gdiplus::Bitmap> topbar(GPBitmapFromPNG(iconId));

		// draw the messages
		Gdiplus::PointF origin((float)margins + outline, (float)(headerHeight + margins));
		Gdiplus::RectF layoutRect((float)margins + outline, 0, (float)(layoutWidth - 2*margins - 2*outline), (float)layoutHeight);
		Gdiplus::SolidBrush br(Gdiplus::Color(255, 128, 0, 0));
		int n = 0;
		for (auto m : messages)
		{
			// draw a separator below the first item if we have a group list
			if (n == 1)
			{
				Gdiplus::Pen pen(Gdiplus::Color(255, 220, 200, 200), 2.0f);
				g.DrawLine(&pen, 0, (int)origin.Y - spacing/2, layoutWidth, (int)origin.Y - spacing/2);
			}

			// draw the message
			GPDrawStringAdv(g, m.c_str(), font.get(), &br, origin, layoutRect);

			// count it and add in the vertical spacing between lines
			++n;
			origin.Y += spacing;
		}

		// draw an outline
		Gdiplus::Pen pen(frameColor, (float)outline);
		g.DrawRectangle(&pen, outline/2, outline/2, layoutWidth - outline, layoutHeight - outline);

		// draw the top bar
		g.DrawImage(topbar.get(), 0, 0, layoutWidth, headerHeight);

		// sync pixels with the bitmap
		g.Flush();
	}, eh, _T("Error Box"));

	// adjust it to the canonical popup position
	AdjustSpritePosition(popupSprite);

	// remove the displayed error
	queuedErrors.pop_front();

	// animate the popup
	StartPopupAnimation(PopupErrorMessage, popupName, true);
	UpdateDrawingList();
}

void PlayfieldView::ShowFlashError(const ErrorList &list)
{
	// if Flash errors are disabled for this session, ignore the error
	if (!showFlashErrors)
		return;

	// make a string out of the error list
	TSTRING msg;
	list.EnumErrors([&msg](const ErrorList::Item &item) {
		if (msg.length() != 0)
			msg.append(_T("\n"));
		if (item.details.length() != 0)
			msg.append(MsgFmt(_T("%s (%s)"), item.message.c_str(), item.details.c_str()));
		else
			msg.append(item.message.c_str());
	});

	// construct the menu
	std::list<MenuItemDesc> md;
	md.emplace_back(MsgFmt(IDS_SWF_ERROR, msg.c_str()), -1);
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_SWF_ERROR_DISABLE), ID_SWF_ERROR_DISABLE);
	md.emplace_back(LoadStringT(IDS_SWF_ERROR_SUPPRESS), ID_SWF_ERROR_SUPPRESS);
	md.emplace_back(LoadStringT(IDS_SWF_ERROR_HELP), ID_SWF_ERROR_HELP, MenuStayOpen);
	md.emplace_back(LoadStringT(IDS_SWF_ERROR_IGNORE), ID_MENU_RETURN);

	// show the menu
	ShowMenu(md, _T("swf error"), SHOWMENU_DIALOG_STYLE);
}

void PlayfieldView::AdjustSpritePosition(Sprite *sprite)
{
	// get the load height
	float loadHt = sprite->loadSize.y;

	// Figure the Y offset that leaves at least a top margin of
	// at least 1/16 of the screen height.  This will be the minimum
	// Y offset.
	float yOfsMin = 0.5f - loadHt/2.0f - .0625f;

	// The sprite will be centered in the top half of the screen if
	// we offset it to a normalized position of +.25.  Apply this
	// offset or the minimum to maintain the margin.
	float yOfs = fminf(0.25f, yOfsMin);

	// apply the offset and update the sprite's world transform
	sprite->offset.y = yOfs;
	sprite->UpdateWorld();
}

void PlayfieldView::MenuPageUpDown(int dir)
{
	// add a reference to the current menu
	RefPtr<Menu> m;
	m = curMenu;

	// select the Page Up or Page Down item in the re-displayed menu
	int cmd = dir > 0 ? ID_MENU_PAGE_DOWN : ID_MENU_PAGE_UP;
	for (auto &d : m->descs)
	{
		if (d.cmd == cmd)
		{
			d.selected = true;
			break;
		}
	}

	// re-show the menu at the new page
	ShowMenu(m->descs, m->id.c_str(), m->flags | SHOWMENU_NO_ANIMATION, menuPage + dir);
}

void PlayfieldView::JsShowMenu(WSTRING name, std::vector<JsValueRef> items, JavascriptEngine::JsObj options)
{
	try
	{
		// build the native menu descriptor list from the Javascript menu descriptor list
		auto js = JavascriptEngine::Get();
		std::list<MenuItemDesc> md;
		for (auto &i : items)
		{
			// convert to object
			auto mi = JavascriptEngine::JsToNative<JavascriptEngine::JsObj>(i);

			// retrieve the properties
			auto title = mi.Get<TSTRING>("title");
			auto cmd = mi.Get<int>("cmd");
			UINT flags = 0;
			if (mi.Get<bool>("selected")) flags |= MenuSelected;
			if (mi.Get<bool>("checked")) flags |= MenuChecked;
			if (mi.Get<bool>("radio")) flags |= MenuRadio;
			if (mi.Get<bool>("hasSubmenu")) flags |= MenuHasSubmenu;
			if (mi.Get<bool>("stayOpen")) flags |= MenuStayOpen;

			// add the menu descriptor
			md.emplace_back(title.c_str(), cmd, flags);
		}

		// get the menu flags from the options object argument
		DWORD menuFlags = SHOWMENU_USER;
		int pageno = 0;
		if (!options.IsNull())
		{
			if (options.Get<bool>("isExitMenu")) menuFlags |= SHOWMENU_IS_EXIT_MENU;
			if (options.Get<bool>("noAnimation")) menuFlags |= SHOWMENU_NO_ANIMATION;
			if (options.Get<bool>("dialogStyle")) menuFlags |= SHOWMENU_DIALOG_STYLE;

			// get the page number from the options argument
			pageno = options.Get<int>("pageNo");
		}

		// remove any menu/popup currently showing
		CloseMenusAndPopups();

		// show the menu
		ShowMenu(md, name.c_str(), menuFlags, pageno);
	}
	catch (JavascriptEngine::CallException exc)
	{
		JavascriptEngine::Get()->Throw(exc.jsErrorCode, AnsiToTSTRING(exc.what()).c_str());
	}
}

// To make it easier for Javascript scripts to distinguish Page Up and
// Page Down menu items from separators, we use Unicode up/down arrows
// as the title strings for the page commands.  These aren't actually 
// displayed, as we substitute other arrow glyphs when actually drawing
// the menu items.  We use them in the menu command purely so that
// Javascript won't find empty strings for any items other than
// separator bars, so that it only has to test for an empty title.
static const TCHAR *PageUpTitle = _T("\u2191");
static const TCHAR *PageDownTitle = _T("\u2193");


// Show a menu
void PlayfieldView::ShowMenu(const std::list<MenuItemDesc> &items, const WCHAR *id, DWORD flags, int pageno)
{
	// create the new menu container
	RefPtr<Menu> m(new Menu(id, flags));

	// copy the descriptor list to the new menu
	m->descs = items;

	// If it's not a user menu, fire Menu Open event
	if (!(flags & SHOWMENU_USER))
	{
		// fire the event; if that prevents the system default handling, cancel
		// the new menu display
		if (!FireMenuEvent(true, m, pageno))
			return;
	}

	// remember the page of the menu being displayed
	menuPage = pageno;

	// set the initial animation for the incoming menu to the start of the sequence
	UpdateMenuAnimation(m, true, 0.0f);

	// set up a GDI+ Graphics object on a memory DC to prepare the graphics
	MemoryDC memdc;
	Gdiplus::Graphics g(memdc);

	// set up a generic typographic formatter
	Gdiplus::StringFormat tformat(Gdiplus::StringFormat::GenericTypographic());
	tformat.SetAlignment(Gdiplus::StringAlignmentCenter);
	tformat.SetFormatFlags((tformat.GetFormatFlags() & ~Gdiplus::StringFormatFlagsLineLimit)
		| Gdiplus::StringFormatFlagsMeasureTrailingSpaces);

	// set up our main text font and checkmark font
	std::unique_ptr<Gdiplus::Font> symfont(CreateGPFont(_T("Wingdings"), menuFont.ptSize, 400));
	ArrowFont arrowFont(menuFont.ptSize);

	// checkmark and bullet characters in Wingdings
	static const TCHAR *checkmark = _T("\xFC");
	static const TCHAR *bullet = _T("\x9F");

	// arrows
	const TCHAR *subMenuArrow = arrowFont.subMenuArrow;
	const TCHAR *upArrow = arrowFont.menuArrowUp;
	const TCHAR *downArrow = arrowFont.menuArrowDown;

	// get the text font height
	int txtHt = (int)menuFont->GetHeight(&g);

	// calculate the height of each item and of the overall menu
	const int yPadding = 4;
	const int spacerHt = 12;
	const int borderWidth = 4;
	const int margin = 8;
	int lineHt = txtHt + yPadding*2;
	int menuWid = 1080 * 3/4;
	int boxWid = menuWid;

	// if it's a "dialog" style menu, use a wider overall box
	if ((flags & SHOWMENU_DIALOG_STYLE) != 0)
		boxWid = 1080 * 9/10;

	// set up the layout area, for text wrap formatting
	Gdiplus::RectF rcLayout((float)(borderWidth + margin), (float)(borderWidth + margin),
		(float)(boxWid - 2*borderWidth - 2*margin), 1920.0f);

	// add up the line heights, and note if there are Page Up/Page
	// Down items
	int menuHt = 2 * borderWidth;
	bool inPagedSection = false;
	bool hasPagedSection = false;
	int nPagedItems = 0;
	for (auto i : items)
	{
		// Check the type:
		//
		// - A spacer uses command -1 and empty text
		//
		// - Everything else uses the text height
		//
		if (i.cmd == -1 && i.text.length() == 0)
			menuHt += spacerHt;
		else
			menuHt += lineHt;

		// check for page up/down items
		if (i.cmd == ID_MENU_PAGE_UP)
		{
			// The Page Up item always appears at the top of the group
			// of paged items, so we're entering the paged section.
			inPagedSection = true;
			hasPagedSection = true;
		}
		else if (i.cmd == ID_MENU_PAGE_DOWN)
		{
			// The Page Down item always appears at the bottom of the
			// paged group, so we're leaving the paged section.
			inPagedSection = false;
		}
		else if (inPagedSection)
		{
			// we're in the paged section - count the item
			++nPagedItems;
		}
	}

	// If it's a dialog-style menu, figure the difference in height for
	// the prompt message, since it uses a different font and might wrap
	// across several lines.  The prompt text is always the first item
	// in the descriptor list.
	int promptHt = 0;
	if ((flags & SHOWMENU_DIALOG_STYLE) != 0)
	{
		// measure the prompt text (the first item's text), with wrapping
		// in the layout area
		auto i = items.front();
		Gdiplus::RectF bbox;
		g.MeasureString(i.text.c_str(), -1, menuHeaderFont, rcLayout, &tformat, &bbox);

		// Use the actual text height, and then add a little height for
		// whitespace between the prompt and the top menu item, to set
		// off the prompt visually from the menu list.
		promptHt = (int)bbox.Height + spacerHt;

		// add the excess over the line height we already calculated
		menuHt += promptHt - lineHt;

		// Add a little extra height for some padding at the bottom as 
		// well.  The layout looks unbalanced in dialog mode if the last
		// menu item abuts the bottom border, because the prompt at the
		// top doesn't visually abut the top border.  (The prompt actually
		// *does* abut the top border, but it doesn't look that way to the
		// eye, because it doesn't have a separate background fill to show
		// its exact borders.  What's important to the eye is that the
		// glyphs don't abut the top border, because of line spacing built
		// into the font.)
		menuHt += spacerHt;
	}

	// Check for pagination.  If the menu has Page Up and Page Down
	// items, the items between those items can be shown in pages if
	// necessary.  It's necessary if the height of the menu exceeds
	// the available display area height.
	int nItemsPerPage = 1;
	if (hasPagedSection)
	{
		// figure the available display height - allow the full
		// screen height, minus the area below the upper status
		// line (plus a little margin), minus the top and bottom
		// 1/8 of the screen area
		int availableHt = (int)(1920.0f * .75f);

		// Use pagination if the menu height exceeds the available
		// display height.  If we don't use pagination, we'll hide
		// the page up/down items in the menu, so exclude their
		// line heights from the computed height for the purposes
		// of determining if we can fit without paging.
		int unpaginatedHeight = menuHt - 2*lineHt;
		if (nPagedItems > 4 && unpaginatedHeight > availableHt)
		{
			// We need pagination.  Figure out how many items
			// we can show per page by excluding items until
			// we fit.  Don't go below a minimum to keep the
			// number of pages the user has to scroll through
			// within reason.
			for (nItemsPerPage = nPagedItems; nItemsPerPage > 4 && menuHt > availableHt ;)
			{
				--nItemsPerPage;
				menuHt -= lineHt;
			}

			// flag in the menu object that we're using pagination
			m->paged = true;
		}
		else
		{
			// It'll fit without pagination.  Change the required
			// height to the unpaginated height.
			menuHt = unpaginatedHeight;
		}
	}


	// create the background
    Application::InUiErrorHandler eh;
	if (!m->sprBkg->Load(boxWid, menuHt, [boxWid, menuHt, borderWidth](HDC hdc, HBITMAP hbmp) 
	{
		// fill the background
		Gdiplus::Graphics g(hdc);
		Gdiplus::SolidBrush br(Gdiplus::Color(0xA8, 0x00, 0x00, 0x00));
		g.FillRectangle(&br, 0, 0, boxWid, menuHt);

		// draw the border
		Gdiplus::Pen pen(Gdiplus::Color(0xE0, 0xFF, 0xFF, 0xFF), float(borderWidth));
		g.DrawRectangle(&pen, borderWidth/2, borderWidth/2, boxWid - borderWidth, menuHt - borderWidth);

		// make sure the pixels hit the DIB
		g.Flush();
	}, eh, _T("menu background")))
		return;

	// Adjust the item list sprite to the canonical popup position, and
	// apply the same offset to the background sprite.
	AdjustSpritePosition(m->sprBkg);
	m->sprItems->offset.y = m->sprBkg->offset.y;

	// Create the highlight overlay.  We set this up at a single line height,
	// and then move it behind the selected menu item to highlight it.
	if (!m->sprHilite->Load(boxWid, lineHt, [boxWid, menuHt, lineHt, borderWidth](HDC hdc, HBITMAP hbmp)
	{
		Gdiplus::Graphics g(hdc);
		Gdiplus::SolidBrush br(Gdiplus::Color(0xE0, 0x40, 0xA0, 0xFF));
		g.FillRectangle(&br, borderWidth, 0, boxWid - 2 * borderWidth, lineHt);
		g.Flush();
	}, eh, _T("menu hilite")))
		return;

	// create the text item overlay
	if (!m->sprItems->Load(boxWid, menuHt, 
		[this, boxWid, menuHt, &symfont, &arrowFont, &items, &m, 
		lineHt, spacerHt, yPadding, borderWidth, nPagedItems, nItemsPerPage, &tformat,
		upArrow, downArrow, subMenuArrow, promptHt, &rcLayout, flags]
	    (HDC hdc, HBITMAP hbmp)
	{
		// start just below the top border
		int y = borderWidth;

		// In case we're paginated, set up the paging section counters
		bool inPagedSection = false;
		int pagedItemNum = 0;

		// if the page is negative, go to the last page; if we're past
		// the last page, go to the first page
		int lastPage = (nPagedItems - 1) / nItemsPerPage;
		if (menuPage < 0)
			menuPage = lastPage;
		else if (menuPage > lastPage)
			menuPage = 0;

		// figure the range of paged items shown
		int firstPagedItem = menuPage * nItemsPerPage;
		int lastPagedItem = firstPagedItem + nItemsPerPage - 1;

		// set up the drawing objects
		Gdiplus::Graphics g(hdc);
		Gdiplus::SolidBrush textBr(Gdiplus::Color(0xFF, 0xFF, 0xFF, 0xFF));
		Gdiplus::SolidBrush groupTextBr(Gdiplus::Color(0xFF, 0x00, 0xFF, 0xFF));
		Gdiplus::Pen pen(Gdiplus::Color(0xff, 0xa0, 0xa0, 0xa0), 2.0f);
		for (auto &i : m->descs)
		{
			// If the command ID is -1 and the text is empty, it's a spacer.
			// Just add some vertical space.
			if (i.cmd == -1 && i.text.length() == 0)
			{
				int yLine = y + spacerHt / 2 - 1;
				int inset = 32;
				g.DrawLine(&pen, inset, yLine, boxWid - inset, yLine);
				y += spacerHt;
				continue;
			}

			// use the regular text brush for regular items, or the group
			// brush for non-command items
			Gdiplus::SolidBrush *br = i.cmd == -1 ? &groupTextBr : &textBr;

			// get the label
			const TCHAR *text;
			Gdiplus::Font *font;
			switch (i.cmd)
			{
			case ID_MENU_PAGE_UP:
				// Page Up - omit this item if we're not using pagination
				if (!m->paged)
					continue;

				// use the up arrow in the symbol font
				text = upArrow;
				font = arrowFont.get();

				// note that we're entering the paged section
				inPagedSection = true;
				break;

			case ID_MENU_PAGE_DOWN:
				// Page Down - omit this item if we're not using pagination
				if (!m->paged)
					continue;

				// use the down arrow in the symbol font
				text = downArrow;
				font = arrowFont.get();

				// we're exiting the paged section
				inPagedSection = false;

				// If we haven't filled out this page, add vertical whitespace
				// to fill out the unused items.  This keeps the overall height
				// of the paged section the same from page to page, even if the
				// last page is only partially filled.
				if (pagedItemNum < lastPagedItem + 1)
					y += (lastPagedItem + 1 - pagedItemNum)*lineHt;
				break;

			default:
				// use the item's text label
				text = i.text.c_str();
				font = menuFont.Get();

				// If we're in the paged section, count the item and check
				// to see if it's within the current page
				if (inPagedSection)
				{
					// check if it's within the current page
					bool inCurPage = (pagedItemNum >= firstPagedItem && pagedItemNum <= lastPagedItem);

					// count it whether it's in the current page or not
					++pagedItemNum;

					// if it's not in the current page, don't display it
					if (!inCurPage)
						continue;
				}
				break;
			}

			// If this is the prompt text in a dialog-style menu, draw it
			// in the dialog font, and wrap the text across multiple lines
			// as needed.
			if ((flags & SHOWMENU_DIALOG_STYLE) != 0 && &i == &m->descs.front())
			{
				// draw it centered in the layout area
				g.DrawString(text, -1, menuHeaderFont, rcLayout, &tformat, &textBr);

				// advance by the prompt height
				y += promptHt;

				// we're done with this item 
				continue;
			}

			// measure the string for centering
			Gdiplus::RectF rc;
			Gdiplus::PointF pt(0.0f, float(y + yPadding));
			g.MeasureString(text, -1, font, pt, &tformat, &rc);

			// figure the centering point
			pt.X = (boxWid - rc.Width) / 2.0f;

			// draw the checkmark or radio button checkmark if present
			if (i.checked || i.radioChecked)
			{
				// get the appropriate checkmark character
				const TCHAR *mark = i.checked ? checkmark : bullet;

				// measure the checkmark - note that we center the string
				// without counting the checkmark, so this hangs off the
				// left of the box for the text
				Gdiplus::RectF ckrc;
				g.MeasureString(mark, -1, symfont.get(), pt, &tformat, &ckrc);

				// draw it to the left of the text box
				Gdiplus::PointF ptck(pt.X - ckrc.Width - 6, pt.Y + lineHt - ckrc.Height + 4);
				g.DrawString(mark, -1, symfont.get(), ptck, br);
			}

			// draw the submenu arrow if present
			if (i.hasSubmenu)
			{
				// measure the arrow
				Gdiplus::RectF arrowrc;
				g.MeasureString(subMenuArrow, -1, arrowFont.get(), pt, &tformat, &arrowrc);

				// draw it to the right of the text box
				Gdiplus::PointF ptarrow(pt.X + rc.Width + 8, pt.Y + rc.Height - arrowrc.Height);
				g.DrawString(subMenuArrow, -1, arrowFont.get(), ptarrow, br);
			}

			// draw the label text
			g.DrawString(text, -1, font, pt, br);

			// If it has a valid command code, add it as an active item
			if (i.cmd > 0)
			{
				// Add the menu item to the list.  Note that Page Up and Page Down
				// commands automatically get the "stay open" flag, since they
				// operate within the menu.
				m->items.emplace_back(
					0, y, i.cmd, 
					i.stayOpen || i.cmd == ID_MENU_PAGE_UP || i.cmd == ID_MENU_PAGE_DOWN);

				// select the item if desired
				if (i.selected)
				{
					// set the selection
					auto it = m->items.end();
					it--;
					m->Select(it);

					// Clear the selection flag in the menu descriptor.  If
					// we re-show this menu, we'll want to use the new selection
					// provided by the caller next time.
					i.selected = false;
				}
			}

			// move to the next item vertically
			y += lineHt;
		}

		// make sure the pixels hit the DIB
		g.Flush();
	}, eh, _T("menu items")))
		return;

	// Select the first item if we didn't already select something else
	if (m->selected == m->items.end())
		m->Select(m->items.begin());

	// Check if there's an old menu to animate out of view first, or
	// if we're even animating the transition at all.
	if ((flags & SHOWMENU_NO_ANIMATION) != 0)
	{
		// skip all menu animation - close out the old menu, and bring
		// up the new menu immediately
		OnCloseMenu(&items);
		curMenu = m;
		menuAnimMode = MenuAnimNone;
		UpdateMenuAnimation(m, true, 1.0f);
		UpdateDrawingList();
	}
	else if (curMenu != nullptr)
	{
		// start the exit animation for the outgoing old menu
		StartMenuAnimation(false);

		// stash the new menu in the "pending" slot - we'll use that
		// to start the incoming menu animation when the exit animation
		// for the old menu finishes
		newMenu = m;
	}
	else
	{
		// no old menu - go straight into the animation for the new menu
		curMenu = m;
		StartMenuAnimation(true);

		// add the new menu to the drawing list
		UpdateDrawingList();
	}

	// set DOF to menu mode
	dof.SetUIContext(L"PBYMenu");
}

void PlayfieldView::AccelerateCloseMenu()
{
	if (curMenu != nullptr && newMenu == nullptr && menuAnimMode == MenuAnimClose)
	{
		OnCloseMenu(nullptr);
		curMenu = nullptr;
		menuAnimMode = MenuAnimNone;
		UpdateDrawingList();
	}
}

void PlayfieldView::OnCloseMenu(const std::list<MenuItemDesc> *incomingMenu)
{
	// Fire the Javascript Menu Close event.  This isn't cancelable, so
	// ignore the result.
	if (curMenu != nullptr)
		FireMenuEvent(false, nullptr, 0);

	// If we're editing a game's category list, remove the 
	// category list unless we're switching to another menu.
	if (categoryEditList != nullptr && incomingMenu == nullptr)
		categoryEditList.reset(nullptr);
}

void PlayfieldView::MenuNext(int dir)
{
	if (curMenu != nullptr && curMenu->items.size() != 0)
	{
		// get the current item
		std::list<MenuItem>::iterator cur = curMenu->selected;

		// move forward or backwards as appropriate
		if (dir > 0)
		{
			++cur;
			if (cur == curMenu->items.end())
				cur = curMenu->items.begin();
		}
		else
		{
			if (cur == curMenu->items.begin())
				cur = curMenu->items.end();
			--cur;
		}

		// update the selection visually
		curMenu->Select(cur);
	}
}

void PlayfieldView::UpdateMenuAnimation(Menu *menu, bool opening, float progress)
{
	// some of our operations are symmetrical, such that the opening
	// and closing operations are mirror images; for these, calculate
	// the symmetry point that we can use in either direction
	float symProgress = opening ? progress : 1.0f - progress;

	// nonlinear ramps for some effects
	float symSq = symProgress * symProgress;
	float symCube = symSq * symProgress;

	// slide and fade the background
	menu->sprBkg->offset.x = (opening ? -0.025f : 0.025f) * (1.0f - symCube);
	menu->sprBkg->alpha = symSq;
	menu->sprBkg->UpdateWorld();

	// zoom the text layer and fade it
	menu->sprItems->scale.x = symSq;
	menu->sprItems->scale.y = 0.5f + symCube / 2.0f;
	menu->sprItems->alpha = symSq;
	menu->sprItems->UpdateWorld();

	// hide the highlight entirely during animation
	menu->sprHilite->alpha = (symProgress == 1.0f ? 1.0f : 0.0f);
}

PlayfieldView::Menu::Menu(const WCHAR *id, DWORD flags) :
	id(id), flags(flags)
{
	// create our sprite objects on creation
	sprBkg.Attach(new Sprite());
	sprItems.Attach(new Sprite());
	sprHilite.Attach(new Sprite());

	// no selection yet
	selected = items.end();

	// assume it's not paged
	paged = false;
}

PlayfieldView::Menu::~Menu()
{
}

void PlayfieldView::Menu::Select(std::list<MenuItem>::iterator sel)
{
	// store the new selection
	selected = sel;

	// update the highlight area
	if (sel != items.end())
	{
		// position it relative to the top of the menu
		float menuTop = sprBkg->offset.y + sprBkg->loadSize.y / 2.0f;
		float itemTopOfs = float(sel->y)/1920.0f;
		sprHilite->offset.y = menuTop - sprHilite->loadSize.y/2.0f - itemTopOfs;
		sprHilite->UpdateWorld();
	}
	else
	{
		// no selection - hide the highlighter
		sprHilite->alpha = 0.0f;
	}
}

void PlayfieldView::UpdatePopupAnimation(bool opening, float progress)
{
	// do nothing if there's no popup
	if (popupSprite == nullptr)
		return;

	// this process is symmterical - use the mirror image when closing
	float symProgress = opening ? progress : 1.0f - progress;

	// do a simple fade
	popupSprite->alpha = symProgress;
}

void PlayfieldView::SyncPlayfield(SyncPlayfieldMode mode)
{
	// if an animation is in progress, or a startup video is in progress,
	// or if there's anything in the key command queue, defer this until later
	if (isAnimTimerRunning 
		|| keyQueue.size() != 0
		|| (videoOverlay != nullptr && videoOverlayID == _T("Startup")))
		return;

	// if a game is running, don't do anything unless we're
	// explicitly returning from the running game
	if (runningGamePopup != nullptr && mode != SyncEndGame)
		return;

	// if the current playfield matches the game list selection,
	// we're already set
	GameListItem *newGame = GameList::Get()->GetNthGame(0);
	if (currentPlayfield.game == newGame)
		return;

	// load the new incoming playfield media
	LoadIncomingPlayfieldMedia(newGame);
}

// Update the drawing sprite list
void PlayfieldView::UpdateDrawingList()
{
	// empty the old lists
	sprites.clear();

	// Add the current and incoming playfields at the bottom of the
	// Z order.  The current playfield goes first, and the incoming
	// goes on top of it, so that we can cross-fade into the new
	// playfield by ramping its alpha from 0 to 1.  
	if (currentPlayfield.sprite != nullptr)
		sprites.push_back(currentPlayfield.sprite);
	if (incomingPlayfield.sprite != nullptr)
		sprites.push_back(incomingPlayfield.sprite);

	// add the status lines
	if (statusLineBkg != nullptr)
		sprites.push_back(statusLineBkg);

	if (attractMode.active)
		attractModeStatus.AddSprites(sprites);
	else
	{
		upperStatus.AddSprites(sprites);
		lowerStatus.AddSprites(sprites);
	}

	// Add the wheel images.  
	//
	// Don't show wheel images if we're in attract mode and the option is set 
	// to hide them.  Some people use wheel images with very uniform background
	// areas, so even when we're constantly changing which specific icon is 
	// showing, the basic footprint of each icon can still be overly static
	// and can cause burn-in.  Hiding the wheel images while in attract mode
	// helps avoid this.
	if (!attractMode.active || !attractMode.hideWheelImages)
	{
		for (auto& s : wheelImages)
			sprites.push_back(s);
	}

	// add the game info box
	if (infoBox.sprite != nullptr)
		sprites.push_back(infoBox.sprite);

	// add the running game overlay
	if (runningGamePopup != nullptr)
		sprites.push_back(runningGamePopup);

	// add the video overlay sprite
	if (videoOverlay != nullptr)
		sprites.push_back(videoOverlay);

	// add the popup
	if (popupSprite != nullptr)
		sprites.push_back(popupSprite);

	// add the menu
	if (curMenu != nullptr)
	{
		sprites.push_back(curMenu->sprBkg);
		sprites.push_back(curMenu->sprHilite);
		sprites.push_back(curMenu->sprItems);
	}

	// add the credits overlay
	if (creditsSprite != nullptr)
		sprites.push_back(creditsSprite);

	// add the drop target overlay
	if (dropTargetSprite != nullptr)
		sprites.push_back(dropTargetSprite);

	// rescale sprites that vary by window size
	ScaleSprites();
}

void PlayfieldView::ScaleSprites()
{
	// The instruction card and flyer popups scale to fill 95% of
	// the window height and/or width, maintaining aspect ratio
	switch (popupType)
	{
	case PopupFlyer:
	case PopupInstructions:
		ScaleSprite(popupSprite, .95f, true);
		break;
	}

	// Scale the playfield to fill as much of the window as possible,
	// maintaining the original aspect ratio or stretching, according
	// to the option settings.
	ScaleSprite(currentPlayfield.sprite, 1.0f, !stretchPlayfield);
	ScaleSprite(incomingPlayfield.sprite, 1.0f, !stretchPlayfield);

	// make the drop target sprite as large as possible, maintaining
	// aspect ratio
	ScaleSprite(dropTargetSprite, 1.0f, true);
}

// Start a menu animation
void PlayfieldView::StartMenuAnimation(bool opening)
{
	// if an info box is showing, hide it
	HideInfoBox();

	// set the new mode
	menuAnimMode = opening ? MenuAnimOpen : MenuAnimClose;

	// start the animation timer running, noting our start time
	StartAnimTimer(menuAnimStartTime);

	// update the javascript UI mode
	UpdateJsUIMode();
}

// Start a playfield crossfade
void PlayfieldView::StartPlayfieldCrossfade()
{
	// start the animation timer, and note the starting time for the fade
	StartAnimTimer();

	// start the fade in the sprite
	static const DWORD playfieldCrossFadeTime = 120;
	incomingPlayfield.sprite->StartFade(1, playfieldCrossFadeTime);
}

// Start the animation timer if it's not already running
const DWORD AnimTimerInterval = 8;
void PlayfieldView::StartAnimTimer(DWORD &startTime)
{
	// start the animation timer if it's not already running
	StartAnimTimer();

	// note the starting time for the animation mode
	startTime = GetTickCount();
}

void PlayfieldView::StartAnimTimer()
{
	if (!isAnimTimerRunning)
	{
		SetTimer(hWnd, animTimerID, AnimTimerInterval, 0);
		isAnimTimerRunning = true;
	}
}

void PlayfieldView::UpdateInfoBox()
{
	// start the timer to check for an update
	SetTimer(hWnd, infoBoxSyncTimerID, 250, 0);

	// update the javascript UI mode
	UpdateJsUIMode();
}

void PlayfieldView::SyncInfoBox()
{
	// Don't show the info box if there's a menu or popup
	// showing, or if any animation is running.
	if (isAnimTimerRunning || popupSprite != nullptr || curMenu != nullptr || attractMode.active)
		return;

	// skip the info box if it's disabled in the options
	if (!infoBoxOpts.show)
		return;

	// get the current selection and check for a change
	GameListItem *game = GameList::Get()->GetNthGame(0);
	if (game != infoBox.game)
	{
		// prepare the new info box
		if (IsGameValid(game))
		{
			// request high score information if we don't already have it
			RequestHighScores(game, true);

			// set our initial proposed width and height
			int width = 712, height = 343;

			// draw the box contents
			auto Draw = [this, game, &width, &height](HDC hdc, HBITMAP)
			{
				// set up a GDI+ drawing context on the DC
				Gdiplus::Graphics g(hdc);

				// draw the background
				Gdiplus::SolidBrush bkg(Gdiplus::Color(192, 0, 0, 0));
				g.FillRectangle(&bkg, 0, 0, width, height);

				// draw the frame
				Gdiplus::Pen pen(Gdiplus::Color(192, 255, 255, 255), 4);
				g.DrawRectangle(&pen, 2, 2, width - 4, height - 4);

				// set up for text drawing
				Gdiplus::SolidBrush txt(Gdiplus::Color(255, 255, 255, 255));
				const int marginX = 24, marginY = 16;
				Gdiplus::RectF rcLayout((float)marginX, (float)marginY, (float)(width - 2*marginX), (float)(height - 2*marginY));
				Gdiplus::PointF origin((float)marginX, (float)marginY);

				// add the logo or title, if desired
				FontPref &titleFont = infoBoxTitleFont;
				TSTRING wheelFile;
				std::unique_ptr<Gdiplus::Bitmap> wheelImage;
				if (infoBoxOpts.gameLogo
					&& game->GetMediaItem(wheelFile, GameListItem::wheelImageType)
					&& (wheelImage.reset(Gdiplus::Bitmap::FromFile(wheelFile.c_str())), wheelImage != nullptr))
				{
					// scale the logo to the text height
					float txtHt = titleFont->GetHeight(&g);
					float ht = txtHt * 1.4f;
					float wid = (float)wheelImage->GetWidth() / (float)wheelImage->GetHeight() * ht;
					g.DrawImage(wheelImage.get(), origin.X, origin.Y, wid, ht);
					origin.Y += ht + 12;
				}
				else if (infoBoxOpts.title)
				{
					// draw the title as text
					GPDrawStringAdv(g, game->title.c_str(), titleFont, &txt, origin, rcLayout);
					origin.Y += 12;
				}

				// build the machine type + year string: start with the type
				TSTRING typeAndYear;
				if (game->tableType.length() != 0)
				{
					if (infoBoxOpts.tableTypeAbbr)
						typeAndYear += game->tableType;
					else if (infoBoxOpts.tableType)
					{
						if (auto tt = tableTypeNameMap.find(game->tableType); tt != tableTypeNameMap.end())
							typeAndYear += tt->second;
					}
				}

				// add the year
				if (game->year != 0 && infoBoxOpts.year != 0)
				{
					if (typeAndYear.length() != 0)
						typeAndYear += _T(", ");
					typeAndYear += MsgFmt(_T("%d"), game->year);
				}

				// add the manufacturer, type, and year
				FontPref &txtFont = infoBoxFont;
				Gdiplus::Image *manufLogo;
				if (infoBoxOpts.manufLogo && LoadManufacturerLogo(manufLogo, game->manufacturer, game->year))
				{
					// scale the image according to the text height
					float txtHt = txtFont->GetHeight(&g);
					float ht = txtHt * 1.4f;
					float wid = (float)manufLogo->GetWidth()/(float)manufLogo->GetHeight() * ht;
					g.DrawImage(manufLogo, origin.X, origin.Y, wid, ht);

					// add the type and year, if non-empty
					if (typeAndYear.length() != 0)
						g.DrawString(MsgFmt(_T("  (%s)"), typeAndYear.c_str()), -1, txtFont,
							Gdiplus::PointF(origin.X + wid, origin.Y + txtHt * .2f), &txt);

					// advance past it
					origin.Y += ht + 10;
				}
				else if (infoBoxOpts.manuf && game->manufacturer != nullptr)
				{
					// Set up the string to draw
					TSTRING str = game->manufacturer->manufacturer;
					if (typeAndYear.length() != 0)
					{
						str += _T(" (");
						str += typeAndYear;
						str += _T(")");
					}

					// draw the combined tstring
					GPDrawString gp(g, rcLayout);
					gp.curOrigin = origin;
					gp.DrawString(str.c_str(), txtFont, &txt);
					origin = gp.curOrigin;
				}
				else if (typeAndYear.length() != 0)
				{
					// draw just the type and year string
					GPDrawStringAdv(g, typeAndYear.c_str(), txtFont, &txt, origin, rcLayout);
				}

				// add the system
				Gdiplus::Image *systemLogo;
				if (infoBoxOpts.systemLogo && LoadSystemLogo(systemLogo, game->system))
				{
					// scale it to the text height
					float txtHt = txtFont->GetHeight(&g);
					float ht = txtHt;
					float wid = (float)systemLogo->GetWidth() / (float)systemLogo->GetHeight() * ht;
					g.DrawImage(systemLogo, origin.X, origin.Y, wid, ht);

					// advance past it
					origin.Y += ht + 10;
				}
				else if (infoBoxOpts.system && game->system != nullptr)
				{
					// draw the system name as text
					GPDrawStringAdv(g, game->system->displayName.c_str(), txtFont, &txt, origin, rcLayout);
				}

				// add the game file name
				if (infoBoxOpts.tableFile && game->filename.length() != 0)
				{
					Gdiplus::SolidBrush gray(Gdiplus::Color(255, 192, 192, 192));
					GPDrawStringAdv(g, game->filename.c_str(), infoBoxDetailFont, &gray, origin, rcLayout);
				}

				// add the rating, if set
				float rating;
				if (infoBoxOpts.rating 
					&& stars != nullptr 
					&& (rating = GameList::Get()->GetRating(game)) >= 0)
				{
					origin.Y += stars.get()->GetHeight()/3;
					DrawStars(g, origin.X, origin.Y, rating);
					origin.Y += stars.get()->GetHeight()*4/3;
				}

				// if the actual height is more than the original proposed
				// height, increase the box height
				int actualHeight = (int)ceilf(origin.Y + marginY) + 4;
				if (actualHeight > height)
					height = actualHeight;

				// make sure the bitmap is updated
				g.Flush();
			};

			// do one drawing pass to a memory DC, just to measure the height
			MemoryDC memdc;
			Draw(memdc, NULL);
			
			// create the new sprite
			Application::InUiErrorHandler eh;
			infoBox.sprite.Attach(new Sprite());
			infoBox.sprite->Load(width, height, Draw, eh, _T("Info Box"));

			// move it up towards the top of the screen
			infoBox.sprite->offset.y = 0.25f;
			infoBox.sprite->UpdateWorld();

			// start the fade-in animation
			infoBox.sprite->alpha = 0;
			SetTimer(hWnd, infoBoxFadeTimerID, AnimTimerInterval, 0);
			infoBoxStartTime = GetTickCount();
		}
		else
		{
			// no new game - simply remove the box
			infoBox.sprite = nullptr;
		}

		// set the new game
		infoBox.game = game;

		// the drawing list has changed
		UpdateDrawingList();
	}
	else if (infoBox.sprite != nullptr && infoBox.sprite->alpha == 0.0f)
	{
		// start the fade
		SetTimer(hWnd, infoBoxFadeTimerID, AnimTimerInterval, 0);
		infoBoxStartTime = GetTickCount();
	}

	// we've completed the update, so remove the sync timer
	KillTimer(hWnd, infoBoxSyncTimerID);
}

void PlayfieldView::HideInfoBox()
{
	if (infoBox.sprite != 0)
	{
		infoBox.sprite->alpha = 0;
		KillTimer(hWnd, infoBoxFadeTimerID);
		KillTimer(hWnd, infoBoxSyncTimerID);
	}
}

// update the info box animation
void PlayfieldView::UpdateInfoBoxAnimation()
{
	static DWORD lastTimer;
	DWORD thisTimer = GetTickCount();
	lastTimer = thisTimer;

	// make sure there's an info box to update
	if (infoBox.sprite == nullptr)
	{
		KillTimer(hWnd, infoBoxFadeTimerID);
		return;
	}

	// figure the progress
	const float infoBoxAnimTime = 250.0f;
	float progress = fminf(1.0f, float(GetTickCount() - infoBoxStartTime) / infoBoxAnimTime);

	// update the fade
	infoBox.sprite->alpha = progress;

	// end the animation if we've reached the end of the fade
	if (progress == 1.0f)
	{
		lastTimer = 0;
		KillTimer(hWnd, infoBoxFadeTimerID);
	}
}

bool PlayfieldView::LoadManufacturerLogo(Gdiplus::Image* &image, const GameManufacturer *manuf, int year)
{
	// fail if there's no manufacturer
	if (manuf == nullptr)
		return nullptr;

	// try a cache lookup first
	if (auto it = manufacturerLogoMap.find(manuf->manufacturer); it != manufacturerLogoMap.end())
	{
		image = it->second.get();
		return true;
	}

	// look up the file
	TSTRING filename;
	if (GetManufacturerLogo(filename, manuf, year))
	{
		// try loading the image
		if (auto i = Gdiplus::Bitmap::FromFile(filename.c_str()); i != nullptr)
		{
			// add it to the cache
			manufacturerLogoMap.emplace(manuf->manufacturer, i);

			// return the image pointer
			image = i;
			return true;
		}
	}

	// not found or failed to load
	return false;
}

bool PlayfieldView::LoadSystemLogo(Gdiplus::Image* &image, const GameSystem *system)
{
	// fail if there's no system
	if (system == nullptr)
		return nullptr;

	// try a cache lookup first
	if (auto it = systemLogoMap.find(system->displayName); it != systemLogoMap.end())
	{
		image = it->second.get();
		return true;
	}

	// look up the file
	TSTRING filename;
	if (GetSystemLogo(filename, system))
	{
		// try loading the image
		if (auto i = Gdiplus::Bitmap::FromFile(filename.c_str()); i != nullptr)
		{
			// cache it
			systemLogoMap.emplace(system->displayName, i);

			// return it
			image = i;
			return true;
		}
	}

	// not found or failed to load
	return false;
}

bool PlayfieldView::GetManufacturerLogo(TSTRING &result, const GameManufacturer *manuf, int year)
{
	// if there's no manufacturer, there's no logo
	if (manuf == nullptr)
		return false;

	// get the Company Logos media folder
	TCHAR folder[MAX_PATH];
	PathCombine(folder, GameList::Get()->GetMediaPath(), _T("Company Logos"));

	// We scan for files with names of these forms:
	//
	//   Gottlieb (1990-1997).png  - matches Gottlieb for years 1990 through 1997
	//   Gottlieb (-1990).png      - matches Gottlieb for all years through 1990
	//   Gottlieb (1990-).png      - matches Gottlieb for all years 1990 and later
	//   Gottlieb (1990).png       - matches Gottlieb for year 1990 only
	//   Gottlieb.png              - matches company name Gottlieb, any year
	//
	// If the year passed in is non-zero, we'll match the first file we find
	// with a year span that includes the given year.  If the year given is 
	// zero, we'll match the file with the year span with the highest ending
	// year.  In either case, if we don't find a matching file with a year
	// span, we'll match the file (if any) that exactly matches the company 
	// name.
	TSTRING exactMatch;
	TSTRING highestYearMatch;
	int highestEndingYear = 0;

	// scan files in the media folder
	namespace fs = std::experimental::filesystem;
	for (auto &f : fs::directory_iterator(folder))
	{
		// only consider ordinary files with appropriate image extensions
		static const std::basic_regex<wchar_t> extPat(L"(.*)\\.(png)", std::regex_constants::icase);
		std::match_results<const wchar_t*> m; 
		WSTRING fname = f.path().filename().wstring();
		if (f.status().type() == fs::file_type::regular	&& std::regex_match(fname.c_str(), m, extPat))
		{
			// pull out the base filename
			WSTRING basename = m[1].str();

			// Check for a year span pattern
			static const std::basic_regex<wchar_t> yearPat(L"(.*)\\s*\\((\\d{4})?(-)?(\\d{4})?\\)");
			if (std::regex_match(basename.c_str(), m, yearPat))
			{
				// reject this one out of hand if the name doesn't match
				if (_tcsicmp(WSTRINGToTSTRING(m[1].str()).c_str(), manuf->manufacturer.c_str()) != 0)
					continue;

				// get the starting year if matched, otherwise 0 for the beginning of time
				int startYear =  m[2].matched ? _wtoi(m[2].str().c_str()) : 0;

				// If '-' was present: if there's an explicit ending year, get it, 
				// otherwise use 9999 for the end of time.  If no '-' is present,
				// it's a one-year span, so end year == start year.
				int endYear = m[3].matched ? (m[4].matched ? _wtoi(m[4].str().c_str()) : 9999) : startYear;

				// If a year was given, and it's within the span, this is
				// automatically the best match.
				if (year != 0 && year >= startYear && year <= endYear)
				{
					result = WideToTSTRING(f.path().c_str());
					return true;
				}

				// If a year was specified, and it's not later than the
				// end date of this span, and this span is the latest we've
				// seen so far, remember it as the closest match.  Or, if
				// no year was specified, just remember the highest ending
				// year.
				if (endYear > highestEndingYear
					&& ((year != 0 && year < endYear) || year == 0))
				{
					highestYearMatch = f.path();
					highestEndingYear = endYear;
				}
			}
			else if (_tcsicmp(WSTRINGToTSTRING(basename).c_str(), manuf->manufacturer.c_str()) == 0)
			{
				// This is an exact match for the name, but it has no year.
				// If no year was specified in the query, use this as the
				// automatic match.
				if (year == 0)
				{
					result = WideToTSTRING(f.path().c_str());
					return true;
				}

				// remember it as the exact match
				exactMatch = f.path();
			}
		}
	}

	// We didn't find an exact match for the year, so return the next
	// best thing.  If we found any year matches the manufacturer, use
	// the one with the highest ending year of the candidates we found.
	// If not, use the exact match, if we found one.
	if (highestYearMatch.length() != 0)
	{
		result = WSTRINGToTSTRING(highestYearMatch);
		return true;
	}
	if (exactMatch.length() != 0)
	{
		result = WSTRINGToTSTRING(exactMatch);
		return true;
	}

	// no results found
	return false;
}

bool PlayfieldView::GetSystemLogo(TSTRING &result, const GameSystem *system)
{
	// if there's no system, there's no logo
	if (system == nullptr)
		return false;

	// get the System Logos media folder
	TCHAR folder[MAX_PATH];
	PathCombine(folder, GameList::Get()->GetMediaPath(), _T("System Logos"));

	// Prefix match.  If we find a file whose name is a leading
	// substring of the system name, keep it as a partial match.
	// This allows matching a generic "Visual Pinball.png" file
	// to "Visual Pinball 9.2" and other similar versioned system
	// names.
	WSTRING prefixMatch;

	// scan files in the media folder
	namespace fs = std::experimental::filesystem;
	for (auto &f : fs::directory_iterator(folder))
	{
		// only consider ordinary files with appropriate image extensions
		static const std::basic_regex<WCHAR> extPat(_T("(.*)\\.(png)"), std::regex_constants::icase);
		std::match_results<std::wstring::const_iterator> m;
		WSTRING path = f.path().filename().wstring();
		if (f.status().type() == fs::file_type::regular
			&& std::regex_match(path, m, extPat))
		{
			// check for an exact match
			TSTRING basename = WSTRINGToTSTRING(m[1].str());
			if (_tcsicmp(basename.c_str(), system->displayName.c_str()) == 0)
			{
				result = WideToTSTRING(f.path().c_str());
				return true;
			}

			// check for a prefix match
			if (tstriStartsWith(system->displayName.c_str(), basename.c_str()))
				prefixMatch = f.path().c_str();
		}
	}

	// no exact match found; if we found a prefix match, use that
	if (prefixMatch.length() != 0)
	{
		result = WSTRINGToTSTRING(prefixMatch);
		return true;
	}

	// no match
	return false;
}

void PlayfieldView::EndAnimation()
{
	// stop the animation timer
	KillTimer(hWnd, animTimerID);
	isAnimTimerRunning = false;

	// process any key events queued during the animation
	ProcessKeyQueue();

	// sync the playfield to the game list when idle
	SetTimer(hWnd, pfTimerID, 100, 0);

	// check for a game info box update
	UpdateInfoBox();
}

// update audio fadeout
void PlayfieldView::UpdateAudioFadeout()
{
	// presume we won't need to keep running
	bool keepRunning = false;

	// scan for audio tracks needing fadeout
	for (auto it = activeAudio.begin(); it != activeAudio.end(); )
	{
		// remember the next list entry, in case we delete this one
		auto nxt = it;
		++nxt;

		// check the clip type
		auto &audio = it->second;
		switch (audio.clipType)
		{
		case ActiveAudio::LaunchAudio:
			// don't fade launch audio
			break;

		default:
			// Fade out other types.  We run on a 20ms timer = 50 iterations
			// per second, so a linear 1-second fade requires a 1/50 = 2%
			// reduction per iteration.
			audio.fade -= .02f;
			if (auto newvol = static_cast<int>(roundf(static_cast<float>(audio.volume) * audio.fade)); newvol > 0)
			{
				// not at zero yet - set the new volume
				audio.player->SetVolume(newvol);

				// we'll need to keep running the timer to continue fading this item
				keepRunning = true;
			}
			else
			{
				// track is inaudible - stop it and remove it from the list
				audio.player->Stop(SilentErrorHandler());
				audio.player->Shutdown();
				activeAudio.erase(it);
			}
			break;
		}

		// on to the next one
		it = nxt;
	}

	// kill the timer if we didn't find any items that need further work
	if (!keepRunning)
		KillTimer(hWnd, audioFadeoutTimerID);
}

// Update the animation
void PlayfieldView::UpdateAnimation()
{
	// presume no sprite update will be needed
	bool updateDrawingList = false;

	// presume that the current animation is done
	bool done = true;

	// animation sequence timings
	const DWORD popupOpenTime = 150;
	const DWORD popupCloseTime = 150;
	const DWORD fastWheelTime = 50;
	const DWORD menuOpenTime = 150;
	const DWORD menuCloseTime = 150;
	const DWORD videoStartTimeout = 1500;

	// Animate the playfield cross-fade
	if (incomingPlayfield.sprite != 0)
	{
		// the sprite manages its own fade - check to see if it's done
		if (incomingPlayfield.sprite->IsFadeDone())
		{
			// End of cross-fade.  Switch the active playfields and
			// forget the old one.
			currentPlayfield = incomingPlayfield;
			updateDrawingList = true;

			// clear the incoming playfield
			incomingPlayfield.Clear();

			// Sync the backglass.  Defer this via a posted message so
			// that we update rendering before we do the sync; this makes
			// transitions smoother than if we loaded all of the media
			// files in the same message loop cycle.  The backglass sync
			// handler will in turn fire off a deferred sync to the DMD,
			// which will fire off a deferred sync to the topper.
			PostMessage(WM_COMMAND, ID_SYNC_BACKGLASS);
	
			// update DOF for the new game
			QueueDOFPulse(L"PBYGameSelect");

			// set the DOF ROM
			dof.SyncSelectedGame();
		}
		else if (!incomingPlayfield.sprite->IsFading()
			&& GetTickCount() - incomingPlayfieldLoadTime > videoStartTimeout)
		{
			// It's been too long since we started the sprite loading.  It
			// might not be able to load a video or start playback.  Force the
			// crossfade to start so that we don't get stuck in this state.
			StartPlayfieldCrossfade();
			done = false;
		}
		else
		{
			// still in the cross-fade
			done = false;
		}
	}

	// Animate the popup open/close
	if (popupAnimMode != PopupAnimNone && popupSprite != nullptr)
	{
		// figure the elapsed time
		DWORD dt = GetTickCount() - popupAnimStartTime;

		// check the mode
		if (popupAnimMode == PopupAnimOpen)
		{
			if (dt < popupOpenTime)
			{
				// still running - update the animation and keep running
				UpdatePopupAnimation(true, float(dt) / float(popupOpenTime));
				done = false;
			}
			else
			{
				// done - show the final animation step
				UpdatePopupAnimation(true, 1.0f);
				popupAnimMode = PopupAnimNone;
			}
		}
		else
		{
			if (dt < popupCloseTime)
			{
				// still running - update the animation and keep running
				UpdatePopupAnimation(false, float(dt) / float(popupCloseTime));
				done = false;
			}
			else
			{
				// remove the popup and end the animation
				popupSprite = nullptr;
				popupType = PopupNone;
				updateDrawingList = true;
				popupAnimMode = PopupAnimNone;

				// Fire the end-popup event.  This event fires after the fact, so ignore
				// any attempted cancellation.
				FirePopupEvent(false, popupName.c_str());

				// if there's a queued error popup, display it
				if (queuedErrors.size() != 0)
				{
					ShowQueuedError();
					done = false;
				}
			}
		}
	}

	// Animate the running game overlay
	if (runningGamePopupMode != RunningGamePopupNone && runningGamePopup != nullptr)
	{
		// update the fade
		DWORD dt = GetTickCount() - runningGamePopupStartTime;
		float progress = fminf(1.0f, float(dt) / float(popupOpenTime));
		runningGamePopup->alpha = (runningGamePopupMode == RunningGamePopupOpen ? progress : 1.0f - progress);
		if (progress == 1.0f)
		{
			switch (runningGamePopupMode)
			{
			case RunningGamePopupOpen:
				// opening - clear the playfield sprites
				currentPlayfield.Clear();
				incomingPlayfield.Clear();
				updateDrawingList = true;
				break;

			case RunningGamePopupClose:
				// closing - remove the popup
				runningGamePopup = 0;
				updateDrawingList = true;
				break;
			}

			// done with this animation
			runningGamePopupMode = RunningGamePopupNone;
		}
		else
		{
			// not done
			done = false;
		}
	}

	// animate the current sequence
	if (wheelAnimMode != WheelAnimNone && animAddedToWheel != 0)
	{
		// note the time since the animation started
		DWORD dt = GetTickCount() - wheelAnimStartTime;

		// note the direction we're going
		int dn = animWheelDistance > 0 ? 1 : -1;

		// Figure our progress in the wheel animation
		DWORD t = wheelAnimMode == WheelAnimNormal ? wheelTime : fastWheelTime;
		float progress = fminf(float(dt) / float(t), 1.0f);

		// update wheel positions
		int n = animFirstInWheel;
		for (auto& s : wheelImages)
		{
			SetWheelImagePos(s, n, progress*dn);
			++n;
		}

		// check for end of animation
		if (progress >= 1.0f)
		{
			// Discard the outgoing wheel images
			while (animAddedToWheel-- != 0)
			{
				if (dn > 0)
					wheelImages.pop_front();
				else
					wheelImages.pop_back();
			}

			// we've updated the sprite list
			updateDrawingList = true;

			// the animation is done
			wheelAnimMode = WheelAnimNone;
		}
		else
			done = false;
	}

	// Animate the menu
	if (menuAnimMode != MenuAnimNone)
	{
		// note the time
		DWORD dt = GetTickCount() - menuAnimStartTime;

		// check the mode
		if (menuAnimMode == MenuAnimOpen)
		{
			// opening a menu
			if (curMenu != nullptr)
			{
				if (dt < menuOpenTime)
				{
					// still running - update the animation and keep running
					UpdateMenuAnimation(curMenu, true, float(dt) / float(menuOpenTime));
					done = false;
				}
				else
				{
					// Done - the menu is now fully open.  Make sure it gets animated
					// at the exact final position, and allow the animation to end.
					UpdateMenuAnimation(curMenu, true, 1.0f);
					menuAnimMode = MenuAnimNone;
				}
			}
		}
		else
		{
			// closing a menu
			if (curMenu != nullptr && dt < menuCloseTime)
			{
				// still running - update the animation and keep running
				UpdateMenuAnimation(curMenu, false, float(dt) / float(menuCloseTime));
				done = false;
			}
			else if (newMenu != nullptr)
			{
				// We're done with the outgoing menu, and we're switching to a
				// new incoming menu.  Move the new menu over to the current 
				// menu slot and start the incoming menu animation.
				OnCloseMenu(&newMenu->descs);
				curMenu = newMenu;
				newMenu = nullptr;
				StartMenuAnimation(true);
				done = false;

				// we need the update the drawing list for the menu change
				updateDrawingList = true;
			}
			else
			{
				// We're done with the outgoing menu, and there's no incoming
				// menu, which means there's now no menu at all.  Forget the
				// old one and update the drawing list for the change.
				OnCloseMenu(nullptr);
				curMenu = nullptr;
				updateDrawingList = true;
				menuAnimMode = MenuAnimNone;

				// return to wheel mode in DOF
				dof.SetUIContext(L"PBYWheel");
			}
		}
	}

	// check if we've reached the end of the animation
	if (done)
		EndAnimation();

	// update the sprite list if necessary
	if (updateDrawingList)
		UpdateDrawingList();
}

void PlayfieldView::UpdateMenu(HMENU hMenu, BaseWin *fromWin)
{
	// update base class items
	__super::UpdateMenu(hMenu, fromWin);

	// update frame items via the parent
	HWND hwndParent = GetParent(hWnd);
	if (fromWin != 0 && fromWin->GetHWnd() != hwndParent)
		::SendMessage(hwndParent, BWMsgUpdateMenu, (WPARAM)hMenu, (LPARAM)this);

	// update the real-DMD enabling commands
	auto dmdStat = GetRealDMDStatus();
	CheckMenuItem(hMenu, ID_REALDMD_AUTO_ENABLE, MF_BYCOMMAND | (dmdStat == RealDMDAuto ? MF_CHECKED : MF_UNCHECKED));
	CheckMenuItem(hMenu, ID_REALDMD_ENABLE, MF_BYCOMMAND | (dmdStat == RealDMDEnable ? MF_CHECKED : MF_UNCHECKED));
	CheckMenuItem(hMenu, ID_REALDMD_DISABLE, MF_BYCOMMAND | (dmdStat == RealDMDDisable ? MF_CHECKED : MF_UNCHECKED));
	CheckMenuItem(hMenu, ID_REALDMD_AUTO_ENABLE, MF_BYCOMMAND | (dmdStat == RealDMDAuto ? MF_CHECKED : MF_UNCHECKED));

	// update the real-DMD orientation commands; disable these if the DMD isn't
	// currently active
	UINT ena = MF_BYCOMMAND | MF_DISABLED;
	if (realDMD != nullptr)
	{
		ena = (ena & ~MF_DISABLED) | MF_ENABLED;
		CheckMenuItem(hMenu, ID_REALDMD_MIRROR_HORZ, MF_BYCOMMAND | (realDMD->IsMirrorHorz() ? MF_CHECKED : MF_UNCHECKED));
		CheckMenuItem(hMenu, ID_REALDMD_MIRROR_VERT, MF_BYCOMMAND | (realDMD->IsMirrorVert() ? MF_CHECKED : MF_UNCHECKED));
	}
	EnableMenuItem(hMenu, ID_REALDMD_MIRROR_HORZ, ena);
	EnableMenuItem(hMenu, ID_REALDMD_MIRROR_VERT, ena);
}

bool PlayfieldView::OnRawInputEvent(UINT rawInputCode, RAWINPUT *raw, DWORD dwSize)
{
	// If this is a keyboard event, translate the virtual key and
	// keep track of shift key changes.
	USHORT vkey = 0;
	if (raw->header.dwType == RIM_TYPEKEYBOARD)
	{
		// get the vkey, translated for special key distinctions
		// (left/right shift, keypad versions of keys, etc)
		vkey = InputManager::GetInstance()->TranslateVKey(raw);

		// if it's a shift key, update our internal shift state tracking
		switch (vkey)
		{
		case VK_LSHIFT:
			rawShiftKeyState.left = ((raw->data.keyboard.Flags & RI_KEY_BREAK) == 0);
			break;

		case VK_RSHIFT:
			rawShiftKeyState.right = ((raw->data.keyboard.Flags & RI_KEY_BREAK) == 0);
			break;
		}
	}

	// If this is a "sink" event, it means that the key was pressed
	// while we're in the background.  These events won't turn into
	// regular WM_KEYxxx events, since we don't have focus.  Check
	// for special commands that are active in the background in
	// this case.  If it's a normal "input" event, we don't need any
	// special handling as the key will enter our normal event queue.
	if (rawInputCode == RIM_INPUTSINK)
	{
		// If this is a keyboard event, queue it as a background key 
		// event.
		if (raw->header.dwType == RIM_TYPEKEYBOARD)
		{
			// Figure if it's a key-down or key-up based on the make/break
			// state.  Note that the Windows headers are muddled about the 
			// bit flags here: they provide the 0 and 1 bit values without 
			// the masks to isolate them.  Specifically, they define
			// RI_KEY_MAKE as 0 and RI_KEY_BREAK as 1.  So the usual C 
			// idiom of (x & FLAG) doesn't work here with RI_KEY_MAKE, 
			// since it's not really a bit flag, it's really a bit value
			// after masking off the other bits.  I'm not sure why they
			// bothered to define a bit flag with no bits, but there it is.
			// This means the test for MAKE is really !BREAK.
			bool down = ((raw->data.keyboard.Flags & RI_KEY_BREAK) == 0);
			bool repeat = ((raw->data.keyboard.Flags & RI_KEY_AUTOREPEAT) != 0);
			KeyPressType keyType = repeat ? KeyBgRepeat : down ? KeyBgDown : KeyUp;

			// run it through any Javascript event handlers
			if (FireKeyEvent(vkey, down, repeat, true))
			{
				// look up the command; if we find a match, process the key press
				if (auto it = vkeyToCommand.find(vkey); it != vkeyToCommand.end())
				{
					// process the key press
					ProcessKeyPress(hWnd, keyType, true, false, it->second);
				}
			}
		}
	}

	// if it's a keyboard event, note the event time, for the purposes 
	// of the running game inactivity timer
	if (raw->header.dwType == RIM_TYPEKEYBOARD)
		lastInputEventTime = GetTickCount();

	// Whatever we did with the event, we don't "consume" it in the sense
	// of blocking other subscribers from seeing it.  Return false to allow
	// the event to propagate to other subscribers.
	return false;
}

bool PlayfieldView::OnJoystickButtonChange(
	JoystickManager::PhysicalJoystick *js,
	int button, bool pressed, bool foreground)
{
	// note the time of the event
	lastInputEventTime = GetTickCount();

	// figure the key press mode
	KeyPressType mode = pressed ? (foreground ? KeyDown : KeyBgDown) : KeyUp;

	// fire a javascript joystick event; if that says to ignore it, we're done
	if (!FireJoystickEvent(js->logjs->index, button, pressed, false, !foreground))
		return false;

	// look up the button in the command table
	if (auto it = jsCommands.find(JsCommandKey(js->logjs->index, button)); it != jsCommands.end())
	{
		// process the key press
		ProcessKeyPress(hWnd, mode, !foreground, false, it->second);

		// if it's a key-press event, start auto-repeat; otherwise cancel
		// any existing auto-repeat
		if (pressed)
			JsAutoRepeatStart(js->logjs->index, button, foreground ? KeyRepeat : KeyBgRepeat);
		else
			StopAutoRepeat();
	}

	// return 'false' to allow other the event to be forwarded to
	// any other subscribers
	return false;
}

void PlayfieldView::KbAutoRepeatStart(int vkey, int vkeyOrig, KeyPressType repeatMode)
{
	// remember the key for auto-repeat
	kbAutoRepeat.vkey = vkey;
	kbAutoRepeat.vkeyOrig = vkeyOrig;
	kbAutoRepeat.repeatMode = repeatMode;

	// auto-repeat is now active
	kbAutoRepeat.active = true;

	// Start the delay timer for the initial key repeat delay.  The
	// Windows parameter is documented as being only an approximation,
	// but it's supposed to be in 250ms units with a minimum of 250ms.
	//
	// Note that this replaces any previous auto-repeat timer, which
	// has exactly the desired effect of starting a new repeat timing
	// cycle for the new key press.
	int kbDelay;
	SystemParametersInfo(SPI_GETKEYBOARDDELAY, 0, &kbDelay, 0);
	SetTimer(hWnd, kbRepeatTimerID, 250 + kbDelay * 250, 0);
}

void PlayfieldView::OnKbAutoRepeatTimer()
{
	// If a key is active, execute an auto-repeat
	if (kbAutoRepeat.active)
	{
		// if the key isn't still pressed, cancel auto-repeat mode
		if (GetAsyncKeyState(kbAutoRepeat.vkeyOrig) >= 0)
		{
			KillTimer(hWnd, kbRepeatTimerID);
			return;
		}

		// don't deliver auto-repeat events when a wheel animation is running
		if (wheelAnimMode == WheelAnimNone)
		{
			// fire a javascript key event; if that says to ignore it, we're done
			if (FireKeyEvent(kbAutoRepeat.vkey, true, true, kbAutoRepeat.repeatMode == KeyBgRepeat))
			{
				// look up the button in the command table
				if (auto it = vkeyToCommand.find(kbAutoRepeat.vkey); it != vkeyToCommand.end())
					ProcessKeyPress(hWnd, kbAutoRepeat.repeatMode, kbAutoRepeat.repeatMode == KeyBgRepeat, false, it->second);
			}
		}

		// Reset the timer for the repeat interval.  The system parameter
		// for the repeat rate is only approximate, since the actual repeat
		// function is farmed out to the keyboard hardware, but the nominal
		// unit system is a frequency value from 0 to 31, where 0 represents
		// 2.5 Hz and 31 represents 30 Hz.  So we'll interpolate linearly
		// over this range and invert the value to get a time value.  We
		// only need a value to the nearest millisecond when we're done, so
		// to make the calculation fast, use scaled integers.  Calculate the
		// frequency value in mHz units by multiplying everything by 1000;
		// inverting this give us kilo seconds, so we then multiply the
		// result by a million to get milliseconds.  At this scale, 
		// everything is in a nice range for the integer calculations.
		DWORD rate;
		SystemParametersInfo(SPI_GETKEYBOARDSPEED, 0, &rate, 0);
		SetTimer(hWnd, kbRepeatTimerID, 1000000 / (2500 + 917 * rate), 0);
	}
}

void PlayfieldView::JsAutoRepeatStart(int unit, int button, KeyPressType repeatMode)
{
	// remember the key for auto-repeat
	jsAutoRepeat.unit = unit;
	jsAutoRepeat.button = button;
	jsAutoRepeat.repeatMode = repeatMode;

	// remember that we're in auto-repeat mode
	jsAutoRepeat.active = true;

	// Start the delay timer for the initial key repeat delay.  The
	// Windows parameter is documented as being only an approximation,
	// but it's supposed to be in 250ms units with a minimum of 250ms.
	//
	// Note that this replaces any previous auto-repeat timer, which
	// has exactly the desired effect of starting a new repeat timing
	// cycle for the new key press.
	int kbDelay;
	SystemParametersInfo(SPI_GETKEYBOARDDELAY, 0, &kbDelay, 0);
	SetTimer(hWnd, jsRepeatTimerID, 250 + kbDelay * 250, 0);
}

void PlayfieldView::OnJsAutoRepeatTimer()
{
	// If a key is active, execute an auto-repeat
	if (jsAutoRepeat.active)
	{
		// don't deliver auto-repeat events when a wheel animation is running
		if (wheelAnimMode == WheelAnimNone)
		{
			// fire a joystick event
			if (FireJoystickEvent(jsAutoRepeat.unit, jsAutoRepeat.button, true, true, jsAutoRepeat.repeatMode == KeyBgRepeat))
			{
				// look up the button in the command table
				if (auto it = jsCommands.find(JsCommandKey(jsAutoRepeat.unit, jsAutoRepeat.button)); it != jsCommands.end())
					ProcessKeyPress(hWnd, jsAutoRepeat.repeatMode, jsAutoRepeat.repeatMode == KeyBgRepeat, false, it->second);
			}
		}

		// Reset the timer for the repeat interval.  The system parameter
		// for the repeat rate is only approximate, since the actual repeat
		// function is farmed out to the keyboard hardware, but the nominal
		// unit system is a frequency value from 0 to 31, where 0 represents
		// 2.5 Hz and 31 represents 30 Hz.  So we'll interpolate linearly
		// over this range and invert the value to get a time value.  We
		// only need a value to the nearest millisecond when we're done, so
		// to make the calculation fast, use scaled integers.  Calculate the
		// frequency value in mHz units by multiplying everything by 1000;
		// inverting this give us kilo seconds, so we then multiply the
		// result by a million to get milliseconds.  At this scale, 
		// everything is in a nice range for the integer calculations.
		DWORD rate;
		SystemParametersInfo(SPI_GETKEYBOARDSPEED, 0, &rate, 0);
		SetTimer(hWnd, jsRepeatTimerID, 1000000/(2500 + 917*rate), 0);
	}
}

void PlayfieldView::StopAutoRepeat()
{
	// stop joystick auto-repeat
	if (jsAutoRepeat.active)
	{
		jsAutoRepeat.active = false;
		KillTimer(hWnd, jsRepeatTimerID);
	}

	// stop keyboard auto-repeat
	if (kbAutoRepeat.active)
	{
		kbAutoRepeat.active = false;
		KillTimer(hWnd, kbRepeatTimerID);
	}
}

void PlayfieldView::FireConfigEvent(JsValueRef type, ...)
{
	if (auto js = JavascriptEngine::Get(); js != nullptr)
	{
		// set up to read varargs
		va_list va;
		va_start(va, type);

		// read extra args for certain events
		if (type == jsSettingsPostSaveEvent)
		{
			// postSaveEvent(bool succeeded)
			auto succeeded = va_arg(va, bool);
			js->FireEvent(jsOptionSettings, type, succeeded);
		}
		else
		{
			// others have no extra arguments
			js->FireEvent(jsOptionSettings, type);
		}

		// done with varargs
		va_end(va);
	}
}

bool PlayfieldView::FireFilterSelectEvent(GameListFilter *filter)
{
	bool ret = true;
	if (filter != nullptr)
	{
		if (auto js = JavascriptEngine::Get(); js != nullptr)
			ret = js->FireEvent(jsGameList, jsFilterSelectEvent, filter->GetFilterId());
	}
	return ret;
}

void PlayfieldView::OnConfigPreSave()
{
	FireConfigEvent(jsSettingsPreSaveEvent);
}

void PlayfieldView::OnConfigPostSave(bool succeeded)
{
	FireConfigEvent(jsSettingsPostSaveEvent, succeeded);
}

// Manual start/stop button list
const PlayfieldView::CaptureManualGoButtonMap PlayfieldView::captureManualGoButtonMap[] = {
	{ _T("flippers"), CaptureManualGoButton::Flippers, IDS_CAPSTAT_BTN_FLIPPERS },
	{ _T("magnasave"), CaptureManualGoButton::MagnaSave, IDS_CAPSTAT_BTN_MAGNASAVE },
	{ _T("launch"), CaptureManualGoButton::Launch, IDS_CAPSTAT_BTN_LAUNCH },
	{ _T("info"), CaptureManualGoButton::Info, IDS_CAPSTAT_BTN_INFO },
	{ _T("instructions"), CaptureManualGoButton::Instructions, IDS_CAPSTAT_BTN_INSTR },
};

int PlayfieldView::GetCaptureManualGoButtonNameResId() const
{
	// search for the current capture button
	for (size_t i = 0; i < countof(captureManualGoButtonMap); ++i)
	{
		if (captureManualGoButton == captureManualGoButtonMap[i].id)
			return captureManualGoButtonMap[i].nameStrResId;
	}
	
	// not found - use the flippers by default
	return IDS_CAPSTAT_BTN_FLIPPERS;
}

void PlayfieldView::OnConfigChange()
{
	ConfigManager *cfg = ConfigManager::GetInstance();

	// get the playfield stretch mode
	stretchPlayfield = cfg->GetBool(ConfigVars::PlayfieldStretch, false);

	// load the attract mode settings
	attractMode.enabled = cfg->GetBool(ConfigVars::AttractModeEnabled, true);
	attractMode.idleTime = cfg->GetInt(ConfigVars::AttractModeIdleTime, 60) * 1000;
	attractMode.switchTime = cfg->GetInt(ConfigVars::AttractModeSwitchTime, 5) * 1000;
	attractMode.hideWheelImages = cfg->GetBool(ConfigVars::AttractModeHideWheelImages, true);

	// Get the default font.  If it's undefined or "*", use the system default.
	if (auto df = cfg->Get(ConfigVars::DefaultFontFamily, _T("*")); _tcscmp(df, _T("*")) != 0)
		defaultFontFamily = df;
	else
		defaultFontFamily = _T("Tahoma");

	// load the individual task-specific font preferences
	popupFont.ParseConfig(ConfigVars::PopupFont);
	popupTitleFont.ParseConfig(ConfigVars::PopupTitleFont);
	popupSmallerFont.ParseConfig(ConfigVars::PopupSmallerFont);
	popupDetailFont.ParseConfig(ConfigVars::PopupDetailFont);
	mediaDetailFont.ParseConfig(ConfigVars::MediaDetailFont);
	wheelFont.ParseConfig(ConfigVars::WheelFont);
	menuFont.ParseConfig(ConfigVars::MenuFont);
	menuHeaderFont.ParseConfig(ConfigVars::MenuHeaderFont);
	statusFont.ParseConfig(ConfigVars::StatusFont);
	creditsFont.ParseConfig(ConfigVars::CreditsFont);
	highScoreFont.ParseConfig(ConfigVars::HighScoreFont);
	infoBoxFont.ParseConfig(ConfigVars::InfoBoxFont);
	infoBoxTitleFont.ParseConfig(ConfigVars::InfoBoxTitleFont);
	infoBoxDetailFont.ParseConfig(ConfigVars::InfoBoxDetailFont);

	// load the media capture mode defaults
	RestoreLastCaptureModes();

	// reload the status lines
	InitStatusLines();

	// load the game timeout setting
	gameTimeout = cfg->GetInt(ConfigVars::GameTimeout, 0) * 1000;

	// load the credit balance
	bankedCredits = cfg->GetFloat(ConfigVars::CreditBalance, 0.0f);
	maxCredits = cfg->GetFloat(ConfigVars::MaxCreditBalance, 10.0f);

	// reset the coin balance
	coinBalance = 0.0f;

	// load the coin slot values (defaulting to quarters)
	for (int i = 1; i <= 4; ++i)
		coinVal[i - 1] = cfg->GetFloat(MsgFmt(ConfigVars::CoinSlotValue, i), 0.25f);

	// Load the pricing model.  This is a comma-separated list of 
	// entries like this:  <coinValue> <credits>
	pricePoints.clear();
	const TCHAR *pricingModel = cfg->Get(ConfigVars::PricingModel, _T(".25 .5, .50 1, .75 2, 1.00 3"));
	for (const TCHAR *p = pricingModel; ; )
	{
		// find the next comma-delimited section
		const TCHAR *endp = p;
		for (; *endp != 0 && *endp != ','; ++endp);

		// parse the coin value and credits value
		float coinVal, creditVal;
		if (_stscanf_s(p, _T("%f %f"), &coinVal, &creditVal) == 2)
			pricePoints.emplace_back(coinVal, creditVal);

		// stop if that's the last section
		if (*endp != ',')
			break;

		// advance to the next section
		p = endp + 1;
	}

	// load the button mute and volume settings
	muteButtons = cfg->GetBool(ConfigVars::MuteButtons, false);
	muteRepeatButtons = cfg->GetBool(ConfigVars::MuteRepeatButtons, false);
	buttonVolume = cfg->GetInt(ConfigVars::ButtonVolume, 100);

	// load the capture Manual Go button setting
	const TCHAR *capbtns = cfg->Get(ConfigVars::CaptureManualStartStopButtons, _T("flippers"));
	captureManualGoButton = CaptureManualGoButton::Flippers;
	for (size_t i = 0; i < countof(captureManualGoButtonMap); ++i)
	{
		if (_tcsicmp(captureManualGoButtonMap[i].configName, capbtns) == 0)
		{
			captureManualGoButton = captureManualGoButtonMap[i].id;
			break;
		}
	}

	// if the manual start/stop gesture uses a single button, count
	// the "right" button as always down
	manualGoLeftDown = manualGoRightDown = false;
	switch (captureManualGoButton)
	{
	case CaptureManualGoButton::Flippers:
	case CaptureManualGoButton::MagnaSave:
		// these are two-button gestures - use both variables
		break;

	default:
		// others are single-button gestures - count the "right" button as
		// always down, so that the single "left" button will act as the
		// trigger by itself
		manualGoRightDown = true;
		break;
	}

	// load the instruction card location; lower-case it for case-insensitive comparisons
	instCardLoc = cfg->Get(ConfigVars::InstCardLoc, _T(""));
	std::transform(instCardLoc.begin(), instCardLoc.end(), instCardLoc.begin(), ::_totlower);

	// are SWF files enabled for instruction cards?
	instCardEnableFlash = cfg->GetBool(ConfigVars::InstCardEnableFlash, true);

	// we're not currently using any mouse commands with an Alt key
	altHasMouseCommand = false;

	// Clear the keyboard and joystick command tables so we can 
	// rebuild them
	vkeyToCommand.clear();
	jsCommands.clear();

	// clear the key list for each command
	for (auto &c : commandsByName)
		c.second.keys.clear();

	// reset the Alt/F10 menu key flags
	leftAltHasCommand = false;
	rightAltHasCommand = false;
	f10HasCommand = false;

	// get the number of logical joysticks currently in the system
	size_t numLogJs = JoystickManager::GetInstance()->GetLogicalJoystickCount();

	// Enumerate command key assignments:
	//
	// - Populate the key-to-command table
	//
	// - Check to see if any Alt keys are used as command keys
	//   or as mouse button modifiers
	//
	// - Check to see if F10 is assigned as a command key
	//
	// - Populate the key list for the admin host
	//
	std::list<TSTRING> adminHostKeys;
	InputManager::GetInstance()->EnumButtons([this, numLogJs, &adminHostKeys](
		const InputManager::Command &cmd, const InputManager::Button &btn)
	{
		// get the command
		auto itCmd = commandsByName.find(cmd.configID);
		const KeyCommand &keyCmd = itCmd == commandsByName.end() ? NoCommand : itCmd->second;

		// if we found the command, add the button to the command's button list
		if (itCmd != commandsByName.end())
			itCmd->second.keys.emplace_back(btn);

		// assign a mapping based on the button type
		switch (btn.devType)
		{
		case InputManager::Button::TypeKB:
			// Keyboard key.  Assign the command handler in the dispatch table.
			AddVkeyCommand(btn.code, keyCmd);

			// add it to the admin host list
			adminHostKeys.emplace_back(MsgFmt(_T("%s kb %d"), cmd.configID, btn.code));

			// Note the special menu keys: Left Alt, Right Alt, F10.
			// These require special handling in the window message
			// handler if used for custom key or mouse assignments,
			// so that they don't trigger the standard Windows menu
			// navigation features.
			switch (btn.code)
			{
			case VK_LMENU:
				leftAltHasCommand = true;
				break;

			case VK_RMENU:
				rightAltHasCommand = true;
				break;

			case VK_F10:
				f10HasCommand = true;
				break;
			}
			break;

		case InputManager::Button::TypeJS:
			// Joystick button.  Check if the command is tied to a
			// particular joystick or is for any joystick (unit -1).
			if (&keyCmd != &NoCommand)
			{
				if (btn.unit != -1)
				{
					// The command is for a specific unit.  Add an entry
					// to the joystick lookup table.
					AddJsCommand(btn.unit, btn.code, keyCmd);

					// add it to the admin host list
					auto js = JoystickManager::GetInstance()->GetLogicalJoystick(btn.unit);
					adminHostKeys.emplace_back(
						MsgFmt(_T("%s js %d %x %x %s"), cmd.configID, btn.code, js->vendorID, js->productID, js->prodName.c_str()));
				}
				else
				{
					// The command is for any joystick.  Add an entry to
					// each logical joystick currently in the system.
					for (size_t unit = 0; unit < numLogJs; ++unit)
						AddJsCommand((int)unit, btn.code, keyCmd);

					// add it to the admin host list
					adminHostKeys.emplace_back(MsgFmt(_T("%s js %d"), cmd.configID, btn.code));
				}
			}
			break;
		}
	});

	// update the context menu key mappings
	UpdateMenuKeys(GetSubMenu(hContextMenu, 0));

	// update the system menu key mappings in the parent 
	HWND parent = GetParent(hWnd);
	HMENU parentWindowMenu = parent != 0 ? GetSystemMenu(parent, FALSE) : 0;
	if (parentWindowMenu != 0)
		UpdateMenuKeys(parentWindowMenu);

	// update the Admin Host with the new EXIT GAME key mappings
	Application::Get()->SendKeysToAdminHost(adminHostKeys);

	// load the info box options
	infoBoxOpts.show = cfg->GetBool(ConfigVars::InfoBoxShow, true);
	infoBoxOpts.title = cfg->GetBool(ConfigVars::InfoBoxTitle, true);
	infoBoxOpts.gameLogo = cfg->GetBool(ConfigVars::InfoBoxGameLogo, false);
	infoBoxOpts.manuf = cfg->GetBool(ConfigVars::InfoBoxManufacturer, true);
	infoBoxOpts.manufLogo = cfg->GetBool(ConfigVars::InfoBoxManufacturerLogo, true);
	infoBoxOpts.year = cfg->GetBool(ConfigVars::InfoBoxYear, true);
	infoBoxOpts.system = cfg->GetBool(ConfigVars::InfoBoxSystem, true);
	infoBoxOpts.systemLogo = cfg->GetBool(ConfigVars::InfoBoxSystemLogo, true);
	infoBoxOpts.tableType = cfg->GetBool(ConfigVars::InfoBoxTableType, false);
	infoBoxOpts.tableTypeAbbr = cfg->GetBool(ConfigVars::InfoBoxTableTypeAbbr, false);
	infoBoxOpts.rating = cfg->GetBool(ConfigVars::InfoBoxRating, true);
	infoBoxOpts.tableFile = cfg->GetBool(ConfigVars::InfoBoxTableFile, false);

	// update real DMD gamma
	if (realDMD != nullptr)
		realDMD->UpdateGamma();

	// notify Javascript
	FireConfigEvent(jsSettingsReloadEvent);
}

void PlayfieldView::FontPref::Parse(const TCHAR *text, bool useDefaults)
{
	// try matching the standard format: <size> <weight> <name>
	static std::basic_regex<TCHAR> pat(_T("\\s*(\\d+(pt)?|\\*)\\s+(\\S+)\\s+(.*)"), std::regex_constants::icase);
	std::match_results<const TCHAR*> m;
	if (std::regex_match(text, m, pat))
	{
		// read the size
		ptSize = defaultPtSize;
		auto ptSizeStr = m[1].str();
		if (int n = _ttoi(ptSizeStr.c_str()); n > 0)
		{
			// non-zero point size specified - use that
			ptSize = n;
		}

		// read the weight
		weight = defaultWeight;
		auto weightStr = m[3].str();
		if (int n = _ttoi(weightStr.c_str()); n >= 100 && n <= 900)
		{
			// numeric weight specified
			weight = n;
		}
		else if (weightStr.length() != 0 && weightStr != _T("*"))
		{
			// try a standard weight keyword
			static const struct
			{
				const TCHAR *name;
				int weight;
			}
			names[] = {
				{ _T("thin"), 100 },
				{ _T("hairline"), 100 },
				{ _T("xlight"), 200 },
				{ _T("extralight"), 200 },
				{ _T("extra-light"), 200 },
				{ _T("ultralight"), 200 },
				{ _T("ultra-light"), 200 },
				{ _T("light"), 300 },
				{ _T("normal"), 400 },
				{ _T("medium"), 500 },
				{ _T("semibold"), 600 },
				{ _T("semi-bold"), 600 },
				{ _T("bold"), 700 },
				{ _T("extrabold"), 800 },
				{ _T("extra-bold"), 800 },
				{ _T("xbold"), 800 },
				{ _T("black"), 900 },
				{ _T("heavy"), 900 }
			};
			for (size_t i = 0; i < countof(names); ++i)
			{
				if (_tcsicmp(weightStr.c_str(), names[i].name) == 0)
				{
					weight = names[i].weight;
					break;
				}
			}
		}

		// get the family
		auto familyStr = m[4].str();
		if (familyStr.length() != 0 && familyStr != _T("*"))
		{
			// use the explicit family name
			family = familyStr;
		}
		else
		{
			// use our use-specific default family if specified, otherwise use the
			// global default family from the preferences
			if (defaultFamily != nullptr)
				family = defaultFamily;
			else
				family = pfv->defaultFontFamily;
		}

		// clear any cached font object
		font.reset();
	}
	else if (useDefaults)
	{
		// it's not in the standard format, and the caller directed us to
		// apply defaults in this case, so apply the defaults
		ptSize = defaultPtSize;
		weight = defaultWeight;
		if (defaultFamily != nullptr)
			family = defaultFamily;
		else
			family = pfv->defaultFontFamily;

		// clear any cached font object
		font.reset();
	}
}

void PlayfieldView::FontPref::ParseConfig(const TCHAR *varname)
{
	// Parse the config variable value.  If it's not defined, just parse an
	// empty string, which will set the defaults for the font.
	Parse(ConfigManager::GetInstance()->Get(varname, _T("")));
}

Gdiplus::Font* PlayfieldView::FontPref::Get()
{
	if (font == nullptr)
		font.reset(CreateGPFont(family.c_str(), ptSize, weight));

	return font.get();
}

void PlayfieldView::AddJsCommand(int unit, int button, const KeyCommand &cmd)
{
	// look up the unit:button key in the command table
	int key = JsCommandKey(unit, button);
	auto it = jsCommands.find(key);

	// if it's not in the table already, emplace it
	if (it == jsCommands.end())
		it = jsCommands.emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple()).first;

	// now add the function to the list of associated functions
	it->second.emplace_back(&cmd);
}

void PlayfieldView::AddVkeyCommand(int vkey, const KeyCommand &cmd)
{
	// look up the vkey in the command table
	auto it = vkeyToCommand.find(vkey);

	// if it's not in the table already, emplace it
	if (it == vkeyToCommand.end())
		it = vkeyToCommand.emplace(std::piecewise_construct, std::forward_as_tuple(vkey), std::forward_as_tuple()).first;

	// now add the function to the list of associated functions
	it->second.emplace_back(&cmd);
}

void PlayfieldView::OnJoystickAdded(JoystickManager::PhysicalJoystick *js, bool logicalIsNew)
{
	// If this represents a new logical joystick, we need to add
	// command mappings for any buttons mapped to "any joystick"
	// (unit -1).
	if (logicalIsNew)
	{
		InputManager::GetInstance()->EnumButtons([this, js](const InputManager::Command &cmd, const InputManager::Button &btn)
		{
			// get the command
			auto itCmd = commandsByName.find(cmd.configID);
			const KeyCommand &keyCmd = itCmd == commandsByName.end() ? NoCommand : itCmd->second;

			// if this is a button specifically on our joystick OR assigned
			// for "any joystick", create a command mapping
			if (&keyCmd != &NoCommand
				&& btn.devType == InputManager::Button::TypeJS
				&& (btn.unit == js->logjs->index || btn.unit == -1))
			{
				AddJsCommand(btn.unit, btn.code, keyCmd);
			}
		});
	}
}

template<typename T, T (*conv)(const TCHAR*)>
JsValueRef PlayfieldView::JsSettingsGet(WSTRING varname, JsValueRef defval)
{
	auto js = JavascriptEngine::Get();
	auto cfg = ConfigManager::GetInstance();

	try
	{
		// retrieve the raw string version of the variable
		auto val = cfg->Get(varname.c_str(), nullptr);

		// if it's undefined, return the caller's default value
		if (val == nullptr)
			return defval;

		// convert to the desired native type, then to the corresponding javascript type
		return js->NativeToJs<T>(conv(val));
	}
	catch (JavascriptEngine::CallException exc)
	{
		return js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

void PlayfieldView::JsSettingsSet(WSTRING varname, JsValueRef val)
{
	auto js = JavascriptEngine::Get();
	try
	{
		// get the type of the value to set
		JsValueType type;
		JsErrorCode err;
		if ((err = JsGetValueType(val, &type)) != JsNoError)
			return js->Throw(err), static_cast<void>(0);

		// interpret the value according to the config type
		auto cfg = ConfigManager::GetInstance();
		switch (type)
		{
		case JsNull:
			// null = delete the variable
			cfg->Delete(varname.c_str());
			break;

		case JsBoolean:
			// boolean
			{
				bool b;
				if ((err = JsBooleanToBool(val, &b)) != JsNoError)
					return js->Throw(err), static_cast<void>(0);

				cfg->Set(varname.c_str(), b);
			}
			break;

		case JsNumber:
			// number - this could be an int or float, depending on the value
			{
				double d = js->JsToNative<double>(val);
				double intpart, fracpart = modf(d, &intpart);
				if (fracpart == 0.0 && intpart >= static_cast<double>(INT_MIN) && intpart <= static_cast<double>(INT_MAX))
					cfg->Set(varname.c_str(), static_cast<int>(intpart));
				else
					cfg->SetFloat(varname.c_str(), static_cast<float>(d));
			}
			break;

		case JsString:
			// string
			{
				// get the javascript string
				const wchar_t *p;
				size_t len;
				if ((err = JsStringToPointer(val, &p, &len)) != JsNoError)
					return js->Throw(err), static_cast<void>(0);

				// store it as a WSTRING to make sure it's null-terminated
				WSTRING str(p, len);

				// set the value
				cfg->Set(varname.c_str(), str.c_str());
			}
			break;

		case JsObject:
			// object
			{
				// try as a RECT
				auto obj = JavascriptEngine::JsObj(val);
				if (obj.Has(L"left") && obj.Has(L"right") && obj.Has(L"top") && obj.Has(L"bottom"))
				{
					RECT rc;
					rc.left = obj.Get<int>("left");
					rc.top = obj.Get<int>("top");
					rc.right = obj.Get<int>("right");
					rc.bottom = obj.Get<int>("bottom");
					cfg->Set(varname.c_str(), rc);
				}
				else
				{
					// other types are invalid
					js->Throw(_T("Invalid object type for settings variable value"));
				}
			}
			break;

		default:
			// other types are invalid
			js->Throw(_T("Invalid type for settings variable value"));
			break;
		}
	}
	catch (JavascriptEngine::CallException exc)
	{
		js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}
}

bool PlayfieldView::JsSettingsIsDirty()
{
	return ConfigManager::GetInstance()->IsDirty();
}

bool PlayfieldView::JsSettingsSave()
{
	return ConfigManager::GetInstance()->Save(true);
}

void PlayfieldView::JsSettingsReload()
{
	Application::Get()->ReloadConfig();
}

void PlayfieldView::CmdSelect(const QueuedKey &key)
{
	if (key.mode == KeyDown)
		DoSelect(key.cmd->func == &PlayfieldView::CmdExit);
}

void PlayfieldView::DoSelect(bool usingExitKey)
{
	// check what's showing
	if (CancelStartupVideo())
	{
		// a startup video was playing - don't do anything else until it stops
	}
	else if (curMenu != nullptr)
	{
		// activate the current selection on the menu
		if (curMenu->selected != curMenu->items.end())
		{
			// play the Select sound and show the DOF menu effect
			PlayButtonSound(_T("Select"));
			QueueDOFPulse(L"PBYMenuSelect");

			// dismiss the menu, unless the item is marked "Stay Open"
			if (!curMenu->selected->stayOpen)
				StartMenuAnimation(false);

			// run the menu command
			SendMessage(WM_COMMAND, curMenu->selected->cmd);
		}
	}
	else if (popupSprite != nullptr)
	{
		// assume we'll use the "deselect menu" sound
		const TCHAR *sound = _T("Deselect");

		// assume we'll close the popup
		bool close = true;

		// check the popup type
		if (popupType == PopupRateGame)
		{
			// Rate Game dialog - commit the new rating to the game database
			GameList *gl = GameList::Get();
			GameListItem *game = gl->GetNthGame(0);
			if (IsGameValid(game))
				gl->SetRating(game, workingRating);

			// use the "select" sound effect to indicate success
			sound = _T("Select");

			// make sure we redraw the info box to reflect the new rating
			infoBox.game = nullptr;

			// If a rating filter is in effect, this could filter out
			// the current game, so refresh the selection.
			if (dynamic_cast<const RatingFilter*>(gl->GetCurFilter()) != nullptr)
				SetTimer(hWnd, fullRefreshTimerID, 0, NULL);
		}
		else if (popupType == PopupGameAudioVolume)
		{
			// Audio volume dialog - commit the new volume
			GameList *gl = GameList::Get();
			GameListItem *game = gl->GetNthGame(0);
			if (IsGameValid(game))
				gl->SetAudioVolume(game, workingAudioVolume);

			// use the "select" sound effect to indicate success
			sound = _T("Select");
		}
		else if (popupType == PopupCaptureDelay)
		{
			// Capture Delay dialog - commit the new adjusted startup 
			// delay and return to the capture menu
			captureStartupDelay = adjustedCaptureStartupDelay;
			DisplayCaptureMenu(true, ID_CAPTURE_ADJUSTDELAY);
		}
		else if (popupType == PopupMediaList)
		{
			// Media list dialog - exeucte the current command
			DoMediaListCommand(close);
		}
		else if (popupType == PopupBatchCapturePreview)
		{
			ClosePopup();
			BatchCaptureStep4();
			return;
		}

		// if desired, remove the popup
		if (close)
			ClosePopup();

		// play the selected button sound
		PlayButtonSound(sound);
	}
	else if (runningGamePopup != nullptr)
	{
		// Running a game.  Show the game menu.
		ShowPauseMenu(usingExitKey);

		// play the Select sound
		PlayButtonSound(_T("Select"));
	}
	else
	{
		// Base mode.  Show the main game menu.  Do this through the
		// command handler to give Javascript a chance to intervene.
		OnCommand(ID_SHOW_MAIN_MENU, 0, NULL);
	}
}

void PlayfieldView::ShowMainMenu()
{
	std::list<MenuItemDesc> md;
	GameList *gl = GameList::Get();
	GameListItem *curGame = gl->GetNthGame(0);
	if (IsGameValid(curGame))
	{
		// add "Play Game"
		md.emplace_back(LoadStringT(IDS_MENU_PLAY), ID_PLAY_GAME);

		// if it's not set up yet, add setup options
		if (!curGame->isConfigured)
			md.emplace_back(LoadStringT(IDS_MENU_GAME_SETUP), ID_GAME_SETUP);

		// add a separator
		md.emplace_back(_T(""), -1);

		// add "Information"
		md.emplace_back(LoadStringT(IDS_MENU_INFO), ID_GAMEINFO);

		// add "High Scores", if high scores are available
		if (curGame->highScores.size() != 0)
			md.emplace_back(LoadStringT(IDS_MENU_HIGH_SCORES), ID_HIGH_SCORES);

		// add "Flyer", if the game has a flyer
		if (curGame->MediaExists(GameListItem::flyerImageType))
			md.emplace_back(LoadStringT(IDS_MENU_FLYER), ID_FLYER);

		// add "Instructions", if the game has an instruction card
		if (InstructionCardExists(curGame))
			md.emplace_back(LoadStringT(IDS_MENU_INSTRUCTIONS), ID_INSTRUCTIONS);

		// add a separator
		md.emplace_back(_T(""), -1);

		// add "Rate Table"
		md.emplace_back(LoadStringT(IDS_MENU_RATE_GAME), ID_RATE_GAME);

		// add "Add to Favorites" or "In Favorites", as appropriate
		if (gl->IsFavorite(curGame))
			md.emplace_back(LoadStringT(IDS_MENU_INFAVORITES), ID_REMOVE_FAVORITE, MenuChecked);
		else
			md.emplace_back(LoadStringT(IDS_MENU_ADDFAVORITE), ID_ADD_FAVORITE);

		// add a separator
		md.emplace_back(_T(""), -1);
	}

	// Start a pagination group for the filters, in case the user adds
	// a large number of custom filters.
	md.emplace_back(PageUpTitle, ID_MENU_PAGE_UP);

	// Create a vector of top-level filters.  Start with the All Games
	// and Favorites filters.
	std::vector<GameListFilter*> topFilters;
	topFilters.emplace_back(gl->GetAllGamesFilter());
	topFilters.emplace_back(gl->GetFavoritesFilter());

	// add any user-defined filters in the [Top] group
	gl->EnumUserDefinedFilters([&topFilters](GameListFilter *filter) {
		if (filter->menuGroup == _T("[Top]"))
			topFilters.emplace_back(filter);
	});

	// sort the top-level filter list
	std::sort(topFilters.begin(), topFilters.end(), [](GameListFilter* const &a, GameListFilter* const &b) {
		return lstrcmpi(a->menuSortKey.c_str(), b->menuSortKey.c_str()) < 0;
	});

	// add the top-level filters
	const GameListFilter *curFilter = gl->GetCurFilter();
	for (auto f : topFilters)
		md.emplace_back(f->GetFilterTitle(), f->cmd, f == curFilter ? MenuRadio : 0);

	// Add the filter classes that have submenus with the specific filters
	md.emplace_back(LoadStringT(IDS_FILTER_BY_ERA), ID_FILTER_BY_ERA, MenuHasSubmenu);
	md.emplace_back(LoadStringT(IDS_FILTER_BY_MANUF), ID_FILTER_BY_MANUF, MenuHasSubmenu);
	md.emplace_back(LoadStringT(IDS_FILTER_BY_SYS), ID_FILTER_BY_SYS, MenuHasSubmenu);
	md.emplace_back(LoadStringT(IDS_FILTER_BY_CATEGORY), ID_FILTER_BY_CATEGORY, MenuHasSubmenu);
	md.emplace_back(LoadStringT(IDS_FILTER_BY_RATING), ID_FILTER_BY_RATING, MenuHasSubmenu);
	md.emplace_back(LoadStringT(IDS_FILTER_BY_RECENCY), ID_FILTER_BY_RECENCY, MenuHasSubmenu);
	md.emplace_back(LoadStringT(IDS_FILTER_BY_ADDED), ID_FILTER_BY_ADDED, MenuHasSubmenu);

	// Get an alphabetically sorted list of user-defined filter groups
	struct udfg
	{
		udfg(const TSTRING &name, int command) : name(name), command(command) { }
		TSTRING name;
		int command;
	};
	std::vector<udfg> udfgs;
	gl->EnumUserDefinedFilterGroups([&udfgs](const TSTRING &name, int command) { udfgs.emplace_back(name, command); });
	std::sort(udfgs.begin(), udfgs.end(), [](const udfg &a, const udfg &b) { return lstrcmpi(a.name.c_str(), b.name.c_str()) < 0; });

	// add the user-defined filter groups
	for (auto &u : udfgs)
		md.emplace_back(u.name.c_str(), u.command, MenuHasSubmenu);

	// that's it for the paged filter area
	md.emplace_back(PageDownTitle, ID_MENU_PAGE_DOWN);

	// add the "Return" item to exit the menu
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_MENU_MAINRETURN), ID_MENU_RETURN);

	// display the menu
	ShowMenu(md, L"main", 0);

	// play the Select sound and show the Menu Open DOF effect
	PlayButtonSound(_T("Select"));
	QueueDOFPulse(L"PBYMenuOpen");
}

// Show the "pause" menu.  This is the menu displayed when a game
// is running.  It's the "pause" menu because the game must be paused
// if we're back here while it's running.
void PlayfieldView::ShowPauseMenu(bool usingExitKey)
{
	// set up a menu descriptor
	std::list<MenuItemDesc> md;

	// resume/terminate
	md.emplace_back(LoadStringT(IDS_MENU_RESUMEGAME), ID_RESUME_GAME, usingExitKey ? 0 : MenuSelected);
	md.emplace_back(LoadStringT(IDS_MENU_KILLGAME), ID_KILL_GAME);
	md.emplace_back(_T(""), -1);

	// game information, high scores, flyer, instructions
	if (GameListItem *curGame = GameList::Get()->GetNthGame(0);  IsGameValid(curGame))
	{
		md.emplace_back(LoadStringT(IDS_MENU_INFO), ID_GAMEINFO);
		if (curGame->highScores.size() != 0)
			md.emplace_back(LoadStringT(IDS_MENU_HIGH_SCORES), ID_HIGH_SCORES);
		if (curGame->MediaExists(GameListItem::flyerImageType))
			md.emplace_back(LoadStringT(IDS_MENU_FLYER), ID_FLYER);
		if (InstructionCardExists(curGame))
			md.emplace_back(LoadStringT(IDS_MENU_INSTRUCTIONS), ID_INSTRUCTIONS);
		md.emplace_back(_T(""), -1);
	}

	// exit/shutdown/cancel
	md.emplace_back(LoadStringT(IDS_MENU_EXIT), ID_EXIT);
	md.emplace_back(LoadStringT(IDS_MENU_SHUTDOWN), ID_SHUTDOWN);
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_MENU_GAMERETURN), ID_MENU_RETURN, usingExitKey ? MenuSelected : 0);

	// show the menu
	ShowMenu(md, L"pause game", usingExitKey ? SHOWMENU_IS_EXIT_MENU : 0);
}

void PlayfieldView::ShowFilterSubMenu(int cmd, const TCHAR *group, const WCHAR *menuID)
{
	// set up to add filters to the menu
	std::list<MenuItemDesc> md;
	GameList *gl = GameList::Get();
	const GameListFilter *curFilter = gl->GetCurFilter();
	auto AddFilter = [gl, &md, curFilter](const GameListFilter *f) {
		md.emplace_back(f->GetMenuTitle(), f->cmd, f == curFilter ? MenuRadio : 0);
	};

	// add a Page Up item at the start of the filter list, in case pagination is needed
	md.emplace_back(PageUpTitle, ID_MENU_PAGE_UP);

	// traverse the master filter list, adding each filter that matches the
	// filter group
	for (auto f : gl->GetFilters())
	{
		if (f->menuGroup == group)
			AddFilter(f);
	}

	// add a Page Down item to end the pagination area
	md.emplace_back(PageDownTitle, ID_MENU_PAGE_DOWN);

	// add a Cancel item at the end
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_MENU_FILTER_RETURN), ID_MENU_RETURN);

	// show the menu
	ShowMenu(md, menuID, 0);

	// show the Menu Open DOF effect
	QueueDOFPulse(L"PBYMenuOpen");
}

void PlayfieldView::ShowRecencyFilterMenu(
	const TCHAR *incGroup, const TCHAR *excGroup, const TCHAR *neverGroup, 
	const WCHAR *menuID, int idStrWithin, int idStrNotWithin)
{
	// set up to add filters to the menu
	std::list<MenuItemDesc> md;
	GameList *gl = GameList::Get();
	const GameListFilter *curFilter = gl->GetCurFilter();
	auto AddFilters = [gl, &md, curFilter](const TCHAR *group)
	{
		for (auto f : gl->GetFilters())
		{
			if (f->menuGroup == group)
				md.emplace_back(f->GetMenuTitle(), f->cmd, f == curFilter ? MenuRadio : 0);
		}
	};

	// Add the group header for the "Played/added within:" section, then add
	// the inclusion filters
	md.emplace_back(LoadStringT(idStrWithin), -1);
	AddFilters(incGroup);

	// Add the group header for the "Not played within/added before:" section, then
	// add the exclusion filters
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(idStrNotWithin), -1);
	AddFilters(excGroup);

	// If there's a "Never" item, add it after a separator
	if (neverGroup != nullptr)
	{
		md.emplace_back(_T(""), -1);
		AddFilters(neverGroup);
	}

	// add a Cancel item at the end
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_MENU_FILTER_RETURN), ID_MENU_RETURN);

	// show the menu
	ShowMenu(md, menuID, 0);

	// show the Menu Open DOF effect
	QueueDOFPulse(L"PBYMenuOpen");
}

void PlayfieldView::CmdExit(const QueuedKey &key)
{
	if (key.mode == KeyDown)
	{
		// Check what's showing:
		//
		// * Startup video -> cancel it
		// * Menu -> close it, or activate item on an Exit menu
		// * Popup -> close it
		// * Instruction card -> close it
		// * Nothing -> show exit menu
		//
		if (CancelStartupVideo())
		{
			// a startup video was playing - don't do anything else until it stops
		}
		else if (curMenu != nullptr)
		{
			// A menu is showing.  For an Exit menu, the Exit button can
			// selects menu items, if the configuration says so; for others,
			// the Exit button always cancels the menu.
			if ((curMenu->flags & SHOWMENU_IS_EXIT_MENU) != 0 && exitMenuExitKeyIsSelectKey)
			{
				// process it as though it were the Select key
				CmdSelect(key);
			}
			else
			{
				// play the deselect sound and the DOF close-menu effect
				PlayButtonSound(_T("Deselect"));
				QueueDOFPulse(L"PBYMenuQuit");

				// close the menu
				StartMenuAnimation(false);
			}
		}
		else if (popupSprite != nullptr)
		{
			// check the popup type
			if (popupType == PopupCaptureDelay)
			{
				// capture delay - return to the capture setup menu, 
				// without committing the adjusted delay time
				ClosePopup();
				DisplayCaptureMenu(true, ID_CAPTURE_ADJUSTDELAY);
			}
			else if (popupType == PopupMediaList)
			{
				PlayButtonSound(_T("Deselect"));
				ShowMediaFilesExit();
			}
			else if (popupType == PopupBatchCapturePreview)
			{
				ClosePopup();
				BatchCaptureStep4();
			}
			else
			{
				// close the popup sprite
				PlayButtonSound(_T("Deselect"));
				ClosePopup();
			}
		}
		else if (runningGamePopup != nullptr)
		{
			// treat this as a Select button, to show the game control menu
			CmdSelect(key);
		}
		else if (ConfigManager::GetInstance()->GetBool(ConfigVars::ExitMenuEnabled, true))
		{
			// nothing's showing - bring up the Exit menu
			OnCommand(ID_SHOW_EXIT_MENU, 0, NULL);
		}
	}
}

void PlayfieldView::ShowExitMenu()
{
	std::list<MenuItemDesc> md;
	md.emplace_back(LoadStringT(IDS_MENU_EXIT), ID_EXIT);
	md.emplace_back(LoadStringT(IDS_MENU_SHUTDOWN), ID_SHUTDOWN);

	// add the Operator Meu command if desired
	if (ConfigManager::GetInstance()->GetBool(ConfigVars::ShowOpMenuInExitMenu, false))
	{
		md.emplace_back(_T(""), -1);
		md.emplace_back(LoadStringT(IDS_MENU_OPERATOR), ID_OPERATOR_MENU);
	}

	// add the About block
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_MENU_HELP), ID_HELP);
	md.emplace_back(LoadStringT(IDS_MENU_ABOUT), ID_ABOUT);

	// add the Cancel block
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_MENU_EXITRETURN), ID_MENU_RETURN, MenuSelected);

	// show the menu
	ShowMenu(md, L"exit", SHOWMENU_IS_EXIT_MENU);

	// trigger the normal Menu Open effects
	PlayButtonSound(_T("Select"));
	QueueDOFPulse(L"PBYMenuOpen");
}

void PlayfieldView::PlayButtonSound(const TCHAR *effectName, float volume)
{
	if (!muteButtons)
	{
		// look up the effect file
		TCHAR path[MAX_PATH];
		if (auto gl = GameList::Get(); gl != nullptr && gl->FindGlobalWaveFile(path, _T("Button Sounds"), effectName))
		{
			// play back the file, combining the caller's volume level and the global
			// button volume setting
			AudioManager::Get()->PlayFile(path, volume*buttonVolume / 100);
		}
	}
}

void PlayfieldView::CloseMenusAndPopups()
{
	// remove menus
	if (curMenu != 0)
		StartMenuAnimation(false);

	// remove popups
	ClosePopup();

	// hide the info box
	HideInfoBox();
}

float PlayfieldView::GetContextSensitiveButtonVolume(const QueuedKey &) const
{
	// if we're displaying the game media audio volume adjustment
	// dialog, play next/prev/pgup/pgdn button sounds at the working
	// volume level
	if (popupSprite != nullptr && popupType == PopupGameAudioVolume)
		return static_cast<float>(workingAudioVolume)/100.0f;

	// for everything else, play buttons at normal volume
	return 1.0f;
}

void PlayfieldView::CmdNext(const QueuedKey &key)
{
	static bool soundRepeated = FALSE;

	if ((key.mode & KeyDown) != 0)
	{

		if ((key.mode & KeyRepeat) != KeyRepeat)
		{
			// for normal keypress, always play the audio effect
			PlayButtonSound(_T("Next"), GetContextSensitiveButtonVolume(key));
			soundRepeated = FALSE;
		}
		else if (!muteRepeatButtons || !soundRepeated)
		{
			// for auto-repeat, play audio effect only one additional time
			PlayButtonSound(_T("Next"), GetContextSensitiveButtonVolume(key));
			soundRepeated = TRUE;
		}

		// do the basic Next processing
		DoCmdNext(key.mode == KeyRepeat);
	}

	// check for a media capture "Manual Go" gesture (next + prev keys down)
	if (captureManualGoButton == CaptureManualGoButton::Flippers)
		CheckManualGo(manualGoRightDown, key);
}

void PlayfieldView::DoCmdNext(bool fast)
{
	// check what's showing
	if (CancelStartupVideo())
	{
		// a startup video was playing - don't do anything else until it stops
	}
	else if (curMenu != nullptr)
	{
		// a menu is active - go to the next item
		QueueDOFPulse(L"PBYMenuDown");
		MenuNext(1);
	}
	else if (popupSprite != nullptr)
	{
		// check the popup type
		if (popupType == PopupFlyer)
		{
			// flyer - go to the next page
			ShowFlyer(flyerPage + 1);
		}
		else if (popupType == PopupInstructions)
		{
			// instructions - go to the next card
			ShowInstructionCard(instCardPage + 1);
		}
		else if (popupType == PopupRateGame)
		{
			// Rate Game popup dialog - adjust the rating up 1/2 star
			AdjustRating(0.5f);
		}
		else if (popupType == PopupGameAudioVolume)
		{
			AdjustWorkingAudioVolume(1);
		}
		else if (popupType == PopupGameInfo && GameList::Get()->GetNthGame(0)->highScores.size() != 0)
		{
			// game info - show high scores
			ShowHighScores();
		}
		else if (popupType == PopupHighScores)
		{
			// high scores - go to game info
			ShowGameInfo();
		}
		else if (popupType == PopupCaptureDelay)
		{
			// increment the startup delay time and update the dialog
			adjustedCaptureStartupDelay += 1;
			ShowCaptureDelayDialog(true);
		}
		else if (popupType == PopupMediaList)
		{
			ShowMediaFiles(1);
		}
		else if (popupType == PopupBatchCapturePreview)
		{
			// scroll down a bit
			batchViewScrollY += 32;
			UpdateBatchCaptureView();
		}
		else
		{
			// for others, just cancel the popup
			ClosePopup();
		}
	}
	else if (runningGamePopup != nullptr)
	{
		// don't change games while a game is running
	}
	else
	{
		// Base mode - go to the next game
		QueueDOFPulse(L"PBYWheelNext");
		SwitchToGame(1, fast, true);
	}
}

void PlayfieldView::CmdPrev(const QueuedKey &key)
{
	static bool soundRepeated = FALSE;

	if ((key.mode & KeyDown) != 0)
	{

		if ((key.mode & KeyRepeat) != KeyRepeat)
		{
			// for normal keypress, always play the audio effect
			PlayButtonSound(_T("Prev"), GetContextSensitiveButtonVolume(key));
			soundRepeated = FALSE;
		}
		else if (!muteRepeatButtons || !soundRepeated)
		{
			// for auto-repeat, play audio effect only one additional time
			PlayButtonSound(_T("Prev"), GetContextSensitiveButtonVolume(key));
			soundRepeated = TRUE;
		}

		// carry out the basic command action
		DoCmdPrev(key.mode == KeyRepeat);
	}

	// check for a media capture "Manual Go" gesture (next + prev keys down)
	if (captureManualGoButton == CaptureManualGoButton::Flippers)
		CheckManualGo(manualGoLeftDown, key);
}

void PlayfieldView::CheckManualGo(bool &thisButtonDown, const QueuedKey &key)
{
	// update this button's status
	thisButtonDown = ((key.mode & (KeyDown | KeyBgDown)) != 0);

	// if we're pressing the button (it's not an auto-repeat), and the
	// other button is down, count it as a "Manual Go" for capture mode
	if (manualGoLeftDown && manualGoRightDown && key.mode == KeyBgDown)
		Application::Get()->ManualCaptureGo();
}

void PlayfieldView::DoCmdPrev(bool fast)
{
	// check what's showing
	if (CancelStartupVideo())
	{
		// a startup video was playing - don't do anything else until it stops
	}
	else if (curMenu != nullptr)
	{
		// a menu is active - move to the prior item
		QueueDOFPulse(L"PBYMenuUp");
		MenuNext(-1);
	}
	else if (popupSprite != nullptr)
	{
		// check the popup type
		if (popupType == PopupFlyer)
		{
			// flyer - go to the prior page
			ShowFlyer(flyerPage - 1);
		}
		else if (popupType == PopupInstructions)
		{
			// instructions - go to the prior card
			ShowInstructionCard(instCardPage - 1);
		}
		else if (popupType == PopupRateGame)
		{
			// Rate Game popup dialog - adjust the rating down 1/2 star
			AdjustRating(-0.5f);
		}
		else if (popupType == PopupGameAudioVolume)
		{
			AdjustWorkingAudioVolume(-1);
		}
		else if (popupType == PopupGameInfo && GameList::Get()->GetNthGame(0)->highScores.size() != 0)
		{
			// game info - show high scores
			ShowHighScores();
		}
		else if (popupType == PopupHighScores)
		{
			// high scores - go to game info
			ShowGameInfo();
		}
		else if (popupType == PopupCaptureDelay)
		{
			// decrement the startup delay time and re-show the dialog
			adjustedCaptureStartupDelay -= 1;
			if (adjustedCaptureStartupDelay < 0) adjustedCaptureStartupDelay = 0;
			ShowCaptureDelayDialog(true);
		}
		else if (popupSprite != nullptr && popupType == PopupMediaList)
		{
			ShowMediaFiles(-1);
		}
		else if (popupType == PopupBatchCapturePreview)
		{
			// scroll up a bit
			batchViewScrollY -= 32;
			UpdateBatchCaptureView();
		}
		else
		{
			// for others, just cancel the popup
			ClosePopup();
		}
	}
	else if (runningGamePopup != nullptr)
	{
		// don't change games while a game is running
	}
	else
	{
		// base mode - go to the previous game
		QueueDOFPulse(L"PBYWheelPrev");
		SwitchToGame(-1, fast, true);
	}
}

void PlayfieldView::CmdNextPage(const QueuedKey &key)
{
	static bool soundRepeated = FALSE;

	if ((key.mode & KeyDown) != 0)
	{

		if ((key.mode & KeyRepeat) != KeyRepeat)
		{
			// for normal keypress, always play the audio effect
			PlayButtonSound(_T("Next"), GetContextSensitiveButtonVolume(key));
			soundRepeated = FALSE;
		}
		else if (!muteRepeatButtons || !soundRepeated)
		{
			// for auto-repeat, play audio effect only one additional time
			PlayButtonSound(_T("Next"), GetContextSensitiveButtonVolume(key));
			soundRepeated = TRUE;
		}

		// Check the current UI state.
		// 
		// If there's a startup video, cancel it.  Otherwise, if a
		// menu is showing, and it has a page-down item, treat the
		// Next Page button as a shortcut for the page-down command.
		// Otherwise, if there's a menu or popup showing, treat the
		// button as equivalent to the Right/Down button. Otherwise,
		// go to the next letter group in the game list. 
		if (CancelStartupVideo())
		{
			// startup video is playing - don't do anything else until it stops
		}
		else if (curMenu != nullptr && curMenu->paged)
		{
			// there's a Page Down item - send the command
			PostMessage(WM_COMMAND, ID_MENU_PAGE_DOWN);
		}
		else if (popupSprite != nullptr && popupType == PopupCaptureDelay)
		{
			// increment the startup delay time and re-show the dialog
			adjustedCaptureStartupDelay += 5;
			ShowCaptureDelayDialog(true);
		}
		else if (popupSprite != nullptr && popupType == PopupMediaList)
		{
			ShowMediaFiles(2);
		}
		else if (popupSprite != nullptr && popupType == PopupBatchCapturePreview)
		{
			batchViewScrollY += 1250;
			UpdateBatchCaptureView();
		}
		else if (popupSprite != nullptr && popupType == PopupGameAudioVolume)
		{
			AdjustWorkingAudioVolume(10);
		}
		else if (curMenu != nullptr || popupSprite != nullptr)
		{
			// menu/popup - treat it as a regular 'next'
			DoCmdNext(key.mode == KeyRepeat);
		}
		else if (runningGamePopup != nullptr)
		{
			// running a game - do nothing
		}
		else
		{
			// base state - advance by a letter group
			QueueDOFPulse(L"PBYWheelNextPage");
			SwitchToGame(GameList::Get()->FindNextLetter(), key.mode == KeyRepeat, true);
		}
	}

	// check for a media capture "Manual Go" gesture
	if (captureManualGoButton == CaptureManualGoButton::MagnaSave)
		CheckManualGo(manualGoRightDown, key);
}

void PlayfieldView::CmdPrevPage(const QueuedKey &key)
{
	static bool soundRepeated = FALSE;

	if ((key.mode & KeyDown) != 0)
	{

		if ((key.mode & KeyRepeat) != KeyRepeat)
		{
			// for normal keypress, always play the audio effect
			PlayButtonSound(_T("Prev"), GetContextSensitiveButtonVolume(key));
			soundRepeated = FALSE;
		}
		else if (!muteRepeatButtons || !soundRepeated)
		{
			// for auto-repeat, play audio effect only one additional time
			PlayButtonSound(_T("Prev"), GetContextSensitiveButtonVolume(key));
			soundRepeated = TRUE;
		}


		// If a startup video is playing, cancel it.
		// If there's an active menu, and it has a Page Up command, treat 
		// this as a shortcut for that command.  Otherwise, if there's a
		// menu or popup showing, treat this as equivalent to Left/Up.
		// Otherwise, back up by a letter group in the game list.
		if (CancelStartupVideo())
		{
			// a startup video was playing - don't do anything else until it stops
		}
		else if (curMenu != nullptr && curMenu->paged)
		{
			PostMessage(WM_COMMAND, ID_MENU_PAGE_UP);
		}
		else if (popupSprite != nullptr && popupType == PopupCaptureDelay)
		{
			// decrement the startup delay time and re-show the dialog
			adjustedCaptureStartupDelay -= 5;
			if (adjustedCaptureStartupDelay < 0) adjustedCaptureStartupDelay = 0;
			ShowCaptureDelayDialog(true);
		}
		else if (popupSprite != nullptr && popupType == PopupMediaList)
		{
			ShowMediaFiles(-2);
		}
		else if (popupSprite != nullptr && popupType == PopupBatchCapturePreview)
		{
			batchViewScrollY -= 1250;
			UpdateBatchCaptureView();
		}
		else if (popupSprite != nullptr && popupType == PopupGameAudioVolume)
		{
			AdjustWorkingAudioVolume(-10);
		}
		else if (curMenu != nullptr || popupSprite != nullptr)
		{
			// menu/popup - treat as equivalent to 'previous'
			DoCmdPrev(key.mode == KeyRepeat);
		}
		else if (runningGamePopup != nullptr)
		{
			// running a game - do nothing
		}
		else
		{
			// base state - go backwards by a letter group
			SwitchToGame(GameList::Get()->FindPrevLetter(), key.mode == KeyRepeat, true);
			QueueDOFPulse(L"PBYWheelPrevPage");
		}
	}

	// check for a media capture "Manual Go" gesture
	if (captureManualGoButton == CaptureManualGoButton::MagnaSave)
		CheckManualGo(manualGoLeftDown, key);
}

void PlayfieldView::CmdLaunch(const QueuedKey &key)
{
	// launch the current game (bypassing the menu)
	if ((key.mode & KeyDown) != 0)
	{
		if (CancelStartupVideo())
		{
			// a startup video was playing - don't do anything else until it stops
		}
		else if (curMenu != nullptr || popupSprite != nullptr || runningGamePopup != nullptr)
		{
			// Menu, popup, or running game popup is showing.  Treat Launch
			// as Select.
			CmdSelect(key);
		}
		else
		{
			// launch it
			PlayButtonSound(_T("Select"));
			SendMessage(WM_COMMAND, ID_PLAY_GAME);
		}
	}

	// check for a media capture "Manual Go" gesture
	if (captureManualGoButton == CaptureManualGoButton::Launch)
		CheckManualGo(manualGoLeftDown, key);
}

void PlayfieldView::CmdExitGame(const QueuedKey &key)
{
	// "Exit Game" Key.  This only applies when we're running in
	// the background.
	if ((key.mode & (KeyBgDown | KeyBgRepeat)) == KeyBgDown)
		SendMessage(WM_COMMAND, ID_KILL_GAME);
}

void PlayfieldView::CmdPauseGame(const QueuedKey &key)
{
	// "Pause Game" key.  This only applies when we're running in
	// the background.
	if ((key.mode & (KeyBgDown | KeyBgRepeat)) == KeyBgDown)
		SendMessage(WM_COMMAND, ID_PAUSE_GAME);
}

// insert coin - slot 1
void PlayfieldView::CmdCoin1(const QueuedKey &key)
{
	DoCoinCommon(key, 1);
}

// insert coin - slot 2
void PlayfieldView::CmdCoin2(const QueuedKey &key)
{
	DoCoinCommon(key, 2);
}

// insert coin - slot 3
void PlayfieldView::CmdCoin3(const QueuedKey &key)
{
	DoCoinCommon(key, 3);
}

// insert coin - slot 4
void PlayfieldView::CmdCoin4(const QueuedKey &key)
{
	DoCoinCommon(key, 4);
}

void PlayfieldView::DoCoinCommon(const QueuedKey &key, int slotNum)
{
	if (key.mode == KeyDown && slotNum >= 1 && slotNum <= countof(coinVal))
	{
		// remember the original effective credits
		float oldWholeCredits = floorf(GetEffectiveCredits());

		// add the monetary value for this coin slot to the coin balance
		coinBalance += coinVal[slotNum - 1];

		// If we've reached the highest price break point in the pricing
		// model, convert the balance to credits.  There's no point in
		// banking a coin balance past the highest price break, as any
		// balance past that level would be unable to buy any credits.
		float newCredits = bankedCredits;
		if (pricePoints.size() != 0)
		{
			PricePoint &pp = pricePoints.back();
			while (coinBalance >= pp.price)
			{
				// credit the credits
				newCredits += pp.credits;

				// deduct the coin value from the balance
				coinBalance -= pp.price;
			}
		}

		// set the new calculated credits value
		SetCredits(newCredits);

		// if this added a whole effective credit, play the "add credit"
		// sound, otherwise play "coin in"
		float newWholeCredits = floorf(GetEffectiveCredits());
		PlayButtonSound(newWholeCredits != oldWholeCredits ? _T("AddCredit") : _T("CoinIn"));

		// update the status line text
		UpdateAllStatusText();

		// display the new credit balance
		DisplayCredits();
	}
}

void PlayfieldView::DisplayCredits()
{
	// create the new sprite
	creditsSprite.Attach(new Sprite());
	Application::InUiErrorHandler eh;
	int width = 800, height = 400;
	bool ok = creditsSprite->Load(width, height, [width, height, this](HDC hdc, HBITMAP bmp)
	{
		// set up GDI+ on the memory DC
		Gdiplus::Graphics g(hdc);

		// fill the background with transparent color
		Gdiplus::SolidBrush bkg(Gdiplus::Color(0, 0, 0, 0));
		g.FillRectangle(&bkg, 0, 0, width, height);

		// get the text to draw
		float n = GetEffectiveCredits();
		int msg = (n == 1.0f ? IDS_1_CREDIT : n > 0.0f && n < 1.0f ? IDS_FRAC_CREDIT : IDS_N_CREDITS);
		MsgFmt line1(msg, FormatFraction(n).c_str());
		MsgFmt line2(IDS_FREE_PLAY);

		// measure the text
		FontPref &font = creditsFont;
		Gdiplus::PointF pt(0.0f, 0.0f);
		Gdiplus::RectF bbox1, bbox2;
		g.MeasureString(line1, -1, font, pt, &bbox1);
		g.MeasureString(line2, -1, font, pt, &bbox2);
		float txtht = bbox1.Height + bbox2.Height;
		float y = ((float)height - txtht) / 2.0f;

		// draw the text centered
		Gdiplus::SolidBrush br(Gdiplus::Color(0xff, 0xff, 0xff, 0xff));
		g.DrawString(line1, -1, font, Gdiplus::PointF(((float)width - bbox1.Width)/2.0f, y - bbox1.Height), &br);
		g.DrawString(line2, -1, font, Gdiplus::PointF(((float)width - bbox2.Width)/2.0f, y), &br);

		// flush our drawing to the pixel buffer
		g.Flush();

	}, eh, _T("Credits overlay"));

	// set/reset the credits animation timer
	SetTimer(hWnd, creditsDispTimerID, 16, 0);
	creditsStartTime = GetTickCount();

	// open the popup, positioning it in the lower half of the screen
	creditsSprite->alpha = 1.0f;
	creditsSprite->offset.y = -0.2f;
	creditsSprite->UpdateWorld();
	UpdateDrawingList();
}

void PlayfieldView::OnCreditsDispTimer()
{
	// fade out after the display has been up a while
	const DWORD dispTime = 2000;
	DWORD dt = GetTickCount() - creditsStartTime;
	if (dt > dispTime)
	{
		// figure the fade ramp; if we're at zero alpha, remove the
		// sprite entirely
		const DWORD fadeTime = 300;
		if ((creditsSprite->alpha = 1.0f - fmin(1.0f, (float)(dt - dispTime) / (float)fadeTime)) == 0.0f)
		{
			creditsSprite = nullptr;
			UpdateDrawingList();
			KillTimer(hWnd, creditsDispTimerID);
		}
	}
}

void PlayfieldView::ResetCoins()
{
	// bank the current effective credit balance
	SetCredits(GetEffectiveCredits());

	// clear the coin balance
	coinBalance = 0.0f;
}

void PlayfieldView::SetCredits(float c)
{
	// limit the banked credits to the maximum credit setting
	if (maxCredits != 0.0f && c > maxCredits)
		c = maxCredits;

	// if this changes the current credit balance, update it
	if (bankedCredits != c)
	{
		// store the new value
		bankedCredits = c;

		// update the stored credits in the config file
		ConfigManager::GetInstance()->SetFloat(ConfigVars::CreditBalance, c);

		// update the status line text
		UpdateAllStatusText();
	}
}

float PlayfieldView::GetEffectiveCredits()
{
	// Figure the effective credits based on the new coin balance.
	// To do this, find the highest credit value that we can buy for
	// the current coin balance.
	float maxCoinCredits = 0.0f;
	for (auto const &p : pricePoints)
	{
		if (coinBalance >= p.price && p.credits > maxCoinCredits)
			maxCoinCredits = p.credits;
	}

	// now add the coin credits to the banked credit balance to
	// get the effective credits
	float eff = bankedCredits + maxCoinCredits;

	// limit the effective credits to the maximum credit setting
	if (maxCredits != 0.0f && eff > maxCredits)
		eff = maxCredits;

	// return the result
	return eff;
}

// coin door open/close command handler
void PlayfieldView::CmdCoinDoor(const QueuedKey &)
{
	// we don't have anything useful to do with this key; ignore it
}

// Service 1/Escape command handler
void PlayfieldView::CmdService1(const QueuedKey &key)
{
	if (key.mode == KeyDown)
	{
		// If a menu is open, treat this as cancel/exit.  
		//
		// If no menu is open, treat it as "add a credit".  On most of the
		// 1990s Williams machines, the Escape button doubled as a "Service
		// Credit" button when not in a menu or test mode, to let the
		// operator add credits without inserting coins (to compensate a
		// player after a malfunction, for example, or to do a test run
		// after a repair).
		if (curMenu != nullptr)
		{
			// menu is active - treat this as an Exit key
			CmdExit(key);
		}
		else
		{
			// service credit - add a credit to the banked balance
			SetCredits(bankedCredits + 1.0f);
			DisplayCredits();
			PlayButtonSound(_T("AddCredit"));
		}
	}
}

// Service 2/-/Down command handler
void PlayfieldView::CmdService2(const QueuedKey &key)
{
	// if a menu or popup is open, treat this as 'previous' 
	if ((curMenu != nullptr || popupSprite != nullptr) && (key.mode & KeyDown) != 0)
	{
		// play the sound
		PlayButtonSound(_T("Prev"));

		// carry out the basic command action
		DoCmdPrev(key.mode == KeyRepeat);
	}
}

// Service 3/+/Up command handler
void PlayfieldView::CmdService3(const QueuedKey &key)
{
	// if a menu or popup is open, treat this as 'next' 
	if ((curMenu != nullptr || popupSprite != nullptr) && (key.mode & KeyDown) != 0)
	{
		// play the sound
		PlayButtonSound(_T("Next"));

		// carry out the basic command action
		DoCmdNext(key.mode == KeyRepeat);
	}
}

// Service 4/Enter command handler
void PlayfieldView::CmdService4(const QueuedKey &key)
{
	// If a menu or popup is open, treat this as 'select'.  Otherwise,
	// show our "service menu", with program and game setup commands.
	if (key.mode == KeyDown)
	{
		if (curMenu != nullptr || popupSprite != nullptr || runningGamePopup != nullptr)
		{
			// menu or popup is showing - treat this as a normal Select
			CmdSelect(key);
		}
		else
		{
			// no menu or popup is showing - show the Operator menu
			PlayButtonSound(_T("Select"));
			ShowOperatorMenu();
		}
	}
}

void PlayfieldView::ShowOperatorMenu()
{
	// get the current game
	auto gl = GameList::Get();
	auto game = gl->GetNthGame(0);

	// build the service menu
	std::list<MenuItemDesc> md;

	// add the game-specific setup options only if a game is active
	if (IsGameValid(game))
		md.emplace_back(LoadStringT(IDS_MENU_GAME_SETUP), ID_GAME_SETUP);

	// add the global game setup options
	md.emplace_back(LoadStringT(IDS_MENU_BATCH_CAPTURE), ID_BATCH_CAPTURE_STEP1);

	// end the game setup section
	md.emplace_back(_T(""), -1);

	// paginate the filter section, in case the user adds a bunch of custom filters
	md.emplace_back(PageUpTitle, ID_MENU_PAGE_UP);

	// build a vector of filters, starting with the special system filters
	std::vector<GameListFilter*> topFilters;
	topFilters.emplace_back(gl->GetHiddenGamesFilter());
	topFilters.emplace_back(gl->GetUnconfiguredGamesFilter());

	// add any user-defined [Op] filters
	gl->EnumUserDefinedFilters([&topFilters](GameListFilter *filter) {
		if (filter->menuGroup == _T("[Op]"))
			topFilters.emplace_back(filter);
	});

	// sort the filters
	std::sort(topFilters.begin(), topFilters.end(), [](GameListFilter* const& a, GameListFilter* const& b) {
		return lstrcmpi(a->menuSortKey.c_str(), b->menuSortKey.c_str()) < 0;
	});

	// Add the filters.  Because these filters are shown separately from
	// the main menu filter list, treat them as "checkmark" items instead
	// of "radio button" items.  If one of them is selected, show it as
	// checked, and on un-checking it, return to the All Games filter.
	for (auto filter : topFilters)
	{
		// check if this special filter is currently active
		if (gl->GetCurFilter() == filter)
		{
			// it's active - show it as checked; on selecting this menu item,
			// "un-check" it by switching back to the default All Games filter
			md.emplace_back(filter->GetMenuTitle(), gl->GetAllGamesFilter()->cmd, MenuChecked);
		}
		else
		{
			// it's not active - show it unchecked, and on selecting it,
			// activate this filter
			md.emplace_back(filter->GetMenuTitle(), filter->cmd);
		}
	};

	// end the special filters section
	md.emplace_back(PageDownTitle, ID_MENU_PAGE_DOWN);
	md.emplace_back(_T(""), -1);

	// add the miscellaneous setup options
	md.emplace_back(LoadStringT(IDS_MENU_CLEAR_CREDITS), ID_CLEAR_CREDITS);
	md.emplace_back(LoadStringT(IDS_MENU_OPTIONS), ID_OPTIONS);
	md.emplace_back(_T(""), -1);

	// add the Video and Muting section
	md.emplace_back(LoadStringT(IDS_MENU_ENABLE_ALL_VIDEO), ID_ENABLE_VIDEO_GLOBAL,
		Application::Get()->IsEnableVideo() ? MenuChecked : 0);
	md.emplace_back(LoadStringT(IDS_MENU_MUTEVIDEOS),
		ID_MUTE_VIDEOS, Application::Get()->IsMuteVideos() ? MenuChecked : 0);
	md.emplace_back(LoadStringT(IDS_MENU_MUTETABLEAUDIO),
		ID_MUTE_TABLE_AUDIO, Application::Get()->IsMuteTableAudio() ? MenuChecked : 0);
	md.emplace_back(LoadStringT(IDS_MENU_MUTEBUTTONS), ID_MUTE_BUTTONS,
		muteButtons ? MenuChecked : 0);
	md.emplace_back(LoadStringT(IDS_MENU_MUTEREPEATBUTTONS), ID_MUTE_REPEAT_BUTTONS,
		muteRepeatButtons ? MenuChecked : 0);
	md.emplace_back(LoadStringT(IDS_MENU_MUTEATTRACTMODE), ID_MUTE_ATTRACTMODE,
		Application::Get()->IsMuteAttractMode() ? MenuChecked : 0);

	// add the Pinscape Night Mode section, if there are any Pinscape units
	if (Application::Get()->UpdatePinscapeDeviceList())
	{
		bool psNightMode;
		Application::Get()->GetPinscapeNightMode(psNightMode);
		md.emplace_back(LoadStringT(IDS_MENU_PINSCAPENIGHTMODE), ID_PINSCAPE_NIGHT_MODE,
			psNightMode ? MenuChecked : 0);
	}
	md.emplace_back(_T(""), -1);

	// if the Exit menu is disabled, show the Exit options
	if (!ConfigManager::GetInstance()->GetBool(ConfigVars::ExitMenuEnabled, true))
	{
		md.emplace_back(LoadStringT(IDS_MENU_EXIT), ID_EXIT);
		md.emplace_back(LoadStringT(IDS_MENU_SHUTDOWN), ID_SHUTDOWN);
		md.emplace_back(_T(""), -1);
	}

	// Help/About block
	md.emplace_back(LoadStringT(IDS_MENU_HELP), ID_HELP);
	md.emplace_back(LoadStringT(IDS_MENU_ABOUT), ID_ABOUT);
	md.emplace_back(_T(""), -1);

	// cancel menu
	md.emplace_back(LoadStringT(IDS_MENU_SETUP_RETURN), ID_MENU_RETURN);

	// show the menu
	ShowMenu(md, L"operator", 0);

	// trigger the normal Menu Open effects
	QueueDOFPulse(L"PBYMenuOpen");
}

void PlayfieldView::ShowGameSetupMenu()
{
	// get the current game
	auto gl = GameList::Get();
	auto game = gl->GetNthGame(0);

	// do nothing if there's no game
	if (!IsGameValid(game))
		return;

	// set up the menu
	std::list<MenuItemDesc> md;

	// basic game commands - edit, delete, categories
	md.emplace_back(LoadStringT(IDS_MENU_EDIT_GAME_INFO), ID_EDIT_GAME_INFO);
	if (game->gameXmlNode != nullptr)
		md.emplace_back(LoadStringT(IDS_MENU_DEL_GAME_INFO), ID_DEL_GAME_INFO);
	md.emplace_back(LoadStringT(IDS_MENU_HIDE_GAME), ID_HIDE_GAME,
		gl->IsHidden(game) ? MenuChecked : 0);
	md.emplace_back(LoadStringT(IDS_MENU_SET_CATEGORIES), ID_SET_CATEGORIES);
	md.emplace_back(_T(""), -1);

	// Media commands - capture, mark for batch capture, find media, show media
	md.emplace_back(LoadStringT(IDS_MENU_CAPTURE_MEDIA), ID_CAPTURE_MEDIA);

	// include batch capture only if the game has been configured
	if (game->gameXmlNode != nullptr)
	{
		if (gl->IsMarkedForCapture(game))
			md.emplace_back(LoadStringT(IDS_MENU_MARKED_BATCH), ID_MARK_FOR_BATCH_CAPTURE, MenuChecked);
		else
			md.emplace_back(LoadStringT(IDS_MENU_MARK_BATCH), ID_MARK_FOR_BATCH_CAPTURE);
	}
	md.emplace_back(LoadStringT(IDS_MENU_FIND_MEDIA), ID_FIND_MEDIA);
	md.emplace_back(LoadStringT(IDS_MENU_SHOW_MEDIA), ID_SHOW_MEDIA_FILES);
	md.emplace_back(LoadStringT(IDS_MENU_ADJUST_AUDIO_VOLUME), ID_ADJUST_AUDIO_VOLUME);
	md.emplace_back(_T(""), -1);

	// cancel
	md.emplace_back(LoadStringT(IDS_MENU_SETUP_RETURN), ID_MENU_RETURN);

	// show the menu
	ShowMenu(md, L"game setup", 0);
	QueueDOFPulse(L"PBYMenuOpen");
}

void PlayfieldView::EditGameInfo()
{
	// Private messages within this dialog.  Note that it's not safe to
	// define these in the WM_USER range, since the dialog manager owns
	// the underlying dialog window class and defines several messages
	// above WM_USER.  We'll use the private "dialog extension" range
	// we allocate for ourselves.
	static const UINT MsgInitThreadDone = PrivateDialogMessageFirst;
	static const UINT MsgFixTitle = PrivateDialogMessageFirst + 1;

	class EditGameDialog : public RefCounted, public DialogWithSavedPos
	{
	public:
		EditGameDialog(PlayfieldView *pfv, GameListItem *game) : 
			DialogWithSavedPos(ConfigVars::GameInfoDialogPos),
			pfv(pfv), game(game), gameFile(game->filename), saved(false)
		{ 
			// build the full path to the table file
			TCHAR path[MAX_PATH];
			PathCombine(path, game->tableFileSet->tablePath.c_str(), game->filename.c_str());
			gamePath = path;

			// load the custom checkbox bitmap
			bmpKeepWinCkbox.reset(GPBitmapFromPNG(IDB_KEEP_WIN_CKBOX));
		}

		// the "keep window open" custom tri-state checkbox background PNG
		std::unique_ptr<Gdiplus::Bitmap> bmpKeepWinCkbox;

		// Did we save changes?  This is set if the user dismisses
		// the dialog with the Save button, in which case the caller
		// will refresh the UI to apply any updates visible in the
		// game wheel.
		bool saved;

		// the game we're editing
		GameListItem *game;

		// Game filename.  We store this separately for the sake of the
		// background initializer thread, so that the thread doesn't have
		// to maintain a dependency on the GameListItem object.
		TSTRING gameFile;

		// Full path to the game file
		TSTRING gamePath;

		// playfield view we're opened under
		PlayfieldView *pfv;

		// title combo box edit control child
		HWND hwndTitleEdit = NULL;

		// Initial table match list, for the filename.  This is populated by the 
		// InitFields() initializer thread.
		std::list<RefTableList::Table> initTableMatches;
		bool initTableMachesReady = false;

		// Table match list for the name typed into the title box by the user.
		std::list<RefTableList::Table> typedTableList;

		// Custom dialog message handler
		virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam)
		{
			switch (message) 
			{
			case WM_INITDIALOG:
				// initialize fields
				InitFields();

				// subclass the title combo
				SubclassTitleCombo();
				break;

			case MsgInitThreadDone:
				OnInitThreadDone();
				return 0;

			case WM_SYSCOMMAND:
				switch (wParam)
				{
				case SC_CONTEXTHELP:
					pfv->ShowHelp(_T("EditGameDetails"));

					// skip the system handling (which would go into help-cursor mode)
					return TRUE;
				}
				break;

			case WM_COMMAND:
				switch (LOWORD(wParam))
				{
				case IDC_CB_TITLE:
					switch (HIWORD(wParam))
					{
					case CBN_SELENDOK:
						// selected a new title
						OnSelectTitle();
						return 0;

					case CBN_SETFOCUS:
						// show the drop list
						if (IsWindowVisible(GetDlgItem(IDC_CB_TITLE)))
						{
							ComboBox_ShowDropdown(GetDlgItem(IDC_CB_TITLE), TRUE);
							SetCursor(LoadCursor(NULL, IDC_ARROW));
						}
						return 0;

					case CBN_KILLFOCUS:
						// close the drop list
						ComboBox_ShowDropdown(GetDlgItem(IDC_CB_TITLE), FALSE);
						return 0;
					}
					break;

				case IDC_CB_SYSTEM:
					switch (HIWORD(wParam))
					{
					case CBN_SELENDOK:
						// selected a new system
						OnSelectSystem();
						return 0;
					}
					break;

				case IDC_BTN_FILL_FROM_IPDBID:
					OnFillFromIPDB();
					break;

				case IDOK:
					// try saving changes - if that fails, cancel further
					// processing so that we don't dismiss the dialog
					if (!SaveChanges())
						return 0;

					// proceed to the default handling to dismiss
					break;
				}
				break;

			case WM_NOTIFY:
				if (reinterpret_cast<NMHDR*>(lParam)->code == NM_CUSTOMDRAW)
				{
					SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CustomDraw(static_cast<int>(wParam), reinterpret_cast<NMCUSTOMDRAW*>(lParam)));
					return TRUE;
				}
				break;

			case MsgFixTitle:
				OnFixTitle(lParam);
				return 0;
			}

			// do the base class work
			return __super::Proc(message, wParam, lParam);
		}

		LRESULT CustomDraw(int ctlId, NMCUSTOMDRAW *nm)
		{
			switch (ctlId)
			{
			case IDC_CK_SHOW_WHEN_RUNNING_BG:
			case IDC_CK_SHOW_WHEN_RUNNING_DMD:
			case IDC_CK_SHOW_WHEN_RUNNING_TOPPER:
			case IDC_CK_SHOW_WHEN_RUNNING_INSTCARD:
				// custom-draw the "show when running" checkboxes
				return CustomDrawCheckbox(ctlId, nm);
			}

			// use the default drawing behavior
			return CDRF_DODEFAULT;
		}

		LRESULT CustomDrawCheckbox(int ctlId, NMCUSTOMDRAW *nm)
		{
			switch (nm->dwDrawStage)
			{
			case CDDS_PREPAINT:
				return CDRF_NOTIFYPOSTPAINT;

			case CDDS_POSTPAINT:
				if (HWND hwndCtl = GetDlgItem(ctlId); hwndCtl != NULL)
				{
					// get the square at the left of the checkbox area
					RECT rc = nm->rc;
					rc.right = rc.left + rc.bottom - rc.top;

					// erase it by filling it with the parent background color
					DrawThemeParentBackground(hwndCtl, nm->hdc, &rc);

					// figure the current state
					UINT state = IsDlgButtonChecked(hDlg, ctlId);
					bool checked = state == BST_CHECKED;
					bool indet = state == BST_INDETERMINATE;
					bool hot = (nm->uItemState & CDIS_HOT) != 0;
					bool clicked = hot && (GetKeyState(VK_LBUTTON) < 0);

					// Figure the offset based on the state.  Each cell in the source
					// image is 32x32 pixels.  The cells are arranged horizontally,
					// in groups of Normal/Hot/Clicked, in order, Checked, Default,
					// Unchecked.
					int xSrc = (checked ? 0 : indet ? 96 : 192) + (clicked ? 64 : hot ? 32 : 0);

					// draw the bitmap
					Gdiplus::Graphics g(nm->hdc);
					g.DrawImage(bmpKeepWinCkbox.get(), 
						Gdiplus::Rect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top),
						xSrc, 0, 32, 32, Gdiplus::UnitPixel);
				}
				break;

			default:
				break;
			}

			// use the default drawing behavior
			return CDRF_DODEFAULT;
		}

		// subclass the title combo control, for auto-complete actions
		void SubclassTitleCombo()
		{
			// get the combo, and find its edit box component (it's a child of the combo)
			HWND combo = GetDlgItem(IDC_CB_TITLE);
			hwndTitleEdit = FindWindowEx(combo, NULL, WC_EDIT, NULL);

			// subclass the edit control
			SetProp(hwndTitleEdit, _T("WNDPROC"), reinterpret_cast<HANDLE>(GetWindowLongPtr(hwndTitleEdit, GWLP_WNDPROC)));
			SetProp(hwndTitleEdit, _T("THIS"), reinterpret_cast<HANDLE>(this));
			SubclassWindow(hwndTitleEdit, &ComboEditProc);
		}

		// subclassed window proc for the combo edit box
		static LRESULT CALLBACK ComboEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			switch (msg)
			{
			case WM_GETDLGCODE:
				// let the dialog handle tab
				if (wParam == VK_TAB)
					return FALSE;

				// if it's Escape or Enter, and the drop list is closed, let the dialog handle it
				if ((wParam == VK_ESCAPE || wParam == VK_RETURN)
					&& !ComboBox_GetDroppedState(GetParent(hwnd)))
					return FALSE;

				// intercept all other keys
				return DLGC_WANTALLKEYS;

			case WM_CHAR:
				// handle character events specially
				{
					LRESULT lResult;
					if (reinterpret_cast<EditGameDialog*>(GetProp(hwnd, _T("THIS")))->OnTitleChar(hwnd, msg, wParam, lParam, lResult))
						return lResult;
				}
				break;

			case WM_KEYDOWN:
				// handle keystroke events
				{
					LRESULT lResult;
					if (reinterpret_cast<EditGameDialog*>(GetProp(hwnd, _T("THIS")))->OnTitleKeyDown(hwnd, msg, wParam, lParam, lResult))
						return lResult;
				}
				break;

			case WM_DESTROY:
				// undo our subclassing by setting the wndproc back to the original
				SubclassWindow(hwnd, GetProp(hwnd, _T("WNDPROC")));
				RemoveProp(hwnd, _T("WNDPROC"));
				RemoveProp(hwnd, _T("THIS"));
				break;
			}

			// inherit the base class handling
			return CallWindowProc(reinterpret_cast<WNDPROC>(GetProp(hwnd, _T("WNDPROC"))), hwnd, msg, wParam, lParam);
		}

		// Update the title drop list
		void UpdateTitleDroplist()
		{
			// get the current text
			HWND combo = GetDlgItem(IDC_CB_TITLE);
			TCHAR txt[256];
			GetWindowText(combo, txt, countof(txt));

			// get the ref table list
			auto rtl = Application::Get()->refTableList.get();

			// Populate the droplist with close matches to the current string.
			// If the string is short, do a simple leading substring match.  
			// Otherwise do a similarity match.  We don't attempt a similarity
			// match on a short string since we can't get decent results until
			// there's enough text to winnow the field a bit.   If the edit box
			// is completely empty, revert to the original list based on the
			// filename or prior title entry.
			std::list<RefTableList::Table> lst;
			if (txt[0] == 0 && initTableMachesReady)
				lst = initTableMatches;
			else if (_tcslen(txt) <= 3)
				rtl->GetInitMatches(txt, 10, lst);
			else
				rtl->GetTitleFragmentMatches(txt, 10, lst);

			// if we found any matches, populate the combo
			if (lst.size() != 0)
			{
				// clear out the old list
				ClearComboList(combo);
				typedTableList.clear();

				// populate it with the new list
				for (auto &ele : lst)
				{
					int idx = ComboBox_AddString(combo, ele.listName.c_str());
					RefTableList::Table &newEle = typedTableList.emplace_back(ele);
					RefTableList::Table *pNewEle = &newEle;
					ComboBox_SetItemData(combo, idx, reinterpret_cast<LPARAM>(&newEle));
				}
			}
		}

		// Handle key down events in the title combo box
		bool OnTitleKeyDown(HWND edit, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT &lResult)
		{
			// on the down-arrow key, open the drop list if it's not already open
			HWND combo = GetDlgItem(IDC_CB_TITLE);
			if (wParam == VK_DOWN && !ComboBox_GetDroppedState(combo))
			{
				// remember the current text and selection range
				TCHAR txt[256];
				GetWindowText(combo, txt, countof(txt));
				DWORD sel = ComboBox_GetEditSel(combo);
				DWORD start = LOWORD(sel), end = HIWORD(sel);

				// show the drop list
				ComboBox_ShowDropdown(combo, TRUE);

				// set the arrow cursor explicitly
				SetCursor(LoadCursor(NULL, IDC_ARROW));

				// restore the prior edit text and selection range
				SetWindowText(combo, txt);
				ComboBox_SetEditSel(combo, start, end);

				// skip the normal handling, since that would also change the
				// list selection, which we don't want at this point - we want
				// down-arrow to just mean "open the list box"
				lResult = 0;
				return true;
			}

			// let the regular window handler have it
			return false;
		}

		// Handle character events in the title combo box.  Returns true if we
		// handle the event, false if not (in which case control should be passed 
		// to the default window proc).
		bool OnTitleChar(HWND edit, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT &lResult)
		{
			// get the combo
			HWND combo = GetDlgItem(IDC_CB_TITLE);

			// note if the combo list is dropped down
			bool dropped = ComboBox_GetDroppedState(combo);

			// the wparam is the character code
			WCHAR ch = static_cast<WCHAR>(wParam);

			// on escape, just close the drop list
			if (ch == 27)
			{
				ComboBox_ShowDropdown(combo, FALSE);
				return true;
			}

			// on Enter, accept the current selection and close the combo
			if (ch == 10 || ch == 13)
			{
				// if it's open, and no list item is currently selected,
				// close the list and keep the current text
				if (dropped && ComboBox_GetCurSel(combo) < 0)
				{
					// retrieve the current text and selection
					TCHAR txt[256];
					GetWindowText(combo, txt, countof(txt));
					DWORD sel = ComboBox_GetEditSel(combo);
					DWORD start = LOWORD(sel), end = HIWORD(sel);

					// close the combo
					ComboBox_ShowDropdown(combo, FALSE);

					// restore the text and selection
					SetWindowText(combo, txt);
					ComboBox_SetEditSel(combo, start, end);

					// bypass the normal behavior
					lResult = 0;
					return true;
				}

				// use the default behavior
				return false;
			}

			// ignore control characters except backspace
			if (iswcntrl(ch) && ch != 8)
				return true;

			// Backspace is a bit of a special case for auto-complete.  If
			// the current selection range extends to the end of the string,
			// take it to be an auto-complete range.  In this case, we want
			// backspace to delete the prior character, as though the auto-
			// complete selection weren't even there.
			if (ch == 8 && dropped)
			{
				// get the current text and selection range
				TCHAR txt[256];
				GetWindowText(combo, txt, countof(txt));
				DWORD sel = ComboBox_GetEditSel(combo);
				DWORD start = LOWORD(sel), end = HIWORD(sel);

				// does the selection extend to the end of the text?
				if (end >= _tcslen(txt) && start > 0)
				{
					// extend the selection range to the previous character
					ComboBox_SetEditSel(combo, start - 1, end);
				}
			}

			// invoke the normal handling first to update the text
			lResult = CallWindowProc(reinterpret_cast<WNDPROC>(GetProp(edit, _T("WNDPROC"))), edit, msg, wParam, lParam);

			// update auto-complete
			UpdateTitleAutoComplete(true);

			// handled
			return true;
		}

		// Update auto-complete, forcing the combo list open if desired
		void UpdateTitleAutoComplete(bool forceOpen)
		{
			// if the drop list isn't open, do nothing unless we want to force it open
			HWND combo = GetDlgItem(IDC_CB_TITLE);
			bool dropped = ComboBox_GetDroppedState(combo);
			if (!dropped && !forceOpen)
				return;

			// update the auto-complete list
			UpdateTitleDroplist();

			// remember the current text and selection range
			TCHAR txt[256];
			GetWindowText(combo, txt, countof(txt));
			DWORD sel = ComboBox_GetEditSel(combo);
			DWORD start = LOWORD(sel), end = HIWORD(sel);

			// if the selection range extends to the end of the text, keep only
			// the portion up to the end of the selection - we'll assume that the
			// selected range is only there from a prior auto-complete and wasn't
			// actually entered by the user
			bool isAutoComplete = false;
			if (end >= _tcslen(txt) && start >= 0)
			{
				isAutoComplete = true;
				end = start;
				txt[end] = 0;
			}

			// if the combo isn't already open, open it
			if (!dropped)
			{
				// show the drop list
				ComboBox_ShowDropdown(combo, TRUE);

				// Explicitly set the arrow cursor, since the edit control might
				// have hidden it on the key press event, and the drop list window
				// doesn't provide its own cursor.
				SetCursor(LoadCursor(NULL, IDC_ARROW));

				// If we're not auto-completing, restore the old text and remove
				// the drop list selection.  Opening the drop list selects the first
				// list item and updates the text to match, which we don't want to
				// do if we're not auto-completing anything this time.
				if (!isAutoComplete)
				{
					SetWindowText(combo, txt);
					ComboBox_SetEditSel(combo, start, end);
				}
			}

			// fill in the auto-complete if desired
			if (isAutoComplete)
			{
				// find the first drop list entry with a matching leading substring
				int index = ComboBox_FindString(combo, -1, txt);
				if (index >= 0)
				{
					ComboBox_SetCurSel(combo, index);
					ComboBox_SetEditSel(combo, start, -1);
				}
			}
		}

		// Fill the form from the IPDB table ID entry
		void OnFillFromIPDB()
		{
			// get the IPDB ID field
			TCHAR id[128];
			if (!GetDlgItemText(hDlg, IDC_TXT_IPDB_ID, id, countof(id)))
				id[0] = 0;

			// trim leading and trailing spaces
			TCHAR *p;
			for (p = id + _tcslen(id); p > id && _istspace(*(p - 1)); *--p = 0);
			for (p = id; _istspace(*p); ++p);

			// look up the game
			std::unique_ptr<RefTableList::Table> table;
			if (!Application::Get()->refTableList->GetByIpdbId(p, table))
			{
				MessageBox(hDlg, LoadStringT(IDS_ERR_INVAL_IPDB_ID),
					LoadStringT(IDS_APP_TITLE), MB_OK | MB_ICONERROR);
				return;
			}

			// populate the fields from the table data
			SetDlgItemText(hDlg, IDC_CB_TITLE, table->name.c_str());
			SetDlgItemText(hDlg, IDC_CB_MANUF, table->manuf.c_str());
			SetDlgItemInt(hDlg, IDC_TXT_YEAR, table->year, FALSE);
			SetDlgItemText(hDlg, IDC_CB_TABLE_TYPE, table->machineType.c_str());

			// refill the ROM combo with possible matches for the new title
			PopulateROMCombo();
		}

		// Save changes
		bool SaveChanges()
		{
			// Make sure a system is selected.  The database entry is
			// associated with the system, so a system is required.
			HWND cbSys = GetDlgItem(IDC_CB_SYSTEM);
			int sysIdx = ComboBox_GetCurSel(cbSys);
			if (sysIdx < 0)
			{
				MessageBox(hDlg, LoadStringT(IDS_ERR_MUST_SELECT_SYS),
					LoadStringT(IDS_APP_TITLE), MB_OK | MB_ICONINFORMATION);
				return false;
			}

			// get a text field into a TSTRING
			auto gl = GameList::Get();
			auto GetText = [this](int controlId, TSTRING &item)
			{
				// get the text
				TCHAR buf[1024];
				if (GetDlgItemText(hDlg, controlId, buf, countof(buf)) != 0)
					item = buf;
			};

			// Validate the media name.  It must be a valid filename, with
			// no path portion.
			TSTRING newMediaName;
			GetText(IDC_TXT_MEDIA_NAME, newMediaName);
			static const std::basic_regex<TCHAR> invalMediaNamePat(
				_T(".*[\\\\/:<>\"|?*].*|con|prn|aux|nul|com\\d|lpt\\d"), std::regex_constants::icase);
			if (std::regex_match(newMediaName, invalMediaNamePat))
			{
				MessageBox(hDlg, LoadStringT(IDS_ERR_INVAL_MEDIA_NAME),
					LoadStringT(IDS_APP_TITLE), MB_OK | MB_ICONERROR);
				SetFocus(GetDlgItem(IDC_TXT_MEDIA_NAME));
				return false;
			}

			// update the basic metadata items
			GetText(IDC_CB_TITLE, game->title);
			GetText(IDC_CB_ROM, game->rom);
			GetText(IDC_TXT_IPDB_ID, game->ipdbId);

			// update the year
			TSTRING year;
			GetText(IDC_TXT_YEAR, year);
			game->year = _ttoi(year.c_str());

			// make sure there's an era filter that includes this year
			gl->FindOrAddDateFilter(game->year);

			// Set the table type.  Note that we only keep the first token;
			// for readability, the combo box list items show the internal
			// token followed by explanatory text in parens.  We only want
			// that internal token.
			TSTRING tableType;
			GetText(IDC_CB_TABLE_TYPE, tableType);
			game->tableType = GetFirstToken(tableType);

			// Set the high score display style.  Keep only the first token,
			// for the same reasons as for the table type.
			TSTRING hiScoreStyle;
			GetText(IDC_CB_HIGH_SCORE_STYLE, hiScoreStyle);
			gl->SetHighScoreStyle(game, GetFirstToken(hiScoreStyle).c_str());

			// If the ROM name selected is the first item in the list,
			// which is always the default selection, set it to an empty
			// string.  In the database, "default" is represented as
			// an empty string.
			TCHAR dflt[256] = { 0 };
			ComboBox_GetLBText(GetDlgItem(IDC_CB_ROM), 0, dflt);
			if (game->rom == dflt)
				game->rom = _T("");

			// Update the manufacturer
			TSTRING manuf;
			GetText(IDC_CB_MANUF, manuf);
			game->manufacturer = gl->FindOrAddManufacturer(manuf.c_str());

			// Update the Date Added
			TSTRING dateAddedStr;
			GetText(IDC_TXT_DATE_ADDED, dateAddedStr);
			DateTime dateAdded;
			if (dateAdded.Parse(dateAddedStr.c_str()))
				gl->SetDateAdded(game, dateAdded);

			// Update the grid position
			TSTRING gridPos;
			GetText(IDC_CB_GRIDPOS, gridPos);
			std::basic_regex<TCHAR> gridPat(_T("\\s*(\\d+)\\s*x\\s*(\\d+)\\s*"), std::regex_constants::icase);
			std::match_results<TSTRING::const_iterator> m;
			if (std::regex_match(gridPos, m, gridPat))
			{
				// get the Row x Col position from the string
				game->gridPos.row = _ttoi(m[1].str().c_str());
				game->gridPos.col = _ttoi(m[2].str().c_str());
			}
			else
			{
				// no grid position or invalid position - set the row and
				// column to zero to indicate that there's no position set
				game->gridPos.row = game->gridPos.col = 0;
			}

			// Update the "Show When Running" list
			TSTRING showWhenRunning;
			auto TestShowWhenRunning = [&showWhenRunning, this](int buttonId, const TCHAR *windowId)
			{
				int ck = IsDlgButtonChecked(hDlg, buttonId);
				if (ck == BST_CHECKED)
				{
					if (showWhenRunning.length() != 0) showWhenRunning += _T(" ");
					showWhenRunning += windowId;
				}
				else if (ck == BST_UNCHECKED)
				{
					if (showWhenRunning.length() != 0) showWhenRunning += _T(" ");
					showWhenRunning += _T("-");
					showWhenRunning += windowId;
				}
			};
			TestShowWhenRunning(IDC_CK_SHOW_WHEN_RUNNING_BG, _T("bg"));
			TestShowWhenRunning(IDC_CK_SHOW_WHEN_RUNNING_DMD, _T("dmd"));
			TestShowWhenRunning(IDC_CK_SHOW_WHEN_RUNNING_TOPPER, _T("topper"));
			TestShowWhenRunning(IDC_CK_SHOW_WHEN_RUNNING_INSTCARD, _T("instcard"));
			gl->SetShowWhenRunning(game, showWhenRunning.c_str());

			// Update the system.  This is the last step, because it might
			// require moving the entry from one database file to another,
			// or creating a whole new entry.
			gl->ChangeSystem(game, reinterpret_cast<GameSystem*>(ComboBox_GetItemData(cbSys, sysIdx)));

			// If the new media name is empty or "[Default]", apply the default 
			// media name pattern.  Note that this has to wait until we've updated
			// the other fields, since the default name is derived from the other
			// fields.
			static const std::basic_regex<TCHAR> defaultMediaNamePat(_T("\\s*(\\[Default\\])?\\s*"), std::regex_constants::icase);
			if (std::regex_match(newMediaName, defaultMediaNamePat))
				newMediaName = game->GetDefaultMediaName();

			// Check for media item renaming
			std::list<std::pair<TSTRING, TSTRING>> mediaRenameList;
			bool mediaNameChanged = game->UpdateMediaName(&mediaRenameList, newMediaName.c_str());
			bool mediaFilesRenamed = false;
			if (mediaNameChanged && mediaRenameList.size() != 0)
			{
				// ask the user what to do
				if (MessageBox(hDlg, LoadStringT(IDS_RENAME_MEDIA_PROMPT).c_str(),
					LoadStringT(IDS_APP_TITLE), MB_YESNO | MB_ICONQUESTION) == IDYES)
				{
					// yes - do the renaming
					mediaFilesRenamed = true;
					CapturingErrorHandler ceh;
					if (!pfv->ApplyGameChangesRenameMediaFiles(game, mediaRenameList, ceh))
					{
						// one or more renaming errors occurred - report them as a group
						InteractiveErrorHandler ieh;
						ieh.GroupError(EIT_Error, LoadStringT(IDS_ERR_RENAME_MEDIA).c_str(), ceh);
					}
				}
			}

			// If the media name changed, and we didn't rename any files,
			// explicitly clear media, so that we switch the display to
			// media files under the new names (if any).  This isn't
			// necessary if we renamed the files, since that will have
			// cleared the media already as a side effect.
			if (mediaNameChanged && !mediaFilesRenamed)
				Application::Get()->ClearMedia();

			// apply changes to the XML database file
			pfv->ApplyGameChangesToDatabase(game);

			// note that we saved changes
			saved = true;

			// success
			return true;
		}

		// Handle a selection in the Title combo box.  When the user
		// selects a game entry that came from the reference table list,
		// we populate the other fields (manufacturer, year) with the
		// corresponding data from the reference list.  This makes it
		// quicker to set up tables that are re-creations of real arcade
		// machines by pre-populating most of the fields.
		void OnSelectTitle()
		{
			// get the current selection's item data - this is the table
			// record from the reference list
			HWND cbTitle = GetDlgItem(IDC_CB_TITLE);
			if (int selIdx = ComboBox_GetCurSel(cbTitle); selIdx >= 0)
			{
				auto selTable = reinterpret_cast<const RefTableList::Table*>(ComboBox_GetItemData(cbTitle, selIdx));
				if (selTable != nullptr)
				{
					// Post a "fix title" message.  We have to defer this because
					// the combo box select notification is sent before the system
					// code has completed the update itself, so whatever we set as
					// the window text will get overwritten after we return.  We
					// can get control again by deferring the text update via a
					// posted message.
					::PostMessage(hDlg, MsgFixTitle, 0, reinterpret_cast<LPARAM>(selTable));

					// store the manufacturer
					if (selTable->manuf.length() != 0)
						SetDlgItemText(hDlg, IDC_CB_MANUF, selTable->manuf.c_str());

					// store the year
					if (selTable->year != 0)
						SetDlgItemText(hDlg, IDC_TXT_YEAR, MsgFmt(_T("%d"), selTable->year).Get());

					// store the IPDB ID
					if (selTable->ipdbId.length() != 0)
						SetDlgItemText(hDlg, IDC_TXT_IPDB_ID, selTable->ipdbId.c_str());

					// store the type
					if (selTable->machineType.length() != 0)
						SetDlgItemText(hDlg, IDC_CB_TABLE_TYPE, selTable->machineType.c_str());

					// populate the ROM combo list with possible matches
					PopulateROMCombo();
				}
			}
		}

		// handle a selection in the system list
		void OnSelectSystem()
		{
			// get the selection
			HWND cbSys = GetDlgItem(IDC_CB_SYSTEM);
			int sysIdx = ComboBox_GetCurSel(cbSys);

			// get the system 
			GameSystem *sys = sysIdx < 0 ? nullptr :
				reinterpret_cast<GameSystem*>(ComboBox_GetItemData(cbSys, sysIdx));

			// Show or hide the grid position selection according to whether or
			// not the system makes use of it.  It's used if the StartupKeys
			// entry for the system includes "[gridpos]" anywhere.  Note that
			// this simple search for the substring could be fooled by embedding
			// the substring in a comment, but I don't see any good reason to
			// worry about that possibility.
			int showGrid = sys != nullptr && sys->startupKeys.find(_T("[gridpos")) != std::string::npos ?
				SW_SHOW : SW_HIDE;
			ShowWindow(GetDlgItem(IDC_ST_GRIDPOS), showGrid);
			ShowWindow(GetDlgItem(IDC_CB_GRIDPOS), showGrid);
		}

		// populate the ROM combo list
		void PopulateROMCombo()
		{
			// Remember the old default text (the first combo item)
			TCHAR oldDefault[256] = { 0 };
			ComboBox_GetLBText(GetDlgItem(IDC_CB_ROM), 0, oldDefault);

			// Get the game title text
			TCHAR title[512];
			GetDlgItemText(hDlg, IDC_CB_TITLE, title, countof(title));

			// construct a set of names as we go (use a map to avoid duplicates:
			// the key is the lower-case version of the name)
			std::unordered_map<TSTRING, TSTRING> roms;
			auto AddRom = [&roms](const TCHAR *name)
			{
				TSTRING key = name;
				std::transform(key.begin(), key.end(), key.begin(), ::_totlower);
				if (roms.find(key) == roms.end())
					roms.emplace(key, name);
			};
			
			// Start with the list from the PINEmHi ini file, as that
			// has a fairly comprehensive list of games supported in
			// VPinMAME (and thus games likely to be found on a virtual
			// pin cab).
			std::list<TSTRING> nvList;
			HWND cbRom = GetDlgItem(IDC_CB_ROM);
			ClearComboList(cbRom);
			TSTRING vpmRomTemplate;
			if (Application::Get()->highScores->GetAllNvramFiles(nvList, title))
			{
				// Add each .nv file in the list
				std::basic_regex<TCHAR> nvPat(_T("\\.nv$"));
				for (auto &nv : nvList)
				{
					// The NVRAM files for VPinMAME games are the same as the
					// ROM names, with the .nv suffix.  So strip off the .nv
					// suffix to get the ROM name.
					TSTRING rom = std::regex_replace(nv, nvPat, _T(""));
					AddRom(rom.c_str());

					// use the first thing we find as the VPM ROM template, or
					// the last thing that doesn't have an underscore in the name
					if (vpmRomTemplate.length() == 0 || rom.find('_') == std::string::npos)
						vpmRomTemplate = rom;
				}
			}

			// If there's a DOF ROM for the title, and it's not already in
			// the list, add that as well
			if (auto dof = DOFClient::Get(); dof != nullptr && DOFClient::IsReady())
			{
				// DOF is active - look up the DOF ROM by title
				if (auto rom = dof->GetRomForTitle(title, nullptr); rom != nullptr)
				{
					// got it - if it's not already in the combo list, add it
					AddRom(rom);

					// use it as the VPM search template if appropriate
					if (vpmRomTemplate.length() == 0 || _tcschr(rom, '_') == nullptr)
						vpmRomTemplate = rom;
				}
			}

			// If there's a unique entry in the VPinMAME configuration
			// data, set that as the default.
			TSTRINGEx dflt;
			if (vpmRomTemplate.length() != 0)
			{
				// get the installed VPM ROMs matching the template
				std::list<TSTRING> vpmRoms;
				VPinMAMEIfc::GetInstalledRomVersions(vpmRoms, vpmRomTemplate.c_str());

				// if it's unique, add a default entry for it
				if (vpmRoms.size() == 1)
					dflt.Format(LoadStringT(IDS_ROMCOMBO_DEFAULT_NAME), vpmRoms.front().c_str());
			}

			// if we didn't come up with a known default, load the empty default
			if (dflt.length() == 0)
				dflt.Load(IDS_ROMCOMBO_DEFAULT_EMPTY);

			// The default string always goes first in the list
			ComboBox_AddString(cbRom, dflt.c_str());

			// Now add the known ROM names, in sorted order.  To do this, build
			// it out as a vector, then sort the strings.
			std::vector<TSTRING> romv;
			romv.reserve(roms.size());
			for (auto &s : roms)
				romv.emplace_back(s.second);
			std::sort(romv.begin(), romv.end(), [](const TSTRING &a, const TSTRING &b) {
				return lstrcmpi(a.c_str(), b.c_str()) < 0; });

			// add the sorted strings
			for (auto &s : romv)
				ComboBox_AddString(cbRom, s.c_str());

			// If the field is currently blank, or it's set to the old default
			// entry, set it to the new default entry.
			TCHAR curtxt[512];
			GetDlgItemText(hDlg, IDC_CB_ROM, curtxt, countof(curtxt));
			if (curtxt[0] == 0 || _tcscmp(curtxt, oldDefault) == 0)
				SetDlgItemText(hDlg, IDC_CB_ROM, dflt.c_str());
		}

		// clear all items out of a combo box's drop list
		void ClearComboList(HWND cb)
		{
			for (auto cnt = ComboBox_GetCount(cb); cnt != 0; )
			{
				--cnt;
				ComboBox_DeleteString(cb, cnt);
			}
		}

		// Fix up a title for use in the main title field.  This makes
		// a few character pattern substitutions for titles that come
		// from the IPDB reference table list.  In particular, we strip
		// trademark symbols ("TM" superscripts and circle-R symbols),
		// which appear in some IPDB titles and which most people don't
		// want to retain in our UI displays.
		void OnFixTitle(LPARAM lParam)
		{
			// the reference table record is in the LPARAM
			auto selTable = reinterpret_cast<const RefTableList::Table*>(lParam);

			// store the title, with certain special characters removed
			// (\xAE = "(R)" trademark symbol, \x99 = "TM" symbol)
			std::basic_regex<TCHAR> stripPat(_T("[\xAE\x99]"));
			TSTRING title = std::regex_replace(selTable->name, stripPat, _T(""));
			SetDlgItemText(hDlg, IDC_CB_TITLE, title.c_str());
		}

		// extract the first token of a combo box string, delimited by a space
		static TSTRING GetFirstToken(const TSTRING &s)
		{
			const TCHAR *p;
			for (p = s.c_str(); *p != 0 && *p != ' '; ++p);
			return TSTRING(s, 0, p - s.c_str());
		}

		// Initialize fields and combo lists
		void InitFields()
		{
			// set the filename static text
			SetDlgItemText(hDlg, IDC_TXT_FILENAME, game->filename.c_str());

			// populate the system list
			HWND cbSys = GetDlgItem(IDC_CB_SYSTEM);
			for (auto s : game->tableFileSet->systems)
			{
				// add this item
				int idx = ComboBox_AddString(cbSys, s->displayName.c_str());
				ComboBox_SetItemData(cbSys, idx, reinterpret_cast<LPARAM>(s));

				// If this is the current system, or it's the only system
				// in the list, select it.
				if (s == game->system || game->tableFileSet->systems.size() == 1)
					ComboBox_SetCurSel(GetDlgItem(IDC_CB_SYSTEM), idx);
			}

			// add a blank item for "not set"
			ComboBox_AddString(cbSys, _T(""));

			// set the grid position, if present
			HWND cbGridPos = GetDlgItem(IDC_CB_GRIDPOS);
			if (game->gridPos.row != 0 && game->gridPos.col != 0)
				ComboBox_SetText(cbGridPos, MsgFmt(_T("%dx%d"), game->gridPos.row, game->gridPos.col));

			// Get the date added
			DateTime dateAdded = GameList::Get()->GetDateAdded(game);
			if (!dateAdded.IsValid())
			{
				// The date isn't set yet.  If this game was already configured,
				// it must have been inherited from a pre-existing PinballX
				// configuration, so use the application first run date as the
				// default.  If it's not configured yet, we're adding it just now.
				if (game->isConfigured)
					dateAdded = Application::Get()->GetFirstRunTime();
				else
					dateAdded = DateTime();
			}

			// Format the date.  Only include the date part, to keep things
			// simpler in the UI presentation; there's no real benefit to
			// more precision than this, since the main use of this is for
			// the "added since" filter, which only counts days.
			SetDlgItemText(hDlg, IDC_TXT_DATE_ADDED, dateAdded.FormatLocalDate(DATE_SHORTDATE).c_str());

			// populate the Grid Position drop-down with sufficient items for
			// The Pinball Arcade's game selection menu (which is the only place
			// the grid position is currently used)
			for (int row = 1; row <= 12; ++row)
			{
				for (int col = 1; col <= 8; ++col)
					ComboBox_AddString(cbGridPos, MsgFmt(_T("%dx%d"), row, col));
			}

			// populate the manufacturer list with the common manufacturers
			static const TCHAR *commonManuf[] = {
				_T("Alvin G."),
				_T("Atari"),
				_T("Bally"),
				_T("Data East"),
				_T("Gottlieb"),
				_T("Midway"),
				_T("Premier"),
				_T("Stern"),
				_T("Williams")
			};
			HWND cbManuf = GetDlgItem(IDC_CB_MANUF);
			for (auto m : commonManuf)
				ComboBox_AddString(cbManuf, m);

			// Go through the list of manufacturers mentioned in the game
			// list as well, and add any that aren't already mentioned
			GameList::Get()->EnumManufacturers([cbManuf](const GameManufacturer *m)
			{
				// skip it if it's already in our common manufacturer list
				for (auto cm : commonManuf)
				{
					if (_tcsicmp(cm, m->manufacturer.c_str()) == 0)
						return;
				}

				// it's not in the list - add it to the combo list
				ComboBox_AddString(cbManuf, m->manufacturer.c_str());
			});

			// if the game already has a manufacturer, and it's not already
			// in the list, add it
			if (game->manufacturer != nullptr 
				&& ComboBox_FindStringExact(cbManuf, 0, game->manufacturer->manufacturer.c_str()) < 0)
				ComboBox_AddString(cbManuf, game->manufacturer->manufacturer.c_str());

			// populate the fields with the current metadata
			SetDlgItemText(hDlg, IDC_CB_TITLE, game->title.c_str());
			if (game->manufacturer != nullptr)
				SetDlgItemText(hDlg, IDC_CB_MANUF, game->manufacturer->manufacturer.c_str());
			if (game->year != 0)
				SetDlgItemText(hDlg, IDC_TXT_YEAR, MsgFmt(_T("%d"), game->year));
			SetDlgItemText(hDlg, IDC_TXT_IPDB_ID, game->ipdbId.c_str());
			SetDlgItemText(hDlg, IDC_CB_ROM, game->rom.c_str());

			// populate the "Show when running" checkboxes
			const TCHAR *showWhenRunning = GameList::Get()->GetShowWhenRunning(game);
			auto SetShowWhenRunningCheckbox = [this, showWhenRunning](int controlId, const TCHAR *which)
			{
				// search for the 'which' token in the showWhenRunning string
				if (showWhenRunning != nullptr)
				{
					size_t len = _tcslen(which);
					for (const TCHAR *p = showWhenRunning; *p != 0; )
					{
						// find the next token
						const TCHAR *nxt;
						for (; *p == ' '; ++p);
						for (nxt = p; *nxt != 0 && *nxt != ' '; ++nxt);

						// check for negation
						bool negate = false;
						if (*p == '-')
						{
							negate = true;
							++p;
						}

						// check this token
						if (len == nxt - p && _tcsnicmp(p, which, len) == 0)
						{
							// it's a match - check the box and stop looking
							CheckDlgButton(hDlg, controlId, negate ? BST_UNCHECKED : BST_CHECKED);
							return;
						}

						// advance to the next token
						p = nxt;
					}
				}

				// not found - set to the "indeterminate" state for "inherit default"
				CheckDlgButton(hDlg, controlId, BST_INDETERMINATE);
			};
			SetShowWhenRunningCheckbox(IDC_CK_SHOW_WHEN_RUNNING_BG, _T("bg"));
			SetShowWhenRunningCheckbox(IDC_CK_SHOW_WHEN_RUNNING_DMD, _T("dmd"));
			SetShowWhenRunningCheckbox(IDC_CK_SHOW_WHEN_RUNNING_TOPPER, _T("topper"));
			SetShowWhenRunningCheckbox(IDC_CK_SHOW_WHEN_RUNNING_INSTCARD, _T("instcard"));

			// initialize the ROM combo list
			PopulateROMCombo();

			// update dependent items for the initial system selection
			OnSelectSystem();

			// populate the table type combo
			const TCHAR *tableType = game->tableType.c_str();
			HWND cbTableType = GetDlgItem(IDC_CB_TABLE_TYPE);
			for (auto const &s : LoadStringT(IDS_TABLETYPECOMBO_STRINGS).Split(';'))
			{
				// add the string
				ComboBox_AddString(cbTableType, s.c_str());

				// select this string if its first token matches the db value
				if (_tcsicmp(tableType, GetFirstToken(s).c_str()) == 0)
					ComboBox_SetText(cbTableType, s.c_str());
			}

			// populate the high score display type combo
			const TCHAR *hiScoreStyle = GameList::Get()->GetHighScoreStyle(game);
			HWND cbHiScoreStyle = GetDlgItem(IDC_CB_HIGH_SCORE_STYLE);
			auto hiScoreStrings = LoadStringT(IDS_HISCORECOMBO_STRINGS).Split(';');
			for (auto const &s : hiScoreStrings)
			{
				// add the string
				ComboBox_AddString(cbHiScoreStyle, s.c_str());

				// select this string if its first token matches the db value
				if (hiScoreStyle != nullptr && _tcsicmp(hiScoreStyle, GetFirstToken(s).c_str()) == 0)
					ComboBox_SetText(cbHiScoreStyle, s.c_str());
			}

			// If the db value for the high score style is empty, select the 
			// first list item as the default.  The first item should always
			// "Auto".
			if (hiScoreStyle == nullptr || hiScoreStyle[0] == 0)
				ComboBox_SetText(cbHiScoreStyle, hiScoreStrings.front().c_str());

			// If the game's current media name matches the default media name,
			// fill in the media name field with "[Default]", otherwise fill it
			// in with the actual media name.
			TSTRING defMediaName = game->GetDefaultMediaName();
			TSTRING &curMediaName = game->mediaName;
			SetDlgItemText(hDlg, IDC_TXT_MEDIA_NAME,
				defMediaName == curMediaName ? _T("[Default]") : curMediaName.c_str());

			// Thread entrypoint for populating the title drop list.  This
			// can take a few seconds, since we have to scan the whole
			// reference table list for fuzzy matches to the filename to
			// look for likely title matches.
			auto ThreadMain = [](LPVOID lParam) -> DWORD
			{
				// get our 'self' object
				RefPtr<EditGameDialog> self(static_cast<EditGameDialog*>(lParam));

				// get the game file's full path name
				const TCHAR *gamePath = self->gamePath.c_str();

				// Assume we'll use the root filename as the basis for the
				// reference table list match.  Table files usually contain
				// the name of the table, although often in abbreviated form
				// (e.g., TAF for The Addams Family, FH for Funhouse) and
				// often mashed together with author, release, system version
				// info, and mod info (e.g., TAF_SomeAuthor_VP991_NightMod).
				// So it's sometimes a good basis for a fuzzy match, and 
				// sometimes there's just too much other crap in the name to
				// get pick out the right match.  But in the absence of any
				// other metadata, it will have to do.
				const TCHAR *nameToMatch = self->gameFile.c_str();
				bool isFilename = true;

				// In the case of VP, we can sometimes get better metadata
				// from the table info embedded in the VP file.  The VP file
				// format includes a metadata record where the table author
				// can store some basic bibliographic data about the table,
				// including the title, manufacturer, and year.  VP doesn't
				// populate this information automatically, of course; it's
				// just there for the table author to populate manually if
				// desired, so it's only there for tables where the author
				// bothers to enter the data (if they even know about this
				// feature in the first place).  When the author does take
				// the time to fill in the field properly, it's a much more
				// reliable way to identify the game than a fuzzy filename
				// match.
				VPFileReader vpr;
				if (tstriEndsWith(gamePath, _T(".vpt")) || tstriEndsWith(gamePath, _T(".vpx")))
				{
					// read the VP file
					if (SUCCEEDED(vpr.Read(gamePath, false)))
					{
						// We successfully loaded the file.  If there's a "Table
						// Name" field in the metadata, use that as the name to
						// match.  Not all table authors bother to fill in the
						// metadata, so this might be missing even if we were
						// able to read the file.
						if (vpr.tableName != nullptr)
						{
							nameToMatch = vpr.tableName.get();
							isFilename = false;
						}
					}
				}

				// Populate the title drop list with close matches to the filename
				// or title.  Note that the match heuristics are somewhat different
				// for the two cases, so use the appropriate one according to the
				// source of the name.
				if (isFilename)
					Application::Get()->refTableList->GetFilenameMatches(nameToMatch, 10, self->initTableMatches);
				else
					Application::Get()->refTableList->GetTitleFragmentMatches(nameToMatch, 10, self->initTableMatches);

				// Call back into the main thread. via a private message to
				// populate the drop list in the UI
				::SendMessage(self->hDlg, MsgInitThreadDone, 0, 0);

				// done with the thread
				return 0;
			};

			// Add a reference on behalf of the thread, and kick it off
			AddRef();
			DWORD tid;
			HandleHolder hThread = CreateThread(NULL, 0, ThreadMain, this, 0, &tid);

			// if the thread startup failed, we have to release its ref manually
			if (hThread == NULL)
				Release();
		}

		void OnInitThreadDone()
		{
			// note that the initial list is ready
			initTableMachesReady = true;

			// Populate the Title combo list from the matches.  Stash the
			// match item pointer in each combo item's 'data' field.
			HWND cbTitle = GetDlgItem(IDC_CB_TITLE);
			for (auto &t : initTableMatches)
			{
				int idx = ComboBox_AddString(cbTitle, t.listName.c_str());
				ComboBox_SetItemData(cbTitle, idx, reinterpret_cast<LPARAM>(&t));
			}

			// If the game has an existing title that isn't already in the 
			// list, add it at the top of the list.
			if (ComboBox_FindStringExact(cbTitle, 0, game->title.c_str()) < 0)
				ComboBox_InsertString(cbTitle, 0, game->title.c_str());
		}
	};

	// get the game selection
	if (auto game = GameList::Get()->GetNthGame(0); game != nullptr)
	{
		// create the dialog object
		RefPtr<EditGameDialog> dlg(new EditGameDialog(this, game));

		// Run it.  The counted reference we're keeping here will
		// keep the object alive as long as the modal event loop
		// is running, so the object is guaranteed to last as long
		// as the UI dialog is showing.  (It might add its own
		// references in the course of operations, so the object
		// might outlast the dialog, but our reference guarantees
		// that it lasts *at least* as long as the dialog.)
		dlg->Show(IDD_GAME_SETUP);

		// if the user saved changes, reload the wheel to reflect
		// any changes in the title
		if (dlg->saved)
			SetTimer(hWnd, fullRefreshTimerID, 0, NULL);
	}
}

bool PlayfieldView::ApplyGameChangesRenameMediaFiles(
	GameListItem *game,
	const std::list<std::pair<TSTRING, TSTRING>> &mediaRenameList, ErrorHandler &eh)
{
	// presume success
	bool ok = true;

	// If we're updating files for the current game, clear table 
	// media from all windows, to minimize the chances of a sharing
	// conflict that would prevent renaming.
	if (game == GameList::Get()->GetNthGame(0))
		Application::Get()->ClearMedia();

	// Set up a unique pointer to the caller's rename list.  Since
	// the caller owns this memory, our deleter for this pointer does
	// nothing.  The reason we need the unique_ptr at all is that we
	// switch curList to a locally allocated retry list if we find
	// retry items, so we'll need to delete that when it goes out of
	// scope.
	using ListType = std::list<std::pair<TSTRING, TSTRING>>;
	using ListPtr = std::unique_ptr<const ListType, void(*)(const ListType*)>;
	ListPtr curList(&mediaRenameList, [](auto p) {});

	// Rename files.  Do a few retries for any files with sharing
	// conflicts, in case the conflict is coming from our own video
	// or Flash player background threads: we might be able to clear
	// up any such conflicts by waiting a couple of seconds for the
	// background locks to clear.
	const int maxTries = 3;
	for (int tries = 0; ; ++tries)
	{
		// Create a new list to hold items requiring retries.  This is
		// a locally allocated object, so its deleter actually deletes it.
		auto retryList = new ListType();
		ListPtr retryListPtr(retryList, [](auto p) { delete p; });

		// run through the file list
		for (auto &f : *curList.get())
		{
			// try the rename
			if (!MoveFile(f.first.c_str(), f.second.c_str()))
			{
				// failed - get the error
				WindowsErrorMessage winErr;

				// If it's a sharing violation, and we haven't retried
				// too many times already, throw it in the retry list
				// to try again on the next round.  Otherwise just record
				// the error and move on.
				if (tries < maxTries && winErr.GetCode() == ERROR_SHARING_VIOLATION)
				{
					// sharing violation - just add it to the retry list in case
					// it's our own lock for audio/video playback
					retryList->push_back(f);
				}
				else
				{
					// other error - log it 
					eh.Error(MsgFmt(IDS_ERR_MOVEFILE, f.first.c_str(), f.second.c_str(), winErr.Get()));
					ok = false;
				}
			}
		}

		// If the retry list is empty, we're done.  Note that we don't
		// check tries against maxTries here, since it's not necessary:
		// the retry list won't get populated if we're on the last try.
		// It's safer (in terms of code correctness) not to do the
		// redundant check, since that's a good way to cause off-by-one
		// errors and the like.
		if (retryList->size() == 0)
			break;

		// Pause briefly before the retry, to give whoever's holding
		// the file lock a chance to finish what they're doing.  The
		// whole point of the retry is that we might be conflicting
		// with our own threads, so we shouldn't need to wait long:
		// we've already removed all of media objects explicitly via
		// the earlier call to Application::ClearMedia(), so the only
		// self-locking should be against background threads that
		// didn't exit instantly.  And we don't want to pause long
		// enough to freeze the UI noticeably.
		Sleep(250);

		// replace the work list with the retry list
		curList.swap(retryListPtr);
	}

	// return the status
	return ok;
}

void PlayfieldView::ApplyGameChangesToDatabase(GameListItem *game)
{
	// Update the XML and stats DB entries
	auto gl = GameList::Get();
	gl->FlushToXml(game);
	gl->FlushGameIdChange(game);

	// re-sort the title list
	gl->SortTitleIndex();

	// rebuild the filter list if necessary (if we added year or
	// manufacturer filters, for example)
	gl->CheckMasterFilterList();

	// Reload high score data for the game, as we might have changed
	// something that affected the NVRAM source
	game->highScoreStatus = GameListItem::HighScoreStatus::Init;
	RequestHighScores(game, true);
}

void PlayfieldView::DelGameInfo(bool confirmed)
{
	// get the current game
	auto gl = GameList::Get();
	auto game = gl->GetNthGame(0);
	if (!IsGameValid(game))
		return;

	// check if they've confirmed the deletion
	if (confirmed)
	{
		// confirmed - actually delete the game record from the XML
		gl->DeleteXml(game);

		// do a full UI refresh
		SetTimer(hWnd, fullRefreshTimerID, 0, NULL);
	}
	else
	{
		// first pass - ask for confirmation
		std::list<MenuItemDesc> md;
		md.emplace_back(MsgFmt(IDS_CONFIRM_DEL_GAME_INFO, game->title.c_str()), -1);
		md.emplace_back(_T(""), -1);
		md.emplace_back(LoadStringT(IDS_CONFIRM_DEL_GAME_YES), ID_CONFIRM_DEL_GAME_INFO);
		md.emplace_back(LoadStringT(IDS_CONFIRM_DEL_GAME_NO), ID_MENU_RETURN, MenuSelected);
		ShowMenu(md, L"confirm delete gameinfo", SHOWMENU_DIALOG_STYLE);
	}
}

void PlayfieldView::ShowGameCategoriesMenu(GameCategory *curSelection, bool reshow)
{
	// The game categories menu is more like a dialog than a menu, in
	// that selecting a category item toggles its checkmark but doesn't
	// dismiss the menu.  The menu stays open so that multiple items
	// can be selected and deselected.  Nonetheless, we build and show
	// the menu just like any other, the only difference being that we
	// include the MenuStayOpen flag in each category item.

	// get the current game
	auto gl = GameList::Get();
	auto game = gl->GetNthGame(0);
	if (!IsGameValid(game))
		return;

	// construct a list of all categories
	std::list<const GameCategory*> allCats;
	for (auto f : gl->GetFilters())
	{
		// include all of the category filters except "uncategorized"
		if (auto c = dynamic_cast<const GameCategory*>(f);
		    c != nullptr && dynamic_cast<const NoCategory*>(f) == nullptr)
			allCats.push_back(c);
	}

	// If we don't already have a category edit list, create it as
	// a copy of the game's current category list.  If we already
	// have one, just keep working with the current list.
	if (categoryEditList == nullptr)
	{
		categoryEditList.reset(new std::list<const GameCategory*>);
		gl->GetCategoryList(game, *categoryEditList.get());
	}
	auto& gameCats = *categoryEditList.get();

	// set up the menu descriptor
	std::list<MenuItemDesc> md;

	// add a "page up" item at the top
	md.emplace_back(PageUpTitle, ID_MENU_PAGE_UP, MenuStayOpen);

	// add the category items within the page range
	for (auto cat : allCats)
	{
		// start with the 'stay open' flag
		UINT flags = MenuStayOpen;

		// if the game is currently in this category, add a checkmark
		if (findex(gameCats, cat) != gameCats.end())
			flags |= MenuChecked;

		// if this category is the current selection, keep it selected
		// in the new menu
		if (cat == curSelection)
			flags |= MenuSelected;

		// add the item
		md.emplace_back(cat->GetFilterTitle(), cat->cmd, flags);
	}

	// add the Page Down item at the end of the category section
	md.emplace_back(PageDownTitle, ID_MENU_PAGE_DOWN, MenuStayOpen);

	// add a spacer if there were any categories at all
	if (allCats.size() != 0)
		md.emplace_back(_T(""), -1);

	// add the New Category item
	md.emplace_back(LoadStringT(IDS_MENU_EDIT_CATEGORIES), ID_EDIT_CATEGORIES);

	// add the Save and Cancel items
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_MENU_SAVE_CATEGORIES), ID_SAVE_CATEGORIES);
	md.emplace_back(LoadStringT(IDS_MENU_CXL_CATEGORIES), ID_MENU_RETURN);

	// Present the menu
	if (reshow)
	{
		// we're re-showing an existing menu, probably for a change in
		// one of the category items' selection status - stay on the
		// same page and skip the menu popup animation
		ShowMenu(md, L"game categories", SHOWMENU_NO_ANIMATION, menuPage);
	}
	else
	{
		// showing a new menu - show it normally
		ShowMenu(md, L"game categories", 0);

		// signal a Menu Open effect in DOF
		QueueDOFPulse(L"PBYMenuOpen");
	}
}

void PlayfieldView::ToggleCategoryInEditList(int cmd)
{
	// find the category filter
	auto gl = GameList::Get();
	if (GameCategory *category = dynamic_cast<GameCategory*>(gl->GetFilterByCommand(cmd)); category != nullptr)
	{
		// We're toggling the category, so if the category is in the 
		// current edit list, remove it, otherwise add it.
		auto it = findex(*categoryEditList, category);
		if (it != categoryEditList.get()->end())
		{
			// it's currently in the list - remove it
			categoryEditList->erase(it);
		}
		else
		{
			// it's not in the list - add it
			categoryEditList->push_back(category);
		}

		// Rebuild the menu
		ShowGameCategoriesMenu(category, true);
	}
}

void PlayfieldView::SaveCategoryEdits()
{
	// update the category list
	auto gl = GameList::Get();
	auto game = gl->GetNthGame(0);
	if (gl != nullptr)
		gl->SetCategories(game, *categoryEditList.get());

	// If a category filter is in effect, this could filter out the
	// current game.  Reset the filter (to rebuild its current selection
	// list) and update the current game in the wheel if so.
	if (dynamic_cast<const GameCategory*>(gl->GetCurFilter()) != nullptr)
	{
		gl->RefreshFilter();
		UpdateSelection();
		UpdateAllStatusText();
	}
}

void PlayfieldView::EditCategories()
{
	class CatDialog : public DialogWithSavedPos
	{
	public:
		CatDialog() : DialogWithSavedPos(ConfigVars::CategoryDialogPos) { }

	protected:
		class NameDialog : public DialogWithSavedPos
		{
		public:
			typedef std::function<bool(const TCHAR*)> OKCallback;
			NameDialog(OKCallback onOk, const TCHAR *initName) : 
				DialogWithSavedPos(ConfigVars::CatNameDialogPos),
				onOk(onOk), initName(initName) 
			{ }

		protected:
			OKCallback onOk;
			TSTRING initName;

			virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam)
			{
				// on initialize, populate the name field
				if (message == WM_INITDIALOG)
					SetDlgItemText(hDlg, IDC_TXT_CATNAME, initName.c_str());

				// on OK, execute the change
				if (message == WM_COMMAND && LOWORD(wParam) == IDOK)
				{
					TCHAR buf[1024];
					GetDlgItemText(hDlg, IDC_TXT_CATNAME, buf, countof(buf));
					if (!onOk(buf))
						return 0;
				}

				// use the default handling 
				return __super::Proc(message, wParam, lParam);
			}
		};

		GameCategory *GetCurCategory()
		{
			// get the current selection - if there isn't a selection,
			// there's no category
			HWND lb = GetDlgItem(IDC_LB_CATEGORIES);
			int idx = ListBox_GetCurSel(lb);
			if (idx < 0)
				return nullptr;

			// get the name from the list box
			size_t len = ListBox_GetTextLen(lb, idx);
			std::unique_ptr<TCHAR> buf(new TCHAR[len+1]);
			ListBox_GetText(lb, idx, buf.get());

			// find the category by name
			return GameList::Get()->GetCategoryByName(buf.get());
		}

		virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam)
		{
			switch (message)
			{
			case WM_INITDIALOG:
				// populate the list box
				{
					HWND lb = GetDlgItem(IDC_LB_CATEGORIES);
					for (auto f : GameList::Get()->GetFilters())
					{
						if (auto cat = dynamic_cast<GameCategory*>(f); 
						    cat != nullptr && dynamic_cast<NoCategory*>(f) == nullptr)
							ListBox_AddString(lb, cat->name.c_str());
					}
				}
				break;

			case WM_COMMAND:
				switch (LOWORD(wParam))
				{
				case IDC_BTN_NEW:
					{
						NameDialog subdlg([this](const TCHAR *name)
						{
							// object if the new name is already in use
							if (GameList::Get()->CategoryExists(name))
							{
								MessageBoxWithIdleMsg(hDlg, MsgFmt(IDS_CATEGORY_ALREADY_EXISTS, name),
									LoadStringT(IDS_ERRDLG_CAPTION).c_str(), MB_ICONERROR | MB_OK);
								return false;
							}

							// create the category
							GameList::Get()->NewCategory(name);

							// add it to the list box and select it
							HWND lb = GetDlgItem(IDC_LB_CATEGORIES);
							int idx = ListBox_AddString(lb, name);
							ListBox_SetCurSel(lb, idx);
							UpdateSelectionStatus();

							// success
							return true;
						}, _T(""));
						subdlg.Show(IDD_NEW_CATEGORY);
					}
					return 0;

				case IDC_BTN_DELETE:
					if (GameCategory *cat = GetCurCategory(); cat != nullptr)
					{
						if (MessageBoxWithIdleMsg(hDlg, MsgFmt(IDS_CONFIRM_DELETE_CATEGORY, cat->name.c_str()),
							LoadStringT(IDS_MBCAPTION_DELETE_CATEGORY), MB_ICONQUESTION | MB_YESNO) == IDYES)
						{
							// remove it from the list box
							HWND lb = GetDlgItem(IDC_LB_CATEGORIES);
							int idx = ListBox_FindStringExact(lb, 0, cat->name.c_str());
							ListBox_DeleteString(lb, idx);
							UpdateSelectionStatus();

							// delete the category from the game list manager
							GameList::Get()->DeleteCategory(cat);
						}
					}
					return 0;

				case IDC_BTN_RENAME:
					if (GameCategory *cat = GetCurCategory(); cat != nullptr)
					{
						TSTRING oldName;
						NameDialog subdlg([this, cat](const TCHAR *newName)
						{
							// object if the new name is already in use
							if (GameList::Get()->CategoryExists(newName))
							{
								MessageBoxWithIdleMsg(hDlg, MsgFmt(IDS_CATEGORY_ALREADY_EXISTS, newName),
									LoadStringT(IDS_ERRDLG_CAPTION).c_str(), MB_ICONERROR | MB_OK);
								return false;
							}

							// rename it in the list, by deleting the existing item and
							// adding the new one
							HWND lb = GetDlgItem(IDC_LB_CATEGORIES);
							int idx = ListBox_FindStringExact(lb, 0, cat->name.c_str());
							ListBox_DeleteString(lb, idx);
							idx = ListBox_AddString(lb, newName);
							ListBox_SetCurSel(lb, idx);
							UpdateSelectionStatus();

							// rename the category in the game list
							GameList::Get()->RenameCategory(cat, newName);

							// success
							return true;
						}, cat->name.c_str());
						subdlg.Show(IDD_RENAME_CATEGORY);
					}
					return 0;

				case IDC_LB_CATEGORIES:
					switch (HIWORD(wParam))
					{
					case LBN_SELCANCEL:
					case LBN_SELCHANGE:
						UpdateSelectionStatus();
						break;
					}
					break;
				}
			}

			// return the default handling if we didn't do something else first
			return __super::Proc(message, wParam, lParam);
		}

		void UpdateSelectionStatus()
		{
			int sel = ListBox_GetCurSel(GetDlgItem(IDC_LB_CATEGORIES));
			EnableWindow(GetDlgItem(IDC_BTN_DELETE), sel >= 0);
			EnableWindow(GetDlgItem(IDC_BTN_RENAME), sel >= 0);
		}
	};

	CatDialog dlg;
	dlg.Show(IDD_EDIT_CATEGORIES);
}

void PlayfieldView::ShowMediaFiles(int dir)
{
	// get the current game
	auto gl = GameList::Get();
	auto game = gl->GetNthGame(0);
	if (!IsGameValid(game))
		return;

	// if it's unconfigured, there's nothing to show
	if (!game->isConfigured || game->system == nullptr)
	{
		ShowError(EIT_Error, LoadStringT(IDS_SHOWMEDIA_UNCONFIG));
		return;
	}

	// button list for the current selected item, if any
	struct ItemButton
	{
		ItemButton(int strId, ShowMediaState::Command cmd) : strId(strId), cmd(cmd) { }
		int strId;
		ShowMediaState::Command cmd;
	};
	std::vector<ItemButton> itemButtons;
	int activeItemButton = -1;

	// index in the list of the next and previous folder items relative
	// to the current selection
	int prevFolderIndex = -1, nextFolderIndex = -1, lastFolderIndex = -1;

	// draw the media file list
	int width = 972, height = 2000;
	int pass = 1;
	int itemCount = 0;
	auto Draw = [gl, game, this, width, &height,
		&itemCount, &prevFolderIndex, &nextFolderIndex, &lastFolderIndex,
		&itemButtons, &activeItemButton, &pass]
	(HDC hdc, HBITMAP)
	{
		// set up a GDI+ drawing context
		Gdiplus::Graphics g(hdc);

		// start at the first item
		int itemIndex = 0;

		// draw the background and borders
		const float margin = 16.0f;
		const int borderWidth = 2;
		Gdiplus::SolidBrush bkgbr(Gdiplus::Color(224, 0, 0, 0));
		Gdiplus::Pen pen(Gdiplus::Color(0xE0, 0xFF, 0xFF, 0xFF), (float)borderWidth);
		g.FillRectangle(&bkgbr, Gdiplus::RectF(0.0f, 0.0f, (float)width, (float)height));
		g.DrawRectangle(&pen, Gdiplus::Rect(borderWidth/2, borderWidth/2, width - borderWidth, height - borderWidth));

		// set up for text drawing
		GPDrawString gds(g, Gdiplus::RectF(margin, margin, (float)width - 2.0f*margin, (float)height - 2.0f*margin));
		FontPref &titleFont = popupSmallerFont;
		FontPref &textFont = mediaDetailFont;
		Gdiplus::SolidBrush textbr(Gdiplus::Color(255, 255, 255));
		Gdiplus::SolidBrush graybr(Gdiplus::Color(128, 128, 128));
		Gdiplus::SolidBrush hilitebr(Gdiplus::Color(0, 128, 255));

		// show the caption
		gds.DrawString(MsgFmt(IDS_SHOWMEDIA_CAPTION, game->title.c_str()), titleFont, &textbr);
		gds.DrawString(MsgFmt(IDS_SHOWMEDIA_TEMPLATE, game->mediaName.c_str()), textFont, &textbr);
		gds.VertSpace(margin / 2.0f);

		// draw a button
		auto DrawButton = [this, &g, &gds, &textFont, &textbr, &hilitebr, &itemIndex]
		(const TCHAR *name, ShowMediaState::Command command)
		{
			// check if this is the selected button
			if (command == showMedia.command)
			{
				// draw the highlight
				Gdiplus::RectF txtrc;
				g.MeasureString(name, -1, textFont, gds.curOrigin, &txtrc);
				g.FillRectangle(&hilitebr, Gdiplus::RectF(
					gds.curOrigin.X, gds.curOrigin.Y, txtrc.Width, txtrc.Height));

				// set this as the selected command
				showMedia.command = command;
			}

			// draw the text
			gds.DrawString(name, textFont, &textbr, false);
			gds.curOrigin.X += 16;
		};

		// draw a file/folder item
		std::unique_ptr<Gdiplus::Bitmap> folderIcon(GPBitmapFromPNG(IDB_FOLDER_ICON));
		std::unique_ptr<Gdiplus::Bitmap> audioIcon(GPBitmapFromPNG(IDB_AUDIO_FILE_ICON));
		std::unique_ptr<Gdiplus::Bitmap> imageIcon(GPBitmapFromPNG(IDB_IMAGE_FILE_ICON));
		std::unique_ptr<Gdiplus::Bitmap> videoIcon(GPBitmapFromPNG(IDB_VIDEO_FILE_ICON));
		auto DrawFile = [this, &g, &gds, &pass, &textFont, &textbr, &hilitebr,
			&itemIndex, &itemButtons, &activeItemButton, 
			&prevFolderIndex, &nextFolderIndex, &lastFolderIndex,
			&DrawButton]
		(int indentLevel, Gdiplus::Bitmap *icon, const MediaType *mediaType, const TCHAR *name, const TCHAR *parentPath)
		{
			// it's a folder if it doesn't have a media type
			bool isFolder = (mediaType == nullptr);

			// advance by the indent level
			gds.curOrigin.X += (float)(indentLevel * 16);

			// Figure the icon position to center relative to the text.
			// If the icon is taller than the text, add whitespace around
			// the text and draw the folder at the current y position; if
			// the text is taller than the icon, adjust the icon down
			// by half the height difference.
			int textHt = (int)textFont->GetHeight(&g);
			int iconHt = icon->GetHeight();
			int iconY = (int)gds.curOrigin.Y;
			float boxY = gds.curOrigin.Y;
			int lineHt = textHt;
			if (iconHt + 4 > textHt)
			{
				lineHt = iconHt + 4;
				iconY += 2;
				gds.VertSpace(float((lineHt - textHt)/2));
			}
			else
			{
				iconY += (textHt - iconHt)/2;
			}

			// on the first pass, if this is the selected item, build the button list
			if (pass == 1 && itemIndex == showMedia.sel)
			{
				// build the appropriate button list according to the item type
				if (isFolder)
				{
					itemButtons.emplace_back(IDS_SHOWMEDIA_OPEN, ShowMediaState::OpenFolder);
					itemButtons.emplace_back(IDS_SHOWMEDIA_CANCEL, ShowMediaState::Return);
				}
				else
				{
					itemButtons.emplace_back(IDS_SHOWMEDIA_SHOW, ShowMediaState::ShowFile);
					itemButtons.emplace_back(IDS_SHOWMEDIA_DEL, ShowMediaState::DelFile);
					itemButtons.emplace_back(IDS_SHOWMEDIA_CANCEL, ShowMediaState::Return);
				}

				// note if one of our buttons is selected
				for (size_t i = 0 ; i < itemButtons.size(); ++i)
				{
					// if this command is active, we're in one of these buttons
					if (itemButtons[i].cmd == showMedia.command)
					{
						activeItemButton = (int)i;
						break;
					}
				}
			}

			// if the item selected (but not one of its buttons), draw the
			// highlighted background for the whole item
			if (itemIndex == showMedia.sel && activeItemButton < 0)
			{
				Gdiplus::RectF txtrc;
				g.MeasureString(name, -1, textFont, gds.curOrigin, &txtrc);
				g.FillRectangle(&hilitebr, Gdiplus::RectF(
					gds.curOrigin.X, boxY, txtrc.Width + float(icon->GetWidth() + 10), (float)lineHt));
			}

			// draw the folder icon
			g.DrawImage(icon, (int)gds.curOrigin.X, iconY);
			gds.curOrigin.X += icon->GetWidth() + 10;

			// draw the folder name
			gds.DrawString(name, textFont, &textbr, false);

			// if one of our buttons is active, draw the buttons
			if (itemIndex == showMedia.sel && activeItemButton >= 0)
			{
				// add some padding before the buttons
				gds.curOrigin.X += 36;

				// draw the buttons
				for (auto &b : itemButtons)
					DrawButton(LoadStringT(b.strId), b.cmd);
			}

			// add a newline at the end of the line
			gds.DrawString(_T(" "), textFont, &textbr, true);

			// if the icon is taller than the text, add whitespace at
			// the bottom to compensate
			if (gds.curOrigin.Y < boxY + lineHt)
				gds.VertSpace(float(boxY + lineHt) - gds.curOrigin.Y);

			// if this is a folder, update the relative folder indices
			// if necessary
			if (isFolder)
			{
				// if we haven't reached the selected item yet, update
				// the "previous folder" to this item - it's not necessarily
				// the last previous folder, but it's the last one so far
				if (itemIndex < showMedia.sel)
					prevFolderIndex = itemIndex;

				// if we're past the selected item, and we haven't set the
				// "next folder" yet, set it to this item
				if (itemIndex > showMedia.sel && nextFolderIndex < 0)
					nextFolderIndex = itemIndex;

				// this is the last folder so far
				lastFolderIndex = itemIndex;
			}

			// if this is the selected item, store it in the current
			// selection
			if (showMedia.sel == itemIndex)
			{
				TCHAR fullPath[MAX_PATH];
				PathCombine(fullPath, parentPath, name);
				showMedia.file = fullPath;
			}

			// count the item 
			++itemIndex;
		};

		// show the root media folder path first
		DrawFile(0, folderIcon.get(), nullptr, gl->GetMediaPath(), _T(""));

		// show items for a media type
		auto ShowItems = [this, margin, &g, &gds, game, &DrawFile,
			&folderIcon, &imageIcon, &audioIcon, &videoIcon]
			(bool perSystem, int indent)
		{
			// scan all media types
			for (auto mediaType : GameListItem::allMediaTypes)
			{
				// show this type if it matches the perSystem criterion
				if (mediaType->perSystem == perSystem)
				{
					// get the media folder
					TCHAR mediaDir[MAX_PATH];
					mediaType->GetMediaPath(mediaDir, game->system->mediaDir.c_str());

					// show the folder
					TCHAR mediaParentDir[MAX_PATH];
					_tcscpy_s(mediaParentDir, mediaDir);
					PathRemoveFileSpec(mediaParentDir);
					DrawFile(indent, folderIcon.get(), nullptr, mediaType->subdir, mediaParentDir);

					// Get the list of the game's extant media files of this type
					std::list<TSTRING> files;
					game->GetMediaItems(files, *mediaType, GameListItem::GMI_EXISTS | GameListItem::GMI_REL_PATH);

					// show each file
					for (auto &file : files)
					{
						// start the full path at the media folder
						TCHAR subParentPath[MAX_PATH];
						_tcscpy_s(subParentPath, mediaDir);

						// break it into path elements
						int subIndent = 1;
						const TCHAR *p = file.c_str();
						for (const TCHAR *sl = _tcschr(p, '\\'); sl != nullptr; ++subIndent)
						{
							// show this element
							TSTRING subfolder(p, sl - p);
							DrawFile(indent + subIndent, folderIcon.get(), nullptr, subfolder.c_str(), subParentPath);

							// add it to the sub-parent path
							PathAppend(subParentPath, subfolder.c_str());
							
							// find the next path element
							p = sl + 1;
							sl = _tcschr(p, '\\');
						}

						// draw the file
						Gdiplus::Bitmap *icon = mediaType->format == MediaType::Audio ? audioIcon.get() :
							mediaType->format == MediaType::Image ? imageIcon.get() : videoIcon.get();
						DrawFile(indent + subIndent, icon, mediaType, p, subParentPath);
					}
				}
			}

			// add some vertical space
			gds.VertSpace(4.0f);
		};
		
		// draw the generic items first
		ShowItems(false, 1);

		// show the per-system items next
		DrawFile(1, folderIcon.get(), nullptr, game->system->mediaDir.c_str(), gl->GetMediaPath());
		ShowItems(true, 2);

		// show instructions
		gds.VertSpace(12);
		gds.DrawString(LoadStringT(IDS_SHOWMEDIA_INSTRS), textFont, &textbr);

		// show the "close" button
		gds.VertSpace(12);
		DrawButton(LoadStringT(IDS_SHOWMEDIA_CLOSE), ShowMediaState::CloseDialog);
		
		// end the button line
		gds.DrawString(_T(" "), textFont, &textbr);

		// flush the bitmap
		g.Flush();

		// count the pass
		++pass;

		// note the final height and item count
		height = (int)(gds.curOrigin.Y + margin);
		itemCount = itemIndex;
	};

	// do one drawing pass to a dummy context first, to measure the height
	// and count items
	MemoryDC memdc;
	Draw(memdc, NULL);

	// if we're changing buttons or items, update the item position
	if (activeItemButton >= 0)
	{
		// change to a new button
		if (dir > 0)
		{
			if (++activeItemButton >= (int)itemButtons.size())
				activeItemButton = 0;
		}
		else if (dir < 0)
		{
			if (--activeItemButton < 0)
				activeItemButton = (int)(itemButtons.size() - 1);
		}
		showMedia.command = itemButtons[activeItemButton].cmd;
	}
	else
	{
		// change to a new item
		switch (dir)
		{
		case -1:
			// Previous item.  Wrap from the top list item at 0 to the Close button
			// at position -1, then to the last item.
			showMedia.sel -= 1;
			if (showMedia.sel < -1)
				showMedia.sel = itemCount - 1;

			// set the new command
			showMedia.OnSelectItem();
			break;

		case 1:
			// Next item.  Wrap from the last list item to the Close button at -1,
			// then to the top list item at 0.
			showMedia.sel += 1;
			if (showMedia.sel >= itemCount)
				showMedia.sel = -1;

			// set the new command
			showMedia.OnSelectItem();
			break;

		case -2:
			// Previous folder item.  This skips to the nearest previous folder.
			showMedia.sel = prevFolderIndex >= 0 ? prevFolderIndex : lastFolderIndex;
			showMedia.OnSelectItem(); 
			break;

		case 2:
			// Next folder item.
			showMedia.sel = nextFolderIndex >= 0 ? nextFolderIndex : 0;
			showMedia.OnSelectItem();
			break;
		}
	}

	// do the actual drawing
	Application::InUiErrorHandler eh;
	popupSprite.Attach(new Sprite());
	if (!popupSprite->Load(width, height, Draw, eh, _T("Media File Info")))
	{
		popupSprite = nullptr;
		UpdateDrawingList();
		ShowQueuedError();
		return;
	}

	// adjust it to canonical sprite position
	AdjustSpritePosition(popupSprite);

	// start the animation
	StartPopupAnimation(PopupMediaList, L"media list", true);

	// put the new sprite in the drawing list
	UpdateDrawingList();

	// treat it as a Game Information event in DOF
	QueueDOFPulse(L"PBYGameInfo");
}

void PlayfieldView::ShowMediaFilesExit()
{
	switch (showMedia.command)
	{
	case ShowMediaState::SelectItem:
	case ShowMediaState::CloseDialog:
		// a file item or the close button is selected - simply
		// exit out of the dialog
		showMedia.OnCloseDialog();
		ClosePopup();
		break;

	default:
		// some other button is selected - return to the selected item
		showMedia.command = ShowMediaState::SelectItem;
		ShowMediaFiles(0);
		break;
	}
}

void PlayfieldView::ShellExec(const TCHAR *file, const TCHAR *params)
{
	// for commands that do a ShellExecute, do this in a thread, as it
	// can be rather slow
	class ShellLauncher
	{
	public:
		static void Do(HWND hwndPar, const TCHAR *file, const TCHAR *params)
		{
			// create the new object and launch the thread
			ShellLauncher *sl = new ShellLauncher(hwndPar, file, params);
			sl->hThread = CreateThread(NULL, 0, ThreadMain, sl, 0, &sl->tid);

			// if the thread launch failed, do the thread work inline instead
			if (sl->hThread == NULL)
				ThreadMain(sl);
		}

	protected:
		HandleHolder hThread;
		DWORD tid;

		ShellLauncher(HWND hwndPar, const TCHAR *file, const TCHAR *params) :
			file(file), params(params) { }

		static DWORD WINAPI ThreadMain(LPVOID lParam)
		{
			// get 'self'
			auto self = static_cast<ShellLauncher*>(lParam);

			// execute the shell command
			ShellExecute(self->hwndPar, _T("open"), self->file.c_str(),
				self->params.length() != 0 ? self->params.c_str() : NULL,
				NULL, SW_SHOW);

			// done - delete 'self' and exit the thread
			delete self;
			return 0;
		}

		HWND hwndPar;
		TSTRING file;
		TSTRING params;
	};

	ShellLauncher::Do(GetParent(hWnd), file, params);
}

void PlayfieldView::DoMediaListCommand(bool &closePopup)
{
	// presume we won't close the popup
	closePopup = false;

	// execute the current command
	switch (showMedia.command)
	{
	case ShowMediaState::SelectItem:
		// switch to the current item's "return" button
		showMedia.command = ShowMediaState::Return;
		ShowMediaFiles(0);
		break;

	case ShowMediaState::CloseDialog:
		// close the dialog
		closePopup = true;
		break;

	case ShowMediaState::Return:
		// return to the current item selection
		showMedia.command = ShowMediaState::SelectItem;
		ShowMediaFiles(0);
		break;

	case ShowMediaState::ShowFile:
		ShellExec(_T("explorer"), MsgFmt(_T("/select,%s"), showMedia.file.c_str()));
		showMedia.command = ShowMediaState::SelectItem;
		ShowMediaFiles(0);
		break;

	case ShowMediaState::DelFile:
		// hide the media list while the menu is up
		ClosePopup();

		// show the confirmation menu
		{
			std::list<MenuItemDesc> md;
			md.emplace_back(MsgFmt(IDS_SHOWMEDIA_CONFIRM_DEL, showMedia.file.c_str()), -1);
			md.emplace_back(_T(""), -1);
			md.emplace_back(LoadStringT(IDS_SHOWMEDIA_CONFIRM_DEL_YES), ID_DEL_MEDIA_FILE);
			md.emplace_back(LoadStringT(IDS_SHOWMEDIA_CONFIRM_DEL_NO), ID_SHOW_MEDIA_FILES);
			ShowMenu(md, L"confirm delete media", SHOWMENU_DIALOG_STYLE);
		}

		// return to the selection when we get back to the dialog
		showMedia.command = ShowMediaState::SelectItem;
		break;

	case ShowMediaState::OpenFolder:
		// open the folder in Windows Explorer
		ShellExec(showMedia.file.c_str());;

		// return to the item selection
		showMedia.command = ShowMediaState::SelectItem;
		ShowMediaFiles(0);
		break;
	}
}

void PlayfieldView::DelMediaFile()
{
	// clear all media for the current game, to make sure the file
	// we're trying to delete isn't in use by the video player
	Application::Get()->ClearMedia();

	// try a few times if we get a "busy" error
	for (int tries = 0; ; ++tries)
	{
		// try deleting the file
		if (DeleteFile(showMedia.file.c_str()))
		{
			// success - sync media and re-show the media menu
			SyncPlayfield(SyncDelMedia);
			UpdateSelection();
			ShowMediaFiles(0);
			break;
		}
		else
		{
			// chcck the error
			WindowsErrorMessage err;
			if (tries < 3 && (err.GetCode() == ERROR_SHARING_VIOLATION || err.GetCode() == ERROR_LOCK_VIOLATION))
			{
				// Pause briefly and retry.  The video player stops
				// asynchronously, so it might hold the file open
				// briefly after we initially clear the media.
				Sleep(250);
			}
			else
			{
				// other error, or no more retries left - show the error
				ShowSysError(LoadStringT(IDS_ERR_DEL_MEDIA_FILE),
					MsgFmt(_T("File %s: %s"), showMedia.file.c_str(), err.Get()));
			}
		}
	}
}

bool PlayfieldView::CanAddMedia(GameListItem *game)
{
	// check if the game is configured
	if (game->system == nullptr || game->manufacturer == nullptr || game->year == 0)
	{
		// show a menu explaining the problem and offering to edit
		// the game's details now
		std::list<MenuItemDesc> md;
		md.emplace_back(LoadStringT(IDS_ERR_CONFIG_BEFORE_CAPTURE), -1);
		md.emplace_back(_T(""), -1);
		md.emplace_back(LoadStringT(IDS_MENU_EDIT_GAME_INFO), ID_EDIT_GAME_INFO);
		md.emplace_back(LoadStringT(IDS_MENU_SETUP_RETURN), ID_MENU_RETURN);
		ShowMenu(md, L"capture needs gameinfo", SHOWMENU_DIALOG_STYLE);

		// tell the caller we can't add media yet
		return false;
	}

	// looks good to add media
	return true;
}

void PlayfieldView::CaptureLayoutPrompt(int cmd, bool reshow)
{
	// note the current "skip" status
	bool skip = ConfigManager::GetInstance()->GetBool(ConfigVars::CaptureSkipLayoutMessage, false);

	// if we're initially showing the menu, record the command and check
	// to see if we can skip the menu entirely
	if (!reshow)
	{
		// record the command that initiated the capture
		origCaptureCmd = cmd;

		// if the "skip this message" option is set, skip the prompt and go
		// straight to the next step
		if (skip)
		{
			PostMessage(WM_COMMAND, ID_CAPTURE_LAYOUT_OK);
			return;
		}
	}

	// show the menu
	std::list<MenuItemDesc> md;
	md.emplace_back(LoadStringT(IDS_CAPTURE_LAYOUT_MESSAGE), -1);
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_CAPTURE_LAYOUT_SKIP), ID_CAPTURE_LAYOUT_SKIP, (skip ? MenuChecked : 0) | MenuStayOpen);
	md.emplace_back(LoadStringT(IDS_CAPTURE_LAYOUT_OK), ID_CAPTURE_LAYOUT_OK, MenuSelected);
	md.emplace_back(LoadStringT(IDS_CAPTURE_LAYOUT_CANCEL), ID_MENU_RETURN);
	ShowMenu(md, L"capture layout message", SHOWMENU_DIALOG_STYLE | (reshow ? SHOWMENU_NO_ANIMATION : 0));
}

void PlayfieldView::CaptureMediaSetup()
{
	// Get the current game
	GameListItem *game = GameList::Get()->GetNthGame(0);
	if (game == nullptr)
		return;

	// The game has to be configured before we can add media items
	if (!CanAddMedia(game))
		return;

	// build the capture list for this game
	InitCaptureList(game);

	// display the menu
	DisplayCaptureMenu(false, -1, CaptureMenuMode::Single);
}

// Mapping between last capture mode string ID and external config file name.
// Note that the config file names aren't externalized in the resource file,
// because they're for persistent storage, not for presentation to the user.
// These are ID keys, not names, so they need to be permanent and independent 
// of localization.  Note that we don't identify them in external (config)
// files by the IDS_xxx value for two reasons: first, because the IDS_xxx 
// values aren't guaranteed to be permanent (as they're just resource IDs,
// which could change across program versions), and second, because the ID
// values have no mneomonic value in the config file, which is meant to be
// human-readable as well as program-readable.
static const struct {
	int mode;
	const TCHAR *name;
} lastCaptureModeMap[] = {
	{ IDS_CAPTURE_KEEP, _T("keep") },
	{ IDS_CAPTURE_SKIP, _T("skip") },
	{ IDS_CAPTURE_CAPTURE, _T("capture") },
	{ IDS_CAPTURE_SILENT, _T("silent") },
	{ IDS_CAPTURE_WITH_AUDIO, _T("with_audio") },
};

void PlayfieldView::SaveLastCaptureModes()
{
	// save the last capture modes
	auto config = ConfigManager::GetInstance();
	for (auto &c : lastCaptureModes)
	{
		// translate the mode ID to a string for storing in the config file
		const TCHAR *mode = nullptr;
		for (size_t i = 0; i < countof(lastCaptureModeMap); ++i)
		{
			if (lastCaptureModeMap[i].mode == c.second)
			{
				// got it - save it as Capture.<MediaTypeId>.LastMode = <mode>
				config->Set(MsgFmt(_T("Capture.%s.LastMode"), c.first->configId), lastCaptureModeMap[i].name);
				break;
			}
		}
	}

	// save the last batch capture replace modes
	for (auto &c : lastBatchCaptureReplace)
	{
		// save it as Capture.<MediaTypeId>.LastBatchReplace = <bool>
		config->SetBool(MsgFmt(_T("Capture.%s.LastBatchReplace"), c.first->configId), c.second);
	}
}

void PlayfieldView::RestoreLastCaptureModes()
{
	// traverse the media types
	auto config = ConfigManager::GetInstance();
	for (auto &m : GameListItem::allMediaTypes)
	{
		// look up the last capture mode
		if (auto v = config->Get(MsgFmt(_T("Capture.%s.LastMode"), m->configId)); v != nullptr)
		{
			// translate the mode name to an IDS_xxx value
			for (size_t i = 0; i < countof(lastCaptureModeMap); ++i)
			{
				if (_tcsicmp(v, lastCaptureModeMap[i].name) == 0)
				{
					lastCaptureModes[m] = lastCaptureModeMap[i].mode;
					break;
				}
			}
		}

		// look up the last batch capture replace mode
		MsgFmt bcrKey(_T("Capture.%s.LastBatchReplace"), m->configId);
		if (auto v = config->Get(bcrKey); v != nullptr)
			lastBatchCaptureReplace[m] = config->ToBool(v);
	}
}

void PlayfieldView::InitCaptureList(const GameListItem *game)
{
	// set the default capture time to the configured delay time
	captureStartupDelay = ConfigManager::GetInstance()->GetInt(ConfigVars::CaptureStartupDelay, 5);

	// Set up the capture list.  Only include the media types for visible
	// windows with background media displayed by the various game player 
	// systems:  playfield, backglass, DMD, topper.  Don't include hidden
	// windows, since the user presumably has hidden the windows they're
	// not using on this system.  Note that we don't consider instruction
	// cards capturable, because none of the current player systems have
	// their own instruction card support, so we can't expect usable
	// screen images to show up during a game launch.
	captureList.clear();
	int cmd = ID_CAPTURE_FIRST;
	auto AddItem = [this, game, &cmd](D3DView *view, const MediaType &mediaType)
	{
		// only add the item if the window is visible
		if (view != nullptr && IsWindowVisible(GetParent(view->GetHWnd())))
		{
			// determine if the media exists
			bool exists = game != nullptr && game->MediaExists(mediaType);

			// Set the initial mode as follows:
			//
			//  - If we have a disposition for the same item from the last 
			//    capture operation, carry over the same disposition
			//
			//  - Otherwise, if the item already exists, KEEP EXISTING
			//
			//  - Otherwise, use the default disposition: CAPTURE or CAPTURE
			//    WITH AUDIO, depending on the item type
			//
			int mode;
			if (auto last = lastCaptureModes.find(&mediaType); last != lastCaptureModes.end())
			{
				// this item was in the last capture - keep the same disposition
				mode = last->second;

				// fix up KEEP/SKIP to match the item's status
				if (mode == IDS_CAPTURE_SKIP && exists)
					mode = IDS_CAPTURE_KEEP;
				else if (mode == IDS_CAPTURE_KEEP && !exists)
					mode = IDS_CAPTURE_SKIP;
			}
			else if (exists)
			{
				// the item exists, and we don't have a prior dispoition - keep it
				mode = IDS_CAPTURE_KEEP;
			}
			else
			{
				// set the default disposition according to the item type
				mode = mediaType.format == MediaType::VideoWithAudio ? IDS_CAPTURE_WITH_AUDIO : IDS_CAPTURE_CAPTURE;
			}

			// copy the last batch replace mode, if available
			bool batchReplace = false;
			if (auto lastrpl = lastBatchCaptureReplace.find(&mediaType); lastrpl != lastBatchCaptureReplace.end())
				batchReplace = lastrpl->second;

			// add the item
			captureList.emplace_back(cmd++, mediaType, view, exists, mode, batchReplace);
		}
	};
	AddItem(this, GameListItem::playfieldImageType);
	AddItem(this, GameListItem::playfieldVideoType);
	AddItem(this, GameListItem::playfieldAudioType);
	AddItem(Application::Get()->GetBackglassView(), GameListItem::backglassImageType);
	AddItem(Application::Get()->GetBackglassView(), GameListItem::backglassVideoType);
	AddItem(Application::Get()->GetDMDView(), GameListItem::dmdImageType);
	AddItem(Application::Get()->GetDMDView(), GameListItem::dmdVideoType);
	AddItem(Application::Get()->GetTopperView(), GameListItem::topperImageType);
	AddItem(Application::Get()->GetTopperView(), GameListItem::topperVideoType);
}

void PlayfieldView::ShowCaptureDelayDialog(bool update)
{
	// if we're showing the dialog anew (rather than updating the existing
	// popup), copy the current delay time to the temporary value that the
	// dialog adjusts
	if (!update)
		adjustedCaptureStartupDelay = captureStartupDelay;

	// dismiss any menu if not updating
	if (!update)
		CloseMenusAndPopups();

	// set up the new dialog
	const int width = 960, height = 480;
	Application::InUiErrorHandler eh;
	popupSprite.Attach(new Sprite());
	if (popupSprite->Load(width, height, [this, width, height](HDC hdc, HBITMAP)
	{
		// set up the GDI+ context
		Gdiplus::Graphics g(hdc);

		// draw the background
		Gdiplus::SolidBrush bkgBr(Gdiplus::Color(0xd0, 0x00, 0x00, 0x00));
		g.FillRectangle(&bkgBr, 0, 0, width, height);

		// draw the border
		const int borderWidth = 2;
		Gdiplus::Pen pen(Gdiplus::Color(0xe0, 0xff, 0xff, 0xff), float(borderWidth));
		g.DrawRectangle(&pen, borderWidth / 2, borderWidth / 2, width - borderWidth, height - borderWidth);

		// margin for our content area
		const float margin = 16.0f;

		// centered string formatter
		Gdiplus::StringFormat centerFmt;
		centerFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
		centerFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

		// draw the main text
		Gdiplus::RectF rc(0.0f, 0.0f, (float)width, (float)height/2.0f);
		FontPref &font1 = popupTitleFont;
		Gdiplus::SolidBrush textBr(Gdiplus::Color(0xFF, 0xFF, 0xFF, 0xFF));
		g.DrawString(MsgFmt(IDS_CAPTURE_DELAYTIME1, adjustedCaptureStartupDelay), -1, font1, rc, &centerFmt, &textBr);

		// draw the bottom text
		rc.Y += (float)height/2.0f;
		FontPref &font2 = popupSmallerFont;
		g.DrawString(LoadStringT(IDS_CAPTURE_DELAYTIME2), -1, font2, rc, &centerFmt, &textBr);

		// done with GDI+
		g.Flush();

	}, eh, _T("Capture startup delay adjustment dialog")))
	{
		AdjustSpritePosition(popupSprite);
		StartPopupAnimation(PopupCaptureDelay, L"capture delay", true);
	}
	else
	{
		popupSprite = nullptr;
	}

	UpdateDrawingList();
}

void PlayfieldView::DisplayCaptureMenu(bool updating, int selectedCmd, CaptureMenuMode mode)
{
	// If we're not updating, and a capture mode was specified,
	// note the new mode.  We always keep the existing mode when
	// merely updating the menu with a new checkmark selection.
	if (!updating && mode != CaptureMenuMode::NA)
		captureMenuMode = mode;

	// start with an empty menu
	std::list<MenuItemDesc> md;

	// Add the appropriate prompt message at the top of the menu, depending
	// on whether we're in single-game capture mode or batch mode.
	const WCHAR *menuID = L"capture";
	if (captureMenuMode == CaptureMenuMode::Single)
	{
		// Single game mode.  Figure the estimated time.
		int timeEst = EstimateCaptureTime();
		TSTRINGEx timeEstStr = FormatCaptureTimeEstimate(timeEst);

		// generate the prompt message, incorporating the current time estimate
		md.emplace_back(MsgFmt(IDS_CAPTURE_SELECT_MEDIA, timeEstStr.c_str()), -1);
		menuID = L"capture";
	}
	else if (captureMenuMode == CaptureMenuMode::Batch1)
	{
		// Batch mode, step 1
		md.emplace_back(LoadStringT(IDS_BATCH_CAPTURE_MEDIA), -1);
		menuID = L"batch capture media";
	}
	else if (captureMenuMode == CaptureMenuMode::Batch2)
	{
		// Batch mode, step 2
		md.emplace_back(LoadStringT(IDS_BATCH_CAPTURE_DISPOSITION), -1);
		menuID = L"batch capture disposition";
	}

	// add the media items
	md.emplace_back(_T(""), -1);
	for (auto &cap: captureList)
	{
		// keep the menu open on selecting this item
		UINT flags = MenuStayOpen;

		// note if this item is initially selected when we draw the menu
		if (cap.cmd == selectedCmd)
			flags |= MenuSelected;

		// Build the menu item title
		TSTRINGEx val;
		if (captureMenuMode == CaptureMenuMode::Batch2)
		{
			// "batch 2" mode 
			// Skip deselected items in this round, as there's no point in
			// asking whether we want to keep or replace an item that we're
			// simply not going to capture at all.
			if (cap.mode == IDS_CAPTURE_SKIP)
				continue;

			// show the keep/replace disposition
			val = LoadStringT(cap.batchReplace ? IDS_BATCH_CAPTURE_REPLACE : IDS_BATCH_CAPTURE_KEEP);
		}
		else
		{
			// in other modes, show the media type selection options
			val = LoadStringT(cap.mode).c_str();
		}

		// if we're in single-capture mode, and the disposition will overwrite
		// an existing item, add an alert indicator
		const TCHAR *overwriteAlert = _T("");
		if (captureMenuMode == CaptureMenuMode::Single && cap.exists && cap.mode != IDS_CAPTURE_KEEP)
			overwriteAlert = _T(" (!)");

		// add the menu item
		md.emplace_back(
			MsgFmt(_T("%s: %s%s"), LoadStringT(cap.mediaType.nameStrId).c_str(), val.c_str(), overwriteAlert), 
			cap.cmd, flags);
	};

	// add the delay time adjust item (except in the second batch capture step)
	if (captureMenuMode != CaptureMenuMode::Batch2)
	{
		md.emplace_back(_T(""), -1);
		md.emplace_back(MsgFmt(IDS_CAPTURE_ADJUSTDELAY, captureStartupDelay),
			ID_CAPTURE_ADJUSTDELAY,
			selectedCmd == ID_CAPTURE_ADJUSTDELAY ? MenuSelected : 0);
	}

	// Add the Begin (for single capture) or Next Step (for batch capture setup).
	// On the initial menu display, make this the initially selected item, so that
	// the user can click through to the next menu immediately if the current 
	// options look right.
	UINT mdflags = updating ? 0 : MenuSelected;
	md.emplace_back(_T(""), -1);
	switch (captureMenuMode)
	{
	case CaptureMenuMode::Single:
		md.emplace_back(LoadStringT(IDS_CAPTURE_GO), ID_CAPTURE_GO, mdflags);
		break;

	case CaptureMenuMode::Batch1:
		md.emplace_back(LoadStringT(IDS_BATCH_CAPTURE_NEXT), ID_BATCH_CAPTURE_STEP3, mdflags);
		break;

	case CaptureMenuMode::Batch2:
		md.emplace_back(LoadStringT(IDS_BATCH_CAPTURE_NEXT), ID_BATCH_CAPTURE_STEP4, mdflags);
		break;

	case CaptureMenuMode::NA:
		break;
	}

	// add a Cancel command
	md.emplace_back(LoadStringT(IDS_CAPTURE_CANCEL), ID_MENU_RETURN);

	// show the menu
	DWORD flags = SHOWMENU_DIALOG_STYLE;
	if (updating)
		flags |= SHOWMENU_NO_ANIMATION;
	ShowMenu(md, menuID, flags);
}

int PlayfieldView::EstimateCaptureTime(GameListItem *game)
{
	// Start with nothing on the clock
	int timeEst = 0;

	// Add a time allowance for each media type selected.  Images don't 
	// require any fixed wait time, since they just need one video frame,
	// but include a couple of seconds per image in the time estimate to
	// account for the overhead of launching ffmpeg.
	auto config = ConfigManager::GetInstance();
	bool twoPass = config->GetInt(ConfigVars::CaptureTwoPassEncoding, false);
	const int imageTime = 2;
	const int defaultVideoTime = 30;
	for (auto &cap : captureList)
	{
		// If a game was specified, and we're in batch capture mode, check
		// to see if we're including this item according to whether or not
		// the game has the current media type already.
		if (captureMenuMode != CaptureMenuMode::Single && game != nullptr)
		{
			// If this game already has the selected media item, and we're
			// not in "batch replace" mode, we're going to keep this item,
			// so don't include it in the time estimate.
			if (!cap.batchReplace && game->MediaExists(cap.mediaType))
				continue;
		}

		// check the capture mode
		switch (cap.mode)
		{
		case IDS_CAPTURE_CAPTURE:
		case IDS_CAPTURE_SILENT:
		case IDS_CAPTURE_WITH_AUDIO:
			// If the item has a capture time config variable, use that as
			// the time estimate.  Otherwise, it must be a still image, so
			// use the fixed image overhead time.  
			//
			// Note that this doesn't take into account manual start/stop mode,
			// which add indefinite amounts of time.  There's obviously no way
			// to know in advance how long the user will let those run, so we'll
			// just take a wild guess, using the fixed time as the guess.  That
			// should at least be on the right order of magnitude.
			if (auto cfgvar = cap.mediaType.captureTimeConfigVar; cfgvar != nullptr)
			{
				// use the video time
				int videoTime = config->GetInt(cfgvar, defaultVideoTime);
				timeEst += videoTime;

				// If we're using two-pass encoding, add time for the second
				// pass.  Use a factor of 1.5 of the video running time as a
				// wild guess.  The actual time depends on the hardware, but 
				// as explained in the comments for the similar calculation 
				// in Application::GameMonitorThread::Launch, it probably
				// won't be less than the running time (since otherwise
				// the machine is fast enough for real-time encoding) and
				// it's probably not more than about 2x real time (since
				// if it were, the machine would be too underpowered to run
				// any of the common pinball software, so probably isn't
				// running PinballY).  So we'll take the middle of that
				// band (1x to 2x) as our estimate.
				if (twoPass)
					timeEst += videoTime * 3 / 2;
			}
			else
			{
				// use the image time
				timeEst += imageTime;
			}
			break;

		case IDS_CAPTURE_KEEP:
		case IDS_CAPTURE_SKIP:
			// we're not capturing this item, so it doesn't contribute 
			// to the time estimate
			break;
		}
	}

	// If the time estimate is non-zero, add a few seconds for the game
	// launch.  Don't do this if the estimate is exactly zero, as it means
	// that nothing is selected, so we can skip the entire capture process
	// for this game.
	if (timeEst != 0)
		timeEst += 5;

	// return the time estimate in seconds
	return timeEst;
}

TSTRINGEx PlayfieldView::FormatCaptureTimeEstimate(int t)
{
	// Adjust the time to a round number, since it's really a very
	// rough estimate given the number of external factors involved.
	// Format it to a printable string.
	TSTRINGEx timeEstStr;
	if (t < 55)
	{
		// under a minute - round to a 5-second increment, so that
		// it doesn't sound like one of Lt. Cmdr. Data comically
		// precise "approximations"
		timeEstStr.Format(LoadStringT(IDS_N_SECONDS), int(roundf((float)t / 5.0f) * 5.0f));
	}
	else if (t < 75)
	{
		// it's about one minute
		timeEstStr.Load(IDS_1_MINUTE);
	}
	else if (t < 3600)
	{
		// More than a minute, less than an hour.  Round to minutes, but
		// with a heavy bias towards rounding up:  anything over 15 seconds
		// rounds up to the next minute.  It's a more pleasant experience 
		// for the user if the process finishes a bit faster than we led
		// them to expect.
		timeEstStr.Format(LoadStringT(IDS_N_MINUTES), (t + 45) / 60);
	}
	else
	{
		// More than an hour.  Figure the time in hours and minutes, but
		// round to increments of 5 minutes so that we don't sound too
		// much like Mr. Data.
		int hh = t / 3600;
		int mm = (((t % 3600)/60) + 3)/5 * 5;
		if (mm == 60)
			++hh, mm = 0;

		// pick a format based on the hours and minutes
		if (hh == 1 && mm == 0) // 1:00 -> "An hour"
			timeEstStr.Load(IDS_1_HOUR);
		else if (hh == 1)       // 1:mm -> "An hour and m minutes"
			timeEstStr.Format(LoadStringT(IDS_1_HOUR_N_MINUTES), mm);
		else if (mm == 0)       // h:00 -> "h hours"
			timeEstStr.Format(LoadStringT(IDS_N_HOURS_EXACTLY), hh);
		else                    // h:mm -> "h hours and m minutes"
			timeEstStr.Format(LoadStringT(IDS_N_HOURS_N_MINUTES), hh, mm);
	}

	// return the result
	return timeEstStr;
}

void PlayfieldView::AdvanceCaptureItemState(int cmd)
{
	// find this item in the menu
	for (auto &cap : captureList)
	{
		if (cap.cmd == cmd)
		{
			if (captureMenuMode == CaptureMenuMode::Batch2)
			{
				// "Batch 2" mode - selecting keep/replace dispositions.
				// The media mode doesn't change in this step; we only change
				// the batchReplace setting.
				cap.batchReplace = !cap.batchReplace;
			}
			else
			{
				// advance to the next item according to the current state
				switch (cap.mode)
				{
				case IDS_CAPTURE_KEEP:
				case IDS_CAPTURE_SKIP:
					// Advance to "Capture" mode, except for video where we
					// can optionally capture audio, in which case we advance
					// to "capture with audio" mode
					cap.mode = cap.mediaType.format == MediaType::VideoWithAudio ?
						IDS_CAPTURE_WITH_AUDIO : IDS_CAPTURE_CAPTURE;
					break;

				case IDS_CAPTURE_CAPTURE:
				case IDS_CAPTURE_SILENT:
					// Advance to "Skip" or "Keep Existing" mode, according to
					// whether we have existing media for this item
					cap.mode = cap.exists ? IDS_CAPTURE_KEEP : IDS_CAPTURE_SKIP;
					break;

				case IDS_CAPTURE_WITH_AUDIO:
					// Advance to "Capture Silent" mode
					cap.mode = IDS_CAPTURE_SILENT;
					break;
				}
			}

			// found it - stop searching
			break;
		}
	}

	// redraw the menu in place (without any opening animation)
	DisplayCaptureMenu(true, cmd);
}

void PlayfieldView::CaptureMediaGo()
{
	// Save the media type modes for the next capture
	for (auto &c : captureList)
		lastCaptureModes.emplace(&c.mediaType, c.mode);

	// Save the mode updates to the settings
	SaveLastCaptureModes();

	// Run the game in media capture mode
	PlayGame(ID_CAPTURE_GO, Application::LaunchFlags::StdCaptureFlags);
}

void PlayfieldView::OnCaptureDone(const CaptureDoneReport *report)
{
	// on a successful capture, remove any "mark for capture" flag
	// from the game
	if (report->ok)
	{
		if (auto game = GameList::Get()->GetByInternalID(report->gameId); game != nullptr)
			GameList::Get()->MarkForCapture(game, false);
	}

	// check if we're in batch capture mode
	if (batchCaptureMode.active)
	{
		// Batch capture mode.  Don't show individual game results as popup
		// messages, as this would be too cumbersome to wade through in the UI.
		// Instead, just collect aggregate statistics as we go, so that we can
		// display a single message with the overall results at the end of the
		// process.
		batchCaptureMode.nGamesAttempted += 1;
		if (report->ok)	batchCaptureMode.nGamesOk += 1;
		batchCaptureMode.nMediaItemsAttempted += report->nMediaItemsAttempted;
		batchCaptureMode.nMediaItemsOk += report->nMediaItemsOk;
	}
	else
	{
		// Single game capture mode.  Show the results as a popup message.
		ShowError(report->ok ? EIT_Information : EIT_Error, LoadStringT(report->overallStatusMsgId), &report->statusList);
	}
}

void PlayfieldView::ShowMediaSearchMenu()
{
	// The game has to be configured before we can add media items
	auto game = GameList::Get()->GetNthGame(0);
	if (game == nullptr || !CanAddMedia(game))
		return;

	// show the media search menu
	std::list<MenuItemDesc> md;
	md.emplace_back(LoadStringT(IDS_SEARCH_SETUP_MSG), -1);
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_SEARCH_SETUP_GO), ID_MEDIA_SEARCH_GO);
	md.emplace_back(LoadStringT(IDS_SEARCH_SETUP_CANCEL), ID_MENU_RETURN);

	ShowMenu(md, L"media search", SHOWMENU_DIALOG_STYLE);
}

void PlayfieldView::LaunchMediaSearch()
{
	// get the current game
	auto game = GameList::Get()->GetNthGame(0);
	if (game == nullptr)
		return;

	// build the search string
	MsgFmt search(_T("http://www.google.com/search?q=%s+hp+media+pack+hyperpin"),
		UrlParamEncode(game->mediaName).c_str());

	// launch the search
	ShellExec(search);

	// Show a message explaining that a media pack ZIP file can be
	// dropped.  This doesn't put us into any special mode, as we 
	// can always drop a media pack file, but the message should
	// help clarify the workflow for new users.
	ShowError(EIT_Information, LoadStringT(IDS_SEARCH_SETUP_READY).c_str(), nullptr);
}


// Figure an implied game name from a filename.  Tries matching the
// name to the various conventions for naming files of the given media
// type:
//
//     .../Media Type Dir/Game Name.ext
//     .../Media Type - Game Name.ext
//     .../Game Name - Media Type.ext
//     .../Game Name Media Type.ext
//
// The game name can further have a numeric suffix if this is an 
// indexed type.
//
// Returns true if we match one of these formats, false if not.
//
// Even if we return false, we'll still check the last path component
// in the filename to see if it's formatted using the HyperPin and
// PinballX convention:  "Title (Manuf Year)".  If so, we'll return
// that part in gameName.  This allows at least a reasonable guess at
// the intended game name even for standalone files that aren't in a
// media folder tree.
//
static bool GetImpliedGameName(TSTRING &gameName, const TCHAR *fname, const MediaType *mediaType)
{
	// we obviously can't proceed if there's not a media type
	if (mediaType == nullptr)
		return false;

	// Try to weed out Mac OS resource fork files.  Some versions of
	// ZIP embed resource files with a __MACOS[X] prefix and a filename
	// starting with "._".
	static const std::basic_regex<TCHAR> macosPat(_T("__macosx?\\\\([^\\\\]+\\\\)*\\._.*"),
		std::regex_constants::icase);
	if (std::regex_match(fname, macosPat))
		return false;

	// if the file doesn't have a valid extension for the media type, 
	// don't match it
	if (!mediaType->MatchExt(fname))
		return false;

	// adjust a game name: remove the extension and any numeric suffix
	auto AdjustName = [&gameName, mediaType]()
	{
		// remove the extension
		static const std::basic_regex<TCHAR> extPat(_T("\\.[^\\\\]*$"));
		gameName = std::regex_replace(gameName, extPat, _T(""));

		// look for a numeric suffix if it's an indexed media type
		if (mediaType->indexed)
		{
			// remove any suffix
			static const std::basic_regex<TCHAR> numPat(_T("\\s+\\d+$"));
			gameName = std::regex_replace(gameName, numPat, _T(""));
		}
	};

	// for the directory patterns, try each page directory if it's
	// a paged media type (such as Flyer Images)
	std::match_results<const TCHAR *> m;
	for (int pageno = 0; ; )
	{
		// if it's paged, get the page name
		TSTRING pageDir = _T("\\\\");
		if (mediaType->pageList != nullptr)
		{
			pageDir += mediaType->pageList[pageno];
			pageDir += _T("\\\\");
		}

		// try matching .../Media Type Dir/Page Dir/Game Name.ext 
		std::basic_regex<TCHAR> dirPat(MsgFmt(_T(".*\\\\%s%s([^\\\\]*)"), mediaType->subdir, pageDir.c_str()),
			std::regex_constants::icase);
		if (std::regex_match(fname, m, dirPat))
		{
			// the game name is the last path element
			gameName = m[1].str();
			AdjustName();
			return true;
		}

		// if it's not paged, stop after the first (non-)page
		++pageno;
		if (mediaType->pageList == nullptr || mediaType->pageList[pageno] == nullptr)
			break;
	}

	// try various patterns with the type name embedded in the filename
	auto icase = std::regex_constants::icase;
	std::basic_regex<TCHAR> pat1(MsgFmt(_T("(?:.*\\\\)?%s?(?:\\s+(?!-)|\\s*-\\s*)([^\\\\]+?)"), mediaType->subdir), icase);
	std::basic_regex<TCHAR> pat2(MsgFmt(_T("(?:.*\\\\)?([^\\\\]+?)(?:\\s+(?!-)|\\s*-\\s*)%s?(\\.[^\\\\]+)"), mediaType->subdir), icase);
	if (std::regex_match(fname, m, pat1))
	{
		gameName = m[1].str();
		AdjustName();
		return true;
	}
	if (std::regex_match(fname, m, pat2))
	{
		gameName = m[1].str() + m[2].str();
		AdjustName();
		return true;
	}

	// We didn't match any of the standard patterns, so we can't
	// identify the media type from the name.  However, if the base
	// filename portion is in the conventional "Title (Manuf Year)"
	// format for HyperPin/PinballX, at least pull that out as the
	// game name.
	static const std::basic_regex<TCHAR> titlePat(_T("^.*\\\\([^\\\\]+\\s\\([^\\\\]+\\s\\d+\\))\\.[^\\\\]+$"));
	if (std::regex_match(fname, m, titlePat))
		gameName = m[1].str();

	// we didn't match any of the path naming conventions
	return false;
}

const MediaType *PlayfieldView::GetBackgroundImageType() const
{
	return &GameListItem::playfieldImageType;
}

const MediaType *PlayfieldView::GetBackgroundVideoType() const
{
	return &GameListItem::playfieldVideoType;
}

bool PlayfieldView::BuildDropAreaList(const TCHAR *filename)
{
	// set up a rect for a button in the middle of the screen
	const int btnHt = 300;
	RECT rc = { szLayout.cx*2/10, szLayout.cy/2 - btnHt/2, szLayout.cx*8/10, szLayout.cy/2 + btnHt/2 };

	// Check to see if this file is compatible with the wheel image.
	// If so, it can be dropped as a wheel image or background image.
	if (GameListItem::wheelImageType.MatchExt(filename))
	{
		// add the window background as the background image drop area
		// (NB: build the list in painting order -> background first)
		dropAreas.emplace_back(&GameListItem::playfieldImageType, true);

		// add a button for the wheel image in the center of the window
		dropAreas.emplace_back(rc, &GameListItem::wheelImageType);

		// done
		return true;
	}

	// Check to see if this can be used as a launch audio or table
	// audio file.
	if (GameListItem::launchAudioType.MatchExt(filename)
		|| GameListItem::playfieldAudioType.MatchExt(filename))
	{
		// add the main table audio as the background image drop area
		// (NB: build the list in painting order -> background first)
		dropAreas.emplace_back(&GameListItem::playfieldAudioType, true);

		// add a button for the launch audio
		dropAreas.emplace_back(rc, &GameListItem::launchAudioType);

		// done
		return true;
	}

	// it's not a special type - use the default handling
	return __super::BuildDropAreaList(filename);
}

void PlayfieldView::BeginFileDrop()
{
	// discard any previous media drop list
	dropList.clear();

	// forget any error message backlog and clear any popups
	queuedErrors.clear();
	CloseMenusAndPopups();
}

bool PlayfieldView::DropFile(const TCHAR *fname, MediaDropTarget *dropTarget, const MediaType *mediaType)
{
	// get the selected game; fail if there isn't one, as dropped
	// media are added to the selected game
	auto game = GameList::Get()->GetNthGame(0);
	if (game == nullptr)
		return false;

	// Check what kind of file it is.  Unlike in CanDropFile(), where 
	// we don't want to take the time to parse the file, we can do a 
	// more careful inspection of the file's contents here.
	if (tstriEndsWith(fname, _T(".zip"))
		|| tstriEndsWith(fname, _T(".rar"))
		|| tstriEndsWith(fname, _T(".7z")))
	{
		// parse the file contents
		SevenZipArchive arch;
		int nMatched = 0;

		// open the archive
		LogFile::Get()->Write(LogFile::MediaDropLogging, _T("Dropping archive file %s\n"), fname);
		LogFileErrorHandler eh(_T("Media drop: opening archive file: "));
		if (arch.OpenArchive(fname, eh))
		{
			// scan the contents
			arch.EnumFiles([this, fname, game, &nMatched](UINT32 idx, const WCHAR *entryName, bool isDir)
			{
				LogFile::Get()->Write(LogFile::MediaDropLogging, _T(". found %s %ws\n"), isDir ? _T("directory") : _T("file"), entryName);

				// skip directory entries
				if (isDir)
					return;

				// check each media type
				bool curMatched = false;
				for (auto &mediaType : GameListItem::allMediaTypes)
				{
					// check for a valid implied game name
					TSTRING impliedGameName;
					if (GetImpliedGameName(impliedGameName, entryName, mediaType))
					{
						// It's a match - add it as this media type
						dropList.emplace_back(fname, idx, impliedGameName.c_str(),
							game->GetDropDestFile(entryName, *mediaType).c_str(),
							mediaType, game->MediaExists(*mediaType));

						LogFile::Get()->Write(LogFile::MediaDropLogging, _T("  -> type: %s, destination: %s\n"),
							LoadStringT(mediaType->nameStrId).c_str(), dropList.back().destFile.c_str());

						// count it
						curMatched = true;
						++nMatched;

						// no need to look for other types
						break;
					}
				}

				if (!curMatched)
					LogFile::Get()->Write(LogFile::MediaDropLogging, _T("  -> no media type match; file omitted from unpack list"));
			});
		}

		// the file was accepted if we matched at least one item
		return nMatched != 0;
	}

	// Check if it matches the target media type for the drop area
	if (mediaType != nullptr && mediaType->MatchExt(fname))
	{
		// Get the implied game name.  Since the user explicitly
		// dropped this individual file, it's okay if we can't
		// determine the game name - the intention was clear.
		TSTRING impliedGameName;
		GetImpliedGameName(impliedGameName, fname, mediaType);

		// add the item
		dropList.emplace_back(fname, -1, impliedGameName.c_str(), 
			game->GetDropDestFile(fname, *mediaType).c_str(), mediaType, game->MediaExists(*mediaType));

		LogFile::Get()->Write(LogFile::MediaDropLogging, _T("Media drop: %s -> type: %s, destination: %s\n"),
			fname, LoadStringT(mediaType->nameStrId).c_str(), dropList.back().destFile.c_str());

		// matched
		return true;
	}

	// we don't know how to handle it
	LogFile::Get()->Write(LogFile::MediaDropLogging, _T("Media drop: %s: file doesn't match a known type; ignored\n"), fname);
	return false;
}

void PlayfieldView::EndFileDrop()
{
	// Try to bring the app to the foreground.  Windows normally leaves
	// the originating window of a drag/drop in front, which isn't great
	// in this case since the UI workflow takes us to the drop menu next.
	SetForegroundWindow(GetParent(hWnd));

	// If no game is selected, reject the drop
	auto game = GameList::Get()->GetNthGame(0);
	if (game == nullptr)
	{
		ShowError(EIT_Error, LoadStringT(IDS_ERR_DROP_NO_GAME));
		return;
	}

	// Remember the target game for the drop.  Processing the drop can
	// require several UI steps and/or timer delays, so we want to make
	// sure that the user doesn't sneak a control input into the UI that
	// changes the game selection in the course of the processing.
	mediaDropTargetGame = game;

	// check if the game is configured enough to have a valid media name
	if (game->system == nullptr || game->manufacturer == nullptr || game->year == 0)
	{
		// show a menu explaining the problem and offering to edit
		// the game's details now
		std::list<MenuItemDesc> md;
		md.emplace_back(LoadStringT(IDS_ERR_CONFIG_BEFORE_DROP), -1);
		md.emplace_back(_T(""), -1);
		md.emplace_back(LoadStringT(IDS_MENU_EDIT_GAME_INFO), ID_EDIT_GAME_INFO);
		md.emplace_back(LoadStringT(IDS_MENU_SETUP_RETURN), ID_MENU_RETURN);
		ShowMenu(md, L"media drop needs gameinfo", SHOWMENU_DIALOG_STYLE);
		return;
	}

	// if the drop list is empty, simply show an error saying that none
	// of the dropped files were accepted
	if (dropList.size() == 0)
	{
		ShowError(EIT_Error, LoadStringT(IDS_ERR_INVALID_DROP));
		return;
	}

	// Check for duplicate destination files.  We consider it an error
	// to drop, say, two image files onto the backglass window, as they're
	// both going to the same place and thus one would overwrite the other.
	// If the user does that, it suggests that they have a different idea
	// about what's going on than we do, which means we shouldn't do our
	// normal thing because it's probably not what the user expects.
	std::unordered_map<TSTRING, MediaDropItem&> destFileMap;
	for (auto &d : dropList)
	{
		// Get the destination filename minus its extension.  Since we
		// can only display one backglass image or one playfield video,
		// files of different types (e.g., PNG and JPG) going into the
		// same functional slot (e.g., playfield video) are effectively
		// duplicates for our purposes, even though the file system 
		// stores the files separately.
		std::basic_regex<TCHAR> extPat(_T("\\.[^.\\\\]+$"));
		TSTRING baseName = std::regex_replace(d.destFile, extPat, _T(""));

		// check if this file is already in the map
		if (auto it = destFileMap.find(baseName); it != destFileMap.end())
		{
			// It's there.  If this entry comes from the same ZIP file
			// source, allow it; if it's from a separate file, it's an
			// error.  We let this slide for entries from a common ZIP
			// file, as the problem in this case is an ill-formed Media
			// Pack - and the user probably downloaded that rather than
			// building it herself, so it would be more confusing than
			// helpful to complain about internal errors in these.  In
			// this case it's better to silently let the redundant file
			// get lost in the shuffle, as the user probably didn't
			// take account of the ZIP's contents in the first place.
			if (it->second.filename != d.filename)
			{
				// We have clashing files from different sources.  Flag
				// an error and return.
				ShowError(EIT_Error, MsgFmt(IDS_ERR_DROP_DUP_DEST,
					LoadStringT(d.mediaType->nameStrId).c_str()));
				return;
			}
		}
		else
		{
			// this one's not in the map yet, so add it keep going
			destFileMap.emplace(baseName, d);
		}
	}

	// Check the source files to see if they look like they're for the
	// wrong game.  If so, show a confirmation prompt, to warn the user
	// that the import is always for the current game.  A user dropping
	// files for a different game might reasonably think that we infer
	// the target game from the names in the file rather than from the
	// selected game.  For comparison purposes, replace each run of
	// non-word characters (\W+ in regex-speak) with a space, and 
	// convert the whole thing to lower-case.
	auto NameToKey = [](const TSTRING &name)
	{
		std::basic_regex<TCHAR> punctPat(_T("\\W+"));
		TSTRING s = std::regex_replace(name, punctPat, _T(" "));
		std::transform(s.begin(), s.end(), s.begin(), ::_totlower);
		return s;
	};
	TSTRING refName = NameToKey(game->mediaName);
	std::unordered_map<TSTRING, TSTRING> otherNamesMap;
	for (auto &d : dropList)
	{
		// only consider files where we figured out the game name encoded
		// in the file name
		if (d.impliedGameName.length() != 0)
		{
			// Check if the (stripped) name matches.  If not, add it to the
			// map if it's not there already.
			TSTRING n = NameToKey(d.impliedGameName);
			if (n != refName && otherNamesMap.find(n) == otherNamesMap.end())
				otherNamesMap.emplace(n, d.impliedGameName);
		}
	}

	// If we found any mismatches, show a confirmation prompt
	if (otherNamesMap.size() != 0)
	{
		// build a list of the names, separated by newlines
		TSTRING otherNames;
		for (auto &o : otherNamesMap)
		{
			if (otherNames.length() != 0)
				otherNames += _T(", ");
			otherNames += o.second;
		}

		// build the confirmation menu
		std::list<MenuItemDesc> md;
		md.emplace_back(MsgFmt(otherNamesMap.size() == 1 ? IDS_ERR_DROP_OTHER_GAME : IDS_ERR_DROP_OTHER_GAMES,
			otherNames.c_str(), game->mediaName.c_str()), -1);
		md.emplace_back(_T(""), -1);
		md.emplace_back(LoadStringT(IDS_MEDIA_DROP_CONFIRM), ID_MEDIA_DROP_PHASE2);
		md.emplace_back(LoadStringT(IDS_MEDIA_DROP_CANCEL), ID_MENU_RETURN);

		// show it
		ShowMenu(md, L"media drop rename", SHOWMENU_DIALOG_STYLE);
		return;
	}

	// Move on to media drop phase 2
	MediaDropPhase2();
}

void PlayfieldView::MediaDropPhase2()
{
	// Assign a menu command ID to each item in the drop list.  Only assign
	// one command per media type.
	std::unordered_map<const MediaType*, int> typeToCmd;
	int nextCmd = ID_MEDIADROP_FIRST;
	for (auto &d : dropList)
	{
		// check if this media type has been assigned a command yet
		if (auto it = typeToCmd.find(d.mediaType); it != typeToCmd.end())
		{
			// reuse the same command already assigned to the type
			d.cmd = it->second;
		}
		else
		{
			// no command for this type yet - assign a new command ID
			d.cmd = nextCmd++;
			typeToCmd.emplace(d.mediaType, d.cmd);
		}
	}

	// If they dropped an individual media file (not a media pack),
	// and it's for a media item that doesn't already exist, don't
	// bother with a confirmation prompt; just go ahead and add the
	// item.  And if the item does exist, show a simple confirmation
	// prompt rather than the more complex menu for a multi-file
	// media pack installation.
	if (dropList.size() == 1)
	{
		// check if it's an individual media item rather than a media pack
		auto &d = dropList.front();
		if (!d.IsFromMediaPack())
		{
			// check if it exists
			if (d.exists)
			{
				// The item exists, so show a simple confirmation prompt.
				std::list<MenuItemDesc> md;
				md.emplace_back(MsgFmt(IDS_MEDIA_DROP_REPLACE_PROMPT, LoadStringT(d.mediaType->nameStrId).c_str()), -1);
				md.emplace_back(_T(""), -1);
				md.emplace_back(LoadStringT(IDS_MEDIA_DROP_REPLACE_YES), ID_MEDIA_DROP_GO);
				md.emplace_back(LoadStringT(IDS_MEDIA_DROP_REPLACE_NO), ID_MENU_RETURN);
				ShowMenu(md, L"media drop confirm replace", SHOWMENU_DIALOG_STYLE);
			}
			else
			{
				// there's no media item for this type yet, so we won't
				// be replacing anything.  No need to confirm this; just
				// go ahead and add the file.
				MediaDropGo();
			}

			// in either case we're done for now
			return;
		}
	}

	// show the menu of dropped items
	DisplayDropMediaMenu(false, 0);
}

void PlayfieldView::MediaDropGo()
{
	// clear all media before we begin, so that we don't run into any
	// file conflicts with video/audio files currently being played
	Application::Get()->ClearMedia();

	// Get the selected game.  If there isn't one, or it's different
	// from the one in effect when we started processing the drop,
	// abort.
	auto game = GameList::Get()->GetNthGame(0);
	if (game == nullptr || game->system == nullptr || game != mediaDropTargetGame)
	{
		mediaDropTargetGame = nullptr;
		return;
	}

	// If there are any videos pending deletion, wait for them to be
	// cleaned up.
	if (AudioVideoPlayer::ProcessDeletionQueue())
	{
		SetTimer(hWnd, mediaDropTimerID, 50, NULL);
		return;
	}

	// compile a list of errors as we go
	CapturingErrorHandler eh;

	// work though the drop list
	int nInstalled = 0;
	for (auto &d : dropList)
	{
		// if the status is "skip" or "keep existing", skip it
		if (d.status == IDS_MEDIA_DROP_SKIP || d.status == IDS_MEDIA_DROP_KEEP)
			continue;

		// back up any existing file
		TSTRING backupName;
		if (d.exists && !d.mediaType->SaveBackup(d.destFile.c_str(), backupName, eh))
			continue;

		// make sure the destination folder exists
		const TCHAR *slash = _tcsrchr(d.destFile.c_str(), '\\');
		if (slash != nullptr)
		{
			// extract the path portion
			TSTRING path(d.destFile.c_str(), slash - d.destFile.c_str());

			// if the folder doesn't exist, try creating it
			if (!DirectoryExists(path.c_str())
				&& !CreateSubDirectory(path.c_str(), _T(""), NULL))
			{
				WindowsErrorMessage winErr;
				eh.SysError(MsgFmt(IDS_ERR_DROP_MKDIR, LoadStringT(d.mediaType->nameStrId).c_str(), path.c_str()),
					winErr.Get());
				continue;
			}
		}

		// determine the file source
		bool ok = false;
		if (d.zipIndex >= 0)
		{
			// It's an entry in a ZIP file (or other recognized archive type).  
			// Extract it to the destination file.
			// 
			// Note that we extract the file into d.destFile, ignoring the name
			// stored in the archive, since we want to make sure the installed 
			// file matches the game's media file name.  The destination name 
			// already takes into account the proper media folder, page subfolder,
			// and index number, as applicable.  We figured all of that out when
			// we first built the drop list.  We also ignore the path of the
			// archive entry at this point, since our mission is to install the
			// file in the proper media database folder for the media type.  The
			// path information from the archive isn't lost, though, since it 
			// played a role in determining which type of media this entry 
			// represents, which in turn determines the final output folder.
			SevenZipArchive arch;
			if (arch.OpenArchive(d.filename.c_str(), eh)
				&& arch.Extract(d.zipIndex, d.destFile.c_str(), eh))
			{
				// success - count the file installed
				++nInstalled;
				ok = true;

				// set the file's modify time to now, so that we know this
				// is the most recently installed file
				TouchFile(d.destFile.c_str());
			}
		}
		else
		{
			// It's a directly dropped file.  Copy the whole file.
			if (CopyFile(d.filename.c_str(), d.destFile.c_str(), true))
			{
				// success - count the file installed
				++nInstalled;
				ok = true;

				// mark the file as just modified
				TouchFile(d.destFile.c_str());
			}
			else
			{
				// error in the copy - log it
				WindowsErrorMessage winErr;
				eh.Error(MsgFmt(IDS_ERR_DROP_COPY,
					LoadStringT(d.mediaType->nameStrId).c_str(),
					d.filename.c_str(), d.destFile.c_str(), winErr.Get()));
			}
		}

		// If it failed, and we renamed the original file as a backup
		// copy, undo the rename.  Ignore any errors that occur in that
		// attempt, as we've already logged errors for this operation
		// and we don't want to overload the user with alerts.  The
		// user should be able to sort out the mess easily enough if
		// the un-re-name fails by manually inspecting the media folder.
		if (!ok && d.exists)
			MoveFile(backupName.c_str(), d.destFile.c_str());
	}

	// report the results
	if (eh.CountErrors() != 0)
		ShowError(EIT_Error, LoadStringT(IDS_ERR_DROP_FAILED), &eh);
	else if (nInstalled != 0)
		ShowErrorAutoDismiss(nInstalled == 1 ? 2500 : 5000, EIT_Information, LoadStringT(IDS_MEDIA_DROP_SUCCESS));
	else
		ShowErrorAutoDismiss(5000, EIT_Information, LoadStringT(IDS_MEDIA_DROP_ALL_SKIPPED));

	// Make sure the on-screen media are updated with the new media
	UpdateSelection();

	// forget the target game
	mediaDropTargetGame = nullptr;
}

void PlayfieldView::InvertMediaDropState(int cmd)
{
	// Search all items matching the given command.  Note that there
	// might be multiple items per command, as we assign a command to
	// each media type in the list, and there could be multiple files
	// for the same media type.
	for (auto &d : dropList)
	{
		if (d.cmd == cmd)
		{
			// invert the state
			switch (d.status)
			{
			case IDS_MEDIA_DROP_ADD:
				d.status = IDS_MEDIA_DROP_SKIP;
				break;

			case IDS_MEDIA_DROP_SKIP:
				d.status = IDS_MEDIA_DROP_ADD;
				break;

			case IDS_MEDIA_DROP_REPLACE:
				d.status = IDS_MEDIA_DROP_KEEP;
				break;

			case IDS_MEDIA_DROP_KEEP:
				d.status = IDS_MEDIA_DROP_REPLACE;
				break;
			}
		}
	}

	// redraw the menu with the state update
	DisplayDropMediaMenu(true, cmd);
}

void PlayfieldView::DisplayDropMediaMenu(bool updating, int selectedCmd) 
{
	// get the game
	auto game = GameList::Get()->GetNthGame(0);
	if (game == nullptr)
		return;

	// set up the menu header
	std::list<MenuItemDesc> md;
	md.emplace_back(MsgFmt(IDS_MEDIA_DROP_SELECT, game->title.c_str()).Get(), -1);
	md.emplace_back(_T(""), -1);

	// Before displaying the menu, count up repeats of the items.
	struct TypeRec
	{
		std::list<MediaDropItem*> items;
	};
	std::unordered_map<const MediaType*, TypeRec> typeMap;
	for (auto &d : dropList)
	{
		// find or add the type map entry
		auto it = typeMap.find(d.mediaType);
		if (it == typeMap.end())
			it = typeMap.emplace(d.mediaType, TypeRec()).first;

		// add this item to the list
		it->second.items.emplace_back(&d);
	}

	// rebuild the map as a vector, sorted by the "menu order" for the types
	std::vector<TypeRec*> typeVec;
	typeVec.reserve(dropList.size());
	for (auto &it : typeMap)
		typeVec.emplace_back(&it.second);

	std::sort(typeVec.begin(), typeVec.end(), [](const TypeRec *a, const TypeRec *b) {
		return a->items.front()->mediaType->menuOrder < b->items.front()->mediaType->menuOrder; });

	// Add the drop items to the menu
	for (auto &it : typeVec)
	{
		// get the first item in the list
		auto &items = it->items;
		auto d = items.front();

		// note if it's the current item
		UINT itemFlags = MenuStayOpen;
		if (d->cmd == selectedCmd)
			itemFlags |= MenuSelected;

		// if there's more than one item of this type to be added,
		// note the number of items in parens
		TSTRINGEx num;
		if (items.size() > 1)
			num.Format(_T(" (%d)"), items.size());

		// Add the item.  If there's more than one item of this type
		// to be added, note the number of items.
		md.emplace_back(MsgFmt(_T("%s%s: %s"),
			LoadStringT(d->mediaType->nameStrId).c_str(), num.c_str(),
			LoadStringT(d->status).c_str()), d->cmd, itemFlags);
	}

	// add the Go and Cancel items
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_MEDIA_DROP_GO), ID_MEDIA_DROP_GO);
	md.emplace_back(LoadStringT(IDS_MEDIA_DROP_CANCEL), ID_MENU_RETURN);

	// figure the ShowMenu flags
	DWORD flags = SHOWMENU_DIALOG_STYLE;
	if (updating)
		flags |= SHOWMENU_NO_ANIMATION;

	// show the menu
	ShowMenu(md, L"media drop confirm", flags);
}

// Batch Capture step 1.  Show the initial menu, which introduces the process
// and offers selections for All Games, Marked For Capture, and the current
// game list filter (e.g., 70s Tables, Williams Tables, etc).
void PlayfieldView::BatchCaptureStep1()
{
	// build the menu
	std::list<MenuItemDesc> md;
	md.emplace_back(LoadStringT(IDS_BATCH_CAPTURE_PROMPT), -1);
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_BATCH_CAPTURE_ALL), ID_BATCH_CAPTURE_ALL);

	// if a filter other than "all games" or "unconfigured" is selected, add it
	auto gl = GameList::Get();
	auto f = gl->GetCurFilter();
	if (f != gl->GetAllGamesFilter() && f != gl->GetUnconfiguredGamesFilter())
		md.emplace_back(f->GetFilterTitle(), ID_BATCH_CAPTURE_FILTER);

	// add the rest of the menu
	md.emplace_back(LoadStringT(IDS_BATCH_CAPTURE_MARKED), ID_BATCH_CAPTURE_MARKED);
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_BATCH_CAPTURE_CANCEL), ID_MENU_RETURN);

	// show the menu
	ShowMenu(md, L"batch capture games", SHOWMENU_DIALOG_STYLE);
}

// Batch Capture step 2.  Show the media type selection menu, which lets the
// user select which items to capture during this batch process.
void PlayfieldView::BatchCaptureStep2(int cmd)
{
	// store the capture command for later use
	batchCaptureCmd = cmd;

	// Check to see if any games are selected by the filter.  If
	// not, show an error and abort.
	int nGames = 0;
	EnumBatchCaptureGames([&nGames](GameListItem *) { ++nGames; });
	if (nGames == 0)
	{
		int msg = cmd == ID_BATCH_CAPTURE_MARKED ? IDS_ERR_BATCH_CAPTURE_NO_MARKED : IDS_ERR_BATCH_CAPTURE_NO_GAMES;
		ShowError(EIT_Error, LoadStringT(msg));
		return;
	}

	// initialize the media capture list generically (with no game specified)
	InitCaptureList(nullptr);

	// show the media selection menu
	DisplayCaptureMenu(false, -1, CaptureMenuMode::Batch1);
}

// Batch Capture step 3: Show the type selection menu again, to get
// the existing file dispostion per each media type.
void PlayfieldView::BatchCaptureStep3()
{
	// Check to see if any items are selected for capture.  If not, show an
	// error and exit the capture process.
	int nMediaTypes = 0;
	for (auto &c : captureList)
	{
		if (c.mode != IDS_CAPTURE_SKIP)
			++nMediaTypes;
	}
	if (nMediaTypes == 0)
	{
		ShowError(EIT_Error, LoadStringT(IDS_ERR_BATCH_CAPTURE_NO_SEL));
		return;
	}

	// show the media selection menu again, this time in the second
	// phase style to ask for existing item disposition
	DisplayCaptureMenu(false, -1, CaptureMenuMode::Batch2);
}

// Get the list of games selected by the batch capture command
void PlayfieldView::EnumBatchCaptureGames(std::function<void(GameListItem*)> func)
{
	// set up a private filter for "marked for batch capture" games
	class MarkedFilter : public GameListFilter
	{
	public:
		MarkedFilter() : GameListFilter(_T("[Top]"), _T("Marked")) { }
		virtual TSTRING GetFilterId() const override { return _T("MarkedForCapture"); }
		virtual const TCHAR *GetFilterTitle() const override { return _T("Marked For Capture"); }
		virtual bool Include(GameListItem *game) override
			{ return GameList::Get()->IsMarkedForCapture(game); }
	};
	MarkedFilter markedFilter;

	// get the filter selected by the batch capture command
	auto gl = GameList::Get();
	GameListFilter *filter =
		batchCaptureCmd == ID_BATCH_CAPTURE_FILTER ? gl->GetCurFilter() :
		batchCaptureCmd == ID_BATCH_CAPTURE_MARKED ? &markedFilter :
		gl->GetAllGamesFilter();

	// enumerate games selected by the filter
	gl->EnumGames([&func](GameListItem *game)
	{
		// we can only capture media for games configured with bibliographic info
		// and valid systems
		if (game->gameXmlNode != nullptr && game->system != nullptr)
			func(game);
	}, filter);
}

void PlayfieldView::BatchCaptureStep4()
{
	// count games and estimate the running time
	int nGames = 0;
	int totalTime = 0;
	EnumBatchCaptureGames([&nGames, &totalTime, this](GameListItem *game)
	{
		// get the time estimate for this game
		int t = EstimateCaptureTime(game);

		// only count the game if the time is non-zero, as a zero time
		// means that no media were selected for the game
		if (t != 0)
		{
			// count the game
			++nGames;

			// count the total time
			totalTime += t;

			// add the startup wait time
			totalTime += ConfigManager::GetInstance()->GetInt(ConfigVars::CaptureStartupDelay, 5);
		}
	});

	// format the time estimate
	auto totalTimeStr = FormatCaptureTimeEstimate(totalTime);
		
	// build the menu
	std::list<MenuItemDesc> md;
	md.emplace_back(MsgFmt(IDS_BATCH_CAPTURE_READY, nGames, totalTimeStr.c_str()), -1);
	md.emplace_back(_T(""), -1);
	md.emplace_back(LoadStringT(IDS_BATCH_CAPTURE_VIEW), ID_BATCH_CAPTURE_VIEW);
	md.emplace_back(LoadStringT(IDS_BATCH_CAPTURE_GO), ID_BATCH_CAPTURE_GO);
	md.emplace_back(LoadStringT(IDS_BATCH_CAPTURE_CANCEL), ID_MENU_RETURN);
	ShowMenu(md, L"batch capture ready", SHOWMENU_DIALOG_STYLE);
}

void PlayfieldView::BatchCaptureView()
{
	// remove the menu
	CloseMenusAndPopups();

	// do a drawing pass
	int width = 1000, height = 480, contentHeight = 0;
	auto Draw = [this, &width, &height, &contentHeight](HDC hdc, HBITMAP, const void*, const BITMAPINFO&)
	{
		// set up a Gdiplus context for drawing
		Gdiplus::Graphics g(hdc);

		// margins around all edges
		float margin = 24.0f;

		// fill the background
		Gdiplus::SolidBrush bkgBr(Gdiplus::Color(192, 0, 0, 0));
		g.FillRectangle(&bkgBr, 0, 0, width, height);

		// set up resources for text drawing
		std::unique_ptr<Gdiplus::Font> gameTitleFont(CreateGPFont(popupFont.family.c_str(), 16, 400));
		std::unique_ptr<Gdiplus::Font> detailsFont(CreateGPFont(popupFont.family.c_str(), 12, 400));
		std::unique_ptr<Gdiplus::Font> mediaItemFont(CreateGPFont(popupFont.family.c_str(), 14, 400));
		Gdiplus::SolidBrush gameTitleBr(Gdiplus::Color(255, 255, 255));
		Gdiplus::SolidBrush detailsBr(Gdiplus::Color(128, 128, 128));
		Gdiplus::SolidBrush mediaItemBr(Gdiplus::Color(220, 220, 220));
		Gdiplus::SolidBrush replaceBr(Gdiplus::Color(255, 0, 0));
		Gdiplus::SolidBrush skipBr(Gdiplus::Color(96, 96, 96));
		Gdiplus::SolidBrush capNewBr(Gdiplus::Color(255, 255, 255));

		// figure the maximum width of the media items, for setting a "tab stop"
		float cxCol0 = 0;
		for (auto &c : captureList)
		{
			if (c.mode != IDS_CAPTURE_SKIP)
			{
				Gdiplus::RectF bbox;
				g.MeasureString(LoadStringT(c.mediaType.nameStrId), -1, mediaItemFont.get(), Gdiplus::PointF(0.0f, 0.0f), &bbox);
				if (bbox.Width > cxCol0)
					cxCol0 = bbox.Width;
			}
		}

		// account for the margins, and add some spacing
		cxCol0 += margin + 64.0f;

		// Set up the text drawing area.  Leave some extra space at the top for
		// a title bar in the final display window.
		GPDrawString s(g, Gdiplus::RectF(margin, margin + 64.0f, (float)width - 2.0f*margin, (float)height - 2.0f*margin - 64.0f));

		// build the list of games and media items to capture
		EnumBatchCaptureGames([this, &g, &s, &gameTitleFont, &mediaItemFont, &detailsFont,
			&gameTitleBr, &detailsBr, &mediaItemBr, &replaceBr, &capNewBr, &skipBr, cxCol0]
		(GameListItem *game)
		{
			// start with the game name
			s.DrawString(game->GetDisplayName().c_str(), gameTitleFont.get(), &gameTitleBr);

			// add some details
			TSTRINGEx details = game->system->displayName;
			if (game->filename.length() != 0 && game->tableFileSet != nullptr)
			{
				// get the full path
				TCHAR buf[MAX_PATH];
				PathCombine(buf, game->tableFileSet->tablePath.c_str(), game->filename.c_str());

				// if it doesn't exist, try adding the default extension
				if (!FileExists(buf) && game->system->defExt.length() != 0)
					_tcscat_s(buf, game->system->defExt.c_str());

				// add the path
				details += _T(", ");
				details += buf;
			}
			s.DrawString(details, detailsFont.get(), &detailsBr);
			s.curOrigin.Y += 8.0f;

			// add each media item
			for (auto &c : captureList)
			{
				// skip items marked 'skip'
				if (c.mode == IDS_CAPTURE_SKIP)
					continue;

				// show the status
				bool exists = game->MediaExists(c.mediaType);
				bool replacing = exists && c.batchReplace;
				bool capturing = !exists || replacing;
				Gdiplus::PointF pt(cxCol0, s.curOrigin.Y);
				if (replacing)
					g.DrawString(LoadStringT(IDS_CAPPREVIEW_REPLACE), -1, mediaItemFont.get(), pt, &replaceBr);
				else if (exists)
					g.DrawString(LoadStringT(IDS_CAPPREVIEW_KEEP), -1, mediaItemFont.get(), pt, &skipBr);
				else
					g.DrawString(LoadStringT(IDS_CAPPREVIEW_NEW), -1, mediaItemFont.get(), pt, &capNewBr);

				// show the name
				s.curOrigin.X += 32;
				s.DrawString(LoadStringT(c.mediaType.nameStrId), mediaItemFont.get(), capturing ? &mediaItemBr : &skipBr);
			}

			// add a little vertical whitespace between items
			s.curOrigin.Y += 16;
		});

		// note the final height, adding in some space at the bottom for the
		// instructions area if we're scrolling
		contentHeight = (int)(s.curOrigin.Y + margin + 64.0f);

		// commit the drawing to the bitmap
		g.Flush();
	};

	// do a first drawing pass to determine the needed content height
	DrawOffScreen(width, height, Draw);

	// if the content height exceeded the initial minimum height,
	// use the content height instead
	if (contentHeight > height)
		height = contentHeight;

	// do a second drawing pass now that we know the height, keeping
	// the resulting bitmap this time
	DrawOffScreen(batchViewBitmap.dib, width, height, Draw);

	// save it as a Gdiplus bitmap
	batchViewBitmap.gpbmp.reset(Gdiplus::Bitmap::FromBITMAPINFO(&batchViewBitmap.dib.bmi, batchViewBitmap.dib.dibits));

	// show the bitmap, scrolled to the top
	batchViewScrollY = 0;
	UpdateBatchCaptureView();
}

void PlayfieldView::UpdateBatchCaptureView()
{
	// create the popup
	popupSprite.Attach(new Sprite());

	// figure the popup height:  if the bitmap fits into our maximum height,
	// use the bitmap height, otherwise use the maximum height and scroll it
	const int maxHeight = 1500;
	int width = batchViewBitmap.dib.bmi.bmiHeader.biWidth;
	int srcHeight = abs(batchViewBitmap.dib.bmi.bmiHeader.biHeight);
	int height = min(maxHeight, srcHeight);

	// keep the scroll offset in range
	if (batchViewScrollY < 0)
		batchViewScrollY = 0;
	else if (batchViewScrollY > srcHeight - height)
		batchViewScrollY = srcHeight - height;

	// draw the sprite
	bool ok = popupSprite->Load(width, height, [this, width, height, maxHeight, srcHeight](Gdiplus::Graphics &g)
	{
		// figure the scrolling region
		float y = srcHeight <= maxHeight ? 0.0f : (float)batchViewScrollY;
		float drawHeight = (float)height;
		if (y + drawHeight > srcHeight)
			drawHeight = (float)srcHeight - y;

		// Draw the image.  If it fits, ignore the scroll offset.
		g.DrawImage(batchViewBitmap.gpbmp.get(),
			Gdiplus::RectF(0.0f, 0.0f, (float)width, drawHeight),
			0.0f, y, (float)width, drawHeight,
			Gdiplus::UnitPixel);

		// draw the outline
		float penWid = 4.0f;
		Gdiplus::Color frameColor(192, 192, 192);
		Gdiplus::Pen pen(frameColor, penWid);
		g.DrawRectangle(&pen, penWid/2, penWid/2, (float)width - penWid, (float)height - penWid);

		// centered string formatter
		Gdiplus::StringFormat centerFmt;
		centerFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
		centerFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

		// draw a title bar at the top
		auto title = LoadStringT(IDS_CAPPREVIEW_TITLE);
		std::unique_ptr<Gdiplus::Font> titleFont(CreateGPFont(popupFont.family.c_str(), 20, 700));
		Gdiplus::SolidBrush titleBr(Gdiplus::Color(0, 0, 0));
		Gdiplus::SolidBrush titleBkg(frameColor);
		Gdiplus::RectF bbox;
		g.MeasureString(title, -1, titleFont.get(), Gdiplus::PointF(0.0f, 0.0f), &centerFmt, &bbox);
		Gdiplus::RectF rcTitle(0.0f, 0.0f, (float)width, bbox.Height*1.4f);
		g.FillRectangle(&titleBkg, rcTitle);
		g.DrawString(title, -1, titleFont.get(), rcTitle, &centerFmt, &titleBr);

		// if we're scrollable, show scrolling instructions
		if (srcHeight > maxHeight)
		{
			auto instr = LoadStringT(IDS_CAPPREVIEW_INSTRS);
			std::unique_ptr<Gdiplus::Font> instrFont(CreateGPFont(popupFont.family.c_str(), 16, 400));
			g.MeasureString(instr, -1, instrFont.get(), Gdiplus::PointF(0.0f, 0.0f), &centerFmt, &bbox);
			Gdiplus::RectF rcInstr(0.0f, (float)height - bbox.Height*1.4f, (float)width, bbox.Height*1.4f);
			g.FillRectangle(&titleBkg, rcInstr);
			g.DrawString(instr, -1, instrFont.get(), rcInstr, &centerFmt, &titleBr);
		}

	}, Application::InUiErrorHandler(), _T("Batch capture preview"));

	// if that failed, abort
	if (!ok)
	{
		popupSprite = nullptr;
		UpdateDrawingList();
		return;
	}
		
	// finalize the position
	AdjustSpritePosition(popupSprite);

	// animate the popup opening if it wasn't already displayed
	StartPopupAnimation(PopupBatchCapturePreview, L"batch capture preview", true);

	// update the drawing list
	UpdateDrawingList();
}

void PlayfieldView::BatchCaptureGo()
{
	// save the capture modes and keep/replace settings for next time
	for (auto &c : captureList)
	{
		lastCaptureModes.emplace(&c.mediaType, c.mode);
		lastBatchCaptureReplace.emplace(&c.mediaType, c.batchReplace);
	}

	// update the saved capture modes in the settings
	SaveLastCaptureModes();

	// Add up the total time for the whole batch
	int totalTime = 0;
	int nGames = 0;
	EnumBatchCaptureGames([this, &totalTime, &nGames](GameListItem *game) 
	{
		if (auto t = EstimateCaptureTime(game); t != 0)
		{
			totalTime += t + captureStartupDelay;
			++nGames;
		}
	});

	// if the total time is zero, nothing is selected for capture - fail now
	if (totalTime == 0)
	{
		ShowError(EIT_Information, LoadStringT(IDS_ERR_BATCH_CAPTURE_NO_WORK));
		return;
	}

	// Get rid of any prior launch queue.  The launch queue should
	// always be kept clear between batch operations anyway, so this 
	// really shouldn't be necessary, but out of an abundance of
	// caution...
	Application::Get()->ClearLaunchQueue();

	// Queue capture for all games in the capture list
	int nCurGame = 1;
	int remainingTime = totalTime;
	EnumBatchCaptureGames([this, &totalTime, &remainingTime, &nGames, &nCurGame](GameListItem *game)
	{
		// build the capture list for the game
		std::list<Application::LaunchCaptureItem> capList;
		for (auto &c : captureList)
		{
			// skip disabled items
			if (c.mode == IDS_CAPTURE_SKIP)
				continue;

			// skip existing items not marked for batch replacement
			if (!c.batchReplace && game->MediaExists(c.mediaType))
				continue;

			// add this item to the app capture list
			capList.emplace_back(c.win, c.mediaType, c.mode == IDS_CAPTURE_WITH_AUDIO);
		}

		// if we found any capture items for the game, queue the launch
		if (capList.size() != 0)
		{
			// enqueue this launch
			Application::BatchCaptureInfo bci(nCurGame, nGames, remainingTime, totalTime);
			Application::Get()->QueueLaunch(ID_CAPTURE_GO, Application::LaunchFlags::StdCaptureFlags,
				game, game->system, &capList, captureStartupDelay, &bci);

			// deduct this from the remaining time
			remainingTime -= EstimateCaptureTime(game) + captureStartupDelay;
			++nCurGame;
		}
	});

	// set the play command to 'capture'
	lastPlayGameCmd = ID_CAPTURE_GO;
	lastPlayGameLaunchFlags = Application::LaunchFlags::StdCaptureFlags;

	// enter batch capture mode
	EnterBatchCapture();

	// launch the first queued game
	BatchCaptureNextGame();
}

void PlayfieldView::BatchCaptureNextGame()
{
	// if we haven't cancelled the whole operation, and the application
	// still has more queued games, launch the next one
	if (!batchCaptureMode.cancel && Application::Get()->IsGameQueuedForLaunch())
		LaunchQueuedGame();
	else
		ExitBatchCapture();
}

void PlayfieldView::EnterBatchCapture()
{
	// note that we're in batch capture mode
	batchCaptureMode.Enter();

	// temporarily enable capture logging
	auto lf = LogFile::Get();
	lf->EnableTempFeature(LogFile::CaptureLogging);

	// log the start of the batch capture
	lf->Group();
	lf->WriteTimestamp(_T("Batch capture started\n"));
	lf->Write(_T("Batch capture plan:\n"));
	EnumBatchCaptureGames([lf, this](GameListItem *game)
	{
		// log it
		lf->Write(_T("  %s (%s%s%s)\n"),
			game->mediaName.c_str(),
			game->system->displayName.c_str(),
			game->filename.length() != 0 ? _T("; ") : _T(""), game->filename.c_str());

		// scan the media items
		int nItemsInGame = 0;
		for (auto &c : captureList)
		{
			if (c.mode != IDS_CAPTURE_SKIP)
			{
				// log it
				auto exists = game->MediaExists(c.mediaType);
				auto capture = c.batchReplace || !exists;
				lf->Write(_T("    %s: %s, %s\n"),
					LoadStringT(c.mediaType.nameStrId).c_str(),
					exists ? _T("Exists") : _T("Missing"),
					capture ? _T("Capturing") : _T("Skipping"));

				// count it, if we're going to capture it
				if (capture)
				{
					batchCaptureMode.nMediaItemsPlanned += 1;
					nItemsInGame += 1;
				}
			}
		}

		// if we found any media items to capture for this game, count the game
		if (nItemsInGame != 0)
			batchCaptureMode.nGamesPlanned += 1;
	});
}

void PlayfieldView::ExitBatchCapture()
{
	// make sure we're actually in batch capture mode
	if (!batchCaptureMode.active)
		return;

	// Clear any queued batch capture items.  If we cancelled
	// in the middle of a batch, there could still be items queued.
	// They'd get cleaned up in due course anyway, but there's no
	// reason to leave them sitting there taking up memory, and it
	// would probably be confusing in some situation (at the very
	// least, for a human inspecting things in the debugger, if not
	// to some other part of the program) to leave them hanging 
	// around.
	Application::Get()->ClearLaunchQueue();

	// determine if we attempted all of the games in the capture list
	bool ok = (batchCaptureMode.nMediaItemsOk == batchCaptureMode.nMediaItemsPlanned
		&& batchCaptureMode.nGamesOk == batchCaptureMode.nGamesPlanned);

	// show an overall success/failure message
	if (ok)
		ShowError(EIT_Information, MsgFmt(IDS_ERR_BATCH_CAPTURE_DONE_OK,
			batchCaptureMode.nMediaItemsOk, batchCaptureMode.nGamesOk));
	else
		ShowError(EIT_Error, MsgFmt(IDS_ERR_BATCH_CAPTURE_DONE_ERR,
			batchCaptureMode.nGamesPlanned, batchCaptureMode.nGamesAttempted, batchCaptureMode.nGamesOk,
			batchCaptureMode.nMediaItemsPlanned, batchCaptureMode.nMediaItemsAttempted, batchCaptureMode.nMediaItemsOk));

	// log the end of the batch operation
	auto lf = LogFile::Get();
	lf->Group();
	lf->WriteTimestamp(_T("Batch capture %s\n"),
		ok ? _T("successfully completed") : _T("terminated with errors or interruptions; see individual item results above"));
	lf->Write(_T("  Games attempted: %d\n"), batchCaptureMode.nGamesAttempted);
	lf->Write(_T("  Games succeeded: %d\n"), batchCaptureMode.nGamesOk);
	lf->Write(_T("  Media items attempted: %d\n"), batchCaptureMode.nMediaItemsAttempted);
	lf->Write(_T("  Media items succeeded: %d\n"), batchCaptureMode.nMediaItemsOk);
	lf->Group();

	// turn off the temporary capture logging override
	LogFile::Get()->WithdrawTempFeature(LogFile::CaptureLogging);

	// no longer in batch capture mode
	batchCaptureMode.Exit();
}

void PlayfieldView::ToggleHideGame()
{
	// get the current game
	if (auto game = GameList::Get()->GetNthGame(0); game != nullptr)
	{
		// toggle its hidden status
		game->SetHidden(!game->IsHidden());

		// Whichever direction we're going, this will always remove the
		// game from the current view, because we can only be operating
		// on a game if it's in view to start with, meaning that the
		// current filter selects the game with its OLD "hidden" status,
		// meaning that reversing the status will necessarily make the
		// same filter exclude the game, since every filter either
		// includes or excludes hidden games.  So we have to rebuild
		// the filter list.
		GameList::Get()->RefreshFilter();
		UpdateSelection();
		UpdateAllStatusText();
	}
}

// Toggle on-screen frame counter/performance stats command handler
void PlayfieldView::CmdFrameCounter(const QueuedKey &key)
{
	// the frame counter is per-window - forward it to the source window
	if (key.mode == KeyDown && IsWindow(key.hWndSrc))
		::SendMessage(key.hWndSrc, WM_COMMAND, ID_FPS, 0);
}

// Toggle full-screen mode command handler
void PlayfieldView::CmdFullScreen(const QueuedKey &key)
{
	// Toggle full-screen mode in the active window.  Key messages
	// originate from view windows, so send this to the source view's
	// parent window.
	if (key.mode == KeyDown && IsWindow(key.hWndSrc) && IsWindow(GetParent(key.hWndSrc)))
		::SendMessage(GetParent(key.hWndSrc), WM_COMMAND, ID_FULL_SCREEN, 0);
}

// Rotate the monitor 90 degrees clockwise
void PlayfieldView::CmdRotateMonitorCW(const QueuedKey &key)
{
	// set the rotation in the originating window
	if (key.mode == KeyDown && IsWindow(key.hWndSrc))
		::SendMessage(key.hWndSrc, WM_COMMAND, ID_ROTATE_CW, 0);
}

// Rotate the monitor 90 degrees counter-clockwise
void PlayfieldView::CmdRotateMonitorCCW(const QueuedKey &key)
{
	// set the rotation in the originating window
	if (key.mode == KeyDown && IsWindow(key.hWndSrc))
		::SendMessage(key.hWndSrc, WM_COMMAND, ID_ROTATE_CCW, 0);
}

// Settings command handler - show the options dialog.
void PlayfieldView::CmdSettings(const QueuedKey &key)
{
	if (key.mode == KeyDown)
		ShowSettingsDialog();
}

void PlayfieldView::ShowSettingsDialog()
{
	// don't allow this when a game is running
	if (runningGamePopup != nullptr)
	{
		ShowError(EIT_Information, LoadStringT(IDS_ERR_NOT_WHILE_RUNNING));
		return;
	}

	// if we haven't already loaded the options dialog DLL function, do so now
	static HMODULE dll = NULL;
	static decltype(ShowOptionsDialog)* showOptionsDialog = NULL;
	const TCHAR *progress = _T("");
	if (showOptionsDialog == NULL)
	{
		// load the DLL
		if (dll == NULL)
		{
			progress = _T("loading OptionsDialog.dll");
			dll = LoadLibrary(_T("OptionsDialog.dll"));
		}

		// Verify the DLL interface version.  This makes sure that the 
		// install folder has a matching copy of the DLL (not an old copy
		// from a previous program version, for example, which could cause
		// mysterious crashes due to mismatched function parameters or the
		// like).
		if (dll != NULL)
		{
			auto getVer = reinterpret_cast<decltype(GetOptionsDialogVersion)*>(GetProcAddress(dll, "GetOptionsDialogVersion"));
			if (getVer == NULL || getVer() != PINBALLY_OPTIONS_DIALOG_IFC_VSN)
			{
				Application::InUiErrorHandler eh;
				eh.Error(LoadStringT(IDS_ERR_OPTS_DIALOG_DLL_VER));
				return;
			}
		}

		// retrieve the ShowOptionsDialog() entrypoint
		if (dll != NULL)
		{
			progress = _T("binding to OptionsDialog.dll!ShowOptionsDialog()");
			showOptionsDialog = reinterpret_cast<decltype(showOptionsDialog)>(GetProcAddress(dll, "ShowOptionsDialog"));
		}
	}

	// if we loaded it successfully, show the dialog
	if (showOptionsDialog != NULL)
	{
		// remove any menus and popups
		CloseMenusAndPopups();

		// Before loading the dialog, flush our in-memory config to disk, so
		// that the dialog sees our latest updates.  Note that we don't actually
		// pass the in-memory config object to the dialog, since it's in a 
		// separate DLL.  That doesn't *necessarily* prevent us from passing
		// the object across the interface, but it's safer not to, just in
		// case the user has a mismatched version of the dialog DLL installed.
		// Passing the object would absolutely require that the DLL was linked
		// with exactly the same version of config.h/cpp that we're using, so
		// that it uses the byte-for-byte identical memory layout.  Using the
		// file to mediate the exchange insulates us from such dependencies.
		// It's less efficient than passing the config object in memory, but
		// in practice the time needed to read and write the file is trivial,
		// so there's no reason not to go through the file.
		Application::SaveFiles();

		// disable windows
		Application::Get()->EnableSecondaryWindows(false);

		// final dialog location, for saving later
		RECT initDialogRect = ConfigManager::GetInstance()->GetRect(ConfigVars::OptsDialogPos);
		RECT finalDialogRect;

		// Admin Auto Run setup callback
		auto setUpAdminAutoRun = [this](DWORD delayTime)
		{
			// format an installAutoLaunch request
			TCHAR delayTimeStr[30];
			_stprintf_s(delayTimeStr, _T("%lu"), static_cast<unsigned long>(delayTime));
			const TCHAR *request[] = { _T("installAutoLaunch"), delayTimeStr };

			// send the request
			TSTRING errDetails;
			std::vector<TSTRING> reply;
			if (Application::Get()->SendAdminHostRequest(request, countof(request), reply, errDetails))
			{
				// success
				return true;
			}
			else
			{
				// couldn't send the request at all - provide our own error message
				LogSysError(EIT_Error, LoadStringT(IDS_ERR_SYNCAUTOLAUNCHREG), errDetails.c_str());
				return false;
			}
		};

		// create a scope for the dialog tracker
		{
			// track that the dialog is open
			struct DialogTracker
			{
				DialogTracker(PlayfieldView *pfv) : pfv(pfv) { pfv->settingsDialogOpen = true; }
				~DialogTracker() { pfv->settingsDialogOpen = false; }
				PlayfieldView *pfv;
			};
			DialogTracker dialogTracker(this);

			// Run the dialog.   Provide a callback that reloads the config if the
			// dialog saves changes.
			showOptionsDialog(
				[this](bool succeeded) { if (succeeded) Application::Get()->ReloadConfig(); },
				[this](HWND hWnd) { Application::Get()->InitDialogPos(hWnd, ConfigVars::OptsDialogPos); },
				Application::Get()->IsAdminHostAvailable(),
				setUpAdminAutoRun,
				&finalDialogRect);
		}

		// save the final dialog position if it changed
		if (initDialogRect != finalDialogRect)
			ConfigManager::GetInstance()->Set(ConfigVars::OptsDialogPos, finalDialogRect);

		// re-enable windows
		Application::Get()->EnableSecondaryWindows(true);

		// Reset attract mode.  We automatically suppress attract mode while
		// the dialog is showing, but the dialog might have been up for long
		// enough that we'd show it right away as soon as we remove that
		// suppression by closing the dialog.  But closing the dialog counts
		// as an interaction with the UI, so reset the timer now.
		attractMode.Reset(this);

		// Reinitialize raw input.  The options dialog takes over raw input
		// while it's running, so that it can read joystick input.  The whole
		// raw input subsystem is global to the process, so the DLL replaces
		// our registration while it's running.  We need to restore our
		// registration when it's done.
		InputManager::GetInstance()->InitRawInput(Application::Get()->GetPlayfieldWin()->GetHWnd());
	}
	else
	{
		// The options dialog DLL isn't available - show an error
		WindowsErrorMessage winErr;
		Application::InUiErrorHandler eh;
		eh.SysError(LoadStringT(IDS_ERR_OPTS_DIALOG_DLL),
			MsgFmt(_T("Error %s: %s"), progress, winErr.Get()));
	}

	// show the info box again
	SyncInfoBox();
}

void PlayfieldView::CmdGameInfo(const QueuedKey &key)
{
	if (key.mode == KeyDown)
	{
		ShowGameInfo();
		PlayButtonSound(_T("Select"));
	}

	// check for a media capture "Manual Go" gesture
	if (captureManualGoButton == CaptureManualGoButton::Info)
		CheckManualGo(manualGoLeftDown, key);
}

void PlayfieldView::CmdInstCard(const QueuedKey &key)
{
	if (key.mode == KeyDown)
	{
		ShowInstructionCard();
		PlayButtonSound(_T("Select"));
	}

	// check for a media capture "Manual Go" gesture
	if (captureManualGoButton == CaptureManualGoButton::Instructions)
		CheckManualGo(manualGoLeftDown, key);
}

// -----------------------------------------------------------------------
//
// Status lines
//

void PlayfieldView::DisableStatusLine()
{
	// hide the status line elements
	upperStatus.Hide();
	lowerStatus.Hide();
	attractModeStatus.Hide();

	// stop the timer
	KillTimer(hWnd, statusLineTimerID);
	KillTimer(hWnd, attractModeStatusLineTimerID);
}

void PlayfieldView::EnableStatusLine()
{
	// reset the status line elements
	upperStatus.Reset(this);
	lowerStatus.Reset(this);

	// start the timer
	SetTimer(hWnd, statusLineTimerID, statusLineTimerInterval, 0);
}

void PlayfieldView::UpdateAllStatusText() 
{
	upperStatus.OnSourceDataUpdate(this);
	lowerStatus.OnSourceDataUpdate(this);
	attractModeStatus.OnSourceDataUpdate(this);
}

void PlayfieldView::StatusLine::Init(
	PlayfieldView *pfv, int yOfs, int idleSlide, int fadeSlide,
	const TCHAR *cfgVar, int defaultMessageResId)
{
	// remember my location slide distance, normalized to camera coordinates
	this->y = float(yOfs) / 1920.0f;
	this->idleSlide = float(idleSlide) / 1920.0f;
	this->fadeSlide = float(fadeSlide) / 1920.0f;

	// get my time interval, if specified
	ConfigManager *cfg = ConfigManager::GetInstance();
	dispTime = cfg->GetInt(MsgFmt(_T("%s.UpdateTime"), cfgVar), dispTime);

	// clear any existing messages
	items.clear();
	curItem = items.end();

	// get my message list
	const TCHAR *messages = cfg->Get(MsgFmt(_T("%s.Messages"), cfgVar), nullptr);
	TSTRING defmsg;
	if (messages == nullptr)
	{
		defmsg = LoadStringT(defaultMessageResId);
		messages = defmsg.c_str();
	}

	// break it into sections on '|' characters
	TSTRING buf;
	auto AddSect = [&buf, this]()
	{
		// ignore empty sections
		if (buf.length() == 0)
			return;

		// add an item
		items.emplace_back(buf.c_str());

		// clear the buffer for the next section
		buf.clear();
	};
	for (const TCHAR *p = messages; *p != 0; ++p)
	{
		// check for '|' - this is the break character
		if (*p == '|')
		{
			// stuttered -> literal '|' in the output
			if (p[1] == '|')
				++p;
			else
			{
				// it's a new section
				AddSect();
				continue;
			}
		}

		// add the character
		buf.append(p, 1);
	}

	// add the last section
	AddSect();

	// set the starting time such that we do an immediate update
	startTime = GetTickCount() - dispTime - 1;
}

void PlayfieldView::StatusLine::Reset(PlayfieldView *)
{
	// reset to no active item
	curItem = items.end();

	// set the starting time such that we do an immediate update
	startTime = GetTickCount() - dispTime - 1;
}

void PlayfieldView::StatusLine::OnSourceDataUpdate(PlayfieldView *pfv)
{
	// if there's a current item, and it has a sprite, and its text is
	// no longer valid, regenerate its sprite
	if (curItem != items.end() && curItem->sprite != nullptr && curItem->NeedsUpdate(pfv))
	{
		// remember the current alpha
		float alpha = curItem->sprite->alpha;

		// update its text
		curItem->Update(pfv, this, y);

		// set the same alpha in the new item
		if (curItem->sprite != nullptr)
			curItem->sprite->alpha = alpha;
	}
}

void PlayfieldView::StatusLine::TimerUpdate(PlayfieldView *pfv)
{
	// if there are no items, there's nothing to do
	if (items.size() == 0)
		return;

	// if the status line display is disabled, do nothing
	if (!pfv->statusLineEnabled)
	{
		if (curItem != items.end())
		{
			if (curItem->sprite != nullptr)
				curItem->sprite->alpha = 0.0;

			curItem = items.end();
		}
		return;
	}

	// get the time so far in this phase
	DWORD dt = GetTickCount() - startTime;
	const float fadeTime = 350.0f;

	// continue according to the current phase
	if (phase == DispPhase)
	{
		// display phase - start the fade when the display time elapses
		if (dt > dispTime)
		{
			// Done with this display interval.  Check if we need to
			// move on to a new item.  We need to move on if any of
			// the following are true:
			//
			// - We have more than one item in the list
			// - The next item's text needs updating
			// - We have a slide effect
			// - This is a temporary item
			//
			// In any of these cases, we can't leave the current item
			// on the screen, so we have to initiate a fade.
			auto nextItem = NextItem();
			if (nextItem->NeedsUpdate(pfv) 
				|| nextItem != curItem 
				|| idleSlide != 0.0f
				|| (curItem != items.end() && curItem->isTemp))
				phase = FadeOutPhase;

			// start the next phase timer
			startTime = GetTickCount();
		}
		else
		{
			// if this item slides while displayed, slide it
			if (idleSlide != 0 && curItem != items.end() && curItem->sprite != 0)
			{
				float progress = float(dt) / float(dispTime);
				curItem->sprite->offset.x = idleSlide * (0.5f - progress);
				curItem->sprite->UpdateWorld();
			}
		}
	}
	else if (phase == FadeInPhase)
	{
		// fading in
		float progress = fminf(1.0f, float(dt) / fadeTime);
		if (curItem != items.end() && curItem->sprite != 0)
		{
			// fade and slide the item
			curItem->sprite->alpha = progress;
			float mirror = 1.0f - progress;
			float ramp = mirror * mirror * mirror;
			curItem->sprite->offset.x = idleSlide*0.5f + fadeSlide*ramp;
			curItem->sprite->UpdateWorld();
		}

		// transition to display phase when we reach the end of the fade
		if (progress == 1.0f)
		{
			phase = DispPhase;
			startTime = GetTickCount();
		}
	}
	else if (phase == FadeOutPhase)
	{
		// fading out
		float progress = fminf(1.0f, float(dt) / fadeTime);
		if (curItem != items.end() && curItem->sprite != 0)
		{
			// fade and slide the item
			curItem->sprite->alpha = 1.0f - progress;
			float ramp = progress * progress * progress;
			curItem->sprite->offset.x = -idleSlide*0.5f - fadeSlide*ramp;
			curItem->sprite->UpdateWorld();
		}

		// transition to the next item when we reach the end of the fade
		if (progress == 1.0f)
		{
			// get the next item, noting if it's the same as the current item
			auto next = NextItem();

			// if the outgoing item was temporary, delete it
			if (curItem != items.end() && curItem->isTemp)
			{
				// remove the temporary item from the list and discard it
				items.erase(curItem);

				// if the list is now empty, the next item is 'end'
				if (items.size() == 0)
					next = items.end();
			}

			// advance to the next item
			curItem = next;
			
			// initialize the new item's fade-in, if there is a new item
			if (curItem != items.end())
			{
				// update it
				curItem->Update(pfv, this, y);

				// set it up for the fade-in
				curItem->sprite->alpha = 0;
				curItem->sprite->offset.x = idleSlide * 0.5f + fadeSlide;
				curItem->sprite->UpdateWorld();
			}

			// update the drawing list
			pfv->UpdateDrawingList();

			// switch to display mode and reset the timer
			startTime = GetTickCount();
			phase = FadeInPhase;
		}
	}
}

void PlayfieldView::StatusLine::AddSprites(std::list<Sprite*> &sprites)
{
	// add the current item
	if (curItem != items.end() && curItem->sprite != nullptr)
		sprites.push_back(curItem->sprite);
}

bool PlayfieldView::StatusItem::NeedsUpdate(PlayfieldView *pfv)
{
	return sprite == 0 || ExpandText(pfv) != dispText;
}

std::list<PlayfieldView::StatusItem>::iterator PlayfieldView::StatusLine::NextItem()
{
	// get the current item, wrapping to the first if past the end
	auto i = curItem;
	if (i == items.end())
		return items.begin();

	// get the next item
	auto next = i;
	++next;

	// return the new item, wrapping to the first if past the end
	return (next == items.end() ? items.begin() : next);
}

TSTRING PlayfieldView::StatusItem::ExpandText(PlayfieldView *pfv)
{
	// Get the current game list selection and filter, for macro expansion
	GameList *gl = GameList::Get();
	GameListItem *game = gl->GetNthGame(0);
	const GameListFilter *filter = gl->GetCurFilter();

	// Get my new message text, expanding macros
	std::basic_regex<TCHAR> pat(_T("\\[([\\w.]+)((:[^\\]:]*)?(:[^\\]:]*)?(:[^\\]]*)?)\\]"));
	return regex_replace(srcText, pat, [gl, game, filter, pfv](const std::match_results<TSTRING::const_iterator> &m) -> TSTRING
	{
		// pull out the variable name, in lower-case for matching
		TSTRING v = m[1].str().c_str();
		std::transform(v.begin(), v.end(), v.begin(), ::_totlower);

		// Check for ":" suffix strings.  These are used for the singular/plural
		// forms for count variables, and for date format pictures for date variables.
		if (m[2].length() != 0)
		{
			// Check for count variables.  For these, we must match at least
			// two ":" sections.
			if (m[3].matched && m[4].matched)
			{
				auto PluralFormat = [&m](float n)
				{
					// substitute the appropriate section
					if (n == 0.0f && m[5].matched)
						return m[5].str().substr(1);
					else if (n > 0.0f && n <= 1.0f)
						return m[3].str().substr(1);
					else
						return m[4].str().substr(1);
				};

				if (v == _T("filter.count"))
					return PluralFormat(static_cast<float>(GameList::Get()->GetCurFilterCount()));
				else if (v == _T("credits"))
					return PluralFormat(pfv->GetEffectiveCredits());
				else if (v == _T("game.playcount"))
					return PluralFormat(IsGameValid(game) ? static_cast<float>(gl->GetPlayCount(game)) : 0.0f);
			}

			// Check for date variables
			auto XlatLitChars = [](TSTRING &s, bool xlatPct) {
				const static std::basic_regex<TCHAR> litCharPat(_T("%[()!%]"));
				return regex_replace(s, litCharPat, [xlatPct](const std::match_results<TSTRING::const_iterator>& m) -> TSTRING {
					TCHAR c = m[0].str()[1];
					return c == '(' ? _T("[") :
						c == ')' ? _T("]") :
						c == '!' ? _T("|") :
						c == '%' ? (xlatPct ? _T("%") : _T("%%")) :
						TSTRING(c, 1);
				});
			};
			auto DateFormat = [&m, &XlatLitChars](const TCHAR *str) -> TSTRING
			{
				// separate the format string into <date mask>|<never> sections
				TSTRING format = m[2].str().substr(1);
				TSTRING never;
				const TCHAR *bar = _tcschr(format.c_str(), '|');
				if (bar != nullptr)
				{
					never = bar + 1;
					format = format.substr(0, bar - format.c_str());
				}

				// convert to a DateTime and check if it's valid
				DateTime d(str);
				if (d.IsValid())
				{
					// it's a valid date value - convert to a struct tm
					tm tm;
					d.ToStructTm(tm);

					// Apply the string format.  Substitute our additional literal
					// character sequences first, but leave %% sequences intact, since
					// they'll be handled by _tcsftime().
					TCHAR dbuf[512];
					_tcsftime(dbuf, countof(dbuf), XlatLitChars(format, false).c_str(), &tm);

					// return the formatted string
					return dbuf;
				}
				else if (bar != nullptr)
				{
					// Invalid date, and there's a custom "never" string - use it, 
					// applying literal character substitutions.
					return XlatLitChars(never, true);
				}
				else
				{
					// invalid date and no custom "never" string - use the default "never"
					return LoadStringT(IDS_LAST_PLAYED_NEVER);
				}
			};
			if (v == _T("game.lastplayed") && IsGameValid(game))
				return DateFormat(gl->GetLastPlayed(game));
		}

		// it's an ordinary substitution
		if (v == _T("game.title"))
			return game != nullptr ? game->title : _T("?");
		else if (v == _T("game.manuf"))
			return IsGameValid(game) && game->manufacturer != nullptr ? game->manufacturer->manufacturer : LoadStringT(IDS_NO_MANUFACTURER);
		else if (v == _T("game.year"))
			return IsGameValid(game) && game->year != 0 ? MsgFmt(_T("%d"), game->year).Get() : LoadStringT(IDS_NO_YEAR);
		else if (v == _T("game.system"))
			return IsGameValid(game) && game->system != nullptr ? game->system->displayName : LoadStringT(IDS_NO_SYSTEM);
		else if (v == _T("game.rating"))
			return pfv->StarsAsText(IsGameValid(game) ? gl->GetRating(game) : -1);
		else if (v == _T("game.typecode"))
			return IsGameValid(game) ? game->tableType : LoadStringT(IDS_NO_TABLE_TYPE);
		else if (v == _T("game.typename"))
		{
			if (IsGameValid(game))
			{
				if (auto it = pfv->tableTypeNameMap.find(game->tableType); it != pfv->tableTypeNameMap.end())
					return WSTRINGToTSTRING(it->second);
			}
			return LoadStringT(IDS_NO_TABLE_TYPE);
		}
		else if (v == _T("game.playcount"))
		{
			if (IsGameValid(game))
			{
				TCHAR buf[20];
				_stprintf_s(buf, _T("%d"), gl->GetPlayCount(game));
				return buf;
			}
			return LoadStringT(IDS_NO_PLAY_COUNT);
		}
		else if (v == _T("game.playtime"))
			return IsGameValid(game) ? pfv->PlayTimeAsText(gl->GetPlayTime(game)) : LoadStringT(IDS_NO_PLAY_TIME);
		else if (v == _T("game.lastplayed"))
		{
			if (IsGameValid(game))
			{
				DateTime d(gl->GetLastPlayed(game));
				if (d.IsValid())
					return d.FormatLocalDateTime(DATE_LONGDATE, TIME_NOSECONDS);
				return LoadStringT(IDS_LAST_PLAYED_NEVER);
			}
			return LoadStringT(IDS_NO_LAST_PLAYED);
		}
		else if (v == _T("game.tablefilename"))
			return IsGameValid(game) ? game->filename : LoadStringT(IDS_NO_TABLE_FILE);
		else if (v == _T("filter.title"))
			return filter->GetFilterTitle();
		else if (v == _T("filter.count"))
			return MsgFmt(_T("%d"), GameList::Get()->GetCurFilterCount()).Get();
		else if (v == _T("credits"))
			return FormatFraction(pfv->GetEffectiveCredits());
		else if (v == _T("lb"))
			return _T("[");
		else if (v == _T("rb"))
			return _T("]");
		else
			return m[0].str(); // no match - return the full original text
	});
}

void PlayfieldView::FireStatusLineEvent(JsValueRef statusLineObj, const TSTRING &srcText, TSTRING &expandedText)
{
	if (auto js = JavascriptEngine::Get(); js != nullptr)
	{
		// fire the event, retrieving the event object
		JsValueRef eventObjVal;
		js->FireAndReturnEvent(eventObjVal, statusLineObj, jsStatusLineEvent, srcText, expandedText);

		try
		{
			// replace the expanded text with the text in the event object
			JavascriptEngine::JsObj eventObj(eventObjVal);
			if (eventObj.Has(L"expandedText"))
				expandedText = eventObj.Get<TSTRING>("expandedText");
		}
		catch (...)
		{
			// clear and ignore any Javascript exception
			JsValueRef jsexc;
			JsGetAndClearException(&jsexc);
		}
	}
}

void PlayfieldView::StatusItem::Update(PlayfieldView *pfv, StatusLine *sl, float y)
{
	// get my new display text
	TSTRING newDispText = ExpandText(pfv);
	
	// fire the Javascript event
	pfv->FireStatusLineEvent(sl->jsobj, this->srcText, newDispText);

	// if there's already a sprite, and the message is the same as
	// before, no update is necessary
	if (sprite != nullptr && newDispText == dispText)
		return;

	// store the new expanded text
	dispText = newDispText;

	// create the new sprite
	sprite.Attach(new Sprite());
	const int width = 1080, height = 75;
	Application::InUiErrorHandler eh;
	sprite->Load(width, height, [this, pfv, width, height](HDC hdc, HBITMAP)
	{
		// set up a drawing context
		Gdiplus::Graphics g(hdc);

		// measure the text
		Gdiplus::RectF bbox;
		FontPref &font = pfv->statusFont;
		g.MeasureString(dispText.c_str(), -1, font, Gdiplus::PointF(0, 0), &bbox);

		// center it
		float x = (float(width) - bbox.Width) / 2.0f;
		float y = (float(height) - bbox.Height) / 2.0f;

		// draw it centered
		Gdiplus::SolidBrush txt(Gdiplus::Color(255, 255, 255, 255));
		Gdiplus::SolidBrush shadow(Gdiplus::Color(192, 0, 0, 0));
		g.DrawString(dispText.c_str(), -1, font, Gdiplus::PointF(x+2, y+2), &shadow);
		g.DrawString(dispText.c_str(), -1, font, Gdiplus::PointF(x, y), &txt);

		// flush the drawing context to the bitmap
		g.Flush();
	}, eh, _T("Status Message"));

	// set it up in the proper location
	sprite->offset.y = -0.5f + float(height/2)/1920.f + y;
	sprite->UpdateWorld();

	// update the drawing list
	pfv->UpdateDrawingList();
}

// Generic Javascript StatusLine method invoker.  Figures out which concrete
// status line object is being invoked via the Javascript this's "id" property,
// then invokes the StatusLine:: method M using the js arguments.
template<typename MethodType, MethodType M, typename R, typename... Args>
R PlayfieldView::JsStatusLineMethod(JsValueRef selfVal, Args... args)
{
	auto js = JavascriptEngine::Get();
	try
	{
		// get the status line ID from this.id
		JavascriptEngine::JsObj self(selfVal);
		auto id = self.Get<WSTRING>("id");

		// invoke the method on the appropriate status line
		auto pfv = Application::Get()->GetPlayfieldView();
		if (id == L"upper")
			return static_cast<R>((this->upperStatus.*M)(args...));
		else if (id == L"lower")
			return static_cast<R>((this->lowerStatus.*M)(args...));
		else if (id == L"attract")
			return static_cast<R>((this->attractModeStatus.*M)(args...));

		js->Throw(_T("StatusLine method called on non-StatusLine object"));
	}
	catch (JavascriptEngine::CallException exc)
	{
		js->Throw(exc.jsErrorCode, CHARToTCHAR(exc.what()));
	}

	// dummy return value - if we get here, we've already put the javascript
	// engine into an exception state, so it'll ignore whatever we return
	return static_cast<R>(0);
}

JsValueRef PlayfieldView::StatusLine::JsGetText()
{
	// create an array for the results
	auto arr = JavascriptEngine::JsObj::CreateArray();

	// populate it with our messages
	for (auto const &s : items)
	{
		// create an object: { isTemp, text }
		auto obj = JavascriptEngine::JsObj::CreateObject();
		obj.Set("text", s.srcText);
		obj.Set("isTemp", s.isTemp);
		arr.Push(obj);
	}

	// return the array
	return arr.jsobj;
}

int PlayfieldView::StatusLine::JsGetCur()
{
	// if there are no items, return -1
	if (items.size() == 0)
		return -1;

	// Scan the list for the current display index
	int i = 0;
	for (auto s = items.begin(); s != items.end(); ++s, ++i)
	{
		// If this is the current item, return its index
		if (s == curItem)
			return i;
	}

	// we didn't find it
	return -1;
}

void PlayfieldView::StatusLine::JsSetText(int index, TSTRING txt)
{
	// find the item by index
	int i = 0;
	for (auto s = items.begin(); s != items.end(); ++s, ++i)
	{
		// if this is the item we're looking for, update it
		if (i == index)
		{
			// this is the one - update it
			s->srcText = txt;

			// if it's the current display item, refresh the display
			if (s == curItem)
				this->OnSourceDataUpdate(Application::Get()->GetPlayfieldView());

			// no need to keep searching
			break;
		}
	}
}

void PlayfieldView::StatusLine::JsAdd(TSTRING txt, JsValueRef indexVal)
{
	// the index value is undefined, insert at the end of the list.
	auto js = JavascriptEngine::Get();
	if (indexVal != js->GetUndefVal())
	{
		// get the numeric index
		int index = js->JsToNative<int>(indexVal);

		// find the item by index
		int i = 0;
		for (auto s = items.begin(); s != items.end(); ++s, ++i)
		{
			// if this is the item we're looking for, update it
			if (i == index)
			{
				// insert here, then mission accomplished
				items.emplace(s, txt.c_str());
				return;
			}
		}
	}

	// no index specified, or it's not within range; insert at the
	// end of the list
	items.emplace_back(txt.c_str());
}

void PlayfieldView::StatusLine::JsRemove(int index)
{
	// find the item by index
	int i = 0;
	for (auto s = items.begin(); s != items.end(); ++s, ++i)
	{
		// if this is the item we're looking for, remove the item
		if (i == index)
		{
			items.erase(s);
			return;
		}
	}
}

void PlayfieldView::StatusLine::JsShow(TSTRING txt)
{
	// Find the position for the new item.  Insert after the current
	// item and after any temporary items already in the list after
	// the current item.
	auto pos = curItem;
	if (items.size() != 0)
	{
		for (;;)
		{
			// advance to the next item, wrapping at the end
			if (++pos == items.end())
				pos = items.begin();

			// If this *isn't* a temporary item, insert here.  Also stop
			// if we're back to the first item: that must mean that all of
			// the items in the list are temporary, in which case we want
			// to insert before the current item so that the new item is
			// scheduled after all of the existing items.
			if (!pos->isTemp || pos == curItem || (pos == items.begin() && curItem == items.end()))
				break;
		}
	}

	// insert it and mark it as temporary
	auto it = items.emplace(pos, txt.c_str());
	it->isTemp = true;
}

// ------------------------------------------------------------------------
//
// Attract mode
//

void PlayfieldView::AttractMode::OnTimer(PlayfieldView *pfv)
{
	// get the time since the last event
	DWORD dt = GetTickCount() - t0;

	// If a save is pending, and it's been long enough, do a save.
	// This writes any uncommitted, in-memory changes to external
	// files (config, game stats database).  We defer file writes
	// during normal operations to avoid UI pauses.  If we've been
	// idle a while, the user must have their attention elsewhere,
	// so this is a good time to sneak in a save. 
	if (savePending && dt > 15000)
	{
		// save files
		Application::SaveFiles();

		// Clear the "save pending" flag.  We only need to check
		// this once per idle period, since there will be no new
		// changes to commit until the user does something to cause
		// a change.  That will require user interaction, which will
		// reset the idle timer and reset the "save pending" flag.
		savePending = false;
	}

	// check the mode
	if (active)
	{
		// check to see if the auto game switch time has elapsed
		if (dt > switchTime)
		{
			// Select a new game, randomly 1..10 games.  Note that this only
			// goes forwards on the wheel, but if it were desirable we could
			// just as well go backwards at random as well.  However, if we
			// do want to use a +/- range, it's better to have some bias in
			// one direction or the other (say, -5..+10), because a uniform
			// window (e.g., -5..+5) will tend to do a "random walk" that
			// averages out over time to no excursion from the starting point.
			// It seems more interesting to jump around the whole wheel as
			// attract mode progresses.   I actually don't think a +/- range
			// is all that necessary simply because the wheel is a *wheel*,
			// in that we'll cycle back to the "A" games after getting past
			// the "Z" games.  So we'll end up going backwards, in a way,
			// even with a forward-only random range.
			int d = int(roundf((float(rand()) / float(RAND_MAX))*9.0f + 1.0f));
			pfv->SwitchToGame(d, false, false);

			// reset the timer
			t0 = GetTickCount();

			// make sure the cursor stays hidden while in attract mode
			Application::HideCursor();

			// Fire a DOF attract mode game switch event
			pfv->QueueDOFPulse(L"PBYAttractWheelNext");
		}

		// Fire the attract DOF timed events
		pfv->QueueDOFPulse(TSTRINGToWSTRING(MsgFmt(_T("PBYAttractA%d"), dofEventA).Get()));
		pfv->QueueDOFPulse(TSTRINGToWSTRING(MsgFmt(_T("PBYAttractB%d"), dofEventB).Get()));
		
		// Update the timed event loops.  The "A" series is a 5-step loop (1..5),
		// and the "B" series is a 60-step loop (1..60).
		dofEventA = (dofEventA % 5) + 1;
		dofEventB = (dofEventB % 60) + 1;

		// Decide randomly if we should fire a random event on this round
		const double eventProbability = 0.1;
		const int rollUnder = int(eventProbability * double(RAND_MAX));
		if (rand() < rollUnder)
		{
			// randomly choose one of the five events, uniformly distributed
			int numEvents = 5;
			int eventNo = int(floor((double(rand()) / RAND_MAX) * numEvents)) + 1;

			// fire it
			pfv->QueueDOFPulse(TSTRINGToWSTRING(MsgFmt(_T("PBYAttractR%d"), eventNo).Get()));
		}
	}
	else if (enabled)
	{
		// We're in standby mode, and attract mode is enabled.  If we've
		// been inactive for the minimum idle duration, and the application
		// is in the foreground, and we're not disabled (which probably
		// means that we're showing a dialog), enter attract mode.
		if (dt > idleTime 
			&& Application::Get()->IsInForeground()
			&& IsWindowEnabled(GetParent(pfv->GetHWnd())))
		{
			// fire the attract mode start Javascript event; if the handler
			// cancels the event, reset the attract mode timer and go skip
			// the attrac mode entry
			if (!pfv->FireAttractModeEvent(true))
			{
				// event canceled - reset our timers
				Reset(pfv);
			}
			else
			{
				// switch to active mode
				StartAttractMode(pfv);
			}

			// If a save is pending, do it now.  Given that the user
			// has left the machine running without interaction for 
			// long enough to trigger attract mode, it could be quite
			// a while longer still before the user returns, so it's
			// a good idea to commit changes now, in case there's a
			// power loss or crash or something during the potentially
			// long idle time to come.
			if (savePending)
			{
				Application::SaveFiles();
				savePending = false;
			}
		}
	}
}

void PlayfieldView::AttractMode::OnKeyEvent(PlayfieldView *pfv)
{
	// reset attract mode on any keystroke
	Reset(pfv);
}

void PlayfieldView::AttractMode::Reset(PlayfieldView *pfv)
{
	// turn off attract mode if it's active
	if (active)
	{
		// signal javascript
		pfv->FireAttractModeEvent(false);

		// exit attract mode
		EndAttractMode(pfv);
	}

	// reset the attract mode event timer
	t0 = GetTickCount();

	// check again for file-save work the next time we're idle
	savePending = true;
}

void PlayfieldView::AttractMode::StartAttractMode(PlayfieldView *pfv)
{
	// enter active mode
	active = true;

	// notify DOF
	pfv->QueueDOFPulse(L"PBYScreenSaverStart");
	pfv->dof.SetUIContext(L"PBYScreenSaver");

	// notify the playfield
	pfv->OnStartAttractMode();

	// turn off the cursor while in attract mode
	Application::HideCursor();

	// reset the timer, so that we the next elapsed time check
	// measures from when we started attract mode
	t0 = GetTickCount();
}

void PlayfieldView::AttractMode::EndAttractMode(PlayfieldView *pfv)
{
	// no longer in attract mode
	active = false;

	// fire the DOF Screen Saver Quit event
	pfv->QueueDOFPulse(L"PBYScreenSaverQuit");

	// notify the playfield
	pfv->OnEndAttractMode();

	// reset the attract mode event timer
	t0 = GetTickCount();

	// check again for file-save work the next time we're idle
	savePending = true;
}

void PlayfieldView::OnStartAttractMode()
{
	// turn off the status line
	DisableStatusLine();

	// reset and enable the attract mode status line
	attractModeStatus.Reset(this);
	SetTimer(hWnd, attractModeStatusLineTimerID, statusLineTimerInterval, 0);

	// close menus
	CloseMenusAndPopups();

	// update video muting for the new attract mode status
	Application::Get()->UpdateVideoVolume();

	// update the javascript UI mode
	UpdateJsUIMode();

	// rebuild the display list - the list of visible layers can change in
	// attract mode (e.g., we hide the wheel images)
	UpdateDrawingList();
}

void PlayfieldView::OnEndAttractMode()
{
	// set the DOF status to Wheel mode
	dof.SetUIContext(L"PBYWheel");

	// restore status line updates
	DisableStatusLine();
	EnableStatusLine();

	// update the info box popup (since we remove that during attract mode)
	UpdateInfoBox();
		
	// update video muting, in case videos were muted in attract mode
	Application::Get()->UpdateVideoVolume();

	// update the javascript UI mode
	UpdateJsUIMode();

	// rebuild the display list for any layer visibility changes
	UpdateDrawingList();
}

// -----------------------------------------------------------------------
//
// DOF event queue
//

void PlayfieldView::ShowDOFClientInitErrors()
{
	// check for errors
	auto& eh = DOFClient::initErrors;
	if (eh.CountErrors() != 0)
	{
		// If the last DOF initialization attempt failed, don't show errors
		// this time: we're probably just running into the same configuration
		// problem on every attempt, so there's no point in repeating the 
		// error.  If we haven't already shown an error, display the current
		// error list.
		if (!dofInitFailed)
		{
			if (eh.CountErrors() == 1)
				eh.EnumErrors([this](const ErrorList::Item &item) { ShowSysError(item.message.c_str(), item.details.c_str()); });
			else if (eh.CountErrors() > 1)
				ShowError(EIT_Error, LoadStringT(IDS_ERR_DOFLOAD), &eh);
		}

		// remember that this attempt failed, so that we can suppress errors
		// on the next attempt
		dofInitFailed = true;
	}
	else
	{
		// There are no errors to show.  Flag that we successfully initialized,
		// to ensure that we display errors if the next attempt fails.
		dofInitFailed = false;
	}
}

void PlayfieldView::QueueDOFPulse(const TCHAR *name)
{
	// Skip this if DOF isn't ready
	if (!DOFClient::IsReady())
		return;

	// Check to see if this pulse is already queued.  If it is, don't
	// queue a new event, but instead extend the current event:
	//
	// - If there's a pending ON event for this pulse, simply leave
	//   it as is.  We can't make a new event turn on any sooner, so
	//   we'll use the existing ON as our ON.
	//
	// - If there's a pending OFF event for the pulse, replace it 
	//   with a null event, and add a new OFF event at the end of
	//   the queue.  This will effectively extend the pulse to last
	//   until the "queue present time", after all of the past 
	//   events in the queue have been processed.
	bool foundOn = false;
	for (auto &e : dofQueue)
	{
		// check for an ON event - if we find it, simply note it
		if (e.name == name && e.val != 0)
			foundOn = true;

		// check for an OFF event - if we find it, replace it with
		// a null event
		if (e.name == name && e.val == 0)
			e.name = _T("");
	}

	// if we didn't find a pending ON for the same event, queue one now
	if (!foundOn)
		QueueDOFEvent(name, 1);

	// queue an OFF for the event (do this even if we found a pending
	// event already in the queue, as we just replaced that with a
	// null event to extend the ON duration of this event to the end
	// of the current queue)
	QueueDOFEvent(name, 0);
}

const DWORD dofPulseTimerInterval = 20;
void PlayfieldView::QueueDOFEvent(const TCHAR *name, UINT8 val)
{
	// only proceed if DOF is ready
	if (DOFClient::IsReady() && DOFClient::Get() != nullptr)
	{
		// if the queue is empty, start the timer
		if (dofQueue.size() == 0)
		{
			// There's nothing in the queue.  If it's been long enough
			// since the last event, we can fire this event immediately.
			if (GetTickCount64() - lastDOFEventTime > dofPulseTimerInterval)
			{
				FireDOFEvent(name, val);
				return;
			}

			// It hasn't been long enough since we sent the last event;
			// DOF needs time to digest updates.  We'll have to queue this
			// event and handle it on our timer.  Since the queue is empty
			// going in, the timer isn't running, so we have to start it.
			SetTimer(hWnd, dofPulseTimerID, dofPulseTimerInterval, 0);
		}

		// queue the event
		dofQueue.emplace_back(name, val);
	}
}

void PlayfieldView::OnDOFTimer()
{
	// if there's anything in the queue, process the event
	if (DOFClient::IsReady() && dofQueue.size() != 0)
	{
		// Send the effect to DOF, if DOF is active.  Note that
		// we might have nulled out the event by replacing its name
		// with an empty string; these are just there for timer
		// padding, so don't send them to DOF but do count them as
		// consuming this timer event.
		auto &event = dofQueue.front();
		if (event.name.length() != 0)
			FireDOFEvent(event.name.c_str(), event.val);

		// discard the event
		dofQueue.pop_front();
	}

	// if the queue is empty, stop the timer
	if (dofQueue.size() == 0)
		KillTimer(hWnd, dofPulseTimerID);
}

void PlayfieldView::FireDOFEvent(const TCHAR *name, UINT8 val)
{
	// fire the event in DOF
	if (DOFClient *dof = DOFClient::Get(); dof != nullptr && DOFClient::IsReady())
		dof->SetNamedState(name, val);

	// note the last event time
	lastDOFEventTime = GetTickCount64();
}

// -----------------------------------------------------------------------
//
// DOF interaction
//

PlayfieldView::DOFIfc::DOFIfc()
{
}

PlayfieldView::DOFIfc::~DOFIfc()
{
}

void PlayfieldView::DOFIfc::OnDOFReady()
{
	if (DOFClient *dof = DOFClient::Get(); dof != nullptr && DOFClient::IsReady())
	{
		// set the UI context
		if (context.length() != 0)
			dof->SetNamedState(context.c_str(), 1);

		// set the game ROM state
		if (rom.length() != 0)
			dof->SetNamedState(rom.c_str(), 1);
	}
}

void PlayfieldView::DOFIfc::SetContextItem(const WCHAR *newVal, WSTRING &itemVar)
{
	// proceed only if DOF is active
	if (DOFClient *dof = DOFClient::Get(); dof != nullptr && DOFClient::IsReady())
	{
		// proceed only if this reflects a change in the value
		if (itemVar != newVal)
		{
			// turn off the current state, if any
			if (itemVar.length() != 0)
				dof->SetNamedState(itemVar.c_str(), 0);

			// remember the new state
			itemVar = newVal != nullptr ? newVal : _T("");

			// turn it on, if we have a new state
			if (itemVar.length() != 0)
				dof->SetNamedState(newVal, 1);
		}
	}
	else
	{
		// DOF isn't active or isn't ready yet - just store the state 
		// internally, so that we can apply it when DOF is ready
		itemVar = newVal != nullptr ? newVal : _T("");
	}
}

void PlayfieldView::DOFIfc::SyncSelectedGame()
{
	// only proceed if DOF is active
	if (auto dof = DOFClient::Get(); dof != nullptr && DOFClient::IsReady())
	{
		// set the new ROM for the current game selection
		if (auto game = GameList::Get()->GetNthGame(0); IsGameValid(game))
			SetRomContext(dof->GetRomForTable(game));
	}
}

void PlayfieldView::DOFIfc::SetKeyEffectState(const TCHAR *effect, bool keyDown)
{
	// look up the key entry in our map, populating it if necessary
	auto it = keyEffectState.find(effect);
	if (it == keyEffectState.end())
	{
		it = keyEffectState.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(effect),
			std::forward_as_tuple(false)).first;
	}

	// if the state has changed, set the new state
	if (keyDown != it->second)
	{
		// remember the new state
		it->second = keyDown;
		
		// update DOF, if active
		if (auto dof = DOFClient::Get(); dof != nullptr && DOFClient::IsReady())
			dof->SetNamedState(effect, keyDown ? 1 : 0);
	}
}

void PlayfieldView::DOFIfc::KeyEffectsOff()
{
	// if DOF is active, turn off all key effects
	if (auto dof = DOFClient::Get(); dof != nullptr && DOFClient::IsReady())
	{
		// visit all key effects
		for (auto &k : keyEffectState)
		{
			// if this effect is currently on, turn it off
			if (k.second)
			{
				k.second = false;
				dof->SetNamedState(k.first.c_str(), 0);
			}
		}
	}
}
