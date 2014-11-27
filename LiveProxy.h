// LiveProxy.h

#ifndef _JUST_LIVE_WORKER_LIVE_PROXY_H_
#define _JUST_LIVE_WORKER_LIVE_PROXY_H_

#include <just/common/PortManager.h>

#include <framework/network/NetName.h>

#include <boost/function.hpp>

namespace just
{
    namespace live_worker
    {

        class LiveManager;
        class ProxyManager;

        class LiveProxy
            : public just::common::CommonModuleBase<LiveProxy>
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
            just::common::PortManager& portMgr_;
            ProxyManager * mgr_;
            framework::network::NetName addr_;
        };

    } // namespace live_worker
} // namespace just

#endif // _JUST_LIVE_WORKER_LIVE_PROXY_H_
