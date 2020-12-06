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
#include "driver-gekko.h"
#include "crc.h"

#ifdef WIN32
#include <windows.h>
#endif

#include "compat.h"
#include <unistd.h>
#include "miner.h"
#include "usbutils.h"

// The serial I/O speed - Linux uses a define 'B115200' in bits/termios.h
//#define GEKKO_IO_SPEED 115200

static bool compac_prepare(struct thr_info *thr);

uint32_t bmcrc(unsigned char *ptr, uint32_t len)
{
	unsigned char c[5] = {1, 1, 1, 1, 1};
	uint32_t i, c1, ptr_idx = 0;

	for (i = 0; i < len; i++) {
		c1 = c[1];
		c[1] = c[0];
		c[0] = c[4] ^ ((ptr[ptr_idx] & (0x80 >> (i % 8))) ? 1 : 0);
		c[4] = c[3];
		c[3] = c[2];
		c[2] = c1 ^ c[0];

		if (((i + 1) % 8) == 0)
			ptr_idx++;
	}
	return (c[4] * 0x10) | (c[3] * 0x08) | (c[2] * 0x04) | (c[1] * 0x02) | (c[0] * 0x01);
}

void dumpbuffer(struct cgpu_info *compac, int LOG_LEVEL, char *note, unsigned char *ptr, uint32_t len)
{
	struct COMPAC_INFO *info = compac->device_data;
	if (opt_log_output || LOG_LEVEL <= opt_log_level) {
		char str[1024];
		const char * hex = "0123456789ABCDEF";
		char * pout = str;
		int i = 0;

		for(; i < 0xFF && i < len - 1; ++i){
			*pout++ = hex[(*ptr>>4)&0xF];
			*pout++ = hex[(*ptr++)&0xF];
			*pout++ = ':';
		}
		*pout++ = hex[(*ptr>>4)&0xF];
		*pout++ = hex[(*ptr)&0xF];
		*pout = 0;

		applog(LOG_LEVEL, "%s %i: %s: %s", compac->drv->name, compac->device_id, note, str);
	}
}

static int compac_micro_send(struct cgpu_info *compac, uint8_t cmd, uint8_t channel, uint8_t value)
{
	struct COMPAC_INFO *info = compac->device_data;
	int bytes = 1;
	int read_bytes = 1;
	int micro_temp;
	uint8_t temp;
	unsigned short usb_val;
	char null[255];

	// synchronous : safe to run in the listen thread.
	if (!info->micro_found) {
		return 0;
	}

	// Baud Rate : 500,000

	usb_val = (FTDI_BITMODE_CBUS << 8) | 0xF3; // low byte: bitmask - 1111 0011 - CB1(HI), CB0(HI)
	usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_BITMODE, usb_val, info->interface, C_SETMODEM);
	cgsleep_ms(2);
	//usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_BAUD, 0x06, (FTDI_INDEX_BAUD_BTS & 0xff00) | info->interface, C_SETBAUD);

	info->cmd[0] = cmd | channel;
	info->cmd[1] = value;

	if (value != 0x00 || cmd == M2_SET_VCORE) {
		bytes = 2;
	}

	usb_read_timeout(compac, (char *)info->rx, 255, &read_bytes, 1, C_GETRESULTS);

	dumpbuffer(compac, LOG_INFO, "(micro) TX", info->cmd, bytes);
	usb_write(compac, info->cmd, bytes, &read_bytes, C_REQUESTRESULTS);

	memset(info->rx, 0, info->rx_len);
	usb_read_timeout(compac, (char *)info->rx, 1, &read_bytes, 5, C_GETRESULTS);

	if (read_bytes > 0) {
		dumpbuffer(compac, LOG_INFO, "(micro) RX", info->rx, read_bytes);
		switch (cmd) {
			case 0x20:
				temp = info->rx[0];
				micro_temp = 32 + 1.8 * temp;
				if (micro_temp != info->micro_temp) {
					info->micro_temp = micro_temp;
					applog(LOG_WARNING, "%s %d: micro temp changed to %d°C / %.1f°F", compac->drv->name, compac->device_id, temp, info->micro_temp);
				}
				break;
			default:
				break;
		}
	}

	// Restore Baud Rate
	//usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_BAUD, (info->bauddiv + 1), (FTDI_INDEX_BAUD_BTS & 0xff00) | info->interface, C_SETBAUD);

	usb_val = (FTDI_BITMODE_CBUS << 8) | 0xF2; // low byte: bitmask - 1111 0010 - CB1(HI), CB0(LO)
	usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_BITMODE, usb_val, info->interface, C_SETMODEM);
	cgsleep_ms(2);

	return read_bytes;

}

static void compac_send(struct cgpu_info *compac, unsigned char *req_tx, uint32_t bytes, uint32_t crc_bits)
{
	struct COMPAC_INFO *info = compac->device_data;
	int read_bytes = 1;
	int read_wait = 0;
	int i;

	//leave original buffer intact
	for (i = 0; i < bytes; i++) {
		info->cmd[i] = req_tx[i];
	}
	info->cmd[bytes - 1] |= bmcrc(req_tx, crc_bits);

	cgsleep_ms(1);
	dumpbuffer(compac, LOG_INFO, "TX", info->cmd, bytes);
	usb_write(compac, info->cmd, bytes, &read_bytes, C_REQUESTRESULTS);
}

