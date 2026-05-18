#include "mockcanprovider.h"
#include "canscaling.h"
#include <algorithm>

namespace {

struct GearData {
    int    entryRpm;
    double entrySpeed;
    double exitSpeed;
    int    durationTicks; // at 50 Hz
};

constexpr int SHIFT_RPM = 6800;

constexpr GearData GEARS[5] = {
    {  650,   0.0,  50.0, 150 },  // G1 — 3.0 s  (cold start from idle)
    { 5000,  50.0,  90.0, 125 },  // G2 — 2.5 s  (6800 - 1800 drop)
    { 5000,  90.0, 130.0, 225 },  // G3 — 4.5 s
    { 5000, 130.0, 175.0, 250 },  // G4 — 5.0 s
    { 5000, 175.0, 220.0, 400 },  // G5 — 8.0 s
};

// Sum of all durationTicks — loop restarts after G5
constexpr int CYCLE_TICKS = 150 + 125 + 225 + 250 + 400; // 1150 = 23.0 s

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

    // --- RPM + Speed: 5-gear acceleration loop ---
    const int cycleTick = static_cast<int>(m_tick % CYCLE_TICKS);

    int    rpm    = SHIFT_RPM;
    double speedD = 220.0;
    int    gear   = 1;

    int tickStart = 0;
    int gearIdx   = 0;
    for (const GearData &g : GEARS) {
        if (cycleTick < tickStart + g.durationTicks) {
            const double p = static_cast<double>(cycleTick - tickStart) / g.durationTicks;
            rpm    = static_cast<int>(g.entryRpm + (SHIFT_RPM - g.entryRpm) * p);
            speedD = g.entrySpeed + (g.exitSpeed - g.entrySpeed) * p;
            gear   = gearIdx + 1;
            break;
        }
        tickStart += g.durationTicks;
        ++gearIdx;
    }

    // --- Temperature ramps (independent of gear state) ---
    const double coolantProgress = std::min(static_cast<double>(m_tick) / COOLANT_RAMP_TICKS, 1.0);
    const double oilProgress     = std::min(static_cast<double>(m_tick) / OIL_RAMP_TICKS,     1.0);

    emit frameReady(rpmFrame(rpm));
    emit frameReady(tempFrame(20.0 + coolantProgress * 70.0, 20.0 + oilProgress * 90.0));
    emit frameReady(speedFrame(static_cast<int>(speedD)));
    emit frameReady(gearFrame(gear));
}

QCanBusFrame MockCanProvider::rpmFrame(int rpm)
{
    QByteArray payload(8, 0x00);
    qToBigEndian<quint16>(CanScaling::encodeRpm(rpm), payload.data() + 2);
    return QCanBusFrame(CanScaling::kFrameRpm, payload);
}

QCanBusFrame MockCanProvider::tempFrame(double coolant, double oil)
{
    QByteArray payload(8, 0x00);
    payload[1] = static_cast<char>(CanScaling::encodeTemp(coolant));
    payload[3] = static_cast<char>(CanScaling::encodeTemp(oil));
    return QCanBusFrame(CanScaling::kFrameTemp, payload);
}

QCanBusFrame MockCanProvider::speedFrame(int speed)
{
    QByteArray payload(8, 0x00);
    qToBigEndian<quint16>(CanScaling::encodeSpeed(speed), payload.data());
    return QCanBusFrame(CanScaling::kFrameSpeed, payload);
}

QCanBusFrame MockCanProvider::gearFrame(int gear)
{
    QByteArray payload(8, 0x00);
    payload[0] = static_cast<char>(CanScaling::encodeGear(gear));
    return QCanBusFrame(CanScaling::kFrameGear, payload);
}
