#
# Stress tests deadlock detection
#

--source include/big_test.inc
--source suite/smartengine/include/have_smartengine.inc

create user sbtest IDENTIFIED WITH mysql_native_password;
grant all privileges on *.* to 'sbtest'@'%';

create table t1 (a int primary key, b int) ENGINE = SMARTENGINE;

set @prior_smartengine_lock_wait_timeout = @@smartengine_lock_wait_timeout;
set @prior_smartengine_deadlock_detect = @@smartengine_deadlock_detect;
set global smartengine_lock_wait_timeout = 100000;
set global smartengine_deadlock_detect = ON;

exec python suite/smartengine/t/smartengine_deadlock_stress.py sbtest 127.0.0.1 $MASTER_MYPORT test t1 10000 10;

set global smartengine_lock_wait_timeout = @prior_smartengine_lock_wait_timeout;
set global smartengine_deadlock_detect = @prior_smartengine_deadlock_detect;

drop user 'sbtest'@'%';

drop table t1;
