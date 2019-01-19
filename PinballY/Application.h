// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Main application class.  This is a container for app-wide
// global functions and data.

#pragma once
#include "GameList.h"
#include "PlayfieldWin.h"
#include "BackglassWin.h"
#include "InstCardWin.h"
#include "DMDWin.h"
#include "TopperWin.h"
#include "CaptureStatusWin.h"
#include "../Utilities/DateUtil.h"
#include "JavascriptEngine.h"

struct ConfigFileDesc;
class TextureShader;
class DMDShader;
class I420Shader;
class PinscapeDevice;
class HighScores;
class RefTableList;
struct MediaType;

class Application
{
public:
	~Application();

	// get the global singleton
	static Application *Get() { return inst; }
	
	// Main application entrypoint.  WinMain (the standard Windows
	// entrypoint function) simply calls this on startup.  This
	// creates the Application singleton, initializes, shows the
	// UI, and runs our main event loop.
	static int Main(HINSTANCE hInstance, LPTSTR lpCmdLine, int nCmdShow);

	// Explicitly reload the configuration.  This reloads the settings
	// file and rebuilds all game list data.
	bool ReloadConfig();

	// reload settings after a config change
	void OnConfigChange();

	// Save files.  This saves any in-memory changes to the configuration
	// file and the game statistics file.
	static void SaveFiles();

	// Initialize a dialog window position.  A dialog proc can call this
	// on receiving WM_INITDIALOG to set the dialog position to the last
	// saved position, or to initially position the dialog over a non-
	// rotated window.  On pin cabs, the main playfield window is often
	// shown with its contents rotated 90 or 270 degrees from the Windows
	// desktop orientation on its monitor.  Ideally, we'd just rotate the
	// dialog itself to match the content rotation, since that's the
	// user's desired orientation for content on that monitor, but 
	// unfortunately Windows doesn't have a mechanism for rotating the
	// controls within dialog windows.  The next best option for the user
	// is to reposition the dialog onto a different monitor where the
	// content isn't rotated, such as the backglass monitor.  We can help
	// out with this by making the dialog position's window stick across
	// invocations, so that the user doesn't have to keep moving it to a
	// workable location every time it opens.  We can also look for a 
	// non-rotated window and position it there initially, if we don't
	// have a user-defined saved location to restore.
	void InitDialogPos(HWND hDlg, const TCHAR *configVar);

	// save a dialog position for later restoration
	void SaveDialogPos(HWND hDlg, const TCHAR *configVar);

	// Application title, for display purposes (e.g., message box title)
	TSTRINGEx Title;

	// Update secondary windows for a change in the selected game.
	// This notifies the backglass and DMD windows when a new game
	// is selected in the playfield window.
	void SyncSelectedGame();

	// get the playfield window/view
	PlayfieldWin *GetPlayfieldWin() const { return playfieldWin; }
	PlayfieldView *GetPlayfieldView() const
		{ return playfieldWin != 0 ? (PlayfieldView *)playfieldWin->GetView() : 0; }

	// get the backglass window/view
	BackglassWin *GetBackglassWin() const { return backglassWin; }
	BackglassView *GetBackglassView() const
		{ return backglassWin != 0 ? (BackglassView *)backglassWin->GetView() : 0; }

	// get the DMD window/view
	DMDWin *GetDMDWin() const { return dmdWin; }
	DMDView *GetDMDView() const
		{ return dmdWin != 0 ? (DMDView *)dmdWin->GetView() : 0; }

	// get the topper window/view
	TopperWin *GetTopperWin() const { return topperWin; }
	TopperView *GetTopperView() const
		{ return topperWin != 0 ? (TopperView *)topperWin->GetView() : 0; }

	// get the instruction card window/view
	InstCardWin *GetInstCardWin() const { return instCardWin; }
	InstCardView *GetInstCardView() const
		{ return instCardWin != 0 ? (InstCardView *)instCardWin->GetView() : 0; }

	// enable/disable the secondary windows
	void EnableSecondaryWindows(bool enable);

	// Global shared shader instances
	std::unique_ptr<TextureShader> textureShader;
	std::unique_ptr<DMDShader> dmdShader;
	std::unique_ptr<I420Shader> i420Shader;

	// Show one of our application windows.  If the window is currently
	// hidden, we'll make it visible; if it's minimized, we'll restore it.
	// This is used to implement the "Show Xxx" menu commands.
	void ShowWindow(FrameWin *win);

