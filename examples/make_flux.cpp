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
#include <opm/input/eclipse/EclipseState/SummaryConfig/SummaryConfig.hpp>
#include <opm/input/eclipse/Parser/ErrorGuard.hpp>
#include <opm/input/eclipse/Parser/InputErrorAction.hpp>
#include <opm/input/eclipse/Parser/ParseContext.hpp>
#include <opm/input/eclipse/Parser/Parser.hpp>
#include <opm/input/eclipse/Schedule/Action/ActionX.hpp>
#include <opm/input/eclipse/Schedule/Action/Actions.hpp>
#include <opm/input/eclipse/Schedule/RequisiteSummaryVector.hpp>
#include <opm/input/eclipse/Schedule/Schedule.hpp>
#include <opm/input/eclipse/Schedule/UDQ/UDQConfig.hpp>
#include <opm/input/eclipse/Schedule/UDQ/UDQDefine.hpp>
#include <opm/input/eclipse/Units/UnitSystem.hpp>
#include <opm/io/eclipse/EclFile.hpp>
#include <opm/io/eclipse/ERst.hpp>
#include <opm/io/eclipse/ESmry.hpp>
#include <opm/io/eclipse/SummaryNode.hpp>
#include <opm/simulators/flow/flux/FluxDumper.hpp>
#include <opm/simulators/flow/flux/FluxRegions.hpp>
#include <opm/simulators/flow/flux/FluxSummaryKeys.hpp>
#include <opm/simulators/utils/readDeck.hpp>

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
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
constexpr double secondsPerDay = 24.0 * 60.0 * 60.0;

struct Options {
    std::string parent;
    std::string mapping;
    std::string mappingInline;
    std::string fluxnum;
    std::string regions;
    std::string output;
    std::string mode = "pressure";
    std::string sampling = "averaged";
    std::string summary;
    std::string parsingStrictness = "normal";
    std::vector<std::string> summaryDefines;
    bool noSummary = false;
    bool help = false;
};

struct ParentInput {
    fs::path deckPath;
    fs::path rootPath;
    fs::path restartPath;
    std::optional<fs::path> gridPath;
    std::optional<fs::path> initPath;
    std::optional<fs::path> summaryPath;
};

struct SummaryPayload {
    std::vector<std::string> keys;
    std::vector<std::vector<double>> valuesPerStep;
    std::vector<double> reportTimes;
};

struct RequiredSummaryFallback {
    std::string missingKey;
    std::vector<std::string> candidateKeys;
};

using NncKey = std::pair<int, int>;

void printUsage()
{
    std::cout
        << "usage: make_flux --parent=<CASE|CASE.DATA> --output=<SECTOR.FLUX>\n"
        << "                 (--fluxnum=<file.grdecl> --regions=<list>\n"
        << "                  | --mapping=<file> | --mapping-inline=<spec>) [options]\n"
        << "\n"
        << "Builds a sector boundary file from a parent run's output. The FLUXNUM route\n"
        << "is an alternative to running the parent with DUMPFLUX: it needs nothing but\n"
        << "the restart and summary files, at the cost of being limited to what those\n"
        << "hold. Records land on report steps only, and the exterior relative\n"
        << "permeability and capillary pressure are evaluated from the saturation\n"
        << "functions rather than recovered from the parent's converged state, so a\n"
        << "deck with hysteresis will not reproduce a DUMPFLUX file exactly.\n"
        << "\n"
        << "region selection (mutually exclusive):\n"
        << "  --fluxnum=<file.grdecl>              File holding a FLUXNUM array over the\n"
        << "                                       parent grid. Requires --regions.\n"
        << "  --regions=<n[,n...]>                 Which FLUXNUM regions to write. With\n"
        << "                                       more than one, --output is used as a\n"
        << "                                       base and CASE.FLUXnnnn is written per\n"
        << "                                       region, as DUMPFLUX does.\n"
        << "  --mapping=<file>                     Region from mapping directives.\n"
        << "  --mapping-inline=<spec>              Same, given on the command line.\n"
        << "\n"
        << "options:\n"
        << "  --mode=<flux|pressure|both>          Boundary payload (default: pressure).\n"
        << "                                       flux and both need FLOWS restart\n"
        << "                                       arrays and are not available with\n"
        << "                                       --fluxnum.\n"
        << "  --sampling=<averaged|instant>        Flux sampling mode (default: averaged)\n"
        << "  --summary=<CASE|CASE.SMSPEC>         Optional parent summary override\n"
        << "  --parsing-strictness=<normal|low|high>\n"
        << "                                       Deck parsing strictness, as in flow\n"
        << "                                       (default: normal)\n"
        << "  --smry-define=<TARGET,SOURCE>        Supply a summary vector the parent did\n"
        << "                                       not write. SOURCE is either another\n"
        << "                                       key to copy or a constant, so both\n"
        << "                                       --smry-define=WBHP:P1,WBHP:P2 and\n"
        << "                                       --smry-define=WBHP:P1,50.0 are valid.\n"
        << "                                       Repeatable.\n"
        << "  --no-summary                         Ignore SMSPEC/FSMSPEC summary data\n"
        << "  --help                               Show this message\n"
        << "\n"
        << "mapping directives (file or --mapping-inline):\n"
        << "  Legacy line forms:\n"
        << "    <global-cartesian-index>           zero-based parent global cartesian index\n"
        << "    <i> <j> <k>                        one-based parent cartesian coordinates\n"
        << "  Extended forms:\n"
        << "    global <index>                     select one zero-based global index\n"
        << "    ijk <i> <j> <k>                    select one one-based cartesian cell\n"
        << "    global_range <begin> <end>         select inclusive zero-based global range\n"
        << "    box <i1> <j1> <k1> <i2> <j2> <k2>  select inclusive one-based cartesian box\n"
        << "  Comments use '#'. For --mapping-inline, separate directives with ';'\n";
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
        else if (startsWith(arg, "--mapping-inline=")) {
            opt.mappingInline = valueAfterEquals(arg);
        }
        else if (startsWith(arg, "--fluxnum=")) {
            opt.fluxnum = valueAfterEquals(arg);
        }
        else if (startsWith(arg, "--regions=")) {
            opt.regions = valueAfterEquals(arg);
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
        else if (startsWith(arg, "--parsing-strictness=")) {
            opt.parsingStrictness = valueAfterEquals(arg);
        }
        else if (startsWith(arg, "--smry-define=")) {
            opt.summaryDefines.push_back(valueAfterEquals(arg));
        }
        else if (arg == "--no-summary") {
            opt.noSummary = true;
        }
        else {
            throw std::invalid_argument("unrecognized argument '" + arg + "'");
        }
    }

    return opt;
}

//! \brief A summary vector the parent did not write, supplied on the command line.
//!
//! \details Either the name of another vector to copy, or a constant to use at
//!   every step. A sector run needs the vectors its UDQ and ACTIONX expressions
//!   refer to, and this route can only embed what the parent's SUMMARY section
//!   happened to ask for, so there has to be a way to fill a gap by hand.
struct SummaryDefinition {
    std::string sourceKey;      //!< Empty when \c value is used instead.
    double value{0.0};
};

