// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// High Scores interface
//
// This works with the external, third-party PINemHi program to retrieve
// live high score information.  PINemHi accesses high score data from 
// Visual Pinball and Future Pinball games.  In the case of VP, it uses
// the little data files that VPinMAME uses to emulate non-volatile RAM 
// for ROM-based games; for FP, it uses the equivalent that FP uses to
// store settings for its scripted games.
// 

#pragma once
#include "DiceCoefficient.h"

class ErrorHandler;
class GameListItem;

class HighScores : public RefCounted
{
public:
	HighScores();
	~HighScores();

	// Initialize.  This checks to see if PINemHi is available.  If so,
	// we reads the PINemHI .ini file and patch it (if necessary) with
	// the FP path from our own config file, and the VPinMAME NVRAM path
	// from the registry.
	bool Init();

	// Find the NVRAM file to use for a game, based, based on the
	// game's database entry.
	//
	// For Future Pinball games, this is pretty straightforward:
	// the NVRAM file generally has the same name as the .fpt file,
	// with the .fpt suffix replaced by .fpram.
	//
	// For Visual Pinball games that use VPinMAME, a game's NVRAM 
	// file has the same name as the ROM it uses, with the suffix 
	// .nv added.  However, that's not as straightforward as it
	// sounds, because HyperPin and PinballX tried to use heuristics
	// to guess the ROM name without forcing the user to configure
	// the ROM name in their game list.  To allow for easy migration,
	// we need to play the same guessing game.
	//
	// Here's our procedure for guessing the name of a VP game's ROM:
	// 
	// - If there's an explicit "rom" setting in the game database
	//   entry for the game, use it.  This overrides all of the 
	//   other options below, so users can easily add this entry
	//   for any game if our other guesswork below gets it wrong.
	//
	//   The only snag is that PinballX allowed users to enter the
	//   "friendly" name of the ROM here instead of the filename.
	//   So for ease of migration, we have to accept the friendly
	//   names as well.  We therefore look in the [romfind] section
	//   of the PINemHi.ini file to see if there's an entry that
	//   matches the "rom" name in the game list; if so, we'll
	//   substitute the associated filename in the [romfind] list.
	//
	// - If there's no "rom" setting in the game database, we'll
	//   retrieve the DOF ROM name from the DOF config, if present.
	//   If DOF is installed and configured, the DOF table list is
	//   a good bet for accurate mappings on the local machine,
	//   because any error in the DOF config for a table will be
	//   apparent (in that DOF won't work) when you run the table.
	//
	// - If we can't find the table's ROM via DOF, we'll search for
	//   the table by title in the [romfind] list from the PINemHi.ini
	//   file.  We use fuzzy matching for this, so the name doesn't
	//   have to match exactly.  If we find a good title match, we'll
	//   search for copies of the .nv files listed for that table.
	//   If there's exactly one, we'll use it.  If there are multiple
	//   matching files, we'll pick none, because we don't want to
	//   make a random guess.
	//
	// If we don't find anything after trying all of those options,
	// we'll return failure.
	//
	// Since the explicit database entry is always the first choice,
	// the user can override all of the heuristics simply by adding
	// an entry.  So if we're not automatically coming up with the 
	// right solution for a given game, it's easy to override our
	// guesswork for that game.
	//
	// The function returns true if we successfully identify the
	// NVRAM file, false if not.  The returned filename includes the 
	// appropriate extension (.nv, .fpram), but doesn't include the 
	// path, which we return separately.
	//
	bool GetNvramFile(TSTRING &path, TSTRING &file, const GameListItem *game);

	// Get all of the NVRAM filenames associated with a game title.
	// This returns the list of .nv files listed in the [romfind]
	// section for a given title, using the best guess at a title
	// match based on a Dice coefficient search.
	bool GetAllNvramFiles(std::list<TSTRING> &nvList, const TCHAR *title);

	// notification context, for the caller to subclass
	class NotifyContext
	{
	public:
		virtual ~NotifyContext() { }
	};

