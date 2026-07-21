#include "raceboxmodel.h"
#include "logging.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

static constexpr double kEarthRadiusM   = 6371000.0;
static constexpr double kMetersPerDegLat = 111132.0;

namespace {
constexpr qint64 kMinLapMs            = 3000;  // debounce: reject re-crossings within 3 s
constexpr qint64 kMaxFixGapMs         = 500;   // don't bridge a stall (missed/burst fixes)
                                               // into one long segment — the interpolation
                                               // and geometry can't be trusted across it
constexpr double kLearnGateHalfWidthM = 12.5;  // learned gate spans 25 m across the track —
                                               // comfortably covers real track widths without
                                               // dominating a single-lap map's auto-fit scale
constexpr double kGatePrefilterM      = 250.0; // skip crossing math when clearly far away

// Heading is measured from a moving anchor rather than from the previous fix: the
// car must travel this far from the anchor before a new bearing is taken, and the
// anchor then moves up. Sampling consecutive fixes instead would tie the heading to
// the device's data rate — at 25 Hz a 0.5 m step implies ~45 km/h, so the heading
// would silently stop updating below that speed.
constexpr double kHeadingAnchorM  = 0.5;
// A heading older than this is treated as unusable: the car has been sitting still
// (or crawling under kHeadingAnchorM) long enough that its last direction of travel
// says nothing about how it will next cross the line. Learning a gate from a stale
// heading yields one skewed — possibly parallel and uncrossable — to the track.
constexpr qint64 kHeadingMaxAgeMs = 5000;

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

qint64 RaceBoxModel::currentDeltaMs() const
{
    if (!m_lapTimerRunning || m_bestLapTrace.size() < 2)
        return 0;
    return currentLapMs() - referenceElapsedAtPosition(m_lastLat, m_lastLon);
}

qint64 RaceBoxModel::referenceElapsedAtPosition(double lat, double lon) const
{
    const int n = m_bestLapTrace.size();
    if (n < 2) return 0;

    // Local equirectangular projection with the query point Q at the origin:
    // segment endpoints become metre offsets (east, north) from Q.
    const double mPerLon = metersPerDegLon(lat);
    auto projectSeg = [&](int i, double &outFrac) {
        const Sample &s0 = m_bestLapTrace[i];
        const Sample &s1 = m_bestLapTrace[i + 1];
        const double ax = (s0.lon - lon) * mPerLon, ay = (s0.lat - lat) * kMetersPerDegLat;
        const double bx = (s1.lon - lon) * mPerLon, by = (s1.lat - lat) * kMetersPerDegLat;
        const double vx = bx - ax, vy = by - ay;
        const double segLenSq = vx * vx + vy * vy;
        double frac = 0.0;
        if (segLenSq > 1e-9)   // project Q(origin) onto A→B, clamped to the segment
            frac = std::clamp((-ax * vx - ay * vy) / segLenSq, 0.0, 1.0);
        const double px = ax + frac * vx, py = ay + frac * vy;
        outFrac = frac;
        return px * px + py * py; // squared distance from Q to the projected point
    };

    auto scan = [&](int lo, int hi, int &bestIdx, double &bestFrac) {
        double bestSq = std::numeric_limits<double>::max();
        for (int i = lo; i <= hi; ++i) {
            double frac;
            const double dSq = projectSeg(i, frac);
            if (dSq < bestSq) { bestSq = dSq; bestIdx = i; bestFrac = frac; }
        }
        return bestSq;
    };

    // Forward-biased window around the last match keeps the search cheap and
    // picks the right pass where the track nears itself; a small backward margin
    // absorbs GPS jitter. If even the closest in-window segment is implausibly
    // far, we've lost sync (dropout / big line deviation) — rescan the whole lap.
    constexpr int    kBackWindow = 5;    // ~10 m of samples behind the anchor
    constexpr int    kFwdWindow  = 60;   // ~120 m ahead — survives a brief fix gap
    constexpr double kResyncM    = 40.0; // window miss beyond this ⇒ full rescan

    int    bestIdx  = 0;
    double bestFrac = 0.0;
    const int anchor = std::clamp(m_refMatchIdx, 0, n - 2);
    const int lo = std::max(0,     anchor - kBackWindow); // anchor∈[0,n-2] ⇒ lo≤anchor≤hi
    const int hi = std::min(n - 2, anchor + kFwdWindow);
    double bestSq = scan(lo, hi, bestIdx, bestFrac);
    if (bestSq > kResyncM * kResyncM)
        // Lost sync (long fix gap / big line deviation). Recover forward-only:
        // within a lap the car never moves back past its last match, so scanning
        // [anchor, end] avoids snapping to a geographically-adjacent EARLIER pass
        // (e.g. the lap-start segments running beside the finish straight).
        scan(anchor, n - 2, bestIdx, bestFrac);

    m_refMatchIdx = bestIdx;

    const Sample &s0 = m_bestLapTrace[bestIdx];
    const Sample &s1 = m_bestLapTrace[bestIdx + 1];
    return s0.elapsedMs + static_cast<qint64>(bestFrac * double(s1.elapsedMs - s0.elapsedMs));
}

bool RaceBoxModel::canLearnFinishLine() const
{
    if (!m_hasFix || !m_haveHeading) return false;
    return (m_clock.elapsed() - m_headingMs) <= kHeadingMaxAgeMs;
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
    // Without a fix, or without a travel direction, the gate can't be oriented
    // across the track. The UI binds to this same predicate, so reaching here
    // with it false means a caller ignored canLearnFinishLine.
    if (!canLearnFinishLine()) {
        qCWarning(lcRaceBox) << "Cannot learn finish line: need a GPS fix and a heading"
                             << "— drive across the line";
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
        m_havePrevFix = false;       // stale previous fix must not bridge a reconnect
        m_haveHeadingAnchor = false; // nor anchor a bearing across the gap
        m_speedKmh = 0;              // don't leave a moving-speed reading latched across a dropout
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
            m_havePrevFix = false;       // don't bridge a dropout into a false crossing
            m_haveHeadingAnchor = false; // nor measure a bearing across one
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
    m_maxLatG = std::max(m_maxLatG, std::fabs(gx));
    m_maxLonG = std::max(m_maxLonG, std::fabs(gy));

    const int  batt     = d.batteryRaw & 0x7F;
    const bool charging = (d.batteryRaw & 0x80) != 0;
    if (m_batteryPercent  != batt)     { m_batteryPercent  = batt;     m_dirty |= kDirtyBattery; }
    if (m_batteryCharging != charging) { m_batteryCharging = charging; m_dirty |= kDirtyCharging; }

    int kmhInt = static_cast<int>(d.speedMmS / kMmSPerKmh);
    if (kmhInt < kStationarySpeedKmh) kmhInt = 0; // floor GPS jitter to a steady 0 while parked
    if (kmhInt != m_speedKmh) { m_speedKmh = kmhInt; emit speedKmhChanged(kmhInt); }

    if (fix) {
        const double curLat = d.latitude, curLon = d.longitude;
        const qint64 nowMs  = m_clock.elapsed();

        // Refresh the travel heading once the car has moved kHeadingAnchorM from the
        // anchor, then move the anchor up. Anchoring (rather than differencing
        // consecutive fixes) keeps this working at any speed, and gives the heading
        // an age: it only goes stale when the car actually stops moving.
        if (!m_haveHeadingAnchor) {
            m_headingAnchorLat = curLat;
            m_headingAnchorLon = curLon;
            m_haveHeadingAnchor = true;
        } else if (haversineM(m_headingAnchorLat, m_headingAnchorLon, curLat, curLon)
                   >= kHeadingAnchorM) {
            m_headingRad  = bearingRad(m_headingAnchorLat, m_headingAnchorLon, curLat, curLon);
            m_headingMs   = nowMs;
            m_haveHeading = true;
            m_headingAnchorLat = curLat;
            m_headingAnchorLon = curLon;
        }

        if (m_havePrevFix) {
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
                m_currentLapTrace.append({d.latitude, d.longitude, nowMs - m_lapStartMs});
            }
        }
    }

    if (m_lapTimerRunning) m_dirty |= kDirtyCurrentLap;
}

void RaceBoxModel::updateLapTiming(double prevLat, double prevLon, double curLat, double curLon,
                                  double speedKmh, qint64 prevMs, qint64 nowMs)
{
    if (!m_finishLineSet) return;
    if (speedKmh <= kStationarySpeedKmh) return; // parked GPS jitter — ignore (a real slow
                                                 // crossing, e.g. a hairpin S/F or a kart, is
                                                 // above this and still counts)
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
    // Exact crossing point on the gate (fraction t along prev→cur). Anchors both
    // the finishing lap's closing trace sample and the new lap's origin sample to
    // the line rather than to a fix boundary, so consecutive laps' traces share
    // one geographic origin at S/F.
    const double crossLat = prevLat + t * (curLat - prevLat);
    const double crossLon = prevLon + t * (curLon - prevLon);

    // Debounce: reject a second crossing that arrives implausibly soon (GPS jitter
    // straddling the line on consecutive fixes).
    if (m_lapTimerRunning && (crossMs - m_lapStartMs) < kMinLapMs) return;

    if (m_lapTimerRunning && m_lapNumber > 0) {
        const qint64 lapMs = crossMs - m_lapStartMs;
        m_lastLapMs = lapMs;
        m_dirty |= kDirtyLastLap;
        // Close the trace exactly at the finish line so a following lap's delta
        // lookup spans the full lap right up to the line (rather than ending at
        // the last decimated point short of it and freezing the reference).
        m_currentLapTrace.append({crossLat, crossLon, lapMs});
        QVariantList pathList;
        pathList.reserve(m_currentLapPath.size());
        for (double v : m_currentLapPath)
            pathList.append(v);
        emit lapCompleted(lapMs, pathList);
        const bool newBest = (m_bestLapMs == 0 || lapMs < m_bestLapMs);
        if (newBest) {
            m_bestLapMs = lapMs;
            m_dirty |= kDirtyBestLap;
            // This lap becomes the position-matched benchmark the next lap's
            // live delta (currentDeltaMs) is compared against. Move, not copy —
            // m_currentLapTrace is cleared immediately below either way.
            m_bestLapTrace = std::move(m_currentLapTrace);
        }
        m_currentLapPath.clear();
        m_currentLapTrace.clear();
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
    m_refMatchIdx = 0;         // restart the reference nearest-point search at S/F
    // Seed the new lap's trace with its origin exactly at the line (elapsed 0)
    // so the reference geometry/time this lap eventually provides starts at S/F
    // rather than at the first decimated point past it.
    m_currentLapTrace.append({crossLat, crossLon, 0});
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
    m_currentLapTrace.clear();
    m_bestLapTrace.clear();
    m_refMatchIdx = 0;
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

void RaceBoxModel::resetSessionStats()
{
    m_maxLatG = 0.0;
    m_maxLonG = 0.0;
}

void RaceBoxModel::resetLapCounters()
{
    resetLapState();
    qCInfo(lcRaceBox) << "Lap counters reset — session saved";
}

void RaceBoxModel::emitNotifications()
{
    if (m_lapTimerRunning) m_dirty |= kDirtyCurrentLap;

    // Derived, and — unlike every other property here — it decays with time rather
    // than with incoming frames: the heading goes stale while the car sits still,
    // which produces no frames and so no dirty bit. Recompute it above the
    // early-return guard, otherwise a parked car would never see it expire.
    const bool canLearn = canLearnFinishLine();
    if (canLearn != m_lastCanLearnFinishLine) {
        m_lastCanLearnFinishLine = canLearn;
        emit canLearnFinishLineChanged();
    }

    if (!m_dirty) return;
    const quint16 dirty = m_dirty;
    m_dirty = 0;

    if (dirty & kDirtyConnected)  emit connectedChanged();
    if (dirty & kDirtyFix)        emit hasFixChanged();
    if (dirty & kDirtySvs)        emit satellitesChanged();
    if (dirty & kDirtyLapNumber)  emit lapNumberChanged();
    if (dirty & kDirtyCurrentLap) { emit currentLapMsChanged(); emit currentDeltaMsChanged(); }
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
