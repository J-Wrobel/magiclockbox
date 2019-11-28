/*
 *  Some of the code in this file was copied from ST Micro.  Below is their required information.
 *
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT(c) 2018 STMicroelectronics</center></h2>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the name of STMicroelectronics nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"

#include <applibs/log.h>
#include <applibs/i2c.h>

#include "hw/avnet_mt3620_sk.h"
#include "deviceTwin.h"
#include "azure_iot_utilities.h"
#include "build_options.h"
#include "i2c.h"
#include "lsm6dso_reg.h"

#include "magicKey.h"
#include "libs/Seeed_3D_touch_mgc3030.h"
#include "libs/platform_basic_func.h"

uint8_t data[256];

/* Private variables ---------------------------------------------------------*/

static uint8_t whoamI, rst;
static int accelTimerFd = -1;
const uint8_t lsm6dsOAddress = LSM6DSO_ADDRESS;     // Addr = 0x6A
lsm6dso_ctx_t dev_ctx;

//Extern variables
int i2cFd = -1;
extern int epollFd;
extern volatile sig_atomic_t terminationRequired;

//Private functions
// Routines to read/write to the LSM6DSO device
static int32_t platform_write(int* fD, uint8_t reg, uint8_t* bufp, uint16_t len);
static int32_t platform_read(int* fD, uint8_t reg, uint8_t* bufp, uint16_t len);

/// <summary>
///     Sleep for delayTime ms
/// </summary>
void HAL_Delay(int delayTime) {
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = delayTime * 1000000;
	nanosleep(&ts, NULL);
}

