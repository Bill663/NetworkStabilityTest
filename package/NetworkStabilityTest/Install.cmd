@echo off
setlocal

set "SOURCE_DIR=%~dp0"
set "INSTALL_DIR=%LOCALAPPDATA%\NetworkStabilityTest"
set "SHORTCUT=%USERPROFILE%\Desktop\Network Stability Test.cmd"

where node >nul 2>nul
if errorlevel 1 (
  echo Node.js is required and was not found.
  echo.
  echo Install Node.js for this PC from:
  echo https://nodejs.org/
  echo.
  echo Choose the Windows Installer that matches the PC CPU:
  echo - x64 for most Windows PCs
  echo - x86 for older 32-bit Windows PCs
  echo - ARM64 for Windows on ARM PCs
  echo.
  pause
  exit /b 1
)

echo Installing Network Stability Test to:
echo %INSTALL_DIR%
echo.

if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
robocopy "%SOURCE_DIR%NST" "%INSTALL_DIR%\NST" /E /XF *.csv *.log *.json *.stop >nul
copy /Y "%SOURCE_DIR%Launch-NetworkStabilityTest.cmd" "%INSTALL_DIR%\" >nul
copy /Y "%SOURCE_DIR%Uninstall.cmd" "%INSTALL_DIR%\" >nul

(
  echo @echo off
  echo call "%INSTALL_DIR%\Launch-NetworkStabilityTest.cmd"
) > "%SHORTCUT%"

echo.
echo Installed.
echo Launch it from:
echo %SHORTCUT%
echo.
pause
