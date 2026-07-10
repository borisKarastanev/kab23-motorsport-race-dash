import QtQuick 2.15
import QtTest 1.15
import "../../qml" as App

TestCase {
    name: "GearIndicator"

    Component {
        id: gearComp
        App.GearIndicator {}
    }

    function test_neutral() {
        const g = createTemporaryObject(gearComp, null, { gear: 0 })
        compare(g.displayText, "N")
        compare(g.gearColor, "#555555")
    }

    function test_reverse() {
        const g = createTemporaryObject(gearComp, null, { gear: -1 })
        compare(g.displayText, "R")
        compare(g.gearColor, "#ff3333")
    }

    function test_forwardGear() {
        const g = createTemporaryObject(gearComp, null, { gear: 4 })
        compare(g.displayText, "4")
        compare(g.gearColor, "#ffffff")
    }
}
