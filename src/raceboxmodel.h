#pragma once

#include "raceboxdata.h"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantList>
#include <QVector>

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
    Q_PROPERTY(double  gForceX        READ gForceX        NOTIFY gForceXChanged)
    Q_PROPERTY(double  gForceY        READ gForceY        NOTIFY gForceYChanged)
    Q_PROPERTY(double  gForceZ        READ gForceZ        NOTIFY gForceZChanged)
    Q_PROPERTY(int     batteryPercent  READ batteryPercent  NOTIFY batteryPercentChanged)
    Q_PROPERTY(bool    batteryCharging READ batteryCharging NOTIFY batteryChargingChanged)
    Q_PROPERTY(bool    finishLineSet   READ finishLineSet   NOTIFY finishLineSetChanged)
    Q_PROPERTY(LapTimerState lapTimerState READ lapTimerState NOTIFY lapTimerStateChanged)

    explicit RaceBoxModel(QObject *parent = nullptr);

    bool   connected()      const { return m_connected; }
    bool   hasFix()         const { return m_hasFix; }
    int    satellites()     const { return m_satellites; }
    int    lapNumber()      const { return m_lapNumber; }
    qint64 currentLapMs()   const;
    qint64 lastLapMs()      const { return m_lastLapMs; }
    qint64 bestLapMs()      const { return m_bestLapMs; }
    double gForceX()        const { return m_gForceX; }
    double gForceY()        const { return m_gForceY; }
    double gForceZ()        const { return m_gForceZ; }
    int    batteryPercent()  const { return m_batteryPercent; }
    bool   batteryCharging() const { return m_batteryCharging; }
    bool   finishLineSet()   const { return m_finishLineSet; }
    // Single source of truth for the lap-timer lifecycle (see LapTimerState).
    LapTimerState lapTimerState() const;

    // Sets the start/finish line as a gate: a segment between two GPS points
    // (A→B) spanning the track width. A lap is timed when the car's path crosses
    // this segment. All-zero coordinates are treated as "unset" (no-op).
    void setFinishLine(double latA, double lonA, double latB, double lonB);

    // Last known GPS fix — used by TrackModel's nearest-track auto-detect scan
    double lastLat() const { return m_lastLat; }
    double lastLon() const { return m_lastLon; }

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
    void gForceXChanged();
    void gForceYChanged();
    void gForceZChanged();
    void batteryPercentChanged();
    void batteryChargingChanged();
    void finishLineSetChanged();
    void lapTimerStateChanged();
    // Emitted when the finish-line gate is learned (perpendicular to travel) or
    // cleared (all zero), so the caller can persist the two endpoints.
    void finishLineLearned(double latA, double lonA, double latB, double lonB);
    // Emitted to feed CanDataModel
    void speedKmhChanged(int kmh);
    // Emitted immediately when a lap completes — not throttled, safe for persistence.
    // path is a flat [lat, lon, lat, lon, …] list of the GPS fixes recorded during the lap.
    void lapCompleted(qint64 ms, const QVariantList &path);

private slots:
    void emitNotifications();

private:
    // Tests whether the path segment (prev fix → current fix) crosses the
    // finish-line gate; on a crossing it interpolates the exact crossing time
    // between the two fixes and completes/starts a lap. prevMs/nowMs are the
    // fix arrival times (from m_clock) used for that interpolation.
    void updateLapTiming(double prevLat, double prevLon, double curLat, double curLon,
                         double speedKmh, qint64 prevMs, qint64 nowMs);
    // Builds a gate perpendicular to the recent direction of travel, centred on
    // (lat, lon), and writes its two endpoints to the out-params.
    void gateFromHeading(double lat, double lon,
                         double &latA, double &lonA, double &latB, double &lonB) const;
    // Shared by clearFinishLine() and resetLapCounters() — resets lap number,
    // timer, and current-lap path. Does not touch m_hasStartedTiming or the
    // finish-line fields; callers handle those themselves.
    void resetLapState();

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

    // Finish line as a gate: segment A→B spanning the track width. A lap is
    // timed when the path (prev→cur fix) crosses this segment.
    bool   m_finishLineSet = false;
    double m_gateLatA = 0.0, m_gateLonA = 0.0;
    double m_gateLatB = 0.0, m_gateLonB = 0.0;

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

    // Recent direction of travel (radians, bearing) — used to orient a learned
    // gate perpendicular to the track.
    double m_headingRad  = 0.0;
    bool   m_haveHeading = false;

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

    QTimer m_notifyTimer;
};
