import QtQuick 2.15
import QtQuick.Controls.Basic
import "settings"

Item {
    id: detailRoot

    property string settingKey: ""
    property string title: ""
    property var    payload:    ({})

    // Inline component declarations — one per settings section.
    // Each future per-item plan replaces the placeholder here and adds
    // its real QML file; no navigation code changes needed.
    Component { id: compUi;             PlaceholderSetting { title: "CONFIGURE UI" } }
    Component { id: compNetwork;        PlaceholderSetting { title: "NETWORK CONNECTION" } }
    Component { id: compIntegrations;   PlaceholderSetting { title: "INTEGRATIONS" } }
    Component { id: compSessions;         SessionGroupsList {} }
    Component { id: compSessionTrackList; SessionsList {} }
    Component { id: compSessionSummary;   SessionSummary {} }
    Component { id: compTracks;         TracksBrowser {} }
    Component { id: compDevice;         PlaceholderSetting { title: "DEVICE INFO" } }

    function registryFor(key) {
        switch (key) {
            case "ui":              return compUi
            case "network":         return compNetwork
            case "integrations":    return compIntegrations
            case "sessions":           return compSessions
            case "session-track-list": return compSessionTrackList
            case "session-summary":    return compSessionSummary
            case "tracks":          return compTracks
            case "device":          return compDevice
            default:                return compUi
        }
    }

    // ── header bar ───────────────────────────────────────────
    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 44
        color: "#0d0d0d"
        border.color: "#2a2a2a"
        border.width: 1

        Rectangle {
            id: backBtn
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: backLabel.width + 20
            height: 26
            color: backArea.pressed ? "#111111" : "transparent"
            border.color: "#2a2a2a"
            border.width: 1
            radius: 2

            Text {
                id: backLabel
                anchors.centerIn: parent
                text: "‹ BACK"
                color: "#cccccc"
                font.pixelSize: 12
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 2
            }

            MouseArea {
                id: backArea
                anchors.fill: parent
                // detailRoot is the direct StackView child, so StackView.view resolves there
                onClicked: detailRoot.StackView.view.pop()
            }
        }

        Text {
            anchors.centerIn: parent
            text: title
            color: "#cccccc"
            font.pixelSize: 12
            font.bold: true
            font.family: "monospace"
            font.letterSpacing: 2
        }
    }

    // ── content area ─────────────────────────────────────────
    Loader {
        id: contentLoader
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        sourceComponent: registryFor(settingKey)
        onLoaded: {
            if ("payload"   in item) item.payload   = detailRoot.payload
            if ("stackView" in item) item.stackView = detailRoot.StackView.view
        }
    }
}
