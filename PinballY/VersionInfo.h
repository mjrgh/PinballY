// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Verison data
//
#pragma once

//
// ATTENTION DEVELOPERS!  Update the following items manually
// each time you create a new public release. 
//
// For convenience, we define all of the manually updated
// items together here.  Read the documentation below for a
// full description of each item.  The guidelines below 
// explain what these all mean and how they should be update
// when creating a release.
//
#define PINBALLY_VERSION            "1.0.0"
#define PINBALLY_RELEASE_STATUS     STATUS_ALPHA
#define PINBALLY_PRERELEASE_SEQNO   13
#define PINBALLY_COPYRIGHT_OWNERS   "Michael J Roberts"
#define PINBALLY_FORK_ID            ""


// DOCUMENTATION SECTION
// 
// I.  Introduction
// 
// This file defines the application's version number, release status, and
// build information.
// 
// 
// II.  General guidelines for release version numbering
// 
// - Update PINBALLY_VERSION with a new version number when creating a new
//   official public release.
// 
// - During an alpha or beta test cycle, set PINBALLY_VERSION to the
//   version number of the UPCOMING release, and use
//   PINBALLY_PRERELEASE_SEQNO to label each release build in the test
//   cycle, starting with 1.  E.g., if you're working on features that
//   will eventually go into a future version 2.1, set PINBALLY_VERSION to
//   2.1.0, and set the pre-release sequence number to 1, 2, 3, etc as
//   you release new builds.
// 
// - Don't update the main version number string or pre-release
//   sequence number for internal development builds.  Just use the
//   automatically generated build number/date stamp to tell binaries
//   apart as needed.
// 
// - If you're creating a forked version, see the section on fork
//   version numbers below.
// 
// 
// III.  PINBALLY_VERSION
// 
// We use the common Major.Minor.Maintenance convention for version
// labels.  There's no scientific formula to this, but the software
// industry is generally pretty consistent in how these are used, so most
// people have a general sense of what the parts mean.  Here's what we
// recommend:
// 
// - The "major" version changes when there are very large changes to
//   the application, especially changes that are incompatible with past
//   versions (e.g., files from past versions can no longer be used), or 
//   that require users to learn substantial new workflows for tasks, or
//   that substantially change the look and feel of the UI.
// 
// - The "minor" version changes when substantial new functionality is
//   added, but in a way that doesn't change existing functionality very
//   much.  Older files should remain fully compatible, for example, and
//   the user experience for previously existing functionality shouldn't
//   have changed much.
// 
// - The "maintenance" version should change on essentially every public
//   release where the major and minor numbers aren't changing.  This
//   represents a release containing bugs fixes, cosmetic changes, and
//   minor improvements that don't really count as new features.  (Many
//   people call this part of the string the "patch" number, but that's
//   not a very good term for it, because "patch" has technical 
//   connotations that are a bit misleading in this context.)
// 
// 
// IV.  PINBALLY_RELEASE_STATUS
// 
// This specifies the release status - alpha, beta, etc.
// 
// In a sense, the release status is kind of moot with open source,
// because an open source project is in a perpetual "beta" mode by its
// very nature.  In commercial software development, the release level
// typically has a formal meaning, often contractual, that defines the
// type of support and warranty service that customers will receive 
// during a testing cycle.  A testing release also typicall restricts
// the number and type of customers who will have access to the release.
// None of these factors are relevant to open-source projects, where 
// anyone can generally download any testing version and where the
// software is explicitly distributed with no warranty or support.
// 
// Even so, the release status is informative and useful, to both users
// and the developers.  For users, it gives them an idea of how much
// testing a release has had and how stable it's considered to be by
// the developers.  This lets users opt in to releases according to 
// their tolerance for bugs and instability vs. their desire for the 
// latest features.  For the developers, the release status is useful 
// for gauging how much risk they should be willing to take (in terms 
// of the potential for breaking existing functionality and introducing
// new instability) when considering what sorts of changes to include
// in a particular build.
// 
// As with the version numbering scheme, there's not a precise or
// universal set of rules for the release lifecycle, even in the
// commercial software sphere.  But as with version numbering, there's
// enough commonality to industry practice that most users have fairly 
// consistent expectations about what the terms mean.  We recommend 
// using the common progression of Dev, Alpha, Beta, RC, Production:
// 
// STATUS_DEVELOPMENT
// Development = in the development stages, incomplete and possibly
//    non-functional; at this stage, the only builds made are usually the
//    ones the developers create for their own use as they work on the
//    code, and these aren't usually distributed to anyone else.
// 
// STATUS_ALPHA
// Alpha = early testing version, incomplete or not fully functional;
//    generally has limited distribution to a small group of people using
//    it specifically for testing, but can also be made public with
//    warnings about the early status.
// 
// STATUS_BETA
// Beta = late testing version, fully functional or nearly so, with
//    most or all planned features implemented; generally for public
//    distribution, with warnings that it's not a final version.
// 
// STATUS_CANDIDATE
// Candidate ("RC)" = release candidate, essentially a last beta
//    release built as a sanity check before declaring it done.  The code
//    is considered finished, and the expectation is that the identical
//    code will be officially released shortly thereafter, barring the
//    discovery of any serious bugs.  The distinction between beta and RC
//    is that an RC build is presumed "done", whereas a beta is expected
//    to be followed by at least one more test release (which might be
//    another beta or an RC).
// 
// STATUS_RELEASE
// Release = official release version, usually known in software
//    industry parlance as Production or General Availability.  We use
//    the term Release rather than Production or GA, since those have
//    plain-meaning connotations that are more suitable in a commercial
//    context than open source.
// 
// Note that use one-character codes for these internally, to allow for
// easy use in #if tests (in case we want to put some code in only for
// Beta versions, say).  The full words should be used in anything
// displayed to users.
// 
// 
// V.  PINBALLY_PRERELEASE_SEQNO
// 
// Pre-release sequence number.  This is a single number indicating the
// iteration during an alpha, beta, or RC cycle.
// 
// During a testing cycle, use the PINBALLY_VERSION number string for the
// FUTURE release that you're testing, and use the pre-release sequence
// number to version the test builds.  So when you're working on alpha
// test builds for an upcoming version 2.1, you'd set the version string
// to 2.1.0, and set the pre-release cycle number to 1 for the first
// alpha, 2 for the second alpha, and so on.
// 
// Development builds and official release builds should set this to
// zero.
// 
// In the version number displayed to users, the sequence number is
// displayed after the release status designation, to let users know
// which iteration of the current "alpha" or "beta" cycle this is.  For
// example, "1.2.3 (Alpha 3)" is the 3rd alpha release of the upcoming
// 1.2.3 version.
// 
// 
// VI.  PINBALLY_COPYRIGHT_OWNERS
// 
// The copyright owner(s).  This is the copyright owner name(s) to be
// shown in About Boxes and the like.
// 
//
// If you're forking the repository and creating your own separate
// release of the code, you can add your name (or organization name) to
// the copyright.  Current copyright laws in most countries grant
// automatic ownership of copyright to the author of a new work at the
// the moment of its creation.  So you automatically own the copyright
// to any original, new code you add to the project.  This of course
// doesn't change the copyright of the existing code that you added to.
// That creates a complication common to open-source projects with
// multiple contributors, in that it would be impractical to keep track
// of and credit authorship of every bit of code on a line-by-line
// basis.  So most open-source projects do one of two things with regard
// to new code.  First, they might ask that all contributers assign the
// copyright in new code to the original authors, so that the copyright
// status of the overall work is simple and predictable over time.  (You
// automatically own the copyright in new work you create, but that also
// gives you the right to assign it to someone else.)  You can use this
// option for any new code you add to this project, if you wish, simply
// by leaving the copyright owner name unchanged here, and publishing
// your new forked version of the project under the original copyright.
// Authors who add small changes on the order of bug fixes might find
// that option preferable.  The other option that some projects use is
// collective copyrights, where the copyright to the overall project is
// owned by all of the contributing authors together.  You can use this
// option for your forked version, if you wish, simply by adding your
// name to the copyright alongside the original authors already listed.
//
// 
// VII.  Forked Project Versioning
// 
// The "fork ID" is designed for cases where you want to make
// modifications to the software and release your own independent builds,
// outside of the "official" source repository.
// 
// For example, suppose you've been wanting a particular new feature, but
// the base code developers haven't had time to add it or just don't like
// the idea.  You decide to implement the feature yourself and create
// your your own independent release with the new feature.  This is open
// source, so you're free to do this.  You grab a copy of version 1.2.3
// of the base code, add your feature, and get it ready for release.  But
// now, what version number do you put on this release?  Do you just
// leave the version number as 1.2.3?  That's not ideal, because it would
// mean two different programs are in circulation that are both labeled
// "1.2.3".  Do you call it 1.2.4 or 1.3.0?  No, those aren't good
// either, since the base code developers will probably release their own
// future versions with those labels.
// 
// This is where the fork ID comes in.  The fork ID lets you set an extra
// string that gets added to the end of the main version number string
// when the version is displayed.  This lets users see that they're
// working with a modified 1.2.3, and lets them know exactly which
// modified version it is.
// 
// We recommend using a fork ID that consists of a few letters
// identifying you or your project, plus a one- or two-part version
// number.  Something like "MJR.1" or "PM.1.2".  Remember, this string
// will be added to the regular version string, so you don't want to make
// it too long.  The full version would then look like "1.2.3.MJR.1".
// 
// When you release under a fork ID, you should leave the main version
// number (PINBALLY_VERSION) frozen at the version of the main source code
// snapshot you're working from.  But you should still update the release
// status (PINBALLY_RELEASE_STATUS) and pre-release sequence number
// (PINBALLY_PRERELEASE_SEQNO) according to your own release cycle.  It's
// perfectly okay to create a forked version with "Beta" release status
// even if you're working from a "Release" main version, since you might
// consider your code changes to be in beta.  Likewise, you're free to
// create a "Release" version of your mod even if the main code you're
// working from is officially "Beta", if you feel you've tested the
// overall result well enough to consider it release quality.
// 
// The fork ID is most suitable for *limited* modifications, like bug fix
// versions or single-feature additions.  The key thing is that users
// will think of your version as a mod of the original rather than as a
// whole new product in its own right.  This is especially suitable if
// you intend to merge your changes back into the official base version
// at some point.  
// 
// On the other hand, if your forked version diverges so much that users
// start recognizing it as an independent project, you should probably
// consider dropping the fork ID entirely, and instead giving your
// project its own separate project name and its own independent version
// series.  If your project evolves out of something you started as a
// limited mod that you released using a fork ID, I'd recommend starting
// your independent version numbering at the next major version when you
// make the jump to a new product name: if you've been calling your mod
// release series 1.2.3.MJR.x, and you decide to rename the project to
// MikePin, jump straight to MikePin 2.0.  That will help users
// understand that your new renamed release represents an update relative
// to your earlier "mod" work.  On the other hand, if you set out from
// the start to create a whole separate project that people will never
// think of as a modified PinballY, you can simply start numbering your
// releases from 1.0.  There's no need in that case to cue users to the
// time order relative to PinballY, since the two will be on wholly
// parallel tracks.


