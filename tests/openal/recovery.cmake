if(NOT TARGET openal_recovery_stream)
    get_filename_component(_recovery_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    add_library(openal_recovery_stream OBJECT
        "${_recovery_root}/Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALDevice.cpp"
        "${_recovery_root}/Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALPcmStream.cpp")
    target_include_directories(openal_recovery_stream PRIVATE "${_recovery_root}/Core/GameEngineDevice/Include")
    target_link_libraries(openal_recovery_stream PRIVATE OpenAL::OpenAL)
    target_compile_features(openal_recovery_stream PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(openal_recovery_stream PRIVATE "/FI${CMAKE_CURRENT_LIST_DIR}/recovery_calls.h")
    else()
        target_compile_options(openal_recovery_stream PRIVATE -include "${CMAKE_CURRENT_LIST_DIR}/recovery_calls.h")
    endif()
endif()
