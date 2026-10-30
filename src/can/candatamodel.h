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
    // Low oil pressure emergency: the DME4 (0x545) oil-pressure-low bit while
    // the engine is running (fresh 0x316, rpm > kOilPressureMinRpm). A stopped
    // or cranking engine legitimately has no pressure, so the bit alone isn't
    // an alarm. Debounced both ways by kOilPressureDebounceMs, so the switch
    // lagging just after start-up, or chattering at a hot idle, can't flash
    // the full-screen warning. The bit goes stale like the driver inputs, so a
    // dead bus clears it rather than latching the warning on.
    Q_PROPERTY(bool oilPressureWarning READ oilPressureWarning NOTIFY oilPressureWarningChanged)
    // ABS regulating — derived from wheel slip under braking; ASC1 has no
    // documented "ABS active" bit. See the kAbs* constants.
    Q_PROPERTY(bool absActive READ absActive NOTIFY absActiveChanged)
    // MK60 ABS warning lamp (0x153 LV_ABS_LED) — ABS may be off. See kAbsFault*.
    Q_PROPERTY(bool absFault READ absFault NOTIFY absFaultChanged)
    Q_PROPERTY(bool speedFromWheels READ speedFromWheels NOTIFY speedFromWheelsChanged)
    Q_PROPERTY(double wheelSpeedFL READ wheelSpeedFL NOTIFY wheelSpeedsChanged)
    Q_PROPERTY(double wheelSpeedFR READ wheelSpeedFR NOTIFY wheelSpeedsChanged)
    Q_PROPERTY(double wheelSpeedRL READ wheelSpeedRL NOTIFY wheelSpeedsChanged)
    Q_PROPERTY(double wheelSpeedRR READ wheelSpeedRR NOTIFY wheelSpeedsChanged)
    // Driver inputs. Each *Valid flag is true only while its frame is fresh
    // (< kInputStaleMs) — false before the first frame and after the bus goes
    // quiet, so a missing signal can't pass for "centred" / "off" / "closed".
    // While invalid the value itself reads as its neutral default (0 / false).
    Q_PROPERTY(double steeringAngle READ steeringAngle NOTIFY steeringAngleChanged)
    Q_PROPERTY(bool steeringValid READ steeringValid NOTIFY steeringAngleChanged)
    Q_PROPERTY(bool brakePressed READ brakePressed NOTIFY brakePressedChanged)
    Q_PROPERTY(bool brakeValid READ brakeValid NOTIFY brakePressedChanged)
    // Accelerator pedal 0..100 % (0x329 byte 5). Also invalid while the DME
    // reports init (0x00) or a PVS error (0xFF).
    Q_PROPERTY(double throttle READ throttle NOTIFY throttleChanged)
    Q_PROPERTY(bool throttleValid READ throttleValid NOTIFY throttleChanged)
    Q_PROPERTY(int gear READ gear NOTIFY gearChanged)

public:
    explicit CanDataModel(QObject *parent = nullptr);

    int    rpm()         const { return m_rpm; }
    double coolantTemp() const { return m_coolantTemp; }
    double oilTemp()     const { return m_oilTemp; }
    bool   oilTempSeen() const { return m_oilTempSeen; }
    bool   oilPressureWarning() const { return m_oilPressureWarning.state; }
    bool   absActive()   const { return m_absActive; }
    bool   absFault()    const { return m_absFault.state; }
    int    speed()       const { return m_speed; }
    bool   speedFromWheels() const { return m_speedFromWheels; }
    double wheelSpeedFL()  const { return m_wheels[FL]; }
    double wheelSpeedFR()  const { return m_wheels[FR]; }
    double wheelSpeedRL()  const { return m_wheels[RL]; }
    double wheelSpeedRR()  const { return m_wheels[RR]; }
    double steeringAngle() const { return m_steering.value; }
    bool   steeringValid() const { return m_steering.valid; }
    bool   brakePressed()  const { return m_brake.value; }
    bool   brakeValid()    const { return m_brake.valid; }
    double throttle()      const { return m_throttle.value; }
    bool   throttleValid() const { return m_throttle.valid; }
    int    gear()        const { return m_gear; }

    // A wheel-speed reading older than this no longer drives speed — the GPS
    // fallback takes over so a dead ABS can't freeze the gauge at its last value.
    static constexpr qint64 kWheelSpeedStaleMs = 500;
    // Wheel-derived speeds below this show as 0 (standstill sensor offset).
    static constexpr double kWheelSpeedDeadbandKmh = 1.5;
    // Same idea for the driver inputs shown on the INPUTS screen.
    static constexpr qint64 kInputStaleMs = 500;
    // Below this the engine is stopped or cranking — no oil pressure is expected.
    static constexpr int kOilPressureMinRpm = 500;
    // How long "low pressure while running" must hold before the warning shows
    // (and how long it must be gone before the warning clears).
    static constexpr qint64 kOilPressureDebounceMs = 1000;
    // ABS-active: brake on and a wheel kAbsSlipRatio behind the fastest, for
    // kAbsSlipFrames frames, held kAbsHoldMs. Cornering spreads the wheels
    // too, but at ~1 g the inside-rear/outside-front gap reaches 20 % only
    // below ~34 km/h, so above kAbsMinSpeedKmh a 20 % gap can't be the corner.
    static constexpr double kAbsSlipRatio   = 0.20;
    static constexpr double kAbsMinSpeedKmh = 40.0;
    static constexpr int    kAbsSlipFrames  = 2;
    // Slip frames further apart than this aren't "consecutive" — a few 0x1F0
    // periods, well short of the 500 ms stale timeout.
    static constexpr qint64 kAbsSlipGapMs   = 100;
    static constexpr qint64 kAbsHoldMs      = 500;
    // ABS fault: the lamp bit counts only once the car has rolled past
    // kAbsFaultMinSpeedKmh (the MK60 keeps it on until it sees the wheels
    // move), then must hold longer than the ~2 s key-on bulb check.
    static constexpr qint64 kAbsFaultDebounceMs = 3000;
    static constexpr double kAbsFaultMinSpeedKmh = 10.0;
    // Device Log lines for ASC1 bits 5-7 per run — enough to see whether a bit
    // tracks ABS, without flooding the log if one toggles every brake zone.
    static constexpr int    kAsc1StatusLogLimit = 20;

