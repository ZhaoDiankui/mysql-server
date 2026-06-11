cmake_minimum_required(VERSION 2.8)

SET (CONSENSUS_STORAGE_FILES
  ../storage/file_paxos_log.cc
  ../storage/mem_paxos_log.cc
  ../storage/rd_paxos_log.cc)

INCLUDE_DIRECTORIES(${rocksdb_SOURCE_DIR})
INCLUDE_DIRECTORIES(${rocksdb_SOURCE_DIR}/include)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -fsanitize=address -fno-omit-frame-pointer -ggdb")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
SET (EXTRA_LINK tcmalloc_and_profiler)

INCLUDE_DIRECTORIES (${CMAKE_CURRENT_SOURCE_DIR}/../output/include/aliconsensus
  ${CMAKE_CURRENT_SOURCE_DIR}/../output/include/easy)

link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../output/lib
  ${CMAKE_CURRENT_SOURCE_DIR}/../../dependency/rocksdb/)

ADD_EXECUTABLE (echo_debug echo_debug.cc ${CONSENSUS_STORAGE_FILES})
target_link_libraries (echo_debug aliconsensus ${PROTOBUF_LIBRARY} rocksdb pthread snappy z rt ssl crypto myeasy)
ADD_DEPENDENCIES(consensus_example echo_debug)

ADD_EXECUTABLE (echo_learner echo_learner.cc ${CONSENSUS_STORAGE_FILES})
target_link_libraries (echo_learner aliconsensus ${PROTOBUF_LIBRARY} rocksdb pthread snappy z rt ssl crypto myeasy)
ADD_DEPENDENCIES(consensus_example echo_learner)

ADD_EXECUTABLE (echo_purgelog echo_purgelog.cc ${CONSENSUS_STORAGE_FILES})
target_link_libraries (echo_purgelog aliconsensus ${PROTOBUF_LIBRARY} rocksdb pthread snappy z rt ssl crypto myeasy)
ADD_DEPENDENCIES(consensus_example echo_purgelog)

ADD_EXECUTABLE (benchmark3 benchmark3.cc ${CONSENSUS_STORAGE_FILES})
target_link_libraries (benchmark3 aliconsensus ${PROTOBUF_LIBRARY} rocksdb pthread snappy z rt ssl crypto myeasy)
ADD_DEPENDENCIES(consensus_example benchmark3)

ADD_EXECUTABLE (benchmark benchmark.cc ${CONSENSUS_STORAGE_FILES})
target_link_libraries (benchmark aliconsensus ${PROTOBUF_LIBRARY} rocksdb pthread snappy z rt ssl crypto myeasy)
ADD_DEPENDENCIES(consensus_example benchmark)

ADD_EXECUTABLE (benchmark_compression benchmark_compression.cc ${CONSENSUS_STORAGE_FILES})
target_link_libraries (benchmark_compression aliconsensus ${PROTOBUF_LIBRARY} rocksdb pthread snappy z rt ssl crypto myeasy)
ADD_DEPENDENCIES(consensus_example benchmark_compression)