static void compac_send_chain_inactive(struct cgpu_info *compac)
{
	struct COMPAC_INFO *info = compac->device_data;
	int i;

	applog(LOG_INFO,"%s %d: sending chain inactive for %d chip(s)", compac->drv->name, compac->device_id, info->chips);
	if (info->asic_type == BM1387) {
		unsigned char buffer[5] = {0x55, 0x05, 0x00, 0x00, 0x00};
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);; // chain inactive
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);; // chain inactive
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);; // chain inactive
		for (i = 0; i < info->chips; i++) {
			buffer[0] = 0x41;
			buffer[1] = 0x05;
			buffer[2] = (0x100 / info->chips) * i;
			compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);;
		}
		unsigned char gateblk[9] = {0x58, 0x09, 0x00, 0x1C, 0x40, 0x20, 0x99, 0x80, 0x01};
		gateblk[6] = 0x80 | info->bauddiv;
		compac_send(compac, (char *)gateblk, sizeof(gateblk), 8 * sizeof(gateblk) - 8);; // chain inactive
	} else if (info->asic_type == BM1384) {
		unsigned char buffer[] = {0x85, 0x00, 0x00, 0x00};
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5); // chain inactive
		for (i = 0; i < info->chips; i++) {
			buffer[0] = 0x01;
			buffer[1] = (0x100 / info->chips) * i;
			compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
		}
		buffer[0] = 0x86; // GATEBLK
		buffer[1] = 0x00;
		buffer[2] = 0x9a; // 0x80 | 0x1a;
		//compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
	}

	if (info->mining_state != MINER_MINING) {
		applog(info->log_startup, "%s %d: open cores @ %.2fMHz", compac->drv->name, compac->device_id, info->frequency);
		info->zero_check = 0;
		info->task_hcn = 0;
		info->mining_state = MINER_OPEN_CORE;
	}
}

static void compac_set_frequency(struct cgpu_info *compac, float frequency)
{
	struct COMPAC_INFO *info = compac->device_data;
	uint32_t i, r, r1, r2, r3, p1, p2, pll;

	if (info->asic_type == BM1387) {
		unsigned char buffer[] = {0x58, 0x09, 0x00, 0x0C, 0x00, 0x50, 0x02, 0x41, 0x00};   //250MHz -- osc of 25MHz
		frequency = bound(frequency, 50, 900);
		frequency = ceil(100 * (frequency) / 625.0) * 6.25;

		if (frequency < 400) {
			buffer[7] = 0x41;
			buffer[5] = (frequency * 8) / 25;
		} else if (frequency < 600) {
			buffer[7] = 0x21;
			buffer[5] = (frequency * 4) / 25;
		} else {
			buffer[7] = 0x11;
			buffer[5] = (frequency * 2) / 25;
		}
		applog(LOG_WARNING, "%s %d: setting frequency to %.2fMHz", compac->drv->name, compac->device_id, frequency);
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);
		info->frequency = frequency;
	} else if (info->asic_type == BM1384) {
		unsigned char buffer[] = {0x82, 0x0b, 0x83, 0x00};

		frequency = bound(frequency, 6, 500);
		frequency = ceil(100 * (frequency) / 625.0) * 6.25;

		info->frequency = frequency;

		r = floor(log(info->frequency/25) / log(2));

		r1 = 0x0785 - r;
		r2 = 0x200 / pow(2, r);
		r3 = 25 * pow(2, r);

		p1 = r1 + r2 * (info->frequency - r3) / 6.25;
		p2 = p1 * 2 + (0x7f + r);

		pll = ((uint32_t)(info->frequency) % 25 == 0 ? p1 : p2);

		if (info->frequency < 100) {
			pll = 0x0783 - 0x80 * (100 - info->frequency) / 6.25;
		}

		buffer[1] = (pll >> 8) & 0xff;
		buffer[2] = (pll) & 0xff;

		applog(LOG_WARNING, "%s %d: setting frequency to %.2fMHz", compac->drv->name, compac->device_id, frequency);
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
		buffer[0] = 0x84;
		buffer[1] = 0x00;
		buffer[2] = 0x00;
//		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
		buffer[2] = 0x04;
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
	}

	info->hashrate = info->chips * info->frequency * info->cores * 1000000;
	info->fullscan_ms = 1000.0 * 0xffffffffull / info->hashrate;
	info->scanhash_ms = bound(info->fullscan_ms / 2, 1, 100);
	info->ticket_mask = bound(pow(2, ceil(log(info->hashrate / (2.0 * 0xffffffffull)) / log(2))) - 1, 0, 4000);
	info->ticket_mask = (info->asic_type == BM1387) ? 0 : info->ticket_mask;
	info->difficulty = info->ticket_mask + 1;

}

static uint64_t compac_check_nonce(struct cgpu_info *compac)
{
	struct COMPAC_INFO *info = compac->device_data;
	uint32_t nonce = (info->rx[3] << 0) | (info->rx[2] << 8) | (info->rx[1] << 16) | (info->rx[0] << 24);

	uint32_t hwe = compac->hw_errors;
	uint32_t job_id, i;
	uint64_t hashes = 0;
	struct timeval now;

	if (info->asic_type == BM1387) {
		job_id = info->rx[5] & 0xff;
	} else if (info->asic_type == BM1384) {
		job_id = info->rx[4] ^ 0x80;
	}

	if (job_id > info->max_job_id || (abs(info->job_id - job_id) > 3 && abs(info->max_job_id - job_id + info->job_id) > 3)) {
		return hashes;
	}

	if (!info->active_work[job_id] &&
		!(job_id > 0 && info->active_work[job_id - 1]) &&
		!(job_id > 1 && info->active_work[job_id - 2]) &&
		!(job_id > 2 && info->active_work[job_id - 3])) {
		return hashes;
	}

	cgtime(&now);

	info->nonces++;
	info->nonceless = 0;
	if (nonce == info->prev_nonce) {
		applog(LOG_INFO, "%s %d: Duplicate Nonce : %08x @ %02x [%02x %02x %02x %02x %02x %02x %02x]", compac->drv->name, compac->device_id, nonce, job_id,
			info->rx[0], info->rx[1], info->rx[2], info->rx[3], info->rx[4], info->rx[5], info->rx[6]);
		info->dups++;
		if (info->dups == 1) {
			info->mining_state = MINER_MINING_DUPS;
		}
		return hashes;
	} else {
		info->dups = 0;
	}

	hashes = info->difficulty * 0xffffffffull;
	info->prev_nonce = nonce;

	applog(LOG_INFO, "%s %d: Device reported nonce: %08x @ %02x", compac->drv->name, compac->device_id, nonce, job_id);

	struct work *work = info->work[job_id];
	bool active_work = info->active_work[job_id];

	if (info->vmask) {
		// force check last few nonces by [job_id - 1]
		if (info->asic_type == BM1387) {
			for (i = 0; i <= 3; i++) {
				if (job_id >= i) {
					if (info->active_work[job_id - i]) {
						work = info->work[job_id - i];
						active_work = info->active_work[job_id - i];
						if (active_work && work) {
							work->micro_job_id = pow(2, i);
							memcpy(work->data, &(work->pool->vmask_001[work->micro_job_id]), 4);
							if (test_nonce(work, nonce)) {
								applog(LOG_INFO, "%s %d: AsicBoost nonce found : midstate%d", compac->drv->name, compac->device_id, i);
								break;
							}
						}
					}
				}
			}
		}
	}

	if (!active_work || !work) {
		return hashes;
	}

	work->device_diff = info->difficulty;

	if (submit_nonce(info->thr, work, nonce)) {
		cgtime(&info->last_nonce);
		info->accepted++;
		info->failing = false;
	} else {
		if (hwe != compac->hw_errors) {
			cgtime(&info->last_hwerror);
		}
	}

	return hashes;
}

