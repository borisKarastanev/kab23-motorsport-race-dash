#include "raceboxmodel.h"
#include "logging.h"
#include <cmath>

static constexpr double kEarthRadiusM   = 6371000.0;
static constexpr double kMetersPerDegLat = 111132.0;

namespace {
constexpr double kMinCrossSpeedKmh    = 3.0;   // only reject genuinely-parked GPS jitter;
                                               // a real slow crossing (hairpin S/F, kart)
                                               // must still count
constexpr qint64 kMinLapMs            = 3000;  // debounce: reject re-crossings within 3 s
constexpr qint64 kMaxFixGapMs         = 500;   // don't bridge a stall (missed/burst fixes)
                                               // into one long segment — the interpolation
                                               // and geometry can't be trusted across it
constexpr double kLearnGateHalfWidthM = 20.0;  // learned gate spans 40 m across the track
                                               // (matches the old finish circle's diameter)
constexpr double kGatePrefilterM      = 250.0; // skip crossing math when clearly far away

// Metres per degree of longitude at the given latitude.
double metersPerDegLon(double latDeg)
{
    return 111320.0 * std::cos(latDeg * M_PI / 180.0);
}

// Initial bearing (radians, 0 = north, clockwise) from point 1 to point 2.
double bearingRad(double lat1, double lon1, double lat2, double lon2)
{
    const double la1 = lat1 * M_PI / 180.0, la2 = lat2 * M_PI / 180.0;
    const double dLon = (lon2 - lon1) * M_PI / 180.0;
    const double y = std::sin(dLon) * std::cos(la2);
    const double x = std::cos(la1) * std::sin(la2) - std::sin(la1) * std::cos(la2) * std::cos(dLon);
    return std::atan2(y, x);
}
}

RaceBoxModel::RaceBoxModel(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_notifyTimer.setInterval(100); // 10 Hz
    connect(&m_notifyTimer, &QTimer::timeout, this, &RaceBoxModel::emitNotifications);
    m_notifyTimer.start();
}

qint64 RaceBoxModel::currentLapMs() const
{
    if (!m_lapTimerRunning) return 0;
    return m_clock.elapsed() - m_lapStartMs;
}

RaceBoxModel::LapTimerState RaceBoxModel::lapTimerState() const
{
    if (m_lapTimerRunning)              return Running;
    if (!m_finishLineSet || !m_hasFix)  return Idle;
    return m_hasStartedTiming ? Standby : Armed;
}

void RaceBoxModel::setFinishLine(double latA, double lonA, double latB, double lonB)
{
    if (latA == 0.0 && lonA == 0.0 && latB == 0.0 && lonB == 0.0) return; // unset
    m_gateLatA = latA; m_gateLonA = lonA;
    m_gateLatB = latB; m_gateLonB = lonB;
    m_finishLineSet = true;
    m_crossDirSign  = 0; // new gate — re-latch crossing direction on the next pass
    m_dirty |= kDirtyFinishLine;
}

void RaceBoxModel::gateFromHeading(double lat, double lon,
                                   double &latA, double &lonA, double &latB, double &lonB) const
{
    // Perpendicular to the recent heading. If no heading is known the gate would be
    // arbitrarily oriented (hdg=0 gives an E–W gate) and could end up parallel to
    // travel — uncrossable — so learnFinishLineHere() requires a heading before
    // calling this. The fallback here is purely defensive.
    const double hdg = m_haveHeading ? m_headingRad : 0.0;
    // Travel unit vector in ENU is (sin hdg, cos hdg); its perpendicular is (cos hdg, -sin hdg).
    const double perpE = std::cos(hdg);
    const double perpN = -std::sin(hdg);
    const double dLat = (kLearnGateHalfWidthM * perpN) / kMetersPerDegLat;
    const double dLon = (kLearnGateHalfWidthM * perpE) / metersPerDegLon(lat);
    latA = lat + dLat; lonA = lon + dLon;
    latB = lat - dLat; lonB = lon - dLon;
}

void RaceBoxModel::learnFinishLineHere()
{
    if (!m_hasFix) return;
    if (!m_haveHeading) {
        // Without a travel direction the gate can't be oriented across the track.
        qCWarning(lcRaceBox) << "Cannot learn finish line: no heading yet — drive across the line";
        return;
    }
    double latA, lonA, latB, lonB;
    gateFromHeading(m_lastLat, m_lastLon, latA, lonA, latB, lonB);
    setFinishLine(latA, lonA, latB, lonB);
    qCInfo(lcRaceBox) << "Finish-line gate set at" << m_lastLat << m_lastLon
                      << "| endpoints" << latA << lonA << "->" << latB << lonB;
    emit finishLineLearned(latA, lonA, latB, lonB);
}

void RaceBoxModel::onConnectionStateChanged(bool connected)
{
    if (m_connected == connected) return;
    m_connected = connected;
    m_dirty |= kDirtyConnected;
    if (!connected) {
        m_hasFix = false;
        m_havePrevFix = false; // stale previous fix must not bridge a reconnect
        m_dirty |= kDirtyFix;
    }
}

