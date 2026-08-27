#include <QTest>
#include <limits>

#include "src/optimallap.h"

using OptimalLap::BestSector;
using OptimalLap::compute;
using OptimalLap::SectoredLap;

class TestOptimalLap : public QObject {
    Q_OBJECT

private slots:
    void bestsFromThreeDifferentLaps();
    void allBestsFromOneLapReportsMatch();
    void tiesGoToLowerLapNumber();
    void lapsWithMissingSplitsAreIgnored();
    void oneEligibleLapYieldsNoResult();
    void noLapsYieldsNoResult();
    void optimalCanBeStrictlyFasterThanEveryActualLap();
    void sectorCountIsAParameter();
    void maxValuedSectorTimesDoNotDereferenceNull();
};

// Mirrors the worked example in ~/development/optimal-lap-algorithm.md §1:
// three sectors, four laps, the optimal stitched from laps 4, 1 and 2.
void TestOptimalLap::bestsFromThreeDifferentLaps()
{
    const QList<SectoredLap> laps{
        {1, {28410, 30720, 27350}}, // 1:26.48
        {2, {28300, 31060, 26440}}, // 1:25.80 — best actual lap
        {3, {28770, 31440, 27120}}, // 1:27.33 — wins nothing
        {4, {27840, 31660, 27200}}, // 1:26.70
    };

    const auto result = compute(laps, 3);
    QVERIFY(result.has_value());
    QCOMPARE(result->lapMs, qint64(27840 + 30720 + 26440));
    QCOMPARE(result->sectors.size(), 3);
    QCOMPARE(result->sectors[0].lapNumber, 4);
    QCOMPARE(result->sectors[0].sectorMs, qint64(27840));
    QCOMPARE(result->sectors[1].lapNumber, 1);
    QCOMPARE(result->sectors[1].sectorMs, qint64(30720));
    QCOMPARE(result->sectors[2].lapNumber, 2);
    QCOMPARE(result->sectors[2].sectorMs, qint64(26440));
    QCOMPARE(result->matchesLapNumber, -1); // sectors came from three different laps

    // The best actual lap (2) is strictly slower than the stitched optimal —
    // the whole point of the rule.
    QVERIFY(result->lapMs < 85800); // lap 2's 1:25.80
}

void TestOptimalLap::allBestsFromOneLapReportsMatch()
{
    const QList<SectoredLap> laps{
        {1, {28000, 30000, 27000}},
        {2, {29000, 31000, 28000}},
    };

    const auto result = compute(laps, 3);
    QVERIFY(result.has_value());
    QCOMPARE(result->matchesLapNumber, 1); // lap 1 won every sector — the optimal *is* lap 1
    QCOMPARE(result->lapMs, qint64(28000 + 30000 + 27000));
}

void TestOptimalLap::tiesGoToLowerLapNumber()
{
    const QList<SectoredLap> laps{
        {1, {28000, 30000, 27000}},
        {3, {28000, 31000, 26500}}, // ties lap 1's S1 exactly
        {2, {29000, 30000, 27500}}, // ties lap 1's S2 exactly
    };

    const auto result = compute(laps, 3);
    QVERIFY(result.has_value());
    // S1 tied between laps 1 and 3 -> lap 1 (lower number).
    QCOMPARE(result->sectors[0].lapNumber, 1);
    // S2 tied between laps 1 and 2 -> lap 1 (lower number).
    QCOMPARE(result->sectors[1].lapNumber, 1);
    QCOMPARE(result->sectors[2].lapNumber, 3); // outright fastest, no tie
}

void TestOptimalLap::lapsWithMissingSplitsAreIgnored()
{
    const QList<SectoredLap> laps{
        {1, {28000, 30000, 27000}},
        {2, {}},                    // missed a gate entirely — ineligible
        {3, {20000, 20000}},        // short — ineligible, however fast it looks
        {4, {27000, 29000, 26000}},
    };

    const auto result = compute(laps, 3);
    QVERIFY(result.has_value());
    // Only laps 1 and 4 are eligible; lap 3's implausibly fast times must
    // never win a sector despite being numerically lowest.
    QCOMPARE(result->sectors[0].lapNumber, 4);
    QCOMPARE(result->sectors[1].lapNumber, 4);
    QCOMPARE(result->sectors[2].lapNumber, 4);
    QCOMPARE(result->matchesLapNumber, 4);
}

void TestOptimalLap::oneEligibleLapYieldsNoResult()
{
    const QList<SectoredLap> laps{
        {1, {28000, 30000, 27000}},
        {2, {}}, // ineligible
    };

    QVERIFY(!compute(laps, 3).has_value());
}

void TestOptimalLap::noLapsYieldsNoResult()
{
    QVERIFY(!compute({}, 3).has_value());
}

void TestOptimalLap::optimalCanBeStrictlyFasterThanEveryActualLap()
{
    const QList<SectoredLap> laps{
        {1, {27000, 31000, 28000}}, // 1:26.00
        {2, {28000, 30000, 27000}}, // 1:25.00
    };

    const auto result = compute(laps, 3);
    QVERIFY(result.has_value());
    QCOMPARE(result->lapMs, qint64(27000 + 30000 + 27000)); // 1:24.00
    QVERIFY(result->lapMs < 85000); // faster than both actual laps
    QVERIFY(result->lapMs < 86000);
}

void TestOptimalLap::sectorCountIsAParameter()
{
    // A surveyed 4-sector track needs no code change here — only the caller's
    // sectorCount argument differs.
    const QList<SectoredLap> laps{
        {1, {20000, 21000, 19000, 18000}},
        {2, {19500, 21500, 18500, 17500}},
    };

    const auto result = compute(laps, /*sectorCount=*/4);
    QVERIFY(result.has_value());
    QCOMPARE(result->sectors.size(), 4);

    // Asked for 3 sectors, these same 4-entry laps are ineligible — a wrong
    // count yields no optimal lap rather than a wrong one.
    QVERIFY(!compute(laps, 3).has_value());
}

void TestOptimalLap::maxValuedSectorTimesDoNotDereferenceNull()
{
    // qint64's max passes the "positive" eligibility test, so it reaches the
    // per-sector scan with every comparison a tie — the degenerate input for
    // any implementation that picks a winner by scanning. An earlier version
    // seeded that scan with a max sentinel and dereferenced the not-yet-assigned
    // winner on the first tie; std::min_element always returns a real element,
    // so the hazard is now structural, and this pins the tie-break at the
    // extreme regardless of how the winner is chosen.
    constexpr qint64 kMax = std::numeric_limits<qint64>::max();
    const QList<SectoredLap> laps{
        {2, {kMax, kMax, kMax}},
        {1, {kMax, kMax, kMax}},
    };

    const auto result = compute(laps, 3);
    QVERIFY(result.has_value());
    // Every sector is a tie, so all three go to the lower lap number.
    QCOMPARE(result->sectors[0].lapNumber, 1);
    QCOMPARE(result->sectors[1].lapNumber, 1);
    QCOMPARE(result->sectors[2].lapNumber, 1);
    QCOMPARE(result->matchesLapNumber, 1);
}

QTEST_APPLESS_MAIN(TestOptimalLap)
#include "tst_optimallap.moc"
