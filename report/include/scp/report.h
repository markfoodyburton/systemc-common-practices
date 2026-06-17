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

/*
 * Backward-compatibility header.
 *
 * New code should include:
 *   <scp/scp_log.h>              for SCP macros (SCP_LOGGER, SCP_INFO, etc.)
 *   <scp/cci_report_backend.h>   for backend init (scp::LogConfig, scp::LogHandler)
 *   <scp/sc_log.h>               for portable SC_LOG API
 */

#ifndef _SCP_REPORT_H_
#define _SCP_REPORT_H_

#pragma message("DEPRECATED: <scp/report.h> — use <scp/scp_log.h> and/or <scp/cci_report_backend.h> instead")

#include <scp/cci_report_backend.h>
#include <scp/scp_log.h>

namespace scp {

/* Legacy alias — use LogHandler directly in new code */
class [[deprecated("Use scp::LogHandler instead of scp::LoggingGuard")]] LoggingGuard : public LogHandler {
public:
    using LogHandler::LogHandler;
};

/* Legacy free functions — use LogHandler directly in new code.
 * init_logging creates a static LogHandler that lives until program exit. */
inline void init_logging(sc_core::sc_verbosity level = log::WARN, unsigned type_field_width = 24,
                         bool print_time = false) {
    static LogHandler handler(level, type_field_width, print_time);
}
inline void init_logging(const LogConfig& config) {
    static LogHandler handler(config);
}
inline void shutdown_logging() {
    /* No-op — static LogHandler destructor handles cleanup at exit */
}

} // namespace scp

#endif /* _SCP_REPORT_H_ */
