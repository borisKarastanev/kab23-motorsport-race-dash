import QtQuick 2.15

// Minimal on-screen keyboard for kiosk mode (eglfs has no hardware keyboard
// and no virtual keyboard is wired up elsewhere in the app). Generic on
// purpose so other text-entry needs (e.g. the Tracks search box) can reuse it.

Item {
    id: root
    signal keyPressed(string ch)
    signal backspace()
    signal done()

    // Shared key shell: fixed-size Rectangle + centered monospace label + tap
    // handling. Used for every key except the shift key, whose 3-state visuals
    // (idle/shiftOnce/capsLock) and hold-vs-tap handling don't fit this shape.
    // (Inline components must be nested inside the root object, not declared
    // as a sibling before it.)
    component KeyButton: Rectangle {
        id: keyBtn
        property alias label: keyText.text
        property color textColor: "#cccccc"
        property real textSize: 16
        property bool textBold: false
        property real textLetterSpacing: 0
        property color idleColor: "#0d0d0d"
        property color pressedColor: "#111111"
        signal tapped()

        radius: 3
        color: area.pressed ? pressedColor : idleColor
        border.width: 1

        Text {
            id: keyText
            anchors.centerIn: parent
            color: keyBtn.textColor
            font.pixelSize: keyBtn.textSize
            font.bold: keyBtn.textBold
            font.family: "monospace"
            font.letterSpacing: keyBtn.textLetterSpacing
        }

        MouseArea {
            id: area
            anchors.fill: parent
            onClicked: keyBtn.tapped()
        }
    }

    property bool symbols: false
    // One-shot shift: capitalizes exactly the next letter, then auto-reverts.
    // Set by a plain tap on the shift key; cleared after that key is consumed.
    property bool shiftOnce: false
    // Caps lock: capitalizes every letter until the shift key is tapped again.
    // Set by holding the shift key for kCapsLockHoldMs; overrides shiftOnce.
    property bool capsLock: false
    readonly property bool shiftActive: shiftOnce || capsLock
    readonly property int kCapsLockHoldMs: 2000

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
        spacing: 8

        Repeater {
            model: activeRows

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 6

                Repeater {
                    model: modelData

                    KeyButton {
                        width: 44
                        height: 46
                        border.color: "#2a2a2a"
                        label: (root.shiftActive && !root.symbols) ? modelData.toUpperCase() : modelData
                        onTapped: {
                            root.keyPressed(label)
                            // One-shot shift is consumed by the letter it capitalized;
                            // caps lock stays on until the shift key is tapped again.
                            if (root.shiftOnce) root.shiftOnce = false
                        }
                    }
                }
            }
        }

        // ── bottom row: shift, symbols toggle, space, backspace, done ──
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6

            Rectangle {
                id: shiftKey
                width: 64
                height: 46
                radius: 3
                visible: !root.symbols

                // Single source of truth for the key's 3 states (idle/pressed,
                // shiftOnce, capsLock) — every visual below reads from this
                // instead of re-testing capsLock/shiftOnce independently.
                readonly property var visualState: root.capsLock
                    ? { bg: "#2a6b2a", border: "#66ff66", fg: "#66ff66", glyph: "⇪", borderW: 2 }
                    : root.shiftOnce
                        ? { bg: "#224422", border: "#4a9a4a", fg: "#66ff66", glyph: "⇧", borderW: 1 }
                        : { bg: shiftArea.pressed ? "#112211" : "#0d0d0d", border: "#2a4a2a", fg: "#66aa66", glyph: "⇧", borderW: 1 }

                color: visualState.bg
                border.color: visualState.border
                border.width: visualState.borderW

                Text {
                    anchors.centerIn: parent
                    text: shiftKey.visualState.glyph
                    color: shiftKey.visualState.fg
                    font.pixelSize: 18
                    font.bold: true
                    font.family: "monospace"
                }

                MouseArea {
                    id: shiftArea
                    anchors.fill: parent
                    // A hold latches caps lock; a plain tap toggles one-shot shift,
                    // or drops out of caps lock if that's currently active.
                    pressAndHoldInterval: root.kCapsLockHoldMs
                    property bool holdFired: false
                    onPressed: holdFired = false
                    onPressAndHold: {
                        holdFired = true
                        root.shiftOnce = false
                        root.capsLock = true
                    }
                    onClicked: {
                        if (holdFired) { holdFired = false; return }
                        if (root.capsLock)
                            root.capsLock = false
                        else
                            root.shiftOnce = !root.shiftOnce
                    }
                }
            }

            KeyButton {
                width: 64
                height: 46
                border.color: "#2a2a4a"
                pressedColor: "#111122"
                textColor: "#4488cc"
                textSize: 12
                textBold: true
                label: root.symbols ? "ABC" : "123"
                onTapped: root.symbols = !root.symbols
            }

            KeyButton {
                width: 200
                height: 46
                border.color: "#2a2a2a"
                onTapped: root.keyPressed(" ")
            }

            KeyButton {
                width: 64
                height: 46
                border.color: "#442222"
                pressedColor: "#221111"
                textColor: "#cc6666"
                textSize: 18
                label: "⌫"
                onTapped: root.backspace()
            }

            KeyButton {
                width: 90
                height: 46
                border.color: "#2a4a2a"
                pressedColor: "#112211"
                textColor: "#00cc44"
                textSize: 12
                textBold: true
                textLetterSpacing: 1
                label: "DONE"
                onTapped: root.done()
            }
        }
    }
}
