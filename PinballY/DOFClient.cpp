// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <oleidl.h>
#include <propvarutil.h>
#include "../rapidxml/rapidxml.hpp"
#include "../Utilities/ComUtil.h"
#include "../Utilities/ProcUtil.h"
#include "DOFClient.h"
#include "DiceCoefficient.h"
#include "GameList.h"
#include "LogFile.h"

#pragma comment(lib, "Propsys.lib")

// statics
DOFClient *DOFClient::inst = nullptr;
volatile bool DOFClient::ready = false;
HandleHolder DOFClient::hInitThread;
CapturingErrorHandler DOFClient::initErrors;

// 64-bit-only statics
#ifdef _M_X64
bool DOFClient::surrogateStarted;
HandleHolder DOFClient::hSurrogateDoneEvent;
CLSID DOFClient::clsidProxyClass;
#endif

// initialize
void DOFClient::Init()
{
	// if a prior initialization is already in progress, wait for it
	if (hInitThread != nullptr)
		WaitForSingleObject(hInitThread, 15000);

	// reset the initialization status
	hInitThread = nullptr;
	ready = false;
	initErrors.Clear();

	// if there's not an instance yet, create and initialize it
	if (inst == nullptr)
	{
		// do initialization on a background thread
		struct ThreadContext
		{
			ThreadContext() { }
		};
		auto ThreadMain = [](LPVOID lpvParam) -> DWORD
		{
			// retrieve the context
			std::unique_ptr<ThreadContext> ctx(static_cast<ThreadContext*>(lpvParam));

			// log what we're doing
			LogFile::Get()->Group(LogFile::DofLogging);
			LogFile::Get()->Write(LogFile::DofLogging, _T("DOF (DirectOutput): initializing DOF client\n"));

			// initialize COM on this thread
			CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

			// If we're in 64-bit mode, we need to create our surrogate
			// process for loading the DOF DLL.
#ifdef _M_X64
			if (!surrogateStarted)
			{
				// flag that we've at least tried to start the surrogate
				surrogateStarted = true;

				// Generate a random GUID for the proxy class.  We use a random GUID
				// to make the proxy private to this application instance, to avoid any 
				// collisions with other running instances.
				CoCreateGuid(&clsidProxyClass);

				// Create the events to coordinate with the child process.  These are
				// passed by name, with the name generated from our process ID.
				DWORD pid = GetCurrentProcessId();
				TCHAR readyEventName[128], doneEventName[128];
				_stprintf_s(readyEventName, _T("PinballY.Dof6432Surrogate.%lx.Event.Ready"), pid);
				_stprintf_s(doneEventName, _T("PinballY.Dof6432Surrogate.%lx.Event.Done"), pid);

				HandleHolder hSurrogateReadyEvent = CreateEvent(NULL, FALSE, FALSE, readyEventName);
				hSurrogateDoneEvent = CreateEvent(NULL, FALSE, FALSE, doneEventName);

				// get the surrogate exe name
				TCHAR surrogateExe[MAX_PATH];
				GetDeployedFilePath(surrogateExe, _T("Dof3264Surrogate.exe"), _T("$(SolutionDir)$(Configuration)\\Dof3264Surrogate.exe"));

				// build the command line
				TSTRINGEx cmdline;
				cmdline.Format(_T(" -parent_pid=%ld -clsid=%s"),
					pid, FormatGuid(clsidProxyClass).c_str());

				// set up the launch information
				STARTUPINFO si;
				ZeroMemory(&si, sizeof(si));
				si.dwFlags = STARTF_USESHOWWINDOW;
				si.wShowWindow = SW_HIDE;

				// log the proxy setup
				LogFile::Get()->Write(LogFile::DofLogging,
					_T("+ Launching DOF surrogate process.  This is required because PinballY is running\n")
					_T("  in 64-bit, and DOF is a 32-bit COM object.  Surrogate command line:\n")
					_T("  >\"%s\" %s\n"), surrogateExe, cmdline.c_str());

				// launch it
				PROCESS_INFORMATION pi;
				if (!CreateProcess(surrogateExe, cmdline.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
				{
					WindowsErrorMessage err;
					initErrors.SysError(LoadStringT(IDS_ERR_DOFLOAD), MsgFmt(_T("Surrogate process (\"%s\" %s) launch failed: %s"),
						surrogateExe, cmdline.c_str(), err.Get()));

					LogFile::Get()->Group();
					LogFile::Get()->Write(_T("DOF surrogate launch failed:\n")
						_T("  Command line: \"%s\" %s\n")
						_T("  CreateProcess Error: %s\n"),
						surrogateExe, cmdline.c_str(), err.Get());
				}
				else
				{
					// wait for the process to declare itself ready
					if (WaitForSingleObject(hSurrogateReadyEvent, 5000) != WAIT_OBJECT_0)
					{
						initErrors.SysError(LoadStringT(IDS_ERR_DOFLOAD), _T("Surrogate process isn't responding (ready wait timed out"));
						LogFile::Get()->Group();
						LogFile::Get()->Write(_T("DOF surrogate process isn't responding (ready wait timed out)\n")
							_T("Command line: \"%s\" %s\n"),
							surrogateExe, cmdline.c_str());

						// set the 'done' event to try to make the surrogate shut down
						SetEvent(hSurrogateDoneEvent);
						hSurrogateDoneEvent = NULL;

						// give it a moment to shut down on its own, then try to kill it
						Sleep(250);
						SaferTerminateProcess(pi.hProcess);
					}

					// close the process and thread handles
					CloseHandle(pi.hProcess);
					CloseHandle(pi.hThread);
				}
			}
#endif

			// create and initialize a new instance
			auto newInst = std::make_unique<DOFClient>();
			if (newInst->InitInst(initErrors))
			{
				// successfully initialized - store the global singletone
				inst = newInst.release();
			}

			// initialization is completed
			ready = true;

			// done
			CoUninitialize();
			return 0;
		};

		// launch the initializer thread
		auto ctx = std::make_unique<ThreadContext>();
		DWORD tid;
		hInitThread = CreateThread(NULL, 0, ThreadMain, ctx.get(), 0, &tid);

		// if the thread launch succeeded, release our unique pointer to the
		// context, to pass ownership to the thread
		if (hInitThread != nullptr)
			ctx.release();
	}
}

bool DOFClient::WaitReady()
{
	// wait for the initialization thread to complete, if it hasn't already
	if (!ready && hInitThread != nullptr)
		WaitForSingleObject(hInitThread, INFINITE);

	// forget the initializer thread handle
	hInitThread = nullptr;

	// if initialization succeeded, the global singleton will exist
	return inst != nullptr;
}

// shut down
void DOFClient::Shutdown(bool final)
{
	LogFile::Get()->Group(LogFile::DofLogging);
	LogFile::Get()->Write(LogFile::DofLogging, _T("DOF: shutting down DOF client\n"));

	// wait for the initializer thread to finish
	if (hInitThread != nullptr)
	{
		WaitForSingleObject(hInitThread, 15000);
		hInitThread = nullptr;
	}

	// if there's an instance, delete it and forget it
	if (inst != nullptr)
	{
		delete inst;
		inst = nullptr;
	}

	// check for application terminal ('final' mode)
	if (final)
	{
#ifdef _M_X64
		// 64-bit mode - shut down the surrogate process
		if (hSurrogateDoneEvent != NULL)
		{
			SetEvent(hSurrogateDoneEvent);
			hSurrogateDoneEvent = NULL;
		}
#endif
	}
}

DOFClient::DOFClient()
{
}

DOFClient::~DOFClient()
{
	// if we initialized DOF, un-initialize it
	if (ready && pDispatch != 0)
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
	if (ready && pDispatch != nullptr)
	{
		// Invoke UpdateNamedTableElement(name, val)
		// NB - Invoke() arguments are sent in reverse order
		VARIANTEx argp[2];
		InitVariantFromString(name, &argp[1]);		// state name
		InitVariantFromInt32(val, &argp[0]);		// value
		DISPPARAMS args = { argp, nullptr, countof(argp), 0 };
		VARIANTEx result;
		EXCEPINFOEx exc;
		pDispatch->Invoke(dispidUpdateNamedTableElement, IID_NULL,
			LOCALE_SYSTEM_DEFAULT, DISPATCH_METHOD, &args, &result, &exc, 0);
	}
}

// DOF COM object GUIDs
static IID IID_Dof = { 0x63dc1112, 0x571f, 0x4a49, { 0xb2, 0xfd, 0xcf, 0x98, 0xc0, 0x2b, 0xf5, 0xd4 } };
static IID IID_Events = { 0xa5ff940d, 0x41d4, 0x4dad, { 0x80, 0xaf, 0x46, 0x88, 0xe3, 0xf7, 0x37, 0xc1 } };
class __declspec(uuid("{D744EE13-4C70-474D-8FB1-8295C350FB07}")) DOFProxy64;

// Initialize
bool DOFClient::InitInst(ErrorHandler &eh)
{
	LogFile::Get()->Group(LogFile::DofLogging);
	LogFile::Get()->Write(LogFile::DofLogging, _T("DOF: creating DOF COM object (%s)\n"),
		FormatGuid(__uuidof(DirectOutputComObject)).c_str());

	// Create an instance of the DOF COM object.  DOF is implemented as a
	// 32-bit COM object, so we can use a simple in-process server (i.e.,
	// load it as a DLL) if this process is in 32-bit mode.  For 64-bit
	// mode, we have to load it out-of-process as a local server instead,
	// since Windows doesn't allow a 64-bit EXE to load a 32-bit DLL.
	// Do this by creating the proxy class provided by the surrogate
	// COM factory process we launched at startup.
	RefPtr<IUnknown> pUnknown;
	HRESULT hr = CoCreateInstance(
		IF_32_64(__uuidof(DirectOutputComObject), clsidProxyClass), nullptr,
		IF_32_64(CLSCTX_INPROC_SERVER, CLSCTX_LOCAL_SERVER | CLSCTX_INPROC_SERVER),
		IID_Dof, (void **)&pUnknown);

	// If the error is Class Not Registered, fail silently.  This error means
	// that DOF isn't installed on this machine, which is prefectly fine: we
	// just run without any DOF effects.
	if (hr == REGDB_E_CLASSNOTREG)
	{
		LogFile::Get()->Write(LogFile::DofLogging, 
			_T("DOF: DOF COM object (%s) is not registered on this system; DOF will not be used for this session\n"),
			FormatGuid(__uuidof(DirectOutputComObject)).c_str());
		return false;
	}

	// Generate diagnostics for other errors.  If the DOF COM class is
	// installed, DOF must be installed, so the user will want to know the
	// details if anything goes wrong.
	if (!SUCCEEDED(hr))
	{
		// assume the generic DOF load error
		int msgId = IDS_ERR_DOFLOAD;

		// If we're in 64-bit mode, an E_NOINTERFACE error almost always
		// means that an older version of DOF is installed.  Creating an
		// instance out-of-process requires going through the COM marshaller,
		// which requires a type library for the interface being marshalled.
		// Older DOF versions didn't ship with the type library and didn't
		// register it with the COM object.  DOF R3++ 2018-09-04 or later
		// is required.  There's about a 110% chance that this is the source
		// of the problem if we get an E_NOINTERFACE in 64-bit mode.
#ifdef _M_X64
		if (hr == E_NOINTERFACE)
		{
			msgId = IDS_ERR_DOF64_UPGRADE_REQUIRED;
			LogFile::Get()->Write(_T("DOF UPDATE REQUIRED:  It looks like you need an updated version of DOF\n")
				_T("to use with the 64-bit version of PinballY.  Please download and install a version of\n")
				_T("DOF R3++ dated ***2018-09-04 OR LATER*** from http://mjrnet.org/pinscape/dll-updates.html\n")
				_T("The Windows Setup (MSI) installer is recommended because registry updates for the DOF COM\n")
				_T("object are required as part of this update.\n"));
		}
#endif

		// show the message
		WindowsErrorMessage err(hr);
		eh.SysError(LoadStringT(msgId), MsgFmt(_T("CoCreateInstance failed: %s"), err.Get()));
		LogFile::Get()->Write(_T("DOF: CoCreateInstance for DOF COM object (%s) failed: %s (hresult %lx)\n"),
			FormatGuid(__uuidof(DirectOutputComObject)).c_str(), err.Get(), err.GetCode());
		return false;
	}

	// Get the IDispatch
	hr = pUnknown->QueryInterface(IID_IDispatch, (void **)&pDispatch);
	if (!SUCCEEDED(hr))
	{
		WindowsErrorMessage err(hr);
		eh.SysError(LoadStringT(IDS_ERR_DOFLOAD), MsgFmt(_T("QueryInterface(IDispatch) failed: %s"), err.Get()));
		LogFile::Get()->Write(_T("DOF: QueryInterface(IDispatch) failed: %s\n"), err.Get());
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
			eh.SysError(LoadStringT(IDS_ERR_DOFLOAD), MsgFmt(_T("GetIDsOfNames(%ws) failed: %s"), lookup[i].name, err.Get()));
			LogFile::Get()->Write(_T("DOF: GetIDsOfNames(%ws) failed: %s\n"), lookup[i].name, err.Get());
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
	InitVariantFromString(L"PinballY", &argp[0]);	// ROM name
	args = { argp, 0, countof(argp), 0 };
	result.Clear();
	exc.Clear();
	if (!SUCCEEDED(pDispatch->Invoke(dispidInit, IID_NULL, LOCALE_SYSTEM_DEFAULT,
		DISPATCH_METHOD, &args, &result, &exc, 0)))
	{
		WindowsErrorMessage err(hr);
		eh.SysError(LoadStringT(IDS_ERR_DOFLOAD), MsgFmt(_T("DOF Init failed: %s"), err.Get()));
		LogFile::Get()->Write(_T("DOF: Init() failed: %s\n"), err.Get());
		return false;
	}
	if (exc.wCode != 0 || exc.scode != 0)
	{
		eh.SysError(LoadStringT(IDS_ERR_DOFLOAD), MsgFmt(_T("DOF Init: exception: %ws"), exc.bstrSource));
		LogFile::Get()->Write(_T("DOF: Init() exception: %ws\n"), exc.bstrSource);
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
	HRESULT hr;
	if (pDispatch != 0)
	{
		// invoke TableMappingFileName() to get the name of the file
		DISPPARAMS args = { nullptr, nullptr, 0, 0 };
		VARIANTEx result;
		EXCEPINFOEx exc;
		if (SUCCEEDED(hr = pDispatch->Invoke(dispidTableMappingFileName, IID_NULL,
			LOCALE_SYSTEM_DEFAULT, DISPATCH_METHOD, &args, &result, &exc, 0))
			&& result.vt == VT_BSTR)
		{
			filename = WideToTSTRING(result.bstrVal);
			LogFile::Get()->Write(LogFile::DofLogging, _T("DOF: got table mapping file: %s\n"), filename.c_str());
		}
		else
		{
			WindowsErrorMessage err(hr);
			LogFile::Get()->Write(LogFile::DofLogging, _T("DOF: unable to get table mapping file: %s\n"),
				SUCCEEDED(hr) ? _T("result is not BSTR") : err.Get());
		}
	}

	// read the file
	if (filename != _T(""))
	{
		// load the file into memory
		long len = 0;
		std::unique_ptr<char> xml((char *)ReadFileAsStr(filename.c_str(), eh, len, ReadFileAsStr_NullTerm));
		if (xml == nullptr)
		{
			LogFile::Get()->Write(LogFile::DofLogging, _T("DOF: unable to load table mapping file %s\n"), filename.c_str());
			return;
		}

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
			LogFile::Get()->Write(LogFile::DofLogging, _T("DOF: unable to parse table mapping file %s as XML: %hs\n"), 
				filename.c_str(), exc.what());
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
		// Invoke GetConfiguredTableElmentDescriptors() to get the predefined ROM name 
		// list.  This contains a list of all of the defined table elements, which is 
		// a mix of DOF's traditional numbered VPinMAME triggers (e.g., solenoids
		// ["S7"], switches ["W19"], lamps (["L5"]), and abstract named elements. 
		// Named elements use a "$" prefix, and comprise a mix of ROM names and 
		// abstract UI events.  There's no formal way to distinguish the two, but by
		// convention, all of our UI events are prefixed with "PBY".  (This follows the
		// pattern used in PinballX, which uses "PBX" prefixes.)  We could omit all
		// of the PBY* names from the ROM enumeration, since they're almost certainly
		// our UI event names rather than ROMs, but it seems better to keep everything
		// just in case some actual ROM happens to start with "PBY".  The chances of
		// a name collision are negligible, even if such a ROM comes into being, since
		// all of our event names are all long enough to reasonably ensure uniqueness.
		DISPPARAMS args = { nullptr, nullptr, 0, 0 };
		VARIANTEx result;
		EXCEPINFOEx exc;
		if (SUCCEEDED(hr = pDispatch->Invoke(dispidGetConfiguredTableElmentDescriptors, IID_NULL,
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
				// above, a "$" named effect can be either a ROM name or an abstract UI 
				// event name, but we don't try to distinguish; we just keep them all and
				// count on the names being long enough that we don't have any collisions
				// in practice within the mixed namespace.
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
		else
		{
			WindowsErrorMessage err(hr);
			LogFile::Get()->Write(LogFile::DofLogging, _T("DOF: GetConfiguredTableElmentDescriptors failed: %s\n"),
				SUCCEEDED(hr) ? _T("result is not array of BSTR") : err.Get());
		}
	}
}

const TCHAR *DOFClient::GetRomForTable(const GameListItem *game)
{
	// return null if we're not ready
	if (!ready)
		return nullptr;

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
	// return null if not ready
	if (!ready)
		return nullptr;

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
