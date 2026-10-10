// SPDX-FileCopyrightText: 2026 Goshos Dock contributors
// SPDX-License-Identifier: GPL-2.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import "code/DockGeometry.js" as DockGeometry

Flickable {
    id: viewport

    property bool vertical: false
    property bool fixedIconSize: false
    property bool scrollToFocused: true
    property Item taskList
    property real listLength: 0
    property real scrollStep: 48
    property bool dragScrolling: false
    property real dragPosition: -1
    readonly property real viewportLength: vertical ? height : width
    readonly property bool overflow: listLength > viewportLength
    readonly property real scrollOffset: vertical ? contentY : contentX

    signal scrolled

    clip: true
    interactive: overflow
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: vertical ? Flickable.VerticalFlick : Flickable.HorizontalFlick
    contentWidth: vertical ? width : Math.max(width, listLength)
    contentHeight: vertical ? Math.max(height, listLength) : height

    function setOffset(value): void {
        const next = Math.max(0, Math.min(Math.max(0, listLength - viewportLength), value));
        if (vertical) {
            contentY = next;
        } else {
            contentX = next;
        }
    }

    function revealItem(item): void {
        if (!item || !item.visible || !overflow) {
            return;
        }
        const origin = item.mapToItem(contentItem, 0, 0);
        setOffset(DockGeometry.revealOffset(vertical ? origin.y : origin.x,
            vertical ? item.height : item.width, scrollOffset, viewportLength, listLength, 2));
    }

    function revealFocusedTask(): void {
        if (!scrollToFocused || !taskList) {
            return;
        }
        const active = taskList.children.find(item => item.visible && item.model?.IsActive);
        if (active) {
            revealItem(active);
        }
    }

    function updateGeometry(): void {
        setOffset(scrollOffset);
        Qt.callLater(revealFocusedTask);
    }

    onListLengthChanged: updateGeometry()
    onViewportLengthChanged: updateGeometry()
    onVerticalChanged: {
        contentX = 0;
        contentY = 0;
        updateGeometry();
    }
    onScrollToFocusedChanged: if (scrollToFocused) Qt.callLater(revealFocusedTask)
    onContentXChanged: scrolled()
    onContentYChanged: scrolled()

    // The fixed-size mode consumes wheel events just as the GNOME dock does;
    // adaptive mode preserves configured app/workspace scroll actions.
    WheelHandler {
        enabled: viewport.fixedIconSize && viewport.overflow
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            const pixels = viewport.vertical ? event.pixelDelta.y : (event.pixelDelta.x || event.pixelDelta.y);
            const angle = viewport.vertical ? event.angleDelta.y : (event.angleDelta.x || event.angleDelta.y);
            viewport.setOffset(viewport.scrollOffset - (pixels || angle / 120 * viewport.scrollStep));
            event.accepted = true;
        }
    }

    Timer {
        interval: 40
        repeat: true
        running: viewport.dragScrolling && viewport.overflow && viewport.dragPosition >= 0
        onTriggered: {
            const edge = Math.min(28, viewport.viewportLength / 4);
            if (viewport.dragPosition < edge) {
                viewport.setOffset(viewport.scrollOffset - viewport.scrollStep / 5);
            } else if (viewport.dragPosition > viewport.viewportLength - edge) {
                viewport.setOffset(viewport.scrollOffset + viewport.scrollStep / 5);
            }
        }
    }
}
