// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <google/protobuf/io/coded_stream.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "file_paxos_log.h"
#include "files.h"
#include "msg_compress.h"
#include "paxos.h"
#include "paxos.pb.h"
#include "paxos_configuration.h"
#include "paxos_server.h"

using namespace alisql;

TEST(MsgCompress, Compression) {
  int threshold = 4096;

  std::string s(threshold, '0');
  for (int i = 0; i < threshold; ++i) s[i] = i % 256;

  LogEntry le;
  le.set_index(0);
  le.set_term(0);
  le.set_optype(0);
  le.set_value(s);
  {
    PaxosMsg msg;
    *(msg.mutable_entries()->Add()) = le;
    ASSERT_FALSE(msg.has_compressedentries());
    MsgCompressOption option(None, threshold, false);
    msgCompress(option, msg, le.ByteSizeLong());
    ASSERT_FALSE(msg.has_compressedentries());
    option = MsgCompressOption((MsgCompressionType)3190, threshold, false);
    msgCompress(option, msg, le.ByteSizeLong());
    ASSERT_FALSE(msg.has_compressedentries());
  }

#ifdef LZ4
  {
    std::cout << "test LZ4" << std::endl;
    PaxosMsg msg;
    le.set_value(std::string(threshold / 2, '0'));
    *(msg.mutable_entries()->Add()) = le;
    MsgCompressOption option(LZ4, threshold, false);
    msgCompress(option, msg, le.ByteSize());
    // not reaching compression threshold
    ASSERT_FALSE(msg.has_compressedentries());

    le.set_value(s);
    msg.mutable_entries()->Clear();
    *(msg.mutable_entries()->Add()) = le;
    ASSERT_TRUE(msgCompress(option, msg, le.ByteSize()));
    ASSERT_TRUE(msg.entries_size() == 0);
    ASSERT_TRUE(msg.has_compressedentries() &&
                msg.compressedentries().type() == LZ4);
  }
#endif

#ifdef ZSTD
  {
    std::cout << "test ZSTD" << std::endl;
    PaxosMsg msg;
    le.set_value(std::string(threshold / 2, '0'));
    *(msg.mutable_entries()->Add()) = le;
    MsgCompressOption option(ZSTD, threshold, false);
    msgCompress(option, msg, le.ByteSize());
    // not reaching compression threshold
    ASSERT_FALSE(msg.has_compressedentries());

    le.set_value(s);
    msg.mutable_entries()->Clear();
    *(msg.mutable_entries()->Add()) = le;
    ASSERT_TRUE(msgCompress(option, msg, le.ByteSize()));
    ASSERT_TRUE(msg.entries_size() == 0);
    ASSERT_TRUE((msg.has_compressedentries() &&
                 msg.compressedentries().type() == ZSTD));
  }
#endif
}

TEST(MsgCompress, Decompression) {
  int threshold = 4096;

  std::string s(threshold, '0');
  for (int i = 0; i < threshold; ++i) s[i] = i % 256;

  LogEntry le;
  le.set_index(0);
  le.set_term(0);
  le.set_optype(0);
  le.set_value(s);

#ifdef LZ4
  std::cout << "test LZ4" << std::endl;
  PaxosMsg msg1;
  *(msg1.mutable_entries()->Add()) = le;
  *(msg1.mutable_entries()->Add()) = le;
  MsgCompressOption option_lz4(LZ4, threshold, true /* checksum */);
  msgCompress(option_lz4, msg1, le.ByteSize() * 2);

  msg1.mutable_compressedentries()->set_type(3190);
  ASSERT_FALSE(msgDecompress(msg1));

  msg1.mutable_compressedentries()->set_type(LZ4);
  ASSERT_TRUE(msgDecompress(msg1));
  ASSERT_FALSE(msg1.has_compressedentries());
  ASSERT_TRUE(msg1.entries_size() == 2);
  ASSERT_TRUE(msg1.entries().Get(0).value() == s);
  ASSERT_TRUE(msg1.entries().Get(1).value() == s);
#endif

#ifdef ZSTD
  std::cout << "test ZSTD" << std::endl;
  PaxosMsg msg2;
  *(msg2.mutable_entries()->Add()) = le;
  *(msg2.mutable_entries()->Add()) = le;
  MsgCompressOption option_zstd(ZSTD, threshold, true /* checksum */);
  msgCompress(option_zstd, msg2, le.ByteSize() * 2);

  ASSERT_TRUE(msgDecompress(msg2));
  ASSERT_FALSE(msg2.has_compressedentries());
  ASSERT_TRUE(msg2.entries_size() == 2);
  ASSERT_TRUE(msg2.entries().Get(0).value() == s);
  ASSERT_TRUE(msg2.entries().Get(1).value() == s);
#endif
}

TEST(MsgCompress, CompressOption) {
  std::vector<std::string> strConfig;
  strConfig.emplace_back("127.0.0.1:11001");
  strConfig.emplace_back("127.0.0.1:11002");
  strConfig.emplace_back("127.0.0.1:11003");

  std::shared_ptr<PaxosLog> rlog =
      std::make_shared<FilePaxosLog>(std::string("paxosLogTestDir1"));
  Paxos* paxos = new Paxos(10000, rlog);
  paxos->init(strConfig, 1);

  ASSERT_EQ(paxos->setMsgCompressOption(LZ4, 4096, false), 0);
  ASSERT_EQ(paxos->setMsgCompressOption(ZSTD, 8192, true, "127.0.0.1:11002"),
            0);
  ASSERT_EQ(paxos->setMsgCompressOption(ZSTD, 8192, true, "127.0.0.1:11004"),
            1);

  auto config = paxos->getConfig();
  {
    MsgCompressOption option =
        ((RemoteServer*)(config->getServer(2).get()))->msgCompressOption;
    ASSERT_EQ((int)option.type(), ZSTD);
    ASSERT_EQ((int)option.sizeThreshold(), 8192);
    ASSERT_EQ(option.checksum(), true);
  }

  {
    MsgCompressOption option =
        ((RemoteServer*)(config->getServer(3).get()))->msgCompressOption;
    ASSERT_EQ((int)option.type(), LZ4);
    ASSERT_EQ((int)option.sizeThreshold(), 4096);
    ASSERT_EQ(option.checksum(), false);
  }
  deleteDir("paxosLogTestDir1");
}
