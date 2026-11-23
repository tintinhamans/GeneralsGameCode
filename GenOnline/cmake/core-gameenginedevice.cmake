# Generals Online part of Core/GameEngineDevice/CMakeLists.txt, included after its targets are declared.

# Miles and Bink are only built when a game, tool or legacy media build needs them.
list(REMOVE_ITEM GAMEENGINEDEVICE_SRC
    Include/MilesAudioDevice/MilesAudioManager.h
    Include/VideoDevice/Bink/BinkVideoPlayer.h
    Source/MilesAudioDevice/MilesAudioManager.cpp
    Source/VideoDevice/Bink/BinkVideoPlayer.cpp
)

if(RTS_NEEDS_LEGACY_MEDIA)
    target_sources(corei_gameenginedevice_private INTERFACE
        Include/MilesAudioDevice/MilesAudioManager.h
        Source/MilesAudioDevice/MilesAudioManager.cpp
        Include/VideoDevice/Bink/BinkVideoPlayer.h
        Source/VideoDevice/Bink/BinkVideoPlayer.cpp
    )
    target_link_libraries(corei_gameenginedevice_public INTERFACE deps_bink deps_miles)
    target_compile_definitions(corei_gameenginedevice_public INTERFACE RTS_HAS_LEGACY_MEDIA)
endif()

macro(genonline_core_gameenginedevice_targets)
    # The upstream file links Miles and Bink unconditionally, the legacy media block above already linked them when needed.
    get_target_property(_genonline_device_libs corei_gameenginedevice_public INTERFACE_LINK_LIBRARIES)
    list(REMOVE_ITEM _genonline_device_libs deps_bink deps_miles)
    if(RTS_NEEDS_LEGACY_MEDIA)
        list(PREPEND _genonline_device_libs deps_bink deps_miles)
    endif()
    set_target_properties(corei_gameenginedevice_public PROPERTIES INTERFACE_LINK_LIBRARIES "${_genonline_device_libs}")

    if(RTS_BUILD_OPTION_MODERN_MEDIA AND FFMPEG_FOUND)
        target_sources(corei_gameenginedevice_private INTERFACE
            Include/VideoDevice/FFmpeg/FFmpegPcmConverter.h
            Source/VideoDevice/FFmpeg/FFmpegPcmConverter.cpp
            Include/VideoDevice/FFmpeg/FFmpegAudioFile.h
            Source/VideoDevice/FFmpeg/FFmpegAudioFile.cpp
            Include/VideoDevice/FFmpeg/FFmpegAudioMetadata.h
            Source/VideoDevice/FFmpeg/FFmpegAudioMetadata.cpp
        )
    endif()

    if(RTS_BUILD_OPTION_MODERN_MEDIA)
        # A static OpenAL Soft export links Threads::Threads.
        find_package(Threads REQUIRED)
        find_package(OpenAL CONFIG REQUIRED)
        target_sources(corei_gameenginedevice_private INTERFACE
            Include/OpenALAudioDevice/OpenALDevice.h
            Include/OpenALAudioDevice/OpenALPcmStream.h
            Source/OpenALAudioDevice/OpenALDevice.cpp
            Source/OpenALAudioDevice/OpenALPcmStream.cpp
        )
        target_link_libraries(corei_gameenginedevice_private INTERFACE OpenAL::OpenAL)
        target_sources(corei_gameenginedevice_private INTERFACE
            Include/OpenALAudioDevice/MediaBackend.h
            Include/OpenALAudioDevice/OpenALMovieVoice.h
            Source/OpenALAudioDevice/OpenALMovieVoice.cpp
            Include/OpenALAudioDevice/OpenALAudioManager.h
            Include/OpenALAudioDevice/OpenALAudioProducer.h
            Include/OpenALAudioDevice/OpenALAudioCache.h
            Source/OpenALAudioDevice/OpenALAudioManager.cpp
            Source/OpenALAudioDevice/OpenALAudioProducer.cpp
            Source/OpenALAudioDevice/OpenALAudioCache.cpp
        )
    endif()
endmacro()

cmake_language(DEFER CALL genonline_core_gameenginedevice_targets)
