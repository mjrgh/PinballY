// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "RefTableList.h"
#include "Application.h"
#include "DiceCoefficient.h"

RefTableList::RefTableList()
{
}

RefTableList::~RefTableList()
{
	// don't allow destruction until the initializer thread has
	// finished, since it accesses our memory area
	if (hInitThread != NULL)
		WaitForSingleObject(hInitThread, INFINITE);
}

bool RefTableList::GetByIpdbId(const TCHAR *id, std::unique_ptr<Table> &table)
{
	// fail if initialization isn't finished
	if (!IsReady())
		return false;

	// look up the table
	auto it = ipdbIdMap.find(id);
	if (it == ipdbIdMap.end())
		return false;

	// return the result
	table.reset(new Table(this, it->second));
	return true;
}

void RefTableList::GetInitMatches(const TCHAR *leadingSubstr, int n, std::list<Table> &lst)
{
	// if initialization hasn't finished yet, return with nothing added to the list
	if (!IsReady())
		return;

	// find the first item at or after the leading substring
	auto it = std::lower_bound(sortedRows.begin(), sortedRows.end(), leadingSubstr, [this](const int &i, const TCHAR* const &s) {
		return lstrcmpi(sortKeyCol->Get(i), s) < 0;
	});

	// fill in the next N items
	for (; it != sortedRows.end() && n > 0; ++it, --n)
		lst.emplace_back(this, *it);
}


void RefTableList::GetFilenameMatches(const TCHAR *filename, int n, std::list<Table> &lst)
{
	// if initialization hasn't finished yet, return with nothing added to the list
	if (!IsReady())
		return;

	// get the lower-case version of the name
	TSTRING lcName = filename;
	std::transform(lcName.begin(), lcName.end(), lcName.begin(), _totlower);

	// strip any filename extension
	static const std::basic_regex<TCHAR> extPat(_T("\\.[^.]+$"));
	lcName = std::regex_replace(lcName, extPat, _T(""));

	// build the bigram set for the name
	DiceCoefficient::BigramSet<TCHAR> bg;
	DiceCoefficient::BuildBigramSet(bg, lcName.c_str());

	// get the number of rows in the reference list
	size_t nRows = nameBigrams.size();

	// If the target name has any parenthetical suffixes, remove them.
	// It's common for table files to have names that either conform to
	// the PinballX database key format, "Title (Manufacturer Year)",
	// or to include other information in parens at the end, such as
	// the author, a release version number, or a "mod" descriptor
	// (e.g., "(Night Mod)").  These are all specific to the virtual
	// version and won't appear in the reference list, so we'll try
	// each match with and without the parentheticals to see which
	// one gives us a stronger match.  Similarly, many tables have
	// VP version number suffixes of the form "_VP[version]..."; 
	// these also are virtual pinball-specific, so we'll get better
	// match results by removing them.  Yet another common add-on
	// is "FS" (for full-screen).
	TSTRING baseName;
	std::basic_regex<TCHAR> suffixPat(_T("(.+?)(\\s*\\(.*|[\\s_\\-.]vp[89x].*|[\\s_\\-.]fs[\\s_\\-.].*)"));
	std::match_results<const TCHAR*> m;
	if (std::regex_match(lcName.c_str(), m, suffixPat))
		baseName = m[1].str();
	else
		baseName = lcName;

	// get the bigram set for the base name
	DiceCoefficient::BigramSet<TCHAR> bgBase;
	DiceCoefficient::BuildBigramSet(bgBase, baseName.c_str());

	// there's nothing to do if the ref list is empty
	if (nRows == 0)
		return;

	// working search results list
	struct Result
	{
		Result(int idx, float score) : idx(idx), score(score) { }
		int idx;           // CSV row index of the match
		float score;       // match score
	};
	std::vector<Result> searchResults;
	searchResults.reserve(nRows);

	// go through the table list and look for matches
	float highScore = 0.0f;
	for (size_t i = 0; i < nRows; ++i)
	{
		// figure the match strength for this item
		float score = DiceCoefficient::DiceCoefficient(bg, nameBigrams[i]);

		// figure the match strength for the shortened version of the name, and
		// use this score if it's higher than the original
		float score2 = DiceCoefficient::DiceCoefficient(bgBase, nameBigrams[i]);
		score = max(score, score2);

		// try again with the name against the alternate name
		score2 = DiceCoefficient::DiceCoefficient(bg, altNameBigrams[i]);
		score = max(score, score2);

		// try once again the base name against the alt name
		score2 = DiceCoefficient::DiceCoefficient(bgBase, altNameBigrams[i]);
		score = max(score, score2);

		// Try matching the base name to the initials.  This isn't a bigram 
		// match, just a substring match, but we need a score on the 0-1.0
		// scale for comparison purposes.  Score it based on the number of
		// initials.  Don't try to match based on a single initial at all.
		const TCHAR *initials = initialsCol->Get((int)i, _T(""));
		size_t nInitials = _tcslen(initials);
		if (nInitials > 1 && (lcName == initials || baseName == initials))
		{
			float score2 = float(nInitials) * 0.2f;
			score2 = min(1.0f, score2);
			score = max(score, score2);
		}

		// Try the same thing with the initials with a "T" prefix, for "The".
		// We strip out "The" from the reference titles when building the
		// initials string, but the "standard" initials for a very few games
		// include the "T" from "The" in the initials, such as "The Addams
		// Family".
		TSTRING initialsWithT = _T("t");
		initialsWithT += initials;
		if (lcName == initialsWithT || baseName == initialsWithT)
		{
			float score2 = float(nInitials + 1) * 0.2f;
			score2 = min(1.0f, score2);
			score = max(score, score2);
		}

		// add it to the results, using the highest score we came up with
		searchResults.emplace_back((int)i, score);

		// note the highest score so far
		if (score > highScore)
			highScore = score;
	}

	// if we didn't come up with any matches, we're done
	if (searchResults.size() == 0)
		return;

	// sort initially by score (high to low)
	std::sort(searchResults.begin(), searchResults.end(), [](const Result &a, const Result &b) { return a.score > b.score; });

	// Build a subset of the top scoring items, keeping up to N
	// items (the maximum the caller requested), but stopping when
	// we reach an item with a score too far below the highest.
	// This avoids keeping a bunch of garbage matches when we've
	// identified a good match.
	std::vector<Result> finalResults;
	finalResults.reserve(n);
	for (auto &it : searchResults)
	{
		// stop if we've filled out the list, or this item's score 
		// is too far below the top item's score
		if ((int)lst.size() >= n || it.score < highScore - 0.3f)
			break;

		// add this item to the results
		finalResults.emplace_back(it.idx, it.score);
	};

	// Now sort this list so that the best match goes at the top
	// (or the best matches, if there's a tie), and the rest of the
	// list is sorted alphabetically by sort key.
	std::sort(finalResults.begin(), finalResults.end(), [highScore, this](const Result &a, const Result &b)
	{
		// if we can distinguish by score, and one of the items
		// has the highest score we found, put the high-scoring
		// item at the top of the list
		if (a.score != b.score)
		{
			if (a.score == highScore)
				return true;
			if (b.score == highScore)
				return false;
		}

		// otherwise, sort alphabetically by sort key
		return lstrcmp(sortKeyCol->Get(a.idx), sortKeyCol->Get(b.idx)) < 0;
	});

	// now build the result list for the caller
	for (auto &it : finalResults)
		lst.emplace_back(this, it.idx);
}

