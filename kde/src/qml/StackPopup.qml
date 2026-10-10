/* SPDX-License-Identifier: GPL-2.0-or-later */
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import "code/StackLogic.js" as StackLogic

PlasmaCore.Dialog {
    id: root

    required property var configuration
    required property var backend
    property var locationBackend: null
    property var stack: null
    property int edge: PlasmaCore.Types.BottomEdge
    property bool contextMenuOpen: false
    readonly property bool applications: stack !== null && stack.kind === "applications"
    readonly property bool folder: stack !== null && stack.kind === "folder"
    readonly property string stackKey: stack !== null ? stack.key : ""
    readonly property int requestedView: StackLogic.preference(configuration.stackOverrides, stackKey, "view", configuration.stackView)
    readonly property int sortOrder: StackLogic.preference(configuration.stackOverrides, stackKey, "sort", configuration.stackSort)
    readonly property var sourceEntries: !backend ? [] : (applications ? backend.applications : (folder ? backend.entries : []))
    readonly property var sortedEntries: StackLogic.visibleEntries(sourceEntries, sortOrder, searchField.text, applications)
    readonly property int viewMode: StackLogic.effectiveView(requestedView, sortedEntries.length, applications)
    readonly property var shownEntries: sortedEntries.slice(0, StackLogic.itemLimit(configuration.stackItemLimit, viewMode))
    readonly property int overflowCount: sortedEntries.length - shownEntries.length
    readonly property bool loading: folder && backend !== null && backend.busy
    readonly property string listingError: folder && backend ? backend.error : ""

    signal contextRequested(Item anchor)
    signal launchApplicationRequested(string desktopId)

    type: PlasmaCore.Dialog.PopupMenu
    flags: Qt.WindowStaysOnTopHint
    location: edge
    hideOnWindowDeactivate: !contextMenuOpen
    visible: false

    function openStack(entry, anchor): void {
        if (visible && stack !== null && stack.key === entry.key && visualParent === anchor) {
            visible = false;
            return;
        }
        stack = entry;
        visualParent = anchor;
        searchField.clear();
        if (folder && backend) {
            backend.listFolder(entry.url);
        }
        visible = true;
        Qt.callLater(() => searchField.forceActiveFocus());
    }

    function activateEntry(entry): void {
        if (!entry || !backend) {
            return;
        }
        if (applications) {
            launchApplicationRequested(entry.desktopId || entry.url);
        } else {
            if (!entry.isDir || !locationBackend || !locationBackend.activate(entry.url)) backend.openUrl(entry.url);
        }
        visible = false;
    }

    function openFolder(): void {
        if (folder && backend) {
            if (!locationBackend || !locationBackend.activate(stack.url)) backend.openUrl(stack.url);
            visible = false;
        }
    }

    mainItem: FocusScope {
        id: content

        readonly property int unit: Kirigami.Units.gridUnit
        // A bounded content area keeps the popup usable on small and rotated screens.
        readonly property real screenWidth: Math.max(200, Screen.width)
        readonly property real screenHeight: Math.max(200, Screen.height)
        implicitWidth: Math.min(screenWidth - unit * 2, root.viewMode === 2 ? unit * 26 : unit * 22)
        implicitHeight: Math.min(screenHeight * 0.75, contentColumn.implicitHeight)
        width: implicitWidth
        height: implicitHeight
        activeFocusOnTab: true

        Keys.onEscapePressed: root.visible = false
        Keys.onReturnPressed: {
            if (root.shownEntries.length > 0 && searchField.activeFocus) {
                root.activateEntry(root.shownEntries[0]);
            }
        }

        ColumnLayout {
            id: contentColumn
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true

                Kirigami.Icon {
                    source: root.stack !== null ? root.stack.icon : "folder"
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: implicitWidth
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: root.stack !== null ? root.stack.name : ""
                    font.bold: true
                    elide: Text.ElideRight
                    Accessible.role: Accessible.Heading
                }
                QQC2.ToolButton {
                    id: optionsButton
                    icon.name: "configure"
                    Accessible.name: i18nc("@action:button", "Stack options")
                    QQC2.ToolTip.text: Accessible.name
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: root.contextRequested(optionsButton)
                }
            }

            QQC2.TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: root.applications ? i18nc("@info:placeholder", "Search applications…") : i18nc("@info:placeholder", "Filter this folder…")
                Accessible.name: placeholderText
                Keys.onDownPressed: {
                    if (root.shownEntries.length > 0) {
                        if (root.viewMode === 2) {
                            grid.currentIndex = 0;
                            grid.forceActiveFocus();
                        } else {
                            list.currentIndex = 0;
                            list.forceActiveFocus();
                        }
                    }
                }
                onTextChanged: {
                    grid.currentIndex = 0;
                    list.currentIndex = 0;
                }
            }

            QQC2.BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                visible: root.loading
                running: visible
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.listingError.length > 0
                text: root.listingError
                type: Kirigami.MessageType.Error
            }

            QQC2.Label {
                Layout.fillWidth: true
                Layout.preferredHeight: content.unit * 4
                visible: !root.loading && !root.listingError && root.shownEntries.length === 0
                text: searchField.length > 0 ? i18n("No matching items") : i18n("No items to show")
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
                opacity: 0.7
            }

            QQC2.ScrollView {
                id: gridScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: Math.min(content.screenHeight * 0.48, Math.ceil(root.shownEntries.length / Math.max(1, grid.columns)) * grid.cellHeight)
                visible: root.viewMode === 2 && root.shownEntries.length > 0
                clip: true
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                GridView {
                    id: grid
                    readonly property int columns: Math.max(1, Math.floor(width / (content.unit * 5)))
                    cellWidth: width / columns
                    cellHeight: content.unit * 5
                    model: root.shownEntries
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    highlightMoveDuration: 0
                    currentIndex: 0
                    Keys.onReturnPressed: root.activateEntry(root.shownEntries[currentIndex])
                    Keys.onEnterPressed: root.activateEntry(root.shownEntries[currentIndex])
                    Keys.onEscapePressed: root.visible = false

                    delegate: QQC2.ItemDelegate {
                        id: gridEntry
                        required property var modelData
                        required property int index
                        width: grid.cellWidth
                        height: grid.cellHeight
                        text: modelData.name
                        highlighted: grid.currentIndex === index && grid.activeFocus
                        Accessible.name: text
                        onClicked: root.activateEntry(modelData)
                        onHoveredChanged: {
                            if (hovered) {
                                grid.currentIndex = index;
                            }
                        }
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        contentItem: ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing
                            Kirigami.Icon {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: Kirigami.Units.iconSizes.large
                                Layout.preferredHeight: Kirigami.Units.iconSizes.large
                                source: gridEntry.modelData.icon
                                fallback: root.applications ? "application-x-executable" : "unknown"
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: gridEntry.text
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            QQC2.ScrollView {
                id: listScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: Math.min(content.screenHeight * 0.48, root.shownEntries.length * content.unit * (root.viewMode === 1 ? 3 : 2.5))
                visible: root.viewMode !== 2 && root.shownEntries.length > 0
                clip: true
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                ListView {
                    id: list
                    model: root.shownEntries
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    currentIndex: 0
                    highlightMoveDuration: 0
                    Keys.onReturnPressed: root.activateEntry(root.shownEntries[currentIndex])
                    Keys.onEnterPressed: root.activateEntry(root.shownEntries[currentIndex])
                    Keys.onEscapePressed: root.visible = false

                    delegate: Item {
                        id: listEntry
                        required property var modelData
                        required property int index
                        readonly property bool fan: root.viewMode === 1
                        readonly property real fanInset: fan ? Math.sin((index + 1) / Math.max(1, list.count) * Math.PI * 0.5) * content.unit * 2 : 0
                        width: list.width
                        height: content.unit * (fan ? 3 : 2.5)

                        QQC2.ItemDelegate {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: root.edge === PlasmaCore.Types.RightEdge ? 0 : listEntry.fanInset
                            anchors.rightMargin: root.edge === PlasmaCore.Types.RightEdge ? listEntry.fanInset : 0
                            height: parent.height
                            text: listEntry.modelData.name
                            icon.name: listEntry.modelData.icon
                            icon.width: listEntry.fan ? Kirigami.Units.iconSizes.medium : Kirigami.Units.iconSizes.smallMedium
                            icon.height: listEntry.fan ? Kirigami.Units.iconSizes.medium : Kirigami.Units.iconSizes.smallMedium
                            highlighted: list.currentIndex === listEntry.index && list.activeFocus
                            Accessible.name: text
                            onClicked: root.activateEntry(listEntry.modelData)
                            onHoveredChanged: {
                                if (hovered) {
                                    list.currentIndex = listEntry.index;
                                }
                            }
                            QQC2.ToolTip.text: text
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        }
                    }
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                visible: root.overflowCount > 0
                text: root.applications ? i18np("%1 more application — narrow your search to see it", "%1 more applications — narrow your search to see them", root.overflowCount) : i18np("%1 more item — open the folder to see everything", "%1 more items — open the folder to see everything", root.overflowCount)
                wrapMode: Text.Wrap
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.75
            }

            QQC2.Button {
                Layout.fillWidth: true
                visible: root.folder
                icon.name: "system-file-manager"
                text: i18nc("@action:button", "Open in File Manager")
                onClicked: root.openFolder()
            }
        }
    }
}
