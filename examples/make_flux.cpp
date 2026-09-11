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

#include "config.h"

#include <opm/common/OpmLog/OpmLog.hpp>
#include <opm/input/eclipse/Deck/Deck.hpp>
#include <opm/input/eclipse/EclipseState/EclipseState.hpp>
#include <opm/input/eclipse/Parser/ErrorGuard.hpp>
#include <opm/input/eclipse/Parser/ParseContext.hpp>
#include <opm/input/eclipse/Parser/Parser.hpp>
#include <opm/input/eclipse/Units/UnitSystem.hpp>
#include <opm/io/eclipse/EclFile.hpp>
#include <opm/io/eclipse/ERst.hpp>
#include <opm/io/eclipse/ESmry.hpp>
#include <opm/simulators/flow/flux/FluxDumper.hpp>
#include <opm/simulators/flow/flux/FluxRegions.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
constexpr double secondsPerDay = 24.0 * 60.0 * 60.0;

struct Options {
    std::string parent;
    std::string mapping;
    std::string output;
    std::string mode = "flux";
    std::string sampling = "averaged";
    std::string summary;
    bool help = false;
};

struct ParentInput {
    fs::path deckPath;
    fs::path rootPath;
    fs::path restartPath;
    std::optional<fs::path> initPath;
    std::optional<fs::path> summaryPath;
};

struct SummaryPayload {
    std::vector<std::string> keys;
    std::vector<std::vector<double>> valuesPerStep;
    std::vector<double> reportTimes;
};

using NncKey = std::pair<int, int>;

void printUsage()
{
    std::cout
        << "usage: make_flux --parent=<CASE|CASE.DATA> --mapping=<file> --output=<SECTOR.FLUX> [options]\n"
        << "\n"
        << "options:\n"
        << "  --mode=<flux|pressure|both>         Boundary payload mode (default: flux; flux requires FLORES restart arrays)\n"
        << "  --sampling=<averaged|instant>        Flux sampling mode (default: averaged)\n"
        << "  --summary=<CASE|CASE.SMSPEC>         Optional parent summary override\n"
        << "\n"
        << "mapping file:\n"
        << "  One selected parent cell per non-comment line, either as:\n"
        << "    <global-cartesian-index>           zero-based parent global cartesian index\n"
        << "    <i> <j> <k>                        one-based parent cartesian coordinates\n"
        << "  --help                               Show this message\n";
}

bool startsWith(const std::string& text, const std::string& prefix)
{
    return text.rfind(prefix, 0) == 0;
}

std::string valueAfterEquals(const std::string& arg)
{
    const auto pos = arg.find('=');
    if (pos == std::string::npos || pos + 1 >= arg.size()) {
        throw std::invalid_argument("expected --key=value form for argument '" + arg + "'");
    }
    return arg.substr(pos + 1);
}

Options parseOptions(int argc, char** argv)
{
    Options opt;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            opt.help = true;
        }
        else if (startsWith(arg, "--parent=")) {
            opt.parent = valueAfterEquals(arg);
        }
        else if (startsWith(arg, "--mapping=")) {
            opt.mapping = valueAfterEquals(arg);
        }
        else if (startsWith(arg, "--output=")) {
            opt.output = valueAfterEquals(arg);
        }
        else if (startsWith(arg, "--mode=")) {
            opt.mode = valueAfterEquals(arg);
        }
        else if (startsWith(arg, "--sampling=")) {
            opt.sampling = valueAfterEquals(arg);
        }
        else if (startsWith(arg, "--summary=")) {
            opt.summary = valueAfterEquals(arg);
        }
        else {
            throw std::invalid_argument("unrecognized argument '" + arg + "'");
        }
    }

    return opt;
}

bool validMode(const std::string& mode)
{
    return mode == "flux" || mode == "pressure" || mode == "both";
}

bool validSampling(const std::string& sampling)
{
    return sampling == "averaged" || sampling == "instant";
}

std::string toUpper(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return text;
}

bool hasKnownExtension(const fs::path& path)
{
    const auto upper = toUpper(path.extension().string());
    return upper == ".DATA" || upper == ".UNRST" || upper == ".INIT" || upper == ".SMSPEC" || upper == ".FSMSPEC";
}

fs::path rootPathFromArgument(const std::string& text)
{
    fs::path path{text};
    if (hasKnownExtension(path)) {
        return path.parent_path() / path.stem();
    }

    return path;
}