static void compac_update_work(struct cgpu_info *compac)
{
	struct COMPAC_INFO *info = compac->device_data;
	int i;

	for (i = 0; i < JOB_MAX; i++) {
		info->active_work[i] = false;
	}
	info->update_work = 1;
}

static void compac_flush_buffer(struct cgpu_info *compac)
{
	int read_bytes = 1;
	unsigned char resp[32];

	while (read_bytes) {
		usb_read_timeout(compac, (char *)resp, 32, &read_bytes, 1, C_REQUESTRESULTS);
	}
}

static void compac_flush_work(struct cgpu_info *compac)
{
	compac_flush_buffer(compac);
	compac_update_work(compac);
}

static void compac_toggle_reset(struct cgpu_info *compac)
{
	struct COMPAC_INFO *info = compac->device_data;
	unsigned short usb_val;

	applog(LOG_WARNING,"%s %d: Toggling ASIC nRST to reset", compac->drv->name, compac->device_id);

	usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_RESET, FTDI_VALUE_RESET, info->interface, C_RESET);
	usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_DATA, FTDI_VALUE_DATA_BTS, info->interface, C_SETDATA);
	usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_BAUD, FTDI_VALUE_BAUD_BTS, (FTDI_INDEX_BAUD_BTS & 0xff00) | info->interface, C_SETBAUD);
	usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_FLOW, FTDI_VALUE_FLOW, info->interface, C_SETFLOW);

	usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_RESET, FTDI_VALUE_PURGE_TX, info->interface, C_PURGETX);
	usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_RESET, FTDI_VALUE_PURGE_RX, info->interface, C_PURGERX);

	usb_val = (FTDI_BITMODE_CBUS << 8) | 0xF2; // low byte: bitmask - 1111 0010 - CB1(HI)
	usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_BITMODE, usb_val, info->interface, C_SETMODEM);
	cgsleep_ms(30);

	usb_val = (FTDI_BITMODE_CBUS << 8) | 0xF0; // low byte: bitmask - 1111 0000 - CB1(LO)
	usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_BITMODE, usb_val, info->interface, C_SETMODEM);
	cgsleep_ms(30);

	usb_val = (FTDI_BITMODE_CBUS << 8) | 0xF2; // low byte: bitmask - 1111 0010 - CB1(HI)
	usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_BITMODE, usb_val, info->interface, C_SETMODEM);
	cgsleep_ms(30);

	cgtime(&info->last_reset);
}

static void busy_work(struct COMPAC_INFO *info)
{
	memset(info->task, 0, info->task_len);

	if (info->asic_type == BM1387) {
		info->task[0] = 0x21;
		info->task[1] = info->task_len;
		info->task[2] = info->job_id & 0xff;
		info->task[3] = ((opt_gekko_boost) ? 0x04 : 0x01);
		memset(info->task + 8, 0xff, 12);

		unsigned short crc = crc16_false(info->task, info->task_len - 2);
		info->task[info->task_len - 2] = (crc >> 8) & 0xff;
		info->task[info->task_len - 1] = crc & 0xff;
	} else if (info->asic_type == BM1384) {
		if (info->mining_state == MINER_MINING) {
			info->task[39] = info->ticket_mask & 0xff;
			stuff_msb(info->task + 40, info->task_hcn);
		}
		info->task[51] = info->job_id & 0xff;
	}
}

static void init_task(struct COMPAC_INFO *info)
{
	struct work *work = info->work[info->job_id];

	memset(info->task, 0, info->task_len);

	if (info->asic_type == BM1387) {
		info->task[0] = 0x21;
		info->task[1] = info->task_len;
		info->task[2] = info->job_id & 0xff;
		info->task[3] = ((opt_gekko_boost) ? 0x04 : 0x01);

		if (info->mining_state == MINER_MINING) {
			stuff_reverse(info->task + 8, work->data + 64, 12);
			stuff_reverse(info->task + 20, work->midstate, 32);
			if (opt_gekko_boost) {
				stuff_reverse(info->task + 20 + 32, work->midstate1, 32);
				stuff_reverse(info->task + 20 + 32 + 32, work->midstate2, 32);
				stuff_reverse(info->task + 20 + 32 + 32 + 32, work->midstate3, 32);
			}
		} else {
			memset(info->task + 8, 0xff, 12);
		}
		unsigned short crc = crc16_false(info->task, info->task_len - 2);
		info->task[info->task_len - 2] = (crc >> 8) & 0xff;
		info->task[info->task_len - 1] = crc & 0xff;
	} else if (info->asic_type == BM1384) {
		if (info->mining_state == MINER_MINING) {
			stuff_reverse(info->task, work->midstate, 32);
			stuff_reverse(info->task + 52, work->data + 64, 12);
			info->task[39] = info->ticket_mask & 0xff;
			stuff_msb(info->task + 40, info->task_hcn);
		}
		info->task[51] = info->job_id & 0xff;
	}
}