	// Load the high score data for the given game, if possible.  The
	// programs runs asynchronously in a background thread, so the
	// score results are returned via an HSMsgHighScores message to the
	// notification window provided.  The function returns true if the
	// request was successfully started, false if not.  A true return
	// doesn't mean that the request will eventually succeed, as it's
	// possible that something could go wrong launching the process
	// asynchronously, but it does mean that an HSMsgHighScores message
	// will eventually be sent to the notification window with some
	// kind of results.
	//
	// The notification context will be automatically deleted when the
	// request is finished (or if it fails).
	bool GetScores(GameListItem *game, HWND hwndNotify, NotifyContext *notifyContext = nullptr);

	// Get the PINemHi version information.  This runs PINemHi in the
	// background with the -v option (to retrieve the program version
	// data).  Sends a HSMsgHighScores message to the notification
	// window when done.  As with GetScores(), a true return means that
	// the asynchronous request was successfully started, but doesn't
	// guarantee that it will actually succeed.
	bool GetVersion(HWND hwndNotify, NotifyContext *notifyContext = nullptr);

	// type of query
	enum QueryType
	{
		Initialized,
		HighScoreQuery,
		ProgramVersionQuery
	};

	// Score notification.  A pointer to this object is sent as the
	// LPARAM to the notification window.  The window should treat
	// this as constant data.
	struct NotifyInfo
	{
		NotifyInfo(QueryType queryType, GameListItem *game, NotifyContext *notifyContext);

		// query type
		QueryType queryType;

		// game we're fetching high scores for
		LONG gameID;

		// caller's notification context
		NotifyContext *context;

		// status
		enum Status
		{
			Success,
			ThreadLaunchFailed,
			IniFileUpdateFailed,
			CreatePipeFailed,
			ProcessLaunchFailed,
			NoReplyFromProcess,
			FileReadFailed
		} status;

		// source of the results
		enum Source
		{
			None,     // no source/not applicable
			PINemHi,  // results from PINemHi process
			File      // results from an ad hoc scores file
		} source = None;

		// interpret the source code into a name string for Javascript
		const TCHAR *GetSourceName() const
		{
			return source == None ? _T("none") :
				source == PINemHi ? _T("pinemhi") :
				source == File ? _T("file") :
				_T("?");
		}

		// output captured from PINemHi or ad hoc scores file
		TSTRING results;
	};

protected:
	// Initializer thread
	HandleHolder hInitThread;

	// Check if initialization is complete
	bool IsInited();

	// initialization is complete
	bool inited;

	// Try getting scores from the NVRAM file via PINemHi
	bool GetScoresFromNVRAM(GameListItem *game, HWND hwndNotify, std::unique_ptr<NotifyContext> &notifyContext);

	// Try getting scores from our own ad hoc scores file
	bool GetScoresFromFile(GameListItem *game, HWND hwndNotify, std::unique_ptr<NotifyContext> &notifyContext);

	// Global VPinMAME NVRAM path.  This is the path from the VPM
	// config vars in the registry.  This can be overridden per system
	// in the app config, but this usually isn't necessary, as VPM's
	// design as a COM object more or less forces all VP versions to
	// use the same VPM installation.
	TSTRING vpmNvramPath;

	// PINemHi.ini file path
	TSTRING iniFileName;

	// PINemHi.ini data loaded into memory
	std::unique_ptr<BYTE> iniData;

	// Line index on the ini data
	std::vector<char*> iniLines;

	// Path entries in the PINemHi.ini
	struct PathEntry
	{
		PathEntry() : lineNo(-1) { }

		void Set(const CHAR *name, CSTRING &path, size_t lineNo)
		{
			this->name = name;
			this->path = path;
			this->lineNo = lineNo;
		}

		// line number in the INI file, as an index in iniLines
		size_t lineNo;

		// system name variable
		CSTRING name;

		// current path value
		CSTRING path;
	};
	PathEntry vpPath;
	PathEntry fpPath;

