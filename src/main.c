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
#include <zephyr/drivers/flash.h>
#include "npm_fuel_gauge.h"

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

#define GPIO2_NODE DT_NODELABEL(gpio2)

#define AG3352_VCORE_VRF_DCDC_EN 3
#define AG3352_VRTC_LOAD_SW_EN 4
#define AG3352_AVDD_VSYS_PWR_CTRL 6
#define N91_ENABLE 10






#define SPI_FLASH_TEST_REGION_OFFSET 0xA000
#define SPI_FLASH_SECTOR_SIZE        4096
const uint8_t erased[] = { 0xff, 0xff, 0xff, 0xff };
void single_sector_test(const struct device *flash_dev)
{
    const uint8_t expected[] = { 0x55, 0xaa, 0x66, 0x99 };
    const size_t len = sizeof(expected);
    uint8_t buf[sizeof(expected)];
    int rc;

    printf("\nPerform test on single sector");
    /* Write protection needs to be disabled before each write or
     * erase, since the flash component turns on write protection
     * automatically after completion of write and erase
     * operations.
     */
    printf("\nTest 1: Flash erase\n");

    /* Full flash erase if SPI_FLASH_TEST_REGION_OFFSET = 0 and
     * SPI_FLASH_SECTOR_SIZE = flash size
     */
    rc = flash_erase(flash_dev, SPI_FLASH_TEST_REGION_OFFSET,
             SPI_FLASH_SECTOR_SIZE);
    if (rc != 0) {
        printf("Flash erase failed! %d\n", rc);
    } else {
        /* Check erased pattern */
        memset(buf, 0, len);
        rc = flash_read(flash_dev, SPI_FLASH_TEST_REGION_OFFSET, buf, len);
        if (rc != 0) {
            printf("Flash read failed! %d\n", rc);
            return;
        }
        if (memcmp(erased, buf, len) != 0) {
            printf("Flash erase failed at offset 0x%x got 0x%x\n",
                SPI_FLASH_TEST_REGION_OFFSET, *(uint32_t *)buf);
            return;
        }
        printf("Flash erase succeeded!\n");
    }
    printf("\nTest 2: Flash write\n");
	  printf("Attempting to write %zu bytes\n", len);
    rc = flash_write(flash_dev, SPI_FLASH_TEST_REGION_OFFSET, expected, len);
    if (rc != 0) {
        printf("Flash write failed! %d\n", rc);
        return;
    }

    memset(buf, 0, len);
    rc = flash_read(flash_dev, SPI_FLASH_TEST_REGION_OFFSET, buf, len);
    if (rc != 0) {
        printf("Flash read failed! %d\n", rc);
        return;
    }

    if (memcmp(expected, buf, len) == 0) {
        printf("Data read matches data written. Good!!\n");
    } else {
        const uint8_t *wp = expected;
        const uint8_t *rp = buf;
        const uint8_t *rpe = rp + len;

        printf("Data read does not match data written!!\n");
        while (rp < rpe) {
            printf("%08x wrote %02x read %02x %s\n",
                   (uint32_t)(SPI_FLASH_TEST_REGION_OFFSET + (rp - buf)),
                   *wp, *rp, (*rp == *wp) ? "match" : "MISMATCH");
            ++rp;
            ++wp;
        }
    }
}

#define FLASH_NODE DT_NODELABEL(xt25f256b)
#define PARTITION_NODE DT_NODELABEL(lfs1)
FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);


int main(void)
{
	printk("test start -----\n");
	k_msleep(500);

	//const struct device *flash_dev = DEVICE_DT_GET(FLASH_NODE);
	const struct device *flash_dev = DEVICE_DT_GET(DT_ALIAS(spi_flash0));
	int ret = device_init(flash_dev);
	if (ret) {
		printk("flash init failed: %d\n", ret);
	}
	if (!device_is_ready(flash_dev)) {
        printk("%s: device not ready.\n", flash_dev->name);
		return 0;
    }

	single_sector_test(flash_dev);

	uint8_t id[3];
    int rc = flash_read_jedec_id(flash_dev, id);
    if (rc < 0) {
        printk("flash_read_jedec_id failed: %d\n", rc);
    }

    printk("JEDEC ID: %02x %02x %02x\n", id[0], id[1], id[2]);

	static struct fs_mount_t *mp = &FS_FSTAB_ENTRY(PARTITION_NODE);
	struct fs_statvfs sbuf;
	rc = fs_mount(mp);
    if (rc < 0) {
        printk("FAIL: mount id %p at %s: %d\n",
                mp->storage_dev, mp->mnt_point, rc);
    }
    printk("%s mount: %d\n", mp->mnt_point, rc);

    rc = fs_statvfs(mp->mnt_point, &sbuf);
    if (rc < 0) {
        printk("FAIL: statvfs: %d\n", rc);
    } else {
        printk("%s: bsize=%lu, frsize=%lu, blocks=%lu, bfree=%lu\n",
                mp->mnt_point,
                (unsigned long)sbuf.f_bsize,
                (unsigned long)sbuf.f_frsize,
                (unsigned long)sbuf.f_blocks,
                (unsigned long)sbuf.f_bfree);
    }

	printk("a-------------------\n");

	const struct device *gpio2_dev = DEVICE_DT_GET(GPIO2_NODE);
	gpio_pin_configure(gpio2_dev, AG3352_VCORE_VRF_DCDC_EN, GPIO_OUTPUT_LOW);
	gpio_pin_configure(gpio2_dev, AG3352_VRTC_LOAD_SW_EN, GPIO_OUTPUT_LOW);
	gpio_pin_configure(gpio2_dev, AG3352_AVDD_VSYS_PWR_CTRL, GPIO_OUTPUT_LOW);
#if 1
	gpio_pin_configure(gpio2_dev, N91_ENABLE, GPIO_OUTPUT_HIGH);
#else
	gpio_pin_configure(gpio2_dev, N91_ENABLE, GPIO_OUTPUT_LOW);
#endif
	npm_initial();
	// pm_device_action_run(uart, PM_DEVICE_ACTION_SUSPEND);

	for (;;)
	{
		printf("Hello World! %s\n", CONFIG_BOARD_TARGET);
		k_msleep(5000);
	}
	return 0;
}
