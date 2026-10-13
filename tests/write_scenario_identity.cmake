# MatrixGame - licensed under GPLv2 or any later version.
# Invoked at build time; does not require a Git checkout or Git installation.
cmake_minimum_required(VERSION 3.27)
if(NOT DEFINED SOURCE_ROOT OR NOT DEFINED OUTPUT_HEADER)
    message(FATAL_ERROR "SOURCE_ROOT and OUTPUT_HEADER are required")
endif()

set(revision "unknown")
set(dirty -1)
find_package(Git QUIET)
if(GIT_FOUND)
    # A source archive inside another checkout must not inherit its parent's HEAD.
    # rev-parse also supports linked worktrees, whose .git entry is a file.
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_ROOT}" rev-parse --show-toplevel
        RESULT_VARIABLE root_result OUTPUT_VARIABLE git_root
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(root_result EQUAL 0)
        file(REAL_PATH "${SOURCE_ROOT}" source_path)
        file(REAL_PATH "${git_root}" git_path)
        if(WIN32)
            string(TOLOWER "${source_path}" source_path)
            string(TOLOWER "${git_path}" git_path)
        endif()
        if(source_path STREQUAL git_path)
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_ROOT}" rev-parse --verify HEAD
                RESULT_VARIABLE revision_result OUTPUT_VARIABLE git_revision
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(revision_result EQUAL 0 AND git_revision MATCHES "^[0-9a-f]+$")
                set(revision "${git_revision}")
                execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_ROOT}" diff --quiet HEAD --
                    RESULT_VARIABLE dirty_result ERROR_QUIET)
                if("${dirty_result}" MATCHES "^[01]$")
                    set(dirty "${dirty_result}")
                endif()
            endif()
        endif()
    endif()
endif()

set(content "// Generated at build time; tracked edits are reflected in DIRTY.\n#pragma once\n#define MATRIXGAME_SCENARIO_REVISION \"${revision}\"\n#define MATRIXGAME_SCENARIO_DIRTY ${dirty}\n")
if(EXISTS "${OUTPUT_HEADER}")
    file(READ "${OUTPUT_HEADER}" previous)
    if(previous STREQUAL content)
        return()
    endif()
endif()
get_filename_component(output_directory "${OUTPUT_HEADER}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(WRITE "${OUTPUT_HEADER}" "${content}")
