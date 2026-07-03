#include "raceboxmodel.h"
#include "logging.h"
#include <cmath>

static constexpr double kEarthRadiusM = 6371000.0;

RaceBoxModel::RaceBoxModel(QObject *parent)
    : QObject(parent)
{
    m_notifyTimer.setInterval(100); // 10 Hz
    connect(&m_notifyTimer, &QTimer::timeout, this, &RaceBoxModel::emitNotifications);
    m_notifyTimer.start();
}

qint64 RaceBoxModel::currentLapMs() const
{
    if (!m_lapTimerRunning) return 0;
    return m_lapTimer.elapsed();
}

void RaceBoxModel::setFinishLine(double lat, double lon, double radiusM)
{
    if (lat == 0.0 && lon == 0.0) return; // unset
    m_finishLineLat  = lat;
    m_finishLineLon  = lon;
    m_finishRadiusM  = radiusM;
    m_finishLineSet  = true;
    m_dirty |= kDirtyFinishLine;
}

void RaceBoxModel::learnFinishLineHere()
{
    if (!m_hasFix) return;
    setFinishLine(m_lastLat, m_lastLon, m_finishRadiusM);
    qCInfo(lcRaceBox) << "Finish line set at" << m_lastLat << m_lastLon;
    emit finishLineLearned(m_lastLat, m_lastLon);
}

void RaceBoxModel::onConnectionStateChanged(bool connected)
{
    if (m_connected == connected) return;
    m_connected = connected;
    m_dirty |= kDirtyConnected;
    if (!connected) {
        m_hasFix = false;
        m_dirty |= kDirtyFix;
    }
}

void RaceBoxModel::onData(const RaceBoxData &d)
{
    const bool fix = (d.fixStatus >= 2) && (d.fixFlags & 0x01);
    if (m_hasFix != fix) {
        m_hasFix = fix;
        m_dirty |= kDirtyFix;
        if (fix)
            qCInfo(lcRaceBox) << "GPS fix acquired — SVs:" << d.numSvs;
        else
            qCInfo(lcRaceBox) << "GPS fix lost";
    }

    if (m_satellites != d.numSvs) { m_satellites = d.numSvs; m_dirty |= kDirtySvs; }

    const double gx = d.gForceXMg / 1000.0;
    const double gy = d.gForceYMg / 1000.0;
    const double gz = d.gForceZMg / 1000.0;
    if (m_gForceX != gx || m_gForceY != gy || m_gForceZ != gz) {
        m_gForceX = gx; m_gForceY = gy; m_gForceZ = gz;
        m_dirty |= kDirtyGForce;
    }

    const int  batt     = d.batteryRaw & 0x7F;
    const bool charging = (d.batteryRaw & 0x80) != 0;
    if (m_batteryPercent  != batt)     { m_batteryPercent  = batt;     m_dirty |= kDirtyBattery; }
    if (m_batteryCharging != charging) { m_batteryCharging = charging; m_dirty |= kDirtyCharging; }

    const int kmhInt = static_cast<int>(d.speedMmS / kMmSPerKmh);
    if (kmhInt != m_speedKmh) { m_speedKmh = kmhInt; emit speedKmhChanged(kmhInt); }

    if (fix) {
        m_lastLat = d.latitude;
        m_lastLon = d.longitude;
        updateLapTiming(d.latitude, d.longitude, static_cast<double>(kmhInt));

        if (m_lapTimerRunning) {
            static constexpr double kMinPointSpacingM = 2.0;
            const bool firstPoint = m_currentLapPath.isEmpty();
            if (firstPoint
                || haversineM(d.latitude, d.longitude, m_lastStoredLat, m_lastStoredLon) >= kMinPointSpacingM) {
                m_currentLapPath.append(d.latitude);
                m_currentLapPath.append(d.longitude);
                m_lastStoredLat = d.latitude;
                m_lastStoredLon = d.longitude;
            }
        }
    }

    if (m_lapTimerRunning) m_dirty |= kDirtyCurrentLap;
}

