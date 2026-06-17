/*******************************************************************************
 * Copyright 2017-2022 MINRES Technologies GmbH
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
 * report.cpp
 *
 *  Created on: 19.09.2017
 *      Author: eyck@minres.com
 */

#include <scp/cci_report_backend.h>
#include <array>
#include <chrono>
#include <fstream>
#include <numeric>
#include <sstream>
#include <systemc>
#ifdef HAS_CCI
#include <cci_configuration>
#endif
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <tuple>
#if defined(__GNUC__) || defined(__clang__)
#define likely(x)   __builtin_expect(x, 1)
#define unlikely(x) __builtin_expect(x, 0)
#endif
#ifdef __GNUG__
#include <cstdlib>
#include <memory>
#include <cxxabi.h>
#endif
#if !defined(likely)
#define likely(x)   x
#define unlikely(x) x
#endif

#include <regex>
#ifdef ERROR
#undef ERROR
#endif

namespace {

#ifdef HAS_CCI
cci::cci_originator scp_global_originator("scp_reporting_global");
#endif

std::set<std::string> logging_parameters;
std::multimap<std::string, sc_core::sc_log_logger_cache*> logger_caches;
std::mutex logger_caches_mutex;
std::unordered_map<std::string, sc_core::sc_verbosity> tag_lut;
std::shared_mutex tag_lut_mutex;

struct ExtLogConfig : public scp::LogConfig {
    std::shared_ptr<spdlog::logger> file_logger;
    std::shared_ptr<spdlog::logger> console_logger;
    std::regex reg_ex;
    sc_core::sc_time cycle_base{ 0, sc_core::SC_NS };
    auto operator=(const scp::LogConfig& o) -> ExtLogConfig& {
        scp::LogConfig::operator=(o);
        return *this;
    }
    auto match(const char* type) -> bool { return regex_search(type, reg_ex); }
};

#ifdef DISABLE_REPORT_THREAD_LOCAL
ExtLogConfig log_cfg;
#else
thread_local ExtLogConfig log_cfg;
#endif

inline std::string padded(std::string str, size_t width,
                          bool show_ellipsis = true) {
    if (width < 7)
        return str;
    if (str.length() > width) {
        if (show_ellipsis) {
            auto pos = str.size() - (width - 6);
            return str.substr(0, 3) + "..." +
                   str.substr(pos, str.size() - pos);
        } else
            return str.substr(0, width);
    } else {
        return str + std::string(width - str.size(), ' ');
    }
}

auto get_tuple(const sc_core::sc_time& t)
    -> std::tuple<sc_core::sc_time::value_type, sc_core::sc_time_unit> {
    auto val = t.value();
    auto tr = (uint64_t)(sc_core::sc_time::from_value(1).to_seconds() * 1E15);
    auto scale = 0U;
    while ((tr % 10) == 0) {
        tr /= 10;
        scale++;
    }
    sc_assert(tr == 1);

    auto tu = scale / 3;
    while (tu < sc_core::SC_SEC && (val % 10) == 0) {
        val /= 10;
        scale++;
        tu += (0 == (scale % 3));
    }
    for (scale %= 3; scale != 0; scale--)
        val *= 10;
    return std::make_tuple(val, static_cast<sc_core::sc_time_unit>(tu));
}

auto time2string(const sc_core::sc_time& t) -> std::string {
    const std::array<const char*, 6> time_units{ "fs", "ps", "ns",
                                                 "us", "ms", "s " };
    const std::array<uint64_t, 6> multiplier{ 1ULL,
                                              1000ULL,
                                              1000ULL * 1000,
                                              1000ULL * 1000 * 1000,
                                              1000ULL * 1000 * 1000 * 1000,
                                              1000ULL * 1000 * 1000 * 1000 *
                                                  1000 };
    std::ostringstream oss;
    if (!t.value()) {
        oss << "0 s ";
    } else {
        const auto tt = get_tuple(t);
        const auto val = std::get<0>(tt);
        const auto scale = std::get<1>(tt);
        const auto fs_val = val * multiplier[scale];
        for (int j = multiplier.size() - 1; j >= scale; --j) {
            if (fs_val >= multiplier[j]) {
                const auto i = val / multiplier[j - scale];
                const auto f = val % multiplier[j - scale];
                oss << i << '.' << std::setw(3 * (j - scale))
                    << std::setfill('0') << std::right << f << ' '
                    << time_units[j];
                break;
            }
        }
    }
    return oss.str();
}
/* Get the display name for a log message based on the DisplayName setting. */
inline const char* get_display_name(const sc_core::sc_report& rep) {
    auto* lc = sc_core::sc_log_logger_cache::get_current();
    switch (log_cfg.display_name) {
    case scp::DisplayName::TAG:
        return rep.get_msg_type();
    case scp::DisplayName::SCNAME:
        if (lc && !lc->scname.empty())
            return lc->scname.data();
        return rep.get_msg_type();
    case scp::DisplayName::FEATURES:
        if (lc && !lc->tag.empty())
            return lc->tag.data();
        return rep.get_msg_type();
    case scp::DisplayName::FULL: {
        if (lc && (!lc->scname.empty() || !lc->tag.empty())) {
            static thread_local std::string full_name;
            full_name.clear();
            if (!lc->tag.empty())
                full_name = std::string(lc->tag);
            if (!lc->scname.empty()) {
                if (!full_name.empty())
                    full_name += " [";
                full_name += std::string(lc->scname);
                if (!lc->tag.empty())
                    full_name += "]";
            }
            return full_name.c_str();
        }
        return rep.get_msg_type();
    }
    case scp::DisplayName::AUTO:
    default:
        /* Prefer tag (explicit logger identity) when available.
         * Fall back to scname (module hierarchy) when the tag is empty
         * and the msg_type matches scname (i.e. came from GET_TAG).
         * Leave msg_type as-is for explicit overrides like
         * SC_REPORT_INFO("ext test", ..) or SC_WARN(handle, "My.Name"). */
        if (lc) {
            if (!lc->tag.empty())
                return lc->tag.data();
            if (!lc->scname.empty() && lc->scname == rep.get_msg_type())
                return lc->scname.data();
        }
        return rep.get_msg_type();
    }
}

/* Label for a report's level.  `full` selects the full level name (used by the
 * file logger) over the single letter (used by the console).  sc_log messages
 * arrive as severity SC_INFO with the level carried in the verbosity; real
 * SystemC reports use their severity. */
inline std::string scp_level_label(const sc_core::sc_report& rep, bool full) {
    std::string name;
    switch (rep.get_severity()) {
    case sc_core::SC_WARNING: name = "WARNING"; break;
    case sc_core::SC_ERROR:   name = "ERROR";   break;
    case sc_core::SC_FATAL:   name = "FATAL";   break;
    default: { // SC_INFO -> SC_LOG level carried in the verbosity; name it via
               // the shared level<->name map (bucketing any int to a level)
        int v = rep.get_verbosity();
        if (v > sc_core::SC_NONE && v < sc_core::SC_LOW) v *= 10;
        name = scp::level_name(sc_core::as_log(v));
        break;
    }
    }
    // the single-letter form is just the first character of the name
    return full ? name : name.substr(0, 1);
}

auto compose_message(const sc_core::sc_report& rep, const scp::LogConfig& cfg,
                     bool full_label = false)
    -> const std::string {
    if (rep.get_severity() > sc_core::SC_INFO ||
        cfg.log_filter_regex.length() == 0 ||
        rep.get_verbosity() == sc_core::SC_MEDIUM ||
        log_cfg.match(rep.get_msg_type())) {
        std::stringstream os;
        if (cfg.print_severity)
            os << "[" << scp_level_label(rep, full_label) << "] ";
        if (likely(cfg.print_sim_time)) {
            if (unlikely(log_cfg.cycle_base.value())) {
                if (unlikely(cfg.print_delta))
                    os << "[" << std::setw(7) << std::setfill(' ')
                       << sc_core::sc_time_stamp().value() /
                              log_cfg.cycle_base.value()
                       << "(" << std::setw(5) << sc_core::sc_delta_count()
                       << ")]";
                else
                    os << "[" << std::setw(7) << std::setfill(' ')
                       << sc_core::sc_time_stamp().value() /
                              log_cfg.cycle_base.value()
                       << "]";
            } else {
                auto t = time2string(sc_core::sc_time_stamp());
                if (unlikely(cfg.print_delta))
                    os << "[" << std::setw(20) << std::setfill(' ') << t << "("
                       << std::setw(5) << sc_core::sc_delta_count() << ")]";
                else
                    os << "[" << std::setw(20) << std::setfill(' ') << t
                       << "]";
            }
        }
        if (unlikely(rep.get_id() >= 0))
            os << "("
               << "IWEF"[rep.get_severity()] << rep.get_id() << ") "
               << get_display_name(rep) << ": ";
        else if (cfg.msg_type_field_width) {
            if (cfg.msg_type_field_width ==
                std::numeric_limits<unsigned>::max())
                os << get_display_name(rep) << ": ";
            else
                os << padded(get_display_name(rep), cfg.msg_type_field_width)
                   << ": ";
        }
        if (*rep.get_msg())
            os << rep.get_msg();
        if (rep.get_severity() >= cfg.file_info_from) {
            if (rep.get_line_number())
                os << "\n         [FILE:" << rep.get_file_name() << ":"
                   << rep.get_line_number() << "]";
            sc_core::sc_simcontext* simc = sc_core::sc_get_curr_simcontext();
            if (simc && sc_core::sc_is_running()) {
                const char* proc_name = rep.get_process_name();
                if (proc_name)
                    os << "\n         [PROCESS:" << proc_name << "]";
            }
        }
        return os.str();
    } else
        return "";
}

inline auto get_verbosity(const sc_core::sc_report& rep) -> int {
    return rep.get_verbosity() > sc_core::SC_NONE &&
                   rep.get_verbosity() < sc_core::SC_LOW
               ? rep.get_verbosity() * 10
               : rep.get_verbosity();
}

inline void log2logger(spdlog::logger& logger, const sc_core::sc_report& rep,
                       const scp::LogConfig& cfg, bool full_label = false) {
    auto msg = compose_message(rep, cfg, full_label);
    if (!msg.size())
        return;
    switch (rep.get_severity()) {
    case sc_core::SC_INFO:
        /* Map the SC_LOG level (carried in the verbosity) to an spdlog level
         * for threshold filtering / flush / colour.  Kept consistent with the
         * set_logging_level() threshold switch: more verbose -> lower spdlog
         * level.  The printed label comes from compose_message, not from here. */
        switch (get_verbosity(rep)) {
        case sc_core::SC_DEBUG:   // INTERNAL
            logger.trace(msg);
            break;
        case sc_core::SC_FULL:    // DETAIL
            logger.debug(msg);
            break;
        case sc_core::SC_HIGH:    // NOTE
            logger.info(msg);
            break;
        case sc_core::SC_MEDIUM:  // ALERT
            logger.warn(msg);
            break;
        case sc_core::SC_LOW:     // CRITICAL
            logger.critical(msg);
            break;
        default:
            logger.trace(msg);
            break;
        }
        break;
    case sc_core::SC_WARNING:
        logger.warn(msg);
        break;
    case sc_core::SC_ERROR:
        logger.error(msg);
        break;
    case sc_core::SC_FATAL:
        logger.critical(msg);
        break;
    default:
        break;
    }
}

void report_handler(const sc_core::sc_report& rep,
                    const sc_core::sc_actions& actions) {
    thread_local bool sc_stop_called = false;
    if (actions & sc_core::SC_DO_NOTHING)
        return;
    /* Log the message if the logging backend is active */
    if (log_cfg.console_logger) {
        if (rep.get_severity() == sc_core::SC_INFO ||
            !log_cfg.report_only_first_error ||
            sc_core::sc_report_handler::get_count(sc_core::SC_ERROR) < 2) {
            if ((actions & sc_core::SC_DISPLAY) &&
                (!log_cfg.file_logger ||
                 get_verbosity(rep) < sc_core::SC_HIGH))
                log2logger(*log_cfg.console_logger, rep, log_cfg);
            if ((actions & sc_core::SC_LOG) && log_cfg.file_logger) {
                scp::LogConfig lcfg(log_cfg);
                lcfg.print_sim_time = true;
                if (!lcfg.msg_type_field_width)
                    lcfg.msg_type_field_width = 24;
                /* file logger uses the full level name; console uses the letter */
                log2logger(*log_cfg.file_logger, rep, lcfg, /*full_label=*/true);
            }
        }
    }
    /* Always handle actions regardless of logging state for the normal sc_report_ path */
    if (actions & sc_core::SC_STOP) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<unsigned>(log_cfg.level)));
        if (sc_core::sc_is_running() && !sc_stop_called) {
            sc_core::sc_stop();
            sc_stop_called = true;
        }
    }
    if (actions & sc_core::SC_ABORT) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<unsigned>(log_cfg.level)));
        abort();
    }
    if (actions & sc_core::SC_THROW) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<unsigned>(log_cfg.level)));
        throw rep;
    }
    if (sc_core::sc_time_stamp().value() && !sc_core::sc_is_running()) {
        log_cfg.console_logger->flush();
        if (log_cfg.file_logger)
            log_cfg.file_logger->flush();
    }
}

} // namespace

