# Fails the configure step when the C++ compiler is not the MSVC toolset that GENONLINE_REQUIRED_MSVC_TOOLSET names.
# A release preset loads this file through CMAKE_PROJECT_TOP_LEVEL_INCLUDES. The game exe must run on the
# VC++ runtime of that toolset, and a newer compiler can import runtime functions the older runtime lacks.
#
# The file runs before the compilers are known, so the check is deferred to the end of the top level directory.

set(GENONLINE_REQUIRED_MSVC_TOOLSET "" CACHE STRING "MSVC toolset the build must use, for example 14.44. Empty accepts any compiler.")

function(genonline_require_msvc_toolset)
    if("${GENONLINE_REQUIRED_MSVC_TOOLSET}" STREQUAL "")
        return()
    endif()

    if(NOT GENONLINE_REQUIRED_MSVC_TOOLSET MATCHES "^14\\.([0-9]+)$")
        message(FATAL_ERROR "GENONLINE_REQUIRED_MSVC_TOOLSET '${GENONLINE_REQUIRED_MSVC_TOOLSET}' is not of the form 14.NN")
    endif()

    # Toolset 14.NN ships the compiler 19.NN.
    set(requiredCompiler "19.${CMAKE_MATCH_1}")
    if(NOT MSVC OR NOT CMAKE_CXX_COMPILER_VERSION MATCHES "^([0-9]+\\.[0-9]+)\\.")
        message(FATAL_ERROR "This preset needs the MSVC ${GENONLINE_REQUIRED_MSVC_TOOLSET} toolset, but the C++ compiler is ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}.")
    endif()

    if(NOT CMAKE_MATCH_1 STREQUAL requiredCompiler)
        message(FATAL_ERROR "This preset needs the MSVC ${GENONLINE_REQUIRED_MSVC_TOOLSET} toolset (cl ${requiredCompiler}), "
            "but the C++ compiler is cl ${CMAKE_CXX_COMPILER_VERSION} at ${CMAKE_CXX_COMPILER}. "
            "Open the build shell with vcvarsamd64_x86.bat -vcvars_ver=${GENONLINE_REQUIRED_MSVC_TOOLSET}, or run scripts/release/BuildRelease.ps1.")
    endif()

    message(STATUS "MSVC toolset ${GENONLINE_REQUIRED_MSVC_TOOLSET} in use: cl ${CMAKE_CXX_COMPILER_VERSION}")
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL genonline_require_msvc_toolset)
