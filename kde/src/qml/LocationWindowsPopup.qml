/* SPDX-License-Identifier: GPL-2.0-or-later */
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.kwindowsystem

PlasmaCore.Dialog {
    id: root
    required property var backend
    property url locationUrl
    property string locationName: ""
    property real previewSizeScale: 0
    property int edge: PlasmaCore.Types.BottomEdge
    readonly property var windows: {
        const snapshot = backend.matches;
        return backend.windowsForUrl(locationUrl);
    }

    type: PlasmaCore.Dialog.PopupMenu
    flags: Qt.WindowStaysOnTopHint
    location: edge
    hideOnWindowDeactivate: true
    visible: false
    onWindowsChanged: if (!windows.length) visible = false

    function present(entry, anchor): void {
        locationUrl = entry.url;
        locationName = entry.name;
        visualParent = anchor;
        visible = windows.length > 0;
        Qt.callLater(() => previews.forceActiveFocus());
    }

    function activateWindow(window): void {
        backend.activateWindow(locationUrl, window.windowIds[0]);
        visible = false;
    }

    function thumbnailWidth(window): real {
        const geometry = window.geometry;
        return previewSizeScale > 0 && geometry && geometry.width > 0
            ? Math.max(1, Math.min(Screen.width * 0.8, geometry.width * previewSizeScale))
            : Kirigami.Units.gridUnit * 14 - Kirigami.Units.smallSpacing;
    }

    function thumbnailHeight(window): real {
        const geometry = window.geometry;
        return previewSizeScale > 0 && geometry && geometry.height > 0
            ? Math.max(1, Math.min(Screen.height * 0.7, geometry.height * previewSizeScale))
            : Kirigami.Units.gridUnit * 9;
    }

    mainItem: ColumnLayout {
        id: content
        width: Math.min(Screen.width * 0.8, Math.max(Kirigami.Units.gridUnit * 6,
            root.windows.reduce((sum, window) => sum + root.thumbnailWidth(window) + Kirigami.Units.smallSpacing, 0)))
        height: Math.min(Screen.height * 0.8, Math.max(Kirigami.Units.gridUnit * 3,
            root.windows.reduce((maximum, window) => Math.max(maximum, root.thumbnailHeight(window)), 0)) + Kirigami.Units.gridUnit * 3)
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            Layout.fillWidth: true
            text: root.locationName
            font.bold: true
            elide: Text.ElideRight
            Accessible.role: Accessible.Heading
        }
        ListView {
            id: previews
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.windows
            orientation: ListView.Horizontal
            spacing: Kirigami.Units.smallSpacing
            clip: true
            keyNavigationEnabled: true
            focus: true
            QQC2.ScrollBar.horizontal: QQC2.ScrollBar {}
            Keys.onEscapePressed: root.visible = false
            Keys.onReturnPressed: if (currentIndex >= 0) root.activateWindow(root.windows[currentIndex])
            Keys.onEnterPressed: if (currentIndex >= 0) root.activateWindow(root.windows[currentIndex])

            delegate: QQC2.AbstractButton {
                id: windowButton
                required property var modelData
                required property int index
                width: root.thumbnailWidth(modelData)
                height: previews.height
                readonly property bool highlighted: modelData.active || ListView.isCurrentItem
                hoverEnabled: true
                Accessible.name: modelData.title || i18n("File Manager")
                onClicked: root.activateWindow(modelData)
                onHoveredChanged: if (hovered) previews.currentIndex = index
                background: Rectangle {
                    color: Kirigami.Theme.highlightColor
                    opacity: windowButton.hovered || windowButton.highlighted ? 0.2 : 0
                    radius: Kirigami.Units.cornerRadius
                }
                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing
                    RowLayout {
                        Layout.fillWidth: true
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: windowButton.modelData.title || i18n("File Manager")
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                        QQC2.ToolButton {
                            icon.name: "window-close"
                            Accessible.name: i18n("Close Window")
                            onClicked: root.backend.closeWindow(root.locationUrl, windowButton.modelData.windowIds[0])
                        }
                    }
                    Item {
                        id: thumbnailSourceItem
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        readonly property var winId: windowButton.modelData.windowIds[0]
                        Kirigami.Icon {
                            anchors.centerIn: parent
                            width: Kirigami.Units.iconSizes.huge
                            height: width
                            source: "system-file-manager"
                        }
                        Loader {
                            anchors.fill: parent
                            active: root.visible && !KWindowSystem.isPlatformWayland && !windowButton.modelData.minimized
                            sourceComponent: PlasmaCore.WindowThumbnail { winId: thumbnailSourceItem.winId }
                        }
                        Loader {
                            anchors.fill: parent
                            active: root.visible && KWindowSystem.isPlatformWayland
                            asynchronous: true
                            source: "PipeWireThumbnail.qml"
                        }
                    }
                }
            }
        }
    }
}
