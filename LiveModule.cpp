// LiveModule.cpp

#include "ppbox/live_worker/Common.h"
#include "ppbox/live_worker/LiveModule.h"
#include "ppbox/live_worker/LiveInterface.h"

#include <live/live/Name.h>

#include <util/protocol/pptv/Base64.h>
using namespace util::protocol;

#include <framework/system/LogicError.h>
#include <framework/string/Format.h>
#include <framework/string/Slice.h>
#include <framework/logger/LoggerFormatRecord.h>
#include <framework/logger/LoggerStreamRecord.h>
#include <framework/logger/LoggerSection.h>
using namespace framework::string;
using namespace framework::logger;
using namespace framework::system;

#include <boost/bind.hpp>
#include <boost/asio/io_service.hpp>
using namespace boost::system;

FRAMEWORK_LOGGER_DECLARE_MODULE_LEVEL("LiveModule", 0)

static const char PPBOX_LIVE_KEY[] = "pplive";

namespace ppbox
{
    namespace live_worker
    {

        struct LiveChannel
        {
            LiveChannel(
                LiveModule * module) 
                : module(module)
                , nref(0)
                , expire(0)
                , handle(NULL)
                , playing(false)
            {
            }

            LiveModule * module;
            std::string url;
            std::string rid;
            boost::uint32_t nref;
            boost::uint32_t expire;
            void * handle;
            bool playing;
            std::string url2;
            std::vector<LiveModule::call_back_func> call_backs;
        };

        LiveModule::LiveModule(
            util::daemon::Daemon & daemon)
            : util::daemon::ModuleBase<LiveModule>(daemon, "LiveModule")
            , max_parallel_(1)
            , timer_(io_svc())
        {
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
            if (!ec && !live_->startup())
                ec = logic_error::failed_some;
            if (!ec) {
                timer_.expires_from_now(Duration::seconds(1));
                timer_.async_wait(boost::bind(&LiveModule::handle_timer, this, _1));
            }
            return ec;
        }

        void LiveModule::shutdown()
        {
            for (size_t i = 0; i < channels_.size(); ++i) {
                LOG_S(Logger::kLevelEvent, "[shutdown] delete channel " << (void *)channels_[i]);
                if (!channels_[i]->playing) {
                    handle_call_back_innner(channels_[i], boost::asio::error::operation_aborted);
                }
                live_->stop_channel(channels_[i]->handle);
                delete channels_[i];
                channels_[i] = NULL;
            }
            LOG_S(Logger::kLevelDebug, "[shutdown] beg stop kernel");
            live_->cleanup();
            LOG_S(Logger::kLevelDebug, "[shutdown] end stop kernel");
            error_code ec;
            timer_.cancel(ec);
        }

        void LiveModule::set_max_parallel(
            size_t max_parallel)
        {
            if (max_parallel_ != max_parallel) {
                max_parallel_ = max_parallel;
                check_parallel();
            }
        }

        ChannelHandle LiveModule::start_channel(
            std::string const & url, 
            call_back_func const & call_back)
        {
            std::string url_decode = pptv::base64_decode(url.substr(1), PPBOX_LIVE_KEY);
            std::string rid;
            if (!url_decode.empty()) {
                map_find(url_decode, "channel", rid, "&");
            }
            if (rid.empty()) {
                io_svc().post(
                    boost::bind(call_back, logic_error::failed_some, std::string()));
                return ChannelHandle(NULL);
            }
            LiveChannel * channel = NULL;
            for (size_t i = 0; i < channels_.size(); ++i) {
                if (channels_[i]->rid == rid) {
                    channel = channels_[i];
                    channel->expire = 0;
                    LOG_S(Logger::kLevelEvent, "[start_channel] old channel: " << (void *)channel);
                    break;
                }
            }
            if (channel == NULL) {
                channel = new LiveChannel(this);
                channel->url = url;
                channel->rid = rid;
                LOG_S(Logger::kLevelEvent, "[start_channel] new channel: " << (void *)channel);
                channel->handle = 
                    live_->start_channel((std::string("synacast:/") + url).c_str(), tcp_port, udp_port);
                if (channel->handle == NULL) {
                    io_svc().post(
                        boost::bind(call_back, logic_error::failed_some, channel->url2));
                    delete channel;
                    return ChannelHandle(NULL);
                }
                live_->set_channel_callback(channel->handle, LiveModule::call_back_hook, (unsigned long)(channel));
                channels_.push_back(channel);
            }
            ++channel->nref;
            ChannelHandle handle(channel);
            if (channel->playing) {
                io_svc().post(
                    boost::bind(call_back, error_code(), channel->url2));
            } else {
                channel->call_backs.push_back(call_back);
                handle.cancel_token = channel->call_backs.size();
            }
            check_parallel();
            return handle;
        }

