// LiveModule.cpp

#include "ppbox/live_worker/Common.h"
#include "ppbox/live_worker/LiveModule.h"
#include "ppbox/live_worker/LiveInterface.h"

#include <live/live/Name.h>

#include <framework/system/LogicError.h>
#include <framework/string/Format.h>
#include <framework/logger/StreamRecord.h>
#include <framework/logger/LoggerSection.h>
using namespace framework::string;
using namespace framework::logger;
using namespace framework::system;

#include <boost/bind.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/algorithm/string/predicate.hpp>
using namespace boost::system;

FRAMEWORK_LOGGER_DECLARE_MODULE_LEVEL("LiveModule", 0)

namespace ppbox
{
    namespace live_worker
    {

        struct LiveModule::Channel
        {
            Channel(
                LiveModule * module, 
                void * handle, 
                LiveModule::call_back_func const & call_back) 
                : module(module)
                , handle(handle)
                , call_back(call_back)
            {
            }

            LiveModule * module;
            void * handle;
            LiveModule::call_back_func call_back;
        };

        LiveModule::LiveModule(
            util::daemon::Daemon & daemon)
            : util::daemon::ModuleBase<LiveModule>(daemon, "LiveModule")
            , peer_type_(t_client)
        {
            config().register_module("LiveModule") 
                << CONFIG_PARAM_NAME_RDONLY("peer_type", peer_type_);
            if (peer_type_ < t_client || peer_type_ > t_ssn )
            {
                peer_type_ = t_sn;
            }
            

            LOG_DEBUG("[LiveModule] peer_type:"<<peer_type_);

            live_ = new LiveInterface();
        }

        LiveModule::~LiveModule()
        {
            delete live_;
        }

        error_code LiveModule::startup()
        {
            error_code ec = 
                live_->load(live::name_string());

            if (!ec && !live_->startup(peer_type_))
                ec = logic_error::failed_some;
            return ec;
        }

        void LiveModule::shutdown()
        {
            LOG_DEBUG("[shutdown] beg stop kernel");
            live_->cleanup();
            LOG_DEBUG("[shutdown] end stop kernel");
        }

        LiveModule::ChannelHandle LiveModule::start_channel( 
            std::string const & url, 
            boost::uint16_t tcp_port, 
            boost::uint16_t udp_port, 
            call_back_func const & call_back)
        {
            void * handle = live_->start_channel(
                (std::string("synacast:/") + url).c_str(), tcp_port, udp_port);
            if (handle == NULL) {
                return NULL; // Failed.
            }
            Channel * channel = new Channel(this, handle, call_back);
            live_->set_channel_callback(channel->handle, LiveModule::call_back_hook, (unsigned long)(channel));
            LOG_INFO("[start_channel] channel " << (void *)channel);
            return channel;
        }

        void LiveModule::stop_channel(
            ChannelHandle handle)
        {
            Channel * channel = (Channel *)handle;
            LOG_INFO("[stop_channel] channel " << (void *)channel);
            live_->stop_channel(channel->handle);
            channel->handle = NULL;
            if (channel->call_back.empty()) {
                delete channel;
            } else {
                call_back_func call_back;
                call_back.swap(channel->call_back);
                io_svc().post(boost::bind(call_back, 
                    boost::asio::error::operation_aborted, std::string()));
            }
        }

        void LiveModule::handle_call_back(
            Channel * channel, 
            error_code const & ec)
        {
            io_svc().post(boost::bind(
                &LiveModule::handle_call_back_innner, this, channel, ec));
        }

        void LiveModule::handle_call_back_innner(
            Channel * channel, 
            error_code const & ec)
        {
            LOG_INFO("call_back channel " << (void *)channel);
            if (channel->call_back.empty()) {
                delete channel;
                return;
            }
            std::string url;
            if (!ec) {
                CCoreStatus cs;
                live_->get_channel_status(channel->handle, cs);
                url = "http://127.0.0.1:" + format(cs.m_uMediaListenPort) + "/1.asf";
            }
            call_back_func call_back;
            call_back.swap(channel->call_back);
            io_svc().post(boost::bind(call_back, ec, url));
        }

        void LiveModule::dump_channels()
        {
            for (size_t i = 0; i < channels_.size(); ++i) {
                CCoreStatus cs;
                live_->get_channel_status(channels_[i]->handle, cs);
                LOG_TRACE("dump_channels [%d]  p: %d%%  t: %ds  d: %dk  u: %dk  c: %d   s: %d   t: %d"
                    % cs.m_uMediaListenPort
                    % cs.m_BufferPercent
                    % (cs.m_BufferTime / 1000)
                    % (cs.m_DownloadSpeed / 1024)
                    % (cs.m_UploadSpeed / 1024)
                    % cs.m_ConnectionCountt
                    % cs.m_PendingPeerCount
                    % cs.m_TotalPeerCount);
            }
        }

        int LiveModule::call_back_hook(
            unsigned int ChannelHandle, 
            unsigned int Msg, 
            unsigned int wParam, 
            unsigned int lParam)
        {
            Channel * channel = (Channel *)ChannelHandle;
            channel->module->handle_call_back(channel, 
                Msg == UM_LIVEMSG_PLAY ? error_code() : logic_error::failed_some);
            return 0;
        }

    } // namespace live_worker
} // namespace ppbox
