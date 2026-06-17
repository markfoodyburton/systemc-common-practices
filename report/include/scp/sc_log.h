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
  sc_log.h -- Portable SC_LOG API.

  On SystemC 4.0 (SC_HAS_SC_LOG defined), includes the native SC_LOG header.
  On older SystemC, provides the full SC_LOG public API (types, macros,
  sc_logger template) in the sc_core:: namespace, matching the SystemC 4.0
  implementation as close as possible.
*****************************************************************************/

#ifndef _SCP_SC_LOG_H_
#define _SCP_SC_LOG_H_

#include <systemc>

/* If SystemC already provides SC_LOG, nothing to do. */
#if __has_include(<sysc/log/sc_log.h>)
#include <sysc/log/sc_log.h>
#endif

#ifndef SC_HAS_SC_LOG
#define SC_HAS_SC_LOG

#include <array>
#include <climits>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <vector>

#include <sysc/utils/sc_report.h>

/*==========================================================================
 * Types — in sc_core:: namespace, matching SystemC 4.0 sc_log_types.h exactly
 *==========================================================================*/

namespace sc_core {

// Windows defines ERROR as a macro; undef to allow it as a level name.
#undef ERROR

/* Initial logger-cache level meaning "not yet resolved" (replaced once the
 * verbosity is computed).  Internal implementation marker, not part of
 * IEEE 1666.  This definition is only compiled on older SystemC; on
 * SystemC 4.0 SC_UNSET comes from the native sc_log header. */
inline constexpr int SC_UNSET = INT_MAX;

/* Logging levels are sc_core::sc_verbosity values:
 *   SC_LOW (100)=CRITICAL  SC_MEDIUM (200)=ALERT  SC_HIGH (300)=NOTE
 *   SC_FULL (400)=DETAIL   SC_DEBUG (500)=INTERNAL.  SC_NONE (0)="off".
 * Textual level names and string parsing are an SCP convenience
 * (scp::as_log / scp::level_name in cci_report_backend.h). */

/* Bucket a raw verbosity to the nearest level at or above it. */
inline sc_verbosity as_log(int v) {
    if (v <= sc_core::SC_LOW)    return sc_core::SC_LOW;
    if (v <= sc_core::SC_MEDIUM) return sc_core::SC_MEDIUM;
    if (v <= sc_core::SC_HIGH)   return sc_core::SC_HIGH;
    if (v <= sc_core::SC_FULL)   return sc_core::SC_FULL;
    return sc_core::SC_DEBUG;
}

class sc_log_priv__call_sc_name_fn {
    template <class T>
    static auto test(T* p) -> decltype(p->name(), std::true_type());
    template <class T>
    static auto test(...) -> decltype(std::false_type());

    template <class T>
    static constexpr bool has_method = decltype(test<T>(nullptr))::value;

public:
    template <class TYPE>
    auto operator()(TYPE* p) const
        -> std::enable_if_t<has_method<TYPE>, const char*> {
        return p->name();
    }

    template <class TYPE>
    auto operator()(TYPE* p) const
        -> std::enable_if_t<!has_method<TYPE>, const char*> {
        return nullptr;
    }
};

/* Forward declaration */
struct sc_log_impl;

struct sc_log_logger_cache {
    int level = SC_UNSET;
    std::string tag{};
    std::string scname{};
    const char* typename_str = nullptr;

    int get_log_verbosity_cached(const char* file, int line,
                                          std::string_view local_tag = {});

    void set_tag(std::string new_tag) {
        tag = std::move(new_tag);
        level = SC_UNSET;
    }

    /// Lifecycle: set by get_log_verbosity_cached(), cleared by ~sc_logger().
    /// Accessor functions ensure correct linkage across shared libraries.
    static sc_log_logger_cache* get_current();
    static void set_current(sc_log_logger_cache* p);
};

struct sc_log_handle_factory {
    template <class TYPE>
    static sc_log_logger_cache make(int lvl, const char* tag_str,
                                    TYPE* p) {
        const char* n = sc_log_priv__call_sc_name_fn{}(p);
        const char* t = typeid(*p).name();
        return sc_log_logger_cache{
            lvl,
            tag_str ? std::string(tag_str) : std::string(),
            n ? std::string(n) : std::string(),
            t
        };
    }