void RefTableList::GetTitleFragmentMatches(const TCHAR *title, int n, std::list<Table> &lst)
{
	// if initialization hasn't finished yet, return with nothing added to the list
	if (!IsReady())
		return;

	// get the lower-case version of the name
	TSTRING lcName = title;
	std::transform(lcName.begin(), lcName.end(), lcName.begin(), _totlower);

	// build the bigram set for the name
	DiceCoefficient::BigramSet<TCHAR> bg;
	DiceCoefficient::BuildBigramSet(bg, lcName.c_str());

	// get the number of rows in the reference list
	size_t nRows = nameBigrams.size();

	// there's nothing to do if the ref list is empty
	if (nRows == 0)
		return;

	// working search results list
	struct Result
	{
		Result(int idx, float score, bool isLeading) : idx(idx), score(score), isLeading(isLeading) { }
		int idx;           // CSV row index of the match
		float score;       // match score
		bool isLeading;    // is this a leading substring of the name?
	};
	std::vector<Result> searchResults;
	searchResults.reserve(nRows);

	// go through the table list and look for matches
	float highScore = 0.0f;
	for (size_t i = 0; i < nRows; ++i)
	{
		// figure the match strength for this item
		float score = DiceCoefficient::DiceCoefficient(bg, nameBigrams[i]);

		// try again with the name against the alternate name
		float score2 = DiceCoefficient::DiceCoefficient(bg, altNameBigrams[i]);
		score = max(score, score2);

		// Note if this is a leading substring of the name or sort key. 
		// Check the sort key so that we match a fragment like "addams family",
		// where the initial "the" in the regular name has been elided.
		bool isLeading = tstriStartsWith(nameCol->Get(static_cast<int>(i)), title)
			|| tstriStartsWith(sortKeyCol->Get(static_cast<int>(i)), title);

		// add it to the results, using the highest score we came up with
		searchResults.emplace_back((int)i, score, isLeading);

		// note the highest score so far
		if (score > highScore)
			highScore = score;
	}

	// if we didn't come up with any matches, we're done
	if (searchResults.size() == 0)
		return;

	// Sort the list so that leading substring matches are at the top,
	// then by score.
	std::sort(searchResults.begin(), searchResults.end(), [this](const Result &a, const Result &b)
	{
		// sort leading substring matches to the top
		if (a.isLeading != b.isLeading)
			return a.isLeading;

		// then by score (high to low)
		if (a.score != b.score)
			return a.score > b.score;

		// then alphabetically within items with the same score or substring status
		return lstrcmpi(sortKeyCol->Get(a.idx), sortKeyCol->Get(b.idx)) < 0;
	});

	// Build the result list, keep the highest ranking N items.
	for (auto &it : searchResults)
	{
		// stop when we reach the caller's maximum number of items
		if ((int)lst.size() >= n)
			break;

		// If this isn't a leading substring, stop if the score is 
		// too far below the best match, so that we don't keep a bunch
		// of garbage matches once we have a good match.
		if (!it.isLeading && it.score < highScore - 0.3f)
			break;

		// add this item to the results
		lst.emplace_back(this, it.idx);
	}
}

