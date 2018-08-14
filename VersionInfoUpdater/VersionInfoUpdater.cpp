// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// BuildInfo.cpp - Build number updater
//
// This file reads the main version file for the system, VersionInfo.h,
// and generates derived files with version information:
//
//   VersionInfo.cpp - version extern variable definitions
//   VersionInfo.rc  - include header for RC version resources
//   VersionInfo.wxi - include header for the Wix setup builder
// 
// The main version number (the "1.2.3" number) is manually defined and
// manually maintained.  This program takes those numbers as defined in
// VersionInfo.h and generates several derived variables based on them,
// such as a formatted version string for display in the About Box, and
// a "semantic versioning" string useful for tools like installers that
// want to compare version numbers embedded in binaries.
//
// Additionally, this program produces some separate numbers that are
// automatically updated on every build, such as a build number (a serial
// number incremented on each build) and a timestamp.
//
// For WiX purposes, we use the fourth dotted portion of the version
// number to encode the release level and the pre-release sequence number,
// as follows:
//
//     dev builds -> build number
//     alpha      -> 30000 + pre-release sequence number
//     beta       -> 40000 + pre-release sequence number
//     RC         -> 50000 + pre-release sequence number
//     release    -> 60000

#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <regex>
#include <list>
#include <Objbase.h>
#include <Shlwapi.h>


template<typename T> static std::string join(std::list<T> &l, const char *separator = ",", const char *empty = "")
{
	// check for an empty list
	if (l.size() == 0)
		return empty;

	// collect the list
	std::string s;
	const char *prefix = "";
	for (auto it : l)
	{
		s.append(prefix);
		s.append(it);
		prefix = separator;
	}

	// return the result
	return s;
}

static void errexit(int err, const char *msg, ...)
{
	va_list va;
	va_start(va, msg);
	printf("VersionInfoUpdater: Error BI%04d: ", err);
	vprintf(msg, va);
	va_end(va);
	exit(2);
}

