# XID USB Diagnostic

Optional nxdk homebrew app for inspecting USB/XID enumeration on the original Xbox.

The app prints status on screen and broadcasts diagnostic lines to UDP port `49036`.

Build with nxdk:

```powershell
make NXDK_DIR=C:\path\to\nxdk
```

Listen on the PC:

```powershell
.\udp_diag_listener.ps1
```

Generated `.xbe`, `.iso`, object, dependency, and log files are ignored by git.
