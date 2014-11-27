// LiveInterface.h

#include <live/Interface.h>

#include <framework/library/Library.h>

namespace just
{
    namespace live_worker
    {

        struct LiveInterfaceObject
        {
            FUNC_LiveStartup startup;
            FUNC_LiveCleanup cleanup;
            FUNC_LiveStartChannel start_channel;
            FUNC_LiveStopChannel stop_channel;
            FUNC_LiveGetChannelStatus get_status;
            FUNC_LiveGetChannelParameter get_parameter;
            FUNC_LiveSetChannelParameter set_parameter;
            FUNC_LiveSetChannelPlayerStatus set_player_status;
            FUNC_LiveSetChannelCallback set_callback;
            FUNC_LiveSetChannelUPNP set_upnp;

            LiveInterfaceObject()
            {
                memset(this, 0, sizeof(*this));
            }

            bool load(
                framework::library::Library const & dll)
            {
#ifdef JUST_STATIC_BIND_LIVE_LIB
                this->startup = LiveStartup;
                this->cleanup = LiveCleanup;
                this->start_channel = LiveStartChannel;
                this->stop_channel = LiveStopChannel;
                this->get_status = LiveGetChannelStatus;
                this->get_parameter = LiveGetChannelParameter;
                this->set_parameter = LiveSetChannelParameter;
                this->set_player_status = LiveSetChannelPlayerStatus;
                this->set_callback = LiveSetChannelCallback;
                this->set_upnp = LiveSetChannelUPNP;
#else
                retrieve_func(this->startup, "LiveStartup", dll);
                retrieve_func(this->cleanup, "LiveCleanup", dll);
                retrieve_func(this->start_channel, "LiveStartChannel", dll);
                retrieve_func(this->stop_channel, "LiveStopChannel", dll);
                retrieve_func(this->get_status, "LiveGetChannelStatus", dll);
                retrieve_func(this->get_parameter, "LiveGetChannelParameter", dll);
                retrieve_func(this->set_parameter, "LiveSetChannelParameter", dll);
                retrieve_func(this->set_player_status, "LiveSetChannelPlayerStatus", dll);
                retrieve_func(this->set_callback, "LiveSetChannelCallback", dll);
                retrieve_func(this->set_upnp, "LiveSetChannelUPNP", dll);
#endif
                return is_valid();
            }

            bool is_valid() const
            {
                return this->startup != NULL 
                    && this->cleanup != NULL 
                    && this->start_channel != NULL 
                    && this->stop_channel != NULL 
                    && this->get_status != NULL 
                    && this->get_parameter != NULL 
                    && this->set_parameter != NULL 
                    && this->set_player_status != NULL 
                    && this->set_callback != NULL 
                    && this->set_upnp != NULL;
            }

        protected:
            template <typename FuncT>
            static void retrieve_func(
                FuncT& fun, 
                const char* name, 
                framework::library::Library const & dll)
            {
                fun = reinterpret_cast<FuncT>(dll.symbol(name));
            }
        };

        class LiveInterface
        {
        public:
            LiveInterface()
            {
            }

        public:
            boost::system::error_code load(
                std::string const & lib_path)
            {
#ifdef JUST_STATIC_BIND_LIVE_LIB
                boost::system::error_code ec;
#else
                boost::system::error_code ec = 
                    m_dll.open(lib_path);
#endif
                if (!ec) {
                    m_interface.load(m_dll);
                    assert(m_interface.is_valid());
                }
                return ec;
            }

        public:
            bool startup(int type)
            {
                if (NULL == m_interface.startup) {
                    assert(false);
                    return false;
                }
                return m_interface.startup(type);
            }

            void cleanup()
            {
                if (NULL == m_interface.startup) {
                    assert(false);
                    return;
                }
                m_interface.cleanup();
            }

            void* start_channel(const char* url, int tcpPort, int udpPort)
            {
                if (NULL == m_interface.start_channel) {
                    assert(false);
                    return NULL;
                }
                return m_interface.start_channel(url, tcpPort, udpPort);
            }

            void stop_channel(void* channel)
            {
                if (NULL == m_interface.stop_channel) {
                    assert(false);
                    return;
                }
                m_interface.stop_channel(channel);
            }

            bool get_channel_status(void* channel, CCoreStatus& status)
            {
                if (NULL == m_interface.get_status) {
                    assert(false);
                    return false;
                }
                return m_interface.get_status(channel, &status);
            }

            bool get_channel_parameter(void* channel, CCoreParameter& param)
            {
                if (NULL == m_interface.get_parameter) {
                    assert(false);
                    return false;
                }
                return m_interface.get_parameter(channel, &param);
            }

            bool set_channel_parameter(void* channel, const CCoreParameter& param)
            {
                if (NULL == m_interface.set_parameter) {
                    assert(false);
                    return false;
                }
                return m_interface.set_parameter(channel, &param);
            }

            bool set_channel_player_status(
                void* channel, 
                int pstatus)
            {
                if (NULL == m_interface.set_player_status) {
                    assert(false);
                    return false;
                }
                return m_interface.set_player_status(channel, pstatus);
            }

            bool set_channel_callback(
                void* channel, 
                FUNC_CallBack callback, 
                unsigned int handle)
            {
                if (NULL == m_interface.set_callback) {
                    assert(false);
                    return false;
                }
                return m_interface.set_callback(channel, callback, handle);
            }

            bool set_channel_upnp(void* channel, const CCoreUPNP& param)
            {
                if (NULL == m_interface.set_upnp) {
                    assert(false);
                    return false;
                }
                return m_interface.set_upnp(channel, &param);
            }

        private:
            framework::library::Library m_dll;
            LiveInterfaceObject m_interface;
        };

    } // namespace live_worker
} // namespace just
