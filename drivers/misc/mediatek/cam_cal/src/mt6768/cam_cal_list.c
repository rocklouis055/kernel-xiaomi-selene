/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/kernel.h>
#include "cam_cal_list.h"
#include "eeprom_i2c_common_driver.h"
#include "eeprom_i2c_custom_driver.h"
#include "kd_imgsensor.h"

/* selene (Redmi 10) OTP readers, in its sensor drivers */
extern unsigned int gc02m1_read_otp_info(struct i2c_client *client,
	unsigned int addr, unsigned char *data, unsigned int size);
extern unsigned int ov02b1b_read_otp_info(struct i2c_client *client,
	unsigned int addr, unsigned char *data, unsigned int size);

struct stCAM_CAL_LIST_STRUCT g_camCalList[] = {
	/* selene (Redmi 10) */
	{OV50C40_OFILM_MAIN_SENSOR_ID, 0xA2, Common_read_region},
	{S5KJN1_OFILM_MAIN_SENSOR_ID, 0xA2, Common_read_region},
	{OV50C40_QTECH_MAIN_SENSOR_ID, 0xA2, Common_read_region},
	{GC02M1_MACRO_AAC_SENSOR_ID, 0xA4, Common_read_region},
	{GC02M1_MACRO_SY_SENSOR_ID, 0xA4, Common_read_region},
	{IMX355_SUNNY_ULTRA_SENSOR_ID, 0xA0, Common_read_region},
	{IMX355_AAC_ULTRA_SENSOR_ID, 0xA0, Common_read_region},
	{OV8856_OFILM_FRONT_SENSOR_ID, 0xA2, Common_read_region},
	{OV8856_AAC_FRONT_SENSOR_ID, 0xA2, Common_read_region},
	{GC02M1B_SENSOR_ID1, 0xA2, gc02m1_read_otp_info},
	{OV02B1B_OFILM_SENSOR_ID, 0xA2, ov02b1b_read_otp_info},
	{OV50C40_OFILM_MAIN_SENSOR_INDIA_ID, 0xA2, Common_read_region},
	{S5KJN1_OFILM_MAIN_SENSOR_INDIA_ID, 0xA2, Common_read_region},
	{OV50C40_QTECH_MAIN_SENSOR_INDIA_ID, 0xA2, Common_read_region},
	{GC02M1_MACRO_AAC_SENSOR_INDIA_ID, 0xA4, Common_read_region},
	{GC02M1_MACRO_SY_SENSOR_INDIA_ID, 0xA4, Common_read_region},
	{IMX355_SUNNY_ULTRA_SENSOR_INDIA_ID, 0xA0, Common_read_region},
	{IMX355_AAC_ULTRA_SENSOR_INDIA_ID, 0xA0, Common_read_region},
	{OV8856_OFILM_FRONT_SENSOR_INDIA_ID, 0xA2, Common_read_region},
	{OV8856_AAC_FRONT_SENSOR_INDIA_ID, 0xA2, Common_read_region},
	{GC02M1B_SUNNY_SENSOR_INDIA_ID, 0xA2, gc02m1_read_otp_info},
	{OV02B1B_OFILM_SENSOR_INDIA_ID, 0xA2, ov02b1b_read_otp_info},
	{S5KJN1_OFILM_MAIN_SENSOR_CN_ID, 0xA2, Common_read_region},
	{OV50C40_QTECH_MAIN_SENSOR_CN_ID, 0xA2, Common_read_region},
	{IMX355_SUNNY_ULTRA_SENSOR_CN_ID, 0xA0, Common_read_region},
	{IMX355_AAC_ULTRA_SENSOR_CN_ID, 0xA0, Common_read_region},
	
	{S5KJNS_SUNNY_SENSOR_ID, 0xA2, Common_read_region},
	{OV50D40_TRULY_SENSOR_ID, 0xA2, Common_read_region},
	{OV8856_SUNNY_SENSOR_ID, 0xA2, Common_read_region},
	{OV8856_OFILM_SENSOR_ID, 0xA2, Common_read_region},
	{IMX355_SUNNY_SENSOR_ID, 0xA0, Common_read_region},
	{SC820CS_TRULY_SENSOR_ID, 0xA0, Common_read_region},
	{SC202CS_SUNNY_SENSOR_ID, 0xA4, Common_read_region},
	{OV02B10_TRULY_SENSOR_ID, 0xA4, Common_read_region},
        /*ADD M19A INDIA*/
	{S5KJNS_SUNNY_SENSOR_INDIA_ID, 0xA2, Common_read_region},
	{OV50D40_TRULY_SENSOR_INDIA_ID, 0xA2, Common_read_region},
	{OV8856_SUNNY_SENSOR_INDIA_ID, 0xA2, Common_read_region},
	{OV8856_OFILM_SENSOR_INDIA_ID, 0xA2, Common_read_region},
	{IMX355_SUNNY_SENSOR_INDIA_ID, 0xA0, Common_read_region},
	{SC820CS_TRULY_SENSOR_INDIA_ID, 0xA0, Common_read_region},
	{SC202CS_SUNNY_SENSOR_INDIA_ID, 0xA4, Common_read_region},
	{OV02B10_TRULY_SENSOR_INDIA_ID, 0xA4, Common_read_region},
	/*Below is commom sensor */
	{IMX519_SENSOR_ID, 0xA0, Common_read_region},
	{S5K2T7SP_SENSOR_ID, 0xA4, Common_read_region},
	{IMX338_SENSOR_ID, 0xA0, Common_read_region},
	{S5K4E6_SENSOR_ID, 0xA8, Common_read_region},
	{IMX386_SENSOR_ID, 0xA0, Common_read_region},
	{S5K3M3_SENSOR_ID, 0xA0, Common_read_region},
	{S5K2L7_SENSOR_ID, 0xA0, Common_read_region},
	{IMX398_SENSOR_ID, 0xA0, Common_read_region},
	{IMX350_SENSOR_ID, 0xA0, Common_read_region},
	{IMX318_SENSOR_ID, 0xA0, Common_read_region},
	{IMX386_MONO_SENSOR_ID, 0xA0, Common_read_region},
	/*B+B. No Cal data for main2 OV8856*/
	{S5K2P7_SENSOR_ID, 0xA0, Common_read_region},
#ifdef SUPPORT_S5K4H7
	{S5K4H7_SENSOR_ID, 0xA0, zte_s5k4h7_read_region},
	{S5K4H7SUB_SENSOR_ID, 0xA0, zte_s5k4h7_sub_read_region},
#endif
	/*  ADD before this line */
	{0, 0, 0}       /*end of list */
};

unsigned int cam_cal_get_sensor_list(
	struct stCAM_CAL_LIST_STRUCT **ppCamcalList)
{
	if (ppCamcalList == NULL)
		return 1;

	*ppCamcalList = &g_camCalList[0];
	return 0;
}


