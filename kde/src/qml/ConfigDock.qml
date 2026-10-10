/*
    SPDX-FileCopyrightText: 2026 Goshos Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as QtDialogs
import QtQuick.Layouts
import QtCore

import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    id: root

    property alias cfg_manualHide: manualHide.checked
    property alias cfg_dockFixed: dockFixed.checked
    property alias cfg_autoHide: autoHide.checked
    property alias cfg_intelliHide: intelliHide.checked
    property alias cfg_intelliHideMode: intelliHideMode.currentIndex
    property alias cfg_pressureToShow: pressureToShow.checked
    property alias cfg_pressureThreshold: pressureThreshold.value
    property alias cfg_showDelay: showDelay.value
    property alias cfg_hideDelay: hideDelay.value
    property alias cfg_animationTime: animationTime.value
    property alias cfg_hideInFullscreen: hideInFullscreen.checked
    property string cfg_preferredOutput: "primary"
    property alias cfg_allOutputs: allOutputs.checked
    property alias cfg_dockPosition: dockPosition.currentIndex
    property alias cfg_intendedLengthFraction: intendedLengthFraction.value
    property alias cfg_extendDock: extendDock.checked
    property alias cfg_centerIcons: centerIcons.checked
    property alias cfg_appsAlwaysAtEdge: appsAlwaysAtEdge.checked
    property alias cfg_iconSizeFixed: iconSizeFixed.checked
    property alias cfg_scrollToFocused: scrollToFocused.checked
    property alias cfg_workspaceAgnosticUrgent: workspaceAgnosticUrgent.checked
    property alias cfg_defaultPreviewsOpen: defaultPreviewsOpen.checked
    property alias cfg_customBackgroundColor: customBackgroundColor.checked
    property alias cfg_customizeAlphas: customizeAlphas.checked
    property alias cfg_isolateLocations: isolateLocations.checked
    property alias cfg_visualStyle: visualStyle.currentIndex
    property alias cfg_iconSize: iconSize.value
    property alias cfg_magnification: magnification.checked
    property alias cfg_magnificationFactor: magnificationFactor.value
    property alias cfg_magnificationSpread: magnificationSpread.value
    property alias cfg_launchBounce: launchBounce.checked
    property alias cfg_danceUrgent: danceUrgent.checked
    property alias cfg_dimMinimized: dimMinimized.checked
    property alias cfg_minimizedOpacity: minimizedOpacity.value
    property alias cfg_indicatorStyle: indicatorStyle.currentIndex
    property alias cfg_customizeIndicators: customizeIndicators.checked
    property alias cfg_indicatorColor: indicatorColor.value
    property alias cfg_indicatorBorderColor: indicatorBorderColor.value
    property alias cfg_indicatorBorderWidth: indicatorBorderWidth.value
    property alias cfg_dominantIndicatorColor: dominantIndicatorColor.checked
    property alias cfg_unityBacklit: unityBacklit.checked
    property alias cfg_glossyIcons: glossyIcons.checked
    property alias cfg_showEmblems: showEmblems.checked
    property alias cfg_showNotificationCounter: showNotificationCounter.checked
    property alias cfg_applicationCounterOverridesNotifications: applicationCounterOverridesNotifications.checked
    property string cfg_customColor: "#ffffff"
    property alias cfg_transparencyMode: transparencyMode.currentIndex
    property alias cfg_minAlpha: minAlpha.value
    property alias cfg_maxAlpha: maxAlpha.value
    property alias cfg_backgroundOpacity: backgroundOpacity.value
    property alias cfg_cornerRadius: cornerRadius.value
    property alias cfg_compact: compact.checked
    property alias cfg_showRunning: showRunning.checked
    property alias cfg_showFavorites: showFavorites.checked
    property alias cfg_showApplications: showApplications.checked
    property alias cfg_showAppsAtStart: showAppsAtStart.checked
    property alias cfg_showAppsAction: showAppsAction.currentIndex
    property alias cfg_applicationsStack: applicationsStack.checked
    property alias cfg_documentsStack: documentsStack.checked
    property alias cfg_downloadsStack: downloadsStack.checked
    property alias cfg_homeStack: homeStack.checked
    property var cfg_customFolders: []
    property alias cfg_stackView: stackView.currentIndex
    property alias cfg_stackSort: stackSort.currentIndex
    property string cfg_stackOverrides: "{}"
    property alias cfg_stackItemLimit: stackItemLimit.value
    property alias cfg_stacksDivider: stacksDivider.checked
    property alias cfg_showRecentApps: showRecentApps.checked
    property alias cfg_recentAppsLimit: recentAppsLimit.value
    property alias cfg_showTrash: showTrash.checked
    property alias cfg_showDevices: showDevices.checked
    property alias cfg_showNetworkDevices: showNetworkDevices.checked
    property alias cfg_devicesOnlyMounted: devicesOnlyMounted.checked
    property alias cfg_clickAction: clickAction.action
    property alias cfg_shiftClickAction: shiftClickAction.action
    property alias cfg_middleClickDockAction: middleClickAction.action
    property alias cfg_shiftMiddleClickAction: shiftMiddleClickAction.action
    property alias cfg_dockScrollAction: dockScrollAction.currentIndex

    property alias cfg_hideTooltip: hideTooltip.checked
    property alias cfg_previewSizeScale: previewSizeScale.value
    property alias cfg_hotKeys: hotKeys.checked
    property alias cfg_hotkeysOverlay: hotkeysOverlay.checked
    property alias cfg_hotkeysShowDock: hotkeysShowDock.checked
    property alias cfg_shortcutTimeout: shortcutTimeout.value

    readonly property var clickActions: [
        { value: "skip", text: i18nc("@item:inlistbox click action", "Focus application") },
        { value: "minimize", text: i18nc("@item:inlistbox click action", "Minimize") },
        { value: "launch", text: i18nc("@item:inlistbox click action", "Open a new window") },
        { value: "cycle-windows", text: i18nc("@item:inlistbox click action", "Cycle through windows") },
        { value: "minimize-or-overview", text: i18nc("@item:inlistbox click action", "Minimize or show all windows") },
        { value: "previews", text: i18nc("@item:inlistbox click action", "Show window previews") },
        { value: "minimize-or-previews", text: i18nc("@item:inlistbox click action", "Minimize or show previews") },
        { value: "focus-or-previews", text: i18nc("@item:inlistbox click action", "Focus or show previews") },
        { value: "focus-or-appspread", text: i18nc("@item:inlistbox click action", "Focus or show application windows") },
        { value: "focus-minimize-or-previews", text: i18nc("@item:inlistbox click action", "Focus, minimize, or show previews") },
        { value: "focus-minimize-or-appspread", text: i18nc("@item:inlistbox click action", "Focus, minimize, or show application windows") },
        { value: "quit", text: i18nc("@item:inlistbox click action", "Quit application") }
    ]

    component ActionSelector: QQC2.ComboBox {
        property string action: "launch"
        Layout.fillWidth: true
        model: root.clickActions
        textRole: "text"
        valueRole: "value"
        currentIndex: indexOfValue(action)
        onActivated: action = currentValue
    }

    component ColorSelector: RowLayout {
        id: colorSelector
        property color value: "#ffffff"
        property string accessibleLabel
        Layout.fillWidth: true

        QQC2.TextField {
            Layout.fillWidth: true
            text: colorSelector.value.toString()
            validator: RegularExpressionValidator { regularExpression: /#[0-9a-fA-F]{6}/ }
            onEditingFinished: {
                if (acceptableInput) {
                    colorSelector.value = text;
                }
            }
            Accessible.name: colorSelector.accessibleLabel
        }
        QQC2.Button {
            text: i18nc("@action:button", "Choose…")
            icon.name: "color-picker"
            onClicked: selectorDialog.open()
        }
        QtDialogs.ColorDialog {
            id: selectorDialog
            title: colorSelector.accessibleLabel
            selectedColor: colorSelector.value
            onAccepted: colorSelector.value = selectedColor
        }
    }

    function addFolder(input) {
        let folder = input.toString().trim();
        if (folder === "~" || folder.startsWith("~/")) {
            folder = StandardPaths.writableLocation(StandardPaths.HomeLocation) + folder.slice(1);
        }
        if (folder.startsWith("/")) {
            folder = "file://" + encodeURIComponent(folder).replace(/%2F/g, "/");
        }
        if (!/^[a-z][a-z0-9+.-]*:\//i.test(folder)) {
            folderError.text = i18nc("@info", "Enter a folder path beginning with / or ~/, or a folder URL.");
            folderError.visible = true;
            return;
        }
        // Keep the root URL intact and avoid adding the same folder twice.
        if (!/^[a-z][a-z0-9+.-]*:\/\/\/$/i.test(folder)) {
            folder = folder.replace(/\/+$/, "");
        }
        const folders = root.cfg_customFolders.slice();
        if (!folders.includes(folder)) {
            folders.push(folder);
            root.cfg_customFolders = folders;
        }
        folderPath.clear();
        folderError.visible = false;
    }

    function removeFolder(index) {
        const folders = root.cfg_customFolders.slice();
        folders.splice(index, 1);
        root.cfg_customFolders = folders;
    }

    QtDialogs.FolderDialog {
        id: folderDialog
        title: i18nc("@title:window", "Add Folder Stack")
        onAccepted: root.addFolder(selectedFolder)
    }

    QtDialogs.ColorDialog {
        id: colorDialog
        title: i18nc("@title:window", "Choose Dock Color")
        selectedColor: root.cfg_customColor
        onAccepted: root.cfg_customColor = selectedColor.toString()
    }

    Kirigami.FormLayout {
        anchors.left: parent.left
        anchors.right: parent.right

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("@title:group", "Position and Size")
        }

        QQC2.ComboBox {
            id: dockPosition
            Kirigami.FormData.label: i18nc("@label:listbox", "Screen edge:")
            Layout.fillWidth: true
            model: [i18n("Top"), i18n("Right"), i18n("Bottom"), i18n("Left")]
        }
        QQC2.TextField {
            Kirigami.FormData.label: i18nc("@label:textbox", "Screen:")
            Layout.fillWidth: true
            text: root.cfg_preferredOutput
            placeholderText: i18nc("@info:placeholder", "primary, eDP-1, DP-1…")
            onTextEdited: root.cfg_preferredOutput = text.trim() || "primary"
            enabled: !allOutputs.checked
        }
        QQC2.CheckBox {
            id: allOutputs
            text: i18nc("@option:check", "Show a dock on every screen")
        }
        RowLayout {
            Kirigami.FormData.label: i18nc("@label", "Maximum length:")
            Layout.fillWidth: true
            QQC2.Slider {
                id: intendedLengthFraction
                Layout.fillWidth: true
                from: 0.1
                to: 1
                stepSize: 0.01
                Accessible.name: i18nc("@info:accessibility", "Maximum dock length as a fraction of screen size")
            }
            QQC2.Label { text: i18nc("@label percentage", "%1%", Math.round(intendedLengthFraction.value * 100)) }
        }
        QQC2.CheckBox {
            id: extendDock
            text: i18nc("@option:check", "Extend the dock background to its full length")
        }
        QQC2.CheckBox {
            id: centerIcons
            text: i18nc("@option:check", "Center application icons")
            enabled: extendDock.checked
        }
        QQC2.CheckBox {
            id: appsAlwaysAtEdge
            text: i18nc("@option:check", "Keep the application launcher at the dock edge")
            enabled: extendDock.checked && centerIcons.checked && showApplications.checked
        }

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("@title:group", "Visibility")
        }
        QQC2.CheckBox { id: manualHide; text: i18nc("@option:check", "Keep the dock hidden") }
        QQC2.CheckBox {
            id: dockFixed
            text: i18nc("@option:check", "Keep the dock visible and reserve screen space")
            enabled: !manualHide.checked
        }
        QQC2.CheckBox {
            id: autoHide
            text: i18nc("@option:check", "Automatically hide when the pointer leaves")
            enabled: !manualHide.checked && !dockFixed.checked
        }
        QQC2.CheckBox {
            id: intelliHide
            text: i18nc("@option:check", "Hide when windows overlap the dock")
            enabled: !manualHide.checked && !dockFixed.checked
        }
        QQC2.ComboBox {
            id: intelliHideMode
            Kirigami.FormData.label: i18nc("@label:listbox", "Avoid:")
            Layout.fillWidth: true
            model: [i18n("All windows"), i18n("Windows of the focused application"), i18n("Maximized windows"), i18n("Stay above windows")]
            enabled: intelliHide.checked && !dockFixed.checked && !manualHide.checked
        }
        QQC2.CheckBox {
            id: hideInFullscreen
            text: i18nc("@option:check", "Keep hidden while an application is fullscreen")
            enabled: !dockFixed.checked && !manualHide.checked
        }
        QQC2.CheckBox {
            id: pressureToShow
            text: i18nc("@option:check", "Push against the screen edge to reveal the dock")
            enabled: autoHide.checked && !dockFixed.checked && !manualHide.checked
        }
        RowLayout {
            Kirigami.FormData.label: i18nc("@label", "Edge pressure:")
            Layout.fillWidth: true
            enabled: pressureToShow.checked && autoHide.checked && !dockFixed.checked && !manualHide.checked
            QQC2.Slider {
                id: pressureThreshold
                Layout.fillWidth: true
                from: 0
                to: 100
                stepSize: 5
                Accessible.name: i18nc("@info:accessibility", "Pressure required to reveal the dock")
            }
            QQC2.Label { text: pressureThreshold.value.toFixed(0) }
        }
        RowLayout {
            Kirigami.FormData.label: i18nc("@label", "Show delay:")
            Layout.fillWidth: true
            enabled: !dockFixed.checked && !manualHide.checked
            QQC2.Slider {
                id: showDelay
                Layout.fillWidth: true
                from: 0
                to: 1
                stepSize: 0.05
                Accessible.name: i18nc("@info:accessibility", "Delay before showing the dock in seconds")
            }
            QQC2.Label { text: i18nc("@label seconds", "%1 s", showDelay.value.toFixed(2)) }
        }
        RowLayout {
            Kirigami.FormData.label: i18nc("@label", "Hide delay:")
            Layout.fillWidth: true
            enabled: !dockFixed.checked && !manualHide.checked
            QQC2.Slider {
                id: hideDelay
                Layout.fillWidth: true
                from: 0
                to: 1
                stepSize: 0.05
                Accessible.name: i18nc("@info:accessibility", "Delay before hiding the dock in seconds")
            }
            QQC2.Label { text: i18nc("@label seconds", "%1 s", hideDelay.value.toFixed(2)) }
        }
        RowLayout {
            Kirigami.FormData.label: i18nc("@label", "Slide duration:")
            Layout.fillWidth: true
            QQC2.Slider {
                id: animationTime
                Layout.fillWidth: true
                from: 0
                to: 1
                stepSize: 0.05
                Accessible.name: i18nc("@info:accessibility", "Dock slide animation duration in seconds")
            }
            QQC2.Label { text: i18nc("@label seconds", "%1 s", animationTime.value.toFixed(2)) }
        }

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("@title:group", "Keyboard")
        }
        QQC2.CheckBox { id: hideTooltip; text: i18n("Hide hover tooltips") }
        QQC2.Slider {
            id: previewSizeScale
            Kirigami.FormData.label: i18n("Window preview scale:")
            from: 0; to: 1; stepSize: 0.01
            QQC2.ToolTip.visible: pressed
            QQC2.ToolTip.text: value === 0 ? i18n("Automatic") : i18n("%1% of the window", Math.round(value * 100))
        }
        QQC2.CheckBox { id: hotKeys; text: i18n("Enable dock shortcuts") }
        QQC2.CheckBox { id: hotkeysOverlay; text: i18n("Show numbered shortcut badges"); enabled: hotKeys.checked }
        QQC2.CheckBox { id: hotkeysShowDock; text: i18n("Reveal the panel when a shortcut is used"); enabled: hotKeys.checked }
        QQC2.Slider {
            id: shortcutTimeout
            Kirigami.FormData.label: i18n("Shortcut display duration:")
            from: 0.1; to: 10; stepSize: 0.1
            QQC2.ToolTip.visible: pressed
            QQC2.ToolTip.text: i18n("%1 seconds", value.toFixed(1))
        }
        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: i18n("Meta+number activates a task. Ctrl+Meta+number opens a new window; Shift+Meta+number uses the alternate action. Meta+Q shows the numbered badges. Change bindings in System Settings → Keyboard → Shortcuts → Gosho’s Dock.")
        }

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("@title:group", "Style")
        }

        QQC2.ComboBox {
            id: visualStyle
            Kirigami.FormData.label: i18nc("@label:listbox", "Appearance:")
            Layout.fillWidth: true
            model: [i18n("Breeze"), i18n("Glass"), i18n("Minimal"), i18n("Classic")]
        }

        QQC2.SpinBox {
            id: iconSize
            Kirigami.FormData.label: i18nc("@label:spinbox", "Icon size:")
            from: 16
            to: 128
            stepSize: 4
            editable: true
            Accessible.description: i18nc("@info:accessibility", "Preferred icon size in pixels; limited by the panel size.")
        }

        QQC2.CheckBox {
            id: iconSizeFixed
            text: i18nc("@option:check", "Keep icon size fixed and scroll when space is limited")
        }
        QQC2.CheckBox {
            id: scrollToFocused
            text: i18nc("@option:check", "Scroll to the focused application")
            enabled: iconSizeFixed.checked
        }

        QQC2.CheckBox {
            id: compact
            text: i18nc("@option:check", "Use compact spacing")
        }

        QQC2.CheckBox {
            id: customBackgroundColor
            text: i18nc("@option:check", "Use a custom background color")
            enabled: visualStyle.currentIndex !== 2
        }
        RowLayout {
            Kirigami.FormData.label: i18nc("@label", "Background color:")
            Layout.fillWidth: true
            enabled: customBackgroundColor.checked && visualStyle.currentIndex !== 2

            QQC2.TextField {
                Layout.fillWidth: true
                text: root.cfg_customColor
                validator: RegularExpressionValidator { regularExpression: /#[0-9a-fA-F]{6}/ }
                onEditingFinished: {
                    if (acceptableInput) {
                        root.cfg_customColor = text;
                    }
                }
                Accessible.name: i18nc("@info:accessibility", "Background color as a hexadecimal value")
            }

            QQC2.Button {
                text: i18nc("@action:button", "Choose…")
                icon.name: "color-picker"
                onClicked: colorDialog.open()
            }
        }

        QQC2.ComboBox {
            id: transparencyMode
            Kirigami.FormData.label: i18nc("@label:listbox", "Transparency:")
            Layout.fillWidth: true
            enabled: visualStyle.currentIndex !== 2
            model: [i18n("Fixed"), i18n("Adaptive"), i18n("Theme")]
        }

        RowLayout {
            Kirigami.FormData.label: i18nc("@label", "Background opacity:")
            Layout.fillWidth: true
            visible: transparencyMode.currentIndex === 0
            enabled: visualStyle.currentIndex !== 2
            QQC2.Slider {
                id: backgroundOpacity
                Layout.fillWidth: true
                from: 0
                to: 1
                stepSize: 0.05
                Accessible.name: i18nc("@info:accessibility", "Background opacity")
            }
            QQC2.Label { text: i18nc("@label percentage", "%1%", Math.round(backgroundOpacity.value * 100)) }
        }

        RowLayout {
            Kirigami.FormData.label: i18nc("@label", "Unobstructed opacity:")
            Layout.fillWidth: true
            visible: transparencyMode.currentIndex === 1
            enabled: visualStyle.currentIndex !== 2 && customizeAlphas.checked
            QQC2.Slider {
                id: minAlpha
                Layout.fillWidth: true
                from: 0
                to: 1
                stepSize: 0.05
                Accessible.name: i18nc("@info:accessibility", "Background opacity when no window overlaps the dock")
                onMoved: {
                    if (value > maxAlpha.value) {
                        maxAlpha.value = value;
                    }
                }
            }
            QQC2.Label { text: i18nc("@label percentage", "%1%", Math.round(minAlpha.value * 100)) }
        }

        RowLayout {
            Kirigami.FormData.label: i18nc("@label", "Window overlap opacity:")
            Layout.fillWidth: true
            visible: transparencyMode.currentIndex === 1
            enabled: visualStyle.currentIndex !== 2 && customizeAlphas.checked
            QQC2.Slider {
                id: maxAlpha
                Layout.fillWidth: true
                from: 0
                to: 1
                stepSize: 0.05
                Accessible.name: i18nc("@info:accessibility", "Background opacity when a window overlaps the dock")
                onMoved: {
                    if (value < minAlpha.value) {
                        minAlpha.value = value;
                    }
                }
            }
            QQC2.Label { text: i18nc("@label percentage", "%1%", Math.round(maxAlpha.value * 100)) }
        }

        QQC2.CheckBox {
            id: customizeAlphas
            text: i18nc("@option:check", "Customize adaptive opacity levels")
            visible: transparencyMode.currentIndex === 1
            enabled: visualStyle.currentIndex !== 2
        }

        QQC2.SpinBox {
            id: cornerRadius
            Kirigami.FormData.label: i18nc("@label:spinbox", "Corner radius:")
            from: 0
            to: 48
            editable: true
            enabled: visualStyle.currentIndex !== 2
        }

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("@title:group", "Indicators and Badges")
        }

        QQC2.ComboBox {
            id: indicatorStyle
            Kirigami.FormData.label: i18nc("@label:listbox", "Running indicators:")
            Layout.fillWidth: true
            model: [i18n("Default"), i18n("Dots"), i18n("Squares"), i18n("Dashes"), i18n("Segmented"), i18n("Solid"), i18n("Ciliora"), i18n("Metro"), i18n("Binary"), i18n("Dot")]
        }

        QQC2.CheckBox {
            id: dominantIndicatorColor
            text: i18nc("@option:check", "Match indicators to application icon colors")
        }

        QQC2.CheckBox {
            id: customizeIndicators
            text: i18nc("@option:check", "Customize indicator colors and outlines")
        }

        ColorSelector {
            id: indicatorColor
            Kirigami.FormData.label: i18nc("@label", "Indicator color:")
            accessibleLabel: i18nc("@info:accessibility", "Indicator color")
            enabled: customizeIndicators.checked && !dominantIndicatorColor.checked
        }

        ColorSelector {
            id: indicatorBorderColor
            Kirigami.FormData.label: i18nc("@label", "Outline color:")
            accessibleLabel: i18nc("@info:accessibility", "Indicator outline color")
            enabled: customizeIndicators.checked
        }

        RowLayout {
            Kirigami.FormData.label: i18nc("@label", "Outline width:")
            Layout.fillWidth: true
            enabled: customizeIndicators.checked
            QQC2.Slider {
                id: indicatorBorderWidth
                Layout.fillWidth: true
                from: 0
                to: 4
                stepSize: 0.25
                Accessible.name: i18nc("@info:accessibility", "Indicator outline width")
            }
            QQC2.Label { text: i18nc("@label pixels", "%1 px", indicatorBorderWidth.value.toFixed(2)) }
        }

        QQC2.CheckBox {
            id: unityBacklit
            text: i18nc("@option:check", "Illuminate running application icons")
        }

        QQC2.CheckBox {
            id: glossyIcons
            text: i18nc("@option:check", "Add a glossy highlight to icon backgrounds")
            enabled: unityBacklit.checked
        }

        QQC2.CheckBox {
            id: showEmblems
            text: i18nc("@option:check", "Show application badges and progress")
        }

        QQC2.CheckBox {
            id: showNotificationCounter
            text: i18nc("@option:check", "Show notification counts")
            enabled: showEmblems.checked
        }

        QQC2.CheckBox {
            id: applicationCounterOverridesNotifications
            text: i18nc("@option:check", "Prefer application counts over notification counts")
            enabled: showEmblems.checked && showNotificationCounter.checked
        }

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("@title:group", "Animation")
        }

        QQC2.CheckBox {
            id: magnification
            text: i18nc("@option:check", "Enlarge icons near the pointer")
        }

        RowLayout {
            Kirigami.FormData.label: i18nc("@label", "Magnification:")
            Layout.fillWidth: true
            enabled: magnification.checked
            QQC2.Slider {
                id: magnificationFactor
                Layout.fillWidth: true
                from: 1
                to: 3
                stepSize: 0.1
                Accessible.name: i18nc("@info:accessibility", "Maximum icon magnification")
            }
            QQC2.Label { text: i18nc("@label magnification multiplier", "%1×", magnificationFactor.value.toFixed(1)) }
        }

        RowLayout {
            Kirigami.FormData.label: i18nc("@label", "Magnification spread:")
            Layout.fillWidth: true
            enabled: magnification.checked
            QQC2.Slider {
                id: magnificationSpread
                Layout.fillWidth: true
                from: 1
                to: 6
                stepSize: 0.1
                Accessible.name: i18nc("@info:accessibility", "Number of neighboring icons affected by magnification")
            }
            QQC2.Label { text: magnificationSpread.value.toFixed(1) }
        }

        QQC2.CheckBox {
            id: launchBounce
            text: i18nc("@option:check", "Bounce icons when launching applications")
        }

        QQC2.CheckBox {
            id: danceUrgent
            text: i18nc("@option:check", "Animate icons when applications need attention")
        }

        QQC2.CheckBox {
            id: dimMinimized
            text: i18nc("@option:check", "Dim minimized applications")
        }

        RowLayout {
            Kirigami.FormData.label: i18nc("@label", "Minimized opacity:")
            Layout.fillWidth: true
            enabled: dimMinimized.checked
            QQC2.Slider {
                id: minimizedOpacity
                Layout.fillWidth: true
                from: 0.1
                to: 1
                stepSize: 0.05
                Accessible.name: i18nc("@info:accessibility", "Minimized application opacity")
            }
            QQC2.Label { text: i18nc("@label percentage", "%1%", Math.round(minimizedOpacity.value * 100)) }
        }

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("@title:group", "Applications")
        }

        QQC2.CheckBox { id: showRunning; text: i18nc("@option:check", "Show running applications") }
        QQC2.CheckBox { id: showFavorites; text: i18nc("@option:check", "Show pinned applications") }
        QQC2.CheckBox {
            id: workspaceAgnosticUrgent
            text: i18nc("@option:check", "Show applications that need attention on every desktop")
        }
        QQC2.CheckBox {
            id: defaultPreviewsOpen
            text: i18nc("@option:check", "Open window previews when showing an application's menu")
        }
        QQC2.CheckBox { id: showApplications; text: i18nc("@option:check", "Show application launcher") }
        QQC2.CheckBox {
            id: showAppsAtStart
            text: i18nc("@option:check", "Place application launcher before task icons")
            enabled: showApplications.checked
        }
        QQC2.ComboBox {
            id: showAppsAction
            Kirigami.FormData.label: i18nc("@label:listbox", "Application launcher opens:")
            Layout.fillWidth: true
            enabled: showApplications.checked
            model: [i18n("Application launcher"), i18n("Applications stack")]
        }
        QQC2.CheckBox { id: showRecentApps; text: i18nc("@option:check", "Show recently used applications") }

        QQC2.SpinBox {
            id: recentAppsLimit
            Kirigami.FormData.label: i18nc("@label:spinbox", "Recent applications:")
            from: 1
            to: 10
            editable: true
            enabled: showRecentApps.checked
        }

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("@title:group", "Stacks and Places")
        }

        QQC2.CheckBox { id: applicationsStack; text: i18nc("@option:check", "Applications stack") }
        QQC2.CheckBox { id: documentsStack; text: i18nc("@option:check", "Documents stack") }
        QQC2.CheckBox { id: downloadsStack; text: i18nc("@option:check", "Downloads stack") }
        QQC2.CheckBox { id: homeStack; text: i18nc("@option:check", "Home stack") }

        ColumnLayout {
            Kirigami.FormData.label: i18nc("@label", "Custom folders:")
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: root.cfg_customFolders
                delegate: RowLayout {
                    id: folderRow
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: folderRow.modelData
                        elide: Text.ElideMiddle
                        QQC2.ToolTip.text: folderRow.modelData
                        QQC2.ToolTip.visible: folderHover.hovered
                        HoverHandler { id: folderHover }
                    }
                    QQC2.ToolButton {
                        icon.name: "list-remove"
                        text: i18nc("@action:button", "Remove folder")
                        display: QQC2.AbstractButton.IconOnly
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        onClicked: root.removeFolder(folderRow.index)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                QQC2.TextField {
                    id: folderPath
                    Layout.fillWidth: true
                    placeholderText: i18nc("@info:placeholder", "Folder path or URL")
                    Accessible.name: placeholderText
                    onAccepted: root.addFolder(text)
                }
                QQC2.Button {
                    text: i18nc("@action:button", "Add")
                    icon.name: "list-add"
                    enabled: folderPath.text.trim().length > 0
                    onClicked: root.addFolder(folderPath.text)
                }
                QQC2.ToolButton {
                    text: i18nc("@action:button", "Browse for folder…")
                    icon.name: "folder-open"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    onClicked: folderDialog.open()
                }
            }

            Kirigami.InlineMessage {
                id: folderError
                Layout.fillWidth: true
                type: Kirigami.MessageType.Error
            }
        }

        QQC2.ComboBox {
            id: stackView
            Kirigami.FormData.label: i18nc("@label:listbox", "Stack view:")
            Layout.fillWidth: true
            model: [i18n("Automatic"), i18n("Fan"), i18n("Grid"), i18n("List")]
        }

        QQC2.ComboBox {
            id: stackSort
            Kirigami.FormData.label: i18nc("@label:listbox", "Sort stacks by:")
            Layout.fillWidth: true
            model: [i18n("Name"), i18n("Date"), i18n("Kind")]
        }

        QQC2.SpinBox {
            id: stackItemLimit
            Kirigami.FormData.label: i18nc("@label:spinbox", "Maximum items per stack:")
            from: 1
            to: 500
            editable: true
        }

        QQC2.Button {
            text: i18nc("@action:button", "Reset Individual Stack Preferences")
            icon.name: "edit-reset"
            enabled: root.cfg_stackOverrides !== "{}" && root.cfg_stackOverrides.length > 0
            onClicked: root.cfg_stackOverrides = "{}"
        }

        QQC2.CheckBox { id: stacksDivider; text: i18nc("@option:check", "Separate stacks from applications") }
        QQC2.CheckBox { id: showTrash; text: i18nc("@option:check", "Show Trash") }
        QQC2.CheckBox { id: showDevices; text: i18nc("@option:check", "Show removable devices") }
        QQC2.CheckBox {
            id: devicesOnlyMounted
            text: i18nc("@option:check", "Only show mounted devices")
            enabled: showDevices.checked
        }
        QQC2.CheckBox { id: showNetworkDevices; text: i18nc("@option:check", "Show network locations") }
        QQC2.CheckBox {
            id: isolateLocations
            text: i18nc("@option:check", "Keep folder and device windows with their dock icons")
        }

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("@title:group", "Mouse Actions")
        }

        ActionSelector {
            id: clickAction
            Kirigami.FormData.label: i18nc("@label:listbox", "Click:")
        }
        ActionSelector {
            id: shiftClickAction
            Kirigami.FormData.label: i18nc("@label:listbox", "Shift+click:")
        }
        ActionSelector {
            id: middleClickAction
            Kirigami.FormData.label: i18nc("@label:listbox", "Middle-click:")
        }
        ActionSelector {
            id: shiftMiddleClickAction
            Kirigami.FormData.label: i18nc("@label:listbox", "Shift+middle-click:")
        }
        QQC2.ComboBox {
            id: dockScrollAction
            Kirigami.FormData.label: i18nc("@label:listbox", "Scroll over an application:")
            Layout.fillWidth: true
            model: [i18n("Do nothing"), i18n("Cycle through windows"), i18n("Switch virtual desktops")]
        }
    }
}
