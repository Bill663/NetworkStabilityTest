# Project Context

Read HANDOFF.md and TRANSFER-README.md before changing this project.
The active application is native Win32 C in native/src/network_stability_test.c.
The NST directory contains the earlier Node.js/PowerShell web application.
Preserve the credit exactly: A Design By Bill Jiang.
Keep x64, x86, and ARM64 builds supported. Do not assume old machine paths exist.
Build and package scripts resolve paths relative to their own location.
Do not change Windows privacy settings automatically; let the user grant access.
Distinguish cross-compilation from execution testing on each architecture.
