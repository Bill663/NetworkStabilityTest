# Network Stability Test: Transfer Guide

Extract the transfer ZIP to a writable folder on the destination Windows PC.
Double-click `Launch-Native.cmd` to select and run the x64, x86, or ARM64 application.
No installation or Node.js is required for the native application.

## Continue Development With ChatGPT

Open the extracted project folder as the workspace and ask:

> Read AGENTS.md and HANDOFF.md, inspect the current source, and continue working on this project.

Conversation history and ChatGPT settings do not travel with this folder. The handoff
captures the project context. Authenticate to ChatGPT normally on the other PC.

## Build

From a PowerShell terminal in the project folder:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\native\Build-Native.ps1 -Architecture all
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\native\Package-Native.ps1
```

The build script extracts the bundled, checksum-verified Zig 0.16.0 compiler when
needed. The compiler runs on x64 Windows (or compatible x64 emulation on ARM64);
it cross-compiles all three app architectures. A 32-bit Windows PC can run the
x86 app but cannot run this bundled x64 compiler. No compiler download is needed.
Close running native executables before rebuilding.

The older web version is in `NST`; `Launch-NetworkStabilityTest.cmd` starts it and
requires Node.js on PATH. `Build-Package.ps1` packages that older version.

## Data And Permissions

Native reports are stored outside the project under
`%LOCALAPPDATA%\NetworkStabilityTest\reports`. Copy that directory separately if
you need previous native reports. Existing web reports inside `NST` travel with
this folder and may contain network details.

Windows Wi-Fi privacy permissions are specific to each computer. Use the app's
Location Settings button when prompted; permission changes are made by the user.

The transfer ZIP omits compiler caches, the extracted compiler, duplicate package
staging, and other transfer ZIPs. Source, application builds, native release ZIPs,
the old web application, and the original compiler archive are included.

A Design By Bill Jiang
