/*
 * Copyright 2016-2017 Mikeqin <Fengling.Qin@gmail.com>
 * Copyright 2016 Con Kolivas <kernel@kolivas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef _AVALONLC3_H_
#define _AVALONLC3_H_

#include "util.h"
#include "i2c-context.h"

#ifdef USE_AVALONLC3

#define AVALC3_FREQUENCY_MAX		1404

#define AVALC3_DEFAULT_FAN_MIN		5 /* % */
#define AVALC3_DEFAULT_FAN_MAX		100

#define AVALC3_DEFAULT_TEMP_TARGET	90
#define AVALC3_DEFAULT_TEMP_OVERHEAT	105

#define AVALC3_DEFAULT_VOLTAGE_LEVEL_MIN	0
#define AVALC3_DEFAULT_VOLTAGE_LEVEL_MAX	31
#define AVALC3_INVALID_VOLTAGE_LEVEL	-1

#define AVALC3_DEFAULT_VOLTAGE_LEVEL_OFFSET_MIN	-2
#define AVALC3_DEFAULT_VOLTAGE_LEVEL_OFFSET	0
#define AVALC3_DEFAULT_VOLTAGE_LEVEL_OFFSET_MAX	1

#define AVALC3_INVALID_ASIC_OTP	-1

#define AVALC3_DEFAULT_FACTORY_INFO_0_MIN	-15
#define AVALC3_DEFAULT_FACTORY_INFO_0		0
#define AVALC3_DEFAULT_FACTORY_INFO_0_MAX	15
#define AVALC3_DEFAULT_FACTORY_INFO_0_CNT	1
#define AVALC3_DEFAULT_FACTORY_INFO_0_IGNORE	16

#define AVALC3_DEFAULT_FACTORY_INFO_1_CNT	3

#define AVALC3_DEFAULT_OVERCLOCKING_OFF	0
#define AVALC3_DEFAULT_OVERCLOCKING_ON	1

#define AVALC3_DEFAULT_FREQUENCY_0M	0
#define AVALC3_DEFAULT_FREQUENCY_500M	500
#define AVALC3_DEFAULT_FREQUENCY_MAX	1200
#define AVALC3_DEFAULT_FREQUENCY		(AVALC3_DEFAULT_FREQUENCY_MAX)
#define AVALC3_DEFAULT_FREQUENCY_SEL	3

#define AVALC3_DEFAULT_MODULARS		7	/* Only support 6 modules maximum with one AUC */
#define AVALC3_DEFAULT_MINER_CNT	4
#define AVALC3_DEFAULT_ASIC_MAX		34
#define AVALC3_DEFAULT_PLL_CNT		4
#define AVALC3_DEFAULT_CORE_VOLT_CNT	8

#define AVALC3_DEFAULT_POLLING_DELAY	20 /* ms */
#define AVALC3_DEFAULT_NTIME_OFFSET	2

#define AVALC3_DEFAULT_SMARTSPEED_OFF	0
#define AVALC3_DEFAULT_SMARTSPEED_MODE1	1
#define AVALC3_DEFAULT_SMART_SPEED	(AVALC3_DEFAULT_SMARTSPEED_MODE1)

#define AVALC3_DEFAULT_TH_PASS		200
#define AVALC3_DEFAULT_TH_FAIL		7000
#define AVALC3_DEFAULT_TH_INIT		32767
#define AVALC3_DEFAULT_TH_ADD		1
#define AVALC3_DEFAULT_TH_MS		5
#define AVALC3_DEFAULT_TH_TIMEOUT	16000
#define AVALC3_DEFAULT_NONCE_MASK 	27
#define AVALC3_DEFAULT_NONCE_CHECK	1
#define AVALC3_DEFAULT_MUX_L2H		0
#define AVALC3_DEFAULT_MUX_H2L		1
#define AVALC3_DEFAULT_H2LTIME0_SPD	3
#define AVALC3_DEFAULT_ROLL_ENABLE	1
#define AVALC3_DEFAULT_SPDLOW		2
#define AVALC3_DEFAULT_SPDHIGH		3
#define AVALC3_DEFAULT_TBASE		0

