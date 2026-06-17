## SCP Reporting / Logging Library

This library provides a logging backend (spdlog + CCI integration) and optional
SCP macro compatibility for SystemC projects. It supports both SystemC 3
(via an SC_LOG adaptation layer) and SystemC 4 (native SC_LOG).

### Headers

| Header | Purpose | Who includes it |
| ------ | ------- | --------------- |
| `<scp/cci_report_backend.h>` | Backend: `scp::LogHandler`, `scp::LogConfig`, CCI verbosity | Top-level (`sc_main`) |
| `<scp/scp_log.h>` | SCP macros: `SCP_LOGGER`, `SCP_INFO`, etc. | Modules using SCP macros |
| `<scp/sc_log.h>` | Portable SC_LOG API (native on SystemC 4, adaptation on SystemC 3) | Modules using SC_LOG on SystemC 3 or 4 |
| `<scp/report.h>` | **Deprecated** — includes both of the above + legacy aliases | Existing code (migration) |

On SystemC 4, `<systemc>` already provides SC_LOG natively. `<scp/sc_log.h>` is
only needed for portability across SystemC 3 and 4.

> Porting an existing SCP-based project to native SC_LOG? See
> [`MIGRATION.md`](MIGRATION.md) for a short step-by-step checklist.

### Include patterns

| Use case | Modules include | Top level includes |
| -------- | --------------- | ------------------ |
| SCP macros (SystemC 3 or 4) | `<scp/scp_log.h>` | `<scp/cci_report_backend.h>` |
| SC_LOG portable (SystemC 3 or 4) | `<scp/sc_log.h>` | `<scp/cci_report_backend.h>` |
| SC_LOG native (SystemC 4 only) | `<systemc>` | `<scp/cci_report_backend.h>` |

----

## Log Levels

Logging levels are expressed directly as `sc_core::sc_verbosity` values on a
clean 100-step scale.  The convenience macros and the SCP-prefixed macros map
as follows:

| Level | Label | SC_LOG macro | SCP macro | `sc_core::sc_verbosity` |
| ----- | ----- | ------------ | --------- | ----------------------- |
| CRITICAL | `C` | `SC_CRITICAL` | `SCP_FATAL` / `SCP_ERR` / `SCP_CRITICAL` | `SC_LOW` (100) |
| ALERT    | `A` | `SC_ALERT`    | `SCP_WARN`  | `SC_MEDIUM` (200) |
| NOTE     | `N` | `SC_NOTE`     | `SCP_INFO`  | `SC_HIGH` (300) |
| DETAIL   | `D` | `SC_DETAIL`   | `SCP_DEBUG` | `SC_FULL` (400) |
| INTERNAL | `I` | `SC_INTERNAL` | `SCP_TRACE` / `SCP_TRACEALL` | `SC_DEBUG` (500) |

`SC_NONE` (0) is "off" — nothing is emitted at verbosity 0.

**Notes / differences from the older SCP scheme:**

- The convenience-macro family was renamed away from SystemC's reserved
  vocabulary (`SC_INFO`/`SC_DEBUG` collided with `sc_severity`/`sc_verbosity`
  enumerators; `SC_TRACE` was confusable with `sc_trace()`).  The separate
  `sc_log_level` enum was removed — levels *are* `sc_verbosity` values now.

- The **SCP-prefixed macro names are unchanged** (`SCP_INFO`, `SCP_DEBUG`,
  `SCP_TRACE`, ...), so existing call sites keep compiling.  Only the level
  *name* shown in the output changes (e.g. an `SCP_INFO` line now prints with
  the `[N]` / `NOTE` label).

- `SCP_FATAL`, `SCP_ERR`, and `SCP_CRITICAL` map to `SC_CRITICAL`.  They are
  **non-intrusive** — they do NOT call `abort()`, `throw`, or `sc_stop()`.
  This matches the SC_LOG standard which states that logging "shall not
  throw an exception or abort the program." If you need fatal/error behavior
  with side effects, use `SC_REPORT_FATAL` / `SC_REPORT_ERROR` directly.

