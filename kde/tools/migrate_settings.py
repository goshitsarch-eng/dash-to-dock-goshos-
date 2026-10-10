#!/usr/bin/env python3
"""Convert a GNOME dock settings JSON export into a reviewable KDE configuration.

This program does not write desktop settings, launch commands, or execute values.
JSON may contain native values or the literal GVariant strings from `gsettings get`.
"""

from __future__ import annotations

import argparse
import ast
import json
import math
from pathlib import Path
import re
import sys
from urllib.parse import quote, urlsplit, urlunsplit
import xml.etree.ElementTree as ET

SCHEMA_ID = "org.gnome.shell.extensions.dash-to-dock"
SCHEMA_PATH = "/org/gnome/shell/extensions/dash-to-dock/"
DEFAULT_SCHEMA = Path(__file__).resolve().parents[2] / "schemas" / (SCHEMA_ID + ".gschema.xml")
MAX_INPUT_BYTES = 1024 * 1024

DIRECT = {
    "dock-position": "dockPosition", "animation-time": "animationTime",
    "show-delay": "showDelay", "hide-delay": "hideDelay",
    "custom-background-color": "customBackgroundColor", "background-color": "customColor",
    "running-indicator-style": "indicatorStyle", "running-indicator-dominant-color": "dominantIndicatorColor",
    "customize-alphas": "customizeAlphas", "min-alpha": "minAlpha", "max-alpha": "maxAlpha",
    "background-opacity": "backgroundOpacity", "manualhide": "manualHide",
    "intellihide": "intelliHide", "intellihide-mode": "intelliHideMode", "autohide": "autoHide",
    "require-pressure-to-show": "pressureToShow", "pressure-threshold": "pressureThreshold",
    "show-dock-urgent-notify": "unhideOnAttention", "dock-fixed": "dockFixed",
    "dash-max-icon-size": "iconSize", "preview-size-scale": "previewSizeScale", "icon-size-fixed": "iconSizeFixed",
    "custom-theme-shrink": "compact", "custom-theme-customize-running-dots": "customizeIndicators",
    "custom-theme-running-dots-color": "indicatorColor", "custom-theme-running-dots-border-color": "indicatorBorderColor",
    "custom-theme-running-dots-border-width": "indicatorBorderWidth", "show-running": "showRunning",
    "isolate-workspaces": "showOnlyCurrentDesktop", "workspace-agnostic-urgent-windows": "workspaceAgnosticUrgent",
    "isolate-monitors": "showOnlyCurrentScreen", "scroll-to-focused-application": "scrollToFocused",
    "show-windows-preview": "showToolTips", "default-windows-preview-to-open": "defaultPreviewsOpen",
    "show-favorites": "showFavorites", "show-trash": "showTrash", "show-mounts": "showDevices",
    "show-mounts-only-mounted": "devicesOnlyMounted", "show-mounts-network": "showNetworkDevices",
    "isolate-locations": "isolateLocations", "dance-urgent-applications": "danceUrgent",
    "show-show-apps-button": "showApplications", "show-apps-at-top": "showAppsAtStart",
    "show-apps-always-in-the-edge": "appsAlwaysAtEdge", "height-fraction": "intendedLengthFraction",
    "extend-height": "extendDock", "always-center-icons": "centerIcons",
    "preferred-monitor-by-connector": "preferredOutput", "multi-monitor": "allOutputs",
    "click-action": "clickAction", "scroll-action": "dockScrollAction", "shift-click-action": "shiftClickAction",
    "middle-click-action": "middleClickDockAction", "shift-middle-click-action": "shiftMiddleClickAction",
    "hot-keys": "hotKeys", "hotkeys-show-dock": "hotkeysShowDock", "shortcut-timeout": "shortcutTimeout",
    "hotkeys-overlay": "hotkeysOverlay", "unity-backlit-items": "unityBacklit", "apply-glossy-effect": "glossyIcons",
    "hide-tooltip": "hideTooltip", "show-icons-emblems": "showEmblems", "show-icons-notifications-counter": "showNotificationCounter",
    "application-counter-overrides-notifications": "applicationCounterOverridesNotifications",
    "magnification-enabled": "magnification", "magnification-factor": "magnificationFactor", "magnification-range": "magnificationSpread",
    "show-apps-button-action": "showAppsAction", "show-applications-stack": "applicationsStack",
    "show-documents-stack": "documentsStack", "show-downloads-stack": "downloadsStack", "show-home-stack": "homeStack",
    "stack-view": "stackView", "stack-sort": "stackSort", "stack-max-items": "stackItemLimit",
    "show-stacks-separator": "stacksDivider", "show-recent-applications": "showRecentApps",
    "recent-applications-limit": "recentAppsLimit", "launch-bounce-animation": "launchBounce",
    "dim-hidden-applications": "dimMinimized", "hidden-applications-opacity": "minimizedOpacity",
}