        void LiveModule::stop_channel(
            ChannelHandle & handle)
        {
            LiveChannel * channel = handle.channel;
            handle.channel = NULL;
            if (channel == NULL)
                return;
            LOG_S(Logger::kLevelEvent, "[stop_channel] channel: " << (void *)channel);
            if (handle.cancel_token && handle.cancel_token < channel->call_backs.size() + 1) {
                call_back_func call_back;
                call_back.swap(channel->call_backs[handle.cancel_token - 1]);
                io_svc().post(boost::bind(
                    call_back, boost::asio::error::operation_aborted, std::string()));
            }
            assert(channel && channel->nref > 0);
            if (channel && --channel->nref == 0) {
                channel->expire = 10;
                // 移动到最后，达到expire从小到大排列的效果
                std::remove(channels_.begin(), channels_.end(), channel);
                channels_.back() = channel;
            }
            check_parallel();
            return;
        }

        void LiveModule::check_parallel()
        {
            size_t active = 0;
            for (size_t i = 0; i < channels_.size(); ++i) {
                if (channels_[i]->nref > 0)
                    ++active;
            }
            size_t not_active = // 可接受的非活动channel数
                max_parallel_ > active ? max_parallel_ - active : 0;
            size_t stop = channels_.size() - active - not_active;
            for (size_t i = 0; stop > 0 && i < channels_.size(); ++i) {
                if (channels_[i]->nref == 0) {
                    LOG_S(Logger::kLevelEvent, "[check_parallel] delete channel "
                        << (void *)channels_[i] << " expire: " << channels_[i]->expire);
                    live_->stop_channel(channels_[i]->handle);
                    delete channels_[i];
                    channels_[i] = NULL;
                    --stop;
                }
            }
            channels_.erase(
                std::remove(channels_.begin(), channels_.end(), (LiveChannel *)0), channels_.end());
        }

        void LiveModule::handle_timer(
            error_code const & ec)
        {
            LOG_SECTION();

            if (!get_daemon().is_started()) {
                return;
            }

            dump_channels();

            for (size_t i = 0; i < channels_.size(); ++i) {
                if (channels_[i]->nref == 0 && --channels_[i]->expire == 0) {
                    LOG_S(Logger::kLevelEvent, "[handle_timer] delete channel " << (void *)channels_[i]);
                    live_->stop_channel(channels_[i]->handle);
                    delete channels_[i];
                    channels_[i] = NULL;
                }
            }
            channels_.erase(
                std::remove(channels_.begin(), channels_.end(), (LiveChannel *)0), channels_.end());
            timer_.expires_from_now(Duration::seconds(1));
            timer_.async_wait(boost::bind(&LiveModule::handle_timer, this, _1));
        }

        void LiveModule::handle_call_back(
            LiveChannel * channel, 
            error_code const & ec)
        {
            io_svc().post(boost::bind(
                &LiveModule::handle_call_back_innner, this, channel, ec));
        }

        void LiveModule::handle_call_back_innner(
            LiveChannel * channel, 
            error_code const & ec)
        {
            if (std::find(channels_.begin(), channels_.end(), channel) == channels_.end()) {
                LOG_S(Logger::kLevelAlarm, "[handle_call_back_innner] already deleted channel " << (void *)channel);
                return;
            }
            if (!ec) {
                channel->playing = true;
                CCoreStatus cs;
                live_->get_channel_status(channel->handle, cs);
                channel->url2 = "http://127.0.0.1:" + format(cs.m_uMediaListenPort) + "/1.asf";
            }
            std::vector<call_back_func> call_backs;
            call_backs.swap(channel->call_backs);
            for (size_t i = 0; i < call_backs.size(); ++i) {
                if (!call_backs[i].empty())
                    io_svc().post(
                        boost::bind(call_backs[i], ec, channel->url2));
            }
        }

        void LiveModule::dump_channels()
        {
            for (size_t i = 0; i < channels_.size(); ++i) {
                CCoreStatus cs;
                live_->get_channel_status(channels_[i]->handle, cs);
                LOG_F(Logger::kLevelDebug2, "dump_channels [%d]  p: %d%%  t: %ds  d: %dk  u: %dk  c: %d   s: %d   t: %d"
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
            LiveChannel * channel = (LiveChannel *)ChannelHandle;
            channel->module->handle_call_back(channel, 
                Msg == UM_LIVEMSG_PLAY ? error_code() : logic_error::failed_some);
            return 0;
        }

    } // namespace live_worker
} // namespace ppbox
