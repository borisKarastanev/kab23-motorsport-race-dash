#pragma once

#include <QtEndian>
#include <QtGlobal>

// Encode/decode pairs for each PT-CAN frame. Both MockCanProvider and
// CanDataModel use these so a single edit covers both sides of the wire.
namespace CanScaling {

constexpr quint32 kFrameRpm   = 0x316;
constexpr quint32 kFrameTemp  = 0x329;
constexpr quint32 kFrameSpeed = 0x1A0; // provisional — verify via candump on real car

inline quint16 encodeRpm(int rpm)       { return static_cast<quint16>(rpm  * 6.42); }
inline int     decodeRpm(quint16 raw)   { return static_cast<int>(raw / 6.42); }

// 0x329 byte formula: 0.75 * byte - 48.373 °C
inline quint8  encodeTemp(double c)     { return static_cast<quint8>((c + 48.373) / 0.75); }
inline double  decodeTemp(quint8 b)     { return 0.75 * b - 48.373; }

inline quint16 encodeSpeed(int kmh)     { return static_cast<quint16>(kmh * 100); }
inline int     decodeSpeed(quint16 raw) { return static_cast<int>(raw / 100); }

} // namespace CanScaling
