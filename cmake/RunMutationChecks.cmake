function(run_mutation name source_file before after target test_pattern)
  if(DEFINED MUTATION_FILTER AND NOT "${MUTATION_FILTER}" STREQUAL "" AND
     NOT name MATCHES "${MUTATION_FILTER}")
    return()
  endif()
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
  flash_resource_alignment src/k22_data.c
  "(address & (length - 1u)) != 0u" "false"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  flash_sector_alignment src/k22_data.c
  "valid = (address & 0x0fu) == 0u &&\n                flash_memory_range"
  "valid = flash_memory_range"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  flash_block_alignment src/k22_data.c
  "valid = (address & 0x0fu) == 0u &&\n                flash_block_range"
  "valid = flash_block_range"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  flash_eeprom_block_guard src/k22_data.c
  "!(data_flash && data->flexram_eeprom)" "true"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  flash_protection_range src/k22_data.c "region <= last" "region < last"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  sysmpu_access src/kinetis_k22.c "if (!sysmpu_access_allowed("
  "if (false && !sysmpu_access_allowed(" cortex_m4_device_k22_sysmpu_integration_test
  device_k22_sysmpu_integration)
run_mutation(
  flash_controller_program src/kinetis_k22_peripherals.c
  "return kinetis_k22_flash_controller_write(context, address, size, value);"
  "return false && kinetis_k22_flash_controller_write(context, address, size, value);"
  cortex_m4_device_k22_integration_complete_test device_k22_integration_complete)
run_mutation(
  fmc_bank_control src/kinetis_k22.c
  "0x4001f004u + (uint32_t)bank * 4u"
  "((void)bank, 0x4001f004u)" cortex_m4_device_k22_fmc_complete_test
  device_k22_fmc_complete)
run_mutation(
  fmc_bank_identity src/kinetis_k22.c
  "device->fmc_bank[candidate][set] == bank"
  "device->fmc_bank[candidate][set] == 0u" cortex_m4_device_k22_fmc_complete_test
  device_k22_fmc_complete)
run_mutation(
  fmc_flexnvm_access src/kinetis_k22.c
  "if (!flash_access_allowed(device, access, flash_master, false))\n            return false;\n        const uint32_t offset = address - device->profile->flexnvm_address;"
  "if (false && !flash_access_allowed(device, access, flash_master, false))\n            return false;\n        const uint32_t offset = address - device->profile->flexnvm_address;"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  fmc_flexram_access src/kinetis_k22.c
  "return flash_access_allowed(device, access, flash_master, false) &&\n               k22_data_read(device->data, address, size, value);"
  "return k22_data_read(device->data, address, size, value);"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  fmc_flexnvm_cpu_write src/kinetis_k22.c
  "if (access != CORTEX_M4_ACCESS_DEBUG ||\n            !k22_data_write(device->data, address, size, value))"
  "if ((false && access != CORTEX_M4_ACCESS_DEBUG) ||\n            !k22_data_write(device->data, address, size, value))"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  flash_collision_bank src/k22_data.c
  "return (address & 0x800000u) != 0u ? 2u : 1u;"
  "return (address & 0x800000u) != 0u ? 1u : 2u;"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  flash_collision_block src/k22_data.c
  "(data->flash_busy_banks & bank) == 0u || !same_block"
  "(data->flash_busy_banks & bank) == 0u || (false && !same_block)"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  flash_collision_acknowledge src/k22_data.c
  "else\n            flash_update_interrupts(data);"
  "else\n            (void)data;"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  flash_collision_irq src/kinetis_k22_peripherals.c
  "14, 15, 16, 18, 19, 39, 73, 56, 72, 40, 41, 70, 23,"
  "14, 15, 16, 18, 18, 39, 73, 56, 72, 40, 41, 70, 23,"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  package_dac1 src/k22_package.c
  "if (selected->profile == K22_PROFILE_MK22FN51212 && peripheral == K22_PERIPHERAL_DAC1)\n        return selected->package != K22_PACKAGE_FX_88_HVQFN;"
  "if (selected->profile == K22_PROFILE_MK22FN51212 && peripheral == K22_PERIPHERAL_DAC1)\n        return selected->package == K22_PACKAGE_FX_88_HVQFN;"
  cortex_m4_device_k22_package_test device_k22_package)
