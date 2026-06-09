SET (CONSENSUS_STORAGE_FILES
  ../storage/file_paxos_log.cc
  ../storage/mem_paxos_log.cc
  ../storage/rd_paxos_log.cc)

SET(CONSENSUS_TESTS
  client_service
  configuration
  consensus-add
  consensus-async
  consensus-configure-change
  consensus-corner-case
  consensus-crash
  consensus-del
  consensus-entries-precheck
  consensus-fault_simulation
  consensus-force_single_leader
  consensus-force-sync
  consensus-leader-election
  consensus-learner
  consensus-match-index
  consensus-misc
  consensus-purge
  consensus-replicate-with-cache-log
  consensus-topologies
  consensus-transfer
  consensus-truncate-log
  consensus-trx
  easy_debug_log
  easy_net
  fail_point
  log_meta_cache
  misc
  msg_compress
  paxos_log_cache
  paxos_log
  pb
  pxserror
  service
  thread_timer)


INCLUDE_DIRECTORIES(${rocksdb_SOURCE_DIR})
INCLUDE_DIRECTORIES(${rocksdb_SOURCE_DIR}/include)
INCLUDE_DIRECTORIES(${gtest_SOURCE_DIR}/include)

FOREACH(test ${CONSENSUS_TESTS})
  SET(target ${test}-t)

  ADD_EXECUTABLE(${target} ${test}-t.cc ${CONSENSUS_STORAGE_FILES})
  TARGET_LINK_LIBRARIES(${target} aliconsensus ${PROTOBUF_LIBRARY} myeasy rocksdb gtest gtest_main ${EXTRA_LINK})

  ADD_TEST(${test} ${TARGET_RUNTIME_OUTPUT_DIRECTORY}/${target})

  ADD_DEPENDENCIES(unit-test ${target})
ENDFOREACH()
