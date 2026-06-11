// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include "file_paxos_log.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

const std::string logDBName = "@FilePaxosLog";
const std::string lastIndexTag = "@PAXOS_LOG_LEN@";

namespace alisql {

FilePaxosLog::FilePaxosLog(const std::string& dataDir, LogTypeT type)
    : length_(0),
      lastLogTerm_(0),
      lastLogIndex_(0),
      async_(false),
      type_(type),
      fd_(-1) {
  fd_ = open(dataDir.c_str(), O_CREAT | O_RDWR | O_APPEND, 0777);
  log_.reserve(1000);
  log_sz_.reserve(1000);

  if (type_ >= LTFile) {
    // it's better called in an standalone init() function, however since
    // FilePaxosLog is used only in test, instead throw an exception for
    // simplicity
    if (reload() < 0) {
      throw new std::ios_base::failure("FilePaxosLog::reload failure");
    }
  }

  if (length_ == 0) {
    appendEmptyEntry();
  }
}

FilePaxosLog::~FilePaxosLog() {
  if (fd_ != -1) {
    if (type_ >= LTFile) fdatasync(fd_);
    close(fd_);
  }

  for (auto le : log_) delete le;
}

int FilePaxosLog::getEmptyEntry(LogEntry& entry) {
  entry.set_term(0);
  entry.set_index(0);
  entry.set_optype(kNormal);
  entry.set_ikey("");
  entry.set_value("");

  return 0;
}

int FilePaxosLog::reload() {
  off_t offset = 0;

  while (true) {
    size_t sz = 0;
    ssize_t ret = read(fd_, &sz, sizeof(sz));
    if (ret < 0) {
      easy_error_log("read return error %s", strerror(errno));
      return -1;
    }
    if ((size_t)ret < sizeof(sz)) {
      // eof
      break;
    }
    if ((size_t)ret == sizeof(sz)) {
      std::string buf;
      buf.resize(sz);
      ret = read(fd_, (void*)buf.c_str(), sz);
      if (ret < 0) {
        easy_error_log("read return error %s", strerror(errno));
        return -1;
      }
      if ((size_t)ret == sz) {
        LogEntry* le = new LogEntry();
        decodeLogEntry(buf, le);
        log_.push_back(le);
        log_sz_.push_back(sz);

        lastLogIndex_ = le->index();
        lastLogTerm_ = le->term();
      } else {
        // eof
        break;
      }
    }
    offset += (sizeof(sz) + sz);
  }

  // truncate
  int ret = ftruncate(fd_, offset);
  if (ret < 0) {
    easy_error_log("ftruncate return error %s", strerror(errno));
    return -1;
  }

  // seek to the end
  ret = lseek(fd_, 0, SEEK_END);
  if (ret < 0) {
    easy_error_log("lseek return error %s", strerror(errno));
    return -1;
  }

  length_ = log_.size();
  return 0;
}

int FilePaxosLog::getEntry(uint64_t logIndex, LogEntry& entry, bool fastfail) {
  lock_.lock();
  bool ret = readEntry(logIndex, &entry);
  lock_.unlock();

  if (ret) {
    entry.set_index(logIndex);
    return 0;
  } else {
    return -1;
  }
}

const LogEntry* FilePaxosLog::getEntry(uint64_t logIndex, bool fastfail) {
  if (logIndex > length_ - 1) return NULL;

  lock_.lock();
  LogEntry* le = log_[logIndex];
  lock_.unlock();
  le->set_index(logIndex);

  return le;
}

uint64_t FilePaxosLog::append(const LogEntry& entry) {
  uint64_t index = appendEntry(entry);
  return async_ ? 0 : index;
}

uint64_t FilePaxosLog::appendWithCheck(const LogEntry& logEntry) {
  auto le = new LogEntry(logEntry);

  lock_.lock();
  if (currentTerm_ != le->term()) {
    lock_.unlock();
    return 0;
  }
  if (length_ + 1 >= log_.capacity()) log_.reserve(2 * log_.capacity());

  lastLogIndex_ = length_++;
  le->set_index(lastLogIndex_);
  lastLogTerm_ = le->term();

  appendLogEntry(le);
  lock_.unlock();

  if (type_ >= LTSync) {
    fdatasync(fd_);
  }

  return lastLogIndex_;
}

uint64_t FilePaxosLog::append(
    const ::google::protobuf::RepeatedPtrField<LogEntry>& entries) {
  uint64_t index, startIndex, len;
  std::string buf;

  startIndex = index = entries.begin()->index();
  len = entries.size();
  assert(index == length_);

  lock_.lock();
  if (length_ + len >= log_.capacity()) log_.reserve(2 * log_.capacity());
  for (auto it = entries.begin(); it != entries.end(); ++it) {
    assert(index == it->index());
    auto le = new LogEntry(*it);
    le->set_index(index);
    lastLogIndex_ = index;

    appendLogEntry(le);

    if (index == (startIndex + len - 1)) lastLogTerm_ = it->term();
    ++index;
  }
  length_ = index;
  lock_.unlock();

  if (type_ >= LTSync) fdatasync(fd_);

  assert((index - startIndex) == len);
  return index - 1;
}

void FilePaxosLog::encodeLogEntry(const LogEntry& logEntry, std::string* buf) {
  assert(buf);
  logEntry.SerializeToString(buf);
}

void FilePaxosLog::decodeLogEntry(const std::string& buf, LogEntry* logEntry) {
  assert(logEntry);
  logEntry->ParseFromString(buf);
}

uint64_t FilePaxosLog::appendEntry(const LogEntry& logEntry) {
  auto le = new LogEntry(logEntry);

  lock_.lock();
  if (length_ + 1 >= log_.capacity()) log_.reserve(2 * log_.capacity());
  lastLogIndex_ = length_++;
  le->set_index(lastLogIndex_);
  lastLogTerm_ = le->term();

  appendLogEntry(le);
  lock_.unlock();

  if (type_ >= LTSync) {
    fdatasync(fd_);
  }
  return lastLogIndex_;
}

void FilePaxosLog::appendLogEntry(LogEntry* le) {
  log_.push_back(le);

  std::string buf;
  encodeLogEntry(*le, &buf);
  log_sz_.push_back(buf.size());

  if (type_ >= LTFile) {
    size_t sz = buf.size();
    write(fd_, &sz, sizeof(sz));
    write(fd_, buf.c_str(), buf.size());
  }
}

uint64_t FilePaxosLog::getFirstLogIndex() {
  std::lock_guard<std::mutex> lg(lock_);
  if (length_ == 0) {
    return lastLogIndex_;
  }
  return log_[0]->index();
}

uint64_t FilePaxosLog::getLastLogIndex() {
  std::lock_guard<std::mutex> lg(lock_);
  return lastLogIndex_;
}

uint64_t FilePaxosLog::getLastLogTerm() {
  std::lock_guard<std::mutex> lg(lock_);
  return lastLogTerm_;
}

bool FilePaxosLog::readEntry(uint64_t index, LogEntry* logEntry) {
  if (index > length_ - 1) return false;

  logEntry->CopyFrom(*(log_[index]));
  return true;
}

uint64_t FilePaxosLog::getLength() {
  std::lock_guard<std::mutex> lg(lock_);
  return length_;
}

void FilePaxosLog::truncateBackward(uint64_t firstIndex) {
  if (firstIndex < 0) {
    firstIndex = 0;
  }

  {
    std::lock_guard<std::mutex> lg(lock_);
    if (firstIndex > lastLogIndex_) {
      return;
    }

    for (auto it = log_.begin() + firstIndex; it != log_.end(); ++it)
      delete *it;
    log_.erase(log_.begin() + firstIndex, log_.end());
    length_ = firstIndex;
    if (length_ > 0) {
      lastLogTerm_ = log_[length_ - 1]->term();
      lastLogIndex_ = firstIndex - 1;
    } else {
      // reset
      lastLogTerm_ = 0;
      lastLogIndex_ = 0;
    }

    off_t off = 0;
    log_sz_.erase(log_sz_.begin() + firstIndex, log_sz_.end());
    for (auto it = log_sz_.begin(); it != log_sz_.end(); ++it) {
      off += (*it + sizeof(size_t));
    }
    ftruncate(fd_, off);
    lseek(fd_, 0, SEEK_END);
    if (type_ >= LTSync) fdatasync(fd_);
  }
}

void FilePaxosLog::truncateForward(uint64_t lastIndex) {}

int FilePaxosLog::getMetaData(const std::string& key, uint64_t* value) {
  if (key == Paxos::keyCurrentTerm)
    *value = 1;
  else if (key == Paxos::keyVoteFor)
    *value = 0;
  else
    return -1;
  return 0;
}

int FilePaxosLog::setMetaData(const std::string& key, const uint64_t value) {
  return 0;
}

std::string FilePaxosLog::intToString(uint64_t num) {
  std::string key;
  key.resize(sizeof(uint64_t));
  memcpy(&key[0], &num, sizeof(uint64_t));
  return key;
}

void FilePaxosLog::intToString(uint64_t num, std::string& key) {
  key.resize(sizeof(uint64_t));
  memcpy(&key[0], &num, sizeof(uint64_t));
}

uint64_t FilePaxosLog::stringToInt(const std::string& s) {
  assert(s.size() == sizeof(uint64_t));
  uint64_t num = 0;
  memcpy(&num, &s[0], sizeof(uint64_t));
  return num;
}

bool FilePaxosLog::debugDisableWriteFile = false;

}  // namespace alisql
