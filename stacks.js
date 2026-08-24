// -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil -*-

/**
 * macOS-like dock "stacks".
 *
 * A stack is a dock item that pops up the contents of something — the list of
 * installed applications, a folder such as Documents or Downloads, or any
 * folder the user picks — as a fan, a grid or a jump list rising out of the
 * dock, instead of taking over the whole screen.
 *
 * Stacks live in their own container next to the dash scroll view, so the
 * regular application redisplay logic in `dash.js` is left completely alone.
 */

import {
    Clutter,
    Gio,
    GLib,
    GObject,
    Pango,
    Shell,
    St,
} from './dependencies/gi.js';

import {
    BoxPointer,
    Dash,
    IconGrid,
    Main,
    PopupMenu,
} from './dependencies/shell/ui.js';

import {
    ParentalControlsManager,
} from './dependencies/shell/misc.js';

import {
    AppIcons,
    Docking,
    Magnification,
    Theming,
    Utils,
} from './imports.js';

import {Extension} from './dependencies/shell/extensions/extension.js';

const {gettext: __} = Extension;

export const StackKind = Object.freeze({
    APPLICATIONS: 'applications',
    DOCUMENTS: 'documents',
    DOWNLOADS: 'downloads',
    HOME: 'home',
    CUSTOM: 'custom',
});

/** Must match the stack-view enum in the gschema. */
export const StackView = Object.freeze({
    AUTOMATIC: 0,
    FAN: 1,
    GRID: 2,
    LIST: 3,
});

/** Must match the stack-sort enum in the gschema. */
export const StackSort = Object.freeze({
    NAME: 0,
    DATE_MODIFIED: 1,
    KIND: 2,
});

// The nicks the per-stack override dictionaries are stored with.
const StackViewNicks = Object.freeze(['AUTOMATIC', 'FAN', 'GRID', 'LIST']);
const StackSortNicks = Object.freeze(['NAME', 'DATE_MODIFIED', 'KIND']);

/** Must match the show-apps-button-action enum in the gschema. */
export const ShowAppsAction = Object.freeze({
    OVERVIEW: 0,
    STACK: 1,
});

const FILE_ATTRIBUTES = [
    Gio.FILE_ATTRIBUTE_STANDARD_NAME,
    Gio.FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
    Gio.FILE_ATTRIBUTE_STANDARD_TYPE,
    Gio.FILE_ATTRIBUTE_STANDARD_ICON,
    Gio.FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
    Gio.FILE_ATTRIBUTE_STANDARD_IS_BACKUP,
    Gio.FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
    Gio.FILE_ATTRIBUTE_TIME_MODIFIED,
].join(',');

const ENUMERATE_BATCH = 100;
const HARD_ITEM_LIMIT = 500;
const FAN_MAX_ITEMS = 10;
const GRID_MAX_COLUMNS = 6;
const LIST_ICON_SIZE = 22;
// Rough height of one jump list row, used to keep the popup on screen.
const LIST_ROW_HEIGHT = 34;
const TILE_ICON_SIZE = 48;
const FAN_TILE_GAP = 6;
const GRID_SPACING = 6;

const promisifiedPrototypes = new Set();

/**
 * `Gio.File` methods live on an interface, so they have to be promisified on
 * the concrete implementation prototype. Doing that once per implementation is
 * enough.
 *
 * @param {Gio.File} file the file whose implementation must be promisified
 */
function ensureFilePromises(file) {
    const proto = file.constructor.prototype;

    if (!promisifiedPrototypes.has(proto)) {
        promisifiedPrototypes.add(proto);
        Gio._promisify(proto, 'enumerate_children_async', 'enumerate_children_finish');
        Gio._promisify(proto, 'query_info_async', 'query_info_finish');
    }

    if (!promisifiedPrototypes.has(Gio.FileEnumerator.prototype)) {
        promisifiedPrototypes.add(Gio.FileEnumerator.prototype);
        Gio._promisify(Gio.FileEnumerator.prototype, 'next_files_async', 'next_files_finish');
        Gio._promisify(Gio.FileEnumerator.prototype, 'close_async', 'close_finish');
    }
}

/**
 * Turn a user supplied path or URI into a `Gio.File`.
 *
 * @param {string} pathOrUri an absolute path, a `~` path or an URI
 * @returns {?Gio.File} the folder, or null when the string is empty
 */
export function fileForUserPath(pathOrUri) {
    const trimmed = pathOrUri?.trim();
    if (!trimmed)
        return null;

    if (trimmed.includes('://'))
        return Gio.File.new_for_uri(trimmed);

    const path = trimmed.startsWith('~')
        ? GLib.build_filenamev([GLib.get_home_dir(), trimmed.slice(1)]) : trimmed;
    return Gio.File.new_for_path(path);
}

