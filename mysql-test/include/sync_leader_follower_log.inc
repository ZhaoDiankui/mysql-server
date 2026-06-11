# ==== Purpose ====
#
# Waits until the follower log entry has been synced, i.e., all events
# have been copied to follower.
# Will switch to default connection after include
#
# ==== Usage ====
#
# [--let $follower_connection_name= <connection_name>]

# wait sync log
--sleep 1

--let $follower_connection= 
if (!$follower_connection_name)
{
  --echo Please specify follower_connection_name
  --die
}
--let $follower_connection = $follower_connection_name

let $wait_timeout= 60;
if ($wait_timeout)
{
  let $wait_counter= `SELECT $wait_timeout * 10`;
}
while ($wait_counter)
{
  --connection default
  let $master_last_log_index = query_get_value("select LAST_LOG_INDEX from information_schema.wesql_cluster_local", LAST_LOG_INDEX, 1);

  --connection $follower_connection
  let $follower_last_log_index = query_get_value("select LAST_LOG_INDEX from information_schema.wesql_cluster_local", LAST_LOG_INDEX, 1);
  
  --let $success= 0
  if ($master_last_log_index == $follower_last_log_index)
  {
    --let $success= 1
  }

  if ($success)
  {
      --let $wait_counter= 0
  }
  if (!$success)
  {
      sleep 0.1;
      dec $wait_counter;
  }
}

if (!$success)
{
  --die Timeout in sync_leader_follower_log.inc;
}

--connection default
