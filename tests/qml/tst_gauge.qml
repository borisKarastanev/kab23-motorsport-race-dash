import QtQuick 2.15
import QtTest 1.15
import "../../qml" as App

TestCase {
    name: "Gauge"

    Component {
        id: gaugeComp
        App.Gauge {}
    }

    function test_normalBelowWarning() {
        const gauge = createTemporaryObject(gaugeComp, null,
            { warningThreshold: 100, dangerThreshold: 120, value: 50 })
        verify(gauge !== null)
        compare(gauge.isWarning, false)
        compare(gauge.isDanger, false)
    }

    function test_warningBand() {
        const gauge = createTemporaryObject(gaugeComp, null,
            { warningThreshold: 100, dangerThreshold: 120, value: 110 })
        compare(gauge.isWarning, true)
        compare(gauge.isDanger, false)
        compare(gauge.isPulsing, false) // the high-side band stays static
    }

    function test_dangerBand() {
        const gauge = createTemporaryObject(gaugeComp, null,
            { warningThreshold: 100, dangerThreshold: 120, value: 130 })
        compare(gauge.isDanger, true)
        compare(gauge.isWarning, false) // isWarning requires value < dangerThreshold too
        compare(gauge.isPulsing, true)
    }

    function test_formattedValueRespectsDecimalPlaces() {
        const gauge = createTemporaryObject(gaugeComp, null,
            { decimalPlaces: 1, value: 87.66 })
        compare(gauge.formattedValue, "87.7")
    }

    function test_lowWarningBelowThreshold() {
        const gauge = createTemporaryObject(gaugeComp, null,
            { warningThreshold: 120, dangerThreshold: 135,
              lowWarningEnabled: true, lowWarningThreshold: 60, value: 45 })
        compare(gauge.isLowWarning, true)
        compare(gauge.isWarning, true)
        compare(gauge.isDanger, false)
        compare(gauge.isPulsing, true)
    }

    function test_lowWarningNotTriggeredAtOrAboveThreshold() {
        const gauge = createTemporaryObject(gaugeComp, null,
            { warningThreshold: 120, dangerThreshold: 135,
              lowWarningEnabled: true, lowWarningThreshold: 60, value: 60 })
        compare(gauge.isLowWarning, false)
        compare(gauge.isWarning, false)
        compare(gauge.isPulsing, false)
    }

    // The band is opt-in, so a caller whose reading isn't valid yet switches it
    // off outright rather than passing a sentinel threshold.
    function test_lowWarningSuppressedWhenDisabled() {
        const gauge = createTemporaryObject(gaugeComp, null,
            { lowWarningEnabled: false, lowWarningThreshold: 60, value: 45 })
        compare(gauge.isLowWarning, false)
        compare(gauge.isPulsing, false)
    }

    function test_dangerPulsesFasterThanLowWarning() {
        const dangerGauge = createTemporaryObject(gaugeComp, null,
            { warningThreshold: 100, dangerThreshold: 120, value: 130 })
        const lowWarningGauge = createTemporaryObject(gaugeComp, null,
            { lowWarningEnabled: true, lowWarningThreshold: 60, value: 45 })
        verify(dangerGauge.pulseDuration < lowWarningGauge.pulseDuration)
    }

    // The pulse is infinite and repaints every vsync, so the owner must be able
    // to stop it when the gauge is swiped off screen.
    function test_pulseSuppressedWhenPulseDisabled() {
        const gauge = createTemporaryObject(gaugeComp, null,
            { warningThreshold: 100, dangerThreshold: 120, value: 130,
              pulseEnabled: false })
        compare(gauge.isPulsing, true)    // state is still danger...
        compare(gauge.pulseActive, false) // ...but nothing animates
        compare(gauge.pulseAlpha, 1.0)
    }

    // The reading must stay fully legible while the card pulses — pulsing the
    // root's opacity would fade the number too, which is the one thing that
    // has to remain readable during an overheat.
    function test_pulseFadesCardButNotTheReading() {
        const gauge = createTemporaryObject(gaugeComp, null,
            { warningThreshold: 100, dangerThreshold: 120, value: 130 })
        verify(gauge.pulseActive)

        // Root opacity is never animated, so the whole subtree stays opaque.
        compare(gauge.opacity, 1.0)

        // Drive the pulse to its dimmest point and confirm the fade lands on
        // the card's fill/border alpha only.
        gauge.pulseAlpha = 0.3
        compare(gauge.color.a,        0.3)
        compare(gauge.border.color.a, 0.3)
        // Colour channels are untouched — only alpha moves.
        fuzzyCompare(gauge.color.r, gauge.fillDanger.r, 0.001)
        // The reading itself never fades.
        compare(gauge.opacity, 1.0)

        gauge.pulseAlpha = 1.0
        compare(gauge.color.a, 1.0)
    }
}
