/*
  Copyright 2026 Equinor ASA.

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

#define BOOST_TEST_MODULE OPM_test_FluxSummaryKeys

#include <boost/test/unit_test.hpp>

#include <opm/input/eclipse/Deck/Deck.hpp>
#include <opm/input/eclipse/EclipseState/EclipseState.hpp>
#include <opm/input/eclipse/Parser/Parser.hpp>
#include <opm/input/eclipse/Python/Python.hpp>
#include <opm/input/eclipse/Schedule/Schedule.hpp>
#include <opm/simulators/flow/flux/FluxSummaryKeys.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

Opm::Schedule makeSchedule()
{
    const auto deck = Opm::Parser{}.parseString(R"(
RUNSPEC
OIL
METRIC
DIMENS
 3 1 1 /
TABDIMS
 1 1 40 20 1 20 /
WELLDIMS
 2 10 2 2 /
UDQDIMS
 10 2 0 10 10 10 0 10 /
ACTDIMS
 4 40 20 20 /
GRID
DX
 3*1 /
DY
 3*1 /
DZ
 3*1 /
TOPS
 3*0 /
PORO
 3*0.2 /
PERMX
 3*100 /
PERMY
 3*100 /
PERMZ
 3*100 /
PROPS
ROCK
 14.7 3E-6 /
PVDO
 14.7 1.062 1.04 /
DENSITY
 53.66 64.49 0.0533 /
SOLUTION
PRESSURE
 3*300 /
SCHEDULE
WELSPECS
 'P1' 'G1' 1 1 1* 'OIL' /
/
COMPDAT
 'P1' 1 1 1 1 'OPEN' 1* 1.0 /
/
UDQ
 DEFINE FURPR RPR 1 /
 UNITS  FURPR BARSA /
/
ACTIONX
 ACT1 10 1 /
 WBHP 'P1' < 100.0 /
/
WELOPEN
 'P1' 'SHUT' /
/
ENDACTIO
TSTEP
 1 /
ACTIONX
 ACT1 10 1 /
 WGOR 'P1' > 100.0 /
/
WELOPEN
 'P1' 'SHUT' /
/
ENDACTIO
TSTEP
 1 /
END
)");

    const auto es = Opm::EclipseState{deck};
    return Opm::Schedule{deck, es, std::make_shared<const Opm::Python>()};
}

bool contains(const std::vector<std::string>& keys, const std::string& key)
{
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

} // Anonymous namespace

BOOST_AUTO_TEST_CASE(EveryVersionOfARedefinedAction)
{
    const auto keys = Opm::fluxSummaryKeys(makeSchedule(), true, false, false, {});

    BOOST_CHECK(contains(keys, "WBHP:P1"));
    BOOST_CHECK(contains(keys, "WGOR:P1"));
}

BOOST_AUTO_TEST_CASE(UnsuffixedRegionKeywordCoversEveryRegionSet)
{
    const auto available = std::vector<std::string> {
        "RPR:1", "RPR__XYZ:1", "ROIP__XYZ:1",
    };

    const auto keys = Opm::fluxSummaryKeys(makeSchedule(), true, false, false, available);

    BOOST_CHECK(contains(keys, "RPR:1"));
    BOOST_CHECK(contains(keys, "RPR__XYZ:1"));
    BOOST_CHECK(!contains(keys, "ROIP__XYZ:1"));
}