/**
 * The `Gio.File` a stack points at.
 *
 * @param {object} descriptor a stack descriptor
 * @returns {?Gio.File} the target folder, or null for non-folder stacks
 */
export function getStackLocation(descriptor) {
    switch (descriptor?.kind) {
    case StackKind.DOCUMENTS:
        return fileForUserPath(
            GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_DOCUMENTS));
    case StackKind.DOWNLOADS:
        return fileForUserPath(
            GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_DOWNLOAD));
    case StackKind.HOME:
        return fileForUserPath(GLib.get_home_dir());
    case StackKind.CUSTOM:
        return fileForUserPath(descriptor.path);
    default:
        return null;
    }
}

/**
 * The user visible name of a stack. Folders added by the user are named after
 * the folder itself, like they are on macOS.
 *
 * @param {object} descriptor a stack descriptor
 * @returns {string} the stack name
 */
export function getStackName(descriptor) {
    switch (descriptor?.kind) {
    case StackKind.APPLICATIONS:
        return __('Applications');
    case StackKind.DOCUMENTS:
        return __('Documents');
    case StackKind.DOWNLOADS:
        return __('Downloads');
    case StackKind.HOME:
        return __('Home');
    case StackKind.CUSTOM:
        return getStackLocation(descriptor)?.get_basename() ?? __('Folder');
    default:
        return __('Folder');
    }
}

/**
 * The view a stack must use: its own choice from the right click menu, or the
 * default from the preferences.
 *
 * @param {object} descriptor a stack descriptor
 * @returns {number} a `StackView` value
 */
export function getStackView(descriptor) {
    const {settings} = Docking.DockManager;
    const nick = settings.stackViewOverrides?.[descriptor.id];
    const index = StackViewNicks.indexOf(nick);
    return index !== -1 ? index : settings.stackView;
}

/**
 * The sort order a stack must use.
 *
 * @param {object} descriptor a stack descriptor
 * @returns {number} a `StackSort` value
 */
export function getStackSort(descriptor) {
    const {settings} = Docking.DockManager;
    const nick = settings.stackSortOverrides?.[descriptor.id];
    const index = StackSortNicks.indexOf(nick);
    return index !== -1 ? index : settings.stackSort;
}

/**
 * @param {string} key the settings key holding the override dictionary
 * @param {string} id the stack id
 * @param {string} nick the value to store
 */
function setStackOverride(key, id, nick) {
    const {settings} = Docking.DockManager;
    const camelKey = key.replace(/-([a-z\d])/g, k => k[1].toUpperCase());
    const overrides = {...settings[camelKey]};
    overrides[id] = nick;
    settings.set_value(key, new GLib.Variant('a{ss}', overrides));
}

/**
 * @param {object} descriptor a stack descriptor
 * @param {number} view a `StackView` value
 */
export function setStackView(descriptor, view) {
    setStackOverride('stack-view-overrides', descriptor.id, StackViewNicks[view]);
}

/**
 * @param {object} descriptor a stack descriptor
 * @param {number} sort a `StackSort` value
 */
export function setStackSort(descriptor, sort) {
    setStackOverride('stack-sort-overrides', descriptor.id, StackSortNicks[sort]);
}

/**
 * The fallback themed icon name for a stack.
 *
 * @param {string} kind one of `StackKind`
 * @returns {string} an icon name that is present in every icon theme
 */
function getStackFallbackIconName(kind) {
    switch (kind) {
    case StackKind.APPLICATIONS:
        return 'view-app-grid-symbolic';
    case StackKind.DOCUMENTS:
        return 'folder-documents';
    case StackKind.DOWNLOADS:
        return 'folder-download';
    case StackKind.HOME:
        return 'user-home';
    default:
        return 'folder';
    }
}

/**
 * Every stack that is currently in the dock, in dock order. A descriptor is
 * `{id, kind, path}`; the id is what per-stack view and sort choices are keyed
 * on, so each folder keeps its own choice.
 *
 * @returns {object[]} the stack descriptors
 */
export function getStackDescriptors() {
    const {settings} = Docking.DockManager;
    const descriptors = [];

    if (settings.showApplicationsStack)
        descriptors.push({id: StackKind.APPLICATIONS, kind: StackKind.APPLICATIONS});
    if (settings.showDocumentsStack)
        descriptors.push({id: StackKind.DOCUMENTS, kind: StackKind.DOCUMENTS});
    if (settings.showDownloadsStack)
        descriptors.push({id: StackKind.DOWNLOADS, kind: StackKind.DOWNLOADS});
    if (settings.showHomeStack)
        descriptors.push({id: StackKind.HOME, kind: StackKind.HOME});

    const seen = new Set();
    (settings.customStacks ?? []).forEach(path => {
        const file = fileForUserPath(path);
        if (!file || seen.has(file.get_uri()))
            return;
        seen.add(file.get_uri());
        descriptors.push({
            id: `${StackKind.CUSTOM}:${file.get_uri()}`,
            kind: StackKind.CUSTOM,
            path,
        });
    });

    return descriptors;
}

