// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <oleidl.h>
#include <propvarutil.h>
#include "DOFClient.h"
#include "DiceCoefficient.h"
#include "GameList.h"
#include "../rapidxml/rapidxml.hpp"

#pragma comment(lib, "Propsys.lib")

// statics
DOFClient *DOFClient::inst;

// initialize
bool DOFClient::Init(ErrorHandler &eh)
{
	// if there's not an instance yet, create and initialize it
	if (inst == 0)
	{
		inst = new DOFClient();
		if (!inst->InitInst(eh))
		{
			// initialization failed - delete the object and return failure
			delete inst;
			inst = 0;
			return false;
		}
	}

	// success
	return true;
}

// shut down
void DOFClient::Shutdown()
{
	delete inst;
	inst = 0;
}

DOFClient::DOFClient()
{
}

DOFClient::~DOFClient()
{
	// if we initialized DOF, un-initialize it
	if (pDispatch != 0)
	{
		DISPPARAMS args = { nullptr, nullptr, 0, 0 };
		VARIANTEx result;
		EXCEPINFOEx exc;
		pDispatch->Invoke(dispidFinish, IID_NULL,
			LOCALE_SYSTEM_DEFAULT, DISPATCH_METHOD, &args, &result, &exc, 0);
	}
}

void DOFClient::SetNamedState(const WCHAR *name, int val)
{
	if (pDispatch != 0)
	{
		// Invoke UpdateNamedTableElement(name, val)
		// NB - Invoke() arguments are sent in reverse order
		VARIANTEx argp[2];
		InitVariantFromString(name, &argp[1]);		// state name
		InitVariantFromInt32(val, &argp[0]);		// value
		DISPPARAMS args = { argp, 0, countof(argp), 0 };
		VARIANTEx result;
		EXCEPINFOEx exc;
		pDispatch->Invoke(dispidUpdateNamedTableElement, IID_NULL,
			LOCALE_SYSTEM_DEFAULT, DISPATCH_METHOD, &args, &result, &exc, 0);
	}
}

// DOF COM object GUIDs
static const CLSID CLSID_DirectOutputComObject = __uuidof(DirectOutputComObject);
static IID IID_Dof = { 0x63dc1112, 0x571f, 0x4a49, { 0xb2, 0xfd, 0xcf, 0x98, 0xc0, 0x2b, 0xf5, 0xd4 } };
static IID IID_Events = { 0xa5ff940d, 0x41d4, 0x4dad, { 0x80, 0xaf, 0x46, 0x88, 0xe3, 0xf7, 0x37, 0xc1 } };

