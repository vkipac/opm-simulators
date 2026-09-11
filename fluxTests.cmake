opm_set_test_driver(${PROJECT_SOURCE_DIR}/tests/run-test.sh "")

if (TARGET flow_blackoil)
  set(_flux_result_path ${PROJECT_BINARY_DIR}/tests/results/flux/flow_blackoil+flux_dump_smoke)

  opm_add_test(flux_dump_smoke
    EXE_TARGET
      flow_blackoil
    DRIVER_ARGS
      -i ${PROJECT_SOURCE_DIR}/tests/flux
      -r ${_flux_result_path}
      -f FLUX_DUMP_SMOKE
      -p "bash ${PROJECT_SOURCE_DIR}/tests/check-flux-smoke.sh"
  )
endif()
