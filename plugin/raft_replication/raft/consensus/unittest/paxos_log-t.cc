// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include "file_paxos_log.h"
#include "files.h"
#include "mem_paxos_log.h"
#include "paxos.pb.h"
#include "paxos_log.h"
#include "rd_paxos_log.h"
#include "ut_helper.h"

using namespace alisql;

#define AddMemPaxosLogTest(name, ...)     \
  TEST(MemPaxosLog, name) {               \
    MemPaxosLog* log = new MemPaxosLog(); \
    name(log, ##__VA_ARGS__);             \
    delete log;                           \
  }

#define AddFilePaxosLogTest(name, ...)                               \
  TEST(FilePaxosLog, name) {                                         \
    std::string dir = "FilePaxosLog_" #name;                         \
    deleteDir(dir.c_str());                                          \
    FilePaxosLog* log = new FilePaxosLog(dir, FilePaxosLog::LTFile); \
    name(log, ##__VA_ARGS__);                                        \
    delete log;                                                      \
    deleteDir(dir.c_str());                                          \
  }

#define AddRDPaxosLogTest(name, ...)                              \
  TEST(RDPaxosLog, name) {                                        \
    std::string dir = "RDPaxosLog_" #name;                        \
    deleteDir(dir.c_str());                                       \
    RDPaxosLog* log = new RDPaxosLog(dir, true, 4 * 1024 * 1024); \
    name(log, ##__VA_ARGS__);                                     \
    delete log;                                                   \
    deleteDir(dir.c_str());                                       \
  }

uint64_t getFirstLogIndex(PaxosLog* log) {
  if (typeid(*log) == typeid(MemPaxosLog)) {
    return dynamic_cast<MemPaxosLog*>(log)->getFirstLogIndex();
  } else if (typeid(*log) == typeid(FilePaxosLog)) {
    return dynamic_cast<FilePaxosLog*>(log)->getFirstLogIndex();
  } else if (typeid(*log) == typeid(RDPaxosLog)) {
    return dynamic_cast<RDPaxosLog*>(log)->getFirstLogIndex();
  }
  abort();
}

void testInit(PaxosLog* log) {
  if (typeid(*log) == typeid(MemPaxosLog)) {
    EXPECT_EQ(0, log->getLength());
  } else {
    // an empty entry is inserted during initialization for FilePaxosLog and
    // RDPaxosLog
    EXPECT_EQ(1, log->getLength());
  }
  EXPECT_EQ(0, getFirstLogIndex(log));
  EXPECT_EQ(0, log->getLastLogIndex());
}

AddMemPaxosLogTest(testInit);
AddFilePaxosLogTest(testInit);
AddRDPaxosLogTest(testInit);

void testAppend(PaxosLog* log) {
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  EXPECT_EQ(6, log->getLastLogIndex());
  if (typeid(*log) == typeid(RDPaxosLog) ||
      typeid(*log) == typeid(FilePaxosLog)) {
    // has a "first log" whose id is 0 inserted during initialization
    EXPECT_EQ(0, getFirstLogIndex(log));
    EXPECT_EQ(7, log->getLength());
  } else if (typeid(*log) == typeid(MemPaxosLog)) {
    EXPECT_EQ(1, getFirstLogIndex(log));
    EXPECT_EQ(6, log->getLength());
  }
}

AddMemPaxosLogTest(testAppend);
AddFilePaxosLogTest(testAppend);
AddRDPaxosLogTest(testAppend);

void testGetFirstAndLastLogIndex(PaxosLog* log) {
  EXPECT_EQ(0, getFirstLogIndex(log));
  EXPECT_EQ(0, log->getLastLogIndex());

  log->appendEmptyEntry();
  if (typeid(*log) == typeid(MemPaxosLog)) {
    EXPECT_EQ(1, getFirstLogIndex(log));
  } else {
    // an empty entry is inserted during initialization for FilePaxosLog and
    // RDPaxosLog
    EXPECT_EQ(0, getFirstLogIndex(log));
  }
  EXPECT_EQ(1, log->getLastLogIndex());

  log->appendEmptyEntry();
  EXPECT_EQ(2, log->getLastLogIndex());

  // delete all
  log->truncateBackward(0);
  if (typeid(*log) == typeid(MemPaxosLog)) {
    // do not support truncateBackward
  } else {
    EXPECT_EQ(0, getFirstLogIndex(log));
    EXPECT_EQ(0, log->getLastLogIndex());
  }
}

AddMemPaxosLogTest(testGetFirstAndLastLogIndex);
AddFilePaxosLogTest(testGetFirstAndLastLogIndex);
AddRDPaxosLogTest(testGetFirstAndLastLogIndex);

void testTruncateForward(PaxosLog* log) {
  EXPECT_EQ(0, log->getLastLogIndex());

  log->truncateForward(10);
  EXPECT_EQ(0, log->getLastLogIndex());

  for (int i = 0; i < 100; ++i) {
    log->appendEmptyEntry();
  }
  EXPECT_EQ(log->getLastLogIndex(), 100);

  // delete all entries with id <40
  log->truncateForward(40);
  EXPECT_EQ(getFirstLogIndex(log), 40);
  EXPECT_EQ(log->getLastLogIndex(), 100);
}

AddMemPaxosLogTest(testTruncateForward);
// doesn't support truncateForward
// AddFilePaxosLogTest(testTruncateForward);
AddRDPaxosLogTest(testTruncateForward);

void testTruncateBackward(PaxosLog* log) {
  EXPECT_EQ(0, log->getLastLogIndex());
  EXPECT_EQ(1, log->getLength());

  // delete all entries with id >= 10
  log->truncateBackward(10);
  EXPECT_EQ(0, log->getLastLogIndex());
  EXPECT_EQ(1, log->getLength());

  for (int i = 0; i < 100; ++i) {
    log->appendEmptyEntry();
  }
  EXPECT_EQ(log->getLastLogIndex(), 100);

  // delete all entries with id >= 101
  log->truncateBackward(101);
  EXPECT_EQ(log->getLastLogIndex(), 100);

  // delete all entries with id >= 100
  log->truncateBackward(100);
  EXPECT_EQ(log->getLastLogIndex(), 99);

  // delete all entries with id >= 40
  log->truncateBackward(40);
  EXPECT_EQ(log->getLastLogIndex(), 39);
}

// doesn't support truncateBackward
// AddMemPaxosLogTest(testTruncateBackward);
AddFilePaxosLogTest(testTruncateBackward);
AddRDPaxosLogTest(testTruncateBackward);

// TODO FilePaxosLog and RDPaxosLog have different behavior under this corner
// case, fix me later
void testTruncateBackwardZero(PaxosLog* log) {
  EXPECT_EQ(0, log->getLastLogIndex());
  EXPECT_EQ(1, log->getLength());

  // delete all entries with id >= 0
  log->truncateBackward(0);
  EXPECT_EQ(0, log->getLastLogIndex());
  if (typeid(*log) == typeid(FilePaxosLog)) {
    EXPECT_EQ(0, log->getLength());
  } else if (typeid(*log) == typeid(RDPaxosLog)) {
    EXPECT_EQ(1, log->getLength());
  }

  log->appendEmptyEntry();
  if (typeid(*log) == typeid(FilePaxosLog)) {
    EXPECT_EQ(log->getLastLogIndex(), 0);
  } else if (typeid(*log) == typeid(RDPaxosLog)) {
    EXPECT_EQ(log->getLastLogIndex(), 1);
  }
}

// doesn't support truncateBackward
// AddMemPaxosLogTest(testTruncateBackward);
AddFilePaxosLogTest(testTruncateBackwardZero);
AddRDPaxosLogTest(testTruncateBackwardZero);

void testAppendAfterTruncateForward(PaxosLog* log) {
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  EXPECT_EQ(6, log->getLastLogIndex());

  log->truncateForward(4);
  log->appendEmptyEntry();
  EXPECT_EQ(4, getFirstLogIndex(log));
  EXPECT_EQ(7, log->getLastLogIndex());
  EXPECT_EQ(4, log->getLength());
  LogEntry le;
  EXPECT_NE(0, log->getEntry(3, le));
  EXPECT_EQ(0, log->getEntry(4, le));
}

AddMemPaxosLogTest(testAppendAfterTruncateForward);
// don't support truncateForward
// AddFilePaxosLogTest(testAppendAfterTruncateForward);
AddRDPaxosLogTest(testAppendAfterTruncateForward);

void testTruncateForwardThenTruncateBackward(PaxosLog* log) {
  log->appendEmptyEntry();
  log->appendEmptyEntry();

  // delete all records except the last one
  log->truncateForward(10);
  EXPECT_EQ(2, log->getLastLogIndex());

  // delete all records equal or large than 0
  log->truncateBackward(0);
  if (typeid(*log) == typeid(MemPaxosLog)) {
    // do not support truncateBackward
  } else {
    EXPECT_EQ(0, log->getLength());
    EXPECT_EQ(0, getFirstLogIndex(log));
    EXPECT_EQ(0, log->getLastLogIndex());
  }
}

AddMemPaxosLogTest(testTruncateForwardThenTruncateBackward);
AddFilePaxosLogTest(testTruncateForwardThenTruncateBackward);
AddRDPaxosLogTest(testTruncateForwardThenTruncateBackward);

void testReboot(std::function<PaxosLog*()> newLog) {
  PaxosLog* log = newLog();
  auto reboot = [&] {
    delete log;
    log = newLog();
  };

  EXPECT_EQ(1, log->getLength());
  EXPECT_EQ(0, getFirstLogIndex(log));
  EXPECT_EQ(0, log->getLastLogIndex());

  reboot();

  EXPECT_EQ(1, log->getLength());
  EXPECT_EQ(0, getFirstLogIndex(log));
  EXPECT_EQ(0, log->getLastLogIndex());

  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  EXPECT_EQ(0, getFirstLogIndex(log));
  EXPECT_EQ(3, log->getLastLogIndex());

  reboot();

  EXPECT_EQ(0, getFirstLogIndex(log));
  EXPECT_EQ(3, log->getLastLogIndex());

  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  EXPECT_EQ(0, getFirstLogIndex(log));
  EXPECT_EQ(9, log->getLastLogIndex());

  reboot();
  EXPECT_EQ(0, getFirstLogIndex(log));
  EXPECT_EQ(9, log->getLastLogIndex());

  log->truncateBackward(7);
  EXPECT_EQ(0, getFirstLogIndex(log));
  EXPECT_EQ(6, log->getLastLogIndex());

  reboot();
  EXPECT_EQ(0, getFirstLogIndex(log));
  EXPECT_EQ(6, log->getLastLogIndex());

  if (typeid(*log) == typeid(RDPaxosLog)) {
    log->truncateForward(4);
    EXPECT_EQ(4, getFirstLogIndex(log));
    EXPECT_EQ(6, log->getLastLogIndex());

    reboot();
    EXPECT_EQ(4, getFirstLogIndex(log));
    EXPECT_EQ(6, log->getLastLogIndex());
  }

  log->appendEmptyEntry();
  log->appendEmptyEntry();
  EXPECT_EQ(8, log->getLastLogIndex());

  reboot();
  EXPECT_EQ(8, log->getLastLogIndex());

  delete log;
}

TEST(FilePaxosLog, testReboot) {
  std::string dir = "FilePaxosLog_testReboot";
  auto newLog = [dir]() -> PaxosLog* {
    FilePaxosLog* log = new FilePaxosLog(dir, FilePaxosLog::LTFile);
    return log;
  };
  deleteDir(dir.c_str());
  testReboot(newLog);
  deleteDir(dir.c_str());
}

TEST(RDPaxosLog, testReboot) {
  std::string dir = "RDPaxosLog_testReboot";
  auto newLog = [dir]() -> PaxosLog* {
    RDPaxosLog* log = new RDPaxosLog(dir, true, 4 * 1024 * 1024);
    return log;
  };
  deleteDir(dir.c_str());
  testReboot(newLog);
  deleteDir(dir.c_str());
}

TEST(MemPaxosLog, getEntry1) {
  MemPaxosLog* log = new MemPaxosLog();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  log->appendEmptyEntry();
  EXPECT_EQ(3, log->getLength());

  LogEntry le;
  log->getEntry(le);
  EXPECT_EQ(2, log->getLength());

  log->getEntry(le);
  EXPECT_EQ(1, log->getLength());

  delete log;
}

TEST(MemPaxosLog, getEntry1Timeout) {
  MemPaxosLog* log = new MemPaxosLog();
  log->appendEmptyEntry();
  EXPECT_EQ(1, log->getLength());

  LogEntry le;
  EXPECT_EQ(0, log->getEntry(le));
  EXPECT_EQ(0, log->getLength());

  // fail to get
  log->setReadTimeout(100);
  EXPECT_EQ(-1, log->getEntry(le));

  // get successfully before timeout
  log->setReadTimeout(300);
  auto thr = new std::thread([&log] {
    msleep(100);
    log->appendEmptyEntry();
  });
  EXPECT_EQ(0, log->getEntry(le));
  EXPECT_EQ(0, log->getLength());
  thr->join();

  delete log;
  delete thr;
}

TEST(MemPaxosLog, appendTimeout) {
  // cacheSize set to 10
  MemPaxosLog* log = new MemPaxosLog(0, 0, 10);
  log->setAppendTimeout(300);

  // cache is full
  for (int i = 0; i < 10; i++) {
    log->appendEmptyEntry();
  }
  EXPECT_EQ(10, log->getLastLogIndex());

  // fail to append
  log->appendEmptyEntry();
  EXPECT_EQ(10, log->getLastLogIndex());

  // append successfully before timeout
  auto thr = new std::thread([&log] {
    msleep(100);
    // delete the log entry #1
    log->truncateForward(2);
  });
  log->appendEmptyEntry();
  thr->join();
  EXPECT_EQ(11, log->getLastLogIndex());

  delete log;
  delete thr;
}


void test_paxoslog_append_and_truncate(PaxosLog* rlog) {
  LogEntry entry;

  EXPECT_EQ(rlog->getLastLogIndex(), 0);
  rlog->getEntry(0, entry);
  EXPECT_EQ(entry.term(), 0);
  EXPECT_EQ(entry.index(), 0);

  entry.set_term(1);
  entry.set_index(1);
  entry.set_optype(1);
  entry.set_ikey("aa");
  entry.set_value("bb");
  rlog->append(entry);

  entry.Clear();
  rlog->getEntry(1, entry);
  EXPECT_EQ(rlog->getLastLogIndex(), 1);
  EXPECT_EQ(entry.term(), 1);
  EXPECT_EQ(entry.index(), 1);
  EXPECT_EQ(entry.optype(), 1);
  EXPECT_EQ(entry.ikey(), "aa");
  EXPECT_EQ(entry.value(), "bb");

  entry.set_term(1);
  entry.set_index(2);
  entry.set_optype(1);
  entry.set_ikey("aa1");
  entry.set_value("bb1");
  rlog->append(entry);
  entry.set_term(1);
  entry.set_index(3);
  entry.set_optype(1);
  entry.set_ikey("aa2");
  entry.set_value("bb2");
  rlog->append(entry);

  entry.Clear();
  rlog->getEntry(3, entry);
  EXPECT_EQ(entry.term(), 1);
  EXPECT_EQ(entry.index(), 3);
  EXPECT_EQ(entry.optype(), 1);
  EXPECT_EQ(entry.ikey(), "aa2");
  EXPECT_EQ(entry.value(), "bb2");

  rlog->truncateBackward(2);
  EXPECT_EQ(rlog->getLastLogIndex(), 1);

  entry.Clear();
  rlog->getEntry(1, entry);
  EXPECT_EQ(rlog->getLastLogIndex(), 1);
  EXPECT_EQ(entry.term(), 1);
  EXPECT_EQ(entry.index(), 1);
  EXPECT_EQ(entry.optype(), 1);
  EXPECT_EQ(entry.ikey(), "aa");
  EXPECT_EQ(entry.value(), "bb");

  rlog->truncateBackward(1);
  EXPECT_EQ(rlog->getLastLogIndex(), 0);
}

TEST(FilePaxosLog, append_and_truncate) {
  PaxosLog* rlog = (PaxosLog*)new FilePaxosLog("filelog", FilePaxosLog::LTSync);
  test_paxoslog_append_and_truncate(rlog);
  delete rlog;
  deleteDir("filelog");
}

TEST(RDPaxosLog, append_and_truncate) {
  PaxosLog* rlog =
      (PaxosLog*)new RDPaxosLog("paxosLogTestDir", true, 4 * 1024 * 1024);
  test_paxoslog_append_and_truncate(rlog);
  delete rlog;
  deleteDir("paxosLogTestDir");
}

void test_paxoslog_append_entries(PaxosLog* rlog) {
  LogEntry entry;

  EXPECT_EQ(rlog->getLastLogIndex(), 0);

  // Patch entries append API
  ::google::protobuf::RepeatedPtrField<LogEntry> entries;
  entry.set_term(1);
  entry.set_index(1);
  entry.set_optype(1);
  entry.set_ikey("aa");
  entry.set_value("bb");
  *(entries.Add()) = entry;

  entry.set_term(2);
  entry.set_index(2);
  entry.set_optype(1);
  entry.set_ikey("bb");
  entry.set_value("cc");
  *(entries.Add()) = entry;

  EXPECT_EQ(2, rlog->append(entries));

  rlog->getEntry(1, entry);
  EXPECT_EQ(rlog->getLastLogIndex(), 2);
  EXPECT_EQ(entry.term(), 1);
  EXPECT_EQ(entry.index(), 1);
  EXPECT_EQ(entry.optype(), 1);
  EXPECT_EQ(entry.ikey(), "aa");
  EXPECT_EQ(entry.value(), "bb");
  rlog->getEntry(2, entry);
  EXPECT_EQ(rlog->getLastLogIndex(), 2);
  EXPECT_EQ(entry.term(), 2);
  EXPECT_EQ(entry.index(), 2);
  EXPECT_EQ(entry.optype(), 1);
  EXPECT_EQ(entry.ikey(), "bb");
  EXPECT_EQ(entry.value(), "cc");
}

TEST(FilePaxosLog, append_entries) {
  PaxosLog* rlog = (PaxosLog*)new FilePaxosLog("filelog", FilePaxosLog::LTSync);
  test_paxoslog_append_entries(rlog);
  delete rlog;
  deleteDir("filelog");
}

TEST(RDPaxosLog, append_entries) {
  PaxosLog* rlog =
      (PaxosLog*)new RDPaxosLog("paxosLogTestDir", true, 4 * 1024 * 1024);
  test_paxoslog_append_entries(rlog);
  delete rlog;
  deleteDir("paxosLogTestDir");
}
