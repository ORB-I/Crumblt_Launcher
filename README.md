# CrumbltLauncher

Tiny auto-updating launcher for CrumbltClient. Downloads the latest release
from GitHub, extracts it to `%APPDATA%\Crumblt\`, and launches `CrumbltClient.exe`.

## How it works

1. Fetches `version.txt` from the latest GitHub release
2. Compares against `%APPDATA%\Crumblt\version.txt`
3. If outdated (or client missing), downloads `CrumbltClient.zip` and extracts it
4. Launches `CrumbltClient.exe`, passing through any URI args (e.g. `crumblt://play/...`)

## Build

From an x64 Native Tools Command Prompt for VS 2026:

```bat
cd Crumblt_Launcher
cmake -B build -S . -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

Output: `build\Release\CrumbltLauncher.exe`

No external dependencies — uses only Win32 APIs.

## Release workflow

For every new CrumbltClient release on GitHub:

1. Build and zip: `CrumbltClient.exe` + all required DLLs flat in zip root
2. Create a file `version.txt` containing just the version string, e.g. `V_0.7`
3. Attach both `CrumbltClient.zip` and `version.txt` to the GitHub release
4. Make sure the release is tagged as **latest**

The launcher always downloads from:
- `https://github.com/ORB-I/Crumblt_Client/releases/latest/download/version.txt`
- `https://github.com/ORB-I/Crumblt_Client/releases/latest/download/CrumbltClient.zip`

## Register crumblt:// protocol

The launcher should be registered for the `crumblt://` URI protocol instead of
`CrumbltClient.exe`. Update `crumblt-install-protocol.bat` to point to
`CrumbltLauncher.exe` so clicking Play on the website goes through the launcher.

## UI

- Borderless dark window, draggable
- Blue/purple gradient progress bar
- ESC to cancel