static void *compac_mine(void *object)
{
	struct cgpu_info *compac = (struct cgpu_info *)object;
	struct COMPAC_INFO *info = compac->device_data;
	struct work *work = NULL;
	struct work *old_work = NULL;

	struct timeval now;
	struct sched_param param;
	int i, read_bytes, sleep_ms, policy, ret_nice;
	uint32_t err = 0;
	uint64_t hashes = 0;
	uint64_t max_task_wait = 0;
	float wait_factor = ((opt_gekko_boost && info->asic_type == BM1387) ? 1.8 : 0.6);

#ifndef WIN32
	ret_nice = nice(-15);
#else /* WIN32 */
	pthread_getschedparam(pthread_self(), &policy, &param);
	param.sched_priority = sched_get_priority_max(policy);
	pthread_setschedparam(pthread_self(), policy, &param);
	ret_nice = param.sched_priority;
#endif /* WIN32 */
	applog(LOG_INFO, "%s %d: work thread niceness (%d)", compac->drv->name, compac->device_id, ret_nice);

	max_task_wait = bound(wait_factor * info->fullscan_ms, 1, 3 * info->fullscan_ms);
	sleep_ms = bound(ceil(max_task_wait/8.0), 1, 200);

	while (info->mining_state != MINER_SHUTDOWN) {
		cgtime(&now);

		if (compac->deven == DEV_DISABLED || compac->usbinfo.nodev || info->mining_state != MINER_MINING) {
			cgsleep_ms(10);
		} else if (info->update_work || (ms_tdiff(&now, &info->last_task) > max_task_wait)) {

			info->update_work = 0;

			max_task_wait = bound(wait_factor * info->fullscan_ms, 1, 3 * info->fullscan_ms);
			sleep_ms = bound(ceil(max_task_wait/15.0), 1, 200);

			if (info->asic_type == BM1387 && ms_tdiff(&now, &info->monitor_time) > 30000) {
				int max_nononce = 3000.0 * (200.0 / info->frequency_requested);
				if (ms_tdiff(&now, &info->last_nonce) > max_nononce) {
					applog(LOG_WARNING,"%s %d: missing nonces", compac->drv->name, compac->device_id);
					info->mining_state = MINER_RESET;
					continue;
				}
			}
			if (ms_tdiff(&now, &info->last_frequency_ping) > 5000) {
				if (info->asic_type == BM1387) {
					unsigned char buffer[] = {0x54, 0x05, 0x00, 0x0C, 0x00};  // PLL_PARAMETER
					compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);
				} else if (info->asic_type == BM1384) {
					unsigned char buffer[] = {0x84, 0x00, 0x04, 0x00};
					compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
				}
				cgtime(&info->last_frequency_ping);

				if (info->asic_type == BM1384 || info->asic_type == BM1387) {
					uint64_t hashrate_5m, hashrate_1m;

					hashrate_1m = (double)compac->rolling1 * 1000000ull;
					hashrate_5m = (double)compac->rolling5 * 1000000ull;
					if ((hashrate_1m < (info->healthy * info->hashrate)) && ms_tdiff(&now, &info->monitor_time) > (3 * 60 * 1000)) {
						applog(LOG_WARNING, "%" PRIu64 " : %" PRIu64 " : %" PRIu64, hashrate_1m, hashrate_5m, info->hashrate);
						applog(LOG_WARNING,"%s %d: unhealthy miner", compac->drv->name, compac->device_id);
						info->mining_state = MINER_RESET;
						continue;
					}

					if (ms_tdiff(&now, &info->last_frequency_report) > (30 + 7500 * 3)) {
						applog(LOG_WARNING,"%s %d: asic(s) went offline", compac->drv->name, compac->device_id);
						info->mining_state = MINER_RESET;
						continue;
					}
				}
			}

			if (info->accepted > 10 && ms_tdiff(&now, &info->last_frequency_ping) > 100 &&
				ms_tdiff(&info->last_nonce, &info->last_frequency_adjust) > 0 &&
				ms_tdiff(&now, &info->last_frequency_adjust) >= bound(opt_gekko_step_delay, 1, 600) * 1000) {
				if (info->frequency != info->frequency_requested) {
					float new_frequency;
					if (info->frequency < info->frequency_requested) {
						new_frequency = info->frequency + opt_gekko_step_freq;
						if (new_frequency > info->frequency_requested) {
							new_frequency = info->frequency_requested;
						}
					} else {
						new_frequency = info->frequency - opt_gekko_step_freq;
						if (new_frequency < info->frequency_requested) {
							new_frequency = info->frequency_requested;
						}
					}
					compac_set_frequency(compac, new_frequency);
					compac_send_chain_inactive(compac);
					info->update_work = 1;
					info->accepted = 0;
				}

				cgtime(&info->last_frequency_adjust);
			}

			work = get_queued(compac);

			if (work) {
				info->job_id = (info->job_id + 1) % (info->max_job_id - 3);
				old_work = info->work[info->job_id];
				info->work[info->job_id] = work;
				info->active_work[info->job_id] = 1;
				info->vmask = work->pool->vmask;
				init_task(info);
			} else {
				busy_work(info);
				cgtime(&info->monitor_time);
			}

			err = usb_write(compac, (char *)info->task, info->task_len, &read_bytes, C_SENDWORK);
			if (err != LIBUSB_SUCCESS) {
				applog(LOG_WARNING,"%s %d: usb failure (%d)", compac->drv->name, compac->device_id, err);
				info->mining_state = MINER_RESET;
				continue;
			}
			if (read_bytes != info->task_len) {
				if (ms_tdiff(&now, &info->last_write_error) > (5 * 1000)) {
					applog(LOG_WARNING,"%s %d: usb write error [%d:%d]", compac->drv->name, compac->device_id, read_bytes, info->task_len);
					cgtime(&info->last_write_error);
				}
			}
			thread_yield();
			if (old_work) {
				mutex_lock(&info->lock);
				work_completed(compac, old_work);
				mutex_unlock(&info->lock);
				old_work = NULL;
			}

			info->task_ms = (info->task_ms * 9 + ms_tdiff(&now, &info->last_task)) / 10;
			cgtime(&info->last_task);
		}
		cgsleep_ms(sleep_ms);
	}
}

