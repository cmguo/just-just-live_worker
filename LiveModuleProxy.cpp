// LiveModuleProxy.cpp

#include "ppbox/live_worker/Common.h"
#include "ppbox/live_worker/LiveModuleProxy.h"
#include "ppbox/live_worker/LiveModule.h"

#ifdef PPBOX_LIVE_WORKER_MULTI_PROCESS

#include <framework/string/Format.h>
#include <framework/logger/StreamRecord.h>
#include <framework/logger/LoggerSection.h>
#include <framework/system/LogicError.h>
using namespace framework::string;
using namespace framework::system;

#include <util/serialization/ErrorCode.h>
#include <util/archive/TextIArchive.h>
#include <util/archive/TextOArchive.h>

#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/local/connect_pair.hpp>
#include <boost/asio/streambuf.hpp>
using namespace boost::system;

#include <unistd.h> // for fork
#include <sys/types.h>
#include <sys/wait.h> // for waitpid

FRAMEWORK_LOGGER_DECLARE_MODULE_LEVEL("ppbox.live_worker.LiveModuleProxy", framework::logger::Debug)

namespace ppbox
{
    namespace live_worker
    {

        struct LiveModuleProxy::Channel
        {
            Channel()
                : local_socket_(NULL)
            {
                error_code ec;
                boost::asio::local::stream_protocol protocol;
                boost::asio::detail::socket_ops::socketpair(protocol.family(), 
                    protocol.type(), protocol.protocol(), native_sockets_, ec);
            }

            ~Channel()
            {
                if (local_socket_) {
                    delete local_socket_;
                } else {
                    error_code ec;
                    boost::asio::detail::socket_ops::close(native_sockets_[0], ec);
                    boost::asio::detail::socket_ops::close(native_sockets_[1], ec);
                }
            }

            void after_fork(
                bool parent, 
                boost::asio::io_service & io_svc)
            {
                local_socket_ = new boost::asio::local::stream_protocol::socket(io_svc);
                error_code ec;
                if (parent) {
                    boost::asio::detail::socket_ops::close(native_sockets_[1], ec);
                    local_socket_->assign(boost::asio::local::stream_protocol(), native_sockets_[0]);
                } else {
                    boost::asio::detail::socket_ops::close(native_sockets_[0], ec);
                    local_socket_->assign(boost::asio::local::stream_protocol(), native_sockets_[1]);
                }
            }

            void wait_start_channel(
                LiveModuleProxy::call_back_func const & call_back)
            {
                error_code ec;
                local_socket_->async_read_some(buf_.prepare(2048), 
                    boost::bind(&Channel::handle_parent_read_some, this, call_back, _1, _2));
            }

            void handle_parent_read_some(
                LiveModuleProxy::call_back_func const & call_back, 
                error_code const & ecc, 
                size_t bytes_transferred)
            {
                LOG_DEBUG("[handle_parent_read_some] ec = " 
                    << ecc.message() << " bytes_transferred = " << bytes_transferred);
                error_code const & ec = ecc;
                std::string url;
                if (!ec) {
                    buf_.commit(bytes_transferred);
                    util::archive::TextIArchive<> ia(buf_);
                    ia >> ec >> url;
                    assert(ia);
                }
                call_back(ec, url);
            }

            LiveModule::call_back_func get_call_back()
            {
                return boost::bind(&Channel::handle_start_channel, this, _1, _2);
            }

            // parent call this to notify child to stop channel and exit
            void stop_channel(
                error_code const & ec, 
                std::string const & url)
            {
                boost::asio::streambuf buf;
                {
                    util::archive::TextOArchive<> oa(buf);
                    oa << ec << url;
                }
                error_code ec1;
                local_socket_->send(buf.data(), 0, ec1);
            }

            // child call this to wait for notify from  parent to stop channel and exit
            void wait_stop_channel(
                LiveModuleProxy::call_back_func const & call_back)
            {
                local_socket_->async_read_some(buf_.prepare(2048), 
                    boost::bind(&Channel::handle_child_read_some, this, call_back, _1, _2));
            }

            // child has received stop msg from parent
            void handle_child_read_some(
                LiveModuleProxy::call_back_func const & call_back, 
                error_code const & ecc, 
                size_t bytes_transferred)
            {
                LOG_DEBUG("[handle_child_read_some] ec = " 
                    << ecc.message() << " bytes_transferred = " << bytes_transferred);
                error_code const & ec = ecc;
                std::string url;
                if (!ec) {
                    std::istream is(&buf_);
                    url.resize(buf_.size());
                    is.read(&url[0], buf_.size());
                }
                call_back(ec, url);
            }

            // child finish starting channel, notify parent
            void handle_start_channel(
                error_code const & ec, 
                std::string const & url)
            {
                boost::asio::streambuf buf;
                {
                    util::archive::TextOArchive<> oa(buf);
                    oa << ec << url;
                }
                error_code ec1;
                local_socket_->send(buf.data(), 0, ec1);
            }

