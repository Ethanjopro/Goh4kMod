@echo off
rem Goh4kMod build: sets up the 64-bit MSVC tools itself, so it works from any shell.
rem Output is errors and warnings only; full compiler logs land in build\obj\*.txt.
rem Usage: build.cmd           build everything (proxy, drive, tests)
rem        build.cmd proxy     build build\d3d11.dll
rem        build.cmd drive     build build\drive\drive.exe (input + screenshot helper; its own folder
rem                            so it never picks up our d3d11.dll)
rem        build.cmd tests     build build\proxytest.exe
rem        build.cmd check     compile a tiny test program and confirm the output is x64
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
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
if not exist build\obj mkdir build\obj
if not exist build\gen mkdir build\gen
set "CFLAGS=/nologo /c /std:c++17 /O2 /MT /W4 /EHsc /Zi /DUNICODE /D_UNICODE"

if /i "%~1"=="check" goto check
if /i "%~1"=="proxy" goto proxy
if /i "%~1"=="drive" goto drive
if /i "%~1"=="tests" goto tests
call :proxy || exit /b 1
call :drive || exit /b 1
call :tests || exit /b 1
echo Build OK.
exit /b 0

:proxy
> build\gen\project_dir.h echo #define GOH4K_PROJECT_DIR LR"(%ROOT%)"
if not exist build\obj\proxy mkdir build\obj\proxy
ml64 /nologo /c /Fobuild\obj\proxy\exports.obj src\proxy\gen\exports.asm > build\obj\ml64.txt 2>&1
if errorlevel 1 (type build\obj\ml64.txt & exit /b 1)
rc /nologo /fo build\obj\proxy\version.res src\proxy\version.rc > build\obj\rc.txt 2>&1
if errorlevel 1 (type build\obj\rc.txt & exit /b 1)
cl %CFLAGS% /Isrc\proxy /Ibuild\gen /Fobuild\obj\proxy\ /Fdbuild\obj\proxy\vc.pdb src\proxy\*.cpp > build\obj\cl-proxy.txt 2>&1
set "RC=%ERRORLEVEL%"
findstr /c:": error" /c:": warning" /c:"fatal error" build\obj\cl-proxy.txt
if not "%RC%"=="0" exit /b 1
link /nologo /DLL /DEBUG /OPT:REF /OPT:ICF /OUT:build\d3d11.dll /PDB:build\d3d11.pdb /DEF:src\proxy\gen\d3d11.def build\obj\proxy\*.obj build\obj\proxy\version.res user32.lib > build\obj\link-proxy.txt 2>&1
set "RC=%ERRORLEVEL%"
findstr /v /c:"Creating library" build\obj\link-proxy.txt
if not "%RC%"=="0" exit /b 1
echo built build\d3d11.dll
exit /b 0

:drive
if not exist build\obj\drive mkdir build\obj\drive
cl %CFLAGS% /Fobuild\obj\drive\ /Fdbuild\obj\drive\vc.pdb src\drive\drive.cpp > build\obj\cl-drive.txt 2>&1
set "RC=%ERRORLEVEL%"
findstr /c:": error" /c:": warning" /c:"fatal error" build\obj\cl-drive.txt
if not "%RC%"=="0" exit /b 1
if not exist build\drive mkdir build\drive
link /nologo /DEBUG /OUT:build\drive\drive.exe /PDB:build\drive\drive.pdb build\obj\drive\*.obj user32.lib d3d11.lib dxgi.lib ole32.lib windowscodecs.lib > build\obj\link-drive.txt 2>&1
set "RC=%ERRORLEVEL%"
type build\obj\link-drive.txt
if not "%RC%"=="0" exit /b 1
echo built build\drive\drive.exe
exit /b 0

:tests
if not exist build\obj\tests mkdir build\obj\tests
cl %CFLAGS% /Fobuild\obj\tests\ /Fdbuild\obj\tests\vc.pdb src\tests\proxytest.cpp > build\obj\cl-tests.txt 2>&1
set "RC=%ERRORLEVEL%"
findstr /c:": error" /c:": warning" /c:"fatal error" build\obj\cl-tests.txt
if not "%RC%"=="0" exit /b 1
link /nologo /DEBUG /OUT:build\proxytest.exe /PDB:build\proxytest.pdb build\obj\tests\*.obj user32.lib > build\obj\link-tests.txt 2>&1
set "RC=%ERRORLEVEL%"
type build\obj\link-tests.txt
if not "%RC%"=="0" exit /b 1
echo built build\proxytest.exe
exit /b 0

:check
if not exist build\check mkdir build\check
> build\check\hello.c echo int main(void){return 0;}
cl /nologo /O2 build\check\hello.c /Fobuild\check\ /Febuild\check\hello.exe >nul || exit /b 1
dumpbin /nologo /headers build\check\hello.exe | findstr /c:"machine (x64)" >nul || (echo NOT x64 & exit /b 1)
echo Toolchain OK: cl builds x64.
exit /b 0
