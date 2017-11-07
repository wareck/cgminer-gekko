/*
 * Copyright 2012-2013 Andrew Smith
 * Copyright 2012 Xiangfu <xiangfu@openmobilefree.com>
 * Copyright 2013-2015 Con Kolivas <kernel@kolivas.org>
 * Copyright 2015 David McKinnon
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

/*
 * This is a driver for GekkoScience hardware, based initially off of the icarus driver.
 * I have been cutting out a lot of garbage but it's still pretty crappy.
 * Supported hardware:
 * - GekkoScience Compac USB stick
 *
 * Todo: Currently, most stuff.  Gut out a lot of broken functionality and leave only "u3"
 * support as the compac runs well as a U3.  It will need more work to drive multiple 
 * chips.
 */



#include <float.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>

#include "config.h"

#ifdef WIN32
#include <windows.h>
#endif

#include "compat.h"
#include "miner.h"
#include "usbutils.h"

// The serial I/O speed - Linux uses a define 'B115200' in bits/termios.h
#define GEKKO_IO_SPEED 115200

#define GEKKO_BUF_SIZE 8
// The size of a successful nonce read
#define ANT_READ_SIZE 5
#define GEKKO_READ_SIZE 4

// Ensure the sizes are correct for the Serial read
#if (GEKKO_READ_SIZE != 4)
#error GEKKO_READ_SIZE must be 4
#endif
#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

// TODO: USB? Different calculation? - see usbstats to work it out e.g. 1/2 of normal send time
//  or even use that number? 1/2
// #define GEKKO_READ_TIME(baud) ((double)GEKKO_READ_SIZE * (double)8.0 / (double)(baud))
// maybe 1ms?
#define GEKKO_READ_TIME(baud) (0.001)

// USB ms timeout to wait - user specified timeouts are multiples of this
#define ICA_WAIT_TIMEOUT 100
#define ANT_WAIT_TIMEOUT 10
#define AU3_WAIT_TIMEOUT 1
#define GEKKO_WAIT_TIMEOUT (info->u3 ? AU3_WAIT_TIMEOUT : (info->ant ? ANT_WAIT_TIMEOUT : ICA_WAIT_TIMEOUT))

// Defined in multiples of GEKKO_WAIT_TIMEOUT
// Must of course be greater than GEKKO_READ_COUNT_TIMING/GEKKO_WAIT_TIMEOUT
// There's no need to have this bigger, since the overhead/latency of extra work
// is pretty small once you get beyond a 10s nonce range time and 10s also
// means that nothing slower than 429MH/s can go idle so most gekko devices
// will always mine without idling
#define GEKKO_READ_TIME_LIMIT_MAX 100

// In timing mode: Default starting value until an estimate can be obtained
// 5000 ms allows for up to a ~840MH/s device
#define GEKKO_READ_COUNT_TIMING	5000

// Antminer USB is > 1GH/s so use a shorter limit
// 1000 ms allows for up to ~4GH/s device
#define ANTUSB_READ_COUNT_TIMING	1000

#define ANTU3_READ_COUNT_TIMING		100

#define GEKKO_READ_COUNT_MIN		GEKKO_WAIT_TIMEOUT
#define SECTOMS(s)	((int)((s) * 1000))
// How many ms below the expected completion time to abort work
// extra in case the last read is delayed
#define GEKKO_READ_REDUCE	((int)(GEKKO_WAIT_TIMEOUT * 1.5))

// For a standard Gekko REV3 (to 5 places)
// Since this rounds up a the last digit - it is a slight overestimate
// Thus the hash rate will be a VERY slight underestimate
// (by a lot less than the displayed accuracy)
// Minor inaccuracy of these numbers doesn't affect the work done,
// only the displayed MH/s
#define GEKKO_REV3_HASH_TIME 0.0000000026316
#define LANCELOT_HASH_TIME 0.0000000025000
#define ASICMINERUSB_HASH_TIME 0.0000000029761
// TODO: What is it?
#define CAIRNSMORE1_HASH_TIME 0.0000000027000
// Per FPGA
#define CAIRNSMORE2_HASH_TIME 0.0000000066600
#define NANOSEC 1000000000.0
#define ANTMINERUSB_HASH_MHZ 0.000000125
#define ANTMINERUSB_HASH_TIME (ANTMINERUSB_HASH_MHZ / (double)(opt_compac_freq))
#define ANTU3_HASH_MHZ 0.0000000032
#define ANTU3_HASH_TIME (ANTU3_HASH_MHZ / (double)(opt_compac_freq))

#define CAIRNSMORE2_INTS 4

// Gekko Rev3 doesn't send a completion message when it finishes
// the full nonce range, so to avoid being idle we must abort the
// work (by starting a new work item) shortly before it finishes
//
// Thus we need to estimate 2 things:
//	1) How many hashes were done if the work was aborted
//	2) How high can the timeout be before the Gekko is idle,
//		to minimise the number of work items started
//	We set 2) to 'the calculated estimate' - GEKKO_READ_REDUCE
//	to ensure the estimate ends before idle
//
// The simple calculation used is:
//	Tn = Total time in seconds to calculate n hashes
//	Hs = seconds per hash
//	Xn = number of hashes
//	W  = code/usb overhead per work
//
// Rough but reasonable estimate:
//	Tn = Hs * Xn + W	(of the form y = mx + b)
//
// Thus:
//	Line of best fit (using least squares)
//
//	Hs = (n*Sum(XiTi)-Sum(Xi)*Sum(Ti))/(n*Sum(Xi^2)-Sum(Xi)^2)
//	W = Sum(Ti)/n - (Hs*Sum(Xi))/n
//
// N.B. W is less when aborting work since we aren't waiting for the reply
//	to be transferred back (GEKKO_READ_TIME)
//	Calculating the hashes aborted at n seconds is thus just n/Hs
//	(though this is still a slight overestimate due to code delays)
//

// Both below must be exceeded to complete a set of data
// Minimum how long after the first, the last data point must be
#define HISTORY_SEC 60
// Minimum how many points a single GEKKO_HISTORY should have
#define MIN_DATA_COUNT 5
// The value MIN_DATA_COUNT used is doubled each history until it exceeds:
#define MAX_MIN_DATA_COUNT 100

static struct timeval history_sec = { HISTORY_SEC, 0 };

// Store the last INFO_HISTORY data sets
// [0] = current data, not yet ready to be included as an estimate
// Each new data set throws the last old set off the end thus
// keeping a ongoing average of recent data
#define INFO_HISTORY 10

struct GEKKO_HISTORY {
	struct timeval finish;
	double sumXiTi;
	double sumXi;
	double sumTi;
	double sumXi2;
	uint32_t values;
	uint32_t hash_count_min;
	uint32_t hash_count_max;
};

enum timing_mode { MODE_DEFAULT, MODE_SHORT, MODE_LONG, MODE_VALUE };

