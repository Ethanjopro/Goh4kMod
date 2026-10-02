@echo off
rem Goh4kMod: remove the mod from the game's binaries\x64 and restore any stock file it replaced.
rem Then checks the folder against the baseline taken before the first deploy and records it in LOG.md.
rem Run this before playing multiplayer. Double-click is fine; pass /nopause when scripting.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0src\scripts\uninstall.ps1"
set "RC=%ERRORLEVEL%"
if /i not "%~1"=="/nopause" pause
exit /b %RC%
