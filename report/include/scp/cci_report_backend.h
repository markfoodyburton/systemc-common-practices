/*******************************************************************************
 * Copyright 2016-2022 MINRES Technologies GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *******************************************************************************/

/*
 * CCI / SC_LOG integration layer.
 *
 * Provides the spdlog-based report handler and connects it to the CCI
 * configuration system for per-module verbosity control.  The same
 * definitions are used on both SystemC 3 (via the SC_LOG adaptation
 * layer) and SystemC 4 (native SC_LOG).
 *
 * Public API: scp::LogConfig (builder), scp::LogHandler (RAII owner),
 * and the CCI-based scp::get_log_verbosity() family.
 */

#ifndef _SCP_CCI_REPORT_BACKEND_H_
#define _SCP_CCI_REPORT_BACKEND_H_

#include <sysc/kernel/sc_module.h>
#include <sysc/kernel/sc_time.h>
#include <sysc/utils/sc_report.h>

#include <string>
#include <vector>

/* Include SC_LOG: native on SystemC 4, adaptation layer on SystemC 3 */
#include <scp/sc_log.h>

//! the name of the CCI property to attach to modules to control logging of
//! this module
#define SCP_LOG_LEVEL_PARAM_NAME "log_level"

//! separator for compound feature tags (e.g. "dmi,debug")
//! The CCI callback splits on this to check each feature individually.
#define SCP_TAG_SEP ','

/** \ingroup scp-report
 *  @{
 */
/**@{*/
//! @brief reporting utilities
namespace scp {

//! Log levels — alias for sc_core::sc_log_level.
//! Native names: CRITICAL, WARN, INFO, DEBUG, TRACE, UNSET.
//! Legacy SCP aliases (FATAL, ERROR, WARNING, TRACEALL, DBGTRACE) are
//! available via the SC_LOG adaptation layer for backward compatibility.
using log = sc_core::sc_log_level;

/**
 * @enum DisplayName
 * @brief Controls what is shown as the message type in log output.
 */
enum class DisplayName {
    AUTO,      //!< scname if available, else tag as provided by SC_LOG
    TAG,       //!< tag as provided by SC_LOG (GET_TAG: tag if set, else scname)
    SCNAME,    //!< always use module hierarchy name (scname)
    FEATURES,  //!< always use the feature/tag string from the logger
    FULL       //!< scname and features: "top.foo [dmi,debug]"
};

/**
 * @struct LogConfig
 * @brief the configuration class for the logging setup
 *
 * using this class allows to configure the logging output in many aspects. The
 * class follows the builder pattern.
 */
struct LogConfig {
    log level{ log::WARN };
    unsigned msg_type_field_width{ 24 };
    bool print_sys_time{ false };
    bool print_sim_time{ true };
    bool print_delta{ false };
    bool print_severity{ true };
    bool colored_output{ true };
    std::string log_file_name{ "" };
    std::string log_filter_regex{ "" };
    bool log_async{ true };
    bool report_only_first_error{ false };
    int file_info_from{ sc_core::SC_INFO };
    DisplayName display_name{ DisplayName::AUTO };