/**
 * Remove a folder the user added to the dock.
 *
 * @param {object} descriptor the stack descriptor to drop
 */
export function removeCustomStack(descriptor) {
    const {settings} = Docking.DockManager;
    const target = fileForUserPath(descriptor.path)?.get_uri();
    const kept = (settings.customStacks ?? []).filter(path =>
        fileForUserPath(path)?.get_uri() !== target);
    settings.set_strv('custom-stacks', kept);
}

/**
 * A single entry inside a stack popup.
 */
class StackEntry {
    constructor(params) {
        this.name = params.name;
        this.gicon = params.gicon ?? null;
        this.uri = params.uri ?? null;
        this.app = params.app ?? null;
        this.isDirectory = params.isDirectory ?? false;
        this.contentType = params.contentType ?? '';
        this.modified = params.modified ?? 0;
    }

    activate(timestamp) {
        if (this.app) {
            this.app.activate();
            return;
        }

        if (!this.uri)
            return;

        try {
            const context = global.create_app_launch_context(timestamp ?? 0, -1);
            Gio.AppInfo.launch_default_for_uri(this.uri, context);
        } catch (e) {
            logError(e, `Failed to open ${this.uri}`);
            Main.notifyError(__('Could not open the selected item'), e.message);
        }
    }
}

/**
 * Reads the contents a stack should show.
 */
class StackContents {
    constructor(descriptor) {
        this._descriptor = descriptor;
        this._cancellable = null;
    }

    destroy() {
        this._cancellable?.cancel();
        this._cancellable = null;
    }

    get location() {
        return getStackLocation(this._descriptor);
    }

    async read() {
        this._cancellable?.cancel();
        this._cancellable = new Gio.Cancellable();

        const entries = this._descriptor.kind === StackKind.APPLICATIONS
            ? readApplications() : await this._readFolder(this._cancellable);

        return sortEntries(entries, this._descriptor);
    }

    async _readFolder(cancellable) {
        const {location} = this;
        if (!location)
            return [];

        ensureFilePromises(location);

        const entries = [];
        let enumerator = null;

        try {
            enumerator = await location.enumerate_children_async(FILE_ATTRIBUTES,
                Gio.FileQueryInfoFlags.NONE, GLib.PRIORITY_DEFAULT, cancellable);

            while (entries.length < HARD_ITEM_LIMIT) {
                // eslint-disable-next-line no-await-in-loop
                const infos = await enumerator.next_files_async(ENUMERATE_BATCH,
                    GLib.PRIORITY_DEFAULT, cancellable);

                if (!infos.length)
                    break;

                infos.forEach(info => {
                    if (info.get_is_hidden() || info.get_is_backup())
                        return;

                    const child = location.get_child(info.get_name());
                    entries.push(new StackEntry({
                        name: info.get_display_name() || info.get_name(),
                        gicon: info.get_icon(),
                        uri: child.get_uri(),
                        isDirectory: info.get_file_type() === Gio.FileType.DIRECTORY,
                        contentType: info.get_content_type() ?? '',
                        modified: info.get_modification_date_time?.()?.to_unix() ?? 0,
                    }));
                });
            }
        } catch (e) {
            if (!e.matches?.(Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED))
                logError(e, `Could not read the ${this._descriptor.id} stack`);
        } finally {
            try {
                await enumerator?.close_async(GLib.PRIORITY_DEFAULT, null);
            } catch {
                // The enumerator may already be gone, nothing to recover.
            }
        }

        return entries;
    }
}

/**
 * @returns {StackEntry[]} an entry for every installed, visible application
 */
function readApplications() {
    const parentalControls = ParentalControlsManager.getDefault();
    const appSystem = Shell.AppSystem.get_default();

    return appSystem.get_installed().filter(appInfo => {
        try {
            // Catches the invalid file encodings upstream guards against too.
            appInfo.get_id();
        } catch {
            return false;
        }
        return appInfo.should_show() && parentalControls.shouldShowApp(appInfo);
    }).map(appInfo => new StackEntry({
        name: appInfo.get_display_name() || appInfo.get_name(),
        gicon: appInfo.get_icon(),
        app: appSystem.lookup_app(appInfo.get_id()),
        contentType: 'application',
    })).filter(entry => entry.app);
}

/**
 * @param {StackEntry[]} entries the entries to sort in place
 * @returns {StackEntry[]} the same array, sorted according to the settings
 */
