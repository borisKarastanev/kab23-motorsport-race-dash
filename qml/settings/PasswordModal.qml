import QtQuick 2.15
import RaceDash 1.0

// Sudo/administrator password prompt for the app-update flow. Thin binding of
// the generic PasswordEntryModal to updateModel's state machine. The parent
// (AppVersionDetail) controls `visible`.
PasswordEntryModal {
    title: "ADMINISTRATOR PASSWORD REQUIRED"
    busy: updateModel.state === Updates.ValidatingPassword
    busyLabel: "VERIFYING…"
    wrongPass: updateModel.state === Updates.WrongPassword

    onSubmitted: function(password) { updateModel.submitPassword(password) }
    onCancelled: updateModel.cancelPassword()
}
