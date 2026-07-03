import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../TimeFormat.js" as Fmt

Item {
    property var payload: ({})
    property int selectedLapIndex: bestLapIndex

    readonly property var lapMs: payload.lapMs || []
    readonly property var lapPaths: payload.lapPaths || []

    readonly property int bestLapMs: {
        if (lapMs.length === 0) return 0
        return Math.min(...lapMs)
    }

    readonly property int bestLapIndex: lapMs.length === 0 ? -1 : lapMs.indexOf(bestLapMs)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── laps + map (50/50) ───────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── lap list ──────────────────────────────────────
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                model: lapMs
                clip: true

                delegate: Item {
                    width: ListView.view.width
                    height: 40

                    readonly property bool isBest: index === bestLapIndex
                    readonly property bool isSelected: index === selectedLapIndex

                    Rectangle {
                        anchors.fill: parent
                        color: isSelected ? "#141414" : "#0d0d0d"

                        Rectangle {
                            visible: isSelected
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 3
                            color: "#1E88E5"
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            text: "LAP " + (index + 1)
                            color: isBest ? "#00cc44" : "#444444"
                            font.pixelSize: 10
                            font.family: "monospace"
                            font.letterSpacing: 1
                        }

                        Text {
                            anchors.centerIn: parent
                            text: Fmt.formatMs(modelData)
                            color: isBest ? "#00cc44" : "#cccccc"
                            font.pixelSize: 14
                            font.bold: isBest
                            font.family: "monospace"
                            font.letterSpacing: 1
                        }

                        Text {
                            visible: isBest
                            anchors.right: parent.right
                            anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            text: "★ BEST"
                            color: "#00cc44"
                            font.pixelSize: 9
                            font.family: "monospace"
                            font.letterSpacing: 1
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: selectedLapIndex = index
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

            // ── vertical divider ──────────────────────────────
            Rectangle {
                Layout.fillHeight: true
                width: 1
                color: "#2a2a2a"
            }

            // ── track map ─────────────────────────────────────
            TrackMap {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                path: (selectedLapIndex >= 0 && selectedLapIndex < lapPaths.length)
                      ? lapPaths[selectedLapIndex] : []
                finishLat: dashConfig.finishLineLat
                finishLon: dashConfig.finishLineLon
            }
        }

        // ── divider ───────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#2a2a2a"
        }

        // ── stats block ───────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Repeater {
                model: [
                    { label: "TOP SPEED",    value: (payload.topSpeedKmh || 0) + " km/h" },
                    { label: "MAX OIL TEMP", value: (payload.maxOilC     || 0).toFixed(1) + " °C" },
                    { label: "MAX COOLANT",  value: (payload.maxCoolantC || 0).toFixed(1) + " °C" }
                ]

                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 36
                    color: "#0d0d0d"

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        color: "#444444"
                        font.pixelSize: 9
                        font.family: "monospace"
                        font.letterSpacing: 2
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.value
                        color: "#cccccc"
                        font.pixelSize: 12
                        font.bold: true
                        font.family: "monospace"
                        font.letterSpacing: 1
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
