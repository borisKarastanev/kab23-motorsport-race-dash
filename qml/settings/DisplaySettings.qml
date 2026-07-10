import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls.Basic

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        StatRow {
            label: "BRIGHTNESS"
            value: displayModel.brightness + " %"
        }

        Slider {
            id: brightnessSlider
            Layout.fillWidth: true
            Layout.topMargin: 20
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            from: 0
            to: 100
            stepSize: 1
            value: displayModel.brightness
            onMoved: displayModel.brightness = value

            background: Rectangle {
                x: brightnessSlider.leftPadding
                y: brightnessSlider.topPadding + brightnessSlider.availableHeight / 2 - height / 2
                width: brightnessSlider.availableWidth
                height: 6
                radius: 3
                color: "#1a1a1a"

                Rectangle {
                    width: brightnessSlider.visualPosition * parent.width
                    height: parent.height
                    radius: 3
                    color: "#4488cc"
                }
            }

            handle: Rectangle {
                x: brightnessSlider.leftPadding
                   + brightnessSlider.visualPosition * (brightnessSlider.availableWidth - width)
                y: brightnessSlider.topPadding + brightnessSlider.availableHeight / 2 - height / 2
                width: 24
                height: 24
                radius: 12
                color: brightnessSlider.pressed ? "#cccccc" : "#888888"
                border.color: "#2a2a2a"
                border.width: 1
            }
        }

        Text {
            visible: !displayModel.hasBacklight
            Layout.fillWidth: true
            Layout.topMargin: 8
            text: "NO BACKLIGHT DETECTED ON THIS DEVICE — SETTING PERSISTS BUT HAS NO EFFECT HERE"
            color: "#555555"
            font.pixelSize: 9
            font.family: "monospace"
            wrapMode: Text.Wrap
        }

        Item { Layout.fillHeight: true }
    }
}
