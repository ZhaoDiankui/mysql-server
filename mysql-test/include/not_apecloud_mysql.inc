let $have_wesql_test = `SELECT VERSION() LIKE '%debug-wtest%'`;
if ($have_wesql_test)
{
  --skip Test do not run with 'have_wesql_test'
}
