#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/timerfd.h>
#include <getopt.h>
#include <sys/epoll.h>

//C++
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <sstream>


//user
#include "rpmsg_neoproxy.h"

#define MAXEVENTS 64

//handle to rpmsg
static int rpmsgFileHandle = -1;


typedef struct
{
    int id;
} eventInfo_t;

int addToEpoll( int fd, int efd, int evtFlags, void *usrData)
{

    struct epoll_event ev;
    int rc = 0;

    ev.data.fd = fd;
    ev.events = evtFlags;
    ev.data.ptr = usrData;

    if( (rc = epoll_ctl(efd,EPOLL_CTL_ADD,fd,&ev)) < 0)
    {
        std::cerr << "Error: rc="<<rc << "  " << strerror(errno) << std::endl;
        return -1;
    }

    return 0;
}



void exit_action_handler(int signum)
{
    std::cout << __FUNCTION__ << std::endl;
    /* Close proxy rpmsg device */
    if ( rpmsgFileHandle >= 0)
    {
        close(rpmsgFileHandle);
    }
}


void kill_action_handler(int signum)
{

    std::cout << __FUNCTION__ << std::endl;

    /* Close proxy rpmsg device */
    if ( rpmsgFileHandle >= 0)
    {
        close(rpmsgFileHandle);
    }

}


int main(int argc, char *argv[])
{
    struct sigaction exit_action;
    struct sigaction kill_action;
    int i = 0;
    int loopCnt =0;


    /* Initialize signalling infrastructure */
    memset(&exit_action, 0, sizeof(struct sigaction));
    memset(&kill_action, 0, sizeof(struct sigaction));
    exit_action.sa_handler = exit_action_handler;
    kill_action.sa_handler = kill_action_handler;
    sigaction(SIGTERM, &exit_action, NULL);
    sigaction(SIGINT, &exit_action, NULL);
    sigaction(SIGKILL, &kill_action, NULL);
    sigaction(SIGHUP, &kill_action, NULL);

    std::string rpmsgDevName("/dev/rpmsg0");
    std::string sentStr;


    rpmsgFileHandle = open(rpmsgDevName.c_str(),  O_RDWR , S_IRUSR | S_IWUSR);

    std::cout << "Debug " << __LINE__ << std::endl;

    if (rpmsgFileHandle < 0)
    {
        std::cout << "Not open: " << rpmsgFileHandle << std::endl;
        return -1;
    }
    else
    {
        std::cout << "Open: " << rpmsgFileHandle << std::endl;

    }

    //Set handle to non-blocking
    fcntl(rpmsgFileHandle, F_SETFL, O_NONBLOCK);

    struct epoll_event epoll_events[MAXEVENTS];
    eventInfo_t evtData[MAXEVENTS];

    int efd = 0;
    int rval =0;

    efd = epoll_create1(EPOLL_CLOEXEC);

    if (efd < 0)
    {
        std::cerr << "ERROR: Could not create the epoll fd:" <<std::endl;
        return 1;
    }

    if ( addToEpoll(rpmsgFileHandle, efd, EPOLLIN | EPOLLET, (void *)&evtData[0]))
    {
        std::cerr << "Couldn't add server socket to epoll set: " <<  rpmsgFileHandle << std::endl;
        close(rpmsgFileHandle);
        return -1;
    }

    {
        ssize_t rc =0;

        std::stringstream ss;
        ss << "Hello from A9 " << 0;
        sentStr = ss.str();
        rc =write(rpmsgFileHandle, (void *)ss.str().c_str(), ss.str().length());
        if (rc < 1)
            std::cout << "ERROR: rc " << rc << std::endl;
        else
            std::cout << "OK: sent hello " << rc << std::endl;
    }

    std::cout << "epoll wait " << std::endl;

    while ((rval = epoll_wait(efd, epoll_events,MAXEVENTS, 1500)) >=0)
    {
        if ((rval < 0) && (errno != EINTR))
        {
            std::cout << "EPoll failed" << std::endl;
            close(rpmsgFileHandle);
            return -1;
        }
        else if (rval == 0)
        {
            std::cout << "timeout " << std::endl;
            continue;

        }

        //got an event...
        for (int indx =0; indx < rval; ++indx)
        {
            //OK got a read event read everything...
            ssize_t rc =0;
            do
            {
                char rxBuff[512];

                memset(rxBuff, 0, sizeof(rxBuff));
                rc =read(rpmsgFileHandle, rxBuff, sizeof(rxBuff));

                if ( errno == -EAGAIN || rc <=0 )
                {
                    break;
                }
                if (sentStr.length() != rc || strncmp(sentStr.c_str(), rxBuff, rc) )
                    std::cout << "Error buffer not the same " << sentStr << "  " << rxBuff <<  std::endl;


            }
            while(true);

            std::stringstream ss;
            ss << "Hello from A9 " << loopCnt++;
            sentStr = ss.str();
            rc =write(rpmsgFileHandle, (void *)ss.str().c_str(), ss.str().length());
            if (rc < 1)
                std::cout << "ERROR: rc " << rc << std::endl;


        }
    }

    std::cout << "While terminated rval= " << rval << std::endl;


    return 0;
}

