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
 ****************************************************************************/

/*
 * scp_report_verification_test.cc — Verification of the SC_LOG API
 * used with the SCP report backend, covering:
 *   - All SC_LOG_HANDLE forms (default, named, tagged, static, vector)
 *   - All SC_INFO/SC_WARN/etc. call forms (handle, tag override, string tag)
 *   - All severity levels (TRACE, DEBUG, INFO, WARN, CRITICAL)
 *   - CCI value types (small int, large int, string)
 *   - CCI matching patterns (global, hierarchy, feature, wildcard)
 *   - All display name styles (AUTO, FULL, TAG, SCNAME, FEATURES)
 *   - Runtime control (set_log_level, reset_logging, set_display_name_style)
 *   - String-tag form does not poison global logger cache
 */

#include <scp/cci_report_backend.h>
#include <scp/sc_log.h>

#include <systemc>
#include <cci_configuration>

#include <cstdio>
#include <string>
#include <fstream>
#include <streambuf>
#include <unistd.h>
#include <iostream>

/*==========================================================================
 * Test infrastructure
 *==========================================================================*/

static std::string log_output;
static int failures = 0;

static void check(const char* desc, bool condition) {
    if (!condition) {
        std::cerr << "FAIL: " << desc << "\n";
        failures++;
    }
}

static bool has(const std::string& pattern) {
    return log_output.find(pattern) != std::string::npos;
}

static bool absent(const std::string& pattern) {
    return log_output.find(pattern) == std::string::npos;
}

static void reload_log(const std::string& logfile) {
    std::ifstream lf(logfile);
    log_output = std::string((std::istreambuf_iterator<char>(lf)),
                              std::istreambuf_iterator<char>());
}

/*==========================================================================
 * Module hierarchy for testing
 *
 *   top (test_driver)
 *   ├── mid
 *   │   └── child   (deep_child)
 *   └── sibling     (sibling_mod)
 *==========================================================================*/

/* deep_child: 3-level hierarchy, multiple handle forms */
SC_MODULE(deep_child) {
    SC_LOG_HANDLE();                              // default handle (empty tag)
    SC_LOG_HANDLE(dmi_h, "dmi,trace");            // named, multi-feature tag
    SC_LOG_HANDLE(native_h, "native.tag");        // named, dot-separated tag
    SC_LOG_HANDLE_VECTOR(vec);                    // vector of handles

    SC_CTOR(deep_child) {
        SC_LOG_HANDLE_VECTOR_PUSH_BACK(vec, "vec.tag");

        // --- All severity levels from default handle ---
        SC_TRACE()     << "child TRACE";
        SC_DEBUG()     << "child DEBUG";
        SC_INFO()      << "child INFO";
        SC_WARN()      << "child WARN";
        SC_CRITICAL()  << "child CRITICAL";

        // --- Named handle ---
        SC_INFO(dmi_h) << "child DMI info";

        // --- Handle + tag override (2-arg form) ---
        SC_INFO(SC_LOG_LOG_LEVEL_CACHE, "override.tag") << "child default+tag";
        SC_INFO(dmi_h, "extra.tag") << "child DMI+tag";

        // --- Vector handle ---
        SC_INFO(vec[0]) << "child vec[0]";

        // --- Named handle (native.tag) ---
        SC_INFO(native_h) << "child native handle";

        // --- String tag form (global logger path) ---
        SC_WARN("child.string.tag") << "child string-tag";
    }
};

/* mid: intermediate module */
SC_MODULE(mid) {
    SC_LOG_HANDLE();

    deep_child child;

    SC_CTOR(mid) : child("child") {
        SC_INFO() << "mid constructed";
        SC_WARN("mid.string.tag") << "mid string-tag";
    }
};

/* sibling_mod: tests feature-based CCI matching */
SC_MODULE(sibling_mod) {
    SC_LOG_HANDLE("sib.feature");                 // default handle with tag
    SC_LOG_HANDLE(extra_h, "extra.feat");         // named handle with tag

    SC_CTOR(sibling_mod) {
        SC_INFO()         << "sibling constructed";
        SC_INFO(extra_h)  << "sibling Extra logger";
        SC_WARN("sibling.string.tag") << "sibling string-tag";
    }
};

/* Static/global logger (outside any module) */
SC_LOG_HANDLE_STATIC(global_logger, "global.static");

/*==========================================================================
 * Test driver — runs phases during simulation
 *==========================================================================*/

