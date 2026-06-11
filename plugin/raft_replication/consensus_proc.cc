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

#include "consensus_proc.h"
#include "consensus_common.h"
#include "consensus_log_manager.h"
#include "plugin_psi.h"
#include "rpl_consensus.h"
#include "system_variables.h"

#include "fail_point.h"
#include "my_sys.h"
#include "sql/log.h"
#include "mysql/components/services/log_builtins.h"
#include "sql/auth/auth_acls.h"
#include "sql/item.h"
#include "sql/protocol.h"

/**
  Consensus procedures (dbms_consensus package)

  TODO: use a specific PSI_memory_key
*/

using namespace im;

/* The uniform schema name for consensus package */
LEX_CSTRING CONSENSUS_PROC_SCHEMA = {C_STRING_WITH_LEN("dbms_consensus")};

bool Sql_cmd_consensus_proc::check_parameter() {
  if (Sql_cmd_proc::check_parameter()) return true;

  return false;
}

bool Sql_cmd_consensus_proc::check_access(THD *thd) {
  if (Sql_cmd_proc::check_access(thd)) return true;
  return false;
}

bool Sql_cmd_consensus_proc::check_addr_format(const char *node_addr) {
  if (!node_addr) return true;
  int a, b, c, d, p;
  if (std::sscanf(node_addr, "%d.%d.%d.%d:%d", &a, &b, &c, &d, &p) != 5)
    return true;
  if (a >= 0 && a <= 255 && b >= 0 && b <= 255 && c >= 0 && c <= 255 &&
      d >= 0 && d <= 255 && p >= 0 && p <= 65535)
    return false;
  else
    return true;
}

bool Sql_cmd_consensus_option_last_proc::check_parameter() {
  std::size_t actual_size = (m_list == nullptr ? 0 : m_list->size());
  std::size_t define_size = m_proc->get_parameters()->size();

  /* last param is option */
  if (actual_size != define_size && (actual_size + 1) != define_size) {
    my_error(ER_SP_WRONG_NO_OF_ARGS, MYF(0), "PROCEDURE",
             m_proc->qname().c_str(), define_size, actual_size);
    return true;
  }

  if (actual_size > 0) {
    std::size_t i = 0;
    for (Item *item : VisibleFields(*m_list)) {
      if (item->data_type() != m_proc->get_parameters()->at(i)) {
        my_error(ER_NATIVE_PROC_PARAMETER_MISMATCH, MYF(0), i + 1,
                 m_proc->qname().c_str());
        return true;
      }
      i++;
    }
  }
  return false;
}

/**
  dbms_consensus.change_leader(...)
*/
Proc *Consensus_proc_change_leader::instance() {
  static Proc *proc = new Consensus_proc_change_leader(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_change_leader::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_change_leader::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_string *ip_port = dynamic_cast<Item_string *>(*(it++));
  res =
      rpl_consensus_transfer_leader(std::string(ip_port->val_str(nullptr)->ptr()));
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_error(res));
  return (res != 0);
}

/**
  dbms_consensus.add_learner(...)
*/
Proc *Consensus_proc_add_learner::instance() {
  static Proc *proc = new Consensus_proc_add_learner(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_add_learner::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_add_learner::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_string *ip_port = dynamic_cast<Item_string *>(*(it++));
  std::vector<std::string> info_vector;
  info_vector.push_back(ip_port->val_str(nullptr)->ptr());
  res = rpl_consensus_add_learners(info_vector);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_error(res));
  return (res != 0);
}

bool Sql_cmd_consensus_proc_add_learner::prepare(THD *thd) {
  if (Sql_cmd_proc::prepare(thd)) return true;
  /* check max node number */
  if (rpl_consensus_get_cluster_size() > CONSENSUS_MAX_NODE_NUMBER) {
    my_error(ER_CONSENSUS_TOO_MANY_NODE, MYF(0));
    return true;
  }
  return false;
}

/**
  dbms_consensus.add_follower(...)
*/
Proc *Consensus_proc_add_follower::instance() {
  static Proc *proc = new Consensus_proc_add_follower(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_add_follower::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_add_follower::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_string *ip_port = dynamic_cast<Item_string *>(*(it++));
  std::string addr(ip_port->val_str(nullptr)->ptr());
  res = rpl_consensus_add_follower(addr);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_error(res));
  return (res != 0);
}

bool Sql_cmd_consensus_proc_add_follower::prepare(THD *thd) {
  if (Sql_cmd_proc::prepare(thd)) return true;
  /* check max node number */
  if (rpl_consensus_get_cluster_size() > CONSENSUS_MAX_NODE_NUMBER) {
    my_error(ER_CONSENSUS_TOO_MANY_NODE, MYF(0));
    return true;
  }
  return false;
}

