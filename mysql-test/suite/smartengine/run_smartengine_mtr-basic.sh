#!/bin/bash
# by wuha.csb@alibaba-inc.com 2019.7.13

CURRENT_DIR="`pwd`"

if [[ ! -d ${CURRENT_DIR}/mysql-test ]] || [[ ! -f ${CURRENT_DIR}/build.sh ]]; then
  echo "You should run this script in the root directory of MySQL-smartengine source"
  exit 1
fi

P=${1:-48}
MTR_TEST_DIR=${CURRENT_DIR}/mysql-test
MAX_FAIL=0   # run till all test cases finished
SUITES="smartengine,smartengine_binlog,smartengine_main,smartengine_rpl,smartengine_rpl_basic,smartengine_binlog_gtid,smartengine_rpl_gtid"

echo "We will run basic smartengine MTR cases(${SUITES}) in ${MTR_TEST_DIR}"
cd ${MTR_TEST_DIR}
#
RESULT=${MTR_TEST_DIR}/BASIC_SMARTENGINE_SUITES_RESULT
./mysql-test-run.pl --mysqld=--smartengine=1 \
                    --suite=${SUITES} --parallel=${P} --force --retry-failure=1 \
                    --skip-test-list=suite/smartengine/disabled_smartengine-basic.def \
                    --max-test-fail=${MAX_FAIL} | tee -a ${RESULT}

# backup runtime environment for tracing
/bin/rm -rf var_back
mv var var_back

FAIL_CASES=$(grep "Failing test(s)" ${RESULT} | sed  s'/Failing test(s)://g')
if [[ ! -z "${FAIL_CASES}" ]]; then
    echo "Following are failed cases, now run them one by one"
    echo "======================================================================"
    echo ${FAIL_CASES}
    echo "======================================================================"
    LOG_ONE=${MTR_TEST_DIR}/RUN_ONE_RESULT
    for c in ${FAIL_CASES}; do
        ./mysql-test-run.pl --mysqld=--smartengine=1 --force --quiet --retry=0 ${c} | tee -a ${LOG_ONE}
    done
    FAIL_CASES=$(grep "Failing test(s)" ${LOG_ONE} | sed  s'/Failing test(s): //g')
    if [[ -f ${LOG_ONE} ]]; then
      echo -e "\n\nLog for running failed cases one by one" >> ${RESULT}
      cat ${LOG_ONE} >> ${RESULT}
      /bin/rm -f ${LOG_ONE}
    fi
    if [[ ! -z "${FAIL_CASES}" ]]; then
        echo "Following cases are failed again:"
        echo "=================================================================="
        echo ${FAIL_CASES}
        echo "=================================================================="
        echo "${FAIL_CASES}" > ${MTR_TEST_DIR}/BASIC_SMARTENGINE_FAIL_CASES
    fi
fi

