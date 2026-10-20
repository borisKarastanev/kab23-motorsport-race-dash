import QtQuick 2.15

// Full-screen low oil pressure emergency. Sits at the Window level above every
// page and can't be dismissed — it clears only when CanDataModel drops
// oilPressureWarning (pressure back, engine stopped, or 0x545 gone stale).
Rectangle {
    id: root

    component WarningText: Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: "#ffffff"
        font.family: "monospace"
        font.bold: true
        fontSizeMode: Text.Fit
        wrapMode: Text.WordWrap
    }

    property alias pulseAlpha: pulse.alpha

    AlertPulse {
        id: pulse
        active: root.visible
    }

    // Only the fill pulses (not `opacity`), so the message stays at full
    // brightness.
    color:        pulse.withAlpha(pulse.fillDanger, pulseAlpha)
    border.color: pulse.alertDanger
    border.width: 6

    // Swallow every touch so nothing underneath can be operated.
    MouseArea {
        anchors.fill: parent
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - 48
        spacing: 24

        WarningText {
            height: root.height * 0.4
            text: "LOW OIL PRESSURE"
            font.pixelSize: 200
        }

        WarningText {
            height: root.height * 0.2
            text: "TURN OFF ENGINE IMMEDIATELY"
            font.pixelSize: 100
        }
    }
}
