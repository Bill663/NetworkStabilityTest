Network Stability Test - Native Windows Edition
================================================

Launch
------
Double-click NetworkStabilityTest.exe. The application is self-contained and
does not require Node.js, PowerShell, a browser, or an installer.

Use
---
1. Choose a duration and unit.
2. Select Start Test.
3. Watch the live network score, Wi-Fi signal, and probe table.
4. Select Stop when you want to finish early. The partial run is still saved.

Reports are written to:
%LOCALAPPDATA%\NetworkStabilityTest\reports

Wi-Fi permission
----------------
Recent Windows versions can require Location services and desktop-app location
access before exposing Wi-Fi connection details. If Windows blocks the signal
query, use the Location Settings button shown by the application and grant the
requested access. The network probes continue to work without this permission.

Architecture
------------
Use the package matching the destination PC: x64, x86, or ARM64.

A Design By Bill Jiang