static const char *MODE_DEFAULT_STR = "default";
static const char *MODE_SHORT_STR = "short";
static const char *MODE_SHORT_STREQ = "short=";
static const char *MODE_LONG_STR = "long";
static const char *MODE_LONG_STREQ = "long=";
static const char *MODE_VALUE_STR = "value";
static const char *MODE_UNKNOWN_STR = "unknown";

#define MAX_DEVICE_NUM 100
#define MAX_WORK_BUFFER_SIZE 2
#define MAX_CHIP_NUM 24
// Set it to 3, 5 or 9
#define	NONCE_CORRECTION_TIMES	5
#define MAX_TRIES	4
#define	RM_CMD_MASK		0x0F
#define	RM_STATUS_MASK		0xF0
#define	RM_CHIP_MASK		0x3F
#define	RM_PRODUCT_MASK		0xC0
#define	RM_PRODUCT_RBOX		0x00
#define	RM_PRODUCT_T1		0x40
#define	RM_PRODUCT_T2		0x80
#define	RM_PRODUCT_TEST		0xC0

#if (NONCE_CORRECTION_TIMES == 5)
static int32_t rbox_corr_values[] = {0, 1, -1, -2, -4};
#endif
#if (NONCE_CORRECTION_TIMES == 9)
static int32_t rbox_corr_values[] = {0, 1, -1, 2, -2, 3, -3, 4, -4};
#endif
#if (NONCE_CORRECTION_TIMES == 3)
static int32_t rbox_corr_values[] = {0, 1, -1};
#endif

#define ANT_QUEUE_NUM 36

typedef enum {
	NONCE_DATA1_OFFSET = 0,
	NONCE_DATA2_OFFSET,
	NONCE_DATA3_OFFSET,
	NONCE_DATA4_OFFSET,
	NONCE_TASK_CMD_OFFSET,
	NONCE_CHIP_NO_OFFSET,
	NONCE_TASK_NO_OFFSET,
	NONCE_COMMAND_OFFSET,
	NONCE_MAX_OFFSET
} NONCE_OFFSET;

typedef enum {
	NONCE_DATA_CMD = 0,
	NONCE_TASK_COMPLETE_CMD,
	NONCE_GET_TASK_CMD,
} NONCE_COMMAND;

typedef struct nonce_data {
	int chip_no;
	unsigned int task_no ;
	unsigned char work_state;
	int cmd_value;
} NONCE_DATA;

struct GEKKO_INFO {
	enum sub_ident ident;
	int intinfo;

	// time to calculate the golden_ob
	uint64_t golden_hashes;
	struct timeval golden_tv;

	struct GEKKO_HISTORY history[INFO_HISTORY+1];
	uint32_t min_data_count;

	int timeout;

	// seconds per Hash
	double Hs;
	// ms til we abort
	int read_time;
	// ms limit for (short=/long=) read_time
	int read_time_limit;
	// How long without hashes is considered a failed device
	int fail_time;

	enum timing_mode timing_mode;
	bool do_gekko_timing;

	double fullnonce;
	int count;
	double W;
	uint32_t values;
	uint64_t hash_count_range;

	// Determine the cost of history processing
	// (which will only affect W)
	uint64_t history_count;
	struct timeval history_time;

	// gekko-options
	int baud;
	int work_division;
	int fpga_count;
	uint32_t nonce_mask;

	uint8_t cmr2_speed;
	bool speed_next_work;
	bool flash_next_work;

	int nonce_size;

	bool failing;

	pthread_mutex_t lock;

	struct work *base_work; // For when we roll work
	struct work *g_work[MAX_CHIP_NUM][MAX_WORK_BUFFER_SIZE];
	uint32_t last_nonce[MAX_CHIP_NUM][MAX_WORK_BUFFER_SIZE];
	uint64_t nonces_checked;
	uint64_t nonces_correction_times;
	uint64_t nonces_correction_tests;
	uint64_t nonces_fail;
	uint64_t nonces_correction[NONCE_CORRECTION_TIMES];

	struct work **antworks;
	int nonces;
	int workid;
	bool ant;
	bool u3;
};

#define GEKKO_MIDSTATE_SIZE 32
#define GEKKO_UNUSED_SIZE 16
#define GEKKO_WORK_SIZE 12

#define GEKKO_WORK_DATA_OFFSET 64

#define ANT_UNUSED_SIZE 15

struct GEKKO_WORK {
	uint8_t midstate[GEKKO_MIDSTATE_SIZE];
	// These 4 bytes are for CMR2 bitstreams that handle MHz adjustment
	uint8_t check;
	uint8_t data;
	uint8_t cmd;
	uint8_t prefix;
	uint8_t unused[ANT_UNUSED_SIZE];
	uint8_t id; // Used only by ANT, otherwise unused by other gekko
	uint8_t work[GEKKO_WORK_SIZE];
};

#define ANT_U3_DEFFREQ 100
#define ANT_U3_MAXFREQ 500
struct {
	float freq;
	uint16_t hex;
} compacfreqtable[] = {
	{ 100,		0x0783 },
	{ 106.25,	0x0803 },
	{ 112.5,	0x0883 },
	{ 118.75,	0x0903 },
	{ 125,		0x0983 },
	{ 131.25,	0x0a03 },
	{ 137.5,	0x0a83 },
	{ 143.75,	0x1687 },
	{ 150,		0x0b83 },
	{ 156.25,	0x0c03 },
	{ 162.5,	0x0c83 },
	{ 168.75,	0x1a87 },
	{ 175,		0x0d83 },
	{ 181.25,	0x0e83 },
	{ 193.75,	0x0f03 },
	{ 196.88,	0x1f07 },
	{ 200,		0x0782 },
	{ 206.25,	0x1006 },
	{ 212.5,	0x1086 },
	{ 218.75,	0x1106 },
	{ 225,		0x0882 },
	{ 231.25,	0x1206 },
	{ 237.5,	0x1286 },
	{ 243.75,	0x1306 },
	{ 250,		0x0982 },
	{ 256.25,	0x1406 },
	{ 262.5,	0x0a02 },
	{ 268.75,	0x1506 },
	{ 275,		0x0a82 },
	{ 281.25,	0x1606 },
	{ 287.5,	0x0b02 },
	{ 293.75,	0x1706 },
	{ 300,		0x0b82 },
	{ 306.25,	0x1806 },
	{ 312.5,	0x0c02 },
	{ 318.75,	0x1906 },
	{ 325,		0x0c82 },
	{ 331.25,	0x1a06 },
	{ 337.5,	0x0d02 },
	{ 343.75,	0x1b06 },
	{ 350,		0x0d82 },
	{ 356.25,	0x1c06 },
	{ 362.5,	0x0e02 },
	{ 368.75,	0x1d06 },
	{ 375,		0x0e82 },
	{ 381.25,	0x1e06 },
	{ 387.5,	0x0f02 },
	{ 393.75,	0x1f06 },
	{ 400,		0x0f82 },
	{ 412.5,	0x1006 },
	{ 425,		0x0801 },
	{ 437.5,	0x1105 },
	{ 450,		0x0881 },
	{ 462.5,	0x1205 },
	{ 475,		0x0901 },
	{ 487.5,	0x1305 },
	{ 500,		0x0981 },
};





