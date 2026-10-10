// SPDX-FileCopyrightText: 2026 Goshos Dock contributors
// SPDX-License-Identifier: GPL-2.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.components as PlasmaComponents3
import "code/DockGeometry.js" as DockGeometry

// A separate surface lets the icon grow beyond the panel's clipped window.
// Neither the work area nor the panel's pointer region grows with this window.
PlasmaCore.Dialog {
    id: overlay

    required property Item anchorItem
    required property Item iconItem
    required property var iconSource
    property real magnification: 1
    property real maximumMagnification: 1.6
    property string edge: "bottom"
    property bool highlighted: false
    property bool startup: false
    property bool bounceEnabled: false
    property real imageRotation: 0
    property bool backlit: false
    property color backlightColor: Kirigami.Theme.highlightColor
    property bool activeTask: false
    property bool glossy: false
    property real iconOpacity: 1
    property bool countVisible: false
    property int count: 0
    property bool progressVisible: false
    property real progress: 0
    property bool audioVisible: false
    property bool muted: false
    property real bounce: 0
    readonly property real baseSize: Math.min(iconItem.width, iconItem.height)
    readonly property real bounceAmplitude: bounceEnabled ? baseSize * 0.3 : 0
    readonly property int effectPadding: Math.ceil(baseSize * Math.max(1, maximumMagnification) * 0.1)
    readonly property int contentSize: Math.ceil(baseSize * Math.max(1, maximumMagnification) + bounceAmplitude)
    readonly property int canvasSize: contentSize + 2 * effectPadding
    property bool positioning: false
    property bool ready: false

    visualParent: anchorItem
    type: PlasmaCore.Dialog.Tooltip
    backgroundHints: PlasmaCore.Dialog.NoBackground
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    outputOnly: true
    hideOnWindowDeactivate: false
    visible: ready && anchorItem.visible && anchorItem.Window.visibility !== Window.Hidden && baseSize > 0

    // Dialog normally positions its contents next to a panel. This overlay
    // instead overlaps the original icon and grows away from the screen edge.
    function synchronizePosition(): void {
        if (!ready || positioning || !visible) {
            return;
        }
        positioning = true;
        const center = iconItem.mapToGlobal(iconItem.width / 2, iconItem.height / 2);
        const position = DockGeometry.overlayPosition(center.x - baseSize / 2, center.y - baseSize / 2, baseSize, contentSize, edge);
        x = Math.round(position.x - effectPadding);
        y = Math.round(position.y - effectPadding);
        positioning = false;
    }

    onXChanged: if (!positioning) Qt.callLater(synchronizePosition)
    onYChanged: if (!positioning) Qt.callLater(synchronizePosition)
    onWidthChanged: Qt.callLater(synchronizePosition)
    onHeightChanged: Qt.callLater(synchronizePosition)
    onVisibleChanged: Qt.callLater(synchronizePosition)
    onEdgeChanged: Qt.callLater(synchronizePosition)
    Component.onCompleted: {
        ready = true;
        synchronizePosition();
    }

    mainItem: Item {
        width: overlay.canvasSize
        height: overlay.canvasSize
        Accessible.ignored: true

        Connections {
            target: overlay.anchorItem
            function onXChanged(): void { overlay.synchronizePosition(); }
            function onYChanged(): void { overlay.synchronizePosition(); }
        }

        // Parent transforms (drag sorting and floating-panel animation) may move
        // the icon without changing its local x/y. Track them only while displayed.
        FrameAnimation {
            running: overlay.visible
            onTriggered: overlay.synchronizePosition()
        }

        SequentialAnimation {
            running: overlay.startup && overlay.bounceEnabled && overlay.visible
            loops: Animation.Infinite
            NumberAnimation { target: overlay; property: "bounce"; from: 0; to: overlay.bounceAmplitude; duration: 260; easing.type: Easing.OutQuad }
            NumberAnimation { target: overlay; property: "bounce"; from: overlay.bounceAmplitude; to: 0; duration: 340; easing.type: Easing.InQuad }
            PauseAnimation { duration: 180 }
            onStopped: overlay.bounce = 0
        }

        Item {
            id: imageContainer
            readonly property var position: DockGeometry.iconPosition(width, overlay.contentSize, overlay.edge, overlay.bounce)
            width: overlay.baseSize * overlay.magnification
            height: width
            x: position.x + overlay.effectPadding
            y: position.y + overlay.effectPadding
            opacity: overlay.iconOpacity
            rotation: overlay.imageRotation

            Rectangle {
                anchors.fill: parent
                radius: Kirigami.Units.cornerRadius
                visible: overlay.backlit
                opacity: overlay.activeTask ? 0.75 : 0.4
                border.width: 1
                border.color: Qt.lighter(overlay.backlightColor, 1.5)
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.lighter(overlay.backlightColor, 1.3) }
                    GradientStop { position: 1; color: Qt.darker(overlay.backlightColor, 1.5) }
                }
            }

            Kirigami.Icon {
                anchors.fill: parent
                source: overlay.iconSource
                active: overlay.highlighted
                animated: false
            }

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: parent.height / 2
                radius: Kirigami.Units.cornerRadius
                visible: overlay.glossy
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.45) }
                    GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0) }
                }
            }

            PlasmaComponents3.BusyIndicator {
                anchors.centerIn: parent
                width: parent.width
                height: width
                visible: overlay.startup && !overlay.bounceEnabled
                running: visible
            }

            Kirigami.Badge {
                anchors.top: parent.top
                anchors.right: parent.right
                visible: overlay.countVisible
                text: overlay.count > 9999 ? "9k+" : overlay.count > 999 ? `${Math.floor(overlay.count / 1000)}k` : overlay.count.toLocaleString(Qt.locale(), "f", 0)
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Math.max(2, parent.width / 12)
                height: Math.max(3, parent.width / 12)
                radius: height / 2
                visible: overlay.progressVisible
                color: Kirigami.Theme.backgroundColor
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, overlay.progress / 100))
                    height: parent.height
                    radius: parent.radius
                    color: Kirigami.Theme.highlightColor
                }
            }

            Kirigami.Icon {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: Math.max(16, parent.width / 3)
                height: width
                visible: overlay.audioVisible
                source: overlay.muted ? "audio-volume-muted-symbolic" : "audio-volume-high-symbolic"
            }
        }
    }
}
