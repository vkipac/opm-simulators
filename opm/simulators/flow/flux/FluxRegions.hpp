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

#ifndef OPM_FLUX_REGIONS_HPP
#define OPM_FLUX_REGIONS_HPP

#include <opm/input/eclipse/EclipseState/Grid/FaceDir.hpp>

#include <array>
#include <vector>

namespace Opm {

class FluxRegions
{
public:
    struct BoundaryFace {
        int interiorLocalCell = -1;
        int interiorGlobalCell = -1;
        int exteriorGlobalCell = -1;
        FaceDir::DirEnum direction = FaceDir::Unknown;
        bool isNnc = false;

        bool operator==(const BoundaryFace& other) const = default;
    };

    struct Box {
        int i1 = 0;
        int i2 = 0;
        int j1 = 0;
        int j2 = 0;
        int k1 = 0;
        int k2 = 0;

        int nx() const { return i2 - i1 + 1; }
        int ny() const { return j2 - j1 + 1; }
        int nz() const { return k2 - k1 + 1; }

        bool operator==(const Box& other) const = default;
    };

    struct Region {
        int regionId = 0;
        Box box;
        std::vector<int> localToGlobal;
        std::vector<int> selectedGlobalCells;
        std::vector<BoundaryFace> boundaryFaces;

        bool operator==(const Region& other) const = default;
    };

    static std::vector<Region> extract(const std::array<int, 3>& dims,
                                       const std::vector<int>& regionValues);

    static std::vector<Region> extract(const std::array<int, 3>& dims,
                                       const std::vector<int>& regionValues,
                                       const std::vector<std::array<int, 2>>& nncConnections);

    static int uniqueSelectedRegion(const std::vector<int>& regionValues);

    static std::vector<int> buildActnum(const std::vector<int>& regionValues,
                                        int regionId);

    static int cartesianIndex(const std::array<int, 3>& dims,
                              int i,
                              int j,
                              int k);

private:
    static int localBoxIndex(const Box& box, int i, int j, int k);
};

} // namespace Opm

#endif // OPM_FLUX_REGIONS_HPP