#define END_CONDITION 0x0000ffff


int *GEKKO_RAMP_DONE;
int *GEKKO_CLK_INDEX;
int GEKKO_FINAL_CLK;
int GEKKO_CLK_MALLOC=0;
// Looking for options in --gekko-timing and --gekko-options:
//
// Code increments this each time we start to look at a device
// However, this means that if other devices are checked by
// the Gekko code (e.g. Avalon only as at 20130517)
// they will count in the option offset
//
// This, however, is deterministic so that's OK
//
// If we were to increment after successfully finding an Gekko
// that would be random since an Gekko may fail and thus we'd
// not be able to predict the option order
//
// Devices are checked in the order libusb finds them which is ?
//
static int option_offset = -1;

/*
#define ICA_BUFSIZ (0x200)

static void transfer_read(struct cgpu_info *gekko, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, char *buf, int bufsiz, int *amount, enum usb_cmds cmd)
{
	int err;

	err = usb_transfer_read(gekko, request_type, bRequest, wValue, wIndex, buf, bufsiz, amount, cmd);

	applog(LOG_DEBUG, "%s: cgid %d %s got err %d",
			gekko->drv->name, gekko->cgminer_id,
			usb_cmdname(cmd), err);
}
*/

static void _transfer(struct cgpu_info *gekko, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint32_t *data, int siz, enum usb_cmds cmd)
{
	int err;

	err = usb_transfer_data(gekko, request_type, bRequest, wValue, wIndex, data, siz, cmd);

	applog(LOG_DEBUG, "%s: cgid %d %s got err %d",
			gekko->drv->name, gekko->cgminer_id,
			usb_cmdname(cmd), err);
}

#define transfer(gekko, request_type, bRequest, wValue, wIndex, cmd) \
		_transfer(gekko, request_type, bRequest, wValue, wIndex, NULL, 0, cmd)

static void gekko_initialise(struct cgpu_info *gekko, int baud)
{
	struct GEKKO_INFO *info = (struct GEKKO_INFO *)(gekko->device_data);
	uint16_t wValue, wIndex;
	enum sub_ident ident;
	int interface;

	if (gekko->usbinfo.nodev)
		return;

	interface = _usb_interface(gekko, info->intinfo);
	ident = usb_ident(gekko);

	switch (ident) {
		case IDENT_GEK:
		case IDENT_GEK1:
			// Enable the UART
			transfer(gekko, CP210X_TYPE_OUT, CP210X_REQUEST_IFC_ENABLE,
				 CP210X_VALUE_UART_ENABLE,
				 interface, C_ENABLE_UART);

			if (gekko->usbinfo.nodev)
			// Set data control
			transfer(gekko, CP210X_TYPE_OUT, CP210X_REQUEST_DATA, CP210X_VALUE_DATA,
				 interface, C_SETDATA);

			if (gekko->usbinfo.nodev)
				return;

			// Set the baud
			uint32_t data = CP210X_DATA_BAUD;
			_transfer(gekko, CP210X_TYPE_OUT, CP210X_REQUEST_BAUD, 0,
				 interface, &data, sizeof(data), C_SETBAUD);
			break;
		default:
			quit(1, "gekko_intialise() called with invalid %s cgid %i ident=%d",
				gekko->drv->name, gekko->cgminer_id, ident);
	}
}

static void rev(unsigned char *s, size_t l)
{
	size_t i, j;
	unsigned char t;

	for (i = 0, j = l - 1; i < j; i++, j--) {
		t = s[i];
		s[i] = s[j];
		s[j] = t;
	}
}

#define ICA_NONCE_ERROR -1
#define ICA_NONCE_OK 0
#define ICA_NONCE_RESTART 1
#define ICA_NONCE_TIMEOUT 2

static int gekko_get_nonce(struct cgpu_info *gekko, unsigned char *buf, struct timeval *tv_start,
			    struct timeval *tv_finish, struct thr_info *thr, int read_time)
{
	struct GEKKO_INFO *info = (struct GEKKO_INFO *)(gekko->device_data);
	int err, amt, rc;

	if (gekko->usbinfo.nodev)
		return ICA_NONCE_ERROR;

	cgtime(tv_start);
	err = usb_read_ii_timeout_cancellable(gekko, info->intinfo, (char *)buf,
					      info->nonce_size, &amt, read_time,
					      C_GETRESULTS);
	cgtime(tv_finish);

	if (err < 0 && err != LIBUSB_ERROR_TIMEOUT) {
		applog(LOG_ERR, "%s %i: Comms error (rerr=%d amt=%d)", gekko->drv->name,
		       gekko->device_id, err, amt);
		dev_error(gekko, REASON_DEV_COMMS_ERROR);
		return ICA_NONCE_ERROR;
	}

	if (amt >= info->nonce_size)
		return ICA_NONCE_OK;

	rc = SECTOMS(tdiff(tv_finish, tv_start));
	if (thr && thr->work_restart) {
		applog(LOG_DEBUG, "Gekko Read: Work restart at %d ms", rc);
		return ICA_NONCE_RESTART;
	}

	if (amt > 0)
		applog(LOG_DEBUG, "Gekko Read: Timeout reading for %d ms", rc);
	else
		applog(LOG_DEBUG, "Gekko Read: No data for %d ms", rc);
	return ICA_NONCE_TIMEOUT;
}


static const char *timing_mode_str(enum timing_mode timing_mode)
{
	switch(timing_mode) {
	case MODE_DEFAULT:
		return MODE_DEFAULT_STR;
	case MODE_SHORT:
		return MODE_SHORT_STR;
	case MODE_LONG:
		return MODE_LONG_STR;
	case MODE_VALUE:
		return MODE_VALUE_STR;
	default:
		return MODE_UNKNOWN_STR;
	}
}

