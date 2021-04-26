// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include <list>
#include <unordered_map>
#include "../rapidxml/rapidxml.hpp"
#include "../Utilities/DateUtil.h"
#include "Resource.h"
#include "CSVFile.h"

class ErrorHandler;
class GameManufacturer;
class GameCategory;
class GameSystem;
class TableFileSet;

// Game database file.  This represents an XML table list file,
// loaded from the Databases directory.
//
// Each database file is associated with a system (a GameSystem
// object).  All of the games listed in a database file are 
// playable through the associated system.
//
// For compatibility with existing databases created for PinballX,
// a system can have multiple database files.  In PinballX, each
// file defined a "list", which showed up in the UI as a filter.
// In our schema, we've expanded this to a more general "category"
// filter, where each game can be tagged with any number of
// categories.  Since a game can now have multiple categories,
// it's not efficient to represent the categories with the file
// location.  We do still *read* the separate files and treat them
// as category assignments, so that existing PinballX databases
// work as expected, but we use a separate mechanism (through our
// separate statistics database file) to store new category
// assignments made by the user.
//
class GameDatabaseFile
{
public:
	GameDatabaseFile();
	~GameDatabaseFile();

	// load and parse a file
	bool Load(const TCHAR *filename, ErrorHandler &eh);

	// load from text
	bool Load(const char *txt, ErrorHandler &eh);

	// parse the XML in sourceText
	bool Parse(ErrorHandler &eh);

	// have we modified the XML data since loading?
	bool isDirty;

	// have we backed up the original file during this session?
	bool isBackedUp;

	// XML document
	rapidxml::xml_document<char> doc;

	// Filename
	TSTRING filename;

	// Original contents of the file.  We have to retain this
	// for the life of the XML parse tree, since the RapidXML
	// parse tree uses pointers directly into the original
	// source text.
	std::unique_ptr<char> sourceText;

	// The category this file defines.  If the file has the
	// same name as its parent folder, it serves as the list
	// of uncategorized games for that system, so the category
	// pointer will be null.
	GameCategory *category;
};


// Basic game information
class GameBaseInfo
{
public:
	GameBaseInfo() { }

	// Title.  This is the title portion of the full name.
	TSTRING title;

	// IPDB ID, if known
	TSTRING ipdbId;

	// Game filename.  This is the name of the playable simulator
	// file (.vpt, .vpx, .fpt, etc).  
	//
	// If this entry came from an XML database, this is the "name"
	// attribute of the <game> node defining the game.  The exact
	// filename format specified there can vary, since the data
	// can come from a HyperPin or PBX migration or from manual 
	// user input.  This is usually the root filename without a
	// path, but it might or might not have an extension.
	// 
	// If this is an unconfigured game entry created from a table
	// file set scan, this is the root filename with extension.
	TSTRING filename;

	// ROM name
	TSTRING rom;

	// Media name.  This is the full name as it appears in the PBX
	// database, usually in the format "Title (Manufacturer YYYY)".
	// It serves as the root name for all media files (playfield
	// images, backglass images, wheel images, DMD videos, etc).
	TSTRING mediaName;

	// Year (release date of original arcade game).  We use zero
	// if the date is unknown or doesn't apply.
	int year = 0;

	// IPDB table type: SS (solid state), EM (electromechanical),
	// ME (pure mechanical).
	TSTRING tableType;

	// Grid position.  This is essentially a special case for 
	// The Pinball Arcade by Farsight, which doesn't have a way to
	// launch the application directly into a game but rather
	// requires going through a menu system to select the game.
	// The menu shows a list of games arranged in a grid.  The
	// row/col position gives the position in the grid of this
	// game's icon, with (1,1) being the first icon at upper
	// left.  We'll use this to send a series of keystrokes
	// to the game to navigate to the desired game.
	struct GridPos
	{
		GridPos() { }

		int row = 0;
		int col = 0;
	} gridPos;
};

// Media type descriptor
struct MediaType
{
	// Format type
	enum Format
	{
		Image,                 // still image
		SilentVideo,           // video with no audio track
		VideoWithAudio,        // video with an optional audio track
		Audio                  // audio
	};

	// Create a user-defined media type, and add it to the global list
	static MediaType *CreateUserDefined(
		int menuOrder, const TCHAR *subdir, bool perSystem, const TCHAR *exts,
		const TCHAR *name, const TCHAR *configId, const TCHAR *javascriptID,
		const TCHAR *captureStartConfigVar, const TCHAR *captureStopConfigVar, const TCHAR *captureTimeConfigVar,
		Format format, int rotation, bool indexed, const std::list<TSTRING> *pageList, bool hasDropButton);

	// The pre-defined (built-in) media type objects are statically
	// allocated, and only contain references to static strings, so
	// those don't need a destructor.  However, Javascript can also
	// create media types dynamically, and these might contain
	// references to allocated strings, so we need to clean those
	// up at termination.
	~MediaType();

	// Save a backup copy of the given media file, by renaming it
	// to <base name>.old[<N>].<ext>, where N is the next higher
	// integer above any existing files of the same name pattern.
	// Fills in newName with the new filename on a successful
	// rename.
	bool SaveBackup(const TCHAR *filename, TSTRING &newName, ErrorHandler &eh) const;

	// Get the full path to the folder for this media type.  The 
	// provided buffer must have room for at least MAX_PATH 
	// characters.  Note that a paged media type (such as Flyer
	// Images) will have subfolders within this folder where the
	// actual media are stored.  Returns true on success, false
	// if we're unable to constuct the path.
	bool GetMediaPath(TCHAR/*[MAX_PATH]*/ *buf, const TCHAR *systemMediaDir) const;

	// Menu order.  This is for sorting the items in a capture or
	// file-drop menu in a consistent order.
	int menuOrder;

	// Media tree subfolder for this type.  This is just the 
	// relative subfolder name,  such as "Backglass Images" or
	// "Table Videos".
	const TCHAR *subdir;

	// Is this a per-system media type?  The media files for per-
	// system types are stored in <media root>/<system>/<subdir>.
	// Generic types are stored in <media root>/<subdir>.
	bool perSystem;

	// List of valid extensions for the type.  This is a single
	// string with all of the extensions, including the ".", 
	// delimited by spaces: e.g., ".jpg .jpeg .png".
	const TCHAR *exts;

	// determine if a filename matches one of our extensions
	bool MatchExt(const TCHAR *filename) const;

	// Name string resource ID (IDS_MEDIATYPE_xxx).  Note that you
	// should almost always use nameStr instead - that's the actual
	// text from the resource, loaded at program startup.  The
	// resource ID will be zero for any user-defined resource, since
	// the user Javascript code defines the name string directly
	// for those.  So unless you know that you're dealing with a
	// pre-defined static media type, the actual string is the one
	// to use.
	int nameStrId;

	// Config file ID.  This is used to refer to the media type in
	// the saved settings file.  The ID for each type must be
	// permanent (it should never change across program versions),
	// so that config files can be used across versions.
	const TCHAR *configId;

	// Javascript ID.  This is used to refer to the media type in
	// events and native methods exposed to Javascript.  As with the
	// config ID, this must be permanent for a given media type.
	const WCHAR *javascriptId;

	// Config variable names for capture parameters for this type.
	//
	// The Start parameter specifies the start mode, MANUAL or AUTO.
	// This applies to all media types.
	//
	// Stop specifies the start mode, MANUAL or AUTO.  This applies
	// to videos and audios; it should be null for image types.
	//
	// Time specifies the capture time for the type.  This is used
	// in AUTO mode.  It applies only to videos and audios.
	const TCHAR *captureStartConfigVar;
	const TCHAR *captureStopConfigVar;
	const TCHAR *captureTimeConfigVar;

	// Media format class
	Format format;

	// Is this some kind of video format?
	bool IsVideo() const { return format == SilentVideo || format == VideoWithAudio; }

	// Standard rotation for the stored media of this format, in
	// degrees clockwise.  This is a fixed rotation always applied 
	// when loading media of this type.  This is zero for most media
	// types, but it's 270 degrees for the playfield view.  This is
	// for compatibility with existing HyperPin and PinballX media,
	// where the playfield image is always rotated so that the
	// bottom of the playfield is at the left edge of the image
	// frame.
	int rotation;

	// Does this type use indexed items?  If this is true, we can
	// have multiple matching files, with " 1", " 2", etc suffixes
	// after the base name (space + decimal sequence number).  The
	// zeroeth image in this type of sequence has simply the base
	// name with no suffix.
	bool indexed;

	// Page list for the type.  Some types (notably Flyer Images)
	// represent multiple pages as named subdirectories of the main
	// media folder for the type.  The files in the subdirectories
	// all have the base name.  This is an array of strings, with
	// a null string pointer marking the end of the array.
	const TCHAR **pageList;

	// Is this a user-defined custom media type?
	bool isUserDefined = false;

	// Should this media type be included in the DROP HERE buttons
	// offered in the main screen?
	bool hasDropHereButton = false;

	// The name of the media type.  For pre-defined system media
	// types, this is loaded from the resource given by nameStrId.
	// For user-defined media types created through Javascript, this
	// is the name assigned by the user code.
	TSTRING nameStr;
};

// Game list item.  This represents one game in the game list.
class GameListItem: public GameBaseInfo
{
public:
	// Create a game list entry from an XML database file entry
	GameListItem(
		const TCHAR *mediaName,
		const char *title, 
		const char *filename, 
		const GameManufacturer *manufacturer, 
		int year,	
		const TCHAR *ipdbId,
		const char *tableType,
		const char *rom, GameSystem *system, bool enabled,
		const char *gridpos);

