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
  "const bool edge_aligned = ftm_edge_aligned_pwm_mode(ftm, channel);"
  "const bool edge_aligned = true;"
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
  [=[        if ((ftm->registers[6] & (1u << 6u)) != 0u)
            ftm_trigger(timing, index);
    } else if (offset == 8)]=]
  [=[        if (false)
            ftm_trigger(timing, index);
    } else if (offset == 8)]=]
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_init_trigger_clock_start src/k22_timing.c
  "const bool clock_stopped = (ftm->sc & 0x18u) == 0u;"
  "const bool clock_stopped = false;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_center_init_trigger_boundary src/k22_timing.c
  "if (minimum_points != 0u &&"
  "if (false &&"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_up_init_trigger src/k22_timing.c
  "if (overflows != 0u && (ftm->registers[6] & (1u << 6u)) != 0u)"
  "if (false && (ftm->registers[6] & (1u << 6u)) != 0u)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_input_synchronizer_delay src/k22_timing.c
  "return filter == 0u ? 3u : 4u + (uint32_t)filter * 4u;"
  "return filter == 0u ? 2u : 4u + (uint32_t)filter * 4u;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_input_filter_delay src/k22_timing.c
  "return filter == 0u ? 3u : 4u + (uint32_t)filter * 4u;"
  "return filter == 0u ? 3u : 3u + (uint32_t)filter * 4u;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_input_edge_selection src/k22_timing.c
  "const bool selected = current ? (edges & 1u) != 0u : (edges & 2u) != 0u;"
  "const bool selected = current ? (edges & 2u) != 0u : (edges & 1u) != 0u;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_input_counter_reset src/k22_timing.c
  "if ((ftm->channel_sc[channel] & 2u) != 0u) {"
  "if (false) {"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_input_value_write src/k22_timing.c
  "} else if (!ftm_input_capture_mode(ftm, channel))"
  "} else if (ftm != NULL || !ftm_input_capture_mode(ftm, channel))"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_output_compare_toggle src/k22_timing.c
  "if ((count & 1u) != 0u)"
  "if ((count & 1u) == 0u)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_output_compare_clear src/k22_timing.c
  "case 2u:\n        ftm->channel_output[channel] = false;"
  "case 2u:\n        ftm->channel_output[channel] = true;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_output_compare_set src/k22_timing.c
  "case 3u:\n        ftm->channel_output[channel] = true;"
  "case 3u:\n        ftm->channel_output[channel] = false;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_edge_pwm_boundary src/k22_timing.c
  "compare != ftm->initial && ftm->counter < compare"
  "compare == ftm->initial && ftm->counter < compare"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_center_pwm_direction src/k22_timing.c
  "ftm->counting_down && ftm->counter == compare"
  "!ftm->counting_down && ftm->counter == compare"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_output_initialization src/k22_timing.c
  "(ftm->registers[2] & (1u << channel)) != 0u"
  "(ftm->registers[2] & (1u << channel)) == 0u"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_explicit_output_initialization src/k22_timing.c
  "if ((value & 2u) != 0u) {"
  "if (false) {"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_software_output src/k22_timing.c
  "const bool software_enabled = (ftm->registers[16] & (1u << channel)) != 0u;"
  "const bool software_enabled = false;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_complementary_software_output src/k22_timing.c
  "(channel & 1u) != 0u && complementary && pair_software_enabled && output &&"
  "(channel & 1u) != 0u && !complementary && pair_software_enabled && output &&"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_output_mask src/k22_timing.c
  [=[    if ((ftm->registers[3] & (1u << channel)) != 0u)
        output = false;]=]
  [=[    if (false)
        output = false;]=]
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_output_polarity src/k22_timing.c
  "if ((ftm->registers[7] & (1u << channel)) != 0u)"
  "if (false)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_write_protection src/k22_timing.c
  "if ((ftm->registers[0] & 4u) == 0u)"
  "if (false)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_write_protection_unlock src/k22_timing.c
  "ftm->write_protection_read) {"
  "true) {"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_register_mask src/k22_timing.c
  "0x000000ffu, 0x000000ffu, 0x000000ffu, 0x000000ffu, 0x7f7f7f7fu,"
  "0x000000ffu, 0x000000ffu, 0x000000ffu, 0x000000ffu, UINT32_MAX,"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_inverting_control src/k22_timing.c
  "        if (complementary && (((channel & 1u) != 0u) != inverted_pair))"
  "        if (complementary && (((channel & 1u) != 0u) == inverted_pair))"
  cortex_m4_device_k22_ftm_combine_test device_k22_ftm_combine)