public slots:
    void onFrame(const QCanBusFrame &frame);
    void setSpeed(int kmh);

signals:
    void rpmChanged();
    void coolantTempChanged();
    void oilTempChanged();
    void oilTempSeenChanged();
    void oilPressureWarningChanged();
    void absActiveChanged();
    void absFaultChanged();
    void speedChanged();
    void speedFromWheelsChanged();
    void wheelSpeedsChanged();
    void steeringAngleChanged();
    void brakePressedChanged();
    void throttleChanged();
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
    static constexpr quint16 kDirtyThrottle    = 0x400;
    static constexpr quint16 kDirtyOilPressure = 0x800;
    static constexpr quint16 kDirtyAbsActive   = 0x1000;
    static constexpr quint16 kDirtyAbsFault    = 0x2000;

    enum Wheel { FL, FR, RL, RR }; // matches the 0x1F0 field order

    // A driver input: its value, whether that value is live, and when its
    // frame last arrived. While invalid, value holds the neutral T{}.
    template <typename T>
    struct Input {
        T value{};
        bool valid = false;
        QElapsedTimer clock; // restarted on every frame; invalid until the first

        // The value as of right now: neutral once the frame has gone quiet,
        // even before the next notify tick's expireInput() catches up. For
        // per-frame readers that mustn't act on a latched value.
        T live() const { return fresh(clock, kInputStaleMs) ? value : T{}; }
    };

    // A flag that follows its raw input only once the input has held the new
    // state for riseMs (turning on) or fallMs (turning off).
    struct Debounced {
        bool state = false;
        QElapsedTimer pending; // running while the raw input disagrees with state
        bool update(bool raw, qint64 riseMs, qint64 fallMs); // true when state flips
    };

    static bool fresh(const QElapsedTimer &clock, qint64 staleMs);
    bool wheelSpeedFresh() const;
    template <typename T> void updateInput(Input<T> &in, T value, bool valid, quint16 dirtyBit);
    template <typename T> void expireInput(Input<T> &in, quint16 dirtyBit);
    void expireStaleInputs();
    double fastestWheel() const;
    int  vehicleSpeedFromWheels() const;
    void applySpeed(int kmh);
    void updateSpeedSource();
    bool engineRunning() const;
    void updateOilPressureWarning();
    bool wheelsSlipping() const;
    void updateAbsActive();
    void expireAbsActive();
    static bool lagging(double kmh, double ref);
    void updateImplausibleWheels();
    void updateAbsFromWheels();
    void logAsc1StatusBits(quint8 byte0);

    int    m_rpm         = 0;
    QElapsedTimer m_rpmClock;    // restarted on every 0x316; invalid until the first
    bool   m_rpmDropRejected = false; // last 0x316 was rejected as a transient drop
    double m_coolantTemp = 0.0;
    double m_oilTemp     = 0.0;
    bool   m_oilTempSeen = false;
    int    m_speed       = 0;
    int    m_gpsSpeed    = 0;
    bool   m_speedFromWheels = false;
    std::array<double, 4> m_wheels{};
    Input<double> m_steering;
    Input<bool>   m_brake;
    Input<double> m_throttle;
    Input<bool>   m_oilPressureLow; // raw 0x545 bit, fresh-gated like the inputs
    Debounced m_oilPressureWarning;
    bool   m_dme4ShortWarned = false;
    Input<bool>   m_absLampRaw;      // raw LV_ABS_LED, fresh-gated like the inputs
    Debounced m_absFault;
    bool   m_absActive = false;
    int    m_absSlipFrames = 0;      // consecutive 0x1F0 frames showing slip, capped at kAbsSlipFrames
    QElapsedTimer m_absLastSlip;     // restarted on every slipping 0x1F0 frame
    std::array<bool, 4> m_wheelImplausible{}; // read kAbsSlipRatio low while rolling unbraked
    bool   m_hasRolled = false;      // wheels have passed kAbsFaultMinSpeedKmh this run
    int    m_asc1StatusBits = -1;    // last logged byte 0 bits 5-7; -1 = none yet
    int    m_asc1StatusLogged = 0;   // lines written, capped at kAsc1StatusLogLimit
    QElapsedTimer m_wheelSpeedClock; // restarted on every 0x1F0; invalid until the first
    int    m_gear        = 0;

    quint16 m_dirty = 0;
    QTimer m_notifyTimer;
};
