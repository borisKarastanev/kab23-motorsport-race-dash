import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    Component.onCompleted: deviceStatsModel.active = true
    Component.onDestruction: deviceStatsModel.active = false

    function memText() {
        if (deviceStatsModel.memTotalMb < 0) return "—"
        return deviceStatsModel.memUsedMb + " / " + deviceStatsModel.memTotalMb + " MB"
    }

    function diskText() {
        if (deviceStatsModel.diskTotalGb < 0) return "—"
        return deviceStatsModel.diskFreeGb.toFixed(1) + " GB free / "
             + deviceStatsModel.diskTotalGb.toFixed(1) + " GB"
    }

    readonly property color tempColor:
        deviceStatsModel.cpuTempC < 0    ? "#cccccc"
      : deviceStatsModel.cpuTempC > 80.0 ? "#cc4444"
      : deviceStatsModel.cpuTempC > 70.0 ? "#cc8800"
      : "#cccccc"

    // Static rows bound directly to model properties: on each poll tick only the
    // changed values update, rather than a Repeater rebuilding every delegate.
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        StatRow {
            label: "CPU TEMPERATURE"
            value: deviceStatsModel.cpuTempC < 0 ? "—" : deviceStatsModel.cpuTempC.toFixed(1) + " °C"
            valueColor: root.tempColor
        }
        StatRow {
            label: "CPU LOAD"
            value: deviceStatsModel.cpuLoadPct < 0 ? "—" : deviceStatsModel.cpuLoadPct.toFixed(0) + " %"
        }
        StatRow {
            label: "MEMORY"
            value: root.memText()
        }
        StatRow {
            label: "DISK"
            value: root.diskText()
        }
        StatRow {
            label: "UPTIME"
            value: deviceStatsModel.uptimeText
        }
        StatRow {
            label: "IP ADDRESS"
            value: deviceStatsModel.ipAddress
        }
        StatRow {
            label: "THROTTLE STATE"
            value: deviceStatsModel.throttleText
            valueColor: deviceStatsModel.throttleText === "OK" ? "#00cc44"
                      : deviceStatsModel.throttleText === "N/A" ? "#cccccc" : "#cc8800"
        }

        Item { Layout.fillHeight: true }
    }
}