void RaceBoxModel::updateLapTiming(double lat, double lon, double speedKmh)
{
    if (!m_finishLineSet) return;

    // Cheap rectangular pre-filter — skip haversine when clearly far from the line
    static constexpr double kDegGuard = 0.002; // ~220 m at mid-latitudes
    if (!m_inFinishZone
        && std::abs(lat - m_finishLineLat) > kDegGuard
        && std::abs(lon - m_finishLineLon) > kDegGuard)
        return;

    const double dist = haversineM(lat, lon, m_finishLineLat, m_finishLineLon);
    const bool inZone = dist <= m_finishRadiusM;

    if (inZone && !m_inFinishZone && speedKmh > 10.0) {
        // Entered the finish zone at speed — record lap if timer already running
        if (m_lapTimerRunning && m_lapNumber > 0) {
            const qint64 lapMs = m_lapTimer.elapsed();
            m_lastLapMs = lapMs;
            m_dirty |= kDirtyLastLap;
            QVariantList pathList;
            pathList.reserve(m_currentLapPath.size());
            for (double v : m_currentLapPath)
                pathList.append(v);
            emit lapCompleted(lapMs, pathList);
            m_currentLapPath.clear();
            const bool newBest = (m_bestLapMs == 0 || lapMs < m_bestLapMs);
            if (newBest) {
                m_bestLapMs = lapMs;
                m_dirty |= kDirtyBestLap;
            }
            qCInfo(lcRaceBox) << "Lap" << m_lapNumber << "completed:"
                              << lapMs / 60000 << "m"
                              << (lapMs % 60000) / 1000.0 << "s"
                              << (newBest ? "(new best)" : "");
        }
        ++m_lapNumber;
        m_dirty |= kDirtyLapNumber;
        m_lapTimer.restart();
        m_lapTimerRunning = true;
        if (!m_hasStartedTiming) {
            m_hasStartedTiming = true;
            m_dirty |= kDirtyStartedTiming;
        }
    }

    // Hysteresis: enter at 1× radius, exit at 2× radius
    if (inZone)
        m_inFinishZone = true;
    else if (dist > m_finishRadiusM * 2.0)
        m_inFinishZone = false;
}

double RaceBoxModel::haversineM(double lat1, double lon1, double lat2, double lon2)
{
    const double dLat = (lat2 - lat1) * M_PI / 180.0;
    const double dLon = (lon2 - lon1) * M_PI / 180.0;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2)
                   + std::cos(lat1 * M_PI / 180.0) * std::cos(lat2 * M_PI / 180.0)
                   * std::sin(dLon / 2) * std::sin(dLon / 2);
    return kEarthRadiusM * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

void RaceBoxModel::resetLapState()
{
    m_inFinishZone    = false;
    m_lapTimerRunning = false;
    m_lapNumber       = 0;
    m_lastLapMs       = 0;
    m_bestLapMs       = 0;
    m_currentLapPath.clear();
    m_dirty |= kDirtyLapNumber | kDirtyLastLap | kDirtyBestLap | kDirtyCurrentLap;
}

void RaceBoxModel::clearFinishLine()
{
    resetLapState();
    m_finishLineSet    = false;
    m_finishLineLat    = 0.0;
    m_finishLineLon    = 0.0;
    m_hasStartedTiming = false;
    m_dirty |= kDirtyFinishLine | kDirtyStartedTiming;
    emit finishLineLearned(0.0, 0.0);
    qCInfo(lcRaceBox) << "Finish line cleared";
}

void RaceBoxModel::resetLapCounters()
{
    resetLapState();
    qCInfo(lcRaceBox) << "Lap counters reset — session saved";
}

void RaceBoxModel::emitNotifications()
{
    if (m_lapTimerRunning) m_dirty |= kDirtyCurrentLap;
    if (!m_dirty) return;
    const quint16 dirty = m_dirty;
    m_dirty = 0;

    if (dirty & kDirtyConnected)  emit connectedChanged();
    if (dirty & kDirtyFix)        emit hasFixChanged();
    if (dirty & kDirtySvs)        emit satellitesChanged();
    if (dirty & kDirtyLapNumber)  emit lapNumberChanged();
    if (dirty & kDirtyCurrentLap) emit currentLapMsChanged();
    if (dirty & kDirtyLastLap)    emit lastLapMsChanged();
    if (dirty & kDirtyBestLap)    emit bestLapMsChanged();
    if (dirty & kDirtyGForce)     { emit gForceXChanged(); emit gForceYChanged(); emit gForceZChanged(); }
    if (dirty & kDirtyBattery)    emit batteryPercentChanged();
    if (dirty & kDirtyCharging)   emit batteryChargingChanged();
    if (dirty & kDirtyFinishLine) emit finishLineSetChanged();
    if (dirty & kDirtyStartedTiming) emit hasStartedTimingChanged();
}
