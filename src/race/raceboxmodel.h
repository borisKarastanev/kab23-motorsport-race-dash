#pragma once

#include "src/race/raceboxdata.h"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantList>
#include <QVector>
#include <QList>
#include <QDateTime>

// One completed lap, reported the instant it finishes — see
// RaceBoxModel::lapCompleted() below. Declared at file scope rather than
// nested in RaceBoxModel: moc doesn't support a meta-object type (Q_GADGET or
// Q_OBJECT) nested inside another. A Q_GADGET rather than a plain struct so it
// can carry Q_PROPERTYs for the two fields a future QML consumer would
// plausibly want; path and sectorMs are bulk C++-only data with no such need
// today.
struct RaceBoxLapResult {
    Q_GADGET
    Q_PROPERTY(int lapNumber MEMBER lapNumber)
    Q_PROPERTY(qint64 ms MEMBER ms)
public:
    // RaceBoxModel's own lap counter as this lap finished — the number the
    // driver saw on the dash. NOT a session-unique identity: clearFinishLine()
    // resets it to 0 mid-run, so a consumer that files laps under it can end up
    // with two different laps sharing one number. Anything needing a stable
    // per-session number must assign its own (see SessionModel::LapRecord).
    int           lapNumber = 0;
    qint64        ms        = 0;
    QVariantList  path;            // flat [lat, lon, lat, lon, …]
    QList<qint64> sectorMs;        // exactly sectorCount entries, or empty if a gate was missed
};
Q_DECLARE_METATYPE(RaceBoxLapResult)

class RaceBoxModel : public QObject {
    Q_OBJECT
public:
    // Lap-timer lifecycle — the single state the UI binds to instead of
    // deriving it from a handful of loose booleans.
    //   Idle    — no finish line / no GPS fix: timing unavailable
    //   Armed   — finish line + fix, waiting for the first crossing (prompt shown)
    //   Running — a lap is being timed
    //   Standby — timed before this run (e.g. after a session save), waiting for
    //             the next crossing without re-showing the first-run prompt
    enum LapTimerState { Idle, Armed, Running, Standby };
    Q_ENUM(LapTimerState)

    Q_PROPERTY(bool    connected      READ connected      NOTIFY connectedChanged)
    Q_PROPERTY(bool    hasFix         READ hasFix         NOTIFY hasFixChanged)
    Q_PROPERTY(int     satellites     READ satellites     NOTIFY satellitesChanged)
    Q_PROPERTY(int     lapNumber      READ lapNumber      NOTIFY lapNumberChanged)
    Q_PROPERTY(qint64  currentLapMs   READ currentLapMs   NOTIFY currentLapMsChanged)
    Q_PROPERTY(qint64  lastLapMs      READ lastLapMs      NOTIFY lastLapMsChanged)
    Q_PROPERTY(qint64  bestLapMs      READ bestLapMs      NOTIFY bestLapMsChanged)
    // Live delta vs the best lap at the SAME point on track — the reference
    // time is read at the geographically nearest point on the best lap's path,
    // not at the same wall-clock instant nor the same distance travelled, so a
    // different racing line doesn't desync the comparison. Negative/green =
    // ahead of the best lap's pace at this point on track, positive/red = behind.
    Q_PROPERTY(qint64  currentDeltaMs READ currentDeltaMs NOTIFY currentDeltaMsChanged)
    Q_PROPERTY(double  gForceX        READ gForceX        NOTIFY gForceXChanged)
    Q_PROPERTY(double  gForceY        READ gForceY        NOTIFY gForceYChanged)
    Q_PROPERTY(double  gForceZ        READ gForceZ        NOTIFY gForceZChanged)
    Q_PROPERTY(int     batteryPercent  READ batteryPercent  NOTIFY batteryPercentChanged)
    Q_PROPERTY(bool    batteryCharging READ batteryCharging NOTIFY batteryChargingChanged)
    Q_PROPERTY(bool    finishLineSet   READ finishLineSet   NOTIFY finishLineSetChanged)
    // Whether learnFinishLineHere() would actually succeed right now. The gate is
    // built perpendicular to the car's travel heading, so it needs a fix AND a
    // heading — and a heading only exists once the car has moved. This is the same
    // predicate learnFinishLineHere() enforces, so a UI bound to it can never offer
    // a button whose tap silently does nothing.
    Q_PROPERTY(bool canLearnFinishLine READ canLearnFinishLine NOTIFY canLearnFinishLineChanged)
    Q_PROPERTY(LapTimerState lapTimerState READ lapTimerState NOTIFY lapTimerStateChanged)