static void set_timing_mode(int this_option_offset, struct cgpu_info *gekko)
{
	struct GEKKO_INFO *info = (struct GEKKO_INFO *)(gekko->device_data);
	int read_count_timing = 0;
	enum sub_ident ident;
	double Hs, fail_time;
	char buf[BUFSIZ+1];
	char *ptr, *comma, *eq;
	size_t max;
	int i;

	if (opt_gekko_timing == NULL)
		buf[0] = '\0';
	else {
		ptr = opt_gekko_timing;
		for (i = 0; i < this_option_offset; i++) {
			comma = strchr(ptr, ',');
			if (comma == NULL)
				break;
			ptr = comma + 1;
		}

		comma = strchr(ptr, ',');
		if (comma == NULL)
			max = strlen(ptr);
		else
			max = comma - ptr;

		if (max > BUFSIZ)
			max = BUFSIZ;
		strncpy(buf, ptr, max);
		buf[max] = '\0';
	}

	ident = usb_ident(gekko);
	switch (ident) {
		case IDENT_GEK:
		case IDENT_GEK1:
			info->Hs = ANTU3_HASH_TIME;
			read_count_timing = ANTU3_READ_COUNT_TIMING;
			break;
		default:
			quit(1, "Gekko get_options() called with invalid %s ident=%d",
				gekko->drv->name, ident);
	}

	info->read_time = 0;
	info->read_time_limit = 0; // 0 = no limit

	if (strcasecmp(buf, MODE_SHORT_STR) == 0) {
		// short
		info->read_time = read_count_timing;

		info->timing_mode = MODE_SHORT;
		info->do_gekko_timing = true;
	} else if (strncasecmp(buf, MODE_SHORT_STREQ, strlen(MODE_SHORT_STREQ)) == 0) {
		// short=limit
		info->read_time = read_count_timing;

		info->timing_mode = MODE_SHORT;
		info->do_gekko_timing = true;

		info->read_time_limit = atoi(&buf[strlen(MODE_SHORT_STREQ)]);
		if (info->read_time_limit < 0)
			info->read_time_limit = 0;
		if (info->read_time_limit > GEKKO_READ_TIME_LIMIT_MAX)
			info->read_time_limit = GEKKO_READ_TIME_LIMIT_MAX;
	} else if (strcasecmp(buf, MODE_LONG_STR) == 0) {
		// long
		info->read_time = read_count_timing;

		info->timing_mode = MODE_LONG;
		info->do_gekko_timing = true;
	} else if (strncasecmp(buf, MODE_LONG_STREQ, strlen(MODE_LONG_STREQ)) == 0) {
		// long=limit
		info->read_time = read_count_timing;

		info->timing_mode = MODE_LONG;
		info->do_gekko_timing = true;

		info->read_time_limit = atoi(&buf[strlen(MODE_LONG_STREQ)]);
		if (info->read_time_limit < 0)
			info->read_time_limit = 0;
		if (info->read_time_limit > GEKKO_READ_TIME_LIMIT_MAX)
			info->read_time_limit = GEKKO_READ_TIME_LIMIT_MAX;
	} else if ((Hs = atof(buf)) != 0) {
		// ns[=read_time]
		info->Hs = Hs / NANOSEC;
		info->fullnonce = info->Hs * (((double)0xffffffff) + 1);

		if ((eq = strchr(buf, '=')) != NULL)
			info->read_time = atoi(eq+1) * GEKKO_WAIT_TIMEOUT;

		if (info->read_time < GEKKO_READ_COUNT_MIN)
			info->read_time = SECTOMS(info->fullnonce) - GEKKO_READ_REDUCE;

		if (unlikely(info->read_time < GEKKO_READ_COUNT_MIN))
			info->read_time = GEKKO_READ_COUNT_MIN;

		info->timing_mode = MODE_VALUE;
		info->do_gekko_timing = false;
	} else {
		// Anything else in buf just uses DEFAULT mode

		info->fullnonce = info->Hs * (((double)0xffffffff) + 1);

		if ((eq = strchr(buf, '=')) != NULL)
			info->read_time = atoi(eq+1) * GEKKO_WAIT_TIMEOUT;

		if (info->read_time < GEKKO_READ_COUNT_MIN)
			info->read_time = SECTOMS(info->fullnonce) - GEKKO_READ_REDUCE;

		if (unlikely(info->read_time < GEKKO_READ_COUNT_MIN))
			info->read_time = GEKKO_READ_COUNT_MIN;

		info->timing_mode = MODE_DEFAULT;
		info->do_gekko_timing = false;
	}

	info->min_data_count = MIN_DATA_COUNT;

	// All values are in multiples of GEKKO_WAIT_TIMEOUT
	info->read_time_limit *= GEKKO_WAIT_TIMEOUT;

	applog(LOG_DEBUG, "%s: cgid %d Init: mode=%s read_time=%dms limit=%dms Hs=%e",
			gekko->drv->name, gekko->cgminer_id,
			timing_mode_str(info->timing_mode),
			info->read_time, info->read_time_limit, info->Hs);

	/* Set the time to detect a dead device to 30 full nonce ranges. */
	//fail_time = info->Hs * 0xffffffffull * 30.0;
	/* Integer accuracy is definitely enough. */
	//info->fail_time = fail_time + 1;
	info->fail_time=10;
}

static uint32_t mask(int work_division)
{
	uint32_t nonce_mask = 0x7fffffff;

	// yes we can calculate these, but this way it's easy to see what they are
	switch (work_division) {
	case 1:
		nonce_mask = 0xffffffff;
		break;
	case 2:
		nonce_mask = 0x7fffffff;
		break;
	case 4:
		nonce_mask = 0x3fffffff;
		break;
	case 8:
		nonce_mask = 0x1fffffff;
		break;
	default:
		quit(1, "Invalid2 gekko-options for work_division (%d) must be 1, 2, 4 or 8", work_division);
	}

	return nonce_mask;
}

static void get_options(int this_option_offset, struct cgpu_info *gekko, int *baud, int *work_division, int *fpga_count)
{
	char buf[BUFSIZ+1];
	char *ptr, *comma, *colon, *colon2;
	enum sub_ident ident;
	size_t max;
	int i, tmp;

	if (opt_gekko_options == NULL)
		buf[0] = '\0';
	else {
		ptr = opt_gekko_options;
		for (i = 0; i < this_option_offset; i++) {
			comma = strchr(ptr, ',');
			if (comma == NULL)
				break;
			ptr = comma + 1;
		}

		comma = strchr(ptr, ',');
		if (comma == NULL)
			max = strlen(ptr);
		else
			max = comma - ptr;

		if (max > BUFSIZ)
			max = BUFSIZ;
		strncpy(buf, ptr, max);
		buf[max] = '\0';
	}

	ident = usb_ident(gekko);
	switch (ident) {
		case IDENT_GEK:
		case IDENT_GEK1:
			*baud = GEKKO_IO_SPEED;
			*work_division = 1;
			*fpga_count = 1;
			break;
		default:
			quit(1, "Gekko get_options() called with invalid %s ident=%d",
				gekko->drv->name, ident);
	}

	if (*buf) {
		colon = strchr(buf, ':');
		if (colon)
			*(colon++) = '\0';

		if (*buf) {
			tmp = atoi(buf);
			switch (tmp) {
			case 115200:
				*baud = 115200;
				break;
			case 57600:
				*baud = 57600;
				break;
			default:
				quit(1, "Invalid gekko-options for baud (%s) must be 115200 or 57600", buf);
			}
		}

		if (colon && *colon) {
			colon2 = strchr(colon, ':');
			if (colon2)
				*(colon2++) = '\0';

			if (*colon) {
				tmp = atoi(colon);
				if (tmp == 1 || tmp == 2 || tmp == 4 || tmp == 8) {
					*work_division = tmp;
					*fpga_count = tmp;	// default to the same
				} else {
					quit(1, "Invalid gekko-options for work_division (%s) must be 1, 2, 4 or 8", colon);
				}
			}

			if (colon2 && *colon2) {
				tmp = atoi(colon2);
				if (tmp > 0 && tmp <= *work_division)
					*fpga_count = tmp;
				else {
					quit(1, "Invalid gekko-options for fpga_count (%s) must be >0 and <=work_division (%d)", colon2, *work_division);
				}
			}
		}
	}
}

