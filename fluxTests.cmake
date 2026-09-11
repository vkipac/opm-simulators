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
      -p "${PROJECT_BINARY_DIR}/bin/flux_smoke_shape_check FLUX_DUMP_PRESSURE_SMOKE.FLUX 10 1 10 10 1 5 PRESSURE"
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
endif()
