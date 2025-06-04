if(NOT TARGET NXDK::SDL2)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

    add_library(NXDK::SDL2 ALIAS PkgConfig::SDL2)
    set (NXDK_SDL2_LIBRARIES "${SDL2_LIBRARIES}")
    set (NXDK_SDL2_INCLUDE_DIRS "${SDL2_INCLUDE_DIRS}")
endif()
