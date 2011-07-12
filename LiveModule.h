// LiveModule.h

#ifndef _PPBOX_LIVE_WORKER_LIVE_MODULE_H_
#define _PPBOX_LIVE_WORKER_LIVE_MODULE_H_

#include <framework/timer/TimeTraits.h>

#include <boost/function.hpp>

namespace ppbox
{
    namespace live_worker
    {

        class LiveInterface;

        struct LiveChannel;

        struct ChannelHandle
        {
            ChannelHandle(
                LiveChannel * channel = NULL)
                : channel(channel)
                , cancel_token(0)
            {
            }

            LiveChannel * channel;
            size_t cancel_token;
        };

        class LiveModule
            : public util::daemon::ModuleBase<LiveModule>
        {
        public:
            typedef boost::function<void (
                boost::system::error_code const &, 
                std::string const &)> call_back_func;

        public:
            LiveModule(
                util::daemon::Daemon & daemon);

            ~LiveModule();

        public:
            virtual boost::system::error_code startup();

            virtual void shutdown();

        public:
            void set_max_parallel(
                size_t max_parallel);

            ChannelHandle start_channel(
                std::string const & url, 
                call_back_func const & call_back);

            void stop_channel(
                ChannelHandle & handle);

        private:
            void handle_timer(
                boost::system::error_code const & ec);

            void handle_call_back(
                LiveChannel * channel, 
                boost::system::error_code const & ec);

            void handle_call_back_innner(
                LiveChannel * channel, 
                boost::system::error_code const & ec);

            void check_parallel();

            void dump_channels();

        private:
            static int call_back_hook(
                unsigned int ChannelHandle, 
                unsigned int Msg, 
                unsigned int wParam, 
                unsigned int lParam);

        private:
            static boost::uint16_t const udp_port = 3000;
            static boost::uint16_t const tcp_port = 4000;

        private:
            LiveInterface * live_;
            std::vector<LiveChannel *> channels_;
            size_t max_parallel_;
            clock_timer timer_;
        };

    } // namespace live_worker
} // namespace ppbox

#endif // _PPBOX_LIVE_WORKER_LIVE_MODULE_H_
