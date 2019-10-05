// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <regex>
#include <iostream>
#include <fstream>
#include <Shlwapi.h>
#include "../rapidxml/rapidxml_print.hpp"
#include "../Utilities/Config.h"
#include "../Utilities/PBXUtil.h"
#include "../Utilities/GlobalConstants.h"
#include "../Utilities/DateUtil.h"
#include "../Utilities/GraphicsUtil.h"
#include "GameList.h"
#include "Application.h"
#include "LogFile.h"
#include "DialogResource.h"

#include <filesystem>
namespace fs = std::experimental::filesystem;

// include the capture-related variables
#include "CaptureConfigVars.h"

// import the rapidxml namespace for concision
using namespace rapidxml;

// global singleton
GameList *GameList::inst = 0;

// statics
int GameList::nextFilterCmdID = ID_FILTER_FIRST;
std::unordered_map<TSTRING, int> GameList::filterCmdMap;
int GameList::nextFilterGroupCmdID = ID_USER_FILTER_GROUP_FIRST;
std::unordered_map<TSTRING, int> GameList::filterGroupCmdMap;

// config variables for the game list
namespace ConfigVars
{
	static const TCHAR *MediaPath = _T("MediaPath");
	static const TCHAR *TableDatabasePath = _T("TableDatabasePath");
	static const TCHAR *CurGame = _T("GameList.CurrentGame");
	static const TCHAR *CurFilter = _T("GameList.CurrentFilter");
	static const TCHAR *EmptyCategories = _T("GameList.EmptyCategories");
};

void GameList::Create()
{
	if (inst == 0)
		inst = new GameList();
}

void GameList::Shutdown()
{
	delete inst;
	inst = 0;
}

void GameList::ReCreate()
{
	// create a mapping between config IDs and internal IDs for the currently
	// loaded games
	auto idMap = std::make_unique<std::unordered_map<TSTRING, int>>();
	for (auto &g : inst->games)
		idMap->emplace(g.GetGameId(), g.internalID);

	// take ownership of the user-defined filter list and metafilter list,
	// so that we can transfer them to the new game list instance
	decltype(inst->userDefinedFilters) udf(inst->userDefinedFilters.release());
	decltype(inst->metaFilters) mf(inst->metaFilters.release());

	// delete the existing instance
	Shutdown();

	// create a new instance
	Create();

	// store the ID map in the new instance
	inst->reloadIDMap.reset(idMap.release());
	
	// pass the user-defined filter list and metafilter list to the new game list
	inst->userDefinedFilters.reset(udf.release());
	inst->metaFilters.reset(mf.release());
}

int GameList::GetReloadID(GameListItem *game)
{
	// if there's a reload map, look up the game
	if (reloadIDMap != nullptr)
	{
		// look up the game by config ID
		if (auto it = reloadIDMap->find(game->GetGameId()); it != reloadIDMap->end())
			return it->second;
	}

	// no map or game not found
	return 0;
}

GameList::GameList()
{
	// clear variables
	curGame = -1;

	// create the dummy "no game" game
	noGame.reset(new NoGame());

	// create the user-defined filters list and metafilter list
	userDefinedFilters.reset(new decltype(userDefinedFilters)::element_type);
	metaFilters.reset(new decltype(metaFilters)::element_type);

	// start with the All Games filter
	curFilter = &allGamesFilter;

	// Set up our stats columns
	gameCol = statsDb.DefineColumn(_T("Game"));
	lastPlayedCol = statsDb.DefineColumn(_T("Last Played"));
	playCountCol = statsDb.DefineColumn(_T("Play Count"));
	playTimeCol = statsDb.DefineColumn(_T("Play Time"));
	favCol = statsDb.DefineColumn(_T("Is Favorite"));
	ratingCol = statsDb.DefineColumn(_T("Rating"));
	audioVolumeCol = statsDb.DefineColumn(_T("Audio Volume"));
	categoriesCol = statsDb.DefineColumn(_T("Categories"));
	hiddenCol = statsDb.DefineColumn(_T("Is Hidden"));
	dateAddedCol = statsDb.DefineColumn(_T("Date Added"));
	highScoreStyleCol = statsDb.DefineColumn(_T("High Score Style"));
	markedForCaptureCol = statsDb.DefineColumn(_T("Marked For Capture"));
	showWhenRunningCol = statsDb.DefineColumn(_T("Show When Running"));

	// populate the SW_SHOWxxx table
	// populate the SW_SHOW table
#define SetShowMap(s) swShowMap[_T(#s)] = s
	SetShowMap(SW_FORCEMINIMIZE);
	SetShowMap(SW_HIDE);
	SetShowMap(SW_MAXIMIZE);
	SetShowMap(SW_MINIMIZE);
	SetShowMap(SW_RESTORE);
	SetShowMap(SW_SHOW);
	SetShowMap(SW_SHOWDEFAULT);
	SetShowMap(SW_SHOWMAXIMIZED);
	SetShowMap(SW_SHOWMINIMIZED);
	SetShowMap(SW_SHOWMINNOACTIVE);
	SetShowMap(SW_SHOWNA);
	SetShowMap(SW_SHOWNOACTIVATE);
	SetShowMap(SW_SHOWNORMAL);
#undef SetShowMap
}

GameList::~GameList()
{
}

void GameList::Init(ErrorHandler &eh)
{
	// get the expanded media path
	mediaPath = GetDataFilePath(ConfigVars::MediaPath, _T("Media"), IDS_DEFAULT_MEDIA_PATH_PROMPT, eh);

	// find the game stats database file
	TCHAR statsFile[MAX_PATH];
	GetDeployedFilePath(statsFile, _T("GameStats.csv"), _T(""));
	statsDb.SetFile(statsFile);

	// load the game stats database, if it exists
	if (FileExists(statsFile))
		statsDb.Read(SilentErrorHandler());

	// initialize the stats database
	size_t nRows = statsDb.GetNumRows();
	for (int i = 0; i < (int)nRows; ++i)
	{
		// add a game index entry for the game ID at this row
		if (const TCHAR *id = gameCol->Get(i); id != nullptr)
			statsDbIndex.emplace(id, i);

		// Parse the categories column.  This is stored in the CSV file
		// as a string containing a list of category names; this tokenizes
		// the list and converts it to a list of GameCategory pointers for
		// fast run-time access when filtering by category.
		ParseCategoryList(i);
	}
}

void GameList::SaveConfig()
{
	// get the current game selection
	TSTRING newSel;
	if (GameListItem *game = GetNthGame(0); game != nullptr)
		newSel = game->GetGameId();

	// if it's different from the value stored in the config, update the config
	ConfigManager *cfg = ConfigManager::GetInstance();
	if (_tcscmp(cfg->Get(ConfigVars::CurGame, _T("")), newSel.c_str()) != 0)
		cfg->Set(ConfigVars::CurGame, newSel.c_str());

	// save the current filter
	auto curFilterId = curFilter->GetFilterId();
	if (_tcscmp(cfg->Get(ConfigVars::CurFilter, _T("")), curFilterId.c_str()) != 0)
		cfg->Set(ConfigVars::CurFilter, curFilterId.c_str());

	// Figure out which categories are "empty" - i.e., no games are
	// assigned to them.  Categories used in games will be naturally
	// recoverable from the game database, since we list the category
	// for each game there.  Empty categories won't be mentioned 
	// anywhere else, though, so we need to save them separately in
	// the config under the GameList.EmptyCategories variable.
	// Start by going through all of the games and marking the
	// categories they mention as "in use".
	std::unordered_set<const GameCategory*> usedCategories;
	for (auto &game : games)
	{
		// get this game's category list
		std::list<const GameCategory*> gameCategories;
		GetCategoryList(&game, gameCategories);

		// mark each one as in-use
		for (auto cat : gameCategories)
			usedCategories.emplace(cat);
	}

	// Now go through all of the categories and add the ones that
	// don't appear in the in-use set to a list of empty categories.
	std::list<TSTRING> emptyCategories;
	for (auto &cat : categories)
	{
		// if this one isn't in the in-use set, it's empty
		if (usedCategories.find(cat.second.get()) == usedCategories.end())
			emptyCategories.push_back(cat.second->name);
	}

	// construct the string form of the list
	TSTRING val;
	CSVFile::CSVify(emptyCategories, [&val](const TCHAR *seg, size_t len) {
		val.append(seg, len);
		return true; 
	});

	// store the value
	if (_tcsicmp(cfg->Get(ConfigVars::EmptyCategories, _T("")), val.c_str()) != 0)
		cfg->Set(ConfigVars::EmptyCategories, val.c_str());
}

void GameList::SaveStatsDb()
{
	SilentErrorHandler eh;
	statsDb.WriteIfDirty(eh);
}

void GameList::SaveGameListFiles()
{
	// set up a capturing error handler
	CapturingErrorHandler eh;

	// scan the filter list for systems
	for (auto f : filters)
	{
		if (auto sys = dynamic_cast<GameSystem*>(f); sys != nullptr)
		{
			// This is a system entry.  Scan its list of game list XML
			// files and save any changes.
			for (auto &d : sys->dbFiles)
			{
				if (d->isDirty)
				{
					// If the destination folder doesn't exist, create it
					TCHAR dir[MAX_PATH];
					_tcscpy_s(dir, d->filename.c_str());
					PathRemoveFileSpec(dir);
					if (!DirectoryExists(dir))
						CreateSubDirectory(dir, NULL, NULL);

					// Write the XML file.  Do this in two stages:  first, write the
					// contents to a temp file in the same folder, with the same name
					// as the XML file but with ~ appended to the name.  Then delete
					// the original file and rename the temp file to replace it.  The
					// staged procedure is to reduce the chances of corrupting or
					// losing the original file data: if anything goes wrong while
					// writing the XML, the temp file is the only thing affected, as
					// we haven't even touched the original file yet.  We'll only
					// replace the original file after we're sure that the new file
					// has been successfully written.
					TSTRING tmpfile = d->filename + _T("~");
					std::ofstream os;
					os.open(tmpfile.c_str());

					// Print the XML.  The PinballX game list editor wrote empty tags
					// as full begin-end tag pairs ("<tag></tag>", rather than using 
					// the more typical XML empty-tag shorthand "<tag/>".  So we'll
					// do the same thing just to minimize the amount of change we
					// introduce when rewriting files.  This will also make sure that
					// the files remain PinballX compatible even after we've mucked
					// with them, in case someone tries this program and decides to
					// switch back after all.  (PinballX doesn't seem to have any
					// problem reading back the "<tag/>" format, but just in case.)
					rapidxml::print<char>(os, d->doc, rapidxml::print_expand_empty_tags);
					os.close();

					// check for errors
					if (!os.good())
					{
						// write failed - report a file write error on the temp file
						eh.Error(MsgFmt(IDS_ERR_WRITEFILE, tmpfile.c_str(), FileErrorMessage(errno).c_str()));

						// Delete the temp file, if possible, but ignore errors.  If
						// this fails, the worst that happens is that we leave behind
						// a harmless extra file.  Plus, the name should suggest to the 
						// user that it's a temp file, as most of the MSFT productivity 
						// applications use the same naming convention.  So the user
						// will likely know that they can safely hand-delete it if
						// they ever even notice that it's there, and if not, that's
						// fine too; it'll just take up a small amount of disk space
						// in the meantime.
						DeleteFile(tmpfile.c_str());
					}
					else
					{
						// Success - replace the original file with the temp file.
						//
						// If this is the first time we've written the file during this
						// session, rename the original file as a backup copy, just in
						// case anything got screwed up in our update.  Do this only
						// once per session, as we might save several copies, and it
						// would defeat the purpose to save our own intermediate
						// updates as backups.
						TSTRING backup = d->filename + _T(".bak");
						bool ok = true;
						if (d->isBackedUp)
						{
							// We've already done a backup, so just delete any
							// existing copy of the final file.
							DeleteFile(d->filename.c_str());
						}
						else if (FileExists(d->filename.c_str()))
						{
							// We haven't done a backup yet, and the original file
							// exists, so rename it as a backup.  Delete any prior
							// backup first so that the rename succeeds.
							DeleteFile(backup.c_str());
							if (!MoveFile(d->filename.c_str(), backup.c_str()))
							{
								// rename original as backup failed
								WindowsErrorMessage winerr;
								eh.Error(MsgFmt(IDS_ERR_MOVEFILE, d->filename.c_str(), backup.c_str(), winerr.Get()));
								ok = false;
							}
						}

						// If all is well, rename the temp file as the actual file
						if (ok)
						{
							// note that we've done our backup
							d->isBackedUp = true;

							// rename the file
							if (!MoveFile(tmpfile.c_str(), d->filename.c_str()))
							{
								// rename temp as original failed
								WindowsErrorMessage winerr;
								eh.Error(MsgFmt(IDS_ERR_MOVEFILE, tmpfile.c_str(), d->filename.c_str(), winerr.Get()));
								ok = false;
							}
						}

						// check how that all went
						if (ok)
						{
							// success - the on-disk version is now in sync with the
							// in-memory data
							d->isDirty = false;
						}
						else
						{
							// failed - delete the temp file (if it still exists) so 
							// that we don't leave cruft behind
							DeleteFile(tmpfile.c_str());
						}
					}
				}
			}
		}
	}

	// if we caught any errors, report them
	if (eh.CountErrors() != 0)
	{
		Application::InUiErrorHandler uieh;
		uieh.GroupError(EIT_Error, MsgFmt(IDS_ERR_SAVEGAMELIST), eh);
	}
}

void GameList::RestoreConfig()
{
	// set the current filter first
	ConfigManager *cfg = ConfigManager::GetInstance();
	if (const TCHAR *filterId = cfg->Get(ConfigVars::CurFilter); filterId != 0)
	{
		// search for the filter
		bool found = false;
		if (auto f = GetFilterById(filterId); f != nullptr)
		{
			found = true;
			SetFilter(f);
		}

		// If we didn't find the filter, and it's user-defined, it might just
		// not have been created yet.  Set it as the pending filter.
		if (!found && tstrStartsWith(filterId, _T("User.")))
			pendingRestoredFilter = filterId;
	}

	// look up the current game, if any
	if (const TCHAR *gameId = cfg->Get(ConfigVars::CurGame); gameId != 0)
	{
		// search for the game in the list selected by the filter
		for (int i = 0 ; i < (int)byTitleFiltered.size() ; ++i)
		{
			if (byTitleFiltered[i]->GetGameId() == gameId)
			{
				curGame = i;
				break;
			}
		}
	}

	// create category objects for empty categories
	if (const TCHAR *emptyCats = cfg->Get(ConfigVars::EmptyCategories, nullptr); emptyCats != nullptr)
	{
		// parse it from CSV format
		std::list<TSTRING> cats;
		CSVFile::ParseCSV(emptyCats, -1, cats);

		// add the category filters
		for (auto const &cat : cats)
			NewCategory(cat.c_str());
	}
}

void GameList::SetLastPlayedNow(GameListItem *game)
{
	// Set the last played time to the current system time.  Note
	// that this stores a UTC value!  UTC is the right format for
	// storage because it's invariant with respect to local time
	// zone changes.  For example, there will be no confusion
	// about this point in time if we switch from Standard Time to
	// Daylight Time or vice versa, and there will never be any
	// confusion if the local rules for when Daylight Time starts
	// ever change (as they do with surprising frequency).
	DateTime d;
	SetLastPlayed(game, d.ToString().c_str());
}

void GameList::SetDateAddedNow(GameListItem *game)
{
	// Set the Date Added to the current UTC time
	DateTime d;
	SetDateAdded(game, d.ToString().c_str());
}

float GameList::GetRating(GameListItem *game)
{
	// If there's a row and column in our stats database for the 
	// game, use the rating stored there.  This overrides any
	// imported rating from the XML file.  If there's no entry
	// in the stats database, use the XML file value.
	int row = GetStatsDbRow(game, false);
	if (row >= 0)
	{
		// check if the column has a non-empty value; if so,
		// interpret it as a float and return the value
		const TCHAR *val = ratingCol->Get(row, nullptr);
		if (val != nullptr && val[0] != 0)
			return (float)_ttof(val);
	}

	// There's no stats database entry, so fall back on the
	// value from the XML database.  In the PinballX XML
	// schema, a rating of 0 means "undefined".  In contrast,
	// we use 0 as a valid rating meaning "0 stars" and -1
	// to mean "unrated".  So translate a 0 in the PBX data
	// to a -1 in our schema.
	return game->pbxRating > 0.0f ? game->pbxRating : -1.0f;
}

void GameList::SetRating(GameListItem *game, float rating)
{
	// set the rating in the stats database, creating the row as needed
	ratingCol->Set(GetStatsDbRow(game, true), rating);

	// Set the PinballX rating.  -1 in our scheme means "undefined",
	// which PBX represents as zero.  PBX has no way to represent zero
	// stars, so store a zero star rating as the lowest PBX rating of
	// one star.  PBX also can't store half stars, so round half stars
	// up to the next full point.
	if (rating < 0.0f)
		game->pbxRating = 0.0f;
	else if (rating == 0.0f)
		game->pbxRating = 1.0f;
	else
		game->pbxRating = ceilf(rating);

	// flush the changes to the XML record, if present
	FlushToXml(game);
}

GameListItem *GameList::GetNthGame(int n)
{
	// if there's no current game, there's nothing to be relative to
	if (curGame < 0)
		return noGame.get();

	// if the filter is empty, there's no current game
	int cnt = (int)byTitleFiltered.size();
	if (cnt == 0)
		return noGame.get();

	// get the new index in the filtered game list, wrapping
	return byTitleFiltered[Wrap(curGame + n, cnt)];
}

GameListItem *GameList::GetGameByID(const TCHAR *id)
{
	// scan for a game matching the ID
	for (auto &game : byTitle)
	{
		if (game->GetGameId() == id)
			return game;
	}

	// not found
	return nullptr;
}

GameListItem *GameList::GetByInternalID(LONG id)
{
	for (auto &game : byTitle)
	{
		if (game->internalID == id)
			return game;
	}
	return nullptr;
}

int GameList::FindNextLetter()
{
	// if there's no current game, no search is possible
	if (curGame < 0)
		return 0;

	// get the current game's first letter in lower case
	TCHAR l = _totlower(byTitleFiltered[curGame]->title[0]);

	// scan ahead from the current game
	int cnt = (int)byTitleFiltered.size();
	for (int i = (curGame + 1) % cnt, n = 1; i != curGame; i = (i + 1) % cnt, ++n)
	{
		// check for a different first letter
		TCHAR c = _totlower(byTitleFiltered[i]->title[0]);
		if (c != l)
			return n;
	}

	// nothing found - stay on the current game
	return 0;
}

int GameList::FindPrevLetter()
{
	// if there's no current game, no search is possible
	if (curGame < 0)
		return 0;

	// We want to back up to the start of the current letter group,
	// or to the start of the previous group if we're already at the
	// start of a group.  We can accomplish both by searching for the
	// nearest previous item with a different letter from the item
	// just before the current item.  So start by backing up one spot.
	int cnt = (int)byTitleFiltered.size();
	int i = Wrap(curGame - 1, cnt);

	// get this item's first letter
	TCHAR l = _totlower(byTitleFiltered[i]->title[0]);

	// now scan backwards until we find an item with a different first letter
	for (int n = -1; i != curGame; i = Wrap(i - 1, cnt), --n)
	{
		// Check the first letter of this item.  If it differs, we've found
		// the last item in the prior group, so return the *next* item, which
		// is the first item in the group we're looking for.
		TCHAR c = _totlower(byTitleFiltered[i]->title[0]);
		if (c != l)
			return n + 1;
	}

	// nothing found - stay on the current game
	return 0;
}

void GameList::SetGame(int n)
{
	// do nothing if there's no active game or the filter is empty
	int cnt = (int)byTitleFiltered.size();
	if (curGame < 0 || cnt == 0)
		return;

	// switch to the new game, wrapping at the ends of the list
	curGame = Wrap(curGame + n, cnt);
}

GameListFilter *GameList::GetFilterById(const TCHAR *id)
{
	// search for a filter matching the ID
	for (auto it : filters)
	{
		if (it->GetFilterId() == id)
			return it;
	}

	// not found
	return nullptr;
}

GameListFilter *GameList::GetFilterByCommand(int cmdID)
{
	// search for the filter by command ID
	for (auto f : filters)
	{
		if (f->cmd == cmdID)
			return f;
	}

	// not found
	return nullptr;
}