std::optional<fs::path> latestSeparateRestart(const fs::path& rootPath,
                                             const char restartPrefix)
{
    const auto directory = rootPath.parent_path().empty() ? fs::path{"."} : rootPath.parent_path();
    const auto stem = rootPath.filename().string() + ".";

    std::optional<fs::path> latest;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto name = entry.path().filename().string();
        if (!startsWith(name, stem)) {
            continue;
        }

        const auto suffix = name.substr(stem.size());
        if (suffix.size() != 5 || suffix[0] != restartPrefix) {
            continue;
        }

        const auto digits = suffix.substr(1);
        if (!std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); })) {
            continue;
        }

        if (!latest || entry.path().filename().string() > latest->filename().string()) {
            latest = entry.path();
        }
    }

    return latest;
}

ParentInput resolveParentInput(const Options& opt)
{
    ParentInput input;
    input.rootPath = rootPathFromArgument(opt.parent);

    const fs::path parentPath{opt.parent};
    if (toUpper(parentPath.extension().string()) == ".DATA") {
        input.deckPath = parentPath;
    }
    else {
        input.deckPath = input.rootPath;
        input.deckPath += ".DATA";
    }

    input.restartPath = input.rootPath;
    input.restartPath += ".UNRST";
    if (!fs::exists(input.restartPath)) {
        if (const auto latestX = latestSeparateRestart(input.rootPath, 'X')) {
            input.restartPath = *latestX;
        }
        else if (const auto latestF = latestSeparateRestart(input.rootPath, 'F')) {
            input.restartPath = *latestF;
        }
    }

    fs::path initPath = input.rootPath;
    initPath += ".INIT";
    if (fs::exists(initPath)) {
        input.initPath = initPath;
    }

    if (!opt.summary.empty()) {
        fs::path summaryRoot = rootPathFromArgument(opt.summary);
        fs::path summaryPath{opt.summary};
        if (toUpper(summaryPath.extension().string()) == ".SMSPEC"
            || toUpper(summaryPath.extension().string()) == ".FSMSPEC") {
            input.summaryPath = summaryPath;
        }
        else {
            summaryRoot += ".SMSPEC";
            input.summaryPath = summaryRoot;
        }
    }
    else {
        fs::path summaryPath = input.rootPath;
        summaryPath += ".SMSPEC";
        if (fs::exists(summaryPath)) {
            input.summaryPath = summaryPath;
        }
    }

    if (!fs::exists(input.deckPath)) {
        throw std::invalid_argument("parent deck not found: '" + input.deckPath.string() + "'");
    }
    if (!fs::exists(input.restartPath)) {
        throw std::invalid_argument("parent restart file not found: expected '" + input.rootPath.string() + ".UNRST', '.X0000', or '.F0000'");
    }
    if (input.summaryPath && !fs::exists(*input.summaryPath)) {
        throw std::invalid_argument("summary file not found: '" + input.summaryPath->string() + "'");
    }

    return input;
}

Opm::Deck loadDeck(const fs::path& deckPath)
{
    Opm::Parser parser;
    Opm::ParseContext parseContext;
    Opm::ErrorGuard errorGuard;
    auto deck = parser.parseFile(deckPath.string(), parseContext, errorGuard);
    if (errorGuard) {
        errorGuard.dump();
        errorGuard.terminate();
    }

    return deck;
}

std::array<int, 3> gridDims(const Opm::EclipseState& state)
{
    const auto dims = state.gridDims().getNXYZ();
    return {dims[0], dims[1], dims[2]};
}

int phaseMask(const Opm::Deck& deck)
{
    int mask = 0;
    if (deck.hasKeyword("OIL")) {
        mask |= static_cast<int>(Opm::EclIO::FluxFile::Phase::Oil);
    }
    if (deck.hasKeyword("WATER")) {
        mask |= static_cast<int>(Opm::EclIO::FluxFile::Phase::Water);
    }
    if (deck.hasKeyword("GAS")) {
        mask |= static_cast<int>(Opm::EclIO::FluxFile::Phase::Gas);
    }
    return mask;
}

Opm::EclIO::FluxFile::Mode modeFromString(const std::string& mode)
{
    if (mode == "pressure") {
        return Opm::EclIO::FluxFile::Mode::Pressure;
    }
    if (mode == "both") {
        return Opm::EclIO::FluxFile::Mode::Both;
    }
    return Opm::EclIO::FluxFile::Mode::Flux;
}