SPECIAL = {
    "autohide-in-fullscreen", "transparency-mode", "apply-custom-theme", "force-straight-corner",
    "custom-stacks", "stack-view-overrides", "stack-sort-overrides", "shortcut",
}

UNMAPPED = {
    "disable-overview-on-startup": "GNOME Shell startup policy; KDE does not start in the GNOME overview.",
    "bolt-support": "Legacy compatibility key with no current GNOME runtime consumer.",
    "minimize-shift": "Legacy key with no runtime consumer; shift-click-action defines the behavior.",
    "activate-single-window": "Legacy key with no runtime consumer; click-action defines the behavior.",
    "scroll-switch-workspace": "Legacy key with no runtime consumer; scroll-action defines the behavior.",
    "preferred-monitor": "Deprecated GNOME monitor index; preferred-monitor-by-connector supplies the output name.",
    "shortcut-text": "Preference editor helper; the shortcut array contains the actual bindings.",
}

SHORTCUT_FAMILIES = {
    "app-hotkey-": "goshosdock-activate-",
    "app-ctrl-hotkey-": "goshosdock-launch-",
    "app-shift-hotkey-": "goshosdock-alternate-",
}


class MigrationError(ValueError):
    pass


def schema_data(path=DEFAULT_SCHEMA):
    root = ET.parse(path).getroot()
    enums = {item.attrib["id"]: {value.attrib["nick"]: int(value.attrib["value"]) for value in item}
             for item in root.findall("enum")}
    keys = {}
    for item in root.find("schema").findall("key"):
        keys[item.attrib["name"]] = {
            "type": item.attrib.get("type", "enum"),
            "enum": enums.get(item.attrib.get("enum")),
            "default": item.findtext("default", "").strip(),
            "range": item.find("range").attrib if item.find("range") is not None else {},
        }
    return keys


def parse_value(name, value, definition):
    kind = definition["type"]
    if isinstance(value, str):
        literal = value.strip()
        # GVariant prints empty collections with their type, e.g. @as [].
        if literal.startswith("@"):
            prefix, separator, literal = literal.partition(" ")
            if not separator or prefix != "@" + kind:
                raise MigrationError(f"{name}: invalid GVariant type annotation")
            literal = literal.strip()
        if kind == "b" and literal in ("true", "false"):
            value = literal == "true"
        elif kind in ("i", "d"):
            try:
                value = int(literal) if kind == "i" else float(literal)
            except ValueError as error:
                raise MigrationError(f"{name}: expected a number") from error
        elif kind in ("as", "a{ss}") or (kind in ("s", "enum") and literal[:1] in ("'", '"')):
            try:
                value = ast.literal_eval(literal)
            except (ValueError, SyntaxError, RecursionError) as error:
                raise MigrationError(f"{name}: expected a literal value") from error
    valid = {
        "b": type(value) is bool,
        "i": type(value) is int,
        "d": type(value) in (int, float) and math.isfinite(value),
        "s": isinstance(value, str),
        "as": isinstance(value, list) and all(isinstance(item, str) for item in value),
        "a{ss}": isinstance(value, dict) and all(isinstance(key, str) and isinstance(item, str) for key, item in value.items()),
        "enum": isinstance(value, str) and value in (definition["enum"] or {}) or type(value) is int and value in (definition["enum"] or {}).values(),
    }.get(kind, False)
    if not valid:
        raise MigrationError(f"{name}: invalid value for {kind}")
    for bound, relation in (("min", lambda a, b: a < b), ("max", lambda a, b: a > b)):
        if bound in definition["range"] and relation(value, float(definition["range"][bound])):
            raise MigrationError(f"{name}: value is outside the source schema range")
    if kind == "enum" and type(value) is int:
        return next(nick for nick, number in definition["enum"].items() if number == value)
    return value


