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

#ifndef OPM_FLUX_ACTIVATION_HPP
#define OPM_FLUX_ACTIVATION_HPP

#include <opm/simulators/utils/ParallelCommunication.hpp>

namespace Opm {

class EclipseState;

bool applyUseFluxActnum(EclipseState& eclipseState);

//! \brief Whether this run writes a .FLUX file, i.e. it has FLUXNUM but is not
//!   itself a USEFLUX consumer.
//!
//! \details Only the root rank keeps the global field properties once the grid
//!   has been distributed, so the answer is broadcast to keep every rank in
//!   agreement.
bool isFluxDumpRun(const EclipseState& eclipseState,
                   const Parallel::Communication& comm);

} // namespace Opm

#endif // OPM_FLUX_ACTIVATION_HPP
