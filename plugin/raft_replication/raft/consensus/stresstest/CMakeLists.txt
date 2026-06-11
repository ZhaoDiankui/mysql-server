SET (CONSENSUS_STORAGE_FILES
  ../storage/file_paxos_log.cc
  ../storage/mem_paxos_log.cc
  ../storage/rd_paxos_log.cc)

SET(STRESS_TESTS
  paxos_log
  consensus_high_availability
  consensus-stability
)


INCLUDE_DIRECTORIES(${rocksdb_SOURCE_DIR})
INCLUDE_DIRECTORIES(${rocksdb_SOURCE_DIR}/include)
INCLUDE_DIRECTORIES(${gtest_SOURCE_DIR}/include)

FOREACH(test ${STRESS_TESTS})
  SET(target ${test}-st)

  ADD_EXECUTABLE(${target} ${test}-st.cc ${CONSENSUS_STORAGE_FILES})
  TARGET_LINK_LIBRARIES(${target} aliconsensus ${PROTOBUF_LIBRARY} myeasy rocksdb gtest gtest_main ${EXTRA_LINK})

  ADD_TEST(${test} ${TARGET_RUNTIME_OUTPUT_DIRECTORY}/${target})

  ADD_DEPENDENCIES(stress-test ${target})
ENDFOREACH()
