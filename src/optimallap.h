#pragma once

#include <QList>
#include <QtGlobal>
#include <optional>

// Free-function implementation of the shared optimal-lap rule
// (~/development/optimal-lap-algorithm.md), written natively for the dash: no
// QObject, no Qt UI types, plain containers only, so it is testable without a
// running model — in the style of canscaling.h. Written against the
// specification, not transcribed from the cloud's optimal-lap-core.ts; the two
// are expected to look different.
namespace OptimalLap {

// One lap's per-sector times, as recorded by RaceBoxModel's gate detector.
// sectorMs is either exactly sectorCount long (every gate crossed) or empty
// (one was missed) — RaceBoxModel::lapSectorsCompleted never emits a partial
// list, and eligibility below relies on that.
struct SectoredLap {
    int           lapNumber = 0;
    QList<qint64> sectorMs;
};

// One sector's winning time and the lap it came from.
struct BestSector {
    int    sector    = 0;
    int    lapNumber = 0;
    qint64 sectorMs  = 0;
};

struct Result {
    qint64             lapMs = 0;
    QList<BestSector>  sectors;
    // Set when every sector came from one lap — the optimal *is* that lap.
    // -1 otherwise.
    int matchesLapNumber = -1;
};

// Per the shared rule: for each sector position, the lowest time any eligible
// lap set for it, ties to the lower lap number, summed for the total. A lap is
// eligible only with a complete set of positive sector times (see
// SectoredLap::sectorMs above). Returns std::nullopt with fewer than two
// eligible laps.
//
// sectorCount is a required parameter, not a constant with a convenient
// default: three is what the dash splits a lap into today, but a track with
// surveyed splits may have four, and a wrong count fails *silently* — every lap
// misses the eligibility test and the optimal lap just disappears. Callers must
// state the count the laps were actually measured at (SessionModel reads it off
// the gates in force for the session).
std::optional<Result> compute(const QList<SectoredLap> &laps, int sectorCount);

}
