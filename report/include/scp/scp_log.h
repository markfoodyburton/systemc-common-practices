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
 * SCP macro -> SC_LOG translation layer.
 *
 * Translates legacy SCP_INFO, SCP_WARN, SCP_LOGGER, etc. macros to
 * the standard SC_LOG API (SC_INFO, SC_WARN, SC_LOG_HANDLE, etc.).
 * New code should use SC_LOG directly.
 */

#ifndef _SCP_SCP_LOG_H_
#define _SCP_SCP_LOG_H_

#include <scp/sc_log.h>
#include <memory>
#include <vector>

/* scp_logger_cache is an alias for the SC_LOG type (used by qbox registers.h)
 */
namespace scp {
using scp_logger_cache = sc_core::sc_log_logger_cache;
} // namespace scp

/*==========================================================================
 * Preprocessor helpers for SCP parenthesized argument convention
 *==========================================================================*/

#define _SCP_CHECK_N(x, n, ...)  n
#define _SCP_CHECK(...)          _SCP_CHECK_N(__VA_ARGS__, 0, )
#define _SCP_PROBE(x)            x, 1,
#define _SCP_IS_PAREN(x)         _SCP_CHECK(_SCP_IS_PAREN_PROBE x)
#define _SCP_IS_PAREN_PROBE(...) _SCP_PROBE(~)
#define _SCP_IIF_0(t, ...)       __VA_ARGS__
#define _SCP_IIF_1(t, ...)       t
#define _SCP_CAT(a, b)           _SCP_CAT_IMPL(a, b)
#define _SCP_CAT_IMPL(a, b)      a##b
#define _SCP_IIF(c)              _SCP_CAT(_SCP_IIF_, c)
#define _SCP_FIRST_ARG(f, ...)   f
#define _SCP_POP_ARG(f, ...)     __VA_ARGS__
#define _SCP_EXPAND(...)         __VA_ARGS__

#define _SCP_HAS_ARGS(...) _SCP_HAS_ARGS_CHECK(__VA_OPT__(, ) 1, 0, ~)
#define _SCP_HAS_ARGS_CHECK(_1, _2, ...) _2

#define _SCP_PAREN_INNER(paren_tok) _SCP_EXPAND(_SCP_FIRST_ARG paren_tok)

/* SCP-prefixed logger variable name */
#define _SCP_HANDLE_NAME(x)      _SCP_HANDLE_NAME_IMPL(x)
#define _SCP_HANDLE_NAME_IMPL(x) _scp_log_cache_##x

/* Public accessor for the SCP logger variable name.
 * SCP_LOGGER_NAME()    -> _scp_log_cache_  (default logger)
 * SCP_LOGGER_NAME(foo) -> _scp_log_cache_foo (named logger)
 * Migration: replace with the variable name directly. */
#define SCP_LOGGER_NAME(...) _SCP_HANDLE_NAME(__VA_ARGS__)

/*==========================================================================
 * Helper: join feature strings with comma
 *==========================================================================*/

template<typename... Args>
inline const char* _scp_join_tags(Args&&... args) {
    static std::vector<std::shared_ptr<std::string>> tag_storage;
    auto combined = std::make_shared<std::string>();
    ((combined->empty() ? *combined += std::forward<Args>(args)
                        : (*combined += ',', *combined += std::forward<Args>(args))), ...);
    tag_storage.push_back(combined);
    return combined->c_str();
}

/*==========================================================================
 * SCP_LOGGER macros -> SC_LOG_HANDLE
 *
 * SCP_LOGGER(())              -> SC_LOG_HANDLE(_scp_log_cache_, "")
 * SCP_LOGGER((DMI), "dmi")    -> SC_LOG_HANDLE(_scp_log_cache_DMI, "dmi")
 * SCP_LOGGER((DMI), "a", "b") -> SC_LOG_HANDLE(_scp_log_cache_DMI, "a,b")
 * SCP_LOGGER((m_log))         -> SC_LOG_HANDLE(_scp_log_cache_m_log, "")
 * SCP_LOGGER()                -> SC_LOG_HANDLE(_scp_log_cache_, "")
 * SCP_LOGGER("tag")           -> SC_LOG_HANDLE(_scp_log_cache_, "tag")
 *==========================================================================*/

