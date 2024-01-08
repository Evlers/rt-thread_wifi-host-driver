/*
 * Copyright (c) 2006-2023, Evlers Developers
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date         Author      Notes
 * 2023-12-28   Evlers      first implementation
 */

#include "rtthread.h"
#include "rtdevice.h"

#include "cybsp.h"
#include "cyhal_sdio.h"

#include "whd.h"
#include "whd_int.h"
#include "whd_wifi_api.h"
#include "whd_buffer_api.h"
#include "whd_resource_api.h"
#include "whd_network_types.h"
#include "wiced_resource.h"

#include "drv_gpio.h"

#define DBG_TAG           "cyw.wlan"
#define DBG_LVL           DBG_INFO
#include "rtdbg.h"


/* WHD interface */
static whd_driver_t whd_driver;
static cyhal_sdio_t cyhal_sdio;
static whd_init_config_t whd_config =
{
    .thread_priority = CY_WIFI_WHD_THREAD_PRIORITY,
    .thread_stack_size = CY_WIFI_WHD_THREAD_STACK_SIZE,
    .country = WHD_COUNTRY_CHINA,
};
extern whd_resource_source_t resource_ops;
extern struct whd_buffer_funcs whd_buffer_ops;


struct drv_wifi
{
    struct rt_wlan_device *wlan;
    whd_interface_t whd_itf;
};

static struct drv_wifi wifi_sta, wifi_ap;


/* rt-thread wlan interface */

rt_inline struct drv_wifi *get_drv_wifi(struct rt_wlan_device *wlan)
{
    if (wlan == wifi_sta.wlan)
    {
        return &wifi_sta;
    }
    
    if (wlan == wifi_ap.wlan)
    {
        return &wifi_ap;
    }

    return RT_NULL;
}

static rt_err_t drv_wlan_init(struct rt_wlan_device *wlan)
{
    return RT_EOK;
}

static rt_err_t drv_wlan_mode(struct rt_wlan_device *wlan, rt_wlan_mode_t mode)
{
    return RT_EOK;
}

whd_scan_result_t scan_result;

static void whd_scan_callback(whd_scan_result_t **result_ptr, void *user_data, whd_scan_status_t status)
{   
    struct rt_wlan_device *wlan = user_data;

    /* Check if we don't have a scan result to send to the user */
    if (( result_ptr == NULL ) || ( *result_ptr == NULL ))
    {
        /* Check for scan complete */
        if (status == WHD_SCAN_COMPLETED_SUCCESSFULLY || status == WHD_SCAN_ABORTED)
        {
            LOG_D("scan complete!");
            rt_wlan_dev_indicate_event_handle(wlan, RT_WLAN_DEV_EVT_SCAN_DONE, RT_NULL);
        }
        return;
    }

    /* Copy info to wlan */
    whd_scan_result_t *bss_info = *result_ptr;
    struct rt_wlan_info wlan_info;
    struct rt_wlan_buff buff;

    rt_memset(&wlan_info, 0, sizeof(wlan_info));

    rt_memcpy(&wlan_info.bssid[0], bss_info->BSSID.octet, sizeof(wlan_info.bssid));
    rt_memcpy(wlan_info.ssid.val, bss_info->SSID.value, bss_info->SSID.length);
    wlan_info.ssid.len = bss_info->SSID.length;
    if (bss_info->SSID.length)
        wlan_info.hidden = 0;
    else
        wlan_info.hidden = 1;

    wlan_info.channel = (rt_int16_t)bss_info->channel;
    wlan_info.rssi = bss_info->signal_strength;

    wlan_info.datarate = bss_info->max_data_rate * 1000;

    switch (bss_info->band)
    {
        case WHD_802_11_BAND_5GHZ:      wlan_info.band = RT_802_11_BAND_5GHZ; break;
        case WHD_802_11_BAND_2_4GHZ:    wlan_info.band = RT_802_11_BAND_2_4GHZ; break;
        case WHD_802_11_BAND_6GHZ:      wlan_info.band = RT_802_11_BAND_UNKNOWN; break;
    }

    wlan_info.security = (rt_wlan_security_t)bss_info->security;

    bss_info ++;

    buff.data = &wlan_info;
    buff.len = sizeof(wlan_info);

    rt_wlan_dev_indicate_event_handle(wlan, RT_WLAN_DEV_EVT_SCAN_REPORT, &buff);

    memset(*result_ptr, 0, sizeof(whd_scan_result_t));
}

