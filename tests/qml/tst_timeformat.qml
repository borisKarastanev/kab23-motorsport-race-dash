import QtQuick 2.15
import QtTest 1.15
import "../../qml/TimeFormat.js" as Fmt

TestCase {
    name: "TimeFormat"

    function test_zeroOrNegativeShowsPlaceholder() {
        compare(Fmt.formatMs(0), "--:--.---")
        compare(Fmt.formatMs(-500), "--:--.---")
    }

    function test_subMinute() {
        compare(Fmt.formatMs(1234), "0:01.234")
    }

    function test_multiMinute() {
        compare(Fmt.formatMs(125678), "2:05.678")
    }

    function test_secondsAndMillisPadding() {
        compare(Fmt.formatMs(60005), "1:00.005")
    }
}
