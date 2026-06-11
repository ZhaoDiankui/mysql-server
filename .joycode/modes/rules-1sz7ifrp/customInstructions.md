所有拆分后的文档必须满足：

1. 一个 Markdown 文件只描述一个概念或一个操作
2. 文件命名使用 kebab-case
3. 文档必须包含以下标准结构：

# Title
一句话描述该功能或命令

## Overview
用途、适用场景、限制

## Syntax (如果是命令/SQL)
完整语法

## Parameters (如适用)
参数表

## Examples
至少一个示例

## Notes / Compatibility
与 MySQL / PostgreSQL / StarDB 版本的关系


你需要为 StarDB 生成一个 llms.txt 文件，用于 LLM 理解 StarDB 文档结构。
该文件只包含：
- 文档名称
- 文档用途说明（1-2 行）
- 指向的 Markdown 文件名

StarDB 的 llms.txt 示例结构:
# StarDB Documentation for LLMs

## Overview
- StarDB Architecture Overview
- StarDB SQL Compatibility

## SQL Statements
- sql-statement-admin-show-ddl.md
- sql-statement-admin-pause-ddl.md

## Cluster Management
- admin-cluster-scale.md
- admin-cluster-upgrade.md

## Storage & Replication
- replication-overview.md
- replication-s3.md

## Troubleshooting
- ddl-stuck.md
- performance-hotspot.md

TiDB的LLMS：
- https://docs.pingcap.com/llms.txt  
- https://docs.pingcap.com/tidb/stable/sql-statement-admin-show-ddl.md