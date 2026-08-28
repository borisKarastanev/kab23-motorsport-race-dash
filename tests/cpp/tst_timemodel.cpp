#include <QTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QRegularExpression>

#include "src/device/timemodel.h"
#include "src/core/apppaths.h"

namespace {

// A small, fixed IANA-shaped id list — deliberately not the real system
// database, so these tests don't depend on which tz data happens to be
// installed on the machine running them.
const QList<QByteArray> kFixedIds = {
    "Africa/Cairo",
    "America/Argentina/Buenos_Aires",
    "America/New_York",
    "Europe/Amsterdam",
    "Europe/Sofia",
    "Europe/Zurich",
    "UTC",
};

// Puts fake `timedatectl` and `nmcli` shims at the front of PATH and logs every
// timedatectl invocation, so a real (mockMode=false) TimeModel can be driven
// through its actual subprocess path without any real system command running.
// Restores PATH on destruction.
struct FakeSystemTools {
    QTemporaryDir dir;
    QByteArray originalPath = qgetenv("PATH");

    bool install()
    {
        if (!dir.isValid())
            return false;
        const QString binDir = dir.filePath("bin");
        if (!QDir().mkpath(binDir))
            return false;

        auto write = [](const QString &path, const QByteArray &body) {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
                return false;
            f.write(body);
            f.close();
            return QFile::setPermissions(path,
                QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
        };

        if (!write(binDir + "/timedatectl",
                   "#!/bin/sh\necho \"$@\" >> \"$TIMEDATECTL_LOG\"\nexit 0\n"))
            return false;
        if (!write(binDir + "/nmcli", "#!/bin/sh\necho full\nexit 0\n"))
            return false;

        qputenv("TIMEDATECTL_LOG", logPath().toLocal8Bit());
        qputenv("PATH", binDir.toLocal8Bit() + ":" + originalPath);
        return true;
    }

    QString logPath() const { return dir.filePath("timedatectl.log"); }

    // Every timedatectl argv line, in call order. Empty when it was never run.
    QStringList calls() const
    {
        QFile log(logPath());
        if (!log.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};
        return QString::fromUtf8(log.readAll()).split('\n', Qt::SkipEmptyParts);
    }

    ~FakeSystemTools() { qputenv("PATH", originalPath); }
};

// The first-run bootstrap would otherwise call setSyncEnabled(true) on its own
// and interleave with whatever the test is asserting about the same log.
void suppressBootstrap()
{
    QSettings s(AppPaths::dataFile("time.conf"), QSettings::IniFormat);
    s.setValue("Time/Bootstrapped", true);
    s.sync();
}

}

class TestTimeModel : public QObject {
    Q_OBJECT

private slots:
    void init();

    void regionsFromIds_splitsOnFirstSlashOnly();
    void zonesForRegionFromIds_returnsSortedCitySuffixes();
    void formatUtcOffset_matchesWorkedExamples();
    void isKnownTimezoneId_checksAgainstTheGivenList();
    void validateZoneCandidate_rejectsAHostileResponse();
    void validateZoneCandidate_trimsAndAcceptsAKnownZone();

    void manualDateTime_disablesNtpBeforeSettingTime();
    void manualDateTime_rejectsAnImpossibleDateWithoutTouchingNtp();
    void manualDateTime_keepsTheSecondsItIsGiven();
};

void TestTimeModel::init()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("bmw-e46-dash");
}

void TestTimeModel::regionsFromIds_splitsOnFirstSlashOnly()
{
    const QStringList regions = TimeModel::regionsFromIds(kFixedIds);
    // "UTC" has no '/' and is excluded — it doesn't fit the region/city drill-in.
    QCOMPARE(regions, QStringList({"Africa", "America", "Europe"}));
}

void TestTimeModel::zonesForRegionFromIds_returnsSortedCitySuffixes()
{
    // "America/Argentina/Buenos_Aires" keeps everything after the FIRST slash,
    // not just the last path component.
    QCOMPARE(TimeModel::zonesForRegionFromIds(kFixedIds, "America"),
              QStringList({"Argentina/Buenos_Aires", "New_York"}));
    QCOMPARE(TimeModel::zonesForRegionFromIds(kFixedIds, "Europe"),
              QStringList({"Amsterdam", "Sofia", "Zurich"}));
    QCOMPARE(TimeModel::zonesForRegionFromIds(kFixedIds, "Antarctica"), QStringList());
}

void TestTimeModel::formatUtcOffset_matchesWorkedExamples()
{
    QCOMPARE(TimeModel::formatUtcOffset(0), QStringLiteral("+00:00"));
    QCOMPARE(TimeModel::formatUtcOffset(3 * 3600), QStringLiteral("+03:00"));
    QCOMPARE(TimeModel::formatUtcOffset(-5 * 3600), QStringLiteral("-05:00"));
    QCOMPARE(TimeModel::formatUtcOffset(5 * 3600 + 30 * 60), QStringLiteral("+05:30"));
    QCOMPARE(TimeModel::formatUtcOffset(-9 * 3600 - 30 * 60), QStringLiteral("-09:30"));
}