    static sc_log_logger_cache make_static(int lvl,
                                           const char* tag_str) {
        return sc_log_logger_cache{
            lvl,
            tag_str ? std::string(tag_str) : std::string(),
            std::string(),
            nullptr
        };
    }
};

std::vector<std::string> get_logging_parameters();

struct sc_log_impl {
    static void sc_set_log_verbosity_fn(
        std::function<sc_verbosity(sc_log_logger_cache&, const char*, int,
                                   std::string_view)> fn);

    static sc_verbosity sc_get_log_verbosity(
        sc_log_logger_cache& logger, const char* file, int line,
        std::string_view local_tag = {});
};

template <sc_core::sc_severity SEVERITY, bool WITH_ACTIONS = false>
struct sc_logger {
    sc_logger(const char* file, int line,
              int verbosity = sc_core::SC_MEDIUM)
        : t(nullptr), file(file), line(line), level(verbosity) {}

    sc_logger() = delete;
    sc_logger(const sc_logger&) = delete;
    sc_logger(sc_logger&&) = delete;
    sc_logger& operator=(const sc_logger&) = delete;
    sc_logger& operator=(sc_logger&&) = delete;

    virtual ~sc_logger() noexcept(true) {
        auto old = sc_core::sc_report_handler::set_actions(SEVERITY);
        if (WITH_ACTIONS == false) {
            sc_core::sc_report_handler::set_actions(
                SEVERITY, old & ~(sc_core::SC_THROW | sc_core::SC_INTERRUPT |
                                  sc_core::SC_STOP | sc_core::SC_ABORT));
        }
        ::sc_core::sc_report_handler::report(
            SEVERITY, (t && *t) ? t : "SystemC", os.str().c_str(),
            static_cast<sc_core::sc_verbosity>(level), file, line);
        sc_core::sc_report_handler::set_actions(SEVERITY, old);
        sc_log_logger_cache::set_current(nullptr);
    }

    inline sc_logger& type() {
        this->t = nullptr;
        return *this;
    }
    inline sc_logger& type(char const* t) {
        this->t = const_cast<char*>(t);
        return *this;
    }
    inline sc_logger& type(std::string const& t) {
        this->t = const_cast<char*>(t.c_str());
        return *this;
    }
    inline std::ostream& get() { return os; };

protected:
    std::ostringstream os{};
    char* t{ nullptr };
    const char* file;
    const int line;
    const int level;
};

} // namespace sc_core

/*==========================================================================
 * Global default logger — same name as SystemC 4.0
 *
 * The macro must be defined BEFORE the extern declaration so the
 * preprocessor expands the name to _m_sc_log_log_level_cache_ in the
 * declaration (matching the definition in sc_report.cpp).
 *==========================================================================*/

#define SC_LOG_LOG_LEVEL_CACHE _m_sc_log_log_level_cache_
#define SC_LOG_LOG_LEVEL_CACHE_GLOBAL ::_m_sc_log_log_level_cache_

extern sc_core::sc_log_logger_cache SC_LOG_LOG_LEVEL_CACHE;

/*==========================================================================
 * Macros — matching SystemC 4.0 sc_log.h exactly
 *==========================================================================*/

/* 2-step token pasting */
#define SC_LOG_PRIV__CAT_IMPL(a, b) a##b
#define SC_LOG_PRIV__CAT(a, b) SC_LOG_PRIV__CAT_EVAL(a, b)
#define SC_LOG_PRIV__CAT_EVAL(a, b) SC_LOG_PRIV__CAT_IMPL(a, b)

/* Argument counting: 0, 1, or 2 arguments */
#define SC_LOG_PRIV__NARG(...) SC_LOG_PRIV__NARG_IMPL(0, ##__VA_ARGS__, 2, 1, 0)
#define SC_LOG_PRIV__NARG_IMPL(_0, _1, _2, N, ...) N

