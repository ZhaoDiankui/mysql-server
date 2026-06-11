# wait sync log
--sleep 1

# wait apply index = commit index
let $wait_timeout= 600;
let $wait_condition= select LAST_LOG_INDEX=LAST_APPLY_INDEX from information_schema.wesql_cluster_local limit 1;
--source include/wait_condition.inc

#LAST_APPLY_INDEX is not accurate, sleep 1s to ensure the condition is satisfied
--sleep 1
