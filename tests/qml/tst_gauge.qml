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
    }

    function test_dangerBand() {
        const gauge = createTemporaryObject(gaugeComp, null,
            { warningThreshold: 100, dangerThreshold: 120, value: 130 })
        compare(gauge.isDanger, true)
        compare(gauge.isWarning, false) // isWarning requires value < dangerThreshold too
    }

    function test_formattedValueRespectsDecimalPlaces() {
        const gauge = createTemporaryObject(gaugeComp, null,
            { decimalPlaces: 1, value: 87.66 })
        compare(gauge.formattedValue, "87.7")
    }
}
