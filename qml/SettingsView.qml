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

    // ── right-swipe → back to dashboard ──────────────────────
    // Only active at depth 1 (menu); disabled inside a detail view.
    DragHandler {
        enabled: stack.depth === 1
        target: null
        yAxis.enabled: false

        property real startX: 0

        onActiveChanged: {
            if (active) {
                startX = centroid.position.x
            } else {
                if (centroid.position.x - startX > settingsRoot.width * 0.25)
                    settingsRoot.closeDashboardRequested()
            }
        }
    }
}