static rt_err_t drv_wlan_scan(struct rt_wlan_device *wlan, struct rt_scan_info *scan_info)
{
    return whd_wifi_scan(get_drv_wifi(wlan)->whd_itf, WHD_SCAN_TYPE_ACTIVE, WHD_BSS_TYPE_ANY,
                            NULL, NULL, NULL, NULL, whd_scan_callback, &scan_result, wlan);
}

static rt_err_t drv_wlan_join(struct rt_wlan_device *wlan, struct rt_sta_info *sta_info)
{
    rt_err_t ret;
    whd_ssid_t whd_ssid = { .length = sta_info->ssid.len };

    memcpy(whd_ssid.value, sta_info->ssid.val, whd_ssid.length);

    ret = whd_wifi_join(get_drv_wifi(wlan)->whd_itf, &whd_ssid, WHD_SECURITY_WPA2_AES_PSK, 
                            (const uint8_t *)sta_info->key.val, sta_info->key.len);

    if (ret == WHD_SUCCESS)
    {
        rt_wlan_dev_indicate_event_handle(wlan, RT_WLAN_DEV_EVT_CONNECT, RT_NULL);
    }
    else
    {
        rt_wlan_dev_indicate_event_handle(wlan, RT_WLAN_DEV_EVT_CONNECT_FAIL, RT_NULL);
    }

    return ret;
}

static rt_err_t drv_wlan_softap(struct rt_wlan_device *wlan, struct rt_ap_info *ap_info)
{
    rt_err_t ret;

    ret = whd_wifi_init_ap(get_drv_wifi(wlan)->whd_itf,
                            (whd_ssid_t*)&ap_info->ssid,
                            (whd_security_t)ap_info->security, 
                            (const uint8_t *) ap_info->key.val,
                            ap_info->key.len, ap_info->channel);
    
    if (ret != WHD_SUCCESS)
    {
        return ret;
    }

    if ((ret = whd_wifi_start_ap(get_drv_wifi(wlan)->whd_itf)) == WHD_SUCCESS)
    {
        rt_wlan_dev_indicate_event_handle(wifi_ap.wlan, RT_WLAN_DEV_EVT_AP_START, 0);
    }
    else
    {
        rt_wlan_dev_indicate_event_handle(wifi_ap.wlan, RT_WLAN_DEV_EVT_AP_STOP, 0);
    }

    return ret;
}

static rt_err_t drv_wlan_disconnect(struct rt_wlan_device *wlan)
{
    rt_err_t ret = whd_wifi_leave(get_drv_wifi(wlan)->whd_itf);

    if (ret == WHD_SUCCESS)
    {
        rt_wlan_dev_indicate_event_handle(wlan, RT_WLAN_DEV_EVT_DISCONNECT, RT_NULL);
    }
    return ret;
}

static rt_err_t drv_wlan_ap_stop(struct rt_wlan_device *wlan)
{
    if (whd_wifi_stop_ap(get_drv_wifi(wlan)->whd_itf) == WHD_SUCCESS)
    {
        rt_wlan_dev_indicate_event_handle(wifi_ap.wlan, RT_WLAN_DEV_EVT_AP_STOP, 0);
    }
    return RT_EOK;
}

static rt_err_t drv_wlan_ap_deauth(struct rt_wlan_device *wlan, rt_uint8_t mac[])
{
    whd_mac_t _mac;

    memcpy(&_mac, mac, sizeof(whd_mac_t));
    return whd_wifi_deauth_sta(get_drv_wifi(wlan)->whd_itf, &_mac, WHD_DOT11_RC_RESERVED);
}

static rt_err_t drv_wlan_scan_stop(struct rt_wlan_device *wlan)
{
    whd_wifi_stop_scan(get_drv_wifi(wlan)->whd_itf);
    return RT_EOK;
}

static int drv_wlan_get_rssi(struct rt_wlan_device *wlan)
{
    int32_t rssi;

    whd_wifi_get_rssi(get_drv_wifi(wlan)->whd_itf, &rssi);

    return rssi;
}

