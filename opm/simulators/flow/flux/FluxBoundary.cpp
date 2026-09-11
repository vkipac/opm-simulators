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

#include <opm/simulators/flow/flux/FluxBoundary.hpp>

#include <opm/common/ErrorMacros.hpp>

#include <stdexcept>

namespace Opm {

std::vector<int> FluxBoundary::buildLocalToActive(const std::vector<int>& localToGlobal)
{
    std::vector<int> localToActive(localToGlobal.size(), -1);
    int activeIndex = 0;
    for (std::size_t localIndex = 0; localIndex < localToGlobal.size(); ++localIndex) {
        if (localToGlobal[localIndex] >= 0) {
            localToActive[localIndex] = activeIndex;
            ++activeIndex;
        }
    }

    return localToActive;
}

FluxBoundary FluxBoundary::fromData(const EclIO::FluxFile::Data& data,
                                    const std::vector<int>& localToActive)
{
    if (localToActive.size() != data.localToGlobal.size()) {
        OPM_THROW(std::invalid_argument,
                  "FluxBoundary: localToActive size must match FLUX localToGlobal size");
    }

    FluxBoundary boundary;
    boundary.data_ = data;
    boundary.faces_.reserve(data.boundaryFaces.size());

    for (const auto& face : data.boundaryFaces) {
        if (face.interiorLocalCell < 0
            || static_cast<std::size_t>(face.interiorLocalCell) >= localToActive.size()) {
            OPM_THROW(std::invalid_argument,
                      "FluxBoundary: boundary face references invalid interior local cell");
        }

        const auto interiorActive = localToActive[face.interiorLocalCell];
        if (interiorActive < 0) {
            OPM_THROW(std::invalid_argument,
                      "FluxBoundary: boundary face maps to inactive interior local cell");
        }

        const auto interiorGlobal = data.localToGlobal[face.interiorLocalCell];
        if (interiorGlobal < 0) {
            OPM_THROW(std::invalid_argument,
                      "FluxBoundary: localToGlobal missing interior global cell for boundary face");
        }

        boundary.faces_.push_back(Face{
            interiorActive,
            face.interiorLocalCell,
            interiorGlobal,
            face.exteriorGlobalCell,
            static_cast<FaceDir::DirEnum>(face.direction),
            face.direction == static_cast<int>(FaceDir::Unknown),
            face.transmissibility,
        });
    }

    return boundary;
}

FluxBoundary FluxBoundary::load(const std::string& filename,
                                const std::vector<int>& localToActive)
{
    return fromData(EclIO::FluxFile::read(filename), localToActive);
}

std::array<std::vector<int>, 6> FluxBoundary::buildDirectionalFaceIndices(const std::size_t numActiveCells) const
{
    std::array<std::vector<int>, 6> directionalFaceIndices;
    for (auto& values : directionalFaceIndices) {
        values.assign(numActiveCells, 0);
    }

    const auto dirToIndex = [](const FaceDir::DirEnum dir)
    {
        int idx = 0;
        int div = static_cast<int>(dir);
        while ((div /= 2) >= 1) {
            ++idx;
        }
        return idx;
    };

    for (std::size_t faceIndex = 0; faceIndex < this->faces_.size(); ++faceIndex) {
        const auto& face = this->faces_[faceIndex];
        if (face.isNnc || face.direction == FaceDir::Unknown) {
            continue;
        }
        if (face.interiorActiveCell < 0 || static_cast<std::size_t>(face.interiorActiveCell) >= numActiveCells) {
            OPM_THROW(std::invalid_argument,
                      "FluxBoundary: face references invalid active cell for directional mapping");
        }

        auto& slot = directionalFaceIndices[dirToIndex(face.direction)][face.interiorActiveCell];
        if (slot != 0) {
            OPM_THROW(std::invalid_argument,
                      "FluxBoundary: duplicate FLUX boundary face for active cell and direction");
        }

        slot = static_cast<int>(faceIndex) + 1;
    }

    return directionalFaceIndices;
}

const EclIO::FluxFile::Data& FluxBoundary::data() const
{
    return this->data_;
}

const std::vector<FluxBoundary::Face>& FluxBoundary::faces() const
{
    return this->faces_;
}

} // namespace Opm
