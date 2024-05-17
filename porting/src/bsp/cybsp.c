/*
 * Copyright (c) 2006-2023, Evlers Developers
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date         Author      Notes
 * 2023-12-21   Evlers      first implementation
 * 2024-05-17   Evlers      fixed an bug where a module could not be reset
 */

#include "rtthread.h"
#include "rtdevice.h"
#include "drv_gpio.h"

#include "cybsp.h"


static int cybsp_init(void)
{
    /* configure the wl_reg_on pin */
    rt_pin_mode(CYBSP_REG_ON_PIN, PIN_MODE_OUTPUT);

    /* reset modules */
    rt_pin_write(CYBSP_REG_ON_PIN, PIN_LOW);
    rt_thread_mdelay(2);
    rt_pin_write(CYBSP_REG_ON_PIN, PIN_HIGH);

    return RT_EOK;
}
INIT_PREV_EXPORT(cybsp_init);
