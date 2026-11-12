@echo off
rem Configure + build the optional Qt GUI target and its smoke test.
rem
rem The GUI target is OPTIONAL: the default build (scripts\build-core.bat)
rem needs no Qt at all. This script turns it on.
rem
rem Qt 6.6.2 is installed twice: msvc2019_64 (MSVC) and mingw_64 (MinGW
rem g++ 11.2). The GUI target builds and passes its smoke test under BOTH.
rem This script drives the MSVC one; the MinGW one is configured the same
rem way but with -DCMAKE_PREFIX_PATH=C:/Qt/6.6.2/mingw_64 and
rem -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1120_64/bin/g++.exe
rem (the Qt build must MATCH the compiler).
rem
rem The GCC verification also covers the default (GUI-off) configuration.
rem
rem Keep this file ASCII-only (see the note in msvc-env.bat).
setlocal
call "%~dp0msvc-env.bat" cmake -S "%~dp0.." -B "%~dp0..\build-gui" -G Ninja -DBUILD_GUI=ON -DBUILD_TESTS=ON || exit /b 1
call "%~dp0msvc-env.bat" cmake --build "%~dp0..\build-gui" || exit /b 1
call "%~dp0msvc-env.bat" ctest --test-dir "%~dp0..\build-gui" --output-on-failure -C Release || exit /b 1