	// In-UI error handler.  If possible, this displays errors via a
	// graphics overlay in the main playfield window.  This is more
	// harmonious with the video-game look of the UI than a system
	// message box.  If it's not possible to display the graphical
	// style of error box, we'll fall back on a system message box
	// to make sure that the message is displayed somehow.
	class InUiErrorHandler : public ErrorHandler
	{
	public:
		virtual void Display(ErrorIconType iconType, const TCHAR *msg) override;
		virtual void GroupError(ErrorIconType icon, const TCHAR *summary, const class ErrorList &geh) override;
	};

	// Async in-UI error handler.  This uses the same display 
	// mechanism as InUiErrorHandler, but displays errors via window
	// messages to the main playfield window, which makes it usable
	// in asynchronous background threads.  It's also fine to use this
	// in the main UI thread, but unnecessary and less efficient, as we
	// can call methods in the playfield window object directly from
	// the main thread.
	class AsyncErrorHandler : public ErrorHandler
	{
	public:
		virtual void Display(ErrorIconType iconType, const TCHAR *msg) override;
		virtual void GroupError(ErrorIconType icon, const TCHAR *summary, const class ErrorList &geh) override;
		virtual void SysError(const TCHAR *friendly, const TCHAR *details) override;
	};

	// Clear media from all windows.  This releases any sprites
	// showing table media items.  This can be called before 
	// attempting a file operation (such as delete or rename)
	// on media items for the current table, to ensure that none
	// of the files are being held open by active media players.
	void ClearMedia();

	// Capture item descriptor
	struct LaunchCaptureItem
	{
		LaunchCaptureItem(D3DView *win, const MediaType &mediaType, bool videoWithAudio) :
			win(win), mediaType(mediaType), videoWithAudio(videoWithAudio) { }

		// source window
		D3DView *win;

		// type of the media to capture
		const MediaType &mediaType;

		// For video items, are we capturing audio?  This is ignored 
		// for image media types.
		bool videoWithAudio;
	};

	// Launch a game.  Returns true if the game was launched, false
	// if not.
	//
	// 'cmd' specifies the launch mode.  This can be any command code,
	// since Javascript can specify arbitrary commands when launching
	// via scripting, but the built-in code uses the following:
	//
	//    ID_PLAY_GAME -> normal launch to play the game
	//    ID_CAPTURE_GO -> launch for media capture
	//
	// If capture mode is indicated in the launch flags, the caller must
	// supply the list of capture items.  This can be null if capture
	// isn't specified.  captureStartupDelay is the initial startup delay
	// (the time to wait after launching the child process for the game) in 
	// seconds.
	//
	bool Launch(int cmd, DWORD launchFlags, GameListItem *game, GameSystem *system, 
		const std::list<LaunchCaptureItem> *captureList, int captureStartupDelay, ErrorHandler &eh);

	// Batch capture information.  When queueing a game for batch
	// capture, this provides information on the entry's place in the
	// overall capture process, for status reporting during the
	// capture.
	struct BatchCaptureInfo
	{
		BatchCaptureInfo() : nCurGame(0), nGames(0), remainingTime(0), totalTime(0) { }

		BatchCaptureInfo(int nCurGame, int nGames, int remainingTime, int totalTime) :
			nCurGame(nCurGame), 
			nGames(nGames), 
			remainingTime(remainingTime), 
			totalTime(totalTime)
		{}

		// index of current game in overall batch (starting at 0)
		int nCurGame;

		// number of games in overall batch
		int nGames;

		// estimated remaining time (in seconds) in the overall batch, 
		// starting with this game and including its estimated capture time
		int remainingTime;

		// total estimated capture time (in seconds) for the entire batch
		int totalTime;
	};

	// Launch flags
	struct LaunchFlags
	{
		static const DWORD ConsumeCredit = 0x00000001;    // consume a credit
		static const DWORD UpdateStats   = 0x00000002;    // update play count & time stats
		static const DWORD Capturing     = 0x00010000;    // capture media on this launch

		// standard flags for PLAY mode
		static const DWORD StdPlayFlags = ConsumeCredit | UpdateStats;

		// standard flags for CAPTURE mode
		static const DWORD StdCaptureFlags = Capturing;
	};