void GameList::RefreshFilter()
{
	// Remember the current selection, if any
	const GameListItem *oldSel = GetNthGame(0);

	// reset the filter list
	byTitleFiltered.clear();
	curGame = -1;

	// initialize the filter
	curFilter->BeforeScan();

	// initialize all metafilters
	for (auto &mf : *metaFilters.get())
		mf->Before();

	// note if the "Hide Unconfigured Games" option is set
	bool hideUnconfigured = Application::Get()->IsHideUnconfiguredGames();

	// Construct the new list of games that pass the filter
	auto pfv = Application::Get()->GetPlayfieldView();
	for (auto game : byTitle)
	{
		// Test the game against the current filter
		bool include = FilterIncludes(curFilter, game, hideUnconfigured);

		// Invoke the metafilters
		for (auto &mf : *metaFilters.get())
		{
			// Call the filter if the game has passed the other filters
			// so far, OR the metafilter reconsiders excluded games.
			if (include || mf->includeExcluded)
			{
				// The result from this metafilter overrides the prior
				// inclusion status.
				include = mf->Include(game, include);
			}
		}

		// If this game is included, add it to the list
		if (include)
		{
			// note its new index, and add it to the list
			int idx = static_cast<int>(byTitleFiltered.size());
			byTitleFiltered.push_back(game);

			auto IsLexicallyCloser = [](const TSTRING &newName, const TSTRING &oldName, const TSTRING &refName)
			{
				// compare the strings character by character
				for (size_t i = 0; ; ++i)
				{
					// get the current character from each string
					TCHAR cNew = i < newName.length() ? newName[i] : 0;
					TCHAR cOld = i < oldName.length() ? oldName[i] : 0;
					TCHAR cRef = i < refName.length() ? refName[i] : 0;

					// figure the lexical distance new-to-ref and old-to-ref
					int newDist = abs(cNew - cRef);
					int oldDist = abs(cOld - cRef);

					// If the new distance is less than the old distance, the new
					// string is indeed closer than the old string.
					if (newDist < oldDist)
						return true;

					// If the new distance is greater than the old distance, the
					// new string is further away than the old string.
					if (newDist > oldDist)
						return false;

					// The distances are equal for this character position, so we 
					// need to look at the next characters.  If there aren't any
					// more characters, the names must all be identical, so the
					// new name isn't any closer than the old name.  Note that we
					// must be at the end of ALL of the strings if we're at the
					// end of any of them, since we know they're all identical
					// to this point - if they weren't, we would have found a
					// non-zero distance to one or the other above and we would
					// have already returned.
					if (cNew == 0)
						return false;
				}
			};

			// If a game was previously selected, check this game's title
			// against the closest matching game so far, and keep the one
			// that's closest to the old name.  This will give us a selection
			// after the filter update that's at least alphabetically close
			// to the old selection.  This will leave the same game selected
			// if it's present in the new list, since it will obviously have
			// the shortest distance (zero) from its own name.  If we don't
			// have a candidate yet, this game is inherently the closest so
			// far - this also has the side effect of selecting the first
			// game we encounter, so as long the new filter matches at least
			// one game, we'll end up with something selected.
			const std::vector<GameListItem*> bTF = byTitleFiltered;
			if (oldSel != nullptr && 
				(curGame == -1 ||
					IsLexicallyCloser(game->title + _T(".") + (game->system != nullptr ? game->system->displayName : _T("")), 
						bTF[curGame]->title + _T(".") + (bTF[curGame]->system != NULL ? bTF[curGame]->system->displayName : _T("")),
						oldSel->title + _T(".") + (oldSel->system != NULL ? oldSel->system->displayName : _T("")))))
				curGame = idx;
		}
	}

	// end the scan in the main filter
	curFilter->AfterScan();

	// end the scan in the metafilters
	for (auto &mf : *metaFilters.get())
		mf->After();
}

bool GameList::FilterIncludes(GameListFilter *filter, GameListItem *game)
{
	return FilterIncludes(filter, game, Application::Get()->IsHideUnconfiguredGames());
}

bool GameList::FilterIncludes(GameListFilter *filter, GameListItem *game, bool hideUnconfigured)
{
	// If this game is hidden or disabled, check to see if the filter passes
	// hidden games.  If not, skip it.
	if (game->IsHidden() && !filter->IncludeHidden())
		return false;

	// If the game is unconfigured, and the config options are set to hide
	// unconfigured games, hide it unless the filter specifically selects
	// unconfigured.
	if (!game->isConfigured && hideUnconfigured && !filter->IncludeUnconfigured())
		return false;

	// This game passes the generic tests, so test it via the filter
	return filter->Include(game);
}

DATE GameList::GetLocalMidnightUTC()
{
	// Start with the current local system time.  Note that we have to 
	// start with the local time, even though we ultimately want the 
	// result to be in the UTC domain, because we want "today" to have
	// its plain meaning in terms of the local clock.
	SYSTEMTIME localNow;
	GetLocalTime(&localNow);

	// Adjust it to the most recent midnight in local time
	SYSTEMTIME localMidnight = localNow;
	localMidnight.wHour = 0;
	localMidnight.wMinute = 0;
	localMidnight.wSecond = 0;
	localMidnight.wMilliseconds = 0;

	// Now we have the most recent midnight in local time, expressed in 
	// the local time zone.  Convert it to the corresonding UTC time/date.
	// Note that the UTC value might be on a different day, since the 
	// change from local to  UTC can cross a date boundary (e.g., 
	// 2019-01-01 23:00 PST is 2019-01-02 07:00 UTC).  But that's okay!
	// It's the absolute point in time that matters, and we've already 
	// figured what we need to figure in terms of the local clock and 
	// calendar.
	SYSTEMTIME utcMidnight;
	TzSpecificLocalTimeToSystemTime(NULL, &localMidnight, &utcMidnight);

	// Convert to a Variant DATE value.  This is the ideal format for
	// our purposes here because it represents the date/time value as a
	// number of days since an epoch (a fixed zero point in the past).
	// That makes it easy to work in terms of days between dates.
	DATE dMidnight;
	SystemTimeToVariantTime(&utcMidnight, &dMidnight);

	// return the Variant DATE result
	return dMidnight;
}

const std::vector<GameListFilter*> &GameList::GetFilters()
{
	// make sure the list is up to date
	CheckMasterFilterList();

	// return a reference to our internal list
	return filters;
}

void GameList::EnumUserDefinedFilters(std::function<void(GameListFilter*)> func)
{
	for (auto &f : *userDefinedFilters)
		func(f.second);
}

void GameList::EnumUserDefinedFilterGroups(std::function<void(const TSTRING &name, int command)> func)
{
	// make sure the filter list is up to date
	CheckMasterFilterList();

	// populate a set of user-defined group names
	std::unordered_set<TSTRING> groups;
	for (auto f : filters)
	{
		// system group names are enclosed in [square brackets], so anything
		// without square brackets is a user-defined group
		if (f->menuGroup.length() != 0 && f->menuGroup[0] != '[' && groups.find(f->menuGroup) == groups.end())
			groups.emplace(f->menuGroup);
	}

	// enumerate the groups for the caller
	for (auto const &g : groups)
		func(g, filterGroupCmdMap[g]);
}

// System filter groups
static const struct
{
	const TCHAR *name;
	int cmd;
}
sysFilterGroups[] = {
	_T("[Era]"), ID_FILTER_BY_ERA,
	_T("[Manuf]"), ID_FILTER_BY_MANUF,
	_T("[Sys]"), ID_FILTER_BY_SYS,
	_T("[Rating]"), ID_FILTER_BY_RATING,
	_T("[Cat]"), ID_FILTER_BY_CATEGORY,
	_T("[Played]"), ID_FILTER_BY_RECENCY,
	_T("[!Played]"), ID_FILTER_BY_RECENCY,
	_T("[!!Played]"), ID_FILTER_BY_RECENCY,
	_T("[Added]"), ID_FILTER_BY_ADDED,
	_T("[!Added]"), ID_FILTER_BY_ADDED
};

const TCHAR *GameList::GetUserDefinedFilterGroup(int cmd)
{
	// search for a user-defined group
	for (auto const &g : filterGroupCmdMap)
	{
		if (g.second == cmd)
			return g.first.c_str();
	}

	// search for a system group
	for (size_t i = 0; i < countof(sysFilterGroups); ++i)
	{
		if (sysFilterGroups[i].cmd == cmd)
			return sysFilterGroups[i].name;
	}

	// not found
	return nullptr;
}

int GameList::GetFilterGroupCommand(const TCHAR *group)
{
	// there's no command for a null or empty group name
	if (group == nullptr || group[0] == 0)
		return 0;

	// try a user-defined name
	if (auto it = filterGroupCmdMap.find(group); it != filterGroupCmdMap.end())
		return it->second;

	// test against the built-in group names
	if (group[0] == '[')
	{
		for (size_t i = 0; i < countof(sysFilterGroups); ++i)
		{
			if (_tcscmp(group, sysFilterGroups[i].name) == 0)
				return sysFilterGroups[i].cmd;
		}
	}

	// not found 
	return 0;
}

void GameList::SetFilter(int cmdID)
{
	if (auto f = GetFilterByCommand(cmdID); f != nullptr)
		SetFilter(f);
}

void GameList::SetFilter(GameListFilter *filter)
{
	// set the new filter
	curFilter = filter;

	// if there's a pending restore filter, clear it when explicitly
	// setting a different filter
	pendingRestoredFilter.clear();

	// Refresh the filter selection
	RefreshFilter();
}

// Initialize from the PinballX .ini file.  This isn't currently used,
// as we prefer to load from our own config file instead, since the PBX
// data doesn't exactly match our internal model.  This is here in case
// anyone wants to fork the project to create a more direct drop-in 
// replacement for PinballX.  It might also be useful as a reference
// to see how the PinabllX INI variables map to our own settings.
bool GameList::InitFromPinballX(ErrorHandler &eh)
{
	// if the PinballX path is empty in the configuration, skip this
	const TCHAR *pbxPath = ConfigManager::GetInstance()->Get(_T("PinballXPath"), nullptr);
	if (pbxPath == 0)
		return true;

	// read the PinballX .ini file
	TCHAR path[MAX_PATH];
	PathCombine(path, pbxPath, _T("Config\\PinballX.ini"));
	long len;
	std::unique_ptr<wchar_t> ini(ReadFileAsWStr(path, eh, len, ReadFileAsStr_NullTerm));
	if (ini.get() == 0)
		return false;

	// current section name and variables
	WSTRING sect;
	struct
	{
		void Add(const WCHAR *name, const WCHAR *val)
		{
			TSTRING key = WideToTSTRING(name);
			std::transform(key.begin(), key.end(), key.begin(), ::towlower);
			map.emplace(key, val);
		}
		const TCHAR *Get(const WCHAR *name, const TCHAR *defval = 0)
		{
			if (auto it = map.find(name); it != map.end())
				return it->second.c_str();
			else
				return defval;
		}
		bool GetBool(const WCHAR *name, bool defval = false)
		{
			if (auto it = map.find(name); it != map.end())
			{
				TSTRING val = it->second;
				std::transform(val.begin(), val.end(), val.begin(), ::towlower);
				return val == _T("true") || val == _T("1") || val == _T("yes");
			}
			return defval;
		}
		std::unordered_map<WSTRING, TSTRING> map;
	}
	vars;

	// close a section
	int systemIndex = 0;
	auto closeSect = [this, &eh, &sect, &vars, pbxPath, &systemIndex]() -> bool
	{
		// if the section is disabled, skip it - simply return success
		if (!vars.GetBool(L"enabled"))
			return true;

		// if this is one of the pre-defined systems or a custom
		// system, scan its game lists
		TSTRING sys;
		TSTRING dofTitlePrefix;
		TSTRING defExt;
		if (sect == L"VisualPinball")
		{
			sys = _T("Visual Pinball");
			defExt = _T(".vpt");
		}
		else if (sect == L"FuturePinball")
		{
			sys = _T("Future Pinball");
			dofTitlePrefix = _T("FP");
			defExt = _T(".fpt");
		}
		else if (sect == L"PinballFX2")
		{
			sys = _T("Pinball FX2");
			dofTitlePrefix = _T("FX2");
		}
		else if (sect == L"PinballFX3")
		{
			sys = _T("Pinball FX3");
			dofTitlePrefix = _T("FX3");
		}
		else if (sect == L"PinballArcade")
		{
			sys = _T("Pinball Arcade");
		}
		else if (_memicmp(sect.c_str(), L"System_", 14) == 0
			&& _wtoi(sect.c_str() + 7) != 0
			&& vars.Get(L"name") != 0)
		{
			sys = WideToTSTRING(vars.Get(L"name"));
		}

		// if we found a system name, add its games
		if (sys.length() != 0)
		{
			// build the path to the database files for the system
			TCHAR parent[MAX_PATH], path[MAX_PATH];
			PathCombine(parent, pbxPath, _T("Databases"));
			PathCombine(path, parent, sys.c_str());

			// get the table path for the system
			const TCHAR *tablePath = vars.Get(L"tablepath", _T(""));

			// create the system object
			GameSystem *system = CreateSystem(sys.c_str(), systemIndex++, path, tablePath, defExt.c_str());

			// load the system configuration
			system->mediaDir = system->displayName;			// PBX always uses the display name as the media folder name
			system->databaseDir = system->displayName;		// likewise for the table database folder name
			system->exe = vars.Get(L"executable", _T(""));
			system->workingPath = vars.Get(L"workingpath", _T(""));
			system->tablePath = tablePath;
			system->defExt = defExt;
			system->params = vars.Get(L"parameters", _T(""));
			system->process = vars.Get(L"process", _T(""));
			system->dofTitlePrefix = dofTitlePrefix;

			// If "mouseclickfocus" was set, synthesize startup keys sequence to
			// generate a mouse click at the center of the main window at startup.
			if (vars.GetBool(L"mouseclickfocus", false))
				system->startupKeys = _T("[click playfield]");

			// translate PBX's SystemType code to our system class
			if (const TCHAR *sysType = vars.Get(L"SystemType", nullptr); sysType != nullptr)
			{
				switch (_wtoi(sysType))
				{
				case 1:
					system->systemClass = _T("VP");
					break;

				case 2:
					system->systemClass = _T("FP");
					break;
				}
			}

			// PBX keeps the working path and executable name separately,
			// but it treats the working path as the executable folder.
			// Build the full path to the executable by combining them.
			TCHAR exe[MAX_PATH];
			PathCombine(exe, system->workingPath.c_str(), system->exe.c_str());
			system->exe = exe;

			// scan the .XML files for the lists
			for (auto &file : fs::directory_iterator(path))
			{
				// check if it's an XML file
				const wchar_t *fname = file.path().c_str();
				std::basic_regex<wchar_t> xmlExtPat(L".*\\.xml$", std::regex_constants::icase);
				if (std::regex_match(fname, xmlExtPat))
				{
					// it is - load it
					if (!LoadGameDatabaseFile(fname, sys.c_str(), system, eh))
						return false;
				}
			}
		}

		// success
		return true;
	};

	// scan for enabled systems
	typedef std::basic_regex<wchar_t> wregex;
	wregex varPat(L"^\\s*(\\w+)\\s*=\\s*(.*?)\\s*$", std::regex_constants::icase);
	wregex sectPat(L"^\\s*\\[\\s*(.*?)\\s*\\]\\s*$");
	for (wchar_t *p = ini.get(); *p != 0; )
	{
		// find the next line
		const wchar_t *l = p;
		for (; *p != '\n' && *p != '\0'; ++p);

		// convert the newline to a nul byte if found
		if (*p == '\n')
			*p++ = '\0';

		// Check for relevant line formats
		std::match_results<const wchar_t *> m;
		if (std::regex_match(l, m, sectPat))
		{
			// [Section] marker - close the current section
			if (!closeSect())
				return false;

			// enter the new section: note the new section title and clear the 
			// variable table
			sect = m[1].str();
			vars.map.clear();
		}
		else if (std::regex_match(l, m, varPat))
		{
			// add the variable
			vars.Add(m[1].str().c_str(), m[2].str().c_str());
		}
	}

	// close the final section
	if (!closeSect())
		return false;

	// success
	return true;
}

void GameList::Log(const TCHAR *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	LogFile::Get()->WriteV(false, LogFile::SystemSetupLogging, msg, ap);
	va_end(ap);
}

void GameList::LogGroup()
{
	LogFile::Get()->Group(LogFile::SystemSetupLogging);
}

