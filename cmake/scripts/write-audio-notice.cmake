# Writes audio-libraries-NOTICE.txt for one game exe with the source revision it was built from. Runs as a post build step through cmake -P.
# Inputs: TEMPLATE, OUTPUT, SOURCE_DIR, SOURCE_URL.

cmake_minimum_required(VERSION 3.28)

set(NOTICE_SOURCE_URL "${SOURCE_URL}")
set(NOTICE_SOURCE_REVISION "an unknown commit")

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE commit RESULT_VARIABLE commit_result
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(commit_result EQUAL 0 AND commit MATCHES "^[0-9a-f]+$")
        set(NOTICE_SOURCE_REVISION "commit ${commit}")
        execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match HEAD
            WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE tag RESULT_VARIABLE tag_result
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(tag_result EQUAL 0 AND NOT tag STREQUAL "")
            set(NOTICE_SOURCE_REVISION "tag ${tag}, ${NOTICE_SOURCE_REVISION}")
        endif()
        # Tracked changes that are not committed make the commit an inexact pointer, which MakePatch.ps1 refuses to ship.
        execute_process(COMMAND "${GIT_EXECUTABLE}" diff --quiet HEAD --
            WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE dirty_result ERROR_QUIET)
        if(NOT dirty_result EQUAL 0)
            set(NOTICE_SOURCE_REVISION "${NOTICE_SOURCE_REVISION} with uncommitted changes")
        endif()
    endif()
endif()

configure_file("${TEMPLATE}" "${OUTPUT}.tmp" @ONLY NEWLINE_STYLE CRLF)
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
