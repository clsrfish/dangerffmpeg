include(${CMAKE_SOURCE_DIR}/cmake/spdlog.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/ffmpeg.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/sdl2.cmake)

aux_source_directory(. SRC_FILES)

add_library(src ${SRC_FILES})
target_include_directories(src PUBLIC ${spdlog_SOURCE_DIR}/include ${ffmpeg_INCLUDE_DIRS} ${sdl_INCLUDE_DIRS})
target_link_libraries(src PUBLIC PkgConfig::ffmpeg PkgConfig::sdl2)
