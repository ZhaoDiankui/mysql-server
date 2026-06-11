# Introduction

ApeCloud Consensus Library is an open-source C++ library implementing the Raft consensus algorithm. And it is forked from [Alibaba X-Paxos](https://github.com/polardb/polardbx-engine/tree/main/extra/IS). The original goal of this library is to provide a high-performance and easy-to-use implementation of the Raft algorithm for [ApeCloud MySQL Server](https://github.com/apecloud/wesql-server). And this library can also be used in other systems easily.


# Building

Pre-requisites:

* git
* cmake(> 3.13)
* gcc(>7) 

The following commands work in RHEL/CentOS. Haven't tried in other operation systems. 

```shell
sudo yum install openssl-devel

# needed by rocksdb, for unittests
sudo yum install gflags-devel snappy-devel

git clone git@github.com:apecloud/consensus-library.git -b develop

cd consensus-library
 
mkdir bu && cd bu

# debug mode(-D WITH_DEBUG=ON), complie and run the unittest(-D WITH_TEST=ON)
# Fail Point is enabled (-D WITH_FAIL_POINT=ON), complie examples(-D WITH_EXAMPLES=ON)
cmake3 -B ./ -D WITH_DEBUG=ON -D BUILD_WITH_PROTOBUF=bundled -D WITH_TEST=ON -D WITH_FAIL_POINT=ON -D WITH_EXAMPLES=ON -D WITH_INSTALL=ON ..

make -j$(echo "4 * $(nproc)" | bc)

# unittest
make -j$(echo "4 * $(nproc)" | bc) unit-test

# examples, see how to sun in example dir
make -j$(echo "4 * $(nproc)" | bc) consensus_example
```

# How to use

see examples in [examples](consensus/example/) and [unittest](consensus/unittest/).