- `CRITICAL` now sits at verbosity 100 (not 0): at the default verbosity of 0
  nothing prints; raise the verbosity to `SC_LOW` (100) or above to see
  `CRITICAL` messages.

## SC_LOG Macros (Native API)

### Default handle (SC_LOG_HANDLE)

On **SystemC 4**, every `sc_module` automatically has a default logging handle
(provided by `sc_module` itself). No explicit declaration is needed.

On **SystemC 3** (with the adaptation layer), `sc_module` does NOT provide a
default handle. Modules that use `SC_NOTE()`, `SC_ALERT()`, etc. must
declare one explicitly:

```cpp
SC_MODULE(my_module) {
    SC_LOG_HANDLE();  // Required on SystemC 3
    SC_CTOR(my_module) {
        SC_NOTE() << "hello";
    }
};
```

On SystemC 4, the explicit `SC_LOG_HANDLE()` shadows the inherited one.
While this compiles, it creates a redundant member. When targeting
SystemC 4 only, these declarations should be removed.

For **portable code** (SystemC 3 and 4), prefer `SCP_LOGGER(())` from
`<scp/scp_log.h>` instead. It creates a separate `_scp_log_cache_` variable
that does not conflict with `sc_module`'s built-in handle on either version.

### Handle forms

```cpp
SC_LOG_HANDLE()                      // Default handle (needed on SystemC 3)
SC_LOG_HANDLE("tag")                 // Default handle with tag
SC_LOG_HANDLE(name, "tag")           // Named handle with tag
SC_LOG_HANDLE(name, "feat_a,feat_b") // Named handle with multiple features
```

### Logging macros

```cpp
SC_NOTE() << "message";              // Default handle
SC_NOTE(handle) << "message";        // Named handle
SC_NOTE("tag") << "message";         // String tag
SC_ALERT(handle, "tag") << "message"; // Handle with override tag
```

Format strings are supported if `<format>` or fmt is available:
```cpp
SC_NOTE()("The answer is {}.", 42);
```

## SCP Macros (Deprecated — Legacy Compatibility)

The SCP macros translate to standard SC_LOG macros. They are provided for
backward compatibility with existing codebases. **New code should use
SC_LOG directly.**

```cpp
SCP_LOGGER()                         // Default logger
SCP_LOGGER(())                       // Default logger (alternate form)
SCP_LOGGER((D), "other")             // Named logger D with tag "other"
SCP_LOGGER((D), "feat_a", "feat_b")  // Named logger D with multiple features

SCP_INFO(()) << "default";           // Default cached logger
SCP_INFO((D)) << "named";            // Named logger D
SCP_INFO((), "tag") << "override";   // Default handle + tag override
SCP_INFO((D), "tag") << "override";  // Named handle + tag override
SCP_INFO(SCMOD) << "module name";    // String tag (uses global logger)
```

**Translation rules:**

| SCP form | SC_LOG equivalent |
| -------- | ----------------- |
| `SCP_INFO(())` | `SC_NOTE(_scp_log_cache_)` |
| `SCP_INFO((D))` | `SC_NOTE(_scp_log_cache_D)` |
| `SCP_INFO((), "tag")` | `SC_NOTE(_scp_log_cache_, "tag")` |
| `SCP_INFO((D), "tag")` | `SC_NOTE(_scp_log_cache_D, "tag")` |
| `SCP_INFO(SCMOD)` | `SC_NOTE(this->sc_core::sc_module::name())` |
| `SCP_INFO()` | `SC_NOTE()` |

Note: `SCP_INFO(SCMOD)` expands to the string-tag form which uses the
global logger, not the module's cached handle. See **String-Tag Form**
above.

## Migrating from SCP Macros to SC_LOG