run_mutation(
  ftm_outmask_system_sync src/k22_timing.c
  "if ((ftm->registers[1] & 8u) == 0u)"
  "if (false)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_invctrl_system_sync src/k22_timing.c
  "if ((ftm->registers[14] & (1u << 4u)) == 0u)"
  "if (false)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_swoctrl_software_sync src/k22_timing.c
  "if ((synconf & (1u << 12u)) != 0u && (synconf & (1u << 5u)) != 0u)"
  "if (false)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_counter_software_sync src/k22_timing.c
  "(enhanced && (synconf & (1u << 8u)) != 0u)"
  "(false && (synconf & (1u << 8u)) != 0u)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  ftm_modulo_write_buffer src/k22_timing.c
  "ftm->modulo_pending = true;"
  "ftm->modulo_pending = false;"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_counter_initial_write_buffer src/k22_timing.c
  "ftm->initial_pending = true;"
  "ftm->initial_pending = false;"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_channel_value_write_buffer src/k22_timing.c
  "ftm->channel_value_pending[channel] = true;"
  "ftm->channel_value_pending[channel] = false;"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_software_write_buffer_sync src/k22_timing.c
  "enhanced ? (synconf & (1u << 9u)) != 0u : true;"
  "enhanced ? false : true;"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_intermediate_load_enable src/k22_timing.c
  "(ftm->registers[17] & (1u << 9u)) != 0u && intermediate"
  "false && intermediate"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_intermediate_load_complete src/k22_timing.c
  "ftm->registers[17] &= ~(1u << 9u);"
  "ftm->registers[17] |= 1u << 9u;"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_hardware_trigger_selection src/k22_timing.c
  "const uint8_t detected = pending & enabled;"
  "const uint8_t detected = pending | enabled;"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_hardware_trigger_mode src/k22_timing.c
  "if ((ftm->registers[14] & 1u) == 0u)"
  "if ((ftm->registers[14] & 1u) != 0u)"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_hardware_output_sync src/k22_timing.c
  "if ((synconf & (1u << 20u)) != 0u && (synconf & (1u << 5u)) != 0u)"
  "if (false && (synconf & (1u << 5u)) != 0u)"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_hardware_write_buffer_sync src/k22_timing.c
  "if ((synconf & (1u << 17u)) != 0u)"
  "if (false)"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_hardware_counter_reset src/k22_timing.c
  "if ((synconf & (1u << 16u)) != 0u) {"
  "if (false) {"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_hardware_deferred_sync src/k22_timing.c
  "    } else {\n        ftm->hardware_sync_pending = true;\n    }"
  "    } else {\n        ftm->hardware_sync_pending = false;\n    }"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_hardware_legacy_mode src/k22_timing.c
  "    if (!enhanced) {\n        if ((ftm->registers[1] & 8u) != 0u)"
  "    if (enhanced) {\n        if ((ftm->registers[1] & 8u) != 0u)"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_hardware_legacy_outmask src/k22_timing.c
  "        if ((ftm->registers[1] & 8u) != 0u)\n            ftm_apply_outmask(ftm);"
  "        if (false)\n            ftm_apply_outmask(ftm);"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_hardware_legacy_reinitialize src/k22_timing.c
  "const bool reset_counter = (ftm->registers[1] & 4u) != 0u;"
  "const bool reset_counter = false;"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_hardware_legacy_pwm_sync src/k22_timing.c
  "const bool synchronize_buffers = (ftm->registers[0] & 8u) == 0u;"
  "const bool synchronize_buffers = (ftm->registers[0] & 8u) != 0u;"
  cortex_m4_device_k22_ftm_synchronization_test device_k22_ftm_synchronization)
