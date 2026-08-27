import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../TimeFormat.js" as Fmt

Item {
    property var payload: ({})
    property int selectedLapIndex: bestLapIndex

    readonly property var lapMs: payload.lapMs || []
    readonly property var lapPaths: payload.lapPaths || []

    // Finish line for this session's track (falls back to the global line).
    readonly property var finishLine: trackModel.finishLineFor(payload.trackId || "")

    readonly property int bestLapMs: {
        if (lapMs.length === 0) return 0
        return Math.min(...lapMs)
    }

    readonly property int bestLapIndex: lapMs.length === 0 ? -1 : lapMs.indexOf(bestLapMs)

    // Best-sector-stitch optimal lap, computed and persisted at save time (see
    // SessionModel::saveCurrentSession()) — a property of this completed
    // session, not something recomputed here. Absent (0) on a session that had
    // fewer than two laps with a complete set of sector splits, including any
    // session saved before sector gates existed.
    readonly property int optimalLapMs: payload.optimalLapMs || 0

    // Same blue TrackMap.qml strokes the lap line with — ties this readout to
    // the line it's a time for, and matches the .isSelected accent above.
    readonly property color lineColor: "#1E88E5"

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
                finishLat1: finishLine.lat1 || 0
                finishLon1: finishLine.lon1 || 0
                finishLat2: finishLine.lat2 || 0
                finishLon2: finishLine.lon2 || 0
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
                    { label: "TRACK",        value: payload.trackName || "—" },
                    // Only present once the session recorded enough sectored
                    // laps for an optimal lap to exist (see optimalLapMs).
                    ...(optimalLapMs > 0
                        ? [{ label: "OPTIMAL LAP", value: Fmt.formatMs(optimalLapMs),
                             valueColor: lineColor, star: true }]
                        : []),
                    { label: "TOP SPEED",    value: (payload.topSpeedKmh || 0) + " km/h" },
                    { label: "MAX LATERAL/MAX LONGITUDINAL",
                      value: (payload.maxLatG !== undefined && payload.maxLonG !== undefined)
                             ? payload.maxLatG.toFixed(2) + "/" + payload.maxLonG.toFixed(2) + " g"
                             : "—" },
                    { label: "MAX OIL TEMP", value: (payload.maxOilC     || 0).toFixed(1) + " °C" },
                    { label: "MAX COOLANT",  value: (payload.maxCoolantC || 0).toFixed(1) + " °C" }
                ]

                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 36
                    color: "#0d0d0d"

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4

                        Text {
                            text: modelData.label
                            color: "#444444"
                            font.pixelSize: 9
                            font.family: "monospace"
                            font.letterSpacing: 2
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        // Same favorite-star glyph/color as TracksBrowser.qml's
                        // favoriteBtn — ties this row to "the one you've marked
                        // as best", the same visual language used elsewhere.
                        Text {
                            visible: modelData.star === true
                            text: "★"
                            color: "#ffcc00"
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.value
                        color: modelData.valueColor || "#cccccc"
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
