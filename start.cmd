@echo off
rem Goh4kMod: start Claude Code in this folder with the 64-bit MSVC tools set up.
rem Usage: start.cmd                      (normal session)
rem        start.cmd --permission-mode plan  (planning session)
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo Visual Studio C++ tools not found. Run setup-goh4kmod.ps1 again.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=%USERPROFILE%\.local\bin;%PATH%"
cd /d "%~dp0"
claude %*
