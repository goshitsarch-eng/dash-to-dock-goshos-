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
const TILE_ICON_SIZE = 48;
const FAN_TILE_GAP = 6;

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
 * The `Gio.File` a stack of the given kind points at.
 *
 * @param {string} kind one of `StackKind`
 * @returns {?Gio.File} the target folder, or null for non-folder stacks
 */
export function getStackLocation(kind) {
    let path = null;

    switch (kind) {
    case StackKind.DOCUMENTS:
        path = GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_DOCUMENTS);
        break;
    case StackKind.DOWNLOADS:
        path = GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_DOWNLOAD);
        break;
    case StackKind.HOME:
        path = GLib.get_home_dir();
        break;
    case StackKind.CUSTOM: {
        const custom = Docking.DockManager.settings.customStackPath?.trim();
        if (!custom)
            return null;
        if (custom.includes('://'))
            return Gio.File.new_for_uri(custom);
        path = custom.startsWith('~')
            ? GLib.build_filenamev([GLib.get_home_dir(), custom.slice(1)]) : custom;
        break;
    }
    default:
        return null;
    }

    return path ? Gio.File.new_for_path(path) : null;
}

/**
 * The default, user visible name of a stack.
 *
 * @param {string} kind one of `StackKind`
 * @returns {string} the translated stack name
 */