RefTableList::Table::Table(RefTableList *rtl, int row)
{
	listName = rtl->listNameCol->Get(row, _T(""));
	name = rtl->nameCol->Get(row, _T(""));
	manuf = rtl->manufCol->Get(row, _T(""));
	manufOrig = rtl->manufOrigCol->Get(row, _T(""));
	year = rtl->yearCol->GetInt(row, 0);
	players = rtl->playersCol->GetInt(row, 0);
	themes = rtl->themeCol->Get(row, _T(""));
	sortKey = rtl->sortKeyCol->Get(row, _T(""));
	machineType = rtl->typeCol->Get(row, _T(""));
	ipdbId = rtl->ipdbIdCol->Get(row, _T(""));
}

void RefTableList::Init()
{
	// initializer thread
	auto Thread = [](LPVOID lParam) -> DWORD
	{
		// get the 'this' pointer from the parameters
		auto self = static_cast<RefTableList*>(lParam);

		// get the table file path
		TCHAR fname[MAX_PATH];
		GetDeployedFilePath(fname, _T("assets\\ipdbTableList.csv"), _T(""));

		// Load it.  Note that we supply the data in code page 1252
		// single-byte format, so specifically ask for interpretation
		// in CP1252 in case we're on a localized system using a
		// different default ANSI code page.
		Application::AsyncErrorHandler eh;
		self->csvFile.SetFile(fname);
		if (!self->csvFile.Read(eh, 1252))
			return 0;

		// set up our column accessors
		self->nameCol = self->csvFile.DefineColumn(_T("Name"));
		self->altNameCol = self->csvFile.DefineColumn(_T("AltName"));
		self->manufCol = self->csvFile.DefineColumn(_T("ManufacturerShort"));
		self->manufOrigCol = self->csvFile.DefineColumn(_T("Manufacturer"));
		self->yearCol = self->csvFile.DefineColumn(_T("Year"));
		self->playersCol = self->csvFile.DefineColumn(_T("Players"));
		self->typeCol = self->csvFile.DefineColumn(_T("Type"));
		self->themeCol = self->csvFile.DefineColumn(_T("Theme"));
		self->ipdbIdCol = self->csvFile.DefineColumn(_T("IPDBID"));

		// add our synthesized column accessors
		self->sortKeyCol = self->csvFile.DefineColumn(_T("SortKey"));
		self->listNameCol = self->csvFile.DefineColumn(_T("ListName"));
		self->initialsCol = self->csvFile.DefineColumn(_T("Initials"));

		// regex's for building the initials
		static const std::basic_regex<TCHAR> parenPat(_T("\\s*\\(.*\\)\\s*"));
		static const std::basic_regex<TCHAR> punctPat(_T("[^\\w]+"));
		static const std::basic_regex<TCHAR> trimPat(_T("^(the|a|an)?\\s+|\\s+(,\\s+(the|a|an))?$"));
		static const std::basic_regex<TCHAR> initPat(_T("(\\w)\\w+\\s*"));

		// Build the bigram sets and sorting keys
		size_t nRows = self->csvFile.GetNumRows();
		self->nameBigrams.reserve(nRows);
		self->altNameBigrams.reserve(nRows);
		for (size_t i = 0; i < nRows; ++i)
		{
			// get the row number as an integer
			int rownum = static_cast<int>(i);

			// get the name, in lower-case
			TSTRING name = self->nameCol->Get(rownum, _T(""));
			std::transform(name.begin(), name.end(), name.begin(), _totlower);

			// Emplace a new row in the bigram set vector, and build
			// the bigram set for the title into the vector entry.
			self->nameBigrams.emplace_back();
			DiceCoefficient::BuildBigramSet(self->nameBigrams.back(), name.c_str());

			// likewise for the AltName bigrams
			TSTRING altName = self->altNameCol->Get(rownum, _T(""));
			std::transform(altName.begin(), altName.end(), altName.begin(), _totlower);
			self->altNameBigrams.emplace_back();
			DiceCoefficient::BuildBigramSet(self->altNameBigrams.back(), altName.c_str());

			// Synthesize the sorting key
			self->MakeSortKey(rownum);

			// Synthesize the list name
			self->MakeListName(rownum);

			// Synthesize the initials.  Start by stripping out any paren-
			// thetical suffix, then strip out any remaining punctuation
			// entirely (replacing it with spaces), then trim any leading
			// or trailing spaces and articles ("the", "a", "an"), then pull
			// out the first letter of each remaining word.
			TSTRING initName = std::regex_replace(name, parenPat, _T(" "));
			initName = std::regex_replace(initName, punctPat, _T(" "));
			initName = std::regex_replace(initName, trimPat, _T(""));
			initName = std::regex_replace(initName, initPat, _T("$1"));

			// store it
			self->initialsCol->Set(rownum, initName.c_str());

			// if it has an IPDB ID, add it to the IPDB map
			const TCHAR *ipdbId = self->ipdbIdCol->Get(rownum, nullptr);
			if (ipdbId != nullptr && ipdbId[0] != 0)
				self->ipdbIdMap.emplace(ipdbId, rownum);
		}

		// Build the sorted row order vector.  Start by populating it with 
		// all of the row numbers.
		auto &sr = self->sortedRows;
		sr.resize(nRows);
		for (size_t i = 0; i < nRows; ++i)
			sr[i] = static_cast<int>(i);

		// now sort it by the referenced sort key in each row
		std::sort(sr.begin(), sr.end(), [self](const int &a, const int &b) {
			return lstrcmp(self->sortKeyCol->Get(a), self->sortKeyCol->Get(b)) < 0;
		});

		// done (the thread return value isn't used, but we have to return
		// something to conform to the standard thread entrypoint prototype)
		return 0;
	};

	// start the thread
	DWORD tid;
	hInitThread = CreateThread(NULL, 0, Thread, this, 0, &tid);
}

