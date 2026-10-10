// SPDX-FileCopyrightText: 2026 Goshos Dock contributors
// SPDX-License-Identifier: GPL-2.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import "code/DockGeometry.js" as DockGeometry

Item {
    id: root

    property int style: 1
    property int windowCount: 1
    property bool active: false
    property string edge: "bottom"
    property color activeColor: "#3daee9"
    property color inactiveColor: "#eff0f1"
    property color borderColor: "black"
    property real borderWidth: 0
    readonly property bool vertical: edge === "left" || edge === "right"

    visible: windowCount > 0 && style > 0
    Accessible.ignored: true

    Item {
        id: strip
        anchors.centerIn: parent
        width: root.vertical ? root.height : root.width
        height: root.vertical ? root.width : root.height
        rotation: root.edge === "left" ? 90 : root.edge === "right" ? -90 : root.edge === "top" ? 180 : 0

        Repeater {
            model: DockGeometry.indicatorSegments(root.style, root.windowCount, strip.width, strip.height, root.active)
            Rectangle {
                required property var modelData
                x: modelData.x
                y: (strip.height - height) / 2
                width: modelData.width
                height: modelData.height
                radius: modelData.round ? height / 2 : 0
                color: root.active ? root.activeColor : root.inactiveColor
                opacity: modelData.alpha
                border.color: root.borderColor
                border.width: Math.min(root.borderWidth, height / 2)
            }
        }
    }
}