            LiveModule::call_back_func call_back;
            pid_t pid;
            LiveModule::ChannelHandle handle;

        private:
            boost::asio::local::stream_protocol::socket * local_socket_;
            boost::asio::detail::socket_type native_sockets_[2];
            boost::asio::streambuf buf_;
        };

        LiveModuleProxy::LiveModuleProxy(
            util::daemon::Daemon & daemon)
            : util::daemon::ModuleBase<LiveModuleProxy>(daemon, "LiveModuleProxy")
        {
        }

        LiveModuleProxy::~LiveModuleProxy()
        {
        }

        error_code LiveModuleProxy::startup()
        {
            return error_code();
        }

        void LiveModuleProxy::shutdown()
        {
        }

        LiveModuleProxy::ChannelHandle LiveModuleProxy::start_channel( 
            std::string const & url, 
            boost::uint16_t tcp_port, 
            boost::uint16_t udp_port, 
            LiveModule::call_back_func const & call_back)
        {
            Channel * channel = new Channel;
            LOG_INFO("[start_channel] channel " << (void *)channel);
            pid_t pid = ::fork();
            if (pid > 0) {
                channel->after_fork(true, io_svc());
                channel->call_back = call_back;
                channel->pid = pid;
                channel->wait_start_channel(
                    boost::bind(&LiveModuleProxy::handle_start_channel, this, 
                    channel, _1, _2));
                return channel;
            } else if (pid == 0) {
                util::daemon::Daemon daemon;
                daemon.config().profile() = get_daemon().config().profile();
                LiveModule & live_module = util::daemon::use_module<LiveModule>(daemon);
                channel->after_fork(false, daemon.io_svc());
                error_code ec = daemon.start();
                if (ec) {
                    channel->get_call_back()(logic_error::failed_some, std::string());
                } else {
                    channel->handle = live_module.start_channel(
                        url, tcp_port, udp_port, channel->get_call_back());
                    if (channel->handle == NULL) {
                        channel->get_call_back()(logic_error::failed_some, std::string());
                        daemon.stop();
                    } else {
                        channel->wait_stop_channel(
                            boost::bind(&LiveModuleProxy::handle_stop_channel, this, 
                            boost::ref(daemon), channel, _1, _2));
                        daemon.run();
                    }
                }
                ::_exit(0);
                return NULL;
            } else {
                delete channel;
                return NULL;
            }
        }

        void LiveModuleProxy::stop_channel(
            ChannelHandle handle)
        {
            Channel * channel = (Channel *)handle;
            LOG_INFO("[stop_channel] channel " << (void *)channel);
            channel->stop_channel(error_code(), ""); 
            if (channel->call_back.empty()) {
                LOG_INFO("[stop_channel] delete channel " << (void *)channel);
                channels_.erase(
                    std::remove(channels_.begin(), channels_.end(), channel), channels_.end());
                delete channel;
            } else {
                call_back_func call_back;
                call_back.swap(channel->call_back);
                io_svc().post(boost::bind(call_back, 
                    boost::asio::error::operation_aborted, std::string()));
            }
        }

        struct find_channel_by_pid
        {
            find_channel_by_pid(pid_t pid) : pid_(pid) {}
            bool operator()(LiveModuleProxy::Channel * channel) { return channel->pid == pid_; }
            pid_t pid_;
        };

        size_t LiveModuleProxy::check_channels(
            std::vector<ChannelHandle> & failed)
        {
            pid_t pid = ::waitpid(-1, NULL, WNOHANG);
            while (pid > 0) {
                std::vector<Channel *>::const_iterator iter = 
                    std::find_if(channels_.begin(), channels_.end(), find_channel_by_pid(pid));  
                if (iter != channels_.end()) {
                    failed.push_back(*iter);
                }
                pid = ::waitpid(-1, NULL, WNOHANG);
            }
            return failed.size();
        }

        void LiveModuleProxy::dump_channels()
        {
        }

        void LiveModuleProxy::handle_start_channel(
            Channel * channel, 
            error_code const & ec, 
            std::string const & url)
        {
            LOG_INFO("[handle_start_channel] channel = " << (void *)channel << ", ec = " << ec.message() << ", ur; = " << url);
            if (channel->call_back.empty()) {
                LOG_INFO("[handle_start_channel] delete channel " << (void *)channel);
                channels_.erase(
                    std::remove(channels_.begin(), channels_.end(), channel), channels_.end());
                delete channel;
            } else {
                call_back_func call_back;
                call_back.swap(channel->call_back);
                io_svc().post(boost::bind(call_back, ec, url));
            }
        }

        void LiveModuleProxy::handle_stop_channel(
            util::daemon::Daemon & daemon, 
            Channel * channel, 
            error_code const & ec, 
            std::string const & msg)
        {
            LOG_INFO("[handle_stop_channel] channel = " << (void *)channel);
            LiveModule & live_module = util::daemon::use_module<LiveModule>(daemon);
            live_module.stop_channel(channel->handle);
            daemon.stop();
        }

    } // namespace live_worker
} // namespace ppbox

#endif
