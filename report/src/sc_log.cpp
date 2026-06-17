/*****************************************************************************

  Licensed to Accellera Systems Initiative Inc. (Accellera) under one or
  more contributor license agreements.  See the NOTICE file distributed
  with this work for additional information regarding copyright ownership.
  Accellera licenses this file to you under the Apache License, Version 2.0
  (the "License"); you may not use this file except in compliance with the
  License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
  implied.  See the License for the specific language governing
  permissions and limitations under the License.

*****************************************************************************/
/*****************************************************************************
  sc_report.cpp -- SC_LOG adaptation layer implementation.

  Layer 2: Provides the SC_LOG API on SystemC 3.0 (without native SC_LOG).
  When compiled against SystemC 4.0 (SC_HAS_SC_LOG already defined before our
  adaptation header), this file compiles to nothing.
*****************************************************************************/

#include <scp/sc_log.h>

/*
 * The adaptation header defines SC_HAS_SC_LOG itself when SystemC 3.0 is
 * detected.  We need to check a separate guard to know whether the
 * adaptation layer actually provided the types (vs SystemC 4.0 providing them).
 * We use the include guard _SC_LOG_ADAPTATION_H_ combined with the fact
 * that SystemC 4.0's header uses _SC_LOG_H_.  But simpler: check if we need
 * to provide the definitions by looking at whether the adaptation's own
 * types live here (they won't if SystemC 4.0 provided them through sc_log_types.h).
 *
 * The cleanest approach: guard on whether SystemC 4.0's own sc_log_types.h was
 * included. If it was, the types are already defined in the SystemC library.
 */
#ifndef _SC_LOG_TYPES_H_
/* Adaptation layer is active — provide implementations */

#include <map>
#include <string_view>

namespace sc_core {

static thread_local sc_log_logger_cache* s_current = nullptr;

sc_log_logger_cache* sc_log_logger_cache::get_current() { return s_current; }
void sc_log_logger_cache::set_current(sc_log_logger_cache* p) { s_current = p; }

/* get_log_verbosity_cached — delegates to sc_log_impl */
int sc_log_logger_cache::get_log_verbosity_cached(
    const char* file, int line, std::string_view local_tag)
{
    s_current = this;
    if (level != SC_UNSET) {
        return level;
    }
    return sc_log_impl::sc_get_log_verbosity(*this, file, line, local_tag);
}

} // namespace sc_core

/* Global default logger (in global namespace, same as SystemC 4.0's sc_log.cpp) */
sc_core::sc_log_logger_cache _m_sc_log_log_level_cache_{
    sc_core::SC_UNSET,
    {},
    {},
    nullptr
};

/*
 * sc_log_impl — adaptation version.
 * On SystemC 4.0 this lives in sc_simcontext and stores the callback in the
 * simcontext.  Here we use a file-scope static std::function instead.
 */
namespace {
std::function<sc_core::sc_verbosity(
    sc_core::sc_log_logger_cache&, const char*, int, std::string_view)>
    s_dynamic_log_verbosity;
} // anonymous namespace

void sc_core::sc_log_impl::sc_set_log_verbosity_fn(
    std::function<sc_verbosity(sc_log_logger_cache&, const char*, int,
                               std::string_view)> fn)
{
    s_dynamic_log_verbosity = std::move(fn);
}

sc_core::sc_verbosity sc_core::sc_log_impl::sc_get_log_verbosity(
    sc_log_logger_cache& logger, const char* file, int line,
    std::string_view local_tag)
{
    if (s_dynamic_log_verbosity)
        return s_dynamic_log_verbosity(logger, file, line, local_tag);
    return sc_core::as_log(sc_core::sc_report_handler::get_verbosity_level());
}

#endif /* !_SC_LOG_TYPES_H_ */
