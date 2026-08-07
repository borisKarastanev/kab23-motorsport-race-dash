import QtQuick 2.15
import RaceDash 1.0

// Settings > Device Settings > Network Connection > Wi-Fi.
Item {
    id: root
    property var stackView: null
    property bool expanded: false

    Component.onCompleted: networkModel.acquire()
    Component.onDestruction: networkModel.release()

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: mainColumn.height
        clip: true

        Column {
            id: mainColumn
            width: flick.width

            // ── Wi-Fi enable/disable toggle ──────────────────────
            ToggleRow {
                width: parent.width
                label: "WI-FI"
                checked: networkModel.wifiEnabled
                busy: networkModel.busy
                onToggled: networkModel.setWifiEnabled(!networkModel.wifiEnabled)
            }

            // ── NetworkManager unavailable notice ────────────────
            Text {
                visible: !networkModel.nmAvailable
                width: parent.width
                topPadding: 24
                horizontalAlignment: Text.AlignHCenter
                text: "NETWORKMANAGER NOT AVAILABLE"
                color: "#444444"
                font.pixelSize: 11
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 2
            }

            // ── Everything below requires NetworkManager ─────────
            Column {
                width: parent.width
                visible: networkModel.nmAvailable

                Text {
                    visible: !networkModel.wifiEnabled
                    width: parent.width
                    height: 120
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: "WI-FI IS OFF"
                    color: "#444444"
                    font.pixelSize: 12
                    font.bold: true
                    font.family: "monospace"
                    font.letterSpacing: 2
                }

                Column {
                    width: parent.width
                    visible: networkModel.wifiEnabled

                    // ── active connection row ─────────────────────
                    Rectangle {
                        id: activeRow
                        visible: networkModel.wifiConnected
                        width: parent.width
                        height: 60
                        color: activeRowArea.pressed ? "#111111" : "#0d0d0d"

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            WifiIcon { size: 14; color: "#cccccc"; anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                text: networkModel.wifiSsid
                                color: "#cccccc"
                                font.pixelSize: 12
                                font.bold: true
                                font.family: "monospace"
                            }
                        }

                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            Text { text: "✓"; color: "#00cc44"; font.pixelSize: 14; font.bold: true }
                            Text { text: root.expanded ? "▴" : "▾"; color: "#444444"; font.pixelSize: 12 }
                        }

                        MouseArea {
                            id: activeRowArea
                            anchors.fill: parent
                            onClicked: root.expanded = !root.expanded
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

                    Item {
                        width: parent.width
                        visible: networkModel.wifiConnected
                        height: root.expanded ? expandColumn.height : 0
                        clip: true
                        Behavior on height { NumberAnimation { duration: 150 } }

                        Column {
                            id: expandColumn
                            width: parent.width
                            StatRow { width: parent.width; label: "IP ADDRESS";  value: networkModel.wifiIp }
                            StatRow { width: parent.width; label: "SUBNET MASK"; value: networkModel.wifiSubnet }
                            StatRow { width: parent.width; label: "ROUTER";      value: networkModel.wifiGateway }
                        }
                    }

                    // ── connect failure banner ────────────────────
                    Text {
                        visible: networkModel.connectState === Net.ConnectFailed
                        width: parent.width
                        padding: 12
                        wrapMode: Text.WordWrap
                        text: networkModel.errorText
                        color: "#cc4444"
                        font.pixelSize: 10
                        font.family: "monospace"
                        font.letterSpacing: 1
                    }

                    // ── available networks ────────────────────────
                    Text {
                        width: parent.width
                        topPadding: 16
                        bottomPadding: 8
                        leftPadding: 16
                        text: "AVAILABLE NETWORKS"
                        color: "#444444"
                        font.pixelSize: 9
                        font.family: "monospace"
                        font.letterSpacing: 2
                    }

                    Repeater {
                        model: networkModel.networks

                        delegate: Rectangle {
                            id: netRow
                            width: parent.width
                            height: 48
                            color: netRowArea.pressed ? "#111111" : "#0d0d0d"

                            readonly property bool isPending: networkModel.connectState === Net.Connecting
                                                            && networkModel.pendingSsid === modelData.ssid

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.ssid
                                color: "#cccccc"
                                font.pixelSize: 12
                                font.family: "monospace"
                            }

                            Row {
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8

                                Text {
                                    visible: netRow.isPending
                                    text: "CONNECTING…"
                                    color: "#888888"
                                    font.pixelSize: 10
                                    font.family: "monospace"

                                    SequentialAnimation on opacity {
                                        running: netRow.isPending
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.3; duration: 600 }
                                        NumberAnimation { to: 1.0; duration: 600 }
                                    }
                                }

                                Text {
                                    visible: !netRow.isPending && modelData.known
                                    text: "SAVED"
                                    color: "#444444"
                                    font.pixelSize: 9
                                    font.family: "monospace"
                                    font.letterSpacing: 1
                                }

                                Text {
                                    visible: !netRow.isPending && modelData.secured
                                    text: "🔒"
                                    font.pixelSize: 11
                                }

                                Text {
                                    visible: !netRow.isPending
                                    text: modelData.signal >= 75 ? "▂▄▆█"
                                        : modelData.signal >= 50 ? "▂▄▆"
                                        : modelData.signal >= 25 ? "▂▄" : "▂"
                                    color: "#888888"
                                    font.pixelSize: 10
                                }
                            }

                            MouseArea {
                                id: netRowArea
                                anchors.fill: parent
                                enabled: !netRow.isPending
                                onClicked: networkModel.connectToNetwork(modelData.ssid)
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
            }
        }
    }

    WifiPasswordModal {}
}
