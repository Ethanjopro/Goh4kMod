@echo off
rem Goh4kMod uninstaller, shipped in the zip. It lives in the game's binaries\x64 next to the mod's DLLs.
rem Removes d3d11.dll and dxgi.dll only if they are Goh4kMod builds, plus the mod's settings, log and
rem diagnostics folder, then deletes itself. /nopause skips the pauses (used by test-gamefolder.ps1).
setlocal
title Goh4kMod uninstaller
cd /d "%~dp0"
set "FAILED="
set "WAIT=pause"
if /i "%~1"=="/nopause" set "WAIT=rem"
if not exist "call_to_arms.exe" (
  echo This file has to be in the game's binaries\x64 folder, next to call_to_arms.exe.
  echo Nothing was removed.
  goto fail
)
tasklist /fi "imagename eq call_to_arms.exe" 2>nul | find /i "call_to_arms.exe" >nul
if not errorlevel 1 (
  echo The game is running. Close it, then run this again.
  echo Nothing was removed.
  goto fail
)
call :dll d3d11.dll
call :dll dxgi.dll
call :file goh4k.ini
call :file goh4k.log
if exist "goh4k-diagnostics\" (
  rd /s /q "goh4k-diagnostics" >nul 2>&1
  if exist "goh4k-diagnostics\" (echo Could not remove goh4k-diagnostics& set "FAILED=1") else echo Removed goh4k-diagnostics
)
echo.
if defined FAILED (
  echo Some files could not be removed. Close the game and run this again.
  goto fail
)
echo Goh4kMod is uninstalled.
rem Delete this file, then pause. (goto) leaves the batch context first, so cmd doesn't try to read on
rem from a deleted file; the rest of the line was already parsed and still runs.
(goto) 2>nul & del "%~f0" & %WAIT%

:fail
%WAIT%
exit /b 1

:dll
if not exist "%~1" exit /b 0
powershell -NoProfile -Command "exit [int]((Get-Item -LiteralPath '%~1').VersionInfo.ProductName -ne 'Goh4kMod')" >nul 2>&1
if errorlevel 1 (
  echo %~1 is not part of Goh4kMod, left in place.
  exit /b 0
)
:file
if not exist "%~1" exit /b 0
del /f /q "%~1" >nul 2>&1
if exist "%~1" (echo Could not remove %~1& set "FAILED=1") else echo Removed %~1
exit /b 0
