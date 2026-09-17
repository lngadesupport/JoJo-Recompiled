if(NOT DEFINED JOJO_SOURCE_DIR)
  message(FATAL_ERROR "JOJO_SOURCE_DIR is required")
endif()

set(invalid_manifest "${JOJO_SOURCE_DIR}/tests/fixtures/production-readiness-invalid.tsv")
set(check_script "${JOJO_SOURCE_DIR}/cmake/CheckProductionReadiness.cmake")

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -DJOJO_SOURCE_DIR=${JOJO_SOURCE_DIR}
    -DJOJO_READINESS_FILE=${invalid_manifest}
    -DJOJO_SKIP_DOC_CHECKS=ON
    -P ${check_script}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error_output
)

if(result EQUAL 0)
  message(FATAL_ERROR "false verified production readiness claim was accepted")
endif()

string(FIND "${error_output}" "verified workstream" rejected_verified)
if(rejected_verified EQUAL -1)
  message(FATAL_ERROR "invalid readiness fixture failed for an unexpected reason: ${error_output}")
endif()