	// [romfind] mappings.  This is the table of mappings from
	// "friendly" ROM names to NVRAM file names as listed in the
	// PINemHi INI file.  We collect the table in case the user
	// wants to use the friendly names for the ROMs in their
	// database files.  Entries are keyed by friendly name,
	// converted to lower-case, and the associated value is the
	// NVRAM filename.
	std::unordered_map<CSTRING, CSTRING> romFind;

	// Fuzzy ROM match table.  This is a list of the friendly
	// names for the ROMs, with the version variation suffixes
	// removed, and stored with their bigram sets for fuzzy
	// matching via a Dice coefficient search.  The friendly
	// names are usually the table titles, so this lets us 
	// search based on the table title from the game database.
	// The snag is that most tables have multiple ROM versions
	// associated with them, but we pick the right one in many
	// cases by looking for an existing file.  That will work
	// as long as the user hasn't installed and played multiple
	// versions of the ROM.
	struct FuzzyRomEntry
	{
		FuzzyRomEntry(const CHAR *title)
		{
			// pre-compute the bigram set from the title
			DiceCoefficient::BuildBigramSet(bigrams, title);
		}

		// title bigram set for fuzzy matching
		DiceCoefficient::BigramSet<CHAR> bigrams;

		// list of associated .nv files
		std::list<CSTRING> nvFiles;
	};
	
	// map of fuzzy-lookup ROM entries, keyed by title
	std::unordered_map<CSTRING, FuzzyRomEntry> fuzzyRomFind;

	// Lock for resources accessed from the background thread
	CriticalSection threadLock;

	// Base class for our background threads
	class Thread
	{
	public:
		Thread(HighScores *hs, QueryType queryType, GameListItem *game, HWND hwndNotify, NotifyContext *ctx) :
			hs(hs, RefCounted::DoAddRef),
			queryType(queryType),
			game(game),
			hwndNotify(hwndNotify),
			notifyContext(ctx)
		{
		}

		virtual ~Thread() { }

		// main entrypoint
		static DWORD WINAPI SMain(LPVOID param);
		virtual void Main() = 0;

		// high scores object
		RefPtr<HighScores> hs;

		// query type
		QueryType queryType;

		// Game we're retrieving information on.  This can be null
		// if we're doing a general query, such as a PINemHi program
		// version check.
		GameListItem *game;

		// notification window - we send this window a message
		// when finished to give it the new high score information
		HWND hwndNotify;

		// context object for notification message
		std::unique_ptr<NotifyContext> notifyContext;
	};

	// Background thread to read the NVRAM file
	class NVRAMThread : public Thread
	{
	public:
		NVRAMThread(const TCHAR *cmdline, QueryType queryType,
			GameListItem *game,	const TSTRING &nvramPath, const TSTRING &nvramFile,
			HighScores *hs, PathEntry *pathEntry,
			HWND hwndNotify, NotifyContext *notifyContext);

		// main entrypoint
		virtual void Main() override;

		// Command line to send to PINemHi
		TSTRING cmdline;

		// NVRAM path and filename
		TSTRING nvramPath;
		TSTRING nvramFile;

		// INI file path entry for this system.  This can be null
		// if we're running PINemHi for a general query, such as
		// a program version check.
		PathEntry *pathEntry;
	};

	// Background thread to read our ad hoc scores file
	class FileThread : public Thread
	{
	public:
		FileThread(HighScores *hs, QueryType queryType, GameListItem *game,
			HWND hwndNotify, NotifyContext *ctx, const TCHAR *filename) :
			Thread(hs, queryType, game, hwndNotify, ctx),
			filename(filename)
		{
		}

		// main entrypoint
		virtual void Main() override;

		// High scores file we're reading
		TSTRING filename;
	};

	// Enqueue a thread
	void EnqueueThread(Thread *thread);

	// launch the next thread
	void LaunchNextThread(Thread *exitingThread = nullptr);

	// To simplify concurrent issues in our background threads,
	// we only run one thread a time.  We queue pending threads
	// here.  When one thread is about to exit, it launches the
	// next one from the queue.
	std::list<Thread*> threadQueue;
};

