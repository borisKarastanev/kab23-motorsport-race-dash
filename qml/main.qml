import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15

Window {
    width: 800
    height: 480
    visible: true
    title: "BMW E46 Dashboard"
    color: "#0a0a0a"

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
            visible: zoneHasContent("left") || zoneHasContent("center") || zoneHasContent("right")

            ColumnLayout {
                id: leftPanel
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                visible: zoneHasContent("left")

                Loader { active: dashConfig.rpmVisible     && dashConfig.rpmPosition     === "left"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: rpmComp }
                Loader { active: dashConfig.speedVisible   && dashConfig.speedPosition   === "left"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: speedComp }
                Loader { active: dashConfig.coolantVisible && dashConfig.coolantPosition === "left"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: coolantComp }
                Loader { active: dashConfig.oilTempVisible && dashConfig.oilTempPosition === "left"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: oilTempComp }
            }

            ColumnLayout {
                id: centerPanel
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
                id: rightPanel
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

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 160
            spacing: 8
            visible: zoneHasContent("bottom")

            Loader { active: dashConfig.rpmVisible     && dashConfig.rpmPosition     === "bottom"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: rpmComp }
            Loader { active: dashConfig.speedVisible   && dashConfig.speedPosition   === "bottom"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: speedComp }
            Loader { active: dashConfig.coolantVisible && dashConfig.coolantPosition === "bottom"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: coolantComp }
            Loader { active: dashConfig.oilTempVisible && dashConfig.oilTempPosition === "bottom"; visible: active; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: oilTempComp }
        }
    }
}
