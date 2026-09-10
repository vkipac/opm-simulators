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

#define BOOST_TEST_MODULE OPM_test_FluxDumper

#include <boost/test/unit_test.hpp>

#include <opm/io/eclipse/FluxFile.hpp>
#include <opm/simulators/flow/flux/FluxDumper.hpp>
#include <opm/simulators/flow/flux/FluxRegions.hpp>

#include <array>
#include <stdexcept>
#include <vector>

namespace {

int globalIndex(const std::array<int, 3>& dims, const int i, const int j, const int k)
{
    return Opm::FluxRegions::cartesianIndex(dims, i - 1, j - 1, k - 1);
}

} // namespace

BOOST_AUTO_TEST_CASE(BuildsFluxFileMetadataFromRegion)
{
    const std::array<int, 3> dims{4, 1, 1};
    std::vector<int> regionValues(dims[0] * dims[1] * dims[2], 0);
    regionValues[globalIndex(dims, 2, 1, 1)] = 3;
    regionValues[globalIndex(dims, 3, 1, 1)] = 3;

    const auto regions = Opm::FluxRegions::extract(dims, regionValues);
    BOOST_REQUIRE_EQUAL(regions.size(), 1U);

    const std::vector<double> trans{11.0, 22.0};
    Opm::FluxDumper dumper("PARENT", 3, dims, regions.front(),
                           Opm::EclIO::FluxFile::Mode::Both,
                           Opm::EclIO::FluxFile::Sampling::Averaged,
                           static_cast<int>(Opm::EclIO::FluxFile::Phase::Oil)
                             | static_cast<int>(Opm::EclIO::FluxFile::Phase::Water),
                           trans);

    const auto& data = dumper.data();
    BOOST_CHECK_EQUAL(data.header.parentNx, 4);
    BOOST_CHECK_EQUAL(data.header.boxI1, 2);
    BOOST_CHECK_EQUAL(data.header.boxNx, 2);
    BOOST_CHECK_EQUAL(data.header.numBoundaryFaces, 2);
    BOOST_CHECK_EQUAL(data.header.numPhases, 2);
    BOOST_CHECK_EQUAL(data.localToGlobal.size(), 2U);
    BOOST_CHECK_EQUAL(data.boundaryFaces[0].transmissibility, 11.0);
    BOOST_CHECK_EQUAL(data.boundaryFaces[1].transmissibility, 22.0);
}

BOOST_AUTO_TEST_CASE(ValidatesReportStepVectorSizes)
{
    const std::array<int, 3> dims{2, 1, 1};
    std::vector<int> regionValues(dims[0] * dims[1] * dims[2], 0);
    regionValues[globalIndex(dims, 1, 1, 1)] = 1;

    const auto regions = Opm::FluxRegions::extract(dims, regionValues);
    BOOST_REQUIRE_EQUAL(regions.size(), 1U);

    Opm::FluxDumper dumper("PARENT", 1, dims, regions.front(),
                           Opm::EclIO::FluxFile::Mode::Both,
                           Opm::EclIO::FluxFile::Sampling::Instant,
                           static_cast<int>(Opm::EclIO::FluxFile::Phase::Oil));

    Opm::FluxDumper::ReportStepData good;
    good.reportStep = 1;
    good.simStep = 3;
    good.startTime = 10.0;
    good.stepLength = 2.0;
    good.rates = {1.0};
    good.pressures = {100.0};
    good.swat = {0.2};
    good.sgas = {0.1};
    good.rs = {20.0};
    good.rv = {0.5};
    good.temperature = {340.0};

    BOOST_CHECK_NO_THROW(dumper.appendReportStep(good));
    BOOST_CHECK_EQUAL(dumper.data().header.numReportSteps, 1);
    BOOST_CHECK(dumper.data().header.hasTemperature);

    Opm::FluxDumper::ReportStepData bad = good;
    bad.rates = {1.0, 2.0};
    BOOST_CHECK_THROW(dumper.appendReportStep(bad), std::invalid_argument);
}
