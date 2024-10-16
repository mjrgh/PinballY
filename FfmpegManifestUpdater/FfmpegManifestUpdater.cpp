// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// FfmpegManifestUpdater - build tool for updating the FFMPEG.EXE
// manifest to insert a "DPI Aware" indication.  We run this from the
// release batch script to make sure that the FFMPEG.EXE files we
// include in the distribution have the True/PM DPI Awareness flag
// set in the program manifest.
//
// IMPORTANT:  The release script must check the exit code from this
// program and abort the release build process if it's non-zero.  This
// will ensure that we catch any exception conditions where future
// FFMPEG versions start using their own manifests with conflicting
// settings, which will require human intervention to decide what to
// do about it.
//
// The standard distribution of FFMPEG.EXE has no application manifest.
// That makes Windows put the default DPI Awareness status of "completely
// clueless about DPI issues" into effect for the process.  When a 
// process is in DPI Unaware mode, most of the Windows APIs that accept
// or return pixel coordinates for windows, monitors, or the desktop
// use a virtualized pixel space that's normalized to the pre-Vista
// standard of 96 dpi across all monitors.  FFMPEG barely uses such
// APIs, as it has a purely character-mode command line UI, but it does
// have to interrogate the desktop layout to do screen captures.  With
// the default DPI Unaware mode, FFMPEG sees the virtualized version 
// of the desktop layout, so its notion of the overall size of the 
// desktop and the pixel locations of monitors within the desktop space
// don't reflect the true hardware pixel layout - they reflect the
// virtualized layout that Windows provides to legacy apps that don't
// use the newer variable-DPI services.  
//
// But wait!  It gets weirder.  When Windows 7 came along and added
// the *first* iteration of the variable DPI APIs, people noticed that 
// FFMPEG was screwing up their screen captures on multi-monitor systems 
// that took advantage of the new variable DPI settings.  Bug reports
// ensued.  Those bugs finally got addressed in late 2015 (see ffmpeg 
// ticket 4232) with code that explicitly adjusted the fake virtualized
// pixel coordinates that Windows reports through the "DPI Unaware" APIs
// by applying the desktop resolution scaling factor from the system 
// metrics.  That more or less compensates for the variable DPI features
// added in Windows 7.  But Microsoft added another round of new DPI
// features in Windows 8.1, known as Per Monitor Awareness Version 1,
// and then another round in Windows 10, known as Per Monitor V2.
// The first iteration in Windows 7 allowed for the DPI setting to
// break away from the old fixed 96 dpi standard on a system-wide
// basis, across all monitors; the Per Monitor additions in 8.1 and 10
// added the ability to vary the DPI setting and scaling, as the name
// suggests, for each individual monitor in a multi-monitor system.
// The fix added to FFMPEG actually makes things worse in the 8.1/10
// world, because it incorrectly tries to apply a single scaling factor
// to the whole desktop, when in reality each monitor can have its
// own separate scaling.  FFMPEG's adjustment gets it wrong in the
// virtual 96 dpi space and wrong in the true per-monitor space.
//
// Fortunately, there seems to be a really easy solution to this, which
// is to supply a program manifest that declares the program to be 
// fully Per Monitor DPI Aware.  I personally think this is what the
// FFMPEG guys should have done in the first place.  There really was
// no good reason to put in the code they did to do the compensating
// calculation for desktop scale - they could have simply asked Windows
// to report the correct numbers in the first place, and then they
// wouldn't have needed the adjustment.  And they wouldn't have had
// the new issue when Win 8.1 and 10 came along where their adjustment
// is counterproductive.
// 
// That's where this program comes in.  It does the following:
//
// - Reads the FFMPEG.EXE to determine if it has one
//   - If not, it runs the manifest tool (MT) to add one
//   - If so, it looks at the manifest to see if DPI Awareness it set:
//     - Old style: assembly/application/windowsSettings/dpiAware == "True/PM" -> SUCCESS
//     - New style: assembly/application/windowsSettings/dpiAwareness == "permonitor" or "permonitorv2" -> SUCCESS
//     - Otherwise -> FAILURE
//
// The point of this process is to make sure that we explicitly add our
// DPI Aware manifest every time we grab a new official release of FFMPEG.
// The official releases don't currently have manifests at all, so this
// will add one with the information we want.
// 
// The reason that we fail with an error when there's a manifest that 
// doesn't match what we'd add is that this condition will only arise
// if a future official release of FFMPEG actually starts including a
// manifest, and that manifest doesn't specify the DPI awareness setting
// we're looking for.  Should that happen, human intervention will be
// required to figure out why.  Did the FFMPEG developers decide that
// there's some other DPI awareness setting that's actually more correct?
// Did they start adding a manifest for other reasons and leave DPI
// awareness unspecified?  Whatever the situation, we'll have to look
// at what they added for the manifest, understand why, and decide what
// we're going to do about it.  At that point, we'll need to update this
// program to detect that new baseline for official releases and apply
// our fixups as needed.
//

#include "stdafx.h"
#include <list>
#include <unordered_map>
#include <string>
#include <list>
#include <regex>
#include <Shlwapi.h>
#include <Shlobj.h>
#include "../Utilities/Util.h"
#include "../Utilities/ProcUtil.h"
#include "../Utilities/StringUtil.h"