/* Convert CCI integer or string values to sc_verbosity.
 *   0-99:  small int (0=off, 1-3=CRITICAL, 4=ALERT, 5=NOTE, 6+=INTERNAL)
 *   >=100: sc_verbosity value used directly (100=CRITICAL .. 500=INTERNAL)
 *   string: resolved by scp::as_log (canonical + legacy spellings) */
static sc_core::sc_verbosity cci_to_verbosity(int v) {
    if (v < 100) {
        if (v <= 0) return sc_core::SC_NONE;
        if (v <= 3) return sc_core::SC_LOW;
        if (v == 4) return sc_core::SC_MEDIUM;
        if (v == 5) return sc_core::SC_HIGH;
        return sc_core::SC_DEBUG;
    }
    return static_cast<sc_core::sc_verbosity>(v);
}

static sc_core::sc_verbosity cci_to_verbosity(const std::string& name) {
    /* Level-name parsing (canonical + legacy spellings) is an SCP-layer
     * convenience; upstream SystemC core ships no level<->text mapping. */
    return scp::as_log(name);
}

#ifdef HAS_CCI
static sc_core::sc_verbosity scp_cci_log_verbosity(
    sc_core::sc_log_logger_cache& logger, const char* file, int line,
    std::string_view local_tag);
#endif