#define SCMOD this->sc_core::sc_module::name()

/* SCP_LOGGER((DMI), "dmi")    -> SC_LOG_HANDLE(_scp_log_cache_DMI, "dmi")
 * SCP_LOGGER((DMI), "a", "b") -> SC_LOG_HANDLE(_scp_log_cache_DMI, "a,b") */
#define _SCP_LOGGER_PN_1(...)                                            \
    SC_LOG_HANDLE(                                                       \
        _SCP_HANDLE_NAME(_SCP_PAREN_INNER(_SCP_FIRST_ARG(__VA_ARGS__))), \
        _scp_join_tags( _SCP_POP_ARG(__VA_ARGS__)))

/* SCP_LOGGER((m_log))         -> SC_LOG_HANDLE(_scp_log_cache_m_log, "") */
#define _SCP_LOGGER_PN_0(...) \
    SC_LOG_HANDLE(            \
        _SCP_HANDLE_NAME(_SCP_PAREN_INNER(_SCP_FIRST_ARG(__VA_ARGS__))), "")

#define _SCP_LOGGER_PAREN_NAMED(...) \
    _SCP_CAT(_SCP_LOGGER_PN_,        \
             _SCP_HAS_ARGS(_SCP_POP_ARG(__VA_ARGS__)))(__VA_ARGS__)

/* SCP_LOGGER(())              -> SC_LOG_HANDLE(_scp_log_cache_, "")
 * SCP_LOGGER((), "feat")      -> SC_LOG_HANDLE(_scp_log_cache_, "feat")
 * SCP_LOGGER((), "a", "b")   -> SC_LOG_HANDLE(_scp_log_cache_, "a,b") */
#define _SCP_LOGGER_PD_0(...) SC_LOG_HANDLE(_scp_log_cache_, "")
#define _SCP_LOGGER_PD_1(...) \
    SC_LOG_HANDLE(_scp_log_cache_, _scp_join_tags(_SCP_POP_ARG(__VA_ARGS__)))
#define _SCP_LOGGER_PAREN_DEFAULT(...)                                          \
    _SCP_CAT(_SCP_LOGGER_PD_, _SCP_HAS_ARGS(_SCP_POP_ARG(__VA_ARGS__)))(__VA_ARGS__)

#define _SCP_LOGGER_PAREN(...)                                              \
    _SCP_IIF(_SCP_HAS_ARGS(_SCP_PAREN_INNER(_SCP_FIRST_ARG(__VA_ARGS__))))( \
        _SCP_LOGGER_PAREN_NAMED(__VA_ARGS__),                               \
        _SCP_LOGGER_PAREN_DEFAULT(__VA_ARGS__))

/* SCP_LOGGER()                -> SC_LOG_HANDLE(_scp_log_cache_, "") */
#define _SCP_LOGGER_NP_0(...) SC_LOG_HANDLE(_scp_log_cache_, "")
/* SCP_LOGGER("tag")           -> SC_LOG_HANDLE(_scp_log_cache_, "tag") */
#define _SCP_LOGGER_NP_1(...) \
    SC_LOG_HANDLE(_scp_log_cache_, _scp_join_tags( __VA_ARGS__))