function sortEntries(entries, descriptor) {
    const collate = (a, b) => a.name.localeCompare(b.name);

    switch (getStackSort(descriptor)) {
    case StackSort.DATE_MODIFIED:
        entries.sort((a, b) => b.modified - a.modified || collate(a, b));
        break;
    case StackSort.KIND:
        entries.sort((a, b) =>
            a.contentType.localeCompare(b.contentType) || collate(a, b));
        break;
    default:
        // Folders first, exactly like the macOS "Name" ordering.
        entries.sort((a, b) =>
            Number(b.isDirectory) - Number(a.isDirectory) || collate(a, b));
        break;
    }

    return entries;
}

/**
 * A tile used by the grid and fan views: an icon with a caption underneath.
 */
const StackTile = GObject.registerClass(
class StackTile extends St.Button {
    _init(entry) {
        super._init({
            style_class: 'goshos-stack-tile',
            reactive: true,
            can_focus: true,
            track_hover: true,
            x_expand: false,
            y_expand: false,
        });

        this.entry = entry;

        const box = new St.BoxLayout({
            vertical: true,
            x_align: Clutter.ActorAlign.CENTER,
            y_align: Clutter.ActorAlign.CENTER,
        });

        box.add_child(new St.Icon({
            gicon: entry.gicon,
            fallback_icon_name: entry.isDirectory ? 'folder' : 'text-x-generic',
            icon_size: TILE_ICON_SIZE,
            x_align: Clutter.ActorAlign.CENTER,
        }));

        const label = new St.Label({
            text: entry.name,
            style_class: 'goshos-stack-tile-label',
            x_align: Clutter.ActorAlign.CENTER,
        });
        label.clutter_text.set_line_wrap(true);
        label.clutter_text.set_ellipsize(Pango.EllipsizeMode.END);
        label.clutter_text.set_line_alignment(Pango.Alignment.CENTER);
        box.add_child(label);

        this.set_child(box);
        this.accessible_name = entry.name;
    }
});

/**
 * The popup shown when a stack is clicked.
 */