/**
  dbms_consensus.drop_learner(...)
*/
Proc *Consensus_proc_drop_learner::instance() {
  static Proc *proc = new Consensus_proc_drop_learner(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_drop_learner::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_drop_learner::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_string *ip_port = dynamic_cast<Item_string *>(*(it++));
  std::vector<std::string> info_vector;
  info_vector.push_back(ip_port->val_str(nullptr)->ptr());
  res = rpl_consensus_drop_learners(info_vector);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_error(res));
  return (res != 0);
}

/**
  dbms_consensus.upgrade_learner(...)
*/
Proc *Consensus_proc_upgrade_learner::instance() {
  static Proc *proc = new Consensus_proc_upgrade_learner(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_upgrade_learner::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_upgrade_learner::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_string *ip_port = dynamic_cast<Item_string *>(*(it++));
  std::string addr(ip_port->val_str(nullptr)->ptr());
  res = rpl_consensus_upgrade_learner(addr);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_error(res));
  return (res != 0);
}

/**
  dbms_consensus.downgrade_follower(...)
*/
Proc *Consensus_proc_downgrade_follower::instance() {
  static Proc *proc = new Consensus_proc_downgrade_follower(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_downgrade_follower::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_downgrade_follower::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_string *ip_port = dynamic_cast<Item_string *>(*(it++));
  std::string addr(ip_port->val_str(nullptr)->ptr());
  res = rpl_consensus_downgrade_follower(addr);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_error(res));
  return (res != 0);
}

/**
  dbms_consensus.refresh_learner_meta()
*/
Proc *Consensus_proc_refresh_learner_meta::instance() {
  static Proc *proc =
      new Consensus_proc_refresh_learner_meta(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_refresh_learner_meta::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_refresh_learner_meta::pc_execute(THD *thd) {
  int res = 0;
  std::vector<std::string> info_vector;
  res = rpl_consensus_sync_all_learners(info_vector);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_error(res));
  return (res != 0);
}

/**
  dbms_consensus.configure_follower(...)
*/
Proc *Consensus_proc_configure_follower::instance() {
  static Proc *proc = new Consensus_proc_configure_follower(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_configure_follower::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_configure_follower::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_string *ip_port = dynamic_cast<Item_string *>(*(it++));
  std::string addr(ip_port->val_str(nullptr)->ptr());
  Item_int *w = dynamic_cast<Item_int *>(*(it++));

  if (!w) {
    my_error(ER_NATIVE_PROC_PARAMETER_MISMATCH, MYF(0), 2,
             m_proc->qname().c_str());
    return true;
  }

  uint election_weight = w->val_uint();
  /* force_sync is option, default 0 */
  bool force_sync = 0;
  if (m_list->size() == m_proc->get_parameters()->size()) {
    Item_int *fs = dynamic_cast<Item_int *>(*(it++));
    force_sync = fs ? fs->val_uint() : 0;
  }
  res = rpl_consensus_configure_member(addr, force_sync, election_weight);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res != 0 && res != 1)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_error(res));
  return (res != 0 && res != 1);
}

/**
  dbms_consensus.configure_learner(...)
*/
Proc *Consensus_proc_configure_learner::instance() {
  static Proc *proc = new Consensus_proc_configure_learner(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_configure_learner::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_configure_learner::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_string *ip_port = dynamic_cast<Item_string *>(*(it++));
  std::string addr(ip_port->val_str(nullptr)->ptr());
  Item_string *s = dynamic_cast<Item_string *>(*(it++));
  std::string source(s->val_str(nullptr)->ptr());
  /* use_applied is option, default 0 */
  bool use_applied = 0;
  if (m_list->size() == m_proc->get_parameters()->size()) {
    Item_int *ua = dynamic_cast<Item_int *>(*(it++));
    use_applied = ua ? ua->val_uint() : 0;
  }

  res = rpl_consensus_configure_learner(addr, source, use_applied);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res != 0 && res != 1)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_error(res));
  return (res != 0 && res != 1);
}

