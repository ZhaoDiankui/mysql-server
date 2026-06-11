--connect (consensus1,127.0.0.1,root,,test,$MYPORT_1)
--connect (consensus2,127.0.0.1,root,,test,$MYPORT_2)
--connect (consensus3,127.0.0.1,root,,test,$MYPORT_3)

connection consensus1;
--let $CONSENSUS_PORT_1=`select @@port + 8000;`
connection consensus2;
--let $CONSENSUS_PORT_2=`select @@port + 8000;`
connection consensus3;
--let $CONSENSUS_PORT_3=`select @@port + 8000;`
connection default;
