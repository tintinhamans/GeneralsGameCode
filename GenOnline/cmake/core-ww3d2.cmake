# The sound render object needs WWAudio, which only legacy media builds.
if(NOT RTS_NEEDS_LEGACY_MEDIA)
    list(REMOVE_ITEM WW3D2_SRC soundrobj.cpp soundrobj.h)
endif()
