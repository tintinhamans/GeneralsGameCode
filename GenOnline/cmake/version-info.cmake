# The file version resource of the game executables, shared by the Zero Hour and Generals targets.
# The file version and the product version are the release (YYYY.M.P.0). The commit is in the product string and in GitCommit.
# The values are read when CMake configures, so a new commit needs a configure to show up. The git index is a configure dependency.

find_package(Git QUIET)

function(_genonline_git_output out)
    set(_value "")
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" ${ARGN}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE _exit_code
            OUTPUT_VARIABLE _output
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_exit_code EQUAL 0)
            set(_value "${_output}")
        endif()
    endif()
    set(${out} "${_value}" PARENT_SCOPE)
endfunction()

# Escapes a value for an RC string.
function(_genonline_rc_string out value)
    string(REPLACE "\\" "\\\\" _escaped "${value}")
    string(REPLACE "\"" "\"\"" _escaped "${_escaped}")
    set(${out} "${_escaped}" PARENT_SCOPE)
endfunction()

# game is zerohour or generals, description is the FileDescription string.
function(genonline_version_info target game description)
    if(NOT WIN32)
        return()
    endif()

    _genonline_git_output(_tag describe --tags --match "v[0-9]*" HEAD)
    _genonline_git_output(_sha rev-parse HEAD)
    _genonline_git_output(_short rev-parse --short HEAD)
    _genonline_git_output(_date show -s --format=%ci HEAD)
    _genonline_git_output(_status status --porcelain --untracked-files=no)

    # The same rule as GenOnlineRealtime::CompatibilityVersion: the release tag vYYYY.M.P, else the year and month of the commit.
    set(_year 0)
    set(_month 0)
    set(_patch 0)
    if(_tag MATCHES "^v([0-9][0-9][0-9][0-9])\\.([0-9]+)\\.([0-9]+)(-.*)?$" AND CMAKE_MATCH_2 GREATER_EQUAL 1 AND CMAKE_MATCH_2 LESS_EQUAL 12)
        set(_year ${CMAKE_MATCH_1})
        set(_month ${CMAKE_MATCH_2})
        set(_patch ${CMAKE_MATCH_3})
    elseif(_date MATCHES "^([0-9][0-9][0-9][0-9])-([0-9][0-9])")
        set(_year ${CMAKE_MATCH_1})
        math(EXPR _month "${CMAKE_MATCH_2}")
    endif()
    set(_compat "${_year}.${_month}.${_patch}")

    # The channel of the build, with the names of the environments: a tag of exactly vYYYY.M.P is live, vYYYY.M.P-rc.N is test, anything else is dev.
    set(_kind "dev")
    set(_suffix "-dev")
    if(_tag MATCHES "^v[0-9][0-9][0-9][0-9]\.[0-9]+\.[0-9]+$")
        set(_kind "live")
        set(_suffix "")
    elseif(_tag MATCHES "^v[0-9][0-9][0-9][0-9]\.[0-9]+\.[0-9]+-rc\.([0-9]+)$")
        set(_kind "test")
        set(_suffix "-rc.${CMAKE_MATCH_1}")
    endif()

    set(_flags "0x0L")
    set(_flag_list "")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND _flag_list "VS_FF_DEBUG")
    endif()
    if(NOT _kind STREQUAL "live")
        list(APPEND _flag_list "VS_FF_PRERELEASE")
    endif()
    set(_private_line "")
    if(NOT _status STREQUAL "")
        list(APPEND _flag_list "VS_FF_PRIVATEBUILD")
        set(_private_line "VALUE \"PrivateBuild\", \"Built from a working tree with uncommitted changes\\0\"")
    endif()
    if(_flag_list)
        list(JOIN _flag_list " | " _flags)
    endif()

    set(_product "${_compat}${_suffix}")
    if(NOT _short STREQUAL "")
        set(_product "${_compat}${_suffix}+${_short}")
    endif()

    set(GO_FV_MAJOR ${_year})
    set(GO_FV_MINOR ${_month})
    set(GO_FV_PATCH ${_patch})
    set(GO_FILE_FLAGS "${_flags}")
    set(GO_PRIVATE_BUILD_LINE "${_private_line}")
    _genonline_rc_string(GO_DESCRIPTION "${description}")
    _genonline_rc_string(GO_FILE_VERSION "${_product}")
    _genonline_rc_string(GO_PRODUCT_VERSION "${_product}")
    _genonline_rc_string(GO_INTERNAL_NAME "genonline_${game}")
    _genonline_rc_string(GO_ORIGINAL_NAME "genonline_${game}.exe")
    _genonline_rc_string(GO_COMPAT "${_compat}")
    _genonline_rc_string(GO_CHANNEL "${_kind}")
    _genonline_rc_string(GO_TAG "${_tag}")
    _genonline_rc_string(GO_SHA "${_sha}")
    _genonline_rc_string(GO_DATE "${_date}")
    _genonline_rc_string(GO_BASELINE_PACK "genonline_${game}@${_compat}.gpk")

    set(_out "${CMAKE_CURRENT_BINARY_DIR}/genonline_version.rc")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Version.rc.in" "${_out}" @ONLY)
    target_sources(${target} PRIVATE "${_out}")

    # A new commit changes the git index, so CMake configures again and the resource follows.
    _genonline_git_output(_index_path rev-parse --git-path index)
    if(NOT _index_path STREQUAL "")
        get_filename_component(_index_path "${_index_path}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_index_path}")
    endif()
endfunction()
