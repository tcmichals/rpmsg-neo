/*
 * RPMSG Proxy Device Kernel Driver
 *
 * Copyright (C) 2014 Mentor Graphics Corporation
 * Copyright (C) 2015 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/poll.h>

#include "rpmsg_neo.h"
#include "rpmsg_neoproxy.h"

#define RPMSG_KFIFO_SIZE                (MAX_RPMSG_BUFF_SIZE * 4)

#define IOCTL_CMD_GET_KFIFO_SIZE        1
#define IOCTL_CMD_GET_AVAIL_DATA_SIZE   2
#define IOCTL_CMD_GET_FREE_BUFF_SIZE    3


#define RPMG_INIT_MSG "init_msg"

struct _rpmsg_params
{
    wait_queue_head_t usr_wait_q;
    struct mutex sync_lock;
    struct kfifo rpmsg_kfifo;
    int block_flag;
    struct rpmsg_channel *rpmsg_chnl;
    struct rpmsg_endpoint *ept;
    char tx_buff[RPMSG_KFIFO_SIZE]; /* buffer to keep the message to send */
    u32 endpt;
};

struct _rpmsg_device
{
    struct miscdevice 	  device;
    struct _rpmsg_params  rpmsg_params;
    int                   endpt;
};



static int rpmsg_dev_open(struct inode *inode, struct file *filp)
{
    /* Initialize rpmsg instance with device params from inode */
    struct _rpmsg_device *_prpmsg_device = (struct _rpmsg_device *)filp->private_data;
    struct _rpmsg_params *local = ( struct _rpmsg_params *)&_prpmsg_device->rpmsg_params;

    local;

    return nonseekable_open(inode, filp);
}

static ssize_t rpmsg_dev_write(struct file *filp,
                               const char __user *ubuff, size_t len,
                               loff_t *p_off)
{
    struct _rpmsg_device *_prpmsg_device = (struct _rpmsg_device *)filp->private_data;
    struct _rpmsg_params *local = ( struct _rpmsg_params *)&_prpmsg_device->rpmsg_params;

    int err;
    unsigned int size;

    if (len < MAX_RPMSG_BUFF_SIZE)
        size = len;
    else
        size = MAX_RPMSG_BUFF_SIZE;

    if (copy_from_user(local->tx_buff, ubuff, size))
    {
        pr_err("%s: user to kernel buff copy error.\n", __func__);
        return -1;
    }

    err = rpmsg_sendto(local->rpmsg_chnl,
                       local->tx_buff,
                       size,
                       local->endpt);
    if (err)
    {
        pr_err("rpmsg_sendto (size = %d) error: %d\n", size, err);
        size = 0;
    }

    return size;
}

static ssize_t rpmsg_dev_read(struct file *filp, char __user *ubuff,
                              size_t len, loff_t *p_off)
{
    struct _rpmsg_device *_prpmsg_device = (struct _rpmsg_device *)filp->private_data;
    struct _rpmsg_params *local = ( struct _rpmsg_params *)&_prpmsg_device->rpmsg_params;

    int retval;
    unsigned int data_available, data_used, bytes_copied;

    /* Acquire lock to access rpmsg kfifo */
    static int count = 0;
    while (mutex_lock_interruptible(&local->sync_lock))
    {
        if (!count)
        {
            pr_info("%s: error = %d.\n", __func__,mutex_lock_interruptible(&local->sync_lock));
            count++;
        }
    }

    data_available = kfifo_len(&local->rpmsg_kfifo);

    if (data_available ==  0)
    {
        /* Release lock */
        mutex_unlock(&local->sync_lock);

        /* if non-blocking read is requested return error */
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;

        /* Block the calling context till data becomes available */
        wait_event_interruptible(local->usr_wait_q,
                                 local->block_flag != 0);
        while (mutex_lock_interruptible(&local->sync_lock));
    }

    /* reset block flag */
    local->block_flag = 0;

    /* Provide requested data size to user space */
    data_available = kfifo_len(&local->rpmsg_kfifo);
    data_used = (data_available > len) ? len : data_available;
    retval = kfifo_to_user(&local->rpmsg_kfifo, ubuff, data_used, &bytes_copied);

    /* Release lock on rpmsg kfifo */
    mutex_unlock(&local->sync_lock);

    return retval ? retval : bytes_copied;
}

static long rpmsg_dev_ioctl(struct file *filp, unsigned int cmd,
                            unsigned long arg)
{
    unsigned int tmp;
    struct _rpmsg_device *_prpmsg_device = (struct _rpmsg_device *)filp->private_data;
    struct _rpmsg_params *local = ( struct _rpmsg_params *)&_prpmsg_device->rpmsg_params;

    switch (cmd)
    {
    case IOCTL_CMD_GET_KFIFO_SIZE:
        tmp = kfifo_size(&local->rpmsg_kfifo);
        if (copy_to_user((unsigned int *)arg, &tmp, sizeof(int)))
            return -EACCES;
        break;

    case IOCTL_CMD_GET_AVAIL_DATA_SIZE:
        tmp = kfifo_len(&local->rpmsg_kfifo);
        pr_info("kfifo len ioctl = %d ", kfifo_len(&local->rpmsg_kfifo));
        if (copy_to_user((unsigned int *)arg, &tmp, sizeof(int)))
            return -EACCES;
        break;
    case IOCTL_CMD_GET_FREE_BUFF_SIZE:
        tmp = kfifo_avail(&local->rpmsg_kfifo);
        if (copy_to_user((unsigned int *)arg, &tmp, sizeof(int)))
            return -EACCES;
        break;

    default:
        return -EINVAL;
    }

    return 0;
}

