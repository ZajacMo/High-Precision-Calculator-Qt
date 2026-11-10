@echo off
rem Run all CTest suites.
rem -C Release is passed so that multi-config generators (Visual Studio) also
rem find the tests; with the Ninja single-config generator it is simply ignored.
rem Keep this file ASCII-only (see the note in msvc-env.bat).
setlocal
call "%~dp0msvc-env.bat" ctest --test-dir "%~dp0..\build" --output-on-failure -C Release || exit /b 1
