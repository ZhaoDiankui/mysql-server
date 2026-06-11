// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#ifndef cluster_file_paxos_log_INC
#define cluster_file_paxos_log_INC

#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include "paxos.h"
#include "paxos_log.h"

namespace alisql {

/**
 * @class FilePaxosLog
 *
 * @brief class for File and Map based Paxos Log
 *
 **/
class FilePaxosLog : public PaxosLog {
 public:
  typedef enum LogType {
    LTMem = 0,
    LTFile,
    LTSync,
  } LogTypeT;

  FilePaxosLog(const std::string& dataDir, LogTypeT type = LTMem);
  virtual ~FilePaxosLog();

  virtual int getEntry(uint64_t logIndex, LogEntry& entry,
                       bool fastfail = false);
  virtual const LogEntry* getEntry(uint64_t logIndex, bool fastfail = false);
  virtual int getEmptyEntry(LogEntry& entry);
  uint64_t getFirstLogIndex();
  virtual uint64_t getLastLogIndex();
  virtual uint64_t append(const LogEntry& entry);
  virtual uint64_t appendWithCheck(const LogEntry& entry);
  virtual void truncateBackward(uint64_t firstIndex);
  virtual void truncateForward(uint64_t lastIndex);
  virtual int getMetaData(const std::string& key, uint64_t* value);
  virtual int setMetaData(const std::string& key, const uint64_t value);
  virtual uint64_t append(
      const ::google::protobuf::RepeatedPtrField<LogEntry>& entries);
  virtual uint64_t getLastLogTerm();

  virtual uint64_t getLength();
  bool readEntry(uint64_t index, LogEntry* logEntry);
  uint64_t appendEntry(const LogEntry& logEntry);
  static uint64_t stringToInt(const std::string& s);
  static std::string intToString(uint64_t num);
  static void intToString(uint64_t num, std::string& key);
  void encodeLogEntry(const LogEntry& logEntry, std::string* buf);
  void decodeLogEntry(const std::string& buf, LogEntry* logEntry);

  void set_debug_async() { async_ = true; }

 private:
  int reload();
  void appendLogEntry(LogEntry* le);

  // std::atomic<uint64_t> length_;
  uint64_t length_;
  uint64_t lastLogTerm_;
  uint64_t lastLogIndex_;
  bool async_;
  LogTypeT type_;
  std::fstream fs_;
  int fd_;
  std::vector<LogEntry*> log_;
  std::vector<size_t> log_sz_;

 public:
  static bool debugDisableWriteFile;
}; /* end of class FilePaxosLog */

}; /* end of namespace alisql */

#endif  //#ifndef cluster_file_paxos_log_INC
