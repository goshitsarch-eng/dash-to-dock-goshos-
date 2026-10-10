/*
    SPDX-FileCopyrightText: 2012-2016 Eike Hein <hein@kde.org>
    SPDX-FileCopyrightText: 2016 Kai Uwe Broulik <kde@privat.broulik.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

import org.kde.plasma.plasmoid

import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.extras as PlasmaExtras

import org.kde.taskmanager as TaskManager
import org.kde.plasma.private.mpris as Mpris
import plasma.applet.org.gosh.goshosdock as TaskManagerApplet

PlasmaExtras.Menu {
    id: menu

    required property TaskManagerApplet.Backend backend
    required property Mpris.Mpris2Model mpris2Source
    required property /*QModelIndex*/var modelIndex
    property Task rootTask: visualParent as Task
    property var taskModel: tasksModel
    property bool detachedLauncher: false
    property bool launcherUpdating: false
    property bool modelReady: false

    property bool showAllPlaces: false
    property url quicklistLauncher
    property var quicklistItems: []
    property var gpuItems: []

    placement: {
        if (Plasmoid.location === PlasmaCore.Types.LeftEdge) {
            return PlasmaExtras.Menu.RightPosedTopAlignedPopup;
        } else if (Plasmoid.location === PlasmaCore.Types.TopEdge) {
            return PlasmaExtras.Menu.BottomPosedLeftAlignedPopup;
        } else if (Plasmoid.location === PlasmaCore.Types.RightEdge) {
            return PlasmaExtras.Menu.LeftPosedTopAlignedPopup;
        } else {
            return PlasmaExtras.Menu.TopPosedLeftAlignedPopup;
        }
    }

    minimumWidth: (visualParent as Item).width

    onStatusChanged: {
        if (visualParent && get(TaskManager.AbstractTasksModel.LauncherUrlWithoutIcon).toString() !== "" && status === PlasmaExtras.Menu.Open) {
            activitiesDesktopsMenu.refresh();

        } else if (status === PlasmaExtras.Menu.Closed) {
            menu.destroy();
        }
    }

    Component.onCompleted: {
        // Cannot have "Connections" as child of PlasmaExtras.Menu.
        backend.showAllPlaces.connect(showContextMenuWithAllPlaces);
        backend.gpuChoicesChanged.connect(refreshGpuActions);
        quicklistBackend.actionsChanged.connect(refreshQuicklistActions);
        // QQmlParserStatus completes the native task model's proxy graph
        // after property bindings have been evaluated. Query it next turn.
        Qt.callLater(() => { menu.modelReady = true; });
    }

    Component.onDestruction: {
        backend.showAllPlaces.disconnect(showContextMenuWithAllPlaces);
        backend.gpuChoicesChanged.disconnect(refreshGpuActions);
        quicklistBackend.actionsChanged.disconnect(refreshQuicklistActions);
        quicklistBackend.closed(quicklistLauncher);
    }

    function showContextMenuWithAllPlaces(): void {
        if (rootTask) {
            rootTask.showContextMenu({showAllPlaces: true, visualParent: visualParent});
        } else {
            tasks.openLauncherContextMenu(visualParent, quicklistLauncher, {showAllPlaces: true});
        }
    }

    function get(modelProp: int): var {
        // Inherited launcher menus initialize the model and index bindings
        // separately. Qt's proxy model cannot accept an index from another
        // model (even transiently while these bindings are being created).
        const value = modelReady && menu.taskModel && modelIndex?.valid && modelIndex.model === menu.taskModel
            ? menu.taskModel.data(modelIndex, modelProp) : undefined;
        if (value === undefined || value === null) {
            if (modelProp === TaskManager.AbstractTasksModel.LauncherUrl
                || modelProp === TaskManager.AbstractTasksModel.LauncherUrlWithoutIcon
                || modelProp === TaskManager.AbstractTasksModel.AppName || modelProp === Qt.DisplayRole) return "";
            if (modelProp === TaskManager.AbstractTasksModel.Activities || modelProp === TaskManager.AbstractTasksModel.VirtualDesktops
                || modelProp === TaskManager.AbstractTasksModel.WinIdList) return [];
            if (modelProp === TaskManager.AbstractTasksModel.IsLauncher) return true;
            if (modelProp === TaskManager.AbstractTasksModel.AppPid) return 0;
            // Remaining roles used by this menu are boolean capabilities.
            return false;
        }
        return value;
    }

    readonly property bool updateInProgress: (rootTask?.appUpdating ?? false) || launcherUpdating
    // An update beginning while this menu is open must retire executable
    // actions immediately. Reopening shows only pinning and application details.
    onUpdateInProgressChanged: if (status === PlasmaExtras.Menu.Open) close()

    function show(): void {
        if (!modelReady) {
            Qt.callLater(show);
            return;
        }
        Plasmoid.contextualActionsAboutToShow();
        // These submenus capture model data in their action callbacks or
        // skip construction while capability roles are unavailable.
        // Populate them after the model parser-status initialization gate.
        virtualDesktopsMenu.refresh();
        activitiesDesktopsMenu.refresh();
        activitiesLaunchersMenu.refresh();

        quicklistLauncher = get(TaskManager.AbstractTasksModel.LauncherUrlWithoutIcon);
        if (!updateInProgress) {
            loadDynamicLaunchActions(quicklistLauncher);
            loadWindowTitleActions();
            refreshGpuActions();
            refreshQuicklistActions();
            quicklistBackend.aboutToShow(quicklistLauncher);
        }
        openRelative();
    }

    function refreshQuicklistActions(): void {
        for (const item of quicklistItems) {
            menu.removeMenuItem(item);
            // Disconnect wrappers before the backend retires the old actions.
            item.action = null;
            item.destroy();
        }
        quicklistItems = [];
        if (updateInProgress || quicklistLauncher.toString() === "") {
            return;
        }
        const actions = quicklistBackend.actions(quicklistLauncher);
        if (actions.length === 0) {
            return;
        }
        const heading = newMenuItem(menu);
        heading.text = i18nc("@title:group", "Application Actions");
        // Use the real QAction separator: Plasma's section=true creates a
        // replacement action when opening, which cannot be removed on refresh.
        heading.separator = true;
        menu.addMenuItem(heading, startNewInstanceItem);
        quicklistItems.push(heading);
        for (const action of actions) {
            const item = newMenuItem(menu);
            // PlasmaExtras forwards QAction::menu(), preserving submenus.
            item.action = action;
            menu.addMenuItem(item, startNewInstanceItem);
            quicklistItems.push(item);
        }
    }

    function loadWindowTitleActions(): void {
        if (Plasmoid.configuration.showToolTips || get(TaskManager.AbstractTasksModel.IsLauncher)
            || get(TaskManager.AbstractTasksModel.IsStartup)) {
            return;
        }
        const heading = newMenuItem(menu);
        heading.text = i18nc("@title:group", "Windows");
        heading.separator = true;
        menu.addMenuItem(heading, startNewInstanceItem);
        TaskManagerApplet.TaskTools.foreachChildTask(index => {
            const item = newMenuItem(menu);
            item.text = menu.taskModel.data(index, Qt.DisplayRole);
            item.icon = menu.taskModel.data(index, Qt.DecorationRole);
            item.clicked.connect(() => menu.taskModel.requestActivate(index));
            menu.addMenuItem(item, startNewInstanceItem);
        }, menu.modelIndex, menu.taskModel);
    }

    function refreshGpuActions(): void {
        for (const item of gpuItems) {
            menu.removeMenuItem(item);
            item.action = null;
            item.destroy();
        }
        gpuItems = [];
        if (updateInProgress || !menu.get(TaskManager.AbstractTasksModel.IsLauncher)) {
            return;
        }
        for (const action of backend.gpuActions(quicklistLauncher, menu)) {
            const item = newMenuItem(menu);
            item.action = action;
            menu.addMenuItem(item, startNewInstanceItem);
            gpuItems.push(item);
        }
    }

    function newMenuItem(parent: QtObject): PlasmaExtras.MenuItem {
        return Qt.createQmlObject(`
            import org.kde.plasma.extras as PlasmaExtras

            PlasmaExtras.MenuItem {}
        `, parent) as PlasmaExtras.MenuItem;
    }

    function newSeparator(parent: QtObject): PlasmaExtras.MenuItem {
        return Qt.createQmlObject(`
            import org.kde.plasma.extras as PlasmaExtras

            PlasmaExtras.MenuItem { separator: true }
            `, parent) as PlasmaExtras.MenuItem;
    }

    function loadDynamicLaunchActions(launcherUrl: url): void {
        let sections = [];

        const placesActions = backend.placesActions(launcherUrl, showAllPlaces, menu);

        if (placesActions.length > 0) {
            sections.push({
                title: i18nc("@title:group for section of menu items", "Places"),
                group: "places",
                actions: placesActions
            });
        } else {
            sections.push({
                title:  i18nc("@title:group for section of menu items", "Recent Files"),
                group:   "recents",
                actions: backend.recentDocumentActions(launcherUrl, menu)
            });
        }

        // We always have actions category.
        sections = sections.filter(section => section.actions.length > 0);

        sections.push({
            title: i18nc("@title:group for section of menu items", "Actions"),
            group: "actions",
            actions: backend.jumpListActions(launcherUrl, menu)
        });

        // C++ can override section heading by returning a QString as first action
        sections.forEach((section) => {
            if (typeof section.actions[0] === "string") {
                section.title = section.actions.shift(); // take first
            }
        });

        // QMenu does not limit its width automatically. Even if we set a maximumWidth
        // it would just cut off text rather than eliding. So we do this manually.
        const textMetrics = Qt.createQmlObject("import QtQuick; TextMetrics {}", menu);
        textMetrics.elide = Qt.ElideRight;
        textMetrics.elideWidth = TaskManagerApplet.LayoutMetrics.maximumContextMenuTextWidth();

        sections.forEach(section => {
            if (section["actions"].length > 0 || section["group"] === "actions") {
                // Don't add the "Actions" header if the menu has nothing but actions
                // in it, because then it's redundant (all menus have actions)
                if (section.group !== "actions" || sections.length > 1) {
                    var sectionHeader = newMenuItem(menu);
                    sectionHeader.text = section["title"];
                    sectionHeader.section = true;
                    menu.addMenuItem(sectionHeader, startNewInstanceItem);
                }
            }

            for (var i = 0; i < section["actions"].length; ++i) {
                var item = newMenuItem(menu);
                item.action = section["actions"][i];

                textMetrics.text = item.action.text.replace("&", "&&");
                item.action.text = textMetrics.elidedText;

                menu.addMenuItem(item, startNewInstanceItem);
            }
        });

        // Add Media Player control actions
        const playerData = mpris2Source.playerForLauncherUrl(launcherUrl, get(TaskManager.AbstractTasksModel.AppPid));

        if (playerData && playerData.canControl && !(get(TaskManager.AbstractTasksModel.WinIdList) !== undefined && get(TaskManager.AbstractTasksModel.WinIdList).length > 1)) {
            const playing = playerData.playbackStatus === Mpris.PlaybackStatus.Playing;
            let menuItem = menu.newMenuItem(menu);
            menuItem.text = i18nc("Play previous track", "Previous Track");
            menuItem.icon = "media-skip-backward";
            menuItem.enabled = Qt.binding(() => {
                return playerData.canGoPrevious;
            });
            menuItem.clicked.connect(() => {
                playerData.Previous();
            });
            menu.addMenuItem(menuItem, startNewInstanceItem);

            menuItem = menu.newMenuItem(menu);
            // PlasmaCore Menu doesn't actually handle icons or labels changing at runtime...
            menuItem.text = Qt.binding(() => {
                // if CanPause, toggle the menu entry between Play & Pause, otherwise always use Play
                return playing && playerData.canPause ? i18nc("Pause playback", "Pause") : i18nc("Start playback", "Play");
            });
            menuItem.icon = Qt.binding(() => {
                return playing && playerData.canPause ? "media-playback-pause" : "media-playback-start";
            });
            menuItem.enabled = Qt.binding(() => {
                return playing ? playerData.canPause : playerData.canPlay;
            });
            menuItem.clicked.connect(() => {
                if (playing) {
                    playerData.Pause();
                } else {
                    playerData.Play();
                }
            });
            menu.addMenuItem(menuItem, startNewInstanceItem);

            menuItem = menu.newMenuItem(menu);
            menuItem.text = i18nc("Play next track", "Next Track");
            menuItem.icon = "media-skip-forward";
            menuItem.enabled = Qt.binding(() => {
                return playerData.canGoNext;
            });
            menuItem.clicked.connect(() => {
                playerData.Next();
            });
            menu.addMenuItem(menuItem, startNewInstanceItem);

            menuItem = menu.newMenuItem(menu);
            menuItem.text = i18nc("Stop playback", "Stop");
            menuItem.icon = "media-playback-stop";
            menuItem.enabled = Qt.binding(() => {
                return playerData.canStop;
            });
            menuItem.clicked.connect(() => {
                playerData.Stop();
            });
            menu.addMenuItem(menuItem, startNewInstanceItem);

            // Technically media controls and audio streams are separate but for the user they're
            // semantically related, don't add a separator in between.
            if (!(menu.rootTask?.hasAudioStream ?? false)) {
                menu.addMenuItem(newSeparator(menu), startNewInstanceItem);
            }

            // If we don't have a window associated with the player but we can quit
            // it through MPRIS we'll offer a "Quit" option instead of "Close"
            if (!closeWindowItem.visible && playerData.canQuit) {
                menuItem = menu.newMenuItem(menu);
                menuItem.text = i18nc("Quit media player app", "Quit");
                menuItem.icon = "application-exit";
                menuItem.visible = Qt.binding(() => {
                    return !closeWindowItem.visible;
                });
                menuItem.clicked.connect(() => {
                    playerData.Quit();
                });
                menu.addMenuItem(menuItem);
            }

            // If we don't have a window associated with the player but we can raise
            // it through MPRIS we'll offer a "Restore" option
            if (get(TaskManager.AbstractTasksModel.IsLauncher) && !startNewInstanceItem.visible && playerData.canRaise) {
                menuItem = menu.newMenuItem(menu);
                menuItem.text = i18nc("Open or bring to the front window of media player app", "Restore");
                menuItem.icon = playerData.iconName;
                menuItem.visible = Qt.binding(() => {
                    return !startNewInstanceItem.visible;
                });
                menuItem.clicked.connect(() => {
                    playerData.Raise();
                });
                menu.addMenuItem(menuItem, startNewInstanceItem);
            }
        }

        // We allow mute/unmute whenever an application has a stream, regardless of whether it
        // is actually playing sound.
        // This way you can unmute, e.g. a telephony app, even after the conversation has ended,
        // so you still have it ringing later on.
        if ((menu.rootTask?.hasAudioStream ?? false)) {
            const muteItem = menu.newMenuItem(menu);
            muteItem.checkable = true;
            muteItem.checked = Qt.binding(() => {
                return menu.rootTask && menu.rootTask.muted;
            });
            muteItem.clicked.connect(() => {
                menu.rootTask.toggleMuted();
            });
            muteItem.text = i18nc("@option:check inmenu, no separate unmute action", "Mute");
            muteItem.icon = "audio-volume-muted" + (Application.layoutDirection === Qt.RightToLeft ? "-rtl" : "");
            menu.addMenuItem(muteItem, startNewInstanceItem);

            menu.addMenuItem(newSeparator(menu), startNewInstanceItem);
        }
    }

    PlasmaExtras.MenuItem {
        text: i18nc("@info:status application updating", "%1 is being updated…", menu.get(TaskManager.AbstractTasksModel.AppName))
        separator: true
        visible: menu.updateInProgress
    }

    PlasmaExtras.MenuItem {
        id: startNewInstanceItem
        enabled: !menu.updateInProgress
        visible: !menu.updateInProgress && (menu.detachedLauncher || menu.get(TaskManager.AbstractTasksModel.CanLaunchNewInstance))
        text: menu.detachedLauncher ? i18nc("@action:inmenu", "Open") : i18nc("action:inmenu", "Open New Window")
        icon: "window-new"

        onClicked: menu.taskModel.requestNewInstance(menu.modelIndex)
    }

    PlasmaExtras.MenuItem {
        text: i18nc("@action:inmenu", "All Windows")
        icon: "view-preview"
        visible: !menu.updateInProgress && Plasmoid.configuration.showToolTips
            && menu.visualParent && !menu.get(TaskManager.AbstractTasksModel.IsLauncher)
            && !menu.get(TaskManager.AbstractTasksModel.IsStartup)
        onClicked: {
            const root = tasks;
            const task = menu.rootTask || menu.visualParent;
            const index = menu.modelIndex;
            const anchor = menu.visualParent;
            // QMenu closes after emitting clicked. Open the next popup once
            // that close has completed, or its deactivate event dismisses it.
            root.scheduleContextWindowPreviews(task, index, anchor);
        }
    }

    PlasmaExtras.MenuItem {
        id: virtualDesktopsMenuItem

        visible: !menu.updateInProgress && virtualDesktopInfo.numberOfDesktops > 1
            && (menu.visualParent && !menu.get(TaskManager.AbstractTasksModel.IsLauncher)
            && !menu.get(TaskManager.AbstractTasksModel.IsStartup)
            && menu.get(TaskManager.AbstractTasksModel.IsVirtualDesktopsChangeable))

        enabled: visible

        text: i18nc("action:inmenu", "Move to &Desktop")
        icon: "virtual-desktops"

        readonly property Connections virtualDesktopsMenuConnections: Connections {
            target: virtualDesktopInfo

            function onNumberOfDesktopsChanged(): void {
                Qt.callLater(virtualDesktopsMenu.refresh);
            }
            function onDesktopIdsChanged(): void {
                Qt.callLater(virtualDesktopsMenu.refresh);
            }
            function onDesktopNamesChanged(): void {
                Qt.callLater(virtualDesktopsMenu.refresh);
            }
        }

        readonly property PlasmaExtras.Menu _virtualDesktopsMenu: PlasmaExtras.Menu {
            id: virtualDesktopsMenu

            visualParent: virtualDesktopsMenuItem.action

            function refresh(): void {
                clearMenuItems();

                if (virtualDesktopInfo.numberOfDesktops <= 1 || !virtualDesktopsMenuItem.enabled) {
                    return;
                }

                let menuItem = menu.newMenuItem(virtualDesktopsMenu);
                menuItem.text = i18nc("action:inmenu", "Move &To Current Desktop");
                menuItem.enabled = Qt.binding(() => {
                    if (!menu.visualParent) {
                        return false;
                    }

                    let isAnyTaskNotOnCurrentDesktop = false;
                    TaskManagerApplet.TaskTools.foreachChildTask((childIndex) => {
                        const screenGeometry = menu.taskModel.data(childIndex, TaskManager.AbstractTasksModel.ScreenGeometry);
                        const currentDesktop = virtualDesktopInfo.currentDesktopByScreenGeometry(screenGeometry);
                        isAnyTaskNotOnCurrentDesktop = isAnyTaskNotOnCurrentDesktop || menu.taskModel.data(childIndex, TaskManager.AbstractTasksModel.VirtualDesktops).indexOf(currentDesktop) === -1;
                    }, menu.modelIndex, menu.taskModel);

                    return isAnyTaskNotOnCurrentDesktop;
                });
                menuItem.clicked.connect(() => {
                    TaskManagerApplet.TaskTools.foreachChildTask((childIndex) => {
                        menu.taskModel.requestVirtualDesktops(childIndex, [virtualDesktopInfo.currentDesktopByScreenGeometry(menu.taskModel.data(childIndex, TaskManager.AbstractTasksModel.ScreenGeometry))]);
                    }, menu.modelIndex, menu.taskModel);
                });

                menuItem = menu.newMenuItem(virtualDesktopsMenu);
                menuItem.text = i18nc("action:inmenu", "&All Desktops");
                menuItem.checkable = true;
                menuItem.checked = Qt.binding(() => {
                    return menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsOnAllVirtualDesktops);
                });
                menuItem.clicked.connect(() => {
                    menu.taskModel.requestVirtualDesktops(menu.modelIndex, []);
                });
                menu.backend.setActionGroup(menuItem.action);

                menu.newSeparator(virtualDesktopsMenu);

                for (let i = 0; i < virtualDesktopInfo.desktopNames.length; ++i) {
                    menuItem = menu.newMenuItem(virtualDesktopsMenu);
                    menuItem.text = virtualDesktopInfo.desktopNames[i];
                    menuItem.checkable = true;
                    menuItem.checked = Qt.binding((i => {
                        return () => menu.visualParent && menu.get(TaskManager.AbstractTasksModel.VirtualDesktops).indexOf(virtualDesktopInfo.desktopIds[i]) > -1;
                    })(i));
                    menuItem.clicked.connect((i => {
                        return () => menu.taskModel.requestVirtualDesktops(menu.modelIndex, [virtualDesktopInfo.desktopIds[i]]);
                    })(i));
                    menu.backend.setActionGroup(menuItem.action);
                }

                menu.newSeparator(virtualDesktopsMenu);

                menuItem = menu.newMenuItem(virtualDesktopsMenu);
                menuItem.text = i18nc("action:inmenu", "&New Desktop");
                menuItem.icon = "list-add";
                menuItem.clicked.connect(() => {
                    menu.taskModel.requestNewVirtualDesktop(menu.modelIndex);
                });
            }

            Component.onCompleted: refresh()
        }
    }

     PlasmaExtras.MenuItem {
        id: activitiesDesktopsMenuItem

        visible: !menu.updateInProgress && activityInfo.numberOfRunningActivities > 1
            && (menu.visualParent && !menu.get(TaskManager.AbstractTasksModel.IsLauncher)
            && !menu.get(TaskManager.AbstractTasksModel.IsStartup))

        enabled: visible

        text: i18nc("action:inmenu", "Show in &Activities")
        icon: "activities"

        readonly property Connections activityInfoConnections: Connections {
            target: activityInfo

            function onNumberOfRunningActivitiesChanged(): void {
                activitiesDesktopsMenu.refresh()
            }
        }

        readonly property PlasmaExtras.Menu _activitiesDesktopsMenu: PlasmaExtras.Menu {
            id: activitiesDesktopsMenu

            visualParent: activitiesDesktopsMenuItem.action

            function refresh(): void {
                clearMenuItems();

                if (activityInfo.numberOfRunningActivities <= 1) {
                    return;
                }

                let menuItem = menu.newMenuItem(activitiesDesktopsMenu);
                menuItem.text = i18nc("action:inmenu", "Add To Current Activity");
                menuItem.enabled = Qt.binding(() => {
                    return menu.visualParent && menu.get(TaskManager.AbstractTasksModel.Activities).length > 0 &&
                           menu.get(TaskManager.AbstractTasksModel.Activities).indexOf(activityInfo.currentActivity) < 0;
                });
                menuItem.clicked.connect(() => {
                    menu.taskModel.requestActivities(menu.modelIndex, menu.get(TaskManager.AbstractTasksModel.Activities).concat(activityInfo.currentActivity));
                });

                menuItem = menu.newMenuItem(activitiesDesktopsMenu);
                menuItem.text = i18nc("action:inmenu", "All Activities");
                menuItem.checkable = true;
                menuItem.checked = Qt.binding(() => {
                    return menu.visualParent && menu.get(TaskManager.AbstractTasksModel.Activities).length === 0;
                });
                menuItem.toggled.connect(checked => {
                    let newActivities = []; // will cast to an empty QStringList i.e all activities
                    if (!checked) {
                        newActivities = [activityInfo.currentActivity];
                    }
                    menu.taskModel.requestActivities(menu.modelIndex, newActivities);
                });

                menu.newSeparator(activitiesDesktopsMenu);

                const runningActivities = activityInfo.runningActivities();
                for (let i = 0; i < runningActivities.length; ++i) {
                    const activityId = runningActivities[i];

                    menuItem = menu.newMenuItem(activitiesDesktopsMenu);
                    menuItem.text = activityInfo.activityName(runningActivities[i]);
                    menuItem.icon = activityInfo.activityIcon(runningActivities[i]);
                    menuItem.checkable = true;
                    menuItem.checked = Qt.binding((activityId => {
                        return () => menu.visualParent && menu.get(TaskManager.AbstractTasksModel.Activities).indexOf(activityId) >= 0;
                    })(activityId));
                    menuItem.toggled.connect((activityId => {
                        return checked => {
                            let newActivities = menu.get(TaskManager.AbstractTasksModel.Activities);
                            if (checked) {
                                newActivities = newActivities.concat(activityId);
                            } else {
                                const index = newActivities.indexOf(activityId);
                                if (index < 0) {
                                    return;
                                }

                                newActivities.splice(index, 1);
                            }
                            return menu.taskModel.requestActivities(menu.modelIndex, newActivities);
                        };
                    })(activityId));
                }

                menu.newSeparator(activitiesDesktopsMenu);

                for (let i = 0; i < runningActivities.length; ++i) {
                    const activityId = runningActivities[i];
                    const onActivities = menu.get(TaskManager.AbstractTasksModel.Activities);

                    // if the task is on a single activity, don't insert a "move to" item for that activity
                    if (onActivities.length === 1 && onActivities[0] === activityId) {
                        continue;
                    }

                    menuItem = menu.newMenuItem(activitiesDesktopsMenu);
                    menuItem.text = i18nc("action:inmenu", "Move to %1", activityInfo.activityName(activityId))
                    menuItem.icon = activityInfo.activityIcon(activityId)
                    menuItem.clicked.connect((activityId => {
                        return () => menu.taskModel.requestActivities(menu.modelIndex, [activityId]);
                    })(activityId));
                }

                menu.newSeparator(activitiesDesktopsMenu);
            }

            Component.onCompleted: refresh()
        }
    }

    PlasmaExtras.MenuItem {
        id: launcherToggleAction

        visible: menu.visualParent
            && (menu.detachedLauncher || !menu.get(TaskManager.AbstractTasksModel.IsLauncher))
            && !menu.get(TaskManager.AbstractTasksModel.IsStartup)
            && Plasmoid.immutability !== PlasmaCore.Types.SystemImmutable
            && (activityInfo.numberOfRunningActivities < 2)
            && !doesBelongToCurrentActivity()

        enabled: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.LauncherUrlWithoutIcon).toString() !== ""

        text: i18nc("action:inmenu", "&Pin to Task Manager")
        icon: "window-pin"

        function doesBelongToCurrentActivity(): bool {
            return tasksModel.launcherActivities(menu.get(TaskManager.AbstractTasksModel.LauncherUrlWithoutIcon))
                .some(activity => activity === activityInfo.currentActivity || activity === activityInfo.nullUuid);
        }

        onClicked: {
            tasksModel.requestAddLauncher(menu.get(TaskManager.AbstractTasksModel.LauncherUrl));
        }
    }

    PlasmaExtras.MenuItem {
        id: showLauncherInActivitiesItem

        text: i18nc("action:inmenu", "&Pin to Task Manager")
        icon: "window-pin"

        visible: menu.visualParent
            && !menu.get(TaskManager.AbstractTasksModel.IsStartup)
            && Plasmoid.immutability !== PlasmaCore.Types.SystemImmutable
            && (activityInfo.numberOfRunningActivities >= 2)

        readonly property Connections activitiesLaunchersMenuConnections: Connections {
            target: activityInfo

            function onNumberOfRunningActivitiesChanged(): void {
                activitiesLaunchersMenu.refresh()
            }
        }

        readonly property PlasmaExtras.Menu _activitiesLaunchersMenu: PlasmaExtras.Menu {
            id: activitiesLaunchersMenu
            visualParent: showLauncherInActivitiesItem.action

            function refresh(): void {
                clearMenuItems();

                if (menu.visualParent === null) return;

                const createNewItem = (id, title, iconName, url, activities) => {
                    var result = menu.newMenuItem(activitiesLaunchersMenu);
                    result.text = title;
                    result.icon = iconName;

                    result.visible = true;
                    result.checkable = true;

                    result.checked = activities.some(activity => activity === id);

                    result.clicked.connect(() => {
                        if (result.checked) {
                            tasksModel.requestAddLauncherToActivity(url, id);
                        } else {
                            tasksModel.requestRemoveLauncherFromActivity(url, id);
                        }
                    });

                    return result;
                };

                if (menu.visualParent === null) return;

                const url = menu.get(TaskManager.AbstractTasksModel.LauncherUrlWithoutIcon);

                const activities = tasksModel.launcherActivities(url);

                createNewItem(activityInfo.nullUuid, i18nc("action:inmenu", "On All Activities"), "", url, activities);

                if (activityInfo.numberOfRunningActivities <= 1) {
                    return;
                }

                createNewItem(activityInfo.currentActivity, i18nc("action:inmenu", "On The Current Activity"), activityInfo.activityIcon(activityInfo.currentActivity), url, activities);

                menu.newSeparator(activitiesLaunchersMenu);

                activityInfo.runningActivities()
                    .forEach(id => {
                        createNewItem(id, activityInfo.activityName(id), activityInfo.activityIcon(id), url, activities);
                    });
            }

            Component.onCompleted: {
                menu.visualParentChanged.connect(refresh);
                refresh();
            }
        }
    }

    PlasmaExtras.MenuItem {
        visible: (menu.visualParent
                && menu.get(TaskManager.AbstractTasksModel.IsStartup) !== true
                && Plasmoid.immutability !== PlasmaCore.Types.SystemImmutable
                && !launcherToggleAction.visible
                && activityInfo.numberOfRunningActivities < 2)

        text: i18nc("action:inmenu", "Unpin from Task Manager")
        icon: "window-unpin"

        onClicked: {
            tasksModel.requestRemoveLauncher(menu.get(TaskManager.AbstractTasksModel.LauncherUrlWithoutIcon));
        }
    }

    PlasmaExtras.MenuItem {
        id: appDetailsItem
        visible: detailsAction !== null
        readonly property var detailsAction: menu.visualParent
            ? backend.applicationDetailsAction(menu.get(TaskManager.AbstractTasksModel.LauncherUrlWithoutIcon), menu)
            : null
        action: detailsAction
    }

    PlasmaExtras.MenuItem {
        id: moreActionsMenuItem

        visible: (!menu.updateInProgress && menu.visualParent && !menu.get(TaskManager.AbstractTasksModel.IsLauncher) && !menu.get(TaskManager.AbstractTasksModel.IsStartup))

        enabled: visible

        text: i18nc("item:inmenu opens submenu", "More")
        icon: "view-more-symbolic"

        readonly property PlasmaExtras.Menu moreMenu: PlasmaExtras.Menu {
            visualParent: moreActionsMenuItem.action

            PlasmaExtras.MenuItem {
                enabled: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsMovable)

                text: i18nc("action:inmenu", "&Move")
                icon: "transform-move"

                onClicked: menu.taskModel.requestMove(menu.modelIndex)
            }

            PlasmaExtras.MenuItem {
                enabled: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsResizable)

                text: i18nc("action:inmenu", "Re&size")
                icon: "transform-scale"

                onClicked: menu.taskModel.requestResize(menu.modelIndex)
            }

            PlasmaExtras.MenuItem {
                visible: (menu.visualParent && !menu.get(TaskManager.AbstractTasksModel.IsLauncher) && !menu.get(TaskManager.AbstractTasksModel.IsStartup))

                enabled: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsMaximizable)

                checkable: true
                checked: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsMaximized)

                text: i18nc("action:inmenu", "Ma&ximize")
                icon: "window-maximize"

                onClicked: menu.taskModel.requestToggleMaximized(menu.modelIndex)
            }

            PlasmaExtras.MenuItem {
                visible: (menu.visualParent && !menu.get(TaskManager.AbstractTasksModel.IsLauncher) && !menu.get(TaskManager.AbstractTasksModel.IsStartup))

                enabled: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsMinimizable)

                checkable: true
                checked: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsMinimized)

                text: i18nc("action:inmenu", "Mi&nimize")
                icon: "window-minimize"

                onClicked: menu.taskModel.requestToggleMinimized(menu.modelIndex)
            }

            PlasmaExtras.MenuItem {
                checkable: true
                checked: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsKeepAbove)

                text: i18nc("action:inmenu", "Keep &Above Others")
                icon: "window-keep-above"

                onClicked: menu.taskModel.requestToggleKeepAbove(menu.modelIndex)
            }

            PlasmaExtras.MenuItem {
                checkable: true
                checked: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsKeepBelow)

                text: i18nc("action:inmenu", "Keep &Below Others")
                icon: "window-keep-below"

                onClicked: menu.taskModel.requestToggleKeepBelow(menu.modelIndex)
            }

            PlasmaExtras.MenuItem {
                enabled: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsFullScreenable)

                checkable: true
                checked: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsFullScreen)

                text: i18nc("action:inmenu", "&Fullscreen")
                icon: "view-fullscreen"

                onClicked: menu.taskModel.requestToggleFullScreen(menu.modelIndex)
            }

            PlasmaExtras.MenuItem {
                enabled: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsShadeable)

                checkable: true
                checked: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsShaded)
                visible: Qt.platform.pluginName !== "wayland"

                text: i18nc("action:inmenu", "&Shade")
                icon: "window-shade"

                onClicked: menu.taskModel.requestToggleShaded(menu.modelIndex)
            }

            PlasmaExtras.MenuItem {
                enabled: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.CanSetNoBoder)

                checkable: true
                checked: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.HasNoBorder)

                text: i18nc("@action:inmenu", "&No Titlebar and Frame")
                icon: "edit-none-border"

                onClicked: menu.taskModel.requestToggleNoBorder(menu.modelIndex)
            }

            PlasmaExtras.MenuItem {
                enabled: menu.visualParent

                checkable: true
                checked: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsExcludedFromCapture)
                visible: Qt.platform.pluginName === "wayland"

                text: i18nc("@action:inmenu", "&Hide from Screencast")
                icon: "view-private"

                onClicked: menu.taskModel.requestToggleExcludeFromCapture(menu.modelIndex)
            }

            PlasmaExtras.MenuItem {
                separator: true
            }

            PlasmaExtras.MenuItem {
                visible: (Plasmoid.configuration.groupingStrategy !== 0) && menu.get(TaskManager.AbstractTasksModel.IsWindow)

                checkable: true
                checked: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsGroupable)

                text: i18nc("@option:check inmenu", "Allow this program to be grouped")
                icon: "view-group"

                onClicked: menu.taskModel.requestToggleGrouping(menu.modelIndex)
            }
        }
    }

    PlasmaExtras.MenuItem { separator: true; visible: !menu.updateInProgress }

    PlasmaExtras.MenuItem {
        property PlasmaCore.Action configureAction: null

        enabled: configureAction && configureAction.enabled
        visible: !menu.updateInProgress && configureAction && configureAction.visible

        text: configureAction ? configureAction.text : ""
        icon: configureAction ? configureAction.icon : ""

        onClicked: configureAction.trigger()

        Component.onCompleted: configureAction = Plasmoid.internalAction("configure")
    }

    PlasmaExtras.MenuItem {
        property PlasmaCore.Action editModeAction: null

        enabled: editModeAction && editModeAction.enabled
        visible: !menu.updateInProgress && editModeAction && editModeAction.visible

        text: editModeAction ? editModeAction.text : ""
        icon: editModeAction ? editModeAction.icon : ""

        onClicked: editModeAction.trigger()

        Component.onCompleted: editModeAction = Plasmoid.containment.internalAction("configure")
    }

    PlasmaExtras.MenuItem { separator: true; visible: !menu.updateInProgress }

    PlasmaExtras.MenuItem {
        id: closeWindowItem
        visible: (!menu.updateInProgress && menu.visualParent && !menu.get(TaskManager.AbstractTasksModel.IsLauncher) && !menu.get(TaskManager.AbstractTasksModel.IsStartup))

        enabled: menu.visualParent && menu.get(TaskManager.AbstractTasksModel.IsClosable)

        text: menu.get(TaskManager.AbstractTasksModel.IsGroupParent) ? i18nc("@action:inmenu", "&Close All") : i18nc("@action:inmenu", "&Close")
        icon: "window-close"

        onClicked: {
            if (tasks.groupDialog !== null && tasks.groupDialog.visualParent === menu.visualParent) {
                tasks.groupDialog.visible = false;
            }

            menu.taskModel.requestClose(menu.modelIndex);
        }
    }
}
