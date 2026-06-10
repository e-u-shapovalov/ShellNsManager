# Shell Namespace Manager: Windows Explorer namespace editor

**ShellNsManager** is a small portable Windows utility for managing Windows Explorer Shell Namespace items without editing the registry by hand. It helps you add custom folders to **This PC** or the navigation tree, reorder namespace nodes, hide built-in Explorer items, and control **Home** and **Quick access** visibility.

Main README: [Русский](README.md) | Download help: [DOWNLOAD.md](DOWNLOAD.md) | Install/build: [INSTALL.md](INSTALL.md) | Latest release: [GitHub Releases](../../releases/latest)

## Download

Open the latest [GitHub Release](../../releases/latest), expand **Assets**, and download the ready-to-run package, for example `ShellNsManager-v1.0-win64.zip` or `ShellNsManager.exe`.

Do **not** use `Code -> Download ZIP` if you only want to run the app. Do **not** download `Source code (zip)` or `Source code (tar.gz)` from a release unless you are a developer.

If Releases are empty or there are no files under **Assets**, a ready public build has not been published yet. Developers can build the app from source with `build.bat`.

## Features

- List Shell Namespace nodes from This PC and the Desktop navigation tree.
- Merge 64-bit and 32-bit registry views into readable logical entries.
- Add a custom folder with a custom icon.
- Write entries per-user (`HKCU`) or system-wide (`HKLM`).
- Delete namespace nodes with automatic `.reg` backups.
- Reorder items through `SortOrderIndex`.
- Hide built-in Explorer tree items.
- Toggle all-folders navigation mode.
- Hide or restore Windows Explorer **Home**.
- Hide or show **Quick access**.
- Clear pinned and frequent Quick Access folders with a backup.
- Restart Explorer from the app.
- Russian UI by default, English localization available.

## Screenshots

![ShellNsManager main window: namespace nodes under This PC and the Desktop tree](docs/screenshots/main-window.png)

## Installation

ShellNsManager does not require an installer.

1. Download the release archive.
2. Extract it to a folder such as `C:\Tools\ShellNsManager`.
3. Run `ShellNsManager.exe`.
4. Confirm the UAC administrator prompt.

Backups are stored next to the executable in `backups\YYYYMMDD-HHMMSS\`.

## Build from source

Requirements:

- Windows;
- Visual Studio 2022 Build Tools;
- Desktop development with C++;
- Windows SDK.

Build:

```bat
git clone <this repository URL>
cd ShellNsManager
build.bat
```

The output file is `ShellNsManager.exe` in the repository root.

## Safety notes

This tool modifies Windows registry keys used by Explorer. It creates `.reg` backups before destructive or protected operations, but you should still use it carefully and keep restore points or system backups for critical machines.

The executable requests administrator privileges because several Explorer namespace keys live under `HKEY_LOCAL_MACHINE` and may be protected by Windows.

## License

Released under the **MIT License** — see [LICENSE](LICENSE). You may freely use, modify, and distribute the code as long as the license text and copyright notice are preserved.
