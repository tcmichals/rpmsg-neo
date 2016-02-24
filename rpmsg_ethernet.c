/* * RPMSG Proxy Device Kernel Driver
 *
 * Copyright (C) 2016 Tim Michals
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
#include <linux/version.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/ioctl.h>
#include <linux/errno.h>


/* Ethernet info */
#include <linux/sched.h>

#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/interrupt.h> /* mark_bh */

#include <linux/in.h>
#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>          /* struct iphdr */
#include <linux/tcp.h>         /* struct tcphdr */
#include <linux/skbuff.h>

#include <linux/in6.h>
#include <asm/checksum.h>

#include "rpmsg_neo.h"
#include "rpmsg_neoproxy.h"



struct _rpmsg_dev_params {

        struct device *rpmsg_dev;
        struct rpmsg_channel *rpmsg_chnl;
        struct rpmsg_endpoint *ept;
        char tx_buff[MAX_RPMSG_BUFF_SIZE]; /* buffer to keep the message to send */
        struct net_device_stats stats;
        struct net_device *dev;
        spinlock_t lock;
        int endpt;
};


static struct net_device *rpmsg_netdev;

/*
 * The higher levels take care of making this non-reentrant (it's
 * called with bh's disabled).
 */
static netdev_tx_t rpmsg_ether_xmit(struct sk_buff *skb,
                            struct net_device *dev)
{
    struct _rpmsg_dev_params *priv = netdev_priv(dev);
    int len =0;
    int err =0;

  //  skb_orphan(skb);

    /* Before queueing this packet to netif_rx(),
     * make sure dst is refcounted.
     */
   // skb_dst_force(skb);

   // skb->protocol = eth_type_trans(skb, dev);

    len = skb->len;
    
    if (len > ETHERNET_PDU_SIZE)
      len = ETHERNET_PDU_SIZE;
  
    spin_lock_bh(&priv->lock);

    err = rpmsg_trysendto(priv->rpmsg_chnl,
                          skb->data, 
                          len, 
                          priv->endpt);
    
    spin_unlock_bh(&priv->lock);


    if (err < 0)
        pr_err("ERROR: %s %s %d rc=%d no pkts\n", __FILE__, __FUNCTION__, __LINE__,err);

    dev_kfree_skb_any(skb);

    return NETDEV_TX_OK;
}


static void rpmsg_read_mac_addr(struct net_device *dev)
{
    int i=0;
    for (i = 0; i < ETH_ALEN; i++)
        dev->dev_addr[i] = 0;
    
    dev->dev_addr[ETH_ALEN-1] = 1;
}

static int rpmsg_ether_open(struct net_device *ndev)
{
    rpmsg_read_mac_addr(ndev);
    netif_start_queue(ndev);

    return 0;
}


int rpmsg_ether_stop (struct net_device *dev)
{
    pr_info ("stop called\n");
    netif_stop_queue(dev);
    return 0;
}


/*
 * Configuration changes (passed on by ifconfig)
 */
int rpmsg_ether_config(struct net_device *dev, struct ifmap *map)
{
    if (dev->flags & IFF_UP) /* can't act on a running interface */
        return -EBUSY;

    /* ignore other fields */
    return 0;
}



struct net_device_stats *rpmsg_ether_stats(struct net_device *dev)
{
    struct _rpmsg_dev_params *priv = netdev_priv(dev);
    return &priv->stats;
}


static u32 always_on(struct net_device *dev)
{
    return 1;
}


static const struct ethtool_ops rpmsg_ethtool_ops = {
    .get_link           = always_on,
};



static const struct net_device_ops rpmsg_netdev_ops = {
    .ndo_open           = rpmsg_ether_open,
    .ndo_stop           = rpmsg_ether_stop,
    .ndo_start_xmit     = rpmsg_ether_xmit,
    .ndo_set_config     = rpmsg_ether_config,
    .ndo_validate_addr  = eth_validate_addr,
    .ndo_get_stats      = rpmsg_ether_stats,

};


static void rpmsg_ethernet_dev_ept_cb(struct rpmsg_channel *rpdev, void *data,
                                        int len, void *priv, u32 src)
{

        struct _rpmsg_dev_params *local = dev_get_drvdata(&rpdev->dev);
        struct sk_buff *skb;
        
        spin_lock_bh(&local->lock);

        skb= dev_alloc_skb(len);
      
        skb_reserve(skb, 2); /* align IP on 16B boundary */  
        memcpy(skb_put(skb, len), data, len);

        skb->dev = rpmsg_netdev;
        skb->protocol = eth_type_trans(skb, rpmsg_netdev);
        skb->ip_summed = CHECKSUM_UNNECESSARY; /* don't check it */
        local->stats.rx_packets++;
        local->stats.rx_bytes += len;
        netif_rx(skb);
       spin_unlock_bh(&local->lock);


        return;
}


static int rpmsg_neo_ethernet_remove(void )
{
 //FIX up   unregister_rpmsg_driver(&rpmsg_ethernet_dev_drv);
}

int rpmsg_neo_ethernet(struct rpmsg_channel *rpmsg_chnl,
                       rpmsg_neo_remove_t *remove_func  )
{

    int result, ret = -ENOMEM;
    struct _rpmsg_dev_params *priv;
    
    pr_info("INFO:%s %d\n", __FUNCTION__, __LINE__);
    
    *remove_func = rpmsg_neo_ethernet_remove;

    rpmsg_netdev = alloc_etherdev(sizeof(struct _rpmsg_dev_params));
    if (rpmsg_netdev ==NULL) {
        pr_err("ERROR: %s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
        return ret;
    }
    
    rpmsg_netdev->ethtool_ops    = &rpmsg_ethtool_ops;
    rpmsg_netdev->netdev_ops = &rpmsg_netdev_ops;
    rpmsg_netdev->mtu            = ETHERNET_MTU_SIZE;

    priv = netdev_priv(rpmsg_netdev);
    memset(priv, 0, sizeof(*priv));

    pr_info("INFO: %s %s %d\n", __FILE__, __FUNCTION__, __LINE__);

    priv->dev = rpmsg_netdev;

    rpmsg_read_mac_addr(rpmsg_netdev);

  // call 
    priv->rpmsg_chnl = rpmsg_chnl;
    priv->endpt = ETHERNET_ENDPOINT;
    
   spin_lock_init(&priv->lock);

    
    priv->ept = rpmsg_create_ept(priv->rpmsg_chnl,
                                 rpmsg_ethernet_dev_ept_cb,
                                  priv,
                                  priv->endpt);


    if (!priv->ept)
    {
      pr_err("ERROR: %s %d Failed to create endpoint.\n",  __FUNCTION__, __LINE__);
        goto out;
    }

    result = register_netdev(rpmsg_netdev);
    if (result) {
        pr_err("ERROR: %s %s %d\n", __FILE__, __FUNCTION__, __LINE__);

    } else {
           
        ret = 0;
        pr_info("INFO: %s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
    }

    return 0;

out:
    if(rpmsg_netdev) {
        pr_err("ERROR: %s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
        return -1;
    }

    return ret;
}




