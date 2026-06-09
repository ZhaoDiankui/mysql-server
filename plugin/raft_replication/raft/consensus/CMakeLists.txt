cmake_minimum_required(VERSION 3.13)

SET (CONSENSUS_SRC_FILES
  algorithm/configuration.cc
  algorithm/paxos.cc
  algorithm/paxos_server.cc
  algorithm/paxos_log_cache.cc
  algorithm/paxos_error.cc
  algorithm/msg_compress.cc
  algorithm/crc.cc
  algorithm/log_meta_cache.cc
  net/easyNet.cc
  service/service.cc
  service/thread_timer.cc
  service/client_service.cc
  utils/fail_point.cc
  )

SET (CONSENSUS_HEADER_FILES
  include/net.h
  include/paxos_server.h
  include/client_service.h
  include/paxos_configuration.h
  include/paxos.h
  include/files.h
  include/paxos_log.h
  include/consensus.h
  include/easyNet.h
  include/service.h
  include/thread_timer.h
  include/single_process_queue.h
  include/paxos_log_cache.h
  include/paxos_error.h
  include/msg_compress.h
  include/crc.h
  include/log_meta_cache.h
  include/paxos_option.h
  utils/fail_point.h
  )

IF(WITH_TSAN)
  MESSAGE(STATUS "Build with ThreadSanitizer Enable!")
  SET (EXTRA_SAN_FLAG -fsanitize=thread)
ENDIF()

IF(WITH_ASAN)
  MESSAGE(STATUS "Build with AddressSanitizer Enable!")
  SET (EXTRA_SAN_FLAG "-fsanitize=address")
ENDIF()

IF(WITH_GPT)
  MESSAGE(STATUS "Build with GooglePerfTools Enable!")
  SET (EXTRA_LINK tcmalloc_and_profiler)
ENDIF()

IF(CMAKE_BUILD_TYPE_UPPER STREQUAL "DEBUG" OR WITH_DEBUG)
  MESSAGE(STATUS "Build with debug mode!")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -O0 -fno-omit-frame-pointer ${EXTRA_SAN_FLAG}")
ELSE()
  MESSAGE(STATUS "Build with release mode!")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -O3 -fPIC")
ENDIF()

IF(NOT MINIMAL_MAKE)
  MESSAGE(STATUS "Build with full mode!")
ELSE()
  MESSAGE(STATUS "Build with minimal mode!")
ENDIF()

IF(WITH_FAIL_POINT)
  MESSAGE(STATUS "Build with fail point!")
ELSE()
  MESSAGE(STATUS "Build without fail point!")
ENDIF()

INCLUDE_DIRECTORIES (
  ${CMAKE_CURRENT_SOURCE_DIR}/include/
  ${CMAKE_CURRENT_SOURCE_DIR}/../dependency/easy/src/
  ${CMAKE_CURRENT_SOURCE_DIR}/../dependency/easy/src/include/
  )

LINK_DIRECTORIES(
  ${CMAKE_INSTALL_PREFIX}/lib
  )

SET (CONSENSUS_PB_FILES ${CMAKE_CURRENT_BINARY_DIR}/paxos.pb.cc)
SET (CONSENSUS_PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/protocol/paxos.proto)
ADD_CUSTOM_COMMAND(
  OUTPUT ${CONSENSUS_PB_FILES}
  COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
  ARGS
    --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
    -I "${PROTOBUF_INCLUDE_DIR}"
    -I ./
    paxos.proto
  DEPENDS
    ${PROTOBUF_PROTOC_EXECUTABLE}
    ${CONSENSUS_PROTO_FILE}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/protocol
  )

ADD_CUSTOM_TARGET(consensus_pb_file
  ALL DEPENDS ${CONSENSUS_PB_FILES}
  )

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})
INCLUDE_DIRECTORIES(${PROTOBUF_INCLUDE_DIR})

ADD_LIBRARY (aliconsensus STATIC ${CONSENSUS_SRC_FILES} ${CONSENSUS_PB_FILES})
ADD_DEPENDENCIES(aliconsensus consensus_pb_file)
TARGET_LINK_LIBRARIES (aliconsensus ${PROTOBUF_LIBRARY} pthread z
  rt ssl crypto myeasy ${EXTRA_LINK})

IF(WITH_COVERAGE)
  IF(CMAKE_CXX_COMPILER_ID MATCHES GNU)
    MESSAGE(status "Coverage analysis with gcov enabled")
    TARGET_COMPILE_OPTIONS(aliconsensus PUBLIC -fprofile-arcs -ftest-coverage -g)
    TARGET_LINK_LIBRARIES(aliconsensus PUBLIC gcov)
  ELSE()
    MESSAGE(WARNING "Coverage not supported for this compiler")
  ENDIF()
ENDIF()

IF(WITH_INSTALL)
  INSTALL(TARGETS aliconsensus DESTINATION lib)
  INSTALL(FILES ${CONSENSUS_HEADER_FILES} DESTINATION include/aliconsensus)
ENDIF()

IF(WITH_TEST)
  #INCLUDE(ProcessorCount)
  #ProcessorCount(N)
  #IF(NOT N EQUAL 0)
  #  SET(CTEST_ARGS -j${N})
  #ENDIF()
  SET(CTEST_ARGS -j1)

  ADD_CUSTOM_TARGET(
    unit-test
    COMMAND ctest3 ${CTEST_ARGS} --timeout=300
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/consensus/unittest
    USES_TERMINAL
    )

  ADD_SUBDIRECTORY(unittest EXCLUDE_FROM_ALL)

  ADD_CUSTOM_TARGET(
    stress-test
    COMMAND ctest3 ${CTEST_ARGS} --timeout=300
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/consensus/stresstest
    USES_TERMINAL
    )

  ADD_SUBDIRECTORY(stresstest EXCLUDE_FROM_ALL)

ENDIF()

IF(WITH_EXAMPLES)
  ADD_CUSTOM_TARGET(
      consensus_example
      COMMAND ctest3 ${CTEST_ARGS} --timeout=300
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/consensus/example
      USES_TERMINAL
      )

  ADD_SUBDIRECTORY(example EXCLUDE_FROM_ALL)
ENDIF()

set( CMAKE_EXPORT_COMPILE_COMMANDS 1 )
