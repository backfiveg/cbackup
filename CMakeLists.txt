cmake_minimum_required(VERSION 3.16)
project(cbackup VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -pg")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
add_compile_options(-Wall -Wextra -Wpedantic)

# Find zlib
find_package(ZLIB REQUIRED)

# Find GTest
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(googletest)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files grouped by module
set(CLI_SRCS src/cli/cli_parser.cpp)
set(BACKUP_SRCS src/backup/backup.cpp src/backup/restore.cpp)
set(FILTER_SRCS src/filter/filter.cpp)
set(PACK_SRCS src/pack/packer.cpp src/pack/unpacker.cpp)
set(COMPRESS_SRCS src/compress/compressor.cpp)
set(CRON_SRCS src/cron/cron_scheduler.cpp)
set(DAEMON_SRCS src/daemon/inotify_daemon.cpp)
set(UTILS_SRCS src/utils/logger.cpp src/utils/path_utils.cpp src/utils/fd_wrapper.cpp)

# Main executable
add_executable(cbackup
  src/main.cpp
  ${CLI_SRCS}
  ${BACKUP_SRCS}
  ${FILTER_SRCS}
  ${PACK_SRCS}
  ${COMPRESS_SRCS}
  ${CRON_SRCS}
  ${DAEMON_SRCS}
  ${UTILS_SRCS}
)

target_link_libraries(cbackup PRIVATE ZLIB::ZLIB pthread)

# Tests
enable_testing()

set(TEST_SRCS
  tests/test_backup.cpp
  tests/test_filter.cpp
  tests/test_pack.cpp
  tests/test_compress.cpp
  tests/test_cron.cpp
  tests/test_path_utils.cpp
)

add_executable(cbackup_tests
  ${TEST_SRCS}
  ${BACKUP_SRCS}
  ${FILTER_SRCS}
  ${PACK_SRCS}
  ${COMPRESS_SRCS}
  ${CRON_SRCS}
  ${UTILS_SRCS}
)

if(GTest_FOUND)
  target_link_libraries(cbackup_tests PRIVATE GTest::GTest GTest::Main ZLIB::ZLIB pthread)
else()
  target_link_libraries(cbackup_tests PRIVATE gtest gtest_main ZLIB::ZLIB pthread)
endif()

include(GoogleTest)
gtest_discover_tests(cbackup_tests)

install(TARGETS cbackup DESTINATION bin)
