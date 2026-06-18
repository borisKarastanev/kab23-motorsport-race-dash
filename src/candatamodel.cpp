#include "candatamodel.h"
#include "canscaling.h"

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
        if (p.size() >= CanScaling::kOffsetRpm + 2) {
            int rpm = CanScaling::decodeRpm(qFromBigEndian<quint16>(p.constData() + CanScaling::kOffsetRpm));
            rpm = qMin(rpm, 7000);
            // At 50 Hz frame rate, a >2000 RPM single-frame drop is a CAN transient,
            // not a real deceleration event — reject it.
            if (m_rpm > 600 && rpm < m_rpm - 2000)
                break;
            if (rpm == m_rpm)
                break;
            m_rpm = rpm;
            emit rpmChanged(); // bypass the 10 Hz timer — RPM needs immediate QML updates
        }
        break;

    case CanScaling::kFrameTemp:
        if (p.size() >= CanScaling::kOffsetCoolant + 1) {
            m_coolantTemp = CanScaling::decodeCoolant(static_cast<quint8>(p[CanScaling::kOffsetCoolant]));
            m_dirty |= kDirtyCoolant;
        }
        break;

    case CanScaling::kFrameDme4:
        if (p.size() >= CanScaling::kOffsetOilTemp + 1) {
            m_oilTemp = CanScaling::decodeOilTemp(static_cast<quint8>(p[CanScaling::kOffsetOilTemp]));
            m_dirty |= kDirtyOilTemp;
        }
        break;

    default:
        break;
    }
}

void CanDataModel::setSpeed(int kmh)
{
    m_speed = kmh;
    m_dirty |= kDirtySpeed;
}

void CanDataModel::emitNotifications()
{
    if (!m_dirty)
        return;
    const quint8 dirty = m_dirty;
    m_dirty = 0;

    if (dirty & kDirtyCoolant) emit coolantTempChanged();
    if (dirty & kDirtyOilTemp) emit oilTempChanged();
    if (dirty & kDirtySpeed)   emit speedChanged();
    if (dirty & kDirtyGear)    emit gearChanged();
}