	// Create an entry for an unconfigured game file.  The only
	// information we have in this case is the filename.
	GameListItem(const TCHAR *filename, TableFileSet *tableFileSet);

	// set the default title and media name from the filename
	void SetTitleFromFilename();

	// common initialization
	void CommonInit();

	virtual ~GameListItem();

	// Clean up a media name string.  This removes invalid filename
	// characters to make the name suitable as a filename.
	static TSTRING CleanMediaName(const TCHAR *name);

	// Internal ID for the game.  This is only good for the duration
	// of this program session, so it can't be used in saved files.
	// This is *almost* like a pointer to the GameListItem object,
	// but we use a non-recycled serial number instead of an actual
	// memory pointer, to ensure uniqueness across object deletions
	// and full reloads.
	LONG internalID = 0;

	// next available internal ID
	static LONG nextInternalID;

	// ID string for the saved configuration.  This is intended to be
	// a unique identifier, so we need more than just the title, since
	// it's quite possible to have multiple simulation versions of the
	// same title installed (e.g., a Visual Pinball 9 version, a VP 10
	// version, and a Future Pinball version of the same game could all 
	// be installed).  The filename is likely to be unique, but it's
	// not necessarily permanent, as the filename could change as new
	// version updates of the game are installed.  So instead, we'll
	// use the title plus system name.  That should be both unique and
	// permanent for a given game.
	TSTRING GetGameId() const;

	// Old ID string for the saved configuration.  The ID key changed
	// in 1.0 Beta 3 to use the media file format, "Title (Manuf Year)",
	// when those data are known.  Before that we only used the title
	// portion.  When looking up the CSV row, we try the old format if
	// we can't find a match for the new format, in case the CSV file
	// was carried over from a past version.
	TSTRING GetOldGameId() const;

	// Resolve the game file.  This looks in the system's table folder
	// for a file matching the filename in the metadata.  If the file
	// as named doesn't exist, we'll try adding the default extension
	// for the system (if it has one).
	struct ResolvedFile
	{
		// does the file exist?
		bool exists;

		// full filename with path
		TSTRING path;

		// folder containing the file
		TSTRING folder;

		// file spec (no path, includes extension)
		TSTRING file;
	};
	void ResolveFile(ResolvedFile &rf);

	// Get the formatted display name, "Title (Manufacturer Year)".
	TSTRING GetDisplayName() const;

	// Get the default media file name.  This is the same as the
	// display name, but cleaned up for use as a filename (with any
	// invalid filename characters replaced with a valid character
	// or removed).
	TSTRING GetDefaultMediaName() const;

	// Update the media name.
	//
	// If newName is null or an empty string, we use the default 
	// media name, "Title (Manufacturer Year)" (or the game file's
	// root name, if the game has no database entry).
	//
	// If the new name (default or explicit) is different from the
	// old name, and fileRenameList is non-null, we'll populate the
	// list with the names of the existing media files for the game
	// under the old name, and the corresponding new names, as
	// <new,old> pairs.  The caller can use this to rename the media
	// files to account for the name update in the game record.
	//
	// Returns true if the new name is different from the old name,
	// false if not.
	bool UpdateMediaName(std::list<std::pair<TSTRING, TSTRING>> *fileRenameList, const TCHAR *newName);

	// Media types
	static MediaType playfieldImageType;
	static MediaType playfieldVideoType;
	static MediaType playfieldAudioType;
	static MediaType backglassImageType;
	static MediaType backglassVideoType;
	static MediaType dmdImageType;
	static MediaType dmdVideoType;
	static MediaType topperImageType;
	static MediaType topperVideoType;
	static MediaType wheelImageType;
	static MediaType instructionCardImageType;
	static MediaType flyerImageType;
	static MediaType launchAudioType;
	static MediaType realDMDImageType;
	static MediaType realDMDColorImageType;
	static MediaType realDMDVideoType;
	static MediaType realDMDColorVideoType;

	// Master list of media types, and table of types indexed by Javascript ID
	static std::list<const MediaType*> allMediaTypes;
	static std::unordered_map<WSTRING, const MediaType*> jsMediaTypes;
	static void InitMediaTypeList();

	// Add a new (user-defined) media type
	static void AddMediaType(const MediaType *m);

	// Look up a media type by its Javascript ID
	static const MediaType *MediaTypeByJsId(const WCHAR *id);

	// Clear the media type lists, and delete any dynamically
	// allocated media type objects.  This should be called just
	// before program exit to clean up any user-defined media
	// type objects allocated through Javascript.
	static void ClearMediaTypeList();

	// Media file resolution - get the file system filename(s)
	// for a media item of the given type.
	//
	// If 'forCapture' is false, these search in the standard
	// location for a media item of the given type for this game.
	// For each item type, we search for files matching the game
	// name and any of the standard extensions for the acceptable
	// media formats.  For example, for a playfield image, we'll
	// search the game's system's playfield image folder for files
	// with the same base name as the game and ending in .png, 
	// .jpg, and .jpeg. 
	//
	// If an existing file matching one of the possible names and
	// extensions is found, the functions fill in 'filename' with
	// the full path and filename, and returns true.  If not, the
	// functions return false, leaving 'filename' unchanged (so
	// it'll come back with an empty string, unless the caller
	// specifically initializes it to something else).
	//
	// If 'forCapture' is true, the functions fill in 'filename'
	// with the file name to use for capturing the media type.
	// In this case, no check is made to determine if the file
	// already exists, and the return value is always true.
	//
	// If 'enableSwf' is true, we'll include SWF (Shockwave Flash)
	// files in the search; otherwise we'll skip these files.
	//
	// 'index' specifies which item to return for indexed media
	// (such as Instruction Cards) and paged media (such as Flyers).
	// Pass zero to retrieve the first item from the set.  Values
	// greater than zero will return false ("not found") for
	// regular media types.  Pass -1 to retrieve items at all
	// index values.  Note that the index doesn't correspond
	// directly to Flyer Page N or Instruction Card N, because
	// not all slots are necessarily populated.  Rather, it
	// corresponds to the position in the result list.  So if
	// an Instruction Card set has items number 1, 3, and 7,
	// and we're limiting the results to existing items, we
	// have a three-element result with 'index' values 0, 1,
	// and 2.  This also applies comes into play if a media
	// type is both indexed AND paged; in that case, the list
	// is built in page-first order (Page 0 Index 0, Page 0
	// Index 1, Page 0 Index 2, ... Page 1 Index 0, ...), and
	// 'index' applies to the actual results in that order.
	// In most cases, we expect that callers will simply want
	// to step through media sequentially, so this provides an
	// easy interface for that use case.
	//

	// Get a single media item of the given type.  If there are
	// multiple matches, the file with the lowest index is
	// returned, and if there are multiple files with the same
	// index, the one with the most recent 'modified' timestamp
	// (in the file system metadata) is returned.  Multiple
	// matches are possible if 'index' is -1 and the media type
	// is paged or indexed, OR if there are multiple files with
	// different extensions matching the media name (e.g., both
	// a .jpg and .png file with the same root name).  We return
	// the newest match in cases of multiple matches with
	// different extensions on the assumption that the user
	// probably *intended* the most recently added file to
	// overwrite the older file of the same name, but didn't
	// notice that the old file wasn't actually removed, since
	// it has a different suffix and thus counts as a separate
	// file as far as Windows is concerned.
	bool GetMediaItem(TSTRING &filename, 
		const MediaType &mediaType, 
		bool forCapture = false, bool enableSwf = true,
		int index = 0) const;

	// Get the list of media items for the given type.
	//
	// 'flags' is a combination of the GMI_xxx flag constants
	// defined below; see the comments there for the meanings of
	// the bits.
	//
	// 'index' indicates which item we should return out of
	// a set of paged or indexed media items, such as Flyers
	// or Instruction Cards.  The default -1 means that we
	// return all of the items for all pages/indices.
	bool GetMediaItems(std::list<TSTRING> &filenames, 
		const MediaType &mediaType, DWORD flags = GMI_EXISTS,
		int index = -1) const;

	//
	// GetMediaItems() flags
	//
	
	// Include only existing files
	static const DWORD GMI_EXISTS = 0x0001;

	// Relative path: return the filename relative to the media type's 
	// media folder path.  In most cases, this will return only the
	// bare filename, since most media files are directly in their
	// media type folder.  The exception is "paged" items (e.g., Flyer
	// Images), which will include the page folder.
	static const DWORD GMI_REL_PATH = 0x0002;

	// Ignore SWF files
	static const DWORD GMI_NO_SWF = 0x0004;

	// In cases of multiple file matches for a given index/page
	// position, use the newest file (i.e., the one with the most
	// recent 'modified' timestamp in the file system metadata).
	// This applies when there are multiple files with the same
	// root name and different extensions, such as a .jpg file
	// and a .png file.  In this case, we resolve the conflict
	// by only including the newest file.  This is meant to
	// infer the user's intent on the basis that they probably
	// intended whichever file they copied into the folder last
	// to overwrite any previous copy, but didn't notice that
	// the old file wasn't actually overwritten by the copy
	// because its name differed in the extension.  This flag
	// is only meaningful when combined with GWI_EXISTS, since
	// non-existence items don't have file system metadata
	// that we can use to break a tie.
	static const DWORD GMI_NEWEST = 0x0008;

