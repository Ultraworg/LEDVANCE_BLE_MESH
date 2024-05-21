/* board.h - Board-specific hooks */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _BOARD_H_
#define _BOARD_H_

#include "driver/gpio.h"
#include "esp_ble_mesh_defs.h"

#define LED_ON  1
#define LED_OFF 0

void board_led_operation(uint8_t pin, uint8_t onoff);

void board_init(void);

#endif
