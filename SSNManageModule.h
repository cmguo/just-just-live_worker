#ifndef _PPBOX_LIVE_WORKER_SSNMANAGEMODULE_H_
#define _PPBOX_LIVE_WORKER_SSNMANAGEMODULE_H_

#include "ppbox/live_worker/LiveManager.h"

#include <boost/asio/deadline_timer.hpp>

#include <boost/function.hpp>
#include <util/serialization/NVPair.h>

#include <map>

#define LIVESTEAM_HEAD "synacast:/"

namespace util
{
    namespace protocol
    {
        class HttpClient;
    }
}

namespace boost
{
    class thread_group;
}

namespace ppbox
{
    namespace live_worker
    {

 
            using util::protocol::HttpClient;
            typedef void* ChannelHandlePtr;
            using boost::thread_group;
            
          //  class LiveManager;
           // struct LiveManager::ChannelHandle;

            class SSNManageModule 
                : public ppbox::common::CommonModuleBase<SSNManageModule>
            {
            public:
                
                struct channel 
                {
                    std::string id;
                    std::string url;
                    boost::uint16_t tcp_port;
                    boost::uint16_t udp_port;
                    ChannelHandlePtr handle;

                    channel() : handle(NULL) {}

                    template <
                        typename Archive
                    >
                    void serialize(
                    Archive & ar)
                    {
                        std::string port;
                        ar  & SERIALIZATION_NVP(id)
                            & SERIALIZATION_NVP(url)
                            & SERIALIZATION_NVP(port);

                        int pos = port.find_first_of(':');
                        tcp_port = atoi(port.substr(0, pos).c_str());
                        udp_port = atoi(port.substr(pos + 1, port.length()).c_str());

                        pos = url.find_first_of(LIVESTEAM_HEAD);
                        if(0 == pos)
                        {
                            url = url.substr(strlen(LIVESTEAM_HEAD),url.length()-strlen(LIVESTEAM_HEAD));
                        }
                    }
                };

                SSNManageModule(util::daemon::Daemon & daemon);
                virtual ~SSNManageModule();
            public:
                virtual boost::system::error_code startup();
                virtual void shutdown();

                
                 void on_channel_ready(boost::system::error_code const & ec,std::string const & url_str);
            private:

                void handle_timer( boost::system::error_code const & ec );
                void on_fetch_channels( boost::system::error_code const & ec, boost::shared_ptr<HttpClient> snc_client_ptr );
                
                void ClearOldChannel();
            private:
                boost::asio::deadline_timer timer_;
                std::string snc_url_;
                LiveManager& _mgr;
                std::vector<LiveManager::ChannelHandle> channel_;


            };
    }
}
#endif

