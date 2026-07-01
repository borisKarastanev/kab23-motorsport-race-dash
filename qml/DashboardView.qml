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

    function zoneHasContent(zone) {
        if (zone === "center" && dashConfig.gearVisible) return true
        return (dashConfig.rpmVisible     && dashConfig.rpmPosition     === zone) ||
               (dashConfig.speedVisible   && dashConfig.speedPosition   === zone) ||
               (dashConfig.coolantVisible && dashConfig.coolantPosition === zone) ||
               (dashConfig.oilTempVisible && dashConfig.oilTempPosition === zone)
    }

    Component {
        id: rpmComp
        Gauge {
            anchors.fill: parent
            label: "RPM"
            value: dataModel.rpm
            maxValue: 8000
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
            warningThreshold: 95
            dangerThreshold: 105
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
            warningThreshold: 120
            dangerThreshold: 135
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
            visible: zoneHasContent("left") || zoneHasContent("center") || zoneHasContent("right") || dashConfig.lapTimerVisible

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                visible: zoneHasContent("left") || dashConfig.lapTimerVisible

                Gauge {
                    visible: dashConfig.rpmVisible && dashConfig.rpmPosition === "left"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 105
                    label: "RPM"
                    value: dataModel.rpm
                    maxValue: 8000
                    compact: true
                }
                Loader { active: dashConfig.speedVisible   && dashConfig.speedPosition   === "left"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: speedComp }
                Loader { active: dashConfig.coolantVisible && dashConfig.coolantPosition === "left"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: coolantComp }
                Loader { active: dashConfig.oilTempVisible && dashConfig.oilTempPosition === "left"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: oilTempComp }

                LapTimer {
                    visible: dashConfig.lapTimerVisible
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                visible: zoneHasContent("center")

                GearIndicator {
                    visible: dashConfig.gearVisible
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    gear: dataModel.gear
                }

                Loader { active: dashConfig.rpmVisible     && dashConfig.rpmPosition     === "center"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: rpmComp }
                Loader { active: dashConfig.speedVisible   && dashConfig.speedPosition   === "center"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: speedComp }
                Loader { active: dashConfig.coolantVisible && dashConfig.coolantPosition === "center"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: coolantComp }
                Loader { active: dashConfig.oilTempVisible && dashConfig.oilTempPosition === "center"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: oilTempComp }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                visible: zoneHasContent("right")

                Loader { active: dashConfig.rpmVisible     && dashConfig.rpmPosition     === "right"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: rpmComp }
                Loader { active: dashConfig.speedVisible   && dashConfig.speedPosition   === "right"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: speedComp }
                Loader { active: dashConfig.coolantVisible && dashConfig.coolantPosition === "right"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: coolantComp }
                Loader { active: dashConfig.oilTempVisible && dashConfig.oilTempPosition === "right"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: oilTempComp }
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
}
