cmake_minimum_required(VERSION 3.0.0)
project(main VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    # https://github.com/google/sanitizers/wiki/AddressSanitizer
    set(CMAKE_CXX_FLAGS "-Wall -Werror=return-type -fsanitize=address,undefined -fno-omit-frame-pointer ${CMAKE_CXX_FLAGS}")
else()
    set(CMAKE_CXX_FLAGS "-O0 ${CMAKE_CXX_FLAGS}")
endif()

configure_file (
  ${PROJECT_SOURCE_DIR}/config.h.in
  ${PROJECT_BINARY_DIR}/config.h
)

include_directories(
    /usr/local/include
    ${PROJECT_BINARY_DIR}
)
link_directories(
    /usr/local/lib
    ${PROJECT_BINARY_DIR}
)

include(FetchContent)
# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.8.5
)
FetchContent_MakeAvailable(spdlog)
set(SPDLOG_ENABLE_PCH ON)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog)

# Test case
include(CTest)
enable_testing()

# build package installer start
include (InstallRequiredSystemLibraries)
set(CPACK_RESOURCE_FILE_LICENSE  "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
# build package installer end
