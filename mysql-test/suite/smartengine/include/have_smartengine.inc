--disable_query_log
SET default_storage_engine=smartengine;
--enable_query_log
if (`SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.ENGINES WHERE ENGINE = 'SMARTENGINE' AND support IN ('DEFAULT')`)
{
  --skip Test requires default engine smartengine
}

# comment temporarily before add slave_gtid_info
#if (`select count(*) = 0 from information_schema.tables where engine='smartengine' and table_name='slave_gtid_info'`)
#{
#  --skip Test requires default engine smartengine
#}
--disable_query_log
# Table statistics can vary depending on when the memtables are flushed, so
# flush them at the beginning of the test to ensure the test runs consistently.
set global smartengine_flush_memtable = true;
--enable_query_log

