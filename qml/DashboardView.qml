pragma ComponentBehavior: Bound
import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: dashboardRoot

    signal openSettingsRequested()

    // ── right-edge swipe → settings ──────────────────────────
    // 40 px strip along the right edge. DragHandler sits above the ColumnLayout
    // in the item tree so it intercepts horizontal drags without blocking taps
    // on dashboard controls below.
    Item {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 40
        z: 10

        DragHandler {
            target: null
            yAxis.enabled: false

            property real startX: 0

            onActiveChanged: {
                if (active) {
                    startX = centroid.position.x
                } else {
                    // threshold: 40 px leftward drag from the edge strip
                    if (startX - centroid.position.x > 40)
                        openSettingsRequested()
                }
            }
        }
    }

    // Maps a 9-cell position token (e.g. "center-left") to its row/column
    // index (0/1/2). DashConfig guarantees at most one visible entity per
    // token (positions are assigned via swap, never left colliding).
    function rowForPosition(pos) {
        if (pos.indexOf("top") === 0)    return 0
        if (pos.indexOf("bottom") === 0) return 2
        return 1
    }
    function colForPosition(pos) {
        if (pos.indexOf("left") !== -1)  return 0
        if (pos.indexOf("right") !== -1) return 2
        return 1
    }

    // The three column lanes (index 0=left, 1=center, 2=right), each an array
    // of visible entity keys ordered top-to-bottom by row. Each lane below is
    // a plain ColumnLayout over its array — with 0-3 entities per lane, that
    // makes ColumnLayout's own fillHeight distribution do exactly what we
    // want for free: one entity gets the full lane height, two split it
    // evenly, three split it three ways. No manual height fractions needed,
    // and a lane with nothing in it is just an empty array.
    //
    // Computed once per layout change into this single property (reading every
    // dashConfig visible/position property so the binding depends on all of
    // them), rather than a per-lane function the bindings would call ~9 times
    // per change.
    readonly property var lanes: {
        const defs = [
            { key: "rpm",      visible: dashConfig.rpmVisible,      pos: dashConfig.rpmPosition },
            { key: "speed",    visible: dashConfig.speedVisible,    pos: dashConfig.speedPosition },
            { key: "coolant",  visible: dashConfig.coolantVisible,  pos: dashConfig.coolantPosition },
            { key: "oiltemp",  visible: dashConfig.oilTempVisible,  pos: dashConfig.oilTempPosition },
            { key: "gear",     visible: dashConfig.gearVisible,     pos: dashConfig.gearPosition },
            { key: "laptimer", visible: dashConfig.lapTimerVisible, pos: dashConfig.lapTimerPosition },
        ]
        const out = [[], [], []]
        for (const d of defs)
            if (d.visible)
                out[dashboardRoot.colForPosition(d.pos)].push(d)
        for (const lane of out)
            lane.sort((a, b) => dashboardRoot.rowForPosition(a.pos) - dashboardRoot.rowForPosition(b.pos))
        return [out[0].map(d => d.key), out[1].map(d => d.key), out[2].map(d => d.key)]
    }

    function componentFor(key) {
        switch (key) {
            case "rpm":      return rpmComp
            case "speed":    return speedComp
            case "coolant":  return coolantComp
            case "oiltemp":  return oilTempComp
            case "gear":     return gearComp
            case "laptimer": return lapTimerComp
        }
        return null
    }

    Component {
        id: rpmComp
        Gauge {
            anchors.fill: parent
            label: "RPM"
            value: dataModel.rpm
            compact: true
        }
    }

    Component {
        id: speedComp
        Gauge {
            anchors.fill: parent
            label: "SPEED"
            unit: "km/h"
            value: dataModel.speed
            maxValue: 240
        }
    }

    Component {
        id: coolantComp
        Gauge {
            anchors.fill: parent
            compact: true
            label: "COOLANT"
            unit: "°C"
            value: dataModel.coolantTemp
            maxValue: 120
            decimalPlaces: 1
            warningThreshold: dashConfig.coolantWarningTemp
            dangerThreshold: dashConfig.coolantDangerTemp
        }
    }

    Component {
        id: oilTempComp
        Gauge {
            anchors.fill: parent
            compact: true
            label: "OIL TEMP"
            unit: "°C"
            value: dataModel.oilTemp
            maxValue: 150
            decimalPlaces: 1
            warningThreshold: dashConfig.oilWarningTemp
            dangerThreshold: dashConfig.oilDangerTemp
        }
    }

    Component {
        id: gearComp
        GearIndicator {
            anchors.fill: parent
            gear: dataModel.gear
        }
    }

    Component {
        id: lapTimerComp
        LapTimer {
            anchors.fill: parent
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        LedStrip {
            Layout.fillWidth: true
            height: 26
            rpm:             dataModel.rpm
            ledCount:        dashConfig.ledCount
            flashIntervalMs: dashConfig.flashIntervalMs
            pair0Rpm:        dashConfig.pair0Rpm
            pair1Rpm:        dashConfig.pair1Rpm
            pair2Rpm:        dashConfig.pair2Rpm
            pair3Rpm:        dashConfig.pair3Rpm
            pair4Rpm:        dashConfig.pair4Rpm
            allBlueRpm:      dashConfig.allBlueRpm
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            visible: dashboardRoot.lanes[0].length > 0 || dashboardRoot.lanes[1].length > 0
                     || dashboardRoot.lanes[2].length > 0

            // Three independent lanes (left/center/right), each a plain
            // ColumnLayout over just the entities assigned to it. A lane
            // with one entity gives it the full lane height; two split it
            // evenly; three split it three ways — ColumnLayout does that
            // for free, so a sparsely-populated lane never leaves a gauge
            // stranded in a fraction of the space. An empty lane collapses
            // entirely and its width goes to the remaining lanes.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                visible: dashboardRoot.lanes[0].length > 0

                Repeater {
                    model: dashboardRoot.lanes[0]
                    delegate: Loader {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        sourceComponent: dashboardRoot.componentFor(modelData)
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                visible: dashboardRoot.lanes[1].length > 0

                Repeater {
                    model: dashboardRoot.lanes[1]
                    delegate: Loader {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        sourceComponent: dashboardRoot.componentFor(modelData)
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                visible: dashboardRoot.lanes[2].length > 0

                Repeater {
                    model: dashboardRoot.lanes[2]
                    delegate: Loader {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        sourceComponent: dashboardRoot.componentFor(modelData)
                    }
                }
            }
        }

        StatusBar {
            id: statusBar
            Layout.fillWidth: true
            height: 32
            onResetFinishLineRequested: finishResetOverlay.visible = true
        }
    }

    // ── finish line reset confirmation overlay ──────────────
    Rectangle {
        id: finishResetOverlay
        anchors.fill: parent
        z: 100
        visible: false
        color: "#cc000000"

        Rectangle {
            anchors.centerIn: parent
            width: 280
            height: 130
            color: "#0d0d0d"
            border.color: "#2a2a2a"
            border.width: 1
            radius: 2

            Column {
                anchors.centerIn: parent
                spacing: 18

                Text {
                    id: overlayMessage
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "RESET FINISH LINE?"
                    color: "#666666"
                    font.pixelSize: 11
                    font.family: "monospace"
                    font.letterSpacing: 2
                }

                Row {
                    id: overlayButtons
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12

                    Rectangle {
                        width: 110; height: 26
                        color: confirmArea.pressed ? "#0a1a0a" : "transparent"
                        border.color: "#2a4a2a"
                        border.width: 1
                        radius: 2
                        Text {
                            anchors.centerIn: parent
                            text: "CONFIRM"
                            color: "#00cc44"
                            font.pixelSize: 10
                            font.family: "monospace"
                            font.letterSpacing: 2
                        }
                        MouseArea {
                            id: confirmArea
                            anchors.fill: parent
                            onClicked: {
                                raceBoxModel.clearFinishLine()
                                overlayMessage.text = "FINISH LINE CLEARED"
                                overlayButtons.visible = false
                                successTimer.start()
                            }
                        }
                    }

                    Rectangle {
                        width: 110; height: 26
                        color: cancelArea.pressed ? "#111111" : "transparent"
                        border.color: "#333333"
                        border.width: 1
                        radius: 2
                        Text {
                            anchors.centerIn: parent
                            text: "CANCEL"
                            color: "#555555"
                            font.pixelSize: 10
                            font.family: "monospace"
                            font.letterSpacing: 2
                        }
                        MouseArea {
                            id: cancelArea
                            anchors.fill: parent
                            onClicked: finishResetOverlay.visible = false
                        }
                    }
                }
            }
        }

        Timer {
            id: successTimer
            interval: 1500
            onTriggered: {
                finishResetOverlay.visible = false
                overlayMessage.text = "RESET FINISH LINE?"
                overlayButtons.visible = true
            }
        }
    }

    // ── nearest-track suggestion overlay ─────────────────────
    // Suggest-and-confirm: TrackModel's GPS auto-detect never selects a track
    // silently — it only proposes one here, mirroring finishResetOverlay above.
    Rectangle {
        id: trackSuggestOverlay
        anchors.fill: parent
        z: 90
        visible: trackModel.suggestedTrackId !== "" && !finishResetOverlay.visible
        color: "#cc000000"

        Rectangle {
            anchors.centerIn: parent
            width: 280
            height: 130
            color: "#0d0d0d"
            border.color: "#2a2a2a"
            border.width: 1
            radius: 2

            Column {
                anchors.centerIn: parent
                spacing: 18
                width: parent.width - 32

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: "YOU ARE NEAR\n" + trackModel.suggestedTrackName
                          + "\nSELECT FOR CURRENT SESSION?"
                    color: "#666666"
                    font.pixelSize: 11
                    font.family: "monospace"
                    font.letterSpacing: 1
                    wrapMode: Text.WordWrap
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12

                    Rectangle {
                        width: 110; height: 26
                        color: acceptTrackArea.pressed ? "#0a1a0a" : "transparent"
                        border.color: "#2a4a2a"
                        border.width: 1
                        radius: 2
                        Text {
                            anchors.centerIn: parent
                            text: "CONFIRM"
                            color: "#00cc44"
                            font.pixelSize: 10
                            font.family: "monospace"
                            font.letterSpacing: 2
                        }
                        MouseArea {
                            id: acceptTrackArea
                            anchors.fill: parent
                            onClicked: trackModel.acceptSuggestedTrack()
                        }
                    }

                    Rectangle {
                        width: 110; height: 26
                        color: dismissTrackArea.pressed ? "#111111" : "transparent"
                        border.color: "#333333"
                        border.width: 1
                        radius: 2
                        Text {
                            anchors.centerIn: parent
                            text: "CANCEL"
                            color: "#555555"
                            font.pixelSize: 10
                            font.family: "monospace"
                            font.letterSpacing: 2
                        }
                        MouseArea {
                            id: dismissTrackArea
                            anchors.fill: parent
                            onClicked: trackModel.dismissSuggestedTrack()
                        }
                    }
                }
            }
        }
    }
}
