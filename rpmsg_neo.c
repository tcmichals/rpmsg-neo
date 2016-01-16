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


#define RPMSG_MAX_SIZE		(512 - sizeof(struct rpmsg_hdr))
#define MSG		"hello world!"


struct _rpmsg_dev_params
{
    struct device *rpmsg_dev;
    struct rpmsg_channel *rpmsg_chnl;
    rpmsg_neo_remove_t remove_proxy;
    rpmsg_neo_remove_t remove_tty;
    rpmsg_neo_remove_t remove_ethernet;

};


static int rpmsg_proxy_dev_rpmsg_drv_probe(struct rpmsg_channel *rpdev);

static void rpmsg_proxy_dev_rpmsg_drv_remove(struct rpmsg_channel *rpdev);

static struct rpmsg_device_id rpmsg_proxy_dev_drv_id_table[] =
{
    { .name = "rpmsg-openamp-demo-channel" },
    {},
};

static struct rpmsg_driver rpmsg_proxy_dev_drv =
{
    .drv.name = "rpmsg_proxy_dev_rpmsg",
    .drv.owner = THIS_MODULE,
    .id_table = rpmsg_proxy_dev_drv_id_table,
    .probe = rpmsg_proxy_dev_rpmsg_drv_probe,
    .remove = rpmsg_proxy_dev_rpmsg_drv_remove,
    .callback = rpmsg_proxy_dev_rpmsg_drv_cb,
};

static int rpmsg_proxy_dev_rpmsg_drv_probe(struct rpmsg_channel *rpdev)
{
    struct _rpmsg_dev_params *local;
    int status=0;
    int err=0;

    pr_info("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);

    local = devm_kzalloc(&rpdev->dev, sizeof(struct _rpmsg_dev_params),
                         GFP_KERNEL);

    if (!local)
    {
        dev_err(&rpdev->dev, "Failed to allocate memory for rpmsg user dev.\n");
        return -ENOMEM;
    }


    dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!\n",
             rpdev->src, rpdev->dst);

    /*
    * send a message to our remote processor, and tell remote
     * processor about this channel
     */
    err = rpmsg_send(rpdev, MSG, strlen(MSG));
    if (err)
    {
        dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", err);
        return error2;
    }

    memset(local, 0x0, sizeof(struct _rpmsg_dev_params));

    local->rpmsg_chnl = rpdev;

    dev_set_drvdata(&rpdev->dev, local);


    if ( rpmsg_neo_proxy(local->rpmsg_chnl, local->remove_proxy) || local->remove_proxy==NULL)
    {
        dev_err(&rpdev->dev, "Failed: rpmsg_neo_proxy\n");
        goto error2;

    }

    if ( rpmsg_neo_tty(local->rpmsg_chnl, local->remove_tty) ||  local->remove_tty==NULL)
    {
        dev_err(&rpdev->dev, "Failed: rpmsg_neo_tty\n");

        if(local->remove_proxy)
            local->remove_proxy();

        goto error2;

    }

    sprintf(local->tx_buff, RPMG_INIT_MSG);

    if (rpmsg_send(local->rpmsg_chnl,
                   local->tx_buff,
                   sizeof(RPMG_INIT_MSG)))
    {

        pr_err("Failed to send init_msg to target 0x%x.", rpdev->dst);

        local->remove_proxy();
        local->remove_tty();
        goto error2;
    }


    goto out;
error2:
    return -ENODEV;
out:
    return 0;
}

static void rpmsg_proxy_dev_rpmsg_drv_remove(struct rpmsg_channel *rpdev)
{
    struct _rpmsg_device *local = dev_get_drvdata(&rpdev->dev);

}



static int __init rpmsg_init(void)
{
    int err = 0;

    pr_info("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);

    if ( (err = register_rpmsg_driver(&rpmsg_proxy_dev_drv)) != 0)
        else
        {
            pr_err("ERROR: %s %s %d rc=%d\n", __FILE__, __FUNCTION__, __LINE__,err);

        }

    return err;
}

static void __exit rpmsg_exit(void)
{
    unregister_rpmsg_driver(&rpmsg_proxy_dev_drv);
}




module_init(rpmsg_init);
module_exit(rpmsg_exit);

MODULE_AUTHOR("Tim Michals <tcmichals@gmail.com>");
MODULE_DESCRIPTION("rpmsg proxy user access driver");
MODULE_LICENSE("GPL v2");