unsigned char crc5gek(unsigned char *ptr, unsigned char len)
{
	unsigned char i, j, k;
	unsigned char crc = 0x1f;

	unsigned char crcin[5] = {1, 1, 1, 1, 1};
	unsigned char crcout[5] = {1, 1, 1, 1, 1};
	unsigned char din = 0;

	j = 0x80;
	k = 0;
	for (i = 0; i < len; i++) {
		if (*ptr & j)
			din = 1;
		else
			din = 0;
		crcout[0] = crcin[4] ^ din;
		crcout[1] = crcin[0];
		crcout[2] = crcin[1] ^ crcin[4] ^ din;
		crcout[3] = crcin[2];
		crcout[4] = crcin[3];

		j = j >> 1;
		k++;
		if (k == 8) {
			j = 0x80;
			k = 0;
			ptr++;
		}
		memcpy(crcin, crcout, 5);
	}
	crc = 0;
	if (crcin[4])
		crc |= 0x10;
	if (crcin[3])
		crc |= 0x08;
	if (crcin[2])
		crc |= 0x04;
	if (crcin[1])
		crc |= 0x02;
	if (crcin[0])
		crc |= 0x01;
	return crc;
}

static uint16_t anu_find_freqhex(void)
{
	float fout, best_fout = opt_compac_freq;
	int od, nf, nr, no, n, m, bs;
	uint16_t anu_freq_hex = 0;
	float best_diff = 1000;

	if (!best_fout)
		best_fout = ANT_U3_DEFFREQ;

	for (od = 0; od < 4; od++) {
		no = 1 << od;
		for (n = 0; n < 16; n++) {
			nr = n + 1;
			for (m = 0; m < 64; m++) {
				nf = m + 1;
				fout = 25 * (float)nf /((float)(nr) * (float)(no));
				if (fabsf(fout - opt_compac_freq)  > best_diff)
					continue;
				if (500 <= (fout * no) && (fout * no) <= 1000)
					bs = 1;
				else
					bs = 0;
				best_diff = fabsf(fout - opt_compac_freq);
				best_fout = fout;
				anu_freq_hex = (bs << 14) | (m << 7) | (n << 2) | od;
				if (fout == opt_compac_freq) {
					applog(LOG_DEBUG, "ANU found exact frequency %.1f with hex %04x",
					       opt_compac_freq, anu_freq_hex);
					goto out;
				}
			}
		}
	}
	applog(LOG_NOTICE, "ANU found nearest frequency %.1f with hex %04x", best_fout,
	       anu_freq_hex);
out:
	return anu_freq_hex;
}

static uint16_t anu3_find_freqhex(int freq2set)
{
	int i = 0, freq = freq2set, u3freq;
	uint16_t anu_freq_hex = 0x0882;

	if (!freq)
		freq = ANT_U3_DEFFREQ;

	do {
		u3freq = compacfreqtable[i].freq;
		if (u3freq <= freq)
			anu_freq_hex = compacfreqtable[i].hex;
		i++;
	} while (u3freq < ANT_U3_MAXFREQ);

	return anu_freq_hex;
}

static bool set_anu_freq(struct cgpu_info *gekko, struct GEKKO_INFO *info, uint16_t anu_freq_hex)
{
	unsigned char cmd_buf[4], rdreg_buf[4];
	int amount, err;
	char buf[512];

	if (!anu_freq_hex)
		anu_freq_hex = anu_find_freqhex();
	memset(cmd_buf, 0, 4);
	memset(rdreg_buf, 0, 4);
	cmd_buf[0] = 2 | 0x80;
	cmd_buf[1] = (anu_freq_hex & 0xff00u) >> 8;
	cmd_buf[2] = (anu_freq_hex & 0x00ffu);
	cmd_buf[3] = crc5gek(cmd_buf, 27);

	rdreg_buf[0] = 4 | 0x80;
	rdreg_buf[1] = 0;	//16-23
	rdreg_buf[2] = 0x04;	//8-15
	rdreg_buf[3] = crc5gek(rdreg_buf, 27);

	applog(LOG_DEBUG, "%s %i: Send frequency %02x%02x%02x%02x", gekko->drv->name, gekko->device_id,
	       cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3]);
	err = usb_write_ii(gekko, info->intinfo, (char *)cmd_buf, 4, &amount, C_ANU_SEND_CMD);
	if (err != LIBUSB_SUCCESS || amount != 4) {
		applog(LOG_ERR, "%s %i: Write freq Comms error (werr=%d amount=%d)",
		       gekko->drv->name, gekko->device_id, err, amount);
		return false;
	}
	err = usb_read_ii_timeout(gekko, info->intinfo, buf, 512, &amount, 100, C_GETRESULTS);
	if (err < 0 && err != LIBUSB_ERROR_TIMEOUT) {
		applog(LOG_ERR, "%s %i: Read freq Comms error (rerr=%d amount=%d)",
		       gekko->drv->name, gekko->device_id, err, amount);
		return false;
	}

	applog(LOG_DEBUG, "%s %i: Send freq getstatus %02x%02x%02x%02x", gekko->drv->name, gekko->device_id,
	       rdreg_buf[0], rdreg_buf[1], rdreg_buf[2], rdreg_buf[3]);
	err = usb_write_ii(gekko, info->intinfo, (char *)cmd_buf, 4, &amount, C_ANU_SEND_RDREG);
	if (err != LIBUSB_SUCCESS || amount != 4) {
		applog(LOG_ERR, "%s %i: Write freq Comms error (werr=%d amount=%d)",
		       gekko->drv->name, gekko->device_id, err, amount);
		return false;
	}
	err = usb_read_ii_timeout(gekko, info->intinfo, buf, 512, &amount, 100, C_GETRESULTS);
	if (err < 0 && err != LIBUSB_ERROR_TIMEOUT) {
		applog(LOG_ERR, "%s %i: Read freq Comms error (rerr=%d amount=%d)",
		       gekko->drv->name, gekko->device_id, err, amount);
		return false;
	}

	return true;
}

static void gekko_clear(struct cgpu_info *gekko, struct GEKKO_INFO *info)
{
	char buf[512];
	int amt;

	do {
		usb_read_ii_timeout(gekko, info->intinfo, buf, 512, &amt, 100, C_GETRESULTS);
	} while (amt > 0);
}

