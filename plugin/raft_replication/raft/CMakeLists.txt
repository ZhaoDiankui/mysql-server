cmake_minimum_required(VERSION 3.13)

if(POLICY CMP0077)
  SET(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
ENDIF()

OPTION(WITH_DEBUG "Debug Mode" OFF)
OPTION(WITH_DEBUG_LOG_CACHE "Debug Mode For Log Cache" OFF)
OPTION(WITH_TSAN "ThreadSanitizer Mode" OFF)
OPTION(WITH_ASAN "AddressSanitizer Mode" OFF)
OPTION(WITH_GPT "GooglePerfTools Enable" OFF)
OPTION(WITH_COVERAGE "Coverage Enable" OFF)
OPTION(WITH_TEST "With unittest" OFF)
OPTION(MINIMAL_MAKE "Only make libaliconsensus.a" OFF)
OPTION(WITH_INSTALL "Install libaliconsensus" OFF)
OPTION(WITH_FAIL_POINT "enable fail point" OFF)
OPTION(WITH_EXAMPLES "With examples in example dir" OFF)

SET(CMAKE_CXX_STANDARD 17)

IF(WITH_DEBUG)
  SET(CMAKE_BUILD_TYPE "Debug")
ELSEIF (NOT DEFINED CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE "RelWithDebInfo")
ENDIF()

IF (BUILD_WITH_SSL STREQUAL "system")
  FIND_PACKAGE(OpenSSL)
  IF(OPENSSL_FOUND)
    GET_FILENAME_COMPONENT(OPENSSL_ROOT_DIR ${OPENSSL_INCLUDE_DIR} PATH)
    MESSAGE(STATUS "system OpenSSL has root ${OPENSSL_ROOT_DIR}")
  ENDIF()
ENDIF()

IF (BUILD_WITH_PROTOBUF STREQUAL "bundled" OR WITH_TEST)
  INCLUDE(FetchContent)
  SET(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/dependency")
  MESSAGE(STATUS "Dependency are installed at: " ${FETCHCONTENT_BASE_DIR})
ENDIF()

IF (BUILD_WITH_PROTOBUF STREQUAL "bundled")
  MESSAGE(STATUS "Downloading protobuf to ${FETCHCONTENT_BASE_DIR}")

  SET(PROTOBUF_DOWNLOAD_URL "https://github.com/protocolbuffers/protobuf/archive/refs/tags/v3.19.4.tar.gz")
  INCLUDE(FetchContent)
  FetchContent_Declare(
    protobuf
    URL ${PROTOBUF_DOWNLOAD_URL})
  FetchContent_GetProperties(protobuf)
  IF (NOT protobuf_POPULATED)
    FetchContent_Populate(protobuf)
    SET(protobuf_BUILD_TESTS OFF)
    ADD_SUBDIRECTORY(${protobuf_SOURCE_DIR}/cmake ${protobuf_BINARY_DIR})
  ENDIF()

  SET(PROTOBUF_FOUND 1 CACHE INTERNAL "")
  SET(PROTOBUF_PROTOC_EXECUTABLE protoc CACHE INTERNAL "")
  SET(PROTOBUF_INCLUDE_DIR ${protobuf_SOURCE_DIR}/src CACHE INTERNAL "")
  SET(PROTOBUF_INCLUDE_DIRS ${protobuf_SOURCE_DIR}/src CACHE INTERNAL "")
  SET(PROTOBUF_LIBRARY libprotobuf CACHE INTERNAL "")
ELSEIF (BUILD_WITH_PROTOBUF STREQUAL "system")
  FIND_PACKAGE(Protobuf)
ELSEIF (BUILD_WITH_PROTOBUF STREQUAL "mysql_protobuf")
  SET(PROTOBUF_FOUND 1 CACHE INTERNAL "")
  SET(PROTOBUF_LIBRARY ext::libprotobuf CACHE INTERNAL "")
ENDIF()

IF (WITH_TEST)
  ENABLE_TESTING()

  FIND_PACKAGE(gflags REQUIRED)

  MESSAGE(STATUS "Downloading rocksdb to ${FETCHCONTENT_BASE_DIR}")

  SET(ROCKSDB_DOWNLOAD_URL "https://github.com/facebook/rocksdb/archive/refs/tags/v7.9.2.tar.gz")
  FetchContent_Declare(
    rocksdb
    URL ${ROCKSDB_DOWNLOAD_URL})
  FetchContent_GetProperties(rocksdb)
  IF (NOT rocksdb_POPULATED)
    FetchContent_Populate(rocksdb)
    SET(WITH_TESTS OFF)
    SET(WITH_TOOLS OFF)
    SET(WITH_BENCHMARK_TOOLS OFF)
    SET(WITH_SNAPPY ON)
    ADD_COMPILE_OPTIONS(-DGFLAGS_NAMESPACE=gflags)
    ADD_SUBDIRECTORY(${rocksdb_SOURCE_DIR} ${rocksdb_BINARY_DIR} EXCLUDE_FROM_ALL)
    GET_FILENAME_COMPONENT(CMAKE_MODULE_PATH "${rocksdb_SOURCE_DIR}/cmake/modules/" ABSOLUTE)
  ENDIF()

  MESSAGE(STATUS "Downloading googletest to ${FETCHCONTENT_BASE_DIR}")

  SET(GTEST_DOWNLOAD_URL "https://github.com/google/googletest/archive/refs/tags/v1.13.0.tar.gz")
  FetchContent_Declare(
    gtest
    URL ${GTEST_DOWNLOAD_URL})
  FetchContent_GetProperties(gtest)
  IF (NOT gtest_POPULATED)
    FetchContent_Populate(gtest)
    ADD_SUBDIRECTORY(${gtest_SOURCE_DIR} ${gtest_BINARY_DIR} EXCLUDE_FROM_ALL)
  ENDIF()
ENDIF()

IF (NOT PROTOBUF_FOUND)
  MESSAGE(WARNING "Protobuf libraries/headers could not be found")
ENDIF()

IF (NOT PROTOBUF_PROTOC_EXECUTABLE)
  MESSAGE(WARNING "The protoc executable could not be found")
ENDIF()

IF(WITH_FAIL_POINT)
  ADD_DEFINITIONS(-DFAIL_POINT)
ENDIF()

# To be removed
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error")

ADD_SUBDIRECTORY(dependency/easy/src)
ADD_SUBDIRECTORY(consensus)
