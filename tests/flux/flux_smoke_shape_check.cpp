/*
   Copyright 2026 Equinor ASA.

   This file is part of the Open Porous Media project (OPM).

   OPM is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   OPM is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <opm/io/eclipse/FluxFile.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int fail(const std::string& msg)
{
    std::cerr << "flux_smoke_shape_check: " << msg << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        return fail("usage: flux_smoke_shape_check <file.FLUX>");
    }

    const auto data = Opm::EclIO::FluxFile::read(argv[1]);
    const auto& h = data.header;

    if (h.parentNx != 10 || h.parentNy != 1 || h.parentNz != 10) {
        return fail("unexpected parent dimensions");
    }

    if (h.boxI1 != 10 || h.boxJ1 != 1 || h.boxK1 != 5) {
        return fail("unexpected FLUXREG box start");
    }

    if (h.boxNx != 1 || h.boxNy != 1 || h.boxNz != 1) {
        return fail("unexpected FLUXREG box dimensions");
    }

    if (h.numCells != 1 || data.localToGlobal.size() != 1U) {
        return fail("unexpected cell mapping size");
    }

    if (h.numBoundaryFaces <= 0) {
        return fail("missing boundary faces");
    }

    if (data.boundaryFaces.size() != static_cast<std::size_t>(h.numBoundaryFaces)) {
        return fail("boundary face count mismatch");
    }

    if (h.numReportSteps <= 0) {
        return fail("missing report steps");
    }

    if (data.reportSteps.size() != static_cast<std::size_t>(h.numReportSteps)) {
        return fail("report step count mismatch");
    }

    const std::size_t expectedRates = static_cast<std::size_t>(h.numBoundaryFaces)
                                    * static_cast<std::size_t>(h.numPhases);

    for (const auto& step : data.reportSteps) {
        if (step.rates.size() != expectedRates) {
            return fail("unexpected FLXRATE payload size");
        }
    }

    return EXIT_SUCCESS;
}
