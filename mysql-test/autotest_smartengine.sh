#!/usr/bin/env bash

[ ! -f ./autotest_helper.sh ] && echo "Missing autotest_helper.sh" && exit 1
source ./autotest_helper.sh
init_mtr_cmd $@

CMD="$CMD --do-suite=smartengine --mysqld=--smartengine=1 --nowarnings"

run_mtr "$CMD" var_smartengine smartengine.mtrresult
# Test smartengine cases
#CMD="$CMD --do-suite=smartengine --mysqld=--smartengine=1 --xcluster=0"
# --recovery-inconsistency-check doesn't support smartengine
#CMD="$CMD --mysqld=--recovery-inconsistency-check=off"

# with thread pool and ps-protocol disabled
#run_mtr "$CMD --mysqld=--thread-pool-enabled=0" var_smartengine smartengine.mtrresult
#run_mtr "$CMD" var_smartengine smartengine.mtrresult

# with ps-protocol enabled
#run_mtr "$CMD --mysqld=--thread-pool-enabled=0 --ps-protocol" var_smartengine_ps smartengine.ps_mtrresult

# with thread pool enabled
#run_mtr "$CMD --mysqld=--thread-pool-enabled=1" var_smartengine_tp smartengine.tp_mtrresult

# with thread pool enabled on ps-protocol
#run_mtr "$CMD --mysqld=--thread-pool-enabled=1 --ps-protocol" var_smartengine_tp_ps smartengine.tp_ps_mtrresult
