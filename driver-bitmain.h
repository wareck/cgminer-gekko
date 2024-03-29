/*
 * Copyright 2013 BitMain project
 * Copyright 2013 BitMain <xlc1985@126.com>
 * Copyright 2014-2015 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BITMAIN_H
#define BITMAIN_H

// S3 is a USB S2 with other minor changes (search for USE_ANT_S3)
#if defined(USE_ANT_S3)
#define USE_ANT_S2 1
#endif

#if (defined(USE_ANT_S1) || defined(USE_ANT_S2))

#include "util.h"
#include "klist.h"

#define BITMAIN_RESET_FAULT_DECISECONDS 1
#define BITMAIN_MINER_THREADS 1

#define BITMAIN_IO_SPEED		115200
#define BITMAIN_HASH_TIME_FACTOR	((float)1.67/0x32)
#define BITMAIN_RESET_PITCH	(300*1000*1000)

#define BITMAIN_TOKEN_TYPE_TXCONFIG 0x51
#define BITMAIN_TOKEN_TYPE_TXTASK   0x52
#define BITMAIN_TOKEN_TYPE_RXSTATUS 0x53

#define BITMAIN_DATA_TYPE_RXSTATUS  0xa1
#define BITMAIN_DATA_TYPE_RXNONCE   0xa2

#define BITMAIN_FAN_FACTOR 60
#define BITMAIN_PWM_MAX 0xA0
#define BITMAIN_DEFAULT_FAN_MIN 20
#define BITMAIN_DEFAULT_FAN_MAX 100
#define BITMAIN_DEFAULT_FAN_MAX_PWM 0xA0 /* 100% */
#define BITMAIN_DEFAULT_FAN_MIN_PWM 0x20 /*  20% */

#define BITMAIN_TEMP_TARGET 50
#define BITMAIN_TEMP_HYSTERESIS 3
#define BITMAIN_TEMP_OVERHEAT 75

#define BITMAIN_DEFAULT_TIMEOUT 0x2D
#define BITMAIN_MIN_FREQUENCY 10
#define BITMAIN_MAX_FREQUENCY 1000000
#define BITMAIN_TIMEOUT_FACTOR 12690
#define BITMAIN_DEFAULT_FREQUENCY 282
#define BITMAIN_DEFAULT_CHAIN_NUM 8
#define BITMAIN_DEFAULT_ASIC_NUM 32
#define BITMAIN_DEFAULT_REG_DATA 0

#define BITMAIN_AUTO_CYCLE 1024

#ifdef USE_ANT_S1
#define BITMAIN_FTDI_READSIZE 510
#define BITMAIN_DEFAULT_VOLTAGE 5
#define BITMAIN_WORK_DELAY 1
#else // S2 or S3
#ifdef USE_ANT_S3 // S3
#define BITMAIN_FTDI_READSIZE 510
#define BITMAIN_VOLTAGE_DEF "0000"
#define BITMAIN_VOLTAGE0_DEF 0x00
#define BITMAIN_VOLTAGE1_DEF 0x00
#define BITMAIN_WORK_DELAY 1
#else // S2
#define BITMAIN_FTDI_READSIZE 2048
#define BITMAIN_VOLTAGE_DEF "0725"
#define BITMAIN_VOLTAGE0_DEF 0x07
#define BITMAIN_VOLTAGE1_DEF 0x25
#define BITMAIN_WORK_DELAY 1
#endif
#endif
#define BITMAIN_USB_PACKETSIZE 512
#define BITMAIN_SENDBUF_SIZE 8192
#define BITMAIN_READBUF_SIZE 8192
#define BITMAIN_RESET_TIMEOUT 100
#define BITMAIN_LATENCY 1

