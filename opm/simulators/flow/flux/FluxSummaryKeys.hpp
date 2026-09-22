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

#ifndef OPM_FLUX_SUMMARY_KEYS_HPP
#define OPM_FLUX_SUMMARY_KEYS_HPP

#include <string>
#include <string_view>
#include <vector>

namespace Opm {

class Schedule;

/// Which summary KEYWORDS a FLUX file's embedded vectors are drawn from.
///
/// Bare keywords, as UDQDefine::required_summary() and ActionX yield them: a
/// UDQ refers to WBHP, not to WBHP:B-2H, the wells it applies to being held
/// separately. Matching has to be done at this level, because which objects a
/// keyword covers -- wells, groups, regions, segments -- is not something the
/// keyword alone says, and for regions and segments it cannot be enumerated
/// without knowing what the parent asked for.
///
/// \param[in] schedule Parent run's schedule, for the UDQ and ACTIONX
///    definitions.
///
/// \param[in] oil,water,gas Which phases are active.
///
/// \return Bare keywords, sorted and deduplicated.
std::vector<std::string> fluxSummaryKeywords(const Schedule& schedule,
                                             bool oil,
                                             bool water,
                                             bool gas);

/// Which of the parent run's summary vectors belong in a FLUX file.
///
/// A reduced run needs them to reconstruct the wells that fall outside its
/// sector, and to evaluate the deck's UDQ DEFINE expressions and ACTIONX
/// conditions through the ordinary code paths rather than approximating them.
///
/// Shared between the live DUMPFLUX path and the offline make_flux tool so that
/// a file built either way carries the same vectors. Embedding the whole of the
/// parent's summary instead would be both wasteful and unpredictable, since it
/// would depend on what the parent happened to ask for in its SUMMARY section.
///
/// Only the keywords that can be enumerated from the schedule are expanded,
/// which means the well, group, field and miscellaneous ones. A caller holding
/// the parent's actual key list should match on fluxSummaryKeywords() instead,
/// which covers every category.
///
/// \param[in] schedule Parent run's schedule, for the UDQ and ACTIONX
///    definitions and for the wells and groups the bare keywords expand over.
///
/// \param[in] oil,water,gas Which phases are active.
///
/// \return Fully qualified keys, sorted and deduplicated.
std::vector<std::string> fluxSummaryKeys(const Schedule& schedule,
                                         bool oil,
                                         bool water,
                                         bool gas);

/// The keyword part of a summary key, i.e. everything before the first colon.
std::string_view fluxSummaryKeywordOf(std::string_view key);

} // namespace Opm

#endif // OPM_FLUX_SUMMARY_KEYS_HPP
