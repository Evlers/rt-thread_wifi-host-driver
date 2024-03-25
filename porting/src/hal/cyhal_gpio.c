/*
 * Copyright (c) 2006-2023, Evlers Developers
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date         Author      Notes
 * 2023-12-21   Evlers      first implementation
 */

#include "cyhal_gpio.h"
#include <rtdevice.h>
#include "rthw.h"


#define CYHAL_MAX_EXTI_NUMBER       (16U)

/* Return pin number by counts the number of leading zeros of a data value.
 * NOTE: __CLZ  returns  32 if no bits are set in the source register, and zero
 * if bit 31 is set. Parameter 'pin' should be in range GPIO_PIN0..GPIO_PIN15. */
#define CYHAL_GET_PIN_NUMBER(pin)   (uint32_t)(31u - __clz(pin))

typedef struct
{
    cyhal_gpio_irq_handler_t    callback;
    void*                       callback_args;
    bool                        enable;
    cyhal_gpio_t                pin;
} cyhal_gpio_event_callback_info_t;

static cyhal_gpio_event_callback_info_t _exti_callbacks_info[CYHAL_MAX_EXTI_NUMBER] = { 0u };


/** Initialize the GPIO pin
 *
 * @param[in]  pin The GPIO pin to initialize
 * @param[in]  direction The pin direction (input/output)
 * @param[in]  drvMode The pin drive mode
 * @param[in]  initVal Initial value on the pin
 *
 * @return The status of the init request
 */
cy_rslt_t cyhal_gpio_init(cyhal_gpio_t pin, cyhal_gpio_direction_t direction, cyhal_gpio_drive_mode_t drvMode,
                          bool initVal)
{
    rt_uint8_t mode;

    switch (drvMode)
    {
    case CYHAL_GPIO_DRIVE_NONE:
            mode = PIN_MODE_INPUT;
        break;

    case CYHAL_GPIO_DRIVE_PULLUP:
            mode = (direction == CYHAL_GPIO_DIR_INPUT) ? PIN_MODE_INPUT_PULLUP : PIN_MODE_OUTPUT;
        break;

    case CYHAL_GPIO_DRIVE_PULLDOWN:
            mode = (direction == CYHAL_GPIO_DIR_INPUT) ? PIN_MODE_INPUT_PULLDOWN : PIN_MODE_OUTPUT;
        break;

    case CYHAL_GPIO_DRIVE_OPENDRAINDRIVESLOW:
            mode = (direction == CYHAL_GPIO_DIR_INPUT) ? PIN_MODE_INPUT_PULLDOWN : PIN_MODE_OUTPUT_OD;
        break;

    case CYHAL_GPIO_DRIVE_OPENDRAINDRIVESHIGH:
            mode = (direction == CYHAL_GPIO_DIR_INPUT) ? PIN_MODE_INPUT_PULLUP : PIN_MODE_OUTPUT_OD;
        break;

    case CYHAL_GPIO_DRIVE_STRONG:
            mode = PIN_MODE_OUTPUT;
        break;

    case CYHAL_GPIO_DRIVE_PULLUPDOWN:
            if ((direction == CYHAL_GPIO_DIR_OUTPUT) || (direction == CYHAL_GPIO_DIR_BIDIRECTIONAL))
            {
                mode = PIN_MODE_OUTPUT;
            }
            else
            {
                mode = PIN_MODE_INPUT_PULLUP;
            }
        break;
    }

    rt_pin_mode(pin, mode);
    rt_pin_write(pin, initVal);

    return CY_RSLT_SUCCESS;
}

/** Uninitialize the gpio peripheral and the cyhal_gpio_t object
 *
 * @param[in] pin Pin number
 */
void cyhal_gpio_free(cyhal_gpio_t pin)
{
    rt_pin_detach_irq(pin);
    rt_pin_mode(pin, PIN_MODE_INPUT);
}

/** Set the output value for the pin. This only works for output & in_out pins.
 *
 * @param[in] pin   The GPIO object
 * @param[in] value The value to be set (high = true, low = false)
 */
void cyhal_gpio_write(cyhal_gpio_t pin, bool value)
{
    rt_pin_write(pin, value);
}