/**
  dbms_consensus.force_single_mode()
*/
Proc *Consensus_proc_force_single_mode::instance() {
  static Proc *proc = new Consensus_proc_force_single_mode(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_force_single_mode::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_force_single_mode::pc_execute(THD *thd) {
  int res = 0;
  res = rpl_consensus_force_single_leader();
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_error(res));
  return (res != 0);
}

/**
  dbms_consensus.force_promote()
*/
Proc *Consensus_proc_force_promote::instance() {
  static Proc *proc = new Consensus_proc_force_promote(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_force_promote::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_force_promote::pc_execute(THD *thd) {
  rpl_consensus_force_promote();
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, 0);
  return false;
}

/**
  dbms_consensus.fix_cluster_id(...)
*/
Proc *Consensus_proc_fix_cluster_id::instance() {
  static Proc *proc = new Consensus_proc_fix_cluster_id(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_fix_cluster_id::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_fix_cluster_id::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_string *ci = dynamic_cast<Item_string *>(*(it++));

  if (!ci) {
    my_error(ER_NATIVE_PROC_PARAMETER_MISMATCH, MYF(0), 1,
             m_proc->qname().c_str());
    return true;
  }

  std::string cluster_id(ci->val_str(nullptr)->ptr());
  res = rpl_consensus_set_cluster_id(cluster_id);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_error(res));
  return (res != 0);
}

/**
  dbms_consensus.fix_matchindex(...)
*/
Proc *Consensus_proc_fix_matchindex::instance() {
  static Proc *proc = new Consensus_proc_fix_matchindex(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_fix_matchindex::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_fix_matchindex::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_string *ip_port = dynamic_cast<Item_string *>(*(it++));
  std::string addr(ip_port->val_str(nullptr)->ptr());
  Item_int *mi = dynamic_cast<Item_int *>(*(it++));

  if (!mi) {
    my_error(ER_NATIVE_PROC_PARAMETER_MISMATCH, MYF(0), 2,
             m_proc->qname().c_str());
    return true;
  }

  uint64_t matchindex = mi->val_uint();
  rpl_consensus_force_fix_match_index(addr, matchindex);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_error(res));
  return (res != 0);
}

/**
  dbms_consensus.show_cluster_global()
*/
Proc *Consensus_proc_show_global::instance() {
  static Proc *proc = new Consensus_proc_show_global(key_memory_package);
  return proc;
}
Sql_cmd *Consensus_proc_show_global::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

void Sql_cmd_consensus_proc_show_global::send_result(THD *thd, bool error) {
  Protocol *protocol = thd->get_protocol();
  std::vector<Consensus_show_global_result *> results;
  if (error) {
    assert(thd->is_error());
    return;
  }

  // fetch the results
  collect_show_global_results(thd->mem_root, results);

  if (m_proc->send_result_metadata(thd)) return;

  for (auto it = results.cbegin(); it != results.cend(); *(it++)) {
    Consensus_show_global_result *result = *it;
    protocol->start_row();
    protocol->store(result->id);
    protocol->store_string(result->ip_port.str, result->ip_port.length,
                           system_charset_info);
    protocol->store(result->match_index);
    protocol->store(result->next_index);
    protocol->store_string(result->role.str, result->role.length,
                           system_charset_info);
    protocol->store_string(result->force_sync.str, result->force_sync.length,
                           system_charset_info);
    protocol->store(result->election_weight);
    protocol->store(result->learner_source);
    protocol->store(result->applied_index);
    protocol->store_string(result->pipelining.str, result->pipelining.length,
                           system_charset_info);
    protocol->store_string(result->send_applied.str,
                           result->send_applied.length, system_charset_info);
    if (protocol->end_row()) return;
  }

  my_eof(thd);
  return;
}

/**
  dbms_consensus.show_cluster_local()
*/
Proc *Consensus_proc_show_local::instance() {
  static Proc *proc = new Consensus_proc_show_local(key_memory_package);
  return proc;
}
Sql_cmd *Consensus_proc_show_local::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

void Sql_cmd_consensus_proc_show_local::send_result(THD *thd, bool error) {
  Protocol *protocol = thd->get_protocol();
  Consensus_show_local_result *result =
      new (thd->mem_root) Consensus_show_local_result();
  if (error) {
    assert(thd->is_error());
    return;
  }

  // fetch the results
  collect_show_local_results(thd->mem_root, result);

  if (m_proc->send_result_metadata(thd)) return;

  protocol->start_row();
  protocol->store(result->id);
  protocol->store(result->current_term);
  protocol->store_string(result->current_leader.str,
                         result->current_leader.length, system_charset_info);
  protocol->store(result->commit_index);
  protocol->store(result->last_log_term);
  protocol->store(result->last_log_index);
  protocol->store_string(result->role.str, result->role.length,
                         system_charset_info);
  protocol->store(result->vote_for);
  protocol->store(result->applied_index);
  protocol->store_string(result->server_ready_for_rw.str,
                         result->server_ready_for_rw.length,
                         system_charset_info);
  protocol->store_string(result->instance_type.str,
                         result->instance_type.length, system_charset_info);
  if (protocol->end_row()) return;

  my_eof(thd);
  return;
}

/**
  dbms_consensus.show_logs()
*/
Proc *Consensus_proc_show_logs::instance() {
  static Proc *proc = new Consensus_proc_show_logs(key_memory_package);
  return proc;
}
Sql_cmd *Consensus_proc_show_logs::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_show_logs::check_access(THD *thd) {
  Security_context *sctx = thd->security_context();

  if (!sctx->check_access(SUPER_ACL | REPL_CLIENT_ACL)) {
    my_error(ER_SPECIFIC_ACCESS_DENIED_ERROR, MYF(0), "SUPER or REPL_CLIENT");
    return true;
  }
  return false;
}

void Sql_cmd_consensus_proc_show_logs::send_result(THD *thd, bool error) {
  Protocol *protocol = thd->get_protocol();
  std::vector<Consensus_show_logs_result *> results;
  if (error) {
    assert(thd->is_error());
    return;
  }

  // fetch the results
  collect_show_logs_results(thd->mem_root, results);

  if (m_proc->send_result_metadata(thd)) return;

  for (auto it = results.cbegin(); it != results.cend(); *(it++)) {
    Consensus_show_logs_result *result = *it;
    protocol->start_row();
    protocol->store_string(result->log_name.str, result->log_name.length,
                           system_charset_info);
    protocol->store(result->file_size);
    protocol->store(result->start_log_index);
    if (protocol->end_row()) return;
  }

  my_eof(thd);
  return;
}

/**
  dbms_consensus.purge_log(...)
*/
Proc *Consensus_proc_purge_log::instance() {
  static Proc *proc = new Consensus_proc_purge_log(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_purge_log::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_purge_log::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_int *item = dynamic_cast<Item_int *>(*(it++));

  if (!item) {
    my_error(ER_NATIVE_PROC_PARAMETER_MISMATCH, MYF(0), 1,
             m_proc->qname().c_str());
    return true;
  }

  uint64 index = item->val_uint();
  res = rpl_consensus_force_purge_log(false /* local */, index);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_default_error());
  return (res != 0);
}

/**
  dbms_consensus.local_purge_log(...)
*/
Proc *Consensus_proc_local_purge_log::instance() {
  static Proc *proc = new Consensus_proc_local_purge_log(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_local_purge_log::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_local_purge_log::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_int *item = dynamic_cast<Item_int *>(*(it++));

  if (!item) {
    my_error(ER_NATIVE_PROC_PARAMETER_MISMATCH, MYF(0), 1,
             m_proc->qname().c_str());
    return true;
  }

  uint64 index = item->val_uint();
  res = rpl_consensus_force_purge_log(true /* local */, index);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_default_error());
  return (res != 0);
}

/**
  dbms_consensus.force_purge_log(...)
*/
Proc *Consensus_proc_force_purge_log::instance() {
  static Proc *proc = new Consensus_proc_force_purge_log(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_force_purge_log::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_force_purge_log::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_int *item = dynamic_cast<Item_int *>(*(it++));

  if (!item) {
    my_error(ER_NATIVE_PROC_PARAMETER_MISMATCH, MYF(0), 1,
             m_proc->qname().c_str());
    return true;
  }

  uint64 index = item->val_uint();
  res = consensus_log_manager.purge_log(index);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_default_error());
  return (res != 0);
}

/**
  dbms_consensus.drop_prefetch_channel(...)
*/
Proc *Consensus_proc_drop_prefetch_channel::instance() {
  static Proc *proc =
      new Consensus_proc_drop_prefetch_channel(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_drop_prefetch_channel::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_drop_prefetch_channel::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();
  Item_int *item = dynamic_cast<Item_int *>(*(it++));

  if (!item) {
    my_error(ER_NATIVE_PROC_PARAMETER_MISMATCH, MYF(0), 1,
             m_proc->qname().c_str());
    return true;
  }

  uint64 channel_id = item->val_uint();
  res = consensus_log_manager.get_prefetch_manager()->drop_prefetch_channel(
      channel_id);
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);
  if (res)
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_default_error());
  return (res != 0);
}

/**
  dbms_consensus.activate_failpoint(...)
*/
Proc *Consensus_proc_activate_failpoint::instance() {
  static Proc *proc = new Consensus_proc_activate_failpoint(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_activate_failpoint::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_activate_failpoint::pc_execute(THD *thd) {
  int res = 0;
  if (m_list->size() < 1) {
    my_error(ER_NATIVE_PROC_PARAMETER_MISMATCH, MYF(0), 1,
             m_proc->qname().c_str());
    return true;
  }
  auto it = VisibleFields(*m_list).begin();

  // CONSENSUS_PARAM_FAILPOINT_NAME
  Item_string *fail_point_name = dynamic_cast<Item_string *>(*(it++));
  if (!fail_point_name) {
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_default_error());
    return true;
  }
  auto fp = alisql::FailPointRegistry::getGlobalFailPointRegistry().find(
      std::string(fail_point_name->val_str(nullptr)->ptr()));
  if (!fp) {
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_default_error());
    return true;
  }

  // below are all optional parameters

  // CONSENSUS_PARAM_FAILPOINT_EXEC_TYPE
  int exec_type = 1;  // always on
  Item_int *item_exec_type = dynamic_cast<Item_int *>(*(it++));
  if (item_exec_type) exec_type = item_exec_type->val_int();
  if (exec_type > alisql::FailPoint::finiteSkip ||
      exec_type < alisql::FailPoint::off) {
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_default_error());
    return true;
  }
  alisql::FailPoint::FailPointType type =
      static_cast<alisql::FailPoint::FailPointType>(exec_type);

  // CONSENSUS_PARAM_FAILPOINT_EXEC_COUNT
  Item_int *item_exec_count = dynamic_cast<Item_int *>(*(it++));
  int exec_count_int = item_exec_count->val_int();
  if (exec_count_int < 0) {
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_default_error());
    return true;
  }
  uint32 exec_count = static_cast<uint32>(exec_count_int);

  // CONSENSUS_PARAM_INPUT_TYPE
  Item_int *item_input_type = dynamic_cast<Item_int *>(*(it++));
  int input_type = item_input_type->val_int();
  if (input_type < alisql::FailPointData::kInt ||
      input_type > alisql::FailPointData::kString) {
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_default_error());
    return true;
  }

  // CONSENSUS_PARAM_INPUT_VALUE
  Item_string *item_input_value = dynamic_cast<Item_string *>(*(it++));
  std::string input_value = std::string(item_input_value->val_str(nullptr)->ptr());
  alisql::FailPointData fail_point_data;
  switch (input_type) {
    case alisql::FailPointData::kInt:
      fail_point_data = alisql::FailPointData(std::stoi(input_value));
      break;
    case alisql::FailPointData::kUint64:
      fail_point_data = alisql::FailPointData(
          static_cast<uint64_t>(std::stoull(input_value)));
      break;
    case alisql::FailPointData::kDouble:
      fail_point_data = alisql::FailPointData(std::stod(input_value));
      break;
    case alisql::FailPointData::kString:
      fail_point_data = alisql::FailPointData(input_value);
      break;

    default:
      break;
  }

  // CONSENSUS_PARAM_PROBABILITY
  Item_decimal *item_probability = dynamic_cast<Item_decimal *>(*(it++));
  double probability = item_probability->val_real();
  if (probability < 0.0 || probability > 1.0) {
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_default_error());
    return true;
  }
  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_FAILPOINT_ACTIVE,
               fail_point_name->val_str(nullptr)->ptr(), type, exec_count,
               input_type, probability);

  fp->activate(type, exec_count, fail_point_data, probability);

  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);

  return false;
}

