pragma ComponentBehavior: Bound
import QtQuick 2.15
import QtQuick.Window 2.15

// Full-screen dimmed overlay for picking one of the 9 dashboard grid cells.
// Every square is tappable, including occupied ones — tapping an occupied
// square swaps that entity into the caller's old cell (DashConfig.
// setEntityPosition() does the swap; this component has no layout logic of
// its own, it just renders DashConfig's current state and forwards the tap).
//
// Not migrated to ModalScaffold.qml (TICKET-modal-scaffold-extraction): its
// root Rectangle *is* the backdrop at #cc000000 (~0.8 alpha), vs the
// scaffold's separate `color: "#000000"; opacity: 0.7` backdrop. Matching it
// exactly would mean adding a backdrop color/opacity knob for this single
// caller, which the ticket explicitly calls out as over-generalizing. Every
// other knob (coverWindow, overlayZ, dismissOnBackdrop, fixed panel size,
// radius, margins/spacing) maps cleanly — this is the one that doesn't.
Rectangle {
    id: root
    // Reparent to the window's content item so the overlay covers the whole
    // window — including the settings header — rather than only the detail
    // content area below it, which would leave the BACK button tappable
    // "through" the modal. Lifetime still follows this component's QML owner,
    // so it's destroyed with the page that declared it.
    parent: Window.contentItem
    anchors.fill: parent
    z: 200
    visible: false
    color: "#cc000000"

    // Which entity is being repositioned ("rpm", "speed", "coolant",
    // "oiltemp", "gear", "laptimer") and its display title for the header.
    property string entityKey: ""
    property string entityTitle: ""

    // Short labels shown inside occupied squares.
    readonly property var entityLabels: ({
        rpm:      "RPM",
        speed:    "SPEED",
        coolant:  "COOLANT",
        oiltemp:  "OIL",
        gear:     "GEAR",
        laptimer: "LAP"
    })

    // Row-major, matching DashConfig's allPositions() ordering.
    readonly property var positionTokens: [
        "top-left",    "top-center",    "top-right",
        "center-left", "center",        "center-right",
        "bottom-left", "bottom-center", "bottom-right"
    ]

    function open(key, title) {
        root.entityKey = key
        root.entityTitle = title
        root.visible = true
    }

    MouseArea {
        // Swallow taps on the dimmed backdrop so they don't fall through to
        // the dashboard cards underneath; tapping the backdrop cancels.
        anchors.fill: parent
        onClicked: root.visible = false
    }

    Rectangle {
        anchors.centerIn: parent
        width: 280
        height: 340
        color: "#0d0d0d"
        border.color: "#2a2a2a"
        border.width: 1
        radius: 2

        MouseArea { anchors.fill: parent } // block backdrop taps from this panel

        Column {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 16

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: root.entityTitle + " POSITION"
                color: "#888888"
                font.pixelSize: 11
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 2
            }

            Grid {
                anchors.horizontalCenter: parent.horizontalCenter
                columns: 3
                rows: 3
                spacing: 8

                Repeater {
                    model: root.positionTokens

                    Rectangle {
                        id: square
                        width: 72; height: 72
                        radius: 3

                        required property string modelData
                        readonly property string posToken: modelData
                        // entityAtPosition() is a plain C++ call QML can't
                        // track, so depend on layoutRevision to re-evaluate
                        // occupancy whenever any position/visibility changes.
                        readonly property string occupant: (root.visible, dashConfig.layoutRevision,
                            root.visible ? dashConfig.entityAtPosition(posToken) : "")
                        readonly property bool isCurrent: occupant === root.entityKey
                        readonly property bool isOccupiedByOther: occupant !== "" && !isCurrent

                        color: isCurrent ? "#0a2a1a" : isOccupiedByOther ? "#1a1a1a" : "#111111"
                        border.color: isCurrent ? "#00cc44" : squareArea.pressed ? "#4488cc" : "#2a2a2a"
                        border.width: isCurrent ? 2 : 1

                        Text {
                            anchors.centerIn: parent
                            text: square.isOccupiedByOther
                                  ? (root.entityLabels[square.occupant] || square.occupant)
                                  : square.isCurrent ? "●" : ""
                            color: square.isCurrent ? "#00cc44" : "#666666"
                            font.pixelSize: square.isCurrent ? 18 : 9
                            font.bold: true
                            font.family: "monospace"
                            wrapMode: Text.WordWrap
                            width: parent.width - 8
                            horizontalAlignment: Text.AlignHCenter
                        }

                        MouseArea {
                            id: squareArea
                            anchors.fill: parent
                            onClicked: {
                                dashConfig.setEntityPosition(root.entityKey, square.posToken)
                                root.visible = false
                            }
                        }
                    }
                }
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "TAP A SQUARE TO MOVE — OCCUPIED SWAPS"
                color: "#444444"
                font.pixelSize: 8
                font.family: "monospace"
                font.letterSpacing: 1
                wrapMode: Text.WordWrap
            }
        }
    }
}