void TestTimeModel::isKnownTimezoneId_checksAgainstTheGivenList()
{
    QVERIFY(TimeModel::isKnownTimezoneId("Europe/Sofia", kFixedIds));
    QVERIFY(!TimeModel::isKnownTimezoneId("Mars/Colony_One", kFixedIds));
    QVERIFY(!TimeModel::isKnownTimezoneId("", kFixedIds));
}

void TestTimeModel::validateZoneCandidate_rejectsAHostileResponse()
{
    // A geolocation response is third-party text handed straight toward a
    // subprocess argument (timedatectl set-timezone <id>) — this is the one
    // guard between the two.
    QCOMPARE(TimeModel::validateZoneCandidate("; rm -rf /", kFixedIds), QString());
    QCOMPARE(TimeModel::validateZoneCandidate("$(reboot)", kFixedIds), QString());
    QCOMPARE(TimeModel::validateZoneCandidate("", kFixedIds), QString());
}

void TestTimeModel::validateZoneCandidate_trimsAndAcceptsAKnownZone()
{
    QCOMPARE(TimeModel::validateZoneCandidate("Europe/Sofia\n", kFixedIds),
              QStringLiteral("Europe/Sofia"));
    QCOMPARE(TimeModel::validateZoneCandidate("  America/New_York  ", kFixedIds),
              QStringLiteral("America/New_York"));
}

void TestTimeModel::manualDateTime_disablesNtpBeforeSettingTime()
{
    // Real (non-mock) TimeModel against the fake binaries, rather than
    // mockMode=true: mock mode's setManualDateTime never shells out at all (it
    // must never touch this machine's real clock), so it can't demonstrate the
    // set-ntp-before-set-time ordering that timedatectl itself requires.
    FakeSystemTools tools;
    QVERIFY(tools.install());
    suppressBootstrap();

    TimeModel model(/*mockMode=*/false, /*raceBox=*/nullptr);
    model.start(); // deferred off the boot path in main.cpp; nothing polls until it runs
    model.setManualDateTime(2024, 3, 15, 9, 30);
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);

    const QStringList lines = tools.calls();
    int ntpOffLine = -1, setTimeLine = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (ntpOffLine < 0 && lines[i].startsWith("set-ntp false"))
            ntpOffLine = i;
        if (setTimeLine < 0 && lines[i].startsWith("set-time"))
            setTimeLine = i;
    }
    QVERIFY2(ntpOffLine >= 0, "set-ntp false was never called");
    QVERIFY2(setTimeLine >= 0, "set-time was never called");
    QVERIFY(ntpOffLine < setTimeLine);
    QVERIFY(lines[setTimeLine].contains("2024-03-15 09:30:00"));
    QCOMPARE(model.errorText(), QString());
}

void TestTimeModel::manualDateTime_rejectsAnImpossibleDateWithoutTouchingNtp()
{
    // The failure this guards: set-ntp false runs FIRST, so letting a date
    // timedatectl will reject reach set-time leaves the device with sync
    // disabled and the clock unchanged — worse than never tapping SET.
    FakeSystemTools tools;
    QVERIFY(tools.install());
    suppressBootstrap();

    TimeModel model(/*mockMode=*/false, /*raceBox=*/nullptr);
    model.start(); // deferred off the boot path in main.cpp; nothing polls until it runs
    // Let the constructor's own status chain finish first, so the only calls
    // that could still land in the log afterwards are ones setManualDateTime
    // made — and so this doesn't assert before a spurious call had a chance
    // to appear.
    QTRY_VERIFY_WITH_TIMEOUT(!tools.calls().filter(QRegularExpression("^show")).isEmpty(), 5000);

    model.setManualDateTime(2026, 2, 31, 9, 30); // 31 February
    QTRY_VERIFY_WITH_TIMEOUT(!model.errorText().isEmpty(), 5000);
    QTest::qWait(200); // any subprocess it wrongly started would log by now

    for (const QString &line : tools.calls()) {
        QVERIFY2(!line.startsWith("set-ntp"), "NTP was touched for an invalid date");
        QVERIFY2(!line.startsWith("set-time"), "an invalid date reached timedatectl");
    }
    QVERIFY(!model.busy());
}

void TestTimeModel::manualDateTime_keepsTheSecondsItIsGiven()
{
    // syncFromGps() passes the GPS seconds through here. Hardcoding ":00" in
    // the stamp would round every GPS sync back to the top of the minute.
    FakeSystemTools tools;
    QVERIFY(tools.install());
    suppressBootstrap();

    TimeModel model(/*mockMode=*/false, /*raceBox=*/nullptr);
    model.start(); // deferred off the boot path in main.cpp; nothing polls until it runs
    model.setManualDateTime(2024, 3, 15, 9, 30, 47);
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);

    bool sawStamp = false;
    for (const QString &line : tools.calls()) {
        if (line.startsWith("set-time")) {
            QVERIFY2(line.contains("2024-03-15 09:30:47"), qPrintable("set-time was: " + line));
            sawStamp = true;
        }
    }
    QVERIFY2(sawStamp, "set-time was never called");
}

QTEST_GUILESS_MAIN(TestTimeModel)
#include "tst_timemodel.moc"
