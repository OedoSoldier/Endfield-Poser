@echo off
setlocal
cd /d %~dp0
if not exist build mkdir build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build --config Release || exit /b 1
if not exist plugin mkdir plugin
echo Build OK. plugin\ folder contains:
echo   - poser.dll             (Endfield Poser plugin)
echo   - d3dcompiler_47.dll    (DX proxy loader, built locally)
echo Copy plugin\ folder next to the game executable.