    explicit RaceBoxModel(QObject *parent = nullptr);

    bool   connected()      const { return m_connected; }
    bool   hasFix()         const { return m_hasFix; }
    int    satellites()     const { return m_satellites; }
    int    lapNumber()      const { return m_lapNumber; }
    qint64 currentLapMs()   const;
    qint64 lastLapMs()      const { return m_lastLapMs; }
    qint64 bestLapMs()      const { return m_bestLapMs; }
    qint64 currentDeltaMs() const;
    double gForceX()        const { return m_gForceX; }
    double gForceY()        const { return m_gForceY; }
    double gForceZ()        const { return m_gForceZ; }
    int    batteryPercent()  const { return m_batteryPercent; }
    bool   batteryCharging() const { return m_batteryCharging; }
    bool   finishLineSet()   const { return m_finishLineSet; }
    // Single source of truth for "can a finish line be learned right now" —
    // enforced by learnFinishLineHere() and bound to by the UI. Requires a fix and a
    // *fresh* heading: a heading recorded before the car parked is not evidence of
    // how it will next cross the line.
    bool   canLearnFinishLine() const;
    // Single source of truth for the lap-timer lifecycle (see LapTimerState).
    LapTimerState lapTimerState() const;
    // GPS UTC, decoded from the UBX-NAV-PVT time block already present in the
    // RaceBox packet (see onData()) and fed to TimeModel's "SYNC FROM GPS"
    // offline time source. Plain getters, not properties: TimeModel pulls them
    // on its own 1 Hz tick — and must read the time *at the moment the button
    // is tapped*, so a latched, notify-driven copy would be stale by seconds.
    // Same convention as lastLat()/lastLon() below.
    //
    // Validity is the datetime's own: onData() clears m_gpsUtc whenever the
    // reading isn't trustworthy, so there is no second bool to keep in sync.
    bool      gpsTimeValid() const { return m_gpsUtc.isValid(); }
    QDateTime gpsUtc()       const { return m_gpsUtc; }

    // Sets the start/finish line as a gate: a segment between two GPS points
    // (A→B) spanning the track width. A lap is timed when the car's path crosses
    // this segment. All-zero coordinates are treated as "unset" (no-op).
    void setFinishLine(double latA, double lonA, double latB, double lonB);

    // Applies previously-derived sector gates — TrackModel calls this at
    // startup and on track selection, mirroring setFinishLine(), so a track
    // already timed once doesn't waste the first lap of every future session
    // re-deriving gates it already has. Each entry is a map with keys
    // "lat1"/"lon1"/"lat2"/"lon2" (same shape TrackModel already uses for the
    // finish line) plus an optional "dir" carrying the gate's latched crossing
    // direction (absent in gates persisted before direction was recorded — those
    // re-latch on their first crossing). An empty list clears whatever gates are
    // currently set — a pure setter, with no opinion on why the caller is
    // calling it. Any change discards the current lap's splits, which were
    // measured against the outgoing gates.
    void setSectorGates(const QVariantList &gates);

    // Last known GPS fix — used by TrackModel's nearest-track auto-detect scan
    double lastLat() const { return m_lastLat; }
    double lastLon() const { return m_lastLon; }

    // Last decoded ground speed (km/h). Read by TrackModel to gate the
    // nearest-track suggestion on the car being parked. Reset to 0 on disconnect
    // so a value latched while moving can't survive a dropout as a stale reading.
    int    speedKmh() const { return m_speedKmh; }

    // Shared distance helper — also used by TrackModel for nearest-track scans
    static double haversineM(double lat1, double lon1, double lat2, double lon2);