	// Queue a game for launch
	void QueueLaunch(int cmd, DWORD launchFlags, GameListItem *game, GameSystem *system,
		const std::list<LaunchCaptureItem> *captureList, int captureStartupDelay,
		const BatchCaptureInfo *bci = nullptr);

	// apply substitution variables to a game system command line
	TSTRING ExpandGameSysVars(TSTRING &str, GameSystem *system, GameListItem *game);

	// clear the launch queue
	void ClearLaunchQueue() { queuedLaunches.clear(); }

	// Get the next queued game.  Fills in the info struct and returns
	// true if a game is queued; returns false if the queue is empty.
	// The contents of the queue aren't affected.
	struct QueuedGameInfo
	{
		int cmd;          // launch command (ID_PLAY_GAME, ID_CAPTURE_GO)
		LONG gameId;      // internal ID of the game
	};
	bool GetNextQueuedGame(QueuedGameInfo &info) const;

	// Set a launch parameter override for the next queued game.  This is
	// used by the Javascript "prelaunch" event to let event listeners
	// modify the launch parameters.
	void SetNextQueuedGameOverride(const CHAR *prop, const TSTRING &val);

	// Launch the next game in the queue
	bool LaunchNextQueuedGame(ErrorHandler &eh);

	// Remove the next game in the queue without launching it
	void RemoveNextQueuedGame();

	// are any games queued?
	bool IsGameQueuedForLaunch() const { return queuedLaunches.size() != 0; }

	// Kill the running game, if any
	void KillGame();

	// Resume the running game, bringing it to the foreground
	void ResumeGame();

	// Is a game running?
	bool IsGameRunning() const { return gameMonitor != nullptr && gameMonitor->IsGameRunning(); }

	// Is the game running in Admin mode?
	bool IsGameInAdminMode() const { return gameMonitor != nullptr && gameMonitor->IsAdminMode(); }

	// Try to steal focus from the runing game and set it to our window
	void StealFocusFromGame();

	// Signal the user button press to end a manual start/stop wait.
	// The main UI calls this when the user presses the button sequence
	// to start or stop a manual capture.
	void ManualCaptureGo();

	// Show or remove the batch capture cancel pending prompt.  The
	// playfield window calls this when processing the Exit Game 
	// command during a batch capture, to present a prompt in the 
	// status window asking if the user really wants to cancel the 
	// whole batch process.
	void BatchCaptureCancelPrompt(bool show);

	// Show a "cancellation in progress" prompt in the capture
	// status window
	void ShowCaptureCancel();

	// Begin/end running game mode.  The playfield view calls these
	// when games start and stop.  We manage the visibility of the
	// other windows accordingly.
	void BeginRunningGameMode();
	void EndRunningGameMode();

	// Clean up the game monitor thread
	void CleanGameMonitor();

	// Check the application foreground/background status.  Our 
	// top-level windows call this whenever their activation status
	// changes.  We'll check the overall application status and
	// update our internal static accordingly.
	void CheckForegroundStatus();

	// Is the application in the foreground?
	static inline bool IsInForeground() { return isInForeground; }

	// Get the first run time
	DateTime GetFirstRunTime() const { return firstRunTime; }

	// Hide unconfigured games?
	bool IsHideUnconfiguredGames() const { return hideUnconfiguredGames; }

	// Globally enable/disable videos
	void EnableVideos(bool enable);
	void ToggleEnableVideos() { EnableVideos(!enableVideos); }
	bool IsEnableVideo() const { return enableVideos; }

	// Mute videos.  This is the global setting for video muting,
	// independent of the Night Mode status.
	void MuteVideos(bool mute);
	void ToggleMuteVideos() { MuteVideos(!muteVideos); }
	bool IsMuteVideos() const { return muteVideos; }

	// Mute table audio
	void MuteTableAudio(bool mute);
	void ToggleMuteTableAudio() { MuteTableAudio(!muteTableAudio); }
	bool IsMuteTableAudio() const { return muteTableAudio; }

	// Update the video enabled status for active videos in all 
	// windows
	void UpdateEnableVideos();

	// Mute attract mode
	void MuteAttractMode(bool mute);
	void ToggleMuteAttractMode() { MuteAttractMode(!muteAttractMode); }
	bool IsMuteAttractMode() const { return muteAttractMode; }

	// Get the active muting status.  This takes into account the
	// global config settings and the current night mode status.
	bool IsMuteVideosNow() const;

