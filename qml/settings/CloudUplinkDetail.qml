import QtQuick 2.15
import QtQuick.Layouts 1.15
import RaceDash 1.0

// Settings > Device Settings > Cloud Uplink.
//
// Status plus pairing. The credential is collected through PasswordEntryModal
// and handed straight to CloudConfig — it is never held in a QML property, never
// bound to a Text, and never read back: CloudConfig deliberately exposes no
// getter for it, only hasPassword. The Device Log page renders everything the Qt
// message handler captures, so a credential on this screen would also be a
// credential in a log file.
Item {
    id: root
    property var stackView: null

    // NOT `state`: QQuickItem already has a string `state` property, and
    // shadowing it with an int breaks state-group handling in ways that only
    // show up at runtime.
    readonly property int uplinkState: uplinkModel.state

    function stateColor(s) {
        switch (s) {
            case Uplink.Online:       return "#44cc66"
            case Uplink.Offline:      return "#ccaa44"
            case Uplink.Connecting:   return "#4488cc"
            case Uplink.Unconfigured: return "#cc4444"
            default:                  return "#666666"
        }
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 0
        spacing: 0

        // ── status ────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: root.stateColor(root.uplinkState)
            }

            Text {
                text: uplinkModel.stateText.toUpperCase()
                color: root.stateColor(root.uplinkState)
                font.pixelSize: 12
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 2
                Layout.fillWidth: true
            }
        }

        StatRow { Layout.fillWidth: true; label: "BROKER";    value: uplinkModel.brokerSummary }
        StatRow {
            Layout.fillWidth: true
            label: "DEVICE ID"
            value: uplinkModel.deviceId === "" ? "NOT SET" : uplinkModel.deviceId
        }
        StatRow {
            Layout.fillWidth: true
            label: "CREDENTIAL"
            // Whether one exists, never what it is.
            value: cloudConfig.hasPassword ? "STORED" : "NOT SET"
        }
        StatRow {
            Layout.fillWidth: true
            label: "SESSION"
            value: uplinkModel.sessionActive ? "ACTIVE" : "IDLE"
        }
        StatRow {
            Layout.fillWidth: true
            label: "QUEUED FRAMES"
            // Non-zero means the link is down and data is being held for replay
            // — the number a driver actually wants when the uplink looks unwell.
            value: uplinkModel.queuedFrames.toString()
        }

        StatRow {
            Layout.fillWidth: true
            visible: uplinkModel.lastError !== ""
            label: "LAST ERROR"
            value: uplinkModel.lastError
        }

        Item { Layout.fillWidth: true; Layout.preferredHeight: 16 }

        // ── controls ──────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 10

            TextButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                text: cloudConfig.enabled ? "DISABLE UPLINK" : "ENABLE UPLINK"
                onClicked: {
                    cloudConfig.enabled = !cloudConfig.enabled
                    uplinkModel.applyConfiguration()
                }
            }
        }

        Item { Layout.fillWidth: true; Layout.preferredHeight: 8 }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 10

            TextButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                text: "SET BROKER"
                onClicked: {
                    hostEntry.seed = cloudConfig.brokerHost
                    hostEntry.visible = true
                }
            }

            TextButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                text: "SET DEVICE ID"
                onClicked: {
                    deviceEntry.seed = cloudConfig.deviceId
                    deviceEntry.visible = true
                }
            }
        }

        Item { Layout.fillWidth: true; Layout.preferredHeight: 8 }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 10

            TextButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                text: "SET CREDENTIAL"
                onClicked: credentialEntry.visible = true
            }

            TextButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                visible: cloudConfig.hasPassword
                text: "UNPAIR"
                textColor: "#cc4444"
                onClicked: unpairConfirm.visible = true
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 16
            wrapMode: Text.WordWrap
            color: "#555555"
            font.pixelSize: 10
            font.family: "monospace"
            text: "The credential is issued once by the web app (Cars > MQTT "
                  + "credentials) and is stored nowhere else. If it is lost it "
                  + "must be rotated, not recovered."
        }
    }

    // ── modals ────────────────────────────────────────────────

    TextEntryModal {
        id: hostEntry
        anchors.fill: parent
        visible: false
        title: "BROKER HOSTNAME"
        onSubmitted: function(text) {
            cloudConfig.brokerHost = text
            hostEntry.visible = false
            uplinkModel.applyConfiguration()
        }
        onCancelled: hostEntry.visible = false
    }

    TextEntryModal {
        id: deviceEntry
        anchors.fill: parent
        visible: false
        title: "DEVICE ID"
        onSubmitted: function(text) {
            cloudConfig.deviceId = text
            deviceEntry.visible = false
            uplinkModel.applyConfiguration()
        }
        onCancelled: deviceEntry.visible = false
    }

    PasswordEntryModal {
        id: credentialEntry
        anchors.fill: parent
        visible: false
        title: "BROKER CREDENTIAL"
        // Straight through to CloudConfig, which persists it 0600. Nothing here
        // keeps a copy.
        onSubmitted: function(password) {
            cloudConfig.setPassword(password)
            credentialEntry.visible = false
            uplinkModel.applyConfiguration()
        }
        onCancelled: credentialEntry.visible = false
    }

    ConfirmModal {
        id: unpairConfirm
        anchors.fill: parent
        visible: false
        title: "UNPAIR THIS CAR?"
        message: "Forgets the broker credential and discards any queued frames. "
                 + "The credential cannot be recovered — a new one must be issued."
        onConfirmed: {
            cloudConfig.clearPassword()
            cloudConfig.enabled = false
            uplinkModel.applyConfiguration()
            unpairConfirm.visible = false
        }
        onCancelled: unpairConfirm.visible = false
    }
}
