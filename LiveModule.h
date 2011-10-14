// LiveModule.h

#ifndef _PPBOX_LIVE_WORKER_LIVE_MODULE_H_
#define _PPBOX_LIVE_WORKER_LIVE_MODULE_H_

#include <boost/asio/deadline_timer.hpp>
#include <boost/function.hpp>

namespace ppbox
{
    namespace live_worker
    {

        class LiveInterface;

        class LiveModule
            : public util::daemon::ModuleBase<LiveModule>
        {
        public:
            struct Channel;

            typedef Channel * ChannelHandle;

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
            ChannelHandle start_channel(
                std::string const & url,
                boost::uint16_t tcp_port, 
                boost::uint16_t udp_port, 
                call_back_func const & call_back);

            void stop_channel(
                ChannelHandle handle);

            size_t check_channels(
                 std::vector<ChannelHandle> & failed)
            {
                 return 0;
            }

            void dump_channels();

        private:
            void handle_call_back(
                Channel * channel, 
                boost::system::error_code const & ec);

            void handle_call_back_innner(
                Channel * channel, 
                boost::system::error_code const & ec);

        private:
            static int call_back_hook(
                unsigned int ChannelHandle, 
                unsigned int Msg, 
                unsigned int wParam, 
                unsigned int lParam);

        private:
            enum PeerTypeEnum
            {
                t_client, 
                t_sn,
                t_ssn,
            };

            int peer_type_;
            LiveInterface * live_;
            std::vector<Channel *> channels_;
        };

    } // namespace live_worker
} // namespace ppbox

#endif // _PPBOX_LIVE_WORKER_LIVE_MODULE_H_