// Initialize
bool DOFClient::InitInst(ErrorHandler &eh)
{
	// get the IUnknown interface
	RefPtr<IUnknown> pUnknown;
	HRESULT hr = CoCreateInstance(CLSID_DirectOutputComObject, nullptr, CLSCTX_INPROC_SERVER, IID_Dof, (void **)&pUnknown);

	// If the error is Class Not Registered, fail silently.  This error means
	// that DOF isn't installed on this machine, which is prefectly fine: we
	// just run without any DOF effects.
	if (hr == REGDB_E_CLASSNOTREG)
		return false;

	// Generate diagnostics for other errors.  If the DOF COM class is
	// installed, DOF must be installed, so the user will want to know the
	// details if anything goes wrong.
	if (!SUCCEEDED(hr))
	{
		WindowsErrorMessage err(hr);
		eh.SysError(LoadStringT(IDS_ERR_DOFLOAD), MsgFmt(_T("CoCreateInstance failed: %s"), err.Get()));
		return false;
	}

	// Get the IDispatch
	hr = pUnknown->QueryInterface(IID_IDispatch, (void **)&pDispatch);
	if (!SUCCEEDED(hr))
	{
		WindowsErrorMessage err(hr);
		eh.SysError(LoadStringT(IDS_ERR_DOFLOAD), MsgFmt(_T("QueryInterface(IDispatch) failed: %s"), err.Get()));
		return false;
	}

	// look up the dispatch IDs
	struct
	{
		LPOLESTR name;
		DISPID *pId;
	}
	lookup[] = {
		{ L"Finish", &dispidFinish },
		{ L"Init", &dispidInit },
		{ L"GetVersion", &dispidGetVersion },
		{ L"UpdateTableElement", &dispidUpdateTableElement },
		{ L"UpdateNamedTableElement", &dispidUpdateNamedTableElement },
		{ L"TableMappingFileName", &dispidTableMappingFileName },
		{ L"GetConfiguredTableElmentDescriptors", &dispidGetConfiguredTableElmentDescriptors },
	};
	for (int i = 0; i < countof(lookup); ++i)
	{
		hr = pDispatch->GetIDsOfNames(IID_NULL, &lookup[i].name, 1, LOCALE_SYSTEM_DEFAULT, lookup[i].pId);
		if (!SUCCEEDED(hr))
		{
			WindowsErrorMessage err(hr);
			eh.SysError(LoadStringT(IDS_ERR_DOFLOAD), MsgFmt(_T("GetIDsOfNames(%s) failed: %s"), lookup[i].name, err.Get()));
			return false;
		}
	}

	// retrieve the DOF version number
	DISPPARAMS args = { nullptr, nullptr, 0, 0 };
	VARIANTEx result;
	EXCEPINFOEx exc;
	ZeroMemory(&exc, sizeof(exc));
	if (SUCCEEDED(pDispatch->Invoke(dispidGetVersion, IID_NULL, LOCALE_SYSTEM_DEFAULT,
		DISPATCH_METHOD, &args, &result, &exc, 0))
		&& result.vt == VT_BSTR)
		version = WideToTSTRING(result.bstrVal);

	// initialize - note that Invoke arguments are passed in reverse order
	VARIANTEx argp[3];
	InitVariantFromString(L"PinballY", &argp[2]);   // program name
	InitVariantFromString(L"", &argp[1]);			// table name
	InitVariantFromString(L"PinballX", &argp[0]);	// ROM name
	args = { argp, 0, countof(argp), 0 };
	result.Clear();
	exc.Clear();
	if (!SUCCEEDED(pDispatch->Invoke(dispidInit, IID_NULL, LOCALE_SYSTEM_DEFAULT,
		DISPATCH_METHOD, &args, &result, &exc, 0)))
	{
		WindowsErrorMessage err(hr);
		eh.SysError(LoadStringT(IDS_ERR_DOFLOAD), MsgFmt(_T("DOF Init failed: %s"), err.Get()));
		return false;
	}
	if (exc.wCode != 0 || exc.scode != 0)
	{
		eh.SysError(LoadStringT(IDS_ERR_DOFLOAD), MsgFmt(_T("DOF Init: exception: %ws"), exc.bstrSource));
		return false;
	}

	// load the ROM table mapping file
	LoadTableMap(eh);

	// success
	return true;
}

