# Porting an old SCP project to new SystemC (SC_LOG)

A short checklist. Most existing code keeps compiling unchanged — the `SCP_*`
macros and the `scp::log::*` names are preserved for backward compatibility.
Do these steps only when you want to move to the native SC_LOG API.

See `README.md` for the full API. This file is just the "what do I change" list.

## 1. Nothing is mandatory

The `SCP_*` macros (`SCP_INFO`, `SCP_DEBUG`, `SCP_WARN`, `SCP_FATAL`, ...) and
the `scp::log::*` constants still exist and still work. A project can build
against new SystemC with **zero source edits** — the SCP layer absorbs the
rename. The steps below are an optional clean-up, not a requirement.

## 2. Rename the macros (optional clean-up)

If you want to drop the `SCP_` prefix and use SC_LOG directly:

| Old SCP macro          | New SC_LOG macro | Level (sc_verbosity) |
| ---------------------- | ---------------- | -------------------- |
| `SCP_FATAL` / `SCP_ERR` / `SCP_CRITICAL` | `SC_CRITICAL` | `SC_LOW` (100) |
| `SCP_WARN`             | `SC_ALERT`       | `SC_MEDIUM` (200) |
| `SCP_INFO`             | `SC_NOTE`        | `SC_HIGH` (300) |
| `SCP_DEBUG`            | `SC_DETAIL`      | `SC_FULL` (400) |
| `SCP_TRACE` / `SCP_TRACEALL` | `SC_INTERNAL` | `SC_DEBUG` (500) |

Logger declarations:

| Old                    | New |
| ---------------------- | --- |
| `SCP_LOGGER(())`       | `SC_LOG_HANDLE()` |
| `SCP_LOGGER((D), "tag")` | `SC_LOG_HANDLE(D, "tag")` |
| `SCP_LOGGER((D), "a", "b")` | `SC_LOG_HANDLE(D, "a,b")` |

Call sites change argument style too: `SCP_INFO((D))` → `SC_NOTE(D)`,
`SCP_INFO(())` → `SC_NOTE()`.

## 3. Remove redundant loggers

On **new SystemC**, every `sc_module` already owns a default logging handle.
An explicit `SC_LOG_HANDLE();` (or the old `SCP_LOGGER(())`) inside a module
just shadows the built-in one — it compiles, but it is a redundant member.

When you target new SystemC only, **delete the parameter-less default handle
declarations** and use `SC_NOTE() << ...` directly. Keep only *named*
handles (`SC_LOG_HANDLE(D, "dmi")`) and handles with tags — those still add
value.

> Portability note: if the same code must still build on old SystemC, keep
> `SCP_LOGGER(())` (it makes its own cache variable and never collides with the
> module's built-in handle on either version).

## 4. Check your FATAL / ERROR call sites — this is the real gotcha

`SCP_FATAL`, `SCP_ERR`, `SCP_CRITICAL` (and the new `SC_CRITICAL`) are
**logging only**. Per the SC_LOG standard, logging "shall not throw an
exception or abort the program." They do **not** call `abort()`, `throw`, or
`sc_stop()`.

If your old code relied on `SCP_FATAL` to actually stop the run (control flow,
not just a message), that side effect is gone. Audit every `SCP_FATAL` /
`SCP_ERR` and decide:

- It was only a loud log line → leave it (now `SC_CRITICAL`), fine.
- It was meant to terminate / unwind → replace it with the real reporting call:

```cpp
SC_REPORT_FATAL("msg_type", "message");   // aborts / unwinds as before
SC_REPORT_ERROR("msg_type", "message");   // raises an error per the handler
```

This is the one change that can alter program behaviour, so do it deliberately.

## 5. Level names changed in the output

Output labels are now `C` / `A` / `N` / `D` / `I` on the console
(CRITICAL / ALERT / NOTE / DETAIL / INTERNAL) and the full word in the log
file. If you have golden/expected output files, regenerate them.

CCI `log_level` still accepts the same small-int forms and the legacy strings
(`"INFO"`, `"DEBUG"`, `"TRACE"`, `"WARNING"`, ...), so configuration files do
not need to change. `"DEBUG"` now resolves to the DETAIL level (400).

One semantic shift: `CRITICAL` moved from verbosity 0 to 100. At verbosity 0
(`SC_NONE`) nothing is emitted; raise the verbosity to `SC_LOW` (100) or above
to see CRITICAL messages.
