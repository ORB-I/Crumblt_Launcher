# CrumbltLauncher (Rust Edition)

Tiny auto-updating launcher for CrumbltClient. Downloads the latest release from GitHub, extracts it to `%APPDATA%\Crumblt\`, and launches `CrumbltClient.exe`.

## How it works

1. Fetches `Branches.txt` to get available channels
2. Downloads client from selected branch
3. Extracts to `%APPDATA%\Crumblt\`
4. Launches `CrumbltClient.exe`, passing through any URI args

## Build

Install Rust from https://rustup.rs/, then:

```bash
cargo build --release
```

Output: `target\release\crumblt-launcher.exe`

## Release workflow

For every new release:

1. Build: `cargo build --release`
2. Rename: `ren target\release\crumblt-launcher.exe CrumbltLauncher.exe`
3. Attach `CrumbltLauncher.exe` to GitHub release
4. Also attach a `version.txt` with version number (e.g., `1.0.0`)

## Register crumblt:// protocol

Run the launcher once - it auto-registers the protocol via Windows Registry.