static std::mutex cfg_guard;
static std::thread::id sysc_thread_id;

static void configure_logging() {
    std::lock_guard<std::mutex> lock(cfg_guard);
    static bool spdlog_initialized = false;
    sysc_thread_id = std::this_thread::get_id();

    sc_core::sc_report_handler::set_actions(
        sc_core::SC_ERROR,
        sc_core::SC_DEFAULT_ERROR_ACTIONS | sc_core::SC_DISPLAY);
    sc_core::sc_report_handler::set_actions(sc_core::SC_FATAL,
                                            sc_core::SC_DEFAULT_FATAL_ACTIONS);
    sc_core::sc_report_handler::set_verbosity_level(
        static_cast<sc_core::sc_verbosity>(log_cfg.level));
    sc_core::sc_report_handler::set_handler(report_handler);
#ifdef HAS_CCI
    {
        static bool verbosity_fn_set = false;
        if (!verbosity_fn_set) {
            sc_core::sc_log_impl::sc_set_log_verbosity_fn(
                scp_cci_log_verbosity);
            verbosity_fn_set = true;
        }
    }
#endif
    if (!spdlog_initialized) {
        spdlog::init_thread_pool(
            1024U,
            log_cfg.log_file_name.size()
                ? 2U
                : 1U); // queue with 8k items and 1 backing thread.
        log_cfg.console_logger = log_cfg.log_async
                                     ? spdlog::stdout_color_mt<
                                           spdlog::async_factory>(
                                           "console_logger")
                                     : spdlog::stdout_color_mt(
                                           "console_logger");
        /* The level label (e.g. "[C] ") is prepended in compose_message, so
         * the spdlog pattern only needs the message body. */
        const char* logger_fmt = "%v";
        if (log_cfg.colored_output) {
            std::ostringstream os;
            os << "%^" << logger_fmt << "%$";
            log_cfg.console_logger->set_pattern(os.str());
        } else
            log_cfg.console_logger->set_pattern("%v");
        log_cfg.console_logger->flush_on(spdlog::level::warn);
        log_cfg.console_logger->set_level(spdlog::level::level_enum::trace);
        if (log_cfg.log_file_name.size()) {
            {
                std::ofstream ofs;
                ofs.open(log_cfg.log_file_name,
                         std::ios::out | std::ios::trunc);
            }
            log_cfg.file_logger = log_cfg.log_async
                                      ? spdlog::basic_logger_mt<
                                            spdlog::async_factory>(
                                            "file_logger",
                                            log_cfg.log_file_name)
                                      : spdlog::basic_logger_mt(
                                            "file_logger",
                                            log_cfg.log_file_name);
            /* Level label is embedded in the message by compose_message. */
            log_cfg.file_logger->set_pattern("%v");
            log_cfg.file_logger->flush_on(spdlog::level::warn);
            log_cfg.file_logger->set_level(spdlog::level::level_enum::trace);
        }
        spdlog_initialized = true;
    } else {
        log_cfg.console_logger = spdlog::get("console_logger");
        if (log_cfg.log_file_name.size())
            log_cfg.file_logger = spdlog::get("file_logger");
    }
    if (log_cfg.log_filter_regex.size()) {
        log_cfg.reg_ex = std::regex(log_cfg.log_filter_regex,
                                    std::regex::extended | std::regex::icase);
    }
}

