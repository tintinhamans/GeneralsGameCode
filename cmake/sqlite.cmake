# SQLite reads the launcher content database (-contentdb). VC6 cannot build it, so the feature compiles out there.
option(RTS_BUILD_OPTION_SQLITE "Enable loading game data from the launcher content database." ON)

add_library(rts_sqlite INTERFACE)

set(RTS_SQLITE_AVAILABLE OFF)
if(RTS_BUILD_OPTION_SQLITE AND NOT IS_VS6_BUILD)
    find_package(unofficial-sqlite3 CONFIG QUIET)
    if(unofficial-sqlite3_FOUND)
        set(RTS_SQLITE_AVAILABLE ON)
        target_link_libraries(rts_sqlite INTERFACE unofficial::sqlite3::sqlite3)
        target_compile_definitions(rts_sqlite INTERFACE RTS_SQLITE_ARCHIVE)
    endif()
endif()

add_feature_info(SQLiteContentDatabase RTS_SQLITE_AVAILABLE "Load game data from the launcher content database")
