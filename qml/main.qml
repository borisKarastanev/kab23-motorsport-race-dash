import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls.Basic

Window {
    id: appWindow
    width: 800
    height: 480
    visibility: kioskMode ? Window.FullScreen : Window.AutomaticVisibility
    visible: true
    title: "BMW E46 Dashboard"
    color: "#0a0a0a"

    // The scaled/rotated scene below. Anything that must cover the whole
    // screen (a modal reparenting itself out of its page) parents to this, not
    // to Window.contentItem: contentItem is outside the scene's transform, so
    // it would draw — and take touches — in the panel's native orientation.
    readonly property Item uiScene: scene

    // The UI is laid out in a 800x480 design space (the original 7" panel).
    // Every pixel size in the QML is a design-space size, so larger panels
    // (e.g. the 1280x720 Raspberry Pi Touch Display 2) are served by scaling
    // the whole scene uniformly: scale = min(w/800, h/480), and the scene's
    // logical size is the (post-rotation) window size divided by that scale.
    // 800x480 gives exactly 1.0 (pixel-identical to before); 1280x720 gives
    // 1.5 with a 853x480 logical canvas — layouts that fill their parent
    // absorb the extra 53 px of width. Text uses distance-field rendering, so
    // it stays crisp.
    //
    // displayRotation (DASH_ROTATION, see main.cpp) rotates the scene
    // clockwise for natively-portrait panels: at 90/270 the window's width and
    // height swap roles. Pointer events are mapped through these transforms,
    // so touch lands where it is drawn with no calibration needed.
    Item {
        id: scene
        readonly property int rot: displayRotation
        readonly property bool swapped: rot === 90 || rot === 270
        readonly property real availW: swapped ? appWindow.height : appWindow.width
        readonly property real availH: swapped ? appWindow.width : appWindow.height
        readonly property real uiScale: Math.max(0.5, Math.min(availW / 800, availH / 480))
        width: availW / uiScale
        height: availH / uiScale
        // Applied in list order: scale about the origin, rotate clockwise
        // about the origin, then translate the rotated scene back on screen.
        transform: [
            Scale { xScale: scene.uiScale; yScale: scene.uiScale },
            Rotation { angle: scene.rot },
            Translate {
                x: scene.rot === 90 || scene.rot === 180 ? appWindow.width : 0
                y: scene.rot === 180 || scene.rot === 270 ? appWindow.height : 0
            }
        ]

        SwipeView {
            id: rootSwipe
            anchors.fill: parent
            interactive: false
            // Hidden, not torn down, under the oil pressure warning: whatever page
            // or modal was open (a half-typed password, an install in progress)
            // is still there when the warning clears.
            visible: !oilPressureWarning.visible

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

        // Low oil pressure emergency — above every page, so no page or overlay
        // can ever cover it, and nothing underneath needs to know about it.
        OilPressureWarning {
            id: oilPressureWarning
            anchors.fill: parent
            z: 1000
            visible: dataModel.oilPressureWarning
        }
    }
}
