/* SPDX-License-Identifier: GPL-2.0-or-later */
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import plasma.applet.org.gosh.goshosdock as Dock
import "code/StackLogic.js" as StackLogic
import "code/DockGeometry.js" as DockGeometry
import "code/DockActions.js" as DockActions

Item {
    id: root

    required property var configuration
    property real iconSize: configuration.iconSize
    property bool vertical: false
    property int edge: PlasmaCore.Types.BottomEdge
    property list<string> excludedApplications: []
    property bool leadingApplicationsOnly: false
    property bool omitApplicationsButton: false
    property var sharedBackend: null
    property Item dockRoot: root
    property var locationTasksModel: null
    property bool windowViewAvailable: false
    property real dockPointerX: 0
    property real dockPointerY: 0
    property bool dockHovered: false
    readonly property string dockEdge: edge === PlasmaCore.Types.LeftEdge ? "left" : edge === PlasmaCore.Types.RightEdge ? "right" : edge === PlasmaCore.Types.TopEdge ? "top" : "bottom"
    readonly property int buttonExtent: Math.max(16, iconSize) + Kirigami.Units.smallSpacing * 2
    readonly property var entries: buildEntries()
    readonly property int itemCount: entries.filter(entry => entry.kind !== "divider").length
    readonly property int maximumExtent: Math.max(buttonExtent, (vertical ? Screen.height : Screen.width) * 0.4)
    readonly property int dividerExtent: Kirigami.Units.largeSpacing
    readonly property int dividerLength: entries.filter(entry => entry.kind === "divider").length * dividerExtent
    readonly property int contentExtent: entries.reduce((sum, entry) => sum + (entry.kind === "divider" ? dividerExtent : buttonExtent), 0)
    readonly property bool popupVisible: stackPopup.visible || stackContext.opened || windowsMenu.opened || windowPreviews.visible || trashConfirmation.visible || errorDialog.visible
    readonly property var stackBackend: sharedBackend || backendLoader.item
    readonly property alias locationBackend: locationTracker
    property var contextStack: null
    readonly property bool contextEntryBusy: !!entries.find(entry => entry.key === contextStack?.key)?.busy
    property Item contextAnchor: null
    property string operationError: ""

    signal requestApplicationLauncher
    signal requestWindowView(var windowIds)
    signal requestOverview

    implicitWidth: vertical ? (entries.length > 0 ? buttonExtent : 0) : Math.min(contentExtent, maximumExtent)
    implicitHeight: vertical ? Math.min(contentExtent, maximumExtent) : (entries.length > 0 ? buttonExtent : 0)

    Loader {
        id: backendLoader
        active: !root.leadingApplicationsOnly && !root.sharedBackend
        sourceComponent: Dock.StackBackend {}
    }

    Dock.LocationBackend {
        id: locationTracker
        enabled: !root.leadingApplicationsOnly && root.configuration.isolateLocations
        tasksModel: root.locationTasksModel
        locations: root.entries.filter(entry => entry.url && ["folder", "trash", "device"].includes(entry.kind)).map(entry => String(entry.url))
    }

    function locationWindows(entry): var {
        const currentMatches = locationTracker.matches;
        return entry && entry.url ? locationTracker.windowsForUrl(entry.url) : [];
    }

    function openLocation(entry): void {
        if (!entry || !entry.url) return;
        if (!locationTracker.activate(entry.url)) root.stackBackend.openUrl(entry.url);
    }

    function locationAction(entry, anchor, modifiers, middle, tapCount): bool {
        const windows = locationWindows(entry);
        if (!windows.length) return false;
        const shifted = !!(modifiers & Qt.ShiftModifier);
        const action = middle ? (shifted ? configuration.shiftMiddleClickAction : configuration.middleClickDockAction)
            : (shifted ? configuration.shiftClickAction : configuration.clickAction);
        const command = DockActions.resolve(action, {
            running: true, count: windows.length, active: windows.some(window => window.active), urgent: windows.some(window => window.urgent),
            control: !!(modifiers & Qt.ControlModifier), modified: modifiers !== 0, middle: middle, spreadAvailable: windowViewAvailable,
            overviewVisible: !!root.dockRoot.overviewVisible
        });
        if (command === "launch") root.stackBackend.openNewWindow(entry.url);
        else if (command === "close") locationTracker.close(entry.url);
        else if (command === "minimize") locationTracker.minimize(entry.url, action === "minimize" && ((!middle && modifiers === 0) || tapCount > 1));
        else if (command === "cycle") locationTracker.cycle(entry.url, 1);
        else if (command === "spread") requestWindowView(windows.flatMap(window => window.windowIds));
        else if (command === "overview" && root.dockRoot.overviewAvailable) requestOverview();
        else if (command === "previews" || command === "overview") {
            contextStack = entry;
            contextAnchor = anchor;
            windowPreviews.present(entry, anchor);
        } else if (command !== "none") locationTracker.activate(entry.url);
        if (typeof root.dockRoot.finishDockAction === "function") root.dockRoot.finishDockAction(command);
        return true;
    }

    Connections {
        target: root.stackBackend
        function onErrorOccurred(message: string): void {
            if (root.leadingApplicationsOnly) {
                return;
            }
            root.operationError = message;
            errorDialog.visualParent = root;
            errorDialog.visible = true;
        }
    }

    Dock.SmartLauncherItem { id: applicationLaunchState }

    function launchApplication(id): void {
        const value = String(id);
        applicationLaunchState.launcherUrl = value.includes(":") ? value : "applications:" + value;
        if (applicationLaunchState.updating) {
            root.operationError = i18n("This application is being updated. Try again when the update finishes.");
            errorDialog.visualParent = root;
            errorDialog.visible = true;
            return;
        }
        if (root.stackBackend) root.stackBackend.launchApplication(value);
    }

    function folderEntry(key, name, icon, url, custom): var {
        return {
            kind: "folder",
            key: key,
            name: name,
            icon: icon,
            url: String(url),
            custom: custom
        };
    }

    function buildEntries(): var {
        const result = [];
        if (configuration.showApplications && !omitApplicationsButton) {
            result.push({
                kind: "applications",
                key: "applications-button",
                name: i18n("Applications"),
                icon: "view-grid",
                launcher: true
            });
        }
        if (leadingApplicationsOnly || !stackBackend) {
            return result;
        }
        if (configuration.showRecentApps) {
            const recent = StackLogic.recentApplications(root.stackBackend.recentApplications, excludedApplications, configuration.recentAppsLimit);
            for (const app of recent) {
                result.push({
                    kind: "recent",
                    key: "recent:" + (app.desktopId || app.url),
                    name: app.name,
                    icon: app.icon,
                    url: app.url,
                    desktopId: app.desktopId
                });
            }
        }

        const stacks = [];
        if (configuration.applicationsStack) {
            stacks.push({
                kind: "applications",
                key: "applications-stack",
                name: i18n("Applications"),
                icon: "applications-all"
            });
        }
        const locations = root.stackBackend.standardLocations;
        if (configuration.documentsStack && locations.documents) {
            stacks.push(folderEntry("documents", i18n("Documents"), "folder-documents", locations.documents, false));
        }
        if (configuration.downloadsStack && locations.downloads) {
            stacks.push(folderEntry("downloads", i18n("Downloads"), "folder-download", locations.downloads, false));
        }
        if (configuration.homeStack && locations.home) {
            stacks.push(folderEntry("home", i18n("Home"), "user-home", locations.home, false));
        }
        const seenFolders = new Set();
        for (const url of configuration.customFolders || []) {
            const path = String(url);
            if (path && !seenFolders.has(path)) {
                seenFolders.add(path);
                stacks.push(folderEntry(path, StackLogic.nameForUrl(path), "folder", path, true));
            }
        }
        if (configuration.showTrash) {
            stacks.push({
                kind: "trash",
                key: "trash",
                name: i18n("Trash"),
                icon: root.stackBackend.trashEmpty ? "user-trash" : "user-trash-full",
                url: "trash:/"
            });
        }
        if (configuration.showDevices || configuration.showNetworkDevices) {
            for (const place of root.stackBackend.places) {
                if (!place.isDevice && !place.isNetwork) {
                    continue;
                }
                if (place.isNetwork ? !configuration.showNetworkDevices : !configuration.showDevices) {
                    continue;
                }
                if (configuration.devicesOnlyMounted && place.setupNeeded) {
                    continue;
                }
                stacks.push({
                    kind: "device",
                    key: "device:" + place.id,
                    name: place.name,
                    icon: place.icon,
                    url: place.url,
                    placeIndex: place.index,
                    placeId: place.id,
                    setupNeeded: place.setupNeeded,
                    canTeardown: place.canTeardown,
                    canEject: place.canEject,
                    busy: place.busy
                });
            }
        }
        if (stacks.length > 0 && configuration.stacksDivider) {
            result.push({
                kind: "divider",
                key: "divider"
            });
        }
        return result.concat(stacks);
    }

    function activate(entry, anchor, modifiers = 0, middle = false, tapCount = 1): void {
        if (entry.busy) return;
        if (["folder", "trash", "device"].includes(entry.kind) && locationAction(entry, anchor, modifiers, middle, tapCount)) return;
        if (entry.kind === "applications" && entry.launcher && configuration.showAppsAction === 0) {
            requestApplicationLauncher();
        } else if (entry.kind === "applications" || entry.kind === "folder") {
            stackPopup.openStack(entry, anchor);
        } else if (entry.kind === "recent") {
            if (typeof dockRoot.activateRecentApplication !== "function"
                || !dockRoot.activateRecentApplication(entry.desktopId || entry.url, anchor, modifiers, middle, false, tapCount)) {
                root.launchApplication(entry.desktopId || entry.url);
            }
        } else if (entry.kind === "device") {
            root.stackBackend.setupPlace(entry.placeIndex, entry.placeId);
        } else if (entry.url) {
            root.stackBackend.openUrl(entry.url);
        }
    }

    function shortcutTargets(): var {
        const candidates = [];
        for (let index = 0; index < buttonRepeater.count; ++index) candidates.push(buttonRepeater.itemAt(index));
        return StackLogic.shortcutTargets([], candidates);
    }

    function revealEntry(item): void {
        if (root.vertical) {
            extrasScroll.contentY = DockGeometry.revealOffset(item.y, item.height, extrasScroll.contentY, extrasScroll.height, extrasScroll.contentHeight, 0);
        } else {
            extrasScroll.contentX = DockGeometry.revealOffset(item.x, item.width, extrasScroll.contentX, extrasScroll.width, extrasScroll.contentWidth, 0);
        }
    }

    function showContext(entry, anchor): void {
        if (entry.kind === "recent" && typeof root.dockRoot.openLauncherContextMenu === "function") {
            const task = root.dockRoot.findApplicationTask(entry.desktopId || entry.url);
            if (task) task.showContextMenu({visualParent: anchor});
            else root.dockRoot.openLauncherContextMenu(anchor, entry.url);
            return;
        }
        contextStack = entry;
        contextAnchor = anchor;
        stackContext.popup(anchor, Qt.point(0, anchor.height));
    }

    function setStackPreference(field, value): void {
        if (contextStack) {
            configuration.stackOverrides = StackLogic.setPreference(configuration.stackOverrides, contextStack.key, field, value);
        }
    }

    function removeCustomStack(): void {
        if (!contextStack || !contextStack.custom) {
            return;
        }
        const key = contextStack.key;
        configuration.customFolders = Array.from(configuration.customFolders).filter(url => String(url) !== contextStack.url);
        configuration.stackOverrides = StackLogic.withoutStack(configuration.stackOverrides, key);
        if (stackPopup.stack !== null && stackPopup.stack.key === key) {
            stackPopup.visible = false;
        }
    }

    Flickable {
        id: extrasScroll
        anchors.fill: parent
        contentWidth: root.vertical ? width : root.contentExtent
        contentHeight: root.vertical ? root.contentExtent : height
        flickableDirection: root.vertical ? Flickable.VerticalFlick : Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        interactive: contentWidth > width || contentHeight > height
        QQC2.ScrollBar.horizontal: QQC2.ScrollBar {
            policy: root.vertical ? QQC2.ScrollBar.AlwaysOff : QQC2.ScrollBar.AsNeeded
        }
        QQC2.ScrollBar.vertical: QQC2.ScrollBar {
            policy: root.vertical ? QQC2.ScrollBar.AsNeeded : QQC2.ScrollBar.AlwaysOff
        }

        Flow {
            id: buttons
            width: extrasScroll.contentWidth
            height: extrasScroll.contentHeight
            flow: root.vertical ? Flow.TopToBottom : Flow.LeftToRight

            Repeater {
                id: buttonRepeater
                model: root.entries

                delegate: Item {
                    id: entryItem
                    required property var modelData
                    required property int index
                    readonly property var entry: modelData
                    readonly property var applicationTask: modelData.kind === "recent" && typeof root.dockRoot.findApplicationTask === "function"
                        ? root.dockRoot.findApplicationTask(modelData.desktopId || modelData.url) : null
                    readonly property QtObject smartLauncher: applicationTask?.smartLauncherItem || recentLauncherLoader.item
                    readonly property bool appUpdating: smartLauncher?.updating ?? false
                    readonly property bool runningApplication: !!applicationTask?.dockRunning || locationWindows.length > 0
                    readonly property bool activeApplication: !!applicationTask?.model.IsActive || locationWindows.some(window => window.active)
                    readonly property bool startup: !!applicationTask?.model.IsStartup
                    readonly property bool urgent: !!applicationTask?.dockUrgent
                        || (root.configuration.showEmblems && (smartLauncher?.urgent ?? false)) || locationWindows.some(window => window.urgent)
                    readonly property bool countVisible: root.configuration.showEmblems && (smartLauncher?.countVisible ?? false)
                    readonly property bool progressVisible: root.configuration.showEmblems && (smartLauncher?.progressVisible ?? false)
                    readonly property bool useIconIndicatorColor: root.configuration.dominantIndicatorColor || root.configuration.unityBacklit
                    readonly property color dominantColor: applicationTask?.dockDominantColor || iconPaletteLoader.item?.dominant || Kirigami.Theme.highlightColor
                    readonly property bool allMinimized: applicationTask ? !!applicationTask.model.IsMinimized
                        : locationWindows.length > 0 && locationWindows.every(window => window.minimized)
                    readonly property real iconOpacity: appUpdating ? 0.45 : root.configuration.dimMinimized && runningApplication && allMinimized
                        ? Math.max(0.1, root.configuration.minimizedOpacity) : 1
                    readonly property bool audioVisible: root.configuration.indicateAudioStreams && !!applicationTask?.hasAudioStream
                    property real urgentRotation: 0
                    readonly property bool divider: modelData.kind === "divider"
                    readonly property var locationWindows: root.locationWindows(modelData)
                    readonly property bool withinViewport: root.vertical ? y + height > extrasScroll.contentY && y < extrasScroll.contentY + extrasScroll.height : x + width > extrasScroll.contentX && x < extrasScroll.contentX + extrasScroll.width
                    property real magnification: {
                        const geometry = Qt.rect(x, y, width, height);
                        // Keep mapping responsive while the extras strip is scrolled.
                        const offset = Qt.point(extrasScroll.contentX, extrasScroll.contentY);
                        const center = buttonIcon.mapToItem(root.dockRoot, buttonIcon.width / 2, buttonIcon.height / 2);
                        return DockGeometry.magnificationScale(root.vertical ? root.dockPointerY : root.dockPointerX, root.vertical ? center.y : center.x, root.vertical ? geometry.height : geometry.width, root.configuration.magnificationFactor, root.configuration.magnificationSpread, !divider && withinViewport && root.configuration.magnification && root.dockHovered && !root.popupVisible);
                    }
                    Behavior on magnification {
                        NumberAnimation {
                            duration: Kirigami.Units.shortDuration
                            easing.type: Easing.OutQuad
                        }
                    }
                    width: root.vertical ? root.buttonExtent : (divider ? root.dividerExtent : root.buttonExtent)
                    height: root.vertical ? (divider ? root.dividerExtent : root.buttonExtent) : root.buttonExtent

                    onUrgentChanged: {
                        if (urgent && !applicationTask && typeof root.dockRoot.revealForUrgency === "function") root.dockRoot.revealForUrgency();
                    }
                    Loader {
                        id: recentLauncherLoader
                        active: entryItem.modelData.kind === "recent" && !entryItem.applicationTask?.smartLauncherItem
                        sourceComponent: Dock.SmartLauncherItem {
                            launcherUrl: {
                                const value = String(entryItem.modelData.desktopId || entryItem.modelData.url);
                                return value.includes(":") ? value : "applications:" + value;
                            }
                            notificationsEnabled: root.configuration.showNotificationCounter
                            applicationCounterOverridesNotifications: root.configuration.applicationCounterOverridesNotifications
                        }
                    }
                    Loader {
                        id: iconPaletteLoader
                        active: !entryItem.divider && entryItem.useIconIndicatorColor && !entryItem.applicationTask
                        sourceComponent: Kirigami.ImageColors {
                            source: buttonIcon
                            fallbackDominant: Kirigami.Theme.highlightColor
                        }
                    }
                    SequentialAnimation {
                        running: root.configuration.danceUrgent && entryItem.urgent
                        loops: Animation.Infinite
                        NumberAnimation { target: entryItem; property: "urgentRotation"; from: 0; to: -8; duration: 90 }
                        NumberAnimation { target: entryItem; property: "urgentRotation"; from: -8; to: 8; duration: 180 }
                        NumberAnimation { target: entryItem; property: "urgentRotation"; from: 8; to: -8; duration: 180 }
                        NumberAnimation { target: entryItem; property: "urgentRotation"; from: -8; to: 0; duration: 90 }
                        PauseAnimation { duration: 1500 }
                        onStopped: entryItem.urgentRotation = 0
                    }

                    function activateShortcut(launch, shifted): void {
                        if (modelData.busy) return;
                        root.revealEntry(entryItem);
                        const modifiers = shifted ? Qt.ShiftModifier : 0;
                        if (launch && modelData.kind === "recent") {
                            if (typeof root.dockRoot.activateRecentApplication !== "function"
                                || !root.dockRoot.activateRecentApplication(modelData.desktopId || modelData.url, button, modifiers, false, true)) {
                                root.launchApplication(modelData.desktopId || modelData.url);
                            }
                        } else if (launch && modelData.url && !modelData.setupNeeded) root.stackBackend.openNewWindow(modelData.url);
                        else root.activate(modelData, button, modifiers);
                    }

                    Kirigami.Badge {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        z: 100
                        visible: !entryItem.divider && !!root.dockRoot.shortcutOverlayVisible
                            && root.dockRoot.shortcutNumber(entryItem) >= 0
                        text: visible ? root.dockRoot.shortcutNumber(entryItem).toString() : ""
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: root.vertical ? parent.width * 0.55 : 1
                        height: root.vertical ? 1 : parent.height * 0.55
                        color: Kirigami.Theme.textColor
                        opacity: 0.22
                        visible: entryItem.divider
                    }

                    QQC2.AbstractButton {
                        id: button
                        anchors.fill: parent
                        visible: !entryItem.divider
                        enabled: !entryItem.modelData.busy && !entryItem.appUpdating
                        text: entryItem.modelData.name || ""
                        hoverEnabled: true
                        activeFocusOnTab: true
                        Accessible.name: text
                        Accessible.description: entryItem.modelData.kind === "folder" ? i18n("Open folder stack; use the context menu to change its view or sorting") : text
                        onClicked: root.activate(entryItem.modelData, button)
                        onActiveFocusChanged: {
                            if (!activeFocus) {
                                return;
                            }
                            root.revealEntry(entryItem);
                        }
                        Keys.onMenuPressed: root.showContext(entryItem.modelData, button)
                        Keys.onPressed: event => {
                            if ((event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) || event.key === Qt.Key_Menu) {
                                root.showContext(entryItem.modelData, button);
                                event.accepted = true;
                            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                                root.activate(entryItem.modelData, button, event.modifiers);
                                event.accepted = true;
                            }
                        }
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                        background: Rectangle {
                            radius: root.configuration.visualStyle === 2 ? 0 : Kirigami.Units.cornerRadius
                            color: button.down ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                            opacity: button.down ? 0.3 : (button.hovered || button.activeFocus ? 0.12 : 0)
                            border.width: button.activeFocus ? 1 : 0
                            border.color: Kirigami.Theme.highlightColor
                            Behavior on opacity {
                                NumberAnimation {
                                    duration: Kirigami.Units.shortDuration
                                }
                            }
                        }
                        contentItem: Item {
                            opacity: magnifiedIconLoader.item?.visible ? 0 : entryItem.iconOpacity
                            rotation: entryItem.urgentRotation
                            implicitWidth: root.iconSize
                            implicitHeight: implicitWidth
                            scale: button.down ? 0.92 : 1
                            Behavior on scale {
                                NumberAnimation {
                                    duration: Kirigami.Units.shortDuration
                                }
                            }
                            Rectangle {
                                anchors.fill: parent
                                radius: Kirigami.Units.cornerRadius
                                visible: root.configuration.unityBacklit && entryItem.runningApplication
                                opacity: entryItem.activeApplication ? 0.75 : 0.4
                                border.width: 1
                                border.color: Qt.lighter(entryItem.dominantColor, 1.5)
                                gradient: Gradient {
                                    GradientStop { position: 0; color: Qt.lighter(entryItem.dominantColor, 1.3) }
                                    GradientStop { position: 1; color: Qt.darker(entryItem.dominantColor, 1.5) }
                                }
                            }
                            Kirigami.Icon {
                                id: buttonIcon
                                anchors.fill: parent
                                source: entryItem.modelData.icon || "folder"
                                fallback: "unknown"
                            }
                            Rectangle {
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.right: parent.right
                                height: parent.height / 2
                                radius: Kirigami.Units.cornerRadius
                                visible: root.configuration.unityBacklit && root.configuration.glossyIcons
                                gradient: Gradient {
                                    GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.45) }
                                    GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0) }
                                }
                            }
                            QQC2.BusyIndicator {
                                anchors.fill: parent
                                visible: entryItem.startup || !!entryItem.modelData.busy
                                running: visible
                            }
                            Kirigami.Badge {
                                anchors.top: parent.top
                                anchors.right: parent.right
                                visible: entryItem.countVisible
                                text: {
                                    const count = entryItem.smartLauncher?.count ?? 0;
                                    return count > 9999 ? "9k+" : count > 999 ? `${Math.floor(count / 1000)}k` : count.toLocaleString(Qt.locale(), "f", 0);
                                }
                            }
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: Math.max(2, parent.width / 12)
                                height: Math.max(3, parent.width / 12)
                                radius: height / 2
                                visible: entryItem.progressVisible
                                color: Kirigami.Theme.backgroundColor
                                Rectangle {
                                    width: parent.width * Math.max(0, Math.min(1, (entryItem.smartLauncher?.progress ?? 0) / 100))
                                    height: parent.height
                                    radius: parent.radius
                                    color: Kirigami.Theme.highlightColor
                                }
                            }
                        }
                        padding: Kirigami.Units.smallSpacing

                        Loader {
                            id: magnifiedIconLoader
                            active: button.visible && entryItem.withinViewport && !root.popupVisible
                                && (entryItem.magnification > 1.001 || (root.configuration.launchBounce && entryItem.startup))
                            sourceComponent: MagnifiedIcon {
                                anchorItem: button
                                iconItem: buttonIcon
                                iconSource: entryItem.modelData.icon || "folder"
                                magnification: entryItem.magnification
                                maximumMagnification: root.configuration.magnification ? Math.max(1, root.configuration.magnificationFactor) : 1
                                edge: root.dockEdge
                                highlighted: button.hovered || button.activeFocus
                                startup: entryItem.startup
                                bounceEnabled: root.configuration.launchBounce
                                imageRotation: entryItem.urgentRotation
                                backlit: root.configuration.unityBacklit && entryItem.runningApplication
                                backlightColor: entryItem.dominantColor
                                activeTask: entryItem.activeApplication
                                glossy: root.configuration.unityBacklit && root.configuration.glossyIcons
                                iconOpacity: entryItem.iconOpacity
                                countVisible: entryItem.countVisible
                                count: entryItem.smartLauncher?.count ?? 0
                                progressVisible: entryItem.progressVisible
                                progress: entryItem.smartLauncher?.progress ?? 0
                                audioVisible: entryItem.audioVisible
                                muted: entryItem.applicationTask?.muted ?? false
                            }
                        }

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: root.showContext(entryItem.modelData, button)
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                            onClicked: mouse => root.activate(entryItem.modelData, button, mouse.modifiers, mouse.button === Qt.MiddleButton)
                            onDoubleClicked: mouse => root.activate(entryItem.modelData, button, mouse.modifiers, mouse.button === Qt.MiddleButton, 2)
                        }
                        WheelHandler {
                            enabled: root.configuration.dockScrollAction === 2
                                || (root.configuration.dockScrollAction === 1 && (entryItem.locationWindows.length > 0 || entryItem.applicationTask))
                            onWheel: event => {
                                if (event.angleDelta.y !== 0) {
                                    if (root.configuration.dockScrollAction === 2 || entryItem.applicationTask) {
                                        root.dockRoot.scrollDock(entryItem.applicationTask, event.angleDelta.y < 0);
                                    } else locationTracker.cycle(entryItem.modelData.url, event.angleDelta.y < 0 ? 1 : -1);
                                    event.accepted = true;
                                }
                            }
                        }
                        QQC2.ToolButton {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            width: Math.max(16, root.iconSize / 3)
                            height: width
                            z: 2
                            visible: entryItem.audioVisible && !magnifiedIconLoader.item?.visible
                            icon.name: entryItem.applicationTask?.muted ? "audio-volume-muted-symbolic" : "audio-volume-high-symbolic"
                            text: entryItem.applicationTask?.muted ? i18n("Unmute") : i18n("Mute")
                            display: QQC2.AbstractButton.IconOnly
                            onClicked: entryItem.applicationTask.toggleMuted()
                            QQC2.ToolTip.text: text
                            QQC2.ToolTip.visible: hovered
                        }
                    }
                    DockIndicator {
                        readonly property bool atSide: root.dockEdge === "left" || root.dockEdge === "right"
                        width: atSide ? Kirigami.Units.smallSpacing : root.iconSize
                        height: atSide ? root.iconSize : Kirigami.Units.smallSpacing
                        x: root.dockEdge === "left" ? 1 : root.dockEdge === "right" ? parent.width - width - 1 : (parent.width - width) / 2
                        y: root.dockEdge === "top" ? 1 : root.dockEdge === "bottom" ? parent.height - height - 1 : (parent.height - height) / 2
                        windowCount: entryItem.applicationTask?.dockRunning
                            ? (entryItem.applicationTask.model.IsGroupParent ? entryItem.applicationTask.model.ChildCount : 1)
                            : entryItem.locationWindows.length
                        active: entryItem.activeApplication
                        style: root.configuration.indicatorStyle || 1
                        edge: root.dockEdge
                        activeColor: root.configuration.customizeIndicators ? root.configuration.indicatorColor
                            : entryItem.useIconIndicatorColor ? entryItem.dominantColor : entryItem.urgent ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.highlightColor
                        inactiveColor: root.configuration.customizeIndicators ? root.configuration.indicatorColor
                            : entryItem.useIconIndicatorColor ? entryItem.dominantColor : Kirigami.Theme.textColor
                        borderColor: root.configuration.indicatorBorderColor
                        borderWidth: root.configuration.customizeIndicators ? root.configuration.indicatorBorderWidth : 0
                    }
                }
            }
        }
    }

    StackPopup {
        onLaunchApplicationRequested: id => root.launchApplication(id)
        id: stackPopup
        objectName: "stackPopup"
        configuration: root.configuration
        backend: root.stackBackend
        locationBackend: locationTracker
        edge: root.edge
        contextMenuOpen: stackContext.opened
        onContextRequested: anchor => root.showContext(stackPopup.stack, anchor)
    }

    QQC2.Menu {
        id: windowsMenu
        popupType: QQC2.Popup.Window
        title: i18n("Open Windows")
        Instantiator {
            model: root.locationWindows(root.contextStack)
            delegate: QQC2.MenuItem {
                required property var modelData
                text: modelData.title || i18n("File Manager")
                icon.name: modelData.minimized ? "window-minimize" : "system-file-manager"
                checkable: true
                checked: modelData.active
                onTriggered: locationTracker.activateWindow(root.contextStack.url, modelData.windowIds[0])
            }
            onObjectAdded: (index, object) => windowsMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => windowsMenu.removeItem(object)
        }
    }

    LocationWindowsPopup {
        id: windowPreviews
        backend: locationTracker
        previewSizeScale: root.configuration.previewSizeScale
        edge: root.edge
    }

    QQC2.Menu {
        id: stackContext
        // Popups need their own window when the plasmoid occupies a narrow panel.
        popupType: QQC2.Popup.Window
        readonly property bool stackSelected: root.contextStack !== null && (root.contextStack.kind === "folder" || root.contextStack.kind === "applications")
        readonly property bool folderSelected: root.contextStack !== null && root.contextStack.kind === "folder"
        readonly property var locationWindows: root.locationWindows(root.contextStack)
        readonly property string selectedKey: root.contextStack !== null ? root.contextStack.key : ""
        readonly property int selectedView: StackLogic.preference(root.configuration.stackOverrides, selectedKey, "view", root.configuration.stackView)
        readonly property int selectedSort: StackLogic.preference(root.configuration.stackOverrides, selectedKey, "sort", root.configuration.stackSort)

        QQC2.MenuItem {
            text: stackContext.folderSelected ? i18n("Open in File Manager") : i18n("Open")
            icon.name: stackContext.folderSelected ? "system-file-manager" : "document-open"
            visible: root.contextStack !== null
            onTriggered: {
                if (stackContext.folderSelected) {
                    root.openLocation(root.contextStack);
                } else {
                    root.activate(root.contextStack, root.contextAnchor || root);
                }
            }
        }
        QQC2.MenuItem {
            text: i18n("Open Folder Stack")
            icon.name: "view-grid"
            visible: stackContext.folderSelected
            onTriggered: stackPopup.openStack(root.contextStack, root.contextAnchor || root)
        }
        QQC2.MenuItem {
            text: i18n("Open Windows")
            icon.name: "window-duplicate"
            visible: stackContext.locationWindows.length > 0
            onTriggered: windowsMenu.popup(root.contextAnchor || root)
        }
        QQC2.MenuItem {
            text: i18n("Minimize Windows")
            icon.name: "window-minimize"
            visible: stackContext.locationWindows.length > 0
            onTriggered: locationTracker.minimize(root.contextStack.url)
        }
        QQC2.MenuItem {
            text: i18n("Close Windows")
            icon.name: "window-close"
            visible: stackContext.locationWindows.length > 0
            onTriggered: locationTracker.close(root.contextStack.url)
        }
        QQC2.MenuItem {
            text: i18n("Open Applications Stack")
            icon.name: "view-grid"
            visible: root.contextStack !== null && !!root.contextStack.launcher
            onTriggered: stackPopup.openStack(root.contextStack, root.contextAnchor || root)
        }
        QQC2.MenuItem {
            text: i18n("Application Launcher")
            icon.name: "start-here-kde"
            visible: root.contextStack !== null && root.contextStack.kind === "applications"
            onTriggered: root.requestApplicationLauncher()
        }
        QQC2.MenuSeparator {
            visible: stackContext.stackSelected
        }
        QQC2.Menu {
            title: i18n("View")
            enabled: stackContext.stackSelected
            QQC2.MenuItem {
                text: i18n("Automatic")
                checkable: true
                checked: stackContext.selectedView === 0
                onTriggered: root.setStackPreference("view", 0)
            }
            QQC2.MenuItem {
                text: i18n("Fan")
                enabled: stackContext.stackSelected
                checkable: true
                checked: stackContext.selectedView === 1
                onTriggered: root.setStackPreference("view", 1)
            }
            QQC2.MenuItem {
                text: i18n("Grid")
                checkable: true
                checked: stackContext.selectedView === 2
                onTriggered: root.setStackPreference("view", 2)
            }
            QQC2.MenuItem {
                text: i18n("List")
                checkable: true
                checked: stackContext.selectedView === 3
                onTriggered: root.setStackPreference("view", 3)
            }
            QQC2.MenuSeparator {}
            QQC2.MenuItem {
                text: i18n("Use Global Default")
                onTriggered: root.setStackPreference("view", -1)
            }
        }
        QQC2.Menu {
            title: i18n("Sort By")
            enabled: stackContext.folderSelected
            QQC2.MenuItem {
                text: i18n("Name")
                checkable: true
                checked: stackContext.selectedSort === 0
                onTriggered: root.setStackPreference("sort", 0)
            }
            QQC2.MenuItem {
                text: i18n("Date Modified")
                checkable: true
                checked: stackContext.selectedSort === 1
                onTriggered: root.setStackPreference("sort", 1)
            }
            QQC2.MenuItem {
                text: i18n("Kind")
                checkable: true
                checked: stackContext.selectedSort === 2
                onTriggered: root.setStackPreference("sort", 2)
            }
            QQC2.MenuSeparator {}
            QQC2.MenuItem {
                text: i18n("Use Global Default")
                onTriggered: root.setStackPreference("sort", -1)
            }
        }
        QQC2.MenuItem {
            text: i18n("Empty Trash…")
            icon.name: "edit-clear"
            visible: root.contextStack !== null && root.contextStack.kind === "trash"
            enabled: root.stackBackend !== null && !root.stackBackend.trashEmpty
            onTriggered: {
                trashConfirmation.visualParent = root.contextAnchor || root;
                trashConfirmation.visible = true;
            }
        }
        QQC2.MenuItem {
            text: i18n("Safely Remove")
            icon.name: "media-eject"
            visible: root.contextStack !== null && root.contextStack.kind === "device" && root.contextStack.canTeardown
            enabled: !root.contextEntryBusy
            onTriggered: root.stackBackend.teardownPlace(root.contextStack.placeIndex, root.contextStack.placeId)
        }
        QQC2.MenuItem {
            text: i18n("Eject")
            icon.name: "media-eject"
            visible: root.contextStack !== null && root.contextStack.kind === "device" && root.contextStack.canEject
            enabled: !root.contextEntryBusy
            onTriggered: root.stackBackend.ejectPlace(root.contextStack.placeIndex, root.contextStack.placeId)
        }
        QQC2.MenuSeparator {
            visible: root.contextStack !== null && !!root.contextStack.custom
        }
        QQC2.MenuItem {
            text: i18n("Remove from Dock")
            icon.name: "list-remove"
            visible: root.contextStack !== null && !!root.contextStack.custom
            onTriggered: root.removeCustomStack()
        }
    }

    PlasmaCore.Dialog {
        id: trashConfirmation
        onVisibleChanged: {
            if (visible) {
                Qt.callLater(() => cancelTrashButton.forceActiveFocus());
            }
        }
        type: PlasmaCore.Dialog.PopupMenu
        location: root.edge
        hideOnWindowDeactivate: true
        visible: false
        mainItem: ColumnLayout {
            width: Math.min(Kirigami.Units.gridUnit * 22, Screen.width - Kirigami.Units.gridUnit * 2)
            spacing: Kirigami.Units.largeSpacing
            Keys.onEscapePressed: trashConfirmation.visible = false
            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Permanently delete all items in the Trash?")
                wrapMode: Text.Wrap
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("This cannot be undone.")
                wrapMode: Text.Wrap
            }
            QQC2.DialogButtonBox {
                Layout.fillWidth: true
                onRejected: trashConfirmation.visible = false
                QQC2.Button {
                    id: cancelTrashButton
                    text: i18n("Cancel")
                    QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole
                    onClicked: trashConfirmation.visible = false
                }
                QQC2.Button {
                    text: i18n("Empty Trash")
                    icon.name: "edit-delete"
                    QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.DestructiveRole
                    onClicked: {
                        trashConfirmation.visible = false;
                        root.stackBackend.emptyTrash();
                    }
                }
            }
        }
    }

    PlasmaCore.Dialog {
        id: errorDialog
        type: PlasmaCore.Dialog.PopupMenu
        location: root.edge
        hideOnWindowDeactivate: true
        visible: false
        mainItem: ColumnLayout {
            width: Math.min(Kirigami.Units.gridUnit * 22, Screen.width - Kirigami.Units.gridUnit * 2)
            Keys.onEscapePressed: errorDialog.visible = false
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                text: root.operationError
                type: Kirigami.MessageType.Error
                visible: true
            }
            QQC2.Button {
                Layout.alignment: Qt.AlignRight
                text: i18n("Close")
                onClicked: errorDialog.visible = false
            }
        }
    }
}
