import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    property var stackView: null

    property bool showingCountryPicker: false

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── toolbar: search + country filter + refresh ─────────
        Rectangle {
            Layout.fillWidth: true
            height: 48
            color: "#0d0d0d"
            border.color: "#2a2a2a"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 2
                    height: 30
                    color: "#111111"
                    border.color: "#2a2a2a"
                    border.width: 1
                    radius: 2

                    TextInput {
                        id: searchInput
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: TextInput.AlignVCenter
                        color: "#cccccc"
                        font.pixelSize: 11
                        font.family: "monospace"
                        clip: true
                        onTextChanged: trackModel.searchText = text

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: searchInput.text.length === 0
                            text: "SEARCH…"
                            color: "#444444"
                            font.pixelSize: 11
                            font.family: "monospace"
                            font.letterSpacing: 1
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: countryLabel.width + 20
                    height: 30
                    color: countryArea.pressed ? "#111111" : "transparent"
                    border.color: "#2a2a2a"
                    border.width: 1
                    radius: 2

                    Text {
                        id: countryLabel
                        anchors.centerIn: parent
                        text: "COUNTRY: " + trackModel.countryFilter + " ▾"
                        color: "#888888"
                        font.pixelSize: 10
                        font.family: "monospace"
                        font.letterSpacing: 1
                    }

                    MouseArea {
                        id: countryArea
                        anchors.fill: parent
                        onClicked: showingCountryPicker = true
                    }
                }

                Rectangle {
                    Layout.preferredWidth: refreshLabel.width + 20
                    height: 30
                    color: refreshArea.pressed ? "#111122" : "transparent"
                    border.color: "#2a2a4a"
                    border.width: 1
                    radius: 2
                    opacity: trackModel.refreshing ? 0.5 : 1.0

                    Text {
                        id: refreshLabel
                        anchors.centerIn: parent
                        text: trackModel.refreshing ? "REFRESHING…" : "⟳ REFRESH"
                        color: "#4488cc"
                        font.pixelSize: 10
                        font.family: "monospace"
                        font.letterSpacing: 1
                    }

                    MouseArea {
                        id: refreshArea
                        anchors.fill: parent
                        enabled: !trackModel.refreshing
                        onClicked: trackModel.refreshDatabase()
                    }
                }
            }
        }

        // ── refresh error banner ────────────────────────────────
        Text {
            Layout.fillWidth: true
            visible: trackModel.refreshError !== ""
            text: "REFRESH FAILED: " + trackModel.refreshError
            color: "#cc4444"
            font.pixelSize: 9
            font.family: "monospace"
            font.letterSpacing: 1
            padding: 6
        }

        // ── active track bar ─────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 36
            visible: trackModel.activeTrackId !== ""
            color: "#0f150f"
            border.color: "#2a4a2a"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: "ACTIVE: " + trackModel.activeTrackName
                    color: "#00cc44"
                    font.pixelSize: 11
                    font.bold: true
                    font.family: "monospace"
                    font.letterSpacing: 1
                    elide: Text.ElideRight
                }

                Text {
                    visible: trackModel.autoDetected
                    text: "[AUTO]"
                    color: "#444444"
                    font.pixelSize: 9
                    font.family: "monospace"
                    font.letterSpacing: 1
                }

                Text {
                    visible: trackModel.activeTrackHasFinishLine
                    text: "S/F ●"
                    color: "#00cc44"
                    font.pixelSize: 9
                    font.family: "monospace"
                }

                Rectangle {
                    width: 22; height: 22
                    color: clearActiveArea.pressed ? "#221111" : "transparent"
                    border.color: "#442222"
                    border.width: 1
                    radius: 2

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: "#cc6666"
                        font.pixelSize: 11
                        font.family: "monospace"
                    }

                    MouseArea {
                        id: clearActiveArea
                        anchors.fill: parent
                        onClicked: trackModel.clearActiveTrack()
                    }
                }
            }
        }

        // ── track list ────────────────────────────────────────
        ListView {
            id: trackListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !showingCountryPicker
            model: trackModel.filteredTracks
            clip: true

            delegate: Item {
                width: ListView.view.width
                height: 48

                Rectangle {
                    anchors.fill: parent
                    color: "#0d0d0d"

                    Rectangle {
                        visible: modelData.isActive
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 3
                        color: "#1E88E5"
                    }

                    Rectangle {
                        id: favoriteBtn
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40
                        height: 40
                        color: "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: modelData.isFavorite ? "★" : "☆"
                            color: modelData.isFavorite ? "#ffcc00" : "#444444"
                            font.pixelSize: 20
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: trackModel.toggleFavorite(modelData.id)
                        }
                    }

                    Column {
                        anchors.left: favoriteBtn.right
                        anchors.leftMargin: 8
                        anchors.right: statusColumn.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        Text {
                            width: parent.width
                            text: modelData.name
                            color: "#cccccc"
                            font.pixelSize: 12
                            font.bold: modelData.isActive
                            font.family: "monospace"
                            elide: Text.ElideRight
                        }
                        Text {
                            text: modelData.country
                            color: "#888888"
                            font.pixelSize: 10
                            font.family: "monospace"
                            font.letterSpacing: 1
                        }
                    }

                    Column {
                        id: statusColumn
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: 60
                        spacing: 2

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignRight
                            visible: modelData.hasFinishLine
                            text: "S/F ●"
                            color: "#00cc44"
                            font.pixelSize: 9
                            font.family: "monospace"
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignRight
                            visible: modelData.isActive
                            text: "ACTIVE"
                            color: "#00cc44"
                            font.pixelSize: 9
                            font.bold: true
                            font.family: "monospace"
                            font.letterSpacing: 1
                        }
                    }

                    MouseArea {
                        anchors.left: favoriteBtn.right
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        onClicked: trackModel.selectTrack(modelData.id)
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    height: 1
                    color: "#1a1a1a"
                }
            }

            Text {
                anchors.centerIn: parent
                visible: trackModel.filteredTracks.length === 0
                text: "NO TRACKS MATCH"
                color: "#333333"
                font.pixelSize: 12
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 2
            }
        }

        // ── country picker (replaces the list in-place) ────────
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: showingCountryPicker
            model: trackModel.countries
            clip: true

            delegate: Item {
                width: ListView.view.width
                height: 48

                Rectangle {
                    anchors.fill: parent
                    color: countryRowArea.pressed ? "#111111" : "#0d0d0d"

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        color: modelData === trackModel.countryFilter ? "#00cc44" : "#cccccc"
                        font.pixelSize: 12
                        font.bold: modelData === trackModel.countryFilter
                        font.family: "monospace"
                        font.letterSpacing: 1
                    }

                    MouseArea {
                        id: countryRowArea
                        anchors.fill: parent
                        onClicked: {
                            trackModel.countryFilter = modelData
                            showingCountryPicker = false
                        }
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    height: 1
                    color: "#1a1a1a"
                }
            }
        }
    }
}
