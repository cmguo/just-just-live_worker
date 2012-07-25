// LiveProxy.cpp

#include "ppbox/live_worker/Common.h"
#include "ppbox/live_worker/LiveProxy.h"
#include "ppbox/live_worker/LiveManager.h"

#include <util/protocol/http/HttpProxyManager.h>
#include <util/protocol/http/HttpProxy.h>
#include <util/protocol/http/HttpRequest.h>
#include <util/protocol/http/HttpResponse.h>

#include <framework/logger/LoggerStreamRecord.h>
#include <framework/string/Url.h>
#include <framework/string/Parse.h>

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
                LiveManager & module)
                : util::protocol::HttpProxyManager<Proxy, ProxyManager>(io_svc)
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
            LiveManager & module()
            {
                return module_;
            }

            void stop();

        private:
            LiveManager & module_;
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
                response_type const & resp)
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
                HttpProxy::on_broken_pipe();
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
                response_type const & resp, 
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
            LiveManager::ChannelHandle channel_;
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
            , module_(util::daemon::use_module<LiveManager>(daemon))
            , portMgr_(util::daemon::use_module<ppbox::common::PortManager>(daemon))
            , addr_("0.0.0.0:9001+")
        {
            daemon.config().register_module("LiveProxy")
                << CONFIG_PARAM_NAME_RDWR("addr", addr_);

            mgr_ = new ProxyManager(io_svc(),module_);
        }

        LiveProxy::~LiveProxy()
        {
            delete mgr_;
        }

        error_code LiveProxy::startup()
        {
            error_code ec;
            mgr_->start(addr_,ec);
            if(!ec)
                portMgr_.set_port(ppbox::common::live,addr_.port());
            return ec;
        }

        void LiveProxy::shutdown()
        {
            mgr_->stop();
        }

    } // namespace live_worker
} // namespace ppbox
