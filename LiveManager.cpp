// LiveManager.cpp

#include "ppbox/live_worker/Common.h"
#include "ppbox/live_worker/LiveManager.h"
#include "ppbox/live_worker/LiveModuleProxy.h"

#include <live/live/Name.h>

#include <util/protocol/pptv/Base64.h>
using namespace util::protocol;

#include <framework/system/LogicError.h>
#include <framework/string/Format.h>
#include <framework/string/Slice.h>
#include <framework/string/Parse.h>
#include <framework/logger/StreamRecord.h>
#include <framework/logger/LoggerSection.h>
using namespace framework::string;
using namespace framework::logger;
using namespace framework::system;

#include <boost/bind.hpp>
#include <boost/asio/io_service.hpp>
using namespace boost::system;

FRAMEWORK_LOGGER_DECLARE_MODULE_LEVEL("LiveManager", 0)

static const char PPBOX_LIVE_KEY[] = "pplive";

namespace ppbox
{
    namespace live_worker
    {

        struct LiveManager::Channel
        {
            Channel() 
                : nref(0)
                , expire(0)
                , handle(NULL)
                , status(started)
            {
            }

            enum StatusEnum
            {
                started, 
                working, 
                cancel, 
                stopped, 
            };

            std::string url;
            std::string rid;

            boost::uint16_t tcp_port;
            boost::uint16_t udp_port;

            boost::uint32_t nref;
            boost::uint32_t expire;
            LiveModuleProxy::ChannelHandle handle;
            StatusEnum status;
            boost::system::error_code ec;
            std::string url2;
            std::vector<LiveManager::call_back_func> call_backs;
        };

        struct find_channel_not_active
        {
            bool operator()(LiveManager::Channel * channel) {
                return (channel->nref == 0 
                    || channel->status == LiveManager::Channel::cancel 
                    || channel->status == LiveManager::Channel::stopped); 
            }
        };

        struct find_channel_stopped
        {
            bool operator()(LiveManager::Channel * channel) {
                return (channel->status == LiveManager::Channel::cancel 
                    || channel->status == LiveManager::Channel::stopped); 
            }
        };

        //正在工作的频道
        struct find_channel_working
        {
            bool operator()(LiveManager::Channel * channel) {
                return ((channel->status == LiveManager::Channel::working 
                    || channel->status == LiveManager::Channel::started) && (channel->nref > 0));
            }
        };

        LiveManager::LiveManager(
            util::daemon::Daemon & daemon)
            : util::daemon::ModuleBase<LiveManager>(daemon, "LiveManager")
            , live_module_(util::daemon::use_module<LiveModuleProxy>(daemon))
            , timer_(io_svc())
        {
            std::string strParallel("1");
            int iParallel = 0;
            daemon.config().register_module("LiveProxy")
                << CONFIG_PARAM_NAME_NOACC("max_parallel", strParallel);

            LOG_DEBUG("[max_parallel] " << strParallel.c_str());

            framework::string::parse2(strParallel,iParallel);

            max_parallel_ = iParallel;
        }

        LiveManager::~LiveManager()
        {
        }

        error_code LiveManager::startup()
        {
            error_code ec;
            timer_.expires_from_now(Duration::seconds(1), ec);
            timer_.async_wait(boost::bind(&LiveManager::handle_timer, this, _1));
            return ec;
        }

        void LiveManager::shutdown()
        {
            for (size_t i = 0; i < channels_.size(); ++i) {
                stop_channel(channels_[i]);
            }
            channels_.erase(
                std::remove(channels_.begin(), channels_.end(), (Channel *)0), channels_.end());
            error_code ec;
            timer_.cancel(ec);
        }

        void LiveManager::set_max_parallel(
            size_t max_parallel)
        {
            if (max_parallel_ != max_parallel) {
                max_parallel_ = max_parallel;
                check_parallel();
            }
        }

        LiveManager::ChannelHandle LiveManager::start_channel(
            std::string const & url, 
            call_back_func const & call_back)
        {
            return start_channel(url, tcp_port, udp_port, call_back);
        }

