import QtQuick 2.15
import QtQuick.Layouts 1.15

// Settings > Dashboard Layout. One card per positionable dashboard entity;
// each card's controls write straight to dashConfig, which the live
// dashboard (DashboardView.qml) binds to directly — changes apply
// immediately, no restart.
Item {
    id: root

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: mainColumn.y + mainColumn.height + 16
        clip: true

        Column {
            id: mainColumn
            x: 16
            y: 16
            width: flick.width - 32
            spacing: 12

            LayoutCard {
                entityKey: "rpm"
                title: "RPM"
                enabledValue: dashConfig.rpmVisible
                position: dashConfig.rpmPosition
                onEnabledToggled: (value) => dashConfig.rpmVisible = value
                onModifyRequested: positionModal.open("rpm", "RPM")

                StepperRow {
                    label: "LIMITER"
                    value: dashConfig.limiterRpm + " RPM"
                    onDecrementRequested: dashConfig.limiterRpm -= 100
                    onIncrementRequested: dashConfig.limiterRpm += 100
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: "PAIR " + dashConfig.pair0Rpm + " / " + dashConfig.pair1Rpm + " / "
                              + dashConfig.pair2Rpm + " / " + dashConfig.pair3Rpm + " / " + dashConfig.pair4Rpm
                              + "  ·  ALL-BLUE " + dashConfig.allBlueRpm
                        color: "#555555"
                        font.pixelSize: 8
                        font.family: "monospace"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            LayoutCard {
                entityKey: "coolant"
                title: "COOLANT"
                enabledValue: dashConfig.coolantVisible
                position: dashConfig.coolantPosition
                onEnabledToggled: (value) => dashConfig.coolantVisible = value
                onModifyRequested: positionModal.open("coolant", "COOLANT")

                StepperRow {
                    label: "WARNING °C"
                    value: dashConfig.coolantWarningTemp.toFixed(0)
                    onDecrementRequested: dashConfig.coolantWarningTemp -= 1
                    onIncrementRequested: dashConfig.coolantWarningTemp += 1
                }
                StepperRow {
                    label: "DANGER °C"
                    value: dashConfig.coolantDangerTemp.toFixed(0)
                    onDecrementRequested: dashConfig.coolantDangerTemp -= 1
                    onIncrementRequested: dashConfig.coolantDangerTemp += 1
                }
            }

            LayoutCard {
                entityKey: "laptimer"
                title: "LAP TIMER"
                enabledValue: dashConfig.lapTimerVisible
                position: dashConfig.lapTimerPosition
                onEnabledToggled: (value) => dashConfig.lapTimerVisible = value
                onModifyRequested: positionModal.open("laptimer", "LAP TIMER")
            }

            LayoutCard {
                entityKey: "speed"
                title: "SPEED"
                enabledValue: dashConfig.speedVisible
                position: dashConfig.speedPosition
                onEnabledToggled: (value) => dashConfig.speedVisible = value
                onModifyRequested: positionModal.open("speed", "SPEED")
            }

            LayoutCard {
                entityKey: "gear"
                title: "GEAR"
                enabledValue: dashConfig.gearVisible
                position: dashConfig.gearPosition
                onEnabledToggled: (value) => dashConfig.gearVisible = value
                onModifyRequested: positionModal.open("gear", "GEAR")
            }

            LayoutCard {
                entityKey: "oiltemp"
                title: "OIL TEMP"
                enabledValue: dashConfig.oilTempVisible
                position: dashConfig.oilTempPosition
                onEnabledToggled: (value) => dashConfig.oilTempVisible = value
                onModifyRequested: positionModal.open("oiltemp", "OIL TEMP")

                StepperRow {
                    label: "WARNING °C"
                    value: dashConfig.oilWarningTemp.toFixed(0)
                    onDecrementRequested: dashConfig.oilWarningTemp -= 1
                    onIncrementRequested: dashConfig.oilWarningTemp += 1
                }
                StepperRow {
                    label: "DANGER °C"
                    value: dashConfig.oilDangerTemp.toFixed(0)
                    onDecrementRequested: dashConfig.oilDangerTemp -= 1
                    onIncrementRequested: dashConfig.oilDangerTemp += 1
                }
            }
        }
    }

    PositionPickerModal {
        id: positionModal
    }
}
