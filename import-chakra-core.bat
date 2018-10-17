@echo off
if %1# == # (
   echo Usage:  import-chakra-core ^<path to ChakraCore-master^>
   goto done
)

if not exist .\ChakraCore mkdir ChakraCore
if not exist .\ChakraCore\include mkdir ChakraCore\include
if not exist .\ChakraCore\x86 mkdir ChakraCore\x86
if not exist .\ChakraCore\x86\lib mkdir ChakraCore\x86\lib
if not exist .\ChakraCore\x64 mkdir ChakraCore\x64
if not exist .\ChakraCore\x64\lib mkdir ChakraCore\x64\lib

for %%i in (ChakraCore.h ChakraCommon.h ChakraCommonWindows.h ChakraDebug.h) do (
    copy /y "%1\lib\jsrt\%%i" .\ChakraCore\include\*.*
)

for %%i in (x86 x64) do (
    for %%j in (ChakraCore.lib ChakraCore.dll) do (
        copy /y "%1\Build\VcBuild\bin\%%i_release\%%j" .\ChakraCore\%%i\lib\*.*
    )
)

:done
echo.