	// Update the video muting status for all active videos, taking
	// into account the global config settings and the current Attract
	// Mode status.  We automatically call this internally on changing
	// any of the mute settings via our methods (MuteVideos(), etc).
	// This should also be called explicitly after entering or exiting
	// Attract Mode.
	void UpdateVideoMuting();

	// Update the Pinscape device list.  Returns true if any Pinscape
	// devices are currently active, false if not.
	bool UpdatePinscapeDeviceList();

	// Get the Pinscape Night Mode status.  The return value indicates
	// whether or not there are any Pinscape devices in the system:
	// true means there's at least one Pinscape device, false means
	// there aren't any devices.  If there are any Pinscape devices,
	// nightMode is set to true if any of them are in night mode, false
	// if not.
	bool GetPinscapeNightMode(bool &nightMode);

	// Set the Pinscape Night Mode status.  This sets all devices to
	// the new mode.
	void SetPinscapeNightMode(bool nightMode);

	// Toggle Pinscape Night Mode
	void TogglePinscapeNightMode();

	// Run an external command line.  If wait is true, we'll launch the
	// program and wait for it to exit; hProcess is ignored in this case,
	// as the program has already exited by the time we return.  If wait
	// is false, we'll launch the program and return immediately, filling
	// in hProcess (if provided) with the process handle.  Returns true
	// on success, false on failure.
	static bool RunCommand(const TCHAR *cmd, 
		ErrorHandler &eh, int friendlyErrorStringId,
		bool wait = true, HANDLE *phProcess = nullptr, DWORD *pPid = nullptr,
		UINT nShowCmd = SW_SHOW);

	// Process a WM_ACTIVATEAPP notification to one of our windows
	void OnActivateApp(BaseWin *win, bool activating, DWORD otherThreadId);

	// Is the Admin Host available?
	bool IsAdminHostAvailable() const { return adminHost.IsAvailable(); }

	// post a request to the Admin Host, with no expectation of a reply
	void PostAdminHostRequest(const TCHAR *const *request, size_t nItems);

	// send a request to the Admin Host
	bool SendAdminHostRequest(const TCHAR *const *request, size_t nItems, 
		std::vector<TSTRING> &reply, TSTRING &errDetails);

	// Restart the program in Admin mode.  This attempts to launch the
	// Admin Host, and if successful, closes the current session.
	void RestartAsAdmin();

	// Send the key mapping list to the admin host
	void SendKeysToAdminHost(const std::list<TSTRING> &keys);

	// High scores object
	RefPtr<HighScores> highScores;

	// Reference table list object
	std::unique_ptr<RefTableList> refTableList;

	// get the FFmpeg version string
	const CHAR *GetFFmpegVersion() const { return ffmpegVersion.c_str(); }

	// Javascript debugger options
	JavascriptEngine::DebugOptions javascriptDebugOptions;

protected:
	Application();

	// Launch a new session using the Admin Host.  Returns true on
	// success, false on failure.  On a successful return, the caller
	// should terminate the current session, so that the newly launched
	// session can take over.
	bool LaunchAdminHost(ErrorHandler &eh);

	// main application event loop
	int EventLoop(int nCmdShow);

	// initialize
	bool Init();

	// initialize and load the game list
	bool InitGameList(CapturingErrorHandler &loadErrs, ErrorHandler &fatalErrorHandler);

	// global singleton instance
	static Application *inst;

	// Load the configuration.  This loads the configuration file
	// and updates global singletons that use the configuration
	// data, such as the input manager and mouse manager.
	bool LoadConfig(const ConfigFileDesc &fileDesc);

	// Check for a RunBefore command (an external program to execute
	// at PinballY startup)
	void CheckRunAtStartup();

	// Check for a RunAfter command (an external program to execute
	// just before PinballY exits)
	void CheckRunAtExit();

	// First Run time.  This is the timestamp from the first time
	// the program was run on the local machine.
	DateTime firstRunTime;

	// Hide unconfigured games, except when the "Unconfigured Games"
	// filter is in effect.  If this is false, unconfigured games are
	// displayed alongside configured games.
	bool hideUnconfiguredGames;

	// are videos enabled?
	bool enableVideos;

	// are videos muted?
	bool muteVideos;

	// are table audios muted?
	bool muteTableAudio;

	// mute in attract mode?
	bool muteAttractMode;

