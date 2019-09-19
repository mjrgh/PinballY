// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Main playfield view Window
// This is a child window that serves as the D3D drawing surface for
// the main playfield window.


#pragma once

#include "../Utilities/Config.h"
#include "../Utilities/Joystick.h"
#include "../Utilities/KeyInput.h"
#include "../Utilities/InputManager.h"
#include "../Utilities/GraphicsUtil.h"
#include "D3D.h"
#include "D3DWin.h"
#include "Camera.h"
#include "TextDraw.h"
#include "PerfMon.h"
#include "D3DView.h"
#include "BaseView.h"
#include "Sprite.h"
#include "VideoSprite.h"
#include "AudioVideoPlayer.h"
#include "HighScores.h"
#include "GameList.h"
#include "JavascriptEngine.h"

class Sprite;
class TextureShader;
class GameListItem;
class GameCategory;
class MediaDropTarget;
class RealDMD;


// Playfield view
class PlayfieldView : public BaseView,
	JoystickManager::JoystickEventReceiver,
	InputManager::RawInputReceiver,
	ConfigManager::Subscriber,
	D3DView::IdleEventSubscriber
{
	// our drop bridge object is essentially an extension of this object
	friend class PlayfieldViewDropBridge;

public:
	PlayfieldView();

	// Create our system window
	bool Create(HWND parent);

	// Initialize real DMD support
	void InitRealDMD(ErrorHandler &eh);

	// get the real DMD instance
	RealDMD *GetRealDMD() const { return realDMD.get(); }

	// initialize javascript
	void InitJavascript();

	// Show any errors from DOF Client initialization
	void ShowDOFClientInitErrors();

	// Update keyboard shortcut listings in a menu.  We call this when
	// creating a menu and again whenever the keyboard preferences are
	// updated.  The parent window can also call this to update Player
	// commands in its menus, if it includes Player comands in its menu bar.
	void UpdateMenuKeys(HMENU hMenu);

	// Update menu command item checkmarks and enabled statue for current 
	// UI state.  We call this before displaying the context menu or
	// parent frame's window menu to set the menu item states.  The parent
	// window can also call this to update Player commands in its menus,
	// if it includes Player commands in its own menu bar.
	virtual void UpdateMenu(HMENU hMenu, BaseWin *fromWin) override;

	// Player menu update notification message.  The view window sends this
	// to the parent, with the menu handle in the WPARAM, to update a menu
	// that it's about to show with the current status of commands controlled
	// by the parent.
	static const UINT wm_parentUpdateMenu;

	// Global key event handlers.  All of our top-level windows (backglass,
	// DMD) route their keyboard input events here, since most key commands
	// are treated the same way in all of our windows.  A few commands apply
	// to individual windows, such as full-screen mode and monitor rotation,
	// so we provide a source window argument here for routing the command
	// (that we interpret from the key) back to the appropriate window.
	//
	// These routines are used in handling the low-level Windows key messages,
	// so we follow the usual convention that a 'true' return means that the
	// event was fully handled, and a 'false' return means that the system
	// define window proc should be invoked.
	bool HandleKeyEvent(BaseWin *win, UINT msg, WPARAM wParam, LPARAM lParam);
	bool HandleSysKeyEvent(BaseWin *win, UINT msg, WPARAM wParam, LPARAM lParam);
	bool HandleSysCharEvent(BaseWin *win, WPARAM wParam, LPARAM lParam);

	// Show the settings dialog
	void ShowSettingsDialog();

	// Is the settings dialog running?
	bool IsSettingsDialogOpen() const { return settingsDialogOpen; }

	// Show an error box.  If possible, we'll show it as a sprite
	// in the main view; if not, we'll show it as a system message box.
	void ShowError(ErrorIconType iconType, const TCHAR *groupMsg, const ErrorList *list = 0);
	void ShowSysError(const TCHAR *msg, const TCHAR *details);

	// Show an error box for a Flash file loading error.  This offers
	// the user an option to disable Flash permanently or ignore
	// errors for the remainder of this session.
	void ShowFlashError(const ErrorList &list);

	// Are Flash errors enabled for this session?  ShowFlashError()
	// gives the user an option to suppress any Flash error messages
	// for the remainder of the current session without actually
	// disabling SWF media.  
	bool showFlashErrors = true;

	// Show an error with automatic dismissal after a given timeout
	void ShowErrorAutoDismiss(DWORD timeout_ms, ErrorIconType iconType,
		const TCHAR *groupMsg, const ErrorList *list = 0);

	// enter/leave running game mode
	void BeginRunningGameMode(GameListItem *game, GameSystem *system);
	void EndRunningGameMode();

	// Show the pause menu, used as the main menu when a game is running
	void ShowPauseMenu(bool usingExitKey);

	// Application activation change notification.  The app calls this
	// when switching between foreground and background mode.
	virtual void OnAppActivationChange(bool activating);

	// frame window is being shown/hidden
	virtual void OnShowHideFrameWindow(bool show) override { }

	// Is Attract Mode active?
	bool IsAttractMode() const { return attractMode.active; }

	// reset attract mode
	void ResetAttractMode() { attractMode.Reset(this); }

	// change video enabling status
	virtual void OnEnableVideos(bool enable) override;

	// mute/unmute table audio
	void MuteTableAudio(bool mute);

	// clear media
	void ClearMedia();

	// Begin a file drop operation.  Call this at the start of a 
	// group of DropFile() calls to reset the internal records of
	// the files being handled.
	void BeginFileDrop();

	// Process a file dropped onto the application.  This handles
	// dropping a ZIP file containing a HyperPin media pack, or any
	// media file accepted by the target window.  Returns true if
	// the file was a recognized type, false if not.
	//
	// mediaType gives the media type implied by the location of the
	// drop.  When a given file type could be used for multiple media
	// types (e.g., background image or wheel logo), we draw "buttons"
	// during the drop letting the user indicate the media type by
	// dropping the file onto the appropriate button area.  The 
	// mediaType value gives us that result.  Null means that the
	// drop area didn't have an associated media type, which in turn
	// normally means that they're dropping a Media Pack file.
	bool DropFile(const TCHAR *fname, MediaDropTarget *dropTarget, const MediaType *mediaType);

	// End a file drop operation
	void EndFileDrop();

	// Update the UI after new game list files are discovered in a 
	// file system scan
	void OnNewFilesAdded();

	// Handle a change to the game list manager
	void OnGameListRebuild();

	// Media information for the main background image/video
	virtual const MediaType *GetBackgroundImageType() const override;
	virtual const MediaType *GetBackgroundVideoType() const override;

	// Game launch report, for the PFVMsgXxx messages for game launch steps
	struct LaunchReport
	{
		LaunchReport(int launchCmd, DWORD launchFlags, LONG gameInternalID, int systemConfigIndex) :
			launchCmd(launchCmd),
			launchFlags(launchFlags),
			gameInternalID(gameInternalID),
			systemConfigIndex(systemConfigIndex)
		{ }

		int launchCmd;
		DWORD launchFlags;
		LONG gameInternalID;
		int systemConfigIndex;
	};

	// Game launch error report, for PFVMsgGameLaunchError
	struct LaunchErrorReport : LaunchReport
	{
		LaunchErrorReport(int launchCmd, DWORD launchFlags, LONG gameInternalID, int systemConfigIndex,
			const TCHAR *errorMessage) :
			LaunchReport(launchCmd, launchFlags, gameInternalID, systemConfigIndex),
			errorMessage(errorMessage)
		{}

		const TCHAR *errorMessage;
	};

	// Game Over report, for PFVMsgGameOver
	struct GameOverReport : LaunchReport
	{
		GameOverReport(int launchCmd, DWORD launchFlags, LONG gameInternalID, int systemConfigIndex, INT64 runTime_ms) :
			LaunchReport(launchCmd, launchFlags, gameInternalID, systemConfigIndex),
			runTime_ms(runTime_ms)
		{}

		INT64 runTime_ms;
	};

	// Capture Done report, for PFVMsgCaptureDone
	struct CaptureDoneReport
	{
		CaptureDoneReport(LONG gameInternalID, bool ok, bool cancel,
			int overallStatusMsgId, CapturingErrorHandler &statusList,
			int nMediaItemsAttempted, int nMediaItemsOk) :
			gameId(gameInternalID), 
			ok(ok),
			cancel(cancel),
			overallStatusMsgId(overallStatusMsgId),
			statusList(statusList),
			nMediaItemsAttempted(nMediaItemsAttempted),
			nMediaItemsOk(nMediaItemsOk)
		{ }

		// internal ID of the game we're capturing
		LONG gameId;

		// overall capture success/failure status
		bool ok;

		// the operation was cancelled by the user
		bool cancel;

		// message ID for overall status
		int overallStatusMsgId;

		// capture message list
		CapturingErrorHandler &statusList;

		// number of media items attempted/succeeded during this operation
		int nMediaItemsAttempted;
		int nMediaItemsOk;
	};

	// Get the string resource ID for the name of the Manual Go button
	// for capture.  This returns one of the IDS_CAPSTAT_BTN_xxx resource
	// identifiers, according to the current setting.
	int GetCaptureManualGoButtonNameResId() const;

	// The startup video has ended in one of the windows.  Check for overall
	// startup video completion.
	void OnEndExtStartupVideo();

protected:
	// destruction - called internally when the reference count reaches zero
	~PlayfieldView();

	// ConfigManager::Subscriber implementation
	virtual void OnConfigPreSave() override;
	virtual void OnConfigPostSave(bool succeeded) override;
	virtual void OnConfigReload() override { OnConfigChange(); }

	// font options
	struct FontPref
	{
		FontPref(PlayfieldView *pfv, int defaultPtSize, const TCHAR *defaultFamily = nullptr, int defaultWeight = 400) :
			pfv(pfv),
			defaultPtSize(defaultPtSize), 
			defaultFamily(defaultFamily), 
			defaultWeight(defaultWeight)
		{ }

		PlayfieldView *pfv;

		// font description
		TSTRING family;
		int ptSize = 0;
		int weight = 0;

		// Defaults for the font.  defaultName can be null, in which case the
		// global DefaultFontFamily preference is used.
		const TCHAR *defaultFamily;
		int defaultPtSize;
		int defaultWeight;

		// Parse a font option string.  If the string doesn't match the
		// standard format, we'll apply defaults if useDefault is true,
		// otherwise we'll leave the font settings unchanged.
		void Parse(const TCHAR *text, bool useDefaults = true);

		// parse the config setting; applies defaults automatically if
		// the config variable is missing or isn't formatted correctly
		void ParseConfig(const TCHAR *varname);

		// get the font from this descriptor, creating the cached font if needed
		Gdiplus::Font *Get();

		operator Gdiplus::Font*() { return Get(); }
		Gdiplus::Font* operator->() { return Get(); }

		// cached font
		std::unique_ptr<Gdiplus::Font> font;
	};
	TSTRING defaultFontFamily;                // default font family for all fonts
	FontPref popupTitleFont{ this, 48 };      // title font for popups
	FontPref popupFont{ this, 24 };           // base text font for popup dialogs
	FontPref popupSmallerFont{ this, 20 };    // small font for popups
	FontPref popupDetailFont{ this, 18 };     // detail font for popups
	FontPref mediaDetailFont{ this, 12 };     // line items in media file listings
	FontPref wheelFont{ this, 80 };           // wheel titles (in lieu of icons)
	FontPref menuFont{ this, 42 };            // base font for menus
	FontPref menuHeaderFont{ this, 36 };      // font for menu header text
	FontPref statusFont{ this, 36 };          // status line font
	FontPref highScoreFont{ this, 24 };       // high score list font
	FontPref infoBoxFont{ this, 28 };         // info box main text
	FontPref infoBoxTitleFont{ this, 38 };    // info box title font
	FontPref infoBoxDetailFont{ this, 16 };   // info box fine print
	FontPref creditsFont{ this, 42 };         // credits message font

	// name of my startup video
	virtual const TCHAR *StartupVideoName() const override { return _T("Startup Video"); }

	// is a startup video playing?
	bool startupVideoPlaying = false;

	// If a startup video is playing, cancel it and return true.
	// Returns false if no startup video is playing.
	bool CancelStartupVideo();

	// Timer handler for the startup video fadeout
	void UpdateStartupVideoFade();

	// Figure the pixel width of the window layout in terms of the normalized
	// height of 1920 pixels.
	int NormalizedWidth()
	{
		if (szLayout.cy == 0)
			return 1080;
		else
			return static_cast<int>(1920.0f *
				(static_cast<float>(szLayout.cx) / static_cast<float>(szLayout.cy)));
	}

	// InputManager::RawInputReceiver implementation
	virtual bool OnRawInputEvent(UINT rawInputCode, RAWINPUT *raw, DWORD dwSize) override;

	// initialize the window
	virtual bool InitWin() override;

	// Update the sprite drawing list
	virtual void UpdateDrawingList() override;

	// Scale sprites that vary by window size
	virtual void ScaleSprites() override;

	// idle event handler
	virtual void OnIdleEvent() override;

	// Show the initial UI.  This sets up the status lines, loads
	// media for the initially selected game, sets up timers for
	// dynamic UI elements, and optionally brings up the about box.
	void ShowInitialUI(bool showAboutBox);

	// window creation
	virtual bool OnCreate(CREATESTRUCT *cs) override;

	// process a command
	virtual bool OnCommand(int cmd, int source, HWND hwndControl) override;

	// Internal OnCommand processing.  The main OnCommand passes the command event
	// to Javascript, which can prevent the system action.  This carries out the
	// command once Javascript has had its say.
	bool OnCommandImpl(int cmd, int source, HWND hwndControl);

	// process a timer
	virtual bool OnTimer(WPARAM timer, LPARAM callback) override;
		
	// private window class messages (WM_USER to WM_APP-1)
	virtual bool OnUserMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

	// private application message (WM_APP to 0xBFFF)
	virtual bool OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

	// context menu display
	virtual void ShowContextMenu(POINT pt) override;

	// JoystickManager::JoystickEventReceiver implementation
	virtual bool OnJoystickButtonChange(
		JoystickManager::PhysicalJoystick *js, 
		int button, bool pressed, bool foreground) override;
	virtual void OnJoystickAdded(
		JoystickManager::PhysicalJoystick *js, 
		bool logicalIsNew) override;

	// set internal variables according to the config settings
	void OnConfigChange();

	// is the settings dialog open?
	bool settingsDialogOpen;

	// initialize the status lines
	void InitStatusLines();

	// shut down the system - ask for confirmation through the menu system,
	// then actually do it
	void AskPowerOff();
	void PowerOff();

	// Timer IDs
	static const int animTimerID = 101;			  // animation timer
	static const int pfTimerID = 102;			  // playfield update timer
	static const int startupTimerID = 103;		  // startup initialization timer
	static const int infoBoxFadeTimerID = 104;    // info box fade timer
	static const int infoBoxSyncTimerID = 105;    // info box update timer
	static const int statusLineTimerID = 106;     // status line update
	static const int killGameTimerID = 107;		  // kill-game request pending
	static const int jsRepeatTimerID = 108;		  // joystick button auto-repeat timer
	static const int kbRepeatTimerID = 109;       // keyboard auto-repeat timer
	static const int attractModeTimerID = 110;    // attract mode timer
	static const int dofPulseTimerID = 111;		  // DOF signal pulse timer
	static const int attractModeStatusLineTimerID = 112;   // attract mode status line timer
	static const int creditsDispTimerID = 113;	  // number of credits display overlay timer
	static const int gameTimeoutTimerID = 114;    // game inactivity timeout timer
	static const int endSplashTimerID = 115;      // remove the "splash screen"
	static const int restoreDOFAndDMDTimerID = 116; // restore DOF and DMD access after a game terminates
	static const int dofReadyTimerID = 117;       // check if DOF is ready after post-game reinit
	static const int cleanupTimerID = 118;        // periodic cleanup tasks
	static const int mediaDropTimerID = 119;      // media drop continuation timer
	static const int autoDismissMsgTimerID = 120; // auto dismiss a message dialog
	static const int batchCaptureCancelTimerID = 121;  // batch capture cancel button pushed
	static const int javascriptTimerID = 122;     // javascript scheduled tasks
	static const int fullRefreshTimerID = 123;    // full UI refresh (filters, selection, status text)
	static const int overlayFadeoutTimerID = 124; // fading out the video overlay for removal
	static const int audioFadeoutTimerID = 125;   // fading out audio tracks
	static const int startupVideoFadeTimerID = 126; // fading out the startup video

	// update the selection to match the game list
	void UpdateSelection();

	// Is the given game a real game?  Returns true if the game is
	// non-null and isn't the special "No Game" entry in the game list.
	static bool IsGameValid(const GameListItem *game);

	// Load the incoming playfield media.  This starts an asynchronous
	// thread that loads the new sprite.
	void LoadIncomingPlayfieldMedia(GameListItem *game);

	// Finish loading a new playfield.  This is called when the sprite
	// loader thread finishes.  This is called on the main UI thread,
	// so we don't have to do anything special for thread safety.
	void IncomingPlayfieldMediaDone(VideoSprite *sprite);

	// Load a wheel image
	Sprite *LoadWheelImage(const GameListItem *game);

	// Set a wheel image position.  'n' is the wheel image slot
	// relative to the current selection.  'rot' is the additional
	// rotation for animation.
	void SetWheelImagePos(Sprite *image, int n, float rot);

	// switch to the nth game from the current position
	void SwitchToGame(int n, bool fast, bool byUserCommand);

	// about box
	void ShowAboutBox();

	// Show help.  If a section name is provided, it's the base name
	// of the section, without a path or .html extension.
	void ShowHelp(const TCHAR *section = _T("PinballY"));

	// open a file or program via ShellExecute
	void ShellExec(const TCHAR *file, const TCHAR *params = _T(""));

	// commands on the current game
	void PlayGame(int cmd, DWORD launchFlags, int systemIndex = -1);
	void ShowFlyer(int pageNumber = 0);
	void ShowGameInfo();
	void ShowInstructionCard(int cardNumber = 0);
	void RateGame();
	void ShowHighScores();

	// does at least one instruction card image exist for a game?
	bool InstructionCardExists(GameListItem *game);

	// play a specific game with a specific system
	void PlayGame(int cmd, DWORD launchFlags, GameListItem *game, GameSystem *system, 
		const std::list<std::pair<CSTRING, TSTRING>> *overrides = nullptr);

	// Launch the next queued game
	void LaunchQueuedGame();

	// Game inactivity timeout
	void OnGameTimeout();

	// Reset the game inactivity timer.  We call this to reset the
	// timer any time we observe a user input event (mouse, keyboard,
	// joystick) or when the running game state changes (such as
	// switching from "loading" to "running" state).
	void ResetGameTimeout();

	// Game inactivity timeout, in milliseconds.  Zero disables the
	// timeout.
	DWORD gameTimeout;

	// Last keyboard/joystick event time.  We keep track of the last
	// event times, even when we're in the background, via the raw
	// input mechanism.  We use this to detect activity in a running
	// game, for the purposes of the game timeout timer.
	DWORD lastInputEventTime;

	// Last PlayGame() command and launch flags
	int lastPlayGameCmd;
	DWORD lastPlayGameLaunchFlags;

	// remove the instructions card from other windows
	void RemoveInstructionsCard();

	// has the high score system finished initializing yet?
	bool hiScoreSysReady = false;

	// High score request object.  This is an abstract interface that
	// can be implemented to do extra work on receiving high score results.
	class HighScoresReadyCallback
	{
	public:
		HighScoresReadyCallback(LONG gameID) : gameID(gameID) { }
		virtual ~HighScoresReadyCallback() { }

		virtual void Ready(bool success, const WCHAR *source) = 0;

		// the game for the request
		LONG gameID;
	};

	// queue of completion notification objects
	std::list<std::unique_ptr<HighScoresReadyCallback>> highScoresReadyList;

	// process the ready list for a given game
	void OnHighScoresReady(LONG gameID, bool success, const WCHAR *source);

	// Initiate a high score request for the current game.  This sends
	// the request to PINemHi.exe asynchronously; the result will be
	// provided via a notification message when the program finishes.
	void RequestHighScores(GameListItem *game, bool notifyJavascript);

	// high score request context
	class HighScoreRequestContext : public HighScores::NotifyContext
	{
	public:
		HighScoreRequestContext(bool notifyJavascript) : 
			notifyJavascript(notifyJavascript)
		{
		}

		// should javascript be notified when the results are received?
		bool notifyJavascript;
	};

	// receive a high score data update from the HighScores object
	void ReceiveHighScores(const HighScores::NotifyInfo *ni);

	// apply received high scores
	void ApplyHighScores(GameListItem *game, const TCHAR *scores);

	// apply the high scores in the game's highScores list
	void ApplyHighScores(GameListItem *game, bool hadScores);

	// Working rating for the current game.  This is the value shown
	// in the Rate Game dialog, which isn't committed to the database
	// until the player presses Select.
	float workingRating;

	// Update the RateGame dialog with the current working rating
	void UpdateRateGameDialog();

	// Stars image
	std::unique_ptr<Gdiplus::Image> stars;

	// Draw the rating stars image
	void DrawStars(Gdiplus::Graphics &g, float x, float y, float rating);

	// Get a text message with the number of stars
	TSTRING StarsAsText(float rating);

	// Get a play time as text
	TSTRING PlayTimeAsText(int seconds);

	// adjust the current game rating - used for the Next/Previous
	// keys when the Rate Game dialog is showing
	void AdjustRating(float delta);

	// Show the game audio volume dialog
	void ShowAudioVolumeDialog();
	void UpdateAudioVolumeDialog();

	// adjust the working audio volume - used for Next/Previous keys while in the dialog
	void AdjustWorkingAudioVolume(int delta);

	// apply the working audio volume level to all active media in all windows
	void ApplyWorkingAudioVolume();

	// Working audio volume for the current game, as shown in the volume
	// adjustment dialog.  This isn't committed until the user presses
	// Select.
	int workingAudioVolume;

	// show a filter submenu
	void ShowFilterSubMenu(int cmd, const TCHAR *group, const WCHAR *menuID);
	
	// Show a recency filter menu.  We use this separate function to build
	// these menus, since they have some additional internal structure beyond
	// the normal flat list of filters in a regular filter submenu.
	void ShowRecencyFilterMenu(const TCHAR *incGroup, const TCHAR *excGroup, const TCHAR *neverGroup,
		const WCHAR *menuID, int idStrWithin, int idStrNotWithin);

	// update the animation
	void UpdateAnimation();

	// sync the playfield to the current selection in the game list
	enum SyncPlayfieldMode
	{
		SyncByTimer,
		SyncEndGame,
		SyncDelMedia
	};
	void SyncPlayfield(SyncPlayfieldMode mode);

	// show the operator menu
	void ShowOperatorMenu();

	// show the game setup menu
	void ShowGameSetupMenu();

	// set categories for the current game
	void ShowGameCategoriesMenu(GameCategory *curSelection = nullptr, bool reshow = false);

	// show the game setup dialog
	void EditGameInfo();

	// Rename media files for a change to a game's metadata.  To get the list of
	// files that need to be renamed, use GameList::UpdateMediaName(), which
	// figures the new media name and locates any affected files.  Returns true
	// if all renames succeed, false on error.  Any errors are logged to the 
	// provided error handler.
	bool ApplyGameChangesRenameMediaFiles(
		GameListItem *game,
		const std::list<std::pair<TSTRING, TSTRING>> &mediaRenameList,
		ErrorHandler &eh);

	// Apply changes made to a game's in-memory records to the database.
	void ApplyGameChangesToDatabase(GameListItem *game);

	// delete the game details
	void DelGameInfo(bool confirmed = false);

	// toggle a given category in the edit list
	void ToggleCategoryInEditList(int cmd);

	// save category updates
	void SaveCategoryEdits();

	// edit category names
	void EditCategories();

	// Category editing list.  When a game category menu is active, this
	// contains a list of the categories currently assigned to the game.
	// We commit this back to the game's category list when we select
	// the Save command in the menu.
	std::unique_ptr<std::list<const GameCategory*>> categoryEditList;

	// Show the initial capture window layout prompt
	void CaptureLayoutPrompt(int cmd, bool reshow);

	// Original capture command.  On ID_CAPTURE_LAYOUT_OK, we proceed to
	// the next step according to which command was used to start the process.
	int origCaptureCmd;

	// show the media capture setup menu for the current game
	void CaptureMediaSetup();

	// Initialize the capture list.  If a game is specified, we'll set
	// the initial state of each item to "Keep Existing" if the game has
	// a media file of that type, or to "Capture" if not.  If no game
	// is specified, we'll set all to "Capture".
	void InitCaptureList(const GameListItem *game);

	// begin media capture using the menu selections
	void CaptureMediaGo();

	// process a capture done report from the launch thread
	void OnCaptureDone(const CaptureDoneReport *report);

	// Media capture list.  This represents the items selected for
	// screen-shot capture in the menu UI.
	struct CaptureItem
	{
		CaptureItem(int cmd, const MediaType &mediaType, D3DView *win, bool exists, int mode, bool batchReplace) :
			cmd(cmd),
			mediaType(mediaType),
			win(win),
			exists(exists),
			mode(mode),
			batchReplace(batchReplace)
		{
		}

		// menu command ID
		int cmd;

		// Type descriptor for this item
		const MediaType &mediaType;

		// associated view window
		D3DView *win;

		// Capture mode for this item, as an IDS_CAPTURE_xxx string:
		//
		// IDS_CAPTURE_KEEP -> not capturing (keep existing item)
		// IDS_CAPTURE_SKIP -> not capturing (no existing item)
		// IDS_CAPTURE_CAPTURE -> capture
		// IDS_CAPTURE_SILENT -> capture without audio
		// IDS_CAPTURE_WITH_AUDIO -> capture with audio
		//
		int mode;

		// Batch mode for this item: keep or replace.  For batch
		// captures, 'mode' above is never KEEP, because the setting
		// applies to the whole list of games and hence can only tell
		// us the mode for new items.  We have to represent the 
		// disposition of existing items separately.
		bool batchReplace;

		// Is there an existing media item for this object?
		bool exists;
	};
	std::list<CaptureItem> captureList;

	// Prior capture modes.  This keeps track of the disposition the user
	// selected for each media type for the last capture run, so that we 
	// can set the same dispositions initially on the new run.  The 'int'
	// at each mapped item is the IDS_CAPTURE_xxx code selected for the
	// corresponding media type in the last capture.
	std::unordered_map<const MediaType*, int> lastCaptureModes;

	// Prior batch capture 'replace' mode
	std::unordered_map<const MediaType*, bool> lastBatchCaptureReplace;

	// Save/restore the last capture mode information
	void SaveLastCaptureModes();
	void RestoreLastCaptureModes();

	// Startup delay time for the current item, in seconds
	int captureStartupDelay;

	// adjusted startup delay, in the adjustment dialog
	int adjustedCaptureStartupDelay;

	// Capture menu mode
	enum CaptureMenuMode
	{
		NA,              // invalid/not applicable
		Single,          // single game capture
		Batch1,          // batch capture phase 1 - type selection
		Batch2           // batch capture phase 2 - disposition of existing items
	}
	captureMenuMode;

	// Display/update the capture setup menu.  The menu includes
	// checkbox items for all of the available media types, and
	// these operate in "dialog" mode, meaning that selecting them
	// inverts the checkmark status in situ without closing the
	// menu.  Hence we have to update the menu in place any time
	// a checkmark item is selected, which is why this method is
	// for "display/update" rather than just "display".  An update
	// operation is really the same as the initial display, except
	// that the update skips the opening animation and just draws
	// a new version of the menu.
	//
	// The current status of the checkmark items for the media
	// type is stored in 'captureSpec' above, so that has to be
	// initialized before this is called.  'captureExist' also
	// has to be initialized, since we use it to indicate if each
	// items has an existing file.  This routine merely draws the
	// menu with the current settings in those structs.
	//
	// When updating, 'mode' is ignored, as we always keep the
	// same mode that was used to create the menu initially when
	// updating.
	//
	void DisplayCaptureMenu(bool updating = false, int selectedCmd = -1, 
		CaptureMenuMode mode = CaptureMenuMode::NA);

	// Estimate the capture time required for the given game.  The
	// game isn't needed for single capture mode, as the capture
	// information is all contained in the media list; for batch
	// captures, we calculate the time according to the existence
	// of media for this specific game.  Returns the time estimate
	// in seconds.
	int EstimateCaptureTime(GameListItem *game = nullptr);

	// Format a capture time estimate to a printable string
	static TSTRINGEx FormatCaptureTimeEstimate(int t);

	// Advance a capture item to the next state
	void AdvanceCaptureItemState(int cmd);

	// Show/update the capture startup delay dialog
	void ShowCaptureDelayDialog(bool update);

	// Get the drop area list for a given media file
	virtual bool BuildDropAreaList(const TCHAR *filename) override;

	// Media drop list
	struct MediaDropItem
	{
		MediaDropItem(const TCHAR *filename, int zipIndex, 
			const TCHAR *impliedGameName, const TCHAR *destFile,
			const MediaType *mediaType, bool exists) :
			filename(filename), 
			zipIndex(zipIndex), 
			impliedGameName(impliedGameName),
			destFile(destFile),
			mediaType(mediaType), 
			exists(exists),
			status(exists ? 
				(zipIndex == -1 ? IDS_MEDIA_DROP_REPLACE : IDS_MEDIA_DROP_KEEP) : 
				IDS_MEDIA_DROP_ADD),
			cmd(0)
		{
		}

		// Filename (with path).  For an item in a ZIP file, this is
		// the ZIP file path.
		TSTRING filename;

		// Index of the item in a ZIP file, or -1 for a media file
		// dropped directly.
		int zipIndex;

		// Is this from a media pack?
		bool IsFromMediaPack() const { return zipIndex >= 0; }
		
		// Implied game name.  This is the game name implied by the
		// filename, as interpreted through the naming conventions for
		// the source:
		//
		// - For dropped ZIP files that appear to be in HyperPin Media
		//   Pack format, each file in the ZIP should be named in the
		//   format "Prefix/Media Type Dir/Title (Manuf Year).ext".
		//   So the last path element (minus extension) should be the
		//   name of the game, in the "Title (Manuf Year)" format.
		//
		//   One exception: for indexed media types, there might be
		//   a numeric suffix in the name, separated from the rest by
		//   a space: "Title (Manuf Year) Num.ext".
		//
		//   The prefix part seems to be pretty random, judging by the
		//   Media Pack files on vpforums.org.  I'd guess that HyperPin
		//   and PinballX each have their own ad hoc list of patterns
		//   they accept, but for simplicitly we just ignore the prefix,
		//   as it doesn't contain any meaningful information anyway 
		//   (apart, perhaps, from some weak heuristic validation that
		//   this really is a Media Pack file).  Note that the prefix
		//   should generally be fixed within a file, EXCEPT that there
		//   will be one prefix for "per system" media types (such as
		//   playfield and backglass images) and another for "generic"
		//   media types (instruction cards and flyers).  But we don't
		//   enforce this.
		//
		// - For directly dropped media files (.JPG files and the like),
		//   there's no useful convention to apply from HyperPin or 
		//   PinballX.  They'd expect the name of every media file for 
		//   a given to have the same name (modulo extension), "Title 
		//   (Manuf Year).ext".  (Numeric suffixes might also be used
		//   for indexed types.)  The actual media types are determined
		//   by the directory location rather than the name.  This is
		//   inconvenient for users managing media files separately,
		//   though; they might prefer to use one of the following
		//   formats (showing concrete examples for clarity):
		//
		//       .../Xenon (Bally 1980)/Backglass Image.jpg
		//       .../Xenon (Bally 1990) Backglass Image.jpg
		//       .../Xenon (Bally 1990) - Backglass Image.jpg
		//       .../Backglass Image - Xenon (Bally 1990).jpg
		//
		//   So we'll try parsing the path to see if one of those
		//   formats matches.
		//
		// If we can't find a suitable implied name, we'll leave this
		// blank.
		//
		TSTRING impliedGameName;

		// Destination file, with path
		TSTRING destFile;

		// Media type for the item
		const MediaType *mediaType;

		// is there an existing media item of this type?
		bool exists;

		// menu command
		int cmd;

		// current menu status for this item:
		//
		//   IDS_MEDIA_DROP_ADD      - add a new item
		//   IDS_MEDIA_DROP_REPLACE  - replace existing item
		//   IDS_MEDIA_DROP_SKIP     - skip new item
		//   IDS_MEDIA_DROP_KEEP     - keep existing item
		int status;
	};
	std::list<MediaDropItem> dropList;

	// target game for the current media drop
	GameListItem *mediaDropTargetGame;

	// Media drop phase 2: show the selection menu.  This is broken out
	// from the initial drop processing, because that can be interrupted
	// by a confirmation prompt if the game name looks wrong in the 
	// dropped files.
	void MediaDropPhase2();

	// Display the "add dropped media" menu
	void DisplayDropMediaMenu(bool updating, int selectedCmd);

	// invert the state of a media item in the drop menu
	void InvertMediaDropState(int cmd);

	// Add the media from the drop list.  This is invoked when the
	// user clicks the "go" option from the drop confirmation menu.
	void MediaDropGo();

	// Check to see if we can add media to the game.  This checks to
	// see if the game's manufacturer, system, and year are configured.
	// If so, it simply returns true to indicate that media can be
	// added.  If not, it displays a menu asking the user to configure
	// the game now, and returns false to indicate that we can't add
	// media yet.
	bool CanAddMedia(GameListItem *game);

	// show the "find media online" menu
	void ShowMediaSearchMenu();

	// Batch capture menu steps:
	//
	// Step 1 = select games
	//
	// Step 2 = select media types
	//
	// Step 3 = select disposition for existing media
	//
	// Step 4 = review and confirmation
	//
	void BatchCaptureStep1();
	void BatchCaptureStep2(int cmd);
	void BatchCaptureStep3();
	void BatchCaptureStep4();

	// Batch capture go - queue the game list and launch the first game
	void BatchCaptureGo();

	// Batch capture continue - launch the next queued game
	void BatchCaptureNextGame();

	// view the batch capture file list
	void BatchCaptureView();

	// Batch capture view image.  When we open the batch capture view,
	// we render the capture list to an off-screen bitmap.  This might
	// exceed the available window space, so when we display it, we
	// might only display a portion of the bitmap.  The user can then
	// scroll within the bitmap using the flipper buttons.  We keep
	// the bitmap through the popup lifetime for quick scrolling; we
	// just have to create a new D3D texture on each scroll event
	// showing the new scroll window.
	struct
	{
		DIBitmap dib;                                // as a DIB
		std::unique_ptr<Gdiplus::Bitmap> gpbmp;      // as a Gdiplus bitmap
	} batchViewBitmap;

	// current scrolling offset in the bitmap
	int batchViewScrollY;

	// update the batch capture view, showing it at the new scroll offset
	void UpdateBatchCaptureView();

	// Begin/end batch capture mode.  We call these to bracket a batch
	// capture operation, to allow for setting up special state during
	// the batch operation and restoring normal conditions when done.
	void EnterBatchCapture();
	void ExitBatchCapture();

	// Batch capture state
	struct BatchCaptureMode
	{
		BatchCaptureMode() : 
			active(false)
		{
		}

		void Enter()
		{
			active = true;
			nGamesPlanned = 0;
			nGamesAttempted = 0;
			nGamesOk = 0;
			nMediaItemsPlanned = 0;
			nMediaItemsAttempted = 0;
			nMediaItemsOk = 0;
			cancelPending = cancel = false;
		}

		void Exit()
		{
			active = false;
		}

		// batch capture mode is active
		bool active;

		// Cancel command is pending.  We require pressing cancel
		// twice to cancel the batch.
		bool cancelPending;

		// Batch cancelled.
		bool cancel;

		// number of games planned, attemped, and succeeded during this batch
		int nGamesPlanned;
		int nGamesAttempted;
		int nGamesOk;

		// number of media items planned, attemped, and succeeded
		int nMediaItemsPlanned;
		int nMediaItemsAttempted;
		int nMediaItemsOk;
		
	} batchCaptureMode;

	// enumerate the games selected by the current batch capture command
	void EnumBatchCaptureGames(std::function<void(GameListItem *)> f);

	// batch capture command (ID_BATCH_CAPTURE_ALL, ID_BATCH_CAPTURE_FILTER,
	// ID_BATCH_CAPTURE_MARKED)
	int batchCaptureCmd;

	// launch a Web browser to search for game media
	void LaunchMediaSearch();

	// Show media files for the current game.  'dir' is:
	//
	//  0   - do the initial display
	//  +1  - select the next item
	//  -1  - select the previous item
	//  +2  - page down (go to next folder)
	//  -2  - page up (go to previous folder)
	//
	void ShowMediaFiles(int dir);

	// media file dialog state
	struct ShowMediaState
	{
		ShowMediaState() : sel(-1), command(CloseDialog) { }

		// Current item selection index in dialog.  -1 means that
		// the "close" button is selected, otherwise this is an
		// index in the folder/file list.
		int sel;

		// current folder/file selection in the dialog
		TSTRING file;

		// Command to perform when current button is pressed
		enum Command
		{
			SelectItem,     // select the current item
			CloseDialog,	// close the dialog
			Return,			// return to selection
			DelFile,	 	// delete file
			ShowFile,       // show file in Explorer
			OpenFolder		// open folder in Explorer

		} command;

		void OnSelectItem()
		{
			command = sel < 0 ? CloseDialog : SelectItem;
		}

		void OnCloseDialog()
		{
			ResetDialog();
		}

		void ResetDialog()
		{
			sel = -1;
			command = CloseDialog;
		}

	} showMedia;

	// execute the currently selected media item command
	void DoMediaListCommand(bool &closePopup);

	// 'exit' key when media list is showing
	void ShowMediaFilesExit();

	// delete the current media file
	void DelMediaFile();

	// hide/show the current game
	void ToggleHideGame();

	// Enable/disable the status line
	void EnableStatusLine();
	void DisableStatusLine();

	// Status line background.  This is displayed at the bottom of the
	// screen as the background for status text messages.
	RefPtr<Sprite> statusLineBkg;

	// Status line messages.  The message text comes from the config
	// file, so the content and number of messages can vary.  The
	// source text is fixed at load time, but it can contain macros
	// that change according to the current game selection and game 
	// filter, so we keep the source and display text separately. 
	// Each time we're about to rotate in a new message, we check
	// the display text against the current version of the display
	// text, and if it differs, we replace the sprite.  This lets
	// us reuse sprites for as long as they're valid, while still
	// updating them as needed.
	struct StatusLine;
	struct StatusItem
	{
		StatusItem(const TCHAR *srcText) : srcText(srcText) { }

		// Update the item's sprite if necessary
		void Update(PlayfieldView *pfv, StatusLine *sl, float y);

		// determine if an update is needed
		bool NeedsUpdate(PlayfieldView *pfv);

		// expand macros in my text
		TSTRING ExpandText(PlayfieldView *pfv);

		TSTRING srcText;         // source text, which might contain {xxx} macros
		TSTRING dispText;        // display text, with macros expanded
		RefPtr<Sprite> sprite;   // sprite

		// Is this item temporary?  A temporary item is removed from the
		// list after being displayed once.
		bool isTemp = false;
	};

	// Status line 
	struct StatusLine
	{
		StatusLine() : curItem(items.end()), dispTime(2000), y(0), height(75), phase(DispPhase) { }

		// initialize
		void Init(PlayfieldView *pfv,
			int yOfs, int fadeSlide, int idleSlide,
			const TCHAR *cfgVar, int defaultMessageResId);

		// handle a timer update
		void TimerUpdate(PlayfieldView *pfv);

		// Do an explicit update.  We call this whenever one of the data
		// sources for a status line display changes.  This can check the
		// expanded text to see if a new sprite needs to be generated.
		void OnSourceDataUpdate(PlayfieldView *pfv);

		// add my sprites to the window's D3D drawing list
		void AddSprites(std::list<Sprite*> &sprites);

		// reset 
		void Reset(PlayfieldView *pfv);

		// messages for this status line
		std::list<StatusItem> items;

		// current active item, as an index in the items vector
		std::list<StatusItem>::iterator curItem;

		// get the next item
		std::list<StatusItem>::iterator NextItem();

		// hide the current item
		void Hide()
		{
			if (curItem != items.end())
				curItem->sprite->alpha = 0.0f;
		}

		// The Javascript object representing this status line
		JsValueRef jsobj = JS_INVALID_REFERENCE;

		// Javascript methods
		JsValueRef JsGetText();
		int JsGetCur();
		void JsSetText(int index, TSTRING txt);
		void JsAdd(TSTRING txt, JsValueRef index);
		void JsRemove(int index);
		void JsShow(TSTRING txt);

		// Start time for current item display
		DWORD startTime;

		// Display time for this status line.  This is the time in
		// ticks between message changes.
		DWORD dispTime;

		// current display phase
		enum
		{
			FadeInPhase,    // fading in the new message
			DispPhase,      // displaying the current message
			FadeOutPhase    // fading out the outgoing message
		} phase;

		// height
		int height;

		// offset in normalized units from bottom of screen
		float y;

		// Horizontal slide distance while idle.  We'll slide by this 
		// distance (in normalized units) over the course of the
		// display time.
		float idleSlide;

		// Horizontal slide distance while fading
		float fadeSlide;
	};

	// Are the status line messages enabled?
	bool statusLineEnabled = true;

	// Upper and lower status lines
	StatusLine upperStatus;
	StatusLine lowerStatus;

	// Attract mode status line
	StatusLine attractModeStatus;

	// Update status line text.  This calls OnSourceDataUpdate()
	// for each status line, to make sure that the expanded status
	// line text is current.
	void UpdateAllStatusText();

	// Current and incoming playfield media.  The current playfield
	// is the main background when idle.  During animations, the current
	// playfield is the background layer, and the incoming playfield is
	// drawn in front of it with gradually increasing opacity.
	template<class SpriteType> struct GameMedia
	{
		GameMedia() : game(nullptr) { }

		void Clear()
		{
			game = nullptr;
			sprite = nullptr;
			audio = nullptr;
		}

		void ClearVideo()
		{
			game = nullptr;
			sprite = nullptr;
		}
		
		GameListItem *game;
		RefPtr<SpriteType> sprite;
		RefPtr<AudioVideoPlayer> audio;
	};
	GameMedia<VideoSprite> currentPlayfield, incomingPlayfield;
	
	// Should we maintain the playfield image aspect ratio or stretch
	// it to fit the window?
	bool stretchPlayfield = false;

	// asynchronous loader for the playfield sprite
	AsyncSpriteLoader playfieldLoader;

	// List of wheel sprites.  When idle, the wheel shows the current
	// game's wheel image in the middle, and two wheel images on each
	// side for the previous/next games in the list.  During game
	// switch animations, we add the next game on the incoming side.
	std::list<RefPtr<Sprite>> wheelImages;

	// Game info box.  This is a popup that appears when we're idling
	// with a game selected, showing the title and other metadata for
	// the active selection.  This box is automatically removed when
	// any other animation occurs, and comes back when we return to
	// idle state.
	GameMedia<Sprite> infoBox;

	// show a message on the running game overlay
	void ShowRunningGameMessage(const TCHAR *msg);

	// Running game overlay.  This is a separate layer that we bring
	// up in front of everything else when we launch a game.
	RefPtr<Sprite> runningGamePopup;

	// Internal ID of current running game
	LONG runningGameID;

	// Running game mode.  
	enum RunningGameMode
	{
		None,		// no game running
		Starting,	// waiting for startup
		Running,	// game is running
		Exiting		// game is exiting
	};
	RunningGameMode runningGameMode;

	// boxes, dialogs, etc.
	RefPtr<Sprite> popupSprite;

	// Popup type.  This indicates which type of item is currently
	// displayed in the modal popup box.
	enum PopupType
	{
		PopupNone,			       // no popup
		PopupFlyer,			       // game flyer
		PopupGameInfo,		       // game info
		PopupInstructions,         // game instruction card
		PopupAboutBox,		       // about box
		PopupErrorMessage,	       // error message alert
		PopupRateGame,		       // enter game rating "dialog"
		PopupHighScores,           // high scores list
		PopupCaptureDelay,         // capture delay dialog
		PopupMediaList,            // game media list dialog
		PopupBatchCapturePreview,  // batch capture preview
		PopupUserDefined,          // user-defined popup via Javascript
		PopupGameAudioVolume       // game media audio volume dialog
	} 
	popupType;

	// Popup name, for javascript purposes
	WSTRING popupName;

	// popup animation mode and start time
	enum
	{
		PopupAnimNone,		// no animation
		PopupAnimOpen,		// opening a popup
		PopupAnimClose		// closing a popup
	} popupAnimMode;
	DWORD popupAnimStartTime;

	// Start a popup animation.  If replaceTypes is not null, it's an array of
	// popup types that the new type can replace without any animation effects.
	// The array must be terminated with a final PopupNone entry.  If the array
	// pointer is null (or omitted), it defaults to the popup's own type.  The
	// name is only used for PopupUserDefined types.
	struct PopupDesc
	{
		PopupType type;
		const TCHAR *name = nullptr;
	};
	void StartPopupAnimation(PopupType popupType, const WCHAR *popupName, bool opening, const PopupDesc *replaceTypes = nullptr);

	// Adjust a sprite to our standard popup position.  This centers
	// the sprite in the top half of the screen, but leaves a minimum
	// margin above.
	void AdjustSpritePosition(Sprite *sprite);

	// close the current popup
	void ClosePopup();

	// Current flyer page, from 0.  Each game can have up to 8 pages
	// of flyers, stored as separate images in the media folder.  We
	// show one at a time; this keeps track of which one is up.
	int flyerPage;

	// Current instruction card page.  As with the flyer, a game can
	// have multiple instruction cards, but we only show one at a 
	// time, so we keep track of the current one here.
	int instCardPage;

	// Instruction card location, from the configuration
	TSTRING instCardLoc;

	// Are SWF files enabled for instruction cards?
	bool instCardEnableFlash = true;

	// Queued errors.  If an error occurs while we're showing an
	// error popup, we'll queue it for display after the current
	// error is dismissed.
	struct QueuedError
	{
		QueuedError(DWORD timeout, ErrorIconType iconType, 
			const TCHAR *groupMsg, const ErrorList *list)
			: timeout(timeout), iconType(iconType)
		{
			// set the group message, if present
			if (groupMsg != nullptr)
				this->groupMsg = groupMsg;

			// store the list, if present
			if (list != nullptr)
				this->list.Add(list);
		}

		DWORD timeout;
		ErrorIconType iconType;
		TSTRING groupMsg;
		SimpleErrorList list;
	};
	std::list<QueuedError> queuedErrors;

	// show the next queued error
	void ShowQueuedError();

	// separate popup for the credit count overlay, used when a
	// coin is inserted
	RefPtr<Sprite> creditsSprite;
	DWORD creditsStartTime;

	// update/remove the credits display
	void OnCreditsDispTimer();

	// Active audio clips.  Audio clips that we play back via
	// AudioVideoPlayer have to be kept as active object references
	// while playing, and released when playback finishes.  The
	// player notifies us at end of playback by sending the private
	// message AVPMsgEndOfPresentation to our window.  This map
	// keeps track of the active clips that need to be released
	// at end of playback.  Note that clips are keyed by cookie,
	// not object pointer, since the object pointer isn't safe to
	// use for asynchronous purposes like this due to the possibility
	// that C++ could reuse a memory address for a new object after
	// an existing object is deleted.
	struct ActiveAudio
	{
		// clip type
		enum ClipType
		{
			StartupAudio,   // startup audio track, played on program startup
			LaunchAudio     // launch audio clip, played when a game is launched
		};

		// Create.  Note that we assume the caller's reference on the
		// player, so if the caller is using a RefPtr, it should detach
		// its reference before passing it to us.
		ActiveAudio(AudioVideoPlayer *player, ClipType clipType, int pctVol) :
			player(player), clipType(clipType), volume(pctVol) { }

		// the player playing the clip
		RefPtr<AudioVideoPlayer> player;

		// base volume, as a percentage value (0..100)
		int volume;

		// fadeout volume
		float fade = 1.0f;

		// clip type
		ClipType clipType;
	};
	std::unordered_map<DWORD, ActiveAudio> activeAudio;

	// update audio fading - runs on the audio fadeout timer
	void UpdateAudioFadeout();

	// Menu item flags.  MenuStayOpen is mostly intended for use with
	// checkmarks or radio buttons, to allow making a series of item
	// selections before dismissing the menu.
	static const UINT MenuChecked = 0x0001;			// checkmark
	static const UINT MenuRadio = 0x0002;			// radio button checkmark
	static const UINT MenuSelected = 0x0004;		// initial menu selection
	static const UINT MenuHasSubmenu = 0x0008;      // menu opens a submenu
	static const UINT MenuStayOpen = 0x0010;        // menu stays open on selection
	
	// Menu item descriptor.  This is used to create a menu.
	struct MenuItemDesc
	{
		MenuItemDesc(const TCHAR *text, int cmd, UINT flags = 0) :
			text(text), 
			cmd(cmd), 
			selected((flags & MenuSelected) != 0), 
			checked((flags & MenuChecked) != 0),
			radioChecked((flags & MenuRadio) != 0),
			hasSubmenu((flags & MenuHasSubmenu) != 0),
			stayOpen((flags & MenuStayOpen) != 0)
		{ }

		// text label
		TSTRING text;

		// Command ID.  This command is executed via the normal
		// WM_COMMAND mechanism, as though selected from a regular
		// Windows menu.
		int cmd;

		// Is the menu initially selected?
		bool selected;

		// Is the menu item checkmarked?
		bool checked;

		// Is the menu item radio-button checkmarked?
		bool radioChecked;

		// Does this menu open a submenu?
		bool hasSubmenu;

		// Does this menu stay open on selection?
		bool stayOpen;
	};

	// Menu item.  This describes a menu item currently being 
	// displayed.
	struct MenuItem
	{
		MenuItem(int x, int y, int cmd, bool stayOpen) :
			x(x), y(y), cmd(cmd), stayOpen(stayOpen)
		{ }

		// pixel offset from top left of menu
		int x, y;

		// command ID
		int cmd;

		// stay open when selected
		bool stayOpen;
	};

	// Menu.  This describes the menu currently being displayed.
	struct Menu : public RefCounted
	{
		Menu(const WCHAR *id, DWORD flags);
		~Menu();

		// menu ID
		WSTRING id;

		// menu flags (SHOWMENU_xxx bit flags)
		DWORD flags;

		// menu items
		std::list<MenuItem> items;

		// original descriptor list for the menu
		std::list<MenuItemDesc> descs;

		// selected item
		std::list<MenuItem>::iterator selected;

		// select an item
		void Select(std::list<MenuItem>::iterator sel);

		// Is the menu paged?  This is true if the menu has page up/
		// page down command items to scroll through a list that's
		// too long to display all at once.
		bool paged;

		// Menu sprites: background, current highlight, menu items
		RefPtr<Sprite> sprBkg;
		RefPtr<Sprite> sprItems;
		RefPtr<Sprite> sprHilite;
	};

	// Current active menu
	RefPtr<Menu> curMenu;

	// Pending new menu.  If an old menu is showing when a new menu is
	// created, we store the new menu here while the exit animation for
	// the old menu finishes.
	RefPtr<Menu> newMenu;

	// Current menu's item descriptor list
	std::list<MenuItemDesc> curMenuDesc;

	// Current menu "page".  Some menus (e.g., the Categories menu)
	// can grow to arbitrary lengths and so might need to be broken
	// into pages.  This tells us which page we're currently showing
	// in such a menu.
	int menuPage;

	// Show a menu.  This creates the graphics resources for the menu
	// based on the list of item descriptors, and displays the menu.
	// If a menu is already showing, it's replaced by the new menu.
	void ShowMenu(const std::list<MenuItemDesc> &items, const WCHAR *id, DWORD flags, int pageno = 0);

	// Show the next/previous page in the current menu
	void MenuPageUpDown(int dir);

	// menu is an exit menu -> the Escape key can select items from it
	const DWORD SHOWMENU_IS_EXIT_MENU = 0x00000001;

	// skip menu animation
	const DWORD SHOWMENU_NO_ANIMATION = 0x00000002;

	// "Dialog box" style menu.  This is almost like a regular menu,
	// but is for situations where we need a prompt at the top to
	// explain what the menu is about.  The first item is a static
	// text message that we display as the prompt message.  We then
	// display the menu items below this as usual.  We use a wider
	// box than usual to accommodate the longer text of the prompt
	// message, and we allow the prompt message to wrap if necessary
	// to fit it.
	const DWORD SHOWMENU_DIALOG_STYLE = 0x00000004;

	// User menu.  Showing a user menu doesn't fire a menuopen event.
	const DWORD SHOWMENU_USER = 0x80000000;

	// Handle any cleanup tasks when the current menu is closing.
	// 'incomingMenu' is the new menu replacing the old menu, or
	// null if there's no new menu (i.e., we're just returning to
	// the wheel UI).  This doesn't actually do any of the work of
	// closing the menu in the UI; it just takes care of side 
	// effects of removing the menu.
	void OnCloseMenu(const std::list<MenuItemDesc> *incomingMenu);

	// Exit Key mode in the Exit menu.  If this is true, the Exit
	// key acts like the Select key within an Exit menu.  This is
	// configurable, since some people are bound to find this
	// treatment backwards (since the Exit key means Cancel in
	// every other menu).
	bool exitMenuExitKeyIsSelectKey;

	// move to the next/previous menu item
	void MenuNext(int dir);

	// menu animation mode
	enum MenuAnimMode
	{
		MenuAnimNone,			// idle
		MenuAnimOpen,			// opening a menu
		MenuAnimClose			// closing a menu
	} menuAnimMode;

	// menu animation start time
	DWORD menuAnimStartTime;

	// start a menu animation
	void StartMenuAnimation(bool fast);

	// Update menu animation for the given menu.  'progress' is a
	// value from 0.0f to 1.0f giving the point in time as a fraction
	// of the overall animation time for the operation.
	void UpdateMenuAnimation(Menu *menu, bool opening, float progress);

	// update popup animation
	void UpdatePopupAnimation(bool opening, float progress);

	// close any open menus and popups
	void CloseMenusAndPopups();

	// Immediately complete the current "menu closing" animation, if
	// one is in progress.  This is used when switching into the
	// background while a game is running, to ensure that the menu
	// is removed before display updates are frozen.
	void AccelerateCloseMenu();

	// wheel animation mode
	enum WheelAnimMode
	{
		WheelAnimNone,				// idle
		WheelAnimNormal,			// normal speed wheel motion
		WheelAnimFast				// fast wheel switch, when auto-repeating keys
	} wheelAnimMode;

	// Wheel animation start time
	DWORD wheelAnimStartTime;

	// start an animation sequence
	void StartWheelAnimation(bool fast);

	// Start the animation timer if necessary.  This checks to see if
	// the timer is running, and starts it if not.  In any case, we
	// fill in startTime with the current time, as the reference time
	// for the current animation.
	void StartAnimTimer();
	void StartAnimTimer(DWORD &startTime);

	// is the animation timer running?
	bool isAnimTimerRunning;

	// end the current animation sequence
	void EndAnimation();
	
	// info box common drawing
	void DrawInfoBoxCommon(const GameListItem *game, Gdiplus::Graphics &g,
		int width, int height, float margin, GPDrawString &gds);

	// Update the game info box.  This just sets a timer to do a
	// deferred update in a few moments, so that the info box reappears
	// when we're idle.
	void UpdateInfoBox();

	// Sync the info box.  If we're idle, this displays the info box.
	void SyncInfoBox();

	// Hide the info box.  We do this whenever the UI switches out of
	// idle mode - showing a menu, showing a popup, switching games, etc.
	void HideInfoBox();

	// update the info box animation
	void UpdateInfoBoxAnimation();

	// Info box display options
	struct
	{
		bool show;					// show/hide the whole box
		bool title;					// include the game title
		bool gameLogo;				// use the game logo in place of the title
		bool manuf;					// include the manufacturer name
		bool manufLogo;				// use the manufacturer logo in place of the name
		bool year;					// include the release year
		bool system;				// include the player system name
		bool systemLogo;			// use the sysetm logo in place of the name
		bool tableType;				// show the table type
		bool tableTypeAbbr;			// use the abbreviated table type
		bool tableFile;				// include the table file name
		bool rating;                // include the star rating

	} infoBoxOpts;

	// map of table type (SS, EM, ME) to full name; keyed by upper-case type abbreviation
	std::unordered_map<TSTRING, TSTRING> tableTypeNameMap;

	// get a maunfacturer/system logo file
	bool GetManufacturerLogo(TSTRING &file, const GameManufacturer *manuf, int year);
	bool GetSystemLogo(TSTRING &file, const GameSystem *system);

	// Load a manufacturer/system logo file.  These logos are stored
	// in our logo caches (below), so the caller must not delete them.
	bool LoadManufacturerLogo(Gdiplus::Image* &image, const GameManufacturer *manuf, int year);
	bool LoadSystemLogo(Gdiplus::Image* &image, const GameSystem *system);

	// Caches for the logos.  We cache these files as we find matches,
	// since it takes a little work to search the file system and load
	// the image data.
	std::unordered_map<TSTRING, std::unique_ptr<Gdiplus::Image>> manufacturerLogoMap;
	std::unordered_map<TSTRING, std::unique_ptr<Gdiplus::Image>> systemLogoMap;

	// Start a playfield crossfade.  This is a separate mode from the
	// other animations, as it can run in parallel.
	void StartPlayfieldCrossfade();

	// Incoming playfield load time.  This is
	DWORD incomingPlayfieldLoadTime;

	// Game info box fade start time
	DWORD infoBoxStartTime;

	// running game popup start time and animation mode
	DWORD runningGamePopupStartTime;
	enum
	{
		RunningGamePopupNone,
		RunningGamePopupOpen,
		RunningGamePopupClose
	} runningGamePopupMode;

	// Number of wheel positions we're moving in the game switch animation
	int animWheelDistance;

	// Game number of first game in wheel, relative to current game
	int animFirstInWheel;
	int animAddedToWheel;

	// Attract mode.  When there's no user input for a certain
	// length of time, we enter attract mode.  In attract mode,
	// we automatically change games every few seconds.  This
	// adds to the arcade ambiance for a pin cab.  It also
	// serves as a screen saver, since it changes the on-screen
	// graphics every few seconds.
	struct AttractMode
	{
		AttractMode()
		{
			active = false;
			enabled = true;
			t0 = GetTickCount();
			idleTime = 60000;
			switchTime = 5000;
			dofEventA = 1;
			dofEventB = 1;
			savePending = true;
			hideWheelImages = false;
		}

		// should we hide wheel images while in attract mode?
		bool hideWheelImages;

		// Are we in attract mode?
		bool active;

		// Is attract mode enabled?
		bool enabled;

		// Is a save pending?  We automatically save any uncommitted
		// changes to files (config, stats) after a certain amount of
		// idle time, or when entering attract mode.  The idea is to
		// defer file saves until the user won't notice the (slight)
		// time it takes; if the user hasn't interacted in a while,
		// there's a good change it'll be a while longer before they
		// do interact again, so a brief pause won't be noticeable.
		bool savePending;

		// Start time (system ticks) of last relevant event.  When
		// running normally, this is the time of the last user input
		// event.  During attract mode, it's the time of the last
		// game change.
		DWORD t0;

		// Idle time before entering attract mode, in millseconds
		DWORD idleTime;

		// Game switch interval when attract mode is running, in milliseconds
		DWORD switchTime;

		// Next DOF attract mode event IDs.  When attract mode is active,
		// we fire a series of named DOF events that the DOF config can
		// use to trigger lighting effects.  For flexibility in defining
		// animated effects in the DOF lighting, we provide several event
		// series of different lengths:
		//
		// PBYAttractA<N> is a series of 5 events at 1-second intervals,
		// with N running from 1 to 5: PBYAttract1, PBYAttract2, etc.
		// These can be used to trigger animated lighting effects on a
		// 5-second loop.
		//
		// PBYttractB<N> works the same way, firing at 1-second intervals,
		// but with a 60-second loop (so N runs from 1 to 60).  This can be 
		// used to trigger occasional effects on a longer cycle.
		//
		// PBYttractR<N> is a series of 5 events (N from 1 to 5) that fire
		// at random intervals.  Once per second, we randomly decide whether
		// to fire an event at all, and then randomly choose which of the 5
		// "R" events to fire.
		//
		// All of these sequences run concurrently.  The intention is that
		// the DOF config can use the "A" sequence for a basic background
		// sequence of blinking lights, and can fire occasional extra effects
		// on longer loops with the "B" sequence and/or the random "R" 
		// events.
		//
		// Note that attract mode also fires the following events related
		// to status changes:
		// 
		// PBYScreenSaverStart  - fires when attract mode starts
		// PBYScreenSaverQuit   - fires when attract mode ends
		// PBYScreenSaver       - ON (1) the whole time attract mode is active
		// PBYAttractWheelRight - fires when attract mode randomly switches games
		//
		int dofEventA;
		int dofEventB;

		// Enter attract mode now, regardless of the timer
		void StartAttractMode(PlayfieldView *pfv);

		// Exit attract mode
		void EndAttractMode(PlayfieldView *pfv);

		// Handle the attract mode timer event
		void OnTimer(PlayfieldView *pfv);

		// Reset the idle timer and turn off attract mode
		void Reset(PlayfieldView *pfv);

		// Handle a keystroke event.  This updates the last key event
		// time, and exits attract mode if it's active.
		void OnKeyEvent(PlayfieldView *pfv);

	} attractMode;

	// Receive notification of attract mode entry/exit.  The AttractMode
	// subobject calls these when the mode changes.
	void OnStartAttractMode();
	void OnEndAttractMode();

	// Play a button or event sound effect
	void PlayButtonSound(const TCHAR *effectName, float volume = 1.0f);

	// Get the context-sensitive button volume.  A few buttons use a
	// modified volume level in certain contexts.  In particular, the
	// next/prev buttons reflect the working audio volume when the
	// audio volume adjustment dialog is showing, to provide feedback
	// on the level while making adjustments.
	struct QueuedKey;
	float GetContextSensitiveButtonVolume(const QueuedKey &key) const;

	// Button sound effect volume, as a percentage of the system
	// master volume level (0..100)
	int buttonVolume;

	// Are button/event sound effects muted?
	bool muteButtons;

	// Are auto-repeat button/event sound effects muted?
	bool muteRepeatButtons;

	// DOF pulsed effect queue.  Some of the signals we send to DOF are
	// states, where we turn a named DOF item ON for as long as we're in
	// a particular UI state (e.g., showing a menu).  Other signals are
	// of the nature of events, where we want to send DOF a message that
	// something has happened, without leaving the effect ON beyond the
	// momentary trigger signal.  DOF itself doesn't have a concept of
	// events; it was designed around VPinMAME, which thinks in terms 
	// of the physical switches on pinball machines.  So DOF's notion
	// of an "event" is when a switch (or, in DOF terms, a named event)
	// is switched ON briefly and then switched back off.  But it's not
	// good enough to literally turn a DOF effect ON and immediately OFF
	// again, because, again, DOF doesn't think in terms of events, but
	// in terms of states, and it uses a polling model to check states.
	// So to create a pulse that DOF notices, we have to turn the effect
	// on and leave it on long enough for DOF's polling loop to see it.
	// 
	// This queue is for those sorts of events, where we have to pulse a
	// DOF effect briefly, but not *too* briefly.  We could more easily
	// turn an effect on, Sleep() for a few milliseconds, and then turn
	// it off, but that's sucky because it blocks the UI/rendering 
	// thread.  Instead, we use this queue.  Each time we need to pulse
	// a DOF effect, we push an ON/OFF event pair onto the tail of this
	// queue.  A WM_TIMER timer then pulls events off the queue and sends
	// them to DOF.  This lets us leave the effects on for long enough to
	// satisfy DOF without blocking the UI thread.
	struct QueuedDOFEffect
	{
		QueuedDOFEffect(const TCHAR *name, UINT8 val) : name(name), val(val) { }
		TSTRING name;	// effect name
		UINT8 val;		// value to set the effect to
	};
	std::list<QueuedDOFEffect> dofQueue;

	// Last DOF pulsed event time, as a GetTickCount64 value
	ULONGLONG lastDOFEventTime;

	// Queue a DOF ON/OFF pulse.   This queues an ON/OFF event pair
	// for the effect.
	void QueueDOFPulse(const TCHAR *name);

	// Queue a DOF event.  Pushes the event onto the end of the DOF
	// queue, and starts the timer if the queue was empty.
	void QueueDOFEvent(const TCHAR *name, UINT8 val);

	// Fire a DOF event immediately.
	void FireDOFEvent(const TCHAR *name, UINT8 val);

	// Handle the DOF queued event timer.  Pulls an event off the
	// front of the queue and sends it to DOF.  Stops the timer if 
	// the queue is empty after pulling this effect.
	void OnDOFTimer();
	
	// DOF initialization status from the last initialization attempt.
	// We suppress DOF initialization errors if the last attempt to
	// intialize DOF also failed.  This avoids showing the same error
	// messages over and over on a system where DOF isn't set up 
	// properly and will thus fail on every attempt to initialize.
	bool dofInitFailed = false;

	// DOF interaction
	class DOFIfc
	{
	public:
		DOFIfc();
		~DOFIfc();

		// Set the UI context:  wheel, menu, popup, etc.  This allows DOF
		// to set different effects depending on the main UI state.  The
		// state names are arbitrarily defined in the config tool database.
		void SetUIContext(const WCHAR *context) { SetContextItem(context, this->context); }

		// Receive notification that DOF is ready.  We pass all current
		// context states through to DOF.
		void OnDOFReady();

		// Sync the selected game
		void SyncSelectedGame();

		// Set the ROM.  The config tool database uses the ROM name to 
		// determine which table is currently selected in the front-end UI, 
		// so that it can set effects specific to each table (e.g., flipper
		// button RGB colors).
		void SetRomContext(const TCHAR *rom) 
			{ SetContextItem(rom != nullptr ? TCHARToWide(rom).c_str() : _T(""), this->rom); }

		// get the current ROM
		const WCHAR *GetRom() const { return rom.c_str(); }

		// Set a keyboard key effect.  This sets a DOF effect associated
		// with a key/button according to the key state.  The key event
		// handlers calls this on key down and key up events.
		void SetKeyEffectState(const TCHAR *effect, bool keyDown);

		// Turn off all keyboard key effects
		void KeyEffectsOff();

	protected:
		// Update a context item.  A "context item" is a variable that 
		// keeps track of an element of the current program context that's
		// associated with a DOF effect.  At any given time, a context
		// item has a single string value, which is the name of a DOF
		// effect that will be turned on as long as this context is in
		// effect. 
		//
		// The context item itself doesn't have a named DOF state; the
		// DOF states are the possible *values* of the context.
		//
		// For example, the "UI context" keeps track of where we are in
		// the UI presentation: "PBYWheel" when we're in the base mode
		// showing the wheel, "PBYMenu" when a menu is displayed,
		// "PBYScreenSaver" when in attract mode.  Whenever one of these
		// UI contexts is in effect, that named DOF effect is activated
		// (value = 1).  When we change to a new UI context, the named 
		// DOF effect for the previous state is deactivated (value = 0),
		// and the DOF effect for the new state is activated.
		void SetContextItem(const WCHAR *newVal, WSTRING &itemVar);

		// Current UI context
		WSTRING context;

		// Current ROM
		WSTRING rom;

		// Keyboard key effect states.  Each entry here is true
		// the key is down, false if it's up.  Multiple key effects
		// states can be activated at once, since multiple keys can
		// be held down at once.
		std::unordered_map<TSTRING, bool> keyEffectState;

	} dof;

	// PINemHi version number string, if available.  We try to
	// retrieve at startup by running "PINemHi -v" and capturing
	// the version string it returns.
	TSTRING pinEmHiVersion;

	// Is an ALT key mapped to a command?  If so, we'll suppress
	// the Alt key's normal function of activating a menu's
	// keyboard shortcut when used in combination with another
	// key (e.g., Alt+F to open the &File menu), as well as its 
	// function when used alone to enter keyboard menu navigation
	// mode.
	bool leftAltHasCommand;
	bool rightAltHasCommand;

	// Is the F10 key used for a command?  If so, we'll suppress
	// its normal handling (activates keyboard menu navigation).
	bool f10HasCommand;

	// Is the ALT key used as a modifier for any mouse commands?
	// If so, we'll suppress the normal behavior of the Alt key of
	// entering keyboard menu navigation mode.
	bool altHasMouseCommand;
	
	// Command handler function type
	typedef void (PlayfieldView::*KeyCommandFunc)(const QueuedKey &key);

	// Key press event modes.  This is basically a bit mask, where
	// 0x01 is set if the key is down, and 0x02 is set if it's a 
	// repeated key.  The idiom 'if (mode)...' can be used to treat 
	// the all key-down events (normal, auto-repeat, and background)
	// as equivalent.
	//
	// KeyBgDown is set if the key is pressed while we're running
	// in the background.  In this case the KeyDown and KeyRepeat
	// bits are NOT set, so that command handlers can distinguish
	// foreground and background events.  Most commands only apply
	// when we're in the foreground.
	enum KeyPressType
	{
		KeyUp = 0x00,					// Key Up event
		KeyDown = 0x01,					// first Key Down event
		KeyRepeat = 0x02 | 0x01,		// auto-repeat event
		KeyBgDown = 0x10,				// app-in-background Key Down event
		KeyBgRepeat = 0x20 | 0x10		// app-in-background auto-repeat event
	};

	// Raw Shift key state.  We track the state of the Shift keys in
	// the raw input handler, to deal with a VERY strange case that
	// happens with the numeric keypad in NumLock mode.  Consider
	// the following key presses, with NumLock ON:
	//
	//   Press and hold Left Shift
	//   Press and release keypad 4/left arrow
	//   Release Left Shift
	//    
	// If that middle key were *not* a keypad key, you'd get the
	// message sequence you'd expect: a key down for the shift, a
	// key down for the keypad 4, a key up for the keypad 4, and a
	// key up for the shift.  But for any of the numbered keypad
	// keys (but NOT the symbol keys - +-/* Enter), you get the
	// following truly bizarre sequence:
	//
	//    WM_KEYDOWN(VK_SHIFT)
	//    WM_KEYUP(VK_SHIFT)
	//    WM_KEYDOWN(VK_NUMPAD4)
	//    WM_KEYUP(VK_NUMPAD4)
	//    WM_KEYDOWN(VK_SHIFT)
	//    WM_KEYUP(VK_SHIFT)
	//
	// Yes, that's right: Windows synthetically releases the shift
	// key while the keypad key is being pressed.  At the RAW INPUT
	// level, though, there's no synthetic shift release, so this
	// isn't coming from the keyboard hardware, BIOS, or the KB HID
	// driver; it's coming from the/ Windows message translation.  
	//
	// But wait, it gets even weirder!  If you use the RIGHT shift
	// key, you get exactly the sequence above at the WM_KEYxxx 
	// message level, but the Raw Input sequence gets truly bizarre:
	//
	//    RSHIFT MAKE
	//    RSHIFT BREAK       <--- synthetically releases rshift...
	//    LSHIFT MAKE        <--- and substitutes a synthetic lshift!!!
	//    NUMPAD4 MAKE
	//    NUMPAD4 BREAK
	//    LSHIFT BREAK       <--- releases the synthetic lshift
	//    RSHIFT MAKE        <--- and restores the actual rshift state
	//    RSHIFT BREAK
	//
	// "WTF" doesn't begin to express it.  The synthetic left shifts
	// makes it abundantly clear that this is intentional.  And it's
	// coming from some really low-level system inside Windows.
	// There's clearly some kind of tortured logic at work here, but
	// it's like everything in Lovecraft - unfathomable to the human
	// mind, unless you want to risk insanity.  Okay, maybe not quite
	// that unfathomable.  The synthesized shifts must be there to
	// maintain internal consistency for anyone trying to monitor
	// the keyboard state.  The mystery is why it has to be this
	// weird fake key state rather than just reflecting the actual
	// key state.  My best guess is it's for compatibility with 
	// something long since obsolete (I'm imagining a 1987 Compaq 
	// model and Windows 2.0, maybe), and that it was invented by a
	// too-clever-by-half summer intern at Microsoft who was tasked
	// with solving said compatibility issue.  And now we're stuck 
	// with it until the heat death of the universe, because Windows
	// is the roach motel of compatibility hacks.
	//
	// Anyway... Why do I even care?  I discovered this little gem
	// because I was trying to get consistent translation of keypad
	// keys for the purposes of generating Javascript key event
	// parameters.  Keypad keys weren't working properly in NumLock
	// mode, and I traced the problem to the weirdness above.  What
	// I need to do the translation properly is an accurate Shift
	// state, so that I can determine if VK_NUMPAD4 is a "4" key
	// or an "ArrowLeft" key.  Solution: observe the shift key
	// transitions in the Raw Input handler, and use that rather
	// than GetKeyState(VK_SHIFT) to do the VK_NUMPADx translation.
	// (Yes, the people who did all this hackery for WM_KEYxxx
	// and Raw Input at least made sure that GetKeyState returns
	// consistent information: it will ALWAYS report that the
	// Shift keys are un-pressed when a WM_KEYDOWN(VK_NUMPADx)
	// occurs in NumLock mode.)  Thus these state bits.  We
	// manage them in the raw input handler and consume them in
	// the Javascript key event generator.  Note that the right
	// shift key will *still* have bad information about the 
	// actual hardware key state when processing a numpad key,
	// because of the weird synthetic BREAK/MAKE RSHIFT sequence
	// described above, but the compensating synthetic MAKE/BREAK
	// LSHIFT at least makes sure that *a* shift key is down,
	// even if it's the wrong one, and thankfully it makes no
	// difference for this case which key it is.
	//
	// Note that an easier workaround might have been to just
	// use GetAsyncKeyState(), since that presumably isn't
	// affected by all of this.  (I haven't actually checked,
	// though.)  But that's not quite a *correct* workaround,
	// because to do it right we really have to know the key
	// state at the moment of the event - the whole raison
	// d'etre for GetKeyState().  Using the instantaneous
	// key state runs a small risk of getting the wrong state
	// when the user is moving fast and the program is moving
	// slow.  In our case, there was no cost to using the more
	// accurate raw input stream, since we're monitoring raw
	// input anyway.  If you're trying to adapt this technique
	// to your own program, and you're not already using raw
	// input, GetAsyncKeyState() is probably good enough.
	struct RawShiftKeyState
	{
		RawShiftKeyState() 
		{
			// initialize with the live keyboard state
			left = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
			right = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
		}
		bool left;
		bool right;
	} rawShiftKeyState;

	// Keyboard command handlers by name.  We populate this at
	// construction, and use it to find the mapping from a command
	// by name to the handler function.  The config loader uses
	// this mapping to populate the command dispatch table.  We
	// index the commands by name to help reduce the chances of
	// something getting out of sync across versions or between
	// modules.
	//
	// For each command, we store the associated handler function,
	// and a list of the key/button assignments for the command.
	struct KeyCommand
	{
		KeyCommand(const TCHAR *name, KeyCommandFunc func) : name(name), func(func) { }

		// command name (a static const string, so we don't copy it)
		const TCHAR *name;

		// handler function
		KeyCommandFunc func;

		// list of associated keys
		std::list<InputManager::Button> keys;
	};
	std::unordered_map<TSTRING, KeyCommand> commandsByName;

	// "No Command" command
	static const KeyCommand NoCommand;

	// Mappings between our command names and the corresponding
	// menu command IDs.
	std::unordered_map<TSTRING, int> commandNameToMenuID;

	// Keyboard command dispatch table
	std::unordered_map<int, std::list<const KeyCommand*>> vkeyToCommand;

	// Capture "Manual Go" button mode.
	enum CaptureManualGoButton
	{
		Flippers,     // both flipper buttons
		MagnaSave,    // both Magna Save buttons
		Launch,       // launch button
		Info,         // information button
		Instructions  // instructions button
	};
	CaptureManualGoButton captureManualGoButton = CaptureManualGoButton::Flippers;

	// mapping array - config names/internal IDs/resource string IDs
	struct CaptureManualGoButtonMap
	{
		const TCHAR *configName;   // name in the configuration file
		CaptureManualGoButton id;  // internal ID
		int nameStrResId;          // resource ID for name string
	};
	static const CaptureManualGoButtonMap captureManualGoButtonMap[];

	// "Manual Go" button state for capture.  These bits keep track
	// of the states of the buttons assigned above for manually
	// starting and stopping capture operations.  For two-button
	// gestures, both buttons must be down to trigger the start or
	// stop; for single-button gestures, only the "left" button is
	// needed.
	bool manualGoLeftDown = false;
	bool manualGoRightDown = false;

	// Check for a "Manual Go" gesture.  This is called whenever one
	// of the Next/Prev buttons changes state.
	void CheckManualGo(bool &thisButtonDown, const QueuedKey &key);

	// add a command to the vkeyToCommand list
	void AddVkeyCommand(int vkey, const KeyCommand &cmd);

	// key event queue
	struct QueuedKey
	{
		QueuedKey() : hWndSrc(NULL), mode(KeyUp), cmd(&NoCommand), scripted(false) { }

		QueuedKey(HWND hWndSrc, KeyPressType mode, bool bg, bool scripted, const KeyCommand *cmd)
			: hWndSrc(hWndSrc), mode(mode), bg(bg), cmd(cmd), scripted(scripted) { }

		HWND hWndSrc;           // source window
		KeyPressType mode;      // key press mode
		bool bg;                // background mode
		bool scripted;          // originated from a script
		const KeyCommand *cmd;  // command
	};
	std::list<QueuedKey> keyQueue;

	// Add a key press to the queue and process it
	void ProcessKeyPress(HWND hwndSrc, KeyPressType mode, bool bg, bool scripted, std::list<const KeyCommand*> cmds);

	// Process the key queue.  On a keyboard event, we add the key
	// to the queue and call this routine; we also call it whenever
	// an animation ends.  If an animation is in progress, we defer
	// key processing until the animation ends.
	void ProcessKeyQueue();

	// Keyboard auto-repeat.  Rather than using the native Windows
	// auto-repeat feature, we implement our own using a timer.  
	//
	// Handling auto-repeat ourselves lets us make the behavior more 
	// uniform across keyboards.  Windows's native handling is 
	// inconsistent, since the auto-repeat is implemented in hardware 
	// on some keyboards and in the Windows device drivers for other
	// keyboards.
	//
	// Explicit handling also lets us improve the responsiveness of
	// the application.  The native Windows handling is to simply
	// queue key events asynchronously, regardless of the rate at
	// which the application is consuming them.  If a command takes
	// longer for us to process than the repeat interval, holding
	// down a key creates a backlog of repeat events that keeps
	// growing as long as the key is held down, which makes the app
	// feel sluggish.  Using a timer, we can throttle the repeat
	// rate to our actual consumption rate and avoid a backlog.
	struct KbAutoRepeat
	{
		KbAutoRepeat() { active = false; }

		bool active;				// auto-repeat is active
		int vkey;					// virtual key code we're repeating
		int vkeyOrig;               // vkey from original message, before extended key translation
		KeyPressType repeatMode;	// key press mode for repeats
	} kbAutoRepeat;

	// Joystick auto-repeat button.  This simulates the keyboard 
	// auto-repeat feature for joystick buttons.  The last button
	// pressed auto-repeats until released, or until another button
	// is pressed.
	struct JsAutoRepeat
	{
		JsAutoRepeat() { active = false; }

		bool active;				// auto-repeat is active
		int unit;					// logical joystick unit number
		int button;					// button number
		KeyPressType repeatMode;	// key press mode for repeats
	} jsAutoRepeat;

	// Start joystick auto repeat mode
	void JsAutoRepeatStart(int unit, int button, KeyPressType repeatMode);

	// joystick auto repeat timer handler
	void OnJsAutoRepeatTimer();

	// Start keyboard auto-repeat mode
	void KbAutoRepeatStart(int vkey, int vkeyOrig, KeyPressType repeatMode);

	// Keyboard auto repeat timer handler
	void OnKbAutoRepeatTimer();

	// Stop keyboard and joystick auto-repeat timers.  We call this
	// whenever a new joystick button or key press occurs, to stop
	// any previous auto-repeat.
	void StopAutoRepeat();

	// Joystick command dispatch table.  This maps keys generated
	// by JsCommandKey() to command handler function pointers.
	//
	// Note that this table contains no entries here with the
	// unit number encoded as -1, representing generic buttons 
	// that match any joystick.  Instead, for each command
	// assigned to a unit -1 button, we create a separate entry
	// for the same command in each actual joystick unit.  That
	// means we can find every command in one lookup, whether
	// assigned to a particular joystick or to any joysticks.
	//
	// Unordered_map takes slightly longer for a lookup operation
	// than a simple array does (such as we use for keyboard key 
	// dispatch above), but it's still very fast - about 120ns
	// per lookup on average on my 4th gen i7.  That's negligible
	// for a joystick event, so I don't think anything more complex
	// is justified.
	std::unordered_map<int, std::list<const KeyCommand*>> jsCommands;

	// add a command to the map
	void AddJsCommand(int unit, int button, const KeyCommand &cmd);

	// create a key for the jsCommands table
	static inline int JsCommandKey(int unit, int button) { return (unit << 8) | button; }

	// Carry out the Select command
	void DoSelect(bool usingExitKey);

	// Show menus
	void ShowMainMenu();
	void ShowExitMenu();

	// Basic handlers for Next/Previous commands.  These handle
	// the core action part separately from the key processing, so
	// they can be called to carry out the effect of a Next/Prev
	// command for non-UI actions, such as attract mode.
	void DoCmdNext(bool fast);
	void DoCmdPrev(bool fast);

	// Common coin slot handler
	void DoCoinCommon(const QueuedKey &key, int slot);

	// Set the credit balance, updating the stored config value
	void SetCredits(float val);

	// Reset the coin balance.  This converts the current coin balance
	// to credits and clears the coin balance.
	void ResetCoins();

	// Display the current credit balance
	void DisplayCredits();

	// Get the effective credits.  This calculates the total number of
	// credits available, including banked credits and credits that
	// can be converted from the current coin balance.
	float GetEffectiveCredits();

	// Current banked credit balance.  This counts credits that have 
	// been converted from coins, but not the credits from the current
	// coin balance.  We keep the coin balance separate while coins
	// are being inserted, since the number of credits purchased can
	// vary as more coins are inserted (e.g., the typical US pricing
	// where 50 cents buys you one credit and $1 buys three).
	float bankedCredits;

	// Maximum credit balance.  This is the maximum allowed for the
	// effective credits.  Zero means there's no maximum.
	float maxCredits;

	// Current coin balance.  This counts the value of coins inserted
	// in the coin slots since the last coin reset.  The coin balance
	// is reset when a game is launched or when we exit the program.
	// Coins are also deducted whenever the coin value reaches the
	// highest level in the pricing model list: at that point we
	// deduct the coin value for the last entry from the coin balance,
	// and add the corresponding credit value to the credit balance.
	float coinBalance;

	// Coin slot values.  This sets the monetary value of each coin
	// slot, in arbitrary user-defined currency units.
	float coinVal[4];

	// Pricing model.  This is a list, in ascending order, of the
	// monetary values needed to reach each credit level, and the
	// corresponding number of credits at that level.
	struct PricePoint
	{
		PricePoint(float price, float credits) : price(price), credits(credits) { }
		float price;	// coin value needed to reach this level, in coinVal units
		float credits;	// credits awarded at this level; can be fractional (1/2, 1/4, 3/4)
	};
	std::list<PricePoint> pricePoints;

	// Real DMD interface
	std::unique_ptr<RealDMD> realDMD;

	// get/set the DMD enabling status
	enum RealDMDStatus { RealDMDAuto, RealDMDEnable, RealDMDDisable };
	RealDMDStatus GetRealDMDStatus() const;
	void SetRealDMDStatus(RealDMDStatus stat);

	// Javascript object for the main window object
	JsValueRef jsMainWindow = JS_INVALID_REFERENCE;

	// Javascript objects for secondary window objects
	JsValueRef jsBackglassWindow = JS_INVALID_REFERENCE;
	JsValueRef jsDMDWindow = JS_INVALID_REFERENCE;
	JsValueRef jsTopperWindow = JS_INVALID_REFERENCE;
	JsValueRef jsInstCardWindow = JS_INVALID_REFERENCE;

	// Javascript object representing the game list
	JsValueRef jsGameList = JS_INVALID_REFERENCE;

	// console object
	JsValueRef jsConsole = JS_INVALID_REFERENCE;

	// logfile object
	JsValueRef jsLogfile = JS_INVALID_REFERENCE;

	// GameInfo class (prototype for game descriptors)
	JsValueRef jsGameInfo = JS_INVALID_REFERENCE;

	// GameSysInfo class (prototype for system descriptors)
	JsValueRef jsGameSysInfo = JS_INVALID_REFERENCE;

	// FilterInfo class (prototype for filter descriptors)
	JsValueRef jsFilterInfo = JS_INVALID_REFERENCE;

	// optionSettings object - represents the config manager
	JsValueRef jsOptionSettings = JS_INVALID_REFERENCE;

	// event objects
	JsValueRef jsCommandButtonDownEvent = JS_INVALID_REFERENCE;
	JsValueRef jsCommandButtonUpEvent = JS_INVALID_REFERENCE;
	JsValueRef jsCommandButtonBgDownEvent = JS_INVALID_REFERENCE;
	JsValueRef jsCommandButtonBgUpEvent = JS_INVALID_REFERENCE;
	JsValueRef jsKeyDownEvent = JS_INVALID_REFERENCE;
	JsValueRef jsKeyBgDownEvent = JS_INVALID_REFERENCE;
	JsValueRef jsKeyBgUpEvent = JS_INVALID_REFERENCE;
	JsValueRef jsKeyUpEvent = JS_INVALID_REFERENCE;
	JsValueRef jsJoystickButtonDownEvent = JS_INVALID_REFERENCE;
	JsValueRef jsJoystickButtonUpEvent = JS_INVALID_REFERENCE;
	JsValueRef jsJoystickButtonBgDownEvent = JS_INVALID_REFERENCE;
	JsValueRef jsJoystickButtonBgUpEvent = JS_INVALID_REFERENCE;
	JsValueRef jsCommandEvent = JS_INVALID_REFERENCE;
	JsValueRef jsMenuOpenEvent = JS_INVALID_REFERENCE;
	JsValueRef jsMenuCloseEvent = JS_INVALID_REFERENCE;
	JsValueRef jsPopupOpenEvent = JS_INVALID_REFERENCE;
	JsValueRef jsPopupCloseEvent = JS_INVALID_REFERENCE;
	JsValueRef jsAttractModeStartEvent = JS_INVALID_REFERENCE;
	JsValueRef jsAttractModeEndEvent = JS_INVALID_REFERENCE;
	JsValueRef jsWheelModeEvent = JS_INVALID_REFERENCE;
	JsValueRef jsGameSelectEvent = JS_INVALID_REFERENCE;
	JsValueRef jsPreLaunchEvent = JS_INVALID_REFERENCE;
	JsValueRef jsPostLaunchEvent = JS_INVALID_REFERENCE;
	JsValueRef jsLaunchErrorEvent = JS_INVALID_REFERENCE;
	JsValueRef jsRunBeforePreEvent = JS_INVALID_REFERENCE;
	JsValueRef jsRunBeforeEvent = JS_INVALID_REFERENCE;
	JsValueRef jsRunAfterEvent = JS_INVALID_REFERENCE;
	JsValueRef jsRunAfterPostEvent = JS_INVALID_REFERENCE;
	JsValueRef jsGameStartedEvent = JS_INVALID_REFERENCE;
	JsValueRef jsGameOverEvent = JS_INVALID_REFERENCE;
	JsValueRef jsSettingsReloadEvent = JS_INVALID_REFERENCE;
	JsValueRef jsSettingsPreSaveEvent = JS_INVALID_REFERENCE;
	JsValueRef jsSettingsPostSaveEvent = JS_INVALID_REFERENCE;
	JsValueRef jsFilterSelectEvent = JS_INVALID_REFERENCE;
	JsValueRef jsStatusLineEvent = JS_INVALID_REFERENCE;
	JsValueRef jsHighScoresRequestEvent = JS_INVALID_REFERENCE;
	JsValueRef jsHighScoresReadyEvent = JS_INVALID_REFERENCE;

	// Fire javascript events.  These return true if the caller should
	// proceed with the event, false if the script wanted to block the
	// event (via preventDefault() or similar).  Non-blockable events
	// use void returns to clarify that they're not used.  The default
	// is always to proceed with system handling; this applies if 
	// javscript isn't being used, or if anything fails trying to run
	// the script.
	bool FireKeyEvent(int vkey, bool down, bool repeat, bool bg);
	bool FireJoystickEvent(int unit, int button, bool down, bool repeat, bool bg);
	bool FireCommandButtonEvent(const QueuedKey &key);
	bool FireCommandEvent(int cmd);
	bool FireMenuEvent(bool open, Menu *menu, int pageno);
	bool FirePopupEvent(bool open, const WCHAR *id);
	bool FireAttractModeEvent(bool starting);
	void FireWheelModeEvent();
	void FireGameSelectEvent(GameListItem *game);
	bool FireFilterSelectEvent(GameListFilter *filter);
	void FireConfigEvent(JsValueRef type, ...);
	bool FireLaunchEvent(JsValueRef type, LONG gameId, int cmd, const TCHAR *errorMessage = nullptr);
	bool FireLaunchEvent(JsValueRef type, GameListItem *game, int cmd, const TCHAR *errorMessage = nullptr);
	bool FireLaunchEvent(JavascriptEngine::JsObj *overrides, JsValueRef type,
		GameListItem *game, int cmd, const TCHAR *errorMessage = nullptr);
	void FireStatusLineEvent(JsValueRef statusLineObj, const TSTRING &rawText, TSTRING &expandedText);
	bool FireHighScoresRequestEvent(GameListItem *game);
	void FireHighScoresReadyEvent(GameListItem *game, bool success, const TCHAR *source);

	// Current UI mode, for Javascript purposes
	enum JSUIMode
	{
		jsuiWheel,
		jsuiPopup,
		jsuiMenu,
		jsuiAttract,
		jsuiRun
	} jsuiMode = jsuiWheel;

	// Last game selection reported to Javascript
	LONG jsLastGameSelectReport = 0;

	// Update the javascript UI mode.  We call this when showing or hiding
	// popups and menus, and switching to and from run mode or attract mode.
	void UpdateJsUIMode();

	// Javascript alert() callback.  Shows a message in a popup message box, 
	// a la alert() in a Web browser.  As with browser alert(), the dialog
	// box is modal and blocks other UI activity while being displayed.
	void JsAlert(TSTRING msg);

	// Javascript message() callback.  Shows a message in the playfield
	// message box.  This doesn't block the UI; it simply queues the
	// message display and returns immediately.  The 'typ' argument is
	// an optional string giving the visual style of the displayed 
	// message popup: "error", "warning", or "info".
	void JsMessage(TSTRING msg, TSTRING style);

	// Javascript log() callback.  Writes a message to the log file.
	void JsLog(TSTRING msg);

	// Javascript OutputDebugString() callback.  Writes a message to
	// the C++ debugger console.  Mostly for system debugging purposes.
	void JsOutputDebugString(TSTRING msg);

	// Javascript setTimeout() callback
	double JsSetTimeout(JsValueRef func, double dt);

	// Javascript clearTimeout() callback
	void JsClearTimeout(double id);

	// Javascript setInterval() callback
	double JsSetInterval(JsValueRef func, double dt);

	// Javascript clearInterval() callback
	void JsClearInterval(double id);

	// Javascript console _log (low-level write routine that just
	// emits a message; the Javascript side classes are responsible
	// for higher level formatting features).
	void JsConsoleLog(TSTRING level, TSTRING message);

	// Javascript UI mode query
	JsValueRef JsGetUIMode();

	// Get the active UI window
	JsValueRef JsGetActiveWindow();

	// Carry out a menu command
	bool JsDoCommand(int cmd);

	// Carry out a button command
	void JsDoButtonCommand(WSTRING cmd, bool down, bool repeat);
	
	// Show a menu
	void JsShowMenu(WSTRING name, std::vector<JsValueRef> items, JavascriptEngine::JsObj options);

	// Show a popup
	void JsShowPopup(JavascriptEngine::JsObj contents);

	// Javascript drawing callback functions.  These are exposed on a "drawing
	// context" object, which is just for the sake of the js prototype to
	// collect the methods into a coherent namespace.
	JsValueRef jsDrawingContextProto = JS_INVALID_REFERENCE;
	void JsDrawDrawText(TSTRING text);
	void JsDrawSetFont(JsValueRef name, JsValueRef pointSize, JsValueRef weight);
	void JsDrawSetTextColor(int rgb);
	void JsDrawSetTextAlign(JsValueRef horz, JsValueRef vert);
	void JsDrawDrawImage(TSTRING filename, float x, float y, JsValueRef width, JsValueRef height);
	JsValueRef JsDrawGetImageSize(TSTRING filename);
	void JsDrawSetTextArea(float x, float y, float width, float height);
	void JsDrawSetTextOrigin(float x, float y);
	JsValueRef JsDrawGetTextOrigin();
	JsValueRef JsDrawMeasureText(TSTRING text);
	void JsDrawFillRect(float x, float y, float width, float height, int rgb);
	void JsDrawFrameRect(float x, float y, float width, float height, float frameWidth, int rgb);
	JsValueRef JsDrawGetSize();

	// Internal drawing context object.  This is a static object that's
	// only valid for the duration of the js drawing callback.
	struct JsDrawingContext
	{
		JsDrawingContext(Gdiplus::Graphics &g, float width, float height, float borderWidth) :
			g(g), 
			width(width), 
			height(height),
			borderWidth(static_cast<float>(borderWidth)),
			textOrigin(borderWidth, borderWidth),
			textBounds(borderWidth, borderWidth, width - borderWidth*2.0f, height - borderWidth*2.0f)
		{
		}

		// graphics context
		Gdiplus::Graphics &g;

		// drawing area dimensions, including the border space
		float width;
		float height;

		// Border width.  All of the coordinates that Javascript sees are
		// relative to the interior content area, whereas our own drawing
		// surface includes the border, so we need to adjust the js coords
		// by the border size.
		float borderWidth;

		// text color
		Gdiplus::Color textColor{ 0xff, 0xff, 0xff };

		// Current font specs
		TSTRING fontName = _T("Tahoma");
		int fontPtSize = 24;
		int fontWeight = 400;

		// Create a font from the current specs if we don't already have one
		void InitFont();

		// currently selected drawing objects
		std::unique_ptr<Gdiplus::Font> font;
		std::unique_ptr<Gdiplus::Brush> textBrush;

		// text wrapping boundaries
		Gdiplus::RectF textBounds;

		// current text output position
		Gdiplus::PointF textOrigin;

		// current text alignment
		Gdiplus::StringAlignment textAlignHorz = Gdiplus::StringAlignmentNear;
		Gdiplus::StringAlignment textAlignVert = Gdiplus::StringAlignmentNear;
	};
	std::unique_ptr<JsDrawingContext> jsDC;

	// Enter/exit attract mode via javascript
	void JsStartAttractMode() { attractMode.StartAttractMode(this); }
	void JsEndAttractMode() { attractMode.EndAttractMode(this); }

	// Generic status line method handler.  Gets the target status line from
	// the 'this' object and invokes the method M with the Javascript arguments.
	template<typename MethodType, MethodType M, typename R, typename... Args>
	R JsStatusLineMethod(JsValueRef self, Args... args);

	// Launch a game
	void JsPlayGame(JsValueRef val, JsValueRef options);

	// Get game information
	JsValueRef JsGetGameInfo(WSTRING id);

	// Update game information
	JsValueRef JsGameInfoUpdate(JsValueRef self, JsValueRef desc, JsValueRef opts);

	// Rename media files, using the renamedMediaFiles list from a GameInfo.update() call
	JsValueRef JsGameInfoRenameMediaFiles(JsValueRef self, JsValueRef renameArray);

	// GameInfo.update() and GameInfo.renameMediaFiles() helper
	void JsRenameMediaHelper(
		GameListItem *game,
		const std::list<std::pair<TSTRING, TSTRING>> &renameList, 
		JavascriptEngine::JsObj &retobj);

	// delete a game's metadata
	void JsGameInfoErase(JsValueRef);

	// GameInfo prototype getter methods
	template<typename T> static T JsGameInfoGetter(T (*func)(GameListItem*), JsValueRef self);
	template<typename T> static JsValueRef JsGameInfoStatsGetter(T (*func)(GameListItem*), JsValueRef self);

	// Populate a GameInfo getter
	template<typename T> 
	bool AddGameInfoGetter(const CHAR *propName, T (*func)(GameListItem*), ErrorHandler &eh);

	// Populate a GameInfo getter that accesses the game stats database
	template<typename T>
	bool AddGameInfoStatsGetter(const CHAR *propName, T (*func)(GameListItem*), ErrorHandler &eh);

	// expand a game system variable
	WSTRING JsExpandSysVar(JsValueRef self, WSTRING str, JsValueRef game);
	
	// build a GameSysInfo object
	JsValueRef BuildGameSysInfo(GameSystem *system);

	// GameSysInfo prototype getters
	template<typename T> static T JsGameSysInfoGetter(T (*func)(GameSystem*), JsValueRef self);
	template<typename T> bool AddGameSysInfoGetter(const CHAR *propName, T (*func)(GameSystem*), ErrorHandler &eh);

	// internal game info object builder
	JsValueRef BuildJsGameInfo(GameListItem *game);

	// GameInfo methods
	JsValueRef JsGetHighScores(JsValueRef self);
	void JsSetHighScores(JsValueRef self, JsValueRef scores);
	JsValueRef JsResolveGameFile(JsValueRef self);
	JsValueRef JsResolveMedia(JsValueRef self, WSTRING type, bool mustExist);
	JsValueRef JsResolveROM(JsValueRef self);

	// Javascript GameCategory access
	JsValueRef JsGetAllCategories();
	void JsCreateCategory(WSTRING name);
	void JsRenameCategory(WSTRING oldName, WSTRING newName);
	void JsDeleteCategory(WSTRING name);

	// get the total number of games/nth game in the overall game list/array of games
	int JsGetGameCount();
	JsValueRef JsGetGame(int n);
	JsValueRef JsGetAllGames();

	// get the number of games on the wheel/nth game on the wheel/array of wheel games
	int JsGetWheelCount();
	JsValueRef JsGetWheelGame(int n);
	JsValueRef JsGetAllWheelGames();

	// get/set/refresh the current game list filter
	JsValueRef JsGetCurFilter();
	void JsSetCurFilter(WSTRING id);
	void JsRefreshFilter();

	// get all filters
	JsValueRef JsGetAllFilters();

	// create a user filter
	int JsCreateFilter(JavascriptEngine::JsObj desc);

	// get information on a filter
	JsValueRef JsGetFilterInfo(WSTRING id);

	// build a FilterInfo object
	JsValueRef BuildFilterInfo(const WSTRING &id);
	JsValueRef BuildFilterInfo(GameListFilter *filter);

	// FilterInfo methods
	JsValueRef JsFilterInfoGetGames(JsValueRef self);
	bool JsFilterInfoTestGame(JsValueRef self, JsValueRef game);

	// create/remove a metafilter
	int JsCreateMetaFilter(JavascriptEngine::JsObj desc);
	void JsRemoveMetaFilter(int id);

	// play a button sound
	void JsPlayButtonSound(WSTRING name);

	// get the command assigned to a key or joystick button
	JsValueRef JsGetKeyCommand(JavascriptEngine::JsObj desc);

	// Javacript configuration access
	template<typename T, T (*conv)(const TCHAR*)>
	JsValueRef JsSettingsGet(WSTRING varname, JsValueRef jsdefval);

	void JsSettingsSet(WSTRING varname, JsValueRef val);
	bool JsSettingsIsDirty();
	bool JsSettingsSave();
	void JsSettingsReload();

	// User-defined Javascript game filters.  We keep the master list of
	// these here, rather than in the GameList object with the other
	// filters, because the GameList instance reflects the loaded game
	// database and thus has to be re-created on each reload, which
	// happens whenever any settings change.  So we have to re-create
	// the live filter list from this master list on each reload.  The
	// list here has session duration.
	class JavascriptFilter : public GameListFilter
	{
	public:
		JavascriptFilter(JsValueRef func, const TSTRING &id, 
			const TSTRING &title, const TSTRING &menuTitle,
			const TSTRING &group, const TSTRING &sortKey,
			bool includeHidden, bool includeUnconfigured,
			JsValueRef before, JsValueRef after) :
			GameListFilter(group.c_str(), sortKey.c_str()),
			func(func), 
			id(_T("User.") + id), 
			title(title), 
			menuTitle(menuTitle),
			group(group), 
			includeHidden(includeHidden), 
			includeUnconfigured(includeUnconfigured),
			beforeScanFunc(before), 
			afterScanFunc(after)
		{
			// maintain an external reference on the function
			JsAddRef(func, nullptr);
		}

		~JavascriptFilter() { JsRelease(func, nullptr); }

		virtual TSTRING GetFilterId() const override { return id; }
		virtual const TCHAR *GetFilterTitle() const override { return title.c_str(); }
		virtual const TCHAR *GetMenuTitle() const override { return menuTitle.c_str(); }

		virtual void BeforeScan() override;
		virtual void AfterScan() override;
		virtual bool Include(GameListItem *game) override;

		virtual bool IncludeHidden() const override { return includeHidden; }
		virtual bool IncludeUnconfigured() const override { return includeUnconfigured; }

		// the Javascript function implementing the filter, BeforeScan, and
		// AfterScan methods
		JsValueRef func;
		JsValueRef beforeScanFunc;
		JsValueRef afterScanFunc;

		// filter ID
		TSTRING id;

		// filter title, menu title
		TSTRING title;
		TSTRING menuTitle;

		// filter group; determines which menu the filter is shown in
		TSTRING group;

		// include hidden/unconfigured games in the raw filter set
		bool includeHidden;
		bool includeUnconfigured;
	};

	// all user-defined filters, by ID
	std::unordered_map<TSTRING, JavascriptFilter> javascriptFilters;

	// Javascript metafilter
	class JavascriptMetafilter : public MetaFilter
	{
	public:
		JavascriptMetafilter(JsValueRef before, JsValueRef select, JsValueRef after,
			int priority, bool includeExcluded) :
			MetaFilter(priority, includeExcluded),
			before(before),
			select(select),
			after(after)
		{
			JsAddRef(before, nullptr);
			JsAddRef(select, nullptr);
			JsAddRef(after, nullptr);
		}

		virtual ~JavascriptMetafilter()
		{
			JsRelease(before, nullptr);
			JsRelease(select, nullptr);
			JsRelease(after, nullptr);
		}

		// implementation of the virtual interface
		virtual void Before() override;
		virtual void After() override;
		virtual bool Include(GameListItem *game, bool include) override;

		// before/select/after Javascript functions
		JsValueRef before;
		JsValueRef select;
		JsValueRef after;

		// ID, for Javascript code to address the filter (e.g., for deletion)
		int id;
	};

	// all currently active user-defined metafilters
	std::list<std::unique_ptr<JavascriptMetafilter>> javascriptMetaFilters;

	// next available Javascript metafilter ID
	int nextMetaFilterId = 1;

	//
	// Button command handlers
	//

	void CmdNone(const QueuedKey &key) { }          // no command
	void CmdSelect(const QueuedKey &key);			// select menu item
	void CmdExit(const QueuedKey &key);				// exit menu level
	void CmdNext(const QueuedKey &key);				// next item
	void CmdPrev(const QueuedKey &key);				// previous item
	void CmdNextPage(const QueuedKey &key);			// next page/group
	void CmdPrevPage(const QueuedKey &key);			// previous page/group
	void CmdCoin1(const QueuedKey &key);			// insert coin in slot 1
	void CmdCoin2(const QueuedKey &key);			// insert coin in slot 2
	void CmdCoin3(const QueuedKey &key);			// insert coin in slot 3
	void CmdCoin4(const QueuedKey &key);			// insert coin in slot 4
	void CmdCoinDoor(const QueuedKey &key);			// coin door open/close
	void CmdService1(const QueuedKey &key);			// Service 1/Escape
	void CmdService2(const QueuedKey &key);			// Service 2/Down
	void CmdService3(const QueuedKey &key);			// Service 3/Up
	void CmdService4(const QueuedKey &key);			// Service 4/Enter
	void CmdFrameCounter(const QueuedKey &key);		// Toggle on-screen frame counter/performance stats
	void CmdFullScreen(const QueuedKey &key);		// Toggle full-screen mode
	void CmdSettings(const QueuedKey &key);			// Show settings dialog
	void CmdRotateMonitorCW(const QueuedKey &key);	// Rotate monitor by 90 degrees clockwise
	void CmdRotateMonitorCCW(const QueuedKey &key);	// Rotate monitor by 90 degrees counter-clockwise
	void CmdLaunch(const QueuedKey &key);           // Launch game
	void CmdExitGame(const QueuedKey &key);			// Exit running game
	void CmdPauseGame(const QueuedKey &key);        // Pause running game
	void CmdGameInfo(const QueuedKey &key);         // Show game info box
	void CmdInstCard(const QueuedKey &key);			// Show instruction card
};

