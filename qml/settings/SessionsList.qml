import QtQuick 2.15
import QtQuick.Controls.Basic

Item {
    property var stackView: null
    // Set by SettingsDetail from the "session-track-list" push — a single
    // track group from SessionModel.sessionGroups: {trackId, trackName,
    // latestTitle, count, sessions: [records newest-first]}.
    property var payload: ({})

    readonly property string trackId: payload.trackId || ""

    // Re-derived from the live model rather than bound to the pushed payload
    // snapshot, so deleting a session in Details and popping back here shows
    // the updated list immediately instead of the stale entry.
    readonly property var trackSessions: {
        const group = sessionModel.sessionGroups.find(g => g.trackId === trackId)
        return group ? group.sessions : []
    }

    ListView {
        anchors.fill: parent
        model: trackSessions
        clip: true

        delegate: Item {
            width: ListView.view.width
            height: 60

            Rectangle {
                anchors.fill: parent
                color: rowArea.pressed ? "#111111" : "#0d0d0d"

                Text {
                    anchors.centerIn: parent
                    text: modelData.title
                    color: "#cccccc"
                    font.pixelSize: 12
                    font.bold: true
                    font.family: "monospace"
                    font.letterSpacing: 2
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: "›"
                    color: "#444444"
                    font.pixelSize: 18
                    font.family: "monospace"
                }

                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    onClicked: {
                        stackView.push("qrc:/qml/SettingsDetail.qml", {
                            settingKey: "session-summary",
                            title: modelData.title,
                            payload: modelData
                        })
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
                color: "#2a2a2a"
            }
        }

        Text {
            anchors.centerIn: parent
            visible: trackSessions.length === 0
            text: "NO SAVED SESSIONS"
            color: "#333333"
            font.pixelSize: 12
            font.bold: true
            font.family: "monospace"
            font.letterSpacing: 2
        }
    }
}
