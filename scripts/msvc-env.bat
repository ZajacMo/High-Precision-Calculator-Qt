@echo off
rem Configure the MSVC/Qt toolchain, then run the rest of the arguments.
rem Usage: scripts\msvc-env.bat <command> [args...]

rem NOTE: Keep this file ASCII-only. The .bat is parsed under the system ANSI
rem code page (936 on this machine), so non-ASCII characters in comments can
rem corrupt parsing and break the toolchain setup entirely.
rem
rem When the toolchain is upgraded, update these four variables together:
rem   VCT   - MSVC toolset directory
rem   SDK   - Windows Kits root
rem   SDKV  - Windows Kits version; must match the SDK\bin\%SDKV%\x64 directory
rem   QTDIR - Qt installation used by the optional GUI target
rem That SDK bin directory provides rc.exe and mt.exe; without them CMake's
rem try-compile fails.

set "VCT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKV=10.0.26100.0"
set "QTDIR=C:\Qt\6.6.2\msvc2019_64"

set "INCLUDE=%VCT%\include;%SDK%\Include\%SDKV%\ucrt;%SDK%\Include\%SDKV%\um;%SDK%\Include\%SDKV%\shared;%SDK%\Include\%SDKV%\winrt"
set "LIB=%VCT%\lib\x64;%SDK%\Lib\%SDKV%\ucrt\x64;%SDK%\Lib\%SDKV%\um\x64"
set "PATH=%VCT%\bin\Hostx64\x64;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%SDK%\bin\%SDKV%\x64;%QTDIR%\bin;%PATH%"
set "CMAKE_PREFIX_PATH=%QTDIR%"

%*
