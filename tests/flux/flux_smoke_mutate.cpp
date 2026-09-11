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
#include <limits>
#include <string>

namespace {

int fail(const std::string& msg)
{
    std::cerr << "flux_smoke_mutate: " << msg << '\n';
    return EXIT_FAILURE;
}

bool hasFluxMode(const Opm::EclIO::FluxFile::Mode mode)
{
    return (static_cast<int>(mode) & static_cast<int>(Opm::EclIO::FluxFile::Mode::Flux)) != 0;
}

double mutationValue(const std::string& mutation)
{
    if (mutation == "nan") {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (mutation == "inf") {
        return std::numeric_limits<double>::infinity();
    }
    if (mutation == "ninf") {
        return -std::numeric_limits<double>::infinity();
    }

    throw std::invalid_argument("unknown mutation '" + mutation + "' (expected nan|inf|ninf)");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 4) {
        return fail("usage: flux_smoke_mutate <input.FLUX> <output.FLUX> <nan|inf|ninf>");
    }

    const std::string inputPath = argv[1];
    const std::string outputPath = argv[2];
    const std::string mutation = argv[3];

    auto data = Opm::EclIO::FluxFile::read(inputPath);

    if (!hasFluxMode(data.header.mode)) {
        return fail("input file does not contain FLUX mode rates");
    }

    if (data.reportSteps.empty()) {
        return fail("input file has no report steps");
    }

    if (data.reportSteps.front().rates.empty()) {
        return fail("input file has empty FLXRATE payload");
    }

    try {
        data.reportSteps.front().rates.front() = mutationValue(mutation);
    }
    catch (const std::exception& e) {
        return fail(e.what());
    }

    Opm::EclIO::FluxFile::write(outputPath, /*formatted=*/false, data);
    return EXIT_SUCCESS;
}
