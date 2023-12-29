## RT-Thread Wi-Fi Host Driver (WHD)

### Overview
The WHD is an independent, embedded Wi-Fi Host Driver that provides a set of APIs to interact with Infineon WLAN chips. The WHD is an independent firmware product that is easily portable to any embedded software environment, including popular IoT frameworks such as Mbed OS and Amazon FreeRTOS. Therefore, the WHD includes hooks for RTOS and TCP/IP network abstraction layers.

Details about Wi-Fi Host Driver can be found in the [Wi-Fi Host Driver readme](./wifi-host-driver/README.md).<br>
The [release notes](./wifi-host-driver/RELEASE.md) detail the current release. You can also find information about previous versions.

The repository has adapted WHD to the RT-Thread system, currently only supports the SDIO bus protocol, and uses the mmcsd of RT-Thread for sdio bus operations.<br>
Welcome everyone `PR` to support more bus interface and chips.

### Using

- Clone the repository to the `packages` or `libraries` directory in the RT-Thread project.
- Because the `wifi-host-driver` is a submodule, you will need to clone with the --recursive option.
- In the `libraries` or `packages` folder in the RT-Thread project, include `Kconfig` file for `WHD` in its Kconfig files.
- For example, include `WHD` in the `libraries` directory:
```Kconfig
menu "External Libraries"
    source "$RTT_DIR/../libraries/rt-thread_wifi-host-driver/Kconfig"
endmenu
```
**Note:**<br>
sdio driver needs to support byte transfer. In the bsp of RT-Thread, most chips do not have the function of adapting byte transfer. <br>
Please modify the `drv_sdio.c` file according to the following example: <br>
```c
/* The example is an sdio driver for the STM32H750 */
SCB_CleanInvalidateDCache();

reg_cmd |= SDMMC_CMD_CMDTRANS;
hw_sdio->mask &= ~(SDMMC_MASK_CMDRENDIE | SDMMC_MASK_CMDSENTIE);
hw_sdio->dtimer = HW_SDIO_DATATIMEOUT;
hw_sdio->dlen = data->blks * data->blksize;
hw_sdio->dctrl = (get_order(data->blksize)<<4) |
                    (data->flags & DATA_DIR_READ ? SDMMC_DCTRL_DTDIR : 0) | \
                    /* Adds detection of the DATA_STREAM flag */
                    ((data->flags & DATA_STREAM) ? SDMMC_DCTRL_DTMODE_0 : 0);
hw_sdio->idmabase0r = (rt_uint32_t)sdio->cache_buf;
hw_sdio->idmatrlr = SDMMC_IDMA_IDMAEN;
```


### Supported Chip

| **CHIP**  |**SDIO**|**SPI**|**M2M**|
|-----------|--------|-------|-------|
| CYW4343W  |   *    |   x   |   x   |
| CYW43438  |   o    |   x   |   x   |
| CYW4373   |   *    |   x   |   x   |
| CYW43012  |   *    |   x   |   x   |
| CYW43439  |   *    |   x   |   x   |
| CYW43022  |   *    |   x   |   x   |

'x' indicates no support<br>
'o' indicates tested and supported<br>
'*' means theoretically supported, but not tested

### More information
* [Wi-Fi Host Driver API Reference Manual and Porting Guide](https://infineon.github.io/wifi-host-driver/html/index.html)
* [Wi-Fi Host Driver Release Notes](./wifi-host-driver/RELEASE.md)
* [Infineon Technologies](http://www.infineon.com)
