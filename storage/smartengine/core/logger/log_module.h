/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Copyright (c) 2020, Alibaba Group Holding Limited
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "logger.h"

#define K(obj) #obj, obj
#define K_(obj) #obj, obj##_
#define KP(obj) #obj, (reinterpret_cast<const util::MyVoid*>(obj))
#define KP_(obj) #obj, reinterpret_cast<const util::MyVoid*>(obj##_)
#define KE(obj) #obj, ((uint32_t)(obj))
#define KE_(obj) #obj, ((uint32_t)(obj##_))

#define LOG ::smartengine::logger::Logger::get_log()
#define FILE_NAME __FILE__
#define FUNCTION_NAME __FUNCTION__
#define LINE_NUM __LINE__
#define LOG_LEVEL(level) ::smartengine::logger::level##_LEVEL
#define LOG_MOD(mod)	::smartengine::logger::InfoLogModule::mod##_MOD
#define LOG_MOD_SUBMOD(mod, submod) smartengine::logger::InfoLogModule::mod##_##submod##_MOD

//IF the logger with level is enabled
#define XLOG_MOD(mod, level) LOG.need_print_mod(LOG_MOD(mod), LOG_LEVEL(level))
#define XLOG_MOD_SUBMOD(mod, submod, level) LOG.need_print_mod(LOG_MOD_SUBMOD(mod, submod), LOG_LEVEL(level))

#define MOD_LOG(mod, level, info_string, ...) \
  ( XLOG_MOD(mod, level) \
  ? LOG.print_log_kv("["#mod"]", LOG_LEVEL(level), FILE_NAME, FUNCTION_NAME, LINE_NUM, info_string, ##__VA_ARGS__) : (void)(0))
#define __MOD_LOG(mod, level, fmt, ...) \
  ( XLOG_MOD(mod, level) \
  ? LOG.print_log_fmt("["#mod"]", LOG_LEVEL(level), FILE_NAME, FUNCTION_NAME, LINE_NUM, fmt, ##__VA_ARGS__) : (void)(0))
#define __MOD_LOG_OLD(mod, level, fmt, ap) \
  LOG.print_log_fmt("["#mod"]", level, FILE_NAME, FUNCTION_NAME, LINE_NUM, fmt, ap)
#define SUB_MOD_LOG(mod, submod, level, info_string, ...) \
  ( XLOG_MOD_SUBMOD(mod, submod, level) \
  ? LOG.print_log_kv("["#mod"."#submod"]", LOG_LEVEL(level), FILE_NAME, FUNCTION_NAME, LINE_NUM, info_string, ##__VA_ARGS__) : (void)(0))
#define __SUB_MOD_LOG(mod, submod, level, fmt, ...) \
  ( XLOG_MOD_SUBMOD(mod, submod, level) \
  ? LOG.print_log_fmt("["#mod"."#submod"]", LOG_LEVEL(level), FILE_NAME, FUNCTION_NAME, LINE_NUM, fmt, ##__VA_ARGS__) : (void)(0))

//smartengine mod and submod
#define SE_LOG(level, info_string, ...) MOD_LOG(SE, level, info_string, ##__VA_ARGS__)
#define __SE_LOG(level, fmt, ...) __MOD_LOG(SE, level, fmt, ##__VA_ARGS__)

#define HANDLER_LOG(level, info_string, ...) SUB_MOD_LOG(SE, HANDLER, level, info_string, ##__VA_ARGS__)
#define __HANDLER_LOG(level, fmt, ...) __SUB_MOD_LOG(SE, HANDLER, level, fmt, ##__VA_ARGS__)

#define COMMON_LOG(level, info_string, ...) MOD_LOG(COMMON, level, info_string, ##__VA_ARGS__)
#define __COMMON_LOG(level, fmt, ap) __MOD_LOG(COMMON, level, fmt, ap)

#define COMPACTION_LOG(level, info_string, ...) SUB_MOD_LOG(SE, COMPACTION, level, info_string, ##__VA_ARGS__)
#define __COMPACTION_LOG(level, fmt, ...) __SUB_MOD_LOG(SE, COMPACTION, level, fmt, ##__VA_ARGS__)

#define FLUSH_LOG(level, info_string, ...) SUB_MOD_LOG(SE, FLUSH, level, info_string, ##__VA_ARGS__)
#define __FLUSH_LOG(level, fmt, ...) __SUB_MOD_LOG(SE, FLUSH, level, fmt, ##__VA_ARGS__)