/* LogHandler — RAII owner of logging lifecycle */

scp::LogHandler::LogHandler(scp::LogConfig config) {
    log_cfg = config;
    configure_logging();
}

scp::LogHandler::LogHandler(sc_core::sc_verbosity level, unsigned type_field_width,
                            bool print_time):
    LogHandler(LogConfig{}
                   .logLevel(level)
                   .msgTypeFieldWidth(type_field_width)
                   .printSysTime(print_time)) {
}

scp::LogHandler::~LogHandler() {
    if (log_cfg.console_logger) {
        log_cfg.console_logger->flush();
    }
    if (log_cfg.file_logger) {
        log_cfg.file_logger->flush();
    }

    log_cfg.console_logger.reset();
    log_cfg.file_logger.reset();

    spdlog::drop_all();
    spdlog::shutdown();
}

void scp::set_logging_level(sc_core::sc_verbosity level) {
    log_cfg.level = level;
    sc_core::sc_report_handler::set_verbosity_level(
        static_cast<sc_core::sc_verbosity>(level));
    spdlog::level::level_enum spdlvl;
    switch (level) {
    case sc_core::SC_LOW:     // CRITICAL
        spdlvl = spdlog::level::critical;
        break;
    case sc_core::SC_MEDIUM:  // ALERT
        spdlvl = spdlog::level::warn;
        break;
    case sc_core::SC_HIGH:    // NOTE
        spdlvl = spdlog::level::info;
        break;
    case sc_core::SC_FULL:    // DETAIL
        spdlvl = spdlog::level::debug;
        break;
    case sc_core::SC_DEBUG:   // INTERNAL
        spdlvl = spdlog::level::trace;
        break;
    default:
        spdlvl = spdlog::level::trace;
        break;
    }
    log_cfg.console_logger->set_level(spdlvl);
}

