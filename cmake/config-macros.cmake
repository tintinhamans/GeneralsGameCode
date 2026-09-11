# Shared helper macros for the config-*.cmake files.

# Handles a DEFAULT/ON/OFF option, applying OnDefs or OffDefs accordingly.
macro(define_option OptionName FeatureInfoName FeatureInfoDescription OnDefs OffDefs ShowDefaultFeature)
    if(${OptionName} STREQUAL "DEFAULT")
        if(${ShowDefaultFeature})
            add_feature_info(${FeatureInfoName} TRUE ${FeatureInfoDescription})
        endif()
    elseif(${OptionName} STREQUAL "ON")
        foreach(_option_def ${OnDefs})
            target_compile_definitions(core_config INTERFACE ${_option_def})
        endforeach()
        add_feature_info(${FeatureInfoName} TRUE ${FeatureInfoDescription})
    elseif(${OptionName} STREQUAL "OFF")
        foreach(_option_def ${OffDefs})
            target_compile_definitions(core_config INTERFACE ${_option_def})
        endforeach()
        add_feature_info(${FeatureInfoName} FALSE ${FeatureInfoDescription})
    else()
        message(FATAL_ERROR "Unhandled ${OptionName} value: ${${OptionName}}")
    endif()
endmacro()


# Discovers GuardPrefix guards in HeaderFile and applies them via define_option.
macro(define_guarded_option OptionName FeatureInfoName FeatureInfoDescription HeaderFile GuardPrefix)
    file(STRINGS "${HeaderFile}" _guarded_option_defines REGEX "^#[ \t]*define[ \t]+${GuardPrefix}[A-Z0-9_]+")
    set(_guarded_option_guards "")
    foreach(_guarded_option_define IN LISTS _guarded_option_defines)
        string(REGEX MATCH "${GuardPrefix}[A-Z0-9_]+" _guarded_option_name "${_guarded_option_define}")
        list(APPEND _guarded_option_guards "${_guarded_option_name}")
    endforeach()
    list(REMOVE_DUPLICATES _guarded_option_guards)

    if(NOT _guarded_option_guards)
        message(FATAL_ERROR "No ${GuardPrefix}* guards found in ${HeaderFile}.")
    endif()

    set(_guarded_option_on_defs "")
    set(_guarded_option_off_defs "")
    foreach(_guarded_option_name IN LISTS _guarded_option_guards)
        list(APPEND _guarded_option_on_defs "${_guarded_option_name}=1")
        list(APPEND _guarded_option_off_defs "${_guarded_option_name}=0")
    endforeach()

    define_option(${OptionName} ${FeatureInfoName} ${FeatureInfoDescription}
        "${_guarded_option_on_defs}" "${_guarded_option_off_defs}" TRUE)
    unset(_guarded_option_guards)
    unset(_guarded_option_on_defs)
    unset(_guarded_option_off_defs)
endmacro()