export function getStackName(kind) {
    switch (kind) {
    case StackKind.APPLICATIONS:
        return __('Applications');
    case StackKind.DOCUMENTS:
        return __('Documents');
    case StackKind.DOWNLOADS:
        return __('Downloads');
    case StackKind.HOME:
        return __('Home');
    case StackKind.CUSTOM: {
        const name = Docking.DockManager.settings.customStackName?.trim();
        if (name)
            return name;
        return getStackLocation(kind)?.get_basename() ?? __('Folder');
    }
    default:
        return __('Folder');
    }
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
 * The list of stack kinds that are currently enabled, in dock order.
 *
 * @returns {string[]} the enabled stack kinds
 */
export function getEnabledStackKinds() {
    const {settings} = Docking.DockManager;
    const kinds = [];

    if (settings.showApplicationsStack)
        kinds.push(StackKind.APPLICATIONS);
    if (settings.showDocumentsStack)
        kinds.push(StackKind.DOCUMENTS);
    if (settings.showDownloadsStack)
        kinds.push(StackKind.DOWNLOADS);
    if (settings.showHomeStack)
        kinds.push(StackKind.HOME);
    if (settings.showCustomStack && getStackLocation(StackKind.CUSTOM))
        kinds.push(StackKind.CUSTOM);

    return kinds;
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
    constructor(kind) {
        this._kind = kind;
        this._cancellable = null;
    }

    destroy() {
        this._cancellable?.cancel();
        this._cancellable = null;
    }

    get location() {
        return getStackLocation(this._kind);
    }

    async read() {
        this._cancellable?.cancel();
        this._cancellable = new Gio.Cancellable();

        const entries = this._kind === StackKind.APPLICATIONS
            ? readApplications() : await this._readFolder(this._cancellable);

        return sortEntries(entries);
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
                logError(e, `Could not read the ${this._kind} stack`);
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

    return appSystem.get_installed().filter(appInfo =>
        appInfo.should_show() && parentalControls.shouldShowApp(appInfo)
    ).map(appInfo => new StackEntry({
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
function sortEntries(entries) {
    const {settings} = Docking.DockManager;
    const collate = (a, b) => a.name.localeCompare(b.name);

    switch (settings.stackSort) {
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
    constructor(source, kind) {
        super(source, 0.5, Utils.getPosition());

        this._kind = kind;
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

        const {settings} = Docking.DockManager;
        let view = settings.stackView;

        if (view === StackView.AUTOMATIC) {
            if (this._kind === StackKind.APPLICATIONS)
                view = StackView.GRID;
            else
                view = entries.length <= FAN_MAX_ITEMS ? StackView.FAN : StackView.GRID;
        }

        if (!entries.length)
            this.addMenuItem(new PopupMenu.PopupMenuItem(__('Empty'), {reactive: false}));
        else if (view === StackView.LIST)
            this._buildList(entries);
        else if (view === StackView.FAN)
            this._buildFan(entries);
        else
            this._buildGrid(entries);

        const location = getStackLocation(this._kind);
        if (location) {
            this.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
            const open = new PopupMenu.PopupMenuItem(__('Open in Files'));
            open.connect('activate', () => openUri(location.get_uri()));
            this.addMenuItem(open);
        }
    }

    _activateEntry(entry) {
        this.close(BoxPointer.PopupAnimation.FULL);
        Main.overview.hide();
        entry.activate(global.get_current_time());
    }

    _limit(entries) {
        const {settings} = Docking.DockManager;
        const max = this._kind === StackKind.APPLICATIONS
            ? HARD_ITEM_LIMIT : Math.max(1, settings.stackMaxItems);
        return entries.slice(0, max);
    }

    _buildList(entries) {
        const shown = this._limit(entries);

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
    constructor(source, kind) {
        super(source, 0.5, Utils.getPosition());

        this._kind = kind;
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
        const {settings} = Docking.DockManager;

        this.addMenuItem(new PopupMenu.PopupSeparatorMenuItem(__('Sort by')));
        [
            [StackSort.NAME, __('Name')],
            [StackSort.DATE_MODIFIED, __('Date Modified')],
            [StackSort.KIND, __('Kind')],
        ].forEach(([value, label]) => {
            const item = new PopupMenu.PopupMenuItem(label);
            if (settings.stackSort === value)
                item.setOrnament(PopupMenu.Ornament.DOT);
            item.connect('activate', () => settings.set_enum('stack-sort', value));
            this.addMenuItem(item);
        });

        this.addMenuItem(new PopupMenu.PopupSeparatorMenuItem(__('View content as')));
        [
            [StackView.AUTOMATIC, __('Automatic')],
            [StackView.FAN, __('Fan')],
            [StackView.GRID, __('Grid')],
            [StackView.LIST, __('List')],
        ].forEach(([value, label]) => {
            const item = new PopupMenu.PopupMenuItem(label);
            if (settings.stackView === value)
                item.setOrnament(PopupMenu.Ornament.DOT);
            item.connect('activate', () => settings.set_enum('stack-view', value));
            this.addMenuItem(item);
        });

        this.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        const location = getStackLocation(this._kind);
        if (location) {
            const open = new PopupMenu.PopupMenuItem(__('Open in Files'));
            open.connect('activate', () => openUri(location.get_uri()));
            this.addMenuItem(open);
        }

        const prefs = new PopupMenu.PopupMenuItem(_('Settings'));
        prefs.connect('activate', () => Docking.DockManager.extension.openPreferences());
        this.addMenuItem(prefs);
    }
};

/**
 * Owns the stack popup of a dock item. Used both by the dedicated stack icons
 * and by the "Show Applications" button when it is configured to open a stack.
 */
export class StackPopupController {
    constructor(source, kind, menuManager) {
        this._source = source;
        this._kind = kind;
        this._menuManager = menuManager;
        this._menu = null;
        this._optionsMenu = null;
        this._contents = new StackContents(kind);
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
            this._menu = new DockStackMenu(this._source, this._kind);
            this._menu.connect('open-state-changed', (_menu, isOpen) =>
                this._source?.onStackMenuStateChanged?.(isOpen));
            this._menu.connect('destroy', () => (this._menu = null));
            this._menuManager.addMenu(this._menu);
        }

        let entries = [];
        try {
            entries = await this._contents.read();
        } catch (e) {
            logError(e, `Could not read the ${this._kind} stack`);
        }

        if (!this._menu)
            return;

        this._menu.rebuild(entries);
        this._menu.open(BoxPointer.PopupAnimation.FULL);
        this._menuManager.ignoreRelease?.();
    }

    popupOptions() {
        if (!this._optionsMenu) {
            this._optionsMenu = new DockStackOptionsMenu(this._source, this._kind);
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
    _init(kind, position) {
        super._init();

        this.kind = kind;
        this._position = position;

        this.toggleButton = new St.Button({
            style_class: 'show-apps',
            track_hover: true,
            can_focus: true,
            toggle_mode: true,
            y_expand: false,
        });

        this.icon = new IconGrid.BaseIcon(getStackName(kind), {
            setSizeManually: true,
            showLabel: false,
            createIcon: size => this._createIcon(size),
        });
        this.icon.y_align = Clutter.ActorAlign.CENTER;

        this.toggleButton.add_child(this.icon);
        this.toggleButton._delegate = this;
        this.setChild(this.toggleButton);
        this.setLabelText(getStackName(kind));

        this.label?.add_style_class_name(Theming.PositionStyleClass[position]);
        if (Docking.DockManager.settings.customThemeShrink)
            this.label?.add_style_class_name('shrink');

        this._menuManager = new PopupMenu.PopupMenuManager(this);
        this._popup = new StackPopupController(this, kind, this._menuManager);

        this.toggleButton.connect('clicked', () => this._onClicked());
        this.toggleButton.connect('button-press-event', (_actor, event) => {
            if (event.get_button() !== Clutter.BUTTON_SECONDARY)
                return Clutter.EVENT_PROPAGATE;
            this._popup.popupOptions();
            return Clutter.EVENT_STOP;
        });
        this.connect('destroy', () => this._onDestroy());

        this._updateGicon().catch(e => logError(e));
    }

    _onDestroy() {
        this._popup?.destroy();
        this._popup = null;
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

    updateName() {
        this.setLabelText(getStackName(this.kind));
        this.icon?.update();
    }

    onStackMenuStateChanged(isOpen) {
        this.toggleButton.checked = isOpen;
        this.emit('menu-state-changed', isOpen);
        if (!isOpen)
            this.emit('sync-tooltip');
    }

    _createIcon(size) {
        return new St.Icon({
            gicon: this._gicon ?? null,
            fallback_icon_name: getStackFallbackIconName(this.kind),
            icon_size: size,
        });
    }

    async _updateGicon() {
        const location = getStackLocation(this.kind);
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
                logError(e, `Could not read the ${this.kind} stack icon`);
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
            'changed::show-custom-stack',
            'changed::custom-stack-path',
            'changed::custom-stack-name',
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
        const kinds = getEnabledStackKinds();

        [...this._stacks.keys()].forEach(kind => {
            if (!kinds.includes(kind)) {
                this._stacks.get(kind).destroy();
                this._stacks.delete(kind);
            }
        });

        this._updateSeparator(kinds.length > 0);

        kinds.forEach(kind => {
            let stack = this._stacks.get(kind);
            if (!stack) {
                stack = new DockStackIcon(kind, this._position);
                stack.setIconSize(this._dash.iconSize);
                this._dash.hookUpStackItem(stack);
                this._stacks.set(kind, stack);
                this.add_child(stack);
                stack.show(false);
            } else {
                stack.updateName();
            }
            this.set_child_above_sibling(stack, null);
        });

        this.visible = kinds.length > 0;
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