	// Get the destination file for a given drop file
	TSTRING GetDropDestFile(const TCHAR *droppedFile, const MediaType &mediaType) const;

	// Is there an existing media item for a given type?
	bool MediaExists(const MediaType &mediaType) const;

	// manufacturer
	const GameManufacturer *manufacturer;

	// system (VP, FP, etc)
	GameSystem *system;

	// Most recent system index chosen for play.  This only applies
	// to unconfigured games that can be played with multiple systems.
	// When the user tries to play such a game, the UI displays a
	// menu listing the matching systems; this records the user's
	// most recent choice.  We use this to set the initial menu
	// selection to the same item the next time the user tries to
	// play the same game, and we also use it to select the same
	// system if we have to re-launch a game after asking for Admin
	// mode approval.
	int recentSystemIndex;

	// Table file set that the game's table file comes from, if any.
	// This is null if the game doesn't have an associated table file.
	TableFileSet *tableFileSet;

	// get items from the table file set
	TSTRING *GetTablePath() const;

	// Rating from the database file.  PinballX stores a <rating>
	// element in its XML database files, which it displays but (as
	// far as I know) doesn't have any UI to change.  We store our
	// own ratings, which you *can* set in the UI, in our stats
	// file.  We inherit the PBX rating when the stats entry 
	// doesn't exist yet.
	float pbxRating;

	// Database file where the game was defined.  If the file 
	// defines a category, the game is in that category.
	GameDatabaseFile *dbFile;

	// XML <game> node where the game was defined.  This is a pointer
	// into the parse tree for the dbFile defining the game.
	rapidxml::xml_node<char> *gameXmlNode;

	// Is this game configured?  A configurd game has a database
	// record; an unconfigured game is one that we found in the file
	// system with no corresponding database entry.  
	//
	// This status is superficially redundant with the existence
	// of a gameXmlNode.  The reason we keep it separately is that,
	// for UI purposes, we might want the "unconfigured" status to 
	// persist in some cases even after the user creates an XML 
	// record for the game through the UI.  The UI shows some extra
	// "game setup" commands in the main menu for unconfigured games,
	// and for consistency, we want to keep showing those extra menu
	// items for a while even after the user creates an XML record.
	// This flag lets us tell if the game is being given the special
	// "unconfigured" treatment in the UI regardless of whether or
	// not it has an XML record.
	bool isConfigured;

	// Stats database row number (in GameList::statsDb).  Games don't
	// automatically have entries in the stats db; entries are only
	// added when we set a statistic value.  A non-negative value 
	// here is the row number.  If the value is negative, it has a
	// special meaning:
	//
	//   -2 means that this entry is uninitialized, meaning that we
	//   haven't ever tried to look up the stats db row.  We defer
	//   the row lookup until we actually need it, and we store -2
	//   here until the first lookup.
	//
	//   -1 means that the game has no stats db row.  That is, we've
	//   attempted to access this game's stats entries, so we did
	//   the row lookup, and came up empty.  On subsequent accesses,
	//   we can skip the row lookup, since we already know there's
	//   nothing to be found.
	int statsDbRow;

	// High scores.  This is the text returned from PINemHi.exe for
	// this game, broken into lines.  We populate this on demand, so 
	// an empty list mean either that we haven't tried yet (or have a 
	// request out to PINemHi.exe that hasn't returned yet), or that 
	// we've tried and failed.
	std::list<TSTRING> highScores;

	// High score status for the game
	enum HighScoreStatus
	{
		Init,        // initial state: high scores not yet requested
		Requested,   // request initiated, results not received yet
		Received,    // request completed successfully
		Failed       // request completed with error
	};
	HighScoreStatus highScoreStatus = Init;

	// Clear any cached high scores.  This forgets any local copy of
	// the high scores, so that we'll know to get a fresh copy from
	// the NVRAM file the next time we need to display scores.  This
	// should be called when the external NVRAM data might be changed
	// by other programs, such as when we launch the game.
	void ClearCachedHighScores()
	{
		highScores.clear();
		highScoreStatus = Init;
	}

	// Divvy up the game's high score list into groups, invoking the
	// callback for each group.  Groups are separated by blank lines.
	void EnumHighScoreGroups(std::function<void(const std::list<const TSTRING*> &)> func);

	// Enumerate the high score groups for drawing purposes.  This
	// breaks up groups with more than three lines to ensure a fit
	// to the 128x32 DMD layout.
	void DispHighScoreGroups(std::function<void(const std::list<const TSTRING*> &)> func);

	// Get/set the hidden status.  A hidden game isn't shown in the
	// wheel UI, except when the "Hidden Games" filter is selected.
	//
	// A game is hidden if the Hidden column is set to true in the
	// stats DB, OR it has "<enabled>false</enabled>" in its XML
	// database file entry.  We have to keep both representations
	// to maintain compatibility with PinballX database files while
	// also allowing for unconfigured table files that we find in
	// the file system (which, by definition of "unconfigured", 
	// have no XML database entries).
	bool IsHidden() const { return hidden; }
	void SetHidden(bool f, bool updateDatabases = true);

protected:
	GameListItem() = delete;

	struct SpecialListItem { };
	static const SpecialListItem isSpecialListItem;
	GameListItem(const SpecialListItem&) :
		system(nullptr),
		manufacturer(nullptr),
		tableFileSet(nullptr)
	{ 
		CommonInit();
	}

	// Assign an internal ID.  This must be called after the title
	// and system are set up so that GetGameID() can compute the
	// game's actual config ID value.  If the config ID matches a
	// record in the game list's reload map, we'll reuse the
	// internal from the map; otherwise we'll assign a new ID.
	void AssignInternalID();

	// Is the game hidden? 
	bool hidden;
};

// Game list filter.  This selects a subset of games based on
// a selection rule.
class GameListFilter
{
public:
	GameListFilter(const TCHAR *menuGroup, const TCHAR *menuSortKey) :
		menuGroup(menuGroup), 
		menuSortKey(TSTRING(menuGroup) + _T(".") + menuSortKey),
		cmd(0) 
	{ }

	virtual ~GameListFilter() { }

	// filter ID, for saving in the configuration
	virtual TSTRING GetFilterId() const = 0;

	// get the display title of the filter
	virtual const TCHAR *GetFilterTitle() const = 0;

	// Get the menu title of the filter; this is usually the same as 
	// the filter title, but can be overridden separately
	virtual const TCHAR *GetMenuTitle() const { return GetFilterTitle(); }

	// Filter setup/completion.  Before testing a group of games through 
	// the filter, we call 'before' to let the filter set up any temporary
	// state for the scan.  We call 'after' after the scan.
	virtual void BeforeScan() { }
	virtual void AfterScan() { }

	// Is this game included in this filter group?  
	virtual bool Include(GameListItem *game) = 0;

	// Does this filter include hidden games?  We break this out as
	// an extra test to simplify the individual filter Include() tests,
	// since almost all of them would have to include the hidden check
	// if we didn't do this separately.
	virtual bool IncludeHidden() const { return false; }

	// Does this filter specifically select unconfigured games, even
	// when they're hidden from ordinary filters?  As with "hidden", we 
	// break this out as an separate test to simplify the basic Include()
	// tests, as nearly all filters just return false.  
	//
	// Note that this doesn't consider the global option for whether
	// or not to include unconfigured games (GameList.HideUnconfigured),
	// as that's checked separately.  What this method says is whether
	// or not this filter specifically selects unconfigured games when
	// they're otherwise excluded by that option setting.
	virtual bool IncludeUnconfigured() const { return false; }

	// Menu group name
	TSTRING menuGroup;

	// Manu sort key.  This is always qualified by a "Group." prefix,
	// so the filters of a given group sort together.
	TSTRING menuSortKey;

	// Command ID.  This is used to identify filters in menus in 
	// the UI.  We dynamically assign each filter an ID in the range
	// ID_FILTER_FIRST..ID_FILTER_LAST.  Note that the command ID
	// shouldn't be used externally (in config files, for example),
	// as it's arbitrarily assigned in each session and can't be
	// expected to remain the same across sessions.
	int cmd;
};


// "All Games" filter
class AllGamesFilter : public GameListFilter
{
public:
	AllGamesFilter() : GameListFilter(_T("[Top]"), _T("3000")) { title.Load(IDS_FILTER_ALL); }
	virtual const TCHAR *GetFilterTitle() const override { return title.c_str(); }
	virtual bool Include(GameListItem *) override { return true; }
	virtual TSTRING GetFilterId() const override { return _T("All"); }

	TSTRINGEx title;
};

// Favorites filter
class FavoritesFilter : public GameListFilter
{
public:
	FavoritesFilter() : GameListFilter(_T("[Top]"), _T("7000")) { title.Load(IDS_FILTER_FAVORITES); }
	virtual const TCHAR *GetFilterTitle() const override { return title.c_str(); }
	virtual bool Include(GameListItem *game) override;
	virtual TSTRING GetFilterId() const override { return _T("Favorites"); }

	TSTRINGEx title;
};

// Hidden game filter.  This is a special filter that selects
// games that have been otherwise hidden from the UI.  It's the
// only filter that shows these games.
class HiddenGamesFilter : public GameListFilter
{
public:
	HiddenGamesFilter() : GameListFilter(_T("[Op]"), _T("3000")) 
	{ 
		title.Load(IDS_FILTER_HIDDEN); 
		menuTitle.Load(IDS_MENU_SHOW_HIDDEN);
	}
	
