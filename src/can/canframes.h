#pragma once

#include "src/core/canscaling.h"
#include <QByteArray>
#include <QCanBusFrame>

// Builders for complete PT-CAN frames, encoded via CanScaling. Shared by
// MockCanProvider and the unit tests so a frame layout lives in one place.
namespace CanFrames {

inline QCanBusFrame rpm(int rpm)
{
    QByteArray payload(8, 0x00);
    CanScaling::encodeRpm(rpm, payload.data() + CanScaling::kOffsetRpm);
    return QCanBusFrame(CanScaling::kFrameRpm, payload);
}

inline QCanBusFrame coolant(double celsius)
{
    QByteArray payload(8, 0x00);
    payload[CanScaling::kOffsetCoolant] = static_cast<char>(CanScaling::encodeCoolant(celsius));
    return QCanBusFrame(CanScaling::kFrameTemp, payload);
}

inline QCanBusFrame dme4(double oilTemp)
{
    QByteArray payload(8, 0x00);
    payload[CanScaling::kOffsetOilTemp] = static_cast<char>(CanScaling::encodeOilTemp(oilTemp));
    return QCanBusFrame(CanScaling::kFrameDme4, payload);
}

inline QCanBusFrame wheelSpeeds(double fl, double fr, double rl, double rr)
{
    QByteArray payload(8, 0x00);
    CanScaling::encodeWheelSpeed(fl, payload.data() + CanScaling::kOffsetWheelFL);
    CanScaling::encodeWheelSpeed(fr, payload.data() + CanScaling::kOffsetWheelFR);
    CanScaling::encodeWheelSpeed(rl, payload.data() + CanScaling::kOffsetWheelRL);
    CanScaling::encodeWheelSpeed(rr, payload.data() + CanScaling::kOffsetWheelRR);
    return QCanBusFrame(CanScaling::kFrameWheelSpeed, payload);
}

inline QCanBusFrame steering(double deg)
{
    QByteArray payload(8, 0x00);
    CanScaling::encodeSteering(deg, payload.data() + CanScaling::kOffsetSteering);
    return QCanBusFrame(CanScaling::kFrameSteering, payload);
}

// Raw byte 0 rather than a bool, so tests can set the other bits too.
inline QCanBusFrame asc1(quint8 byte0)
{
    QByteArray payload(8, 0x00);
    payload[CanScaling::kOffsetBrake] = static_cast<char>(byte0);
    return QCanBusFrame(CanScaling::kFrameAsc1, payload);
}

} // namespace CanFrames
