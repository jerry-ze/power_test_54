/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/mfd/npm1300.h>
#include <zephyr/drivers/sensor/npm1300_charger.h>
#include <zephyr/dt-bindings/gpio/nordic-npm1300-gpio.h>

#include "nrf_fuel_gauge.h"

/* nPM1300 CHARGER.BCHGCHARGESTATUS.CONSTANTCURRENT register bitmask */
#define NPM1300_CHG_STATUS_CC_MASK 	BIT(3)
#define NPM1300_CHARGING_START	900
#define NPM1300_GPIO_DRIVE_6MA	(1U << 8U) // 6mA drive
#define NPM1300_GPIO_LOAD_SWITCH_1	0
#define NPM1300_GPIO_AP_SENSOR_PWR_VDD	1
#define NPM1300_GPIO_AP_SENSOR_PWR_VDDIO	2


static const struct device *pmic = DEVICE_DT_GET(DT_NODELABEL(npm1300_ek_pmic));
static const struct device *charger = DEVICE_DT_GET(DT_NODELABEL(npm1300_ek_charger));
static const struct device *regulators = DEVICE_DT_GET(DT_NODELABEL(npm1300_ek_regulators));
static const struct device *ldsw1 = DEVICE_DT_GET(DT_NODELABEL(npm1300_ek_ldo1));
static const struct device *ldsw2 = DEVICE_DT_GET(DT_NODELABEL(npm1300_ek_ldo2));
static const struct device *pm_gpio = DEVICE_DT_GET(DT_NODELABEL(npm1300_ek_gpio));

static int64_t ref_time;
extern uint8_t battery_soc;
static float max_charge_current;
static float term_charge_current;
static volatile bool vbus_connected;

static const struct battery_model battery_model = {
#include "battery_model.inc"
};


#if 0
/* Npm sample timer used in active mode. */
static void npm_timer_handler(struct k_timer *timer);
K_TIMER_DEFINE(npm_timer, npm_timer_handler, NULL);
static void npm_timer_handler(struct k_timer *timer)
{
	//开始充电的前15分钟90ma充电;15分钟后 450ma 充电;截止电压4.37v;截止电流10ma.
	ARG_UNUSED(timer);
	printk("npm_timer timeout!\n");
	k_timer_stop(&npm_timer);//15分钟时间到,修改充电电流，关闭定时器。
}
#endif

static void event_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	if (pins & BIT(NPM1300_EVENT_VBUS_DETECTED)) {
		printk("Vbus connected\n");
		vbus_connected = true;
		//k_timer_start(&npm_timer, K_SECONDS(NPM1300_CHARGING_START), K_NO_WAIT);//连接上VBUS之后就开起定时器并开始充电
	}

	if (pins & BIT(NPM1300_EVENT_VBUS_REMOVED)) {
		printk("Vbus removed\n");
		vbus_connected = false;
	}
}

