# wait become leader
let $wait_timeout= 600;
let $wait_condition= select count(*)=1 from information_schema.wesql_cluster_local where role='leader';
--source include/wait_condition.inc

# wait apply index = commit index
let $wait_timeout= 600;
let $wait_condition= select LAST_LOG_INDEX=LAST_APPLY_INDEX from information_schema.wesql_cluster_local limit 1;
--source include/wait_condition.inc

# wait applyindex catch up
let $wait_timeout= 600;
let $wait_condition= select count(distinct applied_index)=1 from information_schema.wesql_cluster_global;
--source include/wait_condition.inc
