#pragma once

#include "raceboxdata.h"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantList>
#include <QVector>

class RaceBoxModel : public QObject {
    Q_OBJECT
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
    Q_PROPERTY(bool    hasStartedTiming READ hasStartedTiming NOTIFY hasStartedTimingChanged)

public:
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
    // True once the first lap of this app run (or since the finish line was
    // last cleared) has started — stays true across a resetLapCounters() so
    // UI prompts like "cross S/F line to start" don't reappear after a save.
    bool   hasStartedTiming() const { return m_hasStartedTiming; }

    // Called from main.cpp to pass dashconfig finish line values at startup
    void setFinishLine(double lat, double lon, double radiusM);

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
    void hasStartedTimingChanged();
    // Emitted on each finish line tap so the caller can persist the coordinates
    void finishLineLearned(double lat, double lon);
    // Emitted to feed CanDataModel
    void speedKmhChanged(int kmh);
    // Emitted immediately when a lap completes — not throttled, safe for persistence.
    // path is a flat [lat, lon, lat, lon, …] list of the GPS fixes recorded during the lap.
    void lapCompleted(qint64 ms, const QVariantList &path);

private slots:
    void emitNotifications();

private:
    void updateLapTiming(double lat, double lon, double speedKmh);
    static double haversineM(double lat1, double lon1, double lat2, double lon2);
    // Shared by clearFinishLine() and resetLapCounters() — resets lap number,
    // timer, and current-lap path. Does not touch m_hasStartedTiming or the
    // finish-line fields; callers handle those themselves.
    void resetLapState();

    // Connection / fix
    bool   m_connected   = false;
    bool   m_hasFix      = false;
    int    m_satellites  = 0;

    // Lap timing
    int           m_lapNumber   = 0;
    qint64        m_lastLapMs   = 0;
    qint64        m_bestLapMs   = 0;
    QElapsedTimer m_lapTimer;
    bool          m_lapTimerRunning = false;
    bool          m_hasStartedTiming = false;

    // Current lap GPS path — flat [lat, lon, lat, lon, …], distance-decimated
    QVector<double> m_currentLapPath;
    double          m_lastStoredLat = 0.0;
    double          m_lastStoredLon = 0.0;

    // Finish line (virtual start/finish)
    bool   m_finishLineSet  = false;
    double m_finishLineLat  = 0.0;
    double m_finishLineLon  = 0.0;
    double m_finishRadiusM  = 20.0;
    bool   m_inFinishZone   = false;

    // Last known position (for learnFinishLineHere)
    double m_lastLat = 0.0;
    double m_lastLon = 0.0;

    // Motion
    int    m_speedKmh       = 0;
    double m_gForceX        = 0.0;
    double m_gForceY        = 0.0;
    double m_gForceZ        = 0.0;
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
    static constexpr quint16 kDirtyStartedTiming = 0x800;
    quint16 m_dirty = 0;

    QTimer m_notifyTimer;
};
