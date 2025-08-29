#ifndef EMW3080_OFFLOAD_DEV_H
#define EMW3080_OFFLOAD_DEV_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_offload.h>
#include <zephyr/net/wifi_mgmt.h>

/* This is a specialized header used for declaring the network device integration
 * for EMW3080 using NET_DEVICE_OFFLOAD_INIT properly, to integrate with Zephyr networking
 */

/* Forward declarations */
extern const struct net_wifi_mgmt_offload emw3080_api;

/* Return the WiFi type for device type identification */
static __maybe_unused enum offloaded_net_if_types emw3080_get_interface_type(void)
{
    return L2_OFFLOADED_NET_IF_TYPE_WIFI;
}

/* Function to initialize the device */
static __maybe_unused int emw3080_net_init(const struct device *dev)
{
    /* Get the SPI device */
    const struct device *spi = DEVICE_DT_GET(DT_NODELABEL(spi2));
    if (!device_is_ready(spi)) {
        return -ENODEV;
    }
    
    struct net_if *iface = net_if_lookup_by_dev(dev);
    if (!iface) {
        return -ENODEV;
    }
    
    /* Let the network stack know we're going to provide offloaded networking */
    net_if_set_link_addr(iface, dev->data, 6, NET_LINK_ETHERNET);
    
    return 0;
}

/* Declare the NET_DEVICE_OFFLOAD_INIT for EMW3080 */
#define EMW3080_NET_DEVICE_INIT(dev_id, dev_name, init_fn, data, config, prio, api, mtu) \
    NET_DEVICE_OFFLOAD_INIT(dev_id, dev_name, init_fn, NULL, data, config, prio, api, mtu)

#endif /* EMW3080_OFFLOAD_DEV_H */
