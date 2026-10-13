import QtQuick 2.15

// Live driver inputs: accelerator pedal, brake, steering angle.
Item {
    id: root

    Row {
        id: bars
        anchors.left: parent.left
        anchors.leftMargin: 40
        anchors.top: parent.top
        anchors.topMargin: 24
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        spacing: 32

        InputBar {
            height: bars.height
            label: "THROTTLE"
            value: dataModel.throttle
            valid: dataModel.throttleValid
            fillColor: "#33cc66"
        }

        // 0x153 carries only the brake light switch, so this is on/off.
        InputBar {
            height: bars.height
            label: "BRAKE"
            value: dataModel.brakePressed ? 100 : 0
            valid: dataModel.brakeValid
            valueText: !valid ? "--" : dataModel.brakePressed ? "ON" : "OFF"
            fillColor: "#cc3333"
        }
    }

    Item {
        anchors.left: bars.right
        anchors.leftMargin: 32
        anchors.right: parent.right
        anchors.rightMargin: 40
        anchors.top: parent.top
        anchors.topMargin: 24
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24

        SteeringWheel {
            anchors.top: parent.top
            anchors.bottom: caption.top
            anchors.bottomMargin: 8
            anchors.left: parent.left
            anchors.right: parent.right
            angle: dataModel.steeringAngle
            valid: dataModel.steeringValid
        }

        InputLabel {
            id: caption
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            text: "STEERING · LWS MUST BE CALIBRATED"
        }
    }
}
