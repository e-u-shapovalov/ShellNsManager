@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" ( echo [ERR] vcvars64 not found & exit /b 1 )
call "%VCVARS%" >nul
if not exist build mkdir build
echo [rc] resources...
rc /nologo /I src /fo build\resource.res src\resource.rc || ( echo [ERR] rc failed & exit /b 1 )
set "SRCS="
for %%f in (src\*.cpp) do set "SRCS=!SRCS! %%f"
echo [cl] compiling:!SRCS!
cl /nologo /W4 /utf-8 /EHsc /O2 /std:c++17 /DUNICODE /D_UNICODE /Fo:build\ /Fe:ShellNsManager.exe ^
   !SRCS! build\resource.res ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib shlwapi.lib propsys.lib ^
   || ( echo [ERR] cl/link failed & exit /b 1 )
echo [OK] ShellNsManager.exe
