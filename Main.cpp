// Main.cpp

#include "just/live_worker/Common.h"
#include "just/live_worker/LiveProxy.h"
#ifdef JUST_LIVE_WORKER_WITH_SSN_MANAGER
#  include "just/live_worker/SSNManageModule.h"
#endif
#include "just/live_worker/Version.h"

//#include <just/common/ConfigMgr.h>
#include <just/common/Debuger.h>
#include <just/common/PortManager.h>

#include <framework/logger/Logger.h>
#include <framework/process/Process.h>
#include <framework/process/SignalHandler.h>

#include <boost/bind.hpp>

#ifndef _LIB

int main(int argc, char * argv[])
{
    util::daemon::Daemon my_daemon("live_worker.conf");
    char const * default_argv[] = {
        "++framework.logger.Stream.0.file=$LOG/live_worker.log", 
        "++framework.logger.Stream.0.append=true", 
        "++framework.logger.Stream.0.roll=true", 
        "++framework.logger.Stream.0.level=5", 
        "++framework.logger.Stream.0.size=102400", 
    };
    my_daemon.parse_cmdline(sizeof(default_argv) / sizeof(default_argv[0]), default_argv);
    my_daemon.parse_cmdline(argc, (char const **)argv);

    framework::process::SignalHandler sig_handler(
        framework::process::Signal::sig_int, 
        boost::bind(&util::daemon::Daemon::post_stop, &my_daemon), true);

    framework::logger::load_config(my_daemon.config());

    just::common::log_versions();

    just::common::CommonModule & common = 
        util::daemon::use_module<just::common::CommonModule>(my_daemon, "LiveWorker");
    common.set_version(just::live_worker::version());

    util::daemon::use_module<just::live_worker::LiveProxy>(my_daemon);

    // SNManager module
#ifdef JUST_LIVE_WORKER_WITH_SSN_MANAGER
    if (atoi(worker_type.c_str()) != 0)
    {
        util::daemon::use_module<just::live_worker::SSNManageModule>(my_daemon);
    }
#endif

    //util::daemon::use_module<just::common::ConfigMgr>(my_daemon);
    util::daemon::use_module<just::common::Debuger>(my_daemon);
    util::daemon::use_module<just::common::PortManager>(my_daemon);

    my_daemon.start(framework::process::notify_wait);

    return 0;
}

#endif
