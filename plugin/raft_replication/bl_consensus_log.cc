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

#include "bl_consensus_log.h"
#include "consensus_log_manager.h"
#include "consensus_meta.h"
#include "consensus_state_process.h"

BLConsensusLog::BLConsensusLog() {}
BLConsensusLog::~BLConsensusLog() {}

void BLConsensusLog::init(uint64 mock_start_index_arg,
                          ConsensusLogManager *consensus_log_manager_arg,
                          ConsensusMeta *consensus_meta_arg,
                          ConsensusStateProcess *consensus_state_process_arg) {
  mock_start_index = mock_start_index_arg;
  consensusLogManager_ = consensus_log_manager_arg;
  consensusMeta_ = consensus_meta_arg;
  consensusStateProcess_ = consensus_state_process_arg;
}

int BLConsensusLog::getEntry(uint64_t logIndex, alisql::LogEntry &entry,
                             bool fastFail, uint64_t serverId) {
  uint64 term = 0;
  std::string log_content;
  Consensus_Log_Op_Type optype = NORMAL;
  bool outerlog = false;
  uint flag = 0;
  uint64 checksum = 0;
  if (logIndex < mock_start_index) {
    term = 0;
    log_content = "";
    optype = MOCK;
  } else {
    if (this->consensusLogManager_->get_log_entry(serverId, logIndex, &term,
                                                  log_content, &outerlog, &flag,
                                                  &checksum, fastFail))
      return 1;
    if (outerlog == true)
      optype = CONFIGCHANGE;
    else {
      if (flag & Consensus_log_event_flag::FLAG_LARGE_TRX)
        optype = LARGETRX;
      else if (flag & Consensus_log_event_flag::FLAG_LARGE_TRX_END)
        optype = LARGETRXEND;
      else
        optype = NORMAL;
    }
  }
  entry.set_term(term);
  entry.set_index(logIndex);
  entry.set_optype(optype);
  entry.set_value(log_content);
  entry.set_info(flag);
  entry.set_checksum(checksum);
  return 0;
}

int BLConsensusLog::getEntry(uint64_t logIndex, alisql::LogEntry &entry,
                             bool fastFail) {
  // if not set serverid, use 0 default
  return getEntry(logIndex, entry, fastFail, 0);
}

uint64_t BLConsensusLog::getLeftSize(uint64_t startLogIndex) {
  return consensusLogManager_->get_left_log_size(startLogIndex, 0);
}

bool BLConsensusLog::getLeftSize(uint64_t startLogIndex,
                                 uint64_t maxPacketSize) {
  if (consensusLogManager_->get_left_log_size(startLogIndex, maxPacketSize) >
      maxPacketSize)
    return true;
  else
    return false;
}

int BLConsensusLog::getEmptyEntry(alisql::LogEntry &entry) {
  std::string log_content = consensusLogManager_->get_empty_log();

  int4store((uchar *)log_content.c_str(),
            static_cast<long>(my_micro_time() / 1000000));

  entry.set_value(log_content);
  entry.set_optype(NORMAL);
  entry.set_info(0);
  entry.set_checksum(0);
  return 0;
}

uint64_t BLConsensusLog::getLastLogIndex() {
  return (this->consensusLogManager_->get_sync_index());
}

uint64_t BLConsensusLog::getLastCachedLogIndex() {
  return (this->consensusLogManager_->get_cache_index());
}

uint64_t BLConsensusLog::appendWithCheck(const alisql::LogEntry &entry) {
  uint64 index = 0;
  if (entry.optype() == UNCERTAIN) {
    // optype UNCERTAIN means group commit replicate log send a fake log
    // do not need to write actually
    index = 0;
  } else {
    uint flag = entry.info();
    if (entry.optype() == CONFIGCHANGE)
      flag |= Consensus_log_event_flag::FLAG_CONFIG_CHANGE;
    ConsensusLogEntry log_entry = {
        entry.term(),
        entry.index(),
        entry.value().length(),
        reinterpret_cast<uchar *>(const_cast<char *>(entry.value().c_str())),
        entry.optype() == CONFIGCHANGE,
        flag,
        entry.checksum()};
    if (consensusLogManager_->write_log_entry(log_entry, &index,
                                              true /*check term*/)) {
      abort();
      index = 0;
    }
  }
  return index;
}

uint64_t BLConsensusLog::append(const alisql::LogEntry &entry) {
  uint64 index = 0;
  if (entry.optype() == UNCERTAIN) {
    // optype UNCERTAIN means group commit replicate log send a fake log
    // do not need to write actually
    index = 0;
  } else {
    uint flag = entry.info();
    if (entry.optype() == CONFIGCHANGE)
      flag |= Consensus_log_event_flag::FLAG_CONFIG_CHANGE;
    ConsensusLogEntry log_entry = {
        entry.term(),
        entry.index(),
        entry.value().length(),
        reinterpret_cast<uchar *>(const_cast<char *>(entry.value().c_str())),
        entry.optype() == CONFIGCHANGE,
        flag,
        entry.checksum()};
    if (consensusLogManager_->write_log_entry(log_entry, &index)) {
      index = 0;
    }
  }
  return index;
}