run_mutation(
  ftm_combine_selection src/k22_timing.c
  "           (pair & 1u) != 0u && (pair & 4u) == 0u;"
  "           (pair & 1u) == 0u && (pair & 4u) == 0u;"
  cortex_m4_device_k22_ftm_combine_test device_k22_ftm_combine)
run_mutation(
  ftm_combine_capture_exclusion src/k22_timing.c
  "           (pair & 1u) != 0u && (pair & 4u) == 0u;"
  "           (pair & 1u) != 0u && (pair & 4u) != 0u;"
  cortex_m4_device_k22_ftm_combine_test device_k22_ftm_combine)
run_mutation(
  ftm_complementary_selection src/k22_timing.c
  "    return !ftm_quadrature_enabled(ftm) && (pair & 2u) != 0u && (pair & 4u) == 0u &&"
  "    return !ftm_quadrature_enabled(ftm) && (pair & 2u) == 0u && (pair & 4u) == 0u &&"
  cortex_m4_device_k22_ftm_combine_test device_k22_ftm_combine)
run_mutation(
  ftm_combine_compare_order src/k22_timing.c
  "            active = first_compare < second_compare && ftm->counter >= first_compare &&"
  "            active = first_compare > second_compare && ftm->counter >= first_compare &&"
  cortex_m4_device_k22_ftm_combine_test device_k22_ftm_combine)
run_mutation(
  ftm_combine_second_boundary src/k22_timing.c
  "                     ftm->counter < second_compare;"
  "                     ftm->counter <= second_compare;"
  cortex_m4_device_k22_ftm_combine_test device_k22_ftm_combine)
run_mutation(
  ftm_combine_channel_event src/k22_timing.c
  "        if ((output_compare || edge_aligned || combine_compare) &&"
  "        if ((output_compare || edge_aligned) &&"
  cortex_m4_device_k22_ftm_combine_test device_k22_ftm_combine)
run_mutation(
  ftm_combine_legacy_buffer src/k22_timing.c
  "            ftm_center_aligned_pwm_mode(ftm, channel) || ftm_combine_mode(ftm, channel))"
  "            ftm_center_aligned_pwm_mode(ftm, channel))"
  cortex_m4_device_k22_ftm_combine_test device_k22_ftm_combine)
run_mutation(
  ftm_deadtime_enable src/k22_timing.c
  "           (ftm->registers[4] & (1u << (pair_shift + 4u))) != 0u &&"
  "           (ftm->registers[4] & (1u << (pair_shift + 4u))) == 0u &&"
  cortex_m4_device_k22_ftm_deadtime_test device_k22_ftm_deadtime)
run_mutation(
  ftm_deadtime_nonzero src/k22_timing.c
  "           (ftm->registers[5] & 0x3fu) != 0u;"
  "           (ftm->registers[5] & 0x3fu) == 0u;"
  cortex_m4_device_k22_ftm_deadtime_test device_k22_ftm_deadtime)
run_mutation(
  ftm_deadtime_output src/k22_timing.c
  "                      ? ftm->channel_deadtime_output[channel]"
  "                      ? ftm_pre_deadtime_output(ftm, channel)"
  cortex_m4_device_k22_ftm_deadtime_test device_k22_ftm_deadtime)
run_mutation(
  ftm_deadtime_divider src/k22_timing.c
  "const uint8_t shift = divider < 2u ? 0u : divider == 2u ? 2u : 4u;"
  "const uint8_t shift = divider <= 2u ? 0u : divider == 2u ? 2u : 4u;"
  cortex_m4_device_k22_ftm_deadtime_test device_k22_ftm_deadtime)