static unsigned int rpmsg_dev_poll(struct file *filp, poll_table *wait)
{
    unsigned int mask = POLLOUT | POLLWRNORM; //POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;
    struct _rpmsg_device *_prpmsg_device = (struct _rpmsg_device *)filp->private_data;
    struct _rpmsg_params *local = ( struct _rpmsg_params *)&_prpmsg_device->rpmsg_params;


    if( local)
    {
        unsigned int data_available;

        if (mutex_lock_interruptible(&local->sync_lock))
            return mask;

        poll_wait(filp,&local->usr_wait_q, wait );

        data_available = kfifo_len(&local->rpmsg_kfifo);

        if (data_available)
        {
            mask |= POLLIN | POLLRDNORM;
        }
    }

    mutex_unlock(&local->sync_lock);
    return mask;
}


static int rpmsg_dev_release(struct inode *inode, struct file *p_file)
{
    return 0;
}

static void rpmsg_proxy_dev_ept_cb(struct rpmsg_channel *rpdev, void *data,
                                   int len, void *priv, u32 src)
{

    struct _rpmsg_params *local = ( struct _rpmsg_params *)priv;

    while(mutex_lock_interruptible(&local->sync_lock));
    if (kfifo_avail(&local->rpmsg_kfifo) < len)
    {
        mutex_unlock(&local->sync_lock);
        return;
    }

    kfifo_in(&local->rpmsg_kfifo, data, (unsigned int)len);

    mutex_unlock(&local->sync_lock);

    /* Wake up any blocking contexts waiting for data */
    local->block_flag = 1;
    wake_up_interruptible(&local->usr_wait_q);

}
static const struct file_operations rpmsg_dev_fops =
{
    .owner = THIS_MODULE,
    .read = rpmsg_dev_read,
    .write = rpmsg_dev_write,
    .open = rpmsg_dev_open,
    .unlocked_ioctl = rpmsg_dev_ioctl,
    .release = rpmsg_dev_release,
    .llseek =	no_llseek,
    .poll		= rpmsg_dev_poll,

};


static struct _rpmsg_device rpmsg_miscdevice0 =
{
    .device.minor = MISC_DYNAMIC_MINOR,
    .device.name  = "rpmsg0",
    .device.fops = &rpmsg_dev_fops,
    .endpt = RPMSG_PROXY_ENDPOINT,

};


static int rpmsg_neo_proxy_remove(void )
{
    misc_deregister(&rpmsg_miscdevice0.device);

    return 0;

}

static int init_neo_proxy(struct _rpmsg_params *local, struct rpmsg_channel *rpmsg_chnl)
{
    int status =0;

    /* Initialize mutex */
    mutex_init(&local->sync_lock);

    /* Initialize wait queue head that provides blocking rx for userspace */
    init_waitqueue_head(&local->usr_wait_q);

    /* Allocate kfifo for rpmsg */
    status = kfifo_alloc(&local->rpmsg_kfifo, RPMSG_KFIFO_SIZE, GFP_KERNEL);
    kfifo_reset(&local->rpmsg_kfifo);

    if (status)
    {
        pr_err("ERROR: %s %d Failed to run kfifo_alloc. rc=%d\n", __FUNCTION__, __LINE__,status);
        goto error0;
    }

    local->rpmsg_chnl = rpmsg_chnl;
    local->block_flag = 0;

    local->ept = rpmsg_create_ept(local->rpmsg_chnl,
                                  rpmsg_proxy_dev_ept_cb,
                                  local,
                                  local->endpt);
    if (!local->ept)
    {
        pr_err("ERROR: %s %d Failed to create endpoint.\n",  __FUNCTION__, __LINE__);
        goto error1;
    }
    goto out;

//TCM        rpmsg_destroy_ept(local->ept);
error1:
    kfifo_free(&local->rpmsg_kfifo);
error0:

    pr_err("ERROR: %s %d\n",  __FUNCTION__, __LINE__);
    return -ENODEV;
out:
    pr_info("%s %d\n",  __FUNCTION__, __LINE__);
    return 0;
}

int rpmsg_neo_proxy(struct rpmsg_channel *rpmsg_chnl,rpmsg_neo_remove_t *remove_func )
{
    int err = 0;

    *remove_func =  rpmsg_neo_proxy_remove;

    pr_info(" %s %d\n",  __FUNCTION__, __LINE__);

    memset(&rpmsg_miscdevice0.rpmsg_params, 0, sizeof(struct _rpmsg_params));

    rpmsg_miscdevice0.rpmsg_params.endpt = rpmsg_miscdevice0.endpt;

    if ((err= init_neo_proxy(&rpmsg_miscdevice0.rpmsg_params, rpmsg_chnl)))
    {
        pr_err("ERROR:  %s %d rc=%d\n", __FUNCTION__, __LINE__,err);
        goto error0;
    }

    err = misc_register(&rpmsg_miscdevice0.device);
    if(err)
    {
        pr_err("ERROR:  %s %d rc=%d\n",  __FUNCTION__, __LINE__,err);
    }
    else
    {
        pr_info("Loaded:  %s %d\n",  __FUNCTION__, __LINE__);

    }

error0:
    return err;

}

