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

    // A pair of edge-pinned action buttons.
    //
    // Pinned to the container edges rather than two half-width buttons: the gap
    // takes the slack, so each button keeps a fixed size and the pair stays
    // aligned whatever the row contains — including when UNPAIR is hidden on an
    // unpaired device and its row would otherwise stretch SET CREDENTIAL across
    // the full width. One component so that alignment is structural rather than
    // a convention each row has to re-honour.
    //
    // Declared inside the root object, not beside it: a sibling `component`
    // declaration is a syntax error QML reports only at load time, which on this
    // dev machine means the app exits silently. See CLAUDE.md.
    component ActionRow: Item {
        id: actionRow

        // One size for every button on the page, so the two rows line up with
        // each other rather than each button sizing to its own label.
        readonly property int buttonWidth: 150
        readonly property int buttonHeight: 40

        property string leftText: ""
        property string rightText: ""
        property bool   rightVisible: true
        property color  rightColor: "#cccccc"

        signal leftClicked()
        signal rightClicked()

        Layout.fillWidth: true
        Layout.preferredHeight: actionRow.buttonHeight
        Layout.leftMargin: 20
        Layout.rightMargin: 20

        TextButton {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: actionRow.buttonWidth
            height: actionRow.buttonHeight
            text: actionRow.leftText
            onClicked: actionRow.leftClicked()
        }

        TextButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: actionRow.buttonWidth
            height: actionRow.buttonHeight
            visible: actionRow.rightVisible
            text: actionRow.rightText
            textColor: actionRow.rightColor
            onClicked: actionRow.rightClicked()
        }
    }

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

            // StatRow sets Layout.fillWidth itself; nothing to restate here.
            StatRow { label: "BROKER"; value: uplinkModel.brokerSummary }
            StatRow {
                label: "DEVICE ID"
                value: uplinkModel.deviceId === "" ? "NOT SET" : uplinkModel.deviceId
            }
            StatRow {
                label: "CREDENTIAL"
                // Whether one exists, never what it is.
                value: cloudConfig.hasPassword ? "STORED" : "NOT SET"
            }
            StatRow {
                label: "SESSION"
                value: uplinkModel.sessionActive ? "ACTIVE" : "IDLE"
            }
            StatRow {
                label: "QUEUED FRAMES"
                // Non-zero means the link is down and data is being held for replay
                // — the number a driver actually wants when the uplink looks unwell.
                value: uplinkModel.queuedFrames.toString()
            }

            StatRow {
                visible: uplinkModel.lastError !== ""
                label: "LAST ERROR"
                value: uplinkModel.lastError
            }

            Item { Layout.fillWidth: true; Layout.preferredHeight: 16 }

            // ── enable toggle ─────────────────────────────────────
            ToggleRow {
                label: "CLOUD UPLINK"
                checked: cloudConfig.enabled
                onToggled: {
                    cloudConfig.enabled = !cloudConfig.enabled
                    uplinkModel.applyConfiguration()
                }
            }

            Item { Layout.fillWidth: true; Layout.preferredHeight: 12 }

            // ── pairing ───────────────────────────────────────────
            ActionRow {
                leftText: "SET BROKER"
                onLeftClicked: {
                    hostEntry.seed = cloudConfig.brokerHost
                    hostEntry.visible = true
                }
                rightText: "SET DEVICE ID"
                onRightClicked: {
                    deviceEntry.seed = cloudConfig.deviceId
                    deviceEntry.visible = true
                }
            }

            Item { Layout.fillWidth: true; Layout.preferredHeight: 8 }

            ActionRow {
                leftText: "SET CREDENTIAL"
                onLeftClicked: credentialEntry.visible = true
                rightText: "UNPAIR"
                rightColor: "#cc4444"
                rightVisible: cloudConfig.hasPassword
                onRightClicked: unpairConfirm.visible = true
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
        // One call, not the four steps this used to spell out: the order between
        // forgetting the credential, discarding the backlog and re-applying the
        // configuration is load-bearing, and it belongs with the model that
        // knows why. See UplinkModel::unpair().
        onConfirmed: {
            uplinkModel.unpair()
            unpairConfirm.visible = false
        }
        onCancelled: unpairConfirm.visible = false
    }
}