/* PID CONTROLLER*/
#define AVALC3_DEFAULT_PID_P		2
#define AVALC3_DEFAULT_PID_I		5
#define AVALC3_DEFAULT_PID_D		0
#define AVALC3_DEFAULT_PID_TEMP_MIN	50
#define AVALC3_DEFAULT_PID_TEMP_MAX	100

#define AVALC3_DEFAULT_IIC_DETECT	false

#define AVALC3_PWM_MAX			0x3FF
#define AVALC3_DRV_DIFFMAX		2700
#define AVALC3_ASIC_TIMEOUT_CONST	419430400 /* (2^32 * 1000) / (256 * 40) */

#define AVALC3_MODULE_DETECT_INTERVAL	30 /* 30 s */

#define AVALC3_AUC_VER_LEN	12	/* Version length: 12 (AUC-YYYYMMDD) */
#define AVALC3_AUC_SPEED	400000
#define AVALC3_AUC_XDELAY  	19200	/* 4800 = 1ms in AUC (11U14)  */
#define AVALC3_AUC_P_SIZE	64

#define AVALC3_CONNECTER_AUC	1
#define AVALC3_CONNECTER_IIC	2

/* avalonlc3 protocol package type from MM protocol.h */
#define AVALC3_MM_VER_LEN		15
#define AVALC3_MM_DNA_LEN		8
#define AVALC3_H1			'C'
#define AVALC3_H2			'N'

#define AVALC3_P_COINBASE_SIZE	(6 * 1024 + 64)
#define AVALC3_P_MERKLES_COUNT	30

#define AVALC3_P_COUNT		40
#define AVALC3_P_DATA_LEN	32

#define AVALC3_OTP_LEN	        32

/* Broadcase with block iic_write*/
#define AVALC3_P_DETECT		0x10

/* Broadcase With non-block iic_write*/
#define AVALC3_P_STATIC		0x11
#define AVALC3_P_JOB_ID		0x12
#define AVALC3_P_COINBASE	0x13
#define AVALC3_P_MERKLES	0x14
#define AVALC3_P_HEADER		0x15
#define AVALC3_P_TARGET		0x16
#define AVALC3_P_JOB_FIN	0x17

/* Broadcase or with I2C address */
#define AVALC3_P_SET		0x20
#define AVALC3_P_SET_FIN	0x21
#define AVALC3_P_SET_VOLT	0x22
#define AVALC3_P_SET_PMU	0x24
#define AVALC3_P_SET_PLL	0x25
#define AVALC3_P_SET_SS		0x26
/* 0x27 reserved */
#define AVALC3_P_SET_FAC	0x28
#define AVALC3_P_SET_OC		0x29

/* Have to send with I2C address */
#define AVALC3_P_POLLING	0x30
#define AVALC3_P_SYNC		0x31
#define AVALC3_P_TEST		0x32
#define AVALC3_P_RSTMMTX	0x33
#define AVALC3_P_GET_VOLT	0x34

/* Back to host */
#define AVALC3_P_ACKDETECT	0x40
#define AVALC3_P_STATUS		0x41
#define AVALC3_P_NONCE		0x42
#define AVALC3_P_TEST_RET	0x43
#define AVALC3_P_STATUS_VOLT	0x46
#define AVALC3_P_STATUS_POWER	0x48
#define AVALC3_P_STATUS_PLL	0x49
#define AVALC3_P_STATUS_LOG	0x4a
#define AVALC3_P_STATUS_ASIC	0x4b
#define AVALC3_P_STATUS_PVT	0x4c
#define AVALC3_P_STATUS_FAC	0x4d
#define AVALC3_P_STATUS_OC	0x4e
#define AVALC3_P_STATUS_OTP	0x4f
#define AVALC3_P_SET_ASIC_OTP	0x50

#define AVALC3_MODULE_BROADCAST	0
/* End of avalonlc3 protocol package type */

#define AVALC3_IIC_RESET	0xa0
#define AVALC3_IIC_INIT		0xa1
#define AVALC3_IIC_DEINIT	0xa2
#define AVALC3_IIC_XFER		0xa5
#define AVALC3_IIC_INFO		0xa6

#define AVALC3_FREQ_INIT_MODE	0x0
#define AVALC3_FREQ_PLLADJ_MODE	0x1

