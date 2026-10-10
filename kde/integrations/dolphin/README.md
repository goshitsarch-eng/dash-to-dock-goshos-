# Dolphin location integration

Stock Dolphin can answer whether an exact URL is open. It does not publish the
URLs in every tab and split view, which the dock needs to associate subdirectories
with folder, device and Trash icons. This optional patch adds that read-only API.
The dock reports `exact` capability with stock Dolphin and `full` when this API is
available. It never guesses a window's directory from its title.

The patch targets **Dolphin 26.08.2**, official tag `v26.08.2`, commit
`f19f070e7dc140213a2c6dd5d11f26122c89e1a0`. It also applies to development commit
`5e8b79e3551a647c24a2782110bc1bd071d017f3`. Build requirements are Dolphin's normal
Qt 6 / KDE Frameworks 6 development dependencies, including Baloo Widgets.

## Build and stage

Use a separate source checkout and a staging directory. The helper verifies the
stable source revision and does not install over the system Dolphin package.

```sh
git clone --branch v26.08.2 --depth 1 https://invent.kde.org/system/dolphin.git dolphin-26.08.2
./build-patched-dolphin.sh "$PWD/dolphin-26.08.2" "$PWD/dolphin-build" "$PWD/dolphin-stage"
```

Build your distribution's Dolphin package with the same patch for installation
on a desktop. Restart all Dolphin instances after installing that package so
each instance exports the API. Existing unpatched instances remain supported
with exact-URL matching; mixed instances report `mixed` capability.

## API and identity

Every `org.kde.dolphin-<pid>` service exports the read-only
`org.freedesktop.FileManager1.OpenWindowsWithLocations` property at
`/org/freedesktop/FileManager1`, with D-Bus type `a{sas}`. Values contain unique,
fully encoded URLs from every tab and both split views, with passwords removed.
The interface emits standard `org.freedesktop.DBus.Properties.PropertiesChanged`
notifications when locations change. Clients can reread the property to reconcile
state after reconnecting.

Keys have these forms:

| Session | Key | Association |
| --- | --- | --- |
| X11 | `x11:<native-window-id>` | Exact native ID from Plasma's `WinIdList` |
| Wayland | `pid:<process-id>` | Unique D-Bus owner PID and the single matching Dolphin task |

Dolphin 26.08.2 creates one `DolphinMainWindow` per process and registers each
instance with `KDBusService::Multiple` (`src/main.cpp`). New windows run in separate
processes. Wayland intentionally does not expose a globally meaningful Qt
`winId()` to clients. The dock authenticates the published PID using D-Bus
`GetConnectionUnixProcessID`, also verifies the task's Dolphin application ID,
and refuses an ambiguous match. Subsequent activate, minimize and close requests
use native Plasma task indexes and original `WinIdList` values, never process
termination. A disappearing service immediately loses its window association.

If Dolphin later supports multiple main windows in one Wayland process, it must
publish an additional compositor-resolvable identifier before those windows can
be associated safely. The current dock will leave ambiguous windows in the normal
task list.

## Validation

The patch was built against the stable revision above with Qt 6.12 and KDE
Frameworks 6.30. A real Dolphin process on a private D-Bus session published both
split-view URLs and a third URL opened in another tab through `openDirectories`.

The dock's `tst_locationbackend` uses real session D-Bus services and native Qt
models to test encoded local/remote URL boundaries, all-tab property discovery,
exact-URL fallback, X11 and Wayland identity matching, native task actions,
ambiguous identity rejection, and stale asynchronous reply rejection.

For a live desktop test, open two Dolphin windows, put a subdirectory of a tracked
folder in a background tab or split view, and verify that only its window belongs
to the folder icon. Navigate outside the folder, close a tab, unplug a device,
and restart Dolphin; the dock should update and restore ordinary task visibility.

```sh
qdbus6 org.kde.dolphin-<pid> /org/freedesktop/FileManager1 \
  org.freedesktop.DBus.Properties.Get \
  org.freedesktop.FileManager1 OpenWindowsWithLocations
```

The system-wide `org.freedesktop.FileManager1` name belongs to only one running
file manager. Query each `org.kde.dolphin-<pid>` owner to cover all Dolphin windows.