run_mutation(
  ftm_deadtime_start src/k22_timing.c
  "ftm->channel_deadtime_remaining[channel] = ftm->registers[5] & 0x3fu;"
  "ftm->channel_deadtime_remaining[channel] = (ftm->registers[5] & 0x3fu) + 1u;"
  cortex_m4_device_k22_ftm_deadtime_test device_k22_ftm_deadtime)
run_mutation(
  ftm_deadtime_cancel src/k22_timing.c
  "        } else if (!raw) {"
  "        } else if (raw) {"
  cortex_m4_device_k22_ftm_deadtime_test device_k22_ftm_deadtime)
run_mutation(
  ftm_deadtime_completion src/k22_timing.c
  "            if (ticks >= remaining) {"
  "            if (ticks > remaining) {"
  cortex_m4_device_k22_ftm_deadtime_test device_k22_ftm_deadtime)
run_mutation(
  ftm_deadtime_segmentation src/k22_timing.c
  "ftm_has_deadtime(ftm, channels) || ftm_fault_processing_active(ftm) ? 1u"
  "(ftm_has_deadtime(ftm, channels) && false) || ftm_fault_processing_active(ftm) ? 1u"
  cortex_m4_device_k22_ftm_deadtime_test device_k22_ftm_deadtime)
run_mutation(
  ftm_fault_mode src/k22_timing.c
  "return (uint8_t)((ftm->registers[0] >> 5u) & 3u);"
  "return (uint8_t)((ftm->registers[0] >> 6u) & 3u);"
  cortex_m4_device_k22_ftm_fault_test device_k22_ftm_fault)
run_mutation(
  ftm_fault_even_channels src/k22_timing.c
  "(mode != 1u || (channel & 1u) == 0u);"
  "(mode != 1u || (channel & 1u) != 0u);"
  cortex_m4_device_k22_ftm_fault_test device_k22_ftm_fault)
run_mutation(
  ftm_fault_pair_enable src/k22_timing.c
  "(ftm->registers[4] & (1u << shift)) != 0u &&"
  "(ftm->registers[4] & (1u << shift)) == 0u &&"
  cortex_m4_device_k22_ftm_fault_test device_k22_ftm_fault)
run_mutation(
  ftm_fault_safe_output src/k22_timing.c
  "if (ftm->fault_output_active && ftm_fault_channel_enabled(ftm, channel))"
  "if (false && ftm_fault_channel_enabled(ftm, channel))"
  cortex_m4_device_k22_ftm_fault_test device_k22_ftm_fault)
run_mutation(
  ftm_fault_irq src/k22_timing.c
  "((ftm->registers[0] & 0x80u) != 0u && (ftm->registers[8] & 0x80u) != 0u);"
  "((ftm->registers[0] & 0x80u) != 0u && false);"
  cortex_m4_device_k22_ftm_fault_test device_k22_ftm_fault)
run_mutation(
  ftm_fault_polarity src/k22_timing.c
  "const bool active = ftm->fault_input[input] != polarity;"
  "const bool active = ftm->fault_input[input] == polarity;"
  cortex_m4_device_k22_ftm_fault_test device_k22_ftm_fault)
run_mutation(
  ftm_fault_filter_delay src/k22_timing.c
  "(filter_enable & bit) != 0u && filter_value != 0u ? 4u + filter_value : 3u;"
  "(filter_enable & bit) != 0u && filter_value != 0u ? 3u + filter_value : 3u;"
  cortex_m4_device_k22_ftm_fault_test device_k22_ftm_fault)
run_mutation(
  ftm_fault_sync_delay src/k22_timing.c
  "(filter_enable & bit) != 0u && filter_value != 0u ? 4u + filter_value : 3u;"
  "(filter_enable & bit) != 0u && filter_value != 0u ? 4u + filter_value : 2u;"
  cortex_m4_device_k22_ftm_fault_test device_k22_ftm_fault)