#ifdef USE_ANT_S1
#define BITMAIN_READ_TIMEOUT 18 /* Enough to only half fill the buffer */
#define BITMAIN_MAX_WORK_NUM       8
#define BITMAIN_MAX_WORK_QUEUE_NUM 64
#define BITMAIN_MAX_DEAL_QUEUE_NUM 1
#define BITMAIN_MAX_NONCE_NUM      8
#define BITMAIN_MAX_CHAIN_NUM      8
#else // S2 or S3
#ifdef USE_ANT_S3
#define BITMAIN_READ_TIMEOUT 100
#define BITMAIN_MAX_WORK_NUM       8
#define BITMAIN_MAX_WORK_QUEUE_NUM 1024
#define BITMAIN_MAX_DEAL_QUEUE_NUM 2
#define BITMAIN_MAX_NONCE_NUM      128
#define BITMAIN_MAX_CHAIN_NUM      8
#else // S2
#define BITMAIN_READ_TIMEOUT 0 // Ignored
#define BITMAIN_MAX_WORK_NUM       64
#define BITMAIN_MAX_WORK_QUEUE_NUM 4096
#define BITMAIN_MAX_DEAL_QUEUE_NUM 32
#define BITMAIN_MAX_NONCE_NUM      128
#define BITMAIN_MAX_CHAIN_NUM      16
#endif
#endif

#define BITMAIN_MAX_TEMP_NUM       32
#define BITMAIN_MAX_FAN_NUM        32

#ifdef USE_ANT_S1
#define BITMAIN_SEND_STATUS_TIME   10 //s
#define BITMAIN_SEND_FULL_SPACE    128
#else // S2 or S3
#define BITMAIN_SEND_STATUS_TIME   15 //s
#ifdef USE_ANT_S3
#define BITMAIN_SEND_FULL_SPACE    256
#else
#define BITMAIN_SEND_FULL_SPACE    512
#endif
#endif

#define BITMAIN_OVERHEAT_SLEEP_MS_MAX 10000
#define BITMAIN_OVERHEAT_SLEEP_MS_MIN 200
#define BITMAIN_OVERHEAT_SLEEP_MS_DEF 600
#define BITMAIN_OVERHEAT_SLEEP_MS_STEP 200

#ifdef USE_ANT_S2
struct bitmain_packet_head {
	uint8_t token_type;
	uint8_t version;
	uint16_t length;
} __attribute__((packed, aligned(4)));
#endif

struct bitmain_txconfig_token {
	uint8_t token_type;
#ifdef USE_ANT_S1
	uint8_t length;
#else // S2
	uint8_t version;
	uint16_t length;
#endif
	uint8_t reset                :1;
	uint8_t fan_eft              :1;
	uint8_t timeout_eft          :1;
	uint8_t frequency_eft        :1;
	uint8_t voltage_eft          :1;
	uint8_t chain_check_time_eft :1;
	uint8_t chip_config_eft      :1;
	uint8_t hw_error_eft         :1;
	uint8_t beeper_ctrl          :1;
	uint8_t temp_over_ctrl       :1;
	uint8_t fan_home_mode        :1;
	uint8_t reserved1            :5;
#ifndef USE_ANT_S1
	uint8_t chain_check_time;
	uint8_t reserved2;
#endif

	uint8_t chain_num;
	uint8_t asic_num;
	uint8_t fan_pwm_data;
	uint8_t timeout_data;

	uint16_t frequency;
#ifdef USE_ANT_S1
	uint8_t voltage;
	uint8_t chain_check_time;
#else
	uint8_t voltage[2];
#endif

	uint8_t reg_data[4];
	uint8_t chip_address;
	uint8_t reg_address;
	uint16_t crc;
} __attribute__((packed, aligned(4)));

struct bitmain_txtask_work {
	uint32_t work_id;
	uint8_t midstate[32];
	uint8_t data2[12];
} __attribute__((packed, aligned(4)));

struct bitmain_txtask_token {
#ifdef USE_ANT_S1
	uint8_t token_type;
	uint8_t reserved1;
	uint16_t length;
	uint8_t new_block            :1;
	uint8_t reserved2            :7;
	uint8_t reserved3[3];
	struct bitmain_txtask_work works[BITMAIN_MAX_WORK_NUM];
	uint16_t crc;
#else // S2
	uint8_t token_type;
	uint8_t version;
	uint16_t length;
	uint8_t new_block            :1;
	uint8_t reserved1            :7;
	uint8_t diff;
	uint8_t reserved2[2];
	struct bitmain_txtask_work works[BITMAIN_MAX_WORK_NUM];
	uint16_t crc;
#endif
} __attribute__((packed, aligned(4)));

struct bitmain_rxstatus_token {
#ifdef USE_ANT_S1
	uint8_t token_type;
	uint8_t length;
	uint8_t chip_status_eft      :1;
	uint8_t detect_get           :1;
	uint8_t reserved1            :6;
	uint8_t reserved2;

