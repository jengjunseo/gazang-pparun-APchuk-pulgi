@echo off
setlocal
set "HX_DATA_HOME=%~dp0data"
if not exist "%~dp0release\HyperExtractGUI.exe" (
 echo Missing release\HyperExtractGUI.exe. Extract the complete delivery ZIP.
 pause
 exit /b 1
)
start "" "%~dp0release\HyperExtractGUI.exe"