bool GameList::InitFromConfig(ErrorHandler &eh)
{
	LogGroup();
	Log(_T("Starting pinball player system setup\n"));

	// get the Steam executable, in case it's needed anywhere
	TCHAR steamExe[MAX_PATH];
	TCHAR steamPath[MAX_PATH];
	{
		DWORD len = countof(steamExe);
		if (SUCCEEDED(AssocQueryString(ASSOCF_NONE, ASSOCSTR_EXECUTABLE,
			_T("steam"), _T("Open"), steamExe, &len)))
		{
			// got it - pull out the path portion
			_tcscpy_s(steamPath, steamExe);
			PathRemoveFileSpec(steamPath);
		}
		else
		{
			// not found - clear the strings
			steamExe[0] = 0;
			steamPath[0] = 0;
		}
	}

	// get the PinballX folder
	const TCHAR *pbxPath = GetPinballXPath();

	// Get the database folder, using "data folder" rules
	TSTRING dbDir = GetDataFilePath(ConfigVars::TableDatabasePath, _T("Databases"), IDS_DEFAULT_TABLEDB_PATH_PROMPT, eh);
	Log(_T("The main table database folder is %s\n"), dbDir.c_str());
	
	// Run through the SystemN variables to see what's populated.
	ConfigManager *cfg = ConfigManager::GetInstance();
	for (int n = 0; n <= PinballY::Constants::MaxSystemNum; ++n)
	{
		// Check if there's an entry for "SystemN"
		MsgFmt sysvar(_T("System%d"), n);
		MsgFmt enabled(_T("%s.Enabled"), sysvar.Get());
		const TCHAR *systemName = cfg->Get(sysvar);
		bool systemEnabled = cfg->GetBool(enabled, true);

		// log disabled systems
		if (systemName != nullptr && !systemEnabled)
			Log(_T("Pinball player system \"%s\" is disabled; skipping\n"), systemName);

		// if this one has a system name, and it's enabled, configure it
		if (systemName != nullptr && systemEnabled)
		{
			// There's an enabled entry for this system slot.  
			// First, get the system's table database folder so that we can 
			// populate its game list.  Note that the system's display name 
			// is the default media folder name, if SystemN.DatabaseDir isn't
			// separately specified.
			const TCHAR *databaseDir = cfg->Get(MsgFmt(_T("%s.DatabaseDir"), sysvar.Get()), _T(""));
			if (databaseDir[0] == 0)
				databaseDir = systemName;

			// Build the full path to the database folder for the system
			TCHAR sysDbDir[MAX_PATH], sysDbDirOrig[MAX_PATH];
			PathCombine(sysDbDirOrig, dbDir.c_str(), databaseDir);
			PathCanonicalize(sysDbDir, sysDbDirOrig);

			// log the database folder information
			LogGroup();
			Log(_T("Configuring pinball player system \"%s\"\n"), systemName);
			Log(_T("+ database folder = %s\n"), sysDbDir);

			// The database directory has to be unique per system
			bool dbDirClash = false;
			for (auto &otherSys : systems)
			{
				// check for a match
				if (_tcsicmp(otherSys.second.databaseDir.c_str(), databaseDir) == 0)
				{
					// uh oh - flag an error and continue
					LogFile::Get()->Write(_T("Error: Table database folder clash: system %s (%s) clashes with %s (%s)\n"),
						systemName, databaseDir, otherSys.second.displayName.c_str(), otherSys.second.databaseDir.c_str());
					eh.Error(MsgFmt(IDS_ERR_DBDIRCLASH, otherSys.second.displayName.c_str(), systemName, systemName));
					dbDirClash = true;
					break;
				}
			}

			// skip this system if we found a database directory collision
			if (dbDirClash)
				continue;

			// Get the system class, executable, table path, and default 
			// extension, if defined in the configuration.
			const TCHAR *sysClass = cfg->Get(MsgFmt(_T("%s.Class"), sysvar.Get()), _T(""));
			const TCHAR *exe = cfg->Get(MsgFmt(_T("%s.Exe"), sysvar.Get()), _T(""));
			const TCHAR *defExt = cfg->Get(MsgFmt(_T("%s.DefExt"), sysvar.Get()), _T(""));
			TSTRING tablePath = cfg->Get(MsgFmt(_T("%s.TablePath"), sysvar.Get()), _T(""));

			// if no system class is defined, infer it from the name
			auto icase = std::regex_constants::icase;
			if (sysClass[0] == 0)
			{
				// try to infer the class from the system name
				std::basic_regex<TCHAR> vpxNamePat(_T("visual\\s*pinball.*(x|10)|vp(x|10).*"), icase);
				std::basic_regex<TCHAR> vpNamePat(_T("visual\\s*pinball.*|vp.*|physmod.*|vp.*pm.*|visual\\s*pinball\\s.*pm.*"), icase);
				std::basic_regex<TCHAR> fpNamePat(_T("future\\s*pinball.*|fp.*"), icase);
				if (std::regex_match(systemName, vpxNamePat))
					sysClass = _T("VPX");
				else if (std::regex_match(systemName, vpNamePat))
					sysClass = _T("VP");
				else if (std::regex_match(systemName, fpNamePat))
					sysClass = _T("FP");

				// log it
				Log(_T("+ no system class specified; class inferred from name is %s\n"), 
					sysClass[0] != 0 ? sysClass : _T("(unknown)"));
			}

			// if we still don't know the system class, try to infer it from 
			// the default extension
			if (sysClass[0] == 0 && defExt[0] != 0)
			{
				if (_tcsicmp(defExt, _T(".vpt")) == 0)
					sysClass = _T("VP");
				else if (_tcsicmp(defExt, _T(".vpx")) == 0)
					sysClass = _T("VPX");
				else if (_tcsicmp(defExt, _T(".fpt")) == 0)
					sysClass = _T("FP");

				// log it
				Log(_T("+ system class inferred from table extension is %s\n"),
					sysClass[0] != 0 ? sysClass : _T("(unknown"));
			}

			// if the system class is still a mystery, try to infer it 
			// from the executable
			if (sysClass[0] == 0 && exe[0] != 0)
			{
				std::basic_regex<TCHAR> vpxExePat(_T(".*\\\\vpinballx[^\\\\]*"), icase);
				std::basic_regex<TCHAR> vpExePat(_T(".*\\\\vpinball[^\\\\]*"), icase);
				std::basic_regex<TCHAR> fpExePat(_T(".*\\\\future\\s*pinball[^\\\\]*"), icase);
				if (std::regex_match(exe, vpxExePat))
					sysClass = _T("VPX");
				else if (std::regex_match(exe, vpExePat))
					sysClass = _T("VP");
				else if (std::regex_match(exe, fpExePat))
					sysClass = _T("FP");

				// log it
				Log(_T("+ system class inferred from system executable is %s\n"),
					sysClass[0] != 0 ? sysClass : _T("(unknown"));
			}

			// supply the suitable default extension for the system, if
			// the config didn't specify one
			if (defExt[0] == 0 && sysClass[0] != 0)
			{
				if (_tcsicmp(sysClass, _T("VPX")) == 0)
					defExt = _T(".vpx");
				else if (_tcsicmp(sysClass, _T("VP")) == 0)
					defExt = _T(".vpt");
				else if (_tcsicmp(sysClass, _T("FP")) == 0)
					defExt = _T(".fpt");

				// log it
				Log(_T("+ no table file extension specified; ext inferred from system class is %s\n"),
					defExt[0] != 0 ? defExt : _T("(unknown"));
			}

			// check to see if steam is present
			auto CheckSteam = [&steamExe, &systemName, &eh](const TCHAR *varname, const TCHAR *place)
			{
				// check if we found Steam
				if (steamExe[0] == 0)
				{
					LogFile::Get()->Write(_T("Error: system %s uses the [%s] substitution variable in its %s setting, but Steam ")
						_T("wasn't found in the Windows registry\n"), systemName, place, varname);
					eh.Error(MsgFmt(IDS_ERR_STEAM_MISSING, systemName, varname, place, systemName, systemName));
					return false;
				}

				// success
				return true;
			};

			// Figure the full name of the program executable:
			//
			// - If it's specified as an absolute path in the configuration,
			//   use it exactly as given.
			//
			// - If the config specifies [STEAM], look in the registry for
			//   the Steam installation.
			//
			// - If the path isn't specified at all, use the executable
			//   associated with the default extension.
			//
			// - If the config gives a relative path, and a default filename
			//   extension is specified, use path portion of the registered
			//   application for the extension.
			//
			TCHAR exeBuf[MAX_PATH];
			TSTRING registeredExe;
			TSTRING updatedExe;
			static const std::basic_regex<TCHAR> steamDirPat(_T("\\[steamdir\\]"), std::regex_constants::icase);
			if (_tcsicmp(exe, _T("[steam]")) == 0)
			{
				// This is shorthand for the Steam executable as specified
				// in the registry, under the "Steam" program ID.  Look it up.
				if (!CheckSteam(_T("STEAM"), _T("Program EXE")))
					continue;

				// use the steam executable
				exe = steamExe;
				Log(_T("+ [STEAM] executable specified, full path is %s\n"), exe);
			}
			else if (std::regex_search(exe, steamDirPat))
			{
				// [STEAMDIR] gets the Steam install location
				if (!CheckSteam(_T("STEAMDIR"), _T("Program EXE")))
					continue;

				// replace [STEAMDIR] with the Steam exe's folder path
				updatedExe = std::regex_replace(exe, steamDirPat, steamPath);
				exe = updatedExe.c_str();
				Log(_T("+ [STEAMDIR] path specified; Steam dir is %s, expanded path result is %s\n"), steamPath, exe);
			}
			else if ((exe[0] == 0 || PathIsRelative(exe)) && GetProgramForExt(registeredExe, defExt))
			{
				// If no program name was specified, use the registered
				// program as found.  If a partial program name was given,
				// take the PATH portion of the registered program, and
				// combine it with the relative filename given in the
				// config.
				if (exe[0] == 0)
				{
					// no program specified - use the registered program
					// name in its entirety
					exe = registeredExe.c_str();
				}
				else
				{
					// partial program name specified - combine the path 
					// from the registered program with the filename from
					// the config
					_tcscpy_s(exeBuf, registeredExe.c_str());
					PathRemoveFileSpec(exeBuf);
					PathAppend(exeBuf, exe);
					exe = exeBuf;
				}

				// log it
				Log(_T("+ full executable path to player program is %s\n"), exe);
			}

			// the working directory is the folder containing the executable
			const TCHAR *exeFileName = PathFindFileName(exe);
			TSTRING workingPath(exe, exeFileName - exe);
			Log(_T("+ working path when launching player program is %s\n"), workingPath.c_str());

			// Apply substitution variables to the table path
			static const std::basic_regex<TCHAR> tablePathVars(_T("\\[(\\w+)\\]"), std::regex_constants::icase);
			tablePath = regex_replace(tablePath, tablePathVars, 
				[&steamPath, &pbxPath, &eh, &CheckSteam, &systemName](const std::match_results<TSTRING::const_iterator> &m) -> TSTRING
			{
				// convert the variable name to lower-case
				TSTRING v = m[1].str();
				std::transform(v.begin(), v.end(), v.begin(), ::_totlower);

				// look it up
				if (v == _T("pinbally"))
				{
					// [PinballY] -> program install folder
					TCHAR path[MAX_PATH];
					GetDeployedFilePath(path, _T(""), _T(""));
					return path;
				}
				else if (v == _T("pinballx"))
				{
					// [PinballX] -> PBX program install folder
					if (pbxPath != nullptr)
						return pbxPath;
					else
					{
						LogFile::Get()->Write(_T("Error: system %s uses the [PinballX] substitution variable in its Table Path setting, but PinballX ")
							_T("doesn't appear to be installed\n"), systemName);
						eh.Error(MsgFmt(IDS_ERR_PBXPATH_NOT_AVAIL, _T("Table Path"), systemName));
						return m[0].str();
					}
				}
				else if (v == _T("steamdir"))
				{
					// replace [STEAMDIR] with the Steam exe's folder path
					if (CheckSteam(_T("STEAMDIR"), _T("Table Path")))
						return steamPath;
					else
						return m[0].str();
				}
				else if (v == _T("lb"))
				{
					return _T("[");
				}
				else if (v == _T("rb"))
				{
					return _T("]");
				}
				else
				{
					// not found - return the original text unchanged
					Log(_T("+ table path contains unknown substitution variable %s\n"), m[0].str().c_str());
					return m[0].str();
				}
			});

			// If the table path is in relative notation, it's relative to the
			// system's program folder.  Expand it to an absolute path.
			TCHAR tablePathBuf[MAX_PATH];
			if (tablePath.length() == 0 || PathIsRelative(tablePath.c_str()))
			{
				// start with the working path
				_tcscpy_s(tablePathBuf, workingPath.c_str());

				// if a path other than empty or "." was specified, append it
				if (tablePath != _T("") && tablePath != _T("."))
					PathAppend(tablePathBuf, tablePath.c_str());

				// store the result back in the table path
				tablePath = tablePathBuf;
			}
			Log(_T("+ full table path (folder containing this system's table files) is %s\n"), tablePath.c_str());

			// create the system object
			GameSystem *system = CreateSystem(systemName, n, sysDbDir, tablePath.c_str(), defExt);

			// Load the config variables for the system
			const TCHAR *mediaDirVar = cfg->Get(MsgFmt(_T("%s.MediaDir"), sysvar.Get()), _T(""));
			system->databaseDir = databaseDir;
			system->exe = exe;
			system->defExt = defExt;
			system->systemClass = sysClass;
			system->tablePath = tablePath;
			system->workingPath = workingPath;
			system->mediaDir = mediaDirVar[0] != 0 ? mediaDirVar : systemName;
			system->params = cfg->Get(MsgFmt(_T("%s.Parameters"), sysvar.Get()), _T(""));
			system->process = cfg->Get(MsgFmt(_T("%s.Process"), sysvar.Get()), _T(""));
			system->startupKeys = cfg->Get(MsgFmt(_T("%s.StartupKeys"), sysvar.Get()), _T(""));
			system->envVars = cfg->Get(MsgFmt(_T("%s.Environment"), sysvar.Get()), _T(""));
			system->dofTitlePrefix = cfg->Get(MsgFmt(_T("%s.DOFTitlePrefix"), sysvar.Get()), _T(""));
			system->runBeforePre = cfg->Get(MsgFmt(_T("%s.RunBeforePre"), sysvar.Get()), _T(""));
			system->runBefore = cfg->Get(MsgFmt(_T("%s.RunBefore"), sysvar.Get()), _T(""));
			system->runAfter = cfg->Get(MsgFmt(_T("%s.RunAfter"), sysvar.Get()), _T(""));
			system->runAfterPost = cfg->Get(MsgFmt(_T("%s.RunAfterPost"), sysvar.Get()), _T(""));
			system->nvramPath = cfg->Get(MsgFmt(_T("%s.NVRAMPath"), sysvar.Get()), _T(""));
			system->terminateBy = cfg->Get(MsgFmt(_T("%s.TerminateBy"), sysvar.Get()), _T(""));
			system->keepOpen = cfg->Get(MsgFmt(_T("%s.ShowWindowsWhileRunning"), sysvar.Get()), _T(""));
			
			// set the SW_SHOW mode for the launched app, using SW_SHOWMINIMIZED as the default
			system->swShow = SW_SHOWMINIMIZED;
			if (auto it = swShowMap.find(cfg->Get(MsgFmt(_T("%s.ShowWindow"), sysvar.Get()), _T(""))); it != swShowMap.end())
				system->swShow = it->second;

			Log(_T("+ media folder base name is %s, full path is %s\\%s; %s\n"),
				system->mediaDir.c_str(), GetMediaPath(), system->mediaDir.c_str(),
				mediaDirVar[0] == 0 ? 
				_T("this is default folder name, which is the same as the system name" :
				_T("this folder name was explicitly specified in the settings")));

			// Search the system's database directory for .XML files.  These 
			// contain the table metadata for the system's tables.
			Log(_T("+ searching folder %s for table database .XML files\n"), sysDbDir);
			for (auto &file : fs::directory_iterator(sysDbDir))
			{
				// check if it's an XML file
				const wchar_t *fname = file.path().c_str();
				std::basic_regex<wchar_t> xmlExtPat(L".*\\.xml$", std::regex_constants::icase);
				if (std::regex_match(fname, xmlExtPat))
				{
					// it's an XML file - load the table list
					LogGroup();
					Log(_T("+ System \"%s\": loading table database file %s\n"), systemName, fname);
					if (!LoadGameDatabaseFile(fname, databaseDir, system, eh))
						return false;
				}
			}
		}
	}

	// success
	return true;
}

bool GameList::Load(ErrorHandler &eh)
{
	// initialize from the configuration variables
	if (!InitFromConfig(eh))
		return false;

	// if the game index is empty, log an error, but continue running,
	// as the user might for some reason just want to run the empty UI
	if (games.size() == 0)
		eh.Error(LoadStringT(IDS_ERR_NOGAMES).c_str());

	// Add game entries for unconfigured table files - that is, files
	// we find in the system folders that match a default extension
	// for one or systems, but which have no game database entries.
	// The direct file entries allow the user to play new table files
	// immediately without setting up their metadata and media files,
	// and also let the user see which files haven't been set up yet
	// and run the setup menus for them.
	AddUnconfiguredGames();

	// Build the title index
	BuildTitleIndex();

	// Create the star rating filters
	for (int stars = -1; stars <= 5; ++stars)
	{
		// add the filter
		auto it = ratingFilters.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(stars),
			std::forward_as_tuple(stars));
	}

	// Create the recency filters.  These sort games by how recently
	// they've been played.
	auto CreateRecencyFilter = [this](int titleStringId, int menuStringId, int days, bool exclude)
	{
		// create the filter
		recencyFilters.emplace_back(new RecentlyPlayedFilter(
			LoadStringT(titleStringId).c_str(), 
			LoadStringT(menuStringId).c_str(),
			days, exclude));
	};
	CreateRecencyFilter(IDS_FILTER_THISWEEK, IDS_SFILTER_THISWEEK, 7, false);
	CreateRecencyFilter(IDS_FILTER_THISMONTH, IDS_SFILTER_THISMONTH, 30, false);
	CreateRecencyFilter(IDS_FILTER_THISYEAR, IDS_SFILTER_THISYEAR, 365, false);
	CreateRecencyFilter(IDS_FILTER_NOTTHISWEEK, IDS_SFILTER_NOTTHISWEEK, 7, true);
	CreateRecencyFilter(IDS_FILTER_NOTTHISMONTH, IDS_SFILTER_NOTTHISMONTH, 30, true);
	CreateRecencyFilter(IDS_FILTER_NOTTHISYEAR, IDS_SFILTER_NOTTHISYEAR, 365, true);

	// create the "Never Played" filter
	recencyFilters.emplace_back(new NeverPlayedFilter(
		LoadStringT(IDS_FILTER_NEVERPLAYED).c_str(),
		LoadStringT(IDS_SFILTER_NEVERPLAYED).c_str()));

	// Create the installation recency filters.  These sort games by
	// how recently they were added to the database.
	auto CreateInstRecencyFilter = [this](int titleStringId, int menuStringId, int days, bool exclude)
	{
		// create the filter
		recencyFilters.emplace_back(new RecentlyAddedFilter(
			LoadStringT(titleStringId).c_str(),
			LoadStringT(menuStringId).c_str(),
			days, exclude));
	};
	CreateInstRecencyFilter(IDS_FILTER_ADDEDTHISWEEK, IDS_SFILTER_THISWEEK, 7, false);
	CreateInstRecencyFilter(IDS_FILTER_ADDEDTHISMONTH, IDS_SFILTER_THISMONTH, 30, false);
	CreateInstRecencyFilter(IDS_FILTER_ADDEDTHISYEAR, IDS_SFILTER_THISYEAR, 365, false);
	CreateInstRecencyFilter(IDS_FILTER_ADDEDOVERWEEK, IDS_SFILTER_WEEKAGO, 7, true);
	CreateInstRecencyFilter(IDS_FILTER_ADDEDOVERMONTH, IDS_SFILTER_MONTHAGO, 30, true);
	CreateInstRecencyFilter(IDS_FILTER_ADDEDOVERYEAR, IDS_SFILTER_YEARAGO, 365, true);

	// Create the master filter list.  The UI uses this to construct
	// menus to select filters, by selecting subsets of the filters
	// of desired types, usually by C++ class (using dynamic_cast<>).
	// For the most part, the UI builds filter menus in the order in
	// which the filters appear in our master list, so we need to pay
	// attention to the presentation aesthetics when choosing how to
	// order the filters.  Within each group, if there's a natural
	// ordering principle for the filter type, we use that; e.g.,
	// we order the "era" filters chronologically.  In the absence 
	// of any ordering principle peculiar to the filter class, we
	// just order the items alphabetically.  The ordering is only 
	// important within subclass groups, because the UI never
	// presents all of the filters in a single flat list (such a
	// list would be too long for usability's sake).
	BuildMasterFilterList();

	// set the "all games" filter to populate the initial filter list
	SetFilter(&allGamesFilter);

	// Delete any reload map.  This is only needed during the load
	// phase; any games that are added after this point (e.g., by
	// file system scans when switch foreground apps) count as new
	// games that should have new internal IDs assigned.
	reloadIDMap.reset();

	// success
	return true;
}

bool GameList::AddUserDefinedFilter(GameListFilter *filter)
{
	// delete it if it already exists
	DeleteUserDefinedFilter(filter);

	// add it
	userDefinedFilters->emplace(filter->GetFilterId(), filter);

	// assign it a command ID
	AssignFilterCommand(filter);

	// If it defines a new user-defined filter group, assign that a command ID.
	// System filter group names are enclosed in [square brackets].
	if (filter->menuGroup.length() != 0 && filter->menuGroup[0] != '[')
	{
		if (auto it = filterGroupCmdMap.find(filter->menuGroup); it == filterGroupCmdMap.end())
			filterGroupCmdMap.emplace(filter->menuGroup, nextFilterGroupCmdID++);
	}

	// the filter list needs to be rebuilt
	isFilterListDirty = true;

	// If this is the pending filter from a recent config restore, tell the 
	// caller to activate it.  We don't activate it ourselves, because filter
	// change operations need to be initiated from the UI to make sure the
	// UI is properly updated to reflect the change.
	return pendingRestoredFilter.length() != 0 && pendingRestoredFilter == filter->GetFilterId();
}

void GameList::DeleteUserDefinedFilter(GameListFilter *filter)
{
	// look up the filter
	if (auto it = userDefinedFilters->find(filter->GetFilterId()); it != userDefinedFilters->end())
	{
		// found it - remove it from the map
		userDefinedFilters->erase(it);

		// the filter list needs to be rebuilt
		isFilterListDirty = true;
	}
}

void GameList::CheckMasterFilterList()
{
	if (isFilterListDirty)
		BuildMasterFilterList();
}

void GameList::BuildMasterFilterList()
{
	// clear any existing list
	filters.clear();

	// Start with the "All Games" filter
	AddFilter(&allGamesFilter);

	// Add the Hidden Games filter
	AddFilter(&hiddenGamesFilter);

	// Add the Unconfigured Games filter
	AddFilter(&unconfiguredGamesFilter);

	// Add the Favorites filter
	AddFilter(&favoritesFilter);

	// Add the Date filters
	for (auto &df : dateFilters)
		AddFilter(&df.second);

	// Add the Manufacturer filters
	for (auto &mf : manufacturers)
		AddFilter(&mf.second);

	// Add the System filters
	for (auto &sys : systems)
		AddFilter(&sys.second);

	// Add the Category filters
	for (auto &cat : categories)
		AddFilter(cat.second.get());

	// Add the "Uncategorized" filter at the end
	AddFilter(&noCategoryFilter);

	// Add the star rating filters
	for (auto &r : ratingFilters)
		AddFilter(&r.second);

	// Add the recently-played and recently-added filters
	for (auto &r : recencyFilters)
		AddFilter(r.get());
	
	// Add the user-defined filters
	for (auto &f : *userDefinedFilters)
		AddFilter(f.second);

	// Sort the list in menu display order
	std::sort(filters.begin(), filters.end(), [](GameListFilter* const &a, GameListFilter* const &b) {
		return lstrcmpi(a->menuSortKey.c_str(), b->menuSortKey.c_str()) < 0;
	});

	// the list is now up-to-date
	isFilterListDirty = false;
}

void GameList::AddFilter(GameListFilter *f)
{
	// if it doesn't have an ID yet, assign one
	AssignFilterCommand(f);
}

void GameList::AssignFilterCommand(GameListFilter *f)
{
	if (f->cmd == 0)
	{
		// If there's already an assigned ID for this filter, reuse it.
		// Otherwise assign a new ID.
		auto id = f->GetFilterId();
		if (auto it = filterCmdMap.find(id); it != filterCmdMap.end())
		{
			// reuse the same ID assigned to this filter previously
			f->cmd = it->second;
		}
		else
		{
			// assign a new ID, and remember it in case we have to rebuild the list
			f->cmd = nextFilterCmdID++;
			filterCmdMap.emplace(id, f->cmd);
		}
	}

	// add it to the filter list
	filters.push_back(f);
}

void GameList::AddMetaFilter(MetaFilter *mf)
{
	// add it to the vector
	metaFilters->emplace_back(mf);

	// sort the vector by priority
	std::sort(metaFilters->begin(), metaFilters->end(), 
		[](MetaFilter* const &a, MetaFilter* const &b) { return a->priority < b->priority; });
}

void GameList::RemoveMetaFilter(MetaFilter *mf)
{
	if (auto it = std::find(metaFilters->begin(), metaFilters->end(), mf); it != metaFilters->end())
		metaFilters->erase(it);
}

void GameList::BuildTitleIndex() 
{
	// clear any previous index
	byTitle.clear();

	// create the title index
	for (auto &g : games)
		byTitle.emplace_back(&g);

	// sort the title index
	SortTitleIndex();
}

void GameList::SortTitleIndex()
{
	// sort the title index alphabetically
	std::sort(byTitle.begin(), byTitle.end(), [](GameListItem* const &a, GameListItem* const &b) {
		return lstrcmpi(a->title.c_str(), b->title.c_str()) < 0;
	});
}

void GameList::EnumGames(std::function<void(GameListItem*)> func)
{
	for (auto &game : byTitle)
		func(game);
}

void GameList::EnumGames(std::function<void(GameListItem*)> func, GameListFilter *filter)
{
	// note if the "Hide Unconfigured Games" option is set
	bool hideUnconfigured = Application::Get()->IsHideUnconfiguredGames();

	// initialize the filter
	filter->BeforeScan();

	// enumerate games that match the filter
	for (auto &game : byTitle)
	{
		if (FilterIncludes(filter, game, hideUnconfigured))
			func(game);
	}

	// end the scan
	filter->AfterScan();
}

void GameList::AddUnconfiguredGames()
{
	// go through the table file sets
	for (auto &tfsIter : tableFileSets)
	{
		// go through the files in this set
		auto &tfs = tfsIter.second;
		for (auto &fileIter : tfsIter.second.files)
		{
			// if there's no GameListItem for this file, it's unconfigured
			auto &file = fileIter.second;
			if (file.game == nullptr)
			{
				// Add a game list item for the file.  Use the filename as
				// the display name and media name.
				auto &newGame = games.emplace_back(file.filename.c_str(), &tfs);

				// initialize its Hidden status
				newGame.SetHidden(IsHidden(&newGame), false);
			}
		}
	}
}

