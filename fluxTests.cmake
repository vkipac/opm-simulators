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
      -p "${PROJECT_BINARY_DIR}/bin/flux_smoke_shape_check FLUX_DUMP_SMOKE.FLUX 10 1 10 10 1 5"
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
      -p "${PROJECT_BINARY_DIR}/bin/flux_smoke_shape_check FLUX_DUMP_MULTI_SMOKE.FLUX0001 10 1 10 10 1 5 FLUX_DUMP_MULTI_SMOKE.FLUX0002 10 1 10 1 1 10"
  )
endif()