/**
  dbms_consensus.deactivate_failpoint(...)
*/
Proc *Consensus_proc_deactivate_failpoint::instance() {
  static Proc *proc =
      new Consensus_proc_deactivate_failpoint(key_memory_package);
  return proc;
}

Sql_cmd *Consensus_proc_deactivate_failpoint::evoke_cmd(
    THD *thd, mem_root_deque<Item *> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

bool Sql_cmd_consensus_proc_deactivate_failpoint::pc_execute(THD *thd) {
  int res = 0;
  auto it = VisibleFields(*m_list).begin();

  // CONSENSUS_PARAM_FAILPOINT_NAM
  Item_string *fail_point_name = dynamic_cast<Item_string *>(*(it++));
  auto fp = alisql::FailPointRegistry::getGlobalFailPointRegistry().find(
      std::string(fail_point_name->val_str(nullptr)->ptr()));
  if (!fp) {
    my_error(ER_CONSENSUS_COMMAND_ERROR, MYF(0), res,
             rpl_consensus_protocol_default_error());
    return true;
  }
  LogPluginErr(SYSTEM_LEVEL, ER_COSENNSUS_FAILPOINT_DEACTIVE,
               fail_point_name->val_str(nullptr)->ptr());

  fp->deactivate();

  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CMD_LOG,
         thd->m_main_security_ctx.user().str,
         thd->m_main_security_ctx.host_or_ip().str, thd->query().str, res);

  return false;
}
