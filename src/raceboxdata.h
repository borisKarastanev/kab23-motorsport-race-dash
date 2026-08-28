#pragma once

#include <QtGlobal>

// Decoded fields from a RaceBox 0xFF/0x01 UBX data packet (80-byte payload, little-endian).
// All conversions to display units happen in RaceBoxModel, not here.
// Single source of truth for the mm/s ↔ km/h conversion used by both
// MockRaceBoxProvider (encoder) and RaceBoxModel (decoder).
static constexpr double kMmSPerKmh = 277.778;

// The car is treated as stationary at or below this ground speed (km/h): GPS
// ground speed jitters by a km/h or two even while genuinely parked, especially
// without a satellite lock. One shared threshold so the displayed-speed noise
// floor and lap-crossing rejection (RaceBoxModel) and the nearby-track "parked"
// gate (TrackModel) can't drift apart when it's tuned.
static constexpr int kStationarySpeedKmh = 3;

struct RaceBoxData {
    quint8  fixStatus;   // 0=no fix, 2=2D, 3=3D
    quint8  fixFlags;    // bit 0 = valid fix
    quint8  numSvs;      // satellites used in solution

    // UBX-NAV-PVT time block (bytes 4-11 of the payload). bit 0 of
    // gnssValidFlags = validDate, bit 1 = validTime, bit 2 = fullyResolved —
    // RaceBoxModel accepts the reading only once bit 2 is set, alongside fixStatus/fixFlags.
    quint16 gnssYear;
    quint8  gnssMonth, gnssDay, gnssHour, gnssMinute, gnssSecond;
    quint8  gnssValidFlags;

    double  latitude;    // degrees (Int32 × 10^-7)
    double  longitude;   // degrees (Int32 × 10^-7)

    qint32  speedMmS;    // mm/s — divide by kMmSPerKmh for km/h

    qint16  gForceXMg;   // milli-g X (lateral)
    qint16  gForceYMg;   // milli-g Y (longitudinal)
    qint16  gForceZMg;   // milli-g Z (vertical)

    quint8  batteryRaw;  // Mini/Mini S: lower 7 bits = %, MSB = charging
};
