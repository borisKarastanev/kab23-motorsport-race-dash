import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    color: "#111111"
    border.color: "#2a2a2a"
    border.width: 1
    radius: 3

    function formatSubSecond(totalMs) {
        const s = Math.floor(totalMs / 1000)
        const t = totalMs % 1000
        return s + "." + String(t).padStart(3, "0")
    }

    function formatMs(ms) {
        if (ms <= 0) return "--:--.---"
        const m = Math.floor(ms / 60000)
        return m + ":" + String(Math.floor((ms % 60000) / 1000)).padStart(2, "0")
               + "." + String(ms % 1000).padStart(3, "0")
    }

    function formatDelta(ms) {
        const sign = ms <= 0 ? "-" : "+"
        return sign + formatSubSecond(Math.abs(ms))
    }

    GridLayout {
        anchors.fill: parent
        anchors.margins: 6
        columns: 2
        rowSpacing: 4
        columnSpacing: 8

        // ── top-left: lap counter ──────────────────────────────
        ColumnLayout {
            spacing: 1
            Text {
                text: "LAP"
                color: "#555555"
                font.pixelSize: 9
                font.letterSpacing: 2
            }
            Text {
                text: raceBoxModel.lapNumber > 0 ? raceBoxModel.lapNumber : "--"
                color: "#ffffff"
                font.pixelSize: 34
                font.bold: true
                font.family: "monospace"
            }
        }

        // ── top-right: current lap + delta ────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            Text {
                text: "CURRENT"
                color: "#555555"
                font.pixelSize: 9
                font.letterSpacing: 2
            }
            Text {
                text: formatMs(raceBoxModel.currentLapMs)
                color: "#ffffff"
                font.pixelSize: 22
                font.bold: true
                font.family: "monospace"
            }

            // Delta vs best lap — only shown once a best exists
            Text {
                visible: raceBoxModel.bestLapMs > 0
                readonly property int deltaMs: raceBoxModel.currentLapMs - raceBoxModel.bestLapMs
                text: formatDelta(deltaMs)
                color: deltaMs <= 0 ? "#00cc44" : "#cc2222"
                font.pixelSize: 16
                font.bold: true
                font.family: "monospace"
            }
        }

        // ── bottom-left: last lap ──────────────────────────────
        ColumnLayout {
            spacing: 1
            Text {
                text: "LAST"
                color: "#555555"
                font.pixelSize: 9
                font.letterSpacing: 2
            }
            Text {
                text: formatMs(raceBoxModel.lastLapMs)
                color: {
                    if (raceBoxModel.lastLapMs <= 0) return "#555555"
                    if (raceBoxModel.bestLapMs > 0 && raceBoxModel.lastLapMs === raceBoxModel.bestLapMs)
                        return "#00cc44"
                    return "#cc2222"
                }
                font.pixelSize: 18
                font.bold: true
                font.family: "monospace"
            }
        }

        // ── bottom-right: best lap ─────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                text: "BEST ★"
                color: raceBoxModel.bestLapMs > 0 ? "#00cc44" : "#555555"
                font.pixelSize: 9
                font.letterSpacing: 2
            }
            Text {
                text: formatMs(raceBoxModel.bestLapMs)
                color: raceBoxModel.bestLapMs > 0 ? "#00cc44" : "#555555"
                font.pixelSize: 18
                font.bold: true
                font.family: "monospace"
            }
        }
    }
}
