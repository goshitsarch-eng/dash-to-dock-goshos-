// SPDX-License-Identifier: GPL-2.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

PlasmaCore.Dialog {
    id: dialog
    required property Task parentTask
    required property var modelIndex
    property var tasksRoot
    readonly property var taskData: parentTask?.model ?? null
    visualParent: parentTask
    type: PlasmaCore.Dialog.PopupMenu
    location: Plasmoid.location
    hideOnWindowDeactivate: true
    visible: false
    onParentTaskChanged: if (!parentTask && visible) visible = false

    onVisibleChanged: {
        if (visible) Qt.callLater(() => actionsButton.forceActiveFocus());
        else Qt.callLater(() => destroy());
    }
    readonly property Connections taskConnections: Connections {
        target: dialog.parentTask
        function onAppUpdatingChanged(): void { if (dialog.parentTask?.appUpdating) dialog.visible = false; }
        function onIsWindowChanged(): void {
            if (!dialog.parentTask?.isWindow && !dialog.taskData?.IsGroupParent) dialog.visible = false;
        }
    }
    readonly property Connections anchorConnections: Connections {
        target: dialog.visualParent
        function onVisibleChanged(): void { if (!dialog.visualParent?.visible) dialog.visible = false; }
    }
    mainItem: ColumnLayout {
        width: Math.max(Kirigami.Units.gridUnit * 16, previews.implicitWidth)
        height: implicitHeight
        Keys.onEscapePressed: dialog.visible = false
        RowLayout {
            Layout.fillWidth: true
            Kirigami.Heading {
                level: 3
                text: i18n("All Windows")
                Layout.fillWidth: true
            }
            QQC2.ToolButton {
                id: actionsButton
                icon.name: "application-menu"
                text: i18n("Application Actions")
                display: QQC2.AbstractButton.IconOnly
                Accessible.name: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: text
                onClicked: {
                    const task = dialog.parentTask;
                    dialog.visible = false;
                    if (task) task.showContextMenu({skipDefaultPreviews: true, visualParent: dialog.visualParent});
                }
            }
            QQC2.ToolButton {
                icon.name: "dialog-close"
                text: i18n("Close")
                display: QQC2.AbstractButton.IconOnly
                Accessible.name: text
                onClicked: dialog.visible = false
            }
        }
        ToolTipDelegate {
            id: previews
            Layout.fillWidth: true
            parentTask: dialog.parentTask
            rootIndex: dialog.modelIndex
            forcePreviews: true
            appName: dialog.taskData?.AppName ?? ""
            pidParent: dialog.taskData?.AppPid ?? 0
            windows: dialog.taskData?.WinIdList ?? []
            isGroup: dialog.taskData?.IsGroupParent ?? false
            icon: dialog.taskData?.decoration
            launcherUrl: dialog.taskData?.LauncherUrlWithoutIcon ?? ""
            isLauncher: false
            isMinimized: dialog.taskData?.IsMinimized ?? false
            display: dialog.taskData?.display ?? ""
            genericName: dialog.taskData?.GenericName ?? ""
            virtualDesktops: dialog.taskData?.VirtualDesktops ?? []
            isOnAllVirtualDesktops: dialog.taskData?.IsOnAllVirtualDesktops ?? false
            activities: dialog.taskData?.Activities ?? []
            isReadyForPainting: dialog.taskData?.Geometry?.width > 0
        }
    }
}
