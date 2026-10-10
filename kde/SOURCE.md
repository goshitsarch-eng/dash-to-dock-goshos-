# Native KDE source provenance

The native applet starts from KDE Plasma Desktop's task manager, rather than
emulating a desktop window manager in a standalone application. This preserves
Plasma's native task model, Wayland integration, launcher handling, window
groups, previews, desktop/activity actions, audio controls, and task menus.

Upstream source:

- Project: [KDE Plasma Desktop](https://invent.kde.org/plasma/plasma-desktop).
- Release: **6.7.5**, tag `v6.7.5`.
- Commit: `43f55fff3480c6eb5f60133f045bfc74b9c67d84`.
- Source directory: `applets/taskmanager/`, copied into `src/`.
- Additional files: `kcms/recentFiles/kactivitymanagerd_plugins_settings.kcfg`
  and `.kcfgc`, copied into `src/` for the recent-document backend.

The upstream file headers retain their copyright notices and SPDX identifiers.
Most files are GPL-2.0-or-later; the tooltip, thumbnail, media-controller, and
scrollable-text components carry LGPL-2.0-or-later notices. The corresponding
license texts are in `LICENSES/`. Individual file headers govern their license.
The upstream configuration schema and build helpers are distributed with the
GPL-licensed task-manager implementation. Original contributors are also
credited in the source headers and applet metadata.

The local fork has its own applet ID (`org.gosh.goshosdock`), QML module
(`plasma.applet.org.gosh.goshosdock`), and translation domain. Its standalone
CMake project packages the applet and its native KWin companion. Dock appearance
and controls are maintained here; the GNOME extension remains in the repository
root. The companion uses KWin's versioned plugin ABI and must be rebuilt against
the installed KWin release.

The optional Dolphin location integration is a patch against **26.08.2**, commit
`f19f070e7dc140213a2c6dd5d11f26122c89e1a0`. Its source, staging helper and protocol
details are documented in [integrations/dolphin](integrations/dolphin/README.md).

The copied backend uses the public task model together with some Plasma
workspace QML modules intended for the matching workspace release. Build and
run it against Plasma 6.7; compatibility with older Plasma versions is not
claimed. When updating upstream code, compare both the QML APIs and the native
library dependencies, preserve local changes and notices, and run desktop
acceptance checks again.
