import QtQuick 2.15
import QtQuick.Controls.Basic

Item {
    id: settingsRoot

    signal closeDashboardRequested()

    function resetToRoot() {
        stack.pop(null)
    }

    Rectangle {
        anchors.fill: parent
        color: "#0a0a0a"
    }

    StackView {
        id: stack
        anchors.fill: parent

        initialItem: menuComponent

        pushEnter: Transition {
            XAnimator { from: stack.width; to: 0; duration: 220; easing.type: Easing.OutCubic }
        }
        pushExit: Transition {
            XAnimator { from: 0; to: -stack.width * 0.3; duration: 220; easing.type: Easing.OutCubic }
        }
        popEnter: Transition {
            XAnimator { from: -stack.width * 0.3; to: 0; duration: 220; easing.type: Easing.OutCubic }
        }
        popExit: Transition {
            XAnimator { from: 0; to: stack.width; duration: 220; easing.type: Easing.OutCubic }
        }
    }

    Component {
        id: menuComponent
        SettingsMenu {
            onItemSelected: function(key, title) {
                stack.push(detailComponent, { settingKey: key, title: title })
            }
        }
    }

    Component {
        id: detailComponent
        SettingsDetail {}
    }

    // ── right-swipe → back ────────────────────────────────────
    // Mirrors the "‹ BACK" button on every settings view: at depth 1 (menu)
    // it returns to the dashboard, otherwise it pops one level off the stack.
    DragHandler {
        target: null
        // Qt applies the axis restriction to the drag delta in window
        // coordinates, not this item's. With the scene rotated 90/270
        // (DASH_ROTATION, main.qml) a horizontal swipe here is vertical in
        // the window, so restrict whichever window axis is "our" vertical.
        readonly property bool sceneTurned: displayRotation === 90 || displayRotation === 270
        xAxis.enabled: !sceneTurned
        yAxis.enabled: sceneTurned

        property real startX: 0

        onActiveChanged: {
            if (active) {
                startX = centroid.position.x
            } else {
                if (centroid.position.x - startX > settingsRoot.width * 0.25) {
                    if (stack.depth > 1)
                        stack.pop()
                    else
                        settingsRoot.closeDashboardRequested()
                }
            }
        }
    }
}
