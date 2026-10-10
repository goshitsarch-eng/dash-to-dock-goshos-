/*
    SPDX-FileCopyrightText: 2012-2013 Eike Hein <hein@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Layouts

import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import plasma.applet.org.gosh.goshosdock as TaskManagerApplet
import "code/DockGeometry.js" as DockGeometry

GridLayout {
    id: list
    property bool animating: false

    rowSpacing: 0
    columnSpacing: 0

    property int animationsRunning: 0
    onAnimationsRunningChanged: {
        animating = animationsRunning > 0;
    }

    required property int count

    readonly property bool vertical: Plasmoid.formFactor === PlasmaCore.Types.Vertical
    readonly property bool dockLayout: true
    property real availableLength: vertical ? parent.height : parent.width
    property real availableThickness: vertical ? parent.width : parent.height
    // A shared size keeps task icons and stack/application buttons consistent.
    // Leave unset when this list computes its own adaptive size.
    property real sharedIconSize: -1
    property real requestedIconSize: Plasmoid.configuration.iconSize
    property bool fixedIconSize: Plasmoid.configuration.iconSizeFixed
    property bool centerIcons: Plasmoid.configuration.extendDock && Plasmoid.configuration.centerIcons
    readonly property real alongPadding: vertical
        ? TaskManagerApplet.LayoutMetrics.verticalMargins()
        : TaskManagerApplet.LayoutMetrics.horizontalMargins()
    readonly property real crossPadding: vertical
        ? TaskManagerApplet.LayoutMetrics.horizontalMargins()
        : TaskManagerApplet.LayoutMetrics.verticalMargins()
    // A shared size has already been capped by panel thickness. Its content
    // length must not depend on the allocated viewport: that viewport is
    // itself calculated from the strip's content length.
    readonly property real effectiveIconSize: sharedIconSize >= 0 ? sharedIconSize
        : DockGeometry.chooseIconSize(requestedIconSize, count, availableLength,
            availableThickness, alongPadding, crossPadding, fixedIconSize)
    readonly property real cellSize: effectiveIconSize + Math.max(0, alongPadding)
    readonly property real cellWidth: vertical ? availableThickness : cellSize
    readonly property real cellHeight: vertical ? cellSize : availableThickness
    readonly property real contentLength: Math.max(0, count) * cellSize
    readonly property real leadingOffset: centerIcons ? Math.max(0, (availableLength - contentLength) / 2) : 0
    readonly property real minimumWidth: cellWidth

    // A dock is one strip. Fixed cells create scrollable overflow instead of
    // allowing GridLayout to squeeze icons or silently introduce more rows.
    rows: vertical ? Math.max(1, count) : 1
    columns: vertical ? 1 : Math.max(1, count)
    flow: vertical ? Grid.TopToBottom : Grid.LeftToRight
    width: vertical ? availableThickness : contentLength
    height: vertical ? contentLength : availableThickness
    Layout.maximumWidth: vertical ? availableThickness : count * (requestedIconSize + alongPadding)
    Layout.maximumHeight: vertical ? count * (requestedIconSize + alongPadding) : availableThickness
}