static struct cgpu_info *gekko_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	//applog(LOG_WARNING, "DETECT");
	int this_option_offset = ++option_offset;
	struct GEKKO_INFO *info;
	struct timeval tv_start, tv_finish;

	// Block 171874 nonce = (0xa2870100) = 0x000187a2
	// N.B. golden_ob MUST take less time to calculate
	//	than the timeout set in gekko_open()
	//	This one takes ~0.53ms on Rev3 Gekko
	const char golden_ob[] =
		"4679ba4ec99876bf4bfe086082b40025"
		"4df6c356451471139a3afa71e48f544a"
		"00000000000000000000000000000000"
		"0000000087320b1a1426674f2fa722ce";

	const char golden_nonce[] = "000187a2";
	const uint32_t golden_nonce_val = 0x000187a2;
	unsigned char nonce_bin[GEKKO_READ_SIZE];
	struct GEKKO_WORK workdata;
	char *nonce_hex;
	int baud, uninitialised_var(work_division), uninitialised_var(fpga_count);
	bool anu_freqset = false;
	struct cgpu_info *gekko;
	int ret, err, amount, tries, i;
	bool ok;
	bool cmr2_ok[CAIRNSMORE2_INTS];
	int cmr2_count;

	if ((sizeof(workdata) << 1) != (sizeof(golden_ob) - 1))
		quithere(1, "Data and golden_ob sizes don't match");

	gekko = usb_alloc_cgpu(&gekko_drv, 1);

	if (!usb_init(gekko, dev, found))
		goto shin;

	get_options(this_option_offset, gekko, &baud, &work_division, &fpga_count);

	hex2bin((void *)(&workdata), golden_ob, sizeof(workdata));

	info = cgcalloc(1, sizeof(struct GEKKO_INFO));
	gekko->device_data = (void *)info;

	info->ident = usb_ident(gekko);
	info->timeout = AU3_WAIT_TIMEOUT;
	info->nonce_size = GEKKO_READ_SIZE;
	info->u3 = true;
	info->ant = true;

retry:
	info->u3=true;	
	tries = 2;
	ok = false;
	while (!ok && tries-- > 0) {
		gekko_clear(gekko, info);
		gekko_initialise(gekko, baud);

			//GEKKO_RAMP_DONE[0] = 0;
			//GEKKO_CLK_INDEX[0] = 0;
			GEKKO_FINAL_CLK = opt_compac_freq;
			uint16_t anu_freq_hex = anu3_find_freqhex(ANT_U3_DEFFREQ);

			if (!set_anu_freq(gekko, info, anu_freq_hex)) {
				applog(LOG_WARNING, "%s %i: Failed to set frequency, too much overclock?",
				       gekko->drv->name, gekko->device_id);
				continue;
			}
			gekko->usbdev->ident = info->ident = IDENT_GEK;
			info->Hs = ANTU3_HASH_TIME;
			gekko->drv->name = "COMPAC";
			applog(LOG_DEBUG, "%s %i: Detected GekkoScience Compac", gekko->drv->name,
			       gekko->device_id);
		err = usb_write_ii(gekko, info->intinfo,
				   (char *)(&workdata), sizeof(workdata), &amount, C_SENDWORK);

		if (err != LIBUSB_SUCCESS || amount != sizeof(workdata))
			continue;

		memset(nonce_bin, 0, sizeof(nonce_bin));
		ret = gekko_get_nonce(gekko, nonce_bin, &tv_start, &tv_finish, NULL, 300);
		info->nonce_size=ANT_READ_SIZE;

	}

	/* We have a real Gekko! */

	if (!add_cgpu(gekko))
		goto unshin;

	update_usb_stats(gekko);

	applog(LOG_INFO, "%s %d: Found at %s",
		gekko->drv->name, gekko->device_id, gekko->device_path);

	applog(LOG_DEBUG, "%s %d: Init baud=%d work_division=%d fpga_count=%d",
		gekko->drv->name, gekko->device_id, baud, work_division, fpga_count);

	info->baud = baud;
	info->work_division = work_division;
	info->fpga_count = fpga_count;
	info->nonce_mask = mask(work_division);

	info->golden_hashes = (golden_nonce_val & info->nonce_mask) * fpga_count;
	timersub(&tv_finish, &tv_start, &(info->golden_tv));

	set_timing_mode(this_option_offset, gekko);
	
	return gekko;

unshin:

	usb_uninit(gekko);
	free(info);
	gekko->device_data = NULL;

shin:

	gekko = usb_free_cgpu(gekko);

	return NULL;
}


static void gekko_detect(bool __maybe_unused hotplug)
{
	usb_detect(&gekko_drv, gekko_detect_one);
}

static bool gekko_prepare(struct thr_info *thr)
{
	struct cgpu_info *gekko = thr->cgpu;
	struct GEKKO_INFO *info = (struct GEKKO_INFO *)(gekko->device_data);
	if(GEKKO_CLK_MALLOC==0)
	{
		GEKKO_CLK_INDEX=malloc(100*sizeof(int));
		GEKKO_RAMP_DONE=malloc(100*sizeof(int));
		GEKKO_CLK_MALLOC=1;
	}
	gekko->device_id=gekko->cgminer_id;
	char *tmp_str;
	tmp_str=malloc(50*sizeof(char));
	strncpy(tmp_str,gekko->unique_id,11);
	strncpy(gekko->unique_id,"\0\0\0\0\0\0\0\0\0\0\0",11);
	strncpy(gekko->unique_id,(tmp_str)+3*sizeof(char),8);
	GEKKO_RAMP_DONE[gekko->device_id] = 0;
	GEKKO_CLK_INDEX[gekko->device_id] = 0;

	//applog(LOG_WARNING, "PREPARE %i %i",gekko->cgminer_id,gekko->device_id);
	if (info->ant)
		info->antworks = cgcalloc(sizeof(struct work *), ANT_QUEUE_NUM);
	return true;
}

