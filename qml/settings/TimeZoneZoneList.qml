import QtQuick 2.15

// Settings > Device Settings > Date & Time > Time Zone > <Region> — city list.
Item {
    id: root
    property var stackView: null
    property var payload: ({})

    readonly property string region: payload.region || ""

    ListView {
        anchors.fill: parent
        model: timeModel.zonesForRegion(root.region)
        clip: true
        opacity: timeModel.busy ? 0.5 : 1.0

        delegate: Item {
            id: delegateRoot
            width: ListView.view.width
            height: 48

            readonly property string fullId: root.region + "/" + modelData
            readonly property bool isActive: fullId === timeModel.timezoneId

            Rectangle {
                anchors.fill: parent
                color: rowArea.pressed ? "#111111" : "#0d0d0d"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData
                    color: delegateRoot.isActive ? "#00cc44" : "#cccccc"
                    font.pixelSize: 12
                    font.bold: delegateRoot.isActive
                    font.family: "monospace"
                    font.letterSpacing: 1
                }

                Text {
                    visible: delegateRoot.isActive
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: "✓"
                    color: "#00cc44"
                    font.pixelSize: 14
                    font.bold: true
                }

                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    // TimeModel::setTimezone early-returns while another
                    // timedatectl call is in flight (up to 8 s — e.g. the
                    // set-ntp fired by the toggle on the page below). Popping
                    // regardless would drop the tap and still navigate away as
                    // if it had worked, so the list refuses taps while busy and
                    // dims to say so.
                    enabled: !timeModel.busy
                    onClicked: {
                        timeModel.setTimezone(delegateRoot.fullId)
                        root.stackView.pop()
                        root.stackView.pop()
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                height: 1
                color: "#1a1a1a"
            }
        }
    }
}
