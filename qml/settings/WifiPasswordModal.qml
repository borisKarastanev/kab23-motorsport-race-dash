import QtQuick 2.15
import RaceDash 1.0

// Password prompt for joining a secured Wi-Fi network, shown by WifiDetail.qml.
// Thin binding of the generic PasswordEntryModal to networkModel's connect
// state machine.
PasswordEntryModal {
    visible: networkModel.connectState === Net.NeedPassword
          || networkModel.connectState === Net.WrongPassword
          || networkModel.connectState === Net.Connecting

    title: "PASSWORD FOR \"" + networkModel.pendingSsid + "\""
    busy: networkModel.connectState === Net.Connecting
    busyLabel: "CONNECTING…"
    wrongPass: networkModel.connectState === Net.WrongPassword

    onSubmitted: function(password) { networkModel.submitPassword(password) }
    onCancelled: networkModel.cancelConnect()
}
