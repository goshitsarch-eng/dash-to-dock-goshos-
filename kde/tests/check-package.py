#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject missing embedded files, invalid metadata, and dangling setting bindings."""
import json
import re
from pathlib import Path
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
src = root / 'src'
metadata = json.loads((src / 'metadata.json').read_text())
assert 'plasma_add_applet(org.gosh.goshosdock' in (src / 'CMakeLists.txt').read_text()
assert metadata['KPlugin']['License'] == 'GPL-2.0-or-later'
ns = {'k': 'http://www.kde.org/standards/kcfg/1.0'}
entries = ET.parse(src / 'main.xml').findall('.//k:entry', ns)
names = [entry.attrib['name'] for entry in entries]
assert len(names) == len(set(names)), 'duplicate KConfig entries'
for entry in entries:
    default = entry.find('k:default', ns)
    assert default is not None, f'missing default: {entry.attrib["name"]}'
    if entry.attrib['type'] in ('Int', 'Double'):
        value = float(default.text)
        for bound, compare in [('min', lambda x, y: x >= y), ('max', lambda x, y: x <= y)]:
            limit = entry.find('k:' + bound, ns)
            assert limit is None or compare(value, float(limit.text)), entry.attrib['name']

snapshot = (src / 'qml/code/ConfigSnapshot.js').read_text()
snapshot_names = json.loads(re.search(r'const keys = (\[.*?\]);', snapshot, re.S).group(1))
assert set(snapshot_names) == set(names), 'configuration snapshot must cover every schema entry'

used = set()
for file in (src / 'qml').rglob('*'):
    if file.suffix in ('.qml', '.js'):
        text = file.read_text()
        used.update(re.findall(r'configuration\.([A-Za-z]\w*)', text))
        used.update(re.findall(r'\bcfg_(\w+)', text))
assert not (used - set(names)), f'undefined configuration: {sorted(used-set(names))}'

cmake = (src / 'CMakeLists.txt').read_text()
embedded = re.findall(r'^\s+(qml/\S+\.(?:qml|js))\s*$', cmake, re.M)
for name in embedded:
    assert (src / name).is_file(), f'missing embedded source: {name}'
actual = {str(path.relative_to(src)) for path in (src/'qml').rglob('*') if path.suffix in ('.qml', '.js')}
assert actual == set(embedded), f'unregistered QML/JS: {sorted(actual-set(embedded))}'
print(f'Package metadata, {len(entries)} settings, and {len(embedded)} embedded QML/JS files validated')