	virtual const TCHAR *GetFilterTitle() const override { return title.c_str(); }
	virtual const TCHAR *GetMenuTitle() const override { return menuTitle.c_str(); }
	virtual TSTRING GetFilterId() const override { return _T("Hidden"); }
	virtual bool Include(GameListItem *game) override;
	virtual bool IncludeHidden() const override { return true; }

	TSTRINGEx title;
	TSTRINGEx menuTitle;
};

// Unconfigured game filter.  This is a special filter that
// selects unconfigured games only.  
//
// This can be used whether or not the global "Hide Unconfigured Games"
// setting is in effect.  When it is, this is the only filter that can
// show unconfigured games.  When the global "Hide" setting isn't in
// effect, unconfigured games show up alongside regular games in all of
// the regular filters, but this filter can still be used to limit the
// view to unconfigured games only.
class UnconfiguredGamesFilter : public GameListFilter
{
public:
	UnconfiguredGamesFilter() : GameListFilter(_T("[Op]"), _T("7000") )
	{
		title.Load(IDS_FILTER_UNCONFIGURED); 
		menuTitle.Load(IDS_MENU_SHOW_UNCONFIG);
	}

	virtual const TCHAR *GetFilterTitle() const override { return title.c_str(); }
	virtual const TCHAR *GetMenuTitle() const override { return menuTitle.c_str(); }

	virtual TSTRING GetFilterId() const override { return _T("Unconfigured"); }
	virtual bool Include(GameListItem *game) override;
	virtual bool IncludeUnconfigured() const override { return true; }

	TSTRINGEx title;
	TSTRINGEx menuTitle;
};

// Rating filter
class RatingFilter : public GameListFilter
{
public:
	// the sort key for the star filters is "0", "1", "2", etc, except
	// for "Unrated" (stars == -1), which we want at the end of the list;
	// so give it sort key "Z".
	RatingFilter(int stars) : 
		GameListFilter(_T("[Rating]"), stars >= 0 ? MsgFmt(_T("%d"), stars) : _T("Z")), 
		stars(stars)
	{
		if (stars < 0)
			title.Load(IDS_FILTER_NORATING);
		else
			title = MsgFmt(IDS_FILTER_NSTARS, stars);
	}

	virtual const TCHAR *GetFilterTitle() const override { return title.c_str(); }
	virtual bool Include(GameListItem *game) override;
	virtual TSTRING GetFilterId() const override { return MsgFmt(_T("Rating.%d"), stars).Get(); }

	// number of stars this filter selects for
	int stars;

	// computed title
	TSTRINGEx title;
};

// Category.  A category is essentially a user-defined tag that
// can be associated with a game.  A game can have zero, one, or
// multiple category associations.  
class GameCategory : public GameListFilter
{
public:
	GameCategory(const TCHAR *name) : 
		GameListFilter(_T("[Cat]"), name),
		name(name)
	{ }

	// use the category name as the filter name
	virtual const TCHAR *GetFilterTitle() const override { return name.c_str(); }
	virtual bool Include(GameListItem *game) override;
	virtual TSTRING GetFilterId() const override { return TSTRING(_T("Category.")) + name; }

	// category name
	TSTRING name;
};

// Category filter for uncategorized games
class NoCategory : public GameCategory
{
public:
	NoCategory() : 
		GameCategory(LoadStringT(IDS_UNCATEGORIZED).c_str()) 
	{
		// Make sure we sort at the end of the list of category filters.
		// U+E800 is in the middle of the private use area at the top of 
		// the Unicode Basic Multilingual Plane, so it should reliably sort
		// after any printable characters our other category names.  We
		// use a character in the middle of the private use area to allow
		// for user keys that also sort after all of the regular category
		// strings but before the "uncategorized" element.
		menuSortKey = _T("[Category].\xE800");
	}

	virtual const TCHAR *GetFilterTitle() const override { return name.c_str(); }
	virtual bool Include(GameListItem *game) override;
	virtual TSTRING GetFilterId() const override { return _T("Uncategorized"); }
};

// Date filter: selects games from a date range
class DateFilter : public GameListFilter
{
public:
	DateFilter(const TCHAR *title, int yearFrom, int yearTo) :
		GameListFilter(_T("[Era]"), MsgFmt(_T("%05d"), yearFrom)),
		title(title), yearFrom(yearFrom), yearTo(yearTo) 
	{ }

	virtual const TCHAR *GetFilterTitle() const override { return title.c_str(); }
	virtual bool Include(GameListItem *game) override
		{ return game->year >= yearFrom && game->year <= yearTo; }
	virtual TSTRING GetFilterId() const override 
		{ return MsgFmt(_T("YearRange.%d.%d"), yearFrom, yearTo).Get(); }

	// name of the filter ("70s Tables")
	TSTRING title;

	// date range of included games, inclusive of the endpoints
	int yearFrom;
	int yearTo;
};

// Base recency filter - common base class for the Recently Played
// and Recently Added filters.
class RecencyFilter : public GameListFilter
{
public:
	RecencyFilter(const TCHAR *title, const TCHAR *menuTitle, const TCHAR *group, int days, bool exclude) :
		GameListFilter(group, MsgFmt(_T("%05d"), days)),
		title(title), menuTitle(menuTitle), days(days), exclude(exclude) 
	{ }

	virtual const TCHAR *GetFilterTitle() const override { return title.c_str(); }
	virtual const TCHAR *GetMenuTitle() const override { return menuTitle.c_str(); }

	// Before the scan, cache the timestamp of midnight on the
	// current day in local time
	virtual void BeforeScan();

	// filter title ("Played This Month", "Not Played in a Month")
	TSTRING title;

	// Filter title for menus.  In the menu, we group the recency
	// filters into sections with headers, since the full name gets
	// unwieldy and redundant in menus.  The section headers say
	// "Played within:" or "Not played within:", and the short menu
	// title then just adds the interval ("A week", "A month", etc).
	TSTRING menuTitle;

	// Filter interval in days.  The filter selects games played or
	// not played within this many days of the current day.  Days
	// start at midnight local time.
	int days;

	// Exclude games: if true, this is an exclusion filter for the
	// interval, meaning that it selects games that HAVEN'T been 
	// played with the last 'days' days.
	bool exclude;

	// Most recent midnight.  We set this up in BeforeScan() to 
	// cache the time reference point for the current scan.
	DATE midnight;
};


// Recency (playing) filter: selects games played within a given timeframe
// or not played within a given timeframe.
class RecentlyPlayedFilter : public RecencyFilter
{
public:
	RecentlyPlayedFilter(const TCHAR *title, const TCHAR *menuTitle, int days, bool exclude) :
		RecencyFilter(title, menuTitle, exclude ? _T("[!Played]") : _T("[Played]"), days, exclude) { }

	virtual bool Include(GameListItem *game) override;
	virtual TSTRING GetFilterId() const override 
		{ return MsgFmt(_T("%s.%d"), exclude ? _T("PlayedWithin") : _T("NotPlayedWithin"), days).Get(); }

};

// Never played filter: selects games that have never been played
class NeverPlayedFilter : public GameListFilter
{
public:
	NeverPlayedFilter(const TCHAR *title, const TCHAR *menuTitle) :
		GameListFilter(_T("[!!Played]"), _T("Z")),
		title(title), menuTitle(menuTitle)
	{ }

	virtual bool Include(GameListItem *game) override;
	virtual TSTRING GetFilterId() const override { return _T("NeverPlayed"); }

	virtual const TCHAR *GetFilterTitle() const override { return title.c_str(); }
	virtual const TCHAR *GetMenuTitle() const override { return menuTitle.c_str(); }

	TSTRING title;
	TSTRING menuTitle;

};

// Recency (installation) filter: selects games installed within a given
// timeframe or longer ago than a given timeframe.
class RecentlyAddedFilter : public RecencyFilter
{
public:
	RecentlyAddedFilter(const TCHAR *title, const TCHAR *menuTitle, int days, bool exclude) :
		RecencyFilter(title, menuTitle, exclude ? _T("[!Added]") : _T("[Added]"), days, exclude)
	{ }

	virtual bool Include(GameListItem *game) override;
	virtual TSTRING GetFilterId() const override
		{ return MsgFmt(_T("%s.%d"), exclude ? _T("AddedWithin") : _T("AddedBefore"), days).Get(); }
};

// Manufacturer filter: selects games from the given manufacturer
class GameManufacturer : public GameListFilter
{
public:
	GameManufacturer(const TCHAR *manufacturer) : 
		GameListFilter(_T("[Manuf]"), manufacturer),
		manufacturer(manufacturer), filterTitle(MsgFmt(IDS_FILTER_MANUF, manufacturer))
    { }

	virtual const TCHAR *GetFilterTitle() const override { return filterTitle.c_str(); }
	virtual bool Include(GameListItem *game) override { return game->manufacturer == this; }
	virtual TSTRING GetFilterId() const override { return TSTRING(_T("Manuf.")) + manufacturer; }

	TSTRING filterTitle;
	TSTRING manufacturer;
};

// Game system information
class GameSysInfo
{
public:
	GameSysInfo() { }
	GameSysInfo(const TCHAR *displayName, int configIndex) :
		displayName(displayName),
		configIndex(configIndex)
	{ }

