import QtQuick 2.15
import QtQuick.Layouts 1.15

// Full-screen manual date/time entry for the offline path on the Date & Time
// page. Reuses StepperRow (± per field) rather than a Tumbler — StepperRow is
// a RowLayout, and ModalScaffold's default slot is a plain Column, so the five
// rows are wrapped in their own ColumnLayout to make Layout.fillWidth work.
ModalScaffold {
    id: root
    maxPanelWidth: 320

    signal submitted(int year, int month, int day, int hour, int minute)
    signal cancelled()

    property int year: 2026
    property int month: 1
    property int day: 1
    property int hour: 0
    property int minute: 0

    onVisibleChanged: {
        if (!visible)
            return
        const now = new Date()
        year = now.getFullYear()
        month = now.getMonth() + 1
        day = now.getDate()
        hour = now.getHours()
        minute = now.getMinutes()
    }

    function pad(n) { return (n < 10 ? "0" : "") + n }
    function wrap(value, delta, min, max) {
        const span = max - min + 1
        return min + ((value - min + delta) % span + span) % span
    }
    // Day 0 of the NEXT month is the last day of this one, so this handles
    // February and leap years without a table.
    function daysInMonth(y, mo) { return new Date(y, mo, 0).getDate() }
    // Re-clamp after a YEAR or MONTH step: 31 January -> February must become
    // the 28th/29th, not stay at an impossible 31st. Without this the modal can
    // emit a date timedatectl rejects, and TimeModel disables NTP before it
    // finds that out.
    function clampDay() {
        root.day = Math.min(root.day, root.daysInMonth(root.year, root.month))
    }

    Text {
        text: "SET DATE & TIME"
        color: "#cccccc"
        font.pixelSize: 12
        font.bold: true
        font.family: "monospace"
        font.letterSpacing: 1
        width: parent.width
    }

    ColumnLayout {
        width: parent.width
        spacing: 4

        StepperRow {
            label: "YEAR"
            value: String(root.year)
            // Leap years: 29 February -> a non-leap year must re-clamp to the 28th.
            onDecrementRequested: { root.year = Math.max(2000, root.year - 1); root.clampDay() }
            onIncrementRequested: { root.year = Math.min(2099, root.year + 1); root.clampDay() }
        }
        StepperRow {
            label: "MONTH"
            value: root.pad(root.month)
            onDecrementRequested: { root.month = root.wrap(root.month, -1, 1, 12); root.clampDay() }
            onIncrementRequested: { root.month = root.wrap(root.month, 1, 1, 12); root.clampDay() }
        }
        StepperRow {
            label: "DAY"
            value: root.pad(root.day)
            // Wraps within the selected month's real length, so the stepper can
            // never reach a 30 February.
            onDecrementRequested: root.day = root.wrap(root.day, -1, 1,
                                                        root.daysInMonth(root.year, root.month))
            onIncrementRequested: root.day = root.wrap(root.day, 1, 1,
                                                        root.daysInMonth(root.year, root.month))
        }
        StepperRow {
            label: "HOUR"
            value: root.pad(root.hour)
            onDecrementRequested: root.hour = root.wrap(root.hour, -1, 0, 23)
            onIncrementRequested: root.hour = root.wrap(root.hour, 1, 0, 23)
        }
        StepperRow {
            label: "MINUTE"
            value: root.pad(root.minute)
            onDecrementRequested: root.minute = root.wrap(root.minute, -1, 0, 59)
            onIncrementRequested: root.minute = root.wrap(root.minute, 1, 0, 59)
        }
    }

    Row {
        width: parent.width
        spacing: 8

        TextButton {
            width: (parent.width - 8) / 2
            text: "CANCEL"
            textColor: "#888888"
            pressedColor: "#111111"
            border.color: "#2a2a2a"
            onClicked: root.cancelled()
        }

        TextButton {
            width: (parent.width - 8) / 2
            text: "SET"
            textColor: "#00cc44"
            pressedColor: "#112211"
            border.color: "#2a4a2a"
            onClicked: root.submitted(root.year, root.month, root.day, root.hour, root.minute)
        }
    }
}
