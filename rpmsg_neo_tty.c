/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * derived from the omap-rpmsg implementation.
 * Remote processor messaging transport - tty driver
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/virtio.h>

#include "rpmsg_neo.h"
#include "rpmsg_neoproxy.h"

#define RPMSG_MAX_SIZE	MAX_RPMSG_BUFF_SIZE
#define MSG		"hello world!"

/*
 * struct rpmsgtty_port - Wrapper struct for imx rpmsg tty port.
 * @port:		TTY port data
 */
struct rpmsgtty_port
{
    struct tty_port		port;
    spinlock_t		rx_lock;
    struct rpmsg_channel   *rpmsg_chnl;
    struct rpmsg_endpoint   *ept;
    char tx_buff[RPMSG_MAX_SIZE]; /* buffer to keep the message to send */
    int endpt;

};

static struct rpmsgtty_port rpmsg_tty_port;

static void rpmsg_tty_cb(struct rpmsg_channel *rpdev, void *data, int len,
                         void *priv, u32 src)
{
    int space;
    unsigned char *cbuf;
    struct rpmsgtty_port *cport = (struct rpmsgtty_port *)priv;

    /* flush the recv-ed none-zero data to tty node */
    if (len == 0)
        return;
 /*
    pr_info("%s lenrcved=%d\n", __FUNCTION__, len);
    print_hex_dump(KERN_DEBUG, __func__, DUMP_PREFIX_NONE, 16, 1,
                    data, len,  true);
*/                    

    spin_lock_bh(&cport->rx_lock);
    space = tty_prepare_flip_string(&cport->port, &cbuf, len);
    if (space <= 0)
    {
        dev_err(&rpdev->dev, "No memory for tty_prepare_flip_string\n");
        spin_unlock_bh(&cport->rx_lock);
        return;
    }

    if( space != len)
        pr_err("Trunc buffer %d\n", len-space);

    memcpy(cbuf, data, space);
    tty_flip_buffer_push(&cport->port);
    spin_unlock_bh(&cport->rx_lock);
}

static struct tty_port_operations  rpmsgtty_port_ops = { };

static int rpmsgtty_install(struct tty_driver *driver, struct tty_struct *tty)
{
    return tty_port_install(&rpmsg_tty_port.port, driver, tty);
}

static int rpmsgtty_open(struct tty_struct *tty, struct file *filp)
{
    return tty_port_open(tty->port, tty, filp);
}

static void rpmsgtty_close(struct tty_struct *tty, struct file *filp)
{
    return tty_port_close(tty->port, tty, filp);
}

static int rpmsgtty_write(struct tty_struct *tty, const unsigned char *buf,
                          int total)
{
    int count, ret = 0;
    const unsigned char *tbuf;
    struct rpmsgtty_port *rptty_port = container_of(tty->port,
                                       struct rpmsgtty_port, port);
    struct rpmsg_channel *rpmsg_chnl = rptty_port->rpmsg_chnl;

    if (NULL == buf)
    {
        pr_err("buf shouldn't be null.\n");
        return -ENOMEM;
    }

    count = total;
    tbuf = buf;

    do
    {
/*
pr_info("%s lentx=%d\n", __FUNCTION__, total);
 	print_hex_dump(KERN_DEBUG, __func__, DUMP_PREFIX_NONE, 16, 1,buf, total,  true);
*/
        /* send a message to our remote processor */
        ret = rpmsg_sendto(rpmsg_chnl, (void *)tbuf,
                           count > RPMSG_MAX_SIZE ? RPMSG_MAX_SIZE : count, rptty_port->endpt);
        if (ret)
        {
            dev_err(&rpmsg_chnl->dev, "rpmsg_send failed: %d\n", ret);
            return ret;
        }

        if (count > RPMSG_MAX_SIZE)
        {
            count -= RPMSG_MAX_SIZE;
            tbuf += RPMSG_MAX_SIZE;
        }
        else
        {
            count = 0;
        }
    }
    while (count > 0);

    return total;
}