static int read_sensors(const struct device *charger,
	float *voltage, float *current, float *temp, int32_t *chg_status)
{
	struct sensor_value value;
	int ret;

	ret = sensor_sample_fetch(charger);
	if (ret < 0) {
		return ret;
	}

	sensor_channel_get(charger, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
	*voltage = (float)value.val1 + ((float)value.val2 / 1000000);

	sensor_channel_get(charger, SENSOR_CHAN_GAUGE_TEMP, &value);
	*temp = (float)value.val1 + ((float)value.val2 / 1000000);

	sensor_channel_get(charger, SENSOR_CHAN_GAUGE_AVG_CURRENT, &value);
	*current = (float)value.val1 + ((float)value.val2 / 1000000);

	sensor_channel_get(charger, SENSOR_CHAN_NPM1300_CHARGER_STATUS, &value);
	*chg_status = value.val1;

	return 0;
}

int fuel_gauge_init(const struct device *charger)
{
	struct sensor_value value;
	struct nrf_fuel_gauge_init_parameters parameters = {
		.model = &battery_model,
		.opt_params = NULL,
	};
	int32_t chg_status;
	int ret;

	printk("nRF Fuel Gauge version: %s\n", nrf_fuel_gauge_version);

	ret = read_sensors(charger, &parameters.v0, &parameters.i0, &parameters.t0, &chg_status);
	if (ret < 0) {
		return ret;
	}

	/* Store charge nominal and termination current, needed for ttf calculation */
	sensor_channel_get(charger, SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT, &value);
	max_charge_current = (float)value.val1 + ((float)value.val2 / 1000000);
	term_charge_current = max_charge_current / 10.f;

	nrf_fuel_gauge_init(&parameters, NULL);

	ref_time = k_uptime_get();

	return 0;
}

uint8_t fuel_gauge_update(const struct device *charger, bool vbus_connected)
{
	float voltage;
	float current;
	float temp;
	float soc;
	float tte;
	float ttf;
	float delta;
	int32_t chg_status;
	bool cc_charging;
	int ret;

	ret = read_sensors(charger, &voltage, &current, &temp, &chg_status);
	if (ret < 0) {
		printk("Error: Could not read from charger device\n");
		return ret;
	}

	cc_charging = (chg_status & NPM1300_CHG_STATUS_CC_MASK) != 0;

	delta = (float) k_uptime_delta(&ref_time) / 1000.f;

	soc = nrf_fuel_gauge_process(voltage, current, temp, delta, vbus_connected, NULL);
	tte = nrf_fuel_gauge_tte_get();
	ttf = nrf_fuel_gauge_ttf_get(cc_charging, -term_charge_current);

	printk("V: %.3f, I: %.3f, T: %.2f, ", (double)voltage, (double)current, (double)temp);
	printk("SoC: %.2f, TTE: %.0f, TTF: %.0f\n", (double)soc, (double)tte, (double)ttf);

	return (uint8_t)soc;
}

int npm_initial(void)
{
	int err;

	if (!device_is_ready(pmic)) {
		printk("Pmic device not ready.\n");
		return 0;
	}

	if (!device_is_ready(charger)) {
		printk("Charger device not ready.\n");
		//return 0;
	}

	if (!device_is_ready(regulators)) {
		printk("Regulators device not ready.\n");
		return 0;
	}

	if (!device_is_ready(ldsw1)) {
		printk("Ldsw1 device not ready.\n");
		return 0;
	}

	if (!device_is_ready(ldsw2)) {
		printk("Ldsw2 device not ready.\n");
		return 0;
	}

	if (!device_is_ready(pm_gpio)) {
		printk("Pm_gpio device not ready.\n");
		return 0;
	}

	if (fuel_gauge_init(charger) < 0) {
		printk("Could not initialise fuel gauge.\n");
		return 0;
	}

	static struct gpio_callback event_cb;

	gpio_init_callback(&event_cb, event_callback, BIT(NPM1300_EVENT_VBUS_DETECTED) | BIT(NPM1300_EVENT_VBUS_REMOVED));

	err = mfd_npm1300_add_callback(pmic, &event_cb);
	if (err) {
		printk("Failed to add pmic callback.\n");
		return 0;
	}

	/* Initialise vbus detection status. */
	struct sensor_value val;
	int ret = sensor_attr_get(charger, SENSOR_CHAN_CURRENT, SENSOR_ATTR_UPPER_THRESH, &val);

	if (ret < 0) {
		return false;
	}

	vbus_connected = (val.val1 != 0) || (val.val2 != 0);

	gpio_pin_configure(pm_gpio, NPM1300_GPIO_LOAD_SWITCH_1, GPIO_INPUT);
	gpio_pin_configure(pm_gpio, NPM1300_GPIO_AP_SENSOR_PWR_VDD, GPIO_OUTPUT_LOW | NPM1300_GPIO_DRIVE_6MA);	//配置pmic的gpio1输出电流为6mA,给气压计供电
	gpio_pin_configure(pm_gpio, NPM1300_GPIO_AP_SENSOR_PWR_VDDIO, GPIO_OUTPUT_LOW | NPM1300_GPIO_DRIVE_6MA);	//配置pmic的gpio2输出电流为6mA,给气压计供电
	//gpio_pin_set(pm_gpio, NPM1300_GPIO_AP_SENSOR_PWR_VDD, 0); // 输出高
	//gpio_pin_set(pm_gpio, NPM1300_GPIO_AP_SENSOR_PWR_VDDIO, 0); // 输出低

	if (!regulator_is_enabled(ldsw2)) {
		regulator_enable(ldsw2);
	}

	printk("PMIC device initial ok\n");
	return 0;
}

void npm_control(void)
{
	battery_soc = fuel_gauge_update(charger, vbus_connected);
}
