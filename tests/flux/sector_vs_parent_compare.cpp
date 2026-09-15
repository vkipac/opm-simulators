/*
   Copyright 2026 Equinor ASA.
*/

#include <opm/io/eclipse/ERst.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int fail(const std::string& message)
{
    std::cerr << "sector_vs_parent_compare: " << message << '\n';
    return EXIT_FAILURE;
}

bool closeEnough(const double lhs, const double rhs, const double tol)
{
    const auto scale = std::max({1.0, std::abs(lhs), std::abs(rhs)});
    return std::abs(lhs - rhs) <= tol * scale;
}

std::vector<int> loadMapping(const std::string& path)
{
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open mapping file '" + path + "'");
    }

    std::vector<int> mapping;
    int value = 0;
    while (input >> value) {
        mapping.push_back(value);
    }

    if (mapping.empty()) {
        throw std::runtime_error("mapping file is empty: '" + path + "'");
    }

    return mapping;
}

std::vector<double> loadStepArray(Opm::EclIO::ERst& rst,
                                  const std::string& keyword,
                                  const int reportStep)
{
    if (!rst.hasArray(keyword, reportStep)) {
        throw std::runtime_error("restart step " + std::to_string(reportStep)
                                 + " missing required array '" + keyword + "'");
    }

    const auto& values = rst.getRestartData<float>(keyword, reportStep);
    return {values.begin(), values.end()};
}

bool compareMappedState(const std::vector<double>& parent,
                        const std::vector<double>& sector,
                        const std::vector<int>& mapping,
                        const std::string& name,
                        const double tol)
{
    if (sector.size() != mapping.size()) {
        std::cerr << name << ": sector size " << sector.size()
                  << " differs from mapping size " << mapping.size() << '\n';
        return false;
    }

    for (std::size_t i = 0; i < mapping.size(); ++i) {
        const auto parentIdx = mapping[i];
        if (parentIdx < 0 || static_cast<std::size_t>(parentIdx) >= parent.size()) {
            std::cerr << name << ": mapping index out of range at local cell " << i
                      << " -> " << parentIdx << '\n';
            return false;
        }

        if (!closeEnough(parent[parentIdx], sector[i], tol)) {
            std::cerr << name << ": mismatch at local cell " << i
                      << " (parent global " << parentIdx << "): "
                      << parent[parentIdx] << " vs " << sector[i] << '\n';
            return false;
        }
    }

    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 4 || argc > 6) {
        return fail("usage: sector_vs_parent_compare <parent.UNRST> <sector.UNRST> <map.txt> [pressureTol] [swatTol] [sgasTol]");
    }

    const double pressureTol = (argc > 4) ? std::stod(argv[4]) : 5e-2;
    const double swatTol = (argc > 5) ? std::stod(argv[5]) : 5e-3;
    const double sgasTol = (argc > 6) ? std::stod(argv[6]) : swatTol;

    try {
        Opm::EclIO::ERst parentRst(argv[1]);
        Opm::EclIO::ERst sectorRst(argv[2]);
        const auto mapping = loadMapping(argv[3]);

        const auto parentSteps = parentRst.listOfReportStepNumbers();
        const auto sectorSteps = sectorRst.listOfReportStepNumbers();
        if (parentSteps.empty() || sectorSteps.empty()) {
            return fail("missing report steps in restart files");
        }

        const int parentStep = parentSteps.back();
        const int sectorStep = sectorSteps.back();

        const auto parentPressure = loadStepArray(parentRst, "PRESSURE", parentStep);
        const auto parentSwat = loadStepArray(parentRst, "SWAT", parentStep);
        const auto parentSgas = loadStepArray(parentRst, "SGAS", parentStep);

        const auto sectorPressure = loadStepArray(sectorRst, "PRESSURE", sectorStep);
        const auto sectorSwat = loadStepArray(sectorRst, "SWAT", sectorStep);
        const auto sectorSgas = loadStepArray(sectorRst, "SGAS", sectorStep);

        if (!compareMappedState(parentPressure, sectorPressure, mapping, "PRESSURE", pressureTol)
            || !compareMappedState(parentSwat, sectorSwat, mapping, "SWAT", swatTol)
            || !compareMappedState(parentSgas, sectorSgas, mapping, "SGAS", sgasTol)) {
            return fail("mapped cell-state comparison failed");
        }

        return EXIT_SUCCESS;
    }
    catch (const std::exception& e) {
        return fail(e.what());
    }
}
