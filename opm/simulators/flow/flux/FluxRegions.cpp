/*
  Copyright 2026 Equinor ASA

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <opm/simulators/flow/flux/FluxRegions.hpp>

#include <opm/common/ErrorMacros.hpp>

#include <algorithm>
#include <map>

namespace Opm {

int FluxRegions::cartesianIndex(const std::array<int, 3>& dims,
                                const int i,
                                const int j,
                                const int k)
{
    return i + dims[0] * (j + dims[1] * k);
}

int FluxRegions::localBoxIndex(const Box& box, const int i, const int j, const int k)
{
    return (i - box.i1) + box.nx() * ((j - box.j1) + box.ny() * (k - box.k1));
}

std::vector<FluxRegions::Region> FluxRegions::extract(const std::array<int, 3>& dims,
                                                      const std::vector<int>& regionValues)
{
    const auto numCells = dims[0] * dims[1] * dims[2];
    if (static_cast<int>(regionValues.size()) != numCells) {
        OPM_THROW(std::invalid_argument,
                  "FluxRegions::extract(): regionValues size must match grid dimensions");
    }

    struct PendingRegion {
        Box box;
        std::vector<int> globalCells;
    };

    std::map<int, PendingRegion> pending;

    for (int k = 0; k < dims[2]; ++k) {
        for (int j = 0; j < dims[1]; ++j) {
            for (int i = 0; i < dims[0]; ++i) {
                const auto globalIndex = cartesianIndex(dims, i, j, k);
                const auto regionId = regionValues[globalIndex];
                if (regionId <= 0) {
                    continue;
                }

                auto& region = pending[regionId];
                if (region.globalCells.empty()) {
                    region.box = Box{dims[0] + 1, 0, dims[1] + 1, 0, dims[2] + 1, 0};
                }
                region.box.i1 = std::min(region.box.i1, i + 1);
                region.box.i2 = std::max(region.box.i2, i + 1);
                region.box.j1 = std::min(region.box.j1, j + 1);
                region.box.j2 = std::max(region.box.j2, j + 1);
                region.box.k1 = std::min(region.box.k1, k + 1);
                region.box.k2 = std::max(region.box.k2, k + 1);
                region.globalCells.push_back(globalIndex);
            }
        }
    }

    std::vector<Region> regions;
    regions.reserve(pending.size());

    for (auto& [regionId, pendingRegion] : pending) {
        Region region;
        region.regionId = regionId;
        region.box = pendingRegion.box;
        region.selectedGlobalCells = std::move(pendingRegion.globalCells);

        region.localToGlobal.assign(region.box.nx() * region.box.ny() * region.box.nz(), -1);
        for (const auto globalIndex : region.selectedGlobalCells) {
            const int k = globalIndex / (dims[0] * dims[1]);
            const int rem = globalIndex % (dims[0] * dims[1]);
            const int j = rem / dims[0];
            const int i = rem % dims[0];
            region.localToGlobal[localBoxIndex(region.box, i + 1, j + 1, k + 1)] = globalIndex;
        }

        regions.push_back(std::move(region));
    }

    return regions;
}

} // namespace Opm