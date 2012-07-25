// LiveProxy.h

#ifndef _PPBOX_LIVE_WORKER_LIVE_PROXY_H_
#define _PPBOX_LIVE_WORKER_LIVE_PROXY_H_

#include <ppbox/common/PortManager.h>

#include <framework/network/NetName.h>

#include <boost/function.hpp>

namespace ppbox
{
    namespace live_worker
    {

        class LiveManager;
        class ProxyManager;

        class LiveProxy
            : public ppbox::common::CommonModuleBase<LiveProxy>
        {
        public:
            LiveProxy(
                util::daemon::Daemon & daemon);

            ~LiveProxy();

        public:
            virtual boost::system::error_code startup();

            virtual void shutdown();

        private:
            LiveManager & module_;
            ppbox::common::PortManager& portMgr_;
            ProxyManager * mgr_;
            framework::network::NetName addr_;
        };

    } // namespace live_worker
} // namespace ppbox

#endif // _PPBOX_LIVE_WORKER_LIVE_PROXY_H_