	uint8_t chip_address;
	uint8_t reg_address;
	uint16_t crc;
#else // S2
	uint8_t token_type;
	uint8_t version;
	uint16_t length;
	uint8_t chip_status_eft      :1;
	uint8_t detect_get           :1;
	uint8_t reserved1            :6;
	uint8_t reserved2[3];

	uint8_t chip_address;
	uint8_t reg_address;
	uint16_t crc;
#endif
} __attribute__((packed, aligned(4)));

struct bitmain_rxstatus_data {
#ifdef USE_ANT_S1
	uint8_t data_type;
	uint8_t length;
	uint8_t chip_value_eft       :1;
	uint8_t reserved1            :7;
	uint8_t version;
	uint32_t fifo_space;
	uint32_t reg_value;
	uint32_t nonce_error;
	uint8_t chain_num;
	uint8_t temp_num;
	uint8_t fan_num;
	uint8_t reserved2;
	uint32_t chain_asic_status[BITMAIN_MAX_CHAIN_NUM];
	uint8_t chain_asic_num[BITMAIN_MAX_CHAIN_NUM];
	uint8_t temp[BITMAIN_MAX_TEMP_NUM];
	uint8_t fan[BITMAIN_MAX_FAN_NUM];
	uint16_t crc;
#else // S2
	uint8_t data_type;
	uint8_t version;
	uint16_t length;
	uint8_t chip_value_eft       :1;
	uint8_t reserved1            :7;
	uint8_t chain_num;
	uint16_t fifo_space;
	uint8_t hw_version[4];
	uint8_t fan_num;
	uint8_t temp_num;
	uint16_t fan_exist;
	uint32_t temp_exist;
	uint32_t nonce_error;
	uint32_t reg_value;
	uint32_t chain_asic_exist[BITMAIN_MAX_CHAIN_NUM*8];
	uint32_t chain_asic_status[BITMAIN_MAX_CHAIN_NUM*8];
	uint8_t chain_asic_num[BITMAIN_MAX_CHAIN_NUM];
	uint8_t temp[BITMAIN_MAX_TEMP_NUM];
	uint8_t fan[BITMAIN_MAX_FAN_NUM];
	uint16_t crc;
#endif
} __attribute__((packed, aligned(4)));

struct bitmain_rxnonce_nonce {
	uint32_t work_id;
	uint32_t nonce;
} __attribute__((packed, aligned(4)));

struct bitmain_rxnonce_data {
#ifdef USE_ANT_S1
	uint8_t data_type;
	uint8_t length;
	uint8_t fifo_space;
	uint8_t nonce_num;
	struct bitmain_rxnonce_nonce nonces[BITMAIN_MAX_NONCE_NUM];
	uint16_t crc;
#else
	uint8_t data_type;
	uint8_t version;
	uint16_t length;
	uint16_t fifo_space;
	uint16_t diff;
	uint64_t total_nonce_num;
	struct bitmain_rxnonce_nonce nonces[BITMAIN_MAX_NONCE_NUM];
	uint16_t crc;
#endif
} __attribute__((packed, aligned(4)));

/* how many usec stats ranges
 * [0] for a call that didn't process
 * [1] to [PROFILE_STATS] for less than NxPROFILE_STEP_USEC
 * [PROFILE_STATS+1] for > PROFILE_STATSxPROFILE_STEP_USEC */
#define PROFILE_STATS 10
#define PROFILE_STEP_USEC 200

// set to 0/1 to disable/enable updating the stats
#if 1
#define PROFILE_START(_stt) cgtime(&(_stt))
#define PROFILE_ZERO(_ranges) (_ranges)[0]++
#define PROFILE_FINISH(_stt, _count, _total, _ranges) do { \
		struct timeval _fin; \
		int64_t _tot, _range; \
		cgtime(&(_fin)); \
		(_count)++; \
		_tot = _fin.tv_usec - (_stt).tv_usec; \
		_tot += ((_fin.tv_sec - (_stt).tv_sec) * 1000000); \
		(_total) += _tot; \
		_range = _tot / PROFILE_STEP_USEC; \
		if (_range < 0) \
			_range = 0; \
		else if (_range > PROFILE_STATS) \
			_range = PROFILE_STATS; \
		(_ranges)[(int)(_range+1)]++; \
	} while(0)