def folder_url(value, home=None):
    if value.startswith("~/") or value == "~":
        if not home:
            raise MigrationError("A custom folder uses ~; pass --source-home to preserve the original home directory")
        value = home.rstrip("/") + value[1:]
    if value.startswith("/"):
        return "file://" + quote(value.rstrip("/") or "/", safe="/")
    parsed = urlsplit(value)
    if not parsed.scheme or not re.fullmatch(r"[A-Za-z][A-Za-z0-9+.-]*", parsed.scheme):
        raise MigrationError(f"Custom folder must be an absolute path or URL: {value!r}")
    result = urlunsplit(parsed)
    return result if result.endswith(":///") else result.rstrip("/")


def stack_key(value, home=None):
    if value == "applications":
        return "applications-stack"
    if value in ("documents", "downloads", "home"):
        return value
    if value.startswith("custom:"):
        value = value[len("custom:"):]
    return folder_url(value, home)


def shortcut(value):
    modifiers = {"Super": "Meta", "Meta": "Meta", "Ctrl": "Ctrl", "Control": "Ctrl", "Primary": "Ctrl", "Shift": "Shift", "Alt": "Alt"}
    parts = []
    while value.startswith("<"):
        match = re.match(r"<([^>]+)>", value)
        if not match or match[1] not in modifiers:
            raise MigrationError(f"Unsupported shortcut modifier: {value!r}")
        modifier = modifiers[match[1]]
        if modifier not in parts:
            parts.append(modifier)
        value = value[match.end():]
    aliases = {"space": "Space", "Return": "Return", "Escape": "Esc", "BackSpace": "Backspace", "Tab": "Tab", "minus": "-", "equal": "=", "plus": "+"}
    key = aliases.get(value, value)
    if not re.fullmatch(r"[A-Za-z0-9_+=-]+", key):
        raise MigrationError(f"Unsupported shortcut key: {value!r}")
    if len(key) == 1:
        key = key.upper()
    return "+".join(parts + [key])


def unpack_export(document):
    if not isinstance(document, dict):
        raise MigrationError("The export must be a JSON object")
    settings = document.get(SCHEMA_ID, document.get(SCHEMA_PATH, document.get("settings", document)))
    if not isinstance(settings, dict):
        raise MigrationError("The settings export must be an object")
    shell = document.get("org.gnome.shell", document.get("/org/gnome/shell/", {}))
    favorites = document.get("favorites", shell.get("favorite-apps") if isinstance(shell, dict) else None)
    mappings = document.get("launcherMappings", {})
    if settings is document:
        settings = {key: value for key, value in settings.items() if key not in ("favorites", "launcherMappings")}
    return settings, favorites, mappings


