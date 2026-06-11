加载Memory存储引擎知识库
    访问以下 urls 获取 Memory存储引擎 相关知识：
    - [Memory Storage Engine](https://dev.mysql.com/doc/refman/8.4/en/memory-storage-engine.html)
    - [Optimizing for Memory Tables](https://dev.mysql.com/doc/refman/8.4/en/optimizing-memory-tables.html)
    - [Internal Temporary Table Use in MySQL](https://dev.mysql.com/doc/refman/8.4/en/internal-temporary-tables.html)
    - [CREATE TABLE Statement](https://dev.mysql.com/doc/refman/8.4/en/create-table.html)
    - [Replication and MEMORY Tables](https://dev.mysql.com/doc/refman/8.4/en/replication-features-memory.html)
    - [CREATE INDEX Statement](https://dev.mysql.com/doc/refman/8.4/en/create-index.html)
    - [GTID Life Cycle](https://dev.mysql.com/doc/refman/8.4/en/replication-gtids-lifecycle.html)
    - [Comparison of B-Tree and Hash Indexes](https://dev.mysql.com/doc/refman/8.4/en/index-btree-hash.html)
    - [Group Replication Requirements](https://dev.mysql.com/doc/refman/8.4/en/group-replication-requirements.html)

Memory存储引擎的本地代码路径
- storage/heap
- 读取分析storage/heap下完整的.h 和.cc文件，不能有任何文件缺失

编写需求和设计文档
    - 根据要求读取MEMORY存储引擎的代码
    - 理解理解代码中.h和.cc文件内容
    - 使用Mermaid8.0.8画图
    - 先生成markdown格式文件
    - 使用pandoc将markdown格式文档转弯word格式文件
    - 写入StarDB Memory存储引擎需求设计文档
    - 写入StarDB Memory存储引擎架构设计文档
    - 写入StarDB Memory存储引擎概要设计文档
    - 写入StarDB Memory存储引擎详细设计文档

需求设计文档内容包括如下章节
    - 需求背景：简要描述需求背景和目的。说明需求在行业市场或竞争产品差异、优势。说明本需求有对当前产品的核心指标要求。
    - 需求分析：分析满足用户使用场景。解决用户或当前产品中什么痛点问题。用户或产品期望达成什么目标。
    - 功能需求：列出关键功能点。描述详细要求。
    - 性能需求：需求期望的并行能力、响应时间(Latency)和吞吐量(tps)、扩展性等。RPO、RTO要求等。
    - 技术架构设计（选填）：系统技术栈、依赖组件。前端服务、后端服务、前后端交互链路流程、API网关、资源需求
    - 数据需求（选填）：数据来源和数据格式，日志数据格式。
    - 安全需求：数据加密。权限认证。访问控制，安全审计
    - 集成需求（选填）：第三方API。开放的标准API。
    - 测试要求（选填）：单元测试。集成测试。功能测试。性能测试。自动化测试。
    - 运维需求（选填）：部署和运维方案等。监控告警。日志记录和分析。
    - 兼容性要求（选填）：数据格式兼容。SQL语法兼容。操作系统兼容。硬件兼容。