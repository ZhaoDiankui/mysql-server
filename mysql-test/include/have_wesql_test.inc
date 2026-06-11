let $have_wesql_test = `SELECT VERSION() LIKE '%debug-wtest%'`;
if (!$have_wesql_test)
{
  --skip Test requires 'have_wesql_test'
}
