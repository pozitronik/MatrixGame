# MatrixGame - licensed under GPLv2 or any later version.
if(NOT DEFINED TEST_EXECUTABLE)
    message(FATAL_ERROR "TEST_EXECUTABLE is required")
endif()
execute_process(COMMAND "${TEST_EXECUTABLE}" probe.intentional_crash
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
if(NOT result STREQUAL "86" OR NOT error MATCHES "Entered intentional CRASH parser probe")
    message(FATAL_ERROR "Expected probe entry and SIGABRT exit code 86; result=${result}\n${output}${error}")
endif()
