function(run_mutation name source_file before after target test_pattern)
  set(mutation_root "${MUTATION_DIRECTORY}/${name}")
  file(REMOVE_RECURSE "${mutation_root}")
  file(MAKE_DIRECTORY "${mutation_root}")
  file(COPY "${SOURCE_DIRECTORY}/CMakeLists.txt" "${SOURCE_DIRECTORY}/cmake"
       "${SOURCE_DIRECTORY}/include" "${SOURCE_DIRECTORY}/src"
       "${SOURCE_DIRECTORY}/tests" DESTINATION "${mutation_root}")
  set(mutated_file "${mutation_root}/${source_file}")
  file(READ "${mutated_file}" contents)
  string(REPLACE "${before}" "${after}" mutated "${contents}")
  if(mutated STREQUAL contents)
    message(FATAL_ERROR "Mutation ${name} did not match the source")
  endif()
  file(WRITE "${mutated_file}" "${mutated}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${mutation_root}" -B "${mutation_root}/build"
            -G Ninja -D CMAKE_BUILD_TYPE=Release -D CMAKE_C_COMPILER=${C_COMPILER}
    RESULT_VARIABLE configure_result
    OUTPUT_QUIET ERROR_QUIET)
  if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Mutation ${name} did not configure")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${mutation_root}/build" --target "${target}"
    RESULT_VARIABLE build_result OUTPUT_QUIET ERROR_QUIET)
  if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Mutation ${name} did not compile")
  endif()
  execute_process(
    COMMAND "${CTEST_COMMAND}" --test-dir "${mutation_root}/build" -R
            "${test_pattern}" --output-on-failure
    RESULT_VARIABLE test_result OUTPUT_QUIET ERROR_QUIET)
  if(test_result EQUAL 0)
    message(FATAL_ERROR "Mutation ${name} survived")
  endif()
  message(STATUS "Mutation ${name} was rejected")
endfunction()

run_mutation(
  it_condition src/cortex_m4_it.c "cpu->it_state == 0 ||"
  "cpu->it_state != 0 ||" cortex_m4_core_it_complete_test core_it_complete)
run_mutation(
  flash_reset src/k22_data.c "data->flash[0] = 0x80u;"
  "data->flash[0] = 0u;" cortex_m4_device_k22_data_complete_test
  device_k22_data_complete)
run_mutation(
  flash_command_layout src/k22_data.c
  "static const uint8_t offsets[12] = {7u, 6u, 5u,  4u,  11u, 10u,"
  "static const uint8_t offsets[12] = {4u, 6u, 5u,  7u,  11u, 10u,"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  flash_partition src/k22_data.c "case 0x0fu:" "case 0x0eu:"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  flash_protection_range src/k22_data.c "region <= last" "region < last"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  sysmpu_access src/kinetis_k22.c "if (!sysmpu_access_allowed("
  "if (false && !sysmpu_access_allowed(" cortex_m4_device_k22_sysmpu_integration_test
  device_k22_sysmpu_integration)
run_mutation(
  lptmr_filter src/k22_timing.c "const uint32_t threshold = 1u << prescale;"
  "const uint32_t threshold = 2u << prescale;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  pit_reenable src/k22_timing.c
  "const bool was_enabled = (pit->control & 1u) != 0u;"
  "const bool was_enabled = false;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  lptmr_initial_input src/k22_timing.c
  "timing->lptmr_observed_active = lptmr_selected_active(timing);"
  "timing->lptmr_observed_active = true;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  adc_alternate_trigger src/kinetis_k22_peripherals.c
  "if ((selection & 0x80u) != 0u && (selection & 15u) == source)"
  "if (false && (selection & 15u) == source)"
  cortex_m4_device_k22_integration_complete_test device_k22_integration_complete)
run_mutation(
  ftm_channel_trigger src/k22_timing.c
  "if (trigger_bit != UINT8_MAX &&"
  "if (false && trigger_bit != UINT8_MAX &&"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  pit_debug_freeze src/k22_timing.c
  "(timing->pit_mcr & 1u) != 0u && timing->debug_halted"
  "(timing->pit_mcr & 1u) != 0u && false"
  cortex_m4_device_k22_integration_complete_test device_k22_integration_complete)
run_mutation(
  rtc_access_control src/k22_timing.c
  "if (!rtc_access_allowed(timing->rtc_war, offset))"
  "if (false && !rtc_access_allowed(timing->rtc_war, offset))"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  rtc_compensation src/k22_timing.c
  "return (uint32_t)(32768 - compensation);"
  "(void)compensation; return 32768u;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  rtc_oscillator_gate src/k22_timing.c
  "(timing->rtc_cr & 0x100u) == 0u ||"
  "false ||"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_irq_aggregation src/k22_timing.c
  "asserted = asserted || (ftm->channel_sc[channel] & 0xc0u) == 0xc0u;"
  "asserted = asserted || false;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_disabled_channel src/k22_timing.c
  "if ((ftm->channel_sc[channel] & 0x3cu) != 0u && compare >= first &&"
  "if (compare >= first &&"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_channel_clear_sequence src/k22_timing.c
  "(value & 0x80u) == 0u && ftm->channel_flag_read[channel]"
  "(value & 0x80u) == 0u && true"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_center_aligned_counting src/k22_timing.c
  "if ((ftm->sc & (1u << 5u)) != 0u)"
  "if (false && (ftm->sc & (1u << 5u)) != 0u)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_center_aligned_overflow src/k22_timing.c
  "ftm_phase_crossing_count(phase, ticks, period, span + 1u)"
  "ftm_phase_crossing_count(phase, ticks, period, span)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_center_aligned_duty_boundary src/k22_timing.c
  "if (compare <= first || compare >= last)"
  "if (compare < first || compare >= last)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_debug_freeze src/k22_timing.c
  "|| clock_select == 0 || timing->debug_halted)"
  "|| clock_select == 0 || false)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_periodic_overflow src/k22_timing.c
  "if (count >= first_set)"
  "if (count > first_set)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_overflow_counter_reset src/k22_timing.c
  "ftm->overflow_count = 0u;"
  "ftm->overflow_count = ftm->overflow_count;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_init_trigger_counter_write src/k22_timing.c
  [=[ftm->overflow_count = 0u;
        if ((ftm->registers[6] & (1u << 6u)) != 0u)]=]
  [=[ftm->overflow_count = 0u;
        if (false)]=]
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_init_trigger_clock_start src/k22_timing.c
  "const bool clock_stopped = (ftm->sc & 0x18u) == 0u;"
  "const bool clock_stopped = false;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_center_init_trigger_boundary src/k22_timing.c
  "ftm_phase_crossing_count(phase, ticks, period, 0u) != 0u"
  "ftm_phase_crossing_count(phase, ticks, period, 1u) != 0u"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_up_init_trigger src/k22_timing.c
  "if (overflows != 0u && (ftm->registers[6] & (1u << 6u)) != 0u)"
  "if (false && (ftm->registers[6] & (1u << 6u)) != 0u)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
