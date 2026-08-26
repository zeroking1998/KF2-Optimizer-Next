@echo off
setlocal

where winget >nul 2>&1
if errorlevel 1 (
    echo Windows Package Manager is required. Install App Installer from Microsoft Store.
    pause
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install_requirements.ps1"
set "KF2_SETUP_EXIT=%errorlevel%"
pause
exit /b %KF2_SETUP_EXIT%