static rt_err_t drv_wlan_set_powersave(struct rt_wlan_device *wlan, int level)
{
    if (level)
    {
        return whd_wifi_enable_powersave(get_drv_wifi(wlan)->whd_itf);
    }
    else
    {
        return whd_wifi_disable_powersave(get_drv_wifi(wlan)->whd_itf);
    }
}

static int drv_wlan_get_powersave(struct rt_wlan_device *wlan)
{
    uint32_t value = 0;

    whd_wifi_get_powersave_mode(get_drv_wifi(wlan)->whd_itf, &value);

    return value;
}

static rt_err_t drv_wlan_set_channel(struct rt_wlan_device *wlan, int channel)
{
    return whd_wifi_set_channel(get_drv_wifi(wlan)->whd_itf, channel);
}

static int drv_wlan_get_channel(struct rt_wlan_device *wlan)
{
    uint32_t channel = 0;

    whd_wifi_get_channel(get_drv_wifi(wlan)->whd_itf, &channel);

    return channel;
}

static rt_country_code_t drv_wlan_get_country(struct rt_wlan_device *wlan)
{
    if (whd_config.country == WHD_COUNTRY_CHINA)
    {
        return RT_COUNTRY_CHINA;
    }
    else
    {
        return RT_COUNTRY_UNKNOWN;
    }
}

static rt_err_t drv_wlan_set_mac(struct rt_wlan_device *wlan, rt_uint8_t mac[])
{
    whd_mac_t whd_mac;

    memcpy(whd_mac.octet, mac, sizeof(whd_mac.octet));

    return whd_wifi_set_mac_address(get_drv_wifi(wlan)->whd_itf, whd_mac);
}

static rt_err_t drv_wlan_get_mac(struct rt_wlan_device *wlan, rt_uint8_t mac[])
{
    rt_err_t ret;
    whd_mac_t whd_mac;

    if (wlan == wifi_ap.wlan)
    {
        if ((ret = whd_wifi_get_mac_address(wifi_sta.whd_itf, &whd_mac)) == WHD_SUCCESS)
        {
            /* AP interface needs to come up with MAC different from STA  */
            if (whd_mac.octet[0] & 0x02)
            {
                whd_mac.octet[0] &= (uint8_t) ~(0x02);
            }
            else
            {
                whd_mac.octet[0] |= 0x02;
            }
        }
    }
    else
    {
        ret = whd_wifi_get_mac_address(get_drv_wifi(wlan)->whd_itf, &whd_mac);
    }

    if (ret == WHD_SUCCESS)
    {
        memcpy(mac, whd_mac.octet, RT_WLAN_BSSID_MAX_LENGTH);
    }

    return ret;
}

static int drv_wlan_recv(struct rt_wlan_device *wlan, void *buff, int len)
{
    return RT_EOK;
}

static int drv_wlan_send(struct rt_wlan_device *wlan, void *buff, int len)
{
    whd_buffer_t buffer;

    /* Determines if a particular interface is ready to transceive ethernet packets */
    if (whd_wifi_is_ready_to_transceive(get_drv_wifi(wlan)->whd_itf) != WHD_SUCCESS)
    {
        return -RT_ERROR;
    }

    /* Creating a transport layer pbuf allows WHD to add link data to the pbuf header */
    if (whd_host_buffer_get(get_drv_wifi(wlan)->whd_itf->whd_driver, &buffer, WHD_NETWORK_TX, len + WHD_LINK_HEADER, 1000) != 0)
    {
        LOG_D("err whd_host_buffer_get failed\n");
        return -RT_ERROR;
    }

    /* Sets the offset of buffer data position
     * It is reserved for whd to write link header data
     */
    whd_buffer_add_remove_at_front(get_drv_wifi(wlan)->whd_itf->whd_driver, &buffer, WHD_LINK_HEADER);

    /* Copy network data to WHD */
    memcpy(whd_buffer_get_current_piece_data_pointer(get_drv_wifi(wlan)->whd_itf->whd_driver, buffer), buff, len);

    /*
     *  This function takes ethernet data from the network stack and queues it for transmission over the wireless network.
     *  The function can be called from any thread context as it is thread safe, however
     *  it must not be called from interrupt context since it might get blocked while waiting
     *  for a lock on the transmit queue.
     */
    whd_network_send_ethernet_data(get_drv_wifi(wlan)->whd_itf, buffer);

    return RT_EOK;
}

