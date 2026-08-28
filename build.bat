@echo off
setlocal
cd /d %~dp0

REM Prefer cmake + MSVC (Visual Studio 17 2022 generator).
REM NOTE: The WinGet cmake is a MinGW build; its default generator may pick
REM Ninja/g++, which cannot compile the plugin source (MSVC __try/__except).
REM So we must force the VS generator, and fall back to build_msvc.ps1.
where cmake >nul 2>&1
if %errorlevel%==0 (
  if exist build\CMakeCache.txt (
    findstr /c:"Visual Studio 17 2022" build\CMakeCache.txt >nul 2>&1 || rmdir /s /q build
  )
  if not exist build mkdir build
  cmake -S . -B build -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release || goto fallback
  cmake --build build --config Release || goto fallback
  goto ok
)
:fallback
echo [build.bat] cmake/VS generator unavailable - falling back to tools\build_msvc.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_msvc.ps1 && goto ok || exit /b 1
:ok
if not exist plugin mkdir plugin
echo Build OK. plugin\ folder contains:
echo   - poser.dll             (Endfield Poser plugin)
echo   - d3dcompiler_47.dll    (DX proxy loader, built locally)
echo Copy plugin\ folder next to the game executable.