#define AVALC3_DEFAULT_FACTORY_INFO_CNT	(AVALC3_DEFAULT_FACTORY_INFO_0_CNT + AVALC3_DEFAULT_FACTORY_INFO_1_CNT)

#define AVALC3_DEFAULT_POWER_INFO_CNT	6

#define AVALC3_DEFAULT_OVERCLOCKING_CNT	1

#define AVALC3_OTP_INDEX_READ_STEP   	27
#define AVALC3_OTP_INDEX_ASIC_NUM	28
#define AVALC3_OTP_INDEX_CYCLE_HIT	29
#define AVALC3_OTP_INFO_LOTIDCRC_OFFSET	0
#define AVALC3_OTP_INFO_LOTID_OFFSET  	6

struct avalonlc3_pkg {
	uint8_t head[2];
	uint8_t type;
	uint8_t opt;
	uint8_t idx;
	uint8_t cnt;
	uint8_t data[32];
	uint8_t crc[2];
};
#define avalonlc3_ret avalonlc3_pkg

struct avalonlc3_info {
	/* Public data */
	int64_t last_diff1;
	int64_t pending_diff1;
	double last_rej;

	int mm_count;
	int xfer_err_cnt;
	int pool_no;

	struct timeval firsthash;
	struct timeval last_fan_adj;
	struct timeval last_stratum;
	struct timeval last_detect;

	cglock_t update_lock;

	struct pool pool0;
	struct pool pool1;
	struct pool pool2;

	bool work_restart;

	uint32_t last_jobid;

	/* For connecter */
	char auc_version[AVALC3_AUC_VER_LEN + 1];

	int auc_speed;
	int auc_xdelay;
	int auc_sensor;

	struct i2c_ctx *i2c_slaves[AVALC3_DEFAULT_MODULARS];

	uint8_t connecter; /* AUC or IIC */

	/* For modulars */
	bool enable[AVALC3_DEFAULT_MODULARS];
	bool reboot[AVALC3_DEFAULT_MODULARS];

	struct timeval elapsed[AVALC3_DEFAULT_MODULARS];

	uint8_t mm_dna[AVALC3_DEFAULT_MODULARS][AVALC3_MM_DNA_LEN];
	char mm_version[AVALC3_DEFAULT_MODULARS][AVALC3_MM_VER_LEN + 1]; /* It's a string */
	uint32_t total_asics[AVALC3_DEFAULT_MODULARS];
	uint32_t max_ntime; /* Maximum: 7200 */

	uint8_t otp_info[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT][AVALC3_OTP_LEN + 1];

	int mod_type[AVALC3_DEFAULT_MODULARS];
	uint8_t miner_count[AVALC3_DEFAULT_MODULARS];
	uint8_t asic_count[AVALC3_DEFAULT_MODULARS];

	uint32_t freq_mode[AVALC3_DEFAULT_MODULARS];
	int led_indicator[AVALC3_DEFAULT_MODULARS];
	int fan_pct[AVALC3_DEFAULT_MODULARS];
	int fan_cpm[AVALC3_DEFAULT_MODULARS];

	int temp[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT][AVALC3_DEFAULT_ASIC_MAX];
	int temp_mm[AVALC3_DEFAULT_MODULARS];

	uint32_t core_volt[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT] \
			  [AVALC3_DEFAULT_ASIC_MAX][AVALC3_DEFAULT_CORE_VOLT_CNT];

	uint8_t cutoff[AVALC3_DEFAULT_MODULARS];
	int temp_target[AVALC3_DEFAULT_MODULARS];
	int temp_overheat[AVALC3_DEFAULT_MODULARS];

	/* pid controler*/
	int pid_p[AVALC3_DEFAULT_MODULARS];
	int pid_i[AVALC3_DEFAULT_MODULARS];
	int pid_d[AVALC3_DEFAULT_MODULARS];
	double pid_u[AVALC3_DEFAULT_MODULARS];
	int pid_e[AVALC3_DEFAULT_MODULARS][3];
	int pid_0[AVALC3_DEFAULT_MODULARS];

	int set_voltage_level[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT];
	uint32_t set_frequency[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT][AVALC3_DEFAULT_PLL_CNT];
	uint32_t get_frequency[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT][AVALC3_DEFAULT_ASIC_MAX][AVALC3_DEFAULT_PLL_CNT];