/** Read the input value.  This only works for input & in_out pins.
 *
 * @param[in]  pin   The GPIO object
 * @return The value of the IO (true = high, false = low)
 */
bool cyhal_gpio_read(cyhal_gpio_t pin)
{
    return rt_pin_read(pin) ? true : false;
}


static void gpio_interrupt(void *args)
{
    cyhal_gpio_event_callback_info_t *info = (cyhal_gpio_event_callback_info_t *)args;
    uint32_t gpio_number = CYHAL_GET_PIN_NUMBER(info->pin);

    /* Check the parameters */
    RT_ASSERT(gpio_number < CYHAL_MAX_EXTI_NUMBER);

    if ((gpio_number < CYHAL_MAX_EXTI_NUMBER) && (info->enable))
    {
        cyhal_gpio_irq_event_t event = (cyhal_gpio_read(info->pin) == true) ?
                                   CYHAL_GPIO_IRQ_RISE : CYHAL_GPIO_IRQ_FALL;

        /* Call user's callback */
        info->callback(info->callback_args, event);
    }
}

/** Register/clear an interrupt handler for the pin toggle pin IRQ event
 *
 * @param[in] pin           The pin number
 * @param[in] intrPriority  The NVIC interrupt channel priority
 * @param[in] handler       The function to call when the specified event happens. Pass NULL to unregister the handler.
 * @param[in] handler_arg   Generic argument that will be provided to the handler when called, can be NULL
 */
void cyhal_gpio_register_irq(cyhal_gpio_t pin, uint8_t intrPriority, cyhal_gpio_irq_handler_t handler,
                             void *handler_arg)
{
    /* Get pin number */
    uint32_t pin_number = CYHAL_GET_PIN_NUMBER(pin);

    if (pin_number < CYHAL_MAX_EXTI_NUMBER && handler != NULL)
    {
        rt_base_t level = rt_hw_interrupt_disable();
        _exti_callbacks_info[pin_number].callback      = handler;
        _exti_callbacks_info[pin_number].callback_args = handler_arg;
        _exti_callbacks_info[pin_number].pin           = pin;
        rt_hw_interrupt_enable(level);
    }
    else if (pin_number >= CYHAL_MAX_EXTI_NUMBER)
    {
        /* Wrong param */
        RT_ASSERT(false);
    }
    else
    {
        /* Clean callback information */
        uint32_t level = rt_hw_interrupt_disable();
        _exti_callbacks_info[pin_number].callback      = NULL;
        _exti_callbacks_info[pin_number].callback_args = NULL;
        _exti_callbacks_info[pin_number].enable        = false;
        _exti_callbacks_info[pin_number].pin           = 0xFFFFFFFF;
        rt_hw_interrupt_enable(level);
    }
}

/** Enable or Disable the GPIO IRQ
 *
 * @param[in] pin    The GPIO object
 * @param[in] event  The GPIO IRQ event
 * @param[in] enable True to turn on interrupts, False to turn off
 */
void cyhal_gpio_irq_enable(cyhal_gpio_t pin, cyhal_gpio_irq_event_t event, bool enable)
{
    /* Get pin number */
    uint32_t pin_number = CYHAL_GET_PIN_NUMBER(pin);

    if (enable)
    {
        rt_uint8_t mode;
        switch(event)
        {
        case CYHAL_GPIO_IRQ_RISE:
                mode = PIN_IRQ_MODE_RISING;
            break;

        case CYHAL_GPIO_IRQ_FALL:
                mode = PIN_IRQ_MODE_FALLING;
            break;

        case CYHAL_GPIO_IRQ_BOTH:
                mode = PIN_IRQ_MODE_RISING_FALLING;
            break;

        default:
            break;
        }

        rt_pin_attach_irq(pin, mode, gpio_interrupt, &_exti_callbacks_info[pin_number]);
        _exti_callbacks_info[pin_number].enable = true;
        rt_pin_irq_enable(pin, RT_TRUE);
    }
    else
    {
        rt_pin_irq_enable(pin, RT_FALSE);
        rt_pin_detach_irq(pin);
    }
}