	// main windows
	RefPtr<PlayfieldWin> playfieldWin;
	RefPtr<BackglassWin> backglassWin;
	RefPtr<DMDWin> dmdWin;
	RefPtr<TopperWin> topperWin;
	RefPtr<InstCardWin> instCardWin;

	// New file scanner thread.   We launch a thread of this type
	// whenever the application switches from the background to the
	// foreground, to look for new game files that might have been
	// added since the last time we loaded the game list.  This lets
	// us automatically find newly downloaded or newly installed
	// games, so that they can be added to the current session on
	// the fly rather than forcing the user to exit and restart the
	// program.
	class NewFileScanThread : public RefCounted
	{
	public:
		NewFileScanThread();
		~NewFileScanThread();

		// Launch the thread.  Returns true on success, false on
		// failure.
		bool Launch();

		// thread handle
		HandleHolder hThread;

	protected:
		// main entrypoint, static and member function versions
		static DWORD WINAPI SMain(LPVOID lParam);
		DWORD Main();

		// playfield view window handle
		HWND hwndPlayfieldView;

		// List of directories to scan, and existing files already
		// in the game list.  This is essentially a private copy of
		// the TableFileSet list from the GameList object.  We make
		// a copy rather than referring directly to the GameList
		// originals to avoid any concurrency issues with accessing
		// the GameList data from a background thread.
		struct Directory
		{
			Directory(const TableFileSet &t);

			// path and extension we're scanning
			TSTRING path;
			TSTRING ext;

			// Old files.  This is the list of existing files from
			// the table file set we're based on (that is, the files
			// already represented in the game list).  We initialize
			// this from the table file set before the thread starts.
			std::unordered_set<TSTRING> oldFiles;

			// New files.  This is the list of files matching our
			// search pattern (path\*.ext) that we find in our new
			// scan of the folder that aren't in the oldFiles list.
			std::list<TSTRING> newFiles;
		};
		std::list<Directory> dirs;
	};

	// Current new file scan thread.  We launch at most one of these
	// threads at a time.  (If the user does a foreground-background-
	// foreground switch so rapidly that the previous thread is still
	// running on the second foreground switch, it seems unlikely
	// that they would have had time to add a new file, so we
	// probably won't miss anything on the second switch.  And even
	// if we do, we'll find it on the next switch.)
	RefPtr<NewFileScanThread> newFileScanThread;

	// is a new file scan currently running?
	bool IsNewFileScanRunning()
	{
		// if we don't have a thread object, it's obviously not running
		if (newFileScanThread == nullptr)
			return false;

		// We have a thread object; check if the thread is still running. 
		// It's running if a zero-timeout wait on the thread handle times out.
		if (WaitForSingleObject(newFileScanThread->hThread, 0) == WAIT_TIMEOUT)
			return true;

		// the thread has exited - forget about the thread object and
		// tell the caller that no thread is running
		newFileScanThread = nullptr;
		return false;
	}

	// FFmpeg version, if available
	CSTRING ffmpegVersion;

	// Game monitor thread.  We launch a game by creating a monitor
	// thread, which does the actual process launch and then monitors
	// the process so that we know when it exits.
	class GameMonitorThread : public RefCounted
	{
	public:
		GameMonitorThread();
		~GameMonitorThread();

		// prepare the object and launch in one step
		bool Launch(int cmd, DWORD launchFlags,
			GameListItem *game, GameSystem *system, 
			const std::list<LaunchCaptureItem> *captureList, int captureStartupDelay,
			ErrorHandler &eh);

		// Prepare the object and launch as separate steps.  This
		// can be used to create a deferred launch object, such as
		// during batch capture, and then launch it when its turn
		// comes up.
		void Prepare(int cmd, DWORD launchFlags,
			GameListItem *game, GameSystem *system,
			const std::list<LaunchCaptureItem> *captureList, int captureStartupDelay,
			const BatchCaptureInfo *bci = nullptr);
		bool Launch(ErrorHandler &eh);

		// try shutting down the game thread
		bool Shutdown(ErrorHandler &eh, DWORD timeout, bool force);

		// is the thread still running?
		bool IsThreadRunning();

		// is the game still running?
		bool IsGameRunning() const;

		// is it running in Admin mode?
		bool IsAdminMode() const { return isAdminMode; }

		// try to steal focus from the game
		void StealFocusFromGame(HWND hwnd);

		// terminate the game
		void CloseGame();

		// bring the game's windows to the foreground
		void BringToForeground();