Opm::EclIO::FluxFile::Sampling samplingFromString(const std::string& sampling)
{
    if (sampling == "instant") {
        return Opm::EclIO::FluxFile::Sampling::Instant;
    }
    return Opm::EclIO::FluxFile::Sampling::Averaged;
}

bool hasFluxMode(const Opm::EclIO::FluxFile::Mode mode)
{
    return (static_cast<int>(mode) & static_cast<int>(Opm::EclIO::FluxFile::Mode::Flux)) != 0;
}

bool hasPressureMode(const Opm::EclIO::FluxFile::Mode mode)
{
    return (static_cast<int>(mode) & static_cast<int>(Opm::EclIO::FluxFile::Mode::Pressure)) != 0;
}

std::vector<int> parseMappingFile(const fs::path& mappingPath,
                                  const std::array<int, 3>& dims)
{
    std::ifstream input(mappingPath);
    if (!input) {
        throw std::invalid_argument("failed to open mapping file '" + mappingPath.string() + "'");
    }

    const int cellCount = dims[0] * dims[1] * dims[2];
    std::vector<int> regionValues(cellCount, 0);
    std::string line;
    int lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;
        const auto commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line.erase(commentPos);
        }

        std::istringstream iss(line);
        std::vector<int> tokens;
        int value = 0;
        while (iss >> value) {
            tokens.push_back(value);
        }

        if (tokens.empty()) {
            continue;
        }

        int globalCell = -1;
        if (tokens.size() == 1U) {
            globalCell = tokens[0];
        }
        else if (tokens.size() == 3U) {
            const int i = tokens[0] - 1;
            const int j = tokens[1] - 1;
            const int k = tokens[2] - 1;
            if (i < 0 || i >= dims[0] || j < 0 || j >= dims[1] || k < 0 || k >= dims[2]) {
                throw std::invalid_argument("mapping line " + std::to_string(lineNumber)
                                            + " has out-of-range i/j/k coordinates");
            }
            globalCell = Opm::FluxRegions::cartesianIndex(dims, i, j, k);
        }
        else {
            throw std::invalid_argument("mapping line " + std::to_string(lineNumber)
                                        + " must contain either 1 or 3 integers");
        }

        if (globalCell < 0 || globalCell >= cellCount) {
            throw std::invalid_argument("mapping line " + std::to_string(lineNumber)
                                        + " refers to out-of-range global cell index "
                                        + std::to_string(globalCell));
        }

        regionValues[globalCell] = 1;
    }

    if (std::find(regionValues.begin(), regionValues.end(), 1) == regionValues.end()) {
        throw std::invalid_argument("mapping file did not select any cells");
    }

    return regionValues;
}

std::vector<double> loadNumericArray(Opm::EclIO::EclFile& file, const std::string& keyword)
{
    if (!file.hasKey(keyword)) {
        throw std::invalid_argument("required INIT array missing: '" + keyword + "'");
    }

    try {
        const auto& values = file.get<float>(keyword);
        return {values.begin(), values.end()};
    }
    catch (const std::exception&) {
        const auto& values = file.get<double>(keyword);
        return {values.begin(), values.end()};
    }
}

std::vector<double> loadTransArray(Opm::EclIO::ERst& restart,
                                   const int reportStep,
                                   const std::optional<fs::path>& initPath,
                                   const std::string& keyword)
{
    if (initPath) {
        Opm::EclIO::EclFile initFile(initPath->string(), /*preload=*/false);
        if (initFile.hasKey(keyword)) {
            return loadNumericArray(initFile, keyword);
        }
    }

    if (!restart.hasArray(keyword, reportStep)) {
        throw std::invalid_argument("required transmissibility array missing from INIT/restart: '" + keyword + "'");
    }

    const auto& values = restart.getRestartData<float>(keyword, reportStep);
    return {values.begin(), values.end()};
}

