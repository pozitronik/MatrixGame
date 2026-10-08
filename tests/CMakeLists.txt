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

add_matrix_test_executable(matrixgame_random_tests random_tests.cpp)
add_matrix_test(base.random.unit_interval matrixgame_random_tests base random)
add_matrix_test(base.random.fractional_scales matrixgame_random_tests base random)
add_matrix_test(base.random.original_sequence matrixgame_random_tests base random)
add_matrix_test(base.random.seed_normalization matrixgame_random_tests base random)
add_matrix_test(base.random.floating_bounds matrixgame_random_tests base random)
add_matrix_test(base.random.integer_bounds matrixgame_random_tests base random)
add_matrix_test(base.random.index_bounds matrixgame_random_tests base random)
add_matrix_test(base.random.shared_stream matrixgame_random_tests base random)

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
