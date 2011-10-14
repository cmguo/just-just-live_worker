// LiveModuleProxy.h

#ifndef _PPBOX_LIVE_WORKER_LIVE_MODULE_PROXY_H_
#define _PPBOX_LIVE_WORKER_LIVE_MODULE_PROXY_H_

#ifdef PPBOX_LIVE_WORKER_MULTI_PROCESS
#  ifdef BOOST_WINDOWS_API
#    undef PPBOX_LIVE_WORKER_MULTI_PROCESS
#  endif
#endif

#ifndef PPBOX_LIVE_WORKER_MULTI_PROCESS
#  include "ppbox/live_worker/LiveModule.h"
#endif

namespace ppbox
{
    namespace live_worker
    {

#ifndef PPBOX_LIVE_WORKER_MULTI_PROCESS

        typedef LiveModule LiveModuleProxy;

#else
        class LiveModuleProxy
            : public util::daemon::ModuleBase<LiveModuleProxy>
        {
        public:
            struct Channel;

            typedef Channel * ChannelHandle;

            typedef boost::function<void (
                boost::system::error_code const &, 
                std::string const &)> call_back_func;

        public:
            LiveModuleProxy(
                util::daemon::Daemon & daemon);

            ~LiveModuleProxy();

        public:
            virtual boost::system::error_code startup();

            virtual void shutdown();

        public:
            ChannelHandle start_channel(
                std::string const & url,
                boost::uint16_t tcp_port, 
                boost::uint16_t udp_port, 
                call_back_func const & call_back);

            void stop_channel(
                ChannelHandle handle);

            void dump_channels();

            size_t check_channels(
                 std::vector<ChannelHandle> & failed);

        private:
            void handle_start_channel(
                Channel * channel, 
                boost::system::error_code const & ec, 
                std::string const & url);

            void handle_stop_channel(
                util::daemon::Daemon & daemon, 
                Channel * channel, 
                boost::system::error_code const & ec, 
                std::string const & msg);

        private:
            std::vector<Channel *> channels_;
        };

#endif

    } // namespace live_worker
} // namespace ppbox

#endif // _PPBOX_LIVE_WORKER_LIVE_MODULE_PROXY_H_
