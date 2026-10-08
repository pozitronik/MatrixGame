if(NOT DEFINED TEST_EXECUTABLE)
    message(FATAL_ERROR "TEST_EXECUTABLE is required")
endif()
execute_process(COMMAND "${TEST_EXECUTABLE}" probe.fail
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
if(NOT result STREQUAL "1" OR NOT error MATCHES "Failed: probe.fail")
    message(FATAL_ERROR "Expected a caught assertion with exit 1; result=${result}\n${output}${error}")
endif()
