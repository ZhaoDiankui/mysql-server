--connect (conn1,127.0.0.1,root,,test,$MYPORT_1)
--connect (conn2,127.0.0.1,root,,test,$MYPORT_1)
--connect (conn3,127.0.0.1,root,,test,$MYPORT_1)

--connect (conn,127.0.0.1,root,,test,$MYPORT_2)
--connection conn
let $master_uuid2= `SELECT @@GLOBAL.SERVER_UUID`;
--disconnect conn
--connect (conn,127.0.0.1,root,,test,$MYPORT_3)
--connection conn
let $master_uuid3= `SELECT @@GLOBAL.SERVER_UUID`;
--disconnect conn
# leader transfer at sync point $sync_point

--connection conn1
let $master_uuid1= `SELECT @@GLOBAL.SERVER_UUID`;
create table t1(id int);
eval SET debug_sync = '$sync_point SIGNAL step_$sync_point WAIT_FOR continue_$sync_point';
send insert into t1 values (1);

--connection conn2
eval SET debug_sync = 'now WAIT_FOR step_$sync_point';

let $counter = 6000;
let $check_counter = 10;
--connect (conn4,127.0.0.1,root,,test,$MYPORT_2)
--connection conn4
while ($counter > 0) {
    let $success= `select count(*)=1 from information_schema.wesql_cluster_local where role='leader'`;
    if ($success) {
        let $counter= 0;
    }
    if (!$success) {
        dec $check_counter;
        if ($check_counter == 0) {
            eval call dbms_consensus.force_promote();
            let $check_counter= 10;
        }
        sleep 0.1;
        dec $counter;
    }
}
# wait leader enable read/write
let $wait_timeout= 60;
let $wait_condition= select SERVER_READY_FOR_RW='Yes' from information_schema.wesql_cluster_local;
--source include/wait_condition.inc
--disconnect conn4

--connection conn3
# maybe fail
send insert into t1 values(2);
--connection conn2
eval SET debug_sync = "now SIGNAL continue_$sync_point";

--connect (conn4,127.0.0.1,root,,test,$MYPORT_2)
--connection conn4
insert into t1 values(3);
--source include/wait_follower_catchup.inc

let $counter = 6000;
let $check_counter = 10;
--connection conn2
while ($counter > 0) {
    let $success= `select count(*)=1 from information_schema.wesql_cluster_local where role='leader'`;
    if ($success) {
        let $counter= 0;
    }
    if (!$success) {
        dec $check_counter;
        if ($check_counter == 0) {
            eval call dbms_consensus.force_promote();
            let $check_counter= 10;
        }
        sleep 0.1;
        dec $counter;
    }
}
# wait leader enable read/write
let $wait_timeout= 60;
let $wait_condition= select SERVER_READY_FOR_RW='Yes' from information_schema.wesql_cluster_local;
--source include/wait_condition.inc
let $cnt1= `select count(*) from t1`;

--connection conn4
let $cnt2= `select count(*) from t1`;
if ($cnt1 == $cnt2)
{
    echo "data consistent!";
}
if($cnt1 != $cnt2)
{
    echo "data not consistent!";
}
--disconnect conn4

--connection conn1
if ($should_commit != 1)
{
    error 7500, 1598;
}
reap;
let $should_commit=;
drop table t1;
--disconnect conn1
--disconnect conn2
--disconnect conn3
