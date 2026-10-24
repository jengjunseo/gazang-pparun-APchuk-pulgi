@echo off
setlocal
cd /d "%~dp0.."
where cmake.exe >nul 2>&1
if errorlevel 1 (
  echo CMake is required. Install it or run tools\bootstrap_windows.ps1 to obtain portable build tools.
  exit /b 1
)
where cl.exe >nul 2>&1
if errorlevel 1 (
  echo MSVC cl.exe not found. Run from x64 Native Tools Command Prompt for Visual Studio.
  echo Alternatively: run tools\bootstrap_windows.ps1 to use portable Zig and CMake.
  exit /b 1
)
cmake -S . -B build\win-msvc -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DHX_PREFER_SYSTEM_LIBS=OFF -DHX_ALLOW_FETCH=ON
if errorlevel 1 exit /b 1
cmake --build build\win-msvc --config Release --parallel 4
if errorlevel 1 exit /b 1
echo Generated build\win-msvc\hx.exe and hx-tune.exe
exit /b 0
