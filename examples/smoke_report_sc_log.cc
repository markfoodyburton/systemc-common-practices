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
 * smoke_report_sc_log.cc — identical to smoke_report.cc but uses
 * SystemC 4.0 native SC_LOG macros (SC_INFO, SC_WARN, SC_LOG_HANDLE, etc.)
 * instead of SCP macros.  Must compile and run on both SystemC 3.0
 * (via the adaptation layer) and SystemC 4.0.
 */

#include <scp/tlm_extensions/initiator_id.h>
#include <scp/tlm_extensions/path_trace.h>
#include <scp/cci_report_backend.h>

#include <systemc>
#include <tlm>
#include <cci_configuration>

#include <cstdio>
#include <string>
#include <fstream>
#include <streambuf>
#include <unistd.h>

SC_MODULE (test4) {
    SC_CTOR (test4) {
        SC_INFO() << " .   T4 Logger() 1";
        SC_WARN() << " .   T4 Logger() 1";
        SC_INFO() << " .   T4 Logger() 2";
        SC_WARN() << " .   T4 Logger() 2";
    }
    SC_LOG_HANDLE();
};

SC_MODULE (test3) {
    SC_CTOR (test3) {
        SC_INFO(D) << " .  T3 D Logger \"other\" \"feature.one\"";
        SC_WARN(D) << " .  T3 D Logger \"other\" \"feature.one\"";
        SC_INFO() << " .  T3 Logger ()";
        SC_WARN() << " .  T3 Logger ()";
    }
    SC_LOG_HANDLE(D, "other");
    SC_LOG_HANDLE();
};

SC_MODULE (test2) {
    SC_CTOR (test2) : t31("t3_1"), t32("t3_2"), t4("t4") {
            SC_INFO() << "  T2 Logger()";
            SC_WARN() << "  T2 Logger()";
        }
    SC_LOG_HANDLE();
    test3 t31, t32;
    test4 t4;
};

SC_MODULE (test1) {
    SC_CTOR (test1) : t2("t2") {
            SC_WARN(my_name_logger) << " T1 My.Name typed log";
            SC_INFO() << " T1 Logger()";
            SC_WARN() << " T1 Logger()";

            SC_LOG_HANDLE_VECTOR_PUSH_BACK(vec, "thing1");
            SC_LOG_HANDLE_VECTOR_PUSH_BACK(vec, "thing2");

            SC_INFO(vec[0]) << "Thing1?";
            SC_WARN(vec[0]) << "Thing1?";
            SC_INFO(vec[1]) << "Thing2?";
            SC_WARN(vec[1]) << "Thing2?";
        }
    SC_LOG_HANDLE("something");
    SC_LOG_HANDLE(my_name_logger, "My.Name");
    SC_LOG_HANDLE_VECTOR(vec);
    test2 t2;
};

SC_LOG_HANDLE_STATIC(out_class_logger, "out.class");

class outside_class
{
public:
    outside_class() {
        SC_INFO(out_class_logger)("constructor");
        SC_WARN(out_class_logger)("constructor");
    }
};

SC_MODULE (test) {
    outside_class oc;
    SC_CTOR (test) {
        SC_DEBUG() << "First part";
        scp::tlm_extensions::path_trace ext;
        ext.stamp(this);
        SC_INFO() << ext.to_string();
        ext.reset();

        ext.stamp(this);
        ext.stamp(this);
        ext.stamp(this);

        SC_INFO() << ext.to_string();
        ext.reset();

        SC_DEBUG() << "Second part";
        scp::tlm_extensions::initiator_id mid(0x1234);
        mid = 0x2345;
        mid &= 0xff;
        mid <<= 4;
        uint64_t myint = mid + mid;
        myint += mid;
        if (mid == 0x450) {
            SC_REPORT_INFO("ext test", "Success");
        } else {
            SC_REPORT_INFO("ext test", "Failour");
        }

        SC_REPORT_INFO("SystemC", "Uncached version empty");
        SC_INFO()("FMT String : Cached version default");
        SC_INFO() << "UnCached version feature using SCMOD macro";
        SC_INFO(m_my_logger) << "Cached version using (m_my_logger)";
        SC_INFO(D) << "Cached version with D";
    }

    SC_LOG_HANDLE(m_my_logger, "my_logger");
    SC_LOG_HANDLE();
    SC_LOG_HANDLE(D, "other");
};

