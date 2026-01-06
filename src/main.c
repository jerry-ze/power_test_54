/*
* Copyright (c) 2012-2014 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*/

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/pm/device.h>

#include "npm_fuel_gauge.h"


#define GPIO2_NODE DT_NODELABEL(gpio2)
const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
const struct device *uart54 = DEVICE_DT_GET(DT_NODELABEL(uart22));
const struct device *gpio2_dev = DEVICE_DT_GET(GPIO2_NODE);


#define AG3352_VCORE_VRF_DCDC_EN 	3
#define AG3352_VRTC_LOAD_SW_EN 		4
#define AG3352_AVDD_VSYS_PWR_CTRL 	6
#define N91_ENABLE 					10


int main(void)
{
	if (!device_is_ready(uart54)) {
		printf("UART device not ready\n");
		return 0;
	}

	gpio_pin_configure(gpio2_dev, AG3352_VCORE_VRF_DCDC_EN, GPIO_OUTPUT_LOW);
	gpio_pin_configure(gpio2_dev, AG3352_VRTC_LOAD_SW_EN, GPIO_OUTPUT_LOW);
	gpio_pin_configure(gpio2_dev, AG3352_AVDD_VSYS_PWR_CTRL, GPIO_OUTPUT_LOW);
	#if 1
	gpio_pin_configure(gpio2_dev, N91_ENABLE, GPIO_OUTPUT_HIGH);
	#else
	gpio_pin_configure(gpio2_dev, N91_ENABLE, GPIO_OUTPUT_LOW);
	#endif
	npm_initial();

	pm_device_action_run(uart, PM_DEVICE_ACTION_SUSPEND);
	pm_device_action_run(uart54, PM_DEVICE_ACTION_SUSPEND);

	for (;;)
	{
		printf("Hello World! %s\n", CONFIG_BOARD_TARGET);
		k_msleep(1000);
	}
	return 0;
}