	int configIndex = -1;       // configuration variable index ("System<N>" variable)
	TSTRING displayName;        // display name for the UI
	TSTRING systemClass;		// system class ("VP", "VPX", "FP", empty for others)
	TSTRING mediaDir;           // Media subfolder name (usually the same as the display name)
	TSTRING databaseDir;		// Database subfolder name (usually the same as the display name)
	TSTRING exe;				// executable
	TSTRING tablePath;          // table path
	TSTRING nvramPath;			// non-volatile RAM file path
	TSTRING defExt;             // default extension for table files
	TSTRING params;				// parameters, with macros ([TABLEPATH], [TABLEFILE])
	TSTRING workingPath;		// working path when invoking executable
	TSTRING process;			// process name to monitor
	TSTRING startupKeys;        // startup key sequence
	TSTRING envVars;			// environment variables to add when launching the program
	WORD swShow;                // SW_SHOW flag for launching the table
	TSTRING terminateBy;        // how to terminate running games (CloseWindow, KillProcess)
	TSTRING keepOpen;           // windows to keep open - space-delimited list (bg dmd topper instcard)

	// DOF config tool title prefix.  This is a prefix string that
	// the DOF table mapping list uses for some systems to distinguish
	// their games from the same titles in other systems.  For example,
	// some Future Pinball games are marked with the prefix "FP:".
	TSTRING dofTitlePrefix;

	// Programs to run before and after launching a game of this system.
	// These are specified using the normal CMD or "Run" dialog command
	// line syntax, and can use the same substitution variables allowed
	// in 'params'.
	TSTRING runBeforePre;
	TSTRING runBefore;
	TSTRING runAfter;
	TSTRING runAfterPost;
};

// Table File Set.
//
// This represents the set of "table files" that can potentially be played
// with one or more GameSystems.  In particular, this is the set of files 
// matching the pattern "<table path>\*.<default ext>" for a particular 
// value of that pattern.  
//
// This object's main purpose is to help us figure out what to do with
// "unconfigured" tables, tables, meaning table files that we find in the
// file system folders associated with game systems, but which have no 
// corresponding entries in the table database files.  In cases where
// only a single system is tied to a table file set, we can automatically
// use that single system to play the unconfigured games.  When multiple
// systems connect to a single table file set, we'll be able to offer the
// user a list of the applicable systems.
//
// Each GameSystem object is associated with a single table file set 
// object, but one table file set can be shared among multiple GameSystem
// objects.  This is because multiple systems can have the same path\*.ext
// pattern.  This isn't just some abstract possibility: it's the actual
// situation with VP on most machines.  Most VP users keep several VP 
// versions installed simultaneously in a single folder tree.  The
// official MSI distribution of VP (as of this writing) encourages this 
// by installing four separate VP versions that all use the .vpt file
// extension (8, 9.2, 9.9, and "physmod5") in a single program folder. 
// People keep multiple VP versions installed intentionally, not out of
// laziness about cleaning up obsolete versions, because VP has broken
// backwards compatibility several times over its version history.
// Tables written for an older version generally won't work with newer
// versions beyond a certain point, plus there was the whole "physmod"
// sideways schism that created a separate incompatible set of forked
// versions.
//
class TableFileSet
{
public:
	TableFileSet(const TCHAR *tablePath, const TCHAR *defExt);

	// List of associated systems.  All of these systems use the same
	// table path and extension.
	std::list<GameSystem*> systems;

	// File entry
	class TableFile
	{
	public:
		TableFile(const TCHAR *filename) :
			filename(filename),
			game(nullptr)
		{ }

		// Filename - no path, with original upper/lower casing as
		// found in the directory listing
		TSTRING filename;

		// Game list entry for the file
		GameListItem *game;
	};

	// Find a file entry by filename.  If 'add' is true, and no entry is
	// found for an existing file, we'll add a new entry for the file.
	TableFile *FindFile(const TCHAR *filename, const TCHAR *defExt, bool add);

	// Add a file.  (The filename is the root name, with no path.)
	TableFile *AddFile(const TCHAR *filename);

	// Map of files matching our filename pattern (tablePath\*.defExt),
	// keyed by filename.  The key is the filespec portion, without the 
	// path prefix, converted to lower-case for case-insensitive lookups.
	// The original filename (with original casing) can be recovered 
	// from the corresponding TableFile object at the key.
	std::unordered_map<TSTRING, TableFile> files;

	// Get the map key for a file set given the table path and extension
	static TSTRING GetKey(const TCHAR *tablePath, const TCHAR *defExt);

	// Enumerate files in a folder matching an extension.  If the
	// extension is null or empty, no files match.
	static void ScanFolder(const TCHAR *path, const TCHAR *ext, 
		std::function<void(const TCHAR *filename)> func);

	TSTRING tablePath;		// full path to the system's table folder
	TSTRING defExt;			// default extension for the system's tables (with '.')
};


// System
class GameSystem: public GameSysInfo, public GameListFilter
{
public:
	GameSystem(const TCHAR *displayName, int configIndex) :
		GameSysInfo(displayName, configIndex),
		GameListFilter(_T("[Sys]"), displayName),
		tableFileSet(nullptr),
		filterTitle(MsgFmt(IDS_FILTER_SYSTEM, displayName)),
		elevationApproved(false)
		{ }

	// filter operations
	virtual const TCHAR *GetFilterTitle() const override { return filterTitle.c_str(); }
	virtual bool Include(GameListItem *game) override { return game->system == this; }
	virtual TSTRING GetFilterId() const override { return TSTRING(_T("System.")) + displayName; }

	TSTRING filterTitle;

	// Filename of the "generic" XML game list (database) file for
	// this system.  This is the default list file for the system,
	// with a name of the form <database path>\<system db name>\<system db name>.xml.
	// The <system db name> is the SystemN.DatabaseDir setting from
	// the config file, which defaults to the system's display name.
	// (That roughly matches up with the way PinballX works, so it
	// lets us read existing PinballX databases with mostly defaults
	// in the configuration.)  
	//
	// The "generic" game list is so named because it doesn't imply
	// a category for the games it contains.  PinballX let you use
	// multiple XML files per system, with each file other than the
	// generic one assigning the games within to what it called a
	// "list", which was essentially a category filter that you could
	// select in the UI.  We've generalized the notion of categories
	// so that you can assign multiple categories per game, but for
	// compatibility with existing databases, we still recognize the
	// PinballX single-category system as a starting point, by
	// implicitly assigning a category to each game listed in a non-
	// generic file.
	//
	// Note that the generic file might not exist when we load files.
	// It's perfectly legitimate for a system db directory to contain 
	// only custom files and no generic file.  That's actually the
	// whole reason we need to know the generic file's name here: we
	// might need to create a new generic file on the fly.  When the
	// user adds new games through our UI, we only add to the generic
	// file, since we have have to use a separate storage model to
	// represent multiple categories per game.  So if there wasn't a
	// generic file to start with, we'd have to create one at that
	// point.  Similarly, if the user re-categorizes a game on the 
	// fly, and the game inherited its category from its XML file 
	// source, we have to move the XML for the game from the custom 
	// list file to the generic file, which might require creating
	// the generic file.  
	TSTRING genericDbFilename;

	// Game database files.  Each system can have one or more
	// associated XML files that list its games.
	std::list<std::unique_ptr<GameDatabaseFile>> dbFiles;

	// The table file set associated with this system
	TableFileSet *tableFileSet;

	// Has the user approved Administrator mode elevation for this
	// system?  This is always false initially, and can't be saved
	// in the configuration.  When this is false, we'll ask the user
	// via an interactive menu if they wish to approve running this
	// system in Admin mode whenever we encounter a need to do so.  
	// The "need to do so" arises when the user attempts to launch
	// a game with this system, and our CreateProcess() call fails
	// with an "elevation required" error.  That happens when the
	// system's .exe's manifest contains a requested privilege level 
	// of "require administrator", and our own process isn't itself
	// running in Admin mode, and UAC is enabled on the system.
	// If the user approves the elevation, we record that approval
	// here for the rest of the current session, so that subsequent
	// attempts to run games with the same system don't have to
	// repeat the prompt - we assume that if the user trusts this
	// system on the first run, they'll continue to trust it
	// throughout the current session, so further security prompts
	// would be redundant and annoying.
	//
	// Two things to note.  First, the manifest privilege level
	// "highestAvailable" WON'T trigger an elevation request,
	// because we coerce the child to run as a normal user when we
	// encounter that request level.  That's different from the
	// normal shell and CreateProcess() behavior, but it's safer
	// and more suitable for a game system.  Elevation will only
	// some play when a manifest calls for administrator mode only.
	// Second, we can elevate multiple invocations of the same
	// system .exe (or even of different system .exe's) with only
	// one actual UAC prompt, because we use separate proxy process
	// that runs in elevated mode itself to carry out the actual
	// launches of the admin subprocesses.  That reduces the
	// annoyance level considerably because we don't have to
	// trigger a separate UAC dialog on every launch - just the
	// initial proxy launch.  We still display our own UAC-like
	// security warning on each launch of a new system, since we
	// obviously don't want to take a user's trust in one system
	// to imply that same trust applies to any other systems.  But
	// we do assume that a system trusted once remains trusted, so
	// we limit these per-system warnings to once per system per
	// session.  That once-per feature is fundamentally what this 
	// flag is for.
	bool elevationApproved;
};

// The special "No Game" item
class NoGame : public GameListItem
{
public:
	NoGame();

	// dummy system and manufacturer
	GameSystem dummySystem;
	GameManufacturer dummyManufacturer;
};

