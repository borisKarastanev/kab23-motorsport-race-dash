#pragma once

#include <QObject>
#include <QTimer>
#include <QCanBusFrame>

class CanDataModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int rpm READ rpm NOTIFY rpmChanged)
    Q_PROPERTY(double coolantTemp READ coolantTemp NOTIFY coolantTempChanged)
    Q_PROPERTY(double oilTemp READ oilTemp NOTIFY oilTempChanged)
    Q_PROPERTY(int speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(int gear READ gear NOTIFY gearChanged)

public:
    explicit CanDataModel(QObject *parent = nullptr);

    int    rpm()         const { return m_rpm; }
    double coolantTemp() const { return m_coolantTemp; }
    double oilTemp()     const { return m_oilTemp; }
    int    speed()       const { return m_speed; }
    int    gear()        const { return m_gear; }

public slots:
    void onFrame(const QCanBusFrame &frame);

signals:
    void rpmChanged();
    void coolantTempChanged();
    void oilTempChanged();
    void speedChanged();
    void gearChanged();

private slots:
    void emitNotifications();

private:
    static constexpr quint8 kDirtyRpm   = 0x01;
    static constexpr quint8 kDirtyTemp  = 0x02;
    static constexpr quint8 kDirtySpeed = 0x04;
    static constexpr quint8 kDirtyGear  = 0x08;

    int    m_rpm         = 0;
    double m_coolantTemp = 0.0;
    double m_oilTemp     = 0.0;
    int    m_speed       = 0;
    int    m_gear        = 0;

    quint8 m_dirty = 0;
    QTimer m_notifyTimer;
};
