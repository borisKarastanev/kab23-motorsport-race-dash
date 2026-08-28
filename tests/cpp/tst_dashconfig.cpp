#include <QTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>
#include <QSettings>

#include "src/device/dashconfig.h"
#include "src/core/apppaths.h"

namespace {

QString configPath()
{
    return AppPaths::dataFile("dashboard.conf");
}

// Writes raw Gauges-group keys directly via QSettings, bypassing DashConfig,
// so a test can seed exactly the pre-load .conf state (legacy tokens,
// deliberately colliding positions, etc.) that DashConfig::load() must then
// reconcile.
void seedGaugesGroup(const QVariantMap &kv)
{
    QSettings s(configPath(), QSettings::IniFormat);
    s.beginGroup("Gauges");
    for (auto it = kv.constBegin(); it != kv.constEnd(); ++it)
        s.setValue(it.key(), it.value());
    s.endGroup();
}

}

class TestDashConfig : public QObject {
    Q_OBJECT

private slots:
    void init();

    void limiterRescaleMatchesWorkedExamples();
    void limiterRescaleIsIdempotentAndMonotonic();
    void limiterClampsToSaneBand();

    void legacyPositionsMigratePerEntityRow();
    void invalidPositionFallsBackToDefault();
    void collidingLoadedPositionsAreResolved();
    void centerPositionSurvivesReloadForTopRowEntity();

    void coolantThresholdOrderingEnforced();
    void invertedThresholdsAreCorrectedOnLoad();

    void setEntityPositionSwapsOccupant();
    void enablingEntityAvoidsCollision();

    void persistenceRoundTrips();
    void debouncedPersistFiresViaTimer();
};

void TestDashConfig::init()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("bmw-e46-dash");
    QFile::remove(configPath());
}

void TestDashConfig::limiterRescaleMatchesWorkedExamples()
{
    DashConfig cfg;

    cfg.setLimiterRpm(7000);
    QCOMPARE(cfg.pair0Rpm(), 5971);
    QCOMPARE(cfg.pair1Rpm(), 6176);
    QCOMPARE(cfg.pair2Rpm(), 6382);
    QCOMPARE(cfg.pair3Rpm(), 6691);
    QCOMPARE(cfg.pair4Rpm(), 6794);
    QCOMPARE(cfg.allBlueRpm(), 6949);

    cfg.setLimiterRpm(7200); // above 7000
    QCOMPARE(cfg.pair0Rpm(), 6141);
    QCOMPARE(cfg.pair1Rpm(), 6353);
    QCOMPARE(cfg.pair2Rpm(), 6565);
    QCOMPARE(cfg.pair3Rpm(), 6882);
    QCOMPARE(cfg.pair4Rpm(), 6988);
    QCOMPARE(cfg.allBlueRpm(), 7147);

    cfg.setLimiterRpm(6300); // below 6500
    QCOMPARE(cfg.pair0Rpm(), 5374);
    QCOMPARE(cfg.pair1Rpm(), 5559);
    QCOMPARE(cfg.pair2Rpm(), 5744);
    QCOMPARE(cfg.pair3Rpm(), 6022);
    QCOMPARE(cfg.pair4Rpm(), 6115);
    QCOMPARE(cfg.allBlueRpm(), 6254);
}

void TestDashConfig::limiterRescaleIsIdempotentAndMonotonic()
{
    DashConfig cfg;
    cfg.setLimiterRpm(7000);
    const int p0 = cfg.pair0Rpm(), p4 = cfg.pair4Rpm(), blue = cfg.allBlueRpm();

    cfg.setLimiterRpm(7000); // same value again — must not drift
    QCOMPARE(cfg.pair0Rpm(), p0);
    QCOMPARE(cfg.pair4Rpm(), p4);
    QCOMPARE(cfg.allBlueRpm(), blue);

    // Ordering must stay monotonic across the whole clamped range.
    QVERIFY(cfg.pair0Rpm() < cfg.pair1Rpm());
    QVERIFY(cfg.pair1Rpm() < cfg.pair2Rpm());
    QVERIFY(cfg.pair2Rpm() < cfg.pair3Rpm());
    QVERIFY(cfg.pair3Rpm() < cfg.pair4Rpm());
    QVERIFY(cfg.pair4Rpm() < cfg.allBlueRpm());
    QVERIFY(cfg.allBlueRpm() < cfg.limiterRpm());
}

