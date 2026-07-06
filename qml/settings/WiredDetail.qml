import QtQuick 2.15
import QtQuick.Layouts 1.15

// Settings > Device Settings > Network Connection > Wired.
Item {
    id: root
    property var stackView: null

    Component.onCompleted: networkModel.acquire()
    Component.onDestruction: networkModel.release()

    Text {
        visible: !networkModel.nmAvailable
        anchors.centerIn: parent
        text: "NETWORKMANAGER NOT AVAILABLE"
        color: "#444444"
        font.pixelSize: 11
        font.bold: true
        font.family: "monospace"
        font.letterSpacing: 2
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: networkModel.nmAvailable && networkModel.wiredConnected
        spacing: 0

        StatRow { Layout.fillWidth: true; label: "IP ADDRESS";  value: networkModel.wiredIp }
        StatRow { Layout.fillWidth: true; label: "SUBNET MASK"; value: networkModel.wiredSubnet }
        StatRow { Layout.fillWidth: true; label: "GATEWAY";     value: networkModel.wiredGateway }
    }

    Text {
        visible: networkModel.nmAvailable && !networkModel.wiredConnected
        anchors.centerIn: parent
        text: "NO ACTIVE WIRED CONNECTION"
        color: "#444444"
        font.pixelSize: 12
        font.bold: true
        font.family: "monospace"
        font.letterSpacing: 2
    }
}
