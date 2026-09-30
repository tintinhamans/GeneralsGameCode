# Stages repo-root Assets/ into a game-relative tree under OUTPUT_DIR (the root
# of the embedded 450_450 archive -- see GenerateEmbeddedData.cmake, which reads
# OUTPUT_DIR as its DATA_DIR instead of a literal Data/ folder).
#
# Run via `cmake -P` from a custom command (see GeneralsMD/Code/Main/CMakeLists.txt),
# with ASSETS_DIR, OUTPUT_DIR and STR2CSF_EXE passed in via -D.
#
# Mapping from Assets/<TopFolder>/... to OUTPUT_DIR/...:
#   Localization/<Language>/<name>.str  -> Data/<Language>/<name>.csf  (compiled via str2csf)
#   Localization/<Language>/<name>.str with 'XX: "text"' lines (multi-language; the US column is the
#                                       <Language> table) -> Data/<Language>/<name>.csf plus
#                                          Data/Languages/<code>/<name>.csf per other column
#   Localization/Languages/<name>.str   -> Data/Languages/<code>/<name>.csf, one per column of the
#   Localization/Languages/<pack>/<name>.str  multi-language file (str2csf --column; see GameText.cpp's
#                                          GameTextLanguages for the codes)
#   Localization/Languages/<pack>/<code>/<name>.str
#                                       -> Data/Languages/<code>/<name>.csf, a single-language table
#                                          (plain "text" lines, read as the US column)
#   Localization/**/*.md                -> not staged (notes beside the sources)
#   UI/**                               -> UI/**                       (passthrough)
#   Window/**                           -> Window/**                   (passthrough)
#   Art/**                              -> Art/**                      (passthrough)
#   INI/**                              -> Data/INI/**                 (passthrough)
#   Game/**                             -> **                          (passthrough, prefix stripped)
# Any other top-level folder under Assets/ fails the build with a clear message.

if(NOT DEFINED ASSETS_DIR)
    message(FATAL_ERROR "StageEmbeddedAssets.cmake: ASSETS_DIR not set")
endif()
if(NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "StageEmbeddedAssets.cmake: OUTPUT_DIR not set")
endif()
if(NOT DEFINED STR2CSF_EXE)
    message(FATAL_ERROR "StageEmbeddedAssets.cmake: STR2CSF_EXE not set")
endif()

# Rebuilding the staged tree from scratch each time is what lets a *removed*
# Assets/ source (or a renamed .str) disappear from OUTPUT_DIR too -- copying
# only changed files would leave stale outputs behind forever.
file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

# Compiled CSFs are kept beside OUTPUT_DIR, keyed by the source and str2csf hashes, so a rebuild
# only recompiles the big multi-language tables when they or the tool actually changed.
set(CSF_CACHE_DIR "${OUTPUT_DIR}.csfcache")
file(MAKE_DIRECTORY "${CSF_CACHE_DIR}")
file(SHA256 "${STR2CSF_EXE}" STR2CSF_HASH)

