#include <cstdint>
#include <iostream>
#include <thread>
#include <mutex>
#include <functional>
#include <string>
#include <sstream>
#include <chrono>
#include <iomanip>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <ev.h>

#define PRINT_LIMIT (4 * 1024)

typedef   std::function< void (struct ev_loop *loop, ev_io *w, int revents)> ev_callback_t;

void genericCallback(struct ev_loop *loop, ev_io *w, int revents)
{

    if(w && w->data)
    {
        ev_callback_t *callback = (ev_callback_t*)w->data;
        (*callback)(loop, w, revents);
    }

}

typedef   std::function< void (struct ev_loop *loop, ev_timer *w, int revents)> ev_timerCallback_t;

void timerCallback(struct ev_loop *loop, ev_timer *w, int revents)
{
    if(w && w->data)
    {
        ev_timerCallback_t *callback = (ev_timerCallback_t*)w->data;
        (*callback)(loop, w, revents);
    }

}

class rpmsg
{
    ev_io         m_evIO;
    ev_timer      m_watchDog;
    ev_timer      m_bandWidthTimer;
    int           m_fd;
    ev_callback_t  m_callback;
    std::string    m_postMsg;
    size_t         m_postMsgCnt;
    size_t         m_bytesRx;
    std::chrono::steady_clock::time_point m_start;
    ev_timerCallback_t  m_timerCB;


public:
    rpmsg(int fd): m_fd(fd), m_postMsgCnt(0), m_bytesRx(0)
    {
    }

    bool setNonBlocking()
    {
        fcntl(m_fd, F_SETFL, O_NONBLOCK);
    }


    bool postMessage()
    {
        std::stringstream ss;
        ss << "Hello from A9 " << m_postMsgCnt++;
        m_postMsg = ss.str();

        int rc =write(m_fd, (void *)m_postMsg.c_str(), m_postMsg.length() );
        if (rc < 1)
        {
            std::cerr << "ERROR: rc " << rc << std::endl;
            return false;
        }

        return true;
    }

    void watchDogCallback(struct ev_loop *loop, ev_timer *w, int revents)
    {
        //watchdog tripped
        // this causes all nested ev_run's to stop iterating

        std::cerr << __FUNCTION__ << " tripped" << std::endl;

        ev_break (loop, EVBREAK_ALL);
    }

    void calBandWidth(struct ev_loop *loop, ev_timer *w, int revents)
    {
      std::chrono::steady_clock::time_point end= std::chrono::steady_clock::now();

       size_t time =  std::chrono::duration_cast<std::chrono::seconds>(end - m_start).count();
        //watchdog tripped
        // this causes all nested ev_run's to stop iterating
           
	if (true)
	{
        std::cout << "Bytes/per second = " <<
                  std::setfill(' ') <<  std::setw(10) << (m_bytesRx) << std::endl;
	m_bytesRx=0;
        m_start = std::chrono::steady_clock::now();
	}
    }

    void clear()
    {
	int rc =-1;
        char rxBuff[512];
        do
        {
            memset(rxBuff, 0, sizeof(rxBuff));
            rc =read(m_fd, rxBuff, sizeof(rxBuff));

            if ( errno == -EAGAIN || rc <=0 )
            {
                break;
            }

        }
        while(rc>0);
    }
    void readCallback(struct ev_loop *loop, ev_io *w, int revents)
    {
        //OK got read back
        std::string _readStr;
        char rxBuff[512];

        int rc = -1;
	int lenRead = 0;

            memset(rxBuff, 0, sizeof(rxBuff));
        do
        {

            rc =read(m_fd, rxBuff, sizeof(rxBuff));

            if ( errno == -EAGAIN || rc <=0 )
            {
                break;
            }
            else
                _readStr +=rxBuff;

	    lenRead+=rc;

        }
        while(rc && _readStr.length() < 512);

        if (_readStr != m_postMsg)
        {
            std::cerr << "Error: strings don't match" << "str sent: " 
                        << m_postMsg << "  str rx: " << _readStr<< std::endl;
	    std::cerr << "counter " << m_postMsgCnt << " len read: " << lenRead << std::endl;
	    printf("Buffer : %s\n", rxBuff);
	    ev_break (loop, EVBREAK_ALL);
        }
        else
        {
          
          m_bytesRx+= _readStr.length();

            //pet the dog to prevent exit..
            ev_timer_again (loop, &m_watchDog);

            //trigger another write
            postMessage();
        }
    }


    bool addWatcher(struct ev_loop *loop)
    {
        m_callback = std::bind(&rpmsg::readCallback, this, std::placeholders::_1, 
                               std::placeholders::_2, std::placeholders::_3);

        setNonBlocking();

        m_evIO.data = (void *)&m_callback;
        ev_init (&m_evIO, genericCallback);
        ev_io_set (&m_evIO, m_fd, EV_READ);
	ev_io_start(loop, &m_evIO);

        m_timerCB = std::bind(&rpmsg::watchDogCallback, this, std::placeholders::_1, std::placeholders::_2, 
                              std::placeholders::_3);
        
        m_watchDog.data = (void *)&m_timerCB;
        ev_timer_init (&m_watchDog, timerCallback,2. , 2.);
        ev_timer_start (loop, &m_watchDog);

        m_timerCB = std::bind(&rpmsg::calBandWidth, this, std::placeholders::_1, std::placeholders::_2, 
                              std::placeholders::_3);

        m_bandWidthTimer.data = (void *)&m_timerCB;
        ev_timer_init (&m_bandWidthTimer, timerCallback, 2, 2.);
        ev_timer_start (loop, &m_bandWidthTimer);

        
        //kick the loop back with posting a write
	clear();
        postMessage();
        m_start = std::chrono::steady_clock::now();
    }
};

static void
sigint_cb (struct ev_loop *loop, ev_signal *w, int revents)
{
    ev_break (loop, EVBREAK_ALL);
}



int main(int argc, char **argv)
{

    struct ev_loop *loop = ev_default_loop (ev_recommended_backends () );

    ev_signal signal_watcher;
    ev_signal_init (&signal_watcher, sigint_cb, SIGINT);
    ev_signal_start (loop, &signal_watcher);


    std::string rpmsgDevName("/dev/rpmsg0");
    int rpmsgFileHandle;
    
    rpmsgFileHandle = open(rpmsgDevName.c_str(),  O_RDWR , S_IRUSR | S_IWUSR);

    if (rpmsgFileHandle < 0)
    {
        std::cout << "Not open: " << rpmsgFileHandle << std::endl;
        return -1;
    }
    else
    {
        std::cout << "Open'ed : " << rpmsgFileHandle << std::endl;
    }

    rpmsg rpmsg0(rpmsgFileHandle);
    rpmsg0.addWatcher(loop);

    std::cout << std::endl;

    ev_run (loop, 0);
}

//eof