		// wait for the process to start up; returns true on success,
		// false if an error occurs or we get a shutdown signal
		bool WaitForStartup(HANDLE pProc);

		// thread main
		static DWORD WINAPI SMain(LPVOID lpParam);
		DWORD Main();

		// apply variable substitution to a command line
		TSTRING SubstituteVars(const TSTRING &str);

		// Command used to launch the game.  This can in principle be any
		// command, since Javascript can specify an arbitrary command code
		// when queueing a launch via scripting.  For built-in actions,
		// though, we always use one of the following
		//
		//   ID_PLAY_GAME -> normal launch
		//   ID_CAPTURE_GO -> media capture
		//
		int cmd;

		// Launch flags.  This is a combination of bit flags from the
		// Application::LaunchFlag:: constants.
		DWORD launchFlags;

		// game description
		GameBaseInfo game;

		// Audio capture device name
		TSTRING audioCaptureDevice;

		// game file with extension
		TSTRING gameFileWithExt;

		// Get the game file with full path
		void GetGameFileWithPath(TCHAR gameFileWithPath[MAX_PATH]);

		// Resolve the game file.  Adds the extension if necessary.
		void ResolveGameFile(TCHAR gameFileWithPath[MAX_PATH], bool logging = true);

		// game ID, for configuration purposes
		LONG gameId;

		// game system 
		GameSysInfo gameSys;

		// Launch overrides.  The Javascript "prelaunch" event can specify
		// overrides for the normal system launch parameters via entries in
		// this map.
		std::unordered_map<CSTRING, TSTRING> overrides;

		// Get the value for a launch parameter, applying the Javascript
		// "prelaunch" event override if it exists, otherwise using the
		// value defined for the system.  'propname' is the property name
		// from the Javascript "prelaunch" event.
		const TSTRING &GetLaunchParam(const CHAR *propname, const TSTRING &defaultVal);
		int GetLaunchParamInt(const CHAR *propname, int defaultVal);

		// is this system approved for elevation to Administrator mode?
		bool elevationApproved;

		// batch capture information
		BatchCaptureInfo batchCaptureInfo;

		// game inactivity timeout, in milliseconds
		TSTRINGEx gameInactivityTimeout;

		// hide the Windows taskbar while the game is running?
		bool hideTaskbar;

		// Media capture information.  To avoid any cross-thread sync
		// issues, we grab all of the information we need for media
		// capture before we launch the game thread, and stash it here.
		// This information is only used if we're in capture mode.
		struct CaptureItem
		{
			CaptureItem(const MediaType &mediaType, bool enableAudio) : 
				mediaType(mediaType),
				enableAudio(enableAudio),
				windowRotation(0),
				windowMirrorVert(false),
				windowMirrorHorz(false),
				mediaRotation(0),
				captureTime(0),
				manualStart(false),
				manualStop(false)
			{ }

			// media type
			const MediaType &mediaType;

			// for a video item, is audio capture enabled?
			bool enableAudio;

			// filename with path of this item
			TSTRING filename;

			// screen area to capture, in screen coordinates
			RECT rc;

			// Current display rotation for this window, in degrees
			// clockwise, relative to the nominal desktop layout.  In
			// most cases, only the playfield window is rotated, and 
			// the typical playfield rotation in a cab is 90 degrees
			// (so that the bottom of the playfield image is drawn
			// at the right edge of the window).
			int windowRotation;

			// Current mirroring settings for this window
			bool windowMirrorVert;
			bool windowMirrorHorz;

			// Target rotation for this media type, in degrees.  This
			// is the rotation used for media of this type as stored
			// on disk.  All media types except playfield are stored
			// with no rotation (0 degrees).  For compatibility with
			// existing HyperPin and PinballX media, playfield media
			// are stored at 270 degrees rotation (so that the bottom
			// of the playfield image is drawn at the left edge of
			// the window).
			int mediaRotation;

			// capture time in milliseconds, for videos
			DWORD captureTime;

			// manual start/stop mode
			bool manualStart;
			bool manualStop;
		};
		struct CaptureInfo
		{
			CaptureInfo() : startupDelay(5000), twoPassEncoding(false) { }
			
			// initialization time (ms)
			static const DWORD initTime = 3000;

			// startup delay time, in milliseconds
			DWORD startupDelay;

			// estimated total capture time
			DWORD totalTime;

			// two-pass encoding mode
			bool twoPassEncoding;

