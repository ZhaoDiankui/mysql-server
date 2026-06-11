<h2>Parameter Template</h2>
The parameters of smartengine are divided into basic parameters and resource-related parameters.

<h3>Basic parameters</h3>

| Parameter Name| Default Value | Description |
| ------------- | ------------- | ----------- |
| loose_smartengine_data_dir | ${datadir}/smartengine |smartengine data directory |
| loose_smartengine_wal_dir | ${datadir}/smartengine | smartengine wal directory |
| innodb_buffer_pool_size | 128M | minimize innodb buffer pool size |


<h3>Resource-related parameters</h3>
Q_MEM: memory size(MB)</br>
Q_CPU: cpu size</br>

| Parameter Name| Recommended Value | Description |
| ------------- | ----------------- | ----------- |
| loose_smartengine_memtable_size | min(max(32, Q_MEM * 0.01), 256) | The maximum memory size used for a single active memtable |
| loose_smartengine_total_memtable_size | Q_MEM * 0.3 | The maximum memory size used for all memtables |
| loose_smartengine_block_cache_size | Q_MEM * 0.3 | The maximum memory size used for block cache |
| loose_smartengine_row_cache_size | Q_MEM * 0.1 | The maximum memory size used for row cache |
| loose_smartengine_total_wal_size | min(Q_MEM * 0.3, 12 * 1024) | The maximum size limit for write-ahead-log |
| loose_smartengine_flush_threads | max(1, min(Q_CPU / 2,  8)) | The number of threads used to execute flush tasks |
| loose_smartengine_compaction_threads | max(1, min(Q_CPU/2, 12)) | The number of threads used to execute compaction tasks |

