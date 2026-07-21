#include <QTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include "src/sessionmodel.h"
#include "src/candatamodel.h"
#include "src/raceboxmodel.h"
#include "src/trackmodel.h"
#include "src/apppaths.h"

namespace {

// Seeds sessions.json directly (bypassing the lap-completion signal chain
// saveCurrentSession() depends on) so deleteSession() can be exercised
// against known fixture records.
void seedSessions(const QString &json)
{
    QDir().mkpath(AppPaths::dataDir());
    QFile f(AppPaths::dataFile("sessions.json"));
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "could not seed sessions.json");
    f.write(json.toUtf8());
}

QJsonArray readPersistedSessions()
{
    QFile f(AppPaths::dataFile("sessions.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).array();
}

const char *kTwoSpaSessions = R"([
    {"title":"2026-07-20 14:30","timestampIso":"2026-07-20T14:30:00","lapMs":[90000],
     "lapPaths":[],"topSpeedKmh":180,"maxOilC":95.0,"maxCoolantC":88.0,
     "maxLatG":1.1,"maxLonG":0.9,"trackId":"spa","trackName":"Spa-Francorchamps"},
    {"title":"2026-07-21 09:00","timestampIso":"2026-07-21T09:00:00","lapMs":[91000],
     "lapPaths":[],"topSpeedKmh":175,"maxOilC":90.0,"maxCoolantC":85.0,
     "maxLatG":1.0,"maxLonG":0.8,"trackId":"spa","trackName":"Spa-Francorchamps"}
])";

// One Spa session and one Monza session — deleting the sole Spa record must
// leave Spa empty (navigate to the tracks list) while Monza is untouched.
const char *kSpaAndMonzaSessions = R"([
    {"title":"2026-07-20 14:30","timestampIso":"2026-07-20T14:30:00","lapMs":[90000],
     "lapPaths":[],"topSpeedKmh":180,"maxOilC":95.0,"maxCoolantC":88.0,
     "maxLatG":1.1,"maxLonG":0.9,"trackId":"spa","trackName":"Spa-Francorchamps"},
    {"title":"2026-07-21 09:00","timestampIso":"2026-07-21T09:00:00","lapMs":[91000],
     "lapPaths":[],"topSpeedKmh":175,"maxOilC":90.0,"maxCoolantC":85.0,
     "maxLatG":1.0,"maxLonG":0.8,"trackId":"monza","trackName":"Monza"}
])";

}

class TestSessionModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void deleteSessionRemovesMatchingRecord();
    void deleteSessionPersistsRemoval();
    void deleteSessionRebuildsGroupCount();
    void deleteSessionUnknownTimestampIsNoOp();

    // Post-delete navigation predicate (SettingsDetail pops one step back vs.
    // all the way to the tracks list based on this):
    void trackKeepsSessionsAfterDeletingOneOfSeveral();
    void trackHasNoSessionsAfterDeletingLastOne();
    void deletingLastSessionForTrackLeavesOtherTracks();
};

void TestSessionModel::initTestCase()
{
    // Sandboxes QStandardPaths::AppDataLocation under a temp dir so this test
    // never touches the real user's sessions.json.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("bmw-e46-dash");
}

void TestSessionModel::init()
{
    QFile::remove(AppPaths::dataFile("sessions.json"));
}

void TestSessionModel::deleteSessionRemovesMatchingRecord()
{
    seedSessions(kTwoSpaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);
    QCOMPARE(sessions.sessions().size(), 2);

    QSignalSpy spy(&sessions, &SessionModel::sessionsChanged);
    sessions.deleteSession("2026-07-20T14:30:00");

    QCOMPARE(spy.count(), 1);
    QCOMPARE(sessions.sessions().size(), 1);
    QCOMPARE(sessions.sessions().first().toMap().value("timestampIso").toString(),
             QStringLiteral("2026-07-21T09:00:00"));
}

void TestSessionModel::deleteSessionPersistsRemoval()
{
    seedSessions(kTwoSpaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);

    sessions.deleteSession("2026-07-20T14:30:00");

    const QJsonArray persisted = readPersistedSessions();
    QCOMPARE(persisted.size(), 1);
    QCOMPARE(persisted.first().toObject().value("timestampIso").toString(),
             QStringLiteral("2026-07-21T09:00:00"));
}

void TestSessionModel::deleteSessionRebuildsGroupCount()
{
    seedSessions(kTwoSpaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);
    QCOMPARE(sessions.sessionGroups().first().toMap().value("count").toInt(), 2);

    sessions.deleteSession("2026-07-20T14:30:00");

    QCOMPARE(sessions.sessionGroups().size(), 1);
    QCOMPARE(sessions.sessionGroups().first().toMap().value("count").toInt(), 1);
}

void TestSessionModel::deleteSessionUnknownTimestampIsNoOp()
{
    seedSessions(kTwoSpaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);

    QSignalSpy spy(&sessions, &SessionModel::sessionsChanged);
    sessions.deleteSession("1999-01-01T00:00:00");

    QCOMPARE(spy.count(), 0);
    QCOMPARE(sessions.sessions().size(), 2);
}

void TestSessionModel::trackKeepsSessionsAfterDeletingOneOfSeveral()
{
    seedSessions(kTwoSpaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);

    sessions.deleteSession("2026-07-20T14:30:00");

    // One Spa session remains → Details pops one step back to the track's list.
    QVERIFY(sessions.hasSessionsForTrack("spa"));
}

void TestSessionModel::trackHasNoSessionsAfterDeletingLastOne()
{
    seedSessions(kSpaAndMonzaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);
    QVERIFY(sessions.hasSessionsForTrack("spa"));

    sessions.deleteSession("2026-07-20T14:30:00"); // the sole Spa session

    // No Spa session left → Details pops all the way to the tracks list.
    QVERIFY(!sessions.hasSessionsForTrack("spa"));
}

void TestSessionModel::deletingLastSessionForTrackLeavesOtherTracks()
{
    seedSessions(kSpaAndMonzaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);

    sessions.deleteSession("2026-07-20T14:30:00"); // sole Spa session

    // Deleting Spa's last session must not disturb Monza's — its group survives.
    QVERIFY(sessions.hasSessionsForTrack("monza"));
    QCOMPARE(sessions.sessions().size(), 1);
    QCOMPARE(sessions.sessionGroups().size(), 1);
    QCOMPARE(sessions.sessionGroups().first().toMap().value("trackId").toString(),
             QStringLiteral("monza"));
}

QTEST_GUILESS_MAIN(TestSessionModel)
#include "tst_sessionmodel.moc"
