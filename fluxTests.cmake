opm_set_test_driver(${PROJECT_SOURCE_DIR}/tests/run-test.sh "")

if (BUILD_FLOW)
  opm_add_executable(
    TARGET
      flux_smoke_shape_check
    SOURCES
      tests/flux/flux_smoke_shape_check.cpp
    LIBRARIES
      opmcommon
  )

  opm_add_executable(
    TARGET
      flux_smoke_mutate
    SOURCES
      tests/flux/flux_smoke_mutate.cpp
    LIBRARIES
      opmcommon
  )

  opm_add_executable(
    TARGET
      flux_smoke_compare
    SOURCES
      tests/flux/flux_smoke_compare.cpp
    LIBRARIES
      opmcommon
  )

  opm_add_executable(
    TARGET
      sector_vs_parent_compare
    SOURCES
      tests/flux/sector_vs_parent_compare.cpp
    LIBRARIES
      opmcommon
  )

  set(_flux_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+flux_dump_smoke)

  opm_add_test(flux_dump_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_flux_result_path}
      -f FLUX_DUMP_SMOKE
        -p "${PROJECT_BINARY_DIR}/bin/flux_smoke_shape_check FLUX_DUMP_SMOKE.FLUX 10 1 10 10 1 5 FLUX"
  )

  set(_flux_multi_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+flux_dump_multi_smoke)

  opm_add_test(flux_dump_multi_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_flux_multi_result_path}
      -f FLUX_DUMP_MULTI_SMOKE
      -p "${PROJECT_BINARY_DIR}/bin/flux_smoke_shape_check FLUX_DUMP_MULTI_SMOKE.FLUX0001 10 1 10 10 1 5 FLUX FLUX_DUMP_MULTI_SMOKE.FLUX0002 10 1 10 1 1 10 FLUX"
  )

  set(_flux_pressure_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+flux_dump_pressure_smoke)

  opm_add_test(flux_dump_pressure_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_flux_pressure_result_path}
      -f FLUX_DUMP_PRESSURE_SMOKE
      -p "${PROJECT_BINARY_DIR}/bin/flux_smoke_shape_check FLUX_DUMP_PRESSURE_SMOKE.FLUX 10 1 10 1 1 2 PRESSURE"
  )

  set(_flux_both_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+flux_dump_both_smoke)

  opm_add_test(flux_dump_both_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_flux_both_result_path}
      -f FLUX_DUMP_BOTH_SMOKE
      -p "${PROJECT_BINARY_DIR}/bin/flux_smoke_shape_check FLUX_DUMP_BOTH_SMOKE.FLUX 10 1 10 10 1 5 BOTH"
  )

  set(_flux_invalid_type_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+flux_dump_invalid_type_smoke)

  opm_add_test(flux_dump_invalid_type_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_flux_invalid_type_result_path}
      -f FLUX_DUMP_INVALID_TYPE_SMOKE
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-flux-invalid-type-smoke.sh ${PROJECT_BINARY_DIR}/bin/flux_smoke_shape_check"
  )

  set(_flux_mixed_case_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+flux_dump_mixed_case_smoke)

  opm_add_test(flux_dump_mixed_case_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_flux_mixed_case_result_path}
      -f FLUX_DUMP_MIXED_CASE_SMOKE
      -p "${PROJECT_BINARY_DIR}/bin/flux_smoke_shape_check FLUX_DUMP_MIXED_CASE_SMOKE.FLUX 10 1 10 10 1 5 BOTH"
  )

  set(_make_flux_pressure_equivalence_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+make_flux_pressure_equivalence_smoke)

  opm_add_test(make_flux_pressure_equivalence_smoke
    DEPENDS
      flux_smoke_compare
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_make_flux_pressure_equivalence_result_path}
      -f FLUX_DUMP_PRESSURE_SMOKE
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-make-flux-equivalence-smoke.sh ${PROJECT_BINARY_DIR}/bin/make_flux ${PROJECT_BINARY_DIR}/bin/flux_smoke_compare ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_DUMP_PRESSURE_SMOKE.DATA FLUX_DUMP_PRESSURE_SMOKE pressure 10"
  )

  set(_make_flux_pressure_equivalence_mpi_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+make_flux_pressure_equivalence_mpi_smoke)

  opm_add_test(make_flux_pressure_equivalence_mpi_smoke
    DEPENDS
      flux_smoke_compare
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_make_flux_pressure_equivalence_mpi_result_path}
      -f FLUX_DUMP_PRESSURE_SMOKE
      -n 2
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-make-flux-equivalence-smoke.sh ${PROJECT_BINARY_DIR}/bin/make_flux ${PROJECT_BINARY_DIR}/bin/flux_smoke_compare ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_DUMP_PRESSURE_SMOKE.DATA FLUX_DUMP_PRESSURE_SMOKE pressure 10 --compare-arg=--ignore-pressures --compare-arg=--ignore-transmissibilities"
  )

    set(_make_flux_fluxnum_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+make_flux_fluxnum_smoke)

    opm_add_test(make_flux_fluxnum_smoke
      DEPENDS
        flux_smoke_compare
        flux_smoke_shape_check
      EXE_TARGET
        flow_blackoil
      DRIVER_ARGS
        -i ${PROJECT_SOURCE_DIR}/tests/flux
        -r ${_make_flux_fluxnum_result_path}
        -f FLUX_DUMP_PRESSURE_SMOKE
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-make-flux-fluxnum-smoke.sh ${PROJECT_BINARY_DIR}/bin/make_flux ${PROJECT_BINARY_DIR}/bin/flux_smoke_compare ${PROJECT_BINARY_DIR}/bin/flux_smoke_shape_check ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_DUMP_PRESSURE_SMOKE.DATA ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_SMOKE_ONE_SECTOR.grdecl ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_SMOKE_TWO_SECTORS.grdecl"
    )

    # The summary reader is built and shipped by opm-common, not by
    # opm-simulators.
    find_program(OPM_SUMMARY_BIN
      NAMES summary
      HINTS ${PROJECT_SOURCE_DIR}/../opm-common/build/bin
            ${CMAKE_INSTALL_PREFIX}/bin
      PATH_SUFFIXES bin)

    if(OPM_SUMMARY_BIN)
      set(_flux_fault_boundary_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+flux_fault_boundary_smoke)

      opm_add_test(flux_fault_boundary_smoke
        EXE_TARGET
          flow_blackoil
        DRIVER_ARGS
          -i ${PROJECT_SOURCE_DIR}/tests/flux
          -r ${_flux_fault_boundary_result_path}
          -f FLUX_DUMP_FAULT_SMOKE
          -p "bash ${PROJECT_SOURCE_DIR}/tests/check-flux-fault-boundary-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${OPM_SUMMARY_BIN} ${PROJECT_BINARY_DIR}/bin/inspect_flux ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_DUMP_FAULT_SMOKE.DATA ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_FAULT_SMOKE.DATA 1,1,5 2,1,5 I+"
      )

      # Same seal, but on a K face, which a cell reaches through a different
      # slot in its boundary-face list because its I neighbours are inside the
      # sector. That is the arrangement the column case above cannot produce.
      set(_flux_kfault_boundary_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+flux_kfault_boundary_smoke)

      opm_add_test(flux_kfault_boundary_smoke
        EXE_TARGET
          flow_blackoil
        DRIVER_ARGS
          -i ${PROJECT_SOURCE_DIR}/tests/flux
          -r ${_flux_kfault_boundary_result_path}
          -f FLUX_DUMP_KFAULT_SMOKE
          -p "bash ${PROJECT_SOURCE_DIR}/tests/check-flux-fault-boundary-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${OPM_SUMMARY_BIN} ${PROJECT_BINARY_DIR}/bin/inspect_flux ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_DUMP_KFAULT_SMOKE.DATA ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_KFAULT_SMOKE.DATA 5,1,1 5,1,2 K+"
      )
    endif()

    set(_flux_minpv_boundary_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+flux_minpv_boundary_smoke)

    opm_add_test(flux_minpv_boundary_smoke
      DEPENDS
        flux_smoke_compare
      EXE_TARGET
        flow_blackoil
      DRIVER_ARGS
        -i ${PROJECT_SOURCE_DIR}/tests/flux
        -r ${_flux_minpv_boundary_result_path}
        -f FLUX_DUMP_MINPV_SMOKE
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-flux-minpv-boundary-smoke.sh ${PROJECT_BINARY_DIR}/bin/make_flux ${PROJECT_BINARY_DIR}/bin/inspect_flux ${PROJECT_BINARY_DIR}/bin/flux_smoke_compare ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_DUMP_MINPV_SMOKE.DATA ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_SMOKE_ONE_SECTOR.grdecl"
    )

    set(_make_flux_flux_equivalence_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+make_flux_flux_equivalence_smoke)

    opm_add_test(make_flux_flux_equivalence_smoke
      DEPENDS
        flux_smoke_compare
      EXE_TARGET
        flow_blackoil
      DRIVER_ARGS
        -i ${PROJECT_SOURCE_DIR}/tests/flux
        -r ${_make_flux_flux_equivalence_result_path}
        -f FLUX_DUMP_FLORES_SMOKE
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-make-flux-equivalence-smoke.sh ${PROJECT_BINARY_DIR}/bin/make_flux ${PROJECT_BINARY_DIR}/bin/flux_smoke_compare ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_DUMP_FLORES_SMOKE.DATA FLUX_DUMP_FLORES_SMOKE flux 0"
    )

    set(_make_flux_flux_equivalence_mpi_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+make_flux_flux_equivalence_mpi_smoke)

    opm_add_test(make_flux_flux_equivalence_mpi_smoke
      DEPENDS
        flux_smoke_compare
      EXE_TARGET
        flow_blackoil
      DRIVER_ARGS
        -i ${PROJECT_SOURCE_DIR}/tests/flux
        -r ${_make_flux_flux_equivalence_mpi_result_path}
        -f FLUX_DUMP_FLORES_SMOKE
        -n 2
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-make-flux-equivalence-smoke.sh ${PROJECT_BINARY_DIR}/bin/make_flux ${PROJECT_BINARY_DIR}/bin/flux_smoke_compare ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_DUMP_FLORES_SMOKE.DATA FLUX_DUMP_FLORES_SMOKE flux 0 --compare-arg=--ignore-transmissibilities"
    )

  set(_make_flux_pressure_sector_regression_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+make_flux_pressure_sector_regression_smoke)

  opm_add_test(make_flux_pressure_sector_regression_smoke
    DEPENDS
      flux_smoke_compare
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_make_flux_pressure_sector_regression_result_path}
        -f FLUX_DUMP_PSECTOR_SMOKE
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-make-flux-equivalence-smoke.sh ${PROJECT_BINARY_DIR}/bin/make_flux ${PROJECT_BINARY_DIR}/bin/flux_smoke_compare ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_DUMP_PSECTOR_SMOKE.DATA FLUX_DUMP_PSECTOR_SMOKE pressure inline:box,8,8,3,22,13,3 --compare-arg=--ignore-times"
  )

  set(_sector_vs_parent_pressure_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+sector_vs_parent_pressure_smoke)

  opm_add_test(sector_vs_parent_pressure_smoke
    DEPENDS
      sector_vs_parent_compare
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/data/sector
      -r ${_sector_vs_parent_pressure_result_path}
      -f BASE_DUMPFLUX_FLORES
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-sector-vs-parent-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_BINARY_DIR}/bin/make_flux ${PROJECT_BINARY_DIR}/bin/sector_vs_parent_compare ${PROJECT_SOURCE_DIR}/tests/data/sector/BASE_SECTOR_PRES.DATA ${PROJECT_SOURCE_DIR}/tests/data/sector/BASE_SECTOR.MAP"
  )

  set(_useflux_pressure_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_pressure_smoke)

  opm_add_test(useflux_pressure_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_pressure_result_path}
      -f FLUX_DUMP_PRESSURE_SMOKE
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-pressure-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_PRESSURE_SMOKE.DATA"
  )

  set(_useflux_flux_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_flux_smoke)

  opm_add_test(useflux_flux_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_flux_result_path}
      -f FLUX_DUMP_USEFLUX_FLUX_SMOKE
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-flux-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_FLUX_SMOKE.DATA"
  )

  # compareECL is built and shipped by opm-common, not by opm-simulators.
  find_program(OPM_COMPARE_ECL_BIN
    NAMES compareECL
    HINTS ${PROJECT_SOURCE_DIR}/../opm-common/build/bin
          ${CMAKE_INSTALL_PREFIX}/bin
    PATH_SUFFIXES bin)

  if(OPM_COMPARE_ECL_BIN)
    set(_onewell_useflux_pressure_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+onewell_useflux_pressure_smoke)

    opm_add_test(onewell_useflux_pressure_smoke
      DEPENDS
        flux_smoke_shape_check
      EXE_TARGET
        flow_blackoil
      DRIVER_ARGS
        -i ${PROJECT_SOURCE_DIR}/tests/data/sector
        -r ${_onewell_useflux_pressure_result_path}
        -f ONEWELL_DUMPFLUX_PRESSURE_1D
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-onewell-useflux-pressure-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${OPM_COMPARE_ECL_BIN} ${PROJECT_SOURCE_DIR}/tests/data/sector/ONEWELL_DUMPFLUX_PRESSURE_1D.DATA ${PROJECT_SOURCE_DIR}/tests/data/sector/ONEWELL_USEFLUX_PRESSURE_1D.DATA"
    )

    set(_onewell_useflux_flux_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+onewell_useflux_flux_smoke)

    opm_add_test(onewell_useflux_flux_smoke
      DEPENDS
        flux_smoke_shape_check
      EXE_TARGET
        flow_blackoil
      DRIVER_ARGS
        -i ${PROJECT_SOURCE_DIR}/tests/data/sector
        -r ${_onewell_useflux_flux_result_path}
        -f ONEWELL_DUMPFLUX_FLUX_1D
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-onewell-useflux-flux-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${OPM_COMPARE_ECL_BIN} ${PROJECT_SOURCE_DIR}/tests/data/sector/ONEWELL_DUMPFLUX_FLUX_1D.DATA ${PROJECT_SOURCE_DIR}/tests/data/sector/ONEWELL_USEFLUX_FLUX_1D.DATA"
    )

    set(_twowell_useflux_flux_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+twowell_useflux_flux_smoke)

    opm_add_test(twowell_useflux_flux_smoke
      DEPENDS
        flux_smoke_shape_check
      EXE_TARGET
        flow_blackoil
      DRIVER_ARGS
        -i ${PROJECT_SOURCE_DIR}/tests/data/sector
        -r ${_twowell_useflux_flux_result_path}
        -f TWOWELL_DUMPFLUX_FLUX_1D
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-twowell-useflux-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${OPM_COMPARE_ECL_BIN} ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_DUMPFLUX_FLUX_1D.DATA ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_USEFLUX_FLUX_1D.DATA"
    )

    set(_twowell_useflux_live_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+twowell_useflux_live_smoke)

    opm_add_test(twowell_useflux_live_smoke
      EXE_TARGET
        flow_blackoil
      DRIVER_ARGS
        -i ${PROJECT_SOURCE_DIR}/tests/data/sector
        -r ${_twowell_useflux_live_result_path}
        -f TWOWELL_DUMPFLUX_LIVE_1D
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-twowell-useflux-live-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${OPM_COMPARE_ECL_BIN} ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_DUMPFLUX_LIVE_1D.DATA ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_USEFLUX_LIVE_1D.DATA"
    )

    set(_twowell_useflux_nnc_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+twowell_useflux_nnc_smoke)

    opm_add_test(twowell_useflux_nnc_smoke
      EXE_TARGET
        flow_blackoil
      DRIVER_ARGS
        -i ${PROJECT_SOURCE_DIR}/tests/data/sector
        -r ${_twowell_useflux_nnc_result_path}
        -f TWOWELL_DUMPFLUX_NNC_1D
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-twowell-useflux-nnc-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${OPM_COMPARE_ECL_BIN} ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_DUMPFLUX_NNC_1D.DATA ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_USEFLUX_NNC_1D.DATA"
    )

    set(_twowell_useflux_pressure_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+twowell_useflux_pressure_smoke)
    opm_add_test(twowell_useflux_pressure_smoke
      DEPENDS
        flux_smoke_shape_check
      EXE_TARGET
        flow_blackoil
      DRIVER_ARGS
        -i ${PROJECT_SOURCE_DIR}/tests/data/sector
        -r ${_twowell_useflux_pressure_result_path}
        -f TWOWELL_DUMPFLUX_PRESSURE_1D
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-twowell-useflux-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${OPM_COMPARE_ECL_BIN} ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_DUMPFLUX_PRESSURE_1D.DATA ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_USEFLUX_PRESSURE_1D.DATA"
    )

    set(_twowell_useflux_smry_fallback_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+twowell_useflux_smry_fallback_smoke)
    opm_add_test(twowell_useflux_smry_fallback_smoke
      DEPENDS
        flux_smoke_mutate
      EXE_TARGET
        flow_blackoil
      DRIVER_ARGS
        -i ${PROJECT_SOURCE_DIR}/tests/data/sector
        -r ${_twowell_useflux_smry_fallback_result_path}
        -f TWOWELL_DUMPFLUX_FLUX_1D
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-twowell-useflux-smry-fallback-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_BINARY_DIR}/bin/flux_smoke_mutate ${OPM_COMPARE_ECL_BIN} ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_DUMPFLUX_FLUX_1D.DATA ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_USEFLUX_FLUX_1D.DATA"
    )

    set(_twowell_useflux_inactive_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+twowell_useflux_inactive_smoke)

    # The region map is dimensioned over every cell of the grid, so it also
    # assigns a region to inactive cells. This case has an inactive cell inside
    # the region, which is what separates the region from the set of cells that
    # actually take part in the flow.
    opm_add_test(twowell_useflux_inactive_smoke
      DEPENDS
        flux_smoke_shape_check
      EXE_TARGET
        flow_blackoil
      DRIVER_ARGS
        -i ${PROJECT_SOURCE_DIR}/tests/data/sector
        -r ${_twowell_useflux_inactive_result_path}
        -f TWOWELL_DUMPFLUX_INACTIVE_2D
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-twowell-useflux-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${OPM_COMPARE_ECL_BIN} ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_DUMPFLUX_INACTIVE_2D.DATA ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_USEFLUX_INACTIVE_2D.DATA"
    )

    set(_twowell_useflux_cadence_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+twowell_useflux_cadence_smoke)

    # Boundary data is written per time step by default; the report-step cadence
    # of earlier versions remains available behind a switch.
    opm_add_test(twowell_useflux_cadence_smoke
      DEPENDS
        flux_smoke_shape_check
      EXE_TARGET
        flow_blackoil
      DRIVER_ARGS
        -i ${PROJECT_SOURCE_DIR}/tests/data/sector
        -r ${_twowell_useflux_cadence_result_path}
        -f TWOWELL_DUMPFLUX_FLUX_1D
        -p "bash ${PROJECT_SOURCE_DIR}/tests/check-twowell-useflux-cadence-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${OPM_COMPARE_ECL_BIN} ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_DUMPFLUX_FLUX_1D.DATA ${PROJECT_SOURCE_DIR}/tests/data/sector/TWOWELL_USEFLUX_FLUX_1D.DATA"
    )
  endif()

  set(_useflux_both_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_both_smoke)

  opm_add_test(useflux_both_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_both_result_path}
      -f FLUX_DUMP_USEFLUX_BOTH_SMOKE
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-both-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_BOTH_SMOKE.DATA"
  )

  set(_useflux_suffix_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_suffix_smoke)

  opm_add_test(useflux_suffix_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_suffix_result_path}
      -f FLUX_DUMP_USEFLUX_SUFFIX_SMOKE
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-suffix-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_SUFFIX_SMOKE.DATA"
  )

  set(_useflux_suffix2_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_suffix2_smoke)

  opm_add_test(useflux_suffix2_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_suffix2_result_path}
      -f FLUX_DUMP_USEFLUX_SUFFIX_SMOKE
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-suffix2-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_SUFFIX_SMOKE.DATA"
  )

  set(_useflux_missing_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_missing_smoke)

  opm_add_test(useflux_missing_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_missing_result_path}
      -f FLUX_DUMP_USEFLUX_SUFFIX_SMOKE
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-missing-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_MISSING_SMOKE.DATA"
  )

  set(_useflux_invalid_suffix_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_invalid_suffix_smoke)

  opm_add_test(useflux_invalid_suffix_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_invalid_suffix_result_path}
      -f FLUX_DUMP_USEFLUX_SUFFIX_SMOKE
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-invalid-suffix-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_INVALID_SUFFIX_SMOKE.DATA"
  )

  set(_useflux_missing_mpi_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_missing_mpi_smoke)

  opm_add_test(useflux_missing_mpi_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_missing_mpi_result_path}
      -f FLUX_DUMP_USEFLUX_SUFFIX_SMOKE
      -n 2
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-missing-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_MISSING_SMOKE.DATA 2"
  )

  set(_useflux_invalid_suffix_mpi_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_invalid_suffix_mpi_smoke)

  opm_add_test(useflux_invalid_suffix_mpi_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_invalid_suffix_mpi_result_path}
      -f FLUX_DUMP_USEFLUX_SUFFIX_SMOKE
      -n 2
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-invalid-suffix-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_INVALID_SUFFIX_SMOKE.DATA 2"
  )

  set(_useflux_mixed_suffix_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_mixed_suffix_smoke)

  opm_add_test(useflux_mixed_suffix_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_mixed_suffix_result_path}
      -f FLUX_DUMP_USEFLUX_SUFFIX_SMOKE
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-mixed-suffix-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_MIXED_SUFFIX_SMOKE.DATA"
  )

  set(_useflux_mixed_suffix_mpi_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_mixed_suffix_mpi_smoke)

  opm_add_test(useflux_mixed_suffix_mpi_smoke
    DEPENDS
      flux_smoke_shape_check
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_mixed_suffix_mpi_result_path}
      -f FLUX_DUMP_USEFLUX_SUFFIX_SMOKE
      -n 2
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-mixed-suffix-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_MIXED_SUFFIX_SMOKE.DATA 2"
  )

  set(_useflux_invalid_rate_nan_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_invalid_rate_nan_smoke)

  opm_add_test(useflux_invalid_rate_nan_smoke
    DEPENDS
      flux_smoke_shape_check
      flux_smoke_mutate
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_invalid_rate_nan_result_path}
      -f FLUX_DUMP_USEFLUX_FLUX_SMOKE
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-invalid-rate-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_BINARY_DIR}/bin/flux_smoke_mutate ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_FLUX_SMOKE.DATA nan"
  )

  set(_useflux_invalid_rate_inf_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_invalid_rate_inf_smoke)

  opm_add_test(useflux_invalid_rate_inf_smoke
    DEPENDS
      flux_smoke_shape_check
      flux_smoke_mutate
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_invalid_rate_inf_result_path}
      -f FLUX_DUMP_USEFLUX_FLUX_SMOKE
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-invalid-rate-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_BINARY_DIR}/bin/flux_smoke_mutate ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_FLUX_SMOKE.DATA inf"
  )

  set(_useflux_invalid_rate_nan_mpi_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_invalid_rate_nan_mpi_smoke)

  opm_add_test(useflux_invalid_rate_nan_mpi_smoke
    DEPENDS
      flux_smoke_shape_check
      flux_smoke_mutate
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_invalid_rate_nan_mpi_result_path}
      -f FLUX_DUMP_USEFLUX_FLUX_SMOKE
      -n 2
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-invalid-rate-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_BINARY_DIR}/bin/flux_smoke_mutate ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_FLUX_SMOKE.DATA nan 2"
  )

  set(_useflux_invalid_rate_inf_mpi_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+useflux_invalid_rate_inf_mpi_smoke)

  opm_add_test(useflux_invalid_rate_inf_mpi_smoke
    DEPENDS
      flux_smoke_shape_check
      flux_smoke_mutate
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_useflux_invalid_rate_inf_mpi_result_path}
      -f FLUX_DUMP_USEFLUX_FLUX_SMOKE
      -n 2
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-useflux-invalid-rate-smoke.sh ${PROJECT_BINARY_DIR}/bin/flow_blackoil ${PROJECT_BINARY_DIR}/bin/flux_smoke_mutate ${PROJECT_SOURCE_DIR}/tests/flux/FLUX_USE_FLUX_SMOKE.DATA inf 2"
  )
endif()
