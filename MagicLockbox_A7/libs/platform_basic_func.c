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

#include "platform_basic_func.h"
#include <stdlib.h>
#include <applibs/i2c.h>
#include <applibs/gpio.h>
#include <applibs/log.h>
#include <errno.h>
#include "..\i2c.h"
#include <time.h>


static int rstGpioFd = -1;
static int tsGpioFd = -1;


/********************************************************************/
/*******************************gpio*********************************/
/********************************************************************/

void power_reset(void)
{
	GPIO_SetValue(rstGpioFd, GPIO_Value_Low);
	delay_us(10000);
	GPIO_SetValue(rstGpioFd, GPIO_Value_High);
	delay_us(50000);
}


void gpio_config(void)
{
	if (tsGpioFd > 0)
	{
		close(tsGpioFd);
	}
	if (rstGpioFd > 0)
	{
		close(rstGpioFd);
	}
	tsGpioFd = GPIO_OpenAsOutput(TRANS_PIN, GPIO_OutputMode_OpenDrain, GPIO_Value_High);
	rstGpioFd = GPIO_OpenAsOutput(RESET_PIN, GPIO_OutputMode_PushPull, GPIO_Value_High);

    power_reset();

}



/********************************************************************/
/*******************************i2c*********************************/
/********************************************************************/


int32_t i2c_init(char *path)
{
 
    return 0;
}


int32_t i2c_config(void)
{
	// I2c configured outdie of this module
    return 0;
}


int32_t i2c_read_block_data(uint8_t *data)
{
	// Read the data into the provided buffer
	int32_t retVal = I2CMaster_Read(i2cFd, MG3030_DEFAULE_I2C_ADDR, data, 192);
	if (retVal < 0) {
		Log_Debug("ERROR: platform_read(read step): errno=%d (%s)\n", errno, strerror(errno));
	}
    return retVal; 
}


int32_t i2c_send_msg(void *data,uint32_t len)
{
    if(NULL == data){
        return -1;
    }
    delay_us(10000);
	// Write the data to the device
	int32_t retVal = I2CMaster_Write(i2cFd, MG3030_DEFAULE_I2C_ADDR, data, (size_t)len);
	if (retVal < 0) {
		Log_Debug("ERROR: platform_write: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}
	delay_us(10000);

    return retVal;
}



int32_t basic_init(void)
{
    gpio_config();
    return 0;
}

void mgc_exit(void)
{

}

bool gpio_is_trans_low()
{	
	GPIO_Value_Type ret;
	if (GPIO_GetValue(tsGpioFd, &ret) < 0)
	{
		Log_Debug("Get TS value error\n");
		return -1;
	}

	return (ret == GPIO_Value_Low);
}

int32_t gpio_pull_trans_low()
{	
	int ret = GPIO_SetValue(tsGpioFd, GPIO_Value_Low);
	delay_us(10000);
	return ret;
}

int32_t gpio_release_trans()
{
	int ret = GPIO_SetValue(tsGpioFd, GPIO_Value_High);
	delay_us(10000);

	return ret;
}

void delay_us(int us)
{
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = us * 1000;
	nanosleep(&ts, NULL);
}



