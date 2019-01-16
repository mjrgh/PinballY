// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once
#include <unordered_set>
#include <propvarutil.h>
#include "../Utilities/LogError.h"
#include "DiceCoefficient.h"

class GameListItem;
class GameSystem;

// DOF COM object class
class __declspec(uuid("{a23bfdbc-9a8a-46c0-8672-60f23d54ffb6}")) DirectOutputComObject;

// DOF client wrapper
class DOFClient
{
public:
	DOFClient();
	~DOFClient();

	// Create the singleton and initialize DOF.  Initialization runs as
	// a background thread, since it can take a noticeable amount of time.
	// To sync up with the initialization process, use WaitReady().
	static void Init();

	// get the singleton
	static DOFClient *Get() { return inst; }

	// Destroy the singleton.  If 'final' is true, the process is
	// exiting, so do full global cleanup of all static state as
	// well as destroying the singleton.  
	static void Shutdown(bool final);

	// Wait for initialization to complete.  Returns true if initialization
	// was successful, false if not.
	static bool WaitReady();

	// is the DOF client ready?
	static bool IsReady() { return ready; }

	// Initialization error list.  The initializer thread captures any
	// errors to this list; the client can display or log these errors
	// as desired when initialization finishes.
	static CapturingErrorHandler initErrors;

	// get the DOF version
	const TCHAR *GetDOFVersion() const { return ready ? version.c_str() : _T("N/A"); }
	
	// Set a DOF "named state" value.  Named states are states identified
	// by arbitrary labels.  These labels are reference in the config tool
	// via "$" tags to trigger specific feedback effects when a named state
	// is matched.
	//
	// The config tool uses these named states for two purposes.  One is
	// for events, like "go to next wheel item" ($PBYWheelNext) or "go
	// to previous menu item ($PBYMenuUp).  The other is for table matching,
	// which is done by ROM name.
	//
	// The state names are arbitrary, but we want to trigger the effects
	// defined specifically for PinballY in the default config tool database,
	// so we use special names starting with PBY.  This is just a naming
	// convention to avoid name collisions with table ROMs.  There's no
	// guarantee that some table's ROM name won't start with PBY, but it's
	// pretty unlikely.  Table ROM names are essentially arbitrary, but the
	// normal convention is to use a short abbreviation of the table name.
	// For historical reasons (namely, to minimize file exchange hassles
	// with older operating systems), ROM names are usually limited to 6-8
	// alphanumeric characters.  Most of our events using longer names
	// than that, so they're virtually guaranteed to be unique by virtue
	// of the length alone.  But the PBY prefix further helps avoid
	// accidental collisions.
	void SetNamedState(const WCHAR *name, int val);

	// Map a table to a DOF ROM name.  This consults the table/ROM mapping
	// list from the active DOF configuration to find the closest match.
	// We match by ROM name and title, using a fuzzy string match to the
	// title so that we find near matches even if they're not exact. 
	// Returns null if we can't find a mapping list item that's at least
	// reasonably close on the fuzzy match.
	const TCHAR *GetRomForTable(const GameListItem *game);

	// Get a ROM based on a title and optional system.  (The system can
	// be null to look up a ROM purely based on title.)
	const TCHAR *GetRomForTitle(const TCHAR *title, const GameSystem *system = nullptr);

protected:
	// global singleton instance
	static DOFClient *inst;

	// Is the instance ready for use?  We initialize the instance in a
	// background thread, since DOF itself can take a noticeable amount 
	// of time to initialize, and it can also take a while to build the
	// DOF table list, since we have to compute bigram sets for all of
	// the table titles for fast fuzzy matching.  We set this flag when
	// the initializer thread finishes; until then, we ignore any calls
	// to our public interface, to make sure everything is properly
	// initialized.
	static volatile bool ready;

	// Handle to initializer thread, if any
	static HandleHolder hInitThread;

	// Surrogate process for 64-bit mode.  When we're in 64-bit mode,
	// we can't create the DOF COM object directly, since it's 32-bit
	// code; it doesn't register as a 64-bit object, and can't be loaded
	// as an in-process server in 64-bit code due to the Windows rules 
	// against mixing 32-bit and 64-bit code in a single process.  To
	// deal with this, the 64-bit version launches a 32-bit child
	// process to serve as a surrogate for creating the DOF COM object.
	// The surrogate registers a proxy class factory that loads the
	// 32-bit DOF COM object into the surrogate process and passes it
	// back to us via the COM marshalling mechanism, which can move
	// interface objects between processes.
#ifdef _M_X64
	// Have we initialized the surrogate yet?
	static bool surrogateStarted;

	// "Done" event in our 64/32-bit surrogate process.
	static HandleHolder hSurrogateDoneEvent;

	// Class ID of the proxy class that the surrogate exposes through
	// its class factory.  We randomly generated this for each instance
	// of the application, to make the surrogate private to this process.
	// That avoids any conflicts if multiple instances are running.
	static CLSID clsidProxyClass;
#endif