#define PROFILE_FINISH2(_stt, _count, _total, _total2, _ranges, _ranges2, _delta) do { \
		struct timeval _fin; \
		int64_t _tot, _range; \
		cgtime(&(_fin)); \
		(_count)++; \
		_tot = _fin.tv_usec - (_stt).tv_usec; \
		_tot += ((_fin.tv_sec - (_stt).tv_sec) * 1000000); \
		(_total) += _tot; \
		_range = _tot / PROFILE_STEP_USEC; \
		if (_range < 0) \
			_range = 0; \
		else if (_range > PROFILE_STATS) \
			_range = PROFILE_STATS; \
		(_ranges)[(int)(_range+1)]++; \
		_tot -= _delta; \
		(_total2) += _tot; \
		_range = _tot / PROFILE_STEP_USEC; \
		if (_range < 0) \
			_range = 0; \
		else if (_range > PROFILE_STATS) \
			_range = PROFILE_STATS; \
		(_ranges2)[(int)(_range+1)]++; \
	} while(0)
#else
// Avoid gcc warnings
#define PROFILE_START(_stt) do { \
		if ((_stt).tv_sec != 0L) \
			(_stt).tv_sec = 0L; \
	} while (0)
#define PROFILE_ZERO(_ranges) (_ranges)[0] = 0L
#define PROFILE_FINISH(_stt, _count, _total, _ranges) _count = 0L
#define PROFILE_FINISH2(_stt, _count, _total, _total2, _ranges, _ranges2, _delta) _count = 0L
#endif

struct bitmain_info {
	int queued;
	int results;
#ifdef USE_ANT_S1
	int baud;
	int chain_num;
	int asic_num;
	int chain_asic_num[BITMAIN_MAX_CHAIN_NUM];
	uint32_t chain_asic_status[BITMAIN_MAX_CHAIN_NUM];
	char chain_asic_status_t[BITMAIN_MAX_CHAIN_NUM][40];
#else // S2 or S3
#ifndef USE_ANT_S3
	int device_fd;
#endif
	int baud;
	int chain_num;
	int asic_num;
	int chain_asic_num[BITMAIN_MAX_CHAIN_NUM];
	uint32_t chain_asic_exist[BITMAIN_MAX_CHAIN_NUM*8];
	uint32_t chain_asic_status[BITMAIN_MAX_CHAIN_NUM*8];
	char chain_asic_status_t[BITMAIN_MAX_CHAIN_NUM][320];
#endif
	int timeout;
	int errorcount;
	uint32_t nonce_error;
	uint32_t last_nonce_error;
	uint8_t reg_data[4];

	int fan_num;
	int fan[BITMAIN_MAX_FAN_NUM];
	int temp_num;
	int temp[BITMAIN_MAX_TEMP_NUM];

	int temp_max;
	int temp_avg;
	int temp_history_count;
	int temp_history_index;
	int temp_sum;
	int fan_pwm;

	int frequency;
	int temp_hi;
#ifdef USE_ANT_S1
	int voltage;
#else
	uint8_t voltage[2];
	uint64_t total_nonce_num;
	int diff;
#endif

	int no_matching_work;
	//int matching_work[BITMAIN_DEFAULT_CHAIN_NUM];

	struct thr_info *thr;
	pthread_t read_thr;
	pthread_t write_thr;
	pthread_mutex_t lock;
	pthread_mutex_t qlock;
	pthread_cond_t qcond;
	cgsem_t write_sem;
	int nonces;
	int fifo_space;
	unsigned int last_work_block;
	struct timeval last_status_time;
	int send_full_space;
#ifdef USE_ANT_S2
	int hw_version[4];
#endif

	int auto_queued;
	int auto_hw;

	int idle;
	bool reset;
	bool optimal;
#ifdef USE_ANT_S1
	bool overheat;
	int overheat_temp;
	uint32_t overheat_count;
	uint32_t overheat_sleep_ms;
	uint32_t overheat_sleeps;
	uint32_t overheat_slept;
	uint64_t overheat_total_sleep;
	uint32_t overheat_recovers;
#endif

	// Work
	K_LIST *work_list;
	K_STORE *work_ready;
#ifdef USE_ANT_S2
	K_STORE *wbuild;
#endif
	uint32_t last_wid;
	uint64_t work_search;
	uint64_t tot_search;
	uint64_t min_search;
	uint64_t max_search;