const DockStackMenu = class DockStackMenu extends PopupMenu.PopupMenu {
    constructor(source, descriptor) {
        super(source, 0.5, Utils.getPosition());

        this._descriptor = descriptor;
        this._extraActors = [];
        this._signalsHandler = new Utils.GlobalSignalsHandler(this);
        this.blockSourceEvents = true;

        this.actor.add_style_class_name('app-menu');
        this.actor.add_style_class_name('dock-app-menu');
        this.actor.add_style_class_name('goshos-stack-menu');

        this._signalsHandler.add(source, 'notify::mapped', () => {
            if (!source.mapped)
                this.close();
        });
        this._signalsHandler.add(source, 'destroy', () => this.destroy());

        Main.uiGroup.add_child(this.actor);
    }

    destroy() {
        this._signalsHandler?.destroy();
        this._signalsHandler = null;
        super.destroy();
    }

    /**
     * @param {StackEntry[]} entries the contents to display
     */
    rebuild(entries) {
        this.removeAll();
        this._extraActors.forEach(actor => actor.destroy());
        this._extraActors = [];

        let view = getStackView(this._descriptor);

        if (view === StackView.AUTOMATIC) {
            // Same rule as macOS: a fan for a handful of items, a grid once
            // there are too many for the arc to stay readable.
            view = entries.length <= FAN_MAX_ITEMS && !this._isApplications
                ? StackView.FAN : StackView.GRID;
        }

        if (!entries.length)
            this.addMenuItem(new PopupMenu.PopupMenuItem(__('Empty'), {reactive: false}));
        else if (view === StackView.LIST)
            this._buildList(entries);
        else if (view === StackView.FAN)
            this._buildFan(entries);
        else
            this._buildGrid(entries);

        const location = getStackLocation(this._descriptor);
        if (location) {
            this.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
            const open = new PopupMenu.PopupMenuItem(__('Open in Files'));
            open.connect('activate', () => openUri(location.get_uri()));
            this.addMenuItem(open);
        }
    }

    get _isApplications() {
        return this._descriptor.kind === StackKind.APPLICATIONS;
    }

    _activateEntry(entry) {
        this.close(BoxPointer.PopupAnimation.FULL);
        Main.overview.hide();
        entry.activate(global.get_current_time());
    }

    _limit(entries) {
        const {settings} = Docking.DockManager;
        const max = this._isApplications
            ? HARD_ITEM_LIMIT : Math.max(1, settings.stackMaxItems);
        return entries.slice(0, max);
    }

    /**
     * A popup menu does not scroll, so the jump list can only be as long as
     * the monitor allows. Anything past that is reported as overflow.
     *
     * @returns {number} how many rows fit on screen
     */
    _listCapacity() {
        const monitor = Main.layoutManager.findMonitorForActor(this.sourceActor) ??
            Main.layoutManager.primaryMonitor;
        const {scaleFactor} = St.ThemeContext.get_for_stage(global.stage);
        const rowHeight = LIST_ROW_HEIGHT * scaleFactor;
        return Math.max(5, Math.floor((monitor?.height ?? 720) * 0.7 / rowHeight));
    }

    _buildList(entries) {
        const shown = this._limit(entries).slice(0, this._listCapacity());

        shown.forEach(entry => {
            const item = new PopupMenu.PopupBaseMenuItem();
            item.add_child(new St.Icon({
                gicon: entry.gicon,
                fallback_icon_name: entry.isDirectory ? 'folder' : 'text-x-generic',
                icon_size: LIST_ICON_SIZE,
                style_class: 'popup-menu-icon',
            }));
            item.add_child(new St.Label({
                text: entry.name,
                y_align: Clutter.ActorAlign.CENTER,
                x_expand: true,
            }));
            item.connect('activate', () => this._activateEntry(entry));
            this.addMenuItem(item);
        });

        this._maybeAddOverflowNote(entries, shown);
    }

    _buildGrid(entries) {
        const shown = this._limit(entries);
        const columns = Math.max(1,
            Math.min(GRID_MAX_COLUMNS, Math.ceil(Math.sqrt(shown.length))));

        const layout = new Clutter.GridLayout({
            orientation: Clutter.Orientation.HORIZONTAL,
            column_homogeneous: true,
            row_homogeneous: true,
            // St does not map CSS spacing onto a Clutter.GridLayout.
            column_spacing: GRID_SPACING,
            row_spacing: GRID_SPACING,
        });
        const grid = new St.Widget({
            style_class: 'goshos-stack-grid',
            layout_manager: layout,
            x_expand: true,
        });

        shown.forEach((entry, index) => {
            const tile = new StackTile(entry);
            tile.connect('clicked', () => this._activateEntry(entry));
            layout.attach(tile, index % columns, Math.floor(index / columns), 1, 1);
        });

        const scrollView = new St.ScrollView({
            style_class: 'goshos-stack-scrollview',
            hscrollbar_policy: St.PolicyType.NEVER,
            vscrollbar_policy: St.PolicyType.AUTOMATIC,
            x_expand: true,
            y_expand: true,
        });
        Utils.addActor(scrollView, grid);

        const monitor = Main.layoutManager.findMonitorForActor(this.sourceActor) ??
            Main.layoutManager.primaryMonitor;
        if (monitor)
            scrollView.set_style(`max-height: ${Math.round(monitor.height * 0.6)}px;`);

        this.box.add_child(scrollView);
        this._extraActors.push(scrollView);

        this._maybeAddOverflowNote(entries, shown);
    }

    _buildFan(entries) {
        const shown = entries.slice(0, FAN_MAX_ITEMS);
        const position = Utils.getPosition();
        const horizontal = position === St.Side.TOP || position === St.Side.BOTTOM;

        const fan = new St.Widget({style_class: 'goshos-stack-fan'});
        const tiles = shown.map(entry => {
            const tile = new StackTile(entry);
            tile.connect('clicked', () => this._activateEntry(entry));
            fan.add_child(tile);
            return tile;
        });

        this.box.add_child(fan);
        this._extraActors.push(fan);

        // Fixed positioning needs the natural tile size, which is only known
        // once the actors have a theme node; re-run it when the popup maps so
        // a first-time style load cannot leave the fan collapsed.
        layOutFan(fan, tiles, position, horizontal);
        fan.connect('notify::mapped', () => {
            if (fan.mapped)
                layOutFan(fan, tiles, position, horizontal);
        });

        this._maybeAddOverflowNote(entries, shown);
    }

    _maybeAddOverflowNote(entries, shown) {
        if (entries.length <= shown.length)
            return;

        this.addMenuItem(new PopupMenu.PopupMenuItem(
            __('%d more items…').format(entries.length - shown.length),
            {reactive: false}));
    }
};

/**
 * The right-click menu of a stack: the macOS "Sort by" / "View content as"
 * options, plus the usual dock entries.
 */
const DockStackOptionsMenu = class DockStackOptionsMenu extends PopupMenu.PopupMenu {
    constructor(source, descriptor) {
        super(source, 0.5, Utils.getPosition());

        this._descriptor = descriptor;
        this.blockSourceEvents = true;
        this.actor.add_style_class_name('app-menu');
        this.actor.add_style_class_name('dock-app-menu');

        Main.uiGroup.add_child(this.actor);
    }

    popup() {
        this._rebuild();
        this.open(BoxPointer.PopupAnimation.FULL);
    }

    _rebuild() {
        this.removeAll();
        addStackViewItems(this, this._descriptor);

        this.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        const location = getStackLocation(this._descriptor);
        if (location) {
            const open = new PopupMenu.PopupMenuItem(__('Open in Files'));
            open.connect('activate', () => openUri(location.get_uri()));
            this.addMenuItem(open);
        }

        if (this._descriptor.kind === StackKind.CUSTOM) {
            const remove = new PopupMenu.PopupMenuItem(__('Remove from Dock'));
            remove.connect('activate', () => removeCustomStack(this._descriptor));
            this.addMenuItem(remove);
        }

        const prefs = new PopupMenu.PopupMenuItem(_('Settings'));
        prefs.connect('activate', () => Docking.DockManager.extension.openPreferences());
        this.addMenuItem(prefs);
    }
};

