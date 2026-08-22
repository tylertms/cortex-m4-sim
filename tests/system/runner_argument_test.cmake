execute_process(
  COMMAND "${RUNNER}" missing.bin --max-instructions 1
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

if(result EQUAL 0 OR NOT error MATCHES "^usage: ")
  message(FATAL_ERROR "runner accepted a missing --reset-address: ${output}${error}")
endif()