#include <string.h>
#define STATUS_DEVELOPMENT  'D'
#define STATUS_ALPHA        'A'
#define STATUS_BETA         'B'
#define STATUS_CANDIDATE    'C'
#define STATUS_RELEASE      'R'


// VSN_BUILD_TYPE - build type string.  This is a string detailing
// the configuration (Release, Debug) and platform (x86, x64), suitable
// for displaying to the user in an About Box or the like.
#if defined(_M_IX86)
# define VSN_BUILD_TYPE_ARCH "x86"
#elif defined(_M_AMD64)
# define VSN_BUILD_TYPE_ARCH "x64"
#else
# error "Unknown build architecture - add an #elif case here for this build type"
#endif
#ifdef _DEBUG
# define VSN_BUILD_TYPE_CONFIG "Debug"
#else
# define VSN_BUILD_TYPE_CONFIG "Release"
#endif

#define VSN_BUILD_TYPE VSN_BUILD_TYPE_ARCH "/" VSN_BUILD_TYPE_CONFIG


// Derived version data variables
//
// This struct is instantiated in the file
//
//   VersionInfoUpdater/Driverd/VersionInfo.cpp
// 
// which is mechanically generated during the build process.  See the
// VersionInfoUpdater sub-project for details on the process.
//
// Note: any changes to this struct must be applied to the generated
// struct initializer in VersionInfoUpdater.cpp.
//
struct VersionInfo
{
	// The build number.  This is an integer that's incremented by one each
	// time the code is compiled.  This is basically a way to identify the
	// exact source code version that was used to create a particular
	// binary, mostly for debugging purposes (e.g., to identify the exact
	// source code version where a bug was introduced, or to allow
	// developers to rebuild an old version from source to help debug a
	// version-sensitive problem).  Note that build numbers won't
	// necessarily be sequential across builds from different developers,
	// since the sequence is generated separately within each build
	// environment.
	//
	// The build number is for comparison within a version series, so it
	// can be reset manually to zero from time to time as desired.  It
	// shouldn't be allowed to exceed 65535, because it's used in a 16-bit
	// field in the Windows VERSIONINFO resource.  I recommend resetting
	// it when starting a new development series with a new major or minor
	// version number, and otherwise letting it run continuously.
	int buildNo;