int GameList::AddNewFiles(const TSTRING &path, const TSTRING &ext, 
	const std::list<TSTRING> newFiles)
{
	// Find the table file set described by the path and extension.  If
	// there isn't such a table file set, ignore the files: we must have
	// launched a file scan and then loaded a new configuration that
	// doesn't include this folder before the scan completed.  Any files
	// found in a folder we're no longer monitoring are of no interest.
	int nAdded = 0;
	auto it = tableFileSets.find(TableFileSet::GetKey(path.c_str(), ext.c_str()));
	if (it != tableFileSets.end())
	{
		// get the table file set object
		auto &ts = it->second;

		// add each file
		for (auto &f : newFiles) 
		{
			// if the file isn't part of the table file set, add it
			if (ts.FindFile(f.c_str(), nullptr, false) == nullptr)
			{
				// add it
				auto tf = ts.AddFile(f.c_str());

				// add a game list item for the file
				auto &newGame = games.emplace_back(f.c_str(), &ts);

				// initialize its Hidden status
				newGame.SetHidden(IsHidden(&newGame), false);

				// count it
				++nAdded;
			}
		}
	}

	// return the number of added games
	return nAdded;
}

GameSystem *GameList::CreateSystem(
	const TCHAR *systemName, int configIndex,
	const TCHAR *sysDatabaseDir, const TCHAR *tablePath, const TCHAR *defExt)
{
	// Look up the system by name
	auto it = systems.find(configIndex);
	if (it == systems.end())
	{
		// it doesn't exist yet - add it
		it = systems.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(configIndex),
			std::forward_as_tuple(systemName, configIndex)).first;

		// Figure the system's generic file name.  The generic file
		// is of the form <database path>\<system dir>\<system dir>.xml.
		TCHAR genericFileName[MAX_PATH];
		_tcscpy_s(genericFileName, sysDatabaseDir);
		PathAppend(genericFileName, PathFindFileName(sysDatabaseDir));
		_tcscat_s(genericFileName, _T(".xml"));

		// assign it
		it->second.genericDbFilename = genericFileName;
	}

	// Get the system
	GameSystem *system = &it->second;

	// Look up the table file set object for the system's table file pattern
	TSTRING key = TableFileSet::GetKey(tablePath, defExt);
	auto itfs = tableFileSets.find(key);
	if (itfs == tableFileSets.end())
	{
		// there's no table file set for this pattern yet - create one
		itfs = tableFileSets.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(key),
			std::forward_as_tuple(tablePath, defExt)).first;
	}
	else if (defExt != nullptr && defExt[0] != 0)
	{
		Log(_T("+ This system uses a folder that has already been scanned (%s\\*%s)\n"),
			tablePath, defExt);
	}
	else
	{
		Log(_T("+ NOT scanning for this system's tables files, because its default extension is empty\n"));
	}

	// cross-reference the system and the table file set
	TableFileSet *tfs = &itfs->second;
	tfs->systems.push_back(system);
	system->tableFileSet = tfs;

	// return the system we found or created
	return system;
}

GameSystem *GameList::GetSystem(int configIndex)
{
	if (auto it = systems.find(configIndex); it != systems.end())
		return &it->second;
	return nullptr;
}

bool GameList::LoadGameDatabaseFile(
	const TCHAR *filename, const TCHAR *parentFolder,
	GameSystem *system, ErrorHandler &eh)
{
	// set up for logging
	auto Log = [](const TCHAR *msg, ...)
	{
		va_list ap;
		va_start(ap, msg);
		LogFile::Get()->WriteV(false, LogFile::SystemSetupLogging, msg, ap);
		va_end(ap);
	};
	auto LogGroup = []() { LogFile::Get()->Group(LogFile::SystemSetupLogging); };

	// read and parse the XML
	std::unique_ptr<GameDatabaseFile> xml(new GameDatabaseFile());
	if (!xml->Load(filename, eh))
	{
		Log(_T("++ XML parse failed\n"));
		return false;
	}

	// make sure it has the root <menu> node
	typedef xml_node<char> node;
	typedef xml_attribute<char> attr;
	node *menu = xml->doc.first_node("menu");
	if (menu == nullptr)
	{
		Log(_T("++ Root <menu> node not found in XML; assuming this isn't a table database file\n"));
		return false;
	}

	// Determine if the file defines a "category".
	//
	// Categories are user-defined tags that can be associated with
	// games and then used to select subsets of games to display in
	// UI.  We *mostly* implement categories using our "game stats" 
	// database file (via a Categories column that lists the names
	// of the categories assigned to a game), but we *also* respect
	// the PinballX convention of using the source file location of
	// a game's definition as an implied category.  That lets us
	// correctly import category assignments when using files ported
	// directly over from PinballX.
	//
	// The PinballX notion of categories was fairly primitive: each
	// game could be assigned one category, determined by its file
	// placement.  The name of XML file that contained a game entry
	// was used as the category name for that game, with the
	// exception that an XML file with same name as its parent
	// system folder didn't define a category - e.g., games in
	// Databases\Visual Pinball\Visual Pinball.xml were treated as
	// uncategorized.
	//
	// So we need to determine the category name (if any) implied by
	// the current XML file.  This category will be assigned to all
	// of the games listed within this file.  Start by pulling out
	// the root filename (that is, minus path prefix and .xml 
	// suffix) to get the implied category name.
	TCHAR categoryNameBuf[MAX_PATH];
	_tcscpy_s(categoryNameBuf, PathFindFileName(filename));
	PathRemoveExtension(categoryNameBuf);

	// Now check the file name against the parent folder name.  If
	// it's the same, this is the "generic" list for the system,
	// which doesn't define a category; otherwise, it implies the 
	// category name.
	const TCHAR *categoryName = nullptr;
	if (_tcsicmp(categoryNameBuf, parentFolder) != 0)
		categoryName = categoryNameBuf;

	// There's one more special case, this time of our own making 
	// rather than a PinballX compatibility point.  Unlike PinballX,
	// we let the user rename (and add and delete) category names
	// through the UI.  If the user renames a category that came
	// from an XML file name, and the new name doesn't form a valid
	// filename, we deal with this by inserting a <CategoryName>
	// tag into the XML file with the new name.  So if we find 
	// this tag, it overrides the name implied by the filename.
	// This is true even if the name matches the system name.
	TSTRING catNameNodeVal;
	if (node *catNameNode = menu->first_node("CategoryName");
		catNameNode != nullptr && catNameNode->value() != nullptr)
	{
		catNameNodeVal = AnsiToTSTRING(catNameNode->value());
		categoryName = catNameNodeVal.c_str();
	}

	// log the category information
	if (categoryName != nullptr)
		Log(_T("++ This file defines category \"%s\" for the games it contains; %s\n"), 
			categoryName, 
			catNameNodeVal.length() != 0 ? 
			_T("the name comes from the explicit <CategoryName> tag in file") :
			_T("the category name is based on the XML file name"));
	else
		Log(_T("++ This is the main file for this system (it doesn't define a category)\n"));

	// If we found a category name, find or create the category
	// object
	GameCategory *category = nullptr;
	if (categoryName != nullptr)
		category = FindOrCreateCategory(categoryName);

	// remember the category in the XML source file
	xml->category = category;

	// The PinballX XML schema:
	//
	//   <menu>
	//     <game name="Game_filename">
	//       <description>Game Title (Manufacturer year)</description>
	//       <rom>Rom Name</rom>						 // VPinMAME ROM name, or DOF effects and high score retrieval
	//       <manufacturer>Williams</manufacturer>		 // manufacturer name
	//       <year>1980</year>                           // YYYY format; year of original arcade game release
	//       <type>SS</type>                             // SS=solid state, EM=electromechanical, ME=pure mechanical
	//       <hidedmd>True</hidedmd>                     // boolean
	//       <hidetopper>True</hidetopper>               // boolean
	//       <hidebackglass>True</hidebackglass>         // boolean
	//       <enabled>True</enabled>                     // boolean; default = true
	//       <rating>0</rating>                          // star rating, 1-5; 0 if unrated
	//       <ipdbid>1234</ipdbid>                       // PinballY extension: the IPDB ID for the game, if known
	//     </game>
	//   </menu>
	if (node *menu = xml->doc.first_node("menu"); menu != nullptr)
	{
		// visit the <game> nodes
		for (node *game = menu->first_node("game"); game != 0; game = game->next_sibling("game"))
		{
			// pull out the name attribute
			attr *nameAttr = game->first_attribute("name");
			char *name = nameAttr != nullptr ? nameAttr->value() : nullptr;

			// scan for subnodes of interest to populate our fields
			char *desc = nullptr;
			TSTRING manufName;
			const char *gridPos = nullptr;
			const char *rom = nullptr;
			const char *tableType = nullptr;
			int year = 0;
			TSTRING ipdbId;
			bool enabled = true;
			float rating = 0.0f;
			for (node *n = game->first_node(); n != 0; n = n->next_sibling())
			{
				char *id = (char *)n->name();
				if (_stricmp(id, "description") == 0)
					desc = n->value();
				else if (_stricmp(id, "manufacturer") == 0)
					manufName = AnsiToTSTRING(n->value());
				else if (_stricmp(id, "year") == 0)
					year = atoi((char *)n->value());
				else if (_stricmp(id, "enabled") == 0)
					enabled = (_stricmp(n->value(), "true") == 0);
				else if (_stricmp(id, "rom") == 0)
					rom = n->value();
				else if (_stricmp(id, "rating") == 0)
					rating = (float)atof(n->value());
				else if (_stricmp(id, "gridposition") == 0)
					gridPos = n->value();
				else if (_stricmp(id, "type") == 0)
					tableType = n->value();
				else if (_stricmp(id, "ipdbid") == 0)
					ipdbId = AnsiToTSTRING(n->value());
			}

			// if the entry has a valid filename and title, add it
			if (name != 0 && desc != 0)
			{
				// The "description" in the PinballX database is conventionally in the 
				// form "Title (Manufacturer YYYY)".  That's redundant with the separate
				// fields provided for the manufacturer and year, but it's the way they
				// did it, so we're stuck with it if we want to parse their files.  It
				// does have the advantage that we can use the description elements as
				// fallbacks in case the separate fields weren't specified.  
				//
				// This can also be just "Title (Manufacturer)" or "Title (Year)" when
				// only one of the items is known, so check for the three formats.
				//
				// First, try parsing out a parenthetical suffix, respecting nested
				// parentheses.  Scan backwards from the right of the string to see if
				// it ends with a right paren (ignoring trailing spaces), and if so,
				// scan backwards from there for the matching open paren.
				CSTRING title;
				const char *p;
				for (p = desc + strlen(desc); p > desc && isspace(*(p - 1)); --p);
				if (p > desc && *(p-1) == ')')
				{
					// yes, it ends with a paren - scan for the matching open paren
					const char *rightParen = --p;
					int level = 1;
					while (level > 0 && p > desc)
					{
						// move to the previous character
						--p;

						// count nesting levels
						if (*p == '(')
							--level;
						else if (*p == ')')
							++level;
					}

					// did we find the matching open paren?
					if (level == 0)
					{
						// yes, we found the open paren
						const char *leftParen = p;

						// skip spaces to the left of the open paren
						for (; p > desc && isspace(*(p - 1)); --p);
						const char *titleEnd = p;

						// skip spaces to at the start of the title portion
						for (p = desc; isspace(*p) && p < titleEnd; ++p);

						// make sure we found a non-empty title portion
						if (titleEnd > p)
						{
							// We have a non-empty title and a parenthesized
							// suffix, so this looks like the canonical format.
							// Pull out the suffix.
							CSTRING suffix(leftParen + 1, rightParen - leftParen - 1);

							// Check if matches one of the standard suffix patterns:
							// (Manufacturer Year), (Manufacturer), or (Year).
							static const std::regex patManYear("\\s*(.*?)\\s+(\\d{4})\\s*");
							static const std::regex patYear("\\s*(\\d{4})\\s*");
							static const std::regex patMan("\\s*(.*?)\\s*");
							std::match_results<CSTRING::const_iterator> m;
							if (std::regex_match(suffix, m, patManYear))
							{
								// We matched the "Title (Manufacturer Year)" pattern.  Pull out the
								// base title stripped of the suffix, and use the manufacturer and
								// year in the suffix to infer the corresponding metadata items if
								// they weren't explicitly specified.
								title.assign(p, titleEnd - p);
								if (manufName.length() == 0)
									manufName = AnsiToTSTRING(m[1].str().c_str());
								if (year == 0)
									year = atoi(m[2].str().c_str());
							}
							else if (std::regex_match(suffix, m, patYear))
							{
								// Matched the "Title (Year)" pattern.  There's an off chance that
								// the YYYY pattern is actually the manufacturer name, since that
								// would end up looking just the same in the combined string.  We
								// obviously can't tell from the string itself (since it looks the
								// same either way), but we can at least check it against the
								// explicit metadata: if it matches, it must not be the year.
								title.assign(p, titleEnd - p);
								if (year == 0 && (manufName.length() == 0 || AnsiToTSTRING(m[1].str().c_str()) != manufName))
									year = atoi(m[1].str().c_str());
							}
							else if (std::regex_match(suffix, m, patMan))
							{
								// matched the "Title (Manufacturer)" pattern
								title.assign(p, titleEnd - p);
								if (manufName.length() == 0)
									manufName = AnsiToTSTRING(m[1].str().c_str());
							}
						}
					}
				}

				// If we didn't assign a title, we must not have matched a 
				// valid suffix format.  Use the entire description as the
				// title.
				if (title.length() == 0)
					title = desc;

				// look up or create the manufacturer object
				GameManufacturer *manuf = FindOrAddManufacturer(manufName.c_str());

				// make sure there's an appropriate era filter
				FindOrAddDateFilter(year);

				// form the media name based on the description string
				TSTRING mediaName = GameListItem::CleanMediaName(AnsiToTSTRING(desc).c_str());

				// add the entry
				GameListItem &g = games.emplace_back(
					mediaName.c_str(), title.c_str(), name, manuf, year, ipdbId.c_str(),
					tableType, rom, system, enabled, gridPos);

				// log it
				Log(_T("++ adding game %hs, table file %hs, media file base name %s\n"),
					title.c_str(), name, mediaName.c_str());

				// remember the table file set for the system, and set the file
				// entry in the system's table file list (if one exists) to point
				// back to the game list entry
				g.tableFileSet = system->tableFileSet;
				auto tableFile = system->tableFileSet->FindFile(g.filename.c_str(), system->defExt.c_str(), true);
				tableFile->game = &g;

				// remember the game's XML source location
				g.dbFile = xml.get();
				g.gameXmlNode = game;

				// set the PBX rating
				g.pbxRating = rating;
			}
		}
	}

	// Hand over the XML file to the system.  The system object
	// will own the file object from now on.  This allows the
	// system to rewrite the XML file if we make any changes,
	// such as adding tem or moving a game to a different
	// category file.
	system->dbFiles.emplace_back(xml.release());

	// success 
	return true;
}

TSTRING GameList::GetDataFilePath(const TCHAR *configVarName, const TCHAR *defaultFolder,
	int promptStringID, ErrorHandler &eh)
{
	// get the config variable setting
	TSTRING cfgVal = ConfigManager::GetInstance()->Get(configVarName, _T(""));

	// if the variable isn't set, apply a default
	if (std::regex_match(cfgVal, std::basic_regex<TCHAR>(_T("\\s*"))))
	{
		// It's not set in the configuration, so apply a default.
		//
		// Before Alpha 21, an empty media path meant "Auto" mode, which used
		// the PinballX Media folder if present, otherwise the local Media
		// folder in our program folder.  Starting with Alpha 21, there is no
		// "Auto" mode, because it was too confusing to have the path change
		// if you installed or uninstalled PinballX.  Instead, an empty path
		// just means that we have to select a specific path.  So if we find
		// an empty path, it means that we're not set up with a specific path
		// yet, so infer what to do based on the system configuration:
		//
		// * If PinballX is installed, ask the user whether to use the 
		//   PinballX or PinballY media folder.
		//
		// * Otherwise, just use the PinballY media folder.
		//
		if (const TCHAR *pbx = GetPinballXPath(true); pbx != nullptr)
		{
			// PinballX is available.  Ask whether they want to share files with
			// PinballX or use the PinballY folders.
			//
			// Only ask the question once per session.  Apply the same answer each
			// time if we encounter another variable in need of a default.
			static int promptResult = 0;
			if (promptResult == 0)
			{
				// this is the first time we've encountered this issue - prompt for 
				// the user's preference
				class FolderDialog : public Dialog
				{
				public:
					FolderDialog() : result(0) { }

					int result;

					virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam)
					{
						switch (message)
						{
						case WM_COMMAND:
							switch (wParam)
							{
							case MAKEWPARAM(IDC_BTN_PINBALLX, BN_CLICKED):
							case MAKEWPARAM(IDC_BTN_PINBALLY, BN_CLICKED):
								result = LOWORD(wParam);
								EndDialog(hDlg, 0);
								break;

							case IDOK:
							case IDCANCEL:
								// ignore these
								return 0;
							}
							break;
						}

						// use the inherited handling
						return __super::Proc(message, wParam, lParam);
					}
				};
				FolderDialog dlg;
				dlg.Show(IDD_PBX_OR_PBY);

				// save the dialog result in the static, so that we can use the same
				// result without asking again if we encounter another variable in need
				// of the same defaulting
				promptResult = dlg.result;
			}

			// set the new value based on the result
			if (promptResult == IDC_BTN_PINBALLX)
			{
				cfgVal = _T("[PinballX]\\");
				cfgVal += defaultFolder;
			}
			else
				cfgVal = defaultFolder;

		}
		else
		{
			// PinballX isn't installed.  Use the PinballY path.
			cfgVal = defaultFolder;
		}

		// in either case, save the updated media path in the config
		ConfigManager::GetInstance()->Set(configVarName, cfgVal.c_str());
	}

	// Replace [PinballX] with the PBX folder path
	auto static const pbxVarPath = std::basic_regex<TCHAR>(_T("\\[pinballx\\]"), std::regex_constants::icase);
	if (std::regex_search(cfgVal, pbxVarPath))
	{
		// get the PBX folder path
		const TCHAR *pbxPath = GetPinballXPath();

		// make sure we found a path
		if (pbxPath == nullptr)
		{
			// note the error, and substitute an obvious error pattern
			eh.Error(MsgFmt(IDS_ERR_PBXPATH_NOT_AVAIL, LoadStringT(promptStringID).c_str(), cfgVal.c_str()));
			pbxPath = _T("C:\\PinballX_Not_Installed");
		}

		// make the substitution
		cfgVal = std::regex_replace(cfgVal, pbxVarPath, pbxPath);
	}

	// If the path is specified in relative notation, get the full path 
	// relative to the deployment folder, otherwise return the exact path
	// as given.
	if (PathIsRelative(cfgVal.c_str()))
	{
		// it's relative - resolve it relative to the deployment folder
		TCHAR buf[MAX_PATH];
		GetDeployedFilePath(buf, cfgVal.c_str(), _T(""));
		return buf;
	}
	else
	{
		// it's an absolute path - use it exactly as given
		return cfgVal;
	}
}
	

int GameList::GetStatsDbRow(const TCHAR *gameId, bool createIfNotFound)
{
	// look up the game in our index
	if (auto it = statsDbIndex.find(gameId); it != statsDbIndex.end())
		return it->second;

	// if they didn't want to create a new row, return "not found"
	if (!createIfNotFound)
		return -1;

	// Create a row in the stats database
	return AddStatsDbRow(gameId);
}

int GameList::AddStatsDbRow(const TCHAR *gameId)
{
	// create a new, empty row
	int row = statsDb.CreateRow();

	// set the Game column in the new row to the game ID
	gameCol->Set(row, gameId);
	
	// add the row to our db index
	statsDbIndex.emplace(gameId, row);

	// return the new row
	return row;
}

int GameList::GetStatsDbRow(GameListItem *game, bool createIfNotFound)
{
	// Start with the row number stored in the game object itself
	int row = game->statsDbRow;

	// If the game object's row number field -2, it means that
	// we haven't done the lookup yet.  Do so now.
	if (row == -2)
	{
		// look up the game in our index
		TSTRING id = game->GetGameId();
		auto it = statsDbIndex.find(id);

		// If we didn't find a match, try looking up the game by its
		// "old" (pre-1.0 Beta 3) ID.  If we find it under that ID,
		// update it to use the new ID instead.
		if (it == statsDbIndex.end() && (it = statsDbIndex.find(game->GetOldGameId())) != statsDbIndex.end())
		{
			// note the row
			int row = it->second;

			// replace the index entry with the new key
			statsDbIndex.erase(it);
			it = statsDbIndex.emplace(id, row).first;

			// update the row in the database
			gameCol->Set(row, id.c_str());
		}

		// If we found a matching row, store the row number in the
		// game object and return the row.
		if (it != statsDbIndex.end())
			return game->statsDbRow = it->second;

		// Not found.  Explicitly update the game object's row number
		// field to -1, so that we don't have to repeat this index 
		// lookup on the next access.  The -1 will tell us that we've
		// already tried looking up the game and found that is has no
		// database row.
		row = game->statsDbRow = -1;
	}

	// If the row number is -1, it means that there's no row for this
	// game in the stats database.  Create a new entry for it if the
	// caller so desires.
	if (row == -1 && createIfNotFound)
	{
		// Create a new, empty row in the stats database
		row = game->statsDbRow = AddStatsDbRow(game->GetGameId().c_str());
	}

	// return the row number
	return row;
}

std::list<const GameCategory*> GameList::GetAllCategories() const
{
	// build a list of the categories
	std::list<const GameCategory*> list;
	for (auto &it : categories)
		list.emplace_back(it.second.get());

	// return the list
	return list;
}

