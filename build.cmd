@echo off
setlocal

where pwsh >nul 2>&1
if errorlevel 1 (
    echo PowerShell 7 is required. Install it from https://aka.ms/powershell-release
    pause
    exit /b 1
)

pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build_for_contributors.ps1" %*
set "KF2_BUILD_EXIT=%errorlevel%"
if not "%KF2_BUILD_EXIT%"=="0" pause
exit /b %KF2_BUILD_EXIT%