void RefTableList::MakeSortKey(int row)
{
	// get the key elements
	TSTRING name = nameCol->Get(row, _T(""));
	TSTRING manuf = manufCol->Get(row, _T(""));
	int year = yearCol->GetInt(row, 0);

	// convert the name and manufacturer to all lower-case for sorting
	std::transform(name.begin(), name.end(), name.begin(), _totlower);
	std::transform(manuf.begin(), manuf.end(), manuf.begin(), _totlower);

	// remove enclosing quotes
	std::basic_regex<TCHAR> quotePat(_T("^([\"'])(.*)\\1$|^[\x84\x93](.*)\x94$"));
	name = std::regex_replace(name, quotePat, _T("$2$3"));

	// move "The" and "A" prefixes to the end
	std::basic_regex<TCHAR> articlePat(_T("^(the|a|an)\\s+(.*)$"));
	name = std::regex_replace(name, articlePat, _T("$2, $1"));

	// now build the full key
	sortKeyCol->Set(row, MsgFmt(_T("%s.%04d.%s"), name.c_str(), year, manuf.c_str()));
}

void RefTableList::MakeListName(int row)
{
	// get the key elements
	TSTRING name = nameCol->Get(row, _T(""));
	TSTRING manuf = manufCol->Get(row, _T(""));
	int year = yearCol->GetInt(row, 0);

	// If we have both year and manufacturer information, show it
	// as "Title (Manufacturer, Year)".
	//
	// If we have one or the other, show it as "Title (other)".
	//
	// Otherwise, just use the unadorned title.
	//
	if (year != 0 && manuf.length() != 0)
		listNameCol->Set(row, MsgFmt(_T("%s (%s, %d)"), name.c_str(), manuf.c_str(), year));
	else if (year != 0)
		listNameCol->Set(row, MsgFmt(_T("%s (%d)"), name.c_str(), year));
	else if (manuf.length() != 0)
		listNameCol->Set(row, MsgFmt(_T("%s (%s)"), name.c_str(), manuf.c_str()));
	else
		listNameCol->Set(row, name.c_str());
}

