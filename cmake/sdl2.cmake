include(FindPkgConfig)
pkg_check_modules(sdl2 REQUIRED IMPORTED_TARGET sdl2)
message(${sdl2_INCLUDE_DIRS})