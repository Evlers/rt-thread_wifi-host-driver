/*
 * MIT License
 *
 * Copyright (c) 2024 Evlers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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
