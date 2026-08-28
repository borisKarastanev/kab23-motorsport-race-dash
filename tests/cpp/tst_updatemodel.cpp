#include <QTest>
#include <QSignalSpy>
#include <QFile>

#include "src/device/updatemodel.h"

class TestUpdateModel : public QObject {
    Q_OBJECT

private slots:
    void init();
    void countStageMarkersCountsStepCalls();
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

void TestUpdateModel::countStageMarkersCountsStepCalls()
{
    QCOMPARE(UpdateModel::countStageMarkers(""), 0);

    QCOMPARE(UpdateModel::countStageMarkers(R"(step "Installing dependencies")"), 1);

    // Realistic install.sh shape: comments, blank lines, actual shell logic
    // between step calls, and a line that MENTIONS "step" inside a comment
    // (must not be miscounted — the marker requires the line to literally
    // start with `step "`).
    const QString script = R"(#!/bin/bash
set -e

step "Installing dependencies"
sudo apt install -y qt6-base-dev

# This comment talks about a step "not a real call" and must be ignored
step "Priming fontconfig cache"
fc-cache -f

step "Building application"
)";
    QCOMPARE(UpdateModel::countStageMarkers(script), 3);

    // Sanity-check against the real install.sh: catches the marker regex
    // silently matching zero real lines (e.g. if install.sh's step() call
    // style ever changes), which synthetic-only input above could miss.
    QFile realScript(DASH_INSTALL_SH);
    QVERIFY2(realScript.open(QIODevice::ReadOnly | QIODevice::Text), "could not open real install.sh");
    QVERIFY(UpdateModel::countStageMarkers(QString::fromUtf8(realScript.readAll())) > 0);
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
