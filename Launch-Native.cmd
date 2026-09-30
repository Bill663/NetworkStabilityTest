@echo off
setlocal
set "ARCH=x86"
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set "ARCH=x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "ARCH=arm64"
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set "ARCH=x64"
if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64" set "ARCH=arm64"
if not exist "%~dp0native\build\%ARCH%\NetworkStabilityTest.exe" (
  echo Native executable missing. See TRANSFER-README.md for build instructions.
  pause
  exit /b 1
)
start "" "%~dp0native\build\%ARCH%\NetworkStabilityTest.exe"
