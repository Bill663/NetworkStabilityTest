# Network Stability Test

Native Windows network stability tester written in Win32 C, with portable x64, x86, and ARM64 builds.

A Design By Bill Jiang

## Run

On Windows, double-click `Launch-Native.cmd`. It selects the existing executable for your architecture under `native/build`.
Portable native ZIP packages are also available in `dist`. No installation or Node.js is required for the native application.

The app measures gateway and public ICMP, DNS resolution, TCP 443 connectivity, and Wi-Fi signal, and writes CSV/text reports under `%LOCALAPPDATA%\NetworkStabilityTest\reports`.
If Windows requires permission to read Wi-Fi details, use the app's Location Settings button and grant access yourself.

## Build

Read `AGENTS.md`, `HANDOFF.md`, and `TRANSFER-README.md` before development.
The compiler archive and extracted compiler are excluded from Git. On a fresh clone, create `.tools` and download the official [Zig 0.16.0 Windows x64 archive](https://ziglang.org/download/0.16.0/zig-x86_64-windows-0.16.0.zip) to `.tools/zig-x86_64-windows-0.16.0.zip`.
The build script verifies SHA-256 `68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e` before extracting it.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\native\Build-Native.ps1 -Architecture all
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\native\Package-Native.ps1
```

The bundled compiler runs on x64 Windows or compatible x64 emulation on ARM64 and cross-compiles all three targets. The existing handoff records successful cross-compilation for x64, x86, and ARM64 and earlier x64 UI testing; this does not establish execution testing on all architectures. Known application limitations are recorded in `HANDOFF.md`.

## Project contents

- `native/src/network_stability_test.c`: active Win32 application.
- `native/resources`: manifest and version resource.
- `native/Build-Native.ps1` and `native/Package-Native.ps1`: build and packaging scripts.
- `NST/app` and `NST/work/router-network-stability-test.ps1`: earlier Node.js/PowerShell web application; its launcher requires Node.js on PATH.
- `Pack-Transfer.ps1`: local transfer packaging, including local historical reports when present. Inspect those reports before sharing a transfer archive.

Compiler caches, debug symbols, package staging, duplicate legacy packages, historical reports/logs, and the transfer archive are excluded from the repository. Existing application executables and native portable ZIPs are retained. Local files are preserved.

`TRANSFER-README.md` describes the original full local transfer bundle, which includes the compiler archive and may include reports; a GitHub clone uses the compiler setup above.