double transmissibilityForFace(const Opm::FluxRegions::BoundaryFace& face,
                               const std::vector<double>& tranx,
                               const std::vector<double>& trany,
                               const std::vector<double>& tranz)
{
    const auto requireIndex = [&](const int index, const char* axis) {
        if (index < 0) {
            throw std::invalid_argument(std::string{"mapping reaches the outer parent boundary; "}
                                        + "cannot build pressure-mode FLUX state for " + axis + " face without exterior cell");
        }
        return static_cast<std::size_t>(index);
    };

    switch (face.direction) {
    case Opm::FaceDir::XPlus:
        return tranx.at(requireIndex(face.interiorGlobalCell, "X+"));
    case Opm::FaceDir::XMinus:
        return tranx.at(requireIndex(face.exteriorGlobalCell, "X-"));
    case Opm::FaceDir::YPlus:
        return trany.at(requireIndex(face.interiorGlobalCell, "Y+"));
    case Opm::FaceDir::YMinus:
        return trany.at(requireIndex(face.exteriorGlobalCell, "Y-"));
    case Opm::FaceDir::ZPlus:
        return tranz.at(requireIndex(face.interiorGlobalCell, "Z+"));
    case Opm::FaceDir::ZMinus:
        return tranz.at(requireIndex(face.exteriorGlobalCell, "Z-"));
    default:
        throw std::invalid_argument("unsupported boundary face direction in mapping");
    }
}

std::string rateArrayName(const Opm::EclIO::FluxFile::Phase phase,
                          const Opm::FaceDir::DirEnum dir)
{
    std::string prefix;
    switch (phase) {
    case Opm::EclIO::FluxFile::Phase::Oil:
        prefix = "FLROIL";
        break;
    case Opm::EclIO::FluxFile::Phase::Water:
        prefix = "FLRWAT";
        break;
    case Opm::EclIO::FluxFile::Phase::Gas:
        prefix = "FLRGAS";
        break;
    }

    switch (dir) {
    case Opm::FaceDir::XPlus:
        return prefix + "I+";
    case Opm::FaceDir::XMinus:
        return prefix + "I-";
    case Opm::FaceDir::YPlus:
        return prefix + "J+";
    case Opm::FaceDir::YMinus:
        return prefix + "J-";
    case Opm::FaceDir::ZPlus:
        return prefix + "K+";
    case Opm::FaceDir::ZMinus:
        return prefix + "K-";
    case Opm::FaceDir::Unknown:
        break;
    }

    throw std::invalid_argument("unsupported face direction for FLUX-mode rate array lookup");
}

std::string nncRateArrayName(const Opm::EclIO::FluxFile::Phase phase)
{
    switch (phase) {
    case Opm::EclIO::FluxFile::Phase::Oil:
        return "FLROILN+";
    case Opm::EclIO::FluxFile::Phase::Water:
        return "FLRWATN+";
    case Opm::EclIO::FluxFile::Phase::Gas:
        return "FLRGASN+";
    }

    throw std::invalid_argument("unsupported phase for NNC FLUX-mode rate array lookup");
}

NncKey normalizedNncPair(const int c1, const int c2)
{
    return (c1 <= c2)
        ? std::make_pair(c1, c2)
        : std::make_pair(c2, c1);
}

std::map<NncKey, int> buildNncPairToIndex(const Opm::EclipseState& state)
{
    std::map<NncKey, int> pairToIndex;
    int nncIndex = 0;

    if (state.hasInputNNC()) {
        const auto& inputNnc = state.getInputNNC().input();
        for (const auto& nnc : inputNnc) {
            pairToIndex.try_emplace(normalizedNncPair(static_cast<int>(nnc.cell1),
                                                      static_cast<int>(nnc.cell2)),
                                    nncIndex);
            ++nncIndex;
        }
    }

    if (state.hasPinchNNC()) {
        const auto& pinchNnc = state.getPinchNNC();
        for (const auto& nnc : pinchNnc) {
            pairToIndex.try_emplace(normalizedNncPair(static_cast<int>(nnc.cell1),
                                                      static_cast<int>(nnc.cell2)),
                                    nncIndex);
            ++nncIndex;
        }
    }

    return pairToIndex;
}