void TestDashConfig::limiterClampsToSaneBand()
{
    DashConfig cfg;
    cfg.setLimiterRpm(100);
    QCOMPARE(cfg.limiterRpm(), 5000);

    cfg.setLimiterRpm(50000);
    QCOMPARE(cfg.limiterRpm(), 12000);
}

void TestDashConfig::legacyPositionsMigratePerEntityRow()
{
    // Old 3-column scheme (left|center|right) with no row concept. Values
    // chosen so none collide, isolating the migration mapping itself from
    // the separate collision-resolution pass.
    seedGaugesGroup({
        { "RPM.Position",     "left" },
        { "Speed.Position",   "right" },
        { "Coolant.Position", "center" },
        { "OilTemp.Position", "center" },
    });

    DashConfig cfg;
    QCOMPARE(cfg.rpmPosition(),      QStringLiteral("top-left"));
    QCOMPARE(cfg.speedPosition(),    QStringLiteral("top-right"));
    QCOMPARE(cfg.coolantPosition(),  QStringLiteral("center"));    // center row + center column is the lone exception token
    QCOMPARE(cfg.oilTempPosition(),  QStringLiteral("top-center"));
    QCOMPARE(cfg.lapTimerPosition(), QStringLiteral("bottom-left")); // no legacy key existed — new default
}

void TestDashConfig::invalidPositionFallsBackToDefault()
{
    seedGaugesGroup({ { "RPM.Position", "nonsense-token" } });

    DashConfig cfg;
    QCOMPARE(cfg.rpmPosition(), QStringLiteral("top-left")); // compiled-in default
}

void TestDashConfig::collidingLoadedPositionsAreResolved()
{
    // RPM, Speed and OilTemp all deliberately claim top-left, and Gear
    // separately claims OilTemp's eventual cell. Priority order (rpm, speed,
    // coolant, oiltemp, gear, laptimer) means each entity either keeps its
    // cell or gets bumped to the next free one, purely based on what
    // earlier-priority entities have already claimed by the time its turn
    // comes — never on what a later entity's raw (still unresolved) config
    // wanted.
    seedGaugesGroup({
        { "RPM.Position",     "top-left" },
        { "Speed.Position",   "top-left" },
        { "Gear.Visible",     true },
        { "Gear.Position",    "top-right" },
        { "OilTemp.Position", "top-left" },
    });

    DashConfig cfg;
    QCOMPARE(cfg.rpmPosition(),     QStringLiteral("top-left"));   // 1st — keeps it
    QCOMPARE(cfg.speedPosition(),   QStringLiteral("top-center")); // 2nd — top-left taken, next free
    QCOMPARE(cfg.coolantPosition(), QStringLiteral("center-left")); // never collided — untouched default
    // 4th: top-left/top-center already claimed by rpm/speed, top-right is
    // still free at this point in the walk (gear hasn't been processed yet —
    // resolution never looks ahead at later entities' not-yet-resolved raw
    // state, only at cells already claimed by earlier-priority entities).
    QCOMPARE(cfg.oilTempPosition(), QStringLiteral("top-right"));
    // 5th: gear's own raw "top-right" is now taken (by oiltemp), so it moves
    // to the next free cell.
    QCOMPARE(cfg.gearPosition(),    QStringLiteral("center"));
    QCOMPARE(cfg.lapTimerPosition(), QStringLiteral("bottom-left"));
}

void TestDashConfig::centerPositionSurvivesReloadForTopRowEntity()
{
    // Regression: a top-row entity (RPM) deliberately placed on the exact
    // middle cell must persist as "center" across a restart. The bare token
    // "center" collides with the legacy column name, so an earlier version
    // re-migrated it to "top-center" on every reload. Schema-versioning the
    // config (persist() writes SchemaVersion=2) is what disambiguates it.
    {
        DashConfig cfg;                          // writes a v2 default config
        cfg.setEntityPosition("rpm", "center");
        QCOMPARE(cfg.rpmPosition(), QStringLiteral("center"));
    } // dtor flushes persist() with SchemaVersion=2

    DashConfig reloaded;
    QCOMPARE(reloaded.rpmPosition(), QStringLiteral("center")); // NOT "top-center"
}

