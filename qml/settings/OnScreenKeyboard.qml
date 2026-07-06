import QtQuick 2.15

// Minimal on-screen keyboard for kiosk mode (eglfs has no hardware keyboard
// and no virtual keyboard is wired up elsewhere in the app). Generic on
// purpose so other text-entry needs (e.g. the Tracks search box) can reuse it.
Item {
    id: root
    signal keyPressed(string ch)
    signal backspace()
    signal done()

    property bool symbols: false

    readonly property var letterRows: [
        ["q","w","e","r","t","y","u","i","o","p"],
        ["a","s","d","f","g","h","j","k","l"],
        ["z","x","c","v","b","n","m"]
    ]
    readonly property var symbolRows: [
        ["1","2","3","4","5","6","7","8","9","0"],
        ["-","_","@",".",",","/",":",";"],
        ["!","?","#","$","%","&","*","("]
    ]

    readonly property var activeRows: symbols ? symbolRows : letterRows

    implicitHeight: keyColumn.height

    Column {
        id: keyColumn
        width: parent.width
        spacing: 6

        Repeater {
            model: activeRows

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 4

                Repeater {
                    model: modelData

                    Rectangle {
                        width: 30
                        height: 34
                        radius: 2
                        color: keyArea.pressed ? "#111111" : "#0d0d0d"
                        border.color: "#2a2a2a"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: "#cccccc"
                            font.pixelSize: 13
                            font.family: "monospace"
                        }

                        MouseArea {
                            id: keyArea
                            anchors.fill: parent
                            onClicked: root.keyPressed(modelData)
                        }
                    }
                }
            }
        }

        // ── bottom row: symbols toggle, space, backspace, done ──
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 4

            Rectangle {
                width: 50
                height: 34
                radius: 2
                color: symToggleArea.pressed ? "#111122" : "#0d0d0d"
                border.color: "#2a2a4a"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: root.symbols ? "ABC" : "123"
                    color: "#4488cc"
                    font.pixelSize: 10
                    font.bold: true
                    font.family: "monospace"
                }

                MouseArea {
                    id: symToggleArea
                    anchors.fill: parent
                    onClicked: root.symbols = !root.symbols
                }
            }

            Rectangle {
                width: 140
                height: 34
                radius: 2
                color: spaceArea.pressed ? "#111111" : "#0d0d0d"
                border.color: "#2a2a2a"
                border.width: 1

                MouseArea {
                    id: spaceArea
                    anchors.fill: parent
                    onClicked: root.keyPressed(" ")
                }
            }

            Rectangle {
                width: 50
                height: 34
                radius: 2
                color: backArea.pressed ? "#221111" : "#0d0d0d"
                border.color: "#442222"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "⌫"
                    color: "#cc6666"
                    font.pixelSize: 14
                    font.family: "monospace"
                }

                MouseArea {
                    id: backArea
                    anchors.fill: parent
                    onClicked: root.backspace()
                }
            }

            Rectangle {
                width: 70
                height: 34
                radius: 2
                color: doneArea.pressed ? "#112211" : "#0d0d0d"
                border.color: "#2a4a2a"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "DONE"
                    color: "#00cc44"
                    font.pixelSize: 10
                    font.bold: true
                    font.family: "monospace"
                    font.letterSpacing: 1
                }

                MouseArea {
                    id: doneArea
                    anchors.fill: parent
                    onClicked: root.done()
                }
            }
        }
    }
}