def migrate(document, source_home=None, schema=DEFAULT_SCHEMA):
    definitions = schema_data(schema)
    provided, favorites, mappings = unpack_export(document)
    unknown = set(provided) - set(definitions)
    if unknown:
        raise MigrationError("Unknown source settings: " + ", ".join(sorted(unknown)))
    values = {key: parse_value(key, provided.get(key, definition["default"]), definition) for key, definition in definitions.items()}
    general, unmapped, actions = {}, {}, {}
    for key, value in values.items():
        if key in DIRECT:
            if definitions[key]["type"] == "enum" and "click-action" not in key:
                value = definitions[key]["enum"][value]
            general[DIRECT[key]] = value
        elif key in UNMAPPED:
            unmapped[key] = {"value": value, "reason": UNMAPPED[key]}
        elif key not in SPECIAL:
            family = next((prefix for prefix in SHORTCUT_FAMILIES if key.startswith(prefix)), None)
            if not family:
                raise MigrationError(f"No migration decision exists for source setting {key}")
            actions[SHORTCUT_FAMILIES[family] + key[len(family):]] = [shortcut(item) for item in value if item]

    general["hideInFullscreen"] = not values["autohide-in-fullscreen"]
    general["transparencyMode"] = {"DEFAULT": 2, "FIXED": 0, "DYNAMIC": 1}[values["transparency-mode"]]
    general["visualStyle"] = 3 if values["apply-custom-theme"] else 0
    if values["custom-background-color"] and not values["apply-custom-theme"]:
        general["visualStyle"] = 1
    general["cornerRadius"] = 0 if values["force-straight-corner"] else 16
    general["customFolders"] = list(dict.fromkeys(folder_url(item, source_home) for item in values["custom-stacks"]))
    preferences = {}
    for source_key, field, allowed in (("stack-view-overrides", "view", {"AUTOMATIC": 0, "FAN": 1, "GRID": 2, "LIST": 3}),
                                       ("stack-sort-overrides", "sort", {"NAME": 0, "DATE_MODIFIED": 1, "KIND": 2})):
        for key, value in values[source_key].items():
            if value not in allowed:
                raise MigrationError(f"{source_key}: invalid preference {value!r}")
            target = stack_key(key, source_home)
            preferences.setdefault(target, {})[field] = allowed[value]
            if key == "applications":
                preferences.setdefault("applications-button", {})[field] = allowed[value]
    general["stackOverrides"] = json.dumps(preferences, sort_keys=True, separators=(",", ":"))
    actions["goshosdock-show-overlay"] = [shortcut(item) for item in values["shortcut"] if item]

    if favorites is not None:
        favorites = parse_value("favorites", favorites, {"type": "as", "range": {}})
        general["launchers"] = [item if re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", item) else "applications:" + item for item in favorites]
    if not isinstance(mappings, dict) or not all(isinstance(key, str) and isinstance(value, str) and re.fullmatch(r"[A-Za-z0-9_.@+-]+\.desktop", key)
                                               and re.fullmatch(r"[A-Za-z0-9_.@+-]+\.desktop", value) for key, value in mappings.items()):
        raise MigrationError("launcherMappings must map desktop file IDs to desktop file IDs")
    return {"formatVersion": 1, "sourceSchema": SCHEMA_ID, "General": general,
            "globalShortcuts": {"component": "org.gosh.goshosdock", "actions": actions},
            # Reimporting an explicit custom/disabled base binding while Plasma
            # is stopped must override an earlier native-default conflict marker.
            "nativeShortcutDefaults": {"file": "goshosdock-shortcutsrc", "group": "NativeDefaults",
                                       "entries": {str(index): False for index in range(1, 11) if f"app-hotkey-{index}" in provided}},
            "unityLauncherMappings": mappings, "unmapped": unmapped,
            "sourceValues": values, "explicitKeys": sorted(provided)}


def kconfig_escape(value, list_element=False):
    text = str(value).replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
    return text.replace(",", "\\,") if list_element else text


def render_kconfig(result):
    lines = ["# Review before applying to the dock's General configuration group.", "[General]"]
    for key, value in sorted(result["General"].items()):
        if type(value) is bool:
            text = "true" if value else "false"
        elif isinstance(value, list):
            text = ",".join(kconfig_escape(item, list_element=True) for item in value)
        else:
            text = kconfig_escape(value)
        lines.append(key + "=" + text)
    # These belong to separate KDE facilities. Keep them reviewable without
    # injecting unrelated groups into an applet's configuration file.
    for key in ("globalShortcuts", "nativeShortcutDefaults", "unityLauncherMappings", "unmapped"):
        lines.append("# " + key + ": " + json.dumps(result[key], sort_keys=True, ensure_ascii=True))
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="JSON export of GNOME dock settings")
    parser.add_argument("--format", choices=("json", "kconfig"), default="json")
    parser.add_argument("--output", type=Path, help="Write a new review file; existing files are never replaced")
    parser.add_argument("--source-home", help="Original absolute home directory, required for custom folders using ~")
    options = parser.parse_args(argv)
    try:
        if options.source_home and not options.source_home.startswith("/"):
            raise MigrationError("--source-home must be an absolute path")
        raw = options.input.read_bytes()
        if len(raw) > MAX_INPUT_BYTES:
            raise MigrationError("Settings export exceeds the 1 MiB limit")
        document = json.loads(raw, parse_constant=lambda value: (_ for _ in ()).throw(MigrationError(f"Invalid JSON constant {value}")))
        result = migrate(document, options.source_home)
        rendered = render_kconfig(result) if options.format == "kconfig" else json.dumps(result, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
        if options.output:
            with options.output.open("x", encoding="utf-8") as destination:
                destination.write(rendered)
        else:
            sys.stdout.write(rendered)
        return 0
    except (MigrationError, OSError, ValueError, RecursionError) as error:
        parser.exit(2, f"Migration failed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