static int rpmsgtty_write_room(struct tty_struct *tty)
{
    /* report the space in the rpmsg buffer */
    return RPMSG_MAX_SIZE;
}

static const struct tty_operations imxrpmsgtty_ops =
{
    .install		= rpmsgtty_install,
    .open			= rpmsgtty_open,
    .close			= rpmsgtty_close,
    .write			= rpmsgtty_write,
    .write_room		= rpmsgtty_write_room,
};



static struct tty_driver *rpmsgtty_driver;


static int rpmsg_neo_tty_remove(void )
{
    struct rpmsgtty_port *cport = &rpmsg_tty_port;

    pr_info("INFO: %s %s %d\n", __FILE__, __FUNCTION__, __LINE__);

    tty_unregister_driver(rpmsgtty_driver);
    put_tty_driver(rpmsgtty_driver);
    tty_port_destroy(&cport->port);
    rpmsgtty_driver = NULL;

    return 0;

}


int rpmsg_neo_tty(struct rpmsg_channel *rpmsg_chnl,rpmsg_neo_remove_t *remove_func )
{
    int err = 0;
    struct rpmsgtty_port *cport = &rpmsg_tty_port;

    *remove_func =  rpmsg_neo_tty_remove;

    memset(cport, 0, sizeof(rpmsg_tty_port));

    cport->rpmsg_chnl = rpmsg_chnl;
    cport->endpt = RPMSG_TTY_ENPT;

    cport->ept = rpmsg_create_ept(cport->rpmsg_chnl,
                                  rpmsg_tty_cb,
                                  cport,
                                  cport->endpt);
    if (!cport->ept)
    {
        pr_err("ERROR: %s %s %d Failed to create proxy service endpoint.\n", __FILE__, __FUNCTION__, __LINE__);
        err = -1;
        goto error0;
    }
    
    rpmsgtty_driver = tty_alloc_driver(1, TTY_DRIVER_RESET_TERMIOS |
			TTY_DRIVER_REAL_RAW |
			TTY_DRIVER_UNNUMBERED_NODE);
    if (IS_ERR(rpmsgtty_driver))
    {
        pr_err("ERROR:%s %d Failed to alloc tty\n", __FUNCTION__, __LINE__);
        rpmsg_destroy_ept(cport->ept);
        return PTR_ERR(rpmsgtty_driver);
    }
         
    spin_lock_init(&cport->rx_lock);
    cport->port.low_latency = cport->port.flags | ASYNC_LOW_LATENCY;
    
    tty_port_init(&cport->port);
    cport->port.ops = &rpmsgtty_port_ops;
    
    rpmsgtty_driver->driver_name = "ttyrpmsg";
    rpmsgtty_driver->name = "ttyrpmsg";
    rpmsgtty_driver->major = TTYAUX_MAJOR;
    rpmsgtty_driver->minor_start = 4;
    rpmsgtty_driver->type = TTY_DRIVER_TYPE_CONSOLE;
    rpmsgtty_driver->init_termios = tty_std_termios;
  //  rpmsgtty_driver->init_termios.c_oflag = OPOST | OCRNL | ONOCR | ONLRET;
rpmsgtty_driver->init_termios.c_cflag |= CLOCAL;

    tty_set_operations(rpmsgtty_driver, &imxrpmsgtty_ops);
    tty_port_link_device(&cport->port, rpmsgtty_driver, 0);
        
    err = tty_register_driver(rpmsgtty_driver);
    if (err < 0)
    {
        pr_err("Couldn't install rpmsg tty driver: err %d\n", err);
        goto error;
    }
    else
    {
        pr_info("Install rpmsg tty driver!\n");
    }

    return 0;

error:
    put_tty_driver(rpmsgtty_driver);
    tty_port_destroy(&cport->port);
    rpmsgtty_driver = NULL;
    rpmsg_destroy_ept(cport->ept);


error0:
    return err;

}






