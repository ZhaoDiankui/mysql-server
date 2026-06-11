#ifndef MYSQL_PLUGIN_CONSENSUS_REPLICATION_INCLUDED
#define MYSQL_PLUGIN_CONSENSUS_REPLICATION_INCLUDED

/**
  @file include/mysql/plugin_group_replication.h
  API for Group Replication plugin. (MYSQL_GROUP_REPLICATION_PLUGIN)
*/

#include <mysql/plugin.h>
#define MYSQL_CONSENSUS_REPLICATION_INTERFACE_VERSION 0x0101

struct st_mysql_consensus_replication {
  int interface_version;

  /*
    This function is used to get the current consensus replication running status.
  */
  bool (*is_running)();

  /*
    This function is used to check consensus applier status.
  */
  bool (*is_applier_running)();

  /*
    This function is used to check log mode.
  */
  bool (*is_log_mode)();

  /*
    This function is used to check leader state.
  */
  bool (*is_state_leader)(uint64 &term);

  /*
    This function is used to show consensus logs.
  */
  bool (*show_logs)(void *thd);

  /*
    This function is used to show consensus log events.
  */
  bool (*show_log_events)(void *thd);

  /* TODO: consensus replication views */
};

#endif