	// IDispatch interface to DOF object
	RefPtr<IDispatch> pDispatch;

	// DOF version number
	TSTRING version = _T("N/A");

	// initialize - load the DOF COM object interface
	bool InitInst(ErrorHandler &eh);

	// load the table mapping file
	void LoadTableMap(ErrorHandler &eh);

	// Game title/ROM mappings from the DOF table mappings file.  The DOF
	// PinballX/front-end configuration uses ROM names to trigger table-
	// specific effects when a game is selected in the menu UI, but the
	// menu UI might only know the title of the table.  SwissLizard ran
	// into this issue when writing the PinballX plugin, and his solution
	// was to use a table title/ROM mapping table generated by the Config
	// Tool to look up the ROM based on title.  Now, DOF and the Config
	// Tool don't actually care about the ROMs qua ROMs; what we're
	// really doing here is coming up with a unique ID for each table 
	// that DOF and the menu system can agree upon, based upon the human-
	// readable table title.  
	//
	// The snag in this approach is that the human-readable titles in 
	// the front-end table list are also human-written, so there can be
	// some superficial variation in the exact rendering of the names,
	// of the sorts common to human-written text: capitalization, 
	// article elisions, misspellings, etc.  So rather than matching the
	// title strings as exact literal matches, we use "fuzzy matching",
	// which allows for approximate matches.
	//
	// The DOF PBX plugin actually implements fuzzy matching for titles
	// that does exactly what we're doing here.  It would have been
	// better design for DOF to have implemented that code as a core
	// service that could be exported through the DOF COM object as
	// well as the PBX plugin, but unfortunately it wasn't implemented
	// that way, so we have to provide our own similar implementation.
	//
	// Because of the need for fuzzy matching to the DOF mapping table,
	// we store the mapping table as a simple list of title/ROM pairs.
	// There are ways to index fuzzy-matched data more efficiently than
	// a linear search, but we have a small data set, so I don't think
	// it's worth the trouble.  However, we do at least pre-compute the
	// bigram set for each title string, which is what the fuzzy
	// matching algorithm uses to compute the similarity to a subject
	// string.  The bigram computation is relatively time-consuming,
	// so it makes the search process much faster if we pre-compute it 
	// for each string in the index.
	//
	// The simplifiedTitle is the title after running it through the
	// SimplifyTitle() function.  This removes extra spaces and
	// punctuation to make fuzzy matching easier.
	//
	struct TitleRomPair
	{
		TitleRomPair(const TCHAR *title, const TCHAR *rom)
			: title(title), rom(rom)
		{
			// pre-compute the bigram set for the simplified title string
			DiceCoefficient::BuildBigramSet(titleBigrams, SimplifiedTitle(title).c_str());
		}

		TSTRING title;
		DiceCoefficient::BigramSet<TCHAR> titleBigrams;
		TSTRING rom;
	};
	std::list<TitleRomPair> titleRomList;

	// Simplified title string.  This removes punctuation marks and
	// collapses runs of whitespace to single spaces.
	static TSTRING SimplifiedTitle(const TCHAR *title);
	
	// Previously resolved mappings.  Whenever we resolve a game's ROM,
	// we'll add an entry here so that we can look up the same game quickly
	// next time it's selected.
	std::unordered_map<const GameListItem*, TSTRING> resolvedRoms;

	// ROM names in the loaded DOF configuration.  This lets us determine
	// if a ROM name from the table database is known in the congiguration,
	// meaning that it will properly trigger table-specific effects if
	// set as the current table.  When a table database entry specifies
	// a ROM, but that ROM isn't in the loaded config, it's better to try
	// to match the table to DOF effects based on the table title.  The
	// reason is that some tables have multiple ROMs available, but the
	// DOF config tool always generates the .ini files for one ROM for
	// each table.  That means the ROM designated in the local table
	// database might be perfectly valid but still not matchable in the
	// DOF config, so we're better off trying to find the one actually
	// used in the config by matching on the game title.
	//
	// This is stored with the lower-case version of the name as the
	// key, and the exact-case version as the value.  This allows quick
	// lookup of the name without regard to case, and retrieves the
	// corresponding exact name that will match the DOF configuration.
	std::unordered_map<TSTRING, TSTRING> knownROMs;


	//
	// DISPIDs for the dispatch functions we need to import
	//

	// void Init(string hostAppName, string tableFileName = "", string gameName = "")
	DISPID dispidInit;

	// void Finish()
	DISPID dispidFinish;

	// string GetVersion()
	DISPID dispidGetVersion;

	// void UpdateTableElement(string elementType, int eleNumber, int value)
	DISPID dispidUpdateTableElement;

	// void UpdateNamedTableElement(string name, int value)
	DISPID dispidUpdateNamedTableElement;

	// String TableMappingFileName()
	DISPID dispidTableMappingFileName;

	// String[] GetConfiguredTableElmentDescriptors() [sic - "Elment" not "Element"]
	DISPID dispidGetConfiguredTableElmentDescriptors;
};