/**
 * Append the macOS "Sort by" and "View content as" sections to a menu. Every
 * stack — Applications, Documents, Downloads, Home and each folder the user
 * added — keeps its own choice, keyed on the stack id.
 *
 * @param {PopupMenu.PopupMenuBase} menu the menu to append to
 * @param {object} descriptor the stack descriptor
 */
export function addStackViewItems(menu, descriptor) {
    const currentSort = getStackSort(descriptor);
    menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem(__('Sort by')));
    [
        [StackSort.NAME, __('Name')],
        [StackSort.DATE_MODIFIED, __('Date Modified')],
        [StackSort.KIND, __('Kind')],
    ].forEach(([value, label]) => {
        const item = new PopupMenu.PopupMenuItem(label);
        if (currentSort === value)
            item.setOrnament(PopupMenu.Ornament.DOT);
        item.connect('activate', () => setStackSort(descriptor, value));
        menu.addMenuItem(item);
    });

    const currentView = getStackView(descriptor);
    menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem(__('View content as')));
    [
        [StackView.FAN, __('Fan')],
        [StackView.GRID, __('Grid')],
        [StackView.LIST, __('List')],
        [StackView.AUTOMATIC, __('Automatic')],
    ].forEach(([value, label]) => {
        const item = new PopupMenu.PopupMenuItem(label);
        if (currentView === value)
            item.setOrnament(PopupMenu.Ornament.DOT);
        item.connect('activate', () => setStackView(descriptor, value));
        menu.addMenuItem(item);
    });
}

/**
 * Owns the stack popup of a dock item. Used both by the dedicated stack icons
 * and by the "Show Applications" button when it is configured to open a stack.
 */
export class StackPopupController {
    constructor(source, descriptor, menuManager) {
        this._source = source;
        this._descriptor = descriptor;
        this._menuManager = menuManager;
        this._menu = null;
        this._optionsMenu = null;
        this._contents = new StackContents(descriptor);
    }

    destroy() {
        this._contents?.destroy();
        this._contents = null;
        this._menu?.destroy();
        this._menu = null;
        this._optionsMenu?.destroy();
        this._optionsMenu = null;
        this._source = null;
    }

    get isOpen() {
        return !!this._menu?.isOpen;
    }

    close() {
        this._menu?.close(BoxPointer.PopupAnimation.FULL);
    }

    async popup() {
        if (!this._menu) {
            this._menu = new DockStackMenu(this._source, this._descriptor);
            this._menu.connect('open-state-changed', (_menu, isOpen) =>
                this._source?.onStackMenuStateChanged?.(isOpen));
            this._menu.connect('destroy', () => (this._menu = null));
            this._menuManager.addMenu(this._menu);
        }

        let entries = [];
        try {
            entries = await this._contents.read();
        } catch (e) {
            logError(e, `Could not read the ${this._descriptor.id} stack`);
        }

        if (!this._menu)
            return;

        this._menu.rebuild(entries);
        this._menu.open(BoxPointer.PopupAnimation.FULL);
        this._menuManager.ignoreRelease?.();
    }

    popupOptions() {
        if (!this._optionsMenu) {
            this._optionsMenu = new DockStackOptionsMenu(this._source, this._descriptor);
            this._optionsMenu.connect('open-state-changed', (_menu, isOpen) =>
                this._source?.onStackMenuStateChanged?.(isOpen));
            this._optionsMenu.connect('destroy', () => (this._optionsMenu = null));
            this._menuManager.addMenu(this._optionsMenu);
        }

        this._optionsMenu.popup();
        this._menuManager.ignoreRelease?.();
    }
}

/**
 * @param {string} uri the URI to hand over to the default handler
 */
function openUri(uri) {
    try {
        Gio.AppInfo.launch_default_for_uri(uri,
            global.create_app_launch_context(global.get_current_time(), -1));
    } catch (e) {
        logError(e, `Could not open ${uri}`);
    }
}

/**
 * Position the fan tiles along a gentle arc leaving the dock.
 *
 * @param {St.Widget} fan the fan container
 * @param {St.Button[]} tiles the tiles to lay out
 * @param {St.Side} position the dock position
 * @param {boolean} horizontal whether the dock is horizontal
 */
