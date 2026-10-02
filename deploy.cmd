@echo off
rem Goh4kMod: copy the proxy into the game's binaries\x64 (path in .claude\game-bin.txt).
rem Backs up any stock file it replaces to scratch\stock-backup, records hashes in scratch\deployed.txt and
rem the change in LOG.md. Refuses to run while the game is running. Build first with build.cmd.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0src\scripts\deploy.ps1"
exit /b %ERRORLEVEL%
