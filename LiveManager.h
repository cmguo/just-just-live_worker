// LiveManager.h

#ifndef _JUST_LIVE_WORKER_LIVE_MANAGER_H_
#define _JUST_LIVE_WORKER_LIVE_MANAGER_H_

#include <framework/timer/TimeTraits.h>

#include <boost/function.hpp>

namespace just
{
    namespace live_worker
    {

#ifndef JUST_LIVE_WORKER_MULTI_PROCESS
        class LiveModule;
        typedef LiveModule LiveModuleProxy;
#else
        class LiveModuleProxy;
#endif

        class LiveManager
            : public util::daemon::ModuleBase<LiveManager>
        {
        public:
            struct Channel;

            struct ChannelHandle
            {
                ChannelHandle(
                    Channel * channel = NULL)
                    : channel(channel)
                    , cancel_token(0)
                {
                }

                Channel * channel;
                size_t cancel_token;
            };

            typedef boost::function<void (
                boost::system::error_code const &, 
                std::string const &)> call_back_func;

        public:
            LiveManager(
                util::daemon::Daemon & daemon);

            ~LiveManager();

        public:
            virtual boost::system::error_code startup();

            virtual void shutdown();

        public:
            void set_max_parallel(
                size_t max_parallel);

            ChannelHandle start_channel(
                std::string const & url, 
                call_back_func const & call_back);

            ChannelHandle start_channel(
                std::string const & url, 
                boost::uint16_t tcp_port, 
                boost::uint16_t udp_port, 
                call_back_func const & call_back);

            void stop_channel(
                ChannelHandle & handle);

        private:
            void handle_timer(
                boost::system::error_code const & ec);

            void handle_start_channel(
                Channel * channel, 
                boost::system::error_code const & ec, 
                std::string const & url);

            void response_channel(
                Channel * channel, 
                boost::system::error_code const & ec, 
                std::string const & url);

            void stop_channel(
                Channel *& channel);

            void check_parallel();

        private:
            static boost::uint16_t const udp_port = 0;
            static boost::uint16_t const tcp_port = 0;

        private:
            LiveModuleProxy & live_module_;
            std::vector<Channel *> channels_;
            size_t max_parallel_;
            clock_timer timer_;
        };

    } // namespace live_worker
} // namespace just

#endif // _JUST_LIVE_WORKER_LIVE_MANAGER_H_