New code should use the SC_LOG API directly. The SCP macros are
deprecated and will be removed in a future release.

### Logger declarations

| SCP form | SC_LOG equivalent |
| -------- | ----------------- |
| `SCP_LOGGER(())` | `SC_LOG_HANDLE()` |
| `SCP_LOGGER((D), "tag")` | `SC_LOG_HANDLE(D, "tag")` |
| `SCP_LOGGER((D), "a", "b")` | `SC_LOG_HANDLE(D, "a,b")` |
| `SCP_LOGGER_VECTOR(v)` | `SC_LOG_HANDLE_VECTOR(v)` |
| `SCP_LOGGER_VECTOR_PUSH_BACK(v, "t")` | `SC_LOG_HANDLE_VECTOR_PUSH_BACK(v, "t")` |

### Logging calls

| SCP form | SC_LOG equivalent |
| -------- | ----------------- |
| `SCP_INFO(())` | `SC_NOTE()` |
| `SCP_INFO((D))` | `SC_NOTE(D)` |
| `SCP_INFO((v[0]))` | `SC_NOTE(v[0])` |

### Special cases

**`SCP_INFO(SCMOD)`** expands to `SC_NOTE(this->sc_core::sc_module::name())`
— the string-tag form. This uses the **global** logger, not the module's
handle. It works correctly but is slower (no caching per module). To
migrate: replace with `SC_NOTE()` (which uses the module's default handle)
and ensure `SC_LOG_HANDLE()` is declared in the module.

**`SCP_INFO((), "tag")`** expands to `SC_NOTE(_scp_log_cache_, "tag")`
— the handle + tag override form. The SC_LOG equivalent is
`SC_NOTE(handle, "tag")` where `handle` is a named `SC_LOG_HANDLE`.
There is no direct equivalent using the default handle without knowing
its variable name. If a tag override is needed, declare a named handle:

```cpp
SC_LOG_HANDLE(my_h, "");           // or SC_LOG_HANDLE(my_h, "tag")
SC_NOTE(my_h, "override") << ...;  // handle + tag override
```

## Multi-Feature Tags

A logger can have multiple features specified as a comma-separated tag string.
The CCI verbosity callback splits the tag and checks each feature individually.

```cpp
// SC_LOG style:
SC_LOG_HANDLE(DMI, "dmi,debug");

// SCP style (joined automatically):
SCP_LOGGER((DMI), "dmi", "debug");
```

Setting `dmi.log_level=5` OR `debug.log_level=5` via CCI will enable
this logger.

### Display name

When a logger has features, the tag (feature list) is shown as the display
name in log output. When a logger has no features (empty tag), the module's
hierarchical name (`scname`) is shown instead. This is controlled by SC_LOG's
`GET_TAG` convention.

## CCI Log Level Values

The `log_level` CCI parameter accepts three forms:

| Form | Example | Resolves to |
| ---- | ------- | ----------- |
| Small int (0–99) | `log_level=5` | 0=off, 1–3=CRITICAL, 4=ALERT, 5=NOTE, 6+=INTERNAL |
| Large int (≥100) | `log_level=400` | Direct `sc_verbosity` value (100=CRITICAL, 200=ALERT, 300=NOTE, 400=DETAIL, 500=INTERNAL) |
| String | `log_level="DETAIL"` | Canonical names: NONE, CRITICAL, ALERT, NOTE, DETAIL, INTERNAL. Also accepts the legacy names WARN, WARNING, INFO, DEBUG, TRACE, TRACEALL, DBGTRACE, FATAL, ERROR. |

For example `log_level=400` and `log_level="DETAIL"` both resolve to the
DETAIL level (the legacy `log_level="DEBUG"` also resolves to DETAIL).

## String-Tag Form

`SC_NOTE("tag")` (1-arg string form) uses the **global** default logger,
not the module's local handle. The verbosity is looked up via CCI using
`"tag"` as the scope name. Each distinct tag is cached in a per-tag
lookup table (thread-safe, `shared_mutex`-protected), so repeated calls
with the same tag are efficient. Different string tags do not interfere
with each other's cached levels.

Note: `SC_NOTE("tag")` does NOT use the module's logger. For module-aware
logging with a tag override, use the 2-arg form:
`SC_NOTE(handle, "tag")`.

## Feature Matching Rules

The CCI verbosity callback builds a prioritized list of parameter names
to check. It uses the logger's **scname** (module hierarchy), **tag**
(comma-separated features), **C++ type name**, and **source filename**.
More specific names (more dots) have higher priority. The first matching
CCI parameter wins.

### Example

Given:
- Module type: `my_mod` (C++ class name)
- Module instance: `top.foo` (SystemC hierarchy)
- Source file: `my_mod.cpp`
- Logger: `SC_LOG_HANDLE(D, "dmi")` or `SCP_LOGGER((D), "dmi")`

Both forms produce tag `dmi` with scname `top.foo`. The CCI callback
uses the scname, tag, C++ type name, and source filename to build a
prioritized list of CCI parameter names to check:

| Priority | Parameter | Matches on |
| -------- | --------- | ---------- |
| 3 | `top.foo.dmi.log_level` | hierarchy + feature |
| 2 | `top.foo.my_mod.log_level` | hierarchy + type |
| 2 | `top.foo.my_mod.cpp.log_level` | hierarchy + file |
| 2 | `top.foo.log_level` | hierarchy |
| 2 | `*.foo.dmi.log_level` | wildcard hierarchy + feature |
| 2 | `*.foo.my_mod.log_level` | wildcard + type |
| 2 | `*.foo.my_mod.cpp.log_level` | wildcard + file |
| 2 | `*.foo.log_level` | wildcard hierarchy |
| 1 | `top.dmi.log_level` | parent + feature |
| 1 | `top.my_mod.log_level` | parent + type |
| 1 | `top.my_mod.cpp.log_level` | parent + file |
| 1 | `top.log_level` | parent |
| 0 | `dmi.log_level` | feature alone |
| 0 | `my_mod.log_level` | type alone |
| 0 | `my_mod.cpp.log_level` | file alone |
| 0 | `*.log_level` | wildcard |
| 0 | `log_level` | global default |

At each priority level, the first match wins. Features listed earlier in a
comma-separated tag are checked before later ones.

For a logger with multiple features (e.g. `SC_LOG_HANDLE(D, "dmi,debug")`),
each feature generates its own set of entries. Setting `dmi.log_level=5`
OR `debug.log_level=5` will enable the logger.

## Initialization

Use `scp::LogHandler` (RAII) to initialize and own the logging backend:

```cpp
int sc_main(int argc, char* argv[]) {
    scp::LogHandler handler(scp::LogConfig()
        .logLevel(scp::log::DEBUG)
        .msgTypeFieldWidth(20)
        .logFileName("/tmp/log.txt"));

    // ... simulation ...
    // Logging is shut down automatically when handler goes out of scope
    return 0;
}
```

`LogHandler` owns the spdlog loggers, installs the `sc_report_handler`
callback, and registers the CCI verbosity function. On destruction it
flushes all pending messages and shuts down spdlog.

### LogConfig options

| Option | Method | Default |
| ------ | ------ | ------- |
| Log level | `logLevel(sc_core::sc_verbosity)` | `ALERT` (`scp::log::WARN`) |
| Message type field width | `msgTypeFieldWidth(unsigned)` | 24 |
| Print system time | `printSysTime(bool)` | false |
| Print simulation time | `printSimTime(bool)` | true |
| Print delta cycles | `printDelta(bool)` | false |
| Print severity level | `printSeverity(bool)` | true |
| Colored output | `coloredOutput(bool)` | true |
| Log file name | `logFileName(std::string)` | (none) |
| Filter regex | `logFilterRegex(std::string)` | (none) |
| Async logging | `logAsync(bool)` | true |
| File info from level | `fileInfoFrom(int)` | `SC_INFO` |
| Report only first error | `reportOnlyFirstError(bool)` | false |
| Display name style | `displayNameStyle(scp::DisplayName)` | `AUTO` |

### Display name styles

The `displayNameStyle` option controls what is shown as the message type
in log output:

| Style | Display | Example |
| ----- | ------- | ------- |
| `AUTO` | tag if non-empty, else scname | `dmi,trace` (or `top.prod` if no tag) |
| `TAG` | tag as provided by SC_LOG | `dmi,trace` (or `top.prod` if no tag) |
| `SCNAME` | module hierarchy, falls back to tag | `top.prod` |
| `FEATURES` | always the feature/tag string | `dmi,trace` |
| `FULL` | tag and scname | `dmi,trace [top.prod]` |

### Runtime control

```cpp
scp::set_logging_level(scp::log::TRACE);
scp::log level = scp::get_logging_level();
scp::set_cycle_base(sc_core::sc_time(1, sc_core::SC_NS));
scp::set_display_name_style(scp::DisplayName::FULL);
```

### Dynamic verbosity control

Logger verbosity levels are cached after the first CCI lookup for
performance. To change verbosity during simulation:

```cpp
// Reset all caches — forces re-query of CCI params on next log statement
scp::reset_logging();

// Directly set verbosity for loggers matching a name (scname or feature).
// Bypasses CCI — takes effect immediately, no reset needed.
scp::set_log_level("top.prod", scp::log::CRITICAL);  // silence producer
scp::set_log_level("dmi", scp::log::DEBUG);           // enable dmi feature
```

`set_log_level` matches against the module hierarchy name (scname) and
feature tags. Multiple loggers may match the same name.

### Computed tags

For components whose identity depends on runtime information (e.g.
registers whose name depends on their position in the hierarchy),
declare a string member before the logger and pass it as the tag.
The member initialisation order guarantees the tag string is available
when the handle is constructed.

```cpp
// SC_LOG style:
SC_MODULE(my_device) {
    std::string m_reg_name;
    SC_LOG_HANDLE(reg_h, m_reg_name.c_str());

    SC_CTOR(my_device)
        : m_reg_name(std::string(name()) + ".control_reg")
    {
        SC_NOTE(reg_h) << "register initialized";
    }
};

// SCP style:
class my_register {
    std::string m_log_name;
    SCP_LOGGER((), m_log_name.c_str());

    my_register(const std::string& name)
        : m_log_name(name)
    {
        SCP_TRACE(()) << "constructed";
    }
};
```

## Logger Vectors

For dynamic per-client logging (e.g. TLM multi-ports):

```cpp
// SC_LOG style:
SC_LOG_HANDLE_VECTOR(vec);
SC_LOG_HANDLE_VECTOR_PUSH_BACK(vec, "client0");
SC_LOG_HANDLE_VECTOR_PUSH_BACK(vec, "client1");
SC_NOTE(vec[0]) << "from client 0";

// SCP style:
SCP_LOGGER_VECTOR(vec);
SCP_LOGGER_VECTOR_PUSH_BACK(vec, "client0");
SCP_LOGGER_VECTOR_PUSH_BACK(vec, "client1");
SCP_INFO((vec[0])) << "from client 0";
```

## Thread Safety

Logger macros are not thread safe. `SCP_LOGGER` / `SC_LOG_HANDLE` must be
used within a SystemC module context. Logging macros may be used on separate
threads but must first be used on the SystemC thread within a module context.

Recommended: include a log statement in every `sc_module` constructor:
```cpp
SCP_TRACE(()) << "Constructor";
```

## Utilities

```cpp
std::vector<std::string> scp::get_logging_parameters();
```

Returns all CCI parameter names used for log level lookups.
