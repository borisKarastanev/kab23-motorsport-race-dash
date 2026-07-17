import QtQuick 2.15
import QtQuick.Layouts 1.15

// One card in the Dashboard Layout screen: title + enabled toggle, a
// position row with a MODIFY button that opens PositionPickerModal, and an
// optional slot (default property) for extra controls like StepperRow.
Rectangle {
    id: card

    property string entityKey: ""
    property string title: ""
    property bool enabledValue: false
    property string position: ""
    property bool showPosition: true

    default property alias contentChildren: extraColumn.data

    signal enabledToggled(bool value)
    signal modifyRequested()

    width: parent ? parent.width : 300
    height: contentColumn.height + 32
    color: "#0d0d0d"
    border.color: "#2a2a2a"
    border.width: 1
    radius: 2

    ColumnLayout {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: card.title
                color: "#cccccc"
                font.pixelSize: 12
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 2
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                id: toggleTrack
                Layout.preferredWidth: 46
                Layout.preferredHeight: 24
                radius: 12
                color: card.enabledValue ? "#00cc44" : "#2a2a2a"
                border.color: "#1a1a1a"
                border.width: 1

                Rectangle {
                    width: 18; height: 18
                    radius: 9
                    anchors.verticalCenter: parent.verticalCenter
                    color: "#0d0d0d"
                    x: card.enabledValue ? (parent.width - width - 3) : 3
                    Behavior on x { NumberAnimation { duration: 150 } }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: card.enabledToggled(!card.enabledValue)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: card.showPosition
            spacing: 10

            Text {
                text: "POSITION"
                color: "#444444"
                font.pixelSize: 9
                font.family: "monospace"
                font.letterSpacing: 2
            }

            Text {
                Layout.fillWidth: true
                text: card.position
                color: "#888888"
                font.pixelSize: 11
                font.family: "monospace"
            }

            TextButton {
                text: "MODIFY"
                textColor: "#4488cc"
                pressedColor: "#0a1a2a"
                border.color: "#2a3a4a"
                onClicked: card.modifyRequested()
            }
        }

        ColumnLayout {
            id: extraColumn
            Layout.fillWidth: true
            spacing: 10
        }
    }
}