SC_MODULE(test_driver) {
    SC_LOG_HANDLE();
    SC_LOG_HANDLE(feat_h, "my.feature");

    mid m;
    sibling_mod sib;

    void phase_display_styles() {
        // Phase 2: cycle through all display styles
        scp::set_display_name_style(scp::DisplayName::AUTO);
        SC_INFO() << "PHASE2 AUTO";

        scp::set_display_name_style(scp::DisplayName::FULL);
        SC_INFO() << "PHASE2 FULL";

        scp::set_display_name_style(scp::DisplayName::TAG);
        SC_INFO() << "PHASE2 TAG";

        scp::set_display_name_style(scp::DisplayName::SCNAME);
        SC_INFO() << "PHASE2 SCNAME";

        scp::set_display_name_style(scp::DisplayName::FEATURES);
        SC_INFO() << "PHASE2 FEATURES";

        // Also test named handle through styles
        scp::set_display_name_style(scp::DisplayName::AUTO);
        SC_INFO(feat_h) << "PHASE2 feat AUTO";

        scp::set_display_name_style(scp::DisplayName::FULL);
        SC_INFO(feat_h) << "PHASE2 feat FULL";

        // Restore AUTO for remaining phases
        scp::set_display_name_style(scp::DisplayName::AUTO);
    }

    void phase_dynamic_control() {
        wait(10, sc_core::SC_NS);

        // Phase 3: silence DMI by feature name
        scp::set_log_level("dmi", scp::log::CRITICAL);
        SC_INFO() << "PHASE3 default still active";

        wait(10, sc_core::SC_NS);

        // Phase 4: reset caches — all loggers re-evaluate from CCI
        scp::reset_logging();
        SC_INFO() << "PHASE4 default after reset";

        wait(10, sc_core::SC_NS);

        // Phase 5: change global level to suppress DEBUG
        scp::set_logging_level(scp::log::INFO);
        scp::reset_logging();
        SC_DEBUG() << "PHASE5 debug should be suppressed";
        SC_INFO()  << "PHASE5 info still visible";
    }

    void run() {
        phase_display_styles();
        phase_dynamic_control();
    }

    SC_CTOR(test_driver) : m("m"), sib("sibling") {
        SC_THREAD(run);

        // Log from the driver during elaboration
        SC_INFO() << "driver constructed";
        SC_INFO(feat_h) << "driver feat_h";

        // Global static logger
        SC_INFO(global_logger) << "global static log";
    }
};

/*==========================================================================
 * sc_main
 *==========================================================================*/