/* Dispatch to appropriate macro based on argument count */
#define SC_LOG_PRIV__DISPATCH(func, ...)                                       \
    SC_LOG_PRIV__DISPATCH_IMPL(func, SC_LOG_PRIV__NARG(__VA_ARGS__), __VA_ARGS__)
#define SC_LOG_PRIV__DISPATCH_IMPL(func, count, ...)                           \
    SC_LOG_PRIV__CAT(func, count)(__VA_ARGS__)

/* Detect logger handle vs tag */
#define SC_LOG_PRIV__IS_LOGGER_HANDLE(x)                                       \
    std::is_same_v<std::decay_t<decltype(x)>, sc_core::sc_log_logger_cache>

/* Format string: const char* without parens, fmt::format with parens */
static const char* SC_LOG_PRIV__FMT_EMPTY_STR = "";
#ifdef FMT_SHARED
#include <fmt/format.h>
#define SC_LOG_PRIV__FMT_EMPTY_STR(...) fmt::format(__VA_ARGS__)
#else
#define SC_LOG_PRIV__FMT_EMPTY_STR(...) ""
#endif

/* Verbosity check variants */
#define SC_LOG_PRIV__VBSTY_CHECK0(lvl)                                         \
    ((SC_LOG_LOG_LEVEL_CACHE.level >= (lvl)) &&                                \
     (SC_LOG_LOG_LEVEL_CACHE.get_log_verbosity_cached(__FILE__, __LINE__) >=   \
      (lvl)))

#define SC_LOG_PRIV__VBSTY_CHECK1(lvl, arg1)                                  \
    ([&](auto&& x) -> bool {                                                   \
        if constexpr (SC_LOG_PRIV__IS_LOGGER_HANDLE(x)) {                     \
            return ((x.level >= (lvl)) &&                                      \
                    (x.get_log_verbosity_cached(__FILE__, __LINE__) >= (lvl))); \
        } else {                                                               \
            return SC_LOG_PRIV__VBSTY_CHECK2(lvl, SC_LOG_LOG_LEVEL_CACHE_GLOBAL, x); \
        }                                                                      \
    }(arg1))

#define SC_LOG_PRIV__VBSTY_CHECK2(lvl, logger, tag)                            \
    ((logger.level >= lvl) &&                                                  \
     (logger.get_log_verbosity_cached(__FILE__, __LINE__, tag) >= lvl))

