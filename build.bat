@echo off
setlocal
cd /d %~dp0
if not exist build mkdir build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build --config Release || exit /b 1
if not exist plugin mkdir plugin
copy /y build\Release\poser.dll plugin\poser.dll
copy /y ..\build\Release\d3dcompiler_47.dll plugin\d3dcompiler_47.dll
echo Build OK. Copy plugin\ folder next to the game executable.
