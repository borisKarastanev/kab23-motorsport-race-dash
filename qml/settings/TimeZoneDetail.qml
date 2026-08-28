import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// Settings > Device Settings > Date & Time.
//
// Three time sources: NTP + IP-geolocated zone (toggle on, needs internet), a
// manual region/city + date/time entry (no internet), and the RaceBox GPS UTC
// fix (no internet, no manual entry either — see TimeModel::syncFromGps()).
Item {
    id: root
    property var stackView: null

    Component.onCompleted: timeModel.acquire()
    Component.onDestruction: timeModel.release()

    // The page's drill-in row: label on the left, "›" on the right, pressed
    // fill, hairline divider — matching StatRow/ToggleRow's inset so the three
    // kinds of row stacked on this page line up with each other.
    //
    // Declared INSIDE the root object, not as a sibling before it: a sibling
    // `component` declaration is a syntax error QML reports only at load time,
    // which on the dev machine means a silent exit 255. Same placement and same
    // reason as CloudUplinkDetail.qml's ActionRow.
    component NavRow: Rectangle {
        id: navRow

        property string rowText: ""
        signal activated()

        Layout.fillWidth: true
        Layout.preferredHeight: 48
        color: navArea.pressed ? "#111111" : "#0d0d0d"

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: navRow.rowText
            color: "#cccccc"
            font.pixelSize: 12
            font.bold: true
            font.family: "monospace"
            font.letterSpacing: 1
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: "›"
            color: "#444444"
            font.pixelSize: 18
            font.family: "monospace"
        }

        MouseArea {
            id: navArea
            anchors.fill: parent
            onClicked: navRow.activated()
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

    // Flickable, matching CloudUplinkDetail: the manual section makes this
    // page's height change at runtime (toggle off / offline reveals it).
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: content
            width: flick.width
            spacing: 0

            StatRow { label: "CURRENT TIME"; value: timeModel.localTimeText }
            StatRow {
                label: "TIME ZONE"
                value: timeModel.timezoneId + "  " + timeModel.utcOffset
            }
            StatRow {
                label: "SYNC STATUS"
                value: timeModel.clockSynced ? "SYNCHRONIZED" : "NOT SYNCHRONIZED"
                valueColor: timeModel.clockSynced ? "#00cc44" : "#cc8800"
            }

            Item { Layout.fillWidth: true; Layout.preferredHeight: 16 }

            ToggleRow {
                label: "SYNCHRONIZE TIMEZONE"
                checked: timeModel.syncEnabled
                busy: timeModel.busy
                onToggled: timeModel.setSyncEnabled(!timeModel.syncEnabled)
            }

            Text {
                visible: !timeModel.online
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 12
                wrapMode: Text.WordWrap
                text: "NO INTERNET CONNECTION — SET THE TIME MANUALLY BELOW"
                color: "#cc8800"
                font.pixelSize: 10
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 1
            }

            Text {
                visible: timeModel.errorText !== ""
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 12
                wrapMode: Text.WordWrap
                text: timeModel.errorText
                color: "#cc4444"
                font.pixelSize: 10
                font.family: "monospace"
                font.letterSpacing: 1
            }

            // ── manual section ────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                visible: !timeModel.syncEnabled || !timeModel.online

                Item { Layout.fillWidth: true; Layout.preferredHeight: 16 }

                NavRow {
                    rowText: "TIME ZONE   " + timeModel.timezoneId
                    onActivated: root.stackView.push("qrc:/qml/SettingsDetail.qml", {
                        settingKey: "timezone-regions",
                        title: "TIME ZONE"
                    })
                }

                NavRow {
                    rowText: "SET DATE & TIME"
                    onActivated: dateTimeModal.visible = true
                }

                Item { Layout.fillWidth: true; Layout.preferredHeight: 16 }

                TextButton {
                    Layout.leftMargin: 16
                    visible: timeModel.gpsTimeValid
                    text: "SYNC FROM GPS"
                    onClicked: timeModel.syncFromGps()
                }

                Text {
                    visible: timeModel.gpsTimeValid
                    Layout.leftMargin: 16
                    Layout.topMargin: 6
                    text: "GPS UTC: " + timeModel.gpsTimeText
                    color: "#555555"
                    font.pixelSize: 9
                    font.family: "monospace"
                    font.letterSpacing: 1
                }

                Item { Layout.fillWidth: true; Layout.preferredHeight: 16 }
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.topMargin: 16
                Layout.bottomMargin: 16
                wrapMode: Text.WordWrap
                color: "#555555"
                font.pixelSize: 10
                font.family: "monospace"
                text: "Turning SYNCHRONIZE TIMEZONE on sends this device's IP address to a "
                      + "third-party service to detect its timezone."
            }
        }
    } // Flickable

    DateTimeModal {
        id: dateTimeModal
        anchors.fill: parent
        visible: false
        onCancelled: dateTimeModal.visible = false
        onSubmitted: function(y, mo, d, h, mi) {
            timeModel.setManualDateTime(y, mo, d, h, mi)
            dateTimeModal.visible = false
        }
    }
}
