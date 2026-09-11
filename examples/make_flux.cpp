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

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
    std::string parent;
    std::string mapping;
    std::string output;
    std::string mode = "flux";
    std::string sampling = "averaged";
    std::string summary;
    bool help = false;
};

void printUsage()
{
    std::cout
        << "usage: make_flux --parent=<CASE|CASE.DATA> --mapping=<file> --output=<SECTOR.FLUX> [options]\n"
        << "\n"
        << "options:\n"
        << "  --mode=<flux|pressure|both>         Boundary payload mode (default: flux)\n"
        << "  --sampling=<averaged|instant>        Flux sampling mode (default: averaged)\n"
        << "  --summary=<CASE>                     Optional parent summary override\n"
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

    std::cerr
        << "make_flux: bootstrap CLI is wired, but full standalone FLUX generation is not implemented yet.\n"
        << "requested parent='" << opt.parent
        << "' mapping='" << opt.mapping
        << "' output='" << opt.output
        << "' mode='" << opt.mode
        << "' sampling='" << opt.sampling
        << "'\n";

    return 2;
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
