/*
 * Copyright (c) 2006-2023, Evlers Developers
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date         Author      Notes
 * 2023-12-21   Evlers      first implementation
 */

#include "rtthread.h"
#include "rtdevice.h"
#include "drv_gpio.h"

#include "cybsp.h"


static int cybsp_init(void)
{
    /* enable the WLAN REG pin */
    rt_pin_mode(CYBSP_REG_ON_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(CYBSP_REG_ON_PIN, PIN_HIGH);

    return RT_EOK;
}
INIT_PREV_EXPORT(cybsp_init);