        LiveManager::ChannelHandle LiveManager::start_channel(
            std::string const & url, 
            boost::uint16_t tcp_port, 
            boost::uint16_t udp_port, 
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

            LOG_INFO("[start_channel] rid: " << rid);
            Channel * channel = NULL;
            for (size_t i = 0; i < channels_.size(); ++i) 
            {
                if (channels_[i]->rid == rid) 
                {
                    channel = channels_[i];
                    assert(NULL != channel);
                    channel->expire = 0;

                    //端口的比对
                    if(tcp_port != 0 && udp_port != 0  && (tcp_port != channel->tcp_port || udp_port != channel->udp_port))
                    {
                        LOG_INFO("[re_start_channel] old channel: " << (void *)channel);
                        //删除旧的频道
                        stop_channel(channels_[i]);

                        //清除被清完的频道
                        channels_.erase(
                            std::remove(channels_.begin(), channels_.end(), (Channel *)0), channels_.end());

                        //重新打开该频道
                        return start_channel(url,tcp_port,udp_port,call_back);       
                    }

                    LOG_INFO("[start_channel] old channel: " << (void *)channel);

                    /*
                    if ((channel->status == Channel::working  || channel->status == Channel::started) && (0 == channel->nref))
                    {
                    LOG_INFO("[change palce to front] status: " << (int)channel->status);
                    channels_.erase(
                    std::remove(channels_.begin(), channels_.end(),channel), channels_.end());

                    channels_.insert(channels_.begin(), channel);
                    }*/
                    break;
                }
            }
            if (channel == NULL) {
                channel = new Channel;
                channel->url = url;
                channel->rid = rid;
                channel->tcp_port = tcp_port;
                channel->udp_port = udp_port;
                channel->handle = live_module_.start_channel(
                    (std::string("synacast:/") + url).c_str(), tcp_port, udp_port, 
                    boost::bind(&LiveManager::handle_start_channel, this, channel, _1, _2));
                if (channel->handle == NULL) {
                    io_svc().post(
                        boost::bind(call_back, logic_error::failed_some, channel->url2));
                    delete channel;
                    return ChannelHandle(NULL);
                }
                LOG_INFO("[start_channel] new channel: " << (void *)channel);
                /*std::vector<Channel *>::iterator iter = 
                std::find_if(channels_.begin(), channels_.end(), find_channel_not_active());
                channels_.insert(iter, channel);*/
                channels_.push_back(channel);
            }
            ++channel->nref;
            ChannelHandle handle(channel);
            if (channel->status == Channel::working) {
                io_svc().post(
                    boost::bind(call_back, channel->ec, channel->url2));
            }else {
                channel->call_backs.push_back(call_back);
                handle.cancel_token = channel->call_backs.size();
            }
            check_parallel();
            return handle;
        }

        void LiveManager::stop_channel(
            ChannelHandle & handle)
        {
            Channel * channel = handle.channel;
            handle.channel = NULL;
            if (channel == NULL)
                return;
            LOG_INFO("[stop_channel] rid: " << channel->rid << ", channel: " << (void *)channel);
            if (handle.cancel_token && handle.cancel_token < channel->call_backs.size() + 1) {
                call_back_func call_back;
                call_back.swap(channel->call_backs[handle.cancel_token - 1]);
                io_svc().post(boost::bind(
                    call_back, boost::asio::error::operation_aborted, std::string()));
            }
            assert(channel && channel->nref > 0);
            if (channel && --channel->nref == 0)
            {
                if (!find_channel_stopped()(channel)) 
                {
                    channel->expire = 10;
                    channels_.erase(std::remove(channels_.begin(), channels_.end(), channel), channels_.end());
                    channels_.insert(channels_.begin(),channel);  //移到最前面
                }
                else
                {
                    //stop_channel   
                }
            }
            check_parallel();
            return;
        }

        void LiveManager::check_parallel()
        {

            int iUserSize = 0;
            iUserSize = count_if(channels_.begin(),channels_.end(),find_channel_working());
            LOG_INFO("[check_parallel] max_parallel_: " << max_parallel_ << ", iUserSize: " << iUserSize);

            int iLeftSize = ((max_parallel_ - iUserSize)<0)?0:(max_parallel_ - iUserSize); 
            int iFindCount = 0;

            for(size_t i = 0; i < channels_.size(); ++i )
            {
                if(0 == channels_[i]->nref)
                {
                    if( channels_[i]->status == LiveManager::Channel::cancel 
                        || channels_[i]->status == LiveManager::Channel::stopped )
                    {
                        stop_channel(channels_[i]);
                    }
                    else
                    {
                        ++iFindCount;
                        if(iFindCount > iLeftSize)
                        {
                            stop_channel(channels_[i]);
                        }
                    }
                }
            }

            channels_.erase(
                std::remove(channels_.begin(), channels_.end(), (Channel *)0), channels_.end());
        }

