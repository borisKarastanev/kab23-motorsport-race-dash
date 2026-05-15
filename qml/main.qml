import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15

Window {
    width: 800
    height: 480
    visible: true
    title: "BMW E46 Dashboard"
    color: "#0a0a0a"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        LedStrip {
            Layout.fillWidth: true
            height: 26
            rpm:             dataModel.rpm
            ledCount:        dashConfig.ledCount
            greenStart:      dashConfig.greenStart
            yellowStart:     dashConfig.yellowStart
            redStart:        dashConfig.redStart
            flashStart:      dashConfig.flashStart
            flashIntervalMs: dashConfig.flashIntervalMs
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            rowSpacing: 8
            columnSpacing: 8

            Gauge {
                Layout.fillWidth: true;  Layout.fillHeight: true
                label: "RPM"
                value: dataModel.rpm
                maxValue: 8000
            }

            Gauge {
                Layout.fillWidth: true;  Layout.fillHeight: true
                label: "SPEED"
                unit: "km/h"
                value: dataModel.speed
                maxValue: 240
            }

            Gauge {
                Layout.fillWidth: true;  Layout.fillHeight: true
                label: "COOLANT"
                unit: "°C"
                value: dataModel.coolantTemp
                maxValue: 120
                decimalPlaces: 1
                warningThreshold: 95
                dangerThreshold: 105
            }

            Gauge {
                Layout.fillWidth: true;  Layout.fillHeight: true
                label: "OIL TEMP"
                unit: "°C"
                value: dataModel.oilTemp
                maxValue: 150
                decimalPlaces: 1
                warningThreshold: 120
                dangerThreshold: 135
            }
        }
    }
}
