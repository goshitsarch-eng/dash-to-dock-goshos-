#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import configparser
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

SCRIPT = Path(__file__).resolve().parents[1] / "tools/migrate_settings.py"
SPEC = importlib.util.spec_from_file_location("migrate_settings", SCRIPT)
migration = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(migration)


class MigrationTest(unittest.TestCase):
    def test_all_source_keys_have_an_explicit_decision(self):
        result = migration.migrate({})
        self.assertEqual(len(result["sourceValues"]), 127)
        self.assertEqual(len(result["globalShortcuts"]["actions"]), 31)
        self.assertEqual(set(result["unmapped"]), set(migration.UNMAPPED))
        namespace = {"k": "http://www.kde.org/standards/kcfg/1.0"}
        schema = ET.parse(SCRIPT.parents[1] / "src/main.xml")
        names = {entry.attrib["name"] for entry in schema.findall(".//k:entry", namespace)}
        self.assertFalse(set(result["General"]) - names)

    def test_source_defaults_and_visibility_enum(self):
        config = migration.migrate({})["General"]
        self.assertEqual(config["dockPosition"], 2)
        self.assertEqual(config["intelliHideMode"], 1)
        self.assertFalse(config["dockFixed"])
        self.assertTrue(config["hideInFullscreen"])
        self.assertEqual(config["transparencyMode"], 2)
        self.assertEqual(config["clickAction"], "cycle-windows")
        self.assertEqual(config["shiftClickAction"], "minimize")
        self.assertEqual(config["minimizedOpacity"], 0.55)

    def test_native_and_gvariant_values(self):
        result = migration.migrate({"settings": {
            "dock-position": "'LEFT'", "intellihide-mode": 2,
            "autohide-in-fullscreen": "true", "show-delay": "0.35",
            "magnification-enabled": True, "click-action": 9,
            "custom-stacks": "@as []",
        }})["General"]
        self.assertEqual(result["dockPosition"], 3)
        self.assertEqual(result["intelliHideMode"], 2)
        self.assertFalse(result["hideInFullscreen"])
        self.assertEqual(result["showDelay"], 0.35)
        self.assertEqual(result["clickAction"], "focus-minimize-or-previews")
        self.assertEqual(result["customFolders"], [])

    def test_folders_and_both_per_stack_preferences(self):
        config = migration.migrate({
            "custom-stacks": ["~/Work files/", "/home/person/Work files", "smb://server/share"],
            "stack-view-overrides": {"custom:file:///home/person/Work%20files/": "FAN", "applications": "LIST"},
            "stack-sort-overrides": {"custom:file:///home/person/Work%20files": "DATE_MODIFIED", "documents": "KIND"},
        }, source_home="/home/person")["General"]
        self.assertEqual(config["customFolders"], ["file:///home/person/Work%20files", "smb://server/share"])
        prefs = json.loads(config["stackOverrides"])
        self.assertEqual(prefs["file:///home/person/Work%20files"], {"view": 1, "sort": 1})
        self.assertEqual(prefs["applications-stack"], {"view": 3})
        self.assertEqual(prefs["applications-button"], {"view": 3})
        self.assertEqual(prefs["documents"], {"sort": 2})

    def test_original_home_must_be_explicit(self):
        with self.assertRaisesRegex(migration.MigrationError, "source-home"):
            migration.migrate({"custom-stacks": ["~/Documents"]})
        self.assertEqual(migration.migrate({"custom-stacks": ["/"]})["General"]["customFolders"], ["file:///"])

    def test_favorites_and_launcher_aliases_survive(self):
        result = migration.migrate({
            migration.SCHEMA_ID: {},
            "org.gnome.shell": {"favorite-apps": ["org.kde.dolphin.desktop", "applications:org.kde.konsole.desktop"]},
            "launcherMappings": {"app-beta.desktop": "app.desktop"},
        })
        self.assertEqual(result["General"]["launchers"], ["applications:org.kde.dolphin.desktop", "applications:org.kde.konsole.desktop"])
        self.assertEqual(result["unityLauncherMappings"], {"app-beta.desktop": "app.desktop"})

    def test_shortcuts_preserve_disabled_and_multiple_bindings(self):
        actions = migration.migrate({"app-hotkey-1": [], "app-ctrl-hotkey-2": ["<Super><Control>2", "<Alt>F2"], "shortcut": ["<Super>q"]})["globalShortcuts"]["actions"]
        self.assertEqual(actions["goshosdock-activate-1"], [])
        self.assertEqual(actions["goshosdock-launch-2"], ["Meta+Ctrl+2", "Alt+F2"])
        self.assertEqual(actions["goshosdock-show-overlay"], ["Meta+Q"])

    def test_explicit_base_shortcuts_clear_prior_native_default_markers(self):
        result = migration.migrate({"app-hotkey-1": [], "app-hotkey-4": ["<Alt>F4"], "app-hotkey-10": ["<Super>0"],
                                    "app-ctrl-hotkey-2": ["<Alt>F2"]})
        expected = {"file": "goshosdock-shortcutsrc", "group": "NativeDefaults", "entries": {"1": False, "4": False, "10": False}}
        self.assertEqual(result["nativeShortcutDefaults"], expected)
        self.assertEqual(migration.migrate({})["nativeShortcutDefaults"]["entries"], {})
        rendered = migration.render_kconfig(result)
        supplemental = next(line.removeprefix("# nativeShortcutDefaults: ") for line in rendered.splitlines()
                            if line.startswith("# nativeShortcutDefaults: "))
        self.assertEqual(json.loads(supplemental), expected)
        config = configparser.ConfigParser(interpolation=None)
        config.read_string(rendered)
        self.assertEqual(config.sections(), ["General"])

    def test_style_flags_and_transparency_are_independent(self):
        config = migration.migrate({"apply-custom-theme": True, "custom-background-color": False, "customize-alphas": True,
                                    "transparency-mode": "DYNAMIC", "force-straight-corner": True})["General"]
        self.assertEqual(config["visualStyle"], 3)
        self.assertFalse(config["customBackgroundColor"])
        self.assertTrue(config["customizeAlphas"])
        self.assertEqual(config["transparencyMode"], 1)
        self.assertEqual(config["cornerRadius"], 0)

    def test_invalid_input_is_rejected_without_evaluation(self):
        for settings in ({"unknown-key": True}, {"magnification-factor": 5}, {"magnification-factor": "nan"},
                         {"dock-position": "DIAGONAL"}, {"show-trash": 1}, {"custom-stacks": "__import__('os').system('false')"},
                         {"stack-view-overrides": {"documents": "invalid"}}, {"custom-stacks": "@s []"}):
            with self.subTest(settings=settings), self.assertRaises(migration.MigrationError):
                migration.migrate(settings)

    def test_kconfig_values_cannot_inject_sections(self):
        result = migration.migrate({"background-color": "white\n[Injected]\nDanger=true", "custom-stacks": ["file:///tmp/a,b"]})
        text = migration.render_kconfig(result)
        config = configparser.ConfigParser(interpolation=None)
        config.read_string(text)
        self.assertEqual(config.sections(), ["General"])
        self.assertIn(r"customFolders=file:///tmp/a\,b", text)
        self.assertIn(r"customColor=white\n[Injected]\nDanger=true", text)

    def test_cli_creates_a_review_file_and_never_overwrites(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "source.json"
            output = Path(temporary) / "review.json"
            source.write_text(json.dumps({"dock-position": "TOP"}))
            result = subprocess.run([sys.executable, str(SCRIPT), str(source), "--output", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(output.read_text())["General"]["dockPosition"], 0)
            old = output.read_bytes()
            result = subprocess.run([sys.executable, str(SCRIPT), str(source), "--output", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(output.read_bytes(), old)


if __name__ == "__main__":
    unittest.main()
