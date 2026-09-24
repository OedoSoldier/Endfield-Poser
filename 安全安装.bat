@echo off
setlocal EnableExtensions DisableDelayedExpansion
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\deploy.ps1" %*
set "POSER_EXIT=%errorlevel%"
if not "%~1"=="" exit /b %POSER_EXIT%
pause
exit /b %POSER_EXIT%