GameCategory *GameList::GetCategoryByName(const TCHAR *name) const
{
	// look up the category by name
	auto it = categories.find(name);
	return it != categories.end() ? it->second.get() : nullptr;
}

bool GameList::CategoryExists(const TCHAR *name) const
{
	return GetCategoryByName(name) != nullptr;
}

GameCategory *GameList::FindOrCreateCategory(const TCHAR *name)
{
	// look up the category by name - if there's already a matching 
	// category, simply return it
	auto it = categories.find(name);
	if (it != categories.end())
		return it->second.get();

	// create a new category object
	GameCategory *newcat = new GameCategory(name);

	// add it to the map
	categories.emplace(
		std::piecewise_construct,
		std::forward_as_tuple(name),
		std::forward_as_tuple(newcat));

	// we'll need to update the filter list to include the new category
	isFilterListDirty = true;

	// return the new category object
	return newcat;
}

void GameList::NewCategory(const TCHAR *name)
{
	// if the category exists, do nothing
	if (CategoryExists(name))
		return;

	// create the category
	GameCategory *category = FindOrCreateCategory(name);

	// assign it a command ID for the menu system 
	category->cmd = nextFilterCmdID++;
}

void GameList::RenameCategory(GameCategory *category, const TCHAR *newName)
{
	// update the name in the category object
	const TSTRING oldName = category->name;
	category->name = newName;

	// The name of the category is also its key in the category
	// map, so we have to remove the existing map entry and add
	// a new one.  There's a slight trick to this: the map entry
	// owns the category object's memory, so we have to take
	// ownership of the pointer before deleting the entry, then
	// convey ownership to the new entry we add.
	if (auto it = categories.find(oldName); it != categories.end())
	{
		// take ownership of the pointer from the map entry
		it->second.release();

		// delete the old map entry
		categories.erase(it);

		// create a new map entry under the new name
		categories.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(newName),
			std::forward_as_tuple(category));
	}

	// This might change the ordering of the master filter list,
	// so mark it as dirty.
	isFilterListDirty = true;

	// The category name might also be the file name of one or
	// more system database files.  Go through all database files
	// tied to this category and rename them.  Start by iterating
	// over all systems...
	for (auto &s : systems)
	{
		// now iterate over all db files for this system...
		for (auto &f : s.second.dbFiles)
		{
			// check for a match to this category
			if (f->category == category)
			{
				// In PinballX, a category file defines a category
				// name via its filename.  So changing the category
				// name means we want to change the filename to
				// match.  Build the new name by replacing the
				// existing file spec with the category name plus
				// the .xml suffix.
				TCHAR newfname[MAX_PATH];
				_tcscpy_s(newfname, f->filename.c_str());
				PathRemoveFileSpec(newfname);
				PathAppend(newfname, newName);
				_tcscat_s(newfname, _T(".xml"));

				// Try renaming the file.  If the file contains any
				// path-related characters or outright invalid characters,
				// don't bother trying.
				std::basic_regex<TCHAR> invPat(_T("[\\\\/*?+:\"|<>]"));
				if (!std::regex_search(newName, invPat) && MoveFile(f->filename.c_str(), newfname))
				{
					// Success - the category name is now implicit in the
					// filename.  If we previously added a <CategoryName>
					// tag to the file's contents (see below), we can now
					// remove it.
					if (auto root = f->doc.first_node(); root != nullptr)
					{
						if (auto node = root->first_node("CategoryName"); node != nullptr)
						{
							// remove the now-unnecessary <CategoryName> tag
							root->remove_node(node);

							// we now have unsaved changes in the file
							f->isDirty = true;
						}
					}

					// Update the filename in the file object
					f->filename = newfname;
				}
				else
				{
					// Failure - we can't use the filename to imply the
					// category name as usual.  This could be due to an
					// ill-formed filename (e.g., invalid characters we
					// didn't already catch in our regex test above, or
					// an over-long path) or some runtime file system
					// error, such as a name collision or permissions
					// restriction.  In any case, we have a fallback:
					// we can insert a special <CategoryName> node into
					// the XML to override the filename as the source of
					// the category name.  PinballX doesn't use that, so
					// it won't get the category name correct if the 
					// user runs PinballX on this database later, so
					// *also* make an attempt to give the file a close-
					// enough name, for PinballX's sake, as well as to
					// make things clearer in if the user manually
					// browses the file system folders.  For the
					// "close enough" name, replace invalid characters
					// in the proposed filename with approximations.
					// 
					// Note that this name munging affects only the name
					// of the file, NOT the category name as it appears 
					// in the UI.  The category name is exactly as the
					// user set it no matter what happens with the filename,
					// thanks to the <CategoryName> insertion below.
					for (TCHAR *p = PathFindFileName(newfname); *p != 0; ++p)
					{
						static const TCHAR *from = _T("/*?+:|<>\"\\");
						static const TCHAR *to = _T(";#;-;;()'_");
						if (auto f = _tcschr(from, *p); f != nullptr)
							*p = to[f - from];
					}

					// Try the rename again with the modified name.  If
					// this fails, give up on the renaming.  As with the
					// munged name, this doesn't affect the category name
					// in the UI, so we don't lose any functionality if
					// this fails; we merely leave behind a file on disk
					// with a name that doesn't match its category as 
					// shown in the UI, which will only matter to the 
					// extent that the user browses the folder manually.
					// Which they shouldn't need to do, thanks to our
					// full-featured UI that offers in-program commands
					// for most operations you'd want to do on the files.
					if (MoveFile(f->filename.c_str(), newfname))
					{
						// the rename succeeded - note the new filename
						// in the in-memory file object
						f->filename = newfname;
					}

					// Whatever happened, add a <CategoryName> tag (or
					// replace an existing one) that records the actual
					// category name as assigned by the user.  This will
					// supersede the filename as the basis for naming the
					// category the next time the program runs, to ensure
					// that the category name as shown in the UI always
					// exactly matches what the user entered, even though 
					// we couldn't give the file that exact name on disk.
					if (auto root = f->doc.first_node(); root != nullptr)
					{
						auto value = f->doc.allocate_string(TCHARToAnsi(newName).c_str());
						auto node = root->first_node("CategoryNode");
						if (node != nullptr)
							node->value(value);
						else
							node = f->doc.allocate_node(rapidxml::node_element, "CategoryName", value);
					}
				}
			}
		}
	}

	// Notify all of the games associated with the category
	// that we're renaming the category.
	for (auto &g : games)
	{
		if (IsInCategory(&g, category))
			OnRenameCategory(&g, category, oldName.c_str());
	}
}

void GameList::DeleteCategory(GameCategory *category)
{
	// first, delete the category from all games that include it
	for (auto &g : games)
		RemoveCategory(&g, category);

	// remove the category from the filter list
	if (auto it = std::find(filters.begin(), filters.end(), category); it != filters.end())
		filters.erase(it);

	// For debugging purposes and protection against self-inflicted errors,
	// we're not going to actually delete the GameCategory object.  We hand
	// out references to this object through the public interface, so there
	// could be some outstanding references elsewhere in the system.  There
	// *shouldn't* be, but like I said, this is for debugging/crashproofing.
	// Instead of deleting it, we're going to take it out of the name table
	// and move it to a list of deleted categories.  That will keep the
	// object alive (and thus keep any remaining pointers to it valid) for
	// the life of the session.  As a debugging aid, we'll change the name
	// to make it obvious that it's been deleted; if we do have any pointers
	// floating around and someone uses one in the UI or a config file, we'll
	// be able to see what happened.  It might still be embarrassing, but
	// it's much less embarrassing than crashing, and will provide much
	// better diagnostic information if it happens.

	// find it in the category map
	if (auto it = categories.find(category->name); it != categories.end())
	{
		// The category map normally owns the object memory.  Assume 
		// ownership by asking the map entry to release the pointer.
		it->second.release();

		// delete the map entry
		categories.erase(it);
	}

	// change the name so that it's obvious that it's been deleted, should
	// someone screw up and use the pointer after this point
	category->name += _T(" [DELETED]");

	// Add it to the deleted categories list.  This list (like the original
	// map) holds the pointer in a std::unique_ptr, so it assumes ownership
	// of the memory and responsibility for deleting it when the list is
	// destroyed.
	deletedCategories.emplace_back(category);
}

void GameList::SetCategories(GameListItem *game, const std::list<const GameCategory*> &newCats)
{
	// get the game's current category list
	std::list<const GameCategory*> oldCats;
	GetCategoryList(game, oldCats);

	// Add categories that appear in the new list but aren't
	// in the old list.  Don't rebuild the stats database row
	// on each addition, since we might have multiple changes
	// to make - wait until we're done with the whole set and
	// just rebuild it once.
	bool changed = false;
	for (auto c : newCats)
	{
		if (findex(oldCats, c) == oldCats.end())
		{
			JustAddCategory(game, c);
			changed = true;
		}
	}

	// Remove categories that appear in the old list but aren't
	// in the new list.
	for (auto c : oldCats)
	{
		if (findex(newCats, c) == newCats.end())
		{
			JustRemoveCategory(game, c);
			changed = true;
		}
	}

	// If we made any changes, rebuild the column in the stats db
	if (changed)
	{
		// Get the row.  Note that we don't have to create a new row if one
		// doesn't already exist, because rebuilding the category list merely
		// syncs the two data formats stored in the same row/column inter-
		// section.  If that intersection doesn't exist already, there's
		// nothing to bring into sync.
		int row = GetStatsDbRow(game);

		// if there's a row, rebuild the field
		if (row >= 0)
			RebuildCategoryList(row);
	}
}

void GameList::AddCategory(GameListItem *game, const GameCategory *category)
{
	// add the category if it's not already in the list
	if (!IsInCategory(game, category))
	{
		// update the parsed list
		JustAddCategory(game, category);

		// rebuild the string form of the column data if desired
		RebuildCategoryList(GetStatsDbRow(game));
	}
}

GameDatabaseFile *GameList::GetGenericDbFile(GameSystem *system, bool create)
{
	// Find the generic XML file for the system.  It's the one 
	// that has no associated category object.
	for (auto const &f : system->dbFiles)
	{
		if (f->category == nullptr)
			return f.get();
	}

	// There's no generic file - this is entirely possible, since
	// a system's database files can all be category files.  If
	// desired, create a new generic file for the system.
	if (create)
	{
		// create the file object and add it to the system's file list
		auto dbFile = new GameDatabaseFile();
		system->dbFiles.emplace_front(dbFile);

		// load an empty initial XML document
		dbFile->Load("<menu></menu>", SilentErrorHandler());

		// give it the name of the system's generic file
		dbFile->filename = system->genericDbFilename;

		// return the new file
		return dbFile;
	}

	// no file found or created
	return nullptr;
}

void GameList::MoveGameToDbFile(GameListItem *game, GameDatabaseFile *dbFile)
{
	// If no database file was specified, it means that we're to
	// move the game to the "generic" uncategorized file for the 
	// system.  Create the generic file if it doesn't already exist.
	if (dbFile == nullptr)
		dbFile = GetGenericDbFile(game->system, true);

	// add the game's XML node to the generic file's XML tree
	if (auto newParent = dbFile->doc.first_node(); newParent != nullptr)
	{
		// get the game's defining node
		if (auto gameNode = game->gameXmlNode; gameNode != nullptr)
		{
			// remove it from its existing document
			if (gameNode->parent() != nullptr)
				gameNode->parent()->remove_node(gameNode);

			// add it to the new document
			newParent->append_node(gameNode);

			// both the old and new db files now have unsaved changes
			dbFile->isDirty = true;
			if (game->dbFile != nullptr)
				game->dbFile->isDirty = true;
		}

		// set the game's new source file location
		game->dbFile = dbFile;
	}
}

void GameList::JustAddCategory(GameListItem *game, const GameCategory *category)
{
	// Check to see if we can categorize the game by XML file placement.
	// If the game isn't currently in a categorizing database file (that
	// is, it's either not in a database file at all, or it's in the
	// "generic" database file for its system, which has no category
	// association), AND the game's system has an existing XML file for
	// this category, move the game into that category file.  
	//
	// Note that we don't create a new category file if there isn't one
	// here already, because category files are really more for legacy 
	// PinballX compatibility than for our own purposes.  We could even
	// just ignore the PinballX categorization scheme on updates, since
	// our stats database provides a more general mechanism.  But we
	// use PinballX categorization on update when possible anyway, for
	// maximum interoperability.  This will allow PinballX to see any
	// category changes (for a single category per game, at least) if
	// the user ever wants to take the same data back to PinballX.
	if (game->dbFile == nullptr || game->dbFile->category == nullptr)
	{
		// It passes the first check - the game isn't currently in a
		// category file.  Now check if there's an existing category
		// file for this category in this system.
		if (game->system != nullptr)
		{
			for (auto &f : game->system->dbFiles) 
			{
				if (f->category == category)
				{
					// We found the category file for this system and
					// this category.  Establish the categorization by
					// moving the game's XML record into this file.
					MoveGameToDbFile(game, f.get());

					// that completes the category addition
					return;
				}
			}
		}
	}

	// We didn't add the category by XML file placement, so use our
	// stats file category list instead.  Simply add the category to
	// the parsed category list for the game.  This requires creating
	// the database row and the parsed data object, if it doesn't
	// already exist.
	int row = GetStatsDbRow(game, true);
	auto d = dynamic_cast<ParsedCategoryData*>(categoriesCol->GetParsedData(row));
	if (d == nullptr)
		categoriesCol->SetParsedData(row, d = new ParsedCategoryData());

	// add the category to the parsed list
	d->categories.push_back(category);
}


void GameList::RemoveCategory(GameListItem *game, const GameCategory *category)
{
	// remove the category if it's in the game's current list
	if (IsInCategory(game, category))
	{
		// update the parsed list and XML file location
		JustRemoveCategory(game, category);

		// Rebuild the stats db row, if there is one.  If there's isn't
		// already a row, there's no need to rebuild the string list,
		// because a remove operation obviously won't make an empty
		// list non-empty.
		if (int row = GetStatsDbRow(game, false); row >= 0)
			RebuildCategoryList(row);
	}
}

void GameList::JustRemoveCategory(GameListItem *game, const GameCategory *category)
{
	// Retrieve the parsed category list, if present.  There's no need
	// to create one just to remove a category, as we obviously wouldn't
	// find a list item to remove if there's no list at all.
	int row = GetStatsDbRow(game, false);
	auto d = dynamic_cast<ParsedCategoryData*>(categoriesCol->GetParsedData(row));
	if (d != nullptr)
	{
		// found it - remove the category from the list if present
		d->categories.remove(category);
	}

	// Now the tricky part!  If the category was established by the
	// game's XML file placement, we need to delete the game entry from
	// that XML file and move it to the generic XML file for the game's
	// system.
	if (game->dbFile != nullptr && game->dbFile->category == category && game->system != nullptr)
		MoveGameToDbFile(game, nullptr);
}

void GameList::OnRenameCategory(GameListItem *game, const GameCategory *category, const TCHAR * /*oldName*/)
{
	// If the game is associated with the category by way of the
	// stats database "Categories" column, that column in the
	// game's row already has a pointer to the category object
	// in its parsed data object, so it already "knows" about
	// the name change by reference to the category object.
	// But that means that we have to rebuild the string version
	// of the column data.  If we don't have a database row or
	// a category column already, there's no need to add one,
	// since that means we don't have such a pointer - our
	// association with the category must come from the XML
	// file placement instead.
	if (auto rownum = GetStatsDbRow(game, false); rownum >= 0)
		RebuildCategoryList(rownum);
}

void GameList::RebuildCategoryList(int rownum)
{
	// ignore this for invalid rows
	if (rownum < 0)
		return;

	// Get the category list.  If there isn't one already, there's
	// no Categories column in this row, so there's nothing to
	// rebuild and we can simply skip this.
	if (auto d = dynamic_cast<ParsedCategoryData*>(categoriesCol->GetParsedData(rownum)); d != nullptr)
	{
		// Build a list of category names
		std::list<TSTRING> catNames;
		for (auto cat : d->categories)
			catNames.emplace_back(cat->name);

		// Construct the comma-separated list, using CSV format rules
		TSTRING buf;
		CSVFile::CSVify(catNames, [&buf](const TCHAR *segment, size_t len) {
			buf.append(segment, len);
			return true;
		});

		// set the text list form of the category list
		categoriesCol->Set(rownum, buf.c_str());
	}
}

void GameList::GetCategoryList(GameListItem *game, std::list<const GameCategory*> &cats)
{
	// look up the category list in the stats database
	if (auto d = dynamic_cast<ParsedCategoryData*>(categoriesCol->GetParsedData(GetStatsDbRow(game, false))); d != nullptr)
	{
		// copy the categories to the caller's result list
		for (auto cat : d->categories)
			cats.push_back(cat);
	}

	// Also add the implicit category set by the XML file where the game
	// entry was defined, if there is one.
	if (game->dbFile != nullptr && game->dbFile->category != nullptr)
		cats.push_back(game->dbFile->category);
}

bool GameList::IsInCategory(GameListItem *game, const GameCategory *category)
{
	// look up the game's category list in the stats database
	if (auto d = dynamic_cast<ParsedCategoryData*>(categoriesCol->GetParsedData(GetStatsDbRow(game, false))); d != nullptr)
	{
		// We have a stats db entry.  Look for the category in that list.
		if (auto it = findex(d->categories, category); it != d->categories.end())
			return true;
	}

	// We didn't find the category in the stats db list, but the game
	// could still implicitly be in the category based on the XML file
	// where the game's entry was found.
	return game->dbFile == nullptr ? false : game->dbFile->category == category;
}

bool GameList::IsUncategorized(GameListItem *game)
{
	// Look up the game's category list in the stats database.  If we have an
	// entry with one or more categories listed, we're not uncategorized.
	if (auto d = dynamic_cast<ParsedCategoryData*>(categoriesCol->GetParsedData(GetStatsDbRow(game, false)));
		d != nullptr && d->categories.size() != 0)
		return false;

	// We don't have any stats db category entries, so we're uncategorized
	// so far.  But there's one more possibility: we could have an implicit
	// category based on the XML file where our game entry was found.  So
	// we're uncategorized if there's no category for the file.
	return game->dbFile == nullptr || game->dbFile->category == nullptr;
}

void GameList::ParseCategoryList(int row)
{
	// get the string data from the row
	if (const TCHAR *txt = categoriesCol->Get(row, nullptr); txt != nullptr && txt[0] != 0)
	{
		// create a category list data object to store in the stats row
		std::unique_ptr<ParsedCategoryData> data(new ParsedCategoryData());

		// Parse the file text using CSV format rules
		std::list<TSTRING> catNames;
		CSVFile::ParseCSV(txt, -1, catNames);

		// add each category to the column data object
		for (auto &catName : catNames)
			data->categories.push_back(FindOrCreateCategory(catName.c_str()));

		// Store the category data object in the database row.  Note that we
		// release the pointer to hand over ownership of the memory.
		categoriesCol->SetParsedData(row, data.release());
	}
}

DateFilter *GameList::FindOrAddDateFilter(int year)
{
	// year 0 represents missing metadata, so there's no filter
	if (year == 0)
		return nullptr;

	// For years before 2000, we create an era filter by decade 
	// (1970s, 1980s, etc).  For 2000 and beyond, we use a single 
	// "2000s" filter.  We *could* filter by decade even in the 
	// 2000s, but I think it works better to lump them all into 
	// one bucket for now, for two reasons.  Reason 1: the rate
	// of new machines released after 1998 is so slow that we
	// don't need multiple buckets for organizational purposes.
	// Reason 2: the evolution of design and technology hasn't
	// reached the point where you'd say today's machines are
	// recognizably different from 1998's machines; there's no
	// distinct new "era" in terms of design or tech yet.  For
	// the time being, "2000+" is good enough.

	// figure the date range: for pre-2000 games, use the decade;
	// lump dates after 2000 into a single category
	int decade = (year / 10) * 10;
	int yearFrom, yearTo;
	TSTRING dateFilterTitle;
	if (decade < 2000)
	{
		yearFrom = decade;
		yearTo = decade + 9;
		dateFilterTitle = MsgFmt(IDS_FILTER_DECADE, yearFrom % 100);
	}
	else
	{
		yearFrom = 2000;
		decade = 2000;
		yearTo = 9999;
		dateFilterTitle = LoadStringT(IDS_FILTER_2000S);
	}

	// find an existing filter
	if (auto it = dateFilters.find(yearFrom); it != dateFilters.end())
		return &it->second;

	// there's no such decade filter yet - add one
	auto it = dateFilters.emplace(
		std::piecewise_construct,
		std::forward_as_tuple(decade),
		std::forward_as_tuple(dateFilterTitle.c_str(), yearFrom, yearTo));

	// mark the master filter list as dirty
	isFilterListDirty = true;

	// return the new filter
	return &it.first->second;
}

GameManufacturer *GameList::FindOrAddManufacturer(const TCHAR *name)
{
	// if the name is empty, use a null manufacturer
	if (name == nullptr || name[0] == 0 || std::regex_match(name, std::basic_regex<TCHAR>(_T("^\\s*$"))))
		return nullptr;

	// look up an existing manufacturer
	if (auto itMan = manufacturers.find(name); itMan != manufacturers.end())
		return &itMan->second;

	// No entry yet.  Mark the master filter list as dirty, and create a new entry.
	isFilterListDirty = true;
	return &manufacturers.emplace(
		std::piecewise_construct,
		std::forward_as_tuple(name),
		std::forward_as_tuple(name)).first->second;
}

