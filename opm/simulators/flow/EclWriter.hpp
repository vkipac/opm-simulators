// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.

  Consult the COPYING file in the top-level source directory of this
  module for the precise wording of the license and the list of
  copyright holders.
*/
/*!
 * \file
 *
 * \copydoc Opm::EclWriter
 */
#ifndef OPM_ECL_WRITER_HPP
#define OPM_ECL_WRITER_HPP

#include <dune/grid/common/partitionset.hh>

#include <opm/common/TimingMacros.hpp> // OPM_TIMEBLOCK
#include <opm/common/OpmLog/OpmLog.hpp>
#include <opm/input/eclipse/Schedule/Action/Actions.hpp>
#include <opm/input/eclipse/Schedule/Action/ActionX.hpp>
#include <opm/input/eclipse/Schedule/RPTConfig.hpp>
#include <opm/input/eclipse/Schedule/UDQ/UDQConfig.hpp>

#include <opm/input/eclipse/Units/Units.hpp>
#include <opm/input/eclipse/Units/UnitSystem.hpp>
#include <opm/input/eclipse/EclipseState/SummaryConfig/SummaryConfig.hpp>

#include <opm/io/eclipse/SummaryNode.hpp>

#include <opm/output/eclipse/Inplace.hpp>
#include <opm/output/eclipse/RegionVariableCollection.hpp>
#include <opm/output/eclipse/RestartValue.hpp>

#include <opm/models/blackoil/blackoilproperties.hh> // Properties::EnableMech, EnableSolvent
#include <opm/models/common/multiphasebaseproperties.hh> // Properties::FluidSystem

#include <opm/simulators/flow/CollectDataOnIORank.hpp>
#include <opm/simulators/flow/countGlobalCells.hpp>
#include <opm/simulators/flow/EclGenericWriter.hpp>
#include <opm/simulators/flow/FlowBaseVanguard.hpp>
#include <opm/simulators/flow/FlowProblemParameters.hpp>
#include <opm/simulators/flow/flux/FluxDumper.hpp>
#include <opm/simulators/flow/flux/FluxRegions.hpp>
#include <opm/simulators/timestepping/SimulatorTimer.hpp>
#include <opm/simulators/utils/DeferredLoggingErrorHelpers.hpp>
#include <opm/simulators/utils/ParallelRestart.hpp>
#include <opm/simulators/utils/ParallelSerialization.hpp>

#include <opm/simulators/flow/rescoup/ReservoirCouplingEnabled.hpp>
#ifdef RESERVOIR_COUPLING_ENABLED
#include <opm/simulators/flow/rescoup/ReservoirCouplingMaster.hpp>
#endif

#include <boost/date_time/posix_time/posix_time.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Opm::Parameters {

// If available, write the ECL output in a non-blocking manner
struct EnableAsyncEclOutput { static constexpr bool value = true; };

// By default, use single precision for the ECL formated results
struct EclOutputDoublePrecision { static constexpr bool value = false; };

// Write all solutions for visualization, not just the ones for the
// report steps...
struct EnableWriteAllSolutions { static constexpr bool value = false; };

// Write ESMRY file for fast loading of summary data
struct EnableEsmry { static constexpr bool value = true; };

} // namespace Opm::Parameters

namespace Opm::Action {
    class State;
} // namespace Opm::Action

namespace Opm {
    class EclipseIO;
    class UDQState;
} // namespace Opm

