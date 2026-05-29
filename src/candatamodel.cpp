#include "candatamodel.h"
#include "canscaling.h"
#include <QDebug>

CanDataModel::CanDataModel(QObject *parent)
    : QObject(parent)
{
    m_notifyTimer.setInterval(100); // 10 Hz — decouples CAN bus rate from QML update rate
    connect(&m_notifyTimer, &QTimer::timeout, this, &CanDataModel::emitNotifications);
    m_notifyTimer.start();
}

void CanDataModel::onFrame(const QCanBusFrame &frame)
{
    const QByteArray &p = frame.payload();

    switch (frame.frameId()) {

    case CanScaling::kFrameRpm:
        if (p.size() >= 4) {
            m_rpm = CanScaling::decodeRpm(qFromBigEndian<quint16>(p.constData() + 2));
            m_dirty |= kDirtyRpm;
        }
        break;

    case CanScaling::kFrameTemp:
        if (p.size() >= 4) {
            m_coolantTemp = CanScaling::decodeTemp(static_cast<quint8>(p[1]));
            m_oilTemp     = CanScaling::decodeTemp(static_cast<quint8>(p[3]));
            m_dirty |= kDirtyTemp;
        }
        break;

    case CanScaling::kFrameSpeed:
        if (p.size() >= 2) {
            m_speed = CanScaling::decodeSpeed(qFromBigEndian<quint16>(p.constData()));
            m_dirty |= kDirtySpeed;
        }
        break;

    case CanScaling::kFrameGear:
        if (p.size() >= 1) {
            m_gear  = CanScaling::decodeGear(static_cast<quint8>(p[0]));
            m_dirty |= kDirtyGear;
        }
        break;

    default:
        break;
    }
}

void CanDataModel::emitNotifications()
{
    if (!m_dirty)
        return;
    const quint8 dirty = m_dirty;
    m_dirty = 0;

    if (dirty & kDirtyRpm)   emit rpmChanged();
    if (dirty & kDirtyTemp) { emit coolantTempChanged(); emit oilTempChanged(); }
    if (dirty & kDirtySpeed) emit speedChanged();
    if (dirty & kDirtyGear)  emit gearChanged();

    qDebug() << "RPM:" << m_rpm
             << "| Coolant:" << m_coolantTemp << "°C"
             << "| Oil:" << m_oilTemp << "°C"
             << "| Speed:" << m_speed << "km/h"
             << "| Gear:" << m_gear;
}