SummaryPayload loadSummaryPayload(const fs::path& summaryPath)
{
    Opm::EclIO::ESmry summary(summaryPath.string());
    const auto keys = summary.keywordList();
    if (keys.empty()) {
        throw std::invalid_argument("summary file contains no keys: '" + summaryPath.string() + "'");
    }

    const auto timesFloat = summary.get_at_rstep("TIME");
    const auto expectedReportSteps = timesFloat.size();

    SummaryPayload payload;
    payload.keys = keys;
    payload.reportTimes.assign(timesFloat.begin(), timesFloat.end());
    payload.valuesPerStep.assign(expectedReportSteps, {});
    for (auto& stepValues : payload.valuesPerStep) {
        stepValues.reserve(keys.size());
    }

    for (const auto& key : keys) {
        const auto values = summary.get_at_rstep(key);
        if (values.size() != expectedReportSteps) {
            throw std::invalid_argument("summary key '" + key + "' does not have one value per report step");
        }

        for (std::size_t stepIdx = 0; stepIdx < expectedReportSteps; ++stepIdx) {
            payload.valuesPerStep[stepIdx].push_back(values[stepIdx]);
        }
    }

    return payload;
}

std::vector<double> optionalRestartArray(Opm::EclIO::ERst& restart,
                                         const std::string& keyword,
                                         const int reportStep)
{
    if (!restart.hasArray(keyword, reportStep)) {
        return {};
    }

    const auto& values = restart.getRestartData<float>(keyword, reportStep);
    return {values.begin(), values.end()};
}

std::vector<double> requiredRestartRealArray(Opm::EclIO::ERst& restart,
                                             const std::string& keyword,
                                             const int reportStep)
{
    if (!restart.hasArray(keyword, reportStep)) {
        throw std::invalid_argument("restart step " + std::to_string(reportStep)
                                    + " is missing required array '" + keyword + "'");
    }

    return optionalRestartArray(restart, keyword, reportStep);
}

std::vector<double> requiredRestartArray(Opm::EclIO::ERst& restart,
                                         const std::string& keyword,
                                         const int reportStep)
{
    if (!restart.hasArray(keyword, reportStep)) {
        throw std::invalid_argument("restart step " + std::to_string(reportStep)
                                    + " is missing required array '" + keyword + "'");
    }

    return optionalRestartArray(restart, keyword, reportStep);
}

void fillPressureStepData(Opm::FluxDumper::ReportStepData& step,
                          const Opm::FluxRegions::Region& region,
                          const Opm::UnitSystem& unitSystem,
                          const std::vector<double>& pressure,
                          const std::vector<double>& swat,
                          const std::vector<double>& sgas,
                          const std::vector<double>& rs,
                          const std::vector<double>& rv,
                          const std::vector<double>& temperature,
                          const bool waterActive,
                          const bool gasActive,
                          const bool includeRs,
                          const bool includeRv,
                          const bool hasTemperature)
{
    const auto numFaces = region.boundaryFaces.size();
    step.pressures.clear();
    step.swat.clear();
    step.sgas.clear();
    step.rs.clear();
    step.rv.clear();
    step.temperature.clear();
    step.pressures.reserve(numFaces);
    if (waterActive) {
        step.swat.reserve(numFaces);
    }
    if (gasActive) {
        step.sgas.reserve(numFaces);
    }
    if (includeRs) {
        step.rs.reserve(numFaces);
    }
    if (includeRv) {
        step.rv.reserve(numFaces);
    }
    if (hasTemperature) {
        step.temperature.reserve(numFaces);
    }

    for (const auto& face : region.boundaryFaces) {
        if (face.exteriorGlobalCell < 0) {
            throw std::invalid_argument("mapping reaches outer parent boundary; pressure-mode output requires an exterior parent cell for every boundary face");
        }

        const auto exterior = static_cast<std::size_t>(face.exteriorGlobalCell);
        step.pressures.push_back(unitSystem.to_si(Opm::UnitSystem::measure::pressure,
                              pressure.at(exterior)));

        if (waterActive) {
            step.swat.push_back(swat.at(exterior));
        }
        if (gasActive) {
            step.sgas.push_back(sgas.at(exterior));
        }
        if (includeRs) {
            step.rs.push_back(rs.empty() ? 0.0 : rs.at(exterior));
        }
        if (includeRv) {
            step.rv.push_back(rv.empty() ? 0.0 : rv.at(exterior));
        }
        if (hasTemperature) {
            step.temperature.push_back(temperature.at(exterior));
        }
    }
}