void GameList::EnumManufacturers(std::function<void(const GameManufacturer*)> func)
{
	for (auto const &m : manufacturers)
		func(&m.second);
}

void GameList::ChangeSystem(GameListItem *game, GameSystem *newSystem)
{
	// If we're not changing systems, do nothing
	if (newSystem == game->system)
		return;

	// If we're currently associated with a system, our XML record
	// is in the old system's database file, so the first step is
	// to remove it from the old XML tree.
	if (game->system != nullptr && game->gameXmlNode != nullptr && game->gameXmlNode->parent() != nullptr)
		game->gameXmlNode->parent()->remove_node(game->gameXmlNode);

	// Get the game's current category list.  The game's database
	// file location can give it a category, so before we remove
	// it from its current file, we need to remember if that gave
	// the game a category so that we can restore it when deciding
	// where to put the game in the new system's db files.
	auto gl = GameList::Get();
	std::list<const GameCategory*> oldCats;
	gl->GetCategoryList(game, oldCats);

	// remove the game from its current database file
	game->dbFile = nullptr;

	// Remember the new system
	game->system = newSystem;

	// If we have a new system, move the XML record into the new
	// system's database file.
	GameDatabaseFile *newDbFile = nullptr;
	if (newSystem != nullptr)
	{
		// Find a suitable target database file.  If we have any
		// categories, look for a db file associated with one of
		// our categories - that maximized PinballX interop by
		// allowing PBX to recognize the category association
		// should the user ever take the data back to PBX.
		for (auto &f : newSystem->dbFiles)
		{
			// if this file has a category, and it's in our category
			// list, use this as the new file location
			if (f->category != nullptr
				&& findex(oldCats, f->category) != oldCats.end())
			{
				newDbFile = f.get();
				break;
			}
		}

		// If we didn't find a category file, use the generic file
		// for the system, creating one if necessary.
		if (newDbFile == nullptr)
			newDbFile = gl->GetGenericDbFile(newSystem, true);

		// Move the game into the new file
		MoveGameToDbFile(game, newDbFile);
	}

	// Fix up the category list.  The storage for the category list
	// is a bit muddled because of our PinballX support.  The db file
	// placement might imply one category, and the stats db row lists
	// the rest of the categories.  What makes it muddled is that the
	// stats db row listing excludes the category implied by the db 
	// file placement.  Moving the game to a new system creates an
	// especially difficult situation: we might have been in one
	// category db file in the old system, and a different category
	// db file in the new system.  Fortunately, we have the complete
	// list of old categories, and we know which one of those is
	// implied by the placement in the new file.  So we'll simply
	// replace the old stats db list with the old category list
	// MINUS the new db file placement category.
	if (newDbFile != nullptr && newDbFile->category != nullptr)
		oldCats.remove(newDbFile->category);

	// Get the category list from the stats db row.  If the new
	// category list is non-empty, we'll have to create the field;
	// if the list is empty, we can leave it blank, since blank
	// is the same as an empty list anyway.
	int row = GetStatsDbRow(game, oldCats.size() != 0);
	if (row >= 0)
	{
		// the row exists - get the field
		auto d = dynamic_cast<ParsedCategoryData*>(categoriesCol->GetParsedData(row));

		// if the field is missing, and we have categories to
		// set, we'll need to create the field
		if (d == nullptr && oldCats.size() != 0)
			categoriesCol->SetParsedData(row, d = new ParsedCategoryData());

		// if we have a field now, update it
		if (d != nullptr)
		{
			// clear the stats db list
			d->categories.clear();

			// copy the new category list into the stats db
			for (auto cat : oldCats)
				d->categories.push_back(cat);
		}

		// rebuild the text list from the parsed list
		RebuildCategoryList(row);
	}
}

void GameList::DeleteXml(GameListItem *game)
{
	// There's nothing to do if the game isn't in a db file or doesn't
	// have an XML record.
	if (game->dbFile == nullptr || game->gameXmlNode == nullptr)
		return;

	// get the document and parent node
	auto &doc = game->dbFile->doc;
	auto par = game->gameXmlNode->parent();
	if (par != nullptr)
	{
		// remove the node from its parent
		par->remove_node(game->gameXmlNode);

		// clear the node from the game
		game->gameXmlNode = nullptr;

		// the database file now has unsaved changes
		game->dbFile->isDirty = true;

		// the game is no longer associated with a db file
		game->dbFile = nullptr;

		// clear the XML-derived fields from the game record
		game->system = nullptr;
		game->manufacturer = nullptr;
		game->isConfigured = false;
		game->tableType = _T("");
		game->rom = _T("");
		game->ipdbId = _T("");
		game->year = 0;
		game->gridPos.col = game->gridPos.row = 0;

		// reset to a filename-based title and media file base name
		game->SetTitleFromFilename();
		game->UpdateMediaName(nullptr, nullptr);
		
		// commit the change to the game ID
		FlushGameIdChange(game);

		// rebuild the title index
		BuildTitleIndex();
	}
}

void GameList::FlushToXml(GameListItem *game)
{
	// There's nothing to do if the game isn't in a db file
	if (game->dbFile == nullptr)
		return;

	// If the game doesn't already have an XML record, create one. 
	// A game that was discovered in the file system rather than
	// in a database will have no XML until the user edits the
	// game record and adds it to a system.
	auto &doc = game->dbFile->doc;
	auto par = game->gameXmlNode;
	if (par == nullptr)
	{
		// create the root game node
		par = game->gameXmlNode = doc.allocate_node(rapidxml::node_element, "game");

		// add it to the document under the root <menu> node
		if (auto menuNode = doc.first_node("menu"); menuNode != nullptr)
			menuNode->append_node(par);

		// If the game is hidden, add <enabled>false</enabled>.  This is
		// only necessary when creating a NEW node, since we otherwise
		// keep <enabled> in sync on any update, and it's only necessary
		// if the game is hidden, since <enabled>true</enabled> is the
 		// default in the absence of the tag.
		if (game->IsHidden())
			par->append_node(doc.allocate_node(rapidxml::node_element, "enabled", "False"));
	}

	// Get the filename to use in the <game name="xxx"> attribute. 
	// This is just the game filename, except that we remove the
	// extension if it matches the default extension for the system.
	// This is just for PinballX's sake, if the user ever wants to
	// repatriate the database files; PBX's convention is to store
	// the filename sans extension.
	TSTRING nameAttrValBuf;
	const TCHAR *nameAttrVal = game->filename.c_str();
	if (game->system != nullptr
		&& game->system->defExt.length() != 0
		&& tstriEndsWith(nameAttrVal, game->system->defExt.c_str()))
	{
		// it ends with the default extension - remove it
		nameAttrValBuf.assign(nameAttrVal, game->filename.length() - game->system->defExt.length());
		nameAttrVal = nameAttrValBuf.c_str();
	}

	// If there's no <game name="xxx"> attribute yet, add it
	auto nameAttr = par->first_attribute("name");
	if (nameAttr == nullptr)
		par->append_attribute(nameAttr = doc.allocate_attribute("name"));

	// Update <game name="xxx"> with the game filename
	nameAttr->value(doc.allocate_string(TCHARToAnsi(nameAttrVal).c_str()));

	// Add or update a child node
	auto UpdateChildA = [game, par, &doc](const char *name, const char *val)
	{
		// if the child doesn't exist, add it
		auto child = par->first_node(name);
		if (child == nullptr)
			par->append_node(child = doc.allocate_node(rapidxml::node_element, name));

		// set the value
		child->value(doc.allocate_string(val));
	};
	auto UpdateChildT = [game, par, &doc, UpdateChildA](const char *name, const TCHAR *val)
	{
		UpdateChildA(name, TCHARToAnsi(val).c_str());
	};

	// store the IPDB ID, if known
	UpdateChildT("ipdbid", game->ipdbId.c_str());

	// store the description (what we call the media name)
	UpdateChildT("description", game->mediaName.c_str());

	// store the table type
	UpdateChildT("type", game->tableType.c_str());

	// store the ROM name
	UpdateChildT("rom", game->rom.c_str());

	// store the manufacturer, or an empty string if there isn't one
	UpdateChildT("manufacturer", game->manufacturer != nullptr ? game->manufacturer->manufacturer.c_str() : _T(""));

	// store the year, or just a blank value if it's zero
	char year[10];
	sprintf_s(year, "%d", game->year);
	UpdateChildA("year", game->year != 0 ? year : "");

	// store the rating
	char rating[10];
	sprintf_s(rating, "%d", (int)game->pbxRating);
	UpdateChildA("rating", rating);

	// If there's a non-zero grid position, update it.  If the grid position
	// in the in-memory record is 0x0, and there's a grid position in the
	// XML, set the XML version to an empty string.  But if we're setting
	// it to empty, and there's not already an XML tag for it, don't add
	// one, as the game system probably doesn't use the tag at all in this
	// case, so it's better not to muddy the XML with the unused tag.
	static const char *gridPosTag = "gridposition";
	if (game->gridPos.row != 0 && game->gridPos.col != 0)
	{
		// We're setting a non-zero row x col address, so set the <gridposition>
		// element, whether or not it already exists.
		char buf[128];
		sprintf_s(buf, "%dx%d", game->gridPos.row, game->gridPos.col);
		UpdateChildA(gridPosTag, buf);
	}
	else if (par->first_node(gridPosTag) != nullptr)
	{
		// The new grid position is 0x0, which means that no position is
		// assigned, which we represent in the XML with an empty tag.  The
		// tag already exists in the XML, so we need to update it to empty.
		// Note that we're only doing this because the tag already exists;
		// if there's not such a tag already, we'll just continue to leave
		// it out of the file, which means the same thing as an empty tag
		// for the purposes of this tag and doesn't muddy the file with an
		// unnecessary tag for a system that probably doesn't ever use it.
		UpdateChildA(gridPosTag, "");
	}

	// the database file now has unsaved changes
	game->dbFile->isDirty = true;
}

void GameList::FlushGameIdChange(GameListItem *game)
{
	// if the game has a row in the stats database, update the Game
	// column with the new ID
	if (auto row = GetStatsDbRow(game, false); row >= 0)
	{
		// if there's an old ID value, delete its stats db index entry
		if (auto oldId = gameCol->Get(row, nullptr); oldId != nullptr)
			statsDbIndex.erase(oldId);

		// update the stats db field
		TSTRING newId = game->GetGameId();
		gameCol->Set(row, newId.c_str());

		// add a new index entry with the new ID
		statsDbIndex.emplace(newId, row);
	}
}

void GameList::EnumTableFileSets(std::function<void(const TableFileSet&)> func)
{
	for (auto &pair : tableFileSets)
		func(pair.second);
}

bool GameList::FindGlobalImageFile(TCHAR path[MAX_PATH], const TCHAR *subfolder, const TCHAR *file)
{
	static const TCHAR *imageExts[] = { _T(".png"), _T(".jpg"), _T(".jpeg") };
	return FindGlobalMediaFile(path, subfolder, file, imageExts, countof(imageExts));
}

bool GameList::FindGlobalVideoFile(TCHAR path[MAX_PATH], const TCHAR *subfolder, const TCHAR *file)
{
	static const TCHAR *videoExts[] = { _T(".mp4"), _T(".mpg"), _T(".f4v"), _T(".mkv"), _T(".wmv"), _T(".m4v"), _T(".avi") };
	return FindGlobalMediaFile(path, subfolder, file, videoExts, countof(videoExts));
}

bool GameList::FindGlobalAudioFile(TCHAR path[MAX_PATH], const TCHAR *subfolder, const TCHAR *file)
{
	static const TCHAR *audioExts[] = { _T(".mp3"), _T(".ogg"), _T(".wav") };
	return FindGlobalMediaFile(path, subfolder, file, audioExts, countof(audioExts));
}

bool GameList::FindGlobalWaveFile(TCHAR path[MAX_PATH], const TCHAR *subfolder, const TCHAR *file)
{
	static const TCHAR *audioExts[] = { _T(".wav") };
	return FindGlobalMediaFile(path, subfolder, file, audioExts, countof(audioExts));
}

bool GameList::FindGlobalMediaFile(TCHAR path[MAX_PATH], const TCHAR *subfolder, const TCHAR *file,
	const TCHAR *const *exts, size_t numExts)
{
	// Try the local <PinballY>\Media\<subfolder> first.  This is the first
	// choice because it's the location for user-supplied media specific to
	// PinballY.  (If we're sharing files with HyperPin or PinballX, we want
	// to use the PinballY version ahead of the shared version, since this
	// lets the user customize files just for PinballY without affecting the
	// other program.)
	TCHAR localMediaPath[MAX_PATH];
	GetDeployedFilePath(localMediaPath, _T("Media"), _T(""));
	PathAppend(localMediaPath, subfolder);
	PathCombine(path, localMediaPath, file);
	LogFile::Get()->Write(LogFile::MediaFileLogging, _T("Searching for %s in %s.*\n"), file, path);
	if (FindFileUsingExtensions(path, exts, numExts))
		return true;

	// No luck there.  Look in the <base media folder>\<subfolder> next.
	// Don't bother doing the file lookup if the path is identical to the
	// local media path, which it will be if this installation isn't set
	// up to share media files with HyperPin or PinballX.
	TCHAR baseMediaPath[MAX_PATH];
	PathCombine(baseMediaPath, GetMediaPath(), subfolder);
	PathCombine(path, baseMediaPath, file);
	if (_tcsicmp(localMediaPath, baseMediaPath) != 0)
	{
		LogFile::Get()->Write(LogFile::MediaFileLogging, _T("Searching for %s in %s.*\n"), file, path);
		if (FindFileUsingExtensions(path, exts, numExts))
			return true;
	}

	// Still not found.  Try the <PinballY>\Assets\<subfolder>, in case this 
	// item has a built-in default media file.  This is the option of last
	// resort, since it's the built-in program option.
	GetDeployedFilePath(path, _T("Assets"), _T(""));
	PathAppend(path, subfolder);
	PathAppend(path, file);
	LogFile::Get()->Write(LogFile::MediaFileLogging, _T("Searching for %s in %s.*\n"), file, path);
	if (FindFileUsingExtensions(path, exts, numExts))
		return true;

	// no files found
	return false;
}



// -----------------------------------------------------------------------
// 
// Game list item.  This represents the entry for a single game.
//

std::list<const MediaType*> GameListItem::allMediaTypes;
std::unordered_map<WSTRING, const MediaType*> GameListItem::jsMediaTypes;
LONG GameListItem::nextInternalID = 1;
const GameListItem::SpecialListItem GameListItem::isSpecialListItem;

void GameListItem::InitMediaTypeList()
{
	// add all of the types to the master type list
	allMediaTypes.clear();
	allMediaTypes.push_back(&playfieldImageType);
	allMediaTypes.push_back(&playfieldVideoType);
	allMediaTypes.push_back(&playfieldAudioType);
	allMediaTypes.push_back(&backglassImageType);
	allMediaTypes.push_back(&backglassVideoType);
	allMediaTypes.push_back(&dmdImageType);
	allMediaTypes.push_back(&dmdVideoType);
	allMediaTypes.push_back(&topperImageType);
	allMediaTypes.push_back(&topperVideoType);
	allMediaTypes.push_back(&wheelImageType);
	allMediaTypes.push_back(&instructionCardImageType);
	allMediaTypes.push_back(&flyerImageType);
	allMediaTypes.push_back(&launchAudioType);
	allMediaTypes.push_back(&realDMDImageType);
	allMediaTypes.push_back(&realDMDColorImageType);
	allMediaTypes.push_back(&realDMDVideoType);
	allMediaTypes.push_back(&realDMDColorVideoType);

	// build the Javascript type map
	for (auto mt : allMediaTypes)
		jsMediaTypes.emplace(mt->javascriptId, mt);
}

GameListItem::GameListItem(
	const TCHAR *mediaName,
	const char *title, 
	const char *filename,
	const GameManufacturer *manufacturer, 
	int year, 
	const TCHAR *ipdbId,
	const char *tableType,
	const char *rom,
	GameSystem *system,
	bool enabled,
	const char *gridPos)
{
	// do the basic initialization
	CommonInit();

	// store the basic attributes
	this->mediaName = mediaName;
	this->title = AnsiToTSTRING(title);
	this->filename = AnsiToTSTRING(filename);
	this->manufacturer = manufacturer;
	this->year = year;
	this->ipdbId = ipdbId;
	if (tableType != nullptr)
		this->tableType = AnsiToTSTRING(tableType);
	if (rom != nullptr)
		this->rom = AnsiToTSTRING(rom);
	this->system = system;
	this->recentSystemIndex = -1;

	// set the Hidden flag if the XML entry is disabled
	this->hidden = !enabled;

	// parse the grid position if present
	if (gridPos != nullptr)
	{
		std::regex gridPat("\\s*(\\d+)x(\\d+)\\s*", std::regex_constants::icase);
		std::match_results<const char*> m;
		if (std::regex_match(gridPos, m, gridPat))
		{
			this->gridPos.row = atoi(m[1].str().c_str());
			this->gridPos.col = atoi(m[2].str().c_str());
		}
	}

	// this game is configured
	this->isConfigured = true;

	// assign an internal ID
	AssignInternalID();
}

GameListItem::GameListItem(const TCHAR *filename, TableFileSet *tableFileSet)
{
	// do the common initialization
	CommonInit();

	// Remember the table file set that the file came from.  This lets
	// us infer which system(s) the game belongs to.  It's possible for
	// the implied system to be ambiguous, since multiple systems can
	// share the same table file set, so we can't just assume a system
	// here.  Keep our options open for now by simply recording the
	// file set, so that we can look at which systems are associated
	// with it when we actually need to know (e.g., when the user tries
	// to run this file).
	this->tableFileSet = tableFileSet;

	// remember the filename
	this->filename = filename;

	// set the default unconfigured title
	SetTitleFromFilename();

	// unconfigured games don't have manufacturer or system settings
	this->manufacturer = nullptr;
	this->system = nullptr;

	// assign the internal ID
	AssignInternalID();
}

void GameListItem::SetTitleFromFilename()
{
	// strip the default extension from the filename for use as the
	// media name and title
	size_t lenSansExt = filename.length();
	if (tableFileSet != nullptr && tstriEndsWith(filename.c_str(), tableFileSet->defExt.c_str()))
		lenSansExt -= tableFileSet->defExt.length();

	this->mediaName.assign(filename.c_str(), lenSansExt);
	this->title.assign(filename.c_str(), lenSansExt);
}

void GameListItem::CommonInit()
{
	// clear fields
	dbFile = nullptr;
	gameXmlNode = nullptr;
	pbxRating = 0;
	highScoreStatus = Init;
	tableFileSet = nullptr;
	hidden = false;
	isConfigured = false;

	// we haven't attempted to look up the stats db row yet - use
	// the magic number -2 to mean "unknown row number"
	statsDbRow = -2;

}

void GameListItem::AssignInternalID()
{
	// try assigning a reload ID
	if (auto gl = GameList::Get(); gl != nullptr)
	{
		if (auto id = gl->GetReloadID(this); id != 0)
		{
			internalID = id;
			return;
		}
	}

	// assign the next available internal ID
	internalID = InterlockedIncrement(&nextInternalID);
}

GameListItem::~GameListItem()
{
}

TSTRING GameListItem::GetGameId() const
{
	// get the system name suffix
	const TCHAR *sys = (system != nullptr ? system->displayName.c_str() : _T("Unconfigured"));

	// use the title, manufacturer, and year, to the extent they're known
	TSTRINGEx result;
	if (manufacturer != nullptr && year != 0)
		result.Format(_T("%s (%s %d).%s"), title.c_str(), manufacturer->manufacturer.c_str(), year, sys);
	else if (manufacturer != nullptr)
		result.Format(_T("%s (%s).%s"), title.c_str(), manufacturer->manufacturer.c_str(), sys);
	else if (year != 0)
		result.Format(_T("%s (%d).%s"), title.c_str(), year, sys);
	else
		result.Format(_T("%s.%s"), title.c_str(), sys);

	// return the result
	return result;
}

TSTRING GameListItem::GetOldGameId() const
{
	return title + _T(".") + (system != nullptr ? system->displayName : _T("Unconfigured"));
}

TSTRING *GameListItem::GetTablePath() const
{
	return tableFileSet != nullptr ? &tableFileSet->tablePath : nullptr;
}

TSTRING GameListItem::CleanMediaName(const TCHAR *src)
{
	// process each character of the source string
	static const TCHAR *inv = _T("<>:/|?*\"\\");
	static const TCHAR *rep = _T("()x;;x+';");
	TSTRING result;
	for (; *src != 0; ++src)
	{
		// get this character
		auto c = *src;

		// check for forbidden characters
		if (auto p = _tcschr(inv, c); p != nullptr)
		{
			// This character is invalid.  Get the replacement.
			c = rep[p - inv];

			// 'x' has the special meaning "no replacement"
			if (c == 'x')
				continue;
		}

		// add this character to the result
		result.append(&c, 1);
	}

	// return the result
	return result;
}

// Define the standard extension lists for the main media type.
// Note that these are explicitly #define's rather than something
// like 'static const TCHAR*', because the #define arrangement lets
// us use C++'s string literal concatenation feature to add more
// extensions to these lists in individual invocations.  E.g., if
// we wanted to add ".tiff" as a possible type for a specific image
// type, we'd write 'ImageExtensions _T(" .tiff")'.
//
// Extensions in the list must include the '.' character.  Use one
// space to delimit adjacent items.
//
// The order of the extensions determines the media search order.
// In addition, the first extension for each type determines the
// default capture format for that type.
//
#define ImageExtensions  _T(".png .jpg .jpeg")
#define VideoExtensions  _T(".f4v .mp4 .mpg .mkv .wmv .m4v .avi")
#define AudioExtensions  _T(".mp3 .wav")