namespace Opm {
/*!
 * \ingroup EclBlackOilSimulator
 *
 * \brief Collects necessary output values and pass it to opm-common's ECL output.
 *
 * Caveats:
 * - For this class to do do anything meaningful, you will have to
 *   have the OPM module opm-common with ECL writing enabled.
 * - The only DUNE grid which is currently supported is Dune::CpGrid
 *   from the OPM module "opm-grid". Using another grid won't
 *   fail at compile time but you will provoke a fatal exception as
 *   soon as you try to write an ECL output file.
 * - This class requires to use the black oil model with the element
 *   centered finite volume discretization.
 */
template <class TypeTag, class OutputModule>
class EclWriter : public EclGenericWriter<GetPropType<TypeTag, Properties::Grid>,
                                          GetPropType<TypeTag, Properties::EquilGrid>,
                                          GetPropType<TypeTag, Properties::GridView>,
                                          GetPropType<TypeTag, Properties::ElementMapper>,
                                          GetPropType<TypeTag, Properties::Scalar>>
{
    using Simulator = GetPropType<TypeTag, Properties::Simulator>;
    using Vanguard = GetPropType<TypeTag, Properties::Vanguard>;
    using GridView = GetPropType<TypeTag, Properties::GridView>;
    using Grid = GetPropType<TypeTag, Properties::Grid>;
    using EquilGrid = GetPropType<TypeTag, Properties::EquilGrid>;
    using Scalar = GetPropType<TypeTag, Properties::Scalar>;
    using ElementContext = GetPropType<TypeTag, Properties::ElementContext>;
    using FluidSystem = GetPropType<TypeTag, Properties::FluidSystem>;
    using Indices = GetPropType<TypeTag, Properties::Indices>;
    using Element = typename GridView::template Codim<0>::Entity;
    using ElementMapper = GetPropType<TypeTag, Properties::ElementMapper>;
    using ElementIterator = typename GridView::template Codim<0>::Iterator;
    using BaseType = EclGenericWriter<Grid,EquilGrid,GridView,ElementMapper,Scalar>;

    typedef Dune::MultipleCodimMultipleGeomTypeMapper< GridView > VertexMapper;

    static constexpr bool enableEnergy =
        getPropValue<TypeTag, Properties::EnergyModuleType>() == EnergyModules::FullyImplicitThermal ||
        getPropValue<TypeTag, Properties::EnergyModuleType>() == EnergyModules::SequentialImplicitThermal;
    enum { enableMech = getPropValue<TypeTag, Properties::EnableMech>() };
    static constexpr bool enableSolvent = getPropValue<TypeTag, Properties::EnableSolvent>();
    enum { enableGeochemistry = getPropValue<TypeTag, Properties::EnableGeochemistry>() };

public:

    using DynamicConns =
        std::vector<std::pair<std::string, std::vector<std::size_t>>>;

    static void registerParameters()
    {
        OutputModule::registerParameters();

        Parameters::Register<Parameters::EnableAsyncEclOutput>
            ("Write the ECL-formated results in a non-blocking way "
             "(i.e., using a separate thread).");
        Parameters::Register<Parameters::EnableEsmry>
            ("Write ESMRY file for fast loading of summary data.");
        Parameters::Register<Parameters::FluxSummaryMinIntervalBetweenSamples<Scalar>>
            ("Minimum time in days between consecutive parent summary samples "
             "written to the .FLUX file by a DUMPFLUX run. Every time step is "
             "sampled unless that would place the sample within this interval "
             "of the previous one; report step boundaries are always sampled. "
             "Smaller values improve the accuracy of the interpolation done by "
             "a USEFLUX run, at the cost of a larger .FLUX file. Use 0 to "
             "sample every time step.");
        Parameters::Register<Parameters::FluxBoundaryMinIntervalBetweenSamples<Scalar>>
            ("Minimum time in days between consecutive sector boundary records "
             "written to the .FLUX file by a DUMPFLUX run. Every time step is "
             "written unless that would place the record within this interval "
             "of the previous one; report step boundaries are always written. "
             "Use 0 to write every time step. Ignored when "
             "--flux-boundary-report-steps-only is set.");
        Parameters::Register<Parameters::FluxBoundaryReportStepsOnly>
            ("Write sector boundary data once per report step rather than per "
             "time step. This restores the behaviour of earlier versions and is "
             "generally less accurate for models whose boundary flow varies "
             "within a report step.");
    }

    // The Simulator object should preferably have been const - the
    // only reason that is not the case is due to the SummaryState
    // object owned deep down by the vanguard.
    explicit EclWriter(Simulator& simulator)
        : BaseType(simulator.vanguard().schedule(),
                   simulator.vanguard().eclState(),
                   simulator.vanguard().summaryConfig(),
                   simulator.vanguard().grid(),
                   ((simulator.vanguard().grid().comm().rank() == 0)
                    ? &simulator.vanguard().equilGrid()
                    : nullptr),
                   simulator.vanguard().gridView(),
                   simulator.vanguard().cartesianIndexMapper(),
                   ((simulator.vanguard().grid().comm().rank() == 0)
                    ? &simulator.vanguard().equilCartesianIndexMapper()
                    : nullptr),
                   Parameters::Get<Parameters::EnableAsyncEclOutput>(),
                   Parameters::Get<Parameters::EnableEsmry>())
        , simulator_(simulator)
    {
#if HAVE_MPI
        if (this->simulator_.vanguard().grid().comm().size() > 1) {
            auto smryCfg = (this->simulator_.vanguard().grid().comm().rank() == 0)
                ? this->eclIO_->finalSummaryConfig()
                : SummaryConfig{};

            eclBroadcast(this->simulator_.vanguard().grid().comm(), smryCfg);

            this->outputModule_ = std::make_unique<OutputModule>
                (simulator, smryCfg, this->collectOnIORank_);
        }
        else
#endif
        {
            this->outputModule_ = std::make_unique<OutputModule>
                (simulator, this->eclIO_->finalSummaryConfig(), this->collectOnIORank_);
        }

        this->rank_ = this->simulator_.vanguard().grid().comm().rank();
        this->initializeFluxDumpers_();

        this->simulator_.vanguard().eclState().computeFipRegionStatistics();
    }

    ~EclWriter()
    {}

    const EquilGrid& globalGrid() const
    {
        return simulator_.vanguard().equilGrid();
    }

    void recordNewDynamicWellConns(const DynamicConns& newConns)
    {
        if (this->collectOnIORank_.isIORank() && (this->eclIO_ != nullptr)) {
            this->eclIO_->recordNewDynamicWellConns(newConns);
        }
    }

    /*!
     * \brief collect and pass data and pass it to eclIO writer
     */
    void evalSummaryState(bool isSubStep)
    {
        OPM_TIMEBLOCK(evalSummaryState);
        const int reportStepNum = simulator_.episodeIndex() + 1;

        /*
          The summary data is not evaluated for timestep 0, that is
          implemented with a:

             if (time_step == 0)
                 return;

          check somewhere in the summary code. When the summary code was
          split in separate methods Summary::eval() and
          Summary::add_timestep() it was necessary to pull this test out
          here to ensure that the well and group related keywords in the
          restart file, like XWEL and XGRP were "correct" also in the
          initial report step.

          "Correct" in this context means unchanged behavior, might very
          well be more correct to actually remove this if test.
        */

        if (reportStepNum == 0)
            return;

        const Scalar curTime = simulator_.time() + simulator_.timeStepSize();
        const Scalar totalCpuTime =
            simulator_.executionTimer().realTimeElapsed() +
            simulator_.setupTimer().realTimeElapsed() +
            simulator_.vanguard().setupTime();

        auto& regVars = this->outputModule_->regionVariables();

        regVars.prepareValueAccumulation();

        if (const auto conn_opt_ix = regVars
            .variableIndex(this->outputModule_->regVarMapping(), "ConnOPT");
            conn_opt_ix.has_value())
        {
            this->simulator_.problem()
                .wellModel().reportIntervalConnectionOilProduction
                (this->simulator_.timeStepSize(), *conn_opt_ix, regVars);
        }

        auto localWellData                  = simulator_.problem().wellModel().wellData();
        const auto localWBP                 = simulator_.problem().wellModel().wellBlockAveragePressures();
        const auto localGroupAndNetworkData = simulator_.problem().wellModel()
            .groupAndNetworkData(reportStepNum);

        const auto localAquiferData = simulator_.problem().aquiferModel().aquiferData();
        const auto localWellTestState = simulator_.problem().wellModel().wellTestState();
        this->prepareLocalCellData(isSubStep, reportStepNum);

        if (this->outputModule_->needInterfaceFluxes(isSubStep)) {
            this->captureLocalFluxData();
        }

        if (this->collectOnIORank_.isParallel()) {
            OPM_BEGIN_PARALLEL_TRY_CATCH()

            std::map<std::pair<std::string,int>,double> dummy;
            this->collectOnIORank_.collect({},
                                           outputModule_->getBlockData(),
                                           dummy,
                                           localWellData,
                                           localWBP,
                                           localGroupAndNetworkData,
                                           localAquiferData,
                                           localWellTestState,
                                           this->outputModule_->getInterRegFlows(),
                                           {},
                                           {},
                                           this->outputModule_->getLgrBlockData());

            if (this->collectOnIORank_.isIORank()) {
                auto& iregFlows = this->collectOnIORank_.globalInterRegFlows();

                if (! iregFlows.readIsConsistent()) {
                    throw std::runtime_error {
                        "Inconsistent inter-region flow "
                        "region set names in parallel"
                    };
                }

                iregFlows.compress();
            }

            OPM_END_PARALLEL_TRY_CATCH("Collect to I/O rank: ",
                                       this->simulator_.vanguard().grid().comm());
        }


        std::map<std::string, double> miscSummaryData;
        std::map<std::string, std::vector<double>> regionData;
        Inplace inplace;

        {
            OPM_TIMEBLOCK(outputFipLogAndFipresvLog);

            inplace = outputModule_->calc_inplace(miscSummaryData, regionData, simulator_.gridView().comm());

            if (this->collectOnIORank_.isIORank()){
                inplace_ = inplace;
            }
        }

        // Add TCPU
        if (totalCpuTime != 0.0) {
            miscSummaryData["TCPU"] = totalCpuTime;
        }
        if (this->sub_step_report_.total_newton_iterations != 0) {
            miscSummaryData["NEWTON"] = this->sub_step_report_.total_newton_iterations;
        }
        if (this->sub_step_report_.total_linear_iterations != 0) {
            miscSummaryData["MLINEARS"] = this->sub_step_report_.total_linear_iterations;
        }
        if (this->sub_step_report_.total_newton_iterations != 0) {
            miscSummaryData["NLINEARS"] =  static_cast<float>(this->sub_step_report_.total_linear_iterations) / this->sub_step_report_.total_newton_iterations;
        }
        if (this->sub_step_report_.min_linear_iterations != std::numeric_limits<unsigned int>::max()) {
            miscSummaryData["NLINSMIN"] = this->sub_step_report_.min_linear_iterations;
        }
        if (this->sub_step_report_.max_linear_iterations != 0) {
            miscSummaryData["NLINSMAX"] = this->sub_step_report_.max_linear_iterations;
        }
        if (this->simulation_report_.success.total_linear_iterations != 0) {
            miscSummaryData["MSUMLINS"] = this->simulation_report_.success.total_linear_iterations;
        }
        if (this->simulation_report_.success.total_newton_iterations != 0) {
            miscSummaryData["MSUMNEWT"] = this->simulation_report_.success.total_newton_iterations;
        }

        // For reservoir coupling master: collect slave production/injection
        // rates to pass through to Summary::eval() via DynamicSimulatorState.
        const auto rcGroupRates = this->collectReservoirCouplingGroupRates_();

        {
            OPM_TIMEBLOCK(evalSummary);

            // Note: This statement sums one value per registered region
            // variable per region per registered region set across all MPI
            // ranks.
            regVars.commitValues();

            // Wells outside the USEFLUX region take no part in the reduced run,
            // so without this the field and group aggregates would only cover
            // the sector. Filling in their rates from the parent run lets
            // Summary::eval() form F* and G* vectors over the union of sector
            // and parent-only wells through its normal code path.
            this->injectParentOnlyWellData_(this->collectOnIORank_.isParallel()
                                            ? this->collectOnIORank_.globalWellData()
                                            : localWellData,
                                            curTime);

            const auto& blockData = this->collectOnIORank_.isParallel()
                ? this->collectOnIORank_.globalBlockData()
                : this->outputModule_->getBlockData();

            const auto& lgrBlockData = this->collectOnIORank_.isParallel()
                ? this->collectOnIORank_.globalLgrBlockData()
                : this->outputModule_->getLgrBlockData();

            const auto& interRegFlows = this->collectOnIORank_.isParallel()
                ? this->collectOnIORank_.globalInterRegFlows()
                : this->outputModule_->getInterRegFlows();

            this->evalSummary(reportStepNum,
                              curTime,
                              localWellData,
                              localWBP,
                              localGroupAndNetworkData,
                              localAquiferData,
                              blockData,
                              lgrBlockData,
                              miscSummaryData,
                              regionData,
                              this->outputModule_->regVarMapping(),
                              regVars,
                              inplace,
                              this->outputModule_->initialInplace(),
                              interRegFlows,
                              this->summaryState(),
                              this->udqState(),
                              rcGroupRates ? &(*rcGroupRates) : nullptr);
        }

        // The SummaryState is now fully populated for this step, so the
        // DUMPFLUX summary snapshot can be taken.
        this->sampleFluxSummary_(isSubStep, curTime);
    }

    //! \brief Writes the initial FIP report as configured in RPTSOL.
    void writeInitialFIPReport()
    {
        const auto& gridView = simulator_.vanguard().gridView();
        const int num_interior = detail::
            countLocalInteriorCellsGridView(gridView);

        this->outputModule_->
            allocBuffers(num_interior, 0, false, false, /*isRestart*/ false);

#ifdef _OPENMP
#pragma omp parallel for
#endif
        for (int dofIdx = 0; dofIdx < num_interior; ++dofIdx) {
            const auto& intQuants = *simulator_.model().cachedIntensiveQuantities(dofIdx, /*timeIdx=*/0);
            const auto totVolume = simulator_.model().dofTotalVolume(dofIdx);

            this->outputModule_->updateFluidInPlace(dofIdx, intQuants, totVolume);
        }

        // We always calculate the initial fip values as it may be used by various
        // keywords in the Schedule, e.g. FIP=2 in RPTSCHED but no FIP in RPTSOL
        outputModule_->calc_initial_inplace(simulator_.gridView().comm());

        // check if RPTSOL entry has FIP output
        const auto& fip = simulator_.vanguard().eclState().getEclipseConfig().fip();
        if (fip.output(FIPConfig::OutputField::FIELD) ||
            fip.output(FIPConfig::OutputField::RESV))
        {
            OPM_TIMEBLOCK(outputFipLogAndFipresvLog);

            const auto start_time = boost::posix_time::
                from_time_t(simulator_.vanguard().schedule().getStartTime());

            if (this->collectOnIORank_.isIORank()) {
                this->inplace_ = *this->outputModule_->initialInplace();

                this->outputModule_->
                    outputFipAndResvLog(this->inplace_, 0, 0.0, start_time,
                                        false, simulator_.gridView().comm());
            }
        }

        outputModule_->outputFipAndResvLogToCSV(0, false, simulator_.gridView().comm());
    }

    void writeReports(const SimulatorTimer& timer)
    {
        if (! this->collectOnIORank_.isIORank()) {
            return;
        }

        // SimulatorTimer::reportStepNum() is the simulator's zero-based
        // "episode index".  This is generally the index value needed to
        // look up objects in the Schedule container.  That said, function
        // writeReports() is invoked at the *beginning* of a report
        // step/episode which means we typically need the objects from the
        // *previous* report step/episode.  We therefore need special case
        // handling for reportStepNum() == 0 in base runs and
        // reportStepNum() <= restart step in restarted runs.
        const auto firstStep = this->initialStep();
        const auto simStep =
            std::max(timer.reportStepNum() - 1, firstStep);

        const auto& rpt = this->schedule_[simStep].rpt_config();

        if (rpt.contains("WELSPECS") && (rpt.at("WELSPECS") > 0)) {
            // Requesting a well specification report is valid at all times,
            // including reportStepNum() == initialStep().
            this->writeWellspecReport(timer);
        }

        if (timer.reportStepNum() == firstStep) {
            // No dynamic flows at the beginning of the initialStep().
            return;
        }

        if (rpt.contains("WELLS") && rpt.at("WELLS") > 0) {
            this->writeWellflowReport(timer, simStep, rpt.at("WELLS"));
        }

        this->outputModule_->outputFipAndResvLog(this->inplace_,
                                                 timer.reportStepNum(),
                                                 timer.simulationTimeElapsed(),
                                                 timer.currentDateTime(),
                                                 /* isSubstep = */ false,
                                                 simulator_.gridView().comm());

        OpmLog::note("");   // Blank line after all reports.
    }

    void writeOutput(data::Solution&& localCellData, const bool isSubStep, const bool isForcedFinalOutput)
    {
        OPM_TIMEBLOCK(writeOutput);

        const int reportStepNum = simulator_.episodeIndex() + 1;

        this->prepareLocalCellData(isSubStep, reportStepNum);
        this->outputModule_->outputErrorLog(simulator_.gridView().comm());

        // output using eclWriter if enabled
        auto localWellData = simulator_.problem().wellModel().wellData();
        auto localGroupAndNetworkData = simulator_.problem().wellModel()
            .groupAndNetworkData(reportStepNum);

        auto localAquiferData = simulator_.problem().aquiferModel().aquiferData();
        auto localWellTestState = simulator_.problem().wellModel().wellTestState();

        const bool isFlowsn = this->outputModule_->getFlows().hasFlowsn();
        auto flowsn = this->outputModule_->getFlows().getFlowsn();

        const bool isFloresn = this->outputModule_->getFlows().hasFloresn();
        auto floresn = this->outputModule_->getFlows().getFloresn();

        // assignToSolution() moves the FLORES buffers into the restart solution,
        // so DUMPFLUX has to sample its boundary faces before that happens.
        if (! isSubStep) {
            this->captureFluxDumperFlores_();
        }

        if (! isSubStep || Parameters::Get<Parameters::EnableWriteAllSolutions>()) {

            if (localCellData.empty()) {
                this->outputModule_->assignToSolution(localCellData);
            }

            // Add cell data to perforations for RFT output
            this->outputModule_->addRftDataToWells(localWellData,
                                                   reportStepNum,
                                                   simulator_.gridView().comm());
        }

        if (this->collectOnIORank_.isParallel() ||
            this->collectOnIORank_.doesNeedReordering())
        {
            // Note: We don't need WBP (well-block averaged pressures) or
            // inter-region flow rate values in order to create restart file
            // output.  There's consequently no need to collect those
            // properties on the I/O rank.

            this->collectOnIORank_.collect(localCellData,
                                           this->outputModule_->getBlockData(),
                                           this->outputModule_->getExtraBlockData(),
                                           localWellData,
                                           /* wbpData = */ {},
                                           localGroupAndNetworkData,
                                           localAquiferData,
                                           localWellTestState,
                                           /* interRegFlows = */ {},
                                           flowsn,
                                           floresn,
                                           /* lgrBlockData = */ {});
            if (this->collectOnIORank_.isIORank()) {
                this->outputModule_->assignGlobalFieldsToSolution(this->collectOnIORank_.globalCellData());
            }
        } else {
            this->outputModule_->assignGlobalFieldsToSolution(localCellData);
        }

        if (this->collectOnIORank_.isIORank()) {
            this->updateFluxDumpers_(reportStepNum, isSubStep);

            const Scalar curTime = simulator_.time() + simulator_.timeStepSize();
            const Scalar nextStepSize = simulator_.problem().nextTimeStepSize();
            std::optional<int> timeStepIdx;
            if (Parameters::Get<Parameters::EnableWriteAllSolutions>()) {
                timeStepIdx = simulator_.timeStepIndex();
            }
            this->doWriteOutput(reportStepNum, timeStepIdx, isSubStep,
                                isForcedFinalOutput,
                                std::move(localCellData),
                                std::move(localWellData),
                                std::move(localGroupAndNetworkData),
                                std::move(localAquiferData),
                                std::move(localWellTestState),
                                this->actionState(),
                                this->udqState(),
                                this->summaryState(),
                                this->simulator_.problem().thresholdPressure().getRestartVector(),
                                curTime, nextStepSize,
                                Parameters::Get<Parameters::EclOutputDoublePrecision>(),
                                isFlowsn, std::move(flowsn),
                                isFloresn, std::move(floresn));
        }
    }

    //! \brief Sample the DUMPFLUX boundary rates for the time step that just finished.
    //! \details The FLUX payload is declared as averaged over a report step. The
    //!          values are taken straight from the linearizer, which holds the
    //!          converged fluxes of the step and - unlike the output module's
    //!          buffers - is not tied to the restart output cadence.
    void sampleFluxDumperRates(const Scalar dt)
    {
        if (this->fluxDumpers_.empty() || !this->collectOnIORank_.isIORank()) {
            return;
        }

        if (!(dt > Scalar{0})) {
            return;
        }

        const auto& floresInfo = this->simulator_.problem().model().linearizer().getFloresInfo();
        if (floresInfo.empty()) {
            return;
        }

        const auto& vanguard = this->simulator_.vanguard();

        auto floresValue = [&floresInfo, &vanguard](const int globalCell,
                                                    const FaceDir::DirEnum dir,
                                                    const int eqIdx) -> double
        {
            const auto cell = vanguard.compressedIndex(globalCell);
            if (cell < 0
                || static_cast<std::size_t>(cell) >= static_cast<std::size_t>(floresInfo.size()))
            {
                return 0.0;
            }

            const auto faceId = FaceDir::ToIntersectionIndex(dir);
            for (const auto& info : floresInfo[cell]) {
                if (info.faceId == faceId) {
                    return info.flow[eqIdx];
                }
            }

            return 0.0;
        };

        if (this->fluxRateSnapshots_.size() != this->fluxDumpers_.size()) {
            this->fluxRateSnapshots_.assign(this->fluxDumpers_.size(), {});
        }

        if (this->fluxMassSnapshots_.size() != this->fluxDumpers_.size()) {
            this->fluxMassSnapshots_.assign(this->fluxDumpers_.size(), {});
        }

        // Phase density of a cell, or zero when it is not available.
        const auto phaseDensity = [&vanguard, this](const int globalCell,
                                                    const EclIO::FluxFile::Phase phase) -> double
        {
            const auto cell = vanguard.compressedIndex(globalCell);
            if (cell < 0) {
                return 0.0;
            }

            const auto phaseIdx = this->fluxPhaseIndex_(phase);
            if (!FluidSystem::phaseIsActive(phaseIdx)) {
                return 0.0;
            }

            const auto* intQuants =
                this->simulator_.model().cachedIntensiveQuantities(cell, /*timeIdx=*/0);
            if (intQuants == nullptr) {
                return 0.0;
            }

            return getValue(intQuants->fluidState().density(phaseIdx));
        };

        const auto orientedFaceValue = [this](const auto& accessor,
                                              const FluxRegions::BoundaryFace& face,
                                              const EclIO::FluxFile::Phase phase) -> double
        {
            if (face.isNnc || face.direction == FaceDir::Unknown) {
                return 0.0;
            }

            const auto eqIdx = this->fluxEquationIndex_(phase);
            if (eqIdx < 0) {
                return 0.0;
            }

            // Values are stored per cell for the positive face directions only
            // and are oriented along the positive axis, while the FLUX file is
            // positive into the sector.
            switch (face.direction) {
            case FaceDir::XPlus:
            case FaceDir::YPlus:
            case FaceDir::ZPlus:
                return -accessor(face.interiorGlobalCell, face.direction, eqIdx);

            case FaceDir::XMinus:
                return accessor(face.exteriorGlobalCell, FaceDir::XPlus, eqIdx);

            case FaceDir::YMinus:
                return accessor(face.exteriorGlobalCell, FaceDir::YPlus, eqIdx);

            case FaceDir::ZMinus:
                return accessor(face.exteriorGlobalCell, FaceDir::ZPlus, eqIdx);

            default:
                return 0.0;
            }
        };

        for (std::size_t i = 0; i < this->fluxDumpers_.size(); ++i) {
            this->fluxRateSnapshots_[i].push_back(
                this->fluxDumpers_[i].makeFaceMajorRates(
                    [&orientedFaceValue, &floresValue](const FluxRegions::BoundaryFace& face,
                                                       const EclIO::FluxFile::Phase phase)
                    {
                        return orientedFaceValue(floresValue, face, phase);
                    }));

            // Convert to a mass flux here, where the density of the cell the
            // flow actually comes from is known. A reduced run cannot do this
            // for inflow, because that upstream cell lies outside its grid.
            auto massSnapshot = this->fluxDumpers_[i].makeFaceMajorRates(
                [&orientedFaceValue, &floresValue, &phaseDensity]
                (const FluxRegions::BoundaryFace& face, const EclIO::FluxFile::Phase phase)
                {
                    const auto volRate = orientedFaceValue(floresValue, face, phase);
                    if (volRate == 0.0) {
                        return 0.0;
                    }

                    // Positive is into the sector, so inflow comes from the
                    // exterior cell and outflow from the interior one.
                    const auto upstream = (volRate > 0.0)
                        ? face.exteriorGlobalCell
                        : face.interiorGlobalCell;

                    auto rho = phaseDensity(upstream, phase);
                    if (!(rho > 0.0)) {
                        // Fall back to the other side rather than dropping the
                        // flux, e.g. when the upstream cell is inactive.
                        const auto other = (volRate > 0.0)
                            ? face.interiorGlobalCell
                            : face.exteriorGlobalCell;
                        rho = phaseDensity(other, phase);
                    }

                    return (rho > 0.0) ? (volRate * rho) : 0.0;
                });

            if (std::any_of(massSnapshot.begin(), massSnapshot.end(),
                            [](const double v) { return v != 0.0; }))
            {
                this->fluxMassUsable_ = true;
            }

            this->fluxMassSnapshots_[i].push_back(std::move(massSnapshot));
        }

        this->fluxRateTimeWeights_.push_back(static_cast<double>(dt));
    }

    void beginRestart()
    {        const auto enablePCHysteresis = simulator_.problem().materialLawManager()->enablePCHysteresis();
        const auto enableNonWettingHysteresis = simulator_.problem().materialLawManager()->enableNonWettingHysteresis();
        const auto enableWettingHysteresis = simulator_.problem().materialLawManager()->enableWettingHysteresis();
        const auto oilActive = FluidSystem::phaseIsActive(FluidSystem::oilPhaseIdx);
        const auto gasActive = FluidSystem::phaseIsActive(FluidSystem::gasPhaseIdx);
        const auto waterActive = FluidSystem::phaseIsActive(FluidSystem::waterPhaseIdx);
        const auto enableSwatinit = simulator_.vanguard().eclState().fieldProps().has_double("SWATINIT");

        std::vector<RestartKey> solutionKeys {
            {"PRESSURE", UnitSystem::measure::pressure},
            {"SWAT",     UnitSystem::measure::identity,    waterActive},
            {"SGAS",     UnitSystem::measure::identity,    gasActive},
            {"TEMP",     UnitSystem::measure::temperature, enableEnergy},
            {"SSOLVENT", UnitSystem::measure::identity,    enableSolvent},

            {"RS",  UnitSystem::measure::gas_oil_ratio, FluidSystem::enableDissolvedGas()},
            {"RV",  UnitSystem::measure::oil_gas_ratio, FluidSystem::enableVaporizedOil()},
            {"RVW", UnitSystem::measure::oil_gas_ratio, FluidSystem::enableVaporizedWater()},
            {"RSW", UnitSystem::measure::gas_oil_ratio, FluidSystem::enableDissolvedGasInWater()},

            {"SGMAX", UnitSystem::measure::identity, enableNonWettingHysteresis && oilActive && gasActive},
            {"SHMAX", UnitSystem::measure::identity, enableWettingHysteresis && oilActive && gasActive},

            {"SOMAX", UnitSystem::measure::identity,
             (enableNonWettingHysteresis && oilActive && waterActive)
             || simulator_.problem().vapparsActive(simulator_.episodeIndex())},

            {"SOMIN", UnitSystem::measure::identity, enablePCHysteresis && oilActive && gasActive},
            {"SWHY1", UnitSystem::measure::identity, enablePCHysteresis && oilActive && waterActive},
            {"SWMAX", UnitSystem::measure::identity, enableWettingHysteresis && oilActive && waterActive},

            {"PPCW", UnitSystem::measure::pressure, enableSwatinit},
        };

        {
            const auto& tracers = simulator_.vanguard().eclState().tracer();

            for (const auto& tracer : tracers) {
                const auto enableSolTracer =
                    ((tracer.phase == Phase::GAS) && FluidSystem::enableDissolvedGas()) ||
                    ((tracer.phase == Phase::OIL) && FluidSystem::enableVaporizedOil());

                solutionKeys.emplace_back(tracer.fname(), UnitSystem::measure::identity, true);
                solutionKeys.emplace_back(tracer.sname(), UnitSystem::measure::identity, enableSolTracer);
            }
        }

        const auto& inputThpres = eclState().getSimulationConfig().getThresholdPressure();
        const std::vector<RestartKey> extraKeys {
            {"OPMEXTRA", UnitSystem::measure::identity, false},
            {"THRESHPR", UnitSystem::measure::pressure, inputThpres.active()},
        };

        const auto& gridView = this->simulator_.vanguard().gridView();
        const auto numElements = gridView.size(/*codim=*/0);

        // Try to load restart step 0 to calculate initial FIP
        {
            this->outputModule_->allocBuffers(numElements,
                                              0,
                                              /*isSubStep = */false,
                                              /*log = */      false,
                                              /*isRestart = */true);

            const auto restartSolution =
                loadParallelRestartSolution(this->eclIO_.get(),
                                            solutionKeys, gridView.comm(), 0);

            if (!restartSolution.empty()) {
                for (auto elemIdx = 0*numElements; elemIdx < numElements; ++elemIdx) {
                    const auto globalIdx = this->collectOnIORank_.localIdxToGlobalIdx(elemIdx);
                    this->outputModule_->setRestart(restartSolution, elemIdx, globalIdx);
                }

                this->simulator_.problem().readSolutionFromOutputModule(0, true);
                this->simulator_.problem().temperatureModel().init();
                ElementContext elemCtx(this->simulator_);
                for (const auto& elem : elements(gridView, Dune::Partitions::interior)) {
                    elemCtx.updatePrimaryStencil(elem);
                    elemCtx.updatePrimaryIntensiveQuantities(/*timeIdx=*/0);

                    this->outputModule_->updateFluidInPlace(elemCtx);
                }

                this->outputModule_->calc_initial_inplace(this->simulator_.gridView().comm());
            }
        }

        {
            // The episodeIndex is rewound one step back before calling
            // beginRestart() and cannot be used here.  We just ask the
            // initconfig directly to be sure that we use the correct index.
            const auto restartStepIdx = this->simulator_.vanguard()
                .eclState().getInitConfig().getRestartStep();

            this->outputModule_->allocBuffers(numElements,
                                              restartStepIdx,
                                              /*isSubStep = */false,
                                              /*log = */      false,
                                              /*isRestart = */true);
        }

        {
            const auto restartValues =
                loadParallelRestart(this->eclIO_.get(),
                                    this->actionState(),
                                    this->summaryState(),
                                    solutionKeys, extraKeys, gridView.comm());

            for (auto elemIdx = 0*numElements; elemIdx < numElements; ++elemIdx) {
                const auto globalIdx = this->collectOnIORank_.localIdxToGlobalIdx(elemIdx);
                this->outputModule_->setRestart(restartValues.solution, elemIdx, globalIdx);
            }

            auto& tracer_model = simulator_.problem().tracerModel();
            for (int tracer_index = 0; tracer_index < tracer_model.numTracers(); ++tracer_index) {
                // Free tracers
                {
                    const auto& free_tracer_name = tracer_model.fname(tracer_index);
                    const auto& free_tracer_solution = restartValues.solution
                        .template data<double>(free_tracer_name);

                    for (auto elemIdx = 0*numElements; elemIdx < numElements; ++elemIdx) {
                        const auto globalIdx = this->collectOnIORank_.localIdxToGlobalIdx(elemIdx);
                        tracer_model.setFreeTracerConcentration
                            (tracer_index, elemIdx, free_tracer_solution[globalIdx]);
                    }
                }

                // Solution tracer (only if DISGAS/VAPOIL are active for gas/oil tracers)
                if ((tracer_model.phase(tracer_index) == Phase::GAS && FluidSystem::enableDissolvedGas()) ||
                    (tracer_model.phase(tracer_index) == Phase::OIL && FluidSystem::enableVaporizedOil()))
                {
                    tracer_model.setEnableSolTracers(tracer_index, true);

                    const auto& sol_tracer_name = tracer_model.sname(tracer_index);
                    const auto& sol_tracer_solution = restartValues.solution
                        .template data<double>(sol_tracer_name);

                    for (auto elemIdx = 0*numElements; elemIdx < numElements; ++elemIdx) {
                        const auto globalIdx = this->collectOnIORank_.localIdxToGlobalIdx(elemIdx);
                        tracer_model.setSolTracerConcentration
                            (tracer_index, elemIdx, sol_tracer_solution[globalIdx]);
                    }
                }
                else {
                    tracer_model.setEnableSolTracers(tracer_index, false);

                    for (auto elemIdx = 0*numElements; elemIdx < numElements; ++elemIdx) {
                        tracer_model.setSolTracerConcentration(tracer_index, elemIdx, 0.0);
                    }
                }
            }

            if (inputThpres.active()) {
                const_cast<Simulator&>(this->simulator_)
                    .problem().thresholdPressure()
                    .setFromRestart(restartValues.getExtra("THRESHPR"));
            }

            restartTimeStepSize_ = restartValues.getExtra("OPMEXTRA")[0];
            if (restartTimeStepSize_ <= 0) {
                restartTimeStepSize_ = std::numeric_limits<double>::max();
            }

            // Initialize the well model from restart values
            this->simulator_.problem().wellModel()
                .initFromRestartFile(restartValues);

            if (!restartValues.aquifer.empty()) {
                this->simulator_.problem().mutableAquiferModel()
                    .initFromRestart(restartValues.aquifer);
            }
        }
    }

    void endRestart()
    {
        // Calculate initial in-place volumes.
        // Does nothing if they have already been calculated,
        // e.g. from restart data at T=0.
        this->outputModule_->calc_initial_inplace(this->simulator_.gridView().comm());

        if (this->collectOnIORank_.isIORank()) {
            if (const auto* iip = this->outputModule_->initialInplace(); iip != nullptr) {
                this->inplace_ = *iip;
            }
        }
    }

    const OutputModule& outputModule() const
    { return *outputModule_; }

    OutputModule& mutableOutputModule() const
    { return *outputModule_; }

    Scalar restartTimeStepSize() const
    { return restartTimeStepSize_; }

    template <class Serializer>
    void serializeOp(Serializer& serializer)
    {
        serializer(*outputModule_);
    }

private:
    static bool enableEclOutput_()
    {
        static bool enable = Parameters::Get<Parameters::EnableEclOutput>();
        return enable;
    }

    const EclipseState& eclState() const
    { return simulator_.vanguard().eclState(); }

    SummaryState& summaryState()
    { return simulator_.vanguard().summaryState(); }

    const SummaryState& summaryState() const
    { return simulator_.vanguard().summaryState(); }

    Action::State& actionState()
    { return simulator_.vanguard().actionState(); }

    UDQState& udqState()
    { return simulator_.vanguard().udqState(); }

    const Schedule& schedule() const
    { return simulator_.vanguard().schedule(); }

    /// Collect reservoir coupling master group rates for Summary::eval().
    /// Returns nullopt for non-RC simulations or non-master processes.
    std::optional<data::ReservoirCouplingGroupRates> collectReservoirCouplingGroupRates_()
    {
#ifdef RESERVOIR_COUPLING_ENABLED
        // Guard: only BlackoilWellModel has reservoir coupling support.
        // CompWellModel (compositional) does not, so we use if constexpr
        // to avoid compilation errors when EclWriter is instantiated with
        // a compositional TypeTag.
        using WellModelType = std::remove_cvref_t<
            decltype(simulator_.problem().wellModel())>;
        if constexpr (requires(WellModelType& wm) { wm.isReservoirCouplingMaster(); }) {
            auto& wellModel = simulator_.problem().wellModel();
            if (!wellModel.isReservoirCouplingMaster()) {
                return std::nullopt;
            }
            return wellModel.reservoirCouplingMaster()
                .collectGroupRatesForSummary();
        }
#endif
        return std::nullopt;
    }

    void prepareLocalCellData(const bool isSubStep,
                              const int  reportStepNum)
    {
        OPM_TIMEBLOCK(prepareLocalCellData);

        if (this->outputModule_->localDataValid()) {
            return;
        }

        const auto& gridView = simulator_.vanguard().gridView();
        const bool log = this->collectOnIORank_.isIORank();

        const int num_interior = detail::
            countLocalInteriorCellsGridView(gridView);
        this->outputModule_->
            allocBuffers(num_interior, reportStepNum,
                         isSubStep && !Parameters::Get<Parameters::EnableWriteAllSolutions>(),
                         log, /*isRestart*/ false);

        ElementContext elemCtx(simulator_);

        OPM_BEGIN_PARALLEL_TRY_CATCH();

        {
            OPM_TIMEBLOCK(prepareCellBasedData);

            this->outputModule_->prepareDensityAccumulation();
            this->outputModule_->setupExtractors(isSubStep, reportStepNum);
            for (const auto& elem : elements(gridView, Dune::Partitions::interior)) {
                elemCtx.updatePrimaryStencil(elem);
                elemCtx.updatePrimaryIntensiveQuantities(/*timeIdx=*/0);

                this->outputModule_->processElement(elemCtx);
                this->outputModule_->processElementBlockData(elemCtx);
            }
            this->outputModule_->clearExtractors();

            this->outputModule_->accumulateDensityParallel();
        }

        {
            OPM_TIMEBLOCK(prepareFluidInPlace);

#ifdef _OPENMP
#pragma omp parallel for
#endif
            for (int dofIdx = 0; dofIdx < num_interior; ++dofIdx) {
                const auto& intQuants = *simulator_.model().cachedIntensiveQuantities(dofIdx, /*timeIdx=*/0);
                const auto totVolume = simulator_.model().dofTotalVolume(dofIdx);

                this->outputModule_->updateFluidInPlace(dofIdx, intQuants, totVolume);
            }
        }

        this->outputModule_->validateLocalData();

        OPM_END_PARALLEL_TRY_CATCH("EclWriter::prepareLocalCellData() failed: ",
                                   this->simulator_.vanguard().grid().comm());
    }

    void captureLocalFluxData()
    {
        OPM_TIMEBLOCK(captureLocalData);

        const auto& gridView = this->simulator_.vanguard().gridView();
        const auto timeIdx = 0u;

        auto elemCtx = ElementContext { this->simulator_ };

        const auto elemMapper = ElementMapper { gridView, Dune::mcmgElementLayout() };
        const auto activeIndex = [&elemMapper](const Element& e)
        {
            return elemMapper.index(e);
        };

        const auto cartesianIndex = [this](const int elemIndex)
        {
            return this->cartMapper_.cartesianIndex(elemIndex);
        };

        this->outputModule_->initializeFluxData();

        OPM_BEGIN_PARALLEL_TRY_CATCH();

        for (const auto& elem : elements(gridView, Dune::Partitions::interiorBorder)) {
            elemCtx.updateStencil(elem);
            elemCtx.updateIntensiveQuantities(timeIdx);
            elemCtx.updateExtensiveQuantities(timeIdx);

            this->outputModule_->processFluxes(elemCtx, activeIndex, cartesianIndex);
        }

        OPM_END_PARALLEL_TRY_CATCH("EclWriter::captureLocalFluxData() failed: ",
                                   this->simulator_.vanguard().grid().comm())

        this->outputModule_->finalizeFluxData();
    }

    void writeWellspecReport(const SimulatorTimer& timer) const
    {
        const auto changedWells = this->schedule_
            .changed_wells(timer.reportStepNum(), this->initialStep());

        const auto changedWellLists = this->schedule_
            .changedWellLists(timer.reportStepNum(), this->initialStep());

        if (changedWells.empty() && !changedWellLists) {
            return;
        }

        this->outputModule_->outputWellspecReport(changedWells,
                                                  changedWellLists,
                                                  timer.reportStepNum(),
                                                  timer.simulationTimeElapsed(),
                                                  timer.currentDateTime());
    }

    void writeWellflowReport(const SimulatorTimer& timer,
                             const int             simStep,
                             const int             wellsRequest) const
    {
        this->outputModule_->outputTimeStamp("WELLS",
                                             timer.simulationTimeElapsed(),
                                             timer.reportStepNum(),
                                             timer.currentDateTime());

        const auto wantConnData = wellsRequest > 1;

        this->outputModule_->outputProdLog(simStep, wantConnData);
        this->outputModule_->outputInjLog(simStep, wantConnData);
        this->outputModule_->outputCumLog(simStep, wantConnData);
        this->outputModule_->outputMSWLog(simStep);
    }

    int initialStep() const
    {
        const auto& initConfig = this->eclState().cfg().init();

        return initConfig.restartRequested()
            ? initConfig.getRestartStep()
            : 0;
    }

    int fluxPhaseMask_() const
    {
        auto mask = 0;

        if (FluidSystem::phaseIsActive(FluidSystem::oilPhaseIdx)) {
            mask |= static_cast<int>(EclIO::FluxFile::Phase::Oil);
        }
        if (FluidSystem::phaseIsActive(FluidSystem::waterPhaseIdx)) {
            mask |= static_cast<int>(EclIO::FluxFile::Phase::Water);
        }
        if (FluidSystem::phaseIsActive(FluidSystem::gasPhaseIdx)) {
            mask |= static_cast<int>(EclIO::FluxFile::Phase::Gas);
        }

        return mask;
    }

    int fluxComponentIndex_(const EclIO::FluxFile::Phase phase) const
    {
        switch (phase) {
        case EclIO::FluxFile::Phase::Oil:
            return FluidSystem::oilCompIdx;
        case EclIO::FluxFile::Phase::Water:
            return FluidSystem::waterCompIdx;
        case EclIO::FluxFile::Phase::Gas:
            return FluidSystem::gasCompIdx;
        }

        return FluidSystem::oilCompIdx;
    }

    int fluxPhaseIndex_(const EclIO::FluxFile::Phase phase) const
    {
        switch (phase) {
        case EclIO::FluxFile::Phase::Oil:
            return FluidSystem::oilPhaseIdx;
        case EclIO::FluxFile::Phase::Water:
            return FluidSystem::waterPhaseIdx;
        case EclIO::FluxFile::Phase::Gas:
            return FluidSystem::gasPhaseIdx;
        }

        return FluidSystem::oilPhaseIdx;
    }

    int fluxEquationIndex_(const EclIO::FluxFile::Phase phase) const
    {
        switch (phase) {
        case EclIO::FluxFile::Phase::Oil:
            return Indices::oilEnabled
                ? Indices::conti0EqIdx + FluidSystem::canonicalToActiveCompIdx(FluidSystem::oilCompIdx)
                : -1;
        case EclIO::FluxFile::Phase::Water:
            return Indices::waterEnabled
                ? Indices::conti0EqIdx + FluidSystem::canonicalToActiveCompIdx(FluidSystem::waterCompIdx)
                : -1;
        case EclIO::FluxFile::Phase::Gas:
            return Indices::gasEnabled
                ? Indices::conti0EqIdx + FluidSystem::canonicalToActiveCompIdx(FluidSystem::gasCompIdx)
                : -1;
        }

        return -1;
    }

    EclIO::FluxFile::Mode fluxOutputMode_(const std::string& fluxType) const
    {
        if (fluxType == "PRESSURE") {
            return EclIO::FluxFile::Mode::Pressure;
        }
        if (fluxType == "BOTH") {
            return EclIO::FluxFile::Mode::Both;
        }
        return EclIO::FluxFile::Mode::Flux;
    }

    static std::pair<int, int> normalizedNncPair_(const int c1, const int c2)
    {
        return (c1 <= c2)
            ? std::make_pair(c1, c2)
            : std::make_pair(c2, c1);
    }

    void initializeFluxDumpers_()
    {
        if (!this->collectOnIORank_.isIORank()) {
            return;
        }

        const auto& state = this->eclState();
        const auto& fieldProps = state.globalFieldProps();
        const auto& io = state.getIOConfig();

        // USEFLUX consumer decks now also carry FLUXNUM as the region map, so
        // FLUXNUM alone no longer means "this run should dump FLUX files".
        // Only producer runs bootstrap dumpers.
        if (io.getUseFlux()) {
            return;
        }

        if (!fieldProps.has_int("FLUXNUM")) {
            return;
        }

        // FLUXNUM is dimensioned over every cell of the grid, active or not,
        // so it has to be read with the global accessor rather than the one
        // that returns a value per active cell. ACTNUM is passed alongside it
        // so that inactive cells are kept out of the region.
        const auto regionValues = fieldProps.get_global_int("FLUXNUM");
        const auto& actnum = fieldProps.actnumRaw();
        const auto dims = state.gridDims().getNXYZ();

        std::vector<std::array<int, 2>> nncConnections;
        this->fluxNncPairToIndex_.clear();
        int nncIndex = 0;
        if (state.hasInputNNC()) {
            const auto& inputNnc = state.getInputNNC().input();
            nncConnections.reserve(inputNnc.size());
            for (const auto& nnc : inputNnc) {
                nncConnections.push_back({
                    static_cast<int>(nnc.cell1),
                    static_cast<int>(nnc.cell2),
                });

                const auto key = normalizedNncPair_(static_cast<int>(nnc.cell1),
                                                    static_cast<int>(nnc.cell2));
                this->fluxNncPairToIndex_.try_emplace(key, nncIndex);
                ++nncIndex;
            }
        }

        if (state.hasPinchNNC()) {
            const auto& pinchNnc = state.getPinchNNC();
            nncConnections.reserve(nncConnections.size() + pinchNnc.size());
            for (const auto& nnc : pinchNnc) {
                nncConnections.push_back({
                    static_cast<int>(nnc.cell1),
                    static_cast<int>(nnc.cell2),
                });

                const auto key = normalizedNncPair_(static_cast<int>(nnc.cell1),
                                                    static_cast<int>(nnc.cell2));
                this->fluxNncPairToIndex_.try_emplace(key, nncIndex);
                ++nncIndex;
            }
        }

        const auto regions = FluxRegions::extract(dims, regionValues, actnum, nncConnections);
        if (regions.empty()) {
            return;
        }

        const auto phaseMask = this->fluxPhaseMask_();
        const auto fluxMode = this->fluxOutputMode_(io.getFluxType());

        this->fluxOutputPaths_.clear();
        this->fluxDumpers_.reserve(regions.size());
        this->fluxOutputPaths_.reserve(regions.size());

        const auto basePath = (std::filesystem::path{io.getOutputDir()} / io.getBaseName()).string();
        const auto multipleRegions = regions.size() > 1;

        int sequence = 1;
        for (const auto& region : regions) {
            this->fluxDumpers_.emplace_back(io.getBaseName(),
                                            region.regionId,
                                            dims,
                                            region,
                                            fluxMode,
                                            EclIO::FluxFile::Sampling::Averaged,
                                            phaseMask);

            if (multipleRegions) {
                std::ostringstream os;
                os << basePath << ".FLUX" << std::setw(4) << std::setfill('0') << sequence;
                this->fluxOutputPaths_.push_back(os.str());
            }
            else {
                this->fluxOutputPaths_.push_back(basePath + ".FLUX");
            }
            ++sequence;
        }

        OpmLog::note("DUMPFLUX bootstrap: initialized "
                     + std::to_string(this->fluxDumpers_.size())
                 + " region dumper(s) from FLUXNUM");

        this->initializeFluxSummarySampling_();
    }

    // Establish the fixed set of parent summary vectors embedded in the .FLUX
    // file, together with the sampling throttle.
    void initializeFluxSummarySampling_()
    {
        if (this->fluxDumpers_.empty()) {
            return;
        }

        const auto intervalInDays = Parameters::Get<
            Parameters::FluxSummaryMinIntervalBetweenSamples<Scalar>>();

        this->fluxSummaryMinInterval_ =
            unit::convert::from(static_cast<double>(intervalInDays), unit::day);

        const auto boundaryIntervalInDays = Parameters::Get<
            Parameters::FluxBoundaryMinIntervalBetweenSamples<Scalar>>();

        this->fluxBoundaryMinInterval_ =
            unit::convert::from(static_cast<double>(boundaryIntervalInDays), unit::day);

        this->fluxBoundaryReportStepsOnly_ =
            Parameters::Get<Parameters::FluxBoundaryReportStepsOnly>();

        this->fluxBoundaryWindowStart_ = 0.0;
        this->fluxBoundaryHasRecord_ = false;

        for (auto& dumper : this->fluxDumpers_) {
            dumper.setBoundaryMinSampleInterval(this->fluxBoundaryReportStepsOnly_
                                                ? 0.0
                                                : this->fluxBoundaryMinInterval_);
        }

        if (this->fluxBoundaryReportStepsOnly_) {
            OpmLog::note("DUMPFLUX will write sector boundary data once per report step");
        }
        else {
            OpmLog::note(fmt::format("DUMPFLUX will write sector boundary data every time "
                                     "step, at most every {} day(s)",
                                     boundaryIntervalInDays));
        }

        this->fluxSummaryKeyList_ = this->fluxSummaryKeys_();

        this->fluxSummaryKeyTypes_.clear();
        this->fluxSummaryKeyTypes_.reserve(this->fluxSummaryKeyList_.size());
        for (const auto& key : this->fluxSummaryKeyList_) {
            const auto colon = key.find(':');
            const auto keyword = (colon == std::string::npos)
                ? key
                : key.substr(0, colon);

            this->fluxSummaryKeyTypes_.push_back(parseKeywordType(keyword));
        }

        this->fluxSummaryRateAccum_.assign(this->fluxSummaryKeyList_.size(), 0.0);
        this->fluxSummaryAccumDt_ = 0.0;
        this->fluxSummaryLastSampleTime_ = 0.0;
        this->fluxSummaryHasSample_ = false;

        for (auto& dumper : this->fluxDumpers_) {
            dumper.setSummaryKeys(this->fluxSummaryKeyList_);
            dumper.setSummaryMinSampleInterval(this->fluxSummaryMinInterval_);
        }

        OpmLog::note(fmt::format("DUMPFLUX will embed {} summary vector(s), "
                                 "sampled at most every {} day(s)",
                                 this->fluxSummaryKeyList_.size(),
                                 intervalInDays));
    }

    // The FLORES buffers owned by the output module are moved into the restart
    // solution by assignToSolution(). DUMPFLUX therefore has to sample the
    // Cartesian boundary-face values before that happens.
    void captureFluxDumperFlores_()
    {
        this->fluxCapturedFaceRates_.clear();

        if (this->fluxDumpers_.empty() || !this->collectOnIORank_.isIORank()) {
            return;
        }

        const auto& flows = this->outputModule_->getFlows();
        if (!flows.hasFlores()) {
            return;
        }

        this->fluxCapturedFaceRates_.reserve(this->fluxDumpers_.size());
        for (const auto& dumper : this->fluxDumpers_) {
            this->fluxCapturedFaceRates_.push_back(
                dumper.makeFaceMajorRates(
                    [&flows, this](const FluxRegions::BoundaryFace& face,
                                   const EclIO::FluxFile::Phase phase)
                    {
                        if (face.isNnc || face.direction == FaceDir::Unknown) {
                            return 0.0;
                        }

                        const auto comp = this->fluxComponentIndex_(phase);

                        // FLORES is stored per cell for the positive face
                        // directions only, and is oriented along the positive
                        // axis. The FLUX file convention is positive into the
                        // sector.
                        //
                        // For a boundary face on a positive direction the
                        // interior cell holds the value and the orientation has
                        // to be flipped. For a boundary face on a negative
                        // direction the value lives on the exterior cell's
                        // positive face and already points into the sector.
                        switch (face.direction) {
                        case FaceDir::XPlus:
                        case FaceDir::YPlus:
                        case FaceDir::ZPlus:
                            return -flows.getFloresIfAvailable(face.interiorGlobalCell,
                                                               face.direction,
                                                               comp);

                        case FaceDir::XMinus:
                            return flows.getFloresIfAvailable(face.exteriorGlobalCell,
                                                              FaceDir::XPlus,
                                                              comp);

                        case FaceDir::YMinus:
                            return flows.getFloresIfAvailable(face.exteriorGlobalCell,
                                                              FaceDir::YPlus,
                                                              comp);

                        case FaceDir::ZMinus:
                            return flows.getFloresIfAvailable(face.exteriorGlobalCell,
                                                              FaceDir::ZPlus,
                                                              comp);

                        default:
                            return 0.0;
                        }
                    }));
        }
    }

    // The boundary transmissibilities are not available while the writer is
    // being constructed, so record them the first time a report step is dumped.
    // The consumer uses these to reproduce the parent's inter-cell
    // transmissibility instead of the (larger) default outer-boundary value.
    void assignFluxDumperTransmissibilities_()
    {
        if (this->fluxTransmissibilitiesAssigned_ || this->fluxDumpers_.empty()) {
            return;
        }

        this->fluxTransmissibilitiesAssigned_ = true;

        const auto& vanguard = this->simulator_.vanguard();
        const auto& problem = this->simulator_.problem();

        for (auto& dumper : this->fluxDumpers_) {
            const auto& faces = dumper.regionBoundaryFaces();

            std::vector<double> trans(faces.size(), 0.0);
            for (std::size_t i = 0; i < faces.size(); ++i) {
                const auto& face = faces[i];
                const auto interior = vanguard.compressedIndex(face.interiorGlobalCell);
                const auto exterior = vanguard.compressedIndex(face.exteriorGlobalCell);
                if (interior < 0 || exterior < 0) {
                    continue;
                }

                // Cartesian adjacency does not imply a connection: faults with
                // throw and pinched-out cells leave neighbouring active cells
                // with no shared face, and hence no transmissibility. Such a
                // face carries no flow.
                trans[i] = problem.transmissibilityOrZero(static_cast<unsigned>(interior),
                                                          static_cast<unsigned>(exterior));
            }

            dumper.setBoundaryTransmissibilities(trans);
        }
    }

    void updateFluxDumpers_(const int reportStepNum, const bool isSubStep)
    {
        if (this->fluxDumpers_.empty()) {
            return;
        }

        // Boundary flow generally varies within a report step, so a record is
        // written per time step by default. The throttle keeps that from
        // producing an unreasonable number of records when the time steps are
        // short; report step boundaries are always written, so the
        // report-step-only behaviour is a strict subset of the default.
        const auto endTime = static_cast<double>(simulator_.time())
            + static_cast<double>(simulator_.timeStepSize());
        const auto elapsed = endTime - this->fluxBoundaryWindowStart_;

        const bool emit = this->fluxBoundaryReportStepsOnly_
            ? !isSubStep
            : (!isSubStep
               || !this->fluxBoundaryHasRecord_
               || (elapsed >= this->fluxBoundaryMinInterval_));

        if (!emit) {
            return;
        }

        this->assignFluxDumperTransmissibilities_();

        const auto& flows = this->outputModule_->getFlows();
        const auto& floresn = this->collectOnIORank_.isParallel()
            ? this->collectOnIORank_.globalFloresn()
            : flows.getFloresn();
        const auto simStep = simulator_.timeStepIndex();

        // The record covers everything since the previous record, which is not
        // the same as the last time step once several steps are accumulated.
        const auto startTime = this->fluxBoundaryWindowStart_;
        const auto stepLength = endTime - this->fluxBoundaryWindowStart_;

        const auto pressurePhaseIdx = FluidSystem::phaseIsActive(FluidSystem::oilPhaseIdx)
            ? FluidSystem::oilPhaseIdx
            : (FluidSystem::phaseIsActive(FluidSystem::gasPhaseIdx)
               ? FluidSystem::gasPhaseIdx
               : FluidSystem::waterPhaseIdx);
        const auto& vanguard = this->simulator_.vanguard();

        if (!flows.anyFlores() && !this->fluxMissingFloresReported_) {
            const auto anyFluxMode =
                std::any_of(this->fluxDumpers_.begin(), this->fluxDumpers_.end(),
                            [](const auto& dumper)
                            {
                                return (static_cast<int>(dumper.data().header.mode)
                                        & static_cast<int>(EclIO::FluxFile::Mode::Flux)) != 0;
                            });

            if (anyFluxMode) {
                this->fluxMissingFloresReported_ = true;
                OpmLog::warning("DUMPFLUX is writing FLUX-mode boundary rates, but no FLORES "
                                "data is available. All boundary rates will be zero and a "
                                "USEFLUX run will behave as a closed region. Add 'FLOWS' and "
                                "'FLORES' to RPTRST in the DUMPFLUX deck.");
            }
        }

        const bool haveCapturedRates =
            this->fluxCapturedFaceRates_.size() == this->fluxDumpers_.size();

        const bool haveAggregatedRates =
            !this->fluxRateTimeWeights_.empty()
            && this->fluxRateSnapshots_.size() == this->fluxDumpers_.size();

        for (std::size_t dumperIdx = 0; dumperIdx < this->fluxDumpers_.size(); ++dumperIdx) {
            auto& dumper = this->fluxDumpers_[dumperIdx];

            auto step = dumper.makeZeroFluxStep(reportStepNum,
                                                simStep,
                                                startTime,
                                                stepLength);

            if (haveAggregatedRates) {
                // Time-weighted average over the time steps that make up this
                // record's window, so that a consumer holding the value
                // constant across the window reproduces the flow over it.
                step.rates = FluxDumper::aggregateRates(EclIO::FluxFile::Sampling::Averaged,
                                                        this->fluxRateSnapshots_[dumperIdx],
                                                        this->fluxRateTimeWeights_);

                // Same treatment for the mass flux, so that the value a
                // consumer holds across the window is the mass actually
                // transferred over it, independent of how the window was
                // subdivided.
                if (this->fluxMassUsable_
                    && (dumperIdx < this->fluxMassSnapshots_.size())
                    && (this->fluxMassSnapshots_[dumperIdx].size()
                        == this->fluxRateTimeWeights_.size()))
                {
                    step.massRates =
                        FluxDumper::aggregateRates(EclIO::FluxFile::Sampling::Averaged,
                                                   this->fluxMassSnapshots_[dumperIdx],
                                                   this->fluxRateTimeWeights_);
                }
            }
            else if (haveCapturedRates) {
                // Cartesian contributions were sampled before the FLORES buffers
                // were moved into the restart solution.
                step.rates = this->fluxCapturedFaceRates_[dumperIdx];
            }

            if (haveAggregatedRates || haveCapturedRates) {
                // NNC contributions are not affected by that move and are
                // resolved here so the parallel gather has completed.
                const auto nncRates = dumper.makeFaceMajorRates(
                    [&floresn, this](const FluxRegions::BoundaryFace& face,
                                     const EclIO::FluxFile::Phase phase)
                    {
                        if (!face.isNnc) {
                            return 0.0;
                        }

                        const auto comp = this->fluxComponentIndex_(phase);
                        const auto key = normalizedNncPair_(face.interiorGlobalCell,
                                                            face.exteriorGlobalCell);
                        const auto it = this->fluxNncPairToIndex_.find(key);
                        if (it == this->fluxNncPairToIndex_.end()) {
                            return 0.0;
                        }

                        const auto nncIdx = static_cast<std::size_t>(it->second);
                        if (nncIdx >= floresn[comp].values.size()) {
                            return 0.0;
                        }

                        // For NNC values we assume the stored direction is cell1->cell2
                        // of the normalized pair and flip sign if the interior is cell1.
                        const auto nncFlux = floresn[comp].values[nncIdx];
                        return (face.interiorGlobalCell == key.second)
                            ? nncFlux
                            : -nncFlux;
                    });

                for (std::size_t i = 0; i < step.rates.size(); ++i) {
                    step.rates[i] += nncRates[i];
                }
            }

            if ((static_cast<int>(dumper.data().header.mode) & static_cast<int>(EclIO::FluxFile::Mode::Pressure)) != 0) {
                step.pressures.clear();
                step.swat.clear();
                step.sgas.clear();
                step.rs.clear();
                step.rv.clear();
                step.temperature.clear();

                const auto& faces = dumper.data().boundaryFaces;
                step.pressures.reserve(faces.size());
                if (FluidSystem::phaseIsActive(FluidSystem::waterPhaseIdx)) {
                    step.swat.reserve(faces.size());
                }
                if (FluidSystem::phaseIsActive(FluidSystem::gasPhaseIdx)) {
                    step.sgas.reserve(faces.size());
                }
                step.rs.reserve(faces.size());
                step.rv.reserve(faces.size());

                for (const auto& face : faces) {
                    const auto compressedExterior = vanguard.compressedIndex(face.exteriorGlobalCell);
                    if (compressedExterior < 0) {
                        step.pressures.push_back(0.0);
                        if (FluidSystem::phaseIsActive(FluidSystem::waterPhaseIdx)) {
                            step.swat.push_back(0.0);
                        }
                        if (FluidSystem::phaseIsActive(FluidSystem::gasPhaseIdx)) {
                            step.sgas.push_back(0.0);
                        }
                        step.rs.push_back(0.0);
                        step.rv.push_back(0.0);
                        continue;
                    }

                    const auto& intQuants = *simulator_.model().cachedIntensiveQuantities(compressedExterior, /*timeIdx=*/0);
                    const auto& fs = intQuants.fluidState();
                    step.pressures.push_back(getValue(fs.pressure(pressurePhaseIdx)));
                    if (FluidSystem::phaseIsActive(FluidSystem::waterPhaseIdx)) {
                        step.swat.push_back(getValue(fs.saturation(FluidSystem::waterPhaseIdx)));
                    }
                    if (FluidSystem::phaseIsActive(FluidSystem::gasPhaseIdx)) {
                        step.sgas.push_back(getValue(fs.saturation(FluidSystem::gasPhaseIdx)));
                    }
                    step.rs.push_back(getValue(fs.Rs()));
                    step.rv.push_back(getValue(fs.Rv()));
                }
            }

            dumper.appendReportStep(step);
        }

        // Start a fresh accumulation window for the next record.
        for (auto& snapshots : this->fluxRateSnapshots_) {
            snapshots.clear();
        }
        for (auto& snapshots : this->fluxMassSnapshots_) {
            snapshots.clear();
        }
        this->fluxRateTimeWeights_.clear();
        this->fluxBoundaryWindowStart_ = endTime;
        this->fluxBoundaryHasRecord_ = true;

        // The whole file is rewritten on every write, so only do so at report
        // step boundaries rather than for every throttled record.
        if (!isSubStep) {
            this->writeFluxDumpers_();
        }
    }

    void writeFluxDumpers_() const
    {
        for (std::size_t i = 0; i < this->fluxDumpers_.size(); ++i) {
            const auto& path = this->fluxOutputPaths_[i];
            this->fluxDumpers_[i].write(path, /*formatted=*/false);
        }
    }

    std::vector<std::string> fluxSummaryKeys_() const
    {
        const auto& schedule = this->simulator_.vanguard().schedule();

        auto keywords = std::unordered_set<std::string>{};

        const auto addAll = [&keywords](std::initializer_list<const char*> names)
        {
            for (const auto* name : names) {
                keywords.insert(name);
            }
        };

        // Surface rates and cumulatives for the conserved quantities of every
        // active phase. A USEFLUX run needs these to reconstruct the
        // contribution of wells that fall outside the sector.
        if (FluidSystem::phaseIsActive(FluidSystem::oilPhaseIdx)) {
            addAll({"WOPR", "WOPT", "WOIR", "WOIT"});
        }
        if (FluidSystem::phaseIsActive(FluidSystem::waterPhaseIdx)) {
            addAll({"WWPR", "WWPT", "WWIR", "WWIT"});
        }
        if (FluidSystem::phaseIsActive(FluidSystem::gasPhaseIdx)) {
            addAll({"WGPR", "WGPT", "WGIR", "WGIT"});
        }

        // Reservoir volume rates and cumulatives.
        addAll({"WVPR", "WVPT", "WVIR", "WVIT"});

        // Everything referenced by the deck's UDQ DEFINE expressions and by
        // ACTIONX conditions, so that both can be evaluated through the
        // standard code paths in the reduced run.
        for (const auto& udq : schedule.template unique<UDQConfig>()) {
            udq.second.required_summary(keywords);
        }

        for (const auto& action : schedule.back().actions.get()) {
            action.required_summary(keywords);
        }

        // required_summary() yields bare keywords, so expand the well and
        // group level ones over the objects they can apply to.
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
                // quantities are evaluated locally by the reduced run and are
                // not expandable without further context.
                break;
            }
        }

        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

