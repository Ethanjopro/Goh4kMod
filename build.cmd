@echo off
rem Goh4kMod build: sets up the 64-bit MSVC tools itself, so it works from any shell.
rem Output is errors and warnings only; full compiler logs land in build\obj\*.txt.
rem Usage: build.cmd           build everything for development (proxy, dxgi, drive, tests)
rem        build.cmd proxy     build build\d3d11.dll (the main proxy: wrappers, logging, config)
rem        build.cmd dxgi      build build\dxgi.dll (wraps the game's DXGI factory, hands swapchains to d3d11.dll)
rem        build.cmd drive     build build\drive\drive.exe (input + screenshots) and build\drive\imgtool.exe
rem                            (crop/side/diff); their own folder so they never pick up our d3d11.dll
rem        build.cmd tests     build build\proxytest.exe
rem        build.cmd release   build the shareable DLLs (settings next to the DLL, no local paths embedded)
rem                            and package build\release\Goh4kMod-2.1.2.zip
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
rem Output folder, object folder and extra flags for the proxy/dxgi targets; the release target overrides them.
set "OUT=build"
set "OBJ=build\obj"
set "XFLAGS="
set "LFLAGS="

if /i "%~1"=="check" goto check
if /i "%~1"=="proxy" goto proxy
if /i "%~1"=="dxgi" goto dxgi
if /i "%~1"=="drive" goto drive
if /i "%~1"=="tests" goto tests
if /i "%~1"=="release" goto release
call :proxy || exit /b 1
call :dxgi || exit /b 1
call :drive || exit /b 1
call :tests || exit /b 1
echo Build OK.
exit /b 0

:release
rem GOH4K_RELEASE: settings/log next to the DLL, native scaling on by default, diagnostics off.
rem /PDBALTPATH keeps the build machine's paths (and user name) out of the shipped DLLs.
set "OUT=build\release"
set "OBJ=build\release\obj"
set "XFLAGS=/DGOH4K_RELEASE"
set "LFLAGS=/PDBALTPATH:%%_PDB%%"
if not exist %OBJ% mkdir %OBJ%
call :proxy || exit /b 1
call :dxgi || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File src\scripts\package.ps1 || exit /b 1
exit /b 0

:proxy
> build\gen\project_dir.h echo #define GOH4K_PROJECT_DIR LR"(%ROOT%)"
if not exist %OBJ%\proxy mkdir %OBJ%\proxy
rem The link takes %OBJ%\proxy\*.obj, so objects of removed sources must not linger.
del /q %OBJ%\proxy\*.obj 2>nul
ml64 /nologo /c /Fo%OBJ%\proxy\exports.obj src\proxy\gen\exports.asm > %OBJ%\ml64.txt 2>&1
if errorlevel 1 (type %OBJ%\ml64.txt & exit /b 1)
rc /nologo /fo %OBJ%\proxy\version.res src\proxy\version.rc > %OBJ%\rc.txt 2>&1
if errorlevel 1 (type %OBJ%\rc.txt & exit /b 1)
rem MinHook (vendored, BSD-2): user32 hooks for native scaling. Third-party code, so its warnings are off.
cl /nologo /c /O2 /MT /W0 /Zi /Fo%OBJ%\proxy\ /Fd%OBJ%\proxy\vc.pdb src\third_party\minhook\src\buffer.c src\third_party\minhook\src\hook.c src\third_party\minhook\src\trampoline.c src\third_party\minhook\src\hde\hde64.c > %OBJ%\cl-minhook.txt 2>&1
if errorlevel 1 (findstr /c:"error" %OBJ%\cl-minhook.txt & exit /b 1)
cl %CFLAGS% %XFLAGS% /Isrc\proxy /Ibuild\gen /Isrc\third_party\minhook\include /Fo%OBJ%\proxy\ /Fd%OBJ%\proxy\vc.pdb src\proxy\*.cpp > %OBJ%\cl-proxy.txt 2>&1
set "RC=%ERRORLEVEL%"
findstr /c:": error" /c:": warning" /c:"fatal error" %OBJ%\cl-proxy.txt
if not "%RC%"=="0" exit /b 1
link /nologo /DLL /DEBUG %LFLAGS% /OPT:REF /OPT:ICF /OUT:%OUT%\d3d11.dll /PDB:%OUT%\d3d11.pdb /DEF:src\proxy\gen\d3d11.def %OBJ%\proxy\*.obj %OBJ%\proxy\version.res user32.lib gdi32.lib > %OBJ%\link-proxy.txt 2>&1
set "RC=%ERRORLEVEL%"
findstr /v /c:"Creating library" %OBJ%\link-proxy.txt
if not "%RC%"=="0" exit /b 1
echo built %OUT%\d3d11.dll
exit /b 0

:dxgi
if not exist %OBJ%\dxgi mkdir %OBJ%\dxgi
ml64 /nologo /c /Fo%OBJ%\dxgi\exports.obj src\dxgi\gen\exports.asm > %OBJ%\ml64-dxgi.txt 2>&1
if errorlevel 1 (type %OBJ%\ml64-dxgi.txt & exit /b 1)
rc /nologo /fo %OBJ%\dxgi\version.res src\dxgi\version.rc > %OBJ%\rc-dxgi.txt 2>&1
if errorlevel 1 (type %OBJ%\rc-dxgi.txt & exit /b 1)
cl %CFLAGS% %XFLAGS% /Fo%OBJ%\dxgi\ /Fd%OBJ%\dxgi\vc.pdb src\dxgi\*.cpp > %OBJ%\cl-dxgi.txt 2>&1
set "RC=%ERRORLEVEL%"
findstr /c:": error" /c:": warning" /c:"fatal error" %OBJ%\cl-dxgi.txt
if not "%RC%"=="0" exit /b 1
link /nologo /DLL /DEBUG %LFLAGS% /OPT:REF /OPT:ICF /OUT:%OUT%\dxgi.dll /PDB:%OUT%\dxgi.pdb /DEF:src\dxgi\gen\dxgi.def %OBJ%\dxgi\*.obj %OBJ%\dxgi\version.res > %OBJ%\link-dxgi.txt 2>&1
set "RC=%ERRORLEVEL%"
findstr /v /c:"Creating library" %OBJ%\link-dxgi.txt
if not "%RC%"=="0" exit /b 1
echo built %OUT%\dxgi.dll
exit /b 0

:drive
if not exist build\obj\drive mkdir build\obj\drive
cl %CFLAGS% /Fobuild\obj\drive\ /Fdbuild\obj\drive\vc.pdb src\drive\drive.cpp src\drive\imgtool.cpp > build\obj\cl-drive.txt 2>&1
set "RC=%ERRORLEVEL%"
findstr /c:": error" /c:": warning" /c:"fatal error" build\obj\cl-drive.txt
if not "%RC%"=="0" exit /b 1
if not exist build\drive mkdir build\drive
link /nologo /DEBUG /OUT:build\drive\drive.exe /PDB:build\drive\drive.pdb build\obj\drive\drive.obj user32.lib gdi32.lib d3d11.lib dxgi.lib ole32.lib windowscodecs.lib > build\obj\link-drive.txt 2>&1
set "RC=%ERRORLEVEL%"
type build\obj\link-drive.txt
if not "%RC%"=="0" exit /b 1
link /nologo /DEBUG /OUT:build\drive\imgtool.exe /PDB:build\drive\imgtool.pdb build\obj\drive\imgtool.obj > build\obj\link-imgtool.txt 2>&1
set "RC=%ERRORLEVEL%"
type build\obj\link-imgtool.txt
if not "%RC%"=="0" exit /b 1
echo built build\drive\drive.exe and imgtool.exe
exit /b 0

:tests
if not exist build\obj\tests mkdir build\obj\tests
cl %CFLAGS% /Fobuild\obj\tests\ /Fdbuild\obj\tests\vc.pdb src\tests\proxytest.cpp > build\obj\cl-tests.txt 2>&1
set "RC=%ERRORLEVEL%"
findstr /c:": error" /c:": warning" /c:"fatal error" build\obj\cl-tests.txt
if not "%RC%"=="0" exit /b 1
link /nologo /DEBUG /OUT:build\proxytest.exe /PDB:build\proxytest.pdb build\obj\tests\*.obj user32.lib gdi32.lib > build\obj\link-tests.txt 2>&1
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
