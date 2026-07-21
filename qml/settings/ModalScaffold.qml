import QtQuick 2.15

// Shared full-screen modal scaffold: dim backdrop + centered dark panel with
// click-absorbing MouseAreas. Callers use this as their root type and
// declare the panel body as plain children — routed into the content Column
// via the default property, e.g.:
//
//   ModalScaffold {
//       maxPanelWidth: 360
//       Text { ... }
//       Row { TextButton {...} TextButton {...} }
//   }
//
// Extracted from the near-identical scaffolds duplicated across
// PasswordEntryModal.qml and ConfirmModal.qml (TICKET-modal-scaffold-extraction).
// The scaffold literals (#0d0d0d panel, #2a2a2a border, radius/margins/spacing)
// are fixed house style; only the panel's width cap varies between callers, so
// maxPanelWidth is the single knob. PositionPickerModal keeps its own scaffold
// (see the note there) rather than forcing extra configurability in here.
Item {
    id: root
    anchors.fill: parent
    visible: false
    z: 100

    default property alias content: contentColumn.data

    property int maxPanelWidth: 560   // panel = min(this, width - 40)

    // dim backdrop — swallows all clicks behind the modal
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.7

        MouseArea {
            anchors.fill: parent
            onClicked: {} // absorb clicks
        }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(root.maxPanelWidth, root.width - 40)
        height: contentColumn.height + 32
        color: "#0d0d0d"
        border.color: "#2a2a2a"
        border.width: 1
        radius: 3

        MouseArea { anchors.fill: parent; onClicked: {} } // absorb clicks

        Column {
            id: contentColumn
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 16
            spacing: 12
        }
    }
}
