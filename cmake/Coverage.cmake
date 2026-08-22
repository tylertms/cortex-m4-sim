foreach(required_variable IN
        ITEMS COVERAGE_BUILD_DIRECTORY COVERAGE_REPORT_DIRECTORY
              COVERAGE_SOURCE_DIRECTORY COVERAGE_TARGET_PREFIX GCOV_EXECUTABLE)
  if(NOT DEFINED ${required_variable})
    message(FATAL_ERROR "${required_variable} is required")
  endif()
endforeach()

file(MAKE_DIRECTORY "${COVERAGE_REPORT_DIRECTORY}")
set(coverage_data)
foreach(target_suffix IN ITEMS simulator firmware_image firmware_runner)
  file(
    GLOB_RECURSE target_coverage_data
    LIST_DIRECTORIES false
    "${COVERAGE_BUILD_DIRECTORY}/CMakeFiles/${COVERAGE_TARGET_PREFIX}_${target_suffix}.dir/*.gcda"
  )
  list(APPEND coverage_data ${target_coverage_data})
endforeach()

if(NOT coverage_data)
  message(FATAL_ERROR "No simulator coverage data was generated")
endif()

set(coverage_summary)
foreach(coverage_file IN LISTS coverage_data)
  execute_process(
    COMMAND
      "${GCOV_EXECUTABLE}" --branch-counts --branch-probabilities
      --preserve-paths --source-prefix "${COVERAGE_SOURCE_DIRECTORY}"
      "${coverage_file}"
    WORKING_DIRECTORY "${COVERAGE_REPORT_DIRECTORY}"
    RESULT_VARIABLE coverage_result
    OUTPUT_VARIABLE coverage_output
    ERROR_VARIABLE coverage_error)
  string(APPEND coverage_summary "${coverage_output}${coverage_error}")
  if(NOT coverage_result EQUAL 0)
    message(FATAL_ERROR "gcov failed for ${coverage_file}")
  endif()
endforeach()

file(WRITE "${COVERAGE_REPORT_DIRECTORY}/summary.txt" "${coverage_summary}")
message(STATUS "Coverage report: ${COVERAGE_REPORT_DIRECTORY}")