// Flyer images are arranged into subfolders by page.  (Note that these
// are directory names used in the HyperPin/PinballX media database
// structure, so they're intentionally not localized.)
static const TCHAR *flyerPages[] = {
	_T("Front"),
	_T("Inside1"),
	_T("Inside2"),
	_T("Inside3"),
	_T("Inside4"),
	_T("Inside5"),
	_T("Inside6"),
	_T("Back"),
	nullptr
};

// Define the media types.  Note that the media type subdirectory names 
// are explicitly not localized, since they're internal names defined by
// the HyperPin/PinballX media database structure.
const MediaType GameListItem::wheelImageType = {
	100, _T("Wheel Images"), true, _T(".png"), IDS_MEDIATYPE_WHEELPIC, _T("WheelImage"), L"wheel image",
	nullptr, nullptr, nullptr,
	MediaType::Image, 0 };
const MediaType GameListItem::instructionCardImageType = {
	200, _T("Instruction Cards"), false, ImageExtensions _T(" .swf"), IDS_MEDIATYPE_INSTR, _T("InstCardImage"), L"inst card image",
	nullptr, nullptr, nullptr,
	MediaType::Image, 0, true };
const MediaType GameListItem::flyerImageType = {
	300, _T("Flyer Images"), false, ImageExtensions, IDS_MEDIATYPE_FLYERPIC, _T("FlyerImage"), L"flyer image",
	nullptr, nullptr, nullptr,
	MediaType::Image, 0, false, flyerPages };
const MediaType GameListItem::launchAudioType = {
	400, _T("Launch Audio"), true, AudioExtensions, IDS_MEDIATYPE_LAUNCHAUDIO, _T("LaunchAudio"), L"launch audio",
	nullptr, nullptr, nullptr,
	MediaType::Audio, 0 };
const MediaType GameListItem::playfieldImageType = {
	400, _T("Table Images"), true, ImageExtensions, IDS_MEDIATYPE_PFPIC, _T("PlayfieldImage"), L"table image",
	ConfigVars::CapturePFImageStart, nullptr, nullptr,
	MediaType::Image, 270 };
const MediaType GameListItem::playfieldVideoType = {
	401, _T("Table Videos"), true, VideoExtensions, IDS_MEDIATYPE_PFVID, _T("PlayfieldVideo"), L"table video",
	ConfigVars::CapturePFVideoStart, ConfigVars::CapturePFVideoStop, ConfigVars::CapturePFVideoTime,
	MediaType::VideoWithAudio, 270 };
const MediaType GameListItem::playfieldAudioType = {
	410, _T("Table Audio"), true, AudioExtensions, IDS_MEDIATYPE_PFAUDIO, _T("PlayfieldAudio"), L"table audio",
	ConfigVars::CapturePFAudioStart, ConfigVars::CapturePFAudioStop, ConfigVars::CapturePFAudioTime,
	MediaType::Audio, 270 };
const MediaType GameListItem::backglassImageType = {
	500, _T("Backglass Images"), true, ImageExtensions, IDS_MEDIATYPE_BGPIC, _T("BackglassImage"), L"bg image",
	ConfigVars::CaptureBGImageStart, nullptr, nullptr, 
	MediaType::Image, 0 };
const MediaType GameListItem::backglassVideoType = {
	501, _T("Backglass Videos"), true, VideoExtensions, IDS_MEDIATYPE_BGVID, _T("BackglassVideo"), L"bg video",
	ConfigVars::CaptureBGVideoStart, ConfigVars::CaptureBGVideoStop, ConfigVars::CaptureBGVideoTime,
	MediaType::SilentVideo, 0 };
const MediaType GameListItem::dmdImageType = {
	600, _T("DMD Images"), true, ImageExtensions, IDS_MEDIATYPE_DMPIC, _T("DMDImage"), L"dmd image",
	ConfigVars::CaptureDMImageStart, nullptr, nullptr,
	MediaType::Image, 0 };
const MediaType GameListItem::dmdVideoType = {
	601, _T("DMD Videos"), true, VideoExtensions, IDS_MEDIATYPE_DMVID, _T("DMDVideo"), L"dmd video",
	ConfigVars::CaptureDMVideoStart, ConfigVars::CaptureDMVideoStop, ConfigVars::CaptureDMVideoTime,
	MediaType::SilentVideo, 0 };
const MediaType GameListItem::topperImageType = {
	700, _T("Topper Images"), true, ImageExtensions, IDS_MEDIATYPE_TPPIC, _T("TopperImage"), L"topper image",
	ConfigVars::CaptureTPImageStart, nullptr, nullptr,
	MediaType::Image, 0 };
const MediaType GameListItem::topperVideoType = {
	701, _T("Topper Videos"), true, VideoExtensions, IDS_MEDIATYPE_TPVID, _T("TopperVideo"), L"topper video",
	ConfigVars::CaptureTPVideoStart, ConfigVars::CaptureTPVideoStop, ConfigVars::CaptureTPVideoTime,
	MediaType::SilentVideo, 0 };
const MediaType GameListItem::realDMDImageType = {
	800, _T("Real DMD Images"), true, ImageExtensions, IDS_MEDIATYPE_REALDMDPIC, _T("RealDMDImage"), L"real dmd image",
	nullptr, nullptr, nullptr,
	MediaType::Image, 0 };
const MediaType GameListItem::realDMDColorImageType = {
	801, _T("Real DMD Color Images"), true, ImageExtensions, IDS_MEDIATYPE_REALDMDCLRPIC, _T("RealDMDColorImage"), L"real dmd color image",
	nullptr, nullptr, nullptr,
	MediaType::Image, 0 };
const MediaType GameListItem::realDMDVideoType = {
	810, _T("Real DMD Videos"), true, VideoExtensions, IDS_MEDIATYPE_REALDMDVID, _T("RealDMDVideo"), L"real dmd video",
	nullptr, nullptr, nullptr,
	MediaType::Image, 0 };
const MediaType GameListItem::realDMDColorVideoType = {
	811, _T("Real DMD Color Videos"), true, VideoExtensions, IDS_MEDIATYPE_REALDMDCLRVID, _T("RealDMDColorVideo"), L"real dmd color video",
	nullptr, nullptr, nullptr,
	MediaType::Image, 0 };


bool GameListItem::MediaExists(const MediaType &mediaType) const
{
	TSTRING filename;
	return GetMediaItem(filename, mediaType, false);
}

TSTRING GameListItem::GetDisplayName() const 
{
	// Figure the new name, base on the the PinballX convention:
	// "Title (Manufacturer Year)".  If the manufacturer or year
	// is missing, simply omit that element.  If we don't even
	// have a title, use the filename minus the default extension.
	TSTRINGEx s;
	if (title.length() == 0)
	{
		// figure the length, minus the default extension if present
		size_t len = filename.length();
		if (tableFileSet != nullptr && tstriEndsWith(filename.c_str(), tableFileSet->defExt.c_str()))
			len -= tableFileSet->defExt.length();

		// use the filename minus extension
		s.assign(filename, len);
	}
	else if (manufacturer != nullptr && year != 0)
		s.Format(_T("%s (%s %d)"), title.c_str(), manufacturer->manufacturer.c_str(), year);
	else if (manufacturer != nullptr)
		s.Format(_T("%s (%s)"), title.c_str(), manufacturer->manufacturer.c_str());
	else if (year != 0)
		s.Format(_T("%s (%d)"), title.c_str(), year);
	else
		s = title;

	// return the result
	return s;
}

void GameListItem::ResolveFile(ResolvedFile &rf)
{
	// The treatment depends on where the file entry came from
	TCHAR fullPath[MAX_PATH];
	if (system != nullptr)
	{
		// We have a system, so this game came from an XML database entry.
		// The filename is the "name" attribute from the <game> node in 
		// the XML record, which might or might not include the path and 
		// extension.  So first, check if it has an absolute path.
		if (PathIsRelative(filename.c_str()))
		{
			// Relative path.  The filename is relative to the system
			// table file folder.
			PathCombine(fullPath, system->tablePath.c_str(), filename.c_str());
		}
		else
		{
			// Absolute path.  Use the exact path given.
			_tcscpy_s(fullPath, filename.c_str());
		}

		// The <game name="xxx"> attribute might or might not include an
		// extension.  If the filename as currently constituted exists, 
		// take this as the exact file; otherwise, if adding the system's
		// default extension gives us an extant file, add the extension.
		if (!FileExists(fullPath) && system != nullptr && system->defExt.length() != 0)
		{
			// doesn't exist - try with the default extension
			TSTRING plusExt = fullPath + system->defExt;
			if (FileExists(plusExt.c_str()))
				_tcscpy_s(fullPath, plusExt.c_str());
		}

		// save the full path
		rf.path = fullPath;

		// separate the path and filename
		LPTSTR n = PathFindFileName(fullPath);
		if (n != nullptr)
		{
			rf.file = n;
			rf.folder.assign(fullPath, n - fullPath - 1);
		}
		else
		{
			rf.file = fullPath;
		}
	}
	else if (tableFileSet != nullptr)
	{
		// This is an unconfigured game: it came from a table file set scan.
		// The folder is the table file set folder, and the filename definitely
		// includes its extension.
		rf.folder = tableFileSet->tablePath;
		rf.file = filename;
		PathCombine(fullPath, rf.folder.c_str(), rf.file.c_str());
		rf.path = fullPath;
	}
	else
	{
		// We have neither a system nor table file set.  This should be
		// impossible, as game list entries should always come from one of
		// those sources.
		assert(FALSE);
	}

	// determine if the file exists
	rf.exists = FileExists(rf.path.c_str());
}

TSTRING GameListItem::GetDefaultMediaName() const
{
	// By convention, we use the display name ("Title (Manufacturer Year)")
	// as the root media name, so get the display name.
	TSTRING newMediaName = GetDisplayName();

	// clean up the name to remove invalid filename characters
	return CleanMediaName(newMediaName.c_str());
}

bool GameListItem::UpdateMediaName(std::list<std::pair<TSTRING, TSTRING>> *mediaRenameList, const TCHAR *newMediaName)
{
	// if the caller didn't provide a new name, use the default
	TSTRING defaultName;
	if (newMediaName == nullptr || newMediaName[0] == 0)
	{
		defaultName = GetDefaultMediaName();
		newMediaName = defaultName.c_str();
	}

	// if the name has changed (ignoring case), apply the change
	if (_tcsicmp(mediaName.c_str(), newMediaName) != 0)
	{
		// find existing media items if desired
		if (mediaRenameList != nullptr)
		{
			auto AddItem = [this, mediaRenameList, &newMediaName](const TSTRING &oldPath)
			{
				// make a copy of the path
				TCHAR newPath[MAX_PATH];
				_tcscpy_s(newPath, oldPath.c_str());

				// The file spec for a media item always starts with the base
				// media name, and has a varying suffix following that.  The
				// suffix is usually just a type extension (e.g., ".jpg"), but
				// not always - sometimes there's also a numeric suffix, such
				// as for the instructions cards ("<media> 1.flv", etc).  We
				// can handle all of this uniformly here by just assuming
				// there's an opaque string suffix following the media base
				// name.  So as long as the old file spec starts with the old
				// media base name, pull out whatever's left after that and
				// append it to the new base name to get the full new name.
				TCHAR *oldName = PathFindFileName(newPath);
				if (oldName != nullptr && tstriStartsWith(oldName, mediaName.c_str()))
				{
					// pull out the original suffix
					TSTRING suffix(oldName + mediaName.length());

					// remove the old name from the new path by null-terminating
					// the string at the start of the file spec
					*oldName = 0;

					// form the new filename by combining the new media base
					// name with the suffix from the existing file
					TSTRING newName = newMediaName + suffix;

					// append the new name to the new path
					PathAppend(newPath, newName.c_str());

					// add this <old, new> pair to the result list
					mediaRenameList->emplace_back(oldPath, newPath);
				}
				else
				{
					// This really shouldn't be possible
					assert(false);
				}
			};

			auto AddItems = [this, mediaRenameList, &newMediaName, &AddItem](
				const MediaType &mediaType)
			{
				// get the list of existing files for this media type
				std::list<TSTRING> filenames;
				GetMediaItems(filenames, mediaType);

				// add each item to the result list with its old and new name
				for (auto &f : filenames)
					AddItem(f);
			};

			// add all matching media items to the rename list
			AddItems(playfieldImageType);
			AddItems(playfieldVideoType);
			AddItems(playfieldAudioType);
			AddItems(backglassImageType);
			AddItems(backglassVideoType);
			AddItems(dmdImageType);
			AddItems(dmdVideoType);
			AddItems(topperImageType);
			AddItems(topperVideoType);
			AddItems(wheelImageType);
			AddItems(launchAudioType);
			AddItems(instructionCardImageType);
			AddItems(flyerImageType);
		}

		// store the new name
		mediaName = newMediaName;

		// tell the caller that a name change occurred
		return true;
	}
	else
	{
		// no change
		return false;
	}
}

bool GameListItem::GetMediaItem(TSTRING &filename,
	const MediaType &mediaType, bool forCapture, bool enableSwf) const
{
	// if we're getting a name for capture purposes, the file
	// doesn't have to exist; otherwise, we're looking for an
	// extant file
	DWORD flags = 0;
	if (!forCapture)
		flags |= GMI_EXISTS;
	if (!enableSwf)
		flags |= GMI_NO_SWF;

	// get the list of media items
	std::list<TSTRING> lst;
	if (!GetMediaItems(lst, mediaType, flags) || lst.size() == 0)
		return false;

	// log the lookup
	if (LogFile::Get()->IsFeatureEnabled(LogFile::MediaFileLogging))
	{
		TCHAR dir[MAX_PATH];
		mediaType.GetMediaPath(dir, system != nullptr ? system->mediaDir.c_str() : nullptr);
		LogFile::Get()->Group();
		LogFile::Get()->Write(_T("Media file lookup for %s%s%s: %s, path %s, found %s\n"),
			title.c_str(), 
			forCapture ? _T(", for capture") : _T(""),
			enableSwf ? _T("") : _T(", ignore .swf"),
			LoadStringT(mediaType.nameStrId).c_str(),
			dir, lst.size() == 0 ? _T("no matches") : lst.front().c_str());
	}

	// If we're not in capture mode, return the newest file in the list
	// (newest in the sense of modification timestamp).  In cases where
	// there are multiple matches with different formats (e.g., PNG and 
	// JPG images), this will pick out the one most recently installed.
	if (!forCapture && lst.size() > 1)
	{
		FILETIME newestTime = { 0, 0 };
		const TSTRING *newest = nullptr;
		for (auto const &f : lst)
		{
			// get the file's attributes
			WIN32_FILE_ATTRIBUTE_DATA attrs;
			if (GetFileAttributesEx(f.c_str(), GetFileExInfoStandard, &attrs))
			{
				// if this is the last modified so far, remember it
				if (newest == nullptr || CompareFileTime(&attrs.ftLastWriteTime, &newestTime) > 0)
				{
					newest = &f;
					newestTime = attrs.ftLastWriteTime;
				}
			}
		}

		// return the newest file
		if (newest != nullptr)
		{
			filename = *newest;
			return true;
		}
	}


	// return the first item in the list
	filename = lst.front();
	return true;
}


bool GameListItem::GetMediaItems(std::list<TSTRING> &filenames,
	const MediaType &mediaType, DWORD flags) const
{
	// get the media folder
	TCHAR dir[MAX_PATH];
	if (!mediaType.GetMediaPath(dir, system != nullptr ? system->mediaDir.c_str() : nullptr))
		return false;

	// If this is an indexed media type, search for an arbitrary
	// maximum number of index values.  For non-indexed types, we
	// only need to make one index pass.
	int maxMediaIndex = mediaType.indexed ? 32 : 0;

	// iterate over image index values
	for (int mediaIndex = 0; mediaIndex <= maxMediaIndex; ++mediaIndex)
	{
		// iterate over the page subfolders
		for (int pageno = 0; ; ++pageno)
		{
			// build the base filename sans extension
			TCHAR relName[MAX_PATH];
			if (mediaType.pageList != nullptr)
			{
				// This is a paged item type.  Stop if we've exhausted
				// the page list.
				if (mediaType.pageList[pageno] == nullptr)
					break;

				// Combine the media folder name and page subfolder, then
				// add the media base name
				PathCombine(relName, mediaType.pageList[pageno], mediaName.c_str());
			}
			else
			{
				// This isn't a paged type, so we only need to consider 
				// a single item.  Stop the page iteration after that.
				if (pageno > 0)
					break;

				// Combine the media folder name and media file base name
				_tcscpy_s(relName, mediaName.c_str());
			}

			// get the index of the end of the string
			size_t endIndex = _tcslen(relName);

			// If the index is non-zero, add it as well.  The zeroeth
			// item just uses the base name with no index, so we don't
			// add anything for media index 0.
			if (mediaIndex != 0)
			{
				_stprintf_s(&relName[endIndex], countof(relName) - endIndex, _T(" %d"), mediaIndex);
				endIndex += _tcslen(&relName[endIndex]);
			}

			// check each extension
			for (const TCHAR *ext = mediaType.exts; *ext != 0; )
			{
				// Append the current extension to the end of the full filename
				// string.  Note that the 'extensions' string can list more than
				// one extension by separating items with spaces, so only append
				// up to (and not including) the next space.
				size_t index = endIndex;
				const TCHAR *curExt = relName + index;
				for (; *ext != 0 && *ext != ' '; ++ext)
				{
					// add this character if there's room
					if (index + 1 < MAX_PATH)
						relName[index++] = *ext;
				}

				// null-terminate the path name
				relName[index] = 0;

				// build the full name (with the media folder path)
				TCHAR fullName[MAX_PATH];
				PathCombine(fullName, dir, relName);

				// assume we'll include this file, but we'll have to run some checks
				// before knowing for sure
				bool include = true;

				// if the GMI_EXISTS flag is set, only include the file if it exists
				if ((flags & GMI_EXISTS) != 0 && !FileExists(fullName))
					include = false;

				// If GMI_NO_SWF is set, skip it if it's an SWF file.  Note that there's
				// a crazy special case thanks to HyperPin history: HyperPin required
				// instruction card files to be named with an .swf extension, but it 
				// nonetheless accepted PNG and JPG files, so long as they had the .swf
				// suffix.  That is, the *contents* could actually be PNG or JPG format
				// as long as the *name* looked like *.swf.  So: if the filename ends in
				// .swf, and the file exists, check its contents to determine the actual
				// format.  If the name ends in .swf and the file doesn't exist, treat
				// it as SWF and exclude it.
				if ((flags & GMI_NO_SWF) != 0 && _tcsicmp(curExt, _T(".swf")) == 0)
				{
					// it ends with .swf, so assume it's SWF based on the name...
					bool swf = true;

					// ...but if the file exists, check the contents to be sure
					if (FileExists(fullName))
					{
						ImageFileDesc desc;
						if (GetImageFileInfo(fullName, desc) && desc.imageType != ImageFileDesc::SWF)
							swf = false;
					}

					// if it's an SWF file after all, exclude it
					if (swf)
						include = false;
				}

				// if we decided to include the file, add it to the list
				if (include)
					filenames.emplace_back((flags & GMI_REL_PATH) != 0 ? relName : fullName);

				// if we're at a space separator in the extension string, skip it
				if (*ext == ' ')
					++ext;
			}
		}
	}

	// return true if we found any items
	return filenames.size() != 0;
}

TSTRING GameListItem::GetDropDestFile(const TCHAR *droppedFile, const MediaType &t) const
{
	// Split the dropped filename into path, base filename, and
	// extension elements
	std::basic_regex<TCHAR> compPat(_T("(?:(.*)\\\\)?([^\\\\]+)(\\.[^\\\\.]+)$"));
	std::match_results<const TCHAR*> m;
	TSTRING path, baseName, ext;
	if (std::regex_match(droppedFile, m, compPat))
	{
		path = m[1].str();
		baseName = m[2].str();
		ext = m[3].str();
	}
	else
	{
		baseName = droppedFile;
	}

	// If this is an indexed type, get the index in the name of
	// the dropped file, if any.
	int index = 0;
	if (t.indexed)
	{
		std::basic_regex<TCHAR> indexPat(_T(".*\\s(\\d+)$"));
		if (std::regex_match(baseName.c_str(), m, indexPat))
			index = _ttoi(m[1].str().c_str());
	}

	// If this type breaks up pages into subfolders, get the 
	// folder from the last path element
	TSTRING pageDir;
	if (t.pageList != nullptr)
	{
		// pull out the last element
		std::basic_regex<TCHAR> lastElePat(_T("(?:.*\\\\)?([^\\\\]+)$"));
		if (std::regex_match(path.c_str(), m, lastElePat))
		{
			// Validate that it's in the list
			for (size_t i = 0; t.pageList[i] != nullptr; ++i)
			{
				// check for a case-insensitive match
				if (_tcsicmp(t.pageList[i], m[1].str().c_str()) == 0)
				{
					// it's a match - use the original form from the
					// list to make sure we have the original case
					pageDir = t.pageList[i];
					break;
				}
			}
		}
	}

	// Assemble the name: start with the base media folder
	TCHAR buf[MAX_PATH];
	_tcscpy_s(buf, GameList::Get()->GetMediaPath());

	// add the system-specific folder if appropriate
	if (t.perSystem && system != nullptr)
		PathAppend(buf, system->databaseDir.c_str());

	// add the type folder
	PathAppend(buf, t.subdir);

	// add the page folder
	if (pageDir.length() != 0)
		PathAppend(buf, pageDir.c_str());

	// add the game's base media name
	PathAppend(buf, mediaName.c_str());

	// Add the index, if it's non-zero.  Note that we don't add
	// anything if the index is zero, even for an indexed type,
	// since the zeroeth element of an indexed type just uses
	// the base name.
	if (index != 0)
	{
		TCHAR indexBuf[10];
		_stprintf_s(indexBuf, _T(" %d"), index);
		_tcscat_s(buf, indexBuf);
	}

	// Add the extension
	_tcscat_s(buf, ext.c_str());

	// and that's it!
	return buf;
}

