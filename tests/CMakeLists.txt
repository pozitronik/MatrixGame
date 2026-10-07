# MatrixGame - licensed under GPLv2 or any later version.
add_custom_target(matrixgame_tests)

function(add_matrix_test_executable target)
    add_executable(${target} ${ARGN} test_support.hpp)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE MatrixLib)
    target_compile_options(${target} PRIVATE ${COMPILE_OPTIONS} "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
    target_compile_definitions(${target} PRIVATE ${COMPILE_DEFINITIONS} _ALLOW_RTCc_IN_STL)
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