static const struct rt_wlan_dev_ops ops =
{
    .wlan_init = drv_wlan_init,
    .wlan_mode = drv_wlan_mode,
    .wlan_scan = drv_wlan_scan,
    .wlan_join = drv_wlan_join,
    .wlan_softap = drv_wlan_softap,
    .wlan_disconnect = drv_wlan_disconnect,
    .wlan_ap_stop = drv_wlan_ap_stop,
    .wlan_ap_deauth = drv_wlan_ap_deauth,
    .wlan_scan_stop = drv_wlan_scan_stop,
    .wlan_get_rssi = drv_wlan_get_rssi,
    .wlan_set_powersave = drv_wlan_set_powersave,
    .wlan_get_powersave = drv_wlan_get_powersave,
    .wlan_cfg_promisc = NULL,
    .wlan_cfg_filter = NULL,
    .wlan_cfg_mgnt_filter = NULL,
    .wlan_set_channel = drv_wlan_set_channel,
    .wlan_get_channel = drv_wlan_get_channel,
    .wlan_set_country = NULL,
    .wlan_get_country = drv_wlan_get_country,
    .wlan_set_mac = drv_wlan_set_mac,
    .wlan_get_mac = drv_wlan_get_mac,
    .wlan_recv = drv_wlan_recv,
    .wlan_send = drv_wlan_send,
};

/*
 * This function takes packets from the radio driver and passes them into the
 * lwIP stack. If the stack is not initialized, or if the lwIP stack does not
 * accept the packet, the packet is freed (dropped). If the packet is of type EAPOL
 * and if the Extensible Authentication Protocol over LAN (EAPOL) handler is registered, the packet will be redirected to the registered
 * handler and should be freed by the EAPOL handler.
 */
void cy_network_process_ethernet_data(whd_interface_t iface, whd_buffer_t buf)
{
    uint16_t ethertype;
    uint8_t *data = whd_buffer_get_current_piece_data_pointer(iface->whd_driver, buf);
    struct rt_wlan_device *wlan = wifi_ap.whd_itf == iface ? wifi_ap.wlan : wifi_sta.wlan;

    #define EAPOL_PACKET_TYPE                        (0x888E)

    if (iface->role == WHD_STA_ROLE)
    {

    }
    else if (iface->role == WHD_AP_ROLE)
    {

    }
    else
    {
        whd_buffer_release(iface->whd_driver, buf, WHD_NETWORK_RX);
        return;
    }

    ethertype = (uint16_t) (data[12] << 8 | data[13]);
    if (ethertype == EAPOL_PACKET_TYPE)
    {
        LOG_D("EAPOL_PACKET_TYPE");
    }
    else
    {
        /* If the interface is not yet set up, drop the packet */
        LOG_D("Send data up to wlan");
        rt_wlan_dev_report_data(wlan, data, whd_buffer_get_current_piece_size(iface->whd_driver, buf));
        whd_buffer_release(iface->whd_driver, buf, WHD_NETWORK_RX);
    }
}

static whd_netif_funcs_t netif_if_ops =
{
    .whd_network_process_ethernet_data = cy_network_process_ethernet_data,
};