			// capture list
			std::list<CaptureItem> items;

			// status window
			RefPtr<CaptureStatusWin> statusWin;
		} capture;

		// is the game running in admin mode?
		bool isAdminMode;
		
		// handle to game monitor thread
		HandleHolder hThread;

		// handle to game process
		HandleHolder hGameProc;

		// Have we tried closing the game process in response to 
		// an Exit Game command?
		bool closedGameProc;

		// main thread of the game process
		DWORD tidMainGameThread;

		// Start/stop a manual capture.  The application calls this when
		// the user presses the "proceed" button combination.
		void ManualCaptureGo();

		// Manual start/stop event.  The main UI uses this to let us know
		// when the user has pressed the button sequence to un-pause the
		// current operation.
		HandleHolder startStopEvent;

		// Shutdown event for this thread.  The application sets this
		// event when the program is ready to shut down.
		HandleHolder shutdownEvent;

		// Close Game event for this thread.  The application sets 
		// this event when the user closes the game through the UI.
		HandleHolder closeEvent;

		// Set the close event
		void SetCloseEvent() { SetEvent(closeEvent); }

		// test the close event
		bool IsCloseEventSet() { return WaitForSingleObject(closeEvent, 0) == WAIT_OBJECT_0; }

		// Test and reset the close event.  This atomically retrieves 
		// the current event state and then resets it.
		void ResetCloseEvent() { ResetEvent(closeEvent); }

		// game windows
		RefPtr<PlayfieldView> playfieldView;
		RefPtr<D3DView> backglassView;
		RefPtr<D3DView> dmdView;
		RefPtr<D3DView> topperView;
		RefPtr<D3DView> instCardView;

		// launch time
		ULONGLONG launchTime;

		// exit time
		ULONGLONG exitTime;

		// Window rotation manager.  The RunBefore/RunAfter commands can rotate
		// any of the UI windows via the [ROTATE(window,theta)] flags.  This object
		// keeps track of all rotations applied so far, so that we can undo any
		// outstanding rotation before we return.
		class RotationManager
		{
		public:
			RotationManager(GameMonitorThread *monitor) : monitor(monitor) { }
			~RotationManager()
			{
				// on exit, restore all windows to their original rotations
				for (auto const &r : rotations)
				{
					int theta = r.second;
					if (theta != 0)
						RotateNoStore(r.first, -theta);
				}
			}

			// rotate a window
			void Rotate(TSTRING &winName, int theta)
			{
				// do the rotation
				RotateNoStore(winName, theta);

				// add this to the cumulative rotation for the window
				auto it = rotations.find(winName);
				if (it != rotations.end())
					it->second = (it->second + theta) % 360;
				else
					rotations.emplace(winName, theta);
			}

		private:
			// rotate a window without storing the result
			void RotateNoStore(const TSTRING &winName, int theta);

			// table of windows and cumulative rotations applied so far
			std::unordered_map<TSTRING, int> rotations;

			// containing game monitor thread object
			GameMonitorThread *monitor;
		};
	};

	// launch the game prepared into a game monitor object
	bool Launch(GameMonitorThread *mon, ErrorHandler &eh);

	// game monitor thread for current running game
	RefPtr<GameMonitorThread> gameMonitor;

	// queued game launches
	std::list<RefPtr<GameMonitorThread>> queuedLaunches;

	// Watchdog process interface.  This manages the pipes
	// related to the watchdog process, which monitors for abnormal
	// termination of the main PinballY process and clean up after
	// us should we crash.  This helps avoid leaving global system
	// state changes in effect after we exit.  For example, the
	// watchdog will restore visibility of the taskbar window if
	// we crash after hiding it.
	struct Watchdog
	{
		// launch the watchdog process
		void Launch();

		// send a notification message to the watchdog process
		void Notify(const char *msg);

		// process handle of the watchdog (we launch it as a child)
		HandleHolder hProc;

		// communications pipes
		HandleHolder hPipeRead;
		HandleHolder hPipeWrite;
	};
	Watchdog watchdog;

