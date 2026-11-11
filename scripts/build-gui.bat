@echo off
rem Configure + build the optional Qt GUI target and its smoke test.
rem
rem The GUI target is OPTIONAL: the default build (scripts\build-core.bat)
rem needs no Qt at all. This script turns it on.
rem
rem Qt 6.6.2 is an MSVC build, so the GUI target is MSVC-only here. The
rem GCC verification covers the default (GUI-off) configuration.
rem
rem Keep this file ASCII-only (see the note in msvc-env.bat).
setlocal
call "%~dp0msvc-env.bat" cmake -S "%~dp0.." -B "%~dp0..\build-gui" -G Ninja -DBUILD_GUI=ON -DBUILD_TESTS=ON || exit /b 1
call "%~dp0msvc-env.bat" cmake --build "%~dp0..\build-gui" || exit /b 1
call "%~dp0msvc-env.bat" ctest --test-dir "%~dp0..\build-gui" --output-on-failure -C Release || exit /b 1
