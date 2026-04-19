@echo off
setlocal

echo Finding Visual Studio...

:: Method 1: Use vswhere for VS 2022/2019/2017
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath 2^>nul`) do (
    set "VS_PATH=%%i"
    goto :found
)

:: Method 2: Find VS 2022
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
    goto :found
)
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
    goto :found
)
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
    goto :found
)

:: Method 3: Find VS 2019
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community"
    goto :found
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional"
    goto :found
)

:: Method 4: Find VS 2017
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community"
    goto :found
)

echo ERROR: Visual Studio not found. Please install VS 2017 or later.
pause
exit /b 1

:found
echo Found Visual Studio: %VS_PATH%
echo.

:: Setup VS environment
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

:: Compile resource file
echo Compiling resources...
rc /fo main.res main.rc
if %errorlevel% neq 0 (
    echo ERROR: Resource compilation failed!
    echo Please ensure main.rc and xunjieso.ico exist.
    pause
    exit /b 1
)

:: Compile
echo Compiling XunJieSo.exe ...
cl /EHsc /O2 /GL /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN main.cpp main.res /Fe:XunJieSo.exe /link xunjieso.lib comctl32.lib user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib gdiplus.lib /SUBSYSTEM:WINDOWS /LTCG /MANIFEST:EMBED

if %errorlevel% equ 0 (
    echo.
    echo Build succeeded!
    echo Output: XunJieSo.exe
    
    :: Clean up
    del /Q *.obj *.res 2>nul
    
    :: Run
    echo.
    echo Press any key to run...
    pause >nul
    start XunJieSo.exe
) else (
    echo.
    echo Build failed!
    pause
)

exit /b %errorlevel%