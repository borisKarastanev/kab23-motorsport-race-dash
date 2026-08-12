import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls.Basic

Window {
    width: 800
    height: 480
    visibility: kioskMode ? Window.FullScreen : Window.AutomaticVisibility
    visible: true
    title: "BMW E46 Dashboard"
    color: "#0a0a0a"

    SwipeView {
        id: rootSwipe
        anchors.fill: parent
        interactive: false

        DashboardView {
            id: dashboardView
            // SwipeView keeps a non-current page `visible`, so the dashboard
            // can't detect being swiped away on its own.
            onScreen: rootSwipe.currentIndex === 0
            onOpenSettingsRequested: rootSwipe.currentIndex = 1
        }

        SettingsView {
            id: settingsView
            onCloseDashboardRequested: rootSwipe.currentIndex = 0
        }

        onCurrentIndexChanged: {
            if (currentIndex === 0)
                settingsView.resetToRoot()
        }
    }
}
