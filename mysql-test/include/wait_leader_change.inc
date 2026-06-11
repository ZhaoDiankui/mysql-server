--let $wait_counter= 300
--connect (con_tmp2,127.0.0.1,root,,test,$MYPORT_2)
--connect (con_tmp3,127.0.0.1,root,,test,$MYPORT_3)
--let $aux_connect=
while ($wait_counter)
{
    connection con_tmp2;
    --let $role=`select ROLE from information_schema.wesql_cluster_local`
    --let $success= 0
    if ($role == "Leader")
    {
        --let $aux_connect=con_tmp2
        --let $success= 1
    }
    if($success == 0)
    {
        connection con_tmp3;
        --let $role=`select ROLE from information_schema.wesql_cluster_local`
        if ($role == "Leader")
        {
            --let $aux_connect=con_tmp3
            --let $success= 1
        }
    }

    if ($success)
    {
        --let $wait_counter= 0
    }
    if (!$success)
    {
        sleep 0.5;
        dec $wait_counter;
    }
}

if (!$success)
{
  --die Timeout in wait_leader_change.inc leader election;
}

--let $wait_counter= 300
--let $success= 0
while ($wait_counter)
{
    connection $aux_connect;
    --let $is_ready=`select SERVER_READY_FOR_RW from information_schema.wesql_cluster_local`
    if ($is_ready == "Yes")
    {
        --let $success= 1
    }

    if ($success)
    {
        --let $wait_counter= 0
        if ($aux_connect == con_tmp2)
        {
            --let $leader_connect= connection2
        }
        if ($aux_connect == con_tmp3)
        {
            --let $leader_connect= connection3
        }
    }
    if (!$success)
    {
        sleep 0.5;
        dec $wait_counter;
    }
}

if (!$success)
{
  --die Timeout in wait_leader_change.inc wait leader ready for write;
}

--disconnect con_tmp2
--disconnect con_tmp3
