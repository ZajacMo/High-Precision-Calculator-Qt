@echo off
rem Configure the MSVC/Qt toolchain, then run the rest of the arguments.
rem Usage: scripts\msvc-env.bat <command> [args...]

set "VCT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKV=10.0.26100.0"
set "QTDIR=C:\Qt\6.6.2\msvc2019_64"

set "INCLUDE=%VCT%\include;%SDK%\Include\%SDKV%\ucrt;%SDK%\Include\%SDKV%\um;%SDK%\Include\%SDKV%\shared;%SDK%\Include\%SDKV%\winrt"
set "LIB=%VCT%\lib\x64;%SDK%\Lib\%SDKV%\ucrt\x64;%SDK%\Lib\%SDKV%\um\x64"
set "PATH=%VCT%\bin\Hostx64\x64;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%SDK%\bin\%SDKV%\x64;%QTDIR%\bin;%PATH%"
set "CMAKE_PREFIX_PATH=%QTDIR%"

%*
