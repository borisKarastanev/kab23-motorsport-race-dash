#include <QTest>
#include <QSignalSpy>
#include <QFile>

#include "src/device/updatemodel.h"

class TestUpdateModel : public QObject {
    Q_OBJECT

private slots:
    void init();
    void countStageMarkersCountsStepCalls();
    void isInstallSummaryLineMatchesAnnotations();
    void takeCompleteLinesReassemblesSplitChunks();
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

void TestUpdateModel::isInstallSummaryLineMatchesAnnotations()
{
    // install.sh's own indented "(...)" annotations and the closing total.
    QVERIFY(UpdateModel::isInstallSummaryLine("    (Building application: 412s)"));
    QVERIFY(UpdateModel::isInstallSummaryLine("    (build jobs: 1 of 4 cores, 512 MB available)"));
    QVERIFY(UpdateModel::isInstallSummaryLine("    (ccache: 30 hits, 2 misses)"));
    QVERIFY(UpdateModel::isInstallSummaryLine("Total install.sh time: 731s"));

    // Ordinary tool output is not copied into the Device Log.
    QVERIFY(!UpdateModel::isInstallSummaryLine("=== Building application ==="));
    QVERIFY(!UpdateModel::isInstallSummaryLine("[ 42%] Building CXX object main.cpp.o"));
    QVERIFY(!UpdateModel::isInstallSummaryLine("  (indented by two, not install.sh's four)"));
    QVERIFY(!UpdateModel::isInstallSummaryLine(""));
}

void TestUpdateModel::takeCompleteLinesReassemblesSplitChunks()
{
    QString buf;
    // A stage marker split across two reads is only returned once whole.
    QCOMPARE(UpdateModel::takeCompleteLines(buf, "=== Building app"), QStringList());
    QCOMPARE(buf, QString("=== Building app"));
    QCOMPARE(UpdateModel::takeCompleteLines(buf, "lication ===\nnext"),
             QStringList({"=== Building application ==="}));
    QCOMPARE(buf, QString("next"));

    // CRLF endings are stripped so anchored patterns still match.
    QCOMPARE(UpdateModel::takeCompleteLines(buf, " line\r\n    (x: 1s)\r\n"),
             QStringList({"next line", "    (x: 1s)"}));
    QVERIFY(buf.isEmpty());
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
    QVERIFY(model.errorText().isEmpty());

    // A failed restart returns to the success screen with a reason, so
    // RESTART NOW can be retried — never stuck on Rebooting.
    qputenv("DASH_MOCK_UPDATE", "reboot-fail");
    model.requestReboot();
    QCOMPARE(model.state(), UpdateModel::Rebooting);
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), UpdateModel::InstallSucceeded, 2000);
    QVERIFY(model.errorText().startsWith("Restart failed:"));
    qunsetenv("DASH_MOCK_UPDATE");
}

QTEST_GUILESS_MAIN(TestUpdateModel)
#include "tst_updatemodel.moc"
