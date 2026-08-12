import QtQuick 2.15

Rectangle {
    id: root

    property real   value:            0
    property real   maxValue:         100
    property string label:            ""
    property string unit:             ""
    property int    decimalPlaces:    0
    property real   warningThreshold: 999999
    property real   dangerThreshold:  999999
    // Opt-in low-side warning band (e.g. cold oil), shown with the same
    // treatment as warningThreshold but pulsing. Off by default so gauges that
    // don't want it are unaffected, and so a caller whose reading isn't valid
    // yet can switch the band off outright rather than passing a sentinel
    // threshold that both files would have to agree on.
    property bool   lowWarningEnabled:   false
    property real   lowWarningThreshold: 0
    property bool   compact:          false

    readonly property bool   isLowWarning: lowWarningEnabled && value < lowWarningThreshold
    readonly property bool   isWarning:    (value >= warningThreshold && value < dangerThreshold)
                                           || isLowWarning
    readonly property bool   isDanger:     value >= dangerThreshold

    // Danger always pulses; the low-side warning (e.g. cold oil) pulses too
    // since it demands the same urgency as danger, unlike the plain
    // high-side warning band which stays static.
    readonly property bool   isPulsing: isDanger || isLowWarning

    // Danger pulses noticeably faster than the low-side warning so the
    // higher-urgency state is distinguishable at a glance, not just by color.
    readonly property int    pulseDuration: isDanger ? 350 : 1000

    // Lets the owner suppress the pulse when the gauge isn't actually on
    // screen. The animation is infinite and repaints every vsync, so left
    // ungated it burns frames for the whole warm-up while the driver sits in
    // Settings. An item's own `visible` can't detect that — SwipeView leaves a
    // non-current page visible — so the dashboard passes the state down.
    property bool   pulseEnabled: true
    readonly property bool   pulseActive: isPulsing && pulseEnabled

    // Deliberately NOT `opacity` on the root: that fades the whole subtree,
    // including the reading itself — the one thing that must stay legible when
    // the car is overheating. Animating a plain real and applying it to the
    // card's fill and border only (the LedStrip flashAlpha idiom) keeps the
    // three Text nodes at full opacity, and keeps them out of the blended
    // render pass instead of dirtying the entire subtree every vsync.
    property real pulseAlpha: 1.0

    onPulseActiveChanged: if (!pulseActive) pulseAlpha = 1.0

    SequentialAnimation on pulseAlpha {
        running: root.pulseActive
        loops:   Animation.Infinite
        NumberAnimation { to: 0.3; duration: root.pulseDuration; easing.type: Easing.InOutSine }
        NumberAnimation { to: 1.0; duration: root.pulseDuration; easing.type: Easing.InOutSine }
    }

    readonly property string formattedValue: value.toFixed(decimalPlaces)

    // Shared accent colours — border and fill bar use the same bright shades.
    // Declared as `color` (not bare string literals) so withAlpha() can read
    // their .r/.g/.b channels; a string has no such components.
    readonly property color alertDanger:  "#ff3333"
    readonly property color alertWarning: "#ffd700"
    readonly property color fillDanger:   "#8b0000"
    readonly property color fillWarning:  "#b8860b"
    readonly property color fillNormal:   "#111111"
    readonly property color borderNormal: "#2a2a2a"

    function stateColor(danger, warning, normal) {
        return isDanger ? danger : isWarning ? warning : normal
    }

    function withAlpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    color:        withAlpha(stateColor(fillDanger, fillWarning, fillNormal), pulseAlpha)
    border.color: withAlpha(stateColor(alertDanger, alertWarning, borderNormal), pulseAlpha)
    border.width: 2
    radius: 3

    Text {
        anchors.top: parent.top
        anchors.topMargin: compact ? 6 : 12
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label
        color: "#888888"
        font.pixelSize: compact ? 11 : 16
        font.letterSpacing: 3
    }

    Text {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: compact ? -4 : -8
        text: root.formattedValue
        color: "#ffffff"
        font.pixelSize: compact ? 44 : 72
        font.bold: true
        font.family: "monospace"
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: compact ? 6 : 12
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.unit
        color: root.isWarning || root.isDanger ? "#cccccc" : "#555555"
        font.pixelSize: compact ? 11 : 15
        font.letterSpacing: 1
    }
}
