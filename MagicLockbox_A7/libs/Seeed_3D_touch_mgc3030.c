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
#include "Seeed_3D_touch_mgc3030.h"
#include <applibs/log.h>



Sensor_info_t mgc_info;

void print_recv_buf(uint8_t *data,uint32_t len)
{
    printf("Recv buf = ");
    for(int i=0;i<len;i++){
        printf(" %x ",data[i]);
    }
    printf("\n");
}

int32_t mg3030_read_data(void *data)
{
    int32_t ret = -1;
    uint32_t time_out = 0;
	
    /*while(!gpio_is_trans_low()){
		delay_us(200);
        time_out++;
        if(time_out > 1 ){
            Log_Debug("wait for sensor data time out!\n");
            return -1;
        }
    }*/	
    if (gpio_is_trans_low()) 
	{	  
		gpio_pull_trans_low();
		delay_us(10000);
		mgc_info.gesture = GESTURE_NOT_DEFINED;
        ret = i2c_read_block_data(data);
		delay_us(10000);
		gpio_release_trans();
	}
    return ret;
}


int32_t read_version_info(void *data)
{
    int ret = 0;
    ret = mg3030_read_data(data);
    // print_recv_buf(data,255);
    return ret;
}

static void generate_runtime_param_pack(uint16_t id,uint32_t arg0,uint32_t arg1,uint8_t *data)
{
    /*msg size*/
    data[0] = 0x10;
    /*flag*/
    data[1] = 0;
    /*sequence*/
	data[2] = 0;
    /*set runtime param cmd id.*/
	data[3] = SET_RUNTIME_PARAM_CMD;
    /*set runtime param internal id.*/
    data[4] = id & 0xFFU;
	data[5] = id >> 8U;
    /*Two reserve bit.*/
    data[6] = 0U;
	data[7] = 0U;
    /*arg0 , 4 bit*/
    data[8] = arg0 & 0xFFU;
	arg0 >>= 8;
	data[9] = arg0 & 0xFFU;
	arg0 >>= 8;
	data[10] = arg0 & 0xFFU;
	arg0 >>= 8;
	data[11] = arg0 & 0xFFU;
    /*arg1 4 bit*/
    data[12] = arg1 & 0xFFU;
	arg1 >>= 8;
	data[13] = arg1 & 0xFFU;
	arg1 >>= 8;
	data[14] = arg1 & 0xFFU;
	arg1 >>= 8;
	data[15] = arg1 & 0xFFU;
    //print_recv_buf(data,16);
}



/* Argument values, depending on runtime parameter ID. If not used, Argument0
should be provided as '0' */

int32_t mg3030_set_runtime_param(uint16_t run_time_id,uint32_t arg0,uint32_t arg1)
{
    uint8_t buf[16] = {0};
    uint8_t recv_buf[MAX_RECV_LEN] = {0};
    generate_runtime_param_pack(run_time_id,arg0,arg1,buf);
    //i2c_read_block_data(recv_buf);
    // memset(recv_buf,0,sizeof(recv_buf));
    if(i2c_send_msg(buf,16) >= 0){
        // printf("Send msg ok\n");
		//delay_us(10000);
        //int ret = i2c_read_block_data(recv_buf);
        // print_recv_buf(recv_buf,ret);
        // printf("Read msg ok,len = %d\n",ret);
		//delay_us(10000);
        //if(ret >= 16){
            return 0;
        //}
    }
    return -1;
}

// typedef enum{
//     INIT = 0,
//     DSP_STATUS_ENABLE = 1<<0,
//     GESTURE_DATA = 1<<1,
//     TOUCH_INFO = 1<<2,
//     AIR_WHEEL_INFO = 1<<3,
//     XYZ_POSITION = 1<<4,
//     NOISE_POWER = 1<<5,
// }Sensor_func_t;




int32_t set_lock_mask(void)
{
    uint32_t arg0,arg1;
    arg0 = 0x00U;
    arg1 = 0xffffffffU;
    return mg3030_set_runtime_param(RUNTIME_PARAM_ID_DATA_OUTPUT_LOCK_MASK,arg0,arg1);
}

int32_t air_wheel_select(Enable_t flag)
{
    uint32_t arg0,arg1;
    if(flag){
        arg0 = 0x20U;
        arg1 = 0x20U;
    }
    else{
        arg0 = 0U;
        arg1 = 0x20U;
    }
    return mg3030_set_runtime_param(SELECT_AIR_WHEEL_FUNCTION_ID,arg0,arg1);
}

