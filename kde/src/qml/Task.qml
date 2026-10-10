/*
    SPDX-FileCopyrightText: 2012-2013 Eike Hein <hein@kde.org>
    SPDX-FileCopyrightText: 2024 Nate Graham <nate@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.plasma.core as PlasmaCore
import org.kde.ksvg as KSvg
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami
import plasma.applet.org.gosh.goshosdock as TaskManagerApplet
import org.kde.plasma.plasmoid

import org.kde.taskmanager as TaskManager
import "code/DockGeometry.js" as DockGeometry

PlasmaCore.ToolTipArea {
    id: task

    activeFocusOnTab: true

    // To achieve a bottom-to-top layout on vertical panels, the task manager
    // is rotated by 180 degrees(see main.qml). This makes the tasks rotated,
    // so un-rotate them here to fix that.
    rotation: Plasmoid.configuration.reverseMode && Plasmoid.formFactor === PlasmaCore.Types.Vertical ? 180 : 0

    implicitHeight: inPopup
                    ? TaskManagerApplet.LayoutMetrics.preferredHeightInPopup()
                    : (tasksRoot.vertical
                        ? TaskManagerApplet.LayoutMetrics.preferredMinHeight()
                        : Math.max(tasksRoot.height / Plasmoid.configuration.maxStripes,
                             TaskManagerApplet.LayoutMetrics.preferredMinHeight()))
    implicitWidth: tasksRoot.vertical
        ? Math.max(TaskManagerApplet.LayoutMetrics.preferredMinWidth(), Math.min(TaskManagerApplet.LayoutMetrics.preferredMaxWidth(), tasksRoot.width / Plasmoid.configuration.maxStripes))
        : 0

    Layout.fillWidth: true
    Layout.fillHeight: !inPopup
    Layout.minimumWidth: dockLayout ? parent.cellWidth : 0
    Layout.minimumHeight: dockLayout ? parent.cellHeight : 0
    Layout.maximumWidth: dockLayout ? parent.cellWidth : tasksRoot.vertical
        ? -1
        : ((model.IsLauncher && !tasksRoot.iconsOnly) ? tasksRoot.height / taskList.rows : TaskManagerApplet.LayoutMetrics.preferredMaxWidth())
    Layout.maximumHeight: dockLayout ? parent.cellHeight : tasksRoot.vertical ? TaskManagerApplet.LayoutMetrics.preferredMaxHeight() : -1

    required property var model
    required property int index
    required property /*main.qml*/ Item tasksRoot

    readonly property int pid: model.AppPid
    readonly property string appName: model.AppName
    readonly property string appId: model.AppId.replace(/\.desktop/, '')
    readonly property bool isIcon: tasksRoot.iconsOnly || model.IsLauncher
    property bool toolTipOpen: false
    property bool inPopup: false
    readonly property bool dockLayout: !inPopup && (parent?.dockLayout ?? false)
    readonly property real dockIconSize: dockLayout ? parent.effectiveIconSize : Plasmoid.configuration.iconSize
    readonly property bool dockItemInView: {
        const view = tasksRoot.taskViewport;
        if (!dockLayout || !view) return true;
        // Track scroll and delegate reordering as well as panel resizes.
        const scroll = Qt.point(view.contentX, view.contentY);
        const geometry = Qt.rect(x, y, width, height);
        const center = task.mapToItem(view, geometry.width / 2, geometry.height / 2);
        return center.x + geometry.width / 2 > 0 && center.y + geometry.height / 2 > 0
            && center.x - geometry.width / 2 < view.width && center.y - geometry.height / 2 < view.height;
    }
    property bool isWindow: model.IsWindow
    property int childCount: model.ChildCount
    property int previousChildCount: 0
    property alias labelText: label.text
    property QtObject contextMenu: null
    readonly property bool smartLauncherEnabled: !inPopup
    property QtObject smartLauncherItem: null

    property Item audioStreamIcon: null
    property var audioStreams: []
    property bool delayAudioStreamIndicator: false
    property bool completed: false
    readonly property bool audioIndicatorsEnabled: Plasmoid.configuration.indicateAudioStreams
    readonly property bool tooltipControlsEnabled: Plasmoid.configuration.tooltipControls
    readonly property bool hasAudioStream: audioStreams.length > 0
    readonly property bool playingAudio: hasAudioStream && audioStreams.some(item => !item.corked)
    readonly property bool muted: hasAudioStream && audioStreams.every(item => item.muted)

    readonly property string dockEdge: Plasmoid.location === PlasmaCore.Types.LeftEdge ? "left"
        : Plasmoid.location === PlasmaCore.Types.RightEdge ? "right"
        : Plasmoid.location === PlasmaCore.Types.TopEdge ? "top" : "bottom"
    readonly property bool appUpdating: smartLauncherItem?.updating ?? false
    readonly property real dockIconOpacity: appUpdating ? 0.45 : !inPopup && Plasmoid.configuration.dimMinimized && model.IsMinimized
        ? Math.max(0.1, Plasmoid.configuration.minimizedOpacity) : 1
    readonly property bool iconOverlayVisible: !!magnifiedIconLoader.item?.visible
    readonly property color dockDominantColor: iconPaletteLoader.item?.dominant ?? Kirigami.Theme.highlightColor
    readonly property bool dockRunning: model.IsWindow || model.IsGroupParent
    readonly property bool dockUrgent: model.IsDemandingAttention || (Plasmoid.configuration.showEmblems && (smartLauncherItem?.urgent ?? false))
    onDockUrgentChanged: if (dockUrgent && !inPopup) tasksRoot.revealForUrgency()
    readonly property bool useIconIndicatorColor: Plasmoid.configuration.dominantIndicatorColor || Plasmoid.configuration.unityBacklit
    property real urgentRotation: 0
    property real dockMagnification: {
        // Reading the delegate geometry also refreshes this binding on sorting.
        const geometry = Qt.rect(x, y, width, height);
        const center = icon.mapToItem(tasksRoot, icon.width / 2, icon.height / 2);
        return DockGeometry.magnificationScale(
            tasksRoot.vertical ? tasksRoot.dockPointerY : tasksRoot.dockPointerX,
            tasksRoot.vertical ? center.y : center.x,
            tasksRoot.vertical ? geometry.height : geometry.width,
            Plasmoid.configuration.magnificationFactor,
            Plasmoid.configuration.magnificationSpread,
            !inPopup && dockItemInView && Plasmoid.configuration.magnification && tasksRoot.dockHovered
                && !tasksRoot.dragSource && !tasksRoot.groupDialog && !toolTipOpen
                && contextMenu?.status !== PlasmaExtras.Menu.Open);
    }
    Behavior on dockMagnification {
        NumberAnimation { duration: Kirigami.Units.shortDuration; easing.type: Easing.OutQuad }
    }

    Loader {
        id: iconPaletteLoader
        active: !task.inPopup && task.useIconIndicatorColor
        sourceComponent: Kirigami.ImageColors {
            source: task.model.decoration
            fallbackDominant: Kirigami.Theme.highlightColor
        }
    }

    SequentialAnimation {
        running: !task.inPopup && Plasmoid.configuration.danceUrgent && task.dockUrgent
        loops: Animation.Infinite
        NumberAnimation { target: task; property: "urgentRotation"; from: 0; to: -8; duration: 90 }
        NumberAnimation { target: task; property: "urgentRotation"; from: -8; to: 8; duration: 180 }
        NumberAnimation { target: task; property: "urgentRotation"; from: 8; to: -8; duration: 180 }
        NumberAnimation { target: task; property: "urgentRotation"; from: -8; to: 0; duration: 90 }
        PauseAnimation { duration: 1500 }
        onStopped: task.urgentRotation = 0
    }

    readonly property bool highlighted: (inPopup && activeFocus) || (!inPopup && containsMouse)
        || (task.contextMenu && task.contextMenu.status === PlasmaExtras.Menu.Open)
        || (!!tasksRoot.groupDialog && tasksRoot.groupDialog.visualParent === task)

    active: (!Plasmoid.configuration.hideTooltip || tasksRoot.toolTipOpenedByClick === task || task.toolTipOpen)
        && !inPopup && !tasksRoot.groupDialog && task.contextMenu?.status !== PlasmaExtras.Menu.Open
    interactive: model.IsWindow || mainItem.playerData
    location: Plasmoid.location
    mainItem: !Plasmoid.configuration.showToolTips || !model.IsWindow ? pinnedAppToolTipDelegate : openWindowToolTipDelegate

    onXChanged: {
        if (!completed) {
            return;
        }
        if (oldX < 0) {
            oldX = x;
            return;
        }
        moveAnim.x = oldX - x + translateTransform.x;
        moveAnim.y = translateTransform.y;
        oldX = x;
        moveAnim.restart();
    }
    onYChanged: {
        if (!completed) {
            return;
        }
        if (oldY < 0) {
            oldY = y;
            return;
        }
        moveAnim.y = oldY - y + translateTransform.y;
        moveAnim.x = translateTransform.x;
        oldY = y;
        moveAnim.restart();
    }

    property real oldX: -1
    property real oldY: -1
    SequentialAnimation {
        id: moveAnim
        property real x
        property real y
        onRunningChanged: {
            if (running) {
                ++task.parent.animationsRunning;
            } else {
                --task.parent.animationsRunning;
            }
        }
        ParallelAnimation {
            NumberAnimation {
                target: translateTransform
                properties: "x"
                from: moveAnim.x
                to: 0
                easing.type: Easing.OutQuad
                duration: Kirigami.Units.longDuration
            }
            NumberAnimation {
                target: translateTransform
                properties: "y"
                from: moveAnim.y
                to: 0
                easing.type: Easing.OutQuad
                duration: Kirigami.Units.longDuration
            }
        }
    }
    transform: Translate {
        id: translateTransform
    }

    Accessible.name: model.display
    Accessible.description: {
        if (appUpdating) return i18n("%1 is being updated", model.display);
        if (!model.display) {
            return "";
        }

        if (model.IsLauncher) {
            return i18nc("@info:usagetip %1 application name", "Launch %1", model.display)
        }

        let smartLauncherDescription = "";
        if (iconBox.active) {
            smartLauncherDescription += i18ncp("@info:tooltip", "There is %1 new message.", "There are %1 new messages.", task.smartLauncherItem.count);
        }

        if (model.IsGroupParent) {
            switch (Plasmoid.configuration.groupedTaskVisualization) {
            case 0:
                break; // Use the default description
            case 1: {
                return `${i18nc("@info:usagetip %1 task name", "Show Task tooltip for %1", model.display)}; ${smartLauncherDescription}`;
            }
            case 2: {
                if (effectWatcher.registered) {
                    return `${i18nc("@info:usagetip %1 task name", "Show windows side by side for %1", model.display)}; ${smartLauncherDescription}`;
                }
                // fallthrough
            }
            default:
                return `${i18nc("@info:usagetip %1 task name", "Open textual list of windows for %1", model.display)}; ${smartLauncherDescription}`;
            }
        }

        return `${i18nc("@info:usagetip %1 task name", "Activate %1", model.display)}; ${smartLauncherDescription}`;
    }
    Accessible.role: Accessible.Button
    Accessible.onPressAction: leftTapHandler.leftClick()

    onToolTipVisibleChanged: toolTipVisible => {
        task.toolTipOpen = toolTipVisible;
        if (!toolTipVisible) {
            tasksRoot.toolTipOpenedByClick = null;
        } else {
            tasksRoot.toolTipAreaItem = task;
        }
    }

    onContainsMouseChanged: {
        if (containsMouse) {
            task.forceActiveFocus(Qt.MouseFocusReason);
            task.updateMainItemBindings();
        } else {
            tasksRoot.toolTipOpenedByClick = null;
        }
    }

    onHighlightedChanged: {
        // ensure it doesn't get stuck with a window highlighted
        tasksRoot.cancelHighlightWindows();
    }

    onPidChanged: updateAudioStreams({delay: false})
    onAppNameChanged: updateAudioStreams({delay: false})

    onIsWindowChanged: {
        if (model.IsWindow) {
            taskInitComponent.createObject(task);
            updateAudioStreams({delay: false});
        }
    }

    onChildCountChanged: {
        if (TaskManagerApplet.TaskTools.taskManagerInstanceCount < 2 && childCount > previousChildCount) {
            tasksModel.requestPublishDelegateGeometry(modelIndex(), backend.globalRect(task), task);
        }

        previousChildCount = childCount;
    }

    onIndexChanged: {
        hideToolTip();

        if (!inPopup && !tasksRoot.vertical
                && !Plasmoid.configuration.separateLaunchers) {
            tasksRoot.requestLayout();
        }
    }

    onSmartLauncherEnabledChanged: {
        if (smartLauncherEnabled && !smartLauncherItem) {
            const component = Qt.createComponent("plasma.applet.org.gosh.goshosdock", "SmartLauncherItem");
            const smartLauncher = component.createObject(task);
            component.destroy();

            smartLauncher.launcherUrl = Qt.binding(() => model.LauncherUrlWithoutIcon);
            smartLauncher.notificationsEnabled = Qt.binding(() => Plasmoid.configuration.showNotificationCounter);
            smartLauncher.applicationCounterOverridesNotifications = Qt.binding(() => Plasmoid.configuration.applicationCounterOverridesNotifications);

            smartLauncherItem = smartLauncher;
        }
    }

    onHasAudioStreamChanged: {
        const audioStreamIconActive = hasAudioStream && audioIndicatorsEnabled;
        if (!audioStreamIconActive) {
            if (audioStreamIcon !== null) {
                audioStreamIcon.destroy();
                audioStreamIcon = null;
            }
            return;
        }
        // Create item on demand instead of using Loader to reduce memory consumption,
        // because only a few applications have audio streams.
        const component = Qt.createComponent("AudioStream.qml");
        audioStreamIcon = component.createObject(task);
        component.destroy();
    }
    onAudioIndicatorsEnabledChanged: task.hasAudioStreamChanged()

    Kirigami.Badge {
        anchors.left: parent.left
        anchors.top: parent.top
        z: 100
        visible: !task.inPopup && task.tasksRoot.shortcutOverlayVisible && task.tasksRoot.shortcutNumber(task) >= 0
        text: task.tasksRoot.shortcutNumber(task).toString()
    }

    Keys.onMenuPressed: event => contextMenuTimer.start()
    Keys.onReturnPressed: event => tasksRoot.performDockAction(task, event.modifiers, false)
    Keys.onEnterPressed: event => Keys.returnPressed(event);
    Keys.onSpacePressed: event => Keys.returnPressed(event);
    Keys.onUpPressed: event => Keys.leftPressed(event)
    Keys.onDownPressed: event => Keys.rightPressed(event)
    Keys.onLeftPressed: event => {
        if (!inPopup && (event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)) {
            tasksModel.move(task.index, task.index - 1);
        } else {
            event.accepted = false;
        }
    }
    Keys.onRightPressed: event => {
        if (!inPopup && (event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)) {
            tasksModel.move(task.index, task.index + 1);
        } else {
            event.accepted = false;
        }
    }

    function modelIndex(): /*QModelIndex*/ var {
        return inPopup
            ? tasksModel.makePersistentModelIndex(groupDialog.visualParent.index, index)
            : tasksModel.makePersistentModelIndex(index);
    }

    function showContextMenu(args: var): void {
        task.hideImmediately();
        if (Plasmoid.configuration.defaultPreviewsOpen && Plasmoid.configuration.showToolTips && (model.IsWindow || model.IsGroupParent)
            && !args?.skipDefaultPreviews && !appUpdating) {
            tasksRoot.openContextWindowPreviews(task, modelIndex(), args?.visualParent || null);
            return;
        }
        const options = Object.assign({}, args || {});
        delete options.skipDefaultPreviews;
        contextMenu = tasksRoot.createContextMenu(task, modelIndex(), options) as ContextMenu;
        contextMenu.show();
    }

    function updateAudioStreams(args: var): void {
        if (args) {
            // When the task just appeared (e.g. virtual desktop switch), show the audio indicator
            // right away. Only when audio streams change during the lifetime of this task, delay
            // showing that to avoid distraction.
            delayAudioStreamIndicator = !!args.delay;
        }

        var pa = pulseAudio.item;
        if (!pa || !task.isWindow) {
            task.audioStreams = [];
            return;
        }

        // Check appid first for app using portal
        // https://docs.pipewire.org/page_portal.html
        var streams = pa.streamsForAppId(task.appId);
        if (!streams.length) {
            streams = pa.streamsForPid(model.AppPid);
            if (streams.length) {
                pa.registerPidMatch(model.AppName);
            } else {
                // We only want to fall back to appName matching if we never managed to map
                // a PID to an audio stream window. Otherwise if you have two instances of
                // an application, one playing and the other not, it will look up appName
                // for the non-playing instance and erroneously show an indicator on both.
                if (!pa.hasPidMatch(model.AppName)) {
                    streams = pa.streamsForAppName(model.AppName);
                }
            }
        }

        task.audioStreams = streams;
    }

    function toggleMuted(): void {
        if (muted) {
            task.audioStreams.forEach(item => item.unmute());
        } else {
            task.audioStreams.forEach(item => item.mute());
        }
    }

    // Will also be called in activateTaskAtIndex(index)
    function updateMainItemBindings(): void {
        if ((mainItem.parentTask === this && mainItem.rootIndex.row === index)
            || (tasksRoot.toolTipOpenedByClick === null && !active)
            || (tasksRoot.toolTipOpenedByClick !== null && tasksRoot.toolTipOpenedByClick !== this)) {
            return;
        }

        mainItem.blockingUpdates = (mainItem.isGroup !== model.IsGroupParent); // BUG 464597 Force unload the previous component

        mainItem.parentTask = this;
        // Hover/context previews can outlive a model reorder or removal.
        mainItem.rootIndex = task.modelIndex();

        mainItem.appName = Qt.binding(() => model.AppName);
        mainItem.pidParent = Qt.binding(() => model.AppPid);
        mainItem.windows = Qt.binding(() => model.WinIdList);
        mainItem.isGroup = Qt.binding(() => model.IsGroupParent);
        mainItem.icon = Qt.binding(() => model.decoration);
        mainItem.launcherUrl = Qt.binding(() => model.LauncherUrlWithoutIcon);
        mainItem.isLauncher = Qt.binding(() => model.IsLauncher);
        mainItem.isMinimized = Qt.binding(() => model.IsMinimized);
        mainItem.display = Qt.binding(() => model.display);
        mainItem.genericName = Qt.binding(() => model.GenericName);
        mainItem.virtualDesktops = Qt.binding(() => model.VirtualDesktops);
        mainItem.isOnAllVirtualDesktops = Qt.binding(() => model.IsOnAllVirtualDesktops);
        mainItem.activities = Qt.binding(() => model.Activities);
        mainItem.isReadyForPainting = Qt.binding(() => model.Geometry?.width > 0 && model.Geometry?.height > 0);

        mainItem.smartLauncherCountVisible = Qt.binding(() => smartLauncherItem?.countVisible ?? false);
        mainItem.smartLauncherCount = Qt.binding(() => mainItem.smartLauncherCountVisible ? (smartLauncherItem?.count ?? 0) : 0);

        mainItem.blockingUpdates = false;
        tasksRoot.toolTipAreaItem = this;
    }

    Connections {
        target: pulseAudio.item
        ignoreUnknownSignals: true // Plasma-PA might not be available
        function onStreamsChanged(): void {
            task.updateAudioStreams({delay: true})
        }
    }

    TapHandler {
        id: menuTapHandler
        acceptedButtons: Qt.LeftButton
        acceptedDevices: PointerDevice.TouchScreen | PointerDevice.Stylus
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onLongPressed: {
            // When we're a launcher, there's no window controls, so we can show all
            // places without the menu getting super huge.
            if (task.model.IsLauncher) {
                task.showContextMenu({showAllPlaces: true})
            } else {
                task.showContextMenu();
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        gesturePolicy: TapHandler.WithinBounds // Release grab when menu appears
        onPressedChanged: if (pressed) contextMenuTimer.start()
    }

    Timer {
        id: contextMenuTimer
        interval: 0
        onTriggered: menuTapHandler.longPressed()
    }

    TapHandler {
        id: leftTapHandler
        acceptedButtons: Qt.LeftButton
        onTapped: (eventPoint, button) => leftClick()

        function leftClick(): void {
            if (task.active) {
                task.hideToolTip();
            }
            tasksRoot.performDockAction(task, point.modifiers, false, null, leftTapHandler.tapCount);
        }
    }

    TapHandler {
        id: auxiliaryTapHandler
        acceptedButtons: Qt.MiddleButton | Qt.BackButton | Qt.ForwardButton
        onTapped: (eventPoint, button) => {
            if (button === Qt.MiddleButton) {
                tasksRoot.performDockAction(task, eventPoint.modifiers, true, null, auxiliaryTapHandler.tapCount);
            } else if (button === Qt.BackButton || button === Qt.ForwardButton) {
                const playerData = mpris2Source.playerForLauncherUrl(task.model.LauncherUrlWithoutIcon, task.model.AppPid);
                if (playerData) {
                    if (button === Qt.BackButton) {
                        playerData.Previous();
                    } else {
                        playerData.Next();
                    }
                } else {
                    eventPoint.accepted = false;
                }
            }

            task.tasksRoot.cancelHighlightWindows();
        }
    }

    KSvg.FrameSvgItem {
        id: frame

        anchors {
            fill: parent

            topMargin: (!task.tasksRoot.vertical && taskList.rows > 1) ? TaskManagerApplet.LayoutMetrics.iconMargin : 0
            bottomMargin: (!task.tasksRoot.vertical && taskList.rows > 1) ? TaskManagerApplet.LayoutMetrics.iconMargin : 0
            leftMargin: ((task.inPopup || task.tasksRoot.vertical) && taskList.columns > 1) ? TaskManagerApplet.LayoutMetrics.iconMargin : 0
            rightMargin: ((task.inPopup || task.tasksRoot.vertical) && taskList.columns > 1) ? TaskManagerApplet.LayoutMetrics.iconMargin : 0
        }

        imagePath: "widgets/tasks"
        // Keep the frame item and its drag handler enabled for custom styles.
        opacity: task.inPopup || Plasmoid.configuration.indicatorStyle === 0 ? 1 : 0
        property bool isHovered: task.highlighted && Plasmoid.configuration.taskHoverEffect
        property string basePrefix: "normal"
        prefix: isHovered ? TaskManagerApplet.TaskTools.taskPrefixHovered(basePrefix, Plasmoid.location) : TaskManagerApplet.TaskTools.taskPrefix(basePrefix, Plasmoid.location)

        // Avoid repositioning delegate item after dragFinished
        DragHandler {
            id: dragHandler
            grabPermissions: PointerHandler.CanTakeOverFromHandlersOfDifferentType

            function setRequestedInhibitDnd(value: bool): void {
                // This is modifying the value in the panel containment that
                // inhibits accepting drag and drop, so that we don't accidentally
                // drop the task on this panel.
                let item = this;
                while (item.parent) {
                    item = item.parent;
                    if (item.appletRequestsInhibitDnD !== undefined) {
                        item.appletRequestsInhibitDnD = value
                    }
                }
            }

            onActiveChanged: {
                if (active) {
                    icon.grabToImage(result => {
                        if (!dragHandler.active) {
                            // BUG 466675 grabToImage is async, so avoid updating dragSource when active is false
                            return;
                        }
                        setRequestedInhibitDnd(true);
                        tasksRoot.dragSource = task;
                        dragHelper.Drag.imageSource = result.url;
                        const mimeData = {
                            "text/x-orgkdeplasmataskmanager_taskurl": backend.tryDecodeApplicationsUrl(model.LauncherUrlWithoutIcon).toString(),
                            "application/x-orgkdeplasmataskmanager_taskbuttonitem": model.MimeData ?? "",
                        };
                        // Launcher-only rows have no native window MIME data.
                        if (model.MimeType && model.MimeData !== undefined && model.MimeData !== null)
                            mimeData[model.MimeType] = model.MimeData;
                        dragHelper.Drag.mimeData = mimeData;
                        dragHelper.Drag.active = dragHandler.active;
                    });
                } else {
                    setRequestedInhibitDnd(false);
                    dragHelper.Drag.active = false;
                    dragHelper.Drag.imageSource = "";
                }
            }
        }
    }

    Loader {
        id: taskProgressOverlayLoader

        anchors.fill: frame
        asynchronous: true
        active: Plasmoid.configuration.showEmblems && task.smartLauncherItem && task.smartLauncherItem.progressVisible
        visible: !task.iconOverlayVisible

        source: "TaskProgressOverlay.qml"
    }

    Loader {
        id: iconBox

        opacity: task.iconOverlayVisible ? 0 : task.dockIconOpacity
        rotation: task.urgentRotation

        anchors {
            left: parent.left
            leftMargin: adjustMargin(true, parent.width, taskFrame.margins.left)
            top: parent.top
            topMargin: task.inPopup ? adjustMargin(false, parent.height, taskFrame.margins.top)
                : Math.max(adjustMargin(false, parent.height, taskFrame.margins.top), (parent.height - iconBox.height) / 2)
        }

        width: task.inPopup ? Math.max(Kirigami.Units.iconSizes.sizeForLabels, Kirigami.Units.iconSizes.medium)
            : Math.min(task.parent?.minimumWidth ?? 0, task.height, task.dockIconSize)
        height: task.inPopup ? width : Math.min(task.dockIconSize,
            parent.height - adjustMargin(false, parent.height, taskFrame.margins.top)
                - adjustMargin(false, parent.height, taskFrame.margins.bottom))

        asynchronous: true
        active: Plasmoid.configuration.showEmblems && height >= Kirigami.Units.iconSizes.small
                && task.smartLauncherItem && task.smartLauncherItem.countVisible
        source: "TaskBadgeOverlay.qml"

        function adjustMargin(isVertical: bool, size: real, margin: real): real {
            if (!size) {
                return margin;
            }

            var margins = isVertical ? TaskManagerApplet.LayoutMetrics.horizontalMargins() : TaskManagerApplet.LayoutMetrics.verticalMargins();

            if ((size - margins) < Kirigami.Units.iconSizes.small) {
                return Math.ceil((margin * (Kirigami.Units.iconSizes.small / size)) / 2);
            }

            return margin;
        }

        Rectangle {
            anchors.fill: parent
            radius: Kirigami.Units.cornerRadius
            visible: !task.inPopup && Plasmoid.configuration.unityBacklit && task.dockRunning
            opacity: task.model.IsActive ? 0.75 : 0.4
            border.width: 1
            border.color: Qt.lighter(task.dockDominantColor, 1.5)
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.lighter(task.dockDominantColor, 1.3) }
                GradientStop { position: 1; color: Qt.darker(task.dockDominantColor, 1.5) }
            }
        }

        Kirigami.Icon {
            id: icon

            anchors.fill: parent

            active: task.highlighted
            enabled: true

            source: task.model.decoration
        }

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: parent.height / 2
            radius: Kirigami.Units.cornerRadius
            visible: !task.inPopup && Plasmoid.configuration.unityBacklit && Plasmoid.configuration.glossyIcons
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.45) }
                GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0) }
            }
        }

        states: [
            // Using a state transition avoids a binding loop between label.visible and
            // the text label margin, which derives from the icon width.
            State {
                name: "standalone"
                when: !label.visible && task.parent

                AnchorChanges {
                    target: iconBox
                    anchors.left: undefined
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                PropertyChanges {
                    iconBox.anchors.leftMargin: 0
                    iconBox.width: Math.min(task.dockIconSize, Math.min(task.parent.minimumWidth, tasksRoot.height)
                        - iconBox.adjustMargin(true, task.width, taskFrame.margins.left)
                        - iconBox.adjustMargin(true, task.width, taskFrame.margins.right))
                }
            }
        ]

        Loader {
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height)
            height: width
            active: task.model.IsStartup
            sourceComponent: busyIndicator
        }
    }

    Loader {
        id: magnifiedIconLoader
        active: !task.inPopup && task.visible && task.dockItemInView && !task.tasksRoot.dragSource
            && (task.dockMagnification > 1.001 || (Plasmoid.configuration.launchBounce && task.model.IsStartup))
        sourceComponent: MagnifiedIcon {
            anchorItem: iconBox
            iconItem: icon
            iconSource: task.model.decoration
            magnification: task.dockMagnification
            maximumMagnification: Plasmoid.configuration.magnification
                ? Math.max(1, Plasmoid.configuration.magnificationFactor) : 1
            edge: task.dockEdge
            highlighted: task.highlighted
            startup: task.model.IsStartup
            bounceEnabled: Plasmoid.configuration.launchBounce
            imageRotation: task.urgentRotation
            backlit: Plasmoid.configuration.unityBacklit && task.dockRunning
            backlightColor: task.dockDominantColor
            activeTask: task.model.IsActive
            glossy: Plasmoid.configuration.unityBacklit && Plasmoid.configuration.glossyIcons
            iconOpacity: task.dockIconOpacity
            countVisible: Plasmoid.configuration.showEmblems && (task.smartLauncherItem?.countVisible ?? false)
            count: task.smartLauncherItem?.count ?? 0
            progressVisible: Plasmoid.configuration.showEmblems && (task.smartLauncherItem?.progressVisible ?? false)
            progress: task.smartLauncherItem?.progress ?? 0
            audioVisible: task.audioIndicatorsEnabled && task.hasAudioStream
            muted: task.muted
        }
    }

    Rectangle {
        anchors.fill: frame
        anchors.margins: Kirigami.Units.smallSpacing / 2
        radius: Plasmoid.configuration.visualStyle === 3 ? 0 : Kirigami.Units.cornerRadius
        visible: !task.inPopup && Plasmoid.configuration.indicatorStyle > 0
            && (task.highlighted || task.model.IsActive || task.model.IsDemandingAttention)
        color: task.model.IsDemandingAttention ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.highlightColor
        opacity: task.model.IsActive ? 0.18 : 0.1
        z: -1
    }

    DockIndicator {
        readonly property bool atSide: task.dockEdge === "left" || task.dockEdge === "right"
        width: atSide ? Kirigami.Units.smallSpacing : icon.width
        height: atSide ? icon.height : Kirigami.Units.smallSpacing
        x: task.dockEdge === "left" ? 1 : task.dockEdge === "right" ? task.width - width - 1 : (task.width - width) / 2
        y: task.dockEdge === "top" ? 1 : task.dockEdge === "bottom" ? task.height - height - 1 : (task.height - height) / 2
        style: task.inPopup ? 0 : Plasmoid.configuration.indicatorStyle
        windowCount: task.dockRunning ? Math.max(1, task.childCount) : 0
        active: task.model.IsActive
        edge: task.dockEdge
        activeColor: Plasmoid.configuration.customizeIndicators ? Plasmoid.configuration.indicatorColor
            : task.useIconIndicatorColor ? task.dockDominantColor
            : task.dockUrgent ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.highlightColor
        inactiveColor: Plasmoid.configuration.customizeIndicators ? Plasmoid.configuration.indicatorColor
            : task.useIconIndicatorColor ? task.dockDominantColor : Kirigami.Theme.textColor
        borderColor: Plasmoid.configuration.indicatorBorderColor
        borderWidth: Plasmoid.configuration.customizeIndicators ? Plasmoid.configuration.indicatorBorderWidth : 0
    }

    PlasmaComponents3.Label {
        id: label

        visible: (task.inPopup || !task.tasksRoot.iconsOnly && !task.model.IsLauncher
            && (parent.width - iconBox.height - Kirigami.Units.smallSpacing) >= TaskManagerApplet.LayoutMetrics.spaceRequiredToShowText())

        anchors {
            fill: parent
            leftMargin: taskFrame.margins.left + iconBox.width + TaskManagerApplet.LayoutMetrics.labelMargin
            topMargin: taskFrame.margins.top
            rightMargin: taskFrame.margins.right + (task.audioStreamIcon !== null && task.audioStreamIcon.visible ? (task.audioStreamIcon.width + TaskManagerApplet.LayoutMetrics.labelMargin) : 0)
            bottomMargin: taskFrame.margins.bottom
        }

        wrapMode: (maximumLineCount === 1) ? Text.NoWrap : Text.Wrap
        elide: Text.ElideRight
        textFormat: Text.PlainText
        verticalAlignment: Text.AlignVCenter
        maximumLineCount: Plasmoid.configuration.maxTextLines || undefined

        // The accessible item of this element is only used for debugging
        // purposes, and it will never gain focus (thus it won't interfere
        // with screenreaders).
        Accessible.ignored: !visible
        Accessible.name: parent.Accessible.name + "-labelhint"

        // use State to avoid unnecessary re-evaluation when the label is invisible
        states: State {
            name: "labelVisible"
            when: label.visible

            PropertyChanges {
                label.text: task.model.display
            }
        }
    }

    states: [
        State {
            name: "launcher"
            when: task.model.IsLauncher

            PropertyChanges {
                frame.basePrefix: ""
            }
        },
        State {
            name: "attention"
            when: task.model.IsDemandingAttention || (Plasmoid.configuration.showEmblems && task.smartLauncherItem && task.smartLauncherItem.urgent)

            PropertyChanges {
                frame.basePrefix: "attention"
            }
        },
        State {
            name: "minimized"
            when: task.model.IsMinimized

            PropertyChanges {
                frame.basePrefix: "minimized"
            }
        },
        State {
            name: "active"
            when: task.model.IsActive

            PropertyChanges {
                frame.basePrefix: "focus"
            }
        }
    ]

    Component.onCompleted: {
        if (!inPopup && model.IsWindow) {
            const component = Qt.createComponent("GroupExpanderOverlay.qml");
            component.createObject(task);
            component.destroy();
            updateAudioStreams({delay: false});
        }

        if (!inPopup && !model.IsWindow) {
            taskInitComponent.createObject(task);
        }
        completed = true;
    }
    Component.onDestruction: {
        if (moveAnim.running) {
            (task.parent as TaskList).animationsRunning -= 1;
        }
    }
}