// Load the table mapping file
using namespace rapidxml;
void DOFClient::LoadTableMap(ErrorHandler &eh)
{
	// Query the table mapping XML filename from DOF
	TSTRING filename;
	if (pDispatch != 0)
	{
		// invoke TableMappingFileName() to get the name of the file
		DISPPARAMS args = { nullptr, nullptr, 0, 0 };
		VARIANTEx result;
		EXCEPINFOEx exc;
		if (SUCCEEDED(pDispatch->Invoke(dispidTableMappingFileName, IID_NULL,
			LOCALE_SYSTEM_DEFAULT, DISPATCH_METHOD, &args, &result, &exc, 0))
			&& result.vt == VT_BSTR)
			filename = WideToTSTRING(result.bstrVal);
	}

	// read the file
	if (filename != _T(""))
	{
		// load the file into memory
		long len = 0;
		std::unique_ptr<char> xml((char *)ReadFileAsStr(filename.c_str(), eh, len, ReadFileAsStr_NullTerm));
		if (xml == 0)
			return;

		// parse the XML
		xml_document<char> doc;
		try
		{
			doc.parse<0>(xml.get());
		}
		catch (std::exception &exc)
		{
			return;
			eh.SysError(
				MsgFmt(IDS_ERR_LOADGAMELIST, filename.c_str()),
				MsgFmt(_T("XML parsing error: %hs"), exc.what()));
			return;
		}

		// The table mapping file schema:
		//
		//   <TableNameMappings>
		//     <Mapping>
		//       <TableName>Game Title</TableName>
		//       <RomName>romname</RomName>
		//     </Mapping>
		//   </TableNameMappings>
		//
		typedef xml_node<char> node;
		typedef xml_attribute<char> attr;
		node *mappings = doc.first_node("TableNameMappings");
		if (mappings != 0)
		{
			// visit the <Mapping> nodes
			for (node *mapping = mappings->first_node("Mapping"); mapping != 0; mapping = mapping->next_sibling("Mapping"))
			{
				// Get the <TableName> and <RomName> subnodes
				if (node *t = mapping->first_node("TableName"); t != 0)
				{
					// Get the table name.  Convert it to lower-case so that matches
					// are case-insensitive.
					TSTRING tableName = AnsiToTSTRING(t->value());
					std::transform(tableName.begin(), tableName.end(), tableName.begin(), ::_totlower);

					// look up the ROM name
					if (node *r = mapping->first_node("RomName"); r != 0)
					{
						// get the ROM name and add the mapping
						TSTRING romName = AnsiToTSTRING(r->value());
						titleRomList.emplace_back(tableName.c_str(), romName.c_str());

						// add this ROM to the list of known ROMs
						TSTRING romKey = romName;
						std::transform(romKey.begin(), romKey.end(), romKey.begin(), ::_totlower);
						knownROMs[romKey] = romName;
					}
				}
			}
		}
	}

	// Retrieve the pre-configured table element descriptors
	if (pDispatch != 0)
	{
		// Invoke GetConfiguredTableElmentDescriptors() to get the predefined ROM name list.
		// This contains a list of all of the defined table elements, which is a mix of
		// DOF's traditional numbered VPinMAME triggers, like solenoid (e.g., "S7") and
		// switches ("W19"), and abstract named elements.  Named elements are designated
		// with a "$" prefix.  In the PinballX/front-end configuration, the named elements
		// comprise a mix of ROM names and front-end UI events.  There's no formal way to 
		// distinguish them, but by convention the UI events are prefixed with "PBX".
		// We could use that to leave the PBX* items out of the ROM enumeration, but it 
		// seems better to just keep everything, in case some ROM happens to start with 
		// "PBX".  The chances of a name collision are negligible because the abstract
		// event names are all long enough to reasonably ensure uniqueness.
		DISPPARAMS args = { nullptr, nullptr, 0, 0 };
		VARIANTEx result;
		EXCEPINFOEx exc;
		if (SUCCEEDED(pDispatch->Invoke(dispidGetConfiguredTableElmentDescriptors, IID_NULL,
			LOCALE_SYSTEM_DEFAULT, DISPATCH_METHOD, &args, &result, &exc, 0))
			&& result.vt == (VARTYPE)((DWORD)VT_ARRAY | (DWORD)VT_BSTR))
		{
			// lock the array
			SafeArrayLock(result.parray);

			// traverse it
			LONG lo, hi;
			SafeArrayGetLBound(result.parray, 1, &lo);
			SafeArrayGetUBound(result.parray, 1, &hi);
			for (LONG n = lo; n <= hi; ++n)
			{
				// pull out the element
				BSTR bstr;
				SafeArrayGetElement(result.parray, &n, &bstr);

				// If it starts with "$", it's a named effect, so keep it.  As described
				// above, a named effect for the PinballX/front-end config can be either
				// a ROM name or an abstract UI event name, but we don't try to distinguish;
				// we just keep them all and count on the names being long enough that we
				// don't have any collisions in practice within the mixed namespace.
				if (bstr[0] == '$')
				{
					// keep the part after the "$" prefix
					TSTRING romName = WideToTSTRING(&bstr[1]);

					// save it in the list of known ROMs
					TSTRING romKey = romName;
					std::transform(romKey.begin(), romKey.end(), romKey.begin(), ::_totlower);
					knownROMs[romKey] = romName;
				}

				// free the BSTR
				SysFreeString(bstr);
			}

			SafeArrayUnlock(result.parray);
		}
	}
}