// Set the Hidden flag.  The Hidden status is kept in two places
// in the external databases: it's stored in the XML database entry
// via the <enabled> property, and it's stored in the game's stats 
// DB row in the Hidden column.  The redundant representation is
// unfortunate, but it lets us meet two needs that are somewhat in
// conflict for this particular datum:  PinballX compatibility, and
// support for unconfigured table files (those without XML entries).
// The stats DB entry is specifically to give us a place to stick
// metadata for files that don't appear in the PinballX databases.
// We *could* have just used that exclusively and avoided this dual
// storage mess, but PinballX already has the equivalent concept of 
// <enabled> in its databases, so the only way to maintain two-way
// compatibility is to maintain <enabled> in sync with our own stats
// DB entry.
void GameListItem::SetHidden(bool f, bool updateDatabases)
{
	// set the internal flag
	hidden = f;

	// update the database entries if desired
	if (updateDatabases)
	{
		// update the stats DB Hidden column
		GameList::Get()->SetHidden(this, f);

		// If we have an XML entry, update its <enabled> status such
		// that enabled == !hidden.  There's a special case here: the
		// default for <enabled> is true, so if the new hidden status
		// is false (!f), and there's no <enabled> node in the XML,
		// we don't have to add one - a missing node means the same
		// thing.  This is just an optimization to skip a file update
		// when possible.  And if there's no XML node at all, we don't
		// need to add one - this means that the game is unconfigured
		// and thus (by definition) has no XML database entry.
		if (gameXmlNode != nullptr && dbFile != nullptr)
		{
			// find the <enabled> node
			if (auto enabledNode = gameXmlNode->first_node("enabled"); enabledNode != nullptr)
			{
				// update it to the new enabled status to !hidden
				enabledNode->value(f ? "False" : "True");
				dbFile->isDirty = true;
			}
			else
			{
				// There's no existing <enabled> node.  If the new status
				// is Not Hidden, <enabled> is TRUE, which is the default
				// implied by a missing node, so we can just leave well
				// enough alone.  Otherwise, we have to add the node.
				if (f)
				{
					// add the node
					enabledNode = dbFile->doc.allocate_node(rapidxml::node_element, "enabled", "False");
					gameXmlNode->append_node(enabledNode);
					dbFile->isDirty = true;
				}
			}
		}
	}
}

void GameListItem::EnumHighScoreGroups(std::function<void(const std::list<const TSTRING*> &)> func)
{
	std::list<const TSTRING*> group;
	for (auto it = highScores.begin(); ; ++it)
	{
		// if we're at a blank line or we've reached the end, process
		// the current group
		if (it == highScores.end() || it->length() == 0)
		{
			// if the current group isn't empty, send it to the callback
			if (group.size() != 0)
				func(group);

			// start the next group
			group.clear();

			// if this is the end of the list, we're done
			if (it == highScores.end())
				break;
		}
		else
		{
			// add this item to the current group
			group.emplace_back(&*it);
		}
	}
}

void GameListItem::DispHighScoreGroups(std::function<void(const std::list<const TSTRING*> &)> func)
{
	EnumHighScoreGroups([&func](const std::list<const TSTRING*> &group)
	{
		// A real DMD usually displays only two lines at a time,
		// but examples exist in nature of three-line screens (e.g.,
		// Medieval Madness's roster of mission champions).  More
		// than that would be illegible on a 32-pixel-high display.
		// Now, we're only simulating a DMD using a modern video
		// display, so we could cheat and pack a lot more into
		// each page by using the full video resolution.  But we
		// display the score screens in alternation with images
		// or videos captured from the DMD during game play,
		// which will normally be at DMD resultion, so we want 
		// to use the same low-res look for our generated score
		// screens.  So limit our screens to three lines.
		//
		// PINEmHi generally breaks things up into groups that
		// match the pages displayed during the actual high score
		// rotation as displayed in the original ROM, but in some
		// cases it will give us larger groups, so we'll have to
		// do our own re-grouping in some cases.  Here's how we
		// do this:
		//
		// - If the current group is three lines or fewer, show
		//   it as given
		//
		// - Otherwise, break it into two-line groups.  If the
		//   group has an odd number of lines, show the FIRST
		//   line as the odd man out.  PINEmHi usually uses the
		//   first line of a group as a "header" of sorts, so
		//   it usually produces a pleasing effect to show
		//   this by itself ahead of the remaining items.
		auto it = group.begin();
		if (group.size() <= 3)
		{
			// three or fewer lines - draw it as-is
			func(group);
		}
		else
		{
			// More than three lines.  Break into two-line groups.
			auto it = group.begin();
			auto Disp = [&it, &func](int n)
			{
				// build the subgroup list
				std::list<const TSTRING*> subgroup;
				for (; n != 0; --n, ++it)
					subgroup.emplace_back(*it);

				// pass it to the callback
				func(subgroup);
			};

			// If we have an odd number of lines, break out the
			// first line as the one going solo.
			if ((group.size() & 1) != 0)
				Disp(1);

			// show the remaining lines in pairs
			while (it != group.end())
				Disp(2);
		}
	});
}

// -----------------------------------------------------------------------
//
// Favorites filter
//

bool FavoritesFilter::Include(GameListItem *game)
{
	return GameList::Get()->IsFavorite(game);
}

// -----------------------------------------------------------------------
//
// Hidden game filter
//

bool HiddenGamesFilter::Include(GameListItem *game) 
{
	return game->IsHidden();
}

// -----------------------------------------------------------------------
//
// Unconfigured games filter
//

bool UnconfiguredGamesFilter::Include(GameListItem *game)
{
	return !game->isConfigured;
}

// -----------------------------------------------------------------------
//
// Rating filter
//

bool RatingFilter::Include(GameListItem *game)
{
	float gameRating = GameList::Get()->GetRating(game);
	float minRating = (float)stars, maxRating = minRating + 1.0f;
	return gameRating >= minRating && gameRating < maxRating;
}

// -----------------------------------------------------------------------
//
// Recency filters
//

void RecencyFilter::BeforeScan()
{
	// cache 12:00AM today, local time, as the reference point for
	// the recency filter
	midnight = GameList::GetLocalMidnightUTC(); 

	// do any base class work
	__super::BeforeScan();
}


// -----------------------------------------------------------------------
//
// Recently played filter
//

bool RecentlyPlayedFilter::Include(GameListItem *game)
{
	// Get the game's last played time, as a DateTime value.
	// Note that this is in UTC.
	DateTime lastPlayed(GameList::Get()->GetLastPlayed(game));

	// If there's not a valid Last Played value for the game, treat it
	// as "never played".  That means that this game can't pass any date
	// inclusion filter, and that it passes every exclusion filter.
	if (!lastPlayed.IsValid())
		return exclude;

	// Figure the starting point of the filter interval, by
	// subtracting the filter's interval in days from the current
	// midnight.  In concrete terms, the DATE value is actually a
	// 'double' representing the number of days since the epoch.
	// The fractional part thus represents the time within the day
	// as a fraction of 24 hours.  So to do a "days ago" calculation
	// with an integral number of days, we simply subtract the number
	// of days from the DATE value.
	DATE dStart = midnight - days;

	// Determine if the Last Played time is within the interval
	bool lastPlayedInInterval = lastPlayed.ToVariantDate() >= dStart;

	// Now determine if it passes the filter: if it's an inclusion
	// filter, it passes if the game was last played in the interval,
	// otherwise it passes if the game wasn't played in the interval.
	// That makes it an XOR truth table.
	return exclude ^ lastPlayedInInterval;
}

// -----------------------------------------------------------------------
//
// Never-played filter
//
bool NeverPlayedFilter::Include(GameListItem *game)
{
	// Get the game's last played time, as a DateTime value.  If there's
	// no valid stored value, the game has never been played, so it
	// passes the filter.
	DateTime lastPlayed(GameList::Get()->GetLastPlayed(game));
	return !lastPlayed.IsValid();
}

// -----------------------------------------------------------------------
//
// Recently added filter
//

bool RecentlyAddedFilter::Include(GameListItem *game)
{
	// if the game isn't configured, it doesn't pass any Added Date test
	if (!game->isConfigured)
		return false;

	// Get the date/time the game was added, as a DateTime value.
	// This is in UTC.
	DateTime added(GameList::Get()->GetDateAdded(game));

	// If there's not a valid Added date, it must have come from a
	// pre-existing PinballX database.  PBX doesn't track added dates,
	// so all we can say is that the game was added before our first
	// run.
	if (!added.IsValid())
		added = Application::Get()->GetFirstRunTime();

	// Figure the starting point of the filter interval, by
	// subtracting the filter's interval in days from the current
	// midnight.  DATE values are in terms of days since an epoch,
	// so date arithmetic in whole days is just a matter of
	// adding/subtracting the number of days.
	DATE dStart = midnight - days;

	// Determine if the game was added during the interval
	bool addedDuringInterval = added.ToVariantDate() >= dStart;

	// Now determine if it passes the filter: if it's an inclusion
	// filter, it passes if the game was added within the interval,
	// otherwise it passes if the game wasn't added within the
	// interval.  That makes it an XOR truth table.
	return exclude ^ addedDuringInterval;
}

// -----------------------------------------------------------------------
//
// Special "No Game" entry.  The game list creates a singleton instance
// of this class, and uses it to satisfy queries that select no games.
// We mostly could have used a null pointer to represent this case instead 
// of the special "no game" singleton, but using a valid object is more
// convenient for a lot of cases (e.g., we don't have to test for a null
// pointer every time we retrieve the title), and is especially useful to
// distinguish the case "no selection" from "not initialized" and the like.
// We need to handle the "no game" condition gracefully in many places in
// the UI, because it's a perfectly valid and ordinary condition.  For
// example, selecting the "5-star games" filter when there are no 5-star
// games will select an empty list into the "wheel".
//

NoGame::NoGame() : 
	GameListItem(isSpecialListItem),
	dummySystem(LoadStringT(IDS_NO_SYSTEM), -1),
	dummyManufacturer(LoadStringT(IDS_NO_MANUFACTURER))
{
	// set the empty game title
	title = LoadStringT(IDS_NO_GAME_TITLE);

	// set our dummy system and manufacturer
	system = &dummySystem;
	manufacturer = &dummyManufacturer;
}

// -----------------------------------------------------------------------
//
// Category filters
//

bool GameCategory::Include(GameListItem *game)
{
	return GameList::Get()->IsInCategory(game, this);
}

bool NoCategory::Include(GameListItem *game)
{
	return GameList::Get()->IsUncategorized(game);
}

// -----------------------------------------------------------------------
//
// Game database file object
//

GameDatabaseFile::GameDatabaseFile() :
	category(nullptr),
	isDirty(false),
	isBackedUp(false)
{
}

GameDatabaseFile::~GameDatabaseFile()
{
}

bool GameDatabaseFile::Load(const TCHAR *filename, ErrorHandler &eh)
{
	// set the filename
	this->filename = filename;

	// read the file
	long len = 0;
	sourceText.reset((char *)ReadFileAsStr(filename, eh, len, ReadFileAsStr_NullTerm));
	if (sourceText == nullptr)
		return false;

	// parse it
	return Parse(eh);
}

bool GameDatabaseFile::Load(const char *txt, ErrorHandler &eh)
{
	// set the filename to indicate a memory source
	this->filename = _T("internal:");

	// allocate space for a private copy of the text
	size_t len = strlen(txt) + 1;
	sourceText.reset(new (std::nothrow) char[len]);
	if (sourceText == nullptr)
	{
		eh.SysError(
			MsgFmt(IDS_ERR_LOADGAMELIST, _T("[Internal]")),
			_T("Out of memory"));
		return false;
	}

	// copy it
	memcpy(sourceText.get(), txt, len * sizeof(txt[0]));

	// parse it
	return Parse(eh);
}

bool GameDatabaseFile::Parse(ErrorHandler &eh)
{
	try
	{
		// Parse the file.  Omit data nodes: this represents the text
		// contents of a node in the 'value' property of the node, which
		// is simpler to work with than storing the text in separate
		// child data nodes.  Data nodes would be useful if our document
		// schema used tags embedded within text (the way HTML does),
		// but the PinballX database schema doesn't do this, so we can 
		// use this simpler tree representation.
		//
		// Note that if we *did* want to use data nodes, we'd have to
		// change our document manipulation operations to update the 
		// data nodes in addition to the value nodes, since the two
		// locations store the text redundantly, and rapidxml doesn't 
		// make any attempt to keep them in sync - that's up to the 
		// caller.  That's the big reason it's so much easier to just 
		// skip the data nodes entirely if you're ever going to make
		// any updates to the parsed tree.
		doc.parse<rapidxml::parse_no_data_nodes>(sourceText.get());
	}
	catch (std::exception &exc)
	{
		eh.SysError(
			MsgFmt(IDS_ERR_LOADGAMELIST, filename),
			MsgFmt(_T("XML parsing error: %hs"), exc.what()));
		return false;
	}

	// success
	return true;
}

// -----------------------------------------------------------------------
//
// Table file sets
//

TableFileSet::TableFileSet(const TCHAR *tablePath, const TCHAR *defExt) :
	tablePath(tablePath), defExt(defExt)
{
	// build our initial file set from a directory scan
	ScanFolder(tablePath, defExt, [this](const TCHAR *filename) 
	{
		GameList::Log(_T("++ found file:  %s\n"), filename);
		AddFile(filename);
	});
}

void TableFileSet::ScanFolder(const TCHAR *path, const TCHAR *ext,
	std::function<void(const TCHAR *filename)> func)
{
	// If the default extension is non-empty, build the list of files
	// in this folder matching *.<defExt>.  An empty extension means
	// that the system is something like Steam that doesn't use the 
	// VP-style model with a player program and separate table files
	// that it can load.  An extension ".*" has the special meaning
	// of selecting all files.
	if (ext != nullptr && ext[0] != 0)
	{
		// note what's going on
		GameList::Log(_T("+ scanning for table files: %s\\*%s\n"), path, ext);
			
		// note if we're dealing with the all-file wildcard
		bool dotStar = (_tcscmp(ext, _T(".*")) == 0);

		// build the list of files in this folder that match *.<defExt>
		for (auto &file : fs::directory_iterator(path))
		{
			// skip directories
			if (file.status().type() == fs::file_type::directory)
				continue;

			// Match this file to the default extension.  It matches
			// if either this is the special ".*" wildcard, or the
			// filename ends with the extension, ignoring case.
			if (dotStar || tstriEndsWith(file.path().c_str(), ext))
				func(WSTRINGToTSTRING(file.path().filename().wstring()).c_str());
		}
	}
	else
	{
		GameList::Log(_T("+ NOT scanning for this system's table files (because its default table file extension is empty)\n"));
	}
}

TSTRING TableFileSet::GetKey(const TCHAR *tablePath, const TCHAR *defExt)
{
	// get the canonical form of the path
	TCHAR buf[MAX_PATH];
	PathCanonicalize(buf, tablePath);

	// append *.<defExt>
	PathAppend(buf, _T("*"));
	_tcscat_s(buf, defExt != nullptr && defExt[0] != 0 ? defExt : _T(".*"));

	// convert it all to lower-case
	_tcslwr_s(buf);

	// return the result
	return buf;
}

TableFileSet::TableFile *TableFileSet::AddFile(const TCHAR *fname)
{
	// get the lower-case version to use as the key
	TSTRING key(fname);
	std::transform(key.begin(), key.end(), key.begin(), ::_totlower);

	// add it to my file list
	auto &it = files.emplace(
		std::piecewise_construct,
		std::forward_as_tuple(key),
		std::forward_as_tuple(fname));

	// return the new list entry
	return &it.first->second;
}

TableFileSet::TableFile *TableFileSet::FindFile(
	const TCHAR *filename, const TCHAR *defExt, bool add)
{
	// Try looking up the filename, in lower-case
	TSTRING key(filename);
	std::transform(key.begin(), key.end(), key.begin(), ::_totlower);
	if (auto it = files.find(key); it != files.end())
		return &it->second;

	// We didn't find the exact filename as given.  Try again with
	// the default extension added.  The PinballX database convention
	// is to list the filename without the extension, but we can't
	// count on that since the file could be hand-edited by a user.
	TSTRING fnameWithExt(filename);
	if (defExt != nullptr && !tstriEndsWith(key.c_str(), defExt))
	{
		// add the default extension to the key and filename
		key += defExt;
		fnameWithExt += defExt;

		// look up the key with the extension added
		if (auto it = files.find(key); it != files.end())
			return &it->second;
	}

	// We still didn't find it.  If the 'add' option is set, create
	// a new entry representing the file, even though no such file
	// currently exists in the file system.
	if (add)
	{
		// Create the entry.  Note that we use the name with the 
		// extension added, if it didn't already have the default 
		// extension, since this is the file we'll find on future
		// scans of the directory for new files.
		return &files.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(key),
			std::forward_as_tuple(fnameWithExt.c_str())).first->second;
	}

	// No match
	return nullptr;
}

// -----------------------------------------------------------------------
//
// Media Type Descriptor
//

bool MediaType::GetMediaPath(TCHAR/*[MAX_PATH]*/ *buf, const TCHAR *systemMediaDir) const
{
	// check if the media for this type are stored per-system or generically
	if (perSystem)
	{
		// if there's no system, we can't get system-specific media
		if (systemMediaDir == nullptr)
			return false;

		// system-specific items are in <media dir>\<System>\<Subdir>\<Game Media Name>.ext
		PathCombine(buf, GameList::Get()->GetMediaPath(), systemMediaDir);
		PathAppend(buf, subdir);
	}
	else
	{
		// generic items are in <media dir>\<Subdir>\<Game Media Name>.ext
		PathCombine(buf, GameList::Get()->GetMediaPath(), subdir);
	}

	// success
	return true;
}

// determine if a filename has an extension matching a media type descriptor
bool MediaType::MatchExt(const TCHAR *filename) const
{
	// if the media type is null, or we don't have an extension list, 
	// there's nothing to match
	if (this == nullptr || exts == nullptr)
		return false;

	// check each extension in the list
	for (const TCHAR *p = exts; *p != 0; )
	{
		// scan for the next extension
		const TCHAR *start = p;
		for (; *p != 0 && *p != ' '; ++p);

		// extract the current extension
		TSTRING curExt(start, p - start);

		// check for a match
		if (tstriEndsWith(filename, curExt.c_str()))
			return true;

		// if we're at a space, skip it
		if (*p == ' ')
			++p;
	}

	// no match found
	return false;
}

bool MediaType::SaveBackup(const TCHAR *filename, TSTRING &newName, ErrorHandler &eh) const
{
	// Separate the name into path and filename
	TSTRING path, base;
	const TCHAR *slash = _tcsrchr(filename, '\\');
	if (slash != nullptr)
	{
		path.assign(filename, slash - filename);
		base = slash + 1;
	}
	else
		return false;

	// Pull out the extension separately
	TSTRING ext;
	const TCHAR *dot = _tcsrchr(base.c_str(), '.');
	if (dot != nullptr)
	{
		ext = dot;
		base.resize(dot - base.c_str());
	}

	// search the folder for previous backups - <base>.old[n].<ext>
	int nMax = 0;
	std::basic_regex<WCHAR> filePat(L"(.*)(\\.old\\[(\\d+)\\])(\\.[^.]+)",	std::regex_constants::icase);
	for (auto &file : fs::directory_iterator(path))
	{
		// get the name and parse it into our sections
		WSTRING fname = file.path().filename();
		std::match_results<const WCHAR*> m;
		if (std::regex_match(fname.c_str(), m, filePat))
		{
			// Retrieve the name and extension.  If it doesn't match the 
			// subject game, we don't have to touch this file.
			WSTRING curBase = m[1].str();
			WSTRING curExt = m[4].str();
			if (_tcsicmp(curBase.c_str(), base.c_str()) != 0
				|| _tcsicmp(curExt.c_str(), ext.c_str()) != 0)
				continue;

			// It's a match.  Get the .old[N] number and remember it if
			// it's the highest so far.
			int n = _wtoi(m[3].str().c_str());
			if (n > nMax)
				nMax = n;
		}
	}

	// Build the new name for the file
	MsgFmt newExt(_T("old[%d]%s"), nMax + 1, ext.c_str());
	newName = MsgFmt(_T("%s\\%s.%s"), path.c_str(), base.c_str(), newExt.operator const wchar_t *());

	// It's possible for the file to vanish between the time 
	// we take the directory listing and the time we try to
	// rename it here, so proceed only if it still exists.
	if (FileExists(filename) && !MoveFile(filename, newName.c_str()))
	{
		// log the error
		WindowsErrorMessage winErr;
		eh.Error(MsgFmt(IDS_ERR_MEDIA_ITEM_RENAME,
			LoadStringT(this->nameStrId).c_str(), filename, newExt.Get(), winErr.Get()));

		// return failure
		return false;
	}

	// success
	return true;
}

