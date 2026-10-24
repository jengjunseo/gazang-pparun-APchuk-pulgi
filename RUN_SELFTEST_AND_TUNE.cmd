@echo off
setlocal
set "HX_DATA_HOME=%~dp0data"
if not exist "%HX_DATA_HOME%" mkdir "%HX_DATA_HOME%"
"%~dp0release\hx.exe" selftest > "%HX_DATA_HOME%\selftest.log" 2>&1
if errorlevel 1 (
 type "%HX_DATA_HOME%\selftest.log"
 pause
 exit /b 1
)
"%~dp0release\hx.exe" tune --quick
pause