static int rt_hw_wifi_init(void)
{
    rt_err_t ret = RT_EOK;
    static struct rt_wlan_device wlan_ap, wlan_sta;
    static const whd_oob_config_t OOB_CONFIG =
    {
        .host_oob_pin      = CYBSP_HOST_WAKE_IRQ_PIN,
        .dev_gpio_sel      = 0,
        .is_falling_edge   = (CYBSP_HOST_WAKE_IRQ_EVENT == CYHAL_GPIO_IRQ_FALL) ? WHD_TRUE : WHD_FALSE,
        .intr_priority     = CYBSP_OOB_INTR_PRIORITY
    };
    whd_sdio_config_t whd_sdio_config =
    {
        .sdio_1bit_mode        = WHD_FALSE,
        .high_speed_sdio_clock = WHD_FALSE,
        .oob_config            = OOB_CONFIG
    };

    wifi_ap.wlan = &wlan_ap;
    wifi_sta.wlan = &wlan_sta;


    /* Initialize WiFi Host Drivers (WHD) */

    /* Wait for the mmcsd to finish reading the card */
    rt_thread_mdelay(200);

    /* Register the sdio driver */
    if (cyhal_sdio_init(&cyhal_sdio) != CYHAL_SDIO_RET_NO_ERRORS)
    {
        LOG_E("Unable to register SDIO driver to mmcsd!");
        return -RT_ERROR;
    }

    /* Initialize WiFi host drivers */
    whd_init(&whd_driver, &whd_config, &resource_ops, &whd_buffer_ops, &netif_if_ops);

    /* Attach a bus SDIO */
    if (whd_bus_sdio_attach(whd_driver, &whd_sdio_config, &cyhal_sdio) != WHD_SUCCESS)
    {
        LOG_E("Unable to Attach to the sdio bus!");
        return -RT_ERROR;
    }

    /* Switch on Wifi, download firmware and create a primary interface, returns whd_interface_t */
    if (whd_wifi_on(whd_driver, &wifi_sta.whd_itf) != WHD_SUCCESS)
    {
        LOG_E("Unable to start the WiFi module!");
        return -RT_ERROR;
    }

#ifdef CY_WIFI_DEFAULT_ENABLE_POWERSAVE_MODE
    uint32_t value = 0;
    if ((ret = whd_wifi_get_powersave_mode(wifi_sta.whd_itf, &value)) != WHD_SUCCESS)
    {
        LOG_E("The powersave mode switch status cannot be get!");
        return ret;
    }
#endif

    /* Disables 802.11 power save mode on specified interface */
    if ((ret = whd_wifi_disable_powersave(wifi_sta.whd_itf)) != WHD_SUCCESS)
    {
        LOG_E("Failed to disable the powersave mode!");
        return ret;
    }

#ifdef CY_WIFI_DEFAULT_ENABLE_POWERSAVE_MODE
    if (value == PM1_POWERSAVE_MODE)
    {
        /* Enables powersave mode on specified interface */
        if ((ret = whd_wifi_enable_powersave(wifi_sta.whd_itf)) != WHD_SUCCESS)
        {
            LOG_E("Failed to enable the powersave mode!");
            return ret;
        }

        LOG_D("Enables powersave mode.");
    }
    else if (value == PM2_POWERSAVE_MODE)
    {
        /* Enables powersave mode on specified interface while attempting to maximise throughput */
        /* Note: When working in this mode, sdio communication timeout will occur, which is the normal operation of whd */
        if ((ret = whd_wifi_enable_powersave_with_throughput(wifi_sta.whd_itf, CY_WIFI_DEFAULT_PM2_SLEEP_RET_TIME)) != WHD_SUCCESS)
        {
            LOG_E("Failed to enable the powersave mode!");
            return ret;
        }
        
        LOG_D("Enables powersave mode while attempting to maximise throughput.");
    }
#endif

    /* Creates a secondary interface, returns whd_interface_t */
    if (whd_add_secondary_interface(whd_driver, NULL, &wifi_ap.whd_itf) != WHD_SUCCESS)
    {
        LOG_E("Failed to create a secondary interface!");
        return -RT_ERROR;
    }


    /* Register the wlan device and set its working mode */

    /* register wlan device for ap */
    if ((ret = rt_wlan_dev_register(&wlan_ap, RT_WLAN_DEVICE_AP_NAME, &ops, 0, &wifi_ap)) != RT_EOK)
    {
        LOG_E("Failed to register a wlan_ap device!");
        return ret;
    }

    /* register wlan device for sta */
    if ((ret = rt_wlan_dev_register(&wlan_sta, RT_WLAN_DEVICE_STA_NAME, &ops, 0, &wifi_sta)) != RT_EOK)
    {
        LOG_E("Failed to register a wlan_sta device!");
        return ret;
    }
    
    /* Set wlan_sta to STATION mode */
    if ((ret = rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION)) != RT_EOK)
    {
        LOG_E("Failed to set %s to station mode!", RT_WLAN_DEVICE_STA_NAME);
        return ret;
    }

    /* Set wlan_ap to AP mode */
    if ((ret = rt_wlan_set_mode(RT_WLAN_DEVICE_AP_NAME, RT_WLAN_AP)) != RT_EOK)
    {
        LOG_E("Failed to set %s to ap mode!", RT_WLAN_DEVICE_AP_NAME);
        return ret;
    }

    return ret;
}
INIT_APP_EXPORT(rt_hw_wifi_init);