static void process_history(struct cgpu_info *gekko, struct GEKKO_INFO *info, uint32_t nonce,
			    uint64_t hash_count, struct timeval *elapsed, struct timeval *tv_start)
{
	struct GEKKO_HISTORY *history0, *history;
	struct timeval tv_history_start, tv_history_finish;
	int count;
	double Hs, W, fullnonce;
	int read_time, i;
	bool limited;
	uint32_t values;
	int64_t hash_count_range;
	double Ti, Xi;

	// Ignore possible end condition values ...
	// TODO: set limitations on calculated values depending on the device
	// to avoid crap values caused by CPU/Task Switching/Swapping/etc
	if ((nonce & info->nonce_mask) <= END_CONDITION ||
	    (nonce & info->nonce_mask) >= (info->nonce_mask & ~END_CONDITION))
		return;

	cgtime(&tv_history_start);

	history0 = &(info->history[0]);

	if (history0->values == 0)
		timeradd(tv_start, &history_sec, &(history0->finish));

	Ti = (double)(elapsed->tv_sec)
		+ ((double)(elapsed->tv_usec))/((double)1000000)
		- ((double)GEKKO_READ_TIME(info->baud));
	Xi = (double)hash_count;
	history0->sumXiTi += Xi * Ti;
	history0->sumXi += Xi;
	history0->sumTi += Ti;
	history0->sumXi2 += Xi * Xi;

	history0->values++;

	if (history0->hash_count_max < hash_count)
		history0->hash_count_max = hash_count;
	if (history0->hash_count_min > hash_count || history0->hash_count_min == 0)
		history0->hash_count_min = hash_count;

	if (history0->values >= info->min_data_count
	&&  timercmp(tv_start, &(history0->finish), >)) {
		for (i = INFO_HISTORY; i > 0; i--)
			memcpy(&(info->history[i]),
				&(info->history[i-1]),
				sizeof(struct GEKKO_HISTORY));

		// Initialise history0 to zero for summary calculation
		memset(history0, 0, sizeof(struct GEKKO_HISTORY));

		// We just completed a history data set
		// So now recalc read_time based on the whole history thus we will
		// initially get more accurate until it completes INFO_HISTORY
		// total data sets
		count = 0;
		for (i = 1 ; i <= INFO_HISTORY; i++) {
			history = &(info->history[i]);
			if (history->values >= MIN_DATA_COUNT) {
				count++;

				history0->sumXiTi += history->sumXiTi;
				history0->sumXi += history->sumXi;
				history0->sumTi += history->sumTi;
				history0->sumXi2 += history->sumXi2;
				history0->values += history->values;

				if (history0->hash_count_max < history->hash_count_max)
					history0->hash_count_max = history->hash_count_max;
				if (history0->hash_count_min > history->hash_count_min || history0->hash_count_min == 0)
					history0->hash_count_min = history->hash_count_min;
			}
		}

		// All history data
		Hs = (history0->values*history0->sumXiTi - history0->sumXi*history0->sumTi)
			/ (history0->values*history0->sumXi2 - history0->sumXi*history0->sumXi);
		W = history0->sumTi/history0->values - Hs*history0->sumXi/history0->values;
		hash_count_range = history0->hash_count_max - history0->hash_count_min;
		values = history0->values;

		// Initialise history0 to zero for next data set
		memset(history0, 0, sizeof(struct GEKKO_HISTORY));

		fullnonce = W + Hs * (((double)0xffffffff) + 1);
		read_time = SECTOMS(fullnonce) - GEKKO_READ_REDUCE;
		if (info->read_time_limit > 0 && read_time > info->read_time_limit) {
			read_time = info->read_time_limit;
			limited = true;
		} else
			limited = false;

		info->Hs = Hs;
		info->read_time = read_time;

		info->fullnonce = fullnonce;
		info->count = count;
		info->W = W;
		info->values = values;
		info->hash_count_range = hash_count_range;

		if (info->min_data_count < MAX_MIN_DATA_COUNT)
			info->min_data_count *= 2;
		else if (info->timing_mode == MODE_SHORT)
			info->do_gekko_timing = false;

		applog(LOG_WARNING, "%s %d Re-estimate: Hs=%e W=%e read_time=%dms%s fullnonce=%.3fs",
				gekko->drv->name, gekko->device_id, Hs, W, read_time,
				limited ? " (limited)" : "", fullnonce);
	}
	info->history_count++;
	cgtime(&tv_history_finish);

	timersub(&tv_history_finish, &tv_history_start, &tv_history_finish);
	timeradd(&tv_history_finish, &(info->history_time), &(info->history_time));
}