int main(int argc, char **argv)
{
	// parse arguments
	const char *inFilename = 0;
	const char *cppFilename = 0;
	const char *rcFilename = 0;
	const char *wxiFilename = 0;
	for (int i = 1; i < argc; ++i)
	{
		const char *ap = argv[i];
		if (strcmp(ap, "-in") == 0 && i + 1 < argc)
			inFilename = argv[++i];
		else if (strcmp(ap, "-cpp") == 0 && i + 1 < argc)
			cppFilename = argv[++i];
		else if (strcmp(ap, "-rc") == 0 && i + 1 < argc)
			rcFilename = argv[++i];
		else if (strcmp(ap, "-wxi") == 0 && i + 1 < argc)
			wxiFilename = argv[++i];
		else
		{
			printf("Invalid argument \"%s\"", ap);
			exit(2);
		}
	}

	// make sure we got all of the inputs
	if (inFilename == 0)
		errexit(1001, "Missing input filename; specify with '-in filename'\n");
	if (cppFilename == 0)
		errexit(1002, "Missing cpp filename; specify with '-cpp filename'\n");
	if (rcFilename == 0)
		errexit(1003, "Missing rc filename; specify with '-rc filename'\n");
	if (wxiFilename == 0)
		errexit(1004, "Missing wxi filename; specify with '-wxi filename'\n");
		
	// announce what we're doing
	printf("BuildInfo: %s -> (cpp=%s, rc=%s)\n", inFilename, cppFilename, rcFilename);

	// Read the last build number.  This is in a file in the working
	// directory called BuildNumber.txt.  If not found, start at 1.
	int buildNo = 0;
	FILE *fp;
	const char *buildNoFile = "BuildNumber.txt";
	if (fopen_s(&fp, buildNoFile, "r") == 0)
	{
		// read the first line and try parsing it as an int
		char buf[128];
		if (fgets(buf, sizeof(buf), fp) != 0)
			buildNo = atoi(buf);

		// done with the file
		fclose(fp);
	}

	// increment the build number and write it back to the file
	buildNo += 1;
	if (fopen_s(&fp, buildNoFile, "w") != 0)
		errexit(1100, "Unable to open build number file (%s) for writing\n", buildNoFile);

	// write the update and close the file
	fprintf(fp, "%d\n", buildNo);
	fclose(fp);

	// Generate the date strings.  The regular date is in YYYYMMDD-hhmm
	// format, which makes it fairly human-readable but keeps it compact.
	// "semDate" is for the semantic version string in the build metadata
	// section.  To conform to the semantic version guidelines, we encode
	// this with a "T" in place of the hyphen in the regular date string,
	// so that the whole thing looks like one token.
	char date[128], semDate[128];
	struct tm tm;
	time_t tt;
	time(&tt);
	gmtime_s(&tm, &tt);
	sprintf_s(date, "%04d%02d%02d-%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
	sprintf_s(semDate, "%04d%02d%02dT%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
	int year = tm.tm_year + 1900;

	// Generate a unique GUID for the build
	GUID guid;
	CoCreateGuid(&guid);

	// format it as a string
	char guidStr[40];
	sprintf_s(guidStr, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

	// Generate the copyright date range.  Use "2018-<current year>" if the
	// current year isn't 2018, otherwise just "2018".
	std::string copyrightOwner = "The PinballY Implementers";
	char copyrightDates[128];
	sprintf_s(copyrightDates, "%s%d", year == 2018 ? "" : "2018-", year);

	// we don't have any version info yet
	int versionNumber[3] = { 0, 0, 0 };
	int prereleaseSeqno = 0;
	char releaseStatus = 'R';
	const char *releaseStatusName = "Release";
	std::string fork = "";
	std::string preReleaseSuffix = "";

	// File flags, for the versioning resource.  This is a list 
	// of VS_FF_xxx macro names, which we'll combine together
	// with "|" symbols when the time comes.
	std::list<std::string> fileflags;

	// read the input file
	if (fopen_s(&fp, inFilename, "r") == 0)
	{
		std::regex versionPat("#\\s*define\\s+PINBALLY_VERSION\\s* \"(\\d+)\\.(\\d+)\\.(\\d+)");
		std::regex releaseStatusPat("#\\s*define\\s+PINBALLY_RELEASE_STATUS\\s* STATUS_(\\w+)");
		std::regex prereleaseSeqnoPat("#\\s*define\\s+PINBALLY_PRERELEASE_SEQNO\\s* (\\d+)");
		std::regex copyrightPat("#\\s*define\\s+PINBALLY_COPYRIGHT_OWNERS\\s* \"([^\"]*)\"");
		std::regex forkPat("#\\s*define\\s+PINBALLY_FORK_ID\\s* \"([^\"]*)\"");
		for (;;)
		{
			// read the next line
			char buf[256];
			std::string txt;
			if (fgets(buf, sizeof(buf), fp) == 0)
				break;

			// check for the special macro definitions
			std::cmatch m;
			if (std::regex_search(buf, m, versionPat))
			{
				// Version number string - pull out the three numeric elements.
				// This is an input, so leave it unchanged.
				for (int i = 0; i < 3; ++i)
					versionNumber[i] = atoi(((std::string)m[i + 1]).c_str());
				txt = buf;
			}
			else if (std::regex_search(buf, m, forkPat))
			{
				// Fork ID - this is an input, so simply save it and leave
				// it unchanged.
				fork = m[1];
				txt = buf;
			}
			else if (std::regex_search(buf, m, copyrightPat))
			{
				// Copyright owner - this is an input, so simply save it and
				// leave it unchanged.
				copyrightOwner = m[1];
				txt = buf;
			}
			else if (std::regex_search(buf, m, prereleaseSeqnoPat))
			{
				// Pre-release sequence number - this is an input, so simply
				// save it and leave it unchanged.
				prereleaseSeqno = atoi(((std::string)m[1]).c_str());
				txt = buf;
			}
			else if (std::regex_search(buf, m, releaseStatusPat))
			{
				// Release status.  This is an input, so note it and keep
				// the original value.  Note that these are unique in the 
				// first letter, so just pull out one character.
				releaseStatus = ((std::string)m[1]).c_str()[0];
				txt = buf;

				// Figure some other values based on the release type:
				// the semantic versioning pre-release suffix string (-alpha,
				// -beta, etc), the human-readable release status name,
				// and any VERSIONINFO file flags implied by the status.
				switch (releaseStatus)
				{
				case 'A':
					// Alpha.  These are pre-release builds, so add the
					// PRERELEASE flag to the VERSIONINFO resource.
					preReleaseSuffix = "-alpha";
					releaseStatusName = "Alpha";
					fileflags.push_back("VS_FF_PRERELEASE");
					break;

				case 'B':
					// Beta.  This is also a pre-release, so use the
					// PRERELEASE flag.
					preReleaseSuffix = "-beta";
					releaseStatusName = "Beta";
					fileflags.push_back("VS_FF_PRERELEASE");
					break;

				case 'C':
					// Release candidate (RC).  This is yet another
					// pre-release type.
					preReleaseSuffix = "-rc";
					releaseStatusName = "RC";
					fileflags.push_back("VS_FF_PRERELEASE");
					break;

				case 'D':
					// Development build.  This is a pre-release type, and we
					// also consider it a private build for VERSIONINFO purposes,
					// since these aren't usually distributed at all, or at most
					// to one or two testers.
					preReleaseSuffix = "-Dev";
					releaseStatusName = "Dev";
					fileflags.push_back("VS_FF_PRERELEASE");
					fileflags.push_back("VS_FF_PRIVATEBUILD");
					break;

				case 'R':
					// Release.  There's no pre-release suffix and no VERSIONINFO
					// flags ofr this type.
					preReleaseSuffix = "";
					releaseStatusName = "Release";
					break;
				}
			}
			else
			{
				// It's not one of our special values, so just save this 
				// input line verbatim.
				txt = buf;
			}
		}

		// done with the file
		fclose(fp);
	}

	// Figure the fourth dotted version element for WiX purposes.
	// This encodes the release level and pre-release sequence number.
	int wixLevelNumber = 0;
	switch (releaseStatus)
	{
	case 'A':
		wixLevelNumber = 30000 + prereleaseSeqno;
		break;

	case 'B':
		wixLevelNumber = 40000 + prereleaseSeqno;
		break;

	case 'C':
		wixLevelNumber = 50000 + prereleaseSeqno;
		break;

	case 'D':
		wixLevelNumber = buildNo;
		break;

	case 'R':
		wixLevelNumber = 60000;
		break;
	}

	// open the derived .cpp file for writing
	if (fopen_s(&fp, cppFilename, "w"))
		errexit(1200, "Can't open .cpp file %s for writing\n", cppFilename);

	// write the boilerplate
	static const char *boilerplate =
		"// This file is generated mechanically during the build.  Don't edit it\n"
		"// manually.\n"
		"// \n"
		"// See VersionInfo.h for the struct definition.\n"
		"\n";
	fputs(boilerplate, fp);

	// Figure the path to VersionInfo.h relative to the .cpp file we're generating
	char relVIPath[MAX_PATH];
	PathRelativePathTo(relVIPath, cppFilename, 0, inFilename, 0);
	printf("cppFilename=%s\ninFilename=%s\nrel=%s\n", cppFilename, inFilename, relVIPath);

	// Include VersionInfo.h
	fprintf(fp, "#include \"%s\"\n\n", relVIPath);

	// generate the full version string
	char vsn[128];
	sprintf_s(vsn, "%d.%d.%d%s%s",
		versionNumber[0], versionNumber[1], versionNumber[2],
		fork.length() == 0 ? "" : ".", fork.c_str());

	// format the version string with release status
	char vsnWithStat[128];
	if (prereleaseSeqno != 0 && (releaseStatus == 'A' || releaseStatus == 'B'))
		sprintf_s(vsnWithStat, "%s (%s %d)", vsn, releaseStatusName, prereleaseSeqno);
	else if (prereleaseSeqno != 0 && releaseStatus == 'C')
		sprintf_s(vsnWithStat, "%s (%s%d)", vsn, releaseStatusName, prereleaseSeqno);
	else
		sprintf_s(vsnWithStat, "%s (%s)", vsn, releaseStatusName);

	// If there's a fork ID, it'll go into the metadata section
	// as the first element, so set up a string with a "." suffix
	// if there's anything to include here.
	std::string semFork;
	if (fork.length() != 0)
	{
		semFork.append(fork);
		semFork.append(".");
	}

	// format the semantic version string
	char semanticVer[128];
	if (releaseStatus == 'D')
	{
		// Dev release:  1.2.3-Dev.1563+Fork.20170901T1900  (1563 = build number)
		// Note that in this case, we're using the build number as the
		// pre-release version ("-Dev.1563").  This saves us the
		// trouble of manually updating the sequence number on every
		// debug build iteration but still produces a unique suffix.
		sprintf_s(semanticVer, "%d.%d.%d-Dev.%d+%s%s",
			versionNumber[0], versionNumber[1], versionNumber[2],
			buildNo, semFork.c_str(), semDate);
	}
	else if (prereleaseSeqno != 0 && (releaseStatus == 'A' || releaseStatus == 'B' || releaseStatus == 'C'))
	{
		// Alpha, Beta, RC with a non-zero sequence number:
		//
		// 1.2.3-alpha.7+Fork.1563.20170901T1900
		//
		sprintf_s(semanticVer, "%d.%d.%d%s.%d+%s%d.%s",
			versionNumber[0], versionNumber[1], versionNumber[2],
			preReleaseSuffix.c_str(), prereleaseSeqno,
			semFork.c_str(), buildNo, semDate);
	}
	else
	{
		// For dev and release builds, we don't use a pre-release
		// sequence number.  We also omit the sequence number for
		// alpha/beta/RC builds if it's 0 (there's no -alpha.0, just
		// -alpha).
		//
		// 1.2.3-alpha+Fork.1563.20170901T1900 (pre-release version)
		// 1.2.3+Fork.1563.20170901T1900       (regular release)
		//
		sprintf_s(semanticVer, "%d.%d.%d%s+%s%d.%s",
			versionNumber[0], versionNumber[1], versionNumber[2],
			preReleaseSuffix.c_str(),
			semFork.c_str(), buildNo, semDate);
	}

	// write the generated struct data
	fprintf(fp, "const VersionInfo G_VersionInfo = {\n");

	auto write = [fp](const char *comment, const char *fmt, ...)
	{
		// format the item into a buffer
		char data[512];
		va_list ap;
		va_start(ap, fmt);
		vsprintf_s(data, fmt, ap);
		va_end(ap);

		// figure the padding needed to pad it to the desired length
		const int desiredLen = 50;
		static const char spaces[] = "                                                     ";
		int padding = max(0, 50 - (int)strlen(data));

		// format the output with padding and comment text
		fprintf(fp, "    %s, %.*s// %s\n", data, padding, spaces, comment);
	};
	auto writeInt = [fp, write](const char *comment, int val) { write(comment, "%d", val); };
	auto writeStr = [fp, write](const char *comment, const char *val) { write(comment, "\"%s\"", val); };

	writeInt("Build number",                    buildNo);
	writeStr("Build date",                      date);
	writeInt("Build year",                      year);
	writeStr("Release status",                  releaseStatusName);
	writeStr("Full version string",             vsn);
	writeStr("Full version with status",        vsnWithStat);
	writeStr("Semantic versioning string",      semanticVer);
	writeStr("Semantic version, URL formatted", std::regex_replace(semanticVer, std::regex("\\+"), "%2B").c_str());
	writeStr("Copyright dates",                 copyrightDates);
	writeStr("Build GUID",                      guidStr);

	fprintf(fp, "};\n\n");

	// done with the generated .h file
	fclose(fp);

	// open the version resource file
	if (fopen_s(&fp, rcFilename, "w"))
	{
		printf("Can't open version resource .rc file %s\n", rcFilename);
		exit(2);
	}

	// for alpha, beta, and RC builds, add the pre-release sequence number
	// to the status suffix, if it's nonzero
	if ((releaseStatus == 'A' || releaseStatus == 'B' || releaseStatus == 'C')
		&& prereleaseSeqno != 0)
	{
		char buf[30];
		_snprintf_s(buf, sizeof(buf), ".%d", prereleaseSeqno);
		preReleaseSuffix.append(buf);
	}

	// Write the new VERSIONINFO resource data.
	//
	// PINALLY_CORE_VERSION_LIST = the four-part version number
	// list to embed in the executable.  This is encoded with our
	// nominal three-part version (major.minor.maintenance) in
	// the first three parts, and the build number (mod 64K)
	// in the fourth part.  The build number is included so that
	// different builds with the same nominal version number will
	// be distinguishable.  This is primarily for the sake of
	// development machines where we might have many builds
	// with the same nominal version.
	//
	// PINBALLY_CORE_VERSION_STRING - the printable version number
	// to embed in the executable.  This is encoded similarly to
	// the semantic version string:
	//
	//   major.minor.maintenance.forkid-dev.buildno+yyyymmdd-hhss
	//   major.minor.maintenance.forkid-rc5+buildno.yyyymmdd-hhss
	//
	// PINBALLY_COPYRIGHT - the full printable copyright string
	// with year(s) and author(s)
	//
	// PINBALLY_VERSIONINFO_FILEFLAGS - a set of VS_FF_xxx flags
	// suitable for the release level
	//
	fputs("// PinballY core library version resource definitions\n", fp);
	fputs("// This file was generated mechanically.  Do not edit.\n", fp);
	fputs("\n", fp);
	fprintf(fp, "#define PINBALLY_CORE_VERSION_LIST       %d,%d,%d,%d\n",
		versionNumber[0], versionNumber[1], versionNumber[2], buildNo & 0xffff);
	fprintf(fp, "#define PINBALLY_CORE_VERSION_STRING     \"%d.%d.%d%s%s%s%c%d%c%s\"\n",
		versionNumber[0], versionNumber[1], versionNumber[2],
		fork.length() == 0 ? "" : ".", fork.c_str(),
		preReleaseSuffix.c_str(), 
		releaseStatus == 'D' ? '.' : '+', buildNo, 
		releaseStatus == 'D' ? '+' : '.', date);
	fprintf(fp, "#define PINBALLY_COPYRIGHT               \"Copyright %s, %s\"\n",
		copyrightDates, copyrightOwner.c_str());
	fprintf(fp, "#define PINBALLY_VERSIONINFO_FILEFLAGS   %s\n", 
		join(fileflags, " | ", "0").c_str());
	fprintf(fp, "#define PINBALLY_BUILD_GUID              \"%s\"\n", guidStr);
	fputs("\n", fp);

	// done with the .rc file
	fclose(fp);

	// open the WXI file
	if (fopen_s(&fp, wxiFilename, "w"))
	{
		printf("Can't open Wix version header .wxi file %s\n", wxiFilename);
		exit(2);
	}

	// write the WXI information
	fputs("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n", fp);
	fputs("<Include>\n", fp);
	fputs(
		"<!--\n"
		"  PinballY setup version information.\n"
		"  This file was generated mechanically.  Do not edit.\n"
		"-->\n", fp);
	fprintf(fp, "<?define MajorVersion=\"%d\" ?>\n", versionNumber[0]);
	fprintf(fp, "<?define MinorVersion=\"%d\" ?>\n", versionNumber[1]);
	fprintf(fp, "<?define BuildVersion=\"%d\" ?>\n", versionNumber[2]);
	fprintf(fp, "<?define LevelVersion=\"%d\" ?>\n", wixLevelNumber);
	fputs("</Include>\n", fp);

	// done with the .wxi file
	fclose(fp);

	// success
    return 0;
}