int32_t gestrue_select(Gesture_select_t flag)
{
    uint32_t arg0,arg1;
    switch(flag){
        case ENABLE_ALL_GESTURE:
            arg0 = 0x7fU;
            arg1 = 0xffffffffU;
            break;
        case ENABLE_ONLY_FLICK_GESTURE:
            arg0 = 0x1fU;
            arg1 = 0xffffffffU;
            break;
        case ENABLE_IN_ADDITION_CIRCLES:
            arg0 = 0x60U;
            arg1 = 0xffffffffU;
            break;
        case DISABLE_ALL_GESTURE:
            arg0 = 0x0U;
            arg1 = 0xffffffffU;
        default:break;   
    }
    return mg3030_set_runtime_param(SELECT_GESTURE_FUNCTION_ID,arg0,arg1);
}


int32_t calibration_select(Enable_t flag)
{
    uint32_t arg0,arg1;
    if(flag){
        arg0 = 0x0U;
        arg1 = 0x3fU;
    }
    else{
        arg0 = 0x1bU;
        arg1 = 0x1fU;
    }
    return mg3030_set_runtime_param(RUNTIME_PARAM_ID_CALIBRATION_OPERATION_MODE,arg0,arg1);
}


int32_t output_enable_mask_select(Enable_t flag)
{
    uint32_t arg0,arg1;
    if(flag){
        arg0 = 0b1111U;
        arg1 = 0xffffffffU;
    }
    else{
        arg0 = 0U;
        arg1 = 0xffffffffU;
    }
    return mg3030_set_runtime_param(ENABLE_MASK_BIT,arg0,arg1);
}

Gest_t get_last_gesture(void)
{	
	return mgc_info.gesture;
}


int32_t touch_detection_select(Enable_t flag)
{
    uint32_t arg0,arg1;
    if(flag){
        arg0 = 0x08U;
        arg1 = 0x08U;
    }
    else{
        arg0 = 0x00U;
        arg1 = 0x08U;
    }
    return mg3030_set_runtime_param(SELECT_DETECTION_FUNCTION_ID,arg0,arg1);
}


int32_t approach_detection_select(Enable_t flag)
{
    uint32_t arg0,arg1;
    if(flag){
        arg0 = 0x01U;
        arg1 = 0x01U;
    }
    else{
        arg0 = 0x00U;
        arg1 = 0x01U;
    }
    return mg3030_set_runtime_param(SELECT_APPROACH_DETECTION_FUNCTION_ID,arg0,arg1);
}

//these were externs
uint8_t ges_cnt;
uint8_t pos_cnt;
uint8_t air_wheel_cnt;
uint8_t touch_cnt;

void show_location(int32_t x,int32_t y ,int32_t z)
{
    char x_string[60] = {0};
    char y_string[60] = {0};
    char z_string[60] = {0}; 
    sprintf(x_string,"Position X : %d     ", x );
    sprintf(y_string,"Position Y : %d     ", y );
    sprintf(z_string,"Position Z : %d     ", z );
	Log_Debug(x_string);
	Log_Debug("\n");
	Log_Debug(y_string);
	Log_Debug("\n");
	Log_Debug(z_string);    
	Log_Debug("\n");
	pos_cnt = 0;
}

void show_airwheel_angle(int32_t airwheel_angle)
{
    char angle_string[60] = {0};    
    sprintf(angle_string,"Airwheel angle : %d     ",airwheel_angle);
	Log_Debug(angle_string);
	Log_Debug("\n");
    air_wheel_cnt = 0;
}   

void show_gesture(char *ges_str)
{
	Log_Debug(ges_str);
	Log_Debug("\n");
    ges_cnt = 0;
}
static char *touch_info[16] ={"South  ","West  ","North  ","East  ","Center  ","South  ","West  ","North  ","East  ","Center  ","South  ",
"West  ","North  ","East  ","Center  "};

void show_touch(uint8_t gesture)
{
    char touch_string[60] = {0};
    if(gesture >= 0 && gesture < 5)
    {        
        sprintf(touch_string,"Touch electrode : %s",touch_info[gesture]);        
    }
    else if(gesture >= 5 && gesture < 10)
    {
        sprintf(touch_string,"Tap electrode : %s",touch_info[gesture]);        
    }
    else if(gesture >= 10 && gesture < 15)
    {
        sprintf(touch_string,"Double Tap electrode : %s",touch_info[gesture]);
    }
    else{

    }
	Log_Debug(touch_string);
	Log_Debug("\n");
    touch_cnt = 0;
}