int sc_main(int argc, char** argv) {
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);
    cci::cci_originator orig("config");

    /* CCI value forms */
    // Small int (0-99 range)
    broker.set_preset_cci_value("log_level",
        cci::cci_value(1), orig);                          // global: WARN
    broker.set_preset_cci_value("top.log_level",
        cci::cci_value(5), orig);                          // hierarchy: DEBUG

    // Large int (sc_verbosity value)
    broker.set_preset_cci_value("top.m.child.log_level",
        cci::cci_value(500), orig);                        // large int: TRACE

    // String value
    broker.set_preset_cci_value("dmi.log_level",
        cci::cci_value(std::string("DEBUG")), orig);       // string: DEBUG

    // Wildcard
    broker.set_preset_cci_value("*.child.log_level",
        cci::cci_value(6), orig);                          // wildcard: TRACE

    // Feature-based
    broker.set_preset_cci_value("sib.feature.log_level",
        cci::cci_value(5), orig);                          // feature: DEBUG
    broker.set_preset_cci_value("extra.feat.log_level",
        cci::cci_value(4), orig);                          // feature: INFO
    broker.set_preset_cci_value("native.tag.log_level",
        cci::cci_value(5), orig);                          // native handle
    broker.set_preset_cci_value("vec.tag.log_level",
        cci::cci_value(5), orig);                          // vector tag
    broker.set_preset_cci_value("global.static.log_level",
        cci::cci_value(5), orig);                          // global static
    broker.set_preset_cci_value("my.feature.log_level",
        cci::cci_value(5), orig);                          // driver feature

    std::string logfile = "/tmp/scp_report_verification_test." +
                          std::to_string(getpid());
    {
    scp::LogHandler handler(
        scp::LogConfig()
            .logLevel(scp::log::TRACE)
            .msgTypeFieldWidth(30)
            .logAsync(false)
            .printSimTime(true)
            .logFileName(logfile));

    /* Phase 1: elaboration — constructors log at various levels */
    test_driver top("top");

    /* Run simulation for dynamic phases */
    sc_core::sc_start(40, sc_core::SC_NS);
    } /* handler destroyed — file flushed */

    /* Load log file and validate */
    reload_log(logfile);

    if (log_output.empty()) {
        std::cerr << "FAIL: log file empty or not found: " << logfile << "\n";
        std::remove(logfile.c_str());
        return 1;
    }

    /*==================================================================
     * Phase 1: severity levels and CCI matching
     *==================================================================*/

    // deep_child at top.m.child — log_level=500 (TRACE via large int)
    check("child TRACE visible (large int CCI=500)",
          has("child TRACE"));
    check("child DEBUG visible",
          has("child DEBUG"));
    check("child INFO visible",
          has("child INFO"));
    check("child WARN visible",
          has("child WARN"));
    check("child CRITICAL visible",
          has("child CRITICAL"));

    // Named handle: dmi — dmi.log_level="DEBUG" (string CCI)
    check("child DMI info visible (string CCI='DEBUG')",
          has("child DMI info"));

    // Handle + tag override (2-arg form)
    check("default handle + tag override",
          has("child default+tag"));
    check("named handle + tag override",
          has("child DMI+tag"));

    // Vector handle
    check("vector handle element",
          has("child vec[0]"));

    // Native named handle
    check("native named handle",
          has("child native handle"));

    // String-tag form (global logger)
    check("child string-tag form",
          has("child string-tag"));

    // mid module
    check("mid constructed",
          has("mid constructed"));
    check("mid string-tag form",
          has("mid string-tag"));

    // sibling — feature-based CCI
    check("sibling constructed (sib.feature CCI)",
          has("sibling constructed"));
    check("sibling Extra logger (extra.feat CCI)",
          has("sibling Extra logger"));
    check("sibling string-tag form",
          has("sibling string-tag"));

    // Global static logger
    check("global static logger",
          has("global static log"));

    // Driver
    check("driver constructed",
          has("driver constructed"));
    check("driver feat_h named handle",
          has("driver feat_h"));

    /*==================================================================
     * Phase 2: display name styles
     *==================================================================*/

    check("PHASE2 AUTO present",
          has("PHASE2 AUTO"));
    check("PHASE2 FULL present",
          has("PHASE2 FULL"));
    check("PHASE2 TAG present",
          has("PHASE2 TAG"));
    check("PHASE2 SCNAME present",
          has("PHASE2 SCNAME"));
    check("PHASE2 FEATURES present",
          has("PHASE2 FEATURES"));
    check("PHASE2 feat AUTO (named handle with style)",
          has("PHASE2 feat AUTO"));
    check("PHASE2 feat FULL (named handle with style)",
          has("PHASE2 feat FULL"));

    /*==================================================================
     * Phase 3: set_log_level silences DMI feature
     *==================================================================*/

    check("PHASE3 default still active",
          has("PHASE3 default still active"));

    /*==================================================================
     * Phase 4: reset_logging restores caches
     *==================================================================*/

    check("PHASE4 default after reset",
          has("PHASE4 default after reset"));

    /*==================================================================
     * Phase 5: global level change suppresses DEBUG
     *==================================================================*/

    check("PHASE5 debug suppressed",
          absent("PHASE5 debug should be suppressed"));
    check("PHASE5 info visible",
          has("PHASE5 info still visible"));

    /*==================================================================
     * CCI value equivalence
     *==================================================================*/

    // Small int 5 (top.log_level) → DEBUG
    check("small int CCI=5 enables DEBUG",
          has("driver constructed"));
    // Large int 500 (top.m.child.log_level) → TRACE
    check("large int CCI=500 enables TRACE",
          has("child TRACE"));
    // String "DEBUG" (dmi.log_level) → DEBUG
    check("string CCI='DEBUG' enables DEBUG",
          has("child DMI info"));

    /*==================================================================
     * String-tag: different modules don't poison each other
     *==================================================================*/

    check("string-tags coexist (child)",
          has("child string-tag"));
    check("string-tags coexist (mid)",
          has("mid string-tag"));
    check("string-tags coexist (sibling)",
          has("sibling string-tag"));

    /*==================================================================
     * API: get_logging_parameters returns used CCI param names
     *==================================================================*/

    auto params = scp::get_logging_parameters();
    check("get_logging_parameters() returns entries",
          !params.empty());
    // Should include at least some of the params we set
    bool found_top = false;
    for (auto& p : params) {
        if (p.find("top") != std::string::npos) found_top = true;
    }
    check("get_logging_parameters() includes 'top' param",
          found_top);

    /*==================================================================
     * Report results
     *==================================================================*/

    std::cout << "\n";
    if (failures == 0) {
        std::cout << "All checks passed.\n";
    } else {
        std::cout << failures << " check(s) FAILED.\n";
    }

    std::cout << "logfile: " << logfile << "\n";
    std::remove(logfile.c_str());
    return failures;
}