    // Peak absolute g-force over the current session, read by SessionModel at
    // save time. Accumulated from every sample in onData(), not the throttled
    // gForceX/Y properties, so brief peaks between notify ticks aren't missed.
    double maxLatG() const { return m_maxLatG; }
    double maxLonG() const { return m_maxLonG; }

    // Zeroes the session g-force peaks. Called directly by SessionModel at save
    // time alongside its own accumulator resets, so every session-peak stat is
    // reset by the same mechanism (rather than piggybacking on the lap-state
    // reset in resetLapCounters()).
    void resetSessionStats();

public slots:
    void onData(const RaceBoxData &data);
    void onConnectionStateChanged(bool connected);
    // Callable from QML — sets current GPS position as the finish line
    Q_INVOKABLE void learnFinishLineHere();
    // Callable from QML — clears the finish line and resets all lap data
    Q_INVOKABLE void clearFinishLine();
    // Connected in main.cpp to SessionModel::sessionSaved — clears lap
    // counters/timer so the dashboard starts a fresh session on the next
    // finish-line crossing. Unlike clearFinishLine(), the finish line itself
    // stays configured.
    void resetLapCounters();

signals:
    void connectedChanged();
    void hasFixChanged();
    void satellitesChanged();
    void lapNumberChanged();
    void currentLapMsChanged();
    void lastLapMsChanged();
    void bestLapMsChanged();
    void currentDeltaMsChanged();
    void gForceXChanged();
    void gForceYChanged();
    void gForceZChanged();
    void batteryPercentChanged();
    void batteryChargingChanged();
    void finishLineSetChanged();
    void canLearnFinishLineChanged();
    void lapTimerStateChanged();
    // Emitted when the finish-line gate is learned (perpendicular to travel) or
    // cleared (all zero), so the caller can persist the two endpoints.
    void finishLineLearned(double latA, double lonA, double latB, double lonB);
    // Emitted to feed CanDataModel
    void speedKmhChanged(int kmh);
    // Emitted immediately when a lap completes — not throttled, safe for
    // persistence. One event, one signal: lapNumber, the finished lap's own
    // time/path, and its sector splits (sectorMs — exactly as many entries as
    // sector gates exist, S1/S2/S3 in order, when every gate was crossed;
    // empty when one was missed, per the shared optimal-lap rule,
    // ~/development/optimal-lap-algorithm.md) all travel together, so a caller
    // can never receive one half without the other.
    void lapCompleted(const RaceBoxLapResult &lap);
    // Emitted when the sector gates are (re)derived from a completed lap, or
    // cleared (empty list) — a new finish line makes any prior gate geometry
    // meaningless. TrackModel listens, to persist them per track the same way
    // it persists the finish line. Same map shape as setSectorGates() takes.
    void sectorGatesLearned(const QVariantList &gates);

private slots:
    void emitNotifications();

private:
    // A GPS sample recorded during a lap: position plus elapsed time since the
    // lap's start crossing. Declared up here (rather than beside
    // m_currentLapTrace below) because deriveSectorGates() takes one as a
    // parameter, and a member function's parameter types must already be
    // declared at that point in the class body.
    struct Sample { double lat; double lon; qint64 elapsedMs; };
    // A sector-boundary gate: the same two-point segment as the finish line,
    // tested the same way (see testGateCrossing()). dirSign is the crossing
    // direction the gate accepts (as testGateCrossing() reports it), latched at
    // derivation from the heading the gate was built perpendicular to; 0 = not
    // yet known, in which case the first crossing latches it. Gates are drawn
    // blindly 25 m across the first lap's racing line, so their span can overlap
    // a return leg, the pit lane, or the far side of a hairpin — without this a
    // single wrong-way pass consumes a gate out of order and the lap still
    // reports a full-length (but wrong) set of splits.
    struct Gate { double latA = 0.0, lonA = 0.0, latB = 0.0, lonB = 0.0; int dirSign = 0; };