function layOutFan(fan, tiles, position, horizontal) {
    if (!tiles.length)
        return;

    let tileWidth = 0;
    let tileHeight = 0;
    tiles.forEach(tile => {
        const [, , width, height] = tile.get_preferred_size();
        tileWidth = Math.max(tileWidth, width);
        tileHeight = Math.max(tileHeight, height);
    });

    const stepMain = (horizontal ? tileHeight : tileWidth) + FAN_TILE_GAP;
    const lateralSpread = Math.round((horizontal ? tileWidth : tileHeight) * 0.45);
    const count = tiles.length;
    const mainSize = count * stepMain;
    const lateralSize = (horizontal ? tileWidth : tileHeight) + lateralSpread;

    tiles.forEach((tile, index) => {
        const progress = count > 1 ? index / (count - 1) : 0;
        const lateral = Math.round(lateralSpread * Math.sin(progress * Math.PI / 2));
        // The first entry stays closest to the dock.
        const mainOffset = position === St.Side.BOTTOM || position === St.Side.RIGHT
            ? mainSize - (index + 1) * stepMain : index * stepMain;

        tile.set_size(tileWidth, tileHeight);
        if (horizontal)
            tile.set_position(lateral, mainOffset);
        else
            tile.set_position(mainOffset, lateral);
    });

    if (horizontal)
        fan.set_size(lateralSize, mainSize);
    else
        fan.set_size(mainSize, lateralSize);
}

/**
 * The dock item for a stack.
 */
export const DockStackIcon = GObject.registerClass({
    Signals: {
        'menu-state-changed': {param_types: [GObject.TYPE_BOOLEAN]},
        'sync-tooltip': {},
    },
}, class DockStackIcon extends Dash.DashItemContainer {
    _init(descriptor, position) {
        super._init();

        this.descriptor = descriptor;
        this._position = position;

        this.toggleButton = new St.Button({
            style_class: 'show-apps',
            track_hover: true,
            can_focus: true,
            y_expand: false,
        });

        this.icon = new IconGrid.BaseIcon(getStackName(descriptor), {
            setSizeManually: true,
            showLabel: false,
            createIcon: size => this._createIcon(size),
        });
        this.icon.y_align = Clutter.ActorAlign.CENTER;

        this.toggleButton.add_child(this.icon);
        this.toggleButton._delegate = this;
        this.setChild(this.toggleButton);
        this.setLabelText(getStackName(descriptor));

        this.label?.add_style_class_name(Theming.PositionStyleClass[position]);
        if (Docking.DockManager.settings.customThemeShrink)
            this.label?.add_style_class_name('shrink');

        this._menuManager = new PopupMenu.PopupMenuManager(this);
        this._popup = new StackPopupController(this, descriptor, this._menuManager);

        this.toggleButton.connect('clicked', () => this._onClicked());
        this._enableSecondaryClick();
        this.connect('destroy', () => this._onDestroy());

        this._updateGicon().catch(e => logError(e));
    }

    _onDestroy() {
        this._popup?.destroy();
        this._popup = null;
    }

    /**
     * Right click opens the macOS "Sort by" / "View content as" menu. Newer
     * shells route pointer buttons through gestures, older ones still emit
     * button-press-event.
     */
    _enableSecondaryClick() {
        if (Clutter.ClickGesture) {
            const rightClick = new Clutter.ClickGesture({
                required_button: Clutter.BUTTON_SECONDARY,
                recognize_on_press: true,
            });
            rightClick.connect('recognize', () => this._popup?.popupOptions());
            this.add_action(rightClick);
            return;
        }

        this.toggleButton.connect('button-press-event', (_actor, event) => {
            if (event.get_button() !== Clutter.BUTTON_SECONDARY)
                return Clutter.EVENT_PROPAGATE;
            this._popup?.popupOptions();
            return Clutter.EVENT_STOP;
        });
    }

    showLabel(...args) {
        AppIcons.itemShowLabel.call(this, ...args);
    }

    shouldShowTooltip() {
        return this.toggleButton.hover && !this._popup?.isOpen &&
            !Docking.DockManager.settings.hideTooltip;
    }

    vfunc_get_preferred_width(forHeight) {
        const [min, nat] = super.vfunc_get_preferred_width(forHeight);
        return Magnification.adjustPreferredSize(this, min, nat, true);
    }

    vfunc_get_preferred_height(forWidth) {
        const [min, nat] = super.vfunc_get_preferred_height(forWidth);
        return Magnification.adjustPreferredSize(this, min, nat, false);
    }

    setIconSize(size) {
        this.icon.setIconSize(size);
    }

    get kind() {
        return this.descriptor.kind;
    }

    updateName() {
        this.setLabelText(getStackName(this.descriptor));
        this._gicon = null;
        this.icon?.update();
        this._updateGicon().catch(e => logError(e));
    }

    onStackMenuStateChanged(isOpen) {
        this.toggleButton.checked = isOpen;
        this.emit('menu-state-changed', isOpen);
        if (!isOpen)
            this.emit('sync-tooltip');
    }

    _createIcon(size) {
        const fallbackIconName = getStackFallbackIconName(this.kind);

        if (!this._gicon)
            return new St.Icon({iconName: fallbackIconName, iconSize: size});

        return new St.Icon({
            gicon: this._gicon,
            fallbackIconName,
            iconSize: size,
        });
    }

    async _updateGicon() {
        const location = getStackLocation(this.descriptor);
        if (!location)
            return;

        try {
            ensureFilePromises(location);
            const info = await location.query_info_async(
                Gio.FILE_ATTRIBUTE_STANDARD_ICON, Gio.FileQueryInfoFlags.NONE,
                GLib.PRIORITY_LOW, null);
            if (this.is_finalized?.())
                return;
            this._gicon = info.get_icon();
            this.icon?.update();
        } catch (e) {
            if (!e.matches?.(Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED))
                logError(e, `Could not read the ${this.descriptor.id} stack icon`);
        }
    }

    _onClicked() {
        if (this._popup.isOpen) {
            this._popup.close();
            return;
        }

        this._popup.popup().catch(e => logError(e));
    }
});

