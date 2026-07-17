#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

// Runtime-editable dashboard layout/thresholds, backed by dashboard.conf.
//
// Every gauge/LED/threshold property is READ/WRITE/NOTIFY (unlike the old
// load-once CONSTANT design) so Settings > Dashboard Layout can change the
// live dashboard without a restart. Persistence mirrors DisplayModel: writes
// apply instantly in memory, and a debounced QTimer coalesces the QSettings
// write to dashboard.conf so a burst of stepper taps costs one SD-card write,
// not one per tap.
//
// Entities are addressed by a fixed lowercase key ("rpm", "speed", "coolant",
// "oiltemp", "gear", "laptimer") wherever the API is generic (position swap,
// occupancy lookup) — QML uses these same keys when driving the position
// picker modal.
class DashConfig : public QObject {
    Q_OBJECT

    // --- LED strip -------------------------------------------------------
    Q_PROPERTY(int ledCount        READ ledCount        CONSTANT)
    Q_PROPERTY(int flashIntervalMs READ flashIntervalMs CONSTANT)

    // Limiter is the master control: setting it rescales every threshold
    // below proportionally (see setLimiterRpm()), so the pairs/allBlue are
    // read-only from QML's perspective and all share one NOTIFY.
    Q_PROPERTY(int limiterRpm READ limiterRpm WRITE setLimiterRpm NOTIFY limiterRpmChanged)
    Q_PROPERTY(int pair0Rpm   READ pair0Rpm   NOTIFY pairThresholdsChanged)
    Q_PROPERTY(int pair1Rpm   READ pair1Rpm   NOTIFY pairThresholdsChanged)
    Q_PROPERTY(int pair2Rpm   READ pair2Rpm   NOTIFY pairThresholdsChanged)
    Q_PROPERTY(int pair3Rpm   READ pair3Rpm   NOTIFY pairThresholdsChanged)
    Q_PROPERTY(int pair4Rpm   READ pair4Rpm   NOTIFY pairThresholdsChanged)
    Q_PROPERTY(int allBlueRpm READ allBlueRpm NOTIFY pairThresholdsChanged)

    // --- Gauge visibility & position (9-cell grid) ------------------------
    // Position tokens: top-left | top-center | top-right | center-left |
    // center | center-right | bottom-left | bottom-center | bottom-right
    Q_PROPERTY(bool    gearVisible     READ gearVisible     WRITE setGearVisible     NOTIFY gearVisibleChanged)
    Q_PROPERTY(QString gearPosition    READ gearPosition    NOTIFY gearPositionChanged)
    Q_PROPERTY(bool    rpmVisible      READ rpmVisible      WRITE setRpmVisible      NOTIFY rpmVisibleChanged)
    Q_PROPERTY(QString rpmPosition     READ rpmPosition     NOTIFY rpmPositionChanged)
    Q_PROPERTY(bool    speedVisible    READ speedVisible    WRITE setSpeedVisible    NOTIFY speedVisibleChanged)
    Q_PROPERTY(QString speedPosition   READ speedPosition   NOTIFY speedPositionChanged)
    Q_PROPERTY(bool    coolantVisible  READ coolantVisible  WRITE setCoolantVisible  NOTIFY coolantVisibleChanged)
    Q_PROPERTY(QString coolantPosition READ coolantPosition NOTIFY coolantPositionChanged)
    Q_PROPERTY(bool    oilTempVisible  READ oilTempVisible  WRITE setOilTempVisible  NOTIFY oilTempVisibleChanged)
    Q_PROPERTY(QString oilTempPosition READ oilTempPosition NOTIFY oilTempPositionChanged)
    Q_PROPERTY(bool    lapTimerVisible READ lapTimerVisible WRITE setLapTimerVisible NOTIFY lapTimerVisibleChanged)
    Q_PROPERTY(QString lapTimerPosition READ lapTimerPosition NOTIFY lapTimerPositionChanged)

    // --- Warning/danger thresholds -----------------------------------------
    Q_PROPERTY(double coolantWarningTemp READ coolantWarningTemp WRITE setCoolantWarningTemp NOTIFY coolantWarningTempChanged)
    Q_PROPERTY(double coolantDangerTemp  READ coolantDangerTemp  WRITE setCoolantDangerTemp  NOTIFY coolantDangerTempChanged)
    Q_PROPERTY(double oilWarningTemp     READ oilWarningTemp     WRITE setOilWarningTemp     NOTIFY oilWarningTempChanged)
    Q_PROPERTY(double oilDangerTemp      READ oilDangerTemp      WRITE setOilDangerTemp      NOTIFY oilDangerTempChanged)

    // Monotonic counter bumped on any position/visibility change. QML can't
    // track dependencies through the entityAtPosition() invokable, so views
    // that call it (e.g. the position picker) bind to this to re-evaluate
    // when the layout changes.
    Q_PROPERTY(int layoutRevision READ layoutRevision NOTIFY layoutRevisionChanged)

    // --- RaceBox BLE ---------------------------------------------------------
    Q_PROPERTY(QString raceBoxDeviceName  READ raceBoxDeviceName  CONSTANT)

public:
    explicit DashConfig(QObject *parent = nullptr);
    ~DashConfig() override;

    int     ledCount()        const { return m_ledCount; }
    int     flashIntervalMs() const { return m_flashIntervalMs; }
    int     limiterRpm()      const { return m_limiterRpm; }
    void    setLimiterRpm(int rpm);
    int     pair0Rpm()        const { return m_pair0Rpm; }
    int     pair1Rpm()        const { return m_pair1Rpm; }
    int     pair2Rpm()        const { return m_pair2Rpm; }
    int     pair3Rpm()        const { return m_pair3Rpm; }
    int     pair4Rpm()        const { return m_pair4Rpm; }
    int     allBlueRpm()      const { return m_allBlueRpm; }