int sc_main(int argc, char** argv) {
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);
    cci::cci_originator orig("config");
    broker.set_preset_cci_value("log_level", cci::cci_value(1), orig);
    broker.set_preset_cci_value("top.log_level", cci::cci_value(5), orig);
    broker.set_preset_cci_value("*.t3_1.log_level", cci::cci_value(5), orig);
    broker.set_preset_cci_value("feature.log_level", cci::cci_value(5), orig);

    broker.set_preset_cci_value("out.class.log_level", cci::cci_value(5), orig);
    broker.set_preset_cci_value("other.log_level", cci::cci_value(5), orig);
    broker.set_preset_cci_value("test4.log_level", cci::cci_value(4), orig);
    broker.set_preset_cci_value("thing1.log_level", cci::cci_value(5), orig);
    std::string logfile = "/tmp/scp_smoke_report_sc_log_test." +
                          std::to_string(getpid());
    scp::LogHandler logging_handler(
        scp::LogConfig()
            .logLevel(scp::log::DEBUG) // set log level to debug
            .msgTypeFieldWidth(20)
            .fileInfoFrom(5)
            .logAsync(false)
            .printSimTime(false)
            .displayNameStyle(scp::DisplayName::SCNAME)
            .logFileName(logfile)); // make the msg type column a bit tighter
    SC_INFO() << "Constructing design";
    test toptest("top");
    test1 t1("t1");

    SC_INFO() << "Starting simulation";
    sc_core::sc_start();
    SC_WARN() << "Ending simulation";

#ifdef FMT_SHARED
    std::string fmtstr = "FMT String : Cached version default";
#else
    std::string fmtstr = "";
#endif

    std::string expected =
        R"([    info] [                0 s ]SystemC             : Constructing design
[    info] [                0 s ]out.class           : constructor
[ warning] [                0 s ]out.class           : constructor
[   debug] [                0 s ]top                 : First part
[    info] [                0 s ]top                 : top
[    info] [                0 s ]top                 : top->top->top
[   debug] [                0 s ]top                 : Second part
[    info] [                0 s ]ext test            : Success
[    info] [                0 s ]SystemC             : Uncached version empty
[    info] [                0 s ]top                 : )" +
        fmtstr + R"(
[    info] [                0 s ]top                 : UnCached version feature using SCMOD macro
[    info] [                0 s ]top                 : Cached version using (m_my_logger)
[    info] [                0 s ]top                 : Cached version with D
[    info] [                0 s ]t1.t2.t3_1          :  .  T3 D Logger "other" "feature.one"
[ warning] [                0 s ]t1.t2.t3_1          :  .  T3 D Logger "other" "feature.one"
[    info] [                0 s ]t1.t2.t3_1          :  .  T3 Logger ()
[ warning] [                0 s ]t1.t2.t3_1          :  .  T3 Logger ()
[    info] [                0 s ]t1.t2.t3_2          :  .  T3 D Logger "other" "feature.one"
[ warning] [                0 s ]t1.t2.t3_2          :  .  T3 D Logger "other" "feature.one"
[ warning] [                0 s ]t1.t2.t3_2          :  .  T3 Logger ()
[    info] [                0 s ]t1.t2.t4            :  .   T4 Logger() 1
[ warning] [                0 s ]t1.t2.t4            :  .   T4 Logger() 1
[    info] [                0 s ]t1.t2.t4            :  .   T4 Logger() 2
[ warning] [                0 s ]t1.t2.t4            :  .   T4 Logger() 2
[ warning] [                0 s ]t1.t2               :   T2 Logger()
[ warning] [                0 s ]t1                  :  T1 My.Name typed log
[ warning] [                0 s ]t1                  :  T1 Logger()
[    info] [                0 s ]t1                  : Thing1?
[ warning] [                0 s ]t1                  : Thing1?
[ warning] [                0 s ]t1                  : Thing2?
[    info] [                0 s ]SystemC             : Starting simulation
[ warning] [                0 s ]SystemC             : Ending simulation
)";

    std::ifstream lf(logfile);
    std::string out((std::istreambuf_iterator<char>(lf)),
                    std::istreambuf_iterator<char>());

    std::cout << "out file\n" << out << "\n";
    std::cout << "expected\n" << expected << "\n";
    std::cout << "Number of difference: " << out.compare(expected) << "\n";

    std::remove(logfile.c_str());
    return out.compare(expected);
}