	// Admin Host interface.  This manages the pipes and threads
	// related to the Admin Host.  The Admin Host is a separate
	// program that runs "elevated", with Administrator privileges.
	// Its function is to launch games for us in elevated mode when
	// required.  The main PinballY UI always runs in ordinary
	// non-elevated user mode, so it can't start elevated processes
	// directly without triggering UAC dialogs, which would make
	// for a poor UX on a pin cab where a full mouse and keyboard
	// might not be conveniently at hand.  The Admin Host *can*
	// launch elevated child processes without UAC prompting,
	// though, since an elevated process can freely launch other
	// elevated processes.  Note that the Admin Host is optional;
	// we're perfectly happy to run standalone, and in fact this
	// is preferable if the user doesn't need to launch any games
	// in Admin mode, as it's always best to minimize elevated
	// processs in general.  When the Admin Host is used, it
	// launched the regular PinballY.exe [the program that this
	// class is a part of] as a non-elevated child process, and
	// passes pipe handles down to the child that the child can
	// use to communicate requests back to the host.  This class
	// manages that communications channel.
	//
	// We provide serialized access to the communications channel
	// through a request queue.  A caller submits a request to the
	// queue, and we process it as soon as any prior requests are
	// finished.  This ensures proper sequencing of requests and
	// replies if multiple threads are trying to access the pipe
	// simultaneously.  Some requests are "post only", meaning
	// that no reply is expected; these can be posted to the 
	// queue without blocking.  If a reply is needed, we provide
	// a "send" call that does a blocking wait until the request
	// has been sent and the reply arrives.
	//
	struct AdminHost
	{
		// Is the Admin Host available?
		bool IsAvailable() const { return hPipeOut != NULL; }

		// Start the admin host interface thread.  This thread
		// processes requests on the pipe.
		bool StartThread();

		// Shut down.  This terminates our thread.
		void Shutdown();

		// Post or send a request.  Both of these put the request in the
		// queue for later sending through the pipe to the Admin Host
		// process.  'Post' simply enqueues the request and returns, and
		// 'Send' enqueues it and waits for the reply before returning.
		//
		// Our standard request format consists of a "verb" in the first
		// string, and zero or more parameter values.  The reply has the
		// same format.
		//
		void PostRequest(const TCHAR *const *request, size_t nItems);
		void PostRequest(const std::vector<TSTRING> &request);
		bool SendRequest(const TCHAR *const *request, size_t nItems, 
			std::vector<TSTRING> &reply, TSTRING &errDetails);

		// Process ID (PID) of the Admin Host process.  The host sends
		// us this information on the command line when launching us.
		DWORD pid;

		// Input pipe (Host to UI).  The Admin Host sends us data on
		// this pipe, usually as replies to request messages we send.
		HandleHolder hPipeIn;

		// Output pipe (UI to Host).  We write this pipe to send
		// requests to the Admin process.
		HandleHolder hPipeOut;

		// OVERLAPPED struct for reading input from the Admin Host
		OVERLAPPED ovRead;
		HandleHolder hReadEvent;

		// shutdown event
		HandleHolder hQuitEvent;

		// pipe manager thread handle and ID
		HandleHolder hThread;
		DWORD tid;

		// pipe manager thread main
		static DWORD WINAPI SThreadMain(LPVOID lparam) { return static_cast<AdminHost*>(lparam)->ThreadMain(); }
		DWORD ThreadMain();

		// Request object.  This represents one request/reply
		// transaction on the pipe.
		class Request: public RefCounted
		{
		public:
			Request(const TCHAR *const *request, size_t nItems, bool wait = false);

			// Request message to send
			std::unique_ptr<TCHAR> request;
			size_t requestCharLen;

			// Reply.  This is filled in when the reply is received.
			std::unique_ptr<TCHAR> reply;
			size_t replyCharLen;

			// Did the request successfully complete?
			bool success;

			// Wait handle.  If the request is posted with no reply
			// expected, this is null.  If the caller expects a reply,
			// we populate this with a new event object, which the
			// calling thread waits on.  We signal this when the
			// reply is received to wake up the waiting caller.
			HandleHolder hEvent;
		};

		// Request queue
		std::list<RefPtr<Request>> requests;

		// process requests on the queue
		void ProcessRequests();

		// Request queue event.  Our thread uses this to wait for a
		// new request to arrive.  We signal this whenever we add a
		// request to the queue.  
		HandleHolder hRequestEvent;

		// Critical Section lock object for coordinating thread 
		// access to the object
		CriticalSection lock;
	}
	adminHost;

	// Is the application the foreground?
	static bool isInForeground;

	// Pinscape device list
	std::list<PinscapeDevice> pinscapeDevices;
};
