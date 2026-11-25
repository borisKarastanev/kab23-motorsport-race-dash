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
                    border.color: "#2a2a2a"
                    border.width: 1
                    radius: 2

                    // Tap-to-edit readout rather than a TextInput: the Pi has
                    // no system input method, so a focused TextInput never
                    // gets a keyboard. Entry goes through the shared
                    // TextEntryModal / OnScreenKeyboard instead.
                    color: searchArea.pressed ? "#1a1a1a" : "#111111"

                    MouseArea {
                        id: searchArea
                        anchors.fill: parent
                        onClicked: {
                            searchEntry.seed = trackModel.searchText
                            searchEntry.visible = true
                        }
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: clearSearch.visible ? clearSearch.left : parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: trackModel.searchText.length > 0 ? trackModel.searchText : "SEARCH…"
                        color: trackModel.searchText.length > 0 ? "#cccccc" : "#444444"
                        font.pixelSize: 11
                        font.family: "monospace"
                        font.letterSpacing: trackModel.searchText.length > 0 ? 0 : 1
                        elide: Text.ElideRight
                    }

                    // One-tap clear, so emptying the filter doesn't need a
                    // round of backspaces in the modal.
                    Item {
                        id: clearSearch
                        visible: trackModel.searchText.length > 0
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 30

                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            color: clearSearchArea.pressed ? "#cccccc" : "#888888"
                            font.pixelSize: 11
                            font.family: "monospace"
                        }

                        MouseArea {
                            id: clearSearchArea
                            anchors.fill: parent
                            onClicked: trackModel.searchText = ""
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
                    // Disabled for now: refreshing pulls the track list from
                    // racebox.pro, which doesn't carry our own confirmed "start"
                    // S/F lines (see data/track-db.json) and would silently drop
                    // them from the local DB override until reinstalled.
                    visible: false
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

    // ── search entry modal ──────────────────────────────────
    // Outside the ColumnLayout so it covers the whole page.
    TextEntryModal {
        id: searchEntry
        anchors.fill: parent
        visible: false
        title: "SEARCH TRACKS"
        allowEmpty: true   // submitting an empty field clears the filter
        onSubmitted: function(text) {
            // Trimmed to match TrackModel's own matching, so a spaces-only
            // entry clears the filter instead of showing a blank "active" one.
            trackModel.searchText = text.trim()
            searchEntry.visible = false
        }
        onCancelled: searchEntry.visible = false
    }
}
