# Importing GNOME dock preferences

The converter creates a review file. It does not change GNOME, Plasma, global
shortcuts, or running panels. Run it from the repository with Python 3:

```sh
python3 kde/tools/migrate_settings.py settings.json --output kde-settings.json
python3 kde/tools/migrate_settings.py settings.json --format kconfig --output dock-general.conf
```

Existing output files are never replaced. Exports containing `~/` custom folders
need `--source-home /home/original-user` so paths do not accidentally refer to the
machine running the converter.

The input is JSON containing native JSON values or the literal value strings
printed by `gsettings get`. For example:

```json
{
  "settings": {
    "dock-position": "BOTTOM",
    "click-action": "cycle-windows",
    "magnification-enabled": true,
    "custom-stacks": ["/home/person/Projects"],
    "stack-view-overrides": {"custom:file:///home/person/Projects": "GRID"}
  },
  "favorites": ["org.kde.dolphin.desktop"],
  "launcherMappings": {"example-beta.desktop": "example.desktop"}
}
```

A flat settings object or an object under
`org.gnome.shell.extensions.dash-to-dock` or
`/org/gnome/shell/extensions/dash-to-dock/` also works. GNOME Shell favorites may
be supplied under `org.gnome.shell` → `favorite-apps`. Raw dconf INI text is not
accepted: represent its key/value data as JSON first. No commands or executable
expressions are accepted as values.

Omitted keys use the source extension's schema defaults, so an export containing
only customized dconf values preserves the original behavior. The report includes
all source values and identifies explicitly supplied keys. Unknown keys, invalid
enum values, nonfinite numbers, and values outside source schema limits produce
an error instead of silently changing the preference.

The JSON report contains:

- `General`: settings for the dock applet's General configuration group.
- `globalShortcuts`: Qt shortcut names and action IDs for the
  `org.gosh.goshosdock` component, including disabled and alternative bindings.
- `nativeShortcutDefaults`: explicit base-key import flags for
  `goshosdock-shortcutsrc` → `[NativeDefaults]`. Apply the listed numbered keys
  as `false` with the corresponding shortcut changes while Plasma is stopped.
  This clears a previous dock conflict marker so an imported custom or disabled
  binding is honored. It does not change Plasma's own shortcut assignments.
- `unityLauncherMappings`: desktop-file aliases for the `Unity Launcher Mapping`
  group in `taskmanagerrulesrc`.
- `unmapped`: GNOME startup behavior and legacy schema keys without current
  runtime consumers. Their values are retained with an explanation.

The KConfig format contains the `General` group and comments with the supplementary
shortcut, native-default flag, alias, and unmapped data. Review shortcuts in KDE System Settings before
applying them: existing KDE bindings may already use the same combinations. The
converter does not overwrite those bindings or select a panel instance for you.

Folder URLs, Favorites, both per-stack view and sorting preferences, the original
home directory, all three shortcut families, and Unity launcher aliases retain
their identity. GNOME's application-stack preferences apply to both the KDE
Applications stack and the application button when it opens a stack.