static void *compac_listen(void *object)
{
	struct cgpu_info *compac = (struct cgpu_info *)object;
	struct COMPAC_INFO *info = compac->device_data;
	int read_bytes, crc_ok, cmd_resp;
	uint32_t err = 0;
	struct timeval now;

	while (info->mining_state != MINER_SHUTDOWN) {
		memset(info->rx, 0, info->rx_len);
		thread_yield();
		err = usb_read_timeout(compac, (char *)info->rx, info->rx_len, &read_bytes, 200, C_GETRESULTS);
		cgtime(&now);

		if (read_bytes > 0) {
			cmd_resp = (info->rx[read_bytes - 1] <= 0x1F && bmcrc(info->rx, 8 * read_bytes - 5) == info->rx[read_bytes - 1]) ? 1 : 0;
			dumpbuffer(compac, LOG_INFO, "RX", info->rx, read_bytes);

			if (cmd_resp && info->rx[0] == 0x80) {
				float frequency;
				cgtime(&info->last_frequency_report);

				if (info->asic_type == BM1387 && (info->rx[2] == 0 || (info->rx[3] >> 4) == 0 || (info->rx[3] & 0x0f) == 0)) {
					dumpbuffer(compac, LOG_WARNING, "RX", info->rx, read_bytes);
					applog(LOG_WARNING,"%s %d: bad frequency", compac->drv->name, compac->device_id);
				} else {
					if (info->asic_type == BM1387) {
						frequency = 25.0 * info->rx[1] / (info->rx[2] * (info->rx[3] >> 4) * (info->rx[3] & 0x0f));
					} else if (info->asic_type == BM1384) {
						frequency = (info->rx[1] + 1) * 6.25 / (1 + info->rx[2] & 0x0f) * pow(2, (3 - info->rx[3])) + ((info->rx[2] >> 4) * 6.25);
					}

					if (frequency != info->frequency) {
						applog(LOG_WARNING,"%s %d: frequency changed %.2fMHz -> %.2fMHz", compac->drv->name, compac->device_id, info->frequency, frequency);
					} else {
						applog(LOG_INFO,"%s %d: chip reported frequency of %.2fMHz", compac->drv->name, compac->device_id, frequency);
					}

					info->frequency = frequency;
					info->hashrate = info->chips * info->frequency * info->cores * 1000000;
					info->fullscan_ms = 1000.0 * 0xffffffffull / info->hashrate;
					info->scanhash_ms = bound(info->fullscan_ms / 2, 1, 100);
					info->ticket_mask = bound(pow(2, ceil(log(info->hashrate / (2.0 * 0xffffffffull)) / log(2))) - 1, 0, 4000);
					info->ticket_mask = (info->asic_type == BM1387) ? 0 : info->ticket_mask;
					info->difficulty = info->ticket_mask + 1;

				}

			}

			switch (info->mining_state) {
				case MINER_CHIP_COUNT:
				case MINER_CHIP_COUNT_XX:
					if (cmd_resp && info->rx[0] == 0x13) {
						info->chips++;
						info->mining_state = MINER_CHIP_COUNT_XX;
					}
					break;
				case MINER_OPEN_CORE:
					if ((info->rx[0] == 0x72 && info->rx[1] == 0x03 && info->rx[2] == 0xEA && info->rx[3] == 0x83) ||
						(info->rx[0] == 0xE1 && info->rx[0] == 0x6B && info->rx[0] == 0xF8 && info->rx[0] == 0x09)) {
						//open core nonces = healthy chips.
						info->zero_check++;
					}
					break;
				case MINER_MINING:
					if (!cmd_resp) {
						thread_yield();
						mutex_lock(&info->lock);
						info->hashes += compac_check_nonce(compac);
						mutex_unlock(&info->lock);
					}
					break;
				default:
					break;
			}
		} else {

			// RX line is idle, let's squeeze in a command to the micro if needed.
			if (info->asic_type == BM1387) {
				if (ms_tdiff(&now, &info->last_micro_ping) > 5000 && ms_tdiff(&now, &info->last_task) > 1 && ms_tdiff(&now, &info->last_task) < 3) {
					compac_micro_send(compac, M1_GET_TEMP, 0x00, 0x00);
					cgtime(&info->last_micro_ping);
				}
			}

			switch (info->mining_state) {
				case MINER_CHIP_COUNT_XX:
					applog(info->log_startup, "%s %d: found %d chip(s)", compac->drv->name, compac->device_id, info->chips);
					info->mining_state = MINER_CHIP_COUNT_OK;
					break;
				default:
					break;
			}
		}
	}
}

