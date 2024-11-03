# This file is part of PinballY
# Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
#
# NMake makefile to build the derived VersionInfo files
#
# A surprisingly common question on the Internet is "How do you
# update version numbers automatically on each build with VS?"  Even
# more surprising is the answer: there's really not a good way to do
# this.  At least, there's not a way to do it *properly*, which most
# people define like this:
#
# 1. Every time we rebuild the WHOLE SYSTEM or ANY SUBSET of it, 
# the build number data gets updated automatically.
#
# 2. BUT the build number update occurs ONLY IF there's something 
# that actually needs to be rebuilt.
#
# 3. The build number update itself must not itself trigger any 
# rebuilding anything else that didn't need rebuilding anyway.
#
# 4. All outputs (that is, all separate .EXEs) of a given build end
# up with the same version data.
# 
#
# Some of this is possible with the VS IDE build system, but not all
# of it.  One solution is to use a pre-build event in a root project
# that all of the other projects depend upon, but that fails
# requirement 1 ("update on any partial rebuild"), because the root
# project won't fire its pre-build event unless it contains something
# that needs to be rebuilt, which it usually won't.  Another solution
# is to use a "makefile" project to basically force the rebuild every
# time, but that fails requirements 2 ("no unnecessary updates") and
# 3 ("don't trigger unnecessary rebuilds of other stuff").  Yet
# another approach is to use separate pre-build events in every
# sub-project, which at least ensures that the update happens when
# any portion is built, but fails requirement 4 ("consistent version
# numbers across sub-projects").  
#
# Our solution here meets all of the requirements as follows.  We use
# the basic approach of putting a pre-build event in each
# sub-project, but rather than actually firing off the update there,
# we instead invoke nmake on this makefile.  The makefile checks
# dependencies across the system to see if any project source code
# has been touched since the last version update.  If not, we do
# nothing.  If so, we do the version update. 
#
# This satisfies requirement 1 ("update on partial rebuild") by
# triggering the pre-build step from any sub-project.  This also
# satisfies requirement 2 ("no unnecessary rebuilds"), in that
# VS won't even invoke the makefile unless it detects something 
# that needs to be rebuilt.  We meet requirement 3 by being careful
# that everyone who depends on the version updater outputs calls
# the makefile as part of its pre-build.  And requirement 4 is why
# this is done in a makefile rather than a direct command.  The
# makefile makes the version update conditional on the dependency
# check on the rest of the system's sources, so a build that invokes
# us multiple times will only end up doing the version update on the
# first invocation of the build.  The source files shouldn't ever
# change in the course of the build itself, so our dependency check
# on a second or third invocation on one build should find that
# all of the inputs are now older than our output.
#

BinDir = .\bin
ObjDir = $(BinDir)\$(Configuration)
GenDir = .\Generated

all: directories $(ObjDir)\VersionInfo.obj

directories:
    @if not exist $(BinDir) mkdir $(BinDir)
    @if not exist $(GenDir) mkdir $(GenDir)
    @if not exist $(ObjDir) mkdir $(ObjDir)

$(ObjDir)\VersionInfo.obj: $(GenDir)\VersionInfo.cpp
    cl -MT -Zl -c -Fo$@ $**

$(GenDir)\VersionInfo.cpp $(GenDir)\VersionInfo.rc $(GenDir)\VersionInfo.wxi $(GenDir)\ManifestVersionInfo.xml: \
    ..\VersionHistory.txt ..\PinballY\*.cpp ..\PinballY\*.h ..\PinballY\*.rc \
    ..\OptionsDialog\*.cpp ..\OptionsDialog\*.h ..\OptionsDialog\*.rc
	$(BinDir)\VersionInfoUpdater -in .\..\PinballY\VersionInfo.h -cpp $(GenDir)\VersionInfo.cpp -rc $(GenDir)\VersionInfo.rc -wxi $(GenDir)\VersionInfo.wxi -manifest $(GenDir)\ManifestVersionInfo.xml