/**
 * The container holding every stack icon, plus the macOS style divider that
 * separates them from the application area of the dock.
 */
export const DockStacksContainer = GObject.registerClass(
class DockStacksContainer extends St.BoxLayout {
    _init(dash, position) {
        const horizontal = position === St.Side.TOP || position === St.Side.BOTTOM;

        super._init({
            name: 'goshosDockStacksContainer',
            vertical: !horizontal,
            x_align: Clutter.ActorAlign.CENTER,
            y_align: Clutter.ActorAlign.CENTER,
        });

        this._dash = dash;
        this._position = position;
        this._horizontal = horizontal;
        this._stacks = new Map();
        this._separator = null;
        this._signalsHandler = new Utils.GlobalSignalsHandler(this);

        const {settings} = Docking.DockManager;
        [
            'changed::show-applications-stack',
            'changed::show-documents-stack',
            'changed::show-downloads-stack',
            'changed::show-home-stack',
            'changed::custom-stacks',
            'changed::show-stacks-separator',
        ].forEach(key => this._signalsHandler.add(settings, key, () => this.update()));

        this.connect('destroy', () => {
            this._signalsHandler?.destroy();
            this._signalsHandler = null;
            this._stacks.clear();
        });

        this.update();
    }

    /**
     * @returns {Clutter.Actor[]} the stack items, in dock order
     */
    getStackItems() {
        return this.get_children().filter(child => child instanceof DockStackIcon);
    }

    setIconSize(size) {
        this.getStackItems().forEach(item => item.setIconSize(size));
        if (!this._separator)
            return;

        if (this._horizontal)
            this._separator.height = size;
        else
            this._separator.width = size;
    }

    update() {
        const descriptors = getStackDescriptors();
        const ids = new Set(descriptors.map(d => d.id));

        [...this._stacks.keys()].forEach(id => {
            if (!ids.has(id)) {
                this._stacks.get(id).destroy();
                this._stacks.delete(id);
            }
        });

        this._updateSeparator(descriptors.length > 0);

        descriptors.forEach(descriptor => {
            let stack = this._stacks.get(descriptor.id);
            if (!stack) {
                stack = new DockStackIcon(descriptor, this._position);
                // Same treatment as the show apps button: never claim the
                // extra space of an extended dock.
                stack.x_expand = false;
                stack.y_expand = false;
                if (!this._horizontal)
                    stack.y_align = Clutter.ActorAlign.START;
                stack.setIconSize(this._dash.iconSize);
                this._dash.hookUpStackItem(stack);
                this._stacks.set(descriptor.id, stack);
                this.add_child(stack);
                stack.show(false);
            } else {
                stack.updateName();
            }
            this.set_child_above_sibling(stack, null);
        });

        this.visible = descriptors.length > 0;
        this._dash.onStacksChanged();
    }

    _updateSeparator(needed) {
        const wanted = needed && Docking.DockManager.settings.showStacksSeparator;

        if (!wanted) {
            this._separator?.destroy();
            this._separator = null;
            return;
        }

        if (this._separator)
            return;

        this._separator = new St.Widget({
            style_class: 'dash-separator goshos-stacks-separator',
            x_align: this._horizontal
                ? Clutter.ActorAlign.FILL : Clutter.ActorAlign.CENTER,
            y_align: this._horizontal
                ? Clutter.ActorAlign.CENTER : Clutter.ActorAlign.FILL,
            width: this._horizontal ? -1 : this._dash.iconSize,
            height: this._horizontal ? this._dash.iconSize : -1,
        });
        this.insert_child_at_index(this._separator, 0);
    }
});