void TestDashConfig::coolantThresholdOrderingEnforced()
{
    DashConfig cfg; // defaults: warning 95, danger 105

    cfg.setCoolantWarningTemp(200); // above danger — clamp to danger - 1
    QCOMPARE(cfg.coolantWarningTemp(), 104.0);

    cfg.setCoolantDangerTemp(50); // below warning — clamp to warning + 1
    QCOMPARE(cfg.coolantDangerTemp(), 105.0);

    QVERIFY(cfg.coolantWarningTemp() < cfg.coolantDangerTemp());
}

void TestDashConfig::invertedThresholdsAreCorrectedOnLoad()
{
    // A hand-edited (or partially upgraded) config can store warning >= danger.
    // The runtime setters keep warning < danger; load() must restore the same
    // invariant instead of trusting the file, clamping warning just below danger.
    {
        QSettings s(configPath(), QSettings::IniFormat);
        s.setValue("SchemaVersion", 2);
        s.beginGroup("Gauges");
        s.setValue("Coolant.WarningTemp", 110); // inverted: warning above danger
        s.setValue("Coolant.DangerTemp",  105);
        s.setValue("OilTemp.WarningTemp",  140);
        s.setValue("OilTemp.DangerTemp",   130);
        s.endGroup();
        s.sync();
    }

    DashConfig cfg;
    QVERIFY(cfg.coolantWarningTemp() < cfg.coolantDangerTemp());
    QCOMPARE(cfg.coolantWarningTemp(), 104.0); // clamped to danger - 1
    QVERIFY(cfg.oilWarningTemp() < cfg.oilDangerTemp());
    QCOMPARE(cfg.oilWarningTemp(), 129.0);
}

void TestDashConfig::setEntityPositionSwapsOccupant()
{
    DashConfig cfg; // defaults: rpm=top-left, coolant=center-left

    cfg.setEntityPosition("rpm", "center-left"); // coolant's cell
    QCOMPARE(cfg.rpmPosition(), QStringLiteral("center-left"));
    QCOMPARE(cfg.coolantPosition(), QStringLiteral("top-left")); // swapped into rpm's old cell
}

void TestDashConfig::enablingEntityAvoidsCollision()
{
    DashConfig cfg;
    QCOMPARE(cfg.gearVisible(), false); // starts disabled, stored at "center"

    cfg.setEntityPosition("rpm", "center"); // gear invisible, so this is a plain move, not a swap
    QCOMPARE(cfg.rpmPosition(), QStringLiteral("center"));

    cfg.setGearVisible(true); // now collides with rpm at "center" — must relocate, not block
    QCOMPARE(cfg.gearVisible(), true);
    QCOMPARE(cfg.gearPosition(), QStringLiteral("top-left")); // first free cell in scan order
    QCOMPARE(cfg.rpmPosition(), QStringLiteral("center"));    // rpm itself untouched
}

void TestDashConfig::persistenceRoundTrips()
{
    {
        DashConfig cfg;
        cfg.setLimiterRpm(7100);
        cfg.setCoolantWarningTemp(90);
        cfg.setRpmVisible(false);
        cfg.setEntityPosition("oiltemp", "bottom-right");
    } // destructor flushes the debounced write (no event loop running here)

    DashConfig reloaded;
    QCOMPARE(reloaded.limiterRpm(), 7100);
    QCOMPARE(reloaded.coolantWarningTemp(), 90.0);
    QCOMPARE(reloaded.rpmVisible(), false);
    QCOMPARE(reloaded.oilTempPosition(), QStringLiteral("bottom-right"));
    QCOMPARE(reloaded.pair0Rpm(), qRound(7100 * (5800.0 / 6800.0))); // pairs are always re-derived, never read back
}

void TestDashConfig::debouncedPersistFiresViaTimer()
{
    DashConfig cfg;
    cfg.setLimiterRpm(6900);

    // With an event loop running, the debounce timer (500 ms) fires and
    // writes dashboard.conf on its own — no destructor flush involved. The
    // model is deliberately still alive here so a pass proves the TIMER path.
    QTest::qWait(700);

    QSettings s(configPath(), QSettings::IniFormat);
    s.beginGroup("LedStrip");
    QCOMPARE(s.value("LimiterRpm").toInt(), 6900);
}

QTEST_GUILESS_MAIN(TestDashConfig)
#include "tst_dashconfig.moc"
