@echo off
setlocal

set "INSTALL_DIR=%LOCALAPPDATA%\NetworkStabilityTest"
set "SHORTCUT=%USERPROFILE%\Desktop\Network Stability Test.cmd"

echo This removes Network Stability Test from:
echo %INSTALL_DIR%
echo.
echo Existing CSV reports in that install folder will also be removed.
echo Close the app before continuing.
echo.
choice /C YN /M "Uninstall Network Stability Test"
if errorlevel 2 exit /b 0

if exist "%SHORTCUT%" del "%SHORTCUT%"
if exist "%INSTALL_DIR%" rmdir /S /Q "%INSTALL_DIR%"

echo.
echo Uninstalled.
pause
