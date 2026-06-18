#include "mockraceboxprovider.h"
#include <cmath>

// Simulation parameters
static constexpr int    kLapCount          = 3;
static constexpr double kBaseLapS          = 30.0;   // 0:30.000
static constexpr double kLapImprovementS   = 0.20;   // each lap faster by this much
static constexpr double kAvgSpeedKmh       = 120.0;
static constexpr double kSpeedAmplitudeKmh = 40.0;   // ±40 km/h → range 80–160 km/h
static constexpr int    kTickHz            = 50;

static int lapTicks(int lapIndex)
{
    return static_cast<int>((kBaseLapS - lapIndex * kLapImprovementS) * kTickHz);
}

void MockRaceBoxProvider::defaultFinishLine(double &lat, double &lon, double &radiusM)
{
    lat     = kTrackLat + kTrackRadius; // phase=0 position on the circular path
    lon     = kTrackLon;
    radiusM = 20.0;
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
            m_timer.stop();
            emit connectionStateChanged(false);
            return;
        }
    }

    const int    tickInLap    = static_cast<int>(m_tick - m_lapStartTick);
    const int    thisLapTicks = lapTicks(m_currentLap);
    const double phase        = (static_cast<double>(tickInLap) / thisLapTicks) * 2.0 * M_PI;

    // Speed: two peaks per lap (two straights), average = kAvgSpeedKmh
    const double kmh      = kAvgSpeedKmh + kSpeedAmplitudeKmh * std::cos(phase * 2.0);
    const qint32 speedMmS = static_cast<qint32>(kmh * kMmSPerKmh);

    // Position: circular path — phase=0 puts the vehicle at the finish line
    const double lat = kTrackLat + kTrackRadius * std::cos(phase);
    const double lon = kTrackLon + kTrackRadius * std::sin(phase);

    // G-forces: lateral in corners, longitudinal under braking/acceleration
    const qint16 gx = static_cast<qint16>(-600.0 * std::sin(phase));        // lateral
    const qint16 gy = static_cast<qint16>( 300.0 * std::sin(phase * 2.0));  // longitudinal
    const qint16 gz = static_cast<qint16>(1000);                             // ~1g vertical

    RaceBoxData d;
    d.fixStatus  = 3;
    d.fixFlags   = 0x01;
    d.numSvs     = 12;
    d.latitude   = lat;
    d.longitude  = lon;
    d.speedMmS   = speedMmS;
    d.gForceXMg  = gx;
    d.gForceYMg  = gy;
    d.gForceZMg  = gz;
    d.batteryRaw = 85;

    emit dataReady(d);
}
