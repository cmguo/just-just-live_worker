// Main.cpp

#include "ppbox/live_worker/Common.h"
#include "ppbox/live_worker/LiveProxy.h"
#ifdef PPBOX_LIVE_WORKER_WITH_SSN_MANAGER
#  include "ppbox/live_worker/SSNManageModule.h"
#endif
#include "ppbox/live_worker/Version.h"

//#include <ppbox/common/ConfigMgr.h>
#include <ppbox/common/Debuger.h>
#include <ppbox/common/PortManager.h>

#include <framework/logger/Logger.h>
#include <framework/process/Process.h>
#include <framework/process/SignalHandler.h>

#include <boost/bind.hpp>

#ifndef _LIB

int main(int argc, char * argv[])
{
    util::daemon::Daemon my_daemon("live_worker.conf");
    char const * default_argv[] = {
        "++Logger.stream_count=1", 
        "++Logger.ResolverService=1", 
        "++LogStream0.file=$LOG/live_worker.log", 
        "++LogStream0.append=true", 
        "++LogStream0.roll=true", 
        "++LogStream0.level=5", 
        "++LogStream0.size=102400", 
    };
    my_daemon.parse_cmdline(sizeof(default_argv) / sizeof(default_argv[0]), default_argv);
    my_daemon.parse_cmdline(argc, (char const **)argv);

    framework::process::SignalHandler sig_handler(
        framework::process::Signal::sig_int, 
        boost::bind(&util::daemon::Daemon::post_stop, &my_daemon), true);

    framework::logger::load_config(my_daemon.config());

    ppbox::common::log_versions();

    ppbox::common::CommonModule & common = 
        util::daemon::use_module<ppbox::common::CommonModule>(my_daemon, "LiveWorker");
    common.set_version(ppbox::live_worker::version());

    util::daemon::use_module<ppbox::live_worker::LiveProxy>(my_daemon);

    // SNManager module
#ifdef PPBOX_LIVE_WORKER_WITH_SSN_MANAGER
    if (atoi(worker_type.c_str()) != 0)
    {
        util::daemon::use_module<ppbox::live_worker::SSNManageModule>(my_daemon);
    }
#endif

    //util::daemon::use_module<ppbox::common::ConfigMgr>(my_daemon);
    util::daemon::use_module<ppbox::common::Debuger>(my_daemon);
    util::daemon::use_module<ppbox::common::PortManager>(my_daemon);

    my_daemon.start(framework::this_process::notify_wait);

    return 0;
}

#endif
