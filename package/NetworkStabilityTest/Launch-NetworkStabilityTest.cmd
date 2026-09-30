@echo off
setlocal

set "ROOT_DIR=%~dp0"
set "APP_DIR=%ROOT_DIR%NST\app"
if "%PORT%"=="" set "PORT=3877"

where node >nul 2>nul
if errorlevel 1 (
  echo Node.js was not found on this PC.
  echo.
  echo Install Node.js for your Windows CPU type from:
  echo https://nodejs.org/
  echo.
  echo Use the Windows Installer for x64, x86, or ARM64, then run this file again.
  pause
  exit /b 1
)

if not exist "%APP_DIR%\server.js" (
  echo Could not find the app server at:
  echo %APP_DIR%\server.js
  pause
  exit /b 1
)

if not exist "%ROOT_DIR%NST\work\router-network-stability-test.ps1" (
  echo Could not find the network test script:
  echo %ROOT_DIR%NST\work\router-network-stability-test.ps1
  pause
  exit /b 1
)

echo Starting Network Stability Test...
echo App URL: http://localhost:%PORT%
start "" powershell -NoProfile -WindowStyle Hidden -Command "Start-Sleep -Milliseconds 900; Start-Process 'http://localhost:%PORT%'"

cd /d "%APP_DIR%"
node server.js

echo.
echo Server stopped.
pause
