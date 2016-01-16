

//#include <linux/rpmsg.h>

#define IOCTL_CMD_GET_KFIFO_SIZE        1
#define IOCTL_CMD_GET_AVAIL_DATA_SIZE   2
#define IOCTL_CMD_GET_FREE_BUFF_SIZE    3


#define NEOPROXY_ENDPOINT   128


/* operations definitions */
#define OPEN_SYSCALL_ID         1
#define CLOSE_SYSCALL_ID        2
#define WRITE_SYSCALL_ID        3
#define READ_SYSCALL_ID         4
#define ACK_STATUS_ID           5
#define TERM_SYSCALL_ID         6

struct neo_proxy_msg
{
    uint32_t  operation;
    uint8_t   data[0];  
};


#define MAX_RPMSG_BUFF_SIZE             (512-sizeof(struct rpmsg_hdr)-sizeof(struct neo_proxy_msg))

