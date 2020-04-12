@echo off
if %1# == # goto usageExit
if %2# == # goto usageExit

if not exist .\ChakraCore mkdir ChakraCore
if not exist .\ChakraCore\include mkdir ChakraCore\include
if not exist .\ChakraCore\x86 mkdir ChakraCore\x86
if not exist .\ChakraCore\x86\lib mkdir ChakraCore\x86\lib
if not exist .\ChakraCore\x64 mkdir ChakraCore\x64
if not exist .\ChakraCore\x64\lib mkdir ChakraCore\x64\lib
if not exist .\ChakraCore\x64\debug mkdir ChakraCore\x64\debug
if not exist .\ChakraCore\x64\release mkdir ChakraCore\x64\release
if not exist .\ChakraCore\win32 mkdir ChakraCore\win32
if not exist .\ChakraCore\win32\debug mkdir ChakraCore\win32\debug
if not exist .\ChakraCore\win32\release mkdir ChakraCore\win32\release
if not exist .\ChakraCore\libboost mkdir ChakraCore\libboost

for %%i in (ChakraCore.h ChakraCommon.h ChakraCommonWindows.h ChakraDebug.h) do (
    copy /y "%~1\lib\jsrt\%%i" .\ChakraCore\include\*.*
)

for %%i in (x86 x64) do (
    for %%j in (ChakraCore.lib ChakraCore.dll) do (
        copy /y "%~1\Build\VcBuild\bin\%%i_release\%%j" .\ChakraCore\%%i\lib\*.*
    )
)

for %%i in (Debugger.Service\ChakraDebugService.h Debugger.ProtocolHandler\ChakraDebugProtocolHandler.h) do (
    copy /y "%~2\lib\%%i" .\ChakraCore\include\*.*
)

for %%i in (win32 x64) do (
    for %%j in (Debug Release) do (
        for %%k in (ChakraCore.Debugger.Service.lib ChakraCore.Debugger.ProtocolHandler.lib ChakraCore.Debugger.Protocol.lib) do (
            copy /y "%~2\build\bin\%%i\%%j\%%k" .\ChakraCore\%%i\%%j\%%k
        )
    )
)

copy "%~2\packages\boost_date_time-vc141.1.68.0.0\lib\native\*.lib" .\ChakraCore\libboost\*.*

goto done

:usageExit
echo Usage:  import-chakra-core ^<path to ChakraCore^>  ^<path to ChakraCore-Debugger^>

:done
echo.