    // Tests whether the path segment (prev fix → current fix) crosses the
    // finish-line gate; on a crossing it interpolates the exact crossing time
    // between the two fixes and completes/starts a lap. prevMs/nowMs are the
    // fix arrival times (from m_clock) used for that interpolation. The exact
    // crossing point (fraction t along prev→cur) anchors the finishing lap's
    // closing trace sample and the new lap's origin sample to the line rather
    // than to a fix boundary.
    void updateLapTiming(double prevLat, double prevLon, double curLat, double curLon,
                         double speedKmh, qint64 prevMs, qint64 nowMs);
    // Builds a gate perpendicular to the recent direction of travel, centred on
    // (lat, lon), and writes its two endpoints to the out-params.
    void gateFromHeading(double lat, double lon,
                         double &latA, double &lonA, double &latB, double &lonB) const;
    // Tests whether the path segment (prev fix → current fix) crosses the given
    // gate; on a crossing writes the fraction t along prev→cur to crossFrac and,
    // if dirSign is non-null, the sign of the crossing (which side the path
    // crosses from), so a caller can latch/enforce a consistent direction.
    // Shared by the finish-line gate and the two derived sector gates — one
    // segment-intersection test, three callers.
    bool testGateCrossing(const Gate &gate, double prevLat, double prevLon,
                          double curLat, double curLon,
                          double &crossFrac, int *dirSign = nullptr) const;
    // Derives the two sector-boundary gates from a just-completed lap's own
    // trace, at 1/3 and 2/3 of its cumulative distance, each built perpendicular
    // to the trace's local heading there — the same construction as
    // gateFromHeading(), just fed a heading measured from the recorded path
    // instead of a live one. Called once, the first time a lap completes with no
    // sector gates yet (m_sectorGates.isEmpty()); the result is fixed for the
    // rest of the session, so every lap after the first splits at the same two
    // places. No OSM centreline exists on the dash, so the first completed lap
    // is its only source of truth for where the track is.
    void deriveSectorGates(const QVector<Sample> &trace);
    // Shared by clearFinishLine() and resetLapCounters() — resets lap number,
    // timer, and current-lap path. Does not touch m_hasStartedTiming or the
    // finish-line fields; callers handle those themselves.
    void resetLapState();
    // Interpolates the best lap's elapsed time at the point on its recorded
    // path geographically nearest to (lat, lon), by projecting onto the nearest
    // trace segment. Searches a forward-biased window around the last match
    // (m_refMatchIdx) so a self-intersecting track / the S/F wrap matches the
    // right pass, falling back to a full rescan when the window misses.
    qint64 referenceElapsedAtPosition(double lat, double lon) const;

    // Connection / fix
    bool   m_connected   = false;
    bool   m_hasFix      = false;
    int    m_satellites  = 0;

    // Lap timing — a free-running monotonic clock (never restarted); lap
    // boundaries are recorded as interpolated clock timestamps for sub-sample
    // accuracy at 25 Hz.
    int           m_lapNumber   = 0;
    qint64        m_lastLapMs   = 0;
    qint64        m_bestLapMs   = 0;
    QElapsedTimer m_clock;              // started once in the constructor
    qint64        m_lapStartMs  = 0;    // clock time of the current lap's start crossing
    bool          m_lapTimerRunning = false;
    // Sticky: set on the first crossing of this run, stays true across a
    // resetLapCounters() (session save). Distinguishes Armed from Standby.
    bool          m_hasStartedTiming = false;

    // Current lap GPS path — flat [lat, lon, lat, lon, …], distance-decimated
    QVector<double> m_currentLapPath;
    double          m_lastStoredLat = 0.0;
    double          m_lastStoredLon = 0.0;

    // Live-delta tracking: a (lat, lon, elapsed-ms) trace recorded at the same
    // distance-decimated cadence as m_currentLapPath. When this lap finishes as
    // a new best, its trace becomes the reference (m_bestLapTrace) the next
    // lap's delta is measured against — position-for-position, by projecting the
    // live position onto the nearest reference segment, rather than by raw
    // elapsed time or cumulative distance.
    QVector<Sample> m_currentLapTrace;
    QVector<Sample> m_bestLapTrace;
    // Anchor segment index for the reference nearest-point search; advances with
    // the car and resets to 0 at each lap start. Mutable: the search is driven
    // from the const currentDeltaMs() getter.
    mutable int     m_refMatchIdx = 0;