    //! set the logging level
    LogConfig& logLevel(log);
    //! define the width of the message field, 0 to disable,
    //! std::numeric_limits<unsigned>::max() for arbitrary width
    LogConfig& msgTypeFieldWidth(unsigned);
    //! enable/disable printing of system time
    LogConfig& printSysTime(bool = true);
    //! enable/disable printing of simulation time
    LogConfig& printSimTime(bool = true);
    //! enable/disable printing delta cycles
    LogConfig& printDelta(bool = true);
    //! enable/disable printing of severity level
    LogConfig& printSeverity(bool = true);
    //! enable/disable colored output
    LogConfig& coloredOutput(bool = true);
    //! set the file name for the log output file
    LogConfig& logFileName(std::string&&);
    //! set the file name for the log output file
    LogConfig& logFileName(const std::string&);
    //! set the regular expression to filter the output
    LogConfig& logFilterRegex(std::string&&);
    //! set the regular expression to filter the output
    LogConfig& logFilterRegex(const std::string&);
    //! enable/disable asynchronous output (write to file in separate thread
    LogConfig& logAsync(bool = true);
    //! disable the printing of the file name from this level upwards.
    LogConfig& fileInfoFrom(int);
    //! disable/enable the supression of all error messages after the first
    LogConfig& reportOnlyFirstError(bool = true);
    //! set what is shown as the message type field (AUTO, TAG, SCNAME, FEATURES)
    LogConfig& displayNameStyle(DisplayName);
};

/**
 * @fn void set_logging_level(log)
 * @brief sets the SystemC logging level
 *
 * @param level the logging level
 */
void set_logging_level(log level);
/**
 * @fn void set_display_name_style(DisplayName)
 * @brief change the display name style at runtime
 *
 * @param style the display name style
 */
void set_display_name_style(DisplayName style);
/**
 * @fn void reset_logging()
 * @brief invalidate all cached verbosity levels
 *
 * After calling this, every logger will re-query the CCI callback on
 * its next log statement.  Use this after changing CCI log_level
 * parameters at runtime to make the changes take effect immediately.
 */
void reset_logging();
/**
 * @fn void set_log_level(const std::string&, log)
 * @brief set the verbosity level for all loggers matching a name
 *
 * The name is matched against scname (module hierarchy) and tag features.
 * All matching logger caches are updated immediately — no reset needed.
 *
 * @param name the scname or feature to match (e.g. "top.prod", "dmi")
 * @param level the log level to set
 */
void set_log_level(const std::string& name, log level);
/**
 * @fn log get_logging_level()
 * @brief get the SystemC logging level
 *
 * @return the logging level
 */
log get_logging_level();
/**
 * @fn void set_cycle_base(sc_core::sc_time)
 * @brief sets the cycle base for cycle based logging
 *
 * if this is set to a non-SC_ZERO_TIME value all logging timestamps are
 * printed as cyles (multiple of this value)
 *
 * @param period the cycle period
 */
void set_cycle_base(sc_core::sc_time period);

/**
 * @class LogHandler
 * @brief RAII owner of the logging backend (spdlog + CCI integration)
 *
 * Constructs the spdlog loggers, installs the sc_report_handler callback,
 * and registers the CCI verbosity function.  On destruction, flushes all
 * pending log messages and shuts down the spdlog thread pool.
 *
 * Example usage:
 * @code
 * int sc_main(int argc, char* argv[]) {
 *     scp::LogHandler handler(scp::LogConfig()
 *         .logLevel(scp::log::DEBUG)
 *         .msgTypeFieldWidth(20));
 *     // ... simulation ...
 *     return 0;
 * }
 * @endcode
 */
class LogHandler {
public:
    /**
     * @brief Initialize logging with a LogConfig object
     * @param config the logging configuration
     */
    explicit LogHandler(LogConfig config = LogConfig{});
    /**
     * @brief Initialize logging with level and optional parameters
     * @param level the log level
     * @param type_field_width the width of the type field in the output
     * @param print_time whether to print the system time stamp
     */
    explicit LogHandler(log level, unsigned type_field_width = 24,
                       bool print_time = false);
    /**
     * @brief Destructor that ensures logging resources are cleaned up
     */
    ~LogHandler();

    LogHandler(const LogHandler&) = delete;
    LogHandler& operator=(const LogHandler&) = delete;
    LogHandler(LogHandler&&) = delete;
    LogHandler& operator=(LogHandler&&) = delete;
};

/**
 * @fn sc_core::sc_verbosity get_log_verbosity()
 * @brief get the global verbosity level
 *
 * @return the global verbosity level
 */
inline sc_core::sc_verbosity get_log_verbosity() {
    return static_cast<sc_core::sc_verbosity>(
        ::sc_core::sc_report_handler::get_verbosity_level());
}
/**
 * @fn sc_core::sc_verbosity get_log_verbosity(const char*)
 * @brief get the scope-based verbosity level
 *
 * The function returns a scope specific verbosity level if defined (e.g. by
 * using a CCI param named "log_level"). Otherwise the global verbosity level
 * is being returned
 *
 * @param t the SystemC hierarchy scope name
 * @return the verbosity level
 */
sc_core::sc_verbosity get_log_verbosity(char const* t);
/**
 * @fn sc_core::sc_verbosity get_log_verbosity(const std::string&)
 * @brief get the scope-based verbosity level
 *
 * @param t the SystemC hierarchy scope name
 * @return the verbosity level
 */
inline sc_core::sc_verbosity get_log_verbosity(std::string const& t) {
    return get_log_verbosity(t.c_str());
}

/**
 * @brief Return list of logging parameters that have been used
 */
std::vector<std::string> get_logging_parameters();

/**
 * @fn void set_logger_tag(sc_core::sc_log_logger_cache&, const std::string&)
 * @brief set the tag on a logger cache to a runtime-computed string
 *
 * The string is persisted internally so the string_view remains valid.
 * The cached level is reset so the CCI callback re-evaluates with the
 * new tag on the next log statement.
 *
 * @param logger the logger cache to update
 * @param tag the new tag string
 */
void set_logger_tag(sc_core::sc_log_logger_cache& logger, const std::string& tag);

} // namespace scp

//! Feature detection macro for set_logger_tag
#define SCP_HAS_SET_LOGGER_TAG
/** @} */ // end of scp-report

#endif /* _SCP_CCI_REPORT_BACKEND_H_ */
