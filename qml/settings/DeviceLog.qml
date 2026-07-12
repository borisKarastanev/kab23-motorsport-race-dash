import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    // The list is a snapshot, not a live binding — a log flood can't drive QML
    // relayouts. Entries arriving after this are counted (newSinceRefresh) and
    // shown as a badge on REFRESH rather than being pushed into the view.
    Component.onCompleted: {
        logBuffer.filterLevel = "info"
        logBuffer.limit = 20
        logBuffer.refresh()
    }

    readonly property var levels: [
        { key: "info",  label: "INFO",  color: "#4488cc" },
        { key: "warn",  label: "WARN",  color: "#cc8800" },
        { key: "error", label: "ERROR", color: "#cc4444" }
    ]
    readonly property var limits: [20, 50, 100]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── filter row ────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 44
            color: "#0d0d0d"
            border.color: "#2a2a2a"
            border.width: 1

            Row {
                anchors.centerIn: parent
                spacing: 8

                Repeater {
                    model: root.levels

                    Rectangle {
                        readonly property bool selected: logBuffer.filterLevel === modelData.key
                        width: 62
                        height: 28
                        color: selected ? Qt.darker(modelData.color, 4) : "transparent"
                        border.color: modelData.color
                        border.width: selected ? 2 : 1
                        radius: 2

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: modelData.color
                            font.pixelSize: 11
                            font.bold: selected
                            font.family: "monospace"
                            font.letterSpacing: 1
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: logBuffer.filterLevel = modelData.key
                        }
                    }
                }

                // ── how many entries to pull ──────────────────────
                Repeater {
                    model: root.limits

                    Rectangle {
                        readonly property bool selected: logBuffer.limit === modelData
                        width: 34
                        height: 28
                        color: selected ? "#1a1a1a" : "transparent"
                        border.color: selected ? "#666666" : "#2a2a2a"
                        border.width: 1
                        radius: 2

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: selected ? "#cccccc" : "#555555"
                            font.pixelSize: 10
                            font.bold: selected
                            font.family: "monospace"
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: logBuffer.limit = modelData
                        }
                    }
                }

                // ── refresh, badged with the count of new messages ──
                Rectangle {
                    width: 78
                    height: 28
                    color: refreshArea.pressed ? "#1a2a1a" : "transparent"
                    border.color: logBuffer.newSinceRefresh > 0 ? "#44aa44" : "#2a2a2a"
                    border.width: 1
                    radius: 2

                    Text {
                        anchors.centerIn: parent
                        text: logBuffer.newSinceRefresh > 0
                              ? "⟳ " + Math.min(logBuffer.newSinceRefresh, 999)
                              : "⟳"
                        color: logBuffer.newSinceRefresh > 0 ? "#44aa44" : "#555555"
                        font.pixelSize: 11
                        font.bold: true
                        font.family: "monospace"
                    }

                    MouseArea {
                        id: refreshArea
                        anchors.fill: parent
                        onClicked: logBuffer.refresh()
                    }
                }
            }
        }

        // ── log list ──────────────────────────────────────────────
        ListView {
            id: logList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: logBuffer.entries
            clip: true
            verticalLayoutDirection: ListView.BottomToTop

            delegate: Item {
                width: ListView.view.width
                // Bottom padding matches msgText's own topMargin, so the row
                // is symmetric top/bottom regardless of how many lines the
                // message wraps to.
                height: msgText.anchors.topMargin + msgText.height + 8

                Rectangle {
                    anchors.fill: parent
                    color: "#0d0d0d"
                    opacity: modelData.prev ? 0.6 : 1.0

                    Row {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.leftMargin: 12
                        anchors.topMargin: 8
                        spacing: 8

                        Text {
                            text: modelData.time
                            color: "#444444"
                            font.pixelSize: 9
                            font.family: "monospace"
                        }

                        Text {
                            text: modelData.category
                            color: "#555555"
                            font.pixelSize: 9
                            font.family: "monospace"
                            font.letterSpacing: 1
                        }
                    }

                    Text {
                        id: msgText
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.topMargin: 22
                        text: modelData.message
                        color: "#cccccc"
                        font.pixelSize: 11
                        font.family: "monospace"
                        wrapMode: Text.Wrap
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: logList.count === 0
                text: "NO MESSAGES"
                color: "#333333"
                font.pixelSize: 12
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 2
            }
        }
    }
}
