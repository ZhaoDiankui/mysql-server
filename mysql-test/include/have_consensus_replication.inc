if (!`select count(*) from information_schema.plugins where PLUGIN_NAME like 'raft_replication'`)
{
  --skip Consensus Replication plugin
}

if (!`select PLUGIN_STATUS='ACTIVE' from information_schema.plugins where PLUGIN_NAME like 'raft_replication'`)
{
  --skip Consensus Replication plugin
}