	uint64_t failed_search;
	uint64_t tot_failed;
	uint64_t min_failed;
	uint64_t max_failed;

	uint64_t fill_calls;
	uint64_t fill_loop_count[BITMAIN_MAX_WORK_NUM+1];
	int64_t fill_usec_count;
	int64_t fill_usec;
	int64_t fill_usec_ranges[PROFILE_STATS+2];
	uint64_t fill_nospace;
	uint64_t fifo_checks;
	uint64_t fill_neededless;
	uint64_t fill_totalneeded;
	uint64_t fill_need[BITMAIN_MAX_WORK_NUM+1];
	uint64_t fill_want;
	bool got_work;
	uint64_t fill_start_nowork;
	uint64_t fill_nowork;
	uint64_t fill_roll;
	int fill_rollmin;
	int fill_rollmax;
	uint64_t fill_rolltot;
	uint64_t fill_rolllimit;
	uint64_t fill_toosmall;
	uint64_t fill_less;
	uint64_t fill_sends;
	uint64_t fill_totalsend;
	uint64_t fill_send[BITMAIN_MAX_WORK_NUM+1];
	uint64_t fill_sendless[BITMAIN_MAX_WORK_NUM+1];
	uint64_t fill_seterr;
	uint64_t fill_senderr;
	uint64_t fill_sleepsa;
	uint64_t fill_sleepsb;
	uint64_t need_over;
	uint64_t need_nowork[BITMAIN_MAX_WORK_NUM+1];
	uint64_t fill_sendstatus;
	int64_t fill_stat_usec_count;
	int64_t fill_stat_usec;
	int64_t fill_stat_usec_ranges[PROFILE_STATS+2];
	uint64_t read_good;
	uint64_t read_size;
	uint64_t read_0s;
	uint64_t read_18s;
	int read_sizemin;
	int read_sizemax;
	uint64_t read_bad;
	uint64_t readbuf_over;
	uint64_t get_results;
	uint64_t get_sleepsa;
	uint64_t get_sleepsb;
	uint64_t get_sleepsc;
	int64_t get_usec_count;
	int64_t get_usec;
	int64_t get_usec_ranges[PROFILE_STATS+2];
	int64_t get_usec2;
	int64_t get_usec2_ranges[PROFILE_STATS+2];
};

// Work
typedef struct witem {
	struct work *work;
	uint32_t wid;
} WITEM;

#ifdef USE_ANT_S1
#define ALLOC_WITEMS 4096
#else
#ifdef USE_ANT_S3
#define ALLOC_WITEMS 4096
#else // S2
#define ALLOC_WITEMS 32768
#endif
#endif
/*
 * The limit doesn't matter since we simply take the tail item
 * every time, optionally free it, and then put it on the head
 */
#define LIMIT_WITEMS ALLOC_WITEMS

#define DATAW(_item) ((WITEM *)(_item->data))

#define BITMAIN_READ_SIZE 12
#ifdef USE_ANT_S2
#define BITMAIN_ARRAY_SIZE 16384
#endif

#define BTM_GETS_ERROR -1
#define BTM_GETS_OK 0

#define BTM_SEND_ERROR -1
#define BTM_SEND_OK 0

#define BITMAIN_READ_TIME(baud) ((double)BITMAIN_READ_SIZE * (double)8.0 / (double)(baud))
#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

// All command options

extern char *opt_bitmain_options;
extern char *opt_bitmain_freq;

#ifdef USE_ANT_S2
extern bool opt_bitmain_checkall;
extern bool opt_bitmain_checkn2diff;
#ifndef USE_ANT_S3
extern char *opt_bitmain_dev;
#endif
#endif

extern struct bitmain_info **bitmain_info;
extern bool opt_bitmain_hwerror;
#ifdef USE_ANT_S2
extern bool opt_bitmain_checkall;
extern bool opt_bitmain_checkn2diff;
#endif
extern bool opt_bitmain_beeper;
extern bool opt_bitmain_tempoverctrl;
extern int opt_bitmain_temp;
extern int opt_bitmain_workdelay;
extern int opt_bitmain_overheat;
extern int opt_bitmain_fan_min;
extern int opt_bitmain_fan_max;
extern bool opt_bitmain_auto;
extern char *set_bitmain_fan(char *arg);

#endif /* USE_ANT_S1 || USE_ANT_S2 */
#endif	/* BITMAIN_H */
