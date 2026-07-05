#include "mockraceboxprovider.h"
#include <cmath>

// Simulation parameters
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
    const double halfWidthDeg = 15.0 / 111132.0; // ~15 m each side (30 m gate)
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

    RaceBoxData d;
    d.fixStatus  = 3;
    d.fixFlags   = 0x01;
    d.numSvs     = 12;
    d.gForceZMg  = 1000; // ~1g vertical in both phases
    d.batteryRaw = 85;

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
        // speedMmS, gForceXMg, gForceYMg are zero-initialised
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
