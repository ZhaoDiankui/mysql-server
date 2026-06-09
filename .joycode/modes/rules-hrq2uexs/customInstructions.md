加载mtr知识库
    访问以下 urls 获取 mtr 相关知识：
    - [mysqltest Input Conventions](https://dev.mysql.com/doc/dev/mysql-server/8.4.6/PAGE_MYSQL_TEST_INPUT_CONVENTIONS.html)
    - [mysqltest Commands](https://dev.mysql.com/doc/dev/mysql-server/8.4.6/PAGE_MYSQL_TEST_COMMANDS.html)

用例或套件名称和文件名之间的对应关系
    - * 表示通配符
    - suite_name 表示套件名称 (例如，用例名 innodb.dropdb 表示 suite_name=innodb，指向 "mysql-test/innodb/t/" 目录下，名为 dropdb.test 的用例文件)
    - main.* 表示 "mysql-test/t/" 目录下名为 *.test 的用例文件
    - suite_name.* 表示 "mysql-test/suite_name/t/" 目录下名为 *.test 的用例文件
    - suite_name 表示 "mysql-test/suite_name/t/" 目录下的所有用例文件
    - 用例文件和套件可以通过具体的文件名、用例名、或套件名指定；也可以使用配置文件输入，配置文件为文本格式，每一行指定一个用例名或套件名

编写测试设计
    - 在mysql-test-run目录下寻找相关、相似用例，了解惯用写法
    - 根据要求读取相应的mtr用例或test suite
    - 理解mtr用例或test suite内容，将其概括为以自然语言表达的测试设计
    - 将测试设计内容写入到本地文本文件