# Bunyan Log Viewer Plugin Skeleton

This repository now includes a minimal native Notepad++ plugin scaffold for Bunyan short-log viewing and checkbox-based level filtering.

## What it does

- Adds a `Toggle Short Preview` command that opens a read-only preview pane docked at the bottom of Notepad++ with the same short Bunyan view as the Go helper.
- Adds a `Filter Log Levels...` command that shows only Bunyan lines whose level matches the checked `DEBUG`, `INFO`, `WARN`, and `ERROR` boxes in that same docked pane.
- Adds a `Close Preview` command to hide the docked pane.
- Registers `Toggle Short Log` on the Notepad++ toolbar with a light and dark mode icon.
- Leaves the original Notepad++ document untouched, so closing the tab no longer prompts to save plugin-generated view changes.
- Refreshes the docked pane automatically while it stays open, so the short view tracks incoming log lines.

## Current scope

This is a lightweight skeleton, not a full SDK-vendored Notepad++ plugin project. It exports the standard entry points expected by Notepad++ and renders its short-log output in a docked native preview pane.

## Build

Use a Visual Studio generator on Windows:

```powershell
./build.ps1
```

Optional parameters:

```powershell
./build.ps1 -Configuration Debug
./build.ps1 -BuildDir build-debug
./build.ps1 -Generator "Visual Studio 17 2022"
```

Equivalent raw CMake commands:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The resulting DLL will be placed under the build output for the selected configuration.

For manual installation in Notepad++, the folder name and DLL name must match. Install it like this:

```text
<Notepad++>\plugins\BunyanLogViewer\BunyanLogViewer.dll
```

## Notes

- `Toggle Short Preview` formats each Bunyan line as `HH:MM:SS LEVEL: message` and appends `method` and `caseId` when present, matching the Go utility's output shape.
- `Filter Log Levels...` always regenerates from the current editor contents, so changing the checked levels does not stack filters and the original file stays untouched.
- Preview refreshes now use `SCI_GETTEXT`, which is more reliable than reconstructing the document line by line for live updates.
- `Close Preview` hides the read-only docked pane without affecting the active document.
- The existing Go utility remains untouched and can still be used independently.