        struct find_channel_not_failed
        {
            bool operator()(LiveManager::Channel * channel) {
                return std::find(failed.begin(), failed.end(), channel->handle) == failed.end(); }
            std::vector<LiveModuleProxy::ChannelHandle> failed;
        };

        void LiveManager::handle_timer(
            error_code const & ec)
        {
            LOG_SECTION();

            if (!get_daemon().is_started()) {
                return;
            }

            live_module_.dump_channels();

            find_channel_not_failed finder;
            if (live_module_.check_channels(finder.failed)) {
                std::vector<Channel *>::iterator working_end = 
                    std::find_if(channels_.begin(), channels_.end(), find_channel_stopped());
                std::vector<Channel *>::iterator failed_beg = 
                    std::stable_partition(channels_.begin(), working_end, finder);
                for (; failed_beg != working_end; ++failed_beg) {
                    LOG_INFO("[handle_timer] channel failed: " << (void *)*failed_beg);
                    stop_channel(*failed_beg);
                }
            }

            for (size_t i = 0; i < channels_.size(); ++i) {
                if (channels_[i] && channels_[i]->nref == 0 && --channels_[i]->expire == 0) {
                    stop_channel(channels_[i]);
                }
            }
            channels_.erase(
                std::remove(channels_.begin(), channels_.end(), (Channel *)0), channels_.end());
            timer_.expires_from_now(Duration::seconds(1));
            timer_.async_wait(boost::bind(&LiveManager::handle_timer, this, _1));
        }

        void LiveManager::handle_start_channel(
            Channel * channel, 
            error_code const & ec, 
            std::string const & url)
        {
            std::vector<Channel *>::iterator iter = std::find(channels_.begin(), channels_.end(), channel);
            if ( iter == channels_.end()) {
                LOG_WARN("[handle_call_back_innner] already deleted channel " << (void *)channel);
                return;
            }
            if (channel->status == Channel::cancel) {
                channel->status = Channel::stopped;
                stop_channel(*iter);
                channels_.erase(
                    std::remove(channels_.begin(), channels_.end(), (Channel *)0), channels_.end());
            } else {
                channel->status = Channel::working;
                response_channel(channel, ec, url);
            }
        }

        void LiveManager::response_channel(
            Channel * channel, 
            error_code const & ec, 
            std::string const & url)
        {
            channel->ec = ec;
            channel->url2 = url;
            std::vector<call_back_func> call_backs;
            call_backs.swap(channel->call_backs);
            for (size_t i = 0; i < call_backs.size(); ++i) {
                if (!call_backs[i].empty())
                    io_svc().post(
                    boost::bind(call_backs[i], ec, url));
            }
        }

        void LiveManager::stop_channel(
            Channel *& channel)
        {
            if(NULL == channel)
            {
                LOG_WARN("[stop_channel NULL == channel]");
                return;    
            }
            if (channel->status == Channel::started) {
                channel->status = Channel::cancel;
                response_channel(channel, boost::asio::error::operation_aborted, std::string());
                live_module_.stop_channel(channel->handle);
                channel->handle = NULL;
                channel->rid.clear();
                return;
            } else if (channel->status == Channel::working) {
                channel->status = Channel::stopped;
                live_module_.stop_channel(channel->handle);
                channel->handle = NULL;
                channel->rid.clear();
            } else if (channel->status == Channel::cancel) {
                return;
            }
            LOG_WARN("[kill_chancel] channel" << (void *)channel<<" status:"<<channel->status<<" nref:"<<channel->nref);
            if (channel->nref == 0) 
            { 
                LOG_WARN("[kill_chancel] deleted channel " << (void *)channel<<" channel count:"<<channels_.size());
                delete channel;
                channel = NULL;
            }
        }

    } // namespace live_worker
} // namespace ppbox
