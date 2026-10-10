/*
    SPDX-FileCopyrightText: 2012-2016 Eike Hein <hein@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import "code/DockActions.js" as DockActions
import "code/DockGeometry.js" as DockGeometry
import "code/StackLogic.js" as StackLogic
import "code/ConfigSnapshot.js" as ConfigSnapshot

import org.kde.plasma.plasmoid
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.core as PlasmaCore
import org.kde.ksvg as KSvg
import org.kde.plasma.private.mpris as Mpris
import org.kde.kirigami as Kirigami

import org.kde.plasma.workspace.trianglemousefilter

import org.kde.taskmanager as TaskManager
import plasma.applet.org.gosh.goshosdock as TaskManagerApplet
import org.kde.plasma.workspace.dbus as DBus

PlasmoidItem {
    id: tasks

    // For making a bottom to top layout since qml flow can't do that.
    // We just hang the task manager upside down to achieve that.
    // This mirrors the tasks and group dialog as well, so we un-rotate them
    // to fix that (see Task.qml and GroupDialog.qml).
    rotation: Plasmoid.configuration.reverseMode && Plasmoid.formFactor === PlasmaCore.Types.Vertical ? 180 : 0

    readonly property bool shouldShrinkToZero: visibleTaskCount === 0 && dockExtras.itemCount === 0 && leadingApps.itemCount === 0
    readonly property int visibleTaskCount: taskList.children.filter(item => item.visible && typeof item.modelIndex === "function").length
    readonly property var stripSlots: DockGeometry.dockSlots(vertical ? height : width,
        taskList.contentLength, vertical ? dockExtras.implicitHeight : dockExtras.implicitWidth,
        vertical ? leadingApps.implicitHeight : leadingApps.implicitWidth,
        Plasmoid.configuration.showAppsAtStart,
        Plasmoid.configuration.extendDock && Plasmoid.configuration.appsAlwaysAtEdge,
        Plasmoid.configuration.extendDock && Plasmoid.configuration.centerIcons)
    readonly property real extraLength: stripSlots.extrasLength
    readonly property real leadingLength: stripSlots.appsLength
    readonly property real availableTaskWidth: vertical ? width : stripSlots.taskLength
    readonly property real availableTaskHeight: vertical ? stripSlots.taskLength : height
    readonly property real effectiveIconSize: DockGeometry.chooseIconSize(Plasmoid.configuration.iconSize,
        visibleTaskCount + dockExtras.itemCount + leadingApps.itemCount,
        (vertical ? height : width) - dockExtras.dividerLength - leadingApps.dividerLength,
        vertical ? width : height,
        Math.max(vertical ? TaskManagerApplet.LayoutMetrics.verticalMargins() : TaskManagerApplet.LayoutMetrics.horizontalMargins(), 2 * Kirigami.Units.smallSpacing),
        vertical ? TaskManagerApplet.LayoutMetrics.horizontalMargins() : TaskManagerApplet.LayoutMetrics.verticalMargins(),
        Plasmoid.configuration.iconSizeFixed)
    readonly property real dockPointerX: dockHover.point.position.x
    readonly property real dockPointerY: dockHover.point.position.y
    readonly property bool dockHovered: dockHover.hovered && !dragSource && !dockExtras.popupVisible && !leadingApps.popupVisible
        && !integrationNotice.popupVisible && !shortcutTimer.running

    readonly property bool shortcutOverlayVisible: shortcutTimer.running && Plasmoid.configuration.hotkeysOverlay
    readonly property bool shortcutRevealActive: shortcutTimer.running && Plasmoid.configuration.hotkeysShowDock
    readonly property bool overviewVisible: dockController.overviewVisible
    readonly property bool overviewAvailable: dockController.overviewAvailable

    function finishDockAction(command): void {
        if (overviewVisible && command !== "overview" && command !== "spread") dockController.hideOverview();
    }

    readonly property var configurationSnapshot: ConfigSnapshot.snapshot(Plasmoid.configuration)
    readonly property bool popupOpen: dockExtras.popupVisible || leadingApps.popupVisible
        || integrationNotice.popupVisible
        || (launcherContextMenu?.status === PlasmaExtras.Menu.Open)
        || (contextPreviewDialog?.visible ?? false) || (groupDialog?.visible ?? false)
        || visibleTasks().some(task => task.toolTipOpen || task.contextMenu?.status === PlasmaExtras.Menu.Open)

    TaskManagerApplet.DockController {
        id: dockController
        enabled: true
        target: tasks
        configuration: Object.assign({}, tasks.configurationSnapshot, {showDockUrgent: Plasmoid.configuration.unhideOnAttention})
        holdVisible: tasks.popupOpen || !!tasks.dragSource || mouseHandler.containsDrag || tasks.shortcutRevealActive || Plasmoid.userConfiguring
        urgent: urgentRevealTimer.running
    }
    TaskManagerApplet.PanelLayout {
        id: panelLayout
        panelId: dockController.panelId
        appletId: dockController.appletId
        configuration: tasks.configurationSnapshot
        enabled: panelId > 0 && appletId > 0
    }

    IntegrationNotice {
        id: integrationNotice
        anchors.top: parent.top
        anchors.right: parent.right
        message: [dockController.error, panelLayout.error, globalShortcuts.error].filter(Boolean).join("\n")
    }

    Timer { id: urgentRevealTimer; interval: 3000 }
    function revealForUrgency(): void {
        if (Plasmoid.configuration.unhideOnAttention) urgentRevealTimer.restart();
    }

    HoverHandler { id: dockHover }
    Timer {
        id: shortcutTimer
        interval: Plasmoid.configuration.shortcutTimeout * 1000
    }
    TaskManagerApplet.GlobalShortcuts {
        id: globalShortcuts
        enabled: Plasmoid.configuration.hotKeys
        allOutputs: Plasmoid.configuration.allOutputs
        screenGeometry: Plasmoid.containment.screenGeometry
        onOverlayRequested: shortcutTimer.restart()
        onTaskRequested: (index, launch, shifted) => tasks.activateShortcut(index, launch, shifted)
    }

    function visibleTasks() {
        return taskList.children.filter(item => item.visible && typeof item.modelIndex === "function");
    }

    function shortcutNumber(task): int {
        return StackLogic.shortcutNumber(shortcutTargets(), task);
    }

    function shortcutTargets(): var {
        return StackLogic.shortcutTargets(visibleTasks(), dockExtras.shortcutTargets());
    }

    function activateShortcut(index, launch = false, shifted = false): void {
        const target = shortcutTargets()[index];
        if (!target || target.appUpdating) return;
        shortcutTimer.restart();
        if (typeof target.modelIndex === "function") {
            dockViewport.revealItem(target);
            if (launch) tasksModel.requestNewInstance(target.modelIndex());
            else performDockAction(target, shifted ? Qt.ShiftModifier : 0, false);
        } else target.activateShortcut(launch, shifted);
    }

    function findApplicationTask(application): var {
        const candidates = [];
        for (let index = 0; index < taskRepeater.count; ++index) candidates.push(taskRepeater.itemAt(index));
        return StackLogic.applicationTask(candidates, application);
    }

    function activateRecentApplication(application, anchor, modifiers = 0, middle = false, launch = false, tapCount = 1): bool {
        const task = findApplicationTask(application);
        if (!task) return false;
        if (!task.appUpdating) {
            if (launch) tasksModel.requestNewInstance(task.modelIndex());
            else performDockAction(task, modifiers, middle, anchor, tapCount);
        }
        return true;
    }

    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground

    readonly property bool useThemeBackground: Plasmoid.configuration.visualStyle === 0
        && !Plasmoid.configuration.customBackgroundColor && Plasmoid.configuration.cornerRadius > 0
    readonly property real dockBackgroundOpacity: Plasmoid.configuration.transparencyMode === 1
        ? (overlappingWindows.count > 0 && !dockController.overviewVisible
            ? (Plasmoid.configuration.customizeAlphas ? Plasmoid.configuration.maxAlpha : 1)
            : (Plasmoid.configuration.customizeAlphas ? Plasmoid.configuration.minAlpha : 0.2))
        : Plasmoid.configuration.transparencyMode === 2 ? 1 : Plasmoid.configuration.backgroundOpacity

    KSvg.FrameSvgItem {
        anchors.fill: parent
        visible: tasks.useThemeBackground
        imagePath: "widgets/panel-background"
        prefix: tasks.vertical ? (Plasmoid.location === PlasmaCore.Types.LeftEdge ? "west" : "east")
            : (Plasmoid.location === PlasmaCore.Types.TopEdge ? "north" : "south")
        opacity: tasks.dockBackgroundOpacity
        Behavior on opacity { NumberAnimation { duration: Plasmoid.configuration.animationTime * 1000 } }
    }
    Rectangle {
        anchors.fill: parent
        visible: !tasks.useThemeBackground
        radius: Plasmoid.configuration.visualStyle === 2 ? 0 : Plasmoid.configuration.cornerRadius
        color: Plasmoid.configuration.customBackgroundColor ? Plasmoid.configuration.customColor : Kirigami.Theme.backgroundColor
        opacity: Plasmoid.configuration.visualStyle === 2 ? 0 : tasks.dockBackgroundOpacity
        Behavior on opacity { NumberAnimation { duration: Plasmoid.configuration.animationTime * 1000 } }
        border.width: Plasmoid.configuration.visualStyle === 1 ? 1 : 0
        border.color: Kirigami.Theme.textColor
    }

    DockExtras {
        id: dockExtras
        locationTasksModel: tasks.locationTasksModel
        windowViewAvailable: effectWatcher.registered
        onRequestWindowView: ids => tasks.activateWindowView(ids)
        onRequestOverview: tasks.showOverview()
        configuration: Plasmoid.configuration
        iconSize: tasks.effectiveIconSize
        omitApplicationsButton: true
        dockRoot: tasks
        dockPointerX: tasks.dockPointerX
        dockPointerY: tasks.dockPointerY
        dockHovered: tasks.dockHovered
        rotation: tasks.rotation
        vertical: tasks.vertical
        edge: Plasmoid.location
        x: tasks.vertical ? 0 : tasks.stripSlots.extrasStart
        y: tasks.vertical ? tasks.stripSlots.extrasStart : 0
        width: tasks.vertical ? tasks.width : tasks.extraLength
        height: tasks.vertical ? tasks.extraLength : tasks.height
        excludedApplications: tasks.visibleTasks().map(task => String(task.model.LauncherUrlWithoutIcon))
        onRequestApplicationLauncher: DBus.SessionBus.asyncCall({service: "org.kde.plasmashell", path: "/PlasmaShell", iface: "org.kde.PlasmaShell", member: "activateLauncherMenu"})
    }
    readonly property bool vertical: Plasmoid.formFactor === PlasmaCore.Types.Vertical
    readonly property bool iconsOnly: true

    DockExtras {
        id: leadingApps
        configuration: Plasmoid.configuration
        iconSize: tasks.effectiveIconSize
        leadingApplicationsOnly: true
        visible: Plasmoid.configuration.showApplications
        sharedBackend: dockExtras.stackBackend
        dockRoot: tasks
        dockPointerX: tasks.dockPointerX
        dockPointerY: tasks.dockPointerY
        dockHovered: tasks.dockHovered
        vertical: tasks.vertical
        edge: Plasmoid.location
        rotation: tasks.rotation
        x: tasks.vertical ? 0 : tasks.stripSlots.appsStart
        y: tasks.vertical ? tasks.stripSlots.appsStart : 0
        width: tasks.vertical ? tasks.width : tasks.leadingLength
        height: tasks.vertical ? tasks.leadingLength : tasks.height
        onRequestApplicationLauncher: dockExtras.requestApplicationLauncher()
    }

    function opacityRegion(): rect {
        const screen = Plasmoid.containment.screenGeometry;
        const dock = tasks.backend.globalRect(tasks);
        const edge = Plasmoid.location === PlasmaCore.Types.LeftEdge ? "left"
            : Plasmoid.location === PlasmaCore.Types.RightEdge ? "right"
            : Plasmoid.location === PlasmaCore.Types.TopEdge ? "top" : "bottom";
        const region = DockGeometry.edgeRegion(screen, dock, edge);
        return Qt.rect(region.x, region.y, region.width, region.height);
    }

    TaskManagerApplet.DockTasksModel {
        id: overlappingWindows
        demandingAttentionSkipsFilters: false
        filterByScreen: true
        screenGeometry: Plasmoid.containment.screenGeometry
        filterByCurrentVirtualDesktop: true
        filterByActivity: true
        activity: activityInfo.currentActivity
        filterMinimized: true
        filterByRegion: TaskManager.RegionFilterMode.Intersect
        regionGeometry: tasks.opacityRegion()
        groupMode: TaskManager.TasksModel.GroupDisabled
    }
    Timer {
        interval: 250
        repeat: true
        running: Plasmoid.configuration.transparencyMode === 1
        onTriggered: overlappingWindows.regionGeometry = tasks.opacityRegion()
    }

    property Task toolTipOpenedByClick
    property Task toolTipAreaItem

    readonly property Component contextMenuComponent: Qt.createComponent("ContextMenu.qml")
    readonly property Component pulseAudioComponent: Qt.createComponent("PulseAudio.qml")

    property alias taskList: taskList
    property alias taskViewport: dockViewport
    property alias locationTasksModel: locationTasksModel
    property alias locationBackend: dockExtras.locationBackend

    preferredRepresentation: fullRepresentation

    Plasmoid.constraintHints: Plasmoid.CanFillArea

    Plasmoid.onUserConfiguringChanged: {
        if (Plasmoid.userConfiguring && groupDialog !== null) {
            groupDialog.visible = false;
        }
    }

    Layout.fillWidth: vertical || Plasmoid.configuration.extendDock || Plasmoid.configuration.fill
    Layout.fillHeight: !vertical || Plasmoid.configuration.extendDock || Plasmoid.configuration.fill
    Layout.minimumWidth: {
        if (shouldShrinkToZero) {
            return Kirigami.Units.gridUnit; // For edit mode
        }
        return vertical ? 0 : TaskManagerApplet.LayoutMetrics.preferredMinWidth();
    }
    Layout.minimumHeight: {
        if (shouldShrinkToZero) {
            return Kirigami.Units.gridUnit; // For edit mode
        }
        return !vertical ? 0 : TaskManagerApplet.LayoutMetrics.preferredMinHeight();
    }

//BEGIN TODO: this is not precise enough: launchers are smaller than full tasks
    Layout.preferredWidth: {
        if (shouldShrinkToZero) {
            return 0.01;
        }
        if (vertical) {
            return Plasmoid.configuration.iconSize + Kirigami.Units.largeSpacing;
        }
        return taskList.Layout.maximumWidth + dockExtras.implicitWidth + leadingApps.implicitWidth
    }
    Layout.preferredHeight: {
        if (shouldShrinkToZero) {
            return 0.01;
        }
        if (vertical) {
            return taskList.Layout.maximumHeight + dockExtras.implicitHeight + leadingApps.implicitHeight
        }
        return Plasmoid.configuration.iconSize + Kirigami.Units.largeSpacing;
    }
//END TODO

    property Item dragSource

    signal requestLayout

    onDragSourceChanged: {
        if (dragSource === null) {
            tasksModel.syncLaunchers();
        }
    }

    function windowsHovered(winIds: var, hovered: bool): DBus.DBusPendingReply {
        if (!Plasmoid.configuration.highlightWindows) {
            return;
        }
        return DBus.SessionBus.asyncCall({service: "org.kde.KWin.HighlightWindow", path: "/org/kde/KWin/HighlightWindow", iface: "org.kde.KWin.HighlightWindow", member: "highlightWindows", arguments: [hovered ? winIds : []], signature: "(as)"});
    }

    function cancelHighlightWindows(): DBus.DBusPendingReply {
        return DBus.SessionBus.asyncCall({service: "org.kde.KWin.HighlightWindow", path: "/org/kde/KWin/HighlightWindow", iface: "org.kde.KWin.HighlightWindow", member: "highlightWindows", arguments: [[]], signature: "(as)"});
    }

    function activateWindowView(winIds: var): DBus.DBusPendingReply {
        if (!effectWatcher.registered) {
            return;
        }
        cancelHighlightWindows();
        return DBus.SessionBus.asyncCall({service: "org.kde.KWin.Effect.WindowView1", path: "/org/kde/KWin/Effect/WindowView1", iface: "org.kde.KWin.Effect.WindowView1", member: "activate", arguments: [winIds.map(s => String(s))], signature: "(as)"});
    }

    function publishIconGeometries(taskItems: /*list<Item>*/var): void {
        if (TaskManagerApplet.TaskTools.taskManagerInstanceCount >= 2) {
            return;
        }
        for (let i = 0; i < taskItems.length; ++i) {
            const task = taskItems[i];

            if (task.model && !task.model.IsLauncher && !task.model.IsStartup) {
                tasksModel.requestPublishDelegateGeometry(tasksModel.makeModelIndex(task.index),
                    backend.globalRect(task), task);
            }
        }
    }

    TaskManagerApplet.DockTasksModel {
        id: locationTasksModel
        demandingAttentionSkipsFilters: Plasmoid.configuration.workspaceAgnosticUrgent
        groupMode: TaskManager.TasksModel.GroupDisabled
        screenGeometry: Plasmoid.containment.screenGeometry
        activity: activityInfo.currentActivity
        filterByCurrentVirtualDesktop: Plasmoid.configuration.showOnlyCurrentDesktop
        filterByScreen: Plasmoid.configuration.showOnlyCurrentScreen
        filterByActivity: Plasmoid.configuration.showOnlyCurrentActivity
    }

    readonly property TaskManagerApplet.DockTasksModel tasksModel: TaskManagerApplet.DockTasksModel {
        id: tasksModel
        demandingAttentionSkipsFilters: Plasmoid.configuration.workspaceAgnosticUrgent
        excludedWindowIds: dockExtras.locationBackend ? dockExtras.locationBackend.excludedWindowIds : []

        readonly property int logicalLauncherCount: {
            if (Plasmoid.configuration.separateLaunchers) {
                return launcherCount;
            }

            let startupsWithLaunchers = 0;

            for (let i = 0; i < taskRepeater.count; ++i) {
                const item = taskRepeater.itemAt(i) as Task;

                // During destruction required properties such as item.model can go null for a while,
                // so in paths that can trigger on those moments, they need to be guarded
                if (item?.model?.IsStartup && item.model.HasLauncher) {
                    ++startupsWithLaunchers;
                }
            }

            return launcherCount + startupsWithLaunchers;
        }

        screenGeometry: Plasmoid.containment.screenGeometry
        activity: activityInfo.currentActivity

        filterByCurrentVirtualDesktop: Plasmoid.configuration.showOnlyCurrentDesktop
        filterByScreen: Plasmoid.configuration.showOnlyCurrentScreen
        filterByActivity: Plasmoid.configuration.showOnlyCurrentActivity
        filterNotMinimized: Plasmoid.configuration.showOnlyMinimized

        hideActivatedLaunchers: tasks.iconsOnly || Plasmoid.configuration.hideLauncherOnStart
        sortMode: sortModeEnumValue(Plasmoid.configuration.sortingStrategy)
        launchInPlace: tasks.iconsOnly && Plasmoid.configuration.sortingStrategy === 1
        separateLaunchers: {
            if (!tasks.iconsOnly && !Plasmoid.configuration.separateLaunchers
                && Plasmoid.configuration.sortingStrategy === 1) {
                return false;
            }

            return true;
        }

        groupMode: groupModeEnumValue(Plasmoid.configuration.groupingStrategy)
        groupInline: !Plasmoid.configuration.groupPopups && !tasks.iconsOnly
        groupingWindowTasksThreshold: (Plasmoid.configuration.onlyGroupWhenFull && !tasks.iconsOnly
            ? TaskManagerApplet.LayoutMetrics.optimumCapacity(tasks.width, tasks.height) + 1 : -1)

        onLauncherListChanged: {
            Plasmoid.configuration.launchers = launcherList;
        }

        onGroupingAppIdBlacklistChanged: {
            Plasmoid.configuration.groupingAppIdBlacklist = groupingAppIdBlacklist;
        }

        onGroupingLauncherUrlBlacklistChanged: {
            Plasmoid.configuration.groupingLauncherUrlBlacklist = groupingLauncherUrlBlacklist;
        }

        function sortModeEnumValue(index: int): /*TaskManager.TasksModel.SortMode*/ int {
            switch (index) {
            case 0:
                return TaskManager.TasksModel.SortDisabled;
            case 1:
                return TaskManager.TasksModel.SortManual;
            case 2:
                return TaskManager.TasksModel.SortAlpha;
            case 3:
                return TaskManager.TasksModel.SortVirtualDesktop;
            case 4:
                return TaskManager.TasksModel.SortActivity;
            // 5 is SortLastActivated, skipped
            case 6:
                return TaskManager.TasksModel.SortWindowPositionHorizontal;
            default:
                return TaskManager.TasksModel.SortDisabled;
            }
        }

        function groupModeEnumValue(index: int): /*TaskManager.TasksModel.GroupMode*/ int {
            switch (index) {
            case 0:
                return TaskManager.TasksModel.GroupDisabled;
            case 1:
                return TaskManager.TasksModel.GroupApplications;
            }
        }

        Component.onCompleted: {
            launcherList = Plasmoid.configuration.launchers;
            groupingAppIdBlacklist = Plasmoid.configuration.groupingAppIdBlacklist;
            groupingLauncherUrlBlacklist = Plasmoid.configuration.groupingLauncherUrlBlacklist;

            // Only hook up view only after the above churn is done.
            taskRepeater.model = tasksModel;
        }
    }

    readonly property TaskManagerApplet.Backend backend: TaskManagerApplet.Backend {
        id: backend

        onAddLauncher: url => {
            tasks.addLauncher(url);
        }
    }

    TaskManagerApplet.QuicklistBackend { id: quicklistBackend }

    DBus.DBusServiceWatcher {
        id: effectWatcher
        busType: DBus.BusType.Session
        watchedService: "org.kde.KWin.Effect.WindowView1"
    }

    readonly property Component taskInitComponent: Component {
        Timer {
            interval: 200
            running: true

            onTriggered: {
                const task = parent as Task;
                if (task) {
                    tasks.tasksModel.requestPublishDelegateGeometry(task.modelIndex(), tasks.backend.globalRect(task), task);
                }
                destroy();
            }
        }
    }

    Connections {
        target: Plasmoid

        function onLocationChanged(): void {
            if (TaskManagerApplet.TaskTools.taskManagerInstanceCount >= 2) {
                return;
            }
            // This is on a timer because the panel may not have
            // settled into position yet when the location prop-
            // erty updates.
            iconGeometryTimer.start();
        }
    }

    Connections {
        target: Plasmoid.containment

        function onScreenGeometryChanged(): void {
            iconGeometryTimer.start();
        }
    }

    Connections {
        target: tasksModel
        function onActiveTaskChanged(): void { Qt.callLater(dockViewport.revealFocusedTask); }
    }

    Mpris.Mpris2Model {
        id: mpris2Source
    }

    Item {
        x: tasks.vertical ? 0 : tasks.stripSlots.taskStart
        y: tasks.vertical ? tasks.stripSlots.taskStart : 0
        width: tasks.availableTaskWidth
        height: tasks.availableTaskHeight

        TaskManager.VirtualDesktopInfo {
            id: virtualDesktopInfo
        }

        TaskManager.ActivityInfo {
            id: activityInfo
            readonly property string nullUuid: "00000000-0000-0000-0000-000000000000"
        }

        Loader {
            id: pulseAudio
            sourceComponent: tasks.pulseAudioComponent
            active: tasks.pulseAudioComponent.status === Component.Ready
        }

        Timer {
            id: iconGeometryTimer

            interval: 500
            repeat: false

            onTriggered: {
                tasks.publishIconGeometries(taskList.children, tasks);
            }
        }

        Binding {
            target: Plasmoid
            property: "status"
            value: (dockExtras.popupVisible || leadingApps.popupVisible || integrationNotice.popupVisible || contextPreviewDialog?.visible || shortcutRevealActive)
                ? PlasmaCore.Types.RequiresAttentionStatus
                : (urgentRevealTimer.running && Plasmoid.configuration.unhideOnAttention
                    ? PlasmaCore.Types.NeedsAttentionStatus : PlasmaCore.Types.PassiveStatus)
            restoreMode: Binding.RestoreBinding
        }

        Connections {
            target: Plasmoid.configuration

            function onLaunchersChanged(): void {
                tasksModel.launcherList = Plasmoid.configuration.launchers
            }
            function onGroupingAppIdBlacklistChanged(): void {
                tasksModel.groupingAppIdBlacklist = Plasmoid.configuration.groupingAppIdBlacklist;
            }
            function onGroupingLauncherUrlBlacklistChanged(): void {
                tasksModel.groupingLauncherUrlBlacklist = Plasmoid.configuration.groupingLauncherUrlBlacklist;
            }
        }

        Component {
            id: busyIndicator
            PlasmaComponents3.BusyIndicator {}
        }

        // Save drag data
        Item {
            id: dragHelper

            Drag.dragType: Drag.Automatic
            Drag.supportedActions: Qt.CopyAction | Qt.MoveAction | Qt.LinkAction
            Drag.onDragFinished: dropAction => {
                tasks.dragSource = null;
            }
        }

        KSvg.FrameSvgItem {
            id: taskFrame

            visible: false

            imagePath: "widgets/tasks"
            prefix: TaskManagerApplet.TaskTools.taskPrefix("normal", Plasmoid.location)
        }

        MouseHandler {
            id: mouseHandler

            width: tasks.availableTaskWidth
            height: tasks.availableTaskHeight

            target: taskList
            handleWheelEvents: !(Plasmoid.configuration.iconSizeFixed && dockViewport.overflow)
            onPositionChanged: event => {
                const point = mapToItem(dockViewport, event.x, event.y);
                dockViewport.dragPosition = tasks.vertical ? point.y : point.x;
            }
            onExited: dockViewport.dragPosition = -1

            onUrlsDropped: urls => {
                // If all dropped URLs point to application desktop files, we'll add a launcher for each of them.
                const createLaunchers = urls.every(item => tasks.backend.isApplication(item));

                if (createLaunchers) {
                    urls.forEach(item => addLauncher(item));
                    return;
                }

                if (!hoveredItem) {
                    return;
                }

                // Otherwise we'll just start a new instance of the application with the URLs as argument,
                // as you probably don't expect some of your files to open in the app and others to spawn launchers.
                if (!(hoveredItem as Task).appUpdating)
                    tasksModel.requestOpenUrls((hoveredItem as Task).modelIndex(), urls);
            }
        }

        ToolTipDelegate {
            id: openWindowToolTipDelegate
            visible: false
        }

        ToolTipDelegate {
            id: pinnedAppToolTipDelegate
            visible: false
        }

        DockTaskViewport {
            id: dockViewport
            anchors.fill: parent
            vertical: tasks.vertical
            fixedIconSize: Plasmoid.configuration.iconSizeFixed
            scrollToFocused: Plasmoid.configuration.scrollToFocused
            taskList: taskList
            listLength: taskList.contentLength
            scrollStep: taskList.cellSize
            dragScrolling: mouseHandler.containsDrag
            onScrolled: iconGeometryTimer.restart()

        TriangleMouseFilter {
            id: tmf
            filterTimeOut: 300
            active: tasks.toolTipAreaItem && tasks.toolTipAreaItem.toolTipOpen
            blockFirstEnter: false

            edge: {
                switch (Plasmoid.location) {
                case PlasmaCore.Types.BottomEdge:
                    return Qt.TopEdge;
                case PlasmaCore.Types.TopEdge:
                    return Qt.BottomEdge;
                case PlasmaCore.Types.LeftEdge:
                    return Qt.RightEdge;
                case PlasmaCore.Types.RightEdge:
                    return Qt.LeftEdge;
                default:
                    return Qt.TopEdge;
                }
            }

            LayoutMirroring.enabled: tasks.shouldBeMirrored(Plasmoid.configuration.reverseMode, Application.layoutDirection, tasks.vertical)
            x: tasks.vertical ? 0 : taskList.leadingOffset
            y: tasks.vertical ? taskList.leadingOffset : 0

            height: taskList.height
            width: taskList.width

            TaskList {
                id: taskList

                LayoutMirroring.enabled: tasks.shouldBeMirrored(Plasmoid.configuration.reverseMode, Application.layoutDirection, tasks.vertical)
                anchors {
                    left: parent.left
                    top: parent.top
                }

                count: tasks.visibleTaskCount

                availableLength: dockViewport.viewportLength
                availableThickness: tasks.vertical ? dockViewport.width : dockViewport.height
                sharedIconSize: tasks.effectiveIconSize
                centerIcons: false

                onAnimatingChanged: {
                    if (!animating) {
                        tasks.publishIconGeometries(children, tasks);
                    }
                }

                Repeater {
                    id: taskRepeater

                    delegate: Task {
                        tasksRoot: tasks
                        visible: !!((Plasmoid.configuration.showRunning && !model.IsLauncher)
                            || (Plasmoid.configuration.showFavorites && (model.IsLauncher || model.HasLauncher)))
                    }
                }
            }
        }
        }
    }

    readonly property Component groupDialogComponent: Qt.createComponent("GroupDialog.qml")
    property GroupDialog groupDialog

    readonly property bool supportsLaunchers: true

    function hasLauncher(url: url): bool {
        return tasksModel.launcherPosition(url) !== -1;
    }

    function addLauncher(url: url): void {
        if (Plasmoid.immutability !== PlasmaCore.Types.SystemImmutable) {
            tasksModel.requestAddLauncher(url);
        }
    }

    function removeLauncher(url: url): void {
        if (Plasmoid.immutability !== PlasmaCore.Types.SystemImmutable) {
            tasksModel.requestRemoveLauncher(url);
        }
    }

    // This is called by plasmashell in response to a Meta+number shortcut.
    // TODO: Change type to int
    function activateTaskAtIndex(index: var): void {
        if (typeof index !== "number" || !globalShortcuts.nativeActivationEnabled(index)) {
            return;
        }

        activateShortcut(index);
    }

    function performDockAction(task, modifiers, middle, anchor = null, tapCount = 1): void {
        if (task.appUpdating) return;
        const shifted = !!(modifiers & Qt.ShiftModifier);
        const cfg = Plasmoid.configuration;
        const action = middle ? (shifted ? cfg.shiftMiddleClickAction : cfg.middleClickDockAction)
            : (shifted ? cfg.shiftClickAction : cfg.clickAction);
        const state = {
            running: task.model.IsWindow || task.model.IsGroupParent,
            count: task.model.IsGroupParent ? task.model.ChildCount : 1,
            active: task.model.IsActive, urgent: task.model.IsDemandingAttention,
            modified: shifted, middle, control: !!(modifiers & Qt.ControlModifier),
            spreadAvailable: effectWatcher.registered,
            overviewVisible: dockController.overviewVisible,
        };
        const command = DockActions.resolve(action, state);
        const index = task.modelIndex();
        if (command === "launch") tasksModel.requestNewInstance(index);
        else if (command === "activate") {
            if (task.model.IsGroupParent) {
                const children = [];
                for (let i = 0; i < tasksModel.rowCount(index); ++i)
                    children.push(tasksModel.makeModelIndex(task.index, i));
                const urgentChildren = children.filter(child => tasksModel.data(child, TaskManager.AbstractTasksModel.IsDemandingAttention));
                const preferredChildren = urgentChildren.length ? urgentChildren : children;
                const latest = TaskManagerApplet.TaskTools.groupTopTask(preferredChildren,
                    TaskManager.AbstractTasksModel.LastActivated, tasks);
                const top = latest === undefined ? TaskManagerApplet.TaskTools.groupTopTask(preferredChildren,
                    TaskManager.AbstractTasksModel.StackingOrder, tasks) : latest;
                if (action === "minimize" && !shifted && !middle) {
                    const desktop = virtualDesktopInfo.currentDesktopByScreenGeometry(tasksModel.screenGeometry);
                    for (const child of children) {
                        const desktops = tasksModel.data(child, TaskManager.AbstractTasksModel.VirtualDesktops);
                        if (tasksModel.data(child, TaskManager.AbstractTasksModel.IsOnAllVirtualDesktops)
                            || Array.from(desktops || []).includes(desktop)) tasksModel.requestActivate(child);
                    }
                }
                if (top !== undefined) tasksModel.requestActivate(top);
            } else tasksModel.requestActivate(index);
        }
        else if (command === "close") tasksModel.requestClose(index);
        else if (command === "minimize") {
            const currentDesktop = virtualDesktopInfo.currentDesktopByScreenGeometry(tasksModel.screenGeometry);
            const candidates = [];
            TaskManagerApplet.TaskTools.foreachChildTask(child => {
                const desktops = tasksModel.data(child, TaskManager.AbstractTasksModel.VirtualDesktops);
                const onCurrent = tasksModel.data(child, TaskManager.AbstractTasksModel.IsOnAllVirtualDesktops)
                    || Array.from(desktops || []).includes(currentDesktop);
                if (onCurrent && !tasksModel.data(child, TaskManager.AbstractTasksModel.IsMinimized))
                    candidates.push(child);
            }, index, tasksModel);
            candidates.sort((a, b) => Number(tasksModel.data(b, TaskManager.AbstractTasksModel.IsActive))
                - Number(tasksModel.data(a, TaskManager.AbstractTasksModel.IsActive)));
            const minimizeAll = action === "minimize" && ((!shifted && !middle) || tapCount > 1);
            for (const child of minimizeAll ? candidates : candidates.slice(0, 1))
                tasksModel.requestToggleMinimized(child);
        } else if (command === "cycle") {
            TaskManagerApplet.TaskTools.activateNextPrevTask(task, true, false, 2, tasks);
        } else if (command === "spread") activateWindowView(task.model.WinIdList);
        else if (command === "overview") {
            if (!tasks.showOverview()) showTaskPreviews(task, index, anchor);
        } else if (command !== "none") showTaskPreviews(task, index, anchor);
        finishDockAction(command);
    }

    function showTaskPreviews(task, index, anchor): void {
        if (!Plasmoid.configuration.showToolTips || (anchor && !task.visible))
            openContextWindowPreviews(task, index, anchor);
        else {
            toolTipOpenedByClick = task;
            task.updateMainItemBindings();
            task.showToolTip();
        }
    }

    function showOverview(): bool {
        if (!dockController.overviewAvailable) return false;
        dockController.toggleOverview();
        return true;
    }

    function scrollDock(anchor, forward): void {
        if (Plasmoid.configuration.dockScrollAction === 1) {
            TaskManagerApplet.TaskTools.activateNextPrevTask(anchor, forward, false, 2, tasks);
        } else if (Plasmoid.configuration.dockScrollAction === 2) {
            const target = DockActions.nextDesktop(virtualDesktopInfo.desktopIds,
                virtualDesktopInfo.currentDesktopByScreenGeometry(tasksModel.screenGeometry), forward ? 1 : -1);
            if (target !== null) backend.activateDesktop(target);
        }
    }

    readonly property Component contextPreviewComponent: Qt.createComponent("ContextPreviewDialog.qml")
    property ContextPreviewDialog contextPreviewDialog

    function scheduleContextWindowPreviews(task, modelIndex, anchor): void {
        // Keep the callback in the applet's context: a closing QMenu destroys
        // its own QML context before a callback created there can run.
        Qt.callLater(openContextWindowPreviews, task, modelIndex, anchor);
    }

    function openContextWindowPreviews(task, modelIndex, anchor = null): void {
        if (!task || task.appUpdating) return;
        if (contextPreviewComponent.status !== Component.Ready) {
            console.warn("Cannot open window previews:", contextPreviewComponent.errorString());
            return;
        }
        if (contextPreviewDialog) contextPreviewDialog.visible = false;
        contextPreviewDialog = contextPreviewComponent.createObject(tasks, {
            parentTask: task, modelIndex: modelIndex, tasksRoot: tasks, visualParent: anchor || task
        });
        if (contextPreviewDialog) contextPreviewDialog.visible = true;
    }

    readonly property Component launcherContextComponent: Qt.createComponent("LauncherContextMenu.qml")
    property QtObject launcherContextMenu
    function openLauncherContextMenu(anchor, launcherUrl, args = {}): void {
        if (launcherContextMenu) launcherContextMenu.close();
        launcherContextMenu = launcherContextComponent.createObject(tasks, Object.assign({}, args, {
            visualParent: anchor, launcherUrl: launcherUrl, backend: backend, mpris2Source: mpris2Source
        }));
        if (launcherContextMenu) Qt.callLater(() => { if (launcherContextMenu) launcherContextMenu.show(); });
    }

    function createContextMenu(rootTask, modelIndex, args = {}) {
        const initialArgs = Object.assign(args, {
            visualParent: args.visualParent || rootTask,
            rootTask: rootTask,
            modelIndex,
            mpris2Source,
            backend,
        });
        return contextMenuComponent.createObject(rootTask, initialArgs);
    }

    function shouldBeMirrored(reverseMode, layoutDirection, vertical): bool {
        // LayoutMirroring is only horizontal
        if (vertical) {
            return layoutDirection === Qt.RightToLeft;
        }

        if (layoutDirection === Qt.LeftToRight) {
            return reverseMode;
        }
        return !reverseMode;
    }

    Component.onCompleted: {
        TaskManagerApplet.TaskTools.taskManagerInstanceCount += 1;
        requestLayout.connect(iconGeometryTimer.restart);
    }

    Component.onDestruction: {
        TaskManagerApplet.TaskTools.taskManagerInstanceCount -= 1;
    }
}