static bool compac_init(struct thr_info *thr)
{
	int i;
	struct cgpu_info *compac = thr->cgpu;
	struct COMPAC_INFO *info = compac->device_data;

	info->prev_nonce = 0;
	info->fail_count = 0;
	info->scanhash_ms = 10;
	info->log_startup = LOG_WARNING;

	memset(info->rx, 0, BUFFER_MAX);
	memset(info->tx, 0, BUFFER_MAX);
	memset(info->cmd, 0, BUFFER_MAX);
	memset(info->end, 0, BUFFER_MAX);
	memset(info->task, 0, BUFFER_MAX);

	for (i = 0; i < JOB_MAX; i++) {
		info->active_work[i] = false;
		info->work[i] = NULL;
	}

	cgtime(&info->last_write_error);
	cgtime(&info->last_frequency_adjust);
	cgtime(&info->last_frequency_ping);
	cgtime(&info->last_micro_ping);
	cgtime(&info->last_scanhash);
	cgtime(&info->last_reset);
	cgtime(&info->last_task);
	cgtime(&info->start_time);
	cgtime(&info->monitor_time);

	switch (info->ident) {
		case IDENT_BSC:
		case IDENT_GSC:
			info->frequency_requested = opt_gekko_gsc_freq;
			info->frequency_start = info->frequency_requested;
			break;
		case IDENT_BSD:
		case IDENT_GSD:
			info->frequency_requested = opt_gekko_gsd_freq;
			info->frequency_start = opt_gekko_start_freq;
			break;
		case IDENT_BSE:
		case IDENT_GSE:
			info->frequency_requested = opt_gekko_gse_freq;
			info->frequency_start = opt_gekko_start_freq;
			break;
		case IDENT_GSH:
			info->frequency_requested = opt_gekko_gsh_freq;
			info->frequency_start = opt_gekko_gsh_freq;
			break;
		default:
			info->frequency_requested = 200;
			info->frequency_start = info->frequency_requested;
			break;
	}
	if (info->frequency_start > info->frequency_requested) {
		info->frequency_start = info->frequency_requested;
	}
	info->frequency_requested = ceil(100 * (info->frequency_requested) / 625.0) * 6.25;
	info->frequency_start = ceil(100 * (info->frequency_start) / 625.0) * 6.25;

	if (!info->rthr.pth) {
		pthread_mutex_init(&info->lock, NULL);
		pthread_mutex_init(&info->wlock, NULL);

		if (thr_info_create(&(info->rthr), NULL, compac_listen, (void *)compac)) {
			applog(LOG_ERR, "%s %i: read thread create failed", compac->drv->name, compac->device_id);
			return false;
		} else {
			applog(LOG_INFO, "%s %i: read thread created", compac->drv->name, compac->device_id);
		}

		if (thr_info_create(&(info->wthr), NULL, compac_mine, (void *)compac)) {
			applog(LOG_ERR, "%s %i: write thread create failed", compac->drv->name, compac->device_id);
			return false;
		} else {
			applog(LOG_INFO, "%s %i: write thread created", compac->drv->name, compac->device_id);
		}

		pthread_detach(info->rthr.pth);
		pthread_detach(info->wthr.pth);
	}

	return true;
}

static int64_t compac_scanwork(struct thr_info *thr)
{
	struct cgpu_info *compac = thr->cgpu;
	struct COMPAC_INFO *info = compac->device_data;

	struct timeval now;
	int read_bytes;
	uint32_t err = 0;
	uint64_t hashes = 0;

	if (compac->usbinfo.nodev)
		return -1;

	thread_yield();
	cgtime(&now);

	switch (info->mining_state) {
		case MINER_INIT:
			info->mining_state = MINER_CHIP_COUNT;
			info->chips = 0;
			info->ramping = 0;

			if (info->asic_type == BM1387) {
				unsigned char buffer[] = {0x54, 0x05, 0x00, 0x00, 0x00};
				compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);
			} else if (info->asic_type == BM1384) {
				unsigned char buffer[] = {0x84, 0x00, 0x00, 0x00};
				compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
			}
			return 0;
			break;
		case MINER_CHIP_COUNT:
			if (ms_tdiff(&now, &info->last_reset) > 5000) {
				applog(LOG_WARNING, "%s %d: found 0 chip(s)", compac->drv->name, compac->device_id);
				info->mining_state = MINER_RESET;
				return 0;
			}
			break;
		case MINER_CHIP_COUNT_OK:
			cgsleep_ms(50);
			compac_set_frequency(compac, info->frequency_start);
			compac_send_chain_inactive(compac);
			return 0;
			break;
		case MINER_OPEN_CORE:
			info->job_id = info->ramping % info->max_job_id;

			//info->task_hcn = (0xffffffff / info->chips) * (1 + info->ramping) / info->cores;
			init_task(info);
			dumpbuffer(compac, LOG_DEBUG, "RAMP", info->task, info->task_len);

			usb_write(compac, (char *)info->task, info->task_len, &read_bytes, C_SENDWORK);
			if (info->ramping > info->cores) {
				//info->job_id = 0;
				info->mining_state = MINER_OPEN_CORE_OK;
				info->task_hcn = (0xffffffff / info->chips);
				return 0;
			}

			info->ramping++;
			info->task_ms = (info->task_ms * 9 + ms_tdiff(&now, &info->last_task)) / 10;
			cgtime(&info->last_task);
			cgsleep_ms(10);
			return 0;
			break;
		case MINER_OPEN_CORE_OK:
			applog(info->log_startup, "%s %d: start work @ %.2fMHz", compac->drv->name, compac->device_id, info->frequency);
			cgtime(&info->start_time);
			cgtime(&info->monitor_time);
			cgtime(&info->last_frequency_adjust);
			cgtime(&info->last_frequency_ping);
			cgtime(&info->last_frequency_report);
			cgtime(&info->last_micro_ping);
			cgtime(&info->last_nonce);
			compac_flush_buffer(compac);
			info->log_startup = LOG_WARNING;
			info->mining_state = MINER_MINING;
			return 0;
			break;
		case MINER_MINING:
			if (ms_tdiff(&now, &info->start_time) > ( 15 * 1000 )) {
				info->log_startup = LOG_INFO;
			}
			break;
		case MINER_RESET:
			if (info->asic_type == BM1387) {
				compac_flush_work(compac);
				compac_toggle_reset(compac);
				compac_prepare(thr);

				info->fail_count++;
				info->mining_state = MINER_INIT;
				return 0;
			} else {
				usb_nodev(compac);
				return -1;
			}
			break;
		case MINER_MINING_DUPS:
			info->mining_state = MINER_MINING;
			if ((int)info->frequency == 200) {
				//possible terminus reset condition.
				compac_set_frequency(compac, info->frequency);
				compac_send_chain_inactive(compac);
				cgtime(&info->last_frequency_adjust);
			} else {
				//check for reset condition
				if (info->asic_type == BM1384) {
					unsigned char buffer[] = {0x84, 0x00, 0x04, 0x00};
					compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
				}
				cgtime(&info->last_frequency_ping);
			}
			break;
		default:
			break;
	}
	hashes = info->hashes;
	info->hashes -= hashes;
	cgsleep_ms(info->scanhash_ms);

	return hashes;
}