uint64_t BLConsensusLog::append(
    const ::google::protobuf::RepeatedPtrField<alisql::LogEntry> &entries) {
  std::vector<ConsensusLogEntry> log_vector;
  for (auto iter = entries.begin(); iter != entries.end(); iter++) {
    uint flag = iter->info();
    if (iter->optype() == CONFIGCHANGE)
      flag |= Consensus_log_event_flag::FLAG_CONFIG_CHANGE;
    ConsensusLogEntry log = {
        iter->term(),
        iter->index(),
        iter->value().length(),
        reinterpret_cast<uchar *>(const_cast<char *>(iter->value().c_str())),
        iter->optype() == CONFIGCHANGE,
        flag,
        iter->checksum()};
    log_vector.push_back(log);
  }
  uint64 max_index = 0;

  if (consensusLogManager_->write_log_entries(log_vector, &max_index)) {
    max_index = 0;
  }
  return max_index;
}

void BLConsensusLog::truncateBackward(uint64_t firstIndex) {
  consensusLogManager_->truncate_log(firstIndex);
}

void BLConsensusLog::truncateForward(uint64_t lastIndex) {
  consensusLogManager_->purge_log(lastIndex);
}

int BLConsensusLog::getMetaData(const std::string &key, uint64_t *value) {
  if (consensusMeta_->get_consensus_info()->init_info())
    return 1;
  if (key == "@keyVoteFor_@")
    *value = consensusMeta_->get_consensus_info()->get_vote_for();
  else if (key == "@keyCurrentTerm_@")
    *value = consensusMeta_->get_consensus_info()->get_current_term();
  else if (key == "@keyLastLeaderTerm_@")
    *value = consensusMeta_->get_consensus_info()->get_last_leader_term();
  else if (key == "@keyLastLeaderLogIndex_@")
    *value =
        consensusMeta_->get_consensus_info()->get_start_apply_index();
  else if (key == "@keyScanIndex_@")
    *value =
        consensusMeta_->get_consensus_info()->get_cluster_recover_index();
  else
    assert(0);
  return 0;
}

int BLConsensusLog::getMetaData(const std::string &key, std::string &value) {
  if (consensusMeta_->get_consensus_info()->init_info())
    return 1;
  else if (key == "@keyClusterId_@")
    value = consensusMeta_->get_consensus_info()->get_cluster_id();
  else
    assert(0);
  return 0;
}

int BLConsensusLog::setMetaData(const std::string &key, const uint64_t value) {
  if (key == "@keyVoteFor_@")
    consensusMeta_->get_consensus_info()->set_vote_for(value);
  else if (key == "@keyCurrentTerm_@")
    consensusMeta_->get_consensus_info()->set_current_term(value);
  else if (key == "@keyLastLeaderTerm_@")
    consensusMeta_->set_start_apply_term_if_need(value);
  else if (key == "@keyLastLeaderLogIndex_@")
    consensusMeta_->set_start_apply_index_if_need(value);
  else if (key == "@keyScanIndex_@")
    consensusMeta_->get_consensus_info()->set_cluster_recover_index(
        value);
  else
    assert(0);

  if (consensusMeta_->get_consensus_info()->flush_info(true, true)) {
    abort();
    return 1;
  }
  return 0;
}

int BLConsensusLog::setMetaData(const std::string &key,
                                const std::string &value) {
  if (key == "@keyClusterId_@")
    consensusMeta_->get_consensus_info()->set_cluster_id(value);
  else
    assert(0);

  if (consensusMeta_->get_consensus_info()->flush_info(true, true)) {
    abort();
    return 1;
  }
  return 0;
}

int BLConsensusLog::getMembersConfigure(std::string &strMembers,
                                        std::string &strLearners,
                                        uint64_t &index) {
  return consensusMeta_->get_cluster_info(strMembers, strLearners, index);
}

int BLConsensusLog::setMembersConfigure(bool setMembers,
                                        const std::string &strMembers,
                                        bool setLearners,
                                        const std::string &strLearners,
                                        const uint64_t index) {
  return consensusMeta_->set_cluster_info(setMembers, strMembers, setLearners,
                                          strLearners, true, index);
}

void BLConsensusLog::setTerm(uint64_t term) {
  mysql_mutex_lock(consensusStateProcess_->get_log_term_lock());
  this->currentTerm_ = term;
  mysql_mutex_unlock(consensusStateProcess_->get_log_term_lock());
}

uint64_t BLConsensusLog::getLength() {
  return consensusLogManager_->get_exist_log_length();
}

void BLConsensusLog::packLogEntry(uchar *buffer, size_t buf_size, uint64 term,
                                  uint64 index,
                                  Consensus_Log_Op_Type entry_type,
                                  alisql::LogEntry &log_entry) {
  std::string log_content((char *)buffer, buf_size);
  log_entry.set_value(log_content);
  log_entry.set_term(term);
  log_entry.set_index(index);
  log_entry.set_optype(entry_type);
}

bool BLConsensusLog::isStateMachineHealthy() {
  return consensusStateProcess_->is_state_machine_ready();
}