auto scp::get_logging_level() -> sc_core::sc_verbosity {
    return log_cfg.level;
}

void scp::set_cycle_base(sc_core::sc_time period) {
    log_cfg.cycle_base = period;
}

void scp::set_display_name_style(scp::DisplayName style) {
    log_cfg.display_name = style;
}

void scp::reset_logging() {
    std::lock_guard<std::mutex> lock(logger_caches_mutex);
    { std::unique_lock<std::shared_mutex> lk(tag_lut_mutex); tag_lut.clear(); }
    auto range = logger_caches.equal_range("");
    for (auto it = range.first; it != range.second; ++it) {
        it->second->level = sc_core::SC_UNSET;
    }
}

void scp::set_log_level(const std::string& name, sc_core::sc_verbosity level) {
    std::lock_guard<std::mutex> lock(logger_caches_mutex);
    auto range = logger_caches.equal_range(name);
    for (auto it = range.first; it != range.second; ++it) {
        it->second->level = level;
    }
}

void scp::set_logger_tag(sc_core::sc_log_logger_cache& logger,
                         const std::string& tag) {
    logger.set_tag(tag);
    {
        std::lock_guard<std::mutex> lock(logger_caches_mutex);
        logger_caches.emplace(tag, &logger);
    }
}

auto scp::LogConfig::logLevel(sc_core::sc_verbosity level) -> scp::LogConfig& {
    this->level = level;
    return *this;
}

