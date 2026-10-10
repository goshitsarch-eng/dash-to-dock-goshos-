# Gosho’s Dock for KDE Plasma

A native **Qt Quick / Kirigami 6** port for **Plasma Wayland 6.7.5**, the stable
release used for this port. It includes Breeze, Glass, Minimal and Classic styles,
magnification, application and folder stacks, all original click policies,
notification badges, quicklists, configurable shortcuts and native KDE window
management.

The port includes a KWin companion for independent hiding, pressure, timing and
work-area behavior. Full folder/subfolder window isolation also requires the
supplied [Dolphin integration](integrations/dolphin/README.md). Without that patch,
stock Dolphin supports exact-URL matching. The original GNOME extension remains
at the repository root as the feature reference.

The full native build and component tests have passed against Plasma/KWin 6.7.5,
Frameworks 6.30 and Qt 6.12. The compiled applet also runs under KWin with two
outputs. See [validation](docs/VALIDATION.md) for executed checks and limits, and
[feature parity](docs/FEATURE_PARITY.md) for the audit of all 127 original settings.

## Build and install

Use your distribution’s matching stable KDE SDK. Requirements include:

- Plasma Desktop/Workspace, PlasmaQuick, Plasma Activities/Stats, KWin development
  files, LayerShellQt, LibTaskManager, LibNotificationManager and KSysGuard.
- Qt 6.10+ Core, Concurrent, DBus, Gui, GuiPrivate, Qml, Quick, QuickControls2,
  Widgets and Core5Compat. Tests also use Test, QuickTest and WaylandClientPrivate.
- Frameworks/ECM 6.26+ Config, CoreAddons, GlobalAccel, I18n, KIO, JobWidgets,
  Notifications, Service, Solid, WindowSystem, WidgetsAddons, Kirigami, KCMUtils
  and Svg.
- Wayland client and libxkbcommon development files, GIO, libmalcontent,
  CMake 3.22+, a C++20 compiler, Python 3 and Node.js. Compositor integration
  tests additionally use libei.
- Runtime KIO workers, Plasma audio/MPRIS modules, PipeWire thumbnails and a
  session D-Bus. Application usage history uses KActivities.
- GVfs runtime daemons and the protocol backends for network locations. GIO
  headers alone do not provide remote mount discovery. On Arch, install `gvfs`,
  `gvfs-smb`, `gvfs-nfs` and `gvfs-dnssd` for SMB, NFS and WebDAV/DNS-SD; other
  distributions may bundle these as `gvfs-backends`. Additional providers such
  as online accounts require their matching GVfs backend. KDE Places and KIO
  remain available independently.

```sh
cmake -S kde -B kde/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=ON
cmake --build kde/build --parallel 2
ctest --test-dir kde/build --output-on-failure --timeout 120
sudo cmake --install kde/build
```

The installation provides a compiled Plasma applet and the KWin companion.
`kpackagetool6` cannot install this source directory. **Rebuild the companion when
KWin is updated**: it deliberately uses KWin’s versioned native plugin ABI.

Log out and back in, enter Plasma Edit Mode, add an empty panel, then add
**Gosho’s Dock**. Give the dock its own panel. Configure its edge, display, length,
hiding and appearance in the dock’s settings. Automatic multi-display copies
share settings. Panel management is restricted to verified dedicated dock panels;
its original panel state is restored when management ends. A warning button
explains unavailable integrations or an unsuitable panel.

Configure global keys in **System Settings → Keyboard → Shortcuts**. Existing
KDE shortcuts are preserved: if Meta+0 is assigned to KWin's Zoom to Actual Size,
change that binding before assigning Meta+0 to the dock's tenth activation action.
Likewise, if Meta+Q belongs to Plasma's Activity Switcher, choose another dock
overlay key or change the Activity Switcher binding before assigning Meta+Q.
Alternate number defaults follow the active keyboard layout where it provides
unshifted digits; capture an explicit shortcut for other layouts. Custom and
disabled bindings remain unchanged when the layout changes.

To retain full location isolation, build your distribution’s Dolphin 26.08.2
package with the [provided patch](integrations/dolphin/README.md), then restart
Dolphin. The patch publishes URLs from inactive tabs and split views without
inferring them from window titles.

The [migration tool](docs/MIGRATION.md) converts GNOME settings, favorites,
per-stack overrides and shortcut families into reviewable JSON or KConfig. It
also identifies legacy and GNOME-only settings.

## Cloud SDK and checks

With Docker available, this builds and tests in an official Arch Linux container
using signed stable packages:

```sh
./kde/tools/setup-sdk.sh
```

The SDK container is named `goshos-dock-sdk`; set `GOSHOS_SDK_NAME` to choose
another name. It mounts this checkout at `/src`. Build output is kept in the
ignored `kde/build-sdk` directory. The helper does not require privileged Docker.

Fast checks need only Python and Node:

```sh
make kde-check
```

For native compositor tests after a local build/install:

```sh
./kde/tools/test-wayland.sh "$PWD/kde/build"
```

With the container helper above, install and run inside that container instead:

```sh
docker exec goshos-dock-sdk cmake --install /src/kde/build-sdk
docker exec goshos-dock-sdk /src/kde/tools/test-wayland.sh /src/kde/build-sdk
```

This runner creates a private D-Bus, temporary configuration and an independent
KWin session. It injects pointer input only into that disposable session and
preserves logs in the build directory. Live GPU thumbnails and Overview rendering
need a compositor with working graphics acceleration; physical devices and actual
mixed-DPI output hotplug require desktop acceptance.

## Source and license

GPL-2.0-or-later, with original KDE license/copyright notices retained. See
[SOURCE.md](SOURCE.md) for the upstream release and commits, and
[kwin/README.md](kwin/README.md) for companion implementation and validation.
