// LiveProxy.cpp

#include "ppbox/live_worker/Common.h"
#include "ppbox/live_worker/LiveProxy.h"
#include "ppbox/live_worker/LiveModule.h"

#include <util/protocol/http/HttpProxyManager.h>
#include <util/protocol/http/HttpProxy.h>
#include <util/protocol/http/HttpRequest.h>
#include <util/protocol/http/HttpResponse.h>

#include <framework/network/NetName.h>
#include <framework/logger/LoggerStreamRecord.h>
#include <framework/string/Url.h>
using namespace framework::logger;

using namespace boost::system;

FRAMEWORK_LOGGER_DECLARE_MODULE_LEVEL("LiveProxy", 0)

namespace ppbox
{
    namespace live_worker
    {

        class Proxy;

        class ProxyManager
            : public util::protocol::HttpProxyManager<Proxy, ProxyManager>
        {
        public:
            ProxyManager(
                boost::asio::io_service & io_svc, 
                framework::network::NetName const & addr, 
                LiveModule & module)
                : util::protocol::HttpProxyManager<Proxy, ProxyManager>(io_svc, addr)
                , module_(module)
            {
            }

        public:
            void insert_proxy(
                Proxy * proxy)
            {
                proxys_.push_back(proxy);
            }

            void remove_proxy(
                Proxy * proxy)
            {
                proxys_.erase(
                    std::remove(proxys_.begin(), proxys_.end(), proxy), proxys_.end());
            }

        public:
            LiveModule & module()
            {
                return module_;
            }

            void stop();

        private:
            LiveModule & module_;
            std::vector<Proxy *> proxys_;
        };

        class Proxy
            : public util::protocol::HttpProxy
        {
        public:
            Proxy(
                ProxyManager & mgr)
                : HttpProxy(mgr.io_svc())
                , mgr_(mgr)
                , channel_(NULL)
            {
                mgr_.insert_proxy(this);
            }

            virtual ~Proxy()
            {
                mgr_.remove_proxy(this);
            }

            virtual void on_receive_request_head(
                util::protocol::HttpRequestHead & request_head, 
                prepare_response_type const & resp)
            {
                request_head.get_content(std::cout);
                std::string url = request_head.path;
                channel_ = mgr_.module().start_channel(url, 
                    boost::bind(&Proxy::on_channel_ready, this, resp, _1, _2));
            }

            virtual void on_receive_response_head( 
                util::protocol::HttpResponseHead & response_head)
            {
                response_head.get_content(std::cout);
            }

            virtual void on_broken_pipe()
            {
                mgr_.module().stop_channel(channel_);
            }

            virtual void on_error(
                boost::system::error_code const & ec)
            {
                LOG_S(Logger::kLevelAlarm, "on_error " << ec.message());
                if (ec == boost::asio::error::address_in_use)
                    mgr_.module().get_daemon().post_stop();
            }

            virtual void on_finish()
            {
                mgr_.module().stop_channel(channel_);
            }

            void on_channel_ready(
                prepare_response_type const & resp, 
                boost::system::error_code const & ec, 
                std::string const & url_str)
            {
                framework::string::Url url(url_str);
                get_request_head().host.reset(url.host() + ":" + url.svc());
                get_request_head().path = url.path();
                resp(ec, true);
            }

        private:
            ProxyManager & mgr_;
            ChannelHandle channel_;
        };

        void ProxyManager::stop()
        {
            util::protocol::HttpProxyManager<Proxy, ProxyManager>::stop();
            for (size_t i = 0; i < proxys_.size(); ++i) {
                error_code ec;
                proxys_[i]->cancel(ec);
            }
        }

        LiveProxy::LiveProxy(
            util::daemon::Daemon & daemon)
            : ppbox::common::CommonModuleBase<LiveProxy>(daemon, "LiveProxy")
            , module_(util::daemon::use_module<LiveModule>(daemon))
        {
            framework::network::NetName addr("0.0.0.0", 9001);
            mgr_ = new ProxyManager(io_svc(), addr, module_);
        }

        LiveProxy::~LiveProxy()
        {
            delete mgr_;
        }

        error_code LiveProxy::startup()
        {
            mgr_->start();
            return error_code();
        }

        void LiveProxy::shutdown()
        {
            mgr_->stop();
        }

    } // namespace live_worker
} // namespace ppbox
