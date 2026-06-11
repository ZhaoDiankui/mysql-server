/*
   Portions Copyright (c) 2024, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2018, 2021, Alibaba and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef BL_CONSENSUS_LOG_INCLUDE
#define BL_CONSENSUS_LOG_INCLUDE

#include <memory>

#include "my_inttypes.h"
#include "paxos_log.h"

class ConsensusLogManager;
class ConsensusMeta;
class ConsensusStateProcess;

class BLConsensusLog : public alisql::PaxosLog {
 public:
  enum Consensus_Log_Op_Type {
    NORMAL = 0,
    NOOP = 1,
    CONFIGCHANGE = 7,
    MOCK = 8,
    LARGETRX = 11,
    LARGETRXEND = 12,
    UNCERTAIN = 10
  };

  BLConsensusLog();
  virtual ~BLConsensusLog();
  void init(uint64 fake_start_index_arg,
            ConsensusLogManager *consensus_log_manager_arg,
            ConsensusMeta *consensus_meta_arg,
            ConsensusStateProcess *consensus_state_process_arg);

  virtual int getEntry(uint64_t logIndex, alisql::LogEntry &entry,
                       bool fastFail, uint64_t serverId) override;
  virtual int getEntry(uint64_t logIndex, alisql::LogEntry &entry,
                       bool fastFail) override;
  virtual const alisql::LogEntry *getEntry(
      uint64_t logIndex __attribute__((unused)),
      bool fastfail __attribute__((unused)) = false) override {
    return nullptr;
  }
  virtual uint64_t getLeftSize(uint64_t startLogIndex) override;
  virtual bool getLeftSize(uint64_t startLogIndexi,
                           uint64_t maxPacketSize) override;
  virtual int getEmptyEntry(alisql::LogEntry &entry) override;
  virtual uint64_t getLastLogIndex() override;
  virtual uint64_t getLastCachedLogIndex() override;
  // virtual uint64_t getSafeLastLogIndex() override;
  virtual uint64_t appendWithCheck(const alisql::LogEntry &entry) override;
  virtual uint64_t append(const alisql::LogEntry &entry) override;
  virtual uint64_t append(
      const ::google::protobuf::RepeatedPtrField<alisql::LogEntry> &entries)
      override;
  virtual void truncateBackward(uint64_t firstIndex) override;
  virtual void truncateForward(uint64_t lastIndex) override;
  virtual int getMetaData(const std::string &key, uint64_t *value) override;
  virtual int getMetaData(const std::string &key, std::string &value) override;
  virtual int setMetaData(const std::string &key,
                          const uint64_t value) override;
  virtual int setMetaData(const std::string &key,
                          const std::string &value) override;
  virtual int getMembersConfigure(std::string &strMembers,
                                  std::string &strLearners,
                                  uint64_t &index) override;
  virtual int setMembersConfigure(bool setMembers,
                                  const std::string& strMembers,
                                  bool setLearners,
                                  const std::string& strLearners,
                                  const uint64_t index) override;
  virtual void setTerm(uint64_t term) override;
  virtual uint64_t getLength() override;
  virtual bool isStateMachineHealthy() override;

  uint64 getCurrentTerm() { return currentTerm_; }
  static void packLogEntry(uchar *buffer, size_t buf_size, uint64 term,
                           uint64 index, Consensus_Log_Op_Type entry_type,
                           alisql::LogEntry &log_entry);

 private:
  uint64_t
      mock_start_index;  // before this index, all log entry should be mocked
  ConsensusLogManager *consensusLogManager_;  // ConsensusLog Operation detail
  ConsensusMeta *consensusMeta_;              // ConsensusMeta Operation detail
  ConsensusStateProcess *consensusStateProcess_;
};

#endif