void RaceBoxModel::onData(const RaceBoxData &d)
{
    const bool fix = (d.fixStatus >= 2) && (d.fixFlags & 0x01);
    if (m_hasFix != fix) {
        m_hasFix = fix;
        m_dirty |= kDirtyFix;
        if (fix) {
            qCInfo(lcRaceBox) << "GPS fix acquired — SVs:" << d.numSvs;
        } else {
            qCInfo(lcRaceBox) << "GPS fix lost";
            m_havePrevFix = false; // don't bridge a dropout into a false crossing
        }
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
        const double curLat = d.latitude, curLon = d.longitude;
        const qint64 nowMs  = m_clock.elapsed();

        if (m_havePrevFix) {
            // Refresh the travel heading when the car has moved appreciably.
            if (haversineM(m_lastLat, m_lastLon, curLat, curLon) > 0.5) {
                m_headingRad  = bearingRad(m_lastLat, m_lastLon, curLat, curLon);
                m_haveHeading = true;
            }
            updateLapTiming(m_lastLat, m_lastLon, curLat, curLon,
                            static_cast<double>(kmhInt), m_prevFixMs, nowMs);
        }

        m_lastLat = curLat;
        m_lastLon = curLon;
        m_prevFixMs   = nowMs;
        m_havePrevFix = true;

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

void RaceBoxModel::updateLapTiming(double prevLat, double prevLon, double curLat, double curLon,
                                  double speedKmh, qint64 prevMs, qint64 nowMs)
{
    if (!m_finishLineSet) return;
    if (speedKmh <= kMinCrossSpeedKmh) return; // stationary GPS jitter — ignore
    if (nowMs - prevMs > kMaxFixGapMs) return; // fixes too far apart in time to bridge

    // Cheap pre-filter — skip the crossing math when both fixes are clearly far
    // from the gate midpoint.
    const double midLat = (m_gateLatA + m_gateLatB) / 2.0;
    const double midLon = (m_gateLonA + m_gateLonB) / 2.0;
    if (haversineM(curLat,  curLon,  midLat, midLon) > kGatePrefilterM &&
        haversineM(prevLat, prevLon, midLat, midLon) > kGatePrefilterM)
        return;

    // Project everything to local metres (equirectangular around the gate midpoint):
    // x = east, y = north.
    const double mLon = metersPerDegLon(midLat);
    auto ex = [&](double lon) { return (lon - midLon) * mLon; };
    auto ny = [&](double lat) { return (lat - midLat) * kMetersPerDegLat; };

    const double p0x = ex(prevLon), p0y = ny(prevLat); // path segment start (prev fix)
    const double p1x = ex(curLon),  p1y = ny(curLat);  // path segment end   (cur fix)
    const double ax  = ex(m_gateLonA), ay = ny(m_gateLatA); // gate endpoint A
    const double bx  = ex(m_gateLonB), by = ny(m_gateLatB); // gate endpoint B

    // Intersect path segment P0→P1 with gate segment A→B.
    const double rx = p1x - p0x, ry = p1y - p0y;
    const double sx = bx  - ax,  sy = by  - ay;
    const double denom = rx * sy - ry * sx;
    if (std::abs(denom) < 1e-9) return; // parallel / degenerate — no crossing

    const double t = ((ax - p0x) * sy - (ay - p0y) * sx) / denom; // fraction along the path
    const double u = ((ax - p0x) * ry - (ay - p0y) * rx) / denom; // fraction along the gate
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return;         // crossing is outside the gate width

    // Reject wrong-way crossings. denom's sign is the side the path crosses the gate
    // from; the first accepted crossing latches the racing direction, and later
    // opposite-direction passes (reverse, pit-lane side of the line) are ignored.
    const int dir = (denom > 0.0) ? 1 : -1;
    if (m_crossDirSign == 0)
        m_crossDirSign = dir;      // latch on the arming crossing
    else if (dir != m_crossDirSign)
        return;                    // crossing the line the wrong way — not a lap

    // Sub-sample crossing time: interpolate between the two fix arrival times.
    const qint64 crossMs = prevMs + static_cast<qint64>(t * static_cast<double>(nowMs - prevMs));

    // Debounce: reject a second crossing that arrives implausibly soon (GPS jitter
    // straddling the line on consecutive fixes).
    if (m_lapTimerRunning && (crossMs - m_lapStartMs) < kMinLapMs) return;

    if (m_lapTimerRunning && m_lapNumber > 0) {
        const qint64 lapMs = crossMs - m_lapStartMs;
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
    m_lapStartMs = crossMs;
    m_lapTimerRunning = true;
    m_hasStartedTiming = true; // sticky; lapTimerState() transitions handled at notify time
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
    m_gateLatA = m_gateLonA = m_gateLatB = m_gateLonB = 0.0;
    m_hasStartedTiming = false;
    m_crossDirSign     = 0;
    m_dirty |= kDirtyFinishLine;
    emit finishLineLearned(0.0, 0.0, 0.0, 0.0);
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

    // Derived lap-timer state: recompute once per tick and notify on transition.
    // Every state-input change also sets a dirty bit above, so a real transition
    // never slips past the early-return guard.
    const LapTimerState state = lapTimerState();
    if (state != m_lastLapTimerState) {
        m_lastLapTimerState = state;
        emit lapTimerStateChanged();
    }
}