run_mutation(
  ftm_fault_activation src/k22_timing.c
  "ftm->fault_output_active = true;"
  "ftm->fault_output_active = false;"
  cortex_m4_device_k22_ftm_fault_test device_k22_ftm_fault)
run_mutation(
  ftm_fault_clear_read_sequence src/k22_timing.c
  "ftm->fault_aggregate_read && (value & 0x80u) == 0u && active == 0u"
  "true && (value & 0x80u) == 0u && active == 0u"
  cortex_m4_device_k22_ftm_fault_test device_k22_ftm_fault)
run_mutation(
  ftm_fault_cycle_release src/k22_timing.c
  "if (!new_cycle || !ftm->fault_release_pending)"
  "if (!new_cycle || true)"
  cortex_m4_device_k22_ftm_fault_test device_k22_ftm_fault)
run_mutation(
  ftm_fault_segmentation src/k22_timing.c
  "ftm_has_deadtime(ftm, channels) || ftm_fault_processing_active(ftm) ? 1u"
  "ftm_has_deadtime(ftm, channels) || (ftm_fault_processing_active(ftm) && false) ? 1u"
  cortex_m4_device_k22_ftm_fault_test device_k22_ftm_fault)
run_mutation(
  ftm_quadrature_capability src/k22_timing.c
  "timing->ftm[index].quadrature_capable = index == 1u || index == 2u;"
  "timing->ftm[index].quadrature_capable = index == 0u || index == 2u;"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_enable src/k22_timing.c
  "return ftm->quadrature_capable && (ftm->registers[11] & 1u) != 0u;"
  "return (ftm->registers[11] & 1u) != 0u;"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_output_precedence src/k22_timing.c
  "        *high = false;"
  "        *high = true;"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_system_clock src/k22_timing.c
  "    if (ftm_quadrature_enabled(ftm))\n        return;"
  "    if (false)\n        return;"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_filter_enable src/k22_timing.c
  "(ftm->registers[11] & (1u << (7u - channel))) == 0u"
  "(ftm->registers[11] & (1u << (7u - channel))) != 0u"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_polarity src/k22_timing.c
  "(ftm->registers[11] & (1u << (5u - channel))) != 0u"
  "(ftm->registers[11] & (1u << (4u - channel))) != 0u"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_count_direction src/k22_timing.c
  "ftm_quadrature_step(timing, index, phase_b);"
  "ftm_quadrature_step(timing, index, !phase_b);"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_count_edge src/k22_timing.c
  "if (channel == 0u && !before && after)"
  "if (channel == 0u && before && !after)"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_phase_direction src/k22_timing.c
  "const bool increment = channel == 0u ? after != phase_b : phase_a == after;"
  "const bool increment = channel == 0u ? after == phase_b : phase_a != after;"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_increment_wrap src/k22_timing.c
  "if (counter == last) {"
  "if (counter == (uint16_t)(last - 1u)) {"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_decrement_wrap src/k22_timing.c
  "if (counter == first) {"
  "if (counter == (uint16_t)(first + 1u)) {"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_status_write src/k22_timing.c
  "ftm->registers[register_index] = (value & ~6u) | (current & 6u);"
  "ftm->registers[register_index] = value;"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_top_direction src/k22_timing.c
  "ftm->registers[11] |= 2u;"
  "ftm->registers[11] &= ~2u;"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_bottom_direction src/k22_timing.c
  "ftm->registers[11] &= ~2u;"
  "ftm->registers[11] |= 2u;"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
run_mutation(
  ftm_quadrature_input_routing src/k22_timing.c
  "if (ftm_quadrature_enabled(ftm) && channel < 2u) {"
  "if (false && channel < 2u) {"
  cortex_m4_device_k22_ftm_quadrature_test device_k22_ftm_quadrature)