auto scp::LogConfig::msgTypeFieldWidth(unsigned width) -> scp::LogConfig& {
    this->msg_type_field_width = width;
    return *this;
}

auto scp::LogConfig::printSysTime(bool enable) -> scp::LogConfig& {
    this->print_sys_time = enable;
    return *this;
}

auto scp::LogConfig::printSimTime(bool enable) -> scp::LogConfig& {
    this->print_sim_time = enable;
    return *this;
}

auto scp::LogConfig::printDelta(bool enable) -> scp::LogConfig& {
    this->print_delta = enable;
    return *this;
}

auto scp::LogConfig::printSeverity(bool enable) -> scp::LogConfig& {
    this->print_severity = enable;
    return *this;
}

auto scp::LogConfig::logFileName(std::string&& name) -> scp::LogConfig& {
    this->log_file_name = name;
    return *this;
}

auto scp::LogConfig::logFileName(const std::string& name) -> scp::LogConfig& {
    this->log_file_name = name;
    return *this;
}

auto scp::LogConfig::coloredOutput(bool enable) -> scp::LogConfig& {
    this->colored_output = enable;
    return *this;
}

auto scp::LogConfig::logFilterRegex(std::string&& expr) -> scp::LogConfig& {
    this->log_filter_regex = expr;
    return *this;
}

auto scp::LogConfig::logFilterRegex(const std::string& expr)
    -> scp::LogConfig& {
    this->log_filter_regex = expr;
    return *this;
}

auto scp::LogConfig::logAsync(bool v) -> scp::LogConfig& {
    this->log_async = v;
    return *this;
}

auto scp::LogConfig::reportOnlyFirstError(bool v) -> scp::LogConfig& {
    this->report_only_first_error = v;
    return *this;
}

auto scp::LogConfig::fileInfoFrom(int v) -> scp::LogConfig& {
    this->file_info_from = v;
    return *this;
}

auto scp::LogConfig::displayNameStyle(scp::DisplayName v) -> scp::LogConfig& {
    this->display_name = v;
    return *this;
}

std::vector<std::string> scp::get_logging_parameters() {
    return std::vector<std::string>(logging_parameters.begin(),
                                    logging_parameters.end());
}

/* Shared helpers for CCI-based verbosity lookup */

static const sc_core::sc_verbosity
    SCP_VERBOSITY_UNSET = (sc_core::sc_verbosity)INT_MAX;

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, '.')) {
        result.push_back(item);
    }
    return result;
}

