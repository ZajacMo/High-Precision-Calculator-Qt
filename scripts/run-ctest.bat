@echo off
rem Run all CTest suites.
setlocal
call "%~dp0msvc-env.bat" ctest --test-dir "%~dp0..\build" --output-on-failure || exit /b 1
