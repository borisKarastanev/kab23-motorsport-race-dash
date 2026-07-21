import QtQuick 2.15
import QtQuick.Controls.Basic
import "settings"
import "settings/SessionNav.js" as SessionNav

Item {
    id: detailRoot

    property string settingKey: ""
    property string title: ""
    property var    payload:    ({})

    readonly property bool isSessionSummary: settingKey === "session-summary"

    // Inline component declarations — one per settings section.
    // Each future per-item plan replaces the placeholder here and adds
    // its real QML file; no navigation code changes needed.
    Component { id: compLayout;         DashboardLayout {} }
    Component { id: compNetwork;        NetworkMenu {} }
    Component { id: compNetworkWifi;    WifiDetail {} }
    Component { id: compNetworkWired;   WiredDetail {} }
    Component { id: compIntegrations;   PlaceholderSetting { title: "INTEGRATIONS" } }
    Component { id: compSessions;         SessionGroupsList {} }
    Component { id: compSessionTrackList; SessionsList {} }
    Component { id: compSessionSummary;   SessionSummary {} }
    Component { id: compTracks;         TracksBrowser {} }
    Component { id: compDevice;         DeviceInfoMenu {} }
    Component { id: compDeviceVersion;  AppVersionDetail {} }
    Component { id: compDeviceStats;    DeviceStats {} }
    Component { id: compDeviceLog;      DeviceLog {} }
    Component { id: compDisplay;        DisplaySettings {} }

    function registryFor(key) {
        switch (key) {
            case "layout":          return compLayout
            case "ui":              return compLayout // legacy key alias
            case "network":         return compNetwork
            case "network-wifi":    return compNetworkWifi
            case "network-wired":   return compNetworkWired
            case "integrations":    return compIntegrations
            case "sessions":           return compSessions
            case "session-track-list": return compSessionTrackList
            case "session-summary":    return compSessionSummary
            case "tracks":          return compTracks
            case "device":          return compDevice
            case "device-version":  return compDeviceVersion
            case "device-stats":    return compDeviceStats
            case "device-log":      return compDeviceLog
            case "display":         return compDisplay
            default:                return compLayout
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

        TextButton {
            id: deleteBtn
            visible: detailRoot.isSessionSummary
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            height: 26
            text: "DELETE"
            textColor: "#cc4444"
            pressedColor: "#221111"
            border.color: "#4a2a2a"
            onClicked: deleteConfirmModal.visible = true
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

    // ── delete confirmation ─────────────────────────────────────
    ConfirmModal {
        id: deleteConfirmModal
        title: "DELETE SESSION?"
        message: "This session's laps and stats will be permanently removed. This cannot be undone."
        confirmLabel: "DELETE"
        cancelLabel: "CANCEL"

        onCancelled: visible = false
        onConfirmed: {
            visible = false
            var view = detailRoot.StackView.view
            var trackId = detailRoot.payload.trackId || ""
            sessionModel.deleteSession(detailRoot.payload.timestampIso)
            SessionNav.popAfterDelete(view, sessionModel, trackId)
        }
    }
}