#define SC_LOG_PRIV__VBSTY_CHECK_IMPL(count, lvl, ...)                         \
    SC_LOG_PRIV__CAT(SC_LOG_PRIV__VBSTY_CHECK, count)(lvl, ##__VA_ARGS__)

/* Tag extraction variants — prefer tag for display, fall back to scname.
 * This matches SystemC 4.0 convention where the tag (feature name) is the primary
 * display name for a logger, with scname (module hierarchy) as fallback. */
#define SC_LOG_PRIV__GET_TAG0()                                                \
    (SC_LOG_LOG_LEVEL_CACHE.tag.empty() ? SC_LOG_LOG_LEVEL_CACHE.scname.c_str()\
                                        : SC_LOG_LOG_LEVEL_CACHE.tag.c_str())

#define SC_LOG_PRIV__GET_TAG1(arg1)                                            \
    ([&](auto&& x) -> const char* {                                            \
        if constexpr (SC_LOG_PRIV__IS_LOGGER_HANDLE(x)) {                     \
            return (x.tag.empty() ? x.scname.c_str() : x.tag.c_str());        \
        } else {                                                               \
            return x;                                                          \
        }                                                                      \
    }(arg1))

#define SC_LOG_PRIV__GET_TAG2(logger, tag) tag

/* Handle variants */
#define SC_LOG_PRIV__HANDLE_NAME(x) x

#define SC_LOG_PRIV__HANDLE0()                                                 \
    sc_core::sc_log_logger_cache SC_LOG_LOG_LEVEL_CACHE =                      \
        sc_core::sc_log_handle_factory::make(sc_core::SC_UNSET,     \
                                             "", this)

#define SC_LOG_PRIV__HANDLE1(tag_str)                                          \
    sc_core::sc_log_logger_cache SC_LOG_LOG_LEVEL_CACHE =                      \
        sc_core::sc_log_handle_factory::make(sc_core::SC_UNSET,     \
                                             tag_str, this)

#define SC_LOG_PRIV__HANDLE2(logger_name, tag_str)                             \
    sc_core::sc_log_logger_cache SC_LOG_PRIV__HANDLE_NAME(logger_name) =       \
        sc_core::sc_log_handle_factory::make(sc_core::SC_UNSET,     \
                                             tag_str, this)

/* Static handle variants — use inline for C++17 in-class initialization */
#define SC_LOG_PRIV__HANDLE_STATIC1(tag_str)                                   \
    static inline sc_core::sc_log_logger_cache SC_LOG_LOG_LEVEL_CACHE =        \
        sc_core::sc_log_handle_factory::make_static(                           \
            sc_core::SC_UNSET, tag_str)

#define SC_LOG_PRIV__HANDLE_STATIC2(logger_name, tag_str)                      \
    static inline sc_core::sc_log_logger_cache                                 \
        SC_LOG_PRIV__HANDLE_NAME(logger_name) =                                \
        sc_core::sc_log_handle_factory::make_static(                           \
            sc_core::SC_UNSET, tag_str)

/*==========================================================================
 * Public API macros — matching SystemC 4.0 exactly
 *==========================================================================*/

#define SC_LOG_HANDLE(...)                                                     \
    SC_LOG_PRIV__DISPATCH(SC_LOG_PRIV__HANDLE, ##__VA_ARGS__)

#define SC_LOG_HANDLE_STATIC(...)                                              \
    SC_LOG_PRIV__DISPATCH(SC_LOG_PRIV__HANDLE_STATIC, ##__VA_ARGS__)

#define SC_LOG_HANDLE_VECTOR(NAME)                                             \
    std::vector<sc_core::sc_log_logger_cache> SC_LOG_PRIV__HANDLE_NAME(NAME)

#define SC_LOG_HANDLE_VECTOR_PUSH_BACK(NAME, tag_str)                          \
    SC_LOG_PRIV__HANDLE_NAME(NAME).push_back(                                  \
        sc_core::sc_log_handle_factory::make(                                  \
            sc_core::SC_UNSET, tag_str, this))

#define SC_LOG_AT(lvl, ...)                                                    \
    if (SC_LOG_PRIV__VBSTY_CHECK_IMPL(SC_LOG_PRIV__NARG(__VA_ARGS__), lvl,    \
                                       ##__VA_ARGS__))                         \
    ::sc_core::sc_logger<::sc_core::SC_INFO, false>(__FILE__, __LINE__, lvl)   \
            .type(SC_LOG_PRIV__DISPATCH(SC_LOG_PRIV__GET_TAG, ##__VA_ARGS__))  \
            .get()                                                             \
        << SC_LOG_PRIV__FMT_EMPTY_STR

/* Convenience logging macros, one per level.  The names describe a message's
 * *significance* (importance) to a reader — deliberately distinct from
 * sc_severity (SC_INFO/SC_WARNING/...) and the raw sc_verbosity names —
 * ordered most-significant to least, mapping onto increasing verbosity:
 *   SC_CRITICAL (most) -> SC_LOW   SC_ALERT -> SC_MEDIUM   SC_NOTE -> SC_HIGH
 *   SC_DETAIL -> SC_FULL           SC_INTERNAL (least) -> SC_DEBUG */
#define SC_CRITICAL(...)                                                       \
    SC_LOG_AT(sc_core::SC_LOW, ##__VA_ARGS__)
#define SC_ALERT(...) SC_LOG_AT(sc_core::SC_MEDIUM, ##__VA_ARGS__)
#define SC_NOTE(...) SC_LOG_AT(sc_core::SC_HIGH, ##__VA_ARGS__)
#define SC_DETAIL(...) SC_LOG_AT(sc_core::SC_FULL, ##__VA_ARGS__)
#define SC_INTERNAL(...) SC_LOG_AT(sc_core::SC_DEBUG, ##__VA_ARGS__)

#endif /* !SC_HAS_SC_LOG */
#endif /* _SCP_SC_LOG_H_ */
