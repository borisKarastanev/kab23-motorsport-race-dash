#include "src/race/mockraceboxprovider.h"
#include <QDateTime>
#include <cmath>

// Simulation parameters. kLapCount is the number of speed-profile laps driven,
// not the number of *completed*, timed laps: onTick() switches to the parked
// position on the same tick that would otherwise produce the final lap's
// finish-line-crossing fix, so a run always completes kLapCount - 1 timed
// laps — 2, at 3. That's one short of the two *eligible* laps the optimal
// lap needs on a session's very first-ever run for this track, since the
// first lap completed can never be eligible for itself — it's the one
// RaceBoxModel derives the sector gates from (see
// RaceBoxModel::deriveSectorGates). From the second run on, though, those
// gates are persisted per track (TrackModel::onSectorGatesLearned) and
// restored at startup, so both of a 2-lap run's laps are eligible and the
// optimal lap shows up same as it would on a real track driven before.
static constexpr int    kLapCount          = 3;
static constexpr double kBaseLapS          = 15.0;   // 0:15.000
static constexpr double kLapImprovementS   = 0.20;   // each lap faster by this much
static constexpr double kAvgSpeedKmh       = 120.0;
static constexpr double kSpeedAmplitudeKmh = 40.0;   // ±40 km/h → range 80–160 km/h
static constexpr int    kTickHz            = 50;

static int lapTicks(int lapIndex)
{
    return static_cast<int>((kBaseLapS - lapIndex * kLapImprovementS) * kTickHz);
}

void MockRaceBoxProvider::defaultFinishLine(double &latA, double &lonA, double &latB, double &lonB)
{
    // At phase=0 the car sits at (kTrackLat+kTrackRadius, kTrackLon) moving in the
    // +longitude direction, so a gate spanning the track runs along latitude.
    const double centerLat = kTrackLat + kTrackRadius;
    const double centerLon = kTrackLon;
    const double halfWidthDeg = 12.5 / 111132.0; // ~12.5 m each side (25 m gate) — matches
                                                  // kLearnGateHalfWidthM in raceboxmodel.cpp
    latA = centerLat + halfWidthDeg; lonA = centerLon;
    latB = centerLat - halfWidthDeg; lonB = centerLon;
}

MockRaceBoxProvider::MockRaceBoxProvider(QObject *parent)
    : IRaceBoxProvider(parent)
{
    m_timer.setInterval(1000 / kTickHz);
    connect(&m_timer, &QTimer::timeout, this, &MockRaceBoxProvider::onTick);
}

void MockRaceBoxProvider::start()
{
    m_tick         = 0;
    m_currentLap   = 0;
    m_lapStartTick = 0;
    m_stopped      = false;
    emit connectionStateChanged(true);
    m_timer.start();
}

void MockRaceBoxProvider::stop()
{
    m_timer.stop();
    emit connectionStateChanged(false);
}

void MockRaceBoxProvider::onTick()
{
    ++m_tick;

    // Advance to next lap when current lap duration has elapsed
    const int currentLapTicks = lapTicks(m_currentLap);
    if (static_cast<int>(m_tick - m_lapStartTick) >= currentLapTicks) {
        ++m_currentLap;
        m_lapStartTick = m_tick;

        if (m_currentLap >= kLapCount) {
            m_stopped = true;
            m_lapStartTick = m_tick;
        }
    }

    // Value-initialized: RaceBoxData has no default member initializers, so a
    // plain `RaceBoxData d;` leaves every unassigned field indeterminate. The
    // stationary branch below assigns only lat/lon and relies on speedMmS being
    // zero — without the braces it inherits the previous tick's stack slot and
    // the "parked" phase reports the last lap's speed.
    RaceBoxData d{};
    d.fixStatus  = 3;
    d.fixFlags   = 0x01;
    d.numSvs     = 12;
    d.gForceZMg  = 1000; // ~1g vertical in both phases
    d.batteryRaw = 85;

    // RaceBoxData is hand-constructed here (no wire decode), so the GNSS time
    // block needs seeding too, or it would be left uninitialised. The mock's
    // wall clock is the dev box's real clock, which is exactly what TimeModel's
    // SYNC FROM GPS exercises against under --mock.
    const QDateTime utcNow = QDateTime::currentDateTimeUtc();
    d.gnssYear       = utcNow.date().year();
    d.gnssMonth      = utcNow.date().month();
    d.gnssDay        = utcNow.date().day();
    d.gnssHour       = utcNow.time().hour();
    d.gnssMinute     = utcNow.time().minute();
    d.gnssSecond     = utcNow.time().second();
    d.gnssValidFlags = 0x07; // validDate | validTime | fullyResolved

    // Stationary phase — 15 s at speed 0 so the Save Session button can be used
    if (m_stopped) {
        const int stoppedTicks = static_cast<int>(m_tick - m_lapStartTick);
        if (stoppedTicks > 15 * kTickHz) {
            m_timer.stop();
            emit connectionStateChanged(false);
            return;
        }

        d.latitude  = kTrackLat + kTrackRadius;
        d.longitude = kTrackLon;
        // speedMmS, gForceXMg, gForceYMg stay zero from the `RaceBoxData d{}`
        // value-initialization above — that's what makes this phase "parked".
        emit dataReady(d);
        return;
    }

    const int    tickInLap    = static_cast<int>(m_tick - m_lapStartTick);
    const int    thisLapTicks = lapTicks(m_currentLap);
    const double phase        = (static_cast<double>(tickInLap) / thisLapTicks) * 2.0 * M_PI;

    // Speed: two peaks per lap (two straights), average = kAvgSpeedKmh
    const double kmh      = kAvgSpeedKmh + kSpeedAmplitudeKmh * std::cos(phase * 2.0);

    // Position: circular path, offset by π so the lap seam (tickInLap wrap) sits at
    // the bottom of the circle. The car then crosses the finish-line gate cleanly
    // mid-lap (at the top) — a proper transversal crossing — instead of restarting
    // on the line, so the gate detector arms early and times several laps per run.
    const double angle    = phase + M_PI;
    d.latitude   = kTrackLat + kTrackRadius * std::cos(angle);
    d.longitude  = kTrackLon + kTrackRadius * std::sin(angle);
    d.speedMmS   = static_cast<qint32>(kmh * kMmSPerKmh);
    // G-forces: lateral in corners, longitudinal under braking/acceleration
    d.gForceXMg  = static_cast<qint16>(-600.0 * std::sin(phase));       // lateral
    d.gForceYMg  = static_cast<qint16>( 300.0 * std::sin(phase * 2.0)); // longitudinal

    emit dataReady(d);
}
