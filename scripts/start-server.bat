@echo off
rem Start the Web server on the default port (8098).
rem Keep this file ASCII-only (see the note in msvc-env.bat).
setlocal
call "%~dp0msvc-env.bat" "%~dp0..\build\hpc_server.exe" --port 8098 %*