static struct cgpu_info *compac_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *compac;
	struct COMPAC_INFO *info;
	int err, i;
	bool exclude_me = 0;
	uint32_t baudrate = CP210X_DATA_BAUD;
	unsigned int bits = CP210X_BITS_DATA_8 | CP210X_BITS_PARITY_MARK;

	compac = usb_alloc_cgpu(&gekko_drv, 1);

	if (!usb_init(compac, dev, found)) {
		applog(LOG_INFO, "failed usb_init");
		compac = usb_free_cgpu(compac);
		return NULL;
	}

	info = cgcalloc(1, sizeof(struct COMPAC_INFO));
	compac->device_data = (void *)info;

	info->ident = usb_ident(compac);

	if (opt_gekko_gsc_detect || opt_gekko_gsd_detect || opt_gekko_gse_detect || opt_gekko_gsh_detect) {
		exclude_me  = (info->ident == IDENT_BSC && !opt_gekko_gsc_detect);
		exclude_me |= (info->ident == IDENT_GSC && !opt_gekko_gsc_detect);
		exclude_me |= (info->ident == IDENT_BSD && !opt_gekko_gsd_detect);
		exclude_me |= (info->ident == IDENT_GSD && !opt_gekko_gsd_detect);
		exclude_me |= (info->ident == IDENT_BSE && !opt_gekko_gse_detect);
		exclude_me |= (info->ident == IDENT_GSE && !opt_gekko_gse_detect);
		exclude_me |= (info->ident == IDENT_GSH && !opt_gekko_gsh_detect);
	}

	if (opt_gekko_serial != NULL && (strstr(opt_gekko_serial, compac->usbdev->serial_string) == NULL)) {
		exclude_me = true;
	}

	if (exclude_me) {
		usb_uninit(compac);
		free(info);
		compac->device_data = NULL;
		return NULL;
	}

	switch (info->ident) {
		case IDENT_BSC:
		case IDENT_GSC:
		case IDENT_BSD:
		case IDENT_GSD:
		case IDENT_BSE:
		case IDENT_GSE:
			info->asic_type = BM1384;
			info->cores = 55;
			info->max_job_id = 0x1f;
			info->rx_len = 5;
			info->task_len = 64;
			info->tx_len = 4;
			info->healthy = 0.33;

			usb_transfer_data(compac, CP210X_TYPE_OUT, CP210X_REQUEST_IFC_ENABLE, CP210X_VALUE_UART_ENABLE, info->interface, NULL, 0, C_ENABLE_UART);
			usb_transfer_data(compac, CP210X_TYPE_OUT, CP210X_REQUEST_DATA, CP210X_VALUE_DATA, info->interface, NULL, 0, C_SETDATA);
			usb_transfer_data(compac, CP210X_TYPE_OUT, CP210X_REQUEST_BAUD, 0, info->interface, &baudrate, sizeof (baudrate), C_SETBAUD);
			usb_transfer_data(compac, CP210X_TYPE_OUT, CP210X_SET_LINE_CTL, bits, info->interface, NULL, 0, C_SETPARITY);
			break;
		case IDENT_GSH:
			info->asic_type = BM1387;
			info->rx_len = 7;
			info->task_len = 54;
			if (opt_gekko_boost) {
				info->task_len += 96;
			}
			info->cores = 114;
			info->max_job_id = 0x7f;
			info->healthy = 0.75;

			compac_toggle_reset(compac);
			break;
		default:
			quit(1, "%s compac_detect_one() invalid %s ident=%d",
				compac->drv->dname, compac->drv->dname, info->ident);
	}

	info->interface = usb_interface(compac);
	info->mining_state = MINER_INIT;

	applog(LOG_DEBUG, "Using interface %d", info->interface);

	if (!add_cgpu(compac))
		quit(1, "Failed to add_cgpu in compac_detect_one");

	update_usb_stats(compac);

	for (i = 0; i < 8; i++) {
		compac->unique_id[i] = compac->unique_id[i+3];
	}
	compac->unique_id[8] = 0;

	applog(LOG_WARNING, "%s %d: %s (%s)", compac->drv->name, compac->device_id, compac->usbdev->prod_string, compac->unique_id);

	return compac;
}

static void compac_detect(bool __maybe_unused hotplug)
{
	usb_detect(&gekko_drv, compac_detect_one);
}

