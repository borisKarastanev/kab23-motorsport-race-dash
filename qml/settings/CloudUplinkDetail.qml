import QtQuick 2.15
import QtQuick.Controls 2.15
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

    // One size for every action button, so the left and right columns line up
    // with each other rather than each sizing to its own label.
    readonly property int actionWidth: 150
    readonly property int actionHeight: 40

    function stateColor(s) {
        switch (s) {
            case Uplink.Online:       return "#44cc66"
            case Uplink.Offline:      return "#ccaa44"
            case Uplink.Connecting:   return "#4488cc"
            case Uplink.Unconfigured: return "#cc4444"
            default:                  return "#666666"
        }
    }

    // Flickable, matching WifiDetail. This page grows and shrinks as it runs —
    // LAST ERROR appears on a failed connect, UNPAIR appears once a credential
    // is stored — so its height is not knowable up front, and on the dash's
    // 800x480 panel the pairing buttons fall off the bottom the moment an extra
    // row shows up. Without this there is no way to reach them.
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.height
        clip: true
        // The page is only ever a little taller than the viewport; a rubber-band
        // overshoot on a touchscreen just makes the buttons harder to hit.
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: content
            width: flick.width
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

            // ── enable toggle ─────────────────────────────────────
            // Same control as Settings > Network Connection > Wi-Fi, deliberately:
            // both answer "is this radio on", and a switch reads as current state at
            // a glance where a button labelled with the *opposite* action ("DISABLE
            // UPLINK" when enabled) has to be decoded first — which is the wrong
            // demand to make of somebody glancing at a screen in a pit lane.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                color: "#0d0d0d"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: "CLOUD UPLINK"
                    color: "#cccccc"
                    font.pixelSize: 12
                    font.bold: true
                    font.family: "monospace"
                    font.letterSpacing: 2
                }

                Rectangle {
                    id: enableTrack
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    width: 46
                    height: 24
                    radius: 12
                    color: cloudConfig.enabled ? "#00cc44" : "#2a2a2a"
                    border.color: "#1a1a1a"
                    border.width: 1

                    Rectangle {
                        width: 18
                        height: 18
                        radius: 9
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#0d0d0d"
                        x: cloudConfig.enabled ? (parent.width - width - 3) : 3
                        Behavior on x { NumberAnimation { duration: 150 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            cloudConfig.enabled = !cloudConfig.enabled
                            uplinkModel.applyConfiguration()
                        }
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    height: 1
                    color: "#1a1a1a"
                }
            }

            Item { Layout.fillWidth: true; Layout.preferredHeight: 12 }

            // ── pairing ───────────────────────────────────────────
            // Two columns pinned to the container edges rather than two half-width
            // buttons: the spacer takes the slack, so each button keeps a fixed size
            // and the pair stays aligned whatever the row contains — including when
            // UNPAIR is hidden on an unpaired device and its row would otherwise
            // stretch SET CREDENTIAL across the full width.
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: root.actionHeight
                Layout.leftMargin: 20
                Layout.rightMargin: 20

                TextButton {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.actionWidth
                    height: root.actionHeight
                    text: "SET BROKER"
                    onClicked: {
                        hostEntry.seed = cloudConfig.brokerHost
                        hostEntry.visible = true
                    }
                }

                TextButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.actionWidth
                    height: root.actionHeight
                    text: "SET DEVICE ID"
                    onClicked: {
                        deviceEntry.seed = cloudConfig.deviceId
                        deviceEntry.visible = true
                    }
                }
            }

            Item { Layout.fillWidth: true; Layout.preferredHeight: 8 }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: root.actionHeight
                Layout.leftMargin: 20
                Layout.rightMargin: 20

                TextButton {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.actionWidth
                    height: root.actionHeight
                    text: "SET CREDENTIAL"
                    onClicked: credentialEntry.visible = true
                }

                TextButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.actionWidth
                    height: root.actionHeight
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

            // Breathing room at the end so the last button is not flush against the
            // bottom edge when scrolled fully down.
            Item { Layout.fillWidth: true; Layout.preferredHeight: 16 }
        }
    } // Flickable

    // ── modals ────────────────────────────────────────────────
    // Outside the Flickable, anchored to the page: a modal must cover the whole
    // view and must not scroll with the content underneath it.

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
            // Before applyConfiguration(), so the backlog is gone while the link
            // is still the old car's. The message above promises this; without
            // the call, re-pairing as a different car replayed the previous
            // car's sessions under the new car's topics and credential.
            uplinkModel.clearSpool()
            uplinkModel.applyConfiguration()
            unpairConfirm.visible = false
        }
        onCancelled: unpairConfirm.visible = false
    }
}
