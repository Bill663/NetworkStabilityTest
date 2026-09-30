Network Stability Test - Windows Package

What this is
------------
This package runs the Router And Network Stability Test web UI locally on a
Windows PC. It supports Windows x64, x86, and ARM64 because it uses the Node.js
runtime installed on that PC.

Requirements
------------
1. Windows 10 or newer.
2. Node.js installed from https://nodejs.org/
   - x64: most Windows PCs
   - x86: older 32-bit Windows PCs
   - ARM64: Windows on ARM PCs
3. PowerShell, included with Windows.

Quick portable use
------------------
Double-click:

  Launch-NetworkStabilityTest.cmd

The app opens at:

  http://localhost:3877

Keep the command window open while using the app. Use the "Shut Down App"
button in the web UI to stop the server.

Install use
-----------
Double-click:

  Install.cmd

The installer copies the app to:

  %LOCALAPPDATA%\NetworkStabilityTest

It also creates a desktop launcher:

  Network Stability Test.cmd

Uninstall
---------
Run:

  Uninstall.cmd

Notes
-----
- Test results are saved under the app's NST\work and NST\outputs folders.
- The live network rating combines packet failures, latency, and available
  Wi-Fi signal data. Completed report ratings use packet failures and latency.
- Windows protects Wi-Fi signal details behind Location services. If the app
  shows "Permission needed", use its "Open Location Settings" button, turn on
  Location services, and allow desktop apps to use location. The signal panel
  will update automatically after Windows grants access.
- Wi-Fi signal does not apply to an Ethernet-only connection.
- If port 3877 is already busy, launch from Command Prompt with another port:

  set PORT=4000
  Launch-NetworkStabilityTest.cmd