const TCHAR *DOFClient::GetRomForTable(const GameListItem *game)
{
	// Check to see if we've resolved this game before
	if (auto it = resolvedRoms.find(game); it != resolvedRoms.end())
		return it->second.c_str();

	// If there's a ROM entry in the table database, check to see if
	// it's a known ROM in the DOF list.  If it's not in the DOF list,
	// there's no point in using it, since the DOF configuration won't
	// match it and thus won't know what to do with it.  Some tables
	// have multiple ROMs, so the one in the local database might be
	// perfectly valid but still different from the one selected in
	// the DOF configuration.  In that case, we want to ignore the one
	// in the local database and try to find a match based on the 
	// game's title instead.
	if (game->rom.length() != 0)
	{
		// Match on the lower-case name.  If we find it, return the
		// exact-case ROM name from the table.
		TSTRING romKey = game->rom;
		std::transform(romKey.begin(), romKey.end(), romKey.begin(), ::_totlower);
		if (auto it = knownROMs.find(romKey); it != knownROMs.end())
		{
			// got it - add it to the previously-resolved table and return it
			resolvedRoms[game] = it->second;
			return it->second.c_str();
		}

		// Second chance: if the specified ROM has a "_xxx" suffix,
		// try removing the suffix and searching the DOF list for
		// just the prefix.  Most actual ROM names are of the form
		// "game_ver", but the DOF config usually only stores the
		// "game" prefix portion to make it independent of the
		// specific version in use.  DOF internally matches on the
		// prefix portion, so it's fine to use the full ROM name for
		// DOF purposes when the DOF config uses the prefix only.
		if (const TCHAR *und = _tcschr(romKey.c_str(), '_'); und != nullptr)
		{
			// truncate the key at the '_'
			romKey.resize(und - romKey.c_str());

			// search for the revised name
			if (auto it = knownROMs.find(romKey); it != knownROMs.end())
			{
				resolvedRoms[game] = it->second;
				return it->second.c_str();
			}
		}
	}

	// Look it up based on the title and system
	auto rom = GetRomForTitle(game->title.c_str(), game->system);

	// If we found a match, add it to our previously-matched map and return it
	if (rom != nullptr)
		resolvedRoms[game] = rom;

	// return the result
	return rom;
}

const TCHAR *DOFClient::GetRomForTitle(const TCHAR *title, const GameSystem *system)
{
	// get the simplified title string to use as the fuzzy-match key
	TSTRING titleKey = SimplifiedTitle(title);

	// pre-compute the bigram set for the string
	DiceCoefficient::BigramSet<TCHAR> titleBigrams;
	DiceCoefficient::BuildBigramSet(titleBigrams, titleKey.c_str());

	// The DOF config tool uses a naming convention to distinguish
	// games with titles implemented in multiple systems:
	//
	//  fx2: <title>  ->  PinballFX2
	//  fx3: <title>  ->  PinballFX3
	//  fp: <title>   ->  Future Pinball
	//
	// Since we use fuzzy matching, we'll be able to match with or without
	// the prefix, but we'll generally get a higher fuzzy match score if
	// the prefix matches.  So try it both ways.  Construct a prefixed
	// version of the title that has the appropriate prefix based on the
	// system setting for the title, and try this alongside the plain
	// title string for each stage of the match.
	TSTRING prefixedTitleKey;
	DiceCoefficient::BigramSet<TCHAR> prefixedBigrams;
	if (system != nullptr && system->dofTitlePrefix.length() != 0)
	{
		prefixedTitleKey = system->dofTitlePrefix + _T(" ") + titleKey;
		DiceCoefficient::BuildBigramSet(prefixedBigrams, prefixedTitleKey.c_str());
	}

	// Try finding the name via fuzzy match.  Start with a minimum score
	// of 30% - this is an arbitrary threshold to reduce the chances that
	// we match something wildly unrelated.
	float bestScore = 0.3f;
	TitleRomPair *bestMatch = 0;
	for (auto it = titleRomList.begin(); it != titleRomList.end(); ++it)
	{
		// check the prefixed title, if present
		float score;
		if (prefixedTitleKey.length() != 0
			&& (score = DiceCoefficient::DiceCoefficient(prefixedBigrams, it->titleBigrams)) > bestScore)
		{
			bestMatch = &*it;
			bestScore = score;
		}

		// now try the base title
		if ((score = DiceCoefficient::DiceCoefficient(titleBigrams, it->titleBigrams)) > bestScore)
		{
			bestMatch = &*it;
			bestScore = score;
		}
	}

	// return the best match we found, if any
	return bestMatch != nullptr ? bestMatch->rom.c_str() : nullptr;
}

// Simplified title generator.  Removes leading and trailing whitespace,
// collapses runs of whitespace to single spaces, and replaces any
// non-word characters by spaces.
TSTRING DOFClient::SimplifiedTitle(const TCHAR *title)
{
	// strip extra whitespace and punctuation
	std::basic_regex<TCHAR> pat(_T("^\\s+|[^a-zA-Z0-9\\-]+|\\s\\s+|\\s+$"));
	TSTRING ret = std::regex_replace(title, pat, _T(" "));

	// convert to lower-case
	std::transform(ret.begin(), ret.end(), ret.begin(), ::_totlower);

	// return the result
	return ret;
}
