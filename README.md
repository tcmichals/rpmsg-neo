# rpmsg-neo
General rpmsg for TTY and usr space interface using multiple endpoints

The current implmentation is for the Udoo Neo (http://www.udoo.org/udoo-neo/)  
Linux on A9 (rpmsg) <-----> (rpmsg) M4 on FreeRTOS 

The goal is to have one general Linux driver to support (1) Channel and (2) endpoints
- endpt 127 is for usr space 
- endpt 126 is for tty usr space
- endpt 125 is for Ethernet driver. Linux (Ethernet) (rpmsg) <-----> rpmsg LwIP stack FreeRTOS (M4)


usr-neoproxy.c is a user app with epoll to access rpmsg usr space app.  This more work to do for the protocol.