run_mutation(
  io_irq_deassert src/kinetis_k22_peripherals.c
  "    kinetis_k22_refresh_signals(device);\n    return handled;\n}\n\nstatic void reset_manifest"
  "    (void)device;\n    return handled;\n}\n\nstatic void reset_manifest"
  cortex_m4_device_k22_integration_complete_test device_k22_integration_complete)
run_mutation(
  dma_fixed_priority src/k22_data.c "priority > selected_priority"
  "priority < selected_priority" cortex_m4_device_k22_data_complete_test
  device_k22_data_complete)
run_mutation(
  dma_round_robin src/k22_data.c
  "for (uint8_t step = 1u; step <= data->dma_channel_count; step++)"
  "for (uint8_t step = 0u; step <= data->dma_channel_count; step++)"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  dma_halt src/k22_data.c "(load_bytes(data->dma, 0u, 4u) & 0x20u) == 0u"
  "false" cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  dma_debug_stall src/k22_data.c
  "(load_bytes(data->dma, 0u, 4u) & 2u) != 0u && data->debug_halted"
  "(load_bytes(data->dma, 0u, 4u) & 2u) != 0u && false"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  dma_priority_error src/k22_data.c
  "(load_bytes(data->dma, 0u, 4u) & 4u) == 0u && !dma_priorities_valid(data)"
  "(load_bytes(data->dma, 0u, 4u) == 0u) && dma_priorities_valid(data)"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  dma_hardware_status src/k22_data.c
  "data->dma_hardware_requests &= (uint16_t)~(1u << channel);"
  "data->dma_hardware_requests |= (uint16_t)(1u << channel);"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  dma_priority_reset src/k22_data.c
  "data->dma[dma_priority_offset(channel)] = channel;"
  "data->dma[dma_priority_offset(channel)] = 0u;"
  cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  dma_halt_on_error src/k22_data.c
  "(load_bytes(data->dma, 0u, 4u) & 0x10u) != 0u"
  "false" cortex_m4_device_k22_data_complete_test device_k22_data_complete)