std::unordered_map<std::string, SummaryDefinition>
parseSummaryDefines(const Options& opt)
{
    std::unordered_map<std::string, SummaryDefinition> defines;

    for (const auto& spec : opt.summaryDefines) {
        const auto commaPos = spec.find(',');
        if (commaPos == std::string::npos
            || commaPos == 0
            || (commaPos + 1) >= spec.size())
        {
            throw std::invalid_argument("invalid --smry-define value '" + spec
                                        + "' (expected TARGET,SOURCE where SOURCE is "
                                          "another summary key or a constant)");
        }

        const auto target = spec.substr(0, commaPos);
        const auto source = spec.substr(commaPos + 1);

        // A source that parses cleanly as a number is a constant; anything else
        // names another vector. Summary keys never look like numbers, so this
        // cannot be ambiguous.
        SummaryDefinition definition;
        try {
            std::size_t consumed = 0;
            const auto numeric = std::stod(source, &consumed);
            if (consumed == source.size()) {
                definition.value = numeric;
            }
            else {
                definition.sourceKey = source;
            }
        }
        catch (const std::exception&) {
            definition.sourceKey = source;
        }

        defines[target] = definition;
    }

    return defines;
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

    for (const auto* extension : {".EGRID", ".FEGRID"}) {
        fs::path gridPath = input.rootPath;
        gridPath += extension;
        if (fs::exists(gridPath)) {
            input.gridPath = gridPath;
            break;
        }
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
    else if (!opt.noSummary) {
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

//! \brief The set of cells the parent actually solved for.
//!
//! \details The deck's own ACTNUM is not the answer. The simulator drops
//!   further cells on its way to a grid, MINPV being the usual reason, and it
//!   is the surviving set that the restart arrays are indexed by and that the
//!   region boundary has to be built against. The EGRID records that set, so
//!   read it from there and fall back on the deck only when no EGRID was kept.
std::vector<int> parentActnum(const std::optional<fs::path>& gridPath,
                              const Opm::EclipseState& state,
                              const std::array<int, 3>& dims)
{
    const auto numGlobal = static_cast<std::size_t>(dims[0]) * dims[1] * dims[2];

    if (gridPath) {
        Opm::EclIO::EclFile grid(gridPath->string(), /*preload=*/false);
        if (grid.hasKey("ACTNUM")) {
            const auto& values = grid.get<int>("ACTNUM");
            if (values.size() == numGlobal) {
                return {values.begin(), values.end()};
            }
        }
    }

    return state.globalFieldProps().actnumRaw();
}

//!
//! \details A deck that flow accepts has to be accepted here too. The whole
//!   point of this tool is to work off a parent case that has already been
//!   run, so refusing it over a stray slash or a keyword flow merely warns
//!   about would be gratuitous. Strictness is shared with flow's
//!   --parsing-strictness so a deck that needs "low" there can get it here.
Opm::Deck loadDeck(const fs::path& deckPath, const std::string& parsingStrictness)
{
    if (parsingStrictness != "normal" && parsingStrictness != "low"
        && parsingStrictness != "high") {
        throw std::invalid_argument("--parsing-strictness must be one of 'normal', "
                                    "'low' or 'high', got '" + parsingStrictness + "'");
    }

    auto parseContext = Opm::setupParseContext(parsingStrictness == "high");
    if (parsingStrictness == "low") {
        parseContext->update(Opm::ParseContext::SCHEDULE_INVALID_NAME,
                             Opm::InputErrorAction::WARN);
        parseContext->update(Opm::ParseContext::SCHEDULE_GCONSALE_INVALID_INJECTION,
                             Opm::InputErrorAction::WARN);
    }

    Opm::Parser parser;
    Opm::ErrorGuard errorGuard;
    auto deck = parser.parseFile(deckPath.string(), *parseContext, errorGuard);
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

//! \brief Resolves a global parent-grid cell index into an output array.
//!
//! \details The region description works entirely in global cartesian indices,
//!   but the files it is read against do not agree on an index space. Restart
//!   arrays and most INIT arrays hold one entry per ACTIVE cell, while a few
//!   INIT arrays, PORV among them, hold one per cell of the whole grid. Nothing
//!   in the file says which, so the length decides.
//!
//!   Any model with inactive cells trips over this, which is why every cell
//!   lookup goes through here rather than indexing an array directly.
class CellLookup
{
public:
    CellLookup(const std::array<int, 3>& dims, const std::vector<int>& actnum)
        : globalToActive_(static_cast<std::size_t>(dims[0]) * dims[1] * dims[2], -1)
    {
        int active = 0;
        for (std::size_t cell = 0; cell < this->globalToActive_.size(); ++cell) {
            // An empty ACTNUM means the deck never restricted the grid.
            if (actnum.empty() || ((cell < actnum.size()) && (actnum[cell] > 0))) {
                this->globalToActive_[cell] = active++;
            }
        }

        this->numActive_ = static_cast<std::size_t>(active);
    }

    std::size_t numGlobal() const { return this->globalToActive_.size(); }
    std::size_t numActive() const { return this->numActive_; }

    //! \brief Reject an array that belongs to neither index space.
    //!
    //! \details Checked up front so that a file from a different grid is
    //!   reported as such, rather than as an out-of-range access part way
    //!   through the report steps.
    void require(const std::vector<double>& values, const std::string& name) const
    {
        if ((values.size() != this->numGlobal()) && (values.size() != this->numActive())) {
            throw std::invalid_argument("array '" + name + "' holds " + std::to_string(values.size())
                                        + " values, which is neither the parent grid's "
                                        + std::to_string(this->numGlobal()) + " cells nor its "
                                        + std::to_string(this->numActive())
                                        + " active cells; the output does not match the deck");
        }
    }

    //! \brief Value at a global cell, or nothing when the cell has no entry.
    std::optional<double> operator()(const std::vector<double>& values, const int globalCell) const
    {
        if (values.empty() || (globalCell < 0)) {
            return std::nullopt;
        }

        const auto index = (values.size() == this->numGlobal())
            ? static_cast<std::ptrdiff_t>(globalCell)
            : this->activeIndex(globalCell);

        if ((index < 0) || (static_cast<std::size_t>(index) >= values.size())) {
            return std::nullopt;
        }

        return values[static_cast<std::size_t>(index)];
    }

    //! \brief Value at a global cell, or \p fallback when there is none.
    double valueOr(const std::vector<double>& values,
                   const int globalCell,
                   const double fallback = 0.0) const
    {
        return (*this)(values, globalCell).value_or(fallback);
    }

    //! \brief Value at a global cell, refusing to carry on without one.
    double required(const std::vector<double>& values,
                    const int globalCell,
                    const std::string& name) const
    {
        if (const auto value = (*this)(values, globalCell)) {
            return *value;
        }

        throw std::invalid_argument("parent cell " + std::to_string(globalCell)
                                    + " has no entry in '" + name
                                    + "'; the sector borders a cell the parent did not solve for");
    }

private:
    std::vector<int> globalToActive_;
    std::size_t numActive_{0};

    std::ptrdiff_t activeIndex(const int globalCell) const
    {
        const auto cell = static_cast<std::size_t>(globalCell);
        return (cell < this->globalToActive_.size()) ? this->globalToActive_[cell] : -1;
    }
};

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

std::vector<int> parseMappingLines(const std::vector<std::string>& lines,
                                   const std::array<int, 3>& dims,
                                   const std::string& source)
{
    const int cellCount = dims[0] * dims[1] * dims[2];
    std::vector<int> regionValues(cellCount, 0);

    const auto selectGlobalCell = [&](const int globalCell, const std::string& location) {
        if (globalCell < 0 || globalCell >= cellCount) {
            throw std::invalid_argument(location
                                        + " refers to out-of-range global cell index "
                                        + std::to_string(globalCell));
        }

        regionValues[globalCell] = 1;
    };

    const auto parseInt = [](const std::string& token, const std::string& location) {
        try {
            std::size_t consumed = 0;
            const int value = std::stoi(token, &consumed);
            if (consumed != token.size()) {
                throw std::invalid_argument("");
            }
            return value;
        }
        catch (const std::exception&) {
            throw std::invalid_argument(location + " contains non-integer token '" + token + "'");
        }
    };

    int lineNumber = 0;
    for (const auto& rawLine : lines) {
        ++lineNumber;
        auto line = rawLine;
        const auto commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line.erase(commentPos);
        }

        for (char& c : line) {
            if (c == ',') {
                c = ' ';
            }
        }

        std::istringstream iss(line);
        std::vector<std::string> tokens;
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }

        if (tokens.empty()) {
            continue;
        }

        std::vector<std::string> lowerTokens = tokens;
        for (auto& item : lowerTokens) {
            std::transform(item.begin(), item.end(), item.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
        }

        const std::string location = source + ":" + std::to_string(lineNumber);

        if (lowerTokens[0] == "global") {
            if (tokens.size() != 2U) {
                throw std::invalid_argument(location + " expected 'global <index>'");
            }

            selectGlobalCell(parseInt(tokens[1], location), location);
            continue;
        }

        if (lowerTokens[0] == "ijk") {
            if (tokens.size() != 4U) {
                throw std::invalid_argument(location + " expected 'ijk <i> <j> <k>'");
            }

            const int i = parseInt(tokens[1], location) - 1;
            const int j = parseInt(tokens[2], location) - 1;
            const int k = parseInt(tokens[3], location) - 1;
            if (i < 0 || i >= dims[0] || j < 0 || j >= dims[1] || k < 0 || k >= dims[2]) {
                throw std::invalid_argument(location + " has out-of-range i/j/k coordinates");
            }

            selectGlobalCell(Opm::FluxRegions::cartesianIndex(dims, i, j, k), location);
            continue;
        }

        if (lowerTokens[0] == "global_range") {
            if (tokens.size() != 3U) {
                throw std::invalid_argument(location + " expected 'global_range <begin> <end>'");
            }

            const int begin = parseInt(tokens[1], location);
            const int end = parseInt(tokens[2], location);
            if (end < begin) {
                throw std::invalid_argument(location + " has descending global_range bounds");
            }

            for (int index = begin; index <= end; ++index) {
                selectGlobalCell(index, location);
            }
            continue;
        }

        if (lowerTokens[0] == "box") {
            if (tokens.size() != 7U) {
                throw std::invalid_argument(location + " expected 'box <i1> <j1> <k1> <i2> <j2> <k2>'");
            }

            const int i1 = parseInt(tokens[1], location);
            const int j1 = parseInt(tokens[2], location);
            const int k1 = parseInt(tokens[3], location);
            const int i2 = parseInt(tokens[4], location);
            const int j2 = parseInt(tokens[5], location);
            const int k2 = parseInt(tokens[6], location);

            const int iMin = std::min(i1, i2);
            const int iMax = std::max(i1, i2);
            const int jMin = std::min(j1, j2);
            const int jMax = std::max(j1, j2);
            const int kMin = std::min(k1, k2);
            const int kMax = std::max(k1, k2);

            if (iMin < 1 || iMax > dims[0] || jMin < 1 || jMax > dims[1] || kMin < 1 || kMax > dims[2]) {
                throw std::invalid_argument(location + " has out-of-range box coordinates");
            }

            for (int k = kMin; k <= kMax; ++k) {
                for (int j = jMin; j <= jMax; ++j) {
                    for (int i = iMin; i <= iMax; ++i) {
                        selectGlobalCell(Opm::FluxRegions::cartesianIndex(dims, i - 1, j - 1, k - 1), location);
                    }
                }
            }
            continue;
        }

        // Backward-compatible legacy syntax: either one global index or i j k triplet.
        std::vector<int> legacyValues;
        legacyValues.reserve(tokens.size());
        for (const auto& item : tokens) {
            legacyValues.push_back(parseInt(item, location));
        }

        int globalCell = -1;
        if (legacyValues.size() == 1U) {
            globalCell = legacyValues[0];
        }
        else if (legacyValues.size() == 3U) {
            const int i = legacyValues[0] - 1;
            const int j = legacyValues[1] - 1;
            const int k = legacyValues[2] - 1;
            if (i < 0 || i >= dims[0] || j < 0 || j >= dims[1] || k < 0 || k >= dims[2]) {
                throw std::invalid_argument(location + " has out-of-range i/j/k coordinates");
            }
            globalCell = Opm::FluxRegions::cartesianIndex(dims, i, j, k);
        }
        else {
            throw std::invalid_argument(location + " must contain either 1 or 3 integers, or a supported directive");
        }

        selectGlobalCell(globalCell, location);
    }

    if (std::find(regionValues.begin(), regionValues.end(), 1) == regionValues.end()) {
        throw std::invalid_argument(source + " did not select any cells");
    }

    return regionValues;
}

std::vector<int> parseMappingFile(const fs::path& mappingPath,
                                  const std::array<int, 3>& dims)
{
    std::ifstream input(mappingPath);
    if (!input) {
        throw std::invalid_argument("failed to open mapping file '" + mappingPath.string() + "'");
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    return parseMappingLines(lines, dims, mappingPath.string());
}

std::vector<int> parseMappingSpecification(const Options& opt,
                                           const std::array<int, 3>& dims)
{
    if (!opt.mapping.empty()) {
        return parseMappingFile(opt.mapping, dims);
    }

    std::vector<std::string> lines;
    std::string segment;
    std::istringstream inlineStream(opt.mappingInline);
    while (std::getline(inlineStream, segment, ';')) {
        lines.push_back(segment);
    }

    return parseMappingLines(lines, dims, "--mapping-inline");
}

//! \brief Read a FLUXNUM array from a standalone grdecl file.
//!
//! \details Parsed with the ordinary deck parser rather than by hand, so that
//!   repeat counts, comments and free-form layout behave exactly as they would
//!   inside a deck. The file holds no sections, which the parser is told to
//!   accept.
std::vector<int> readFluxnumFile(const fs::path& path, const std::array<int, 3>& dims)
{
    if (!fs::exists(path)) {
        throw std::invalid_argument("FLUXNUM file not found: '" + path.string() + "'");
    }

    Opm::ParseContext parseContext;
    parseContext.update(Opm::ParseContext::PARSE_MISSING_SECTIONS, Opm::InputErrorAction::IGNORE);
    parseContext.update(Opm::ParseContext::PARSE_UNKNOWN_KEYWORD, Opm::InputErrorAction::IGNORE);
    parseContext.update(Opm::ParseContext::PARSE_RANDOM_TEXT, Opm::InputErrorAction::IGNORE);
    parseContext.update(Opm::ParseContext::PARSE_RANDOM_SLASH, Opm::InputErrorAction::IGNORE);

    Opm::ErrorGuard errorGuard;
    const auto deck = Opm::Parser{}.parseFile(path.string(), parseContext, errorGuard);

    if (!deck.hasKeyword("FLUXNUM")) {
        throw std::invalid_argument("no FLUXNUM keyword in '" + path.string() + "'");
    }

    const auto& keyword = deck["FLUXNUM"].back();
    const auto values = keyword.getIntData();

    const auto expected = static_cast<std::size_t>(dims[0]) * dims[1] * dims[2];
    if (values.size() != expected) {
        throw std::invalid_argument("FLUXNUM in '" + path.string() + "' has "
                                    + std::to_string(values.size()) + " values but the parent grid has "
                                    + std::to_string(expected) + " cells");
    }

    return values;
}

//! \brief Region ids requested on the command line, as a sorted unique list.
std::vector<int> parseRegionList(const std::string& spec)
{
    std::vector<int> regions;

    auto text = spec;
    for (char& c : text) {
        if ((c == ',') || (c == ';')) {
            c = ' ';
        }
    }

    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        std::size_t consumed = 0;
        int value = 0;
        try {
            value = std::stoi(token, &consumed);
        }
        catch (const std::exception&) {
            consumed = 0;
        }

        if (consumed != token.size()) {
            throw std::invalid_argument("invalid --regions value '" + token + "' (expected an integer)");
        }

        if (value <= 0) {
            throw std::invalid_argument("invalid --regions value '" + token
                                        + "' (FLUXNUM region ids start at 1; zero means no region)");
        }

        regions.push_back(value);
    }

    if (regions.empty()) {
        throw std::invalid_argument("--regions did not name any region");
    }

    std::sort(regions.begin(), regions.end());
    regions.erase(std::unique(regions.begin(), regions.end()), regions.end());

    return regions;
}

//! \brief Where to write the file for one region.
//!
//! \details A single region keeps the name the caller gave. Several regions
//!   need one file each, so the name becomes a base and the region id is
//!   appended as DUMPFLUX does it, which is also what the consumer's input
//!   selection already looks for.
fs::path outputPathForRegion(const std::string& output,
                             const int regionId,
                             const bool multipleRegions)
{
    if (!multipleRegions) {
        return fs::path{output};
    }

    auto base = fs::path{output};
    if (base.extension() == ".FLUX") {
        base.replace_extension();
    }

    std::ostringstream os;
    os << base.string() << ".FLUX" << std::setw(4) << std::setfill('0') << regionId;

    return fs::path{os.str()};
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

//! \brief Transmissibility of one boundary face, in SI.
//!
//! \details TRANX holds the transmissibility of the face on the cell's POSITIVE
//!   side, so a minus-facing boundary face is described by the array entry of
//!   the cell outside the sector. The arrays are in the deck's units while the
//!   FLUX payload is SI throughout, so convert on the way out.
double transmissibilityForFace(const Opm::FluxRegions::BoundaryFace& face,
                               const CellLookup& cells,
                               const Opm::UnitSystem& unitSystem,
                               const std::vector<double>& tranx,
                               const std::vector<double>& trany,
                               const std::vector<double>& tranz)
{
    const auto requireIndex = [&](const int index, const char* axis) {
        if (index < 0) {
            throw std::invalid_argument(std::string{"mapping reaches the outer parent boundary; "}
                                        + "cannot build pressure-mode FLUX state for " + axis + " face without exterior cell");
        }
        return index;
    };

    const auto value = [&]()
    {
        switch (face.direction) {
        case Opm::FaceDir::XPlus:
            return cells.required(tranx, requireIndex(face.interiorGlobalCell, "X+"), "TRANX");
        case Opm::FaceDir::XMinus:
            return cells.required(tranx, requireIndex(face.exteriorGlobalCell, "X-"), "TRANX");
        case Opm::FaceDir::YPlus:
            return cells.required(trany, requireIndex(face.interiorGlobalCell, "Y+"), "TRANY");
        case Opm::FaceDir::YMinus:
            return cells.required(trany, requireIndex(face.exteriorGlobalCell, "Y-"), "TRANY");
        case Opm::FaceDir::ZPlus:
            return cells.required(tranz, requireIndex(face.interiorGlobalCell, "Z+"), "TRANZ");
        case Opm::FaceDir::ZMinus:
            return cells.required(tranz, requireIndex(face.exteriorGlobalCell, "Z-"), "TRANZ");
        default:
            throw std::invalid_argument("unsupported boundary face direction in mapping");
        }
    }();

    return unitSystem.to_si(Opm::UnitSystem::measure::transmissibility, value);
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

// FLOWS, the component SURFACE VOLUME flux per face, as opposed to FLORES which
// is the phase RESERVOIR volumetric flux. A FLUX file stores component masses,
// and those are FLOWS times the reference density, so this is the array family
// to read. Going the other way, from FLORES, would need the formation volume
// factors and the Rs/Rv of the upwind cell.
std::string flowsArrayName(const Opm::EclIO::FluxFile::Phase phase,
                           const Opm::FaceDir::DirEnum dir)
{
    std::string prefix;
    switch (phase) {
    case Opm::EclIO::FluxFile::Phase::Oil:
        prefix = "FLOOIL";
        break;
    case Opm::EclIO::FluxFile::Phase::Water:
        prefix = "FLOWAT";
        break;
    case Opm::EclIO::FluxFile::Phase::Gas:
        prefix = "FLOGAS";
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

    throw std::invalid_argument("unsupported face direction for FLUX-mode flow array lookup");
}

std::string nncFlowsArrayName(const Opm::EclIO::FluxFile::Phase phase)
{
    switch (phase) {
    case Opm::EclIO::FluxFile::Phase::Oil:
        return "FLOOILN+";
    case Opm::EclIO::FluxFile::Phase::Water:
        return "FLOWATN+";
    case Opm::EclIO::FluxFile::Phase::Gas:
        return "FLOGASN+";
    }

    throw std::invalid_argument("unsupported phase for NNC FLUX-mode flow array lookup");
}

// FLOWS are surface volumes, so gas is measured differently from the liquids.
Opm::UnitSystem::measure surfaceRateMeasure(const Opm::EclIO::FluxFile::Phase phase)
{
    return (phase == Opm::EclIO::FluxFile::Phase::Gas)
        ? Opm::UnitSystem::measure::gas_surface_rate
        : Opm::UnitSystem::measure::liquid_surface_rate;
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

std::optional<RequiredSummaryFallback>
requiredSummaryFallbackFromGroup(const Opm::Group& group)
{
    const auto& prod = group.productionProperties();

    const auto candidatesForMode = [](const Opm::Group::ProductionCMode cmode,
                                      const std::string& groupName)
        -> std::optional<std::vector<std::string>>
    {
        switch (cmode) {
        case Opm::Group::ProductionCMode::ORAT:
            return std::vector<std::string>{"FOPR", "GOPR:" + groupName};
        case Opm::Group::ProductionCMode::WRAT:
            return std::vector<std::string>{"FWPR", "GWPR:" + groupName};
        case Opm::Group::ProductionCMode::GRAT:
            return std::vector<std::string>{"FGPR", "GGPR:" + groupName};
        case Opm::Group::ProductionCMode::LRAT:
            return std::vector<std::string>{"FLPR", "GLPR:" + groupName};
        case Opm::Group::ProductionCMode::RESV:
            return std::vector<std::string>{"FVPR", "GVPR:" + groupName};
        default:
            return std::nullopt;
        }
    };

    const auto makeFallback = [&group, &prod, &candidatesForMode](const Opm::UDAValue& target)
        -> std::optional<RequiredSummaryFallback>
    {
        if (!target.is<std::string>()) {
            return std::nullopt;
        }

        const auto udqName = target.get<std::string>();
        if (udqName.size() < 2 || udqName[1] != 'U') {
            return std::nullopt;
        }

        const auto candidates = candidatesForMode(prod.cmode, group.name());
        if (!candidates) {
            return std::nullopt;
        }

        if (udqName.front() == 'F') {
            auto fieldCandidates = *candidates;
            if (fieldCandidates.size() >= 2U) {
                fieldCandidates[1] = fieldCandidates[1].substr(0, fieldCandidates[1].find(':')) + ":FIELD";
            }
            return RequiredSummaryFallback{udqName, fieldCandidates};
        }

        if (udqName.front() == 'G') {
            std::vector<std::string> groupCandidates;
            groupCandidates.reserve(2);
            if (candidates->size() >= 2U) {
                groupCandidates.push_back((*candidates)[1]);
                groupCandidates.push_back((*candidates)[0]);
            }
            return RequiredSummaryFallback{udqName + ":" + group.name(), groupCandidates};
        }

        return std::nullopt;
    };

    switch (prod.cmode) {
    case Opm::Group::ProductionCMode::ORAT:
        return makeFallback(prod.oil_target);
    case Opm::Group::ProductionCMode::WRAT:
        return makeFallback(prod.water_target);
    case Opm::Group::ProductionCMode::GRAT:
        return makeFallback(prod.gas_target);
    case Opm::Group::ProductionCMode::LRAT:
        return makeFallback(prod.liquid_target);
    case Opm::Group::ProductionCMode::RESV:
        return makeFallback(prod.resv_target);
    default:
        return std::nullopt;
    }
}

std::vector<RequiredSummaryFallback>
collectRequiredSummaryFallbacks(const Opm::Schedule& schedule)
{
    std::vector<RequiredSummaryFallback> required;
    std::unordered_set<std::string> seen;

    for (std::size_t step = 0; step < schedule.size(); ++step) {
        for (const auto& groupName : schedule.groupNames(step)) {
            const auto& group = schedule.getGroup(groupName, step);
            const auto fallback = requiredSummaryFallbackFromGroup(group);
            if (!fallback) {
                continue;
            }

            if (seen.insert(fallback->missingKey).second) {
                required.push_back(*fallback);
            }
        }
    }

    return required;
}

void addMissingSummaryFallbacks(SummaryPayload& payload,
                                const std::vector<RequiredSummaryFallback>& requiredFallbacks,
                                const std::unordered_map<std::string, SummaryDefinition>& defines)
{
    std::unordered_map<std::string, std::size_t> keyIndex;
    keyIndex.reserve(payload.keys.size());
    for (std::size_t i = 0; i < payload.keys.size(); ++i) {
        keyIndex.emplace(payload.keys[i], i);
    }

    for (const auto& fallback : requiredFallbacks) {
        if (keyIndex.count(fallback.missingKey)) {
            continue;
        }

        // An explicit definition wins over the guessed candidates, and may be a
        // constant rather than another vector.
        if (const auto it = defines.find(fallback.missingKey); it != defines.end()) {
            if (it->second.sourceKey.empty()) {
                payload.keys.push_back(fallback.missingKey);
                keyIndex[fallback.missingKey] = payload.keys.size() - 1;
                for (auto& stepValues : payload.valuesPerStep) {
                    stepValues.push_back(it->second.value);
                }
                continue;
            }

            if (const auto src = keyIndex.find(it->second.sourceKey); src != keyIndex.end()) {
                payload.keys.push_back(fallback.missingKey);
                keyIndex[fallback.missingKey] = payload.keys.size() - 1;
                const auto sourceIndex = src->second;
                for (auto& stepValues : payload.valuesPerStep) {
                    stepValues.push_back(stepValues.at(sourceIndex));
                }
                continue;
            }

            throw std::invalid_argument("--smry-define names source '" + it->second.sourceKey
                                        + "' for '" + fallback.missingKey
                                        + "', but the parent summary does not hold it");
        }

        std::optional<std::string> selectedSource;
        for (const auto& sourceKey : fallback.candidateKeys) {
            if (keyIndex.count(sourceKey)) {
                selectedSource = sourceKey;
                break;
            }
        }

        if (!selectedSource) {
            std::ostringstream msg;
            msg << "missing summary key '" << fallback.missingKey << "' needed for UDA/UDQ control reconstruction in FLUX output; "
                << "none of the fallback vectors are available: ";

            for (std::size_t i = 0; i < fallback.candidateKeys.size(); ++i) {
                msg << "'" << fallback.candidateKeys[i] << "'";
                if (i + 1 < fallback.candidateKeys.size()) {
                    msg << ", ";
                }
            }

            msg << ". Add one of these vectors to SUMMARY (or include the original UDQ key), "
                << "or supply it directly with --smry-define="
                << fallback.missingKey << ",<existing_summary_key|constant>.";
            throw std::invalid_argument(msg.str());
        }

        const auto sourceIndex = keyIndex.at(*selectedSource);
        payload.keys.push_back(fallback.missingKey);
        keyIndex[fallback.missingKey] = payload.keys.size() - 1;

        for (auto& stepValues : payload.valuesPerStep) {
            stepValues.push_back(stepValues.at(sourceIndex));
        }
    }
}

//! \brief Apply every --smry-define that has not already been used.
//!
//! \details The fallback pass above only consults them for keys it knows a
//!   group control needs. A user filling a gap that a UDQ expression needs has
//!   no such entry, so the rest are applied here.
void applySummaryDefines(SummaryPayload& payload,
                         const std::unordered_map<std::string, SummaryDefinition>& defines)
{
    std::unordered_map<std::string, std::size_t> keyIndex;
    keyIndex.reserve(payload.keys.size());
    for (std::size_t i = 0; i < payload.keys.size(); ++i) {
        keyIndex.emplace(payload.keys[i], i);
    }

    for (const auto& [target, definition] : defines) {
        if (keyIndex.count(target)) {
            continue;
        }

        double constant = definition.value;
        std::optional<std::size_t> sourceIndex;

        if (!definition.sourceKey.empty()) {
            const auto src = keyIndex.find(definition.sourceKey);
            if (src == keyIndex.end()) {
                throw std::invalid_argument("--smry-define names source '" + definition.sourceKey
                                            + "' for '" + target
                                            + "', but the parent summary does not hold it");
            }
            sourceIndex = src->second;
        }

        payload.keys.push_back(target);
        keyIndex[target] = payload.keys.size() - 1;

        for (auto& stepValues : payload.valuesPerStep) {
            stepValues.push_back(sourceIndex ? stepValues.at(*sourceIndex) : constant);
        }
    }
}

//! \brief Refuse to write a file whose embedded summary cannot drive the deck.
//!
//! \details A reduced run evaluates the deck's UDQ expressions and ACTIONX
//!   conditions through the ordinary code paths, so it needs the vectors they
//!   refer to. A live DUMPFLUX run can produce any of them from its own
//!   SummaryState; this route has only what the parent's SUMMARY section asked
//!   for. Saying which UDQ wants each missing vector turns an obscure failure
//!   into something the user can act on.
void requireSummaryKeysForUdqs(const Opm::Schedule& schedule,
                               const Opm::SummaryConfig& summaryConfig,
                               const SummaryPayload& payload,
                               const std::unordered_map<std::string, SummaryDefinition>& defines)
{
    // A requirement is a bare KEYWORD while the parent's summary holds
    // qualified keys, so WBHP is satisfied by WBHP:B-2H and GEFF by GEFF:MANI-B.
    // Comparing the two directly reports everything that names an object as
    // missing, which is most of them.
    std::unordered_set<std::string> availableKeywords;
    std::unordered_set<std::string> availableKeys(payload.keys.begin(), payload.keys.end());

    const auto addAvailable = [&availableKeywords](const std::string& key)
    {
        availableKeywords.emplace(Opm::fluxSummaryKeywordOf(key));
    };

    for (const auto& key : payload.keys) {
        addAvailable(key);
    }
    for (const auto& [target, definition] : defines) {
        (void) definition;
        availableKeys.insert(target);
        addAvailable(target);
    }

    // Missing keyword -> the UDQs that refer to it.
    std::map<std::string, std::set<std::string>> missing;

    const auto note = [&missing, &availableKeywords, &availableKeys]
        (const std::string& keyword, const std::string& owner)
    {
        if (keyword.empty()
            || (availableKeywords.count(keyword) != 0)
            || (availableKeys.count(keyword) != 0))
        {
            return;
        }

        missing[keyword].insert(owner);
    };

    for (const auto& udq : schedule.unique<Opm::UDQConfig>()) {
        for (const auto& define : udq.second.definitions()) {
            std::unordered_set<std::string> keys;
            define.required_summary(keys);
            for (const auto& key : keys) {
                note(key, define.keyword());
            }
        }
    }

    for (const auto& action : schedule.back().actions.get()) {
        std::unordered_set<std::string> keys;
        action.required_summary(keys);
        for (const auto& key : keys) {
            note(key, "ACTIONX " + action.name());
        }
    }

    // A requirement that names its object in full is a different matter. The
    // keyword being present on some other object says nothing about whether
    // this one is: RPR__REC on regions 1 and 2 does not make RPR__REC:3
    // available, and a reduced run that asks for it aborts with
    //
    //   Summary vector RPR__REC:3 is unknown
    //
    // which says nothing about where it should have come from.
    //
    // Only worth raising for a vector the reduced run does not produce for
    // itself. It computes whatever its own SUMMARY section asks for, and needs
    // the parent only for what falls outside that.
    auto namedVectors = Opm::RequisiteSummaryVectors{};
    for (const auto& udq : schedule.unique<Opm::UDQConfig>()) {
        udq.second.requisiteSummaryVectors(namedVectors);
    }
    for (const auto& action : schedule.back().actions.get()) {
        action.requisiteSummaryVectors(namedVectors);
    }

    std::set<std::string> missingKeys;

    for (const auto& vector : namedVectors) {
        using Cat = Opm::EclIO::SummaryNode::Category;

        switch (Opm::EclIO::SummaryNode::category_from_keyword(vector.keyword)) {
        case Cat::Well:
        case Cat::Group:
        case Cat::Region:
        case Cat::Segment:
        case Cat::Node:
            break;

        default:
            // A block or connection reference names a cell by I, J and K while
            // the summary names it by global index, so the two spellings
            // cannot be compared here.
            continue;
        }

        auto key = vector.keyword;
        for (const auto& argument : vector.arguments) {
            key += ':';
            key += argument;
        }

        if ((availableKeys.count(key) != 0) || summaryConfig.hasSummaryKey(key)) {
            continue;
        }

        missingKeys.insert(key);
    }

    if (missing.empty() && missingKeys.empty()) {
        return;
    }

    std::ostringstream msg;

    if (!missing.empty()) {
        msg << "the parent summary does not hold " << missing.size()
            << " vector(s) the deck's UDQ and ACTIONX expressions refer to, so a reduced "
               "run could not evaluate them:\n";

        for (const auto& [keyword, owners] : missing) {
            msg << "  " << keyword << "  needed by ";
            auto first = true;
            for (const auto& owner : owners) {
                if (!first) {
                    msg << ", ";
                }
                msg << owner;
                first = false;
            }
            msg << '\n';
        }

        msg << "These are keywords; any of the parent's vectors using one of them counts,\n"
               "so WBHP is satisfied by WBHP on any well.\n";
    }

    if (!missingKeys.empty()) {
        msg << "the deck's UDQ and ACTIONX expressions name " << missingKeys.size()
            << " vector(s) outright which neither the parent summary nor this deck's own\n"
               "SUMMARY section provides, so a reduced run would abort on the first one:\n";

        for (const auto& key : missingKeys) {
            msg << "  " << key << '\n';
        }

        msg << "These are whole keys; another object under the same keyword will not do.\n";
    }

    msg << "Resolve this by adding the vectors to the parent's SUMMARY section and "
           "rerunning it -- FLUXALL asks for everything this route needs -- or by "
           "supplying each one on the command line with\n"
           "  --smry-define=<TARGET>,<EXISTING_KEY>   to copy another vector, or\n"
           "  --smry-define=<TARGET>,<NUMBER>         to use a constant.";

    throw std::invalid_argument(msg.str());
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
                          const CellLookup& cells,
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

        const auto exterior = face.exteriorGlobalCell;
        step.pressures.push_back(unitSystem.to_si(Opm::UnitSystem::measure::pressure,
                              cells.required(pressure, exterior, "PRESSURE")));

        if (waterActive) {
            step.swat.push_back(cells.required(swat, exterior, "SWAT"));
        }
        if (gasActive) {
            step.sgas.push_back(cells.required(sgas, exterior, "SGAS"));
        }
        if (includeRs) {
            step.rs.push_back(cells.valueOr(rs, exterior));
        }
        if (includeRv) {
            step.rv.push_back(cells.valueOr(rv, exterior));
        }
        if (hasTemperature) {
            step.temperature.push_back(cells.valueOr(temperature, exterior));
        }
    }
}

// Reference density of one component, in kg/sm3, for the PVT region a cell
// belongs to.
//
// A FLUX file stores component MASSES, and FLOWS gives component SURFACE
// VOLUMES, so the two differ only by this. Which is the whole reason this tool
// reads FLOWS rather than FLORES: turning a phase volumetric flux into
// component masses needs the formation volume factors and the Rs/Rv of the
// upwind cell, and this tool has no PVT evaluation at all.
class ReferenceDensities
{
public:
    explicit ReferenceDensities(const Opm::EclipseState& eclState)
        : pvtnum_(eclState.fieldProps().get_global_int("PVTNUM"))
    {
        const auto& densities = eclState.getTableManager().getDensityTable();
        this->oil_.reserve(densities.size());
        this->water_.reserve(densities.size());
        this->gas_.reserve(densities.size());

        for (const auto& record : densities) {
            this->oil_.push_back(record.oil);
            this->water_.push_back(record.water);
            this->gas_.push_back(record.gas);
        }

        if (this->oil_.empty()) {
            throw std::invalid_argument("parent deck has no DENSITY table, so component "
                                        "masses cannot be formed for the FLUX file");
        }
    }

    double operator()(const Opm::EclIO::FluxFile::Phase phase, const int globalCell) const
    {
        auto region = std::size_t{0};
        if (const auto cell = static_cast<std::size_t>(globalCell); cell < this->pvtnum_.size()) {
            region = static_cast<std::size_t>(std::max(this->pvtnum_[cell] - 1, 0));
        }

        const auto& table = (phase == Opm::EclIO::FluxFile::Phase::Oil)
            ? this->oil_
            : ((phase == Opm::EclIO::FluxFile::Phase::Gas) ? this->gas_ : this->water_);

        return table[std::min(region, table.size() - 1)];
    }

private:
    std::vector<int> pvtnum_;
    std::vector<double> oil_;
    std::vector<double> water_;
    std::vector<double> gas_;
};

void fillFluxStepData(Opm::FluxDumper::ReportStepData& step,
                      const Opm::FluxRegions::Region& region,
                      Opm::EclIO::ERst& restart,
                      const int reportStep,
                      const int phaseMask,
                      const std::map<NncKey, int>& nncPairToIndex,
                      const Opm::UnitSystem& unitSystem,
                      const CellLookup& cells,
                      const ReferenceDensities& referenceDensity)
{
    step.massRates.clear();

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

            // The reference density is that of the INTERIOR cell, matching what
            // the live DUMPFLUX path uses, so that a consumer converting back to
            // surface volumes with its own densities makes the round trip exact.
            const auto density = referenceDensity(phase, face.interiorGlobalCell);
            const auto measure = surfaceRateMeasure(phase);

            if (face.isNnc) {
                const auto key = normalizedNncPair(face.interiorGlobalCell, face.exteriorGlobalCell);
                const auto indexIt = nncPairToIndex.find(key);
                if (indexIt == nncPairToIndex.end()) {
                    throw std::invalid_argument("NNC boundary face not found in parent NNC list");
                }

                const auto& values = loadArray(nncFlowsArrayName(phase));
                const auto nncIndex = static_cast<std::size_t>(indexIt->second);
                if (nncIndex >= values.size()) {
                    throw std::invalid_argument("NNC flow array is smaller than expected for parent NNC list");
                }

                const auto nncFlux = unitSystem.to_si(measure, values[nncIndex]) * density;
                step.massRates.push_back((face.interiorGlobalCell == key.second) ? nncFlux : -nncFlux);
                continue;
            }

            const auto& values = loadArray(flowsArrayName(phase, face.direction));
            const auto flow = cells.required(values, face.interiorGlobalCell,
                                             flowsArrayName(phase, face.direction));

            // Restart directional face rates use the interior-cell face orientation.
            // FLUX files store positive values into the sector, i.e. opposite sign.
            // The restart arrays are written in the deck's output units while the
            // FLUX payload is SI, so convert here.
            step.massRates.push_back(-unitSystem.to_si(measure, flow) * density);
        }
    }
}

// Narrow an embedded summary payload down to the vectors a reduced run needs.
//
// Selection is by KEYWORD, so every vector the parent wrote under a wanted
// keyword is kept whatever object it names. Region, segment and block
// quantities can only be caught this way: nothing but the parent's own summary
// says which regions or segments it reported on.
//
// Keys the parent did not write are dropped rather than faked: a reduced run
// treats a key it cannot find as absent and falls back on its own evaluation,
// which is better than handing it a zero. The UDQ fallbacks have already been
// resolved by this point, so anything still missing really is unavailable.
void retainSummaryKeys(SummaryPayload& payload, const std::vector<std::string>& wantedKeywords)
{
    const auto keep = std::unordered_set<std::string>(wantedKeywords.begin(),
                                                      wantedKeywords.end());

    std::vector<std::size_t> retained;
    retained.reserve(payload.keys.size());
    for (std::size_t i = 0; i < payload.keys.size(); ++i) {
        if (keep.count(std::string{Opm::fluxSummaryKeywordOf(payload.keys[i])}) != 0) {
            retained.push_back(i);
        }
    }

    if (retained.size() == payload.keys.size()) {
        return;
    }

    std::vector<std::string> keys;
    keys.reserve(retained.size());
    for (const auto i : retained) {
        keys.push_back(payload.keys[i]);
    }
    payload.keys = std::move(keys);

    for (auto& stepValues : payload.valuesPerStep) {
        std::vector<double> values;
        values.reserve(retained.size());
        for (const auto i : retained) {
            values.push_back(stepValues[i]);
        }
        stepValues = std::move(values);
    }
}

//! \brief Pore-volume weighted sums over the cells OUTSIDE the sector.
//!
//! \details A reduced run converts a reservoir-volume target to surface rates
//!   with averages taken over one region covering its whole grid, which in a
//!   sector is only the sector. Adding these sums back gives it the parent's
//!   averages instead, so a well on RESV control gets the target it had in the
//!   full model. The layout is eight values weighted by hydrocarbon pore volume
//!   followed by eight weighted by total pore volume, each being pressure,
//!   temperature, rs, rv, rsw, rvw, pore volume and salt concentration, matching
//!   RateConverter's own accumulation.
//!
//!   The weighting uses RPORV when the parent wrote it, since that is the pore
//!   volume the simulator actually saw at that step. Falling back to the INIT
//!   PORV ignores rock compressibility, which is a small error in the weights
//!   and none at all in the averages when the rock is stiff.
std::array<double, 16>
computeExternalRegionSums(const Opm::FluxRegions::Region& region,
                          const Opm::UnitSystem& unitSystem,
                          const CellLookup& cells,
                          const std::vector<double>& poreVolume,
                          const std::vector<double>& pressure,
                          const std::vector<double>& swat,
                          const std::vector<double>& rs,
                          const std::vector<double>& rv,
                          const std::vector<double>& temperature,
                          const std::array<int, 3>& dims)
{
    std::array<double, 16> sums{};

    std::vector<char> inRegion(static_cast<std::size_t>(dims[0]) * dims[1] * dims[2], 0);
    for (const auto cell : region.selectedGlobalCells) {
        if ((cell >= 0) && (static_cast<std::size_t>(cell) < inRegion.size())) {
            inRegion[cell] = 1;
        }
    }

    for (std::size_t cell = 0; cell < inRegion.size(); ++cell) {
        if (inRegion[cell] != 0) {
            continue;
        }

        const auto global = static_cast<int>(cell);

        // An inactive cell has no entry in the restart arrays and no pore
        // volume to contribute, so it drops out here.
        const auto pv = unitSystem.to_si(Opm::UnitSystem::measure::volume,
                                         cells.valueOr(poreVolume, global));
        if (!(pv > 0.0)) {
            continue;
        }

        const auto p = unitSystem.to_si(Opm::UnitSystem::measure::pressure,
                                        cells.valueOr(pressure, global));
        const auto t = temperature.empty()
            ? 0.0
            : unitSystem.to_si(Opm::UnitSystem::measure::temperature,
                               cells.valueOr(temperature, global));
        const auto rsCell = unitSystem.to_si(Opm::UnitSystem::measure::gas_oil_ratio,
                                             cells.valueOr(rs, global));
        const auto rvCell = unitSystem.to_si(Opm::UnitSystem::measure::oil_gas_ratio,
                                             cells.valueOr(rv, global));

        // Only the oil and gas filled part of a cell counts towards the
        // hydrocarbon weighting, exactly as RateConverter does it.
        const auto hydrocarbon = swat.empty() ? 1.0 : (1.0 - cells.valueOr(swat, global));
        const auto hpv = pv * hydrocarbon;

        if (hpv > 0.0) {
            sums[0] += p * hpv;
            sums[1] += t * hpv;
            sums[2] += rsCell * hpv;
            sums[3] += rvCell * hpv;
            // rsw and rvw stay zero: a restart file does not carry them unless
            // the corresponding options are active, and this route does not
            // support those.
            sums[6] += hpv;
        }

        sums[8] += p * pv;
        sums[9] += t * pv;
        sums[10] += rsCell * pv;
        sums[11] += rvCell * pv;
        sums[14] += pv;
    }

    return sums;
}

int run(const Options& opt)
{
    if (opt.parent.empty()) {
        throw std::invalid_argument("missing required argument --parent=<CASE|CASE.DATA>");
    }
    const bool hasMappingFile = !opt.mapping.empty();
    const bool hasInlineMapping = !opt.mappingInline.empty();
    const bool hasFluxnum = !opt.fluxnum.empty();

    const auto selectorCount = static_cast<int>(hasMappingFile)
        + static_cast<int>(hasInlineMapping)
        + static_cast<int>(hasFluxnum);

    if (selectorCount == 0) {
        throw std::invalid_argument("missing required region argument (--fluxnum=<file.grdecl> with "
                                    "--regions=<list>, --mapping=<file> or --mapping-inline=<spec>)");
    }
    if (selectorCount > 1) {
        throw std::invalid_argument("region selection is ambiguous; pass exactly one of "
                                    "--fluxnum, --mapping or --mapping-inline");
    }
    if (hasFluxnum && opt.regions.empty()) {
        throw std::invalid_argument("--fluxnum requires --regions=<n[,n...]>");
    }
    if (!hasFluxnum && !opt.regions.empty()) {
        throw std::invalid_argument("--regions only applies to --fluxnum; the mapping routes "
                                    "describe a single region themselves");
    }
    if (opt.noSummary && !opt.summary.empty()) {
        throw std::invalid_argument("summary options are ambiguous; pass either --summary=... or --no-summary");
    }
    if (opt.output.empty()) {
        throw std::invalid_argument("missing required argument --output=<SECTOR.FLUX>");
    }
    if (!validMode(opt.mode)) {
        throw std::invalid_argument("invalid --mode value '" + opt.mode + "' (expected flux|pressure|both)");
    }
    if (hasFluxnum && (opt.mode != "pressure")) {
        // Flux mode would need the component fluxes across each boundary face,
        // which only a run that was asked for them writes to its restart file.
        // The whole point of this route is to need nothing of the parent but an
        // ordinary restart.
        throw std::invalid_argument("--fluxnum supports --mode=pressure only; flux and both need "
                                    "FLOWS restart arrays, which an ordinary parent run does not write");
    }
    if (!validSampling(opt.sampling)) {
        throw std::invalid_argument("invalid --sampling value '" + opt.sampling + "' (expected averaged|instant)");
    }

    const ParentInput parentInput = resolveParentInput(opt);
    Opm::OpmLog::setupSimpleDefaultLogging();

    const auto deck = loadDeck(parentInput.deckPath, opt.parsingStrictness);
    const Opm::EclipseState state(deck);
    const Opm::Schedule schedule(deck, state);

    // The consumer runs this same deck, so its SUMMARY section says which
    // vectors the reduced run will compute for itself and which it has to be
    // given.
    const auto summaryConfig = [&deck, &state, &schedule]()
    {
        auto parseContext = Opm::ParseContext{};
        auto errors = Opm::ErrorGuard{};

        return Opm::SummaryConfig {
            deck, schedule, state.fieldProps(), state.aquifer(),
            parseContext, errors
        };
    }();
    const auto requiredFallbacks = collectRequiredSummaryFallbacks(schedule);
    const auto summaryDefines = parseSummaryDefines(opt);
    const auto fluxMode = modeFromString(opt.mode);
    const auto dims = gridDims(state);
    const auto& unitSystem = state.getDeckUnitSystem();
    const ReferenceDensities referenceDensity(state);

    std::vector<int> regionValues;
    std::vector<int> requestedRegions;

    if (hasFluxnum) {
        if (deck.hasKeyword("FLUXNUM")) {
            Opm::OpmLog::warning("The parent deck also defines FLUXNUM. The array from '"
                                 + opt.fluxnum + "' is the one being used.");
        }

        regionValues = readFluxnumFile(opt.fluxnum, dims);
        requestedRegions = parseRegionList(opt.regions);

        if (state.runspec().hysterPar().active()) {
            Opm::OpmLog::warning("The parent deck enables hysteresis. The exterior relative "
                                 "permeability and capillary pressure written here come from the "
                                 "saturation functions alone, because a restart file does not "
                                 "carry the parent's converged phase pressures, so they follow "
                                 "the drainage curves and will not match a DUMPFLUX file.");
        }
    }
    else {
        regionValues = parseMappingSpecification(opt, dims);
        requestedRegions.push_back(1);
    }

    // The region map is given over every cell of the grid, so it also assigns a
    // region to inactive cells. Hand ACTNUM to the extraction so that this
    // tool builds the same region as an in-simulator DUMPFLUX run would.
    const auto actnum = parentActnum(parentInput.gridPath, state, dims);
    const auto regions = Opm::FluxRegions::extract(dims, regionValues, actnum, {});
    if (regions.empty()) {
        throw std::invalid_argument("the region definition selected no cells");
    }

    // Everything from here on is addressed by global cartesian index, while the
    // parent's restart holds one entry per active cell. This is the translation.
    const CellLookup cells(dims, actnum);

    std::vector<const Opm::FluxRegions::Region*> selectedRegions;
    for (const auto wanted : requestedRegions) {
        const auto it = std::find_if(regions.begin(), regions.end(),
                                     [wanted](const auto& candidate)
                                     { return candidate.regionId == wanted; });

        if (it == regions.end()) {
            std::ostringstream msg;
            msg << "region " << wanted << " is not present in the region definition; it holds ";
            for (std::size_t i = 0; i < regions.size(); ++i) {
                msg << regions[i].regionId << ((i + 1 < regions.size()) ? ", " : "");
            }
            throw std::invalid_argument(msg.str());
        }

        if (it->boundaryFaces.empty()) {
            throw std::invalid_argument("region " + std::to_string(wanted)
                                        + " produced no boundary faces");
        }

        selectedRegions.push_back(&*it);
    }

    if (!hasFluxnum && (selectedRegions.size() != 1U)) {
        throw std::invalid_argument("mapping must define exactly one connected region");
    }

    const bool multipleRegions = selectedRegions.size() > 1U;
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
    cells.require(tranx, "TRANX");
    cells.require(trany, "TRANY");
    cells.require(tranz, "TRANZ");

    const bool waterActive = (phaseMaskValue & static_cast<int>(Opm::EclIO::FluxFile::Phase::Water)) != 0;
    const bool gasActive = (phaseMaskValue & static_cast<int>(Opm::EclIO::FluxFile::Phase::Gas)) != 0;
    const bool includeRs = true;
    const bool includeRv = true;
    const bool hasTemperature = restart.hasArray("TEMP", reportSteps.front());

    const auto pvtnum = state.fieldProps().get_global_int("PVTNUM");

    // Reference pore volume, used to weight the external region sums when the
    // parent did not write the dynamic one.
    std::vector<double> staticPoreVolume;
    if (hasPressureMode(fluxMode) && parentInput.initPath) {
        Opm::EclIO::EclFile initFile(parentInput.initPath->string(), /*preload=*/false);
        if (initFile.hasKey("PORV")) {
            staticPoreVolume = loadNumericArray(initFile, "PORV");
            cells.require(staticPoreVolume, "PORV");
        }
    }

    std::vector<Opm::FluxDumper> dumpers;
    std::vector<fs::path> outputPaths;
    dumpers.reserve(selectedRegions.size());
    outputPaths.reserve(selectedRegions.size());

    for (const auto* regionPtr : selectedRegions) {
        const auto& region = *regionPtr;

        std::vector<double> boundaryTransmissibilities;
        boundaryTransmissibilities.reserve(region.boundaryFaces.size());
        for (const auto& face : region.boundaryFaces) {
            boundaryTransmissibilities.push_back(transmissibilityForFace(face, cells, unitSystem,
                                                                         tranx, trany, tranz));
        }

        auto& dumper = dumpers.emplace_back(parentInput.rootPath.filename().string(),
                                            region.regionId,
                                            dims,
                                            region,
                                            fluxMode,
                                            samplingFromString(opt.sampling),
                                            phaseMaskValue,
                                            boundaryTransmissibilities);

        // The PVT region of the cell on the far side of each face. Without it a
        // reduced run evaluates an inflowing stream with its own region, which
        // is the interior one and need not be the same.
        std::vector<int> exteriorPvtRegions;
        exteriorPvtRegions.reserve(region.boundaryFaces.size());
        for (const auto& face : region.boundaryFaces) {
            const auto cell = static_cast<std::size_t>(face.exteriorGlobalCell);
            const auto value = (face.exteriorGlobalCell >= 0) && (cell < pvtnum.size())
                ? std::max(pvtnum[cell] - 1, 0)
                : 0;
            exteriorPvtRegions.push_back(value);
        }
        dumper.setBoundaryExteriorPvtRegions(exteriorPvtRegions);

        outputPaths.push_back(outputPathForRegion(opt.output, region.regionId, multipleRegions));
    }

    std::optional<SummaryPayload> summaryPayload;
    std::size_t restartStepStartIndex = 0;
    bool pairSummaryByPosition = true;
    if (parentInput.summaryPath) {
        summaryPayload = loadSummaryPayload(*parentInput.summaryPath);
        addMissingSummaryFallbacks(*summaryPayload, requiredFallbacks, summaryDefines);
        applySummaryDefines(*summaryPayload, summaryDefines);

        // Refuse to write a file a reduced run could not drive. This is checked
        // before narrowing the payload, so that a vector the parent did write
        // counts as available even if it is not one this file will carry.
        requireSummaryKeysForUdqs(schedule, summaryConfig, *summaryPayload, summaryDefines);

        // Keep the same vectors a live DUMPFLUX run would embed rather than
        // the whole of the parent's summary. Which vectors those are depends on
        // the deck's UDQ DEFINE expressions and ACTIONX conditions, so the
        // selection is shared with the simulator instead of being guessed here.
        //
        // Selected by keyword rather than by fully qualified key. A live run
        // can enumerate the wells and groups a keyword covers, but nothing can
        // enumerate the regions or segments the parent chose to report on, and
        // those vectors are just as needed. Matching the keyword keeps whatever
        // the parent wrote under it.
        const auto& phases = state.runspec().phases();
        const auto wantedKeywords = Opm::fluxSummaryKeywords(schedule,
                                                             phases.active(Opm::Phase::OIL),
                                                             phases.active(Opm::Phase::WATER),
                                                             phases.active(Opm::Phase::GAS));

        const auto available = summaryPayload->keys.size();
        retainSummaryKeys(*summaryPayload, wantedKeywords);

        std::cout << "Embedding " << summaryPayload->keys.size()
                  << " of the parent's " << available << " summary vectors, drawn from "
                  << wantedKeywords.size() << " keyword(s) a reduced run can use\n";

        // The two sequences describe the same run but need not line up. When
        // they do, pair them by position: that covers a parent writing a
        // restart at every report step, and the degenerate case of a parent
        // holding only its initial state, where the one restart record is the
        // only description of the boundary there is and belongs against the
        // first summary step.
        //
        // When they do not, pair by report step number instead. A deck is
        // under no obligation to write a restart at every report step --
        // RPTRST with a FREQ, or BASIC=4 or 5, writes them at a subset -- and
        // then the sequences have different lengths and the same position
        // means a different time in each. Summary report step N sits at index
        // N-1; report step 0 is the initial state, which the summary does not
        // cover, and which the steps that follow make redundant.
        const auto summarySteps = summaryPayload->reportTimes.size();

        if (summarySteps == reportSteps.size()) {
            restartStepStartIndex = 0;
            pairSummaryByPosition = true;
        }
        else if ((summarySteps + 1 == reportSteps.size()) && (reportSteps.front() == 0)) {
            restartStepStartIndex = 1;
            pairSummaryByPosition = true;
        }
        else {
            pairSummaryByPosition = false;
            restartStepStartIndex = (reportSteps.front() == 0) ? 1 : 0;

            if (reportSteps.back() > static_cast<int>(summarySteps)) {
                std::ostringstream msg;
                msg << "the parent's restart file runs to report step " << reportSteps.back()
                    << " but its summary stops at report step " << summarySteps
                    << ", so there is no time or summary sample to go with the "
                       "later restart step(s).\n"
                       "The two files are from different runs, or the summary "
                       "was truncated.";
                throw std::invalid_argument(msg.str());
            }

            // Worth saying: the boundary is described only where the parent
            // wrote a restart, so a reduced run gets a coarser history than
            // the parent's own report steps would suggest.
            std::cout << "Note: the parent wrote "
                      << (reportSteps.size() - restartStepStartIndex)
                      << " restart step(s) over " << summarySteps
                      << " report step(s), so the boundary is described at that "
                         "cadence.\n"
                         "      Rerun the parent with RPTRST BASIC=2 for a record "
                         "at every report step.\n";
        }

        for (auto& dumper : dumpers) {
            dumper.setSummaryKeys(summaryPayload->keys);
        }
    }

    // Steps outer, regions inner, so each restart array is read once however
    // many regions were asked for.
    double previousTime = 0.0;
    for (std::size_t stepIdx = restartStepStartIndex; stepIdx < reportSteps.size(); ++stepIdx) {
        const int sourceReportStep = reportSteps[stepIdx];

        // Summary report step N is at index N-1 when the pairing goes by step
        // number; by position the two sequences advance together.
        const auto summaryStepIdx = pairSummaryByPosition
            ? (stepIdx - restartStepStartIndex)
            : static_cast<std::size_t>(sourceReportStep - 1);

        const int reportStep = summaryPayload
            ? static_cast<int>(summaryStepIdx + 1)
            : sourceReportStep;
        const int simStep = summaryPayload
            ? static_cast<int>(summaryStepIdx + 1)
            : sourceReportStep;

        std::vector<double> pressure, swat, sgas, rs, rv, temperature;
        std::vector<double> poreVolume;
        if (hasPressureMode(fluxMode)) {
            pressure = requiredRestartArray(restart, "PRESSURE", sourceReportStep);
            cells.require(pressure, "PRESSURE");
            swat = waterActive ? requiredRestartArray(restart, "SWAT", sourceReportStep) : std::vector<double>{};
            sgas = gasActive ? requiredRestartArray(restart, "SGAS", sourceReportStep) : std::vector<double>{};
            rs = optionalRestartArray(restart, "RS", sourceReportStep);
            rv = optionalRestartArray(restart, "RV", sourceReportStep);
            temperature = hasTemperature ? requiredRestartArray(restart, "TEMP", sourceReportStep) : std::vector<double>{};

            poreVolume = optionalRestartArray(restart, "RPORV", sourceReportStep);
            if (poreVolume.empty()) {
                poreVolume = staticPoreVolume;
            }
        }

        double currentTime = previousTime;
        if (summaryPayload) {
            currentTime = summaryPayload->reportTimes[summaryStepIdx] * secondsPerDay;
        }

        for (std::size_t r = 0; r < dumpers.size(); ++r) {
            const auto& region = *selectedRegions[r];
            auto& dumper = dumpers[r];

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
                                 nncPairToIndex,
                                 unitSystem,
                                 cells,
                                 referenceDensity);
            }

            if (hasPressureMode(fluxMode)) {
                fillPressureStepData(step,
                                     region,
                                     unitSystem,
                                     cells,
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

                if (!poreVolume.empty()) {
                    const auto sums = computeExternalRegionSums(region,
                                                                unitSystem,
                                                                cells,
                                                                poreVolume,
                                                                pressure,
                                                                swat,
                                                                rs,
                                                                rv,
                                                                temperature,
                                                                dims);
                    step.externalRegionSums.assign(sums.begin(), sums.end());
                }
            }

            if (summaryPayload) {
                step.startTime = previousTime;
                step.stepLength = currentTime - previousTime;

                // Summary samples form their own series; emit one per parent
                // step. Taking every available step means each stored rate is
                // already the parent's average over that step, which is what
                // the format requires of rate-type entries.
                if (!summaryPayload->keys.empty()) {
                    dumper.appendSummarySample(currentTime,
                                               summaryPayload->valuesPerStep[summaryStepIdx]);
                }
            }

            dumper.appendReportStep(step);
        }

        previousTime = currentTime;
    }

    for (std::size_t r = 0; r < dumpers.size(); ++r) {
        dumpers[r].flush(outputPaths[r].string(), /*formatted=*/false);

        std::cout << "Wrote " << opt.mode << "-mode FLUX file '" << outputPaths[r].string()
                  << "' for region " << selectedRegions[r]->regionId << " with "
                  << selectedRegions[r]->boundaryFaces.size() << " boundary faces and "
                  << (reportSteps.size() - restartStepStartIndex) << " report steps\n";
    }

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
