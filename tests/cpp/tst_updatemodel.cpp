#include <QTest>
#include <QSignalSpy>

#include "src/updatemodel.h"

class TestUpdateModel : public QObject {
    Q_OBJECT

private slots:
    void init();
    void checkConnectionMockReachesReadyToCheck();
    void checkForUpdatesMockFindsUpdateAvailable();
    void checkForUpdatesMockUpToDate();
    void wrongPasswordThenCancel();
    void wrongPasswordThenCorrectPassword_fullInstallCycle();
};

void TestUpdateModel::init()
{
    qunsetenv("DASH_MOCK_UPDATE");
}

void TestUpdateModel::checkConnectionMockReachesReadyToCheck()
{
    UpdateModel model(/*mockMode=*/true);
    QSignalSpy spy(&model, &UpdateModel::stateChanged);

    model.checkConnection();
    QCOMPARE(model.state(), UpdateModel::CheckingConnection);

    QVERIFY(spy.wait(2000));
    QCOMPARE(model.state(), UpdateModel::ReadyToCheck);
}

void TestUpdateModel::checkForUpdatesMockFindsUpdateAvailable()
{
    UpdateModel model(/*mockMode=*/true);
    model.checkConnection();
    QSignalSpy spy(&model, &UpdateModel::stateChanged);
    QVERIFY(spy.wait(2000)); // -> ReadyToCheck

    model.checkForUpdates();
    QCOMPARE(model.state(), UpdateModel::Checking);

    QVERIFY(spy.wait(2000));
    QCOMPARE(model.state(), UpdateModel::UpdateAvailable);
    QCOMPARE(model.latestVersion(), QStringLiteral("v9.9.9"));
}

void TestUpdateModel::checkForUpdatesMockUpToDate()
{
    qputenv("DASH_MOCK_UPDATE", "latest");

    UpdateModel model(/*mockMode=*/true);
    model.checkConnection();
    QSignalSpy spy(&model, &UpdateModel::stateChanged);
    QVERIFY(spy.wait(2000));

    model.checkForUpdates();
    QVERIFY(spy.wait(2000));
    QCOMPARE(model.state(), UpdateModel::UpToDate);

    qunsetenv("DASH_MOCK_UPDATE");
}

void TestUpdateModel::wrongPasswordThenCancel()
{
    UpdateModel model(/*mockMode=*/true);
    model.checkConnection();
    QSignalSpy spy(&model, &UpdateModel::stateChanged);
    QVERIFY(spy.wait(2000));
    model.checkForUpdates();
    QVERIFY(spy.wait(2000)); // UpdateAvailable

    model.startInstall();
    QCOMPARE(model.state(), UpdateModel::AwaitingPassword);

    model.submitPassword("wrong");
    QCOMPARE(model.state(), UpdateModel::ValidatingPassword);
    QVERIFY(spy.wait(2000));
    QCOMPARE(model.state(), UpdateModel::WrongPassword);

    model.cancelPassword();
    QCOMPARE(model.state(), UpdateModel::UpdateAvailable);
}

void TestUpdateModel::wrongPasswordThenCorrectPassword_fullInstallCycle()
{
    UpdateModel model(/*mockMode=*/true);
    model.checkConnection();
    QSignalSpy spy(&model, &UpdateModel::stateChanged);
    QVERIFY(spy.wait(2000));
    model.checkForUpdates();
    QVERIFY(spy.wait(2000)); // UpdateAvailable
    model.startInstall();    // AwaitingPassword

    model.submitPassword("wrong");
    QVERIFY(spy.wait(2000));
    QCOMPARE(model.state(), UpdateModel::WrongPassword);

    QSignalSpy progressSpy(&model, &UpdateModel::installProgressChanged);
    model.submitPassword("pass");
    QVERIFY(spy.wait(2000));
    QCOMPARE(model.state(), UpdateModel::Installing);
    QVERIFY(progressSpy.count() >= 1);

    // Mock install runs through 8 stages at 1.5s each — wait for the full
    // cycle to reach a terminal state. Slow but exercises the real state
    // machine end to end, which is exactly what users see in Settings.
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), UpdateModel::InstallSucceeded, 15000);
    QCOMPARE(model.installProgress(), 1.0);
}

QTEST_GUILESS_MAIN(TestUpdateModel)
#include "tst_updatemodel.moc"
