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

#ifndef OPM_FLUX_DUMPER_HPP
#define OPM_FLUX_DUMPER_HPP

#include <opm/io/eclipse/FluxFile.hpp>
#include <opm/simulators/flow/flux/FluxRegions.hpp>

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace Opm {

class FluxDumper
{
public:
    using Mode = EclIO::FluxFile::Mode;
    using Sampling = EclIO::FluxFile::Sampling;

    struct ReportStepData {
        int reportStep = 0;
        int simStep = 0;
        double startTime = 0.0;
        double stepLength = 0.0;

        std::vector<double> rates;
        std::vector<double> pressures;
        std::vector<double> swat;
        std::vector<double> sgas;
        std::vector<double> rs;
        std::vector<double> rv;
        std::vector<double> temperature;
        std::vector<double> massRates;
        std::vector<double> relPerm;
        std::vector<double> capPressure;
        std::vector<double> interiorPressure;
        std::vector<double> massRateDerivative;
    };

    FluxDumper(std::string parentCaseName,
               int regionId,
               const std::array<int, 3>& parentDims,
               const FluxRegions::Region& region,
               Mode mode,
               Sampling sampling,
               int phaseMask,
               const std::vector<double>& boundaryTransmissibilities = {});

    static std::vector<double> aggregateRates(
        Sampling sampling,
        const std::vector<std::vector<double>>& rateSnapshots,
        const std::vector<double>& timeWeights);

    using FaceFluxAccessor = std::function<double(const FluxRegions::BoundaryFace&, EclIO::FluxFile::Phase)>;
    std::vector<double> makeFaceMajorRates(const FaceFluxAccessor& getFaceFlux) const;

    ReportStepData makeZeroFluxStep(int reportStep,
                                    int simStep,
                                    double startTime,
                                    double stepLength) const;

    void appendReportStep(const ReportStepData& stepData);
    void write(const std::string& filename, bool formatted = false) const;
    void setSummaryKeys(std::vector<std::string> summaryKeys);

    /// Append one snapshot of the parent summary vectors.
    ///
    /// Samples are independent of the report-step sequence.  \p time is the
    /// end of the interval the sample represents, in seconds, and \p values
    /// must have one entry per registered summary key.  Rate-type entries
    /// are expected to be time-averaged over the interval since the previous
    /// sample; see EclIO::FluxFile::SummarySample.
    void appendSummarySample(double time, std::vector<double> values);

    /// Record the minimum interval, in seconds, enforced between consecutive
    /// summary samples.  Diagnostic only.
    void setSummaryMinSampleInterval(double interval);

    /// Record the minimum interval, in seconds, enforced between consecutive
    /// boundary records.  Diagnostic only.
    void setBoundaryMinSampleInterval(double interval);

    void setBoundaryTransmissibilities(const std::vector<double>& boundaryTransmissibilities);

    //! \brief PVT region of the cell on the far side of each boundary face.
    void setBoundaryExteriorPvtRegions(const std::vector<int>& exteriorPvtRegions);

    const EclIO::FluxFile::Data& data() const;
    const std::vector<FluxRegions::BoundaryFace>& regionBoundaryFaces() const;

private:
    static int countPhases(int phaseMask);
    static bool hasFluxMode(Mode mode);
    static bool hasPressureMode(Mode mode);

    std::size_t numBoundaryFaces() const;
    std::size_t expectedRateSize() const;
    void validateStepData(const ReportStepData& stepData) const;

    std::vector<FluxRegions::BoundaryFace> regionBoundaryFaces_;

    EclIO::FluxFile::Data data_;
};

} // namespace Opm

#endif // OPM_FLUX_DUMPER_HPP