# Compiles one column of SRC_FILE into DEST_FILE (nothing is written when the column has no text).
function(rts_compile_column SRC_FILE DEST_FILE COLUMN REL_PATH)
    file(SHA256 "${SRC_FILE}" src_hash)
    string(MD5 cache_name "${SRC_FILE}|${COLUMN}|${DEST_FILE}")
    set(cache_csf "${CSF_CACHE_DIR}/${cache_name}.csf")
    set(cache_key "${CSF_CACHE_DIR}/${cache_name}.key")
    set(key "${src_hash}${STR2CSF_HASH}${COLUMN}")

    if(EXISTS "${cache_key}")
        file(READ "${cache_key}" cached_key)
        if(cached_key STREQUAL key)
            if(EXISTS "${cache_csf}")
                get_filename_component(dest_dir "${DEST_FILE}" DIRECTORY)
                file(MAKE_DIRECTORY "${dest_dir}")
                file(COPY_FILE "${cache_csf}" "${DEST_FILE}")
            endif()
            return()
        endif()
    endif()

    file(REMOVE "${cache_csf}" "${cache_key}" "${DEST_FILE}")
    get_filename_component(dest_dir "${DEST_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${dest_dir}")
    execute_process(
        COMMAND "${STR2CSF_EXE}" "${SRC_FILE}" "${DEST_FILE}" --column "${COLUMN}"
        RESULT_VARIABLE str2csf_result
        OUTPUT_VARIABLE str2csf_output
        ERROR_VARIABLE str2csf_error
    )
    if(NOT str2csf_result EQUAL 0)
        message(FATAL_ERROR "StageEmbeddedAssets: str2csf --column ${COLUMN} failed on '${REL_PATH}':
${str2csf_output}${str2csf_error}")
    endif()
    if(EXISTS "${DEST_FILE}")
        file(COPY_FILE "${DEST_FILE}" "${cache_csf}")
    endif()
    file(WRITE "${cache_key}" "${key}")
endfunction()

file(GLOB_RECURSE RTS_ASSET_FILES LIST_DIRECTORIES FALSE "${ASSETS_DIR}/*")
if(RTS_ASSET_FILES)
    list(SORT RTS_ASSET_FILES)
endif()

set(staged_count 0)

# The text languages a multi-language .str can carry besides US.
set(RTS_TEXT_LANGUAGE_COLUMNS de fr es it ko zh bp pl ru ar uk)

foreach(asset_file ${RTS_ASSET_FILES})
    get_filename_component(base_name "${asset_file}" NAME)
    string(SUBSTRING "${base_name}" 0 1 base_name_first_char)
    if(base_name_first_char STREQUAL ".")
        continue()
    endif()

    file(RELATIVE_PATH rel_path "${ASSETS_DIR}" "${asset_file}")

    # First path component selects the mapping rule.
    string(FIND "${rel_path}" "/" first_slash)
    if(first_slash EQUAL -1)
        message(FATAL_ERROR "StageEmbeddedAssets: '${rel_path}' is directly under Assets/ -- every asset must live under one of the known top-level folders (Localization, UI, Window, Art, INI, Game)")
    endif()
    string(SUBSTRING "${rel_path}" 0 ${first_slash} top_folder)
    math(EXPR rest_start "${first_slash} + 1")
    string(SUBSTRING "${rel_path}" ${rest_start} -1 rest_path)

    if(top_folder STREQUAL "Localization")
        # rest_path is "<Language>/<name>.str"
        get_filename_component(lang_name "${rest_path}" DIRECTORY)
        get_filename_component(base_stem "${rest_path}" NAME_WE)
        get_filename_component(ext "${rest_path}" EXT)
        if(ext STREQUAL ".md")
            continue()
        endif()
        if(NOT ext STREQUAL ".str")
            message(FATAL_ERROR "StageEmbeddedAssets: '${rel_path}' -- only .str files are allowed under Assets/Localization/<Language>/")
        endif()
        if(lang_name MATCHES "^Languages/[^/]+/([a-z][a-z])$")
            # A single-language table for that code: its plain "text" lines are the US column.
            set(single_code "${CMAKE_MATCH_1}")
            rts_compile_column("${asset_file}" "${OUTPUT_DIR}/Data/Languages/${single_code}/${base_stem}.csf" us "${rel_path}")
            math(EXPR staged_count "${staged_count} + 1")
            continue()
        endif()
        if(lang_name STREQUAL "Languages" OR lang_name MATCHES "^Languages/[^/]+$")
            foreach(column ${RTS_TEXT_LANGUAGE_COLUMNS})
                rts_compile_column("${asset_file}" "${OUTPUT_DIR}/Data/Languages/${column}/${base_stem}.csf" "${column}" "${rel_path}")
            endforeach()
            math(EXPR staged_count "${staged_count} + 1")
            continue()
        endif()
        file(READ "${asset_file}" str_content)
        if(str_content MATCHES "\n[A-Z][A-Z]: \"")
            # Multi-language: the US column is this language's table, the others go to Data/Languages.
            rts_compile_column("${asset_file}" "${OUTPUT_DIR}/Data/${lang_name}/${base_stem}.csf" us "${rel_path}")
            foreach(column ${RTS_TEXT_LANGUAGE_COLUMNS})
                rts_compile_column("${asset_file}" "${OUTPUT_DIR}/Data/Languages/${column}/${base_stem}.csf" "${column}" "${rel_path}")
            endforeach()
            math(EXPR staged_count "${staged_count} + 1")
            continue()
        endif()
        set(dest_file "${OUTPUT_DIR}/Data/${lang_name}/${base_stem}.csf")
        get_filename_component(dest_dir "${dest_file}" DIRECTORY)
        file(MAKE_DIRECTORY "${dest_dir}")

        execute_process(
            COMMAND "${STR2CSF_EXE}" "${asset_file}" "${dest_file}" --lang "${lang_name}"
            RESULT_VARIABLE str2csf_result
            OUTPUT_VARIABLE str2csf_output
            ERROR_VARIABLE str2csf_error
        )
        if(NOT str2csf_result EQUAL 0)
            message(FATAL_ERROR "StageEmbeddedAssets: str2csf failed on '${rel_path}':\n${str2csf_output}${str2csf_error}")
        endif()
    elseif(top_folder STREQUAL "UI" OR top_folder STREQUAL "Window" OR top_folder STREQUAL "Art")
        set(dest_file "${OUTPUT_DIR}/${top_folder}/${rest_path}")
        get_filename_component(dest_dir "${dest_file}" DIRECTORY)
        file(MAKE_DIRECTORY "${dest_dir}")
        file(COPY_FILE "${asset_file}" "${dest_file}")
    elseif(top_folder STREQUAL "INI")
        set(dest_file "${OUTPUT_DIR}/Data/INI/${rest_path}")
        get_filename_component(dest_dir "${dest_file}" DIRECTORY)
        file(MAKE_DIRECTORY "${dest_dir}")
        file(COPY_FILE "${asset_file}" "${dest_file}")
    elseif(top_folder STREQUAL "Game")
        set(dest_file "${OUTPUT_DIR}/${rest_path}")
        get_filename_component(dest_dir "${dest_file}" DIRECTORY)
        file(MAKE_DIRECTORY "${dest_dir}")
        file(COPY_FILE "${asset_file}" "${dest_file}")
    else()
        message(FATAL_ERROR "StageEmbeddedAssets: unknown top-level folder 'Assets/${top_folder}' (from '${rel_path}') -- expected one of Localization, UI, Window, Art, INI, Game")
    endif()

    math(EXPR staged_count "${staged_count} + 1")
endforeach()

message(STATUS "StageEmbeddedAssets: staged ${staged_count} file(s) from ${ASSETS_DIR} into ${OUTPUT_DIR}")
