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

#include <opm/simulators/flow/flux/FluxSummaryKeys.hpp>

#include <opm/input/eclipse/Schedule/Action/ActionX.hpp>
#include <opm/input/eclipse/Schedule/Action/Actions.hpp>
#include <opm/input/eclipse/Schedule/Schedule.hpp>
#include <opm/input/eclipse/Schedule/UDQ/UDQConfig.hpp>

#include <opm/io/eclipse/SummaryNode.hpp>

#include <algorithm>
#include <initializer_list>
#include <unordered_set>

namespace Opm {

std::vector<std::string> fluxSummaryKeys(const Schedule& schedule,
                                         const bool oil,
                                         const bool water,
                                         const bool gas)
{
    auto keywords = std::unordered_set<std::string>{};

    const auto addAll = [&keywords](std::initializer_list<const char*> names)
    {
        for (const auto* name : names) {
            keywords.insert(name);
        }
    };

    // Surface rates and cumulatives for the conserved quantities of every
    // active phase. A USEFLUX run needs these to reconstruct the contribution
    // of wells that fall outside the sector.
    if (oil) {
        addAll({"WOPR", "WOPT", "WOIR", "WOIT"});
    }
    if (water) {
        addAll({"WWPR", "WWPT", "WWIR", "WWIT"});
    }
    if (gas) {
        addAll({"WGPR", "WGPT", "WGIR", "WGIT"});
    }

    // Reservoir volume rates and cumulatives.
    addAll({"WVPR", "WVPT", "WVIR", "WVIT"});

    // Everything referenced by the deck's UDQ DEFINE expressions and by ACTIONX
    // conditions, so that both can be evaluated through the standard code paths
    // in the reduced run.
    for (const auto& udq : schedule.unique<UDQConfig>()) {
        udq.second.required_summary(keywords);
    }

    for (const auto& action : schedule.back().actions.get()) {
        action.required_summary(keywords);
    }

    // required_summary() yields bare keywords, so expand the well and group
    // level ones over the objects they can apply to.
    const auto& wells = schedule.wellNames();
    const auto& groups = schedule.groupNames();

    auto keys = std::vector<std::string>{};
    keys.reserve(keywords.size());

    for (const auto& keyword : keywords) {
        if (keyword.empty()) {
            continue;
        }

        switch (EclIO::SummaryNode::category_from_keyword(keyword)) {
        case EclIO::SummaryNode::Category::Well:
            for (const auto& well : wells) {
                keys.push_back(keyword + ':' + well);
            }
            break;

        case EclIO::SummaryNode::Category::Group:
            for (const auto& group : groups) {
                keys.push_back(keyword + ':' + group);
            }
            break;

        case EclIO::SummaryNode::Category::Field:
        case EclIO::SummaryNode::Category::Miscellaneous:
            keys.push_back(keyword);
            break;

        default:
            // Region, block, connection, segment, aquifer and node level
            // quantities are evaluated locally by the reduced run and are not
            // expandable without further context.
            break;
        }
    }

    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    return keys;
}

} // namespace Opm