// Metafilter.  This is a user-defined filter that's applied after
// the current game list filter.  Metafilters are for Javascript use
// and aren't automatically visible in the UI the way regular filters
// are; it's up to Javascript to provide any desired UI.  Multiple
// metafilters can be active at the same time.
class MetaFilter
{
public:
	MetaFilter(int priority, bool includeExcluded) :
		priority(priority),
		includeExcluded(includeExcluded)
		{ }

	virtual ~MetaFilter() { }

	// Priority order.  This is used to sort the metafilter list
	// each time a new filter is added.  The list is sorted in
	// ascending order of priority.
	int priority;

	// Initialize the filter for a selection run
	virtual void Before() = 0;

	// Test a game for inclusion.  'included' indicates whether
	// the game has been filtered in or out by the main filter
	// and the other metafilters called so far.
	virtual bool Include(GameListItem *game, bool included) = 0;

	// Finish a selection run
	virtual void After() = 0;

	// Should we include games that were excluded by the main
	// filter or by earlier metafilters when calling select()?
	// If this is true, we call this filter for all games, 
	// whether or not they were accepted by the other filters.
	bool includeExcluded;
};


// Master list.  This is the list of all games.
class GameList
{
public:
	// Create/destroy the global singleton
	static void Create();
	static void Shutdown();

	// Re-create.  This deletes the global singleton and creates a new one,
	// saving the current mapping between config IDs and internal IDs.  The
	// saved mapping lets us restore the internal IDs for games that survive
	// the reload, which lets Javascript GameInfo objects survive the reload,
	// if Javascript is in use.xs
	static void ReCreate();

	// Get the reload internal ID for a game, if available; returns 0 if
	// there's no reload map or the game isn't in the map.
	int GetReloadID(GameListItem *game);

	// Initialize.  Loads the game stats database.
	void Init(ErrorHandler &eh);

	// global singleton
	static GameList *Get() { return inst; }

	// Save/restore the configuration settings: current game selection,
	// current filter.
	void SaveConfig();
	void RestoreConfig();

	// save the stats database if dirty
	void SaveStatsDb();

	// save changes to game list (XML) files
	void SaveGameListFiles();

	// get the media folder path
	const TCHAR *GetMediaPath() const { return mediaPath.c_str(); }

	// Find a global media file.  Global media files are media items not
	// associated with any game, such as startup videos, startup audio,
	// and button sounds.  We search the following locations:
	//
	//   <base media folder>\<subfolder>
	//   <PinballY install folder>\Media\<subfolder>
	//   <PinballY install folder>\Assets\<subfolder>
	//   
	bool FindGlobalMediaFile(TCHAR path[MAX_PATH], const TCHAR *subfolder, const TCHAR *file, 
		const TCHAR *const *exts, size_t numExts);

	// Find a global media file of the given type.  This simply calls
	// FindGlobalMediaFile() with the appropriate list of suffixes for
	// the type.
	bool FindGlobalImageFile(TCHAR path[MAX_PATH], const TCHAR *subfolder, const TCHAR *file);
	bool FindGlobalVideoFile(TCHAR path[MAX_PATH], const TCHAR *subfolder, const TCHAR *file);
	bool FindGlobalAudioFile(TCHAR path[MAX_PATH], const TCHAR *subfolder, const TCHAR *file);
	bool FindGlobalWaveFile(TCHAR path[MAX_PATH], const TCHAR *subfolder, const TCHAR *file);

	// Load all game lists
	bool Load(ErrorHandler &eh);

	// Create a system
	GameSystem *CreateSystem(
		const TCHAR *name, int configIndex,
		const TCHAR *sysDatabaseDir, const TCHAR *tablePath, const TCHAR *defExt);

	// Look up a system by config index
	GameSystem *GetSystem(int configIndex);

	// Load a game database XML file
	bool LoadGameDatabaseFile(
		const TCHAR *filename, const TCHAR *parentFolderName,
		GameSystem *system, ErrorHandler &eh);

	// Get the nth game relative to the current game.  0 is the current
	// game.  1 is the next game (to the "right" in wheel order), 2 is
	// the next game after that, etc.  -1 is the previous game ("left" 
	// in wheel order), -2 is the second previous game.
	GameListItem *GetNthGame(int n);

	// Find a game by ID
	GameListItem *GetGameByID(const TCHAR *id);

	// find a game by internal ID
	GameListItem *GetByInternalID(LONG id);

	// Find the next letter group.  This returns the offset from the
	// current game to the first game whose title starts with a different
	// letter.
	int FindNextLetter();

	// Go to the start of the current letter group, or to the start of
	// the previous letter group if we're at the first of a group.
	int FindPrevLetter();

	// Set the current game.  This switches to the nth game relative to
	// the current selection.
	void SetGame(int n);

	// Get the filter list
	const std::vector<GameListFilter*> &GetFilters();

	// Set the current filter.  If the currently selected game passes
	// the new filter, the current game selection isn't affected; if
	// not, we select a game that passes the new filter.  In any case,
	// the adjacent wheel items should always be updated on the display,
	// since those might change with the new filter.
	//
	// Returns true if the change of filter results in changing the
	// currently selected game, which is necessary if the new filter
	// doesn't include the previously selected game.
	bool SetFilter(GameListFilter *filter);

	// Set a filter by command ID.  Returns true if the change of filter
	// results in changing the current game.
	bool SetFilter(int cmdID);

	// Refresh the current filter.  Call this when making a change
	// to one or more games that could affect which games the current
	// filter selects.  Returns true if the change of filter requires
	// changing the current game, which happens if the new filter
	// doesn't include the previously selected game.
	bool RefreshFilter();

	// get a filter by config ID/command code
	GameListFilter *GetFilterById(const TCHAR *id);
	GameListFilter *GetFilterByCommand(int cmdID);

	// Get the current filter
	GameListFilter *GetCurFilter() const { return curFilter; }

	// Get the All Games filter
	GameListFilter *GetAllGamesFilter() { return &allGamesFilter; }

	// Get the Favorites filter
	GameListFilter *GetFavoritesFilter() { return &favoritesFilter; }

	// Get the Hidden Games filter
	GameListFilter *GetHiddenGamesFilter() { return &hiddenGamesFilter; }

	// Get the Unconfigured Games filter
	GameListFilter *GetUnconfiguredGamesFilter() { return &unconfiguredGamesFilter; }

	// Get the number of games matching the current filter
	int GetCurFilterCount() const { return static_cast<int>(byTitleFiltered.size()); }

	// Test to see if a game is selected by a filter.  Note that this
	// should be used instead of calling filter->Include() directly, 
	// as we apply some additional tests.  The "short" version uses
	// the current default setting for hide-unconfigured.
	bool FilterIncludes(GameListFilter *filter, GameListItem *game);
	bool FilterIncludes(GameListFilter *filter, GameListItem *game, bool hideUnconfigured);

	// columns we use in the database file
	const CSVFile::Column *gameCol;
	const CSVFile::Column *lastPlayedCol;
	const CSVFile::Column *dateAddedCol;
	const CSVFile::Column *highScoreStyleCol;
	const CSVFile::Column *playCountCol;
	const CSVFile::Column *playTimeCol;
	const CSVFile::Column *favCol;
	const CSVFile::Column *ratingCol;
	const CSVFile::Column *categoriesCol;
	const CSVFile::Column *hiddenCol;
	const CSVFile::Column *markedForCaptureCol;
	const CSVFile::Column *showWhenRunningCol;
	const CSVFile::Column *audioVolumeCol;

	// Get/set the Last Played time
	const TCHAR *GetLastPlayed(GameListItem *game) 
	    { return lastPlayedCol->Get(GetStatsDbRow(game)); }
	void SetLastPlayed(GameListItem *game, const TCHAR *val) 
	    { lastPlayedCol->Set(GetStatsDbRow(game, true), val); }
	void SetLastPlayed(GameListItem *game, DateTime val)
		{ lastPlayedCol->Set(GetStatsDbRow(game, true), val.ToString().c_str()); }

	// set the last played time to "now"
	void SetLastPlayedNow(GameListItem *game);

	// Get/set the Date Added
	const TCHAR *GetDateAdded(GameListItem *game)
		{ return dateAddedCol->Get(GetStatsDbRow(game)); }
	void SetDateAdded(GameListItem *game, const TCHAR *val)
		{ dateAddedCol->Set(GetStatsDbRow(game, true), val); }
	void SetDateAdded(GameListItem *game, DateTime val)
		{ dateAddedCol->Set(GetStatsDbRow(game, true), val.ToString().c_str()); }

	// set the Date Added to "now"
	 void SetDateAddedNow(GameListItem *game);

	// Get the high score style: DMD (dot matrix display), Alpha (segmented
	// alphanumeric display, like the 1980s Williams machines), TT (typewriter
	// font), None (no high score display).
	const TCHAR *GetHighScoreStyle(GameListItem *game)
		{ return highScoreStyleCol->Get(GetStatsDbRow(game)); }
	void SetHighScoreStyle(GameListItem *game, const TCHAR *val)
		{ highScoreStyleCol->Set(GetStatsDbRow(game, true), val); }

	// get/set the play count
	int GetPlayCount(GameListItem *game) 
	    { return playCountCol->GetInt(GetStatsDbRow(game)); }
	void SetPlayCount(GameListItem *game, int nPlays) 
	    { playCountCol->Set(GetStatsDbRow(game, true), nPlays); }

