// SPDX-License-Identifier: GPL-2.0-or-later
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
function load(name) {
    const context = vm.createContext({});
    vm.runInContext(fs.readFileSync(path.join(__dirname, '../src/qml/code', name), 'utf8')
        .replace(/^\.pragma library\s*$/m, ''), context);
    return context;
}
const actions = load('DockActions.js');
const stacks = load('StackLogic.js');
test('recent applications dispatch to hidden native running tasks before launcher duplicates', () => {
    const launcher = {visible: true, model: {IsLauncher: true, LauncherUrlWithoutIcon: 'applications:org.kde.kate.desktop'}};
    const running = {visible: false, model: {IsGroupParent: true, AppId: 'org.kde.kate'}};
    const unrelated = {visible: true, model: {IsWindow: true, AppId: 'org.kde.konsole'}};
    assert.equal(stacks.applicationTask([launcher, unrelated, running], 'file:///usr/share/applications/org.kde.kate.desktop'), running);
    assert.equal(stacks.applicationTask([launcher], 'org.kde.kate.desktop'), launcher);
    assert.equal(stacks.applicationTask([launcher, running], 'org.kde.dolphin'), null);
    assert.equal(stacks.applicationTask([launcher], ''), null);
});
test('number shortcuts follow tasks then recent apps and devices, skipping stacks and hidden tasks', () => {
    const task = {visible: true, modelIndex() {}};
    const hidden = {visible: false, modelIndex() {}};
    const extras = ['recent', 'applications', 'divider', 'folder', 'trash', 'device']
        .map(kind => ({visible: true, entry: {kind}}));
    const targets = stacks.shortcutTargets([task, hidden], extras);
    assert.equal(targets.length, 4);
    assert.equal(targets[0], task);
    assert.equal(targets[1], extras[0]);
    assert.equal(targets[2], extras[4]);
    assert.equal(targets[3], extras[5]);
    assert.equal(stacks.shortcutNumber(targets, hidden), -1);
    const many = Array.from({length: 12}, () => ({visible: true, modelIndex() {}}));
    assert.equal(stacks.shortcutNumber(many, many[0]), 1);
    assert.equal(stacks.shortcutNumber(many, many[9]), 0);
    assert.equal(stacks.shortcutNumber(many, many[10]), -1);
});
const running = { running: true, count: 1, active: false, urgent: false,
    middle: false, modified: false, control: false, spreadAvailable: true };
const resolve = (action, state = {}) => actions.resolve(action, {...running, ...state});

