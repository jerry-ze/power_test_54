/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __FUEL_GAUGE_H__
#define __FUEL_GAUGE_H__

#ifdef __cplusplus
extern "C" {
#endif

int fuel_gauge_init(const struct device *charger);
uint8_t fuel_gauge_update(const struct device *charger, bool vbus_connected);

int npm_initial(void);
void npm_control(void);

#ifdef __cplusplus
}
#endif

#endif /* __FUEL_GAUGE_H__ */