void fillFluxStepData(Opm::FluxDumper::ReportStepData& step,
                      const Opm::FluxRegions::Region& region,
                      Opm::EclIO::ERst& restart,
                      const int reportStep,
                      const int phaseMask,
                      const std::map<NncKey, int>& nncPairToIndex)
{
    step.rates.clear();

    const std::array phases{
        Opm::EclIO::FluxFile::Phase::Oil,
        Opm::EclIO::FluxFile::Phase::Water,
        Opm::EclIO::FluxFile::Phase::Gas,
    };

    std::map<std::string, std::vector<double>> cachedArrays;
    auto loadArray = [&](const std::string& name) -> const std::vector<double>&
    {
        const auto [it, inserted] = cachedArrays.try_emplace(name);
        if (inserted) {
            it->second = requiredRestartRealArray(restart, name, reportStep);
        }
        return it->second;
    };

    for (const auto& face : region.boundaryFaces) {
        for (const auto phase : phases) {
            if ((phaseMask & static_cast<int>(phase)) == 0) {
                continue;
            }

            if (face.isNnc) {
                const auto key = normalizedNncPair(face.interiorGlobalCell, face.exteriorGlobalCell);
                const auto indexIt = nncPairToIndex.find(key);
                if (indexIt == nncPairToIndex.end()) {
                    throw std::invalid_argument("NNC boundary face not found in parent NNC list");
                }

                const auto& values = loadArray(nncRateArrayName(phase));
                const auto nncIndex = static_cast<std::size_t>(indexIt->second);
                if (nncIndex >= values.size()) {
                    throw std::invalid_argument("NNC rate array is smaller than expected for parent NNC list");
                }

                const auto nncFlux = values[nncIndex];
                step.rates.push_back((face.interiorGlobalCell == key.second) ? nncFlux : -nncFlux);
                continue;
            }

            const auto& values = loadArray(rateArrayName(phase, face.direction));
            const auto interior = static_cast<std::size_t>(face.interiorGlobalCell);
            if (interior >= values.size()) {
                throw std::invalid_argument("directional FLORES array is smaller than expected for parent grid");
            }

            // Restart directional face rates use the interior-cell face orientation.
            // FLUX files store positive values into the sector, i.e. opposite sign.
            step.rates.push_back(-values[interior]);
        }
    }
}