int lsm6dsoInt1GpioFd = -1;
/// <summary>
///     Print latest data from on-board sensors.
/// </summary>
void AccelTimerEventHandler(EventData *eventData)
{
	// Consume the event.  If we don't do this we'll come right back 
	// to process the same event again
	if (ConsumeTimerFdEvent(accelTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

	// Check for interrupt
	static GPIO_Value_Type newIntState;
	int result = GPIO_GetValue(lsm6dsoInt1GpioFd, &newIntState);
	if (result != 0) {
		Log_Debug("ERROR: Could not read interrupt GPIO: %s (%d).\n", strerror(errno), errno);
		terminationRequired = true;
		return;
	}

	if (newIntState == GPIO_Value_High)
	{
		//newIntState = GPIO_Value_Low;
		static lsm6dso_all_sources_t sources;

		lsm6dso_all_sources_get(&dev_ctx, &sources);
			
		if (sources.wake_up_src.sleep_change_ia)
		{
			if (sources.wake_up_src.sleep_state)
			{
				magicLockbox_notifyState(state_inactivity);
			}
			else
			{
				magicLockbox_notifyState(state_activity);
			}
		}
		if (sources.tap_src.single_tap)
		{
			Log_Debug("\nSingle tap, %d\n", sources.tap_src);
			if (sources.tap_src.x_tap)
			{
				Log_Debug(" on X\n");				
				magicLockbox_registerEvent(event_tap_x);				
			}
			else if (sources.tap_src.y_tap)
			{
				Log_Debug(" on Y\n");				
				magicLockbox_registerEvent(event_tap_y);				
			}
			else if (sources.tap_src.z_tap)
			{
				Log_Debug(" on Z\n");				
				magicLockbox_registerEvent(event_tap_z);				
			}
			return;
		}						
		if (sources.d6d_src.d6d_ia)
		{
			Log_Debug("\n6d sense\n");
			if (sources.d6d_src.xh)
			{
				Log_Debug(" on xh\n");
				magicLockbox_registerEvent(event_4d_top_x);
			}
			else if (sources.d6d_src.xl)
			{
				Log_Debug(" on xl\n");
				magicLockbox_registerEvent(event_4d_bottom_x);
			}
			else if (sources.d6d_src.yh)
			{
				Log_Debug(" on yh\n");
				magicLockbox_registerEvent(event_4d_top_y);
			}
			else if (sources.d6d_src.yl)
			{
				Log_Debug(" on yl\n");
				magicLockbox_registerEvent(event_4d_bottom_y);
			}
			else if (sources.d6d_src.zh)
			{
				Log_Debug(" on zh\n");
				magicLockbox_registerEvent(event_4d_top_z);
			}
			else if (sources.d6d_src.zl)
			{
				Log_Debug(" on zl\n");
				magicLockbox_registerEvent(event_4d_bottom_z);
			}
		}
	}

	if (mg3030_read_data(data) >= 3)
	{
		//there is a bug that causes additional value to be inserted
		//into recevied buffer, actual data starts from second byte
		parse_sensor_msg(&data[1]);
		Gest_t gesture = get_last_gesture();
		if (gesture == GESTURE_EAST_TO_WEST)
		{
			magicLockbox_registerEvent(event_swipe_left);
		}
		else if (gesture == GESTURE_WEST_TO_EAST)
		{
			magicLockbox_registerEvent(event_swipe_right);
		}
		else if (gesture == GESTURE_SOUTH_TO_NORTH)
		{
			magicLockbox_registerEvent(event_swipe_up);
		}
		else if (gesture == GESTURE_NORTH_TO_SOUTH)
		{
			magicLockbox_registerEvent(event_swipe_down);
		}
	}

	return;
}

/// <summary>
///     Initializes the I2C interface.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
int initI2c(void) {

	// Begin MT3620 I2C init 

	i2cFd = I2CMaster_Open(MT3620_ISU2_I2C);
	if (i2cFd < 0) {
		Log_Debug("ERROR: I2CMaster_Open: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	int result = I2CMaster_SetBusSpeed(i2cFd, I2C_BUS_SPEED_STANDARD);
	if (result != 0) {
		Log_Debug("ERROR: I2CMaster_SetBusSpeed: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	result = I2CMaster_SetTimeout(i2cFd, 100);
	if (result != 0) {
		Log_Debug("ERROR: I2CMaster_SetTimeout: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}
	
	// Start lsm6dso specific init

	// Initialize lsm6dso mems driver interface
	dev_ctx.write_reg = platform_write;
	dev_ctx.read_reg = platform_read;
	dev_ctx.handle = &i2cFd;

	// Check device ID
	lsm6dso_device_id_get(&dev_ctx, &whoamI);
	if (whoamI != LSM6DSO_ID) {
		Log_Debug("LSM6DSO not found!\n");
		return -1;
	}
	else {
		Log_Debug("LSM6DSO Found!\n");
	}
		
	 // Restore default configuration
	lsm6dso_reset_set(&dev_ctx, PROPERTY_ENABLE);
	do {
		lsm6dso_reset_get(&dev_ctx, &rst);
	} while (rst);

	 // Disable I3C interface
	lsm6dso_i3c_disable_set(&dev_ctx, LSM6DSO_I3C_DISABLE);

	// Enable Block Data Update
	lsm6dso_block_data_update_set(&dev_ctx, PROPERTY_ENABLE);

	 // Set Output Data Rate
	lsm6dso_xl_data_rate_set(&dev_ctx, LSM6DSO_XL_ODR_417Hz);
	lsm6dso_gy_data_rate_set(&dev_ctx, LSM6DSO_GY_ODR_417Hz);

	 // Set full scale
	lsm6dso_xl_full_scale_set(&dev_ctx, LSM6DSO_2g);
	lsm6dso_gy_full_scale_set(&dev_ctx, LSM6DSO_250dps);

	// Configure filtering chain(No aux interface)
	// Accelerometer - LPF1 + LPF2 path	
	lsm6dso_xl_hp_path_on_out_set(&dev_ctx, LSM6DSO_LP_ODR_DIV_400);
	lsm6dso_xl_filter_lp2_set(&dev_ctx, PROPERTY_ENABLE);

	//Enable tap and double tap detection
	lsm6dso_tap_mode_set(&dev_ctx, PROPERTY_DISABLE);
	lsm6dso_tap_detection_on_x_set(&dev_ctx, PROPERTY_ENABLE);
	lsm6dso_tap_detection_on_y_set(&dev_ctx, PROPERTY_ENABLE);
	lsm6dso_tap_detection_on_z_set(&dev_ctx, PROPERTY_ENABLE);
	// Set tap axes detection priority to be ZXY
	lsm6dso_tap_axis_priority_set(&dev_ctx, LSM6DSO_ZYX);
	//Set duration between double tap and between taps as well as max vibration duration to be still recognized as tap	 
	lsm6dso_tap_dur_set(&dev_ctx, 0b01);
	lsm6dso_tap_shock_set(&dev_ctx, 0b10);
	lsm6dso_tap_quiet_set(&dev_ctx, 0b01);
	//Set thresholds to eliminate light taps/ unintended taps
	lsm6dso_tap_threshold_x_set(&dev_ctx, 0b1001);
	lsm6dso_tap_threshold_y_set(&dev_ctx, 0b1011);
	lsm6dso_tap_threshold_z_set(&dev_ctx, 0b1111);

	// route detected activities to interrupt pin int1 
	static lsm6dso_pin_int1_route_t activities_int_routing;
	lsm6dso_pin_int1_route_get(&dev_ctx, &activities_int_routing);
	activities_int_routing.md1_cfg.int1_single_tap = PROPERTY_ENABLE;
	activities_int_routing.md1_cfg.int1_6d = PROPERTY_ENABLE;
	activities_int_routing.md1_cfg.int1_double_tap = PROPERTY_DISABLE;
	activities_int_routing.md1_cfg.int1_sleep_change = PROPERTY_ENABLE;
	activities_int_routing.md1_cfg.int1_wu = PROPERTY_DISABLE;
	
	lsm6dso_pin_int1_route_set(&dev_ctx, &activities_int_routing);
	lsm6dso_xl_hp_path_internal_set(&dev_ctx, LSM6DSO_USE_HPF);
	//lsm6dso_act_pin_notification_set(&dev_ctx, LSM6DSO_DRIVE_SLEEP_STATUS);
		
	//Use latched interrupts because of latency of reads
	lsm6dso_int_notification_set(&dev_ctx, LSM6DSO_ALL_INT_LATCHED);
	//4D is enabled by default, set threshold lower than default 80deg for less sharp detection
	lsm6dso_6d_threshold_set(&dev_ctx, LSM6DSO_DEG_60);
	lsm6dso_xl_lp2_on_6d_set(&dev_ctx, PROPERTY_ENABLE);

	//setup sleep wake thresholds and duraiton
	lsm6dso_wkup_dur_set(&dev_ctx, 0x04);
	lsm6dso_sleep_dur_set(&dev_ctx, 0x09);
	lsm6dso_wkup_threshold_set(&dev_ctx, 0x02);	

	//Setup pin as input to detect interrupt 
	lsm6dsoInt1GpioFd = GPIO_OpenAsInput(MT3620_GPIO6);
	if (lsm6dsoInt1GpioFd < 0) {
		Log_Debug("ERROR: Could not open lsm6dso int1 GPIO6: %s (%d).\n", strerror(errno), errno);
		return -1;
	}

	// Define the period in the build_options.h file
	struct timespec accelReadPeriod = { .tv_sec = ACCEL_READ_PERIOD_SECONDS,.tv_nsec = ACCEL_READ_PERIOD_NANO_SECONDS };
	// event handler data structures. Only the event handler field needs to be populated.
	static EventData accelEventData = { .eventHandler = &AccelTimerEventHandler };
	accelTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &accelReadPeriod, &accelEventData, EPOLLIN);
	if (accelTimerFd < 0) {
		return -1;
	}	

	if (basic_init() < 0) {
		Log_Debug("Basic init failed!!\n");
		return -1;
	}

	read_version_info(data);

	HAL_Delay(150);
	
	if (mgc3030_init() < 0) {
		Log_Debug("MGC3130 init failed!!\n");
		return -1;
	}
	
	
	return 0;
}

/// <summary>
///     Closes the I2C interface File Descriptors.
/// </summary>
void closeI2c(void) {

	CloseFdAndPrintError(i2cFd, "i2c");
	CloseFdAndPrintError(accelTimerFd, "accelTimer");
}

/// <summary>
///     Writes data to the lsm6dso i2c device
/// </summary>
/// <returns>0</returns>

int32_t platform_write(int *fD, uint8_t reg, uint8_t *bufp,
	uint16_t len)
{

#ifdef ENABLE_READ_WRITE_DEBUG
	Log_Debug("platform_write()\n");
	Log_Debug("reg: %0x\n", reg);
	Log_Debug("len: %0x\n", len);
	Log_Debug("bufp contents: ");
	for (int i = 0; i < len; i++) {

		Log_Debug("%0x: ", bufp[i]);
	}
	Log_Debug("\n");
#endif 

	// Construct a new command buffer that contains the register to write to, then the data to write
	uint8_t cmdBuffer[len + 1];
	cmdBuffer[0] = reg;
	for (int i = 0; i < len; i++) {
		cmdBuffer[i + 1] = bufp[i];
	}

#ifdef ENABLE_READ_WRITE_DEBUG
	Log_Debug("cmdBuffer contents: ");
	for (int i = 0; i < len + 1; i++) {

		Log_Debug("%0x: ", cmdBuffer[i]);
	}
	Log_Debug("\n");
#endif

	// Write the data to the device
	int32_t retVal = I2CMaster_Write(*fD, lsm6dsOAddress, cmdBuffer, (size_t)len + 1);
	if (retVal < 0) {
		Log_Debug("ERROR: platform_write: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}
#ifdef ENABLE_READ_WRITE_DEBUG
	Log_Debug("Wrote %d bytes to device.\n\n", retVal);
#endif
	return 0;
}

/// <summary>
///     Reads generic device register from the i2c interface
/// </summary>
/// <returns>0</returns>

/*
 * @brief  Read generic device register (platform dependent)
 *
 * @param  handle    customizable argument. In this examples is used in
 *                   order to select the correct sensor bus handler.
 * @param  reg       register to read
 * @param  bufp      pointer to buffer that store the data read
 * @param  len       number of consecutive register to read
 *
 */
int32_t platform_read(int *fD, uint8_t reg, uint8_t *bufp,
	uint16_t len)
{

#ifdef ENABLE_READ_WRITE_DEBUG
	Log_Debug("platform_read()\n");
	Log_Debug("reg: %0x\n", reg);
	Log_Debug("len: %d\n", len);
;
#endif

	// Set the register address to read
	int32_t retVal = I2CMaster_Write(*fD, lsm6dsOAddress, &reg, 1);
	if (retVal < 0) {
		Log_Debug("ERROR: platform_read(write step): errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	// Read the data into the provided buffer
	retVal = I2CMaster_Read(*fD, lsm6dsOAddress, bufp, len);
	if (retVal < 0) {
		Log_Debug("ERROR: platform_read(read step): errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

#ifdef ENABLE_READ_WRITE_DEBUG
	Log_Debug("Read returned: ");
	for (int i = 0; i < len; i++) {
		Log_Debug("%0x: ", bufp[i]);
	}
	Log_Debug("\n\n");
#endif 	   

	return 0;
}


