# check error in smartengine log files

--let $MYSQL_DATADIR = `select @@datadir`
# if no ERROR log in log file, grep will return 1
# if no such error log file, will return 2
--error 0,1,2
--exec grep ' ERROR ' $MYSQL_DATADIR/.smartengine/Log* | grep -v 'SEDDL: build base error, code: 122, table_name' | grep -v 'SEDDL: failed to finish inplace populate indexes se_inplace_populate_indexes' | grep -v 'SEDDL: failed populating secondary key during alter, errcode=122, ddl_rebuild_type: 1'

