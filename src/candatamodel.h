#pragma once

#include <QObject>
#include <QTimer>
#include <QCanBusFrame>

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
    Q_PROPERTY(int speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(int gear READ gear NOTIFY gearChanged)

public:
    explicit CanDataModel(QObject *parent = nullptr);

    int    rpm()         const { return m_rpm; }
    double coolantTemp() const { return m_coolantTemp; }
    double oilTemp()     const { return m_oilTemp; }
    bool   oilTempSeen() const { return m_oilTempSeen; }
    int    speed()       const { return m_speed; }
    int    gear()        const { return m_gear; }

public slots:
    void onFrame(const QCanBusFrame &frame);
    void setSpeed(int kmh);

signals:
    void rpmChanged();
    void coolantTempChanged();
    void oilTempChanged();
    void oilTempSeenChanged();
    void speedChanged();
    void gearChanged();

private slots:
    void emitNotifications();

private:
    static constexpr quint8 kDirtyRpm     = 0x01;
    static constexpr quint8 kDirtyCoolant = 0x02;
    static constexpr quint8 kDirtyOilTemp = 0x04;
    static constexpr quint8 kDirtySpeed  = 0x08;
    static constexpr quint8 kDirtyGear   = 0x10;
    static constexpr quint8 kDirtyOilSeen = 0x20;

    int    m_rpm         = 0;
    double m_coolantTemp = 0.0;
    double m_oilTemp     = 0.0;
    bool   m_oilTempSeen = false;
    int    m_speed       = 0;
    int    m_gear        = 0;

    quint8 m_dirty = 0;
    QTimer m_notifyTimer;
};