#define _SCP_LOGGER_NOPAREN(...) \
    _SCP_CAT(_SCP_LOGGER_NP_, _SCP_HAS_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define SCP_LOGGER(...)                                   \
    _SCP_IIF(_SCP_IS_PAREN(_SCP_FIRST_ARG(__VA_ARGS__)))( \
        _SCP_LOGGER_PAREN(__VA_ARGS__), _SCP_LOGGER_NOPAREN(__VA_ARGS__))

#define SCP_LOGGER_VECTOR(NAME) SC_LOG_HANDLE_VECTOR(_SCP_HANDLE_NAME(NAME))

#define SCP_LOGGER_VECTOR_PUSH_BACK(NAME, ...)             \
    SC_LOG_HANDLE_VECTOR_PUSH_BACK(_SCP_HANDLE_NAME(NAME), \
                                   _scp_join_tags( __VA_ARGS__))

/*==========================================================================
 * SCP logging macros -> SC_INFO, SC_WARN, etc.
 *
 * SCP_INFO(())            -> SC_INFO(_scp_log_cache_)
 * SCP_INFO((DMI))         -> SC_INFO(_scp_log_cache_DMI)
 * SCP_INFO((DMI), "tag")  -> SC_INFO(_scp_log_cache_DMI, "tag")
 * SCP_INFO((), "tag")     -> SC_INFO(_scp_log_cache_, "tag")
 * SCP_INFO(SCMOD)         -> SC_INFO(SCMOD)
 * SCP_INFO()              -> SC_INFO()
 *==========================================================================*/

/* SCP_INFO((DMI))         -> SC_INFO(_scp_log_cache_DMI)
 * SCP_INFO((DMI), "tag")  -> SC_INFO(_scp_log_cache_DMI, "tag") */
#define _SCP_DP_NAMED_0(M, ...) \
    M(_SCP_HANDLE_NAME(_SCP_PAREN_INNER(_SCP_FIRST_ARG(__VA_ARGS__))))
#define _SCP_DP_NAMED_1(M, ...) \
    M(_SCP_HANDLE_NAME(_SCP_PAREN_INNER(_SCP_FIRST_ARG(__VA_ARGS__))), \
      _SCP_POP_ARG(__VA_ARGS__))
#define _SCP_DP_NAMED(M, ...) \
    _SCP_CAT(_SCP_DP_NAMED_, _SCP_HAS_ARGS(_SCP_POP_ARG(__VA_ARGS__)))(M, __VA_ARGS__)

/* SCP_INFO(())            -> SC_INFO(_scp_log_cache_) */
#define _SCP_DP_DEFAULT_0(M, ...) M(_scp_log_cache_)
/* SCP_INFO((), "tag")     -> SC_INFO(_scp_log_cache_, "tag") */
#define _SCP_DP_DEFAULT_1(M, ...) M(_scp_log_cache_, _SCP_FIRST_ARG(_SCP_POP_ARG(__VA_ARGS__)))

#define _SCP_DP_DEFAULT(M, ...)                                           \
    _SCP_CAT(_SCP_DP_DEFAULT_, _SCP_HAS_ARGS(_SCP_POP_ARG(__VA_ARGS__)))( \
        M, __VA_ARGS__)

#define _SCP_DP_PAREN(M, ...)                                               \
    _SCP_IIF(_SCP_HAS_ARGS(_SCP_PAREN_INNER(_SCP_FIRST_ARG(__VA_ARGS__))))( \
        _SCP_DP_NAMED(M, __VA_ARGS__), _SCP_DP_DEFAULT(M, __VA_ARGS__))

/* SCP_INFO(SCMOD)      -> SC_INFO(SCMOD)
 * SCP_INFO()           -> SC_INFO() */
#define _SCP_DISPATCH(M, ...)                                 \
    _SCP_IIF(_SCP_HAS_ARGS(__VA_ARGS__))(                     \
        _SCP_IIF(_SCP_IS_PAREN(_SCP_FIRST_ARG(__VA_ARGS__)))( \
            _SCP_DP_PAREN(M, __VA_ARGS__), M(__VA_ARGS__)),   \
        M())

#define SCP_TRACEALL(...) _SCP_DISPATCH(SC_TRACE, __VA_ARGS__)
#define SCP_TRACE(...)    _SCP_DISPATCH(SC_TRACE, __VA_ARGS__)
#define SCP_DEBUG(...)    _SCP_DISPATCH(SC_DEBUG, __VA_ARGS__)
#define SCP_INFO(...)     _SCP_DISPATCH(SC_INFO, __VA_ARGS__)
#define SCP_WARN(...)     _SCP_DISPATCH(SC_WARN, __VA_ARGS__)
#define SCP_CRITICAL(...) _SCP_DISPATCH(SC_CRITICAL, __VA_ARGS__)
#define SCP_ERR(...)      _SCP_DISPATCH(SC_CRITICAL, __VA_ARGS__)
#define SCP_FATAL(...)    _SCP_DISPATCH(SC_CRITICAL, __VA_ARGS__)

#endif /* _SCP_SCP_LOG_H_ */
