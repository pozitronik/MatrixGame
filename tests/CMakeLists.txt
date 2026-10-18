# MatrixGame - licensed under GPLv2 or any later version.
add_custom_target(matrixgame_tests)

function(add_matrix_test_executable target)
    add_executable(${target} ${ARGN} test_support.hpp test_support.cpp)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE MatrixLib)
    if(MSVC)
        target_link_libraries(${target} PRIVATE dbghelp)
    endif()
    target_compile_options(${target} PRIVATE ${COMPILE_OPTIONS} "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
    target_compile_definitions(${target} PRIVATE ${COMPILE_DEFINITIONS})
    if(NOT MSVC)
        # Keep the console tests runnable without copying MinGW runtime DLLs.
        target_link_options(${target} PRIVATE -static)
    endif()
    add_dependencies(matrixgame_tests ${target})
endfunction()

function(add_matrix_test name target)
    add_test(NAME ${name} COMMAND ${target} ${name})
    set_tests_properties(${name} PROPERTIES
        LABELS "engine;asset-free;${ARGN}"
        TIMEOUT 30
    )
endfunction()

add_matrix_test_executable(matrixgame_audio_tests audio_tests.cpp)
target_link_libraries(matrixgame_audio_tests PRIVATE MatrixAudio)
add_matrix_test(game.audio.pcm_formats matrixgame_audio_tests gameplay audio)
add_matrix_test(game.audio.malformed_wave matrixgame_audio_tests gameplay audio)
add_matrix_test(game.audio.legacy_wave_headers matrixgame_audio_tests gameplay audio)
add_matrix_test(game.audio.handles_cache matrixgame_audio_tests gameplay audio)
add_matrix_test(game.audio.parameters matrixgame_audio_tests gameplay audio)
add_matrix_test(game.audio.master_volume matrixgame_audio_tests gameplay audio)
add_matrix_test(game.audio.missing_data matrixgame_audio_tests gameplay audio)
add_matrix_test(game.audio.device_failure matrixgame_audio_tests gameplay audio)
add_matrix_test(game.audio.shutdown_order matrixgame_audio_tests gameplay audio)
foreach(music queue order failures format_change volume device_failure)
  add_matrix_test(game.audio.music_${music} matrixgame_audio_tests gameplay audio)
endforeach()
if(MATRIXGAME_MUSIC)
  target_link_libraries(matrixgame_audio_tests PRIVATE VORBISENC)
  add_matrix_test(game.audio.vorbis_decode matrixgame_audio_tests gameplay audio dependencies)
  add_matrix_test(game.audio.vorbis_rejects matrixgame_audio_tests gameplay audio dependencies)
endif()

add_matrix_test_executable(matrixgame_audio_frontend_tests audio_frontend_tests.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_audio_frontend_tests PRIVATE MatrixGameInternal)
add_matrix_test(game.audio.layer_lifetime matrixgame_audio_frontend_tests gameplay audio)
add_matrix_test(game.audio.clear_reentrancy matrixgame_audio_frontend_tests gameplay audio)
add_matrix_test(game.audio.stale_layer_slot matrixgame_audio_frontend_tests gameplay audio)
add_matrix_test(game.audio.host_layer_lifetime matrixgame_audio_frontend_tests gameplay audio)
add_matrix_test(game.audio.frontend_missing matrixgame_audio_frontend_tests gameplay audio)
add_matrix_test(game.audio.no_optional_packages matrixgame_audio_frontend_tests gameplay audio startup)
add_matrix_test(game.audio.host_routing matrixgame_audio_frontend_tests gameplay audio)
add_matrix_test(game.audio.default_catalog matrixgame_audio_frontend_tests audio startup)
add_matrix_test(game.audio.map_spawner_catalog matrixgame_audio_frontend_tests audio startup)
add_matrix_test(game.audio.master_level_retained matrixgame_audio_frontend_tests audio startup)
add_matrix_test(game.audio.mapping_override matrixgame_audio_frontend_tests audio startup)
add_matrix_test(game.audio.mapping_fallback matrixgame_audio_frontend_tests audio startup)
add_matrix_test(game.audio.startup_unavailable matrixgame_audio_frontend_tests audio startup)
add_matrix_test(game.audio.installed_device_error matrixgame_audio_frontend_tests gameplay audio)
add_matrix_test(game.audio.device_recovery matrixgame_audio_frontend_tests gameplay audio startup)
add_matrix_test(game.audio.device_recovery_limit matrixgame_audio_frontend_tests audio startup)
if(MATRIXGAME_MUSIC)
  add_matrix_test(game.audio.music_sessions matrixgame_audio_frontend_tests gameplay audio startup)
  add_matrix_test(game.audio.music_unavailable matrixgame_audio_frontend_tests gameplay audio startup)
else()
  add_matrix_test(game.audio.music_not_built matrixgame_audio_frontend_tests gameplay audio startup)
endif()
add_matrix_test(game.audio.music_track_names matrixgame_audio_frontend_tests gameplay audio startup)
add_matrix_test(game.audio.host_music_routing matrixgame_audio_frontend_tests gameplay audio)

add_matrix_test_executable(matrixgame_base_tests base_tests.cpp)

add_matrix_test(base.crc.empty matrixgame_base_tests base crc)
add_matrix_test(base.crc.compatibility matrixgame_base_tests base crc)
add_matrix_test(base.crc.incremental matrixgame_base_tests base crc)
add_matrix_test(base.crc.byte_range matrixgame_base_tests base crc)
add_matrix_test(base.point.arithmetic matrixgame_base_tests base geometry)
add_matrix_test(base.point.distance matrixgame_base_tests base geometry)
add_matrix_test(base.rect.empty matrixgame_base_tests base geometry)
add_matrix_test(base.rect.contains matrixgame_base_tests base geometry)
add_matrix_test(base.rect.normalize matrixgame_base_tests base geometry)

add_matrix_test_executable(matrixgame_storage_tests storage_tests.cpp)
add_matrix_test(base.storage.nested_growth matrixgame_storage_tests base storage)
add_matrix_test(base.storage.legacy_format matrixgame_storage_tests base storage)
add_matrix_test(base.storage.records_and_columns matrixgame_storage_tests base storage)
add_matrix_test(base.storage.deletion_and_reuse matrixgame_storage_tests base storage)
add_matrix_test(base.storage.schema_copy matrixgame_storage_tests base storage)
add_matrix_test(base.storage.uncompressed_round_trip matrixgame_storage_tests base storage)
add_matrix_test(base.storage.compressed_round_trip matrixgame_storage_tests base storage)
add_matrix_test(base.storage.empty_round_trip matrixgame_storage_tests base storage)
add_matrix_test(base.storage.column_compression matrixgame_storage_tests base storage)
add_matrix_test(base.storage.partial_load_cleanup matrixgame_storage_tests base storage)
add_matrix_test(base.storage.invalid_tag matrixgame_storage_tests base storage)
add_matrix_test(base.storage.unsupported_version matrixgame_storage_tests base storage)

add_matrix_test_executable(matrixgame_dependency_tests dependency_tests.cpp dependency_fixtures.hpp)
add_matrix_test(base.dependencies.linked_versions matrixgame_dependency_tests base dependencies)
add_matrix_test(base.dependencies.legacy_configuration matrixgame_dependency_tests base dependencies storage)
add_matrix_test(base.dependencies.multiblock_configuration matrixgame_dependency_tests base dependencies storage)
add_matrix_test(base.dependencies.legacy_pack_blocks matrixgame_dependency_tests base dependencies pack)
add_matrix_test(bitmap.png.legacy_gray matrixgame_dependency_tests bitmap dependencies png)
add_matrix_test(bitmap.png.legacy_rgb matrixgame_dependency_tests bitmap dependencies png)
add_matrix_test(bitmap.png.legacy_rgba matrixgame_dependency_tests bitmap dependencies png)
add_matrix_test(bitmap.png.write_round_trip matrixgame_dependency_tests bitmap dependencies png)
add_matrix_test(bitmap.png.invalid matrixgame_dependency_tests bitmap dependencies png)

# Wire-format fixtures for storage, packages and raw-read records (docs/DATA_FORMATS.md).
add_matrix_test_executable(matrixgame_layout_tests layout_tests.cpp wire_bytes.hpp ../MatrixGame/src/MapCatalog.cpp)
target_include_directories(matrixgame_layout_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
target_compile_definitions(matrixgame_layout_tests PRIVATE WINDOWS_IGNORE_PACKING_MISMATCH)
foreach(contract storage_columns storage_tree storage_zl03 storage_column_zl03 storage_truncated storage_malformed)
    add_matrix_test(base.layout.${contract} matrixgame_layout_tests base storage)
endforeach()
foreach(contract package_tree package_handles package_malformed package_file_fallback)
    add_matrix_test(base.layout.${contract} matrixgame_layout_tests base storage pack)
endforeach()
add_matrix_test(base.layout.package_listing matrixgame_layout_tests base storage pack)
add_matrix_test(game.maps.sources matrixgame_layout_tests startup storage pack)
add_matrix_test(base.layout.raw_records matrixgame_layout_tests base storage)

add_matrix_test_executable(matrixgame_error_tests error_tests.cpp)
add_matrix_test(base.error.single_message matrixgame_error_tests base startup)
add_matrix_test(base.error.empty_trace matrixgame_error_tests base startup)
add_matrix_test(base.error.missing_file matrixgame_error_tests base startup)

add_matrix_test_executable(matrixgame_path_tests executable_path_tests.cpp ../MatrixGame/src/ExecutablePath.cpp)
target_include_directories(matrixgame_path_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
target_compile_definitions(matrixgame_path_tests PRIVATE WINDOWS_IGNORE_PACKING_MISMATCH)
add_matrix_test(game.startup.module_unicode matrixgame_path_tests startup)
add_matrix_test(game.startup.module_long matrixgame_path_tests startup)
add_matrix_test(game.startup.module_failure matrixgame_path_tests startup)
add_matrix_test(game.startup.module_truncated matrixgame_path_tests startup)
add_matrix_test(game.startup.module_native matrixgame_path_tests startup)

add_matrix_test_executable(matrixgame_resource_tests resource_file_tests.cpp ../MatrixGame/src/ResourceFiles.cpp)
target_include_directories(matrixgame_resource_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
target_compile_definitions(matrixgame_resource_tests PRIVATE WINDOWS_IGNORE_PACKING_MISMATCH)
add_matrix_test(game.startup.resources_missing matrixgame_resource_tests startup)
add_matrix_test(game.startup.resources_optional matrixgame_resource_tests startup audio)
add_matrix_test(game.startup.resources_exact_files matrixgame_resource_tests startup)
add_matrix_test(game.startup.resources_unopened matrixgame_resource_tests startup audio)
add_matrix_test(game.startup.resources_damaged matrixgame_resource_tests startup audio storage)

add_matrix_test_executable(matrixgame_random_tests random_tests.cpp)
add_matrix_test(base.random.unit_interval matrixgame_random_tests base random)
add_matrix_test(base.random.fractional_scales matrixgame_random_tests base random)
add_matrix_test(base.random.original_sequence matrixgame_random_tests base random)
add_matrix_test(base.random.seed_normalization matrixgame_random_tests base random)
add_matrix_test(base.random.floating_bounds matrixgame_random_tests base random)
add_matrix_test(base.random.integer_bounds matrixgame_random_tests base random)
add_matrix_test(base.random.index_bounds matrixgame_random_tests base random)
add_matrix_test(base.random.shared_stream matrixgame_random_tests base random)
add_matrix_test(base.random.legacy_sequence matrixgame_random_tests base random)
add_matrix_test(base.random.legacy_seeds matrixgame_random_tests base random)
add_matrix_test(base.random.legacy_bounds matrixgame_random_tests base random)
add_matrix_test(base.random.legacy_floating_bounds matrixgame_random_tests base random)
add_matrix_test(base.random.legacy_width_consumption matrixgame_random_tests base random)
add_matrix_test(base.random.legacy_index_mapping matrixgame_random_tests base random)
add_matrix_test(base.random.legacy_crt_interop matrixgame_random_tests base random)

add_matrix_test_executable(matrixgame_random_configuration_tests
    random_configuration_tests.cpp ../MatrixGame/src/RandomConfiguration.cpp)
target_include_directories(matrixgame_random_configuration_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
add_matrix_test(game.random.default_mode matrixgame_random_configuration_tests gameplay startup random)
add_matrix_test(game.random.explicit_mode matrixgame_random_configuration_tests gameplay startup random)
add_matrix_test(game.random.invalid_mode matrixgame_random_configuration_tests gameplay startup random)
add_matrix_test(game.random.fixed_battle_mode matrixgame_random_configuration_tests gameplay startup random)
add_matrix_test(game.random.missing_file matrixgame_random_configuration_tests gameplay startup random)
add_matrix_test(game.random.configuration_file matrixgame_random_configuration_tests gameplay startup random)
add_matrix_test(game.random.configuration_report matrixgame_random_configuration_tests gameplay startup random)

add_matrix_test_executable(matrixgame_standalone_settings_tests standalone_settings_tests.cpp
    ../MatrixGame/src/StandaloneSettings.cpp ../MatrixGame/src/RandomConfiguration.cpp ../MatrixGame/src/MapCatalog.cpp)
target_include_directories(matrixgame_standalone_settings_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
add_matrix_test(game.settings.battle_bonus matrixgame_standalone_settings_tests startup settings)
add_matrix_test(game.settings.defaults matrixgame_standalone_settings_tests startup settings)
add_matrix_test(game.settings.explicit_values matrixgame_standalone_settings_tests startup settings)
add_matrix_test(game.settings.invalid_values matrixgame_standalone_settings_tests startup settings)
add_matrix_test(game.settings.preserves_text matrixgame_standalone_settings_tests startup settings)
add_matrix_test(game.settings.round_trip matrixgame_standalone_settings_tests startup settings)
add_matrix_test(game.settings.round_trip_inline_blocks matrixgame_standalone_settings_tests startup settings)
add_matrix_test(game.settings.save_utf8 matrixgame_standalone_settings_tests startup settings)
add_matrix_test(game.settings.save_wide matrixgame_standalone_settings_tests startup settings)
add_matrix_test(game.settings.save_creates_file matrixgame_standalone_settings_tests startup settings)
add_matrix_test(game.settings.save_failure matrixgame_standalone_settings_tests startup settings)
add_matrix_test(game.settings.save_transient_lock matrixgame_standalone_settings_tests startup settings)
add_matrix_test(game.settings.save_persistent_lock matrixgame_standalone_settings_tests startup settings)
add_matrix_test(game.settings.unreadable_file matrixgame_standalone_settings_tests startup settings)

add_matrix_test_executable(matrixgame_map_metadata_tests map_metadata_tests.cpp ../MatrixGame/src/MapMetadata.cpp)
target_include_directories(matrixgame_map_metadata_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
foreach(contract format envelope damage tree text files catalog substitution_bounds)
    add_matrix_test(game.maps.metadata_${contract} matrixgame_map_metadata_tests startup ui)
endforeach()

add_matrix_test_executable(matrixgame_map_catalog_tests map_catalog_tests.cpp ../MatrixGame/src/MapCatalog.cpp)
target_include_directories(matrixgame_map_catalog_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
foreach(contract merge unsafe_names paths)
    add_matrix_test(game.maps.${contract} matrixgame_map_catalog_tests startup ui)
endforeach()

add_matrix_test_executable(matrixgame_map_model_tests map_model_tests.cpp ../MatrixGame/src/MapModel.cpp ../MatrixGame/src/MapMetadata.cpp
    ../MatrixGame/src/MapCatalog.cpp ../MatrixGame/src/SettingsModel.cpp ../MatrixGame/src/RandomConfiguration.cpp)
target_include_directories(matrixgame_map_model_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
foreach(contract window_selection window_paging window_geometry window_text window_hint preview_markup metadata_display bonus_controls)
    add_matrix_test(game.maps.${contract} matrixgame_map_model_tests ui)
endforeach()

add_matrix_test_executable(matrixgame_settings_model_tests settings_model_tests.cpp
    ../MatrixGame/src/SettingsModel.cpp ../MatrixGame/src/RandomConfiguration.cpp ../MatrixGame/src/MapCatalog.cpp)
target_include_directories(matrixgame_settings_model_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
add_matrix_test(game.settings.window_resolutions matrixgame_settings_model_tests ui settings)
add_matrix_test(game.settings.window_selection matrixgame_settings_model_tests ui settings)
add_matrix_test(game.settings.window_stepping matrixgame_settings_model_tests ui settings)
add_matrix_test(game.settings.window_display_modes matrixgame_settings_model_tests ui settings)
add_matrix_test(game.settings.window_geometry matrixgame_settings_model_tests ui settings)
add_matrix_test(game.settings.window_text matrixgame_settings_model_tests ui settings)
add_matrix_test(game.settings.window_hint matrixgame_settings_model_tests ui settings)

add_matrix_test_executable(matrixgame_settings_runtime_tests settings_runtime_tests.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_settings_runtime_tests PRIVATE MatrixGameInternal)
add_matrix_test(game.settings.startup_settings matrixgame_settings_runtime_tests startup settings)
add_matrix_test(game.settings.immediate_settings matrixgame_settings_runtime_tests gameplay audio settings)
add_matrix_test(game.settings.restart_detection matrixgame_settings_runtime_tests settings)
add_matrix_test(game.settings.independent_shadows matrixgame_settings_runtime_tests gameplay settings)
add_matrix_test(game.settings.window_availability matrixgame_settings_runtime_tests settings)
add_matrix_test(game.settings.delayed_damage_attribution matrixgame_settings_runtime_tests gameplay settings)
add_matrix_test(game.settings.delayed_damage_build_default matrixgame_settings_runtime_tests gameplay settings)
add_matrix_test(game.settings.group_counts_build_default matrixgame_settings_runtime_tests ai settings)
add_matrix_test(game.settings.place_containment_build_default matrixgame_settings_runtime_tests ai settings)
add_matrix_test(game.settings.zero_x_trace_build_default matrixgame_settings_runtime_tests gameplay settings)
target_compile_definitions(matrixgame_settings_runtime_tests PRIVATE MATRIXGAME_TESTS_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>)
add_matrix_test(game.settings.map_choice matrixgame_settings_runtime_tests startup settings)

add_matrix_test_executable(matrixgame_screen_layout_tests screen_layout_tests.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_screen_layout_tests PRIVATE MatrixGameInternal)
foreach(contract anchoring dialog_centring tracking)
    add_matrix_test(game.screen.${contract} matrixgame_screen_layout_tests ui rendering)
endforeach()

add_matrix_test_executable(matrixgame_random_map_tests random_map_tests.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_random_map_tests PRIVATE MatrixGameInternal)
add_matrix_test(game.random.shared_map_helpers matrixgame_random_map_tests gameplay startup random)

add_matrix_test_executable(matrixgame_ai_tests
    ai_robot_tests.cpp
    ../MatrixGame/src/Interface/AIRobotTypes.cpp)
target_include_directories(matrixgame_ai_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
target_compile_definitions(matrixgame_ai_tests PRIVATE WINDOWS_IGNORE_PACKING_MISMATCH)
add_matrix_test(game.ai.head_price matrixgame_ai_tests gameplay ai)
add_matrix_test(game.ai.no_head matrixgame_ai_tests gameplay ai)
add_matrix_test(game.ai.strength_order matrixgame_ai_tests gameplay ai)
add_matrix_test(game.ai.capacity_bounds matrixgame_ai_tests gameplay ai)

add_matrix_test_executable(matrixgame_bomb_tests bomb_order_tests.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_bomb_tests PRIVATE MatrixGameInternal)
add_matrix_test(game.bomb.ground_target matrixgame_bomb_tests gameplay input)
add_matrix_test(game.bomb.inactive_target matrixgame_bomb_tests gameplay input)
add_matrix_test(game.bomb.destroyed_target matrixgame_bomb_tests gameplay input)

add_matrix_test_executable(matrixgame_player_command_probe player_command_probe.cpp command_world.hpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_player_command_probe PRIVATE MatrixGameInternal)
add_matrix_test(game.orders.replacement matrixgame_player_command_probe gameplay input)
add_matrix_test(game.orders.toggle_stop matrixgame_player_command_probe gameplay input)
add_matrix_test(game.orders.partial_setup matrixgame_player_command_probe gameplay input startup memory)
add_matrix_test(game.orders.preorder matrixgame_player_command_probe gameplay input)
add_matrix_test(game.orders.repeated_sessions matrixgame_player_command_probe gameplay input startup memory)

add_matrix_test_executable(matrixgame_battle_scenarios battle_scenario_tests.cpp battle_scenario.cpp
    battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_battle_scenarios PRIVATE MatrixGameInternal)
# Refresh provenance on every build, including plain incremental builds after a
# commit or tracked edit. The generator preserves the header timestamp if unchanged.
set(scenario_identity_header "${CMAKE_CURRENT_BINARY_DIR}/scenario_build_identity.hpp")
add_custom_target(matrixgame_scenario_identity
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_HEADER=${scenario_identity_header}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/write_scenario_identity.cmake"
    BYPRODUCTS "${scenario_identity_header}"
    VERBATIM)
add_dependencies(matrixgame_battle_scenarios matrixgame_scenario_identity)
target_include_directories(matrixgame_battle_scenarios PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_battle_scenarios PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
add_matrix_test(game.scenario.movement matrixgame_battle_scenarios gameplay ai input)
add_matrix_test(game.scenario.partial_setup matrixgame_battle_scenarios gameplay startup memory)
foreach(contract replacement target_removal repeatability observation rejected_command audio_unavailable bounds)
    add_matrix_test(game.scenario.${contract} matrixgame_battle_scenarios gameplay ai input)
endforeach()

add_matrix_test_executable(matrixgame_production_scenarios production_scenario_tests.cpp battle_scenario.cpp
    battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_production_scenarios PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_production_scenarios matrixgame_scenario_identity)
target_include_directories(matrixgame_production_scenarios PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_production_scenarios PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
add_matrix_test(game.production.queue_deployment matrixgame_production_scenarios gameplay ai)
add_matrix_test(game.production.base_exit matrixgame_production_scenarios gameplay ai)
add_matrix_test(game.production.base_terrain_cleanup matrixgame_production_scenarios gameplay memory)
add_matrix_test(game.production.base_terrain_teardown matrixgame_production_scenarios gameplay memory)
add_matrix_test(game.production.building_geometry matrixgame_production_scenarios gameplay)
add_matrix_test(game.production.maintenance_clock matrixgame_production_scenarios gameplay)
add_matrix_test(game.production.scheduled_income matrixgame_production_scenarios gameplay)
add_matrix_test(game.production.paused_production matrixgame_production_scenarios gameplay)
add_matrix_test(game.production.cancel_waiting_during_spawn matrixgame_production_scenarios gameplay)
foreach(contract occupied incompatible over_budget)
    add_matrix_test(game.production.reinforcement_${contract} matrixgame_production_scenarios gameplay ai)
endforeach()
add_matrix_test(game.production.player_purchase matrixgame_production_scenarios gameplay rendering memory)
foreach(contract ai_purchase ai_purchase_neutralized ai_purchase_deleted)
    add_matrix_test(game.production.${contract} matrixgame_production_scenarios gameplay ai memory)
endforeach()
add_matrix_test(game.production.constructor_assembly matrixgame_production_scenarios gameplay ai memory)
add_matrix_test(game.production.constructor_capacity matrixgame_production_scenarios gameplay ai memory)
add_matrix_test(game.production.constructor_deployment matrixgame_production_scenarios gameplay ai memory)

add_matrix_test_executable(matrixgame_combat_scenarios combat_scenario_tests.cpp battle_combat_geometry.cpp
    battle_scenario.cpp battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_combat_scenarios PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_combat_scenarios matrixgame_scenario_identity)
target_include_directories(matrixgame_combat_scenarios PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_combat_scenarios PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
add_matrix_test(game.combat.cannon_firing matrixgame_combat_scenarios gameplay ai)
add_matrix_test(game.combat.robot_firing matrixgame_combat_scenarios gameplay ai)
add_matrix_test(game.combat.plasma_flight matrixgame_combat_scenarios gameplay ai memory)
add_matrix_test(game.combat.axis_aligned_trace matrixgame_combat_scenarios gameplay ai)
add_matrix_test(game.combat.axis_trace_group_clip matrixgame_combat_scenarios gameplay ai)
foreach(contract volcano_firing electric_firing flame_firing gun_firing missile_firing mortar_firing repair_firing heat_boundary bomb_detonation cannon_projectiles cannon_boundaries cannon_obstruction delayed_damage bomb_order repair_death module_modifiers module_firing splash_boundary chained_bombs repair_redirect cannon_patient cannon_acquisition robot_boundaries status_refresh status_lethal friendly_lethal friendly_difficulty cannon_difficulty building_patient building_dying building_volcano building_electric building_flame building_gun building_missile building_mortar)
    add_matrix_test(game.combat.${contract} matrixgame_combat_scenarios gameplay ai memory)
endforeach()
foreach(contract delayed_status_attribution delayed_status_scaling delayed_status_attacker_retirement delayed_status_original turret_delayed_attribution turret_delayed_scaling)
    add_matrix_test(game.combat.${contract} matrixgame_combat_scenarios gameplay ai memory)
endforeach()
add_matrix_test(game.combat.delayed_status_mixed_owners matrixgame_combat_scenarios gameplay ai memory)
add_matrix_test(game.combat.delayed_status_direct_hit matrixgame_combat_scenarios gameplay ai memory)
add_matrix_test(game.combat.turret_mixed_owners matrixgame_combat_scenarios gameplay ai memory)

add_matrix_test_executable(matrixgame_material_ruin_tests material_ruin_tests.cpp battle_combat_geometry.cpp
    battle_scenario.cpp battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_material_ruin_tests PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_material_ruin_tests matrixgame_scenario_identity)
target_include_directories(matrixgame_material_ruin_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_material_ruin_tests PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
foreach(kind base base_asymmetric titan plasma electronics energy repair)
    add_matrix_test(game.harness.materials.demolish_${kind} matrixgame_material_ruin_tests gameplay memory tests)
endforeach()

add_matrix_test_executable(matrixgame_reinforcement_scenarios reinforcement_scenario_tests.cpp battle_scenario.cpp
    battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_reinforcement_scenarios PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_reinforcement_scenarios matrixgame_scenario_identity)
target_include_directories(matrixgame_reinforcement_scenarios PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_reinforcement_scenarios PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
foreach(contract unavailable_robot_icons delivery reservations carrier_limit player_request mixed_proposals)
    add_matrix_test(game.reinforcement.${contract} matrixgame_reinforcement_scenarios gameplay ai rendering)
endforeach()

add_matrix_test_executable(matrixgame_battle_device_tests battle_device_tests.cpp battle_device.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_battle_device_tests PRIVATE MatrixGameInternal)
foreach(contract buffers invalid_buffers com_ownership state_sessions textures invalid_textures texture_ownership managed_texture_upload resource_budget unavailable_icon_targets)
    add_matrix_test(game.harness.device.${contract} matrixgame_battle_device_tests tests rendering)
endforeach()

add_matrix_test_executable(matrixgame_combat_damage_tests combat_damage_tests.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_combat_damage_tests PRIVATE MatrixGameInternal)
foreach(contract robot_families building_families cannon_families difficulty minimum_health repair status_effects bomb_protection
    building_death cannon_repair_state building_repair projectile_range_callback projectile_owner_retirement)
    add_matrix_test(game.combat.${contract} matrixgame_combat_damage_tests gameplay)
endforeach()

add_matrix_test(base.random.draw_count_reset matrixgame_random_tests base random)
add_matrix_test(base.random.draw_count_observation matrixgame_random_tests base random)
add_matrix_test(base.random.draw_count_ranges matrixgame_random_tests base random)

add_matrix_test_executable(matrixgame_keepalive_tests keepalive_tests.cpp command_world.hpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_keepalive_tests PRIVATE MatrixGameInternal)
add_matrix_test(game.input.keepalive_dispatch matrixgame_keepalive_tests gameplay input)
add_matrix_test(game.input.keepalive_keys matrixgame_keepalive_tests gameplay input)
add_matrix_test(game.input.keepalive_fresh matrixgame_keepalive_tests gameplay input)
add_matrix_test(game.input.ordinary_deactivation matrixgame_keepalive_tests gameplay input)

add_matrix_test_executable(matrixgame_lifecycle_tests lifecycle_tests.cpp native_startup_reset.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_lifecycle_tests PRIVATE MatrixGameInternal)
add_matrix_test(game.lifecycle.configuration_cleanup matrixgame_lifecycle_tests startup memory)
add_matrix_test(game.lifecycle.empty_configuration matrixgame_lifecycle_tests startup memory)
add_matrix_test(game.lifecycle.form_transitions matrixgame_lifecycle_tests startup)
add_matrix_test(game.lifecycle.form_construction_failure matrixgame_lifecycle_tests startup)
add_matrix_test(game.lifecycle.repeated_forms matrixgame_lifecycle_tests startup)
add_matrix_test(game.lifecycle.partial_startup matrixgame_lifecycle_tests startup memory)
add_matrix_test(game.lifecycle.held_input_retirement matrixgame_lifecycle_tests startup input)
add_matrix_test(game.lifecycle.safe_free_without_cache matrixgame_lifecycle_tests startup memory)
add_matrix_test(game.lifecycle.cursor_before_initialization matrixgame_lifecycle_tests startup input)
add_matrix_test(game.lifecycle.graphics_references matrixgame_lifecycle_tests startup rendering)
add_matrix_test(game.lifecycle.cleanup_failure matrixgame_lifecycle_tests startup)
add_matrix_test(game.lifecycle.standalone_partial_cleanup matrixgame_lifecycle_tests startup memory)
add_matrix_test(game.lifecycle.graphics_partial_init matrixgame_lifecycle_tests startup rendering)

add_matrix_test_executable(matrixgame_loading_tests battle_loading_tests.cpp ../MatrixGame/src/BattleLoading.cpp)
target_include_directories(matrixgame_loading_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
foreach(contract names presentation failure child_ready child_exit bounded_wait message_pump
                 dismiss_button dismiss_close dismiss_escape)
    add_matrix_test(game.loading.${contract} matrixgame_loading_tests startup ui)
endforeach()

add_matrix_test_executable(matrixgame_battle_launch_tests battle_launch_tests.cpp)
target_include_directories(matrixgame_battle_launch_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
foreach(contract arguments relaunch_command action_keys)
    add_matrix_test(game.startup.${contract} matrixgame_battle_launch_tests startup input)
endforeach()

add_matrix_test_executable(matrixgame_font_tests font_tests.cpp native_font_reset.cpp
    ../MatrixGame/src/Text/Font.cpp ../MatrixGame/src/Text/FontCache.cpp)
target_include_directories(matrixgame_font_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/MatrixGame/src ${PROJECT_SOURCE_DIR}/MatrixGame)
target_compile_definitions(matrixgame_font_tests PRIVATE WINDOWS_IGNORE_PACKING_MISMATCH)
add_matrix_test(game.font.cache_ownership matrixgame_font_tests rendering memory)
add_matrix_test(game.font.device_notifications matrixgame_font_tests rendering)
add_matrix_test(game.font.missing_font matrixgame_font_tests rendering)
add_matrix_test(game.font.partial_creation matrixgame_font_tests rendering memory)
add_matrix_test(game.font.failure_diagnostics matrixgame_font_tests rendering)

add_matrix_test_executable(matrixgame_device_tests device_recovery_tests.cpp)
target_include_directories(matrixgame_device_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
target_compile_definitions(matrixgame_device_tests PRIVATE WINDOWS_IGNORE_PACKING_MISMATCH)
add_matrix_test(game.device.operational matrixgame_device_tests rendering)
add_matrix_test(game.device.lost_wait matrixgame_device_tests rendering)
add_matrix_test(game.device.reset_order matrixgame_device_tests rendering)
add_matrix_test(game.device.transient_reset matrixgame_device_tests rendering)
add_matrix_test(game.device.permanent_error matrixgame_device_tests rendering)
add_matrix_test(game.device.present_loss matrixgame_device_tests rendering)

add_matrix_test_executable(matrixgame_display_tests display_tests.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_display_tests PRIVATE MatrixGameInternal)
target_compile_definitions(matrixgame_display_tests PRIVATE WINDOWS_IGNORE_PACKING_MISMATCH)
foreach(contract defaults options invalid_options files toggle_pairs shortcut coordinates reset_parameters transition_order transition_failure change_plans resize_rollback)
    add_matrix_test(game.display.${contract} matrixgame_display_tests rendering input)
endforeach()

add_matrix_test_executable(matrixgame_selection_tests selection_tests.cpp command_world.hpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_selection_tests PRIVATE MatrixGameInternal)
add_matrix_test(game.selection.queued_moves matrixgame_selection_tests gameplay input)
add_matrix_test(game.selection.latest_rectangle matrixgame_selection_tests gameplay input)
add_matrix_test(game.selection.release_before_frame matrixgame_selection_tests gameplay input)
add_matrix_test(game.selection.reversed_mask matrixgame_selection_tests gameplay input)
add_matrix_test(game.selection.cancel_pending matrixgame_selection_tests gameplay input)
add_matrix_test(game.selection.canceled_release matrixgame_selection_tests gameplay input)
add_matrix_test(game.selection.number_cancel matrixgame_selection_tests gameplay input)
add_matrix_test(game.selection.limit_order matrixgame_selection_tests gameplay input)
add_matrix_test(game.selection.small_rectangle matrixgame_selection_tests gameplay input)
add_matrix_test(game.selection.object_teardown matrixgame_selection_tests gameplay input memory)
add_matrix_test(game.selection.display_cancel matrixgame_selection_tests gameplay input)

add_matrix_test_executable(matrixgame_text_tests text_parser_tests.cpp font_metrics_stub.cpp
    ../MatrixGame/src/Text/Parser.cpp)
target_include_directories(matrixgame_text_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
target_compile_definitions(matrixgame_text_tests PRIVATE WINDOWS_IGNORE_PACKING_MISMATCH)
add_matrix_test(game.text.words_colors matrixgame_text_tests ui)
add_matrix_test(game.text.inline_unicode matrixgame_text_tests ui)
add_matrix_test(game.text.newlines matrixgame_text_tests ui)
add_matrix_test(game.text.bounded_color matrixgame_text_tests ui)
add_matrix_test(game.text.invalid_color matrixgame_text_tests ui)
add_matrix_test(game.text.many_tags matrixgame_text_tests ui)
add_matrix_test(game.text.line_layout matrixgame_text_tests ui)

add_matrix_test_executable(matrixgame_cheat_tests cheat_tests.cpp command_world.hpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_cheat_tests PRIVATE MatrixGameInternal)
add_matrix_test(game.cheats.suffix_matching matrixgame_cheat_tests gameplay input)
add_matrix_test(game.cheats.flag_toggles matrixgame_cheat_tests gameplay input)
add_matrix_test(game.cheats.resource_limit matrixgame_cheat_tests gameplay input)
add_matrix_test(game.cheats.console matrixgame_cheat_tests gameplay input)

add_matrix_test_executable(matrixgame_form_input_tests form_input_tests.cpp command_world.hpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_form_input_tests PRIVATE MatrixGameInternal)
add_matrix_test(game.input.mouse_focus_reset matrixgame_form_input_tests gameplay input)
add_matrix_test(game.input.mouse_fresh_press matrixgame_form_input_tests gameplay input)
add_matrix_test(game.input.console_shift_focus matrixgame_form_input_tests gameplay input)
add_matrix_test(game.input.console_editing matrixgame_form_input_tests gameplay input)
add_matrix_test(game.input.input_only_mouse_reset matrixgame_form_input_tests gameplay input)
add_matrix_test(game.input.input_only_console_reset matrixgame_form_input_tests gameplay input)
add_matrix_test(game.input.display_shortcut matrixgame_form_input_tests rendering input)
add_matrix_test(game.input.pointer_left matrixgame_form_input_tests rendering input)
add_matrix_test(game.input.screenshot_deferred matrixgame_form_input_tests rendering input)

add_matrix_test_executable(matrixgame_input_tests input_tests.cpp ../MatrixGame/src/input.cpp)
target_include_directories(matrixgame_input_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
target_compile_definitions(matrixgame_input_tests PRIVATE WINDOWS_IGNORE_PACKING_MISMATCH)
add_matrix_test(game.input.key_state matrixgame_input_tests gameplay input)
add_matrix_test(game.input.focus_reset matrixgame_input_tests gameplay input)
add_matrix_test(game.input.action_bindings matrixgame_input_tests gameplay input)

add_matrix_test_executable(matrixgame_compiler_tests compiler_tests.cpp)
target_include_directories(matrixgame_compiler_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
target_compile_definitions(matrixgame_compiler_tests PRIVATE WINDOWS_IGNORE_PACKING_MISMATCH)
add_matrix_test(base.compiler.exception_unwind matrixgame_compiler_tests base compiler)
add_matrix_test_executable(matrixgame_compiler_failure_tests compiler_failure_tests.cpp)
target_include_directories(matrixgame_compiler_failure_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
target_compile_definitions(matrixgame_compiler_failure_tests PRIVATE WINDOWS_IGNORE_PACKING_MISMATCH)
add_test(NAME base.compiler.failure_report COMMAND ${CMAKE_COMMAND}
    "-DTEST_EXECUTABLE=$<TARGET_FILE:matrixgame_compiler_failure_tests>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_failure_report.cmake")
set_tests_properties(base.compiler.failure_report PROPERTIES
    LABELS "engine;asset-free;base;compiler"
    TIMEOUT 30
)

add_matrix_test_executable(matrixgame_dll_entry_tests dll_entry_tests.cpp ../MatrixGame/src/MatrixGameDll.cpp)
target_include_directories(matrixgame_dll_entry_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
target_compile_definitions(matrixgame_dll_entry_tests PRIVATE WINDOWS_IGNORE_PACKING_MISMATCH)
target_link_libraries(matrixgame_dll_entry_tests PRIVATE winmm)
add_matrix_test(game.dll.engine_init_failure matrixgame_dll_entry_tests startup dll)
add_matrix_test(game.dll.standard_init_failure matrixgame_dll_entry_tests startup dll)
add_matrix_test(game.dll.unknown_init_failure matrixgame_dll_entry_tests startup dll)
add_matrix_test(game.dll.form_failure matrixgame_dll_entry_tests startup dll)
add_matrix_test(game.dll.loop_failure matrixgame_dll_entry_tests startup dll)
add_matrix_test(game.dll.result_failure matrixgame_dll_entry_tests startup dll)
add_matrix_test(game.dll.success_values matrixgame_dll_entry_tests startup dll)
add_matrix_test(game.dll.interface_layout matrixgame_dll_entry_tests startup dll)
foreach(contract host_layout entry_table borrowed_arguments)
    add_matrix_test(game.dll.${contract} matrixgame_dll_entry_tests startup dll)
endforeach()

# Production consumers of the original host contract (settings, results, progress and sound callbacks).
add_matrix_test_executable(matrixgame_host_contract_tests host_contract_tests.cpp wire_bytes.hpp
    command_world.hpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_host_contract_tests PRIVATE MatrixGameInternal)
add_matrix_test(game.host.settings_image matrixgame_host_contract_tests startup settings dll)
add_matrix_test(game.host.result_statistics matrixgame_host_contract_tests gameplay dll)
add_matrix_test(game.host.progress_callback matrixgame_host_contract_tests startup dll)
add_matrix_test(game.host.sound_arguments matrixgame_host_contract_tests gameplay audio dll)

if(MATRIXGAME_BUILD_DLL)
    add_matrix_test_executable(matrixgame_dll_host_tests dll_host_tests.cpp)
    target_include_directories(matrixgame_dll_host_tests PRIVATE ${PROJECT_SOURCE_DIR}/MatrixGame/src)
    target_compile_definitions(matrixgame_dll_host_tests PRIVATE WINDOWS_IGNORE_PACKING_MISMATCH)
    add_dependencies(matrixgame_dll_host_tests MatrixGame)
    add_test(NAME game.dll.native_rejected_configuration
        COMMAND matrixgame_dll_host_tests game.dll.native_rejected_configuration "$<TARGET_FILE:MatrixGame>")
    set_tests_properties(game.dll.native_rejected_configuration PROPERTIES
        LABELS "engine;asset-free;startup;dll"
        TIMEOUT 30)
    foreach(contract native_entry_table native_language_configuration)
        add_test(NAME game.dll.${contract}
            COMMAND matrixgame_dll_host_tests game.dll.${contract} "$<TARGET_FILE:MatrixGame>")
        set_tests_properties(game.dll.${contract} PROPERTIES
            LABELS "engine;asset-free;startup;dll"
            TIMEOUT 30)
    endforeach()
endif()

# Exercise the actual EXE entry point with synthetic failure inputs on an invisible desktop.
if(NOT MATRIXGAME_BUILD_DLL)
    add_matrix_test_executable(matrixgame_standalone_startup_tests standalone_startup_tests.cpp)
    add_dependencies(matrixgame_standalone_startup_tests MatrixGame)
    foreach(contract missing_resources rejected_packed missing_text invalid_random damaged_optional diagnostic_readiness)
        add_test(NAME game.startup.native_${contract}
            COMMAND matrixgame_standalone_startup_tests game.startup.native_${contract}
                "$<TARGET_FILE:MatrixGame>" "${CMAKE_CXX_COMPILER}")
        set_tests_properties(game.startup.native_${contract} PROPERTIES
            LABELS "engine;asset-free;startup;ui;native-process" TIMEOUT 90)
    endforeach()
endif()

add_matrix_test_executable(matrixgame_production_queue_tests production_queue_tests.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_production_queue_tests PRIVATE MatrixGameInternal)
foreach(contract reinforcement_gates reinforcement_disabled reinforcement_scaling)
    add_matrix_test(game.production.${contract} matrixgame_production_queue_tests gameplay ai)
endforeach()
foreach(contract queue_order capacity refund_owner neutralization price income turret_progress income_ticks ai_constructor_prices weapon_slots)
    add_matrix_test(game.production.${contract} matrixgame_production_queue_tests gameplay)
endforeach()

add_matrix_test_executable(matrixgame_ai_bomb_escape_tests ai_bomb_escape_tests.cpp battle_combat_geometry.cpp
    battle_scenario.cpp battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_ai_bomb_escape_tests PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_ai_bomb_escape_tests matrixgame_scenario_identity)
target_include_directories(matrixgame_ai_bomb_escape_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_ai_bomb_escape_tests PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
foreach(contract free reserved unreachable)
    add_matrix_test(game.ai.bomb_escape_${contract}_journey matrixgame_ai_bomb_escape_tests gameplay ai)
    set_tests_properties(game.ai.bomb_escape_${contract}_journey PROPERTIES TIMEOUT 90)
endforeach()

add_matrix_test_executable(matrixgame_ai_decision_tests ai_decision_tests.cpp ai_observation_tests.cpp ai_cleanup_tests.cpp ai_armed_tests.cpp ai_capture_tests.cpp ai_reinforcement_tests.cpp battle_combat_geometry.cpp battle_scenario.cpp
    battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_ai_decision_tests PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_ai_decision_tests matrixgame_scenario_identity)
target_include_directories(matrixgame_ai_decision_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_ai_decision_tests PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
foreach(contract automatic_reinforcement_designs automatic_reinforcement_buildings automatic_reinforcement_choices automatic_reinforcement_delivery capture_guard_interruption capture_guard_filters turret_acquisition_equality turret_retirement_equality turret_elevated_terrain turret_visual_obstruction turret_acquisition_range turret_retirement_range turret_acquisition_filters visual_acquisition_range visual_enemy_range visual_filters visual_obstruction visual_retirement_range armed_friendly_line ineffective_fire_decisions blocked_fire_decisions moving_fire_decisions moving_combat_continuation patrol_combat_continuation patrol_combat_retirement patrol_combat_replacement armed_target_retention repair_competing_retirement repair_replacement_move repair_movement_completion repair_movement_retirement repair_acquisition_range repair_target_retention repair_explicit_priority memory_cleanup memory_clear_reuse memory_position_eviction memory_ignore_eviction place_bounds bomb_escape_planning enemy_memory memory_target_retirement ignore_expiry bad_positions regroup_thresholds
                 regroup_teams_and_actions action_priorities action_ties interruptibility automatic_reinforcement_gate strategic_tick tactical_target_choice tactical_blocked_line
                 strategic_member_loss strategic_base_loss strategic_forward
                 strategic_objective_replacement strategic_objective_ties strategic_empty_team_split uneven_team_redistribution
                 radio_group radio_team_region radio_filters threat_priorities
                 automatic_attack_route reused_route_distance automatic_capture_route automatic_defence_route
                 production_wait_choice production_wait_limits production_unaffordable)
    add_matrix_test(game.ai.${contract} matrixgame_ai_decision_tests gameplay ai)
endforeach()
add_matrix_test(game.ai.turret_elevation_boundary matrixgame_ai_decision_tests gameplay ai)
add_matrix_test(game.ai.bomb_escape_capacity matrixgame_ai_decision_tests gameplay ai)
add_matrix_test(game.ai.bomb_escape_reservation matrixgame_ai_decision_tests gameplay ai)
add_matrix_test(game.ai.visual_axis_aligned matrixgame_ai_decision_tests gameplay ai)
add_matrix_test(game.ai.place_axis matrixgame_ai_decision_tests gameplay ai)
foreach(contract regroup_split_counts regroup_merge_counts regroup_count_sequences strategic_merged_team_split)
    add_matrix_test(game.ai.${contract} matrixgame_ai_decision_tests gameplay ai)
endforeach()

add_matrix_test_executable(matrixgame_ai_auto_scenarios ai_auto_scenarios.cpp battle_combat_geometry.cpp battle_scenario.cpp
    battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_ai_auto_scenarios PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_ai_auto_scenarios matrixgame_scenario_identity)
target_include_directories(matrixgame_ai_auto_scenarios PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_ai_auto_scenarios PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
add_matrix_test(game.ai.auto_activation matrixgame_ai_auto_scenarios gameplay ai)
add_matrix_test(game.ai.auto_camera_fights matrixgame_ai_auto_scenarios gameplay ai rendering)
add_matrix_test(game.ai.auto_camera_replay matrixgame_ai_auto_scenarios gameplay ai rendering)
add_matrix_test(game.ai.auto_camera_overview_replay matrixgame_ai_auto_scenarios gameplay ai rendering)

add_matrix_test_executable(matrixgame_battle_outcome_scenarios battle_outcome_scenarios.cpp battle_scenario.cpp battle_combat_geometry.cpp
    battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_battle_outcome_scenarios PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_battle_outcome_scenarios matrixgame_scenario_identity)
target_include_directories(matrixgame_battle_outcome_scenarios PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_battle_outcome_scenarios PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
add_matrix_test(game.outcome.terron_blast_after_conversion matrixgame_battle_outcome_scenarios gameplay combat)

foreach(contract falling_cargo_status_scan capture_income_continuity capture_turret_refund capture_move_reassignment terron_destruction transported_robot_damage carrier_damage carrier_removal flyer_does_not_prevent_elimination transported_robot_prevents_elimination transported_robot_removal capture_target_damage special_break special_explosion special_animation capture_combat_death turret_does_not_prevent_elimination registered_enemy_capture capture_target_removal registered_base_capture capture_candidate_lifetime registered_capture base_survival factory_does_not_prevent_elimination neutral_capture enemy_capture capture_rejection capture_rollback competing_capturers competing_color special_survival_flags music_fade music_form_refill form_audio_device_error statistics_formatting scheduled_capture acquisition_after_removal enemy_capture_rollback capture_queue_refund)
    add_matrix_test(game.outcome.${contract} matrixgame_battle_outcome_scenarios gameplay)
endforeach()
foreach(contract guarded_base_journey guarded_base_target_destruction)
    add_matrix_test(game.ai.${contract} matrixgame_battle_outcome_scenarios gameplay ai)
endforeach()
foreach(contract capture_keyboard_completion capture_button_completion)
    add_matrix_test(game.player.${contract} matrixgame_battle_outcome_scenarios gameplay input)
endforeach()
foreach(contract typed_statistics_continuation result_command_boundary legacy_loss_confirmation pending_result_menu menu_exit_surrender_callbacks statistics_cheat_dismissal result_confirmation_callbacks menu_confirmation_cancel base_capture_interruption protected_base_command full_auto_escape legacy_loss_cheat paused_orders)
    add_matrix_test(game.outcome.${contract} matrixgame_battle_outcome_scenarios gameplay input)
endforeach()
foreach(contract victory defeat simultaneous_loss automatic_results pause_resume repeated_sessions pending_result_pause pending_result_late_elimination)
    add_matrix_test(game.outcome.${contract} matrixgame_battle_outcome_scenarios gameplay startup)
endforeach()
if(MATRIXGAME_TRACE)
    add_matrix_test(game.outcome.trace_result_callbacks matrixgame_battle_outcome_scenarios gameplay input)
endif()

add_matrix_test_executable(matrixgame_navigation_tests navigation_tests.cpp navigation_chassis_tests.cpp navigation_integrated_tests.cpp navigation_delivery_tests.cpp navigation_damage_tests.cpp battle_combat_geometry.cpp battle_scenario.cpp
    battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_navigation_tests PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_navigation_tests matrixgame_scenario_identity)
target_include_directories(matrixgame_navigation_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_navigation_tests PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
foreach(contract chassis_journeys neighboring_delivery_landings delivery_flights delivery_carrier_damage local_routes passage_width placement region_routes destination_weights group_movement disconnected opposing_flow corridor_movement mixed_chassis removed_blocker carrier_lifetime carrier_cap carrier_drop_landing
                 multiregion_movement multiregion_road_guidance unreachable_replacement)
    add_matrix_test(game.navigation.${contract} matrixgame_navigation_tests gameplay ai navigation)
endforeach()
add_matrix_test(game.navigation.group_speed_finite matrixgame_navigation_tests gameplay ai navigation)
foreach(contract shorted_route_blocker dying_route_blocker shorted_movement_recovery drowning_retirement)
    add_matrix_test(game.navigation.${contract} matrixgame_navigation_tests gameplay ai navigation)
endforeach()

add_matrix_test_executable(matrixgame_navigation_boundary_tests navigation_boundary_tests.cpp battle_scenario.cpp
    battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_navigation_boundary_tests PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_navigation_boundary_tests matrixgame_scenario_identity)
target_include_directories(matrixgame_navigation_boundary_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_navigation_boundary_tests PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
foreach(contract terrain_ramp_transitions terrain_lateral_slope terrain_water_slope sloped_terrain_journeys zone_routes road_routes road_route_reuse chassis_traversal collision_boundaries collision_wait_threshold moving_target replacement_near_blocker dense_flow terrain_speed water_depth shallow_water_traversal)
    add_matrix_test(game.navigation.${contract} matrixgame_navigation_boundary_tests gameplay ai navigation)
endforeach()
add_matrix_test(game.navigation.formation_speed_boundaries matrixgame_navigation_boundary_tests gameplay ai navigation)
add_matrix_test_executable(matrixgame_player_order_scenarios player_order_scenarios.cpp battle_scenario.cpp battle_combat_geometry.cpp
    battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_player_order_scenarios PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_player_order_scenarios matrixgame_scenario_identity)
target_include_directories(matrixgame_player_order_scenarios PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_player_order_scenarios PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
foreach(contract control_group_reassignment control_group_combat_death move_routes preorder_cancellation capability_rejection stop_key stop_button attack_routes control_groups empty_control_group control_group_camera debug_reinforcement_reset group_add_cap shift_rectangle_group_cap manual_control controlled_unit_removal manual_exit_held manual_focus patrol_reversal patrol_stop_lifecycle patrol_replacement_lifecycle auto_modes cheat_dispatch_gate mesh_selection overlapping_selection_limit independent_group_commands minimap_move_routes edge_and_repeated_move auto_target_retirement mixed_auto_selection capture_target_validation cannon_and_invalid_targets manual_aim_and_fire_input registered_cheat_effects megabust_cheat)
    add_matrix_test(game.player.${contract} matrixgame_player_order_scenarios gameplay input)
endforeach()

foreach(contract mixed_selection_lifecycle manual_track_lifecycle manual_hovercraft_lifecycle mixed_building_selection inactive_selection manual_armed_fire repair_command_routes bomb_command_routes
                 manual_bomb_key manual_combat_death manual_exit_keys)
    add_matrix_test(game.player.${contract} matrixgame_player_order_scenarios gameplay input)
endforeach()

add_matrix_test_executable(matrixgame_player_command_lifecycles player_command_lifecycle_tests.cpp
    battle_scenario.cpp battle_combat_geometry.cpp battle_geometry.cpp battle_device.cpp
    battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_player_command_lifecycles PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_player_command_lifecycles matrixgame_scenario_identity)
target_include_directories(matrixgame_player_command_lifecycles PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_player_command_lifecycles PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
foreach(contract mixed_command_lifecycle spawning_command_lifecycle carried_command_lifecycle dying_command_lifecycle)
    add_matrix_test(game.player.${contract} matrixgame_player_command_lifecycles gameplay input)
    set_tests_properties(game.player.${contract} PROPERTIES TIMEOUT 90)
endforeach()

add_test(NAME game.cheats.intentional_crash
    COMMAND "${CMAKE_COMMAND}" "-DTEST_EXECUTABLE=$<TARGET_FILE:matrixgame_player_order_scenarios>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_intentional_cheat_abort.cmake")
set_tests_properties(game.cheats.intentional_crash PROPERTIES LABELS "engine;asset-free;cheats;parser" TIMEOUT 15)
add_matrix_test_executable(matrixgame_presentation_integration_tests presentation_integration_tests.cpp battle_combat_geometry.cpp
    battle_scenario.cpp battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_presentation_integration_tests PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_presentation_integration_tests matrixgame_scenario_identity)
target_include_directories(matrixgame_presentation_integration_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_presentation_integration_tests PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
foreach(contract positional_attenuation_boundaries positional_loop_reentry push_sounds preset_matching disabled optional_audio interface_layer battle_acknowledgements silent_battle_commands positional_loop missing_speech textured_button_hit_testing)
    add_matrix_test(game.presentation.${contract} matrixgame_presentation_integration_tests ui audio input)
endforeach()

add_matrix_test(game.presentation.manual_fire_interface_focus matrixgame_presentation_integration_tests gameplay ui input)

foreach(contract camera_wheel_limits camera_drag_release camera_input_interruption)
    add_matrix_test(game.presentation.${contract} matrixgame_presentation_integration_tests ui input rendering)
endforeach()

add_matrix_test_executable(matrixgame_settings_window_integration_tests settings_window_integration_tests.cpp
    settings_display_host.cpp battle_scenario.cpp battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_settings_window_integration_tests PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_settings_window_integration_tests matrixgame_scenario_identity)
target_include_directories(matrixgame_settings_window_integration_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_settings_window_integration_tests PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
foreach(contract settings_input_pause_cancel settings_apply_save_failure)
    add_matrix_test(game.presentation.${contract} matrixgame_settings_window_integration_tests gameplay ui input settings)
endforeach()
add_matrix_test(game.presentation.settings_display_rollback matrixgame_settings_window_integration_tests gameplay ui input settings current-desktop)



# Manual correctness probes for catalog defect candidates, intentionally outside
# CTest until separately reviewed fixes establish the expected invariants.
add_matrix_test_executable(matrixgame_presentation_defect_probes presentation_defect_probes.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_presentation_defect_probes PRIVATE MatrixGameInternal)
# Performance workloads. The executable is a measurement tool, not a
# timing test: CTest only runs a short smoke pass that checks repeatable work.
add_matrix_test_executable(matrixgame_benchmarks benchmarks.cpp benchmark_support.cpp benchmark_support.hpp
    command_world.hpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_benchmarks PRIVATE MatrixGameInternal)
add_test(NAME game.benchmark.smoke COMMAND matrixgame_benchmarks --smoke)
set_tests_properties(game.benchmark.smoke PROPERTIES LABELS "engine;asset-free;benchmark" TIMEOUT 30)

add_matrix_test_executable(matrixgame_battle_restart_scenarios battle_restart_scenarios.cpp battle_scenario.cpp
    battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_battle_restart_scenarios PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_battle_restart_scenarios matrixgame_scenario_identity)
target_include_directories(matrixgame_battle_restart_scenarios PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_battle_restart_scenarios PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
foreach(contract movement pending_win pending_loss)
    add_matrix_test(game.restart.${contract} matrixgame_battle_restart_scenarios gameplay startup)
endforeach()

add_matrix_test_executable(matrixgame_map_window_scenarios map_window_scenarios.cpp battle_scenario.cpp
    battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_map_window_scenarios PRIVATE MatrixGameInternal)
add_dependencies(matrixgame_map_window_scenarios matrixgame_scenario_identity)
target_include_directories(matrixgame_map_window_scenarios PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(matrixgame_map_window_scenarios PRIVATE
    MATRIXGAME_SCENARIO_BUILD_DLL=$<BOOL:${MATRIXGAME_BUILD_DLL}>
    MATRIXGAME_SCENARIO_CHEATS=$<BOOL:${MATRIXGAME_CHEATS}>)
add_matrix_test(game.selector.menu_cancel matrixgame_map_window_scenarios gameplay ui startup)
foreach(contract menu_same menu_different win_cancel win_same win_different loss_cancel loss_same loss_different
                 surrender_cancel surrender_same surrender_different exit_statistics)
    foreach(profile park10 park16 legacy10 legacy16)
        add_matrix_test(game.selector.${contract}.${profile} matrixgame_map_window_scenarios gameplay ui startup)
    endforeach()
endforeach()

add_matrix_test_executable(matrixgame_battle_restart_failures battle_restart_failures.cpp
    battle_geometry.cpp battle_device.cpp battle_cursor_stub.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_battle_restart_failures PRIVATE MatrixGameInternal)
add_matrix_test(game.restart.game_owned_cleanup matrixgame_battle_restart_failures gameplay startup)
foreach(contract missing_file_cleanup missing_file_mouse_cleanup callback_before_loop callback_during_tick callback_abandoned)
    add_matrix_test(game.restart.${contract} matrixgame_battle_restart_failures gameplay startup)
endforeach()

add_matrix_test_executable(matrixgame_loop_scheduler_tests loop_scheduler_tests.cpp battle_device.cpp diagnostic_overlay_stub.cpp)
target_link_libraries(matrixgame_loop_scheduler_tests PRIVATE MatrixGameInternal)
foreach(contract timing activation exit_boundaries missing_form native_exit)
    add_matrix_test(game.loop.${contract} matrixgame_loop_scheduler_tests startup input)
endforeach()

if(MATRIXGAME_BUILD_DLL)
    add_matrix_test_executable(matrixgame_native_session_fixture native_session_fixture.cpp
        native_session_assets.cpp battle_geometry.cpp)
    target_link_libraries(matrixgame_native_session_fixture PRIVATE MatrixGameInternal)
    target_compile_definitions(matrixgame_native_session_fixture PRIVATE MATRIXGAME_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
    option(MATRIXGAME_NATIVE_SESSION_TESTS "Run native Direct3D 9 synthetic session checks" OFF)
    if(MATRIXGAME_NATIVE_SESSION_TESTS)
        add_matrix_test(game.session.native_run_lifetimes matrixgame_native_session_fixture gameplay startup native native-process)
        add_matrix_test(game.session.held_input_retirement matrixgame_native_session_fixture gameplay startup input native native-process)
    endif()
endif()