static void errexit(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	printf("FffmpegManifestUpdater *** ERROR ***\n");
	vprintf(msg, ap);
	printf("\n");
	va_end(ap);
	exit(2);
}

int AddManifest(const char *exe)
{
	// get the name of the manifest file - it's the name of the executable
	// with .EXE replaced by .manifest
	char manifest[MAX_PATH];
	strcpy_s(manifest, exe);
	PathRemoveExtensionA(manifest);
	PathAddExtensionA(manifest, ".manifest");

	// search the PATH variable for the manifest tool (MT.EXE)
	char path[8192];
	size_t pathlen;
	if (getenv_s(&pathlen, path, "PATH") != 0)
		errexit("Unable to retrieve PATH");

	char mt[MAX_PATH];
	if (!SearchPathA(path, "MT.EXE", NULL, countof(mt), mt, NULL))
		errexit("Manifest tool (MT.EXE) not found.  Please make sure you're running\n"
			"in a CMD prompt with the PATH set up for your Visual Studio tool set.\n"
			"Most Visual Studio versions provide a batch script (usually VCVARS32.BAT)\n"
			"that sets up the CMD environment properly.\n");

	// set up the command line
	TSTRINGEx cmdline;
	cmdline.Format(_T("\"%s\" -manifest %hs -outputresource:%hs;1"), mt, manifest, exe);

	// set up the startup info
	STARTUPINFOA si;
	ZeroMemory(&si, sizeof(si));
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

	// run the manifest tool
	PROCESS_INFORMATION pi;
	if (!CreateProcessA(mt, TSTRINGToCSTRING(cmdline).data(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
		errexit("Error launching manifest tool");

	// "wait for it..."
	if (WaitForSingleObject(pi.hProcess, 10000) != WAIT_OBJECT_0)
		errexit("Manifest tool process didn't exit as expected (or an error occurred in the wait)");

	// return its return code
	DWORD exitCode;
	GetExitCodeProcess(pi.hProcess, &exitCode);
	return (int)exitCode;
}

int main(int argc, const char **argv)
{
	// check arguments
	int argi = 1;
	if (argi + 1 > argc)
		errexit("usage: FfmpegManifestUpdater <ffmpeg.exe file>");

	// get the filename
	const char *exe = argv[argi];

	// read its manifest
	ProgramManifestReader manifest;
	if (!manifest.Read(AnsiToTSTRING(exe).c_str(), false))
		errexit("FfmpegManifestUpdater: error reading program manifest");

	// if there's no manifest, add our standard manifest
	if (manifest.IsEmpty())
		return AddManifest(exe);

    // what we found
    std::list<std::string> foundWhat;
	bool needsUpdate = false;

    // There's a manifest.  Search for assembly/application/windowsSettings/dpiAware.
	if (auto assembly = manifest.doc.first_node_no_ns("assembly"); assembly != nullptr)
	{
		if (auto appl = assembly->first_node_no_ns("application"); appl != nullptr)
		{
			if (auto settings = appl->first_node_no_ns("windowsSettings"); settings != nullptr)
			{
				if (auto dpi = settings->first_node_no_ns("dpiAware"); dpi != nullptr)
				{
					if (auto dpival = dpi->first_node(); dpival != nullptr && dpival->value() != nullptr)
					{
						if (std::regex_search(dpival->value(), std::regex("true/pm", std::regex_constants::icase)))
						{
							// the desired manifest setting is present - no modification needed
							return 0;
                        }

                        char msg[256];
                        sprintf_s(msg, "Found assembly/application/windowsSettings/dpiAware = \"%s\", required \"true/pm\"", dpival->value());
                        foundWhat.emplace_back(msg);
						needsUpdate = true;
					}
				}

				// check for the newer "dpiAwareness"
				if (auto dpi = settings->first_node_no_ns("dpiAwareness"); dpi != nullptr)
				{
					if (auto dpival = dpi->first_node(); dpival != nullptr && dpival->value() != nullptr)
					{
						if (std::regex_search(dpival->value(), std::regex("\\b(permonitor|permonitorv2)\\b", std::regex_constants::icase)))
						{
							// the desired manifest setting is present - no modification needed
							// on account of this (but we might still need to update because
							// of the old-style <dpiAware> element)
							if (!needsUpdate)
								return 0;
						}

                        char msg[256];
                        sprintf_s(msg, "Found assembly/application/windowsSettings/dpiAwareness = \"%s\", required \"permonitor\" or \"permonitorv2\"", dpival->value());
                        foundWhat.emplace_back(msg);
					}
				}
			}
		}
	}

	// if we didn't fail with an error, but we need an update, apply the update
	if (needsUpdate)
		return AddManifest(exe);

	// The manifest doesn't have our desired DPI Aware flag.  Fail
	// with an error, because we don't want to overwrite any manifest
	// included in a future official FFMPEG release with our own -
	// we only want to add a manifest when none is already present.
	// If the FFMPEG people start adding their own, we'll have to
	// re-evaluate what we do about it at that point.
    printf("*** FfmpegManifestUpdater Error ***\n"
        "This copy of FFMPEG.EXE (%s) already contains an embedded manifest with\n"
		"a different DPI Aware setting (or no DPI Aware setting).  You'll have to\n"
		"inspect the manifest to determine what to do, and update this tool\n"
           "accordingly.\n\n", exe);

    for (auto &f : foundWhat)
        printf("%s\n", f.c_str());
	exit(2);
}
