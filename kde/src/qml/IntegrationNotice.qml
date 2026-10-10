// SPDX-License-Identifier: GPL-2.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

QQC2.ToolButton {
    id: notice
    property string message: ""
    readonly property bool popupVisible: details.visible
    visible: message.length > 0
    z: 100
    icon.name: "dialog-warning"
    text: i18n("Dock integration needs attention")
    display: QQC2.AbstractButton.IconOnly
    Accessible.name: text
    QQC2.ToolTip.visible: hovered && !details.visible
    QQC2.ToolTip.text: message
    onClicked: {
        if (details.visible) details.visible = false;
        else Qt.callLater(() => { if (notice.visible) details.visible = true; });
    }
    PlasmaCore.Dialog {
        id: details
        // Clear the full dock thickness, even though the warning icon is small.
        visualParent: notice.parent
        type: PlasmaCore.Dialog.PopupMenu
        location: Plasmoid.location
        hideOnWindowDeactivate: true
        visible: false
        mainItem: ColumnLayout {
            focus: true
            width: Kirigami.Units.gridUnit * 22
            height: implicitHeight
            Kirigami.Heading { level: 3; text: notice.text; Layout.fillWidth: true; wrapMode: Text.Wrap }
            QQC2.Label { text: notice.message; Layout.fillWidth: true; wrapMode: Text.Wrap }
            QQC2.Button { text: i18n("Close"); onClicked: details.visible = false; Layout.alignment: Qt.AlignRight }
            Keys.onEscapePressed: details.visible = false
        }
    }
}
