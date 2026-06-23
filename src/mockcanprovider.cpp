#include "mockcanprovider.h"
#include "canscaling.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr int SHIFT_RPM   = 6800;
constexpr int IDLE_RPM    =  650;
constexpr int CYCLE_TICKS = 1150; // 23.0 s at 50 Hz

} // namespace

static constexpr quint64 COOLANT_RAMP_TICKS = 3000; // 60 s at 50 Hz
static constexpr quint64 OIL_RAMP_TICKS     = 4500; // 90 s at 50 Hz

MockCanProvider::MockCanProvider(QObject *parent)
    : ICanProvider(parent)
{
    m_timer.setInterval(20); // 50 Hz
    connect(&m_timer, &QTimer::timeout, this, &MockCanProvider::onTick);
}

void MockCanProvider::start() { m_tick = 0; m_timer.start(); }
void MockCanProvider::stop()  { m_timer.stop(); }

void MockCanProvider::onTick()
{
    ++m_tick;

    // RPM: sine wave between idle and redline over the cycle period
    const double phase = (static_cast<double>(m_tick % CYCLE_TICKS) / CYCLE_TICKS) * 2.0 * M_PI;
    const int rpm = IDLE_RPM + static_cast<int>((SHIFT_RPM - IDLE_RPM) * 0.5 * (1.0 - std::cos(phase)));

    const double coolantProgress = std::min(static_cast<double>(m_tick) / COOLANT_RAMP_TICKS, 1.0);
    const double oilProgress     = std::min(static_cast<double>(m_tick) / OIL_RAMP_TICKS,     1.0);

    emit frameReady(rpmFrame(rpm));
    emit frameReady(tempFrame(20.0 + coolantProgress * 70.0));
    emit frameReady(dme4Frame(20.0 + oilProgress * 90.0));
}

QCanBusFrame MockCanProvider::rpmFrame(int rpm)
{
    QByteArray payload(8, 0x00);
    CanScaling::encodeRpm(rpm, payload.data() + CanScaling::kOffsetRpm);
    return QCanBusFrame(CanScaling::kFrameRpm, payload);
}

QCanBusFrame MockCanProvider::tempFrame(double coolant)
{
    QByteArray payload(8, 0x00);
    payload[CanScaling::kOffsetCoolant] = static_cast<char>(CanScaling::encodeCoolant(coolant));
    return QCanBusFrame(CanScaling::kFrameTemp, payload);
}

QCanBusFrame MockCanProvider::dme4Frame(double oilTemp)
{
    QByteArray payload(8, 0x00);
    payload[CanScaling::kOffsetOilTemp] = static_cast<char>(CanScaling::encodeOilTemp(oilTemp));
    return QCanBusFrame(CanScaling::kFrameDme4, payload);
}
