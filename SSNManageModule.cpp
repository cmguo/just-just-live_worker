// SSNManageModule.cpp

#include "just/live_worker/Common.h"

#ifdef JUST_LIVE_WORKER_WITH_SSN_MANAGER

#include "just/live_worker/Define.h"
#include "just/live_worker/SSNManageModule.h"

#include <util/protocol/http/HttpClient.h>

#include <util/serialization/stl/vector.h>
#include <util/archive/ArchiveBuffer.h> 
#include <util/archive/XmlIArchive.h>

#include <framework/string/Parse.h>

#include <boost/bind.hpp>
#include <boost/thread.hpp>

using namespace boost::system;

FRAMEWORK_LOGGER_DECLARE_MODULE_LEVEL("just.live_worker.SSNManageModule", framework::logger::Debug)

BEGIN_NAME_SPACE

SSNManageModule::SSNManageModule(util::daemon::Daemon & daemon)
    : just::common::CommonModuleBase<SSNManageModule>(daemon, "SSNManageModule"), 
    timer_(io_svc()),
    _mgr(util::daemon::use_module<LiveManager>(daemon))
{
    std::string service_ip("0.0.0.0"), sn_type("1");

    snc_url_ = "http://snc2.synacast.com/sn/channel_list.aspx";

    daemon.config().register_module("SNManageModule")
        << CONFIG_PARAM_NAME_NOACC("service_ip", service_ip)
        << CONFIG_PARAM_NAME_NOACC("snc_url", snc_url_);
    daemon.config().register_module("LiveModule")    
        << CONFIG_PARAM_NAME_NOACC("peer_type", sn_type);

    LOG_DEBUG("[service_ip] " << service_ip);
    LOG_DEBUG("[snc_url] " << snc_url_);
    LOG_DEBUG("[sn_type] " << sn_type);

    int iType = atoi(sn_type.c_str());
    if (iType < 1 || iType > 2)
    {
        sn_type = "1";
    }

    snc_url_ += "?ip=" + service_ip;
    snc_url_ += "&type=" + sn_type;


    LOG_DEBUG("[snc_url_] " << snc_url_.c_str());
        
}
SSNManageModule::~SSNManageModule()
{

}

boost::system::error_code SSNManageModule::startup()
{   

    timer_.expires_from_now(boost::posix_time::seconds(1));
    timer_.async_wait(boost::bind(&SSNManageModule::handle_timer, this, _1));


    return boost::system::error_code();    
}
void SSNManageModule::shutdown()
{
    ClearOldChannel();
    timer_.cancel();
}

void SSNManageModule::ClearOldChannel()
{
    for(std::vector<LiveManager::ChannelHandle>::iterator iter = channel_.begin(); iter != channel_.end(); ++iter)
    {
        _mgr.stop_channel(*iter);
    }

    channel_.clear();
}


void SSNManageModule::handle_timer( boost::system::error_code const & ec )
{
    if (!get_daemon().is_started()) {
        return;
    }
   
    boost::shared_ptr<HttpClient> snc_client_ptr(new HttpClient(io_svc()));
    snc_client_ptr->async_fetch_get(snc_url_, boost::bind(&SSNManageModule::on_fetch_channels, this, _1, snc_client_ptr));

    timer_.expires_from_now(boost::posix_time::seconds(60));
    timer_.async_wait(boost::bind(&SSNManageModule::handle_timer, this, _1));
}


void SSNManageModule::on_channel_ready(boost::system::error_code const & ec,std::string const & url_str)
{
    LOG_INFO("SSNManageModule::on_channel_ready");
}


void SSNManageModule::on_fetch_channels( boost::system::error_code const & ec, boost::shared_ptr<HttpClient> snc_client_ptr )
{

    boost::system::error_code ec1;
    std::vector<LiveManager::ChannelHandle> tempList;
    if(!ec)
    {
        util::archive::XmlIArchive<> ia(snc_client_ptr->response().data());
        std::vector<channel> channels;
        ia>> SERIALIZATION_NVP(channels);

        LOG_INFO("on_fetch_channels,channel size :"<<channels.size());

        for(std::vector<channel>::iterator iter = channels.begin(); 
                iter != channels.end(); 
                ++iter)
        {
            LiveManager::ChannelHandle chandle =_mgr.start_channel((*iter).url,(*iter).tcp_port,(*iter).udp_port,
                                                boost::bind(&SSNManageModule::on_channel_ready, this, _1, _2));
            if(NULL != chandle.channel)
            {
                tempList.push_back(chandle);
            }
            else
            {
                LOG_INFO("start_channel failed, channel id:"<<(*iter).id.c_str());
            }
        }
 
    }
    else
    {
        LOG_INFO("SSNManageModule::on_fetch_channels failed ");
    }

    //清除旧频道
    ClearOldChannel();

    //将新列表保存起来
    channel_.swap(tempList);

    snc_client_ptr->close(ec1);
}
END_NAME_SPACE

#endif

