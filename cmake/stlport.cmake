if (MSVC AND MSVC_VERSION LESS 1300)
    # Use stlport for VS6 builds
    FetchContent_Declare(
        stlport
        GIT_REPOSITORY https://github.com/TheSuperHackers/stlport-4.5.3.git
        GIT_TAG        501824065f5b1eab77ca7f53ed3a227acf965eb1
    )

    FetchContent_MakeAvailable(stlport)
    target_compile_definitions(stlport INTERFACE USING_STLPORT=1)
else()
    # Do not use stlport
    add_library(stlport INTERFACE)
endif()
