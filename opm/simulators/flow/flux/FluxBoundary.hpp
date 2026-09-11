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

#ifndef OPM_FLUX_BOUNDARY_HPP
#define OPM_FLUX_BOUNDARY_HPP

#include <opm/input/eclipse/EclipseState/Grid/FaceDir.hpp>
#include <opm/io/eclipse/FluxFile.hpp>

#include <array>
#include <string>
#include <vector>

namespace Opm {

class FluxBoundary
{
public:
    struct Face {
        int interiorActiveCell = -1;
        int interiorLocalCell = -1;
        int interiorGlobalCell = -1;
        int exteriorGlobalCell = -1;
        FaceDir::DirEnum direction = FaceDir::Unknown;
        bool isNnc = false;
        double transmissibility = 0.0;

        bool operator==(const Face& other) const = default;
    };

    static std::vector<int> buildLocalToActive(const std::vector<int>& localToGlobal);

    static FluxBoundary fromData(const EclIO::FluxFile::Data& data,
                                 const std::vector<int>& localToActive);

    static FluxBoundary load(const std::string& filename,
                             const std::vector<int>& localToActive);

    static const EclIO::FluxFile::ReportStep* selectReportStep(const EclIO::FluxFile::Data& data,
                                                                int episodeIndex);

    std::array<std::vector<int>, 6> buildDirectionalFaceIndices(std::size_t numActiveCells) const;
    const Face* faceFromSlot(int slot) const;

    const EclIO::FluxFile::Data& data() const;
    const std::vector<Face>& faces() const;

private:
    EclIO::FluxFile::Data data_;
    std::vector<Face> faces_;
};

} // namespace Opm

#endif // OPM_FLUX_BOUNDARY_HPP
