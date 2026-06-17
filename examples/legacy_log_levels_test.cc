/*****************************************************************************
  legacy_log_levels_test.cc -- back-compat check for the scp::log constants.

  After the SC_LOG redesign the level *type* is sc_core::sc_verbosity and
  scp::log is a namespace of named constants.  qbox (and other downstream
  code) still spells levels as scp::log::INFO, scp::log::TRACE,
  scp::log::DBGTRACE, scp::log::WARNING, ...  This test pins down that every
  legacy name still compiles and resolves to the expected verbosity value, so
  the adaptation layer keeps those call sites working unchanged.
*****************************************************************************/

#include <scp/cci_report_backend.h>
#include <scp/sc_log.h>
#include <systemc>
#include <iostream>
#include <string>
#include <type_traits>

/* --- The level type is sc_core::sc_verbosity (no separate enum). --- */
static_assert(std::is_same<decltype(scp::log::INFO),
                           const sc_core::sc_verbosity>::value,
              "scp::log:: constants must be sc_core::sc_verbosity");

/* --- Current names map onto the 100-step verbosity scale. --- */
static_assert(scp::log::NONE == sc_core::SC_NONE, "NONE");
static_assert(scp::log::CRITICAL == sc_core::SC_LOW, "CRITICAL=100");
static_assert(scp::log::ALERT == sc_core::SC_MEDIUM, "ALERT=200");
static_assert(scp::log::NOTE == sc_core::SC_HIGH, "NOTE=300");
static_assert(scp::log::DETAIL == sc_core::SC_FULL, "DETAIL=400");
static_assert(scp::log::INTERNAL == sc_core::SC_DEBUG, "INTERNAL=500");
static_assert(scp::log::UNSET == sc_core::SC_UNSET, "UNSET");

/* --- Legacy SCP names (the original enum: NONE, FATAL, ERROR, WARNING,
       INFO, DEBUG, TRACE, TRACEALL, DBGTRACE) remain available. --- */
static_assert(scp::log::FATAL == sc_core::SC_LOW, "FATAL->CRITICAL");
static_assert(scp::log::ERROR == sc_core::SC_LOW, "ERROR->CRITICAL");
static_assert(scp::log::WARN == sc_core::SC_MEDIUM, "WARN->ALERT");
static_assert(scp::log::WARNING == sc_core::SC_MEDIUM, "WARNING->ALERT");
static_assert(scp::log::INFO == sc_core::SC_HIGH, "INFO->NOTE");
static_assert(scp::log::DEBUG == sc_core::SC_FULL, "DEBUG->DETAIL");
static_assert(scp::log::TRACE == sc_core::SC_DEBUG, "TRACE->INTERNAL");
static_assert(scp::log::TRACEALL == sc_core::SC_DEBUG, "TRACEALL->INTERNAL");
static_assert(scp::log::DBGTRACE == sc_core::SC_DEBUG, "DBGTRACE->INTERNAL");

static int failures = 0;
static void expect(const char* what, sc_core::sc_verbosity got,
                   sc_core::sc_verbosity want) {
    if (got != want) {
        std::cerr << "FAIL: " << what << " got " << static_cast<int>(got)
                  << " want " << static_cast<int>(want) << "\n";
        ++failures;
    }
}

int sc_main(int, char*[]) {
    /* String parsing is an SCP-layer convenience (scp::as_log); SystemC core
     * deliberately ships no level<->text mapping.  Canonical names: */
    expect("as_log(NONE)", scp::as_log("NONE"), sc_core::SC_NONE);
    expect("as_log(CRITICAL)", scp::as_log("CRITICAL"), sc_core::SC_LOW);
    expect("as_log(ALERT)", scp::as_log("ALERT"), sc_core::SC_MEDIUM);
    expect("as_log(NOTE)", scp::as_log("NOTE"), sc_core::SC_HIGH);
    expect("as_log(DETAIL)", scp::as_log("DETAIL"), sc_core::SC_FULL);
    expect("as_log(INTERNAL)", scp::as_log("INTERNAL"), sc_core::SC_DEBUG);
    /* Legacy spellings resolve too (same scale): */
    expect("as_log(FATAL)", scp::as_log("FATAL"), sc_core::SC_LOW);
    expect("as_log(ERROR)", scp::as_log("ERROR"), sc_core::SC_LOW);
    expect("as_log(WARN)", scp::as_log("WARN"), sc_core::SC_MEDIUM);
    expect("as_log(WARNING)", scp::as_log("WARNING"), sc_core::SC_MEDIUM);
    expect("as_log(INFO)", scp::as_log("INFO"), sc_core::SC_HIGH);
    expect("as_log(DEBUG)", scp::as_log("DEBUG"), sc_core::SC_FULL);
    expect("as_log(TRACE)", scp::as_log("TRACE"), sc_core::SC_DEBUG);
    expect("as_log(TRACEALL)", scp::as_log("TRACEALL"), sc_core::SC_DEBUG);
    expect("as_log(DBGTRACE)", scp::as_log("DBGTRACE"), sc_core::SC_DEBUG);
    /* level_name() round-trips the canonical names: */
    expect("name(CRITICAL)", scp::as_log(scp::level_name(sc_core::SC_LOW)),
           sc_core::SC_LOW);
    expect("name(INTERNAL)", scp::as_log(scp::level_name(sc_core::SC_DEBUG)),
           sc_core::SC_DEBUG);

    if (failures) {
        std::cerr << failures << " legacy-level check(s) FAILED.\n";
        return 1;
    }
    std::cout << "legacy scp::log levels: all checks passed.\n";
    return 0;
}
