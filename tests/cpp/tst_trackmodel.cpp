#include <QTest>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>
#include <QDir>

#include "src/trackmodel.h"
#include "src/raceboxmodel.h"
#include "src/apppaths.h"

namespace {

// TrackModel::loadDatabase() checks this override file before falling back to
// the qrc-bundled DB; this test binary doesn't compile in resources.qrc, so
// seeding the override is the only way (without touching src/) to get
// multi-track data into a TrackModel instance under test.
void seedOverrideTrackDb()
{
    QDir().mkpath(AppPaths::dataDir());
    QFile f(AppPaths::dataFile("track-db.json"));
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "could not seed track-db.json");
    f.write(R"([
        {"id":"spa","name":"Spa-Francorchamps","country":"Belgium","c":[50.4372,5.9714]},
        {"id":"silverstone","name":"Silverstone","country":"UK","c":[52.0786,-1.0169]},
        {"id":"monza","name":"Monza","country":"Italy","c":[45.6156,9.2811]}
    ])");
}

QStringList idsOf(const QVariantList &tracks)
{
    QStringList ids;
    for (const QVariant &v : tracks)
        ids << v.toMap().value("id").toString();
    return ids;
}

}

class TestTrackModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void filteringBySearchAndCountry();
    void finishLineGlobalFallback();
};

void TestTrackModel::initTestCase()
{
    // Sandboxes QStandardPaths::AppDataLocation under a temp dir so this test
    // never touches the real user's ~/.local/share/bmw-e46-dash.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("bmw-e46-dash");
}

void TestTrackModel::init()
{
    seedOverrideTrackDb();
    QFile::remove(AppPaths::dataFile("tracks-user.json"));
}

void TestTrackModel::filteringBySearchAndCountry()
{
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50); // let the deferred (singleShot(0)) DB load run

    track.setSearchText("mon");
    QCOMPARE(idsOf(track.filteredTracks()), QStringList{"monza"});

    track.setSearchText("");
    track.setCountryFilter("UK");
    QCOMPARE(idsOf(track.filteredTracks()), QStringList{"silverstone"});

    track.setCountryFilter("ALL");
    QCOMPARE(idsOf(track.filteredTracks()).size(), 3);
}

void TestTrackModel::finishLineGlobalFallback()
{
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50);

    // No finish line stored yet for any track -> empty map.
    QVERIFY(track.finishLineFor("spa").isEmpty());

    // No track is active (m_activeTrackId is empty by default), so this is
    // learned as the GLOBAL finish line.
    track.onFinishLineLearned(0.1, 0.2, 0.3, 0.4);

    const QVariantMap fl = track.finishLineFor("spa"); // spa has no line of its own
    QVERIFY(!fl.isEmpty());
    QCOMPARE(fl.value("lat1").toDouble(), 0.1);
    QCOMPARE(fl.value("lon1").toDouble(), 0.2);
    QCOMPARE(fl.value("lat2").toDouble(), 0.3);
    QCOMPARE(fl.value("lon2").toDouble(), 0.4);

    // Directly asking for the global key returns the same gate.
    QCOMPARE(track.finishLineFor(QString()), fl);
}

QTEST_GUILESS_MAIN(TestTrackModel)
#include "tst_trackmodel.moc"