test('non-running launchers and Ctrl click always launch, including quit mode', () => {
    for (const action of ['skip', 'quit', 'previews', 'minimize', 'launch']) {
        assert.equal(resolve(action, {running:false}), 'launch');
        assert.equal(resolve(action, {control:true}), 'launch');
    }
});
test('plain click focus/minimize/preview follows active window and group size', () => {
    assert.equal(resolve('focus-minimize-or-previews'), 'activate');
    assert.equal(resolve('focus-minimize-or-previews', {active:true}), 'minimize');
    assert.equal(resolve('focus-minimize-or-previews', {active:true,count:3}), 'previews');
    assert.equal(resolve('focus-minimize-or-previews', {active:true,count:3,urgent:true}), 'activate');
    assert.equal(resolve('focus-or-previews', {active:true}), 'activate');
    assert.equal(resolve('focus-or-previews', {active:true,middle:true}), 'previews');
});
test('minimize/cycle/quit/skip match existing running-app semantics', () => {
    assert.equal(resolve('minimize'), 'activate');
    assert.equal(resolve('minimize', {active:true}), 'minimize');
    assert.equal(resolve('minimize', {middle:true}), 'minimize');
    assert.equal(resolve('cycle-windows', {active:true,count:3}), 'cycle');
    assert.equal(resolve('cycle-windows', {active:true,count:3,urgent:true}), 'activate');
    assert.equal(resolve('quit'), 'close');
    assert.equal(resolve('skip'), 'activate');
});
test('overview and previews treat a single plain-click window differently from modifiers', () => {
    assert.equal(resolve('minimize-or-overview', {active:true}), 'minimize');
    assert.equal(resolve('minimize-or-overview', {count:2}), 'overview');
    assert.equal(resolve('minimize-or-overview', {modified:true}), 'overview');
    assert.equal(resolve('previews'), 'activate');
    assert.equal(resolve('previews', {count:2}), 'previews');
    assert.equal(resolve('minimize-or-previews', {active:true}), 'minimize');
    assert.equal(resolve('minimize-or-previews', {active:true,middle:true}), 'previews');
});
test('application spread falls back to previews when KWin effect is unavailable', () => {
    assert.equal(resolve('focus-or-appspread', {active:true,count:2}), 'spread');
    assert.equal(resolve('focus-or-appspread', {active:true,count:2,spreadAvailable:false}), 'previews');
    assert.equal(resolve('focus-minimize-or-appspread', {active:true}), 'minimize');
    assert.equal(resolve('focus-or-appspread', {active:true,count:2,urgent:true}), 'activate');
});
test('workspace cycling wraps and tolerates no desktops', () => {
    assert.equal(actions.nextDesktop(['a','b','c'], 'a', -1), 'c');
    assert.equal(actions.nextDesktop(['a','b','c'], 'c', 1), 'a');
    assert.equal(actions.nextDesktop([], 'a', 1), null);
});
test('per-stack preferences persist independently and corrupt state recovers', () => {
    let state = stacks.setPreference('{bad', 'file:///a', 'view', 1);
    state = stacks.setPreference(state, 'sftp://host/b', 'sort', 2);
    state = stacks.setPreference(state, 'file:///a', 'sort', 1);
    assert.equal(stacks.preference(state, 'file:///a', 'view', 0), 1);
    assert.equal(stacks.preference(state, 'sftp://host/b', 'sort', 0), 2);
    state = stacks.setPreference(state, 'file:///a', 'view', -1);
    assert.equal(stacks.preference(state, 'file:///a', 'view', 3), 3);
    assert.equal(stacks.preference(state, 'file:///a', 'sort', 0), 1);
    assert.equal(stacks.preference(stacks.withoutStack(state, 'file:///a'), 'sftp://host/b', 'sort', 0), 2);
    assert.equal(stacks.preference('{"x":{"view":99}}', 'x', 'view', 0), 0);
});
test('recent applications exclude pinned/running aliases and duplicates before limiting', () => {
    const entries = [{url:'applications:org.kde.dolphin.desktop'},
        {url:'applications:org.kde.konsole.desktop'},
        {url:'applications:firefox.desktop'}, {url:'applications:firefox.desktop'},
        {url:'applications:org.kde.kate.desktop'}];
    const result = stacks.recentApplications(entries,
        ['file:///usr/share/applications/org.kde.dolphin.desktop', 'applications:org.kde.konsole.desktop'], 2);
    assert.equal(result.length, 2);
    assert.equal(result[0].url, 'applications:firefox.desktop');
    assert.equal(result[1].url, 'applications:org.kde.kate.desktop');
});
test('folder sorting excludes hidden/backup files without mutating backend order', () => {
    const entries = [{name:'z.txt',modified:30,mimeType:'text/plain'},
        {name:'.hidden'}, {name:'backup~'}, {name:'a',modified:10,isDir:true,mimeType:'inode/directory'},
        {name:'b.txt',modified:20,mimeType:'text/plain'}];
    assert.equal(stacks.visibleEntries(entries, 0, '', false).map(x=>x.name).join(','), 'a,b.txt,z.txt');
    assert.equal(stacks.visibleEntries(entries, 1, '', false).map(x=>x.name).join(','), 'z.txt,b.txt,a');
    assert.equal(stacks.visibleEntries(entries, 2, '', false)[0].name, 'a');
    assert.equal(stacks.visibleEntries(entries, 0, 'TXT', false).length, 2);
    assert.equal(entries[0].name, 'z.txt');
});
test('automatic application stacks use grids and explicit fan choices are preserved', () => {
    assert.equal(stacks.effectiveView(0, 10, false), 1);
    assert.equal(stacks.effectiveView(0, 11, false), 2);
    assert.equal(stacks.effectiveView(0, 4, true), 2);
    assert.equal(stacks.effectiveView(1, 4, true), 1);
    assert.equal(stacks.effectiveView(3, 4, true), 3);
    assert.equal(stacks.itemLimit(500, 1), 10);
    assert.equal(stacks.itemLimit(9999, 2), 500);
    assert.equal(stacks.nameForUrl('file:///tmp/My%20Files/'), 'My Files');
});