std::string join(std::vector<std::string> vec) {
    if (vec.empty())
        return "";
    return std::accumulate(
        vec.begin(), vec.end(), std::string(),
        [](const std::string& a, const std::string& b) -> std::string {
            return a + (a.length() > 0 ? "." : "") + b;
        });
}

#ifdef HAS_CCI
sc_core::sc_verbosity cci_lookup(cci::cci_broker_handle broker,
                                 std::string name) {
    auto param_name = (name.empty()) ? SCP_LOG_LEVEL_PARAM_NAME
                                     : name + "." SCP_LOG_LEVEL_PARAM_NAME;
    auto h = broker.get_param_handle(param_name);
    if (h.is_valid()) {
        auto val = h.get_cci_value();
        if (val.is_string())
            return cci_to_verbosity(val.get_string());
        return cci_to_verbosity(val.get_int());
    } else {
        auto val = broker.get_preset_cci_value(param_name);

        if (val.is_string()) {
            broker.lock_preset_value(param_name);
            return cci_to_verbosity(val.get_string());
        }
        if (val.is_int()) {
            broker.lock_preset_value(param_name);
            return cci_to_verbosity(val.get_int());
        }
    }
    return SCP_VERBOSITY_UNSET;
}
#endif

#ifdef __GNUG__
std::string demangle(const char* name) {
    int status = -4;
    std::unique_ptr<char, void (*)(void*)> res{
        abi::__cxa_demangle(name, NULL, NULL, &status), std::free
    };
    return (status == 0) ? res.get() : name;
}
#else
std::string demangle(const char* name) {
    return name;
}
#endif

void insert(std::multimap<int, std::string, std::greater<int>>& map,
            std::string s, bool interesting) {
    int n = std::count(s.begin(), s.end(), '.');
    map.insert(make_pair(n, s));

    if (interesting) {
        logging_parameters.insert(s + "." SCP_LOG_LEVEL_PARAM_NAME);
    }
}

#ifdef HAS_CCI
/* CCI-based verbosity callback for SC_LOG.
 * Replicates old SCP's feature-priority CCI lookup using the logger's
 * tag (comma-separated features), scname, and typename. */
