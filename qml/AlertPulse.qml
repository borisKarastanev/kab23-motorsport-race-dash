import QtQuick 2.15

// Shared alert pulse + palette for Gauge, LedStrip and OilPressureWarning, so
// the danger colours and blink behaviour can't drift apart between them.
// Non-visual: a QtObject, not an Item, so owners don't carry an extra node.
//
// Owners apply `alpha` to their fill/border only — never `opacity` on their
// root, which would fade the reading itself, the one thing that must stay
// legible while the alert is showing. Owners fold their own visibility into
// `active` so the infinite animation stops while hidden.
QtObject {
    id: pulse

    property bool active: false
    // Danger pulses noticeably faster than a warning so the higher-urgency
    // state is distinguishable at a glance, not just by colour.
    property bool danger: true
    property int  duration: danger ? 350 : 1000
    property real minAlpha: 0.3
    property int  easingType: Easing.InOutSine
    property real alpha: 1.0

    // Declared as `color` (not bare string literals) so withAlpha() can read
    // their .r/.g/.b channels; a string has no such components.
    readonly property color alertDanger:  "#ff3333"
    readonly property color alertWarning: "#ffd700"
    readonly property color fillDanger:   "#8b0000"
    readonly property color fillWarning:  "#b8860b"

    function withAlpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    onActiveChanged: if (!active) alpha = 1.0

    property SequentialAnimation animation: SequentialAnimation {
        running: pulse.active
        loops:   Animation.Infinite
        NumberAnimation {
            target: pulse; property: "alpha"
            to: pulse.minAlpha; duration: pulse.duration; easing.type: pulse.easingType
        }
        NumberAnimation {
            target: pulse; property: "alpha"
            to: 1.0; duration: pulse.duration; easing.type: pulse.easingType
        }
    }
}