int run(const Options& opt)
{
    if (opt.parent.empty()) {
        throw std::invalid_argument("missing required argument --parent=<CASE|CASE.DATA>");
    }
    if (opt.mapping.empty()) {
        throw std::invalid_argument("missing required argument --mapping=<file>");
    }
    if (opt.output.empty()) {
        throw std::invalid_argument("missing required argument --output=<SECTOR.FLUX>");
    }
    if (!validMode(opt.mode)) {
        throw std::invalid_argument("invalid --mode value '" + opt.mode + "' (expected flux|pressure|both)");
    }
    if (!validSampling(opt.sampling)) {
        throw std::invalid_argument("invalid --sampling value '" + opt.sampling + "' (expected averaged|instant)");
    }

    const ParentInput parentInput = resolveParentInput(opt);
    Opm::OpmLog::setupSimpleDefaultLogging();

    const auto deck = loadDeck(parentInput.deckPath);
    const Opm::EclipseState state(deck);
    const auto fluxMode = modeFromString(opt.mode);
    const auto dims = gridDims(state);
    const auto& unitSystem = state.getDeckUnitSystem();
    const auto regionValues = parseMappingFile(opt.mapping, dims);
    const auto regions = Opm::FluxRegions::extract(dims, regionValues);
    if (regions.size() != 1U) {
        throw std::invalid_argument("mapping must define exactly one connected region");
    }

    const auto& region = regions.front();
    if (region.boundaryFaces.empty()) {
        throw std::invalid_argument("mapping produced no boundary faces");
    }

    const auto phaseMaskValue = phaseMask(deck);
    const auto nncPairToIndex = buildNncPairToIndex(state);

    Opm::EclIO::ERst restart(parentInput.restartPath.string());
    const auto reportSteps = restart.listOfReportStepNumbers();
    if (reportSteps.empty()) {
        throw std::invalid_argument("parent restart file contains no report steps");
    }

    const auto tranx = loadTransArray(restart, reportSteps.front(), parentInput.initPath, "TRANX");
    const auto trany = loadTransArray(restart, reportSteps.front(), parentInput.initPath, "TRANY");
    const auto tranz = loadTransArray(restart, reportSteps.front(), parentInput.initPath, "TRANZ");

    std::vector<double> boundaryTransmissibilities;
    boundaryTransmissibilities.reserve(region.boundaryFaces.size());
    for (const auto& face : region.boundaryFaces) {
        boundaryTransmissibilities.push_back(transmissibilityForFace(face, tranx, trany, tranz));
    }

    const bool waterActive = (phaseMaskValue & static_cast<int>(Opm::EclIO::FluxFile::Phase::Water)) != 0;
    const bool gasActive = (phaseMaskValue & static_cast<int>(Opm::EclIO::FluxFile::Phase::Gas)) != 0;
    const bool includeRs = true;
    const bool includeRv = true;
    const bool hasTemperature = restart.hasArray("TEMP", reportSteps.front());

    Opm::FluxDumper dumper(parentInput.rootPath.filename().string(),
                           /*regionId=*/1,
                           dims,
                           region,
                           fluxMode,
                           samplingFromString(opt.sampling),
                           phaseMaskValue,
                           boundaryTransmissibilities);

    std::optional<SummaryPayload> summaryPayload;
    std::size_t restartStepStartIndex = 0;
    if (parentInput.summaryPath) {
        summaryPayload = loadSummaryPayload(*parentInput.summaryPath);
        if (summaryPayload->reportTimes.size() == reportSteps.size()) {
            restartStepStartIndex = 0;
        }
        else if (summaryPayload->reportTimes.size() + 1 == reportSteps.size() && reportSteps.front() == 0) {
            restartStepStartIndex = 1;
        }
        else {
            throw std::invalid_argument("summary report-step TIME vector size does not match restart report-step sequence");
        }
        dumper.setSummaryKeys(summaryPayload->keys);
    }

    double previousTime = 0.0;
    for (std::size_t stepIdx = restartStepStartIndex; stepIdx < reportSteps.size(); ++stepIdx) {
        const int sourceReportStep = reportSteps[stepIdx];
        const auto summaryStepIdx = stepIdx - restartStepStartIndex;
        const int reportStep = summaryPayload
            ? static_cast<int>(summaryStepIdx + 1)
            : sourceReportStep;
        const int simStep = summaryPayload
            ? static_cast<int>(summaryStepIdx + 1)
            : sourceReportStep;
        auto step = dumper.makeZeroFluxStep(reportStep,
                                            simStep,
                                            previousTime,
                                            0.0);

        if (hasFluxMode(fluxMode)) {
            fillFluxStepData(step,
                             region,
                             restart,
                             sourceReportStep,
                             phaseMaskValue,
                             nncPairToIndex);
        }

        if (hasPressureMode(fluxMode)) {
            const auto pressure = requiredRestartArray(restart, "PRESSURE", sourceReportStep);
            const auto swat = waterActive ? requiredRestartArray(restart, "SWAT", sourceReportStep) : std::vector<double>{};
            const auto sgas = gasActive ? requiredRestartArray(restart, "SGAS", sourceReportStep) : std::vector<double>{};
            const auto rs = optionalRestartArray(restart, "RS", sourceReportStep);
            const auto rv = optionalRestartArray(restart, "RV", sourceReportStep);
            const auto temperature = hasTemperature ? requiredRestartArray(restart, "TEMP", sourceReportStep) : std::vector<double>{};

            fillPressureStepData(step,
                                 region,
                                 unitSystem,
                                 pressure,
                                 swat,
                                 sgas,
                                 rs,
                                 rv,
                                 temperature,
                                 waterActive,
                                 gasActive,
                                 includeRs,
                                 includeRv,
                                 hasTemperature);
        }

        if (summaryPayload) {
            const double currentTime = summaryPayload->reportTimes[summaryStepIdx] * secondsPerDay;
            step.startTime = previousTime;
            step.stepLength = currentTime - previousTime;
            step.summaryValues = summaryPayload->valuesPerStep[summaryStepIdx];
            previousTime = currentTime;
        }

        dumper.appendReportStep(step);
    }

    dumper.write(opt.output, /*formatted=*/false);

    std::cout << "Wrote " << opt.mode << "-mode FLUX file '" << opt.output << "' with "
              << region.boundaryFaces.size() << " boundary faces and "
              << (reportSteps.size() - restartStepStartIndex) << " report steps\n";

    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const auto opt = parseOptions(argc, argv);
        if (opt.help) {
            printUsage();
            return 0;
        }

        return run(opt);
    }
    catch (const std::exception& e) {
        std::cerr << "make_flux: " << e.what() << '\n';
        printUsage();
        return 1;
    }
}
