# ==== Purpose ====
# initialize a new learner node
#
# ==== usage ====
# [--let $learner_server_id= 6]
# [--let $learner_datadir= $MYSQL_TMP_DIR/learner1]
# [--let $learner_logdir= $MYSQL_TMP_DIR/server.log]
# [--let $learner_cluster_id= 1]
# [--let $learner_cluster_info= '127.0.0.1:13200']
# [--let $learner_port= 13009]
# --source include/start_new_learner.inc

--echo # start a new learner
if (!$learner_server_id)
{
	let $learner_server_id= 6;
}
if (!$learner_datadir)
{
	let $learner_datadir= $MYSQL_TMP_DIR/learner1;
}
if (!$learner_logdir)
{
	let $learner_logdir= $MYSQL_TMP_DIR/server.log;
}
if (!$learner_cluster_id)
{
	let $learner_cluster_id= 1;
}
if (!$learner_cluster_info)
{
	let $learner_cluster_info= '127.0.0.1:13200';
}
if (!$learner_port)
{
  let $learner_port= 13009;
}
--let $learner_socket_path= $learner_datadir.sock
--let BASEDIR= `select @@basedir`
--let DDIR= $learner_datadir
--let MYSQLD_LOG= $learner_logdir
let $bootstrap_sql_file = $MYSQLTEST_VARDIR/tmp/bootstrap.sql;
write_file $bootstrap_sql_file;
  CREATE DATABASE test;
EOF
--let extra_args=--defaults-file='$MYSQLTEST_VARDIR/my.cnf' --console --skip-ssl --basedir=$BASEDIR --lc-messages-dir=$MYSQL_SHAREDIR --socket=$learner_socket_path
--let init_args=--server_id=$learner_server_id --raft-replication-cluster-id=$learner_cluster_id --raft-replication-learner-node=ON --raft-replication-cluster-info=$learner_cluster_info
--exec if [ -d $DDIR ]; then rm -rf $DDIR; fi
--exec $MYSQLD $extra_args --initialize-insecure  --init-file=$bootstrap_sql_file --datadir=$DDIR $init_args > $MYSQLD_LOG 2>&1
--exec $MYSQLD $extra_args --datadir=$DDIR $init_args --raft-replication-force-change-meta=ON --port=$learner_port >> $MYSQLD_LOG 2>&1 &
--remove_file $bootstrap_sql_file

--disable_abort_on_error
--disable_query_log
--disable_result_log
--let $mysql_errno= 9999
--let $wait_counter= 300
while ($mysql_errno)
{
  connect (check_learner,127.0.0.1,root,,,$learner_port,);
  if ($mysql_errno == 1045){
    --let mysql_errno=0
  }
  if ($mysql_errname == ER_SECURE_TRANSPORT_REQUIRED){
    --let mysql_errno=0
  }
  if ($mysql_errno){
    --sleep 0.1
    --dec $wait_counter
    if (!$wait_counter){
      --die Server failed to connect new learner
    }
  }
}
connection default;
if(!$mysql_errno)
{
  --disconnect check_learner
}
--enable_result_log
--enable_query_log
--enable_abort_on_error
