@echo off
setlocal
powershell.exe -NoProfile -File "%~dp0tools\bootstrap_windows.ps1"
set "HX_BUILD_RESULT=%ERRORLEVEL%"
if not "%HX_BUILD_RESULT%"=="0" pause
exit /b %HX_BUILD_RESULT%
