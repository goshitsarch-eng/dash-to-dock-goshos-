// SPDX-FileCopyrightText: 2026 Goshos Dock contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../src/qml/code/DockGeometry.js" as Geometry
import "../src/qml"

TestCase {
    name: "DockGeometry"

    function test_nearestIconMagnifiesMost() {
        compare(Geometry.magnificationScale(100, 100, 48, 1.6, 2, true), 1.6);
        const neighbor = Geometry.magnificationScale(100, 148, 48, 1.6, 2, true);
        verify(neighbor > 1 && neighbor < 1.6);
        compare(Geometry.magnificationScale(100, 52, 48, 1.6, 2, true), neighbor);
        compare(Geometry.magnificationScale(100, 196, 48, 1.6, 2, true), 1);
        verify(Geometry.magnificationScale(100, 148, 48, 1.6, 3, true) > neighbor);
    }

    function test_disabledAndInvalidGeometryIsStable() {
        compare(Geometry.magnificationScale(100, 100, 48, 1.6, 2, false), 1);
        compare(Geometry.magnificationScale(100, 100, 48, 1, 2, true), 1);
        compare(Geometry.magnificationScale(100, 100, 0, 1.6, 2, true), 1);
        compare(Geometry.magnificationScale(NaN, 100, 48, 1.6, 2, true), 1);
    }

    function test_fixedAndAdaptiveSizes() {
        const fixed = Geometry.dockLayout(10, 400, 64, 48, true, 8, 8, false);
        compare(fixed.iconSize, 48);
        compare(fixed.contentLength, 560);
        verify(fixed.overflow);
        const adaptive = Geometry.dockLayout(10, 400, 64, 48, false, 8, 8, false);
        compare(adaptive.iconSize, 32);
        compare(adaptive.contentLength, 400);
        verify(!adaptive.overflow);
        // User sizes between the predefined steps remain valid maxima.
        compare(Geometry.chooseIconSize(40, 10, 480, 64, 8, 8, false), 40);
        compare(Geometry.chooseIconSize(40, 10, 450, 64, 8, 8, false), 32);
        // A thin panel remains a real limit even in fixed mode.
        compare(Geometry.chooseIconSize(48, 10, 400, 32, 8, 8, true), 24);
        // Passing a globally chosen size into a task viewport caps thickness
        // once; it does not subtract its margins again from the icon itself.
        compare(Geometry.dockLayout(10, 400, 32, 24, true, 8, 8, false).iconSize, 24);
    }

    function test_emptyAndCrowdedLayouts() {
        const empty = Geometry.dockLayout(0, 400, 64, 48, false, 8, 8, true);
        compare(empty.contentLength, 0);
        verify(!empty.overflow);
        const crowded = Geometry.dockLayout(100, 400, 64, 48, false, 8, 8, true);
        compare(crowded.iconSize, 16);
        compare(crowded.contentLength, 2400);
        verify(crowded.overflow);
        compare(crowded.offset, 0);
        const centered = Geometry.dockLayout(3, 400, 64, 48, false, 8, 8, true);
        compare(centered.offset, 116);
        compare(Geometry.dockLayout(3, 400, 64, 48, false, 8, 8, false).offset, 0);
    }

    function test_focusedReveal() {
        compare(Geometry.revealOffset(280, 56, 0, 200, 560, 0), 136);
        compare(Geometry.revealOffset(56, 56, 200, 200, 560, 0), 56);
        compare(Geometry.revealOffset(150, 40, 100, 200, 560, 0), 100);
        compare(Geometry.revealOffset(504, 56, 0, 200, 560, 4), 360);
        compare(Geometry.revealOffset(0, 56, 100, 200, 560, 4), 0);
        compare(Geometry.revealOffset(200, 300, 100, 200, 560, 0), 200);
        compare(Geometry.revealOffset(0, 56, 300, 200, 112, 0), 0);
    }

    Component {
        id: viewportComponent
        DockTaskViewport {
            width: vertical ? 64 : 200
            height: vertical ? 200 : 64
            listLength: 560
            fixedIconSize: true
            taskList: testList
            Item {
                id: testList
                width: parent.width
                height: parent.height
                Rectangle {
                    property var model: ({IsActive: true})
                    x: 280
                    y: 280
                    width: 56
                    height: 56
                }
            }
        }
    }

    function test_scrollViewport_data() {
        return [{tag: "horizontal", vertical: false}, {tag: "vertical", vertical: true}];
    }

    function test_scrollViewport(data) {
        const viewport = createTemporaryObject(viewportComponent, null, {vertical: data.vertical});
        verify(viewport !== null);
        verify(viewport.overflow);
        viewport.revealFocusedTask();
        compare(viewport.scrollOffset, 138);
        viewport.setOffset(5000);
        compare(viewport.scrollOffset, 360);
        viewport.scrollToFocused = false;
        viewport.revealFocusedTask();
        compare(viewport.scrollOffset, 360);
        viewport.listLength = 100;
        compare(viewport.scrollOffset, 0);
        verify(!viewport.overflow);
    }

    function test_edgeAnchoring_data() {
        return ["top", "bottom", "left", "right"].map(edge => ({tag: edge, edge: edge}));
    }

    function test_edgeAnchoring(data) {
        const origin = Geometry.overlayPosition(100, 200, 48, 110, data.edge);
        const resting = Geometry.iconPosition(48, 110, data.edge, 0);
        // Creating the overlay must not move a resting icon at any panel edge.
        compare(origin.x + resting.x, 100);
        compare(origin.y + resting.y, 200);

        const enlarged = Geometry.iconPosition(80, 110, data.edge, 12);
        verify(enlarged.x >= 0 && enlarged.y >= 0);
        verify(enlarged.x + 80 <= 110 && enlarged.y + 80 <= 110);
        if (data.edge === "bottom") {
            compare(origin.y + enlarged.y + 80, 200 + 48 - 12);
        } else if (data.edge === "top") {
            compare(origin.y + enlarged.y, 200 + 12);
        } else if (data.edge === "left") {
            compare(origin.x + enlarged.x, 100 + 12);
        } else {
            compare(origin.x + enlarged.x + 80, 100 + 48 - 12);
        }
    }

    function test_indicatorStyles() {
        for (let style = 1; style <= 9; ++style) {
            compare(Geometry.indicatorSegments(style, 0, 48, 6, false).length, 0);
            const segments = Geometry.indicatorSegments(style, 3, 48, 6, true);
            verify(segments.length > 0);
            for (const segment of segments) {
                verify(segment.x >= 0);
                verify(segment.width > 0 && segment.height > 0);
                verify(segment.x + segment.width <= 48.001);
                verify(segment.height <= 6);
            }
        }
        compare(Geometry.indicatorSegments(1, 8, 48, 6, true).length, 4);
        compare(Geometry.indicatorSegments(9, 8, 48, 6, true).length, 1);
        const binary = Geometry.indicatorSegments(8, 10, 48, 6, true);
        compare(binary.map(segment => segment.round).join(","), "true,false,true,false");
        const ciliora = Geometry.indicatorSegments(6, 3, 48, 6, true);
        verify(ciliora[0].width > ciliora[1].width);
        compare(ciliora[1].width, ciliora[2].width);
    }

    Component {
        id: indicatorComponent
        DockIndicator { width: 48; height: 6 }
    }

    function test_indicatorComponent_data() {
        return ["top", "bottom", "left", "right"].map(edge => ({tag: edge, edge: edge}));
    }

    function test_indicatorComponent(data) {
        const indicator = createTemporaryObject(indicatorComponent, null, {edge: data.edge, windowCount: 3});
        verify(indicator !== null);
        for (let style = 1; style <= 9; ++style) {
            indicator.style = style;
            verify(indicator.visible);
        }
        indicator.windowCount = 0;
        verify(!indicator.visible);
    }
}