test('desktop IDs preserve case and decode local file URLs', () => {
    assert.equal(stacks.applicationId('applications:Org.Example.desktop'), 'Org.Example');
    assert.equal(stacks.applicationId('file:///usr/share/applications/My%20App.desktop'), 'My App');
    assert.equal(stacks.recentApplications([{url:'applications:Foo.desktop'}, {url:'applications:foo.desktop'}], ['applications:Foo.desktop'], 10).length, 1);
});

const geometry = load('DockGeometry.js');
test('preview scaling uses window geometry and preserves aspect ratio within the output', () => {
    const automatic = geometry.previewSize(1600, 1000, 0, 1920, 1080);
    assert.equal(automatic.width, 240);
    assert.equal(automatic.height, 150);
    const scaled = geometry.previewSize(800, 600, 0.5, 1920, 1080);
    assert.equal(scaled.width, 400);
    assert.equal(scaled.height, 300);
    const bounded = geometry.previewSize(1600, 1000, 1, 800, 400);
    assert.equal(bounded.width, 640);
    assert.equal(bounded.height, 400);
    assert.equal(geometry.previewSize(100, 80, 0, 1920, 1080).width, 100);
    assert.equal(geometry.previewSize(undefined, 0, 0, 1920, 1080).width, 250);
});
test('Applications can stay at either outer edge or join the centered task and stack strip', () => {
    for (const atStart of [false, true]) {
        const fixed = geometry.dockSlots(1000, 300, 100, 50, atStart, true, true);
        assert.equal(fixed.appsStart, atStart ? 0 : 950);
        assert.equal(fixed.taskStart, atStart ? 325 : 275);
        const joined = geometry.dockSlots(1000, 300, 100, 50, atStart, false, true);
        assert.equal(joined.appsStart, atStart ? 275 : 675);
        assert.equal(joined.taskStart, atStart ? 325 : 275);
        const overflow = geometry.dockSlots(200, 400, 100, 50, atStart, true, true);
        assert.equal(overflow.taskLength, 50);
        assert.ok(overflow.extrasStart + overflow.extrasLength <= 200);
    }
});
test('adaptive opacity includes near windows across an offset output edge', () => {
    const screen = {x: 1920, y: 100, width: 1280, height: 720};
    const dock = {x: 2100, y: 772, width: 400, height: 48};
    const region = geometry.edgeRegion(screen, dock, 'bottom', false);
    assert.equal(region.x, 1920);
    assert.equal(region.width, 1280);
    assert.equal(region.y, 767);
    assert.equal(region.height, 53);
});

test('overview clicks activate rather than cycling or minimizing except explicit modifiers', () => {
    for (const action of ['cycle-windows', 'previews', 'minimize-or-previews']) {
        assert.equal(resolve(action, {overviewVisible:true, active:true, count:3}), 'activate');
    }
    assert.equal(resolve('minimize', {overviewVisible:true, active:true, middle:true}), 'activate');
    assert.equal(resolve('minimize', {overviewVisible:true, active:true, modified:true}), 'minimize');
    assert.equal(resolve('focus-minimize-or-previews', {overviewVisible:true, active:true}), 'none');
    assert.equal(resolve('focus-minimize-or-previews', {overviewVisible:true, active:true, count:2}), 'previews');
    assert.equal(resolve('minimize-or-overview', {overviewVisible:true, active:true, count:2}), 'overview');
});

test('adaptive opacity includes the actual floating-panel offset', () => {
    const screen = {x: 0, y: 0, width: 1280, height: 720};
    const bottom = geometry.edgeRegion(screen, {x:400,y:656,width:400,height:48}, 'bottom');
    assert.equal(bottom.y, 651);
    assert.equal(bottom.height, 69);
    const left = geometry.edgeRegion(screen, {x:16,y:100,width:48,height:400}, 'left');
    assert.equal(left.width, 69);
});
