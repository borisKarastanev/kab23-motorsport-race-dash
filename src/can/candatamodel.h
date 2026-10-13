#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QCanBusFrame>
#include <array>

class CanDataModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int rpm READ rpm NOTIFY rpmChanged)
    Q_PROPERTY(double coolantTemp READ coolantTemp NOTIFY coolantTempChanged)
    Q_PROPERTY(double oilTemp READ oilTemp NOTIFY oilTempChanged)
    // False until a 0x545 (DME4) frame has actually been decoded. Without
    // this, oilTemp's 0.0 default is indistinguishable from a genuine 0 °C
    // reading, so consumers that treat "cold" as actionable (the cold-oil
    // limiter reduction) would latch on at startup, and stay latched forever
    // on a car/DME that never sends 0x545 or when can0 fails to come up.
    //
    // "Seen", not "valid": this latches on the first frame and never resets,
    // so it answers "has this signal ever arrived", NOT "is this reading
    // current". A mid-session bus dropout leaves it true with a stale temp.
    // Freshness would need a per-signal last-seen timestamp — worth adding if
    // a "NO CAN" indicator ever lands, but deliberately not built here.
    Q_PROPERTY(bool oilTempSeen READ oilTempSeen NOTIFY oilTempSeenChanged)
    // Displayed speed. Sourced from the MK60 wheel speeds (0x1F0) while those
    // frames are fresh — front average normally, fastest wheel while braking —
    // otherwise from the RaceBox GPS via setSpeed().
    Q_PROPERTY(int speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(bool speedFromWheels READ speedFromWheels NOTIFY speedFromWheelsChanged)
    Q_PROPERTY(double wheelSpeedFL READ wheelSpeedFL NOTIFY wheelSpeedsChanged)
    Q_PROPERTY(double wheelSpeedFR READ wheelSpeedFR NOTIFY wheelSpeedsChanged)
    Q_PROPERTY(double wheelSpeedRL READ wheelSpeedRL NOTIFY wheelSpeedsChanged)
    Q_PROPERTY(double wheelSpeedRR READ wheelSpeedRR NOTIFY wheelSpeedsChanged)
    Q_PROPERTY(double steeringAngle READ steeringAngle NOTIFY steeringAngleChanged)
    Q_PROPERTY(bool brakePressed READ brakePressed NOTIFY brakePressedChanged)
    Q_PROPERTY(int gear READ gear NOTIFY gearChanged)

public:
    explicit CanDataModel(QObject *parent = nullptr);

    int    rpm()         const { return m_rpm; }
    double coolantTemp() const { return m_coolantTemp; }
    double oilTemp()     const { return m_oilTemp; }
    bool   oilTempSeen() const { return m_oilTempSeen; }
    int    speed()       const { return m_speed; }
    bool   speedFromWheels() const { return m_speedFromWheels; }
    double wheelSpeedFL()  const { return m_wheels[FL]; }
    double wheelSpeedFR()  const { return m_wheels[FR]; }
    double wheelSpeedRL()  const { return m_wheels[RL]; }
    double wheelSpeedRR()  const { return m_wheels[RR]; }
    double steeringAngle() const { return m_steeringAngle; }
    bool   brakePressed()  const { return m_brakePressed; }
    int    gear()        const { return m_gear; }

    // A wheel-speed reading older than this no longer drives speed — the GPS
    // fallback takes over so a dead ABS can't freeze the gauge at its last value.
    static constexpr qint64 kWheelSpeedStaleMs = 500;

public slots:
    void onFrame(const QCanBusFrame &frame);
    void setSpeed(int kmh);

signals:
    void rpmChanged();
    void coolantTempChanged();
    void oilTempChanged();
    void oilTempSeenChanged();
    void speedChanged();
    void speedFromWheelsChanged();
    void wheelSpeedsChanged();
    void steeringAngleChanged();
    void brakePressedChanged();
    void gearChanged();

private slots:
    void emitNotifications();

private:
    static constexpr quint16 kDirtyRpm         = 0x001;
    static constexpr quint16 kDirtyCoolant     = 0x002;
    static constexpr quint16 kDirtyOilTemp     = 0x004;
    static constexpr quint16 kDirtySpeed       = 0x008;
    static constexpr quint16 kDirtyGear        = 0x010;
    static constexpr quint16 kDirtyOilSeen     = 0x020;
    static constexpr quint16 kDirtyWheelSpeeds = 0x040;
    static constexpr quint16 kDirtySpeedSource = 0x080;
    static constexpr quint16 kDirtySteering    = 0x100;
    static constexpr quint16 kDirtyBrake       = 0x200;

    enum Wheel { FL, FR, RL, RR }; // matches the 0x1F0 field order

    bool wheelSpeedFresh() const;
    int  vehicleSpeedFromWheels() const;
    void applySpeed(int kmh);
    void updateSpeedSource();

    int    m_rpm         = 0;
    double m_coolantTemp = 0.0;
    double m_oilTemp     = 0.0;
    bool   m_oilTempSeen = false;
    int    m_speed       = 0;
    int    m_gpsSpeed    = 0;
    bool   m_speedFromWheels = false;
    std::array<double, 4> m_wheels{};
    double m_steeringAngle = 0.0;
    bool   m_brakePressed  = false;
    QElapsedTimer m_wheelSpeedClock; // restarted on every 0x1F0; invalid until the first
    int    m_gear        = 0;

    quint16 m_dirty = 0;
    QTimer m_notifyTimer;
};