static bool compac_prepare(struct thr_info *thr)
{
	struct cgpu_info *compac = thr->cgpu;
	struct COMPAC_INFO *info = compac->device_data;
	int i;
	int read_bytes = 1;
	bool miner_ok = true;

	info->thr = thr;
	info->bauddiv = 0x19; // 115200
	//info->bauddiv = 0x0D; // 214286
	//info->bauddiv = 0x07; // 375000

	//Sanity check and abort to prevent miner thread from being created.
	if (info->asic_type == BM1387) {
		unsigned char buffer[] = { 0x58, 0x09, 0x00, 0x1C, 0x00, 0x20, 0x07, 0x00, 0x19 };
		info->bauddiv = 0x01; // 1.5Mbps baud.
		buffer[6] = info->bauddiv;
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);
		cgsleep_ms(1);
		usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_BAUD, (info->bauddiv + 1), (FTDI_INDEX_BAUD_BTS & 0xff00) | info->interface, C_SETBAUD);
		cgsleep_ms(1);

		// Ping Micro
		if (info->asic_type == BM1387) {
			info->vcore = bound(opt_gekko_gsh_vcore, 300, 810);
			info->micro_found = 1;
			if (!compac_micro_send(compac, M1_GET_TEMP, 0x00, 0x00)) {
				info->micro_found = 0;
				applog(LOG_INFO, "%s %d: micro not found : dummy mode", compac->drv->name, compac->device_id);
			} else {
				uint8_t vcc = (info->vcore / 1000.0 - 0.3) / 0.002;
				applog(LOG_INFO, "%s %d: requesting vcore of %dmV (%x)", compac->drv->name, compac->device_id, info->vcore, vcc);
				compac_micro_send(compac, M2_SET_VCORE, 0x00, vcc);   // Default 400mV
			}
		}
	}

	if (info->mining_state == MINER_INIT) {
		if (info->asic_type == BM1387) {
			unsigned char buffer[] = {0x54, 0x05, 0x00, 0x00, 0x00};
			compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);
			compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);
		} else if (info->asic_type == BM1384) {
			unsigned char buffer[] = {0x84, 0x00, 0x00, 0x00};
			compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
			compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
		}

		miner_ok = false;
		while (read_bytes) {
			memset(info->rx, 0, info->rx_len);
			usb_read_timeout(compac, (char *)info->rx, info->rx_len, &read_bytes, 50, C_GETRESULTS);
			if (read_bytes > 0 && info->rx[0] == 0x13) {
				dumpbuffer(compac, LOG_INFO, "RX", info->rx, read_bytes);
				miner_ok = true;
			}
		}

		if (!miner_ok) {
			applog(LOG_WARNING, "%s %d: found 0 chip(s)", compac->drv->name, compac->device_id);
			if (info->ident == IDENT_BSD || info->ident == IDENT_GSD) {
				//Don't bother retyring, will just waste resources.
				compac->deven = DEV_DISABLED;
			}
		}
	}

	return true;
}

static void compac_statline(char *buf, size_t bufsiz, struct cgpu_info *compac)
{
	struct COMPAC_INFO *info = compac->device_data;
	if (info->chips == 0) {
		return;
	}
	if (info->asic_type == BM1387) {
		if (info->micro_found) {
			tailsprintf(buf, bufsiz, "BM1387:%i %.2fMHz (%d/%d/%d/%.0fF)", info->chips, info->frequency, info->scanhash_ms, info->task_ms, info->fullscan_ms, info->micro_temp);
		} else {
			if (opt_log_output) {
				tailsprintf(buf, bufsiz, "BM1387:%i %.2fMHz (%d/%d/%d)", info->chips, info->frequency, info->scanhash_ms, info->task_ms, info->fullscan_ms);
			} else {
				tailsprintf(buf, bufsiz, "BM1387:%i %.2fMHz", info->chips, info->frequency);
			}
		}
	} else {
		if (opt_log_output) {
			tailsprintf(buf, bufsiz, "BM1384:%i %.2fMHz (%d/%d/%d)", info->chips, info->frequency, info->scanhash_ms, info->task_ms, info->fullscan_ms);
		} else {
			tailsprintf(buf, bufsiz, "BM1384:%i %.2fMHz", info->chips, info->frequency_requested);
		}
	}
}

static struct api_data *compac_api_stats(struct cgpu_info *compac)
{
	struct COMPAC_INFO *info = compac->device_data;
	struct api_data *root = NULL;

	root = api_add_int(root, "Nonces", &info->nonces, false);
	root = api_add_int(root, "Accepted", &info->accepted, false);

	//root = api_add_temp(root, "Temp", &info->micro_temp, false);

	return root;
}

static void compac_shutdown(struct thr_info *thr)
{
	struct cgpu_info *compac = thr->cgpu;
	struct COMPAC_INFO *info = compac->device_data;
	if (!compac->usbinfo.nodev) {
		if (info->asic_type == BM1387) {
			compac_micro_send(compac, M2_SET_VCORE, 0x00, 0x00);   // 300mV
			compac_toggle_reset(compac);
		} else if (info->asic_type == BM1384 && info->frequency != 100) {
			compac_set_frequency(compac, 100);
		}
	}
	info->mining_state = MINER_SHUTDOWN;
	pthread_join(info->rthr.pth, NULL); // Let thread close.
	pthread_join(info->wthr.pth, NULL); // Let thread close.
	PTH(thr) = 0L;
}

uint64_t bound(uint64_t value, uint64_t lower_bound, uint64_t upper_bound)
{
	if (value < lower_bound)
		return lower_bound;
	if (value > upper_bound)
		return upper_bound;
	return value;
}

void stuff_reverse(unsigned char *dst, unsigned char *src, uint32_t len)
{
	uint32_t i;
	for (i = 0; i < len; i++) {
		dst[i] = src[len - i - 1];
	}
}

void stuff_lsb(unsigned char *dst, uint32_t x)
{
	dst[0] = (x >>  0) & 0xff;
	dst[1] = (x >>  8) & 0xff;
	dst[2] = (x >> 16) & 0xff;
	dst[3] = (x >> 24) & 0xff;
}

void stuff_msb(unsigned char *dst, uint32_t x)
{
	dst[0] = (x >> 24) & 0xff;
	dst[1] = (x >> 16) & 0xff;
	dst[2] = (x >>  8) & 0xff;
	dst[3] = (x >>  0) & 0xff;
}

struct device_drv gekko_drv = {
    .drv_id              = DRIVER_gekko,
    .dname               = "GekkoScience",
    .name                = "GSX",
    .hash_work           = hash_queued_work,
    .get_api_stats       = compac_api_stats,
    .get_statline_before = compac_statline,
    .drv_detect          = compac_detect,
    .scanwork            = compac_scanwork,
    .flush_work          = compac_flush_work,
    .update_work         = compac_update_work,
    .thread_prepare      = compac_prepare,
    .thread_init         = compac_init,
    .thread_shutdown     = compac_shutdown,
};
