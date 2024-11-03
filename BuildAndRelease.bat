@echo off
rem  PinballY Release Builder
rem
rem  Run from a Visual Studio CMD prompt window
rem

rem Clean old builds
echo ^>^>^> Removing old builds
del VersionInfoUpdater\Generated\VersionInfo.cpp
msbuild PinballY.sln -t:Clean -p:Configuration=Release;Platform=x86 -v:q -nologo
if errorlevel 1 goto abort
msbuild PinballY.sln -t:Clean -p:Configuration=Release;Platform=x64 -v:q -nologo
if errorlevel 1 goto abort

rem Build the release configurations
echo.
echo ^>^>^> Building Release^|x86
msbuild PinballY.sln -t:Build -p:Configuration=Release;Platform=x86 -v:q -nologo
if errorlevel 1 goto abort

echo.
echo ^>^>^> Building Release^|x64
msbuild PinballY.sln -t:Build -p:Configuration=Release;Platform=x64 -v:q -nologo
if errorlevel 1 goto abort


rem Run the release ZIP builder
echo.
echo ^>^>^> Building release packages
call .\release.bat

echo.
echo ^>^>^> Release completed
goto EOF

:abort
echo MSBUILD exited with error - aborted

:EOF
