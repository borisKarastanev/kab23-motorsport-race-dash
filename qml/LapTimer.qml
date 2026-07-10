import QtQuick 2.15
import QtQuick.Layouts 1.15
import RaceDash 1.0
import "TimeFormat.js" as Fmt

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
                text: Fmt.formatMs(raceBoxModel.currentLapMs)
                color: "#ffffff"
                font.pixelSize: 22
                font.bold: true
                font.family: "monospace"
            }

            // Live delta vs best lap at the same distance traveled — only
            // shown once a best exists (see RaceBoxModel::currentDeltaMs).
            Text {
                visible: raceBoxModel.bestLapMs > 0
                text: formatDelta(raceBoxModel.currentDeltaMs)
                color: raceBoxModel.currentDeltaMs <= 0 ? "#00cc44" : "#cc2222"
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
                text: Fmt.formatMs(raceBoxModel.lastLapMs)
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
                text: Fmt.formatMs(raceBoxModel.bestLapMs)
                color: raceBoxModel.bestLapMs > 0 ? "#00cc44" : "#555555"
                font.pixelSize: 18
                font.bold: true
                font.family: "monospace"
            }
        }
    }

    // Waiting overlay — pulsing prompt shown only while the timer is Armed (first
    // crossing of this run). After a Save Session reset the state is Standby, so
    // the prompt stays hidden and the panel shows plain "--:--" instead.
    Rectangle {
        id: waitingOverlay
        visible: raceBoxModel.lapTimerState === RaceBox.Armed
        anchors.fill: parent
        color: parent.color
        radius: parent.radius
        z: 1

        Text {
            anchors.centerIn: parent
            text: "CROSS S/F LINE\nTO START TIMER"
            color: "#558855"
            font.pixelSize: 12
            font.family: "monospace"
            font.letterSpacing: 2
            horizontalAlignment: Text.AlignHCenter

            SequentialAnimation on opacity {
                // Was hardcoded `running: true` — ran this infinite animation
                // (forcing a scene-graph sync + repaint every frame) from app
                // startup regardless of whether the overlay was ever shown,
                // since an item's own `visible` doesn't track an ancestor's.
                running: waitingOverlay.visible
                loops: Animation.Infinite
                NumberAnimation { to: 0.3; duration: 1000; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 1000; easing.type: Easing.InOutSine }
            }
        }
    }
}
