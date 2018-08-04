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

rem  64-bit versions
set ReleaseZipFull64=Builds\PinballY-64bit-%ReleaseDate%.zip
set ReleaseZipMin64=Builds\PinballY-64-bit-Min-%ReleaseDate%.zip


rem  Build the full ZIP.  Note that this includes BOTH the "Full"
rem  and "Min" manifest files, since the "Full" manifest is really
rem  the delta between Min and Full.  (The manifest files are just
rem  listings of the files to include in the ZIPs, one per line.
rem  Each line can also contain zip option flags as needed.)
if exist %ReleaseZipFull% del %ReleaseZipFull%
for /f "delims=" %%i in (ReleaseManifestExe32.txt) do zip %ReleaseZipFull% %%i
for /f "delims=" %%i in (ReleaseManifestBase.txt) do zip %ReleaseZipFull% %%i
for /f "delims=" %%i in (ReleaseManifestVLC32.txt) do zip %ReleaseZipFull% %%i
for /f "delims=" %%i in (ReleaseManifestFull.txt) do zip %ReleaseZipFull% %%i

rem  Build the "Min" ZIP.  This uses only the "Min" manifest.
if exist %ReleaseZipMin% del %ReleaseZipMin%
for /f "delims=" %%i in (ReleaseManifestExe32.txt) do zip %ReleaseZipMin% %%i
for /f "delims=" %%i in (ReleaseManifestBase.txt) do zip %ReleaseZipMin% %%i


rem  *** Build the 64-bit releases ***
set ReleasePlatformDir=x64\release
set ReleaseVLCDir=VLC64

rem  Build the Full 64-bit ZIP
if exist %ReleaseZipFull64% del %ReleaseZipFull64%
for /f "delims=" %%i in (ReleaseManifestExe64.txt) do zip %ReleaseZipFull64% %%i
for /f "delims=" %%i in (ReleaseManifestBase.txt) do zip %ReleaseZipFull64% %%i
for /f "delims=" %%i in (ReleaseManifestVLC64.txt) do zip %ReleaseZipFull64% %%i
for /f "delims=" %%i in (ReleaseManifestFull.txt) do zip %ReleaseZipFull64% %%i

rem  Build the "Min" 64-bit ZIP.  This uses only the "Min" manifest.
if exist %ReleaseZipMin64% del %ReleaseZipMin64%
for /f "delims=" %%i in (ReleaseManifestExe64.txt) do zip %ReleaseZipMin64% %%i
for /f "delims=" %%i in (ReleaseManifestBase.txt) do zip %ReleaseZipMin64% %%i


rem  Copy the MSI installers
copy "WixSetup\bin\x86-Release\PinballY Setup.msi" Builds\PinballY-%ReleaseDate%.msi
copy "WixSetup\bin\x64-Release\PinballY Setup.msi" Builds\PinballY-64bit-%ReleaseDate%.msi