        return keys;
    }

    std::vector<double> fluxSummaryValues_(const std::vector<std::string>& keys) const
    {
        std::vector<double> values;
        values.reserve(keys.size());

        const auto& summaryState = this->summaryState();
        for (const auto& key : keys) {
            values.push_back(summaryState.get(key, 0.0));
        }

        return values;
    }

    // Take one snapshot of the parent summary vectors for the DUMPFLUX file.
    //
    // Called once per time step, after Summary::eval() has populated the
    // SummaryState. Rate-type quantities are accumulated over every step and
    // emitted as a time-average over the interval since the previous retained
    // sample, so that a USEFLUX run holding the value piecewise-constant
    // across that interval reproduces this run's production over it exactly.
    void sampleFluxSummary_(const bool isSubStep, const Scalar sampleTime)
    {
        if (this->fluxDumpers_.empty() || !this->collectOnIORank_.isIORank()) {
            return;
        }

        const auto& keys = this->fluxSummaryKeyList_;
        if (keys.empty()) {
            return;
        }

        const auto values = this->fluxSummaryValues_(keys);
        const auto dt = static_cast<double>(this->simulator_.timeStepSize());

        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (this->fluxSummaryKeyTypes_[i] == SummaryConfigNode::Type::Rate) {
                this->fluxSummaryRateAccum_[i] += values[i] * dt;
            }
        }
        this->fluxSummaryAccumDt_ += dt;

        const auto elapsed = static_cast<double>(sampleTime) - this->fluxSummaryLastSampleTime_;
        const bool retain = !this->fluxSummaryHasSample_
            || !isSubStep
            || (elapsed >= this->fluxSummaryMinInterval_);

        if (!retain) {
            return;
        }

        auto sample = values;
        if (this->fluxSummaryAccumDt_ > 0.0) {
            for (std::size_t i = 0; i < keys.size(); ++i) {
                if (this->fluxSummaryKeyTypes_[i] == SummaryConfigNode::Type::Rate) {
                    sample[i] = this->fluxSummaryRateAccum_[i] / this->fluxSummaryAccumDt_;
                }
            }
        }

        for (auto& dumper : this->fluxDumpers_) {
            dumper.appendSummarySample(static_cast<double>(sampleTime), sample);
        }

        std::fill(this->fluxSummaryRateAccum_.begin(), this->fluxSummaryRateAccum_.end(), 0.0);
        this->fluxSummaryAccumDt_ = 0.0;
        this->fluxSummaryLastSampleTime_ = static_cast<double>(sampleTime);
        this->fluxSummaryHasSample_ = true;
    }

    // Fill in the rates of wells that lie outside the USEFLUX region from the
    // parent run, so that summary aggregation covers the whole field.
    //
    // Three conventions matter here: data::Rates holds production as NEGATIVE
    // values, it is expressed in SI units while summary vectors are in the
    // deck's output units, and Summary::eval() skips any well whose dynamic
    // status is SHUT - which is exactly what a well with no active connection
    // would otherwise be.
    void injectParentOnlyWellData_(data::Wells& wellData, const Scalar time)
    {
        const auto* parent = this->simulator_.problem().fluxParentSummary();
        if (parent == nullptr) {
            return;
        }

        const auto& vanguard = this->simulator_.vanguard();
        const auto& schedule = vanguard.schedule();
        const auto& units = vanguard.eclState().getUnits();
        const auto stepIdx =
            static_cast<std::size_t>(std::max(this->simulator_.episodeIndex(), 0));

        for (const auto& wellName : schedule.wellNames(stepIdx)) {
            const auto& well = schedule.getWell(wellName, stepIdx);

            const auto hasLocalConnection =
                std::any_of(well.getConnections().begin(), well.getConnections().end(),
                            [&vanguard](const auto& conn)
                            {
                                return vanguard.compressedIndex(conn.global_index()) >= 0;
                            });

            if (hasLocalConnection) {
                continue;
            }

            const auto isProducer = well.isProducer();
            const auto sign = isProducer ? -1.0 : 1.0;

            auto& target = wellData[wellName];
            target.dynamicStatus = Well::Status::OPEN;

            const auto assign = [&](const data::Rates::opt opt,
                                    const UnitSystem::measure measure,
                                    const std::string& prodKeyword,
                                    const std::string& injKeyword)
            {
                const auto key = (isProducer ? prodKeyword : injKeyword) + ':' + wellName;
                const auto value = parent->valueAt(key, static_cast<double>(time));

                if (std::isfinite(value)) {
                    target.rates.set(opt, sign * units.to_si(measure, value));
                }
            };

            if (FluidSystem::phaseIsActive(FluidSystem::oilPhaseIdx)) {
                assign(data::Rates::opt::oil, UnitSystem::measure::liquid_surface_rate,
                       "WOPR", "WOIR");
            }
            if (FluidSystem::phaseIsActive(FluidSystem::waterPhaseIdx)) {
                assign(data::Rates::opt::wat, UnitSystem::measure::liquid_surface_rate,
                       "WWPR", "WWIR");
            }
            if (FluidSystem::phaseIsActive(FluidSystem::gasPhaseIdx)) {
                assign(data::Rates::opt::gas, UnitSystem::measure::gas_surface_rate,
                       "WGPR", "WGIR");
            }
        }
    }

    Simulator& simulator_;
    std::unique_ptr<OutputModule> outputModule_;
    Scalar restartTimeStepSize_;
    int rank_ ;
    Inplace inplace_;
    std::vector<FluxDumper> fluxDumpers_;
    std::vector<std::string> fluxOutputPaths_;
    std::map<std::pair<int, int>, int> fluxNncPairToIndex_;
    std::vector<std::vector<double>> fluxCapturedFaceRates_;
    std::vector<std::vector<std::vector<double>>> fluxRateSnapshots_;
    std::vector<std::vector<std::vector<double>>> fluxMassSnapshots_;
    std::vector<double> fluxRateTimeWeights_;
    std::vector<std::string> fluxSummaryKeyList_;
    std::vector<SummaryConfigNode::Type> fluxSummaryKeyTypes_;
    std::vector<double> fluxSummaryRateAccum_;
    double fluxSummaryAccumDt_ = 0.0;
    double fluxSummaryLastSampleTime_ = 0.0;
    double fluxSummaryMinInterval_ = 0.0;
    bool fluxSummaryHasSample_ = false;
    double fluxBoundaryMinInterval_ = 0.0;
    double fluxBoundaryWindowStart_ = 0.0;
    bool fluxBoundaryHasRecord_ = false;
    bool fluxBoundaryReportStepsOnly_ = false;
    bool fluxMassUsable_ = false;
    bool fluxMissingFloresReported_ = false;
    bool fluxTransmissibilitiesAssigned_ = false;
};

} // namespace Opm

#endif // OPM_ECL_WRITER_HPP