static sc_core::sc_verbosity scp_cci_log_verbosity(
    sc_core::sc_log_logger_cache& logger, const char* file, int line,
    std::string_view local_tag) {
    /* Fast path: per-tag LUT for string-tag lookups */
    if (!local_tag.empty()) {
        std::shared_lock<std::shared_mutex> lk(tag_lut_mutex);
        auto it = tag_lut.find(std::string(local_tag));
        if (it != tag_lut.end())
            return it->second;
    }

    /* Register this cache under all its identifiers for reset_logging/
     * set_log_level_by_name lookups. Protected by mutex since the callback
     * may be invoked from non-SystemC threads. */
    {
        std::lock_guard<std::mutex> lock(logger_caches_mutex);
        if (!logger.scname.empty())
            logger_caches.emplace(std::string(logger.scname), &logger);
        if (!logger.tag.empty()) {
            std::string tag_str(logger.tag);
            std::istringstream iss(tag_str);
            std::string item;
            while (std::getline(iss, item, SCP_TAG_SEP)) {
                if (!item.empty())
                    logger_caches.emplace(item, &logger);
            }
        }
        logger_caches.emplace("", &logger);
    }

    std::string scname_str;
    std::vector<std::string> features;

    if (!logger.tag.empty()) {
        /* Split compound tag on SCP_TAG_SEP to recover individual features */
        std::string tag_str(logger.tag);
        std::istringstream iss(tag_str);
        std::string item;
        while (std::getline(iss, item, SCP_TAG_SEP)) {
            if (!item.empty())
                features.push_back(item);
        }
    }

    if (!logger.scname.empty()) {
        scname_str = std::string(logger.scname);
    } else if (!local_tag.empty()) {
        scname_str = std::string(local_tag);
    } else if (!features.empty()) {
        scname_str = features[0];
    }

    std::string tname_str;
    if (logger.typename_str) {
        tname_str = demangle(logger.typename_str);
    }

    std::string fname_str;
    if (file) {
        fname_str = file;
        auto slash = fname_str.find_last_of("/\\");
        if (slash != std::string::npos)
            fname_str = fname_str.substr(slash + 1);
    }

    /* For completely anonymous loggers, fall back to global verbosity.
     * filename alone doesn't make a logger non-anonymous — it's just
     * additional context when a scname/tag/typename is present. */
    if (scname_str.empty() && features.empty() && tname_str.empty()) {
        return static_cast<sc_core::sc_verbosity>(
            ::sc_core::sc_report_handler::get_verbosity_level());
    }

    /* Only access CCI broker from the SystemC thread.
     * Other threads (e.g. QEMU) get the global default without caching,
     * so the next call from the SystemC thread will resolve correctly. */
    if (std::this_thread::get_id() != sysc_thread_id) {
        return static_cast<sc_core::sc_verbosity>(
            ::sc_core::sc_report_handler::get_verbosity_level());
    }

    try {
        auto broker = sc_core::sc_get_current_object()
                          ? cci::cci_get_broker()
                          : cci::cci_get_global_broker(scp_global_originator);

        std::multimap<int, std::string, std::greater<int>> allfeatures;

        for (auto scn = split(scname_str); scn.size(); scn.pop_back()) {
            for (size_t first = 0; first < scn.size(); first++) {
                auto f = scn.begin() + first;
                std::vector<std::string> p(f, scn.end());
                auto scn_str = ((first > 0) ? "*." : "") + join(p);

                for (auto& ft : features) {
                    for (auto ftn = split(ft); ftn.size(); ftn.pop_back()) {
                        insert(allfeatures, scn_str + "." + join(ftn),
                               first == 0);
                    }
                }
                if (!tname_str.empty())
                    insert(allfeatures, scn_str + "." + tname_str, first == 0);
                if (!fname_str.empty())
                    insert(allfeatures, scn_str + "." + fname_str, first == 0);
                insert(allfeatures, scn_str, first == 0);
            }
        }
        for (auto& ft : features) {
            for (auto ftn = split(ft); ftn.size(); ftn.pop_back()) {
                insert(allfeatures, join(ftn), true);
                insert(allfeatures, "*." + join(ftn), false);
            }
        }
        if (!tname_str.empty())
            insert(allfeatures, tname_str, true);
        if (!fname_str.empty())
            insert(allfeatures, fname_str, true);
        insert(allfeatures, "*", false);
        insert(allfeatures, "", false);

        for (auto& [priority, name] : allfeatures) {
            sc_core::sc_verbosity v = cci_lookup(broker, name);
            if (v != SCP_VERBOSITY_UNSET) {
                auto lvl = static_cast<sc_core::sc_verbosity>(v);
                if (local_tag.empty() && &logger != &SC_LOG_LOG_LEVEL_CACHE_GLOBAL)
                    logger.level = lvl;
                else if (!local_tag.empty()) {
                    std::unique_lock<std::shared_mutex> lk(tag_lut_mutex);
                    tag_lut[std::string(local_tag)] = lvl;
                }
                return lvl;
            }
        }
    } catch (const std::exception&) {
        // If there is no global broker, revert to initialized verbosity level
    }

    auto lvl = static_cast<sc_core::sc_verbosity>(
        ::sc_core::sc_report_handler::get_verbosity_level());
    if (local_tag.empty() && &logger != &SC_LOG_LOG_LEVEL_CACHE_GLOBAL)
        logger.level = lvl;
    else if (!local_tag.empty()) {
        std::unique_lock<std::shared_mutex> lk(tag_lut_mutex);
        tag_lut[std::string(local_tag)] = lvl;
    }
    return lvl;
}
#endif // HAS_CCI

auto scp::get_log_verbosity(char const* str) -> sc_core::sc_verbosity {
    return static_cast<sc_core::sc_verbosity>(
        ::sc_core::sc_report_handler::get_verbosity_level());
}
