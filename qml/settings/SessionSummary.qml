import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../TimeFormat.js" as Fmt

Item {
    property var payload: ({})

    readonly property int bestLapMs: {
        const laps = payload.lapMs || []
        if (laps.length === 0) return 0
        return Math.min(...laps)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── lap list ──────────────────────────────────────────
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: payload.lapMs || []
            clip: true

            delegate: Item {
                width: ListView.view.width
                height: 40

                readonly property bool isBest: modelData == bestLapMs

                Rectangle {
                    anchors.fill: parent
                    color: "#0d0d0d"

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
