import QtQuick 2.15
import QtTest 1.15
import RaceDash 1.0
import "../../qml/settings" as Settings

// INSTALL must first ask the user to keep the dash powered (engine running /
// battery on a charger) before the update starts. Driven against the real
// UpdateModel in mock mode (registered in tst_qml_runner.cpp).
TestCase {
    name: "InstallPowerConfirm"

    Component {
        id: pageComp
        Settings.AppVersionDetail {}
    }

    // Parentless, like tst_abslamp: a child of the never-visible TestCase item
    // would report every modal as invisible.
    function openPage() {
        const page = createTemporaryObject(pageComp, null)
        verify(page !== null)
        // The page's onCompleted runs checkConnection(); mock -> ReadyToCheck.
        tryCompare(updateModel, "state", Updates.ReadyToCheck, 3000)
        return page
    }

    function toUpdateAvailable() {
        updateModel.checkForUpdates()
        tryCompare(updateModel, "state", Updates.UpdateAvailable, 3000)
    }

    function cleanup() {
        if (updateModel.state === Updates.AwaitingPassword)
            updateModel.cancelPassword()
        updateModel.reset()
    }

    // clicked() is emitted directly below, which ignores `enabled` — so the
    // gate is asserted explicitly: INSTALL is only live while an update is
    // available, and OK hands over to the password prompt.
    function test_okProceedsToPasswordOnlyFromUpdateAvailable() {
        const page = openPage()
        const button = findChild(page, "installButton")
        const modal = findChild(page, "installPowerConfirm")
        verify(!button.enabled)

        toUpdateAvailable()
        verify(button.enabled)
        verify(!modal.visible)

        button.clicked()
        modal.confirmed()
        verify(!modal.visible)
        compare(updateModel.state, Updates.AwaitingPassword)
        verify(!button.enabled)
    }

    function test_installShowsWarningAndCancelAborts() {
        const page = openPage()
        toUpdateAvailable()
        const button = findChild(page, "installButton")
        const modal = findChild(page, "installPowerConfirm")

        button.clicked()
        verify(modal.visible)
        compare(modal.confirmLabel, "OK")
        compare(modal.cancelLabel, "CANCEL")
        verify(modal.message.indexOf("charger") !== -1)
        verify(modal.message.indexOf("engine") !== -1)
        // Nothing has started yet — no password prompt behind the warning.
        compare(updateModel.state, Updates.UpdateAvailable)

        modal.cancelled()
        verify(!modal.visible)
        compare(updateModel.state, Updates.UpdateAvailable)

        // Tapping INSTALL again brings the warning back.
        button.clicked()
        verify(modal.visible)
    }
}