void get_location_xyz(uint8_t *data)
{
    uint16_t x=0,y=0,z=0;
    
    x = data[GESTIC_XYZ_DATA+1] << 8 | data[GESTIC_XYZ_DATA];
    y = data[GESTIC_XYZ_DATA+3] << 8 | data[GESTIC_XYZ_DATA+2];
    z = data[GESTIC_XYZ_DATA+5] << 8 | data[GESTIC_XYZ_DATA+4];

    show_location(x,y,z);
}

void get_airwheel_status(uint8_t *data)
{
    int32_t angle = (int32_t)(data[14] & 0x1F) * 360 / 32;
    int32_t wheel_cnt = data[14] >> 5;
    int32_t wheel_angle = wheel_cnt * 360 + angle;
    int32_t differ_angle = wheel_angle - mgc_info.angle;

    if (differ_angle < (-(int16_t)4*360)) {
        differ_angle += (8*360);
    } else if (differ_angle > ((int16_t)4*360)) {
        differ_angle -= (8*360);
    }
    mgc_info.angle +=  differ_angle;

    show_airwheel_angle(mgc_info.angle);    
}

void get_gesture_event(uint8_t *data)
{
    char ges_string[50] = {0};
    uint8_t gesture = 0;
    /*data 6-9 is gesture info data.*/
    gesture = data[6];
    switch(gesture){
        case 1:
        break;
        case 2:
        sprintf(ges_string,"Gesture : West to East");
        show_gesture(ges_string);
		mgc_info.gesture = GESTURE_WEST_TO_EAST;
        break;
        case 3:
        sprintf(ges_string,"Gesture : East to West");
        show_gesture(ges_string);
		mgc_info.gesture = GESTURE_EAST_TO_WEST;
        break;
        case 4:
        sprintf(ges_string,"Gesture : South to North");
        show_gesture(ges_string);
		mgc_info.gesture = GESTURE_SOUTH_TO_NORTH;
        break;
        case 5:
        sprintf(ges_string,"Gesture : North to South");
        show_gesture(ges_string);
		mgc_info.gesture = GESTURE_NORTH_TO_SOUTH;
        break;
        default:
        sprintf(ges_string,"Gesture : Unknow gesture");
        show_gesture(ges_string);
        break;
    }
}

void get_touch_event(uint8_t *data)
{
    uint16_t touch_action = 0;
    touch_action = data[10] | data[11] << 8;
    for(int i=0;i<15;i++){
        if(touch_action & 1<<i){
            show_touch(i);
        }
    }
}

int32_t parse_sensor_msg(uint8_t *data)
{   
    uint8_t *payload = &data[4];
    /*0x91,indicate sensor data output!*/
    if(SENSOR_OUTPUT_DATA == data[3] ){
        uint8_t action = payload[3];
        
        if((payload[0] & 1<<4) && (action & 0x01)){
            get_location_xyz(payload);
        }
        if((payload[0] & 0x08) && (action & 0x02)){
            get_airwheel_status(payload);
        }
        if((payload[0] & 0x02) && payload[6] > 0){
            get_gesture_event(payload);
        }
        if(payload[0] & 0x04){
            get_touch_event(payload);
        }
    }
    else{
        //  printf("Not support cmd now!\n");
        return -1;
    }
    return 0;
}

int32_t mgc3030_init(void)
{
	mgc_info.angle = 0;
	mgc_info.gesture = GESTURE_NOT_DEFINED;
	mgc_info.touch = TOUCH_NOT_DEFINED;

	
	if (air_wheel_select(DISABLE)) {
		Log_Debug("Select airwheel failed!\n");
		mgc_exit();
		return -1;
	}

	if (gestrue_select(ENABLE_ONLY_FLICK_GESTURE)) {
		Log_Debug("Select gestures failed!\n");
		mgc_exit();
		return -1;
	}

	if (touch_detection_select(DISABLE)) {
		Log_Debug("Select touch detection failed!\n");
		mgc_exit();
		return -1;
	}

	if (approach_detection_select(DISABLE)) {
		Log_Debug("Select approach detection failed!!\n");
		mgc_exit();
		return -1;
	}

	if (output_enable_mask_select(ENABLE)) {
		Log_Debug("Set output mask failed!\n");
		mgc_exit();
		return -1;
	}

	if (set_lock_mask()) {
		Log_Debug("Set lock mask failed!\n");
		mgc_exit();
		return -1;
	}

	if (calibration_select(ENABLE)) {
		Log_Debug("Select calibration failed!\n");
		mgc_exit();
		return -1;
	}
	
	return 0;
}











