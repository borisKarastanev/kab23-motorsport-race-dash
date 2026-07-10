import QtQuick 2.15
import QtTest 1.15
import "../../qml/settings" as Settings

TestCase {
    name: "DisplaySettings"

    Component {
        id: pageComp
        Settings.DisplaySettings {}
    }

    // DisplaySettings' only declared child is a ColumnLayout containing a
    // StatRow, the brightness Slider, a hint Text, and a spacer Item. Search
    // by distinguishing properties rather than assuming array position.
    function findSlider(page) {
        const col = page.children[0]
        for (let i = 0; i < col.children.length; i++) {
            const c = col.children[i]
            if (c.from !== undefined && c.to !== undefined && c.value !== undefined)
                return c
        }
        return null
    }

    function test_sliderRangeAndBindsFromModel() {
        displayModel.brightness = 55
        const page = createTemporaryObject(pageComp, null)
        const slider = findSlider(page)
        verify(slider !== null)
        compare(slider.from, 0)
        compare(slider.to, 100)
        compare(slider.value, 55)
    }

    function test_modelChangeUpdatesSlider() {
        const page = createTemporaryObject(pageComp, null)
        const slider = findSlider(page)
        displayModel.brightness = 30
        compare(slider.value, 30)
    }

    function test_movingSliderWritesBackToModel() {
        const page = createTemporaryObject(pageComp, null)
        const slider = findSlider(page)
        slider.value = 77
        slider.moved()
        compare(displayModel.brightness, 77)
    }
}
