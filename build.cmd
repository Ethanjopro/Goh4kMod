@echo off
rem Goh4kMod build: sets up the 64-bit MSVC tools itself, so it works from any shell.
rem Usage: build.cmd          build everything under src\ (nothing yet; the proxy comes in M1)
rem        build.cmd check    compile a tiny test program and confirm the output is x64
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo Visual Studio C++ tools not found.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
if /i "%~1"=="check" goto check
echo Nothing to build yet (the proxy arrives in milestone 1).
exit /b 0

:check
if not exist build\check mkdir build\check
> build\check\hello.c echo int main(void){return 0;}
cl /nologo /O2 build\check\hello.c /Fobuild\check\ /Febuild\check\hello.exe >nul || exit /b 1
dumpbin /nologo /headers build\check\hello.exe | findstr /c:"machine (x64)" >nul || (echo NOT x64 & exit /b 1)
echo Toolchain OK: cl builds x64.
exit /b 0
