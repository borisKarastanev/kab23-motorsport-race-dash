#include "src/race/optimallap.h"

#include <algorithm>
#include <tuple>

namespace OptimalLap {

std::optional<Result> compute(const QList<SectoredLap> &laps, int sectorCount)
{
    if (sectorCount <= 0) return std::nullopt;

    QList<const SectoredLap *> eligible;
    for (const SectoredLap &lap : laps) {
        if (lap.sectorMs.size() != sectorCount) continue;
        const bool allPositive = std::all_of(lap.sectorMs.cbegin(), lap.sectorMs.cend(),
                                             [](qint64 ms) { return ms > 0; });
        if (allPositive) eligible.append(&lap);
    }

    if (eligible.size() < 2) return std::nullopt;

    Result result;
    result.sectors.reserve(sectorCount);
    for (int s = 0; s < sectorCount; ++s) {
        // "The lap in eligible minimising (lap.sectors[i], lap.number)", straight
        // from the shared rule: the tie-break on lap number rides in the
        // comparator, so the result is deterministic regardless of arrival order.
        const SectoredLap *winner =
            *std::min_element(eligible.cbegin(), eligible.cend(),
                              [s](const SectoredLap *a, const SectoredLap *b) {
                                  return std::tie(a->sectorMs[s], a->lapNumber)
                                       < std::tie(b->sectorMs[s], b->lapNumber);
                              });
        result.sectors.append({s, winner->lapNumber, winner->sectorMs[s]});
        result.lapMs += winner->sectorMs[s];
    }

    const int firstLap = result.sectors.first().lapNumber;
    const bool sameLap = std::all_of(result.sectors.cbegin(), result.sectors.cend(),
                                     [firstLap](const BestSector &sector) {
                                         return sector.lapNumber == firstLap;
                                     });
    result.matchesLapNumber = sameLap ? firstLap : -1;

    return result;
}

}
