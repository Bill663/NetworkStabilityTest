# Development Handoff

Prepared 2026-09-16 for transfer to another Windows computer.

## User Intent

Build a native Windows application rewritten in C, with live network rating,
Wi-Fi signal diagnosis, clean stop/exit behavior, portable x86/x64/ARM64 packages,
and the exact credit "A Design By Bill Jiang". Keep the UI compact and useful.
The latest task is preparing the whole project for transfer and further ChatGPT work.

## Implementation

- `native/src/network_stability_test.c`: Win32 UI, worker thread, gateway/public
  ICMP, DNS resolution, TCP 443 probes, Native WLAN signal query, CSV/text reports.
- `native/resources`: application manifest and version resource.
- `native/Build-Native.ps1`: Zig C cross-builds; local compiler caches.
- `native/Package-Native.ps1`: portable architecture ZIPs and combined ZIP.
- `Launch-Native.cmd`: architecture-selecting launcher.
- `NST/app` and `NST/work`: older web UI, Node server, PowerShell probe engine.

## Verification And Remaining Work

The earlier session visually exercised an x64 build, live readings, normal
completion, and early Stop with partial report creation. It then fixed elapsed
time labeling and separated report-folder opening from report filename creation.
ARM64 rebuilding was interrupted when its executable was open; see the current
build outputs and transfer preparation result for subsequent compilation status.
On 2026-09-16 all three targets rebuilt successfully from the current source.
No claim of complete architecture-specific runtime testing is intended.

Known limitations to review before production use:

- DNS uses blocking getaddrinfo; Stop can wait for Windows DNS timeout.
- TCP timeouts apply per resolved address, so total duration can exceed two seconds.
- The rating uses cumulative samples, not a recent rolling window, and currently
  includes live Wi-Fi even after a run finishes, so the final display may drift.
- ICMP reply handling should validate reply Status, not only the reply count.
- Report write failures are not surfaced reliably; UI can claim a report was saved.
- Duration conversion uses integer multiplication and needs overflow validation.
- Final worker lifecycle needs review: running becomes false before report writing
  finishes, which can race with closing the window.
- WLAN queries run on the UI timer and can block rendering; keyboard navigation,
  accessibility names, and DPI scaling need another refinement pass.
- No automated regression suite is present. Add focused tests when fixing these.

These are recorded for the next development pass; transfer preparation does not
represent a full correctness audit or a production release certification.

## Transfer Notes

The original compiler archive has SHA-256
`68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e`.
It was downloaded from the official Zig 0.16.0 Windows x86_64 release.
Native reports live in LocalAppData and are not included automatically.
No conversation credentials or ChatGPT configuration are required in the package.