    bool    gearVisible()      const { return m_gearVisible; }
    void    setGearVisible(bool v);
    QString gearPosition()     const { return m_gearPosition; }
    bool    rpmVisible()       const { return m_rpmVisible; }
    void    setRpmVisible(bool v);
    QString rpmPosition()      const { return m_rpmPosition; }
    bool    speedVisible()     const { return m_speedVisible; }
    void    setSpeedVisible(bool v);
    QString speedPosition()    const { return m_speedPosition; }
    bool    coolantVisible()   const { return m_coolantVisible; }
    void    setCoolantVisible(bool v);
    QString coolantPosition()  const { return m_coolantPosition; }
    bool    oilTempVisible()   const { return m_oilTempVisible; }
    void    setOilTempVisible(bool v);
    QString oilTempPosition()  const { return m_oilTempPosition; }
    bool    lapTimerVisible()  const { return m_lapTimerVisible; }
    void    setLapTimerVisible(bool v);
    QString lapTimerPosition() const { return m_lapTimerPosition; }

    double  coolantWarningTemp() const { return m_coolantWarningTemp; }
    void    setCoolantWarningTemp(double v);
    double  coolantDangerTemp()  const { return m_coolantDangerTemp; }
    void    setCoolantDangerTemp(double v);
    double  oilWarningTemp()     const { return m_oilWarningTemp; }
    void    setOilWarningTemp(double v);
    double  oilDangerTemp()      const { return m_oilDangerTemp; }
    void    setOilDangerTemp(double v);

    int     layoutRevision()     const { return m_layoutRevision; }

    QString raceBoxDeviceName()  const { return m_raceBoxDeviceName; }

    // Moves `entity` to `pos`. If another visible entity already occupies
    // `pos`, it is swapped into entity's old cell — cells are never blocked,
    // and stored positions stay collision-free by construction.
    Q_INVOKABLE void setEntityPosition(const QString &entity, const QString &pos);

    // Returns the visible entity key occupying `pos` ("" if free). Pass
    // excludeEntity to ignore that entity's own cell (used internally for
    // collision checks; harmless to pass from QML too).
    Q_INVOKABLE QString entityAtPosition(const QString &pos, const QString &excludeEntity = QString()) const;

signals:
    void limiterRpmChanged();
    void pairThresholdsChanged();

    void gearVisibleChanged();
    void gearPositionChanged();
    void rpmVisibleChanged();
    void rpmPositionChanged();
    void speedVisibleChanged();
    void speedPositionChanged();
    void coolantVisibleChanged();
    void coolantPositionChanged();
    void oilTempVisibleChanged();
    void oilTempPositionChanged();
    void lapTimerVisibleChanged();
    void lapTimerPositionChanged();

    void coolantWarningTempChanged();
    void coolantDangerTempChanged();
    void oilWarningTempChanged();
    void oilDangerTempChanged();

    void layoutRevisionChanged();

private:
    void load();
    void persist(); // writes the full config to dashboard.conf; clears m_persistPending
    void schedulePersist();

    // Internal dispatch by entity key — keeps setEntityPosition()/collision
    // resolution generic without a per-entity switch at every call site.
    bool    isVisible(const QString &entity) const;
    QString positionFor(const QString &entity) const;
    void    setPositionRaw(const QString &entity, const QString &pos);

    // If `entity` is visible and its current cell collides with another
    // already-resolved visible entity, moves it to the first free cell.
    // Safe only when every other entity's position is already known
    // collision-free — i.e. a single entity newly becoming visible. For the
    // bulk post-load pass (where multiple entities can collide at once) use
    // resolveLoadedCollisions() instead.
    void assignFreeCellIfOccupied(const QString &entity);

    // One-time pass after load(): walks entities in priority order so the
    // earliest in priority always keeps a contested cell, moving only the
    // later entities that actually lose a conflict.
    void resolveLoadedCollisions();

    void bumpLayoutRevision(); // ++m_layoutRevision; emit layoutRevisionChanged()

    void recomputePairThresholds(); // derives pair0-4/allBlue from m_limiterRpm

    int     m_ledCount        = 10;
    int     m_flashIntervalMs = 80;
    int     m_limiterRpm      = 6800;
    int     m_pair0Rpm        = 5800;
    int     m_pair1Rpm        = 6000;
    int     m_pair2Rpm        = 6200;
    int     m_pair3Rpm        = 6500;
    int     m_pair4Rpm        = 6600;
    int     m_allBlueRpm      = 6750;

    bool    m_gearVisible       = false;
    QString m_gearPosition      = "center";
    bool    m_rpmVisible        = true;
    QString m_rpmPosition       = "top-left";
    bool    m_speedVisible      = true;
    QString m_speedPosition     = "top-center";
    bool    m_coolantVisible    = true;
    QString m_coolantPosition   = "center-left";
    bool    m_oilTempVisible    = true;
    QString m_oilTempPosition   = "top-right";
    bool    m_lapTimerVisible   = true;
    QString m_lapTimerPosition  = "bottom-left";

    double  m_coolantWarningTemp = 95.0;
    double  m_coolantDangerTemp  = 105.0;
    double  m_oilWarningTemp     = 120.0;
    double  m_oilDangerTemp      = 135.0;

    QString m_raceBoxDeviceName = "RaceBox Mini";

    int     m_layoutRevision = 0;

    static constexpr int kPersistDebounceMs = 500;
    QTimer m_persistTimer;
    bool   m_persistPending = false;
};
