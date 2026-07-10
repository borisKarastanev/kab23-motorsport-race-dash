import QtQuick 2.15
import QtTest 1.15
import "../../qml" as App

TestCase {
    name: "LedStrip"

    Component {
        id: stripComp
        App.LedStrip {
            width: 400
            height: 40
        }
    }

    // LedStrip's only declared child is a Row containing a Repeater; the
    // Row's children[] holds the delegate items FIRST and the Repeater
    // object itself LAST (confirmed empirically), so find it by its
    // distinguishing itemAt() method rather than assuming a fixed index.
    function ledItems(strip) {
        const row = strip.children[0]
        let repeater = null
        for (let i = 0; i < row.children.length; i++) {
            if (typeof row.children[i].itemAt === "function") {
                repeater = row.children[i]
                break
            }
        }
        const items = []
        for (let i = 0; i < repeater.count; i++)
            items.push(repeater.itemAt(i))
        return items
    }

    function test_allUnlitAtIdleRpm() {
        const strip = createTemporaryObject(stripComp, null, { rpm: 0, ledCount: 10 })
        const leds = ledItems(strip)
        compare(leds.length, 10)
        for (let i = 0; i < leds.length; i++)
            compare(leds[i].lit, false)
    }

    function test_litCountGrowsWithRpm() {
        const strip = createTemporaryObject(stripComp, null, {
            ledCount: 10, rpm: 6100,
            pair0Rpm: 5800, pair1Rpm: 6000, pair2Rpm: 6200, pair3Rpm: 6500,
            pair4Rpm: 6600, allBlueRpm: 6750
        })
        const leds = ledItems(strip)
        let litCount = 0
        for (let i = 0; i < leds.length; i++)
            if (leds[i].lit) litCount++
        // rpm(6100) clears pair0(5800) and pair1(6000) thresholds only ->
        // 2 pairs x 2 mirrored LEDs = 4 lit.
        compare(litCount, 4)
    }

    function test_mirrorPairingSymmetric() {
        const strip = createTemporaryObject(stripComp, null, {
            ledCount: 10, rpm: 6300,
            pair0Rpm: 5800, pair1Rpm: 6000, pair2Rpm: 6200, pair3Rpm: 6500,
            pair4Rpm: 6600, allBlueRpm: 6750
        })
        const leds = ledItems(strip)
        for (let i = 0; i < 5; i++)
            compare(leds[i].lit, leds[9 - i].lit)
    }

    function test_allBlueAboveThreshold() {
        const strip = createTemporaryObject(stripComp, null, {
            ledCount: 10, rpm: 6800,
            pair0Rpm: 5800, pair1Rpm: 6000, pair2Rpm: 6200, pair3Rpm: 6500,
            pair4Rpm: 6600, allBlueRpm: 6750
        })
        compare(strip.isAllBlue, true)
        const leds = ledItems(strip)
        for (let i = 0; i < leds.length; i++) {
            compare(leds[i].lit, true)
            compare(leds[i].shouldFlash, true) // all-blue flashes every lit LED
        }
    }

    function test_centerFlashBelowAllBlue() {
        const strip = createTemporaryObject(stripComp, null, {
            ledCount: 10, rpm: 6650,
            pair0Rpm: 5800, pair1Rpm: 6000, pair2Rpm: 6200, pair3Rpm: 6500,
            pair4Rpm: 6600, allBlueRpm: 6750
        })
        compare(strip.isAllBlue, false)
        compare(strip.isCenterFlashing, true)
        const leds = ledItems(strip)
        // Only the centre pair (indices 4 and 5 for ledCount=10) should flash.
        for (let i = 0; i < leds.length; i++)
            compare(leds[i].shouldFlash, i === 4 || i === 5)
    }
}
