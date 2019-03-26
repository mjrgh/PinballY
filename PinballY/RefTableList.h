// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Reference Table List.  This object maintains a master list of 
// real pinball machines that we read from an external data file.
// We use this for populating pick lists in the game setup dialog,
// to make data entry easier by pre-filling fields with reference
// data from our master list.  We use fuzzy matching to find 
// likely tables from the master list based on the filename of
// the new game we're setting up.
//
// Note that the table is loaded into memory asynchronously from
// the main UI, in a background thread.  (We do this because it's
// quite large, about 6200 tables, and isn't needed until the user
// navigates somewhere in the UI that consumes the data.)  Any
// code that accesses the table data needs to be aware of the
// delayed loading so that it can tolerate a situation where the
// table isn't loaded yet.  Callers shouldn't wait for the load
// to complete; they should instead gracefully degrade and act
// as though the table simply isn't available, as that's always
// a possibility as well (e.g., the user could accidentally 
// delete the file).
//
// Note on manufacturer names:  Our data set has two versions of
// the manufacturer name, with the columns called "Manufacturer" 
// and "ManufacturerShort" in the CSV file.  Internally, we refer
// to these as "manufOrig" and "manuf" respectively.  The first
// column ("Manufacturer" == manufOrig) is the original text from
// the raw IPDB data.  The second ("ManufacturerShort" == manuf)
// is a shortened and normalized version.  The original IPDB name
// is usually the full legal name of the company.  The shortened
// name is the more informal name by which the company is usually
// known to players.  For example, the short name for "Williams 
// Manufacturing Company" is simply "Williams".  We only use the
// short name for metadata and on-screen displays, since it's the
// name that players will know.  We keep the original name for 
// reference purposes.  Many of the major pinball manufacturers
// were around for a long time and went through numerous corporate
// reorganizations (mergers, acquisitions, spinoffs, etc), so a
// given familiar brand name might have several corresponding
// "original" manufacturer names in the database.  For example,
// "Williams" refers to Williams Manufacturing Company; Williams
// Electronic Manufacturing Company; Williams Electronics, Inc.;
// Williams Electronic Games, Incorporated, a subsidiary of WMS
// Ind., Incorporated; and several other corporate entities, all
// related through successive reorganizations.
// 
//

#pragma once
#include "CSVFile.h"
#include "DiceCoefficient.h"

class RefTableList
{
public:
	RefTableList();
	~RefTableList();

	// Initialize.  This starts the initializer thread to load
	// the table in the background.
	void Init();

	// Table match results struct
	struct Table
	{
		Table(RefTableList *rtl, int row);

		TSTRING listName;     // list name - "title (manufacturer year)"
		TSTRING name;		  // table name
		TSTRING manuf;		  // manufacturer (shortened and normalized name)
		TSTRING manufOrig;    // original IPDB manufacturer name (full corporate legal name)
		int year;			  // year; 0 if unknown
		int players;		  // number of players; 0 if unknown
		TSTRING themes;		  // themes, with " - " delimiters
		TSTRING ipdbId;       // IPDB table ID
		TSTRING sortKey;      // sort key
		TSTRING machineType;  // IPDB machine type code (SS, EM, ME)
	};

	// Get the top 'n' matches to the given filename.  This uses string
	// similarity to look for titles that resemble the filename, with
	// a bunch of heuristics based on file naming patterns frequently
	// used for uploads to the virtual pinball sites.  The first entry
	// in the result list is the best match according to our scoring
	// heuristics; the rest of the list is sorted alphabetically.
	// This routine is specifically designed to populate a list of
	// possible matches for presentation to the user, such as in a
	// combo box drop list.
	void GetFilenameMatches(const TCHAR *filename, int n, std::list<Table> &lst);

	// Get the top 'n' matches to a partial title entered by the user.
	// This looks for similarity matches, giving precedence to leading
	// substring matches.
	void GetTitleFragmentMatches(const TCHAR *titleFragment, int n, std::list<Table> &lst);

	// Get the first N matches, in alphabetical order, to the given string
	// as a leading substring of the title.
	void GetInitMatches(const TCHAR *leadingSubstr, int n, std::list<Table> &lst);

	// Look up an entry by IPDB ID.  Returns true if found.
	bool GetByIpdbId(const TCHAR *ipdbId, std::unique_ptr<Table> &table);

protected:
	// Is the table ready?  This checks to see if the initializer
	// thread has finished.
	bool IsReady() const 
	{ 
		return hInitThread != NULL 
			&& WaitForSingleObject(hInitThread, 0) == WAIT_OBJECT_0; 
	}

	// Loader thread handle.  Because of the large data set (about
	// 6200 tables), we load the file in a background thread.  We
	// keep a handle to the thread here so that we can check for
	// completion.
	HandleHolder hInitThread;

	// underlying CSV file data
	CSVFile csvFile;

	// Bigram sets for the Name, AltName, and Initials fields.  The 
	// table is static, so we just build these vectors in parallel, 
	// indexed by the row numbers in the CSV file data.
	std::vector<DiceCoefficient::BigramSet<TCHAR>> nameBigrams;
	std::vector<DiceCoefficient::BigramSet<TCHAR>> altNameBigrams;

	// IPDB ID map.  This maps IPDB ID keys to row numbers in the CSV.
	std::unordered_map<TSTRING, int> ipdbIdMap;

	// Sorted row order.  This gives the order of the CSV rows after sorting
	// by sort key: sortedRows[0] is the row number of the first row in sorted
	// order, etc.
	std::vector<int> sortedRows;

	// CSV file column accessors
	CSVFile::Column *nameCol;
	CSVFile::Column *altNameCol;
	CSVFile::Column *manufCol;
	CSVFile::Column *manufOrigCol;
	CSVFile::Column *yearCol;
	CSVFile::Column *playersCol;
	CSVFile::Column *typeCol;
	CSVFile::Column *themeCol;
	CSVFile::Column *ipdbIdCol;

	// Sorting key.  This isn't in the CSV file; it's a column we
	// add to the in-memory set, synthesized from the file data.
	CSVFile::Column *sortKeyCol;

	// List name column.  This is another synthesized column, with
	// the name to show in the drop list, formatted as "Title
	// (Manufacturer Year)"
	CSVFile::Column *listNameCol;

	// Initials column.  This is another synthesized column, with
	// the initials of the game's name.  Many filenames refer to 
	// the title by its initials instead of using the full name,
	// to keep the filename compact.  We don't attempt a bigram
	// match on this because that yields too many false positives
	// with such short strings; we just do a plain substring 
	// search instead.
	CSVFile::Column *initialsCol;

	// create the sorting key for a row
	void MakeSortKey(int row);

	// create the list name for a row
	void MakeListName(int row);
};
