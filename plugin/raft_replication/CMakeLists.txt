
IF (NOT WITH_RAFT_REPLICATION)
  MESSAGE(STATUS "either WITHOUT_RAFT_REPLICATION=1 or WITH_RAFT_REPLICATION=0 is specified.")
  RETURN()
ENDIF()

SET(RAFT_REPLICATION_SOURCES
  bl_consensus_log.cc
  consensus_applier.cc
  consensus_binlog.cc
  consensus_binlog_recovery.cc
  consensus_common.cc
  consensus_fifo_cache_manager.cc
  consensus_log_index.cc
  consensus_log_manager.cc
  consensus_meta.cc
  consensus_state_process.cc
  consensus_prefetch_manager.cc
  consensus_proc.cc
  consensus_recovery_manager.cc
  i_s_consensus.cc
  observer_applier.cc
  observer_binlog_manager.cc
  observer_server_state.cc
  observer_trans.cc
  plugin.cc
  plugin_psi.cc
  rpl_consensus.cc
  system_variables.cc
)

IF(WIN32)
  # Activate necessary dllexport/dllimport declarations.
  ADD_DEFINITIONS(-DPROTOBUF_USE_DLLS)
ENDIF()

DISABLE_MISSING_PROFILE_WARNING()

INCLUDE_DIRECTORIES(
  ${CMAKE_CURRENT_SOURCE_DIR}/raft/dependency/easy/src
  ${CMAKE_CURRENT_SOURCE_DIR}/raft/dependency/easy/src/include
  ${CMAKE_CURRENT_BINARY_DIR}/raft/consensus/
  ${CMAKE_CURRENT_SOURCE_DIR}/raft/consensus/include)
SET(BUILD_WITH_PROTOBUF mysql_protobuf)
ADD_SUBDIRECTORY(raft)

ADD_DEFINITIONS(-DMYSQL_SERVER)
ADD_DEFINITIONS(-DLOG_SUBSYSTEM_TAG="Repl")
ADD_DEFINITIONS(-DLOG_COMPONENT_TAG="raft_replication")

### Configuration ###

IF(WIN32)
  ADD_DEFINITIONS(-DIMPORT_UNKNOWN_SQLSTATE)
ENDIF()

# Set extra flags for the C/CXX compiler
IF(MY_COMPILER_IS_GNU_OR_CLANG)
  STRING_APPEND(CMAKE_CXX_FLAGS " -Wno-sign-compare -Wno-suggest-override -Wno-undef -Wno-cast-qual -Wno-extra-semi -Wno-unused-parameter -Wno-unused-function")
ENDIF()

# declare the plugin itself
MYSQL_ADD_PLUGIN(raft_replication
  ${RAFT_REPLICATION_SOURCES}
  LINK_LIBRARIES
  ${SSL_LIBRARIES}
  myeasy
  aliconsensus
  rpl_replica
  ext::libprotobuf-lite
  MODULE_OUTPUT_NAME "raft_replication"
  )