	// Build timestamp.  This is a string in YYYYMMDD-HHMM format, in GMT
	// time, generated when the last build was done.  This can be used in
	// conjunction with the build number to more or less uniquely identify
	// a particular binary build.
	const char *date;

	// Build year.  This is just the year portion of the build date
	// timestamp.  We use this mostly to generate the displayable
	// copyright date range.
	int year;

	// Release status name.  This is the human-readable rendition of the
	// release status (Dev, Alpha, Beta, RC, Release).
	const char *status;

	// The full version string.  This combines the base version string and
	// the fork version string.
	const char *fullVer;

	// Full version string plus plus the release status, formatted in 
	// human-readable format.  If this is a pre-release (alpha, beta, or RC),
	// the sequence number is also included (e.g., \"1.2.3 (Beta 4)\").
	const char *fullVerWithStat;

	// Semantic version string.  This is the version string reformatted
	// according to the conventions outlined at semver.org.  The format is
	// designed for mechanical parsing, to allow tools (such as installers
	// and package managers) to reliably determine the relative release
	// order of different versions.
	const char *semVer;

	// Semantic version string, reformatted for use in URL query
	// parameter strings.  This is the same as the regular semantic
	// version string, except that special characters are escaped
	// for URL use.
	const char *semVerUrl;

	// Copyright dates.  This is the copyright date range, in human
	// readable format (e.g., \"2017-2019\").  The range is updated
	// automatically so that it reflects the range of time from the start
	// of the project to the current build.
	const char *copyrightDates;

	// Build GUID.  This is a globally unique identifier randomly generated
	// for each build.  This can be used to identify an exact binary build
	// with certainty, which is sometimes useful when trying to track down
	// an odd bug.  It's just a randomly-generated unique number; it has
	// no internal structure or meaning other than providing uniqueness.
	const char *buildGuid;
};
extern const VersionInfo G_VersionInfo;
