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
SUITES="smartengine,smartengine_audit_null,smartengine_auth_sec,smartengine_binlog,smartengine_binlog_gtid,smartengine_binlog_nogtid,smartengine_collations,smartengine_ext,smartengine_main,smartengine_perfschema,smartengine_rpl,smartengine_rpl_basic,smartengine_rpl_gtid,smartengine_rpl_nogtid,smartengine_stress,smartengine_sysschema,smartengine_sysvars"

echo "We will run smartengine MTR cases(${SUITES}) in ${MTR_TEST_DIR}"
cd ${MTR_TEST_DIR}
#
RESULT=${MTR_TEST_DIR}/SMARTENGINE_SUITES_RESULT
./mysql-test-run.pl --mysqld=--smartengine=1 --sanitize \
                    --suite=${SUITES} --parallel=${P} --force --retry-failure=1 \
                    --skip-test-list=suite/smartengine/disabled_smartengine.def \
                    --max-test-fail=${MAX_FAIL} > ${RESULT} 2>&1

# backup runtime environment for tracing
/bin/rm -rf smartengine_var_back
mv var smartengine_var_back

cat ${RESULT} | grep "Completed:"
echo "Test result in ${RESULT}"

FAIL_CASES=$(grep "Failing test(s)" ${RESULT} | sed  s'/Failing test(s)://g')
if [[ ! -z "${FAIL_CASES}" ]]; then
    echo "Following are failed cases, now run them one by one"
    echo "======================================================================"
    echo ${FAIL_CASES}
    echo "======================================================================"
    LOG_ONE=${MTR_TEST_DIR}/RUN_ONE_RESULT
    for c in ${FAIL_CASES}; do
        ./mysql-test-run.pl --mysqld=--smartengine=1 --sanitize --force --quiet --retry=0 ${c} >> ${LOG_ONE}
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
        echo "${FAIL_CASES}" > ${MTR_TEST_DIR}/SMARTENGINE_FAIL_CASES
    fi
fi

C=smartengine_stress.concurrent
REP=200
LOG=${MTR_TEST_DIR}/${C}-${REP}.log
[ -f ${LOG} ] && /bin/rm -f ${LOG}
echo "Running ${C} for ${REP} times output to ${LOG}"
# smartengine MTR and stock MTR need 5~7 hours together, set 15 hours for this timeout
./mysql-test-run.pl --mysqld=--smartengine=1 --testcase-timeout=900 --retry=0 --repeat=${REP} ${C} | tee -a ${LOG}
PASSED=`grep '\[ pass \]' ${LOG} | wc -l`
if [ ${PASSED} -lt ${REP} ]; then
    # save the runtime
    mv var ${C}-${REP}-${PASSED}-var
fi

# ignore any error
exit 0

