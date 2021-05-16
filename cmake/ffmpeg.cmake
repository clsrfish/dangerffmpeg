# config ffmpeg
include(FindPkgConfig)
pkg_check_modules(ffmpeg REQUIRED IMPORTED_TARGET libavcodec libavfilter libavresample libpostproc libswscale libavdevice libavformat libavutil libswresample)
message(${ffmpeg_INCLUDE_DIRS})