run_mutation(
  pmc_flag_acknowledge src/k22_timing.c
  "if (((uint8_t)value & 0x40u) != 0u)"
  "if (((uint8_t)value & 0x80u) != 0u)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  pmc_reset_enable_lock src/k22_timing.c
  "if (!timing->pmc_lvdre_written)"
  "if (timing->pmc_lvdre_written)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  pmc_detect_reset src/k22_timing.c
  "if ((timing->pmc[0] & 0x10u) != 0u)"
  "if ((timing->pmc[0] & 0x10u) == 0u)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  pmc_warning_interrupt src/k22_timing.c
  "const bool warning = (timing->pmc[1] & 0xa0u) == 0xa0u;"
  "const bool warning = (timing->pmc[1] & 0xa0u) == 0u;"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  smc_lls_entry src/k22_timing.c
  "stop_mode == 3u && (timing->smc[0] & 8u) != 0u"
  "stop_mode == 4u && (timing->smc[0] & 8u) != 0u"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  smc_vlls_entry src/k22_timing.c
  "stop_mode == 4u && (timing->smc[0] & 2u) != 0u"
  "stop_mode == 5u && (timing->smc[0] & 2u) != 0u"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  llwu_rising_edge src/k22_timing.c
  "edge == 1u && !previous && high"
  "edge == 2u && !previous && high"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  llwu_module_enable src/k22_timing.c
  "(timing->llwu[4] & (1u << module)) != 0u"
  "(timing->llwu[4] & (1u << module)) == 0u"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
run_mutation(
  llwu_filter_acknowledge src/k22_timing.c
  "timing->llwu[offset] & 0x80u & (uint8_t)~value"
  "((timing->llwu[offset] & 0x80u) | (uint8_t)value)"
  cortex_m4_device_k22_timing_complete_test device_k22_timing_complete)
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
run_mutation(
  wdog_initial_window src/k22_timing.c
  "timing->wdog_update_deadline = 256u;"
  "timing->wdog_update_deadline = 255u;"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  wdog_unlock_sequence_window src/k22_timing.c
  "timing->wdog_sequence_deadline = timing->wdog_bus_cycles + 20u;\n        return;"
  "timing->wdog_sequence_deadline = timing->wdog_bus_cycles + 19u;\n        return;"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  wdog_window_boundary src/k22_timing.c
  "timing->wdog_counter < window)"
  "timing->wdog_counter <= window)"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  wdog_initial_debug_window src/k22_timing.c
  "timing->wdog_initial_unlock_required && !timing->wdog_initial_debug_pause &&"
  "timing->wdog_initial_unlock_required && timing->wdog_initial_debug_pause &&"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  wdog_test_disable src/k22_timing.c
  "(timing->wdog[0] & 0x4c00u) != 0x0c00u"
  "(timing->wdog[0] & 0x0c00u) != 0x0c00u"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  wdog_prescaler_mask src/k22_timing.c
  "timing->wdog[11] &= 0x0700u;"
  "timing->wdog[11] &= 0x07ffu;"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  wdog_timer_output src/k22_timing.c
  "register_value = (uint16_t)timing->wdog_counter;"
  "register_value = timing->wdog[index];"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  wdog_reset_count src/k22_timing.c
  "(uint16_t)(wdog_reset_count + 1u)"
  "(uint16_t)(wdog_reset_count + 2u)"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  ewm_service_window src/k22_timing.c
  "timing->ewm_service_deadline = timing->wdog_bus_cycles + 15u;"
  "timing->ewm_service_deadline = timing->wdog_bus_cycles + 14u;"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  ewm_sleep_pause src/k22_timing.c
  "timing->ewm_output || timing->cpu_sleeping)"
  "timing->ewm_output)"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  ewm_service_pause src/k22_timing.c
  "timing->wdog_bus_cycles + timing->ewm_service_remaining;"
  "timing->wdog_bus_cycles + timing->ewm_service_remaining - 1u;"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  ewm_high_boundary src/k22_timing.c
  "timing->ewm_counter >= timing->ewm_cmph)"
  "timing->ewm_counter > timing->ewm_cmph)"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  ewm_low_boundary src/k22_timing.c
  "timing->ewm_counter > timing->ewm_cmpl &&"
  "timing->ewm_counter >= timing->ewm_cmpl &&"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  ewm_input_polarity src/k22_timing.c
  "timing->ewm_input ==\n                                            ((timing->ewm_ctrl & 2u) != 0u);"
  "timing->ewm_input !=\n                                            ((timing->ewm_ctrl & 2u) != 0u);"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  ewm_output_latch src/k22_timing.c
  "timing->ewm_output = true;\n    update_watchdog_irq(timing);"
  "timing->ewm_output = false;\n    update_watchdog_irq(timing);"
  cortex_m4_device_k22_watchdog_complete_test device_k22_watchdog_complete)
run_mutation(
  cmt_time_resolution src/kinetis_k22_peripherals.c
  "uint64_t unit = clock_ticks * 8u;"
  "uint64_t unit = clock_ticks * 7u;"
  cortex_m4_device_k22_cmt_complete_test device_k22_cmt_complete)
run_mutation(
  cmt_primary_prescaler src/kinetis_k22_peripherals.c
  "raw_load(device, K22_CMT + 0x0au, 1u) + 1u;"
  "raw_load(device, K22_CMT + 0x0au, 1u) + 2u;"
  cortex_m4_device_k22_cmt_complete_test device_k22_cmt_complete)
run_mutation(
  cmt_fsk_selection src/kinetis_k22_peripherals.c
  "if ((control & 0x0cu) == 4u)"
  "if ((control & 0x0cu) != 4u)"
  cortex_m4_device_k22_cmt_complete_test device_k22_cmt_complete)
run_mutation(
  cmt_stop_boundary src/kinetis_k22_peripherals.c
  "if (device->cmt_stop_pending ||"
  "if (!device->cmt_stop_pending ||"
  cortex_m4_device_k22_cmt_complete_test device_k22_cmt_complete)
run_mutation(
  cmt_dma_irq_exclusion src/kinetis_k22_peripherals.c
  "(control & 0x82u) == 0x82u && (dma & 1u) == 0u);"
  "(control & 0x82u) == 0x82u && (dma & 1u) != 0u);"
  cortex_m4_device_k22_cmt_complete_test device_k22_cmt_complete)
run_mutation(
  cmt_dma_acceptance src/kinetis_k22_peripherals.c
  "device->cmt_dma_pending = k22_data_dma_request(device->data, 47u);"
  "device->cmt_dma_pending = true;"
  cortex_m4_device_k22_cmt_complete_test device_k22_cmt_complete)
run_mutation(
  cmt_dma_completion_source src/kinetis_k22_peripherals.c
  "if (source == 47u && device->cmt_dma_pending)"
  "if (source == 46u && device->cmt_dma_pending)"
  cortex_m4_device_k22_cmt_complete_test device_k22_cmt_complete)
run_mutation(
  cmt_eoc_read_clear src/kinetis_k22_peripherals.c
  "address == K22_CMT + 7u || address == K22_CMT + 9u"
  "address == K22_CMT + 6u || address == K22_CMT + 8u"
  cortex_m4_device_k22_cmt_complete_test device_k22_cmt_complete)
run_mutation(
  cmt_deep_sleep_pause src/kinetis_k22_peripherals.c
  "device->cpu->sleeping && (device->cpu->scr & 4u) != 0u"
  "device->cpu->sleeping && (device->cpu->scr & 4u) == 0u"
  cortex_m4_device_k22_cmt_complete_test device_k22_cmt_complete)
run_mutation(
  cmt_output_polarity src/kinetis_k22_peripherals.c
  "*high = active == ((output & 0x40u) != 0u);"
  "*high = active != ((output & 0x40u) != 0u);"
  cortex_m4_device_k22_cmt_complete_test device_k22_cmt_complete)
run_mutation(
  cmt_output_delay src/kinetis_k22_peripherals.c
  "primary + 2u : primary * 2u + 3u;"
  "primary + 1u : primary * 2u + 3u;"
  cortex_m4_device_k22_cmt_complete_test device_k22_cmt_complete)
run_mutation(
  cmt_extended_space src/kinetis_k22_peripherals.c
  "device->cmt_extended_space ? 0u : (mark + 1u) * unit;"
  "device->cmt_extended_space ? (mark + 1u) * unit : 0u;"
  cortex_m4_device_k22_cmt_complete_test device_k22_cmt_complete)
run_mutation(
  usbdcd_reset_interrupt_enable src/k22_usbdcd.c
  "usbdcd->control = CONTROL_IE;" "usbdcd->control = 0u;"
  cortex_m4_device_k22_usbdcd_complete_test device_k22_usbdcd_complete)
run_mutation(
  usbdcd_start_elapsed_time src/k22_usbdcd.c
  "(usbdcd->timer0 & 0x03ff0000u) | (usbdcd->timer0 >> 16u)"
  "(usbdcd->timer0 & 0x03ff0000u)"
  cortex_m4_device_k22_usbdcd_complete_test device_k22_usbdcd_complete)
run_mutation(
  usbdcd_active_configuration_lock src/k22_usbdcd.c
  "if ((usbdcd->status & STATUS_ACTIVE) != 0u)" "if (false)"
  cortex_m4_device_k22_usbdcd_complete_test device_k22_usbdcd_complete)
run_mutation(
  usbdcd_clock_unit src/k22_usbdcd.c
  "return (usbdcd->clock & 1u) != 0u ? speed * 1000u : speed;"
  "return (usbdcd->clock & 1u) != 0u ? speed : speed * 1000u;"
  cortex_m4_device_k22_usbdcd_complete_test device_k22_usbdcd_complete)
run_mutation(
  usbdcd_standard_result src/k22_usbdcd.c
  "finish(usbdcd, 2u, 1u, false);" "finish(usbdcd, 2u, 2u, false);"
  cortex_m4_device_k22_usbdcd_complete_test device_k22_usbdcd_complete)
run_mutation(
  usbdcd_primary_delay src/k22_usbdcd.c
  "set_phase(usbdcd, K22_USBDCD_SECONDARY_DELAY);"
  "set_phase(usbdcd, K22_USBDCD_SECONDARY_DETECTION);"
  cortex_m4_device_k22_usbdcd_complete_test device_k22_usbdcd_complete)
run_mutation(
  usbdcd_debounce_restart src/k22_usbdcd.c
  "usbdcd->phase_elapsed = 0u;\n        } else if"
  "usbdcd->phase_elapsed = usbdcd->phase_elapsed;\n        } else if"
  cortex_m4_device_k22_usbdcd_complete_test device_k22_usbdcd_complete)
run_mutation(
  usbdcd_timeout_boundary src/k22_usbdcd.c
  "if (elapsed >= 1000u &&" "if (elapsed > 1000u &&"
  cortex_m4_device_k22_usbdcd_complete_test device_k22_usbdcd_complete)
run_mutation(
  usbdcd_timer_saturation src/k22_usbdcd.c
  "if (elapsed < 0x0fffu)" "if (elapsed <= 0x0fffu)"
  cortex_m4_device_k22_usbdcd_complete_test device_k22_usbdcd_complete)
run_mutation(
  usbdcd_software_reset_start src/k22_usbdcd.c
  "CONTROL_IE | CONTROL_BC12 | CONTROL_START"
  "CONTROL_IE | CONTROL_BC12"
  cortex_m4_device_k22_usbdcd_complete_test device_k22_usbdcd_complete)
run_mutation(
  usbdcd_irq_enable src/k22_usbdcd.c
  "(usbdcd->control & (CONTROL_IF | CONTROL_IE)) == (CONTROL_IF | CONTROL_IE)"
  "(usbdcd->control & CONTROL_IF) == CONTROL_IF"
  cortex_m4_device_k22_usbdcd_complete_test device_k22_usbdcd_complete)
run_mutation(
  usbdcd_dedicated_result src/k22_usbdcd.c
  "usbdcd->charger == KINETIS_K22_USB_CHARGER_DEDICATED ? 3u : 2u"
  "usbdcd->charger == KINETIS_K22_USB_CHARGER_DEDICATED ? 2u : 3u"
  cortex_m4_device_k22_usbdcd_complete_test device_k22_usbdcd_complete)
run_mutation(
  usbdcd_timeout_error_retention src/k22_usbdcd.c
  "error || timeout != 0u ? STATUS_ERROR : 0u"
  "error ? STATUS_ERROR : 0u"
  cortex_m4_device_k22_usbdcd_complete_test device_k22_usbdcd_complete)
run_mutation(
  fmc_cache_geometry src/kinetis_k22_peripherals.c
  "const uint8_t sets = 4u;" "const uint8_t sets = 8u;"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  fmc_tag_invalidation src/kinetis_k22_peripherals.c
  "raw_store(device, tag_address, 4u, 0u);"
  "raw_store(device, tag_address, 4u, raw_load(device, tag_address, 4u) & ~1u);"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  fmc_data_invalidation src/kinetis_k22_peripherals.c
  "for (uint8_t word = 0u; word < 4u; word++)"
  "for (uint8_t word = 0u; word < 1u; word++)"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  fmc_privileged_write src/kinetis_k22_peripherals.c
  "location.id == K22_PERIPHERAL_FMC &&\n         access == CORTEX_M4_ACCESS_UNPRIVILEGED_DATA"
  "location.id == K22_PERIPHERAL_FMC && false"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  fmc_dma_master src/kinetis_k22.c
  "memory_read_unprotected(device, address, size, CORTEX_M4_ACCESS_DATA, 2u, value)"
  "memory_read_unprotected(device, address, size, CORTEX_M4_ACCESS_DATA, 1u, value)"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  fmc_cache_enable src/kinetis_k22.c
  "return (control & (access == CORTEX_M4_ACCESS_INSTRUCTION ? 8u : 16u)) != 0u;"
  "return false && (control & (access == CORTEX_M4_ACCESS_INSTRUCTION ? 8u : 16u)) != 0u;"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  fmc_cache_hit src/kinetis_k22.c
  "fmc_raw_load(device, fmc_tag_address(candidate, set)) == tag"
  "fmc_raw_load(device, fmc_tag_address(candidate, set)) != tag"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  fmc_cache_lock src/kinetis_k22.c
  "(locked & (1u << way)) == 0u" "(locked & (1u << way)) != 0u"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  fmc_lru_selection src/kinetis_k22.c
  "device->fmc_age[way][set] < oldest"
  "device->fmc_age[way][set] > oldest"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  fmc_partitioned_replacement src/kinetis_k22.c
  "return instruction ? way < 2u : way >= 2u;"
  "return instruction ? way >= 2u : way < 2u;"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  fmc_partitioned_data_way src/kinetis_k22.c
  "return instruction ? way < 3u : way == 3u;"
  "return instruction ? way < 3u : way == 2u;"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
run_mutation(
  fmc_cache_word_order src/kinetis_k22.c
  "(3u - memory_word) * 4u" "memory_word * 4u"
  cortex_m4_device_k22_fmc_complete_test device_k22_fmc_complete)
