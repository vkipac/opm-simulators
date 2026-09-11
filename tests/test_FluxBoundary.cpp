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

#include <config.h>

#define BOOST_TEST_MODULE OPM_test_FluxBoundary

#include <boost/test/unit_test.hpp>

#include <opm/io/eclipse/FluxFile.hpp>
#include <opm/simulators/flow/flux/FluxBoundary.hpp>
#include <opm/simulators/flow/flux/FluxDumper.hpp>
#include <opm/simulators/flow/flux/FluxRegions.hpp>

#include <array>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace {

int globalIndex(const std::array<int, 3>& dims, const int i, const int j, const int k)
{
    return Opm::FluxRegions::cartesianIndex(dims, i - 1, j - 1, k - 1);
}

} // namespace

BOOST_AUTO_TEST_CASE(BuildsContiguousLocalToActiveMap)
{
    const std::vector<int> localToGlobal{10, -1, 12, 17, -1};
    const auto localToActive = Opm::FluxBoundary::buildLocalToActive(localToGlobal);

    BOOST_REQUIRE_EQUAL(localToActive.size(), 5U);
    BOOST_CHECK_EQUAL(localToActive[0], 0);
    BOOST_CHECK_EQUAL(localToActive[1], -1);
    BOOST_CHECK_EQUAL(localToActive[2], 1);
    BOOST_CHECK_EQUAL(localToActive[3], 2);
    BOOST_CHECK_EQUAL(localToActive[4], -1);
}

BOOST_AUTO_TEST_CASE(MapsBoundaryFacesFromFluxFileData)
{
    const std::array<int, 3> dims{4, 1, 1};
    std::vector<int> regionValues(dims[0] * dims[1] * dims[2], 0);
    regionValues[globalIndex(dims, 2, 1, 1)] = 3;
    regionValues[globalIndex(dims, 3, 1, 1)] = 3;

    const auto regions = Opm::FluxRegions::extract(dims, regionValues);
    BOOST_REQUIRE_EQUAL(regions.size(), 1U);

    Opm::FluxDumper dumper("PARENT", 3, dims, regions.front(),
                           Opm::EclIO::FluxFile::Mode::Flux,
                           Opm::EclIO::FluxFile::Sampling::Instant,
                           static_cast<int>(Opm::EclIO::FluxFile::Phase::Oil));

    Opm::FluxDumper::ReportStepData step;
    step.reportStep = 0;
    step.simStep = 0;
    step.startTime = 0.0;
    step.stepLength = 1.0;
    step.rates = {1.0, 2.0};
    dumper.appendReportStep(step);

    const auto localToActive = Opm::FluxBoundary::buildLocalToActive(regions.front().localToGlobal);
    const auto boundary = Opm::FluxBoundary::fromData(dumper.data(), localToActive);

    BOOST_REQUIRE_EQUAL(boundary.faces().size(), 2U);
    BOOST_CHECK_EQUAL(boundary.faces()[0].interiorActiveCell, 0);
    BOOST_CHECK_EQUAL(boundary.faces()[0].interiorLocalCell, 0);
    BOOST_CHECK_EQUAL(boundary.faces()[0].interiorGlobalCell, globalIndex(dims, 2, 1, 1));
    BOOST_CHECK_EQUAL(boundary.faces()[0].direction, Opm::FaceDir::XMinus);
    BOOST_CHECK(!boundary.faces()[0].isNnc);

    BOOST_CHECK_EQUAL(boundary.faces()[1].interiorActiveCell, 1);
    BOOST_CHECK_EQUAL(boundary.faces()[1].interiorLocalCell, 1);
    BOOST_CHECK_EQUAL(boundary.faces()[1].interiorGlobalCell, globalIndex(dims, 3, 1, 1));
    BOOST_CHECK_EQUAL(boundary.faces()[1].direction, Opm::FaceDir::XPlus);
}

BOOST_AUTO_TEST_CASE(LoadsFluxFileAndPreservesNncFaces)
{
    const std::array<int, 3> dims{3, 1, 1};
    std::vector<int> regionValues(dims[0] * dims[1] * dims[2], 0);
    regionValues[globalIndex(dims, 1, 1, 1)] = 8;

    const std::vector<std::array<int, 2>> nnc{{globalIndex(dims, 1, 1, 1), globalIndex(dims, 3, 1, 1)}};
    const auto regions = Opm::FluxRegions::extract(dims, regionValues, nnc);
    BOOST_REQUIRE_EQUAL(regions.size(), 1U);

    Opm::FluxDumper dumper("PARENT", 8, dims, regions.front(),
                           Opm::EclIO::FluxFile::Mode::Flux,
                           Opm::EclIO::FluxFile::Sampling::Instant,
                           static_cast<int>(Opm::EclIO::FluxFile::Phase::Oil));

    Opm::FluxDumper::ReportStepData step;
    step.reportStep = 0;
    step.simStep = 0;
    step.startTime = 0.0;
    step.stepLength = 1.0;
    step.rates = {11.0, 99.0};
    dumper.appendReportStep(step);

    const auto outPath = std::filesystem::path{"test_fluxboundary_roundtrip.FLUX"};
    dumper.write(outPath.string(), false);

    const auto localToActive = Opm::FluxBoundary::buildLocalToActive(regions.front().localToGlobal);
    const auto boundary = Opm::FluxBoundary::load(outPath.string(), localToActive);

    BOOST_REQUIRE_EQUAL(boundary.faces().size(), 2U);
    BOOST_CHECK(!boundary.faces()[0].isNnc);
    BOOST_CHECK(boundary.faces()[1].isNnc);
    BOOST_CHECK_EQUAL(boundary.faces()[1].direction, Opm::FaceDir::Unknown);
    BOOST_CHECK_EQUAL(boundary.faces()[1].exteriorGlobalCell, globalIndex(dims, 3, 1, 1));

    std::filesystem::remove(outPath);
}

BOOST_AUTO_TEST_CASE(RejectsMismatchedLocalToActiveSize)
{
    Opm::EclIO::FluxFile::Data data;
    data.localToGlobal = {10};
    data.boundaryFaces = {{0, static_cast<int>(Opm::FaceDir::XMinus), 9, 0.0}};

    BOOST_CHECK_THROW(Opm::FluxBoundary::fromData(data, {}), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(RejectsInactiveBoundaryInteriorCell)
{
    Opm::EclIO::FluxFile::Data data;
    data.localToGlobal = {10};
    data.boundaryFaces = {{0, static_cast<int>(Opm::FaceDir::XMinus), 9, 0.0}};

    BOOST_CHECK_THROW(Opm::FluxBoundary::fromData(data, {-1}), std::invalid_argument);
}