	int set_asic_otp[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT];

	uint16_t get_voltage[AVALC3_DEFAULT_MODULARS][1]; /* Output is the same */
	uint32_t get_pll[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT][AVALC3_DEFAULT_PLL_CNT];

	uint32_t get_asic[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT][AVALC3_DEFAULT_ASIC_MAX][6];

	int8_t factory_info[AVALC3_DEFAULT_FACTORY_INFO_CNT];
	int8_t overclocking_info[AVALC3_DEFAULT_OVERCLOCKING_CNT];

	uint64_t local_works[AVALC3_DEFAULT_MODULARS];
	uint64_t local_works_i[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT];
	uint64_t hw_works[AVALC3_DEFAULT_MODULARS];
	uint64_t hw_works_i[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT];
	uint64_t chip_matching_work[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT][AVALC3_DEFAULT_ASIC_MAX];

	uint32_t error_code[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT + 1];
	uint32_t error_crc[AVALC3_DEFAULT_MODULARS][AVALC3_DEFAULT_MINER_CNT];
	uint8_t error_polling_cnt[AVALC3_DEFAULT_MODULARS];

	uint64_t diff1[AVALC3_DEFAULT_MODULARS];

	uint16_t vin_adc_ratio[AVALC3_DEFAULT_MODULARS];
	uint16_t vout_adc_ratio[AVALC3_DEFAULT_MODULARS];

	uint16_t power_info[AVALC3_DEFAULT_POWER_INFO_CNT];

	bool conn_overloaded;
};

struct avalonlc3_iic_info {
	uint8_t iic_op;
	union {
		uint32_t aucParam[2];
		uint8_t slave_addr;
	} iic_param;
};

struct avalonlc3_dev_description {
	uint8_t dev_id_str[8];
	int mod_type;
	uint8_t miner_count;	/* it should not greater than AVALC3_DEFAULT_MINER_CNT */
	uint8_t asic_count;	/* asic count each miner, it should not great than AVALC3_DEFAULT_ASIC_MAX */
	int set_voltage_level;
	uint16_t set_freq[AVALC3_DEFAULT_PLL_CNT];
	int set_asic_otp;
};

#define AVALC3_WRITE_SIZE (sizeof(struct avalonlc3_pkg))
#define AVALC3_READ_SIZE AVALC3_WRITE_SIZE

#define AVALC3_SEND_OK	0
#define AVALC3_SEND_ERROR -1

extern char *set_avalonlc3_fan(char *arg);
extern char *set_avalonlc3_freq(char *arg);
extern char *set_avalonlc3_voltage_level(char *arg);
extern char *set_avalonlc3_voltage_level_offset(char *arg);
extern char *set_avalonlc3_asic_otp(char *arg);
extern int opt_avalonlc3_temp_target;
extern int opt_avalonlc3_polling_delay;
extern int opt_avalonlc3_aucspeed;
extern int opt_avalonlc3_aucxdelay;
extern int opt_avalonlc3_smart_speed;
extern bool opt_avalonlc3_iic_detect;
extern int opt_avalonlc3_freq_sel;
extern uint32_t opt_avalonlc3_th_pass;
extern uint32_t opt_avalonlc3_th_fail;
extern uint32_t opt_avalonlc3_th_init;
extern uint32_t opt_avalonlc3_th_ms;
extern uint32_t opt_avalonlc3_th_timeout;
extern uint32_t opt_avalonlc3_th_add;
extern uint32_t opt_avalonlc3_nonce_mask;
extern uint32_t opt_avalonlc3_nonce_check;
extern uint32_t opt_avalonlc3_mux_l2h;
extern uint32_t opt_avalonlc3_mux_h2l;
extern uint32_t opt_avalonlc3_h2ltime0_spd;
extern uint32_t opt_avalonlc3_roll_enable;
extern uint32_t opt_avalonlc3_spdlow;
extern uint32_t opt_avalonlc3_spdhigh;
extern uint32_t opt_avalonlc3_tbase;
extern uint32_t opt_avalonlc3_pid_p;
extern uint32_t opt_avalonlc3_pid_i;
extern uint32_t opt_avalonlc3_pid_d;

#endif /* USE_AVALONLC3 */
#endif /* _AVALONLC3_H_ */
