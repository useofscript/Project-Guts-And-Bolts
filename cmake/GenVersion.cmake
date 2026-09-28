# Writes Version.h with the commit this build came from (run on every build,
# only touches the file when something changed).
set(sha "unknown")
set(date "")
set(repo "useofscript/Project-Guts-And-Bolts")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${SRC}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD WORKING_DIRECTORY "${SRC}"
                    OUTPUT_VARIABLE sha OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        set(sha "unknown")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" log -1 --format=%cs WORKING_DIRECTORY "${SRC}"
                    OUTPUT_VARIABLE date OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" config --get remote.origin.url WORKING_DIRECTORY "${SRC}"
                    OUTPUT_VARIABLE url OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(url MATCHES "github\\.com[:/]+([^/]+/[^/.]+)")
        set(repo "${CMAKE_MATCH_1}")
    endif()
elseif(EXISTS "${SRC}/.gb_commit")
    # Installed from a zip: the installer remembers which commit it downloaded.
    file(READ "${SRC}/.gb_commit" sha)
    string(STRIP "${sha}" sha)
endif()
set(content "#pragma once
// Generated at build time by cmake/GenVersion.cmake - don't edit.
#define GB_VERSION     \"${VERSION}\"
#define GB_COMMIT      \"${sha}\"
#define GB_COMMIT_DATE \"${date}\"
#define GB_REPO        \"${repo}\"
#define GB_SOURCE_DIR  \"${SRC}\"
")
set(out "${OUT}/Version.h")
if(EXISTS "${out}")
    file(READ "${out}" old)
endif()
if(NOT "${old}" STREQUAL "${content}")
    file(WRITE "${out}" "${content}")
endif()