	// get/set the total play time
	int GetPlayTime(GameListItem *game) { return playTimeCol->GetInt(GetStatsDbRow(game)); }
	void SetPlayTime(GameListItem *game, int t) { playTimeCol->Set(GetStatsDbRow(game, true), t); }

	// get/set the "is favorite" flag
	bool IsFavorite(GameListItem *game) { return favCol->GetBool(GetStatsDbRow(game)); }
	void SetIsFavorite(GameListItem *game, bool f) { favCol->SetBool(GetStatsDbRow(game, true), f); }

	// get/set the game rating, in stars (0-5 scale, as a float value
	// for fractional stars; -1 means unrated)
	float GetRating(GameListItem *game);
	void SetRating(GameListItem *game, float rating);
	void ClearRating(GameListItem *game) { ratingCol->Set(GetStatsDbRow(game), -1.0f); }

	// get/set the audio volume level for this game's media
	int GetAudioVolume(GameListItem *game) { return audioVolumeCol->GetInt(GetStatsDbRow(game), 100); }
	void SetAudioVolume(GameListItem *game, int vol) { audioVolumeCol->Set(GetStatsDbRow(game, true), vol); }

	// get/set the "Marked for batch capture" flag
	bool IsMarkedForCapture(GameListItem *game) { return markedForCaptureCol->GetBool(GetStatsDbRow(game)); }
	void MarkForCapture(GameListItem *game, bool f) { markedForCaptureCol->SetBool(GetStatsDbRow(game, true), f); }
	void ToggleMarkedForCapture(GameListItem *game) { MarkForCapture(game, !IsMarkedForCapture(game)); }

	// Get/set the Hidden status for a game.  
	//
	// *** Be careful about using GameList::IsHidden and SetHidden! ***
	// These methods only read/write the Hidden column in the GameStats.csv
	// file.  There's a separate "hidden" status in the XML, the <enabled>
	// property.  To maintain PinballX compatibility for configured games,
	// we have to update the XML whenever updating the CSV.  We also have
	// to respect the XML when we're importing data from PinballX.  Most
	// accesses/updates to the "hidden" status should therefore go through
	// GameItem::IsHidden()/SetHidden() instead of the GameList method, as
	// the latter only operate on the CSV portion.
	bool IsHidden(GameListItem *game) { return hiddenCol->GetBool(GetStatsDbRow(game)); }
	void SetHidden(GameListItem *game, bool f) { hiddenCol->SetBool(GetStatsDbRow(game, true), f); }

	// get/set the Show When Running window list for a game
	const TCHAR *GetShowWhenRunning(GameListItem *game) { return showWhenRunningCol->Get(GetStatsDbRow(game)); }
	void SetShowWhenRunning(GameListItem *game, const TCHAR *val) { showWhenRunningCol->Set(GetStatsDbRow(game, true), val); }

	// Find a category or create a new one.  If a category already exists 
	// with this name, this simply returns the existing category object;
	// if not, we create a new object and return it.  This doesn't add the
	// category to the filter list.
	GameCategory *FindOrCreateCategory(const TCHAR *name);

	// Add a category to a game's category list
	void AddCategory(GameListItem *game, const GameCategory *category);

	// Remove a category from a game's category list
	void RemoveCategory(GameListItem *game, const GameCategory *category);

	// Replace a game's category list with the given items
	void SetCategories(GameListItem *game, const std::list<const GameCategory*> &cats);

	// Get the game's category list.  This parses the stats database
	// entry for the game, if any, and falls back on the XML file
	// where the game's source entry was listed if there's no stats
	// entry for it.
	void GetCategoryList(GameListItem *game, std::list<const GameCategory*> &cats);

	// Is the given game a member of the given category?
	bool IsInCategory(GameListItem *game, const GameCategory *category);

	// Is the given game uncategorized (i.e., a member of no categories)?
	bool IsUncategorized(GameListItem *game);

	// Get the list of all categories
	std::list<const GameCategory*> GetAllCategories() const;

	// Does a category exist?/Get by name
	bool CategoryExists(const TCHAR *name) const;
	GameCategory *GetCategoryByName(const TCHAR *name) const;

	// Add a new category 
	void NewCategory(const TCHAR *name);

	// Rename a category
	void RenameCategory(GameCategory *category, const TCHAR *name);

	// Delete a category
	void DeleteCategory(GameCategory *category);

	// Get the stats database row number of a given game, creating a
	// row entry if desired.  If the entry doesn't exist, and the 
	// create option isn't set, returns -1 to indicate that the row
	// doesn't exist.
	int GetStatsDbRow(GameListItem *game, bool createIfNotFound = false);

	// Get the stats database row for a given game by ID.
	int GetStatsDbRow(const TCHAR *gameId, bool createIfNotFound = false);

	// Find or add a manufacturer
	GameManufacturer *FindOrAddManufacturer(const TCHAR *name);

	// Find or add the era filter for the given year
	DateFilter *FindOrAddDateFilter(int year);

	// Rebuild the master filter list if necessary
	void CheckMasterFilterList();

	// Enumerate manufacturers
	void EnumManufacturers(std::function<void(const GameManufacturer *)> func);

	// Flush a game's in-memory data to its XML database record
	void FlushToXml(GameListItem *game);

	// Flush an ID change to the stats db
	void FlushGameIdChange(GameListItem *game);

	// Delete a game's XML entry
	void DeleteXml(GameListItem *game);

	// Build/rebuild the title index
	void BuildTitleIndex();

	// Sort the title index.  We call this implicitly after rebuilding
	// the index.  This should also be called any time we change a 
	// game's title, to update the sorting order accordingly.
	void SortTitleIndex();

	// Update the game's system.  This takes care of moving the game's
	// game's XML record to the new system's database file, or creating
	// a new record if we don't have one already.
	void ChangeSystem(GameListItem *game, GameSystem *newSystem);

	// Special dummy game selection, used when no game is selected
	std::unique_ptr<GameListItem> noGame;

	// enumerate the table files sets
	void EnumTableFileSets(std::function<void(const TableFileSet&)> func);

	// Get the number of games in the overall list
	int GetAllGamesCount() const { return static_cast<int>(games.size()); }

	// Get the nth game in the overall list
	GameListItem *GetAllGamesAt(int n) const 
	{
		return n < 0 || static_cast<size_t>(n) >= games.size() ? nullptr : byTitle[n];
	}

	// enumerate all games
	void EnumGames(std::function<void(GameListItem *)> func);

	// enumerate games selected by a given filter
	void EnumGames(std::function<void(GameListItem *)> func, GameListFilter *filter);

	// Add new files discovered dynamically while running.  Returns
	// the number of new items actually added (which might be less
	// than the number of items in the list, since the table file
	// set involved might no longer exist).
	int GameList::AddNewFiles(const TSTRING &path, const TSTRING &ext, 
		const std::list<TSTRING> newFiles);

	// Remove unconfigured games from the UI for game files that are
	// no longer present in the file system.  Each time the program
	// switches from the background to the foreground, the main app
	// logic rescans all table file folders to look for new and removed
	// game files; this is called after one of those scans determines
	// that one or more game files that we found on *previous* scans
	// is no longer present in the file system.  This can happen if
	// the user deletes or renames an old table file, such as when
	// installing a new version of a table or just clearing out old
	// files that are no longer needed.
	//
	// Configured games are kept even if their game files are removed,
	// since a database entry can represent a table independently of
	// the existence of its table file.
	//
	// Returns the number of items actually removed.
	int GameList::RemoveMissingFiles(const TSTRING &path, const TSTRING &ext,
		const std::list<TSTRING> missingFiles);

	// Logging for system setup events
	static void Log(const TCHAR *msg, ...);
	static void LogGroup();

	// Get the most recent midnight in the local time zone, expressed
	// in UTC time.  For example, if it's 7:00pm Pacific Standard Time
	// on January 1, 2018, the most recent local midnight is 
	// 2018-01-01 12:00 PST, which is 2018-01-01 08:00 UTC.
	static DATE GetLocalMidnightUTC();

	// Add a user-defined filter.  Returns true if this filter should be
	// immediately activated, which is the case when this is the active
	// filter in a recently restored configuration.
	bool AddUserDefinedFilter(GameListFilter *filter);

	// Remove a user-defined filter.
	void DeleteUserDefinedFilter(GameListFilter *filter);

	// Enumerate user-defined filters/filter groups
	void EnumUserDefinedFilters(std::function<void(GameListFilter*)> func);
	void EnumUserDefinedFilterGroups(std::function<void(const TSTRING &name, int command)> func);

	// Find the user-defined filter group for a given command
	const TCHAR *GetUserDefinedFilterGroup(int cmd);
	
	// Get the menu command associated with a filter group name.  The
	// group can be a user-defined group or one of the system groups.
	int GetFilterGroupCommand(const TCHAR *group);

	// Add/remove a metafilter.  The caller is responsible for managing
	// the object lifetime: that is, ensuring that the object remains
	// valid for as long as it's in the metafilter list, and deleting
	// the object once it's no longer needed.
	void AddMetaFilter(MetaFilter *mf);
	void RemoveMetaFilter(MetaFilter *mf);

protected:
	GameList();
	~GameList();

	// Global singleton
	static GameList *inst;

	// Game ID map.  When we're reloading the configuration during a
	// session (due to a settings change), we'll create a map of config
	// IDs to internal IDs for all loaded games in the outgoing list,
	// and then store the map in the new game list.  This lets us reuse
	// the same internal IDs for games that survive the reload, so that
	// Javascript GmaeInfo objects continue to point to the same games.
	std::unique_ptr<std::unordered_map<TSTRING, int>> reloadIDMap;

