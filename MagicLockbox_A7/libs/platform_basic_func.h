/*
 *  
 * Copyright (c) 2019 Seeed Technology Co., Ltd.
 * Website    : www.seeed.cc
 * Author     : downey
 * Create Time: Jan 2019
 * Change Log :
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef PLATFORM_BASIC_FUNC_H
#define PLATFORM_BASIC_FUNC_H


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "hw/avnet_mt3620_sk.h"

#define MG3030_DEFAULE_I2C_ADDR     0x42
#define TRANS_PIN                   MT3620_GPIO28 // MGC3130 TS line SOCKET1: RX. shiled gpio 7
#define RESET_PIN                   MT3620_GPIO26 // MGC3130 Reset line SOCKET1: TX. shield gpio 11
#define MAX_RECV_LEN                255

#define PIN_HIGH				0x01
#define PIN_LOW					0x02
#define PIN_OUTPUT				0x11
#define PIN_INPUT				0x12


int32_t basic_init(void);
int32_t i2c_read_block_data(uint8_t *data);
int32_t i2c_send_msg(void *data,uint32_t len);
void mgc_exit(void);
bool gpio_is_trans_low();
int32_t gpio_pull_trans_low();
int32_t gpio_release_trans();
void delay_us(int ms);

#endif