static int64_t gekko_scanwork(struct thr_info *thr)
{
	struct cgpu_info *gekko = thr->cgpu;
	struct GEKKO_INFO *info = (struct GEKKO_INFO *)(gekko->device_data);
	int ret, err, amount;
	unsigned char nonce_bin[GEKKO_BUF_SIZE];
	struct GEKKO_WORK workdata;
	char *ob_hex;
	uint32_t nonce;
	int64_t hash_count = 0;
	struct timeval tv_start, tv_finish, elapsed;
	int curr_hw_errors;
	bool was_hw_error;
	struct work *work;
	int64_t estimate_hashes;
	uint8_t workid = 0;

		//gekko->device_id=gekko->cgminer_id;
	if(GEKKO_RAMP_DONE[gekko->device_id] == 0)
	{
		int compac_freq;
		
		compac_freq = compacfreqtable[GEKKO_CLK_INDEX[gekko->device_id]].freq;

		uint16_t anu_freq_hex = anu3_find_freqhex(compac_freq);

		if (!set_anu_freq(gekko, info, anu_freq_hex)) {
			applog(LOG_WARNING, "%s %i: Failed to set frequency, too much overclock?",
			       gekko->drv->name, gekko->device_id);
		}
		applog(LOG_DEBUG, "novak: SETTING FREQ ON DEVICE %i (%s), FREQ NOW %d, ramping to %d", gekko->device_id,gekko->unique_id, compac_freq, GEKKO_FINAL_CLK);
		GEKKO_CLK_INDEX[gekko->device_id]++;
		if(compac_freq >= GEKKO_FINAL_CLK)
			GEKKO_RAMP_DONE[gekko->device_id] =1;
	}

	if (unlikely(share_work_tdiff(gekko) > info->fail_time)) {
		if (info->failing) {
			if (share_work_tdiff(gekko) > info->fail_time + 60) {
				applog(LOG_ERR, "%s %d: Device failed to respond to restart",
				       gekko->drv->name, gekko->device_id);
				usb_nodev(gekko);
				return -1;
			}
		} else {
			applog(LOG_WARNING, "%s %d: No valid hashes for over %d secs, attempting to reset",
			       gekko->drv->name, gekko->device_id, info->fail_time);
			usb_reset(gekko);
			info->failing = true;
		}
	}

	// Device is gone
	if (gekko->usbinfo.nodev)
		return -1;

	elapsed.tv_sec = elapsed.tv_usec = 0;

	work = get_work(thr, thr->id);
	memset((void *)(&workdata), 0, sizeof(workdata));
	memcpy(&(workdata.midstate), work->midstate, GEKKO_MIDSTATE_SIZE);
	memcpy(&(workdata.work), work->data + GEKKO_WORK_DATA_OFFSET, GEKKO_WORK_SIZE);
	rev((void *)(&(workdata.midstate)), GEKKO_MIDSTATE_SIZE);
	rev((void *)(&(workdata.work)), GEKKO_WORK_SIZE);
	if (info->ant) {
		workid = info->workid;
		if (++info->workid >= 0x1F)
			info->workid = 0;
		if (info->antworks[workid])
			free_work(info->antworks[workid]);
		info->antworks[workid] = work;
		workdata.id = workid;
	}


	// We only want results for the work we are about to send
	usb_buffer_clear(gekko);

	err = usb_write_ii(gekko, info->intinfo, (char *)(&workdata), sizeof(workdata), &amount, C_SENDWORK);
	if (err < 0 || amount != sizeof(workdata)) {
		applog(LOG_ERR, "%s %i: Comms error (werr=%d amt=%d)",
				gekko->drv->name, gekko->device_id, err, amount);
		dev_error(gekko, REASON_DEV_COMMS_ERROR);
		gekko_initialise(gekko, info->baud);
		goto out;
	}

	if (opt_debug) {
		ob_hex = bin2hex((void *)(&workdata), sizeof(workdata));
		applog(LOG_DEBUG, "%s %d: sent %s",
			gekko->drv->name, gekko->device_id, ob_hex);
		free(ob_hex);
	}
more_nonces:
	/* Gekko will return nonces or nothing. If we know we have enough data
	 * for a response in the buffer already, there will be no usb read
	 * performed. */
	memset(nonce_bin, 0, sizeof(nonce_bin));
	ret = gekko_get_nonce(gekko, nonce_bin, &tv_start, &tv_finish, thr, info->read_time);
	if (ret == ICA_NONCE_ERROR)
		goto out;

	// aborted before becoming idle, get new work
	if (ret == ICA_NONCE_TIMEOUT || ret == ICA_NONCE_RESTART) {
		if (info->ant)
			goto out;

		timersub(&tv_finish, &tv_start, &elapsed);

		// ONLY up to just when it aborted
		// We didn't read a reply so we don't subtract GEKKO_READ_TIME
		estimate_hashes = ((double)(elapsed.tv_sec)
					+ ((double)(elapsed.tv_usec))/((double)1000000)) / info->Hs;

		// If some Serial-USB delay allowed the full nonce range to
		// complete it can't have done more than a full nonce
		if (unlikely(estimate_hashes > 0xffffffff))
			estimate_hashes = 0xffffffff;

		applog(LOG_DEBUG, "%s %d: no nonce = 0x%08lX hashes (%ld.%06lds)",
				gekko->drv->name, gekko->device_id,
				(long unsigned int)estimate_hashes,
				(long)elapsed.tv_sec, (long)elapsed.tv_usec);

		hash_count = estimate_hashes;
		goto out;
	}

	if (info->ant) {
		workid = nonce_bin[4] & 0x1F;
		if (info->antworks[workid])
			work = info->antworks[workid];
		else
			goto out;
	}

	memcpy((char *)&nonce, nonce_bin, GEKKO_READ_SIZE);
	nonce = htobe32(nonce);
	curr_hw_errors = gekko->hw_errors;
	if (submit_nonce(thr, work, nonce))
		info->failing = false;
	was_hw_error = (curr_hw_errors < gekko->hw_errors);

	/* U3s return shares fast enough to use just that for hashrate
	 * calculation, otherwise the result is inaccurate instead. */
	if (info->ant) {
		info->nonces++;
		if (usb_buffer_size(gekko) >= ANT_READ_SIZE)
			goto more_nonces;
	} else {
		hash_count = (nonce & info->nonce_mask);
		hash_count++;
		hash_count *= info->fpga_count;
	}

#if 0
	// This appears to only return zero nonce values
	if (usb_buffer_size(gekko) > 3) {
		memcpy((char *)&nonce, gekko->usbdev->buffer, sizeof(nonce_bin));
		nonce = htobe32(nonce);
		applog(LOG_WARNING, "%s %d: attempting to submit 2nd nonce = 0x%08lX",
				gekko->drv->name, gekko->device_id,
				(long unsigned int)nonce);
		curr_hw_errors = gekko->hw_errors;
		submit_nonce(thr, work, nonce);
		was_hw_error = (curr_hw_errors > gekko->hw_errors);
	}
#endif

	if (opt_debug || info->do_gekko_timing)
		timersub(&tv_finish, &tv_start, &elapsed);

	applog(LOG_DEBUG, "%s %d: nonce = 0x%08x = 0x%08lX hashes (%ld.%06lds)",
			gekko->drv->name, gekko->device_id,
			nonce, (long unsigned int)hash_count,
			(long)elapsed.tv_sec, (long)elapsed.tv_usec);

	if (info->do_gekko_timing && !was_hw_error)
		process_history(gekko, info, nonce, hash_count, &elapsed, &tv_start);
out:
	if (!info->ant)
		free_work(work);
	else {
		/* Ant USBs free the work themselves. Return only one full
		 * nonce worth on each pass to smooth out displayed hashrate */
		if (info->nonces) {
			hash_count = 0xffffffff;
			info->nonces--;
		}
	}

	return hash_count;
}

static struct api_data *gekko_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct GEKKO_INFO *info = (struct GEKKO_INFO *)(cgpu->device_data);
	char data[4096];
	int i, off;
	size_t len;
	float avg;

	// Warning, access to these is not locked - but we don't really
	// care since hashing performance is way more important than
	// locking access to displaying API debug 'stats'
	// If locking becomes an issue for any of them, use copy_data=true also
	root = api_add_int(root, "read_time", &(info->read_time), false);
	root = api_add_int(root, "read_time_limit", &(info->read_time_limit), false);
	root = api_add_double(root, "fullnonce", &(info->fullnonce), false);
	root = api_add_int(root, "count", &(info->count), false);
	root = api_add_hs(root, "Hs", &(info->Hs), false);
	root = api_add_double(root, "W", &(info->W), false);
	root = api_add_uint(root, "total_values", &(info->values), false);
	root = api_add_uint64(root, "range", &(info->hash_count_range), false);
	root = api_add_uint64(root, "history_count", &(info->history_count), false);
	root = api_add_timeval(root, "history_time", &(info->history_time), false);
	root = api_add_uint(root, "min_data_count", &(info->min_data_count), false);
	root = api_add_uint(root, "timing_values", &(info->history[0].values), false);
	root = api_add_const(root, "timing_mode", timing_mode_str(info->timing_mode), false);
	root = api_add_bool(root, "is_timing", &(info->do_gekko_timing), false);
	root = api_add_int(root, "baud", &(info->baud), false);
	root = api_add_int(root, "work_division", &(info->work_division), false);
	root = api_add_int(root, "fpga_count", &(info->fpga_count), false);

	return root;
}

static void gekko_statline_before(char *buf, size_t bufsiz, struct cgpu_info *cgpu)
{
	struct GEKKO_INFO *info = (struct GEKKO_INFO *)(cgpu->device_data);
	tailsprintf(buf, bufsiz, "%3.0fMHz", opt_compac_freq);
}

static void gekko_shutdown(__maybe_unused struct thr_info *thr)
{
	// TODO: ?
}

static void gekko_identify(struct cgpu_info *cgpu)
{
	struct GEKKO_INFO *info = (struct GEKKO_INFO *)(cgpu->device_data);

}

static char *gekko_set(struct cgpu_info *cgpu, char *option, char *setting, char *replybuf)
{

	strcpy(replybuf, "no set options available");
	return replybuf;

}

struct device_drv gekko_drv = {
	.drv_id = DRIVER_gekko,
	.dname = "Gekko",
	.name = "GEK",
	.drv_detect = gekko_detect,
	.hash_work = &hash_driver_work,
	.get_api_stats = gekko_api_stats,
	.get_statline_before = gekko_statline_before,
	.set_device = gekko_set,
	.identify_device = gekko_identify,
	.thread_prepare = gekko_prepare,
	.scanwork = gekko_scanwork,
	.thread_shutdown = gekko_shutdown,
};