	// Game stats database
	CSVFile statsDb;

	// Game stats database index, by game ID.  This maps a game ID to
	// a row number in the stats DB.
	std::unordered_map<TSTRING, int> statsDbIndex;

	// add a new blank row to the stats database, returning the new row number
	int AddStatsDbRow(const TCHAR *gameId);

	// Add unconfigured table files to the game list
	void AddUnconfiguredGames();

	// Add/remove a category in a game's category list.  These are for
	// internal use only as they don't validate the change and they
	// don't rebuild the stats database list - the caller is responsible
	// for those extra steps.  We provide these internal routines for
	// the sake of making a bundle of changes where the caller will do
	// the necessary checks and do the rebuild at the end of the overall
	// operation.
	void JustAddCategory(GameListItem *game, const GameCategory *category);
	void JustRemoveCategory(GameListItem *game, const GameCategory *category);

	// Handle renaming a category.  If this game is associated with the
	// category, we'll update the stats database entry accordingly.  We'll
	// also update the source file location if necessary.  This is called
	// after the name has already been updated in the category object; the
	// old name is passed in separately in case it's needed.
	void OnRenameCategory(GameListItem *game, const GameCategory *category, const TCHAR *oldName);

	// parse the Categories column in the stats database
	void ParseCategoryList(int rownum);

	// Rebuild the category list for a game.  This updates the stats db
	// row text to match changes in the parsed data object list.
	void RebuildCategoryList(int rownum);

	// parsed category data object
	class ParsedCategoryData : public CSVFile::Column::ParsedData 
	{
	public:
		std::list<const GameCategory*> categories;
	};

	// Get a data file path.  This is used for file paths that we can
	// import from PinballX.  We resolve the path as follows:
	//
	// - If there's an explicit setting in the config, use that.  If this
	//   is specified as a relative path, it's relative to our deployment
	//   folder, otherwise use the exact absolute path given.  The path
	//   can contain the substitution variable [PinballX], which expands
	//   to the full absolute path to the PinballX install folder, if
	//   present.
	//
	// - Otherwise, we check to see if PinballX is installed.  If so, we
	//   show a prompt asking the user whether they want to use the 
	//   PinballX path or the PinballY path.  If not, we use the PinballY
	//   path.  In either case, we update the configuration to use the
	//   selected path in the future.
	//
	TSTRING GetDataFilePath(const TCHAR *configVarName, const TCHAR *defaultFolder,
		int promptStringID, ErrorHandler &eh);

	// Current game, as an index in the byTitleFiltered list
	int curGame;

	// Current filter
	GameListFilter *curFilter;

	// Build the master filter list
	void BuildMasterFilterList();

	// add a filter
	void AddFilter(GameListFilter *f);

	// assign a command ID to a filter
	void AssignFilterCommand(GameListFilter *f);

	// Filter-to-command mapping.  This is permanent throughout the
	// session for the sake of Javascript, so that each filter has a
	// stable command ID that survives game list reloads.
	static std::unordered_map<TSTRING, int> filterCmdMap;

	// Next available filter command ID.  This has session lifetime
	// because we only need to assign a new command ID when we add a 
	// filter that hasn't been added during this session before.  We 
	// reuse the same command ID for a given filter ID throughout the
	// session via the map above.
	static int nextFilterCmdID;

	// User-defined filter group command mapping.  This is the same
	// idea as the filter/command map, but for user-defined groups.
	// A filter group corresponds to a command in the top-level menu
	// to choose a filter from the group.  For example, if the user
	// creates a group of Javascript filters that select by table
	// author, the group might be "Filter by Author"; this would
	// appear as a menu in the main menu alongside "Filter by Era",
	// "Filter by System", etc, and would open a submenu populated
	// by the filters of this group.  The parent menu that appears
	// in the main menu needs its own command, hence this map.
	static std::unordered_map<TSTRING, int> filterGroupCmdMap;
	static int nextFilterGroupCmdID;

	// "all games" filter
	AllGamesFilter allGamesFilter;

	// "favorites" filter
	FavoritesFilter favoritesFilter;

	// "hidden games" filter
	HiddenGamesFilter hiddenGamesFilter;

	// "unconfigured games" filter
	UnconfiguredGamesFilter unconfiguredGamesFilter;

	// "no category" filter
	NoCategory noCategoryFilter;

	// all filters
	std::vector<GameListFilter*> filters;

	// Is the filter list dirty?  If we have to create a new filter on
	// the fly for a newly added manufacturer, decade, or category, we'll
	// set this flat so that we know we have to rebuild the master list.
	bool isFilterListDirty = false;
	
	// all categories
	std::unordered_map<TSTRING, std::unique_ptr<GameCategory>> categories;

	// Deleted categories.  This is a list of category entries that were
	// deleted through the UI.  We keep these objects alive here rather
	// than deleting the memory outright as a hedge against errors; any
	// pointers to these objects kept in other subsystems will remain
	// valid, so we won't crash if we (incorrectly) try to use them.
	std::list<std::unique_ptr<GameCategory>> deletedCategories;

	// Move a game to a new database file.  This is used when a game is
	// categorized by virtue of its placement in an XML source file.
	// We use this to move the game to a category file when adding a
	// category to a game, or to move it a "generic" uncategorized XML
	// file when deleting a category that the game inherits from its
	// file placement.
	//
	// If dbFile is null, we'll find the "generic" database file for
	// the system, creating one if necessary, and move the game there.
	void MoveGameToDbFile(GameListItem *game, GameDatabaseFile *dbFile);

	// Get the generic database file for a system (that is, the one
	// that doesn't imply any category association for the files it
	// contains).  Optionally create the generic file if the system
	// doesn't already have one.
	GameDatabaseFile *GetGenericDbFile(GameSystem *system, bool create);

	// decade filters, by start year
	std::unordered_map<int, DateFilter> dateFilters;

	// manufacturers, by manufacturer name
	std::unordered_map<TSTRING, GameManufacturer> manufacturers;

	// systems, by config index
	std::unordered_map<int, GameSystem> systems;

	// Table file sets, keyed by filename pattern: "<table path>\*.<defExt>".
	// The path is canonicalized ('.' and '..' are expanded), and the whole
	// thing is converted to lower-case for case-insensitive lookup.
	std::unordered_map<TSTRING, TableFileSet> tableFileSets;

	// star rating filters, by stars
	std::unordered_map<int, RatingFilter> ratingFilters;

	// recency filters
	std::list<std::unique_ptr<GameListFilter>> recencyFilters;

	// User-defined filters, keyed by ID
	std::unique_ptr<std::unordered_map<TSTRING, GameListFilter*>> userDefinedFilters;

	// Metafilters, in execution order
	std::unique_ptr<std::vector<MetaFilter*>> metaFilters;

	// Pending user-defined filter to restore from the configuration.
	// The game list configuration is loaded before Javascript is 
	// initialized, so if the active filter in the last session was
	// user-defined, it won't have been created yet when we restore
	// the game list configuration.  When we encounter an undefined
	// user filter during a restore, we save it here.  If and when
	// that filter is created, we make it active.  We clear this if
	// a different filter is explicitly activated before the saved
	// filter is created.
	TSTRING pendingRestoredFilter;

	// game list
	std::list<GameListItem> games;

	// list index, sorted by title
	std::vector<GameListItem*> byTitle;

	// filtered index list, sorted by title
	std::vector<GameListItem*> byTitleFiltered;

	// Populate the table list from PinballX.ini.  This reads the system
	// list information using the PinballX.ini format.
	bool InitFromPinballX(ErrorHandler &eh);

	// Populate the table list from our own config variables.
	bool InitFromConfig(ErrorHandler &eh);

	// Media folder path.  We use the HyperPin/PinballX directory tree
	// structure under this folder.
	TSTRING mediaPath;

	// table of SW_SHOW constants by name
	std::unordered_map<TSTRING, WORD> swShowMap;

	//
	// Wheel navigation paging modes.  "Paging" is stepping through the
	// game wheel one letter at a time, which is normally assigned to the
	// Left/Right Control keys.
	//
	// The tester function takes the title of the current selected game,
	// the title of the proposed new stopping point, and the direction
	// we're moving (+1 for right/next, -1 for left/previous).  It returns
	// a paging group ID, which is an arbitrary identifier for the group
	// that game belongs to - typically this will just be the first letter
	// of the title in canonical case (e.g., lower), but it really could
	// be anything.  The special value 0 means that we shouldn't stop on
	// this game, so it's sort of a null group.
	//
	// We implement several options for paging:
	//
	//     Default = page by first character, so each "page" is a group
	//         of games with the same first character of their titles
	//
	//     AlphaNumSym = group by first character, treating all titles
	//         starting with digits as one page (so '2001' and '8 Ball'
	//         are in the same page), and all titles starting with
	//         any other symbol as another group
	// 
	//     AlphaOnly = page groups are alphabetic only, so we skip
	//         straight from Z back to A (or from A to Z if going in
	//         reverse), skipping any titles whose first characters
	//         are numbers of symbols
	//
	typedef int WheelPagingModeFunc(const TCHAR *title);
	static int WheelPagingDefault(const TCHAR *title);
	static int WheelPagingAlphaNumSym(const TCHAR *title);
	static int WheelPagingAlphaOnly(const TCHAR *title);
	WheelPagingModeFunc *wheelPagingFunc = &WheelPagingDefault;
};

