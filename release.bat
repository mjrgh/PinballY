@rem  PinballY release script
@rem
@rem  Run a full clean build of the release version in Visual studio
@rem  before running this script.  See Notes\Release Checklist.txt.
@rem
@echo off

rem  Get the date in YYYYMMDD format, to use as a filename suffix
rem  for the release ZIP files
set SysDateTime=
for /f "skip=1" %%i in ('wmic os get localdatetime') do if not defined SysDateTime set SysDateTime=%%i
set ReleaseDate=%SysDateTime:~0,8%

rem  Form the names of the release ZIP files.  The base file is
rem  the full archive with everything needed for a fresh install.
rem  The "Min" file has just the PinballY files, without any of
rem  the separate third-party dependencies; users with a prior
rem  version installed can use this to install an update without
rem  needlessly re-copying all of the dependencies, which don't
rem  usually change from release to release.
set ReleaseZipFull=Builds\PinballY-%ReleaseDate%.zip
set ReleaseZipMin=Builds\PinballY-Min-%ReleaseDate%.zip

rem  Build the full ZIP.  Note that this includes BOTH the "Full"
rem  and "Min" manifest files, since the "Full" manifest is really
rem  the delta between Min and Full.  (The manifest files are just
rem  listings of the files to include in the ZIPs, one per line.
rem  Each line can also contain zip option flags as needed.)
if exist %ReleaseZipFull% del %ReleaseZipFull%
for /f "delims=" %%i in (ReleaseManifestMin.txt) do zip %ReleaseZipFull% %%i
for /f "delims=" %%i in (ReleaseManifestFull.txt) do zip %ReleaseZipFull% %%i

rem  Build the "Min" ZIP.  This uses only the "Min" manifest.
if exist %ReleaseZipMin% del %ReleaseZipMin%
for /f "delims=" %%i in (ReleaseManifestMin.txt) do zip %ReleaseZipMin% %%i
