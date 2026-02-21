@echo off
REM Build the Spotify visualizer audio bridge for Windows.
REM Requires Visual Studio Build Tools (cl.exe in PATH).
REM
REM Usage:  build.bat
REM Output: vis-capture.exe

where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: cl.exe not found. Run this from a VS Developer Command Prompt.
    echo        Or install "Build Tools for Visual Studio" from:
    echo        https://visualstudio.microsoft.com/downloads/
    exit /b 1
)

cl /O2 /EHsc /std:c++17 /I"..\common" /Fe:vis-capture.exe main.cpp ole32.lib ws2_32.lib
if %errorlevel% equ 0 (
    echo.
    echo Build successful: vis-capture.exe
    echo Run with: vis-capture.exe
) else (
    echo.
    echo Build failed.
    exit /b 1
)