    // Finish line as a gate: segment A→B spanning the track width. A lap is
    // timed when the path (prev→cur fix) crosses this segment.
    bool   m_finishLineSet = false;
    double m_gateLatA = 0.0, m_gateLonA = 0.0;
    double m_gateLatB = 0.0, m_gateLonB = 0.0;

    // Sector-boundary gates (2 gates → 3 sectors), derived once from the first
    // completed lap's own trace (see deriveSectorGates()) and reused, fixed, for
    // the rest of the session. Empty until derivation succeeds; cleared (like
    // the finish line) only when the finish line itself is cleared, since they
    // were derived relative to its geometry.
    QVector<Gate> m_sectorGates;
    // Elapsed-ms (since m_lapStartMs) of each sector gate the current lap has
    // crossed so far, in order. Cleared at the start of every lap. Compared
    // against m_sectorGates.size() at lap completion: anything short means a
    // missed gate, and the lap reports no splits at all (see updateLapTiming()).
    QList<qint64> m_currentLapSectorMs;

    // Previous fix — one endpoint of the crossing test segment.
    double m_lastLat = 0.0;   // also the "last known position" for learn/scan
    double m_lastLon = 0.0;
    qint64 m_prevFixMs   = 0; // arrival time of the previous fix
    bool   m_havePrevFix = false;

    // Latched crossing direction (sign of the path×gate cross product). Set on the
    // first accepted crossing of a run; later crossings in the opposite direction
    // (reverse / pit-side passes) are rejected. Survives a resetLapCounters()
    // session save; cleared only when the gate itself changes. 0 = unlatched.
    int    m_crossDirSign = 0;

    // Recent direction of travel (radians, bearing) — used to orient a learned gate
    // perpendicular to the track. Taken from a moving anchor the car has travelled
    // kHeadingAnchorM away from, not from the previous fix, so it updates at any
    // speed rather than only above the one implied by the device's data rate.
    // m_headingMs times the last refresh: a heading that stops being refreshed means
    // the car has stopped, and goes stale (see canLearnFinishLine()).
    double m_headingRad  = 0.0;
    bool   m_haveHeading = false;
    qint64 m_headingMs   = 0;
    double m_headingAnchorLat = 0.0, m_headingAnchorLon = 0.0;
    bool   m_haveHeadingAnchor = false;

    // Motion
    int    m_speedKmh       = 0;
    double m_gForceX        = 0.0;
    double m_gForceY        = 0.0;
    double m_gForceZ        = 0.0;
    // Session peak absolute g-force, zeroed by resetSessionStats() at save time.
    double m_maxLatG        = 0.0;
    double m_maxLonG        = 0.0;
    int    m_batteryPercent  = 0;
    bool   m_batteryCharging = false;

    // GPS UTC time, decoded from the UBX-NAV-PVT time block. Null whenever the
    // reading isn't trustworthy — that null IS gpsTimeValid()'s answer.
    QDateTime m_gpsUtc;

    // Dirty bits
    static constexpr quint16 kDirtyConnected   = 0x001;
    static constexpr quint16 kDirtyFix         = 0x002;
    static constexpr quint16 kDirtySvs         = 0x004;
    static constexpr quint16 kDirtyLapNumber   = 0x008;
    static constexpr quint16 kDirtyCurrentLap  = 0x010;
    static constexpr quint16 kDirtyLastLap     = 0x020;
    static constexpr quint16 kDirtyBestLap     = 0x040;
    static constexpr quint16 kDirtyGForce      = 0x080;
    static constexpr quint16 kDirtyBattery     = 0x100;
    static constexpr quint16 kDirtyFinishLine  = 0x200;
    static constexpr quint16 kDirtyCharging    = 0x400;
    quint16 m_dirty = 0;

    // Last lapTimerState() emitted — recomputed each notify tick; a transition
    // fires lapTimerStateChanged(). Always coincides with some dirty bit, so the
    // notify tick never early-returns through a real transition.
    LapTimerState m_lastLapTimerState = Idle;
    // Same recompute-on-transition treatment for canLearnFinishLine() — which has no
    // dirty bit at all, because it can go false purely by the heading ageing out
    // while the car sits still and sends nothing.
    bool m_lastCanLearnFinishLine = false;

    QTimer m_notifyTimer;
};
