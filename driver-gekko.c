/*
 * This is a driver for GekkoScience hardware, based initially off of the icarus driver.
 * original code forked from https://github.com/vthoang/cgminer.git

 * Supported hardware:
 * - GekkoScience Compac USB stick
 * - GekkoScience 2pac USB stick
 * - GekkoScience Newpac USB stick
 * - GekkoScience Terminus R606 pod
 */

/*#include <float.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>
#include "config.h"
*/
#include "driver-gekko.h"
#include "crc.h"

#ifdef WIN32
#include <windows.h>
#endif

#include "compat.h"
#include <unistd.h>
/*#include "miner.h"
#include "usbutils.h"
*/

static bool compac_prepare(struct thr_info *thr);
static pthread_mutex_t static_lock = PTHREAD_MUTEX_INITIALIZER;
static bool last_widescreen;
static uint8_t dev_init_count[0xffff] = {0};
static uint8_t *init_count;
static uint32_t stat_len;
static uint32_t chip_max;

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
		char str[2048];
		const char * hex = "0123456789ABCDEF";
		char * pout = str;
		int i = 0;

		for(; i < 768 && i < len - 1; ++i){
			*pout++ = hex[(*ptr>>4)&0xF];
			*pout++ = hex[(*ptr++)&0xF];
			if (i % 42 == 41) {
				*pout = 0;
				pout = str;
				applog(LOG_LEVEL, "%i: %s %s: %s", compac->cgminer_id, compac->drv->name, note, str);
			} else {
				*pout++ = ':';
			}
		}
		*pout++ = hex[(*ptr>>4)&0xF];
		*pout++ = hex[(*ptr)&0xF];
		*pout = 0;
		applog(LOG_LEVEL, "%d: %s %d - %s: %s", compac->cgminer_id, compac->drv->name, compac->device_id, note, str);

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
	usb_write(compac, (char *)info->cmd, bytes, &read_bytes, C_REQUESTRESULTS);

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

	int log_level = (bytes < info->task_len) ? LOG_INFO : LOG_INFO;

	dumpbuffer(compac, LOG_INFO, "TX", info->cmd, bytes);
	usb_write(compac, (char *)info->cmd, bytes, &read_bytes, C_REQUESTRESULTS);

	//let the usb frame propagate
	cgsleep_ms(1);
}

static void compac_send_chain_inactive(struct cgpu_info *compac)
{
	struct COMPAC_INFO *info = compac->device_data;
	int i;

	applog(LOG_INFO,"%d: %s %d - sending chain inactive for %d chip(s)", compac->cgminer_id, compac->drv->name, compac->device_id, info->chips);
	if (info->asic_type == BM1387) {
		unsigned char buffer[5] = {0x55, 0x05, 0x00, 0x00, 0x00};
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);; // chain inactive
		cgsleep_ms(5);
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);; // chain inactive
		cgsleep_ms(5);
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);; // chain inactive
		for (i = 0; i < info->chips; i++) {
			buffer[0] = 0x41;
			buffer[1] = 0x05;
			buffer[2] = (0x100 / info->chips) * i;
			cgsleep_ms(5);
			compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);;
		}

		cgsleep_ms(10);
		unsigned char baudrate[] = { 0x58, 0x09, 0x00, 0x1C, 0x00, 0x20, 0x07, 0x00, 0x19 };
		if (opt_gekko_bauddiv) {
			info->bauddiv = opt_gekko_bauddiv;
		} else {
			info->bauddiv = 0x01; // 1.5Mbps baud.
#ifdef WIN32
			if (info->midstates == 4)
				info->bauddiv = 0x0D; // 214Kbps baud.
#endif
		}
		applog(LOG_INFO, "%d: %s %d - setting bauddiv : %02x", compac->cgminer_id, compac->drv->name, compac->device_id, info->bauddiv);
		baudrate[6] = info->bauddiv;
		compac_send(compac, (char *)baudrate, sizeof(baudrate), 8 * sizeof(baudrate) - 8);
		cgsleep_ms(10);
		usb_transfer(compac, FTDI_TYPE_OUT, FTDI_REQUEST_BAUD, (info->bauddiv + 1), (FTDI_INDEX_BAUD_BTS & 0xff00) | info->interface, C_SETBAUD);
		cgsleep_ms(10);

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

	if (info->mining_state == MINER_CHIP_COUNT_OK) {
		applog(LOG_INFO, "%d: %s %d - open cores", compac->cgminer_id, compac->drv->name, compac->device_id);
		info->zero_check = 0;
		info->task_hcn = 0;
		info->mining_state = MINER_OPEN_CORE;
	}
}

static void compac_update_rates(struct cgpu_info *compac)
{
	struct COMPAC_INFO *info = compac->device_data;
	struct ASIC_INFO *asic;
	float average_frequency = 0;
	int i;

	info->frequency_asic = 0;
	for (i = 0; i < info->chips; i++) {
		asic = &info->asics[i];
		asic->hashrate = asic->frequency * info->cores * 1000000;
		asic->fullscan_ms = 1000.0 * 0xffffffffull / asic->hashrate;
		asic->fullscan_us = 1000.0 * 1000.0 * 0xffffffffull / asic->hashrate;
		average_frequency += asic->frequency;
		info->frequency_asic = (asic->frequency > info->frequency_asic ) ? asic->frequency : info->frequency_asic;
	}

	average_frequency = average_frequency / info->chips;
	if (average_frequency != info->frequency) {
		applog(LOG_INFO,"%d: %s %d - frequency updated %.2fMHz -> %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, info->frequency, average_frequency);
		info->frequency = average_frequency;
		info->wu_max = 0;
	}

	info->wu = info->chips * info->frequency * info->cores / 71.6;
	info->hashrate = info->chips * info->frequency * info->cores * 1000000;
	info->fullscan_ms = 1000.0 * 0xffffffffull / info->hashrate;
	info->fullscan_us = 1000.0 * 1000.0 * 0xffffffffull / info->hashrate;
	info->ticket_mask = bound(pow(2, ceil(log(info->hashrate / (2.0 * 0xffffffffull)) / log(2))) - 1, 0, 4000);
	info->ticket_mask = (info->asic_type == BM1387) ? 0 : info->ticket_mask;
	info->difficulty = info->ticket_mask + 1;
	info->scanhash_ms = info->fullscan_ms * info->difficulty / 4;

	info->wait_factor = ((!opt_gekko_noboost && info->vmask && info->asic_type == BM1387) ? opt_gekko_wait_factor * info->midstates : opt_gekko_wait_factor);
	info->max_task_wait = bound(info->wait_factor * info->fullscan_us, 1, 3 * info->fullscan_us);

	if (info->asic_type == BM1387) {
		if (opt_gekko_tune_up > 95) {
			info->tune_up = 100.0 * ((info->frequency - 6.25 * (600 / info->frequency)) / info->frequency);
		} else {
			info->tune_up = opt_gekko_tune_up;
		}
	} else {
		info->tune_up = 99;
	}
}

static void compac_set_frequency_single(struct cgpu_info *compac, float frequency, int asic_id)
{
	struct COMPAC_INFO *info = compac->device_data;
	struct ASIC_INFO *asic = &info->asics[asic_id];
	uint32_t i, r, r1, r2, r3, p1, p2, pll;

	if (info->asic_type == BM1387) {
		unsigned char buffer[] = {0x48, 0x09, 0x00, 0x0C, 0x00, 0x50, 0x02, 0x41, 0x00};   //250MHz -- osc of 25MHz
		frequency = bound(frequency, 50, 1200);
		frequency = ceil(100 * (frequency) / 625.0) * 6.25;

		if (frequency < 400) {
			buffer[7] = 0x41;
			buffer[5] = (frequency * 8) / 25;
		} else {
			buffer[7] = 0x21;
			buffer[5] = (frequency * 4) / 25;
		}
		buffer[2] = (0x100 / info->chips) * asic_id;

		//asic->frequency = frequency;
		asic->frequency_set = frequency;
		asic->frequency_attempt++;

		applog(LOG_INFO, "%d: %s %d - setting chip[%d] frequency (%d) %.2fMHz -> %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, asic_id, asic->frequency_attempt, asic->frequency, frequency);
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);

		//unsigned char gateblk[9] = {0x48, 0x09, 0x00, 0x1C, 0x40, 0x20, 0x99, 0x80, 0x01};
		//gateblk[6] = 0x80 | info->bauddiv;
		//gateblk[2] = (0x100 / info->chips) * id;
		//compac_send(compac, (char *)gateblk, sizeof(gateblk), 8 * sizeof(gateblk) - 8);; // chain inactive

	}
}

static void compac_set_frequency(struct cgpu_info *compac, float frequency)
{
	struct COMPAC_INFO *info = compac->device_data;
	uint32_t i, r, r1, r2, r3, p1, p2, pll;

	if (info->asic_type == BM1387) {
		unsigned char buffer[] = {0x58, 0x09, 0x00, 0x0C, 0x00, 0x50, 0x02, 0x41, 0x00};   //250MHz -- osc of 25MHz
		frequency = bound(frequency, 50, 1200);
		frequency = ceil(100 * (frequency) / 625.0) * 6.25;

		if (frequency < 400) {
			buffer[7] = 0x41;
			buffer[5] = (frequency * 8) / 25;
		} else {
			buffer[7] = 0x21;
			buffer[5] = (frequency * 4) / 25;
		}
/*
		} else {
			buffer[7] = 0x11;
			buffer[5] = (frequency * 2) / 25;
		}
*/

		applog(LOG_INFO, "%d: %s %d - setting frequency to %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, frequency);
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

		applog(LOG_INFO, "%d: %s %d - setting frequency to %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, frequency);
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
		buffer[0] = 0x84;
		buffer[1] = 0x00;
		buffer[2] = 0x00;
//		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
		buffer[2] = 0x04;
		compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
	}
	compac_update_rates(compac);
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

	applog(info->log_wide,"%d: %s %d - Toggling ASIC nRST to reset", compac->cgminer_id, compac->drv->name, compac->device_id);

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
	cgsleep_ms(200);

	cgtime(&info->last_reset);
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

	if ((info->rx[0] == 0x72 && info->rx[1] == 0x03 && info->rx[2] == 0xEA && info->rx[3] == 0x83) || 
		(info->rx[0] == 0xE1 && info->rx[1] == 0x6B && info->rx[2] == 0xF8 && info->rx[3] == 0x09)) {
		//busy work nonces
		return hashes;
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

	int asic_id = floor(info->rx[0] / (0x100 / info->chips));
	struct ASIC_INFO *asic = &info->asics[asic_id];

	if (nonce == asic->prev_nonce) {
		applog(LOG_INFO, "%d: %s %d - Duplicate Nonce : %08x @ %02x [%02x %02x %02x %02x %02x %02x %02x]", compac->cgminer_id, compac->drv->name, compac->device_id, nonce, job_id,
			info->rx[0], info->rx[1], info->rx[2], info->rx[3], info->rx[4], info->rx[5], info->rx[6]);
#ifndef WIN32
		info->dups++;
		asic->dups++;
		cgtime(&info->last_dup_time);
		if (info->dups == 1) {
			info->mining_state = MINER_MINING_DUPS;
		}
#endif
		return hashes;
	}

	info->prev_nonce = nonce;
	asic->prev_nonce = nonce;

	applog(LOG_INFO, "%d: %s %d - Device reported nonce: %08x @ %02x (%d)", compac->cgminer_id, compac->drv->name, compac->device_id, nonce, job_id, info->tracker);

	struct work *work = info->work[job_id];
	bool active_work = info->active_work[job_id];
	int midnum = 0;
	if (!opt_gekko_noboost && info->vmask) {
		// force check last few nonces by [job_id - 1]
		if (info->asic_type == BM1387) {
			for (i = 0; i < info->midstates; i++) {
				if (job_id >= i) {
					if (info->active_work[job_id - i]) {
						work = info->work[job_id - i];
						active_work = info->active_work[job_id - i];
						if (active_work && work) {
							work->micro_job_id = pow(2, i);
							memcpy(work->data, &(work->pool->vmask_001[work->micro_job_id]), 4);
							if (test_nonce(work, nonce)) {
								midnum = i;
								if (i > 0) {
									info->boosted = true;
								}
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
		cgtime(&asic->last_nonce);

		if (midnum > 0) {
			applog(LOG_INFO, "%d: %s %d - AsicBoost nonce found : midstate%d", compac->cgminer_id, compac->drv->name, compac->device_id, midnum);
		}

		hashes = info->difficulty * 0xffffffffull;
		info->xhashes += info->difficulty;

		info->accepted++;
		info->failing = false;
		info->dups = 0;
		asic->dups = 0;
	} else {
		if (hwe != compac->hw_errors) {
			cgtime(&info->last_hwerror);
		}
	}

	return hashes;
}

static void busy_work(struct COMPAC_INFO *info)
{
	memset(info->task, 0, info->task_len);

	if (info->asic_type == BM1387) {
		info->task[0] = 0x21;
		info->task[1] = info->task_len;
		info->task[2] = info->job_id & 0xff;
		info->task[3] = ((!opt_gekko_noboost && info->vmask) ? 0x04 : 0x01);
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
		info->task[3] = ((!opt_gekko_noboost && info->vmask) ? info->midstates : 0x01);

		if (info->mining_state == MINER_MINING) {
			stuff_reverse(info->task + 8, work->data + 64, 12);
			stuff_reverse(info->task + 20, work->midstate, 32);
			if (!opt_gekko_noboost && info->vmask) {
				if (info->midstates > 1);
					stuff_reverse(info->task + 20 + 32, work->midstate1, 32);
				if (info->midstates > 2);
					stuff_reverse(info->task + 20 + 32 + 32, work->midstate2, 32);
				if (info->midstates > 3);
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
	struct timeval last_rolling = (struct timeval){0};
	struct timeval last_movement = (struct timeval){0};
	struct timeval last_plateau_check = (struct timeval){0};
	struct timeval last_frequency_check = (struct timeval){0};

	struct sched_param param;
	int i, j, read_bytes, sleep_us, policy, ret_nice, ping_itr;
	uint32_t err = 0;
	uint32_t itr = 0;
	uint64_t hashes = 0;
	double rolling_minute[SAMPLE_SIZE] = {0};
	char str_frequency[1024];

	bool adjustable = 0;

#ifndef WIN32
	ret_nice = nice(-15);
#else /* WIN32 */
	pthread_getschedparam(pthread_self(), &policy, &param);
	param.sched_priority = sched_get_priority_max(policy);
	pthread_setschedparam(pthread_self(), policy, &param);
	ret_nice = param.sched_priority;
#endif /* WIN32 */
	applog(LOG_INFO, "%d: %s %d - work thread niceness (%d)", compac->cgminer_id, compac->drv->name, compac->device_id, ret_nice);

	sleep_us = 100;

	while (info->mining_state != MINER_SHUTDOWN) {
		cgtime(&now);

		if (info->chips == 0 || compac->deven == DEV_DISABLED || compac->usbinfo.nodev || info->mining_state != MINER_MINING) {
			cgsleep_ms(10);
		} else if (info->update_work || (us_tdiff(&now, &info->last_task) > info->max_task_wait)) {
			uint64_t hashrate_15, hashrate_5m, hashrate_1m, hashrate_li, hashrate_tm, hashrate_gs;
			double dev_runtime, wu;
			float frequency_computed;
			bool low_eff = 0;
			bool frequency_updated = 0;

			info->update_work = 0;

			sleep_us = bound(ceil(info->max_task_wait / 100), 1, 1000 * 1000);

			dev_runtime = cgpu_runtime(compac);
			wu = compac->diff1 / dev_runtime * 60;

			if (wu > info->wu_max) {
				info->wu_max = wu;
				cgtime(&info->last_wu_increase);
			}

			if (ms_tdiff(&now, &last_rolling) > MS_SECOND_1 * 60.0 / SAMPLE_SIZE) {
				double new_rolling = 0;
				double high_rolling = 0;
				cgtime(&last_rolling);
				rolling_minute[itr] = compac->rolling;
				itr = (itr + 1) % SAMPLE_SIZE;
				for (i = 0; i < SAMPLE_SIZE; i++) {
					high_rolling = (rolling_minute[i] != 0) ? rolling_minute[i] : high_rolling;
					new_rolling += high_rolling;
				}
				new_rolling /= SAMPLE_SIZE;
				info->rolling = new_rolling;
			}

			hashrate_gs = (double)info->rolling * 1000000ull;
			hashrate_li = (double)compac->rolling * 1000000ull;
			hashrate_1m = (double)compac->rolling1 * 1000000ull;
			hashrate_5m = (double)compac->rolling5 * 1000000ull;
			hashrate_15 = (double)compac->rolling15 * 1000000ull;
			hashrate_tm = (double)compac->total_mhashes / dev_runtime * 1000000ull;;

			info->eff_gs = 100.0 * (1.0 * hashrate_gs / info->hashrate);
			info->eff_li = 100.0 * (1.0 * hashrate_li / info->hashrate);
			info->eff_1m = 100.0 * (1.0 * hashrate_1m / info->hashrate);
			info->eff_5m = 100.0 * (1.0 * hashrate_5m / info->hashrate);
			info->eff_15 = 100.0 * (1.0 * hashrate_15 / info->hashrate);
			info->eff_wu = 100.0 * (1.0 * wu / info->wu);
			info->eff_tm = 100.0 * (1.0 * hashrate_tm / info->hashrate);

			info->eff_gs = (info->eff_gs > 100) ? 100 : info->eff_gs;
			info->eff_li = (info->eff_li > 100) ? 100 : info->eff_li;
			info->eff_1m = (info->eff_1m > 100) ? 100 : info->eff_1m;
			info->eff_5m = (info->eff_5m > 100) ? 100 : info->eff_5m;
			info->eff_15 = (info->eff_15 > 100) ? 100 : info->eff_15;
			info->eff_wu = (info->eff_wu > 100) ? 100 : info->eff_wu;
			info->eff_tm = (info->eff_tm > 100) ? 100 : info->eff_tm;

			//info->eff_gs = (((info->eff_tm > info->eff_1m) ? info->eff_tm : info->eff_1m) * 3 + info->eff_li) / 4;

			frequency_computed = ((hashrate_gs / 1000000.0) / info->cores) / info->chips;
			//frequency_computed = ((hashrate_5m / 1000000.0) / info->cores) / info->chips;
			//frequency_computed = info->eff_gs / 100.0 * info->frequency;
			//frequency_computed = ((info->eff_tm > info->eff_5m) ? info->eff_tm : ((info->eff_5m + info->eff_1m) / 2)) / 100.0 * info->frequency;
			if (frequency_computed > info->frequency_computed && frequency_computed <= info->frequency) {
				info->frequency_computed = frequency_computed;
				cgtime(&info->last_computed_increase);
			}
			if (info->asic_type == BM1387 && info->eff_5m > 10.0 && info->eff_1m < opt_gekko_tune_down && info->eff_5m < opt_gekko_tune_down && info->eff_15 < opt_gekko_tune_down && info->eff_wu < opt_gekko_tune_down) {
				low_eff = 1;
			}

			// unhealthy mining condition
			if (low_eff && ms_tdiff(&now, &info->last_frequency_adjust) > MS_MINUTE_10) {
				// only respond when target and peak converges
				if (ceil(100 * (info->frequency_requested) / 625.0) * 6.25 == ceil(100 * (info->frequency_computed) / 625.0) * 6.25) {
					// throttle reaction to once per half hour
					if (ms_tdiff(&now, &info->last_low_eff_reset) > MS_MINUTE_30) {
						applog(info->log_wide,"%d: %s %d - low eff: (1m)%.1f (5m)%.1f (15m)%.1f (WU)%.1f  - [%.1f]", compac->cgminer_id, compac->drv->name, compac->device_id, info->eff_1m, info->eff_5m, info->eff_15, info->eff_wu, opt_gekko_tune_down);
						info->low_eff_resets++;
						info->mining_state = MINER_RESET;
						cgtime(&info->last_low_eff_reset);
						continue;
					}
				}
			}

			// search for plateau
			if (ms_tdiff(&now, &last_plateau_check) > MS_SECOND_5) {
				cgtime(&last_plateau_check);
				for (i = 0; i < info->chips; i++) {
					struct ASIC_INFO *asic = &info->asics[i];
					int plateau_type = 0;

					// missing nonces
					plateau_type = (ms_tdiff(&now, &asic->last_nonce) > asic->fullscan_ms * 60) ? PT_NONONCE : plateau_type;

					// asic check-in failed
					plateau_type = (ms_tdiff(&asic->last_frequency_ping, &asic->last_frequency_reply) > MS_SECOND_30 && ms_tdiff(&now, &asic->last_frequency_reply) > MS_SECOND_30) ? PT_FREQNR : plateau_type;

					// getting duplicate nonces
					plateau_type = (asic->dups > 3) ? PT_DUPNONCE : plateau_type;

					// set frequency requests not honored
					plateau_type = (asic->frequency_attempt > 3) ? PT_FREQSET : plateau_type;

					if (plateau_type) {
						float old_frequency, new_frequency;
						new_frequency = info->frequency_requested;

						char freq_buf[512];
						char freq_chip_buf[15];

						memset(freq_buf, 0, 512);
						memset(freq_chip_buf, 0, 15);
						for (j = 0; j < info->chips; j++) {
							struct ASIC_INFO *asjc = &info->asics[j];
							sprintf(freq_chip_buf, "[%d:%.2f]", j, asjc->frequency);
							strcat(freq_buf, freq_chip_buf);
						}
						applog(LOG_INFO,"%d: %s %d - %s", compac->cgminer_id, compac->drv->name, compac->device_id, freq_buf);

						if (info->plateau_reset < 3) {
							// Capture failure high/low frequencies using first three resets
							if ((info->frequency - 6.25) > info->frequency_fail_high) {
								info->frequency_fail_high = (info->frequency - 6.25);
							}
							if ((info->frequency - 6.25) < info->frequency_fail_low) {
								info->frequency_fail_low = (info->frequency - 6.25);
							}
							applog(LOG_WARNING,"%d: %s %d - asic plateau: (%d/3) %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, info->plateau_reset + 1, info->frequency_fail_high);
						}

						if (info->plateau_reset >= 2) {
							if (ms_tdiff(&now, &info->last_frequency_adjust) > MS_MINUTE_30) {
								// Been running for 30 minutes, possible plateau
								// Overlook the incident
							} else {
								// Step back frequency
								info->frequency_fail_high -= 6.25;
							}
							if (info->frequency_fail_high < info->frequency_computed) {
								info->frequency_fail_high = info->frequency_computed;
							}
							new_frequency = info->frequency_fail_high;
							new_frequency = ceil(100 * (new_frequency) / 625.0) * 6.25;
						}
						info->plateau_reset++;
						asic->last_state = asic->state;
						asic->state = ASIC_HALFDEAD;
						cgtime(&asic->state_change_time);
						cgtime(&info->monitor_time);

						switch (plateau_type) {
							case PT_FREQNR:
								applog(info->log_wide,"%d: %s %d - no frequency reply from chip[%d] - %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, i, info->frequency);
								asic->frequency_attempt = 0;
								break;
							case PT_FREQSET:
								applog(info->log_wide,"%d: %s %d - frequency set fail to chip[%d] - %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, i, info->frequency);
								asic->frequency_attempt = 0;
								break;
							case PT_DUPNONCE:
								applog(info->log_wide,"%d: %s %d - duplicate nonces from chip[%d] - %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, i, info->frequency);
								asic->dups = 0;
								break;
							case PT_NONONCE:
								applog(info->log_wide,"%d: %s %d - missing nonces from chip[%d] - %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, i, info->frequency);
								break;
							default:
								break;
						}

						old_frequency = info->frequency_requested;
						if (new_frequency != old_frequency) {
							info->frequency_requested = new_frequency;
							applog(LOG_WARNING,"%d: %s %d - plateau adjust: target frequency %.2fMHz -> %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, old_frequency, new_frequency);
						}

						if (plateau_type == PT_NONONCE || info->asic_type == BM1387) {
							// BM1384 is less tolerant to sudden drops in frequency.  Ignore other indicators except no nonce.
							info->mining_state = MINER_RESET;
						}
						break;
					}
				}
			}

			// move target fequency towards peak frequency, once peak is done increasing.
			if (ms_tdiff(&now, &info->last_computed_increase) > MS_MINUTE_5 && ms_tdiff(&now, &info->last_frequency_adjust) > MS_MINUTE_5 && info->frequency_computed < info->frequency ) {
				float new_frequency = info->frequency - 6.25;
				if (new_frequency < info->frequency_computed) {
					new_frequency = info->frequency_computed;
				}
				new_frequency = ceil(100 * (new_frequency) / 625.0) * 6.25;

				if (new_frequency != info->frequency) {
					applog(LOG_WARNING,"%d: %s %d - peak adjust: target frequency %.2fMHz -> %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, info->frequency_requested, new_frequency);
					cgtime(&info->last_computed_increase);
					info->frequency_requested = new_frequency;
					info->frequency_fail_high = new_frequency;
					if(info->eff_gs < 90 && info->asic_type == BM1387) {
						//hashing is a bit low.  clear out settings before changing
						info->mining_state = MINER_RESET;
						continue;
					}
				}
			}

			// move running frequency towards target.
			if (ms_tdiff(&now, &last_movement) > 20 && (ms_tdiff(&now, &info->last_frequency_ping) > MS_SECOND_1 || info->frequency_fo != info->chips)) {

				info->frequency_fo--;
				cgtime(&last_movement);
				info->tracker = info->tracker = 0;

				// standard check for ramp up
				if (info->frequency_fo == (info->chips - 1) && info->eff_gs >= info->tune_up) {
					adjustable = 1;
					info->tracker = info->tracker * 10 + 1;
				}

				// seeing dup, hold off.
				if (ms_tdiff(&now, &info->last_dup_time) < MS_MINUTE_1) {
					//adjustable = 0;
					info->tracker = info->tracker * 10 + 2;
				}

				// getting garbage frequency reply, hold off.
				if (ms_tdiff(&now, &info->last_frequency_invalid) < MS_MINUTE_1) {
					adjustable = 0;
					info->tracker = info->tracker * 10 + 3;
				}

				if (info->frequency_fo < 0) {
					info->frequency_fo = info->chips;
					ping_itr = (ping_itr + 1) % 1;
					adjustable = 0;
					info->tracker = info->tracker * 10 + 4;
				} else {
					struct ASIC_INFO *asic = &info->asics[info->frequency_fo];
					if (ping_itr == 0 && asic->frequency != info->frequency_requested) {
						float new_frequency;

						// catching up after reset
						if (asic->frequency < info->frequency_computed) {
							adjustable = 1;
							info->tracker = info->tracker * 10 + 5;
						}

						// startup exception
						if (asic->frequency < info->frequency_start) {
							adjustable = 1;
							info->tracker = info->tracker * 10 + 6;
						}

						// chip speeds aren't matching - special override
						if (!info->frequency_syncd) {
							if (asic->frequency >= info->frequency) {
								adjustable = 0;
								info->tracker = info->tracker * 10 + 7;
							} else if (asic->frequency < info->frequency_asic) {
								adjustable = 1;
								info->tracker = info->tracker * 10 + 8;
							}
						}

						// limit to one adjust per 1 seconds if above peak.
						if (asic->frequency > info->frequency_computed && ms_tdiff(&now, &asic->last_frequency_adjust) < MS_SECOND_1) {
							adjustable = 0;
							info->tracker = info->tracker * 10 + 9;
						}

						if (asic->frequency < info->frequency_requested) {
							if (new_frequency < info->frequency_asic) {
								new_frequency = info->frequency_asic;
							} else {
								new_frequency = asic->frequency + opt_gekko_step_freq;
							}
							if (new_frequency > info->frequency_requested) {
								new_frequency = info->frequency_requested;
							}
							//if (new_frequency < info->frequency_start) {
								//new_frequency = info->frequency_start;
								//new_frequency = asic->frequency + opt_gekko_step_freq;
							//}
							// limit to one adjust per 5 seconds if last set failed.
							if (new_frequency == asic->frequency_set && ms_tdiff(&now, &asic->last_frequency_adjust) < MS_SECOND_5) {
								adjustable = 0;
							}
							if (!adjustable) {
								new_frequency = asic->frequency;
							}
						} else {
							new_frequency = info->frequency_requested;
						}
						if (asic->frequency != new_frequency) {
							cgtime(&info->last_frequency_adjust);
							cgtime(&asic->last_frequency_adjust);
							cgtime(&info->monitor_time);
							asic->frequency_updated = 1;
							frequency_updated = 1;
							if (info->asic_type == BM1387) {
								compac_set_frequency_single(compac, new_frequency, info->frequency_fo);
							} else if (info->asic_type == BM1384) {
								if (info->frequency_fo == 0) {
									compac_set_frequency(compac, new_frequency);
									compac_send_chain_inactive(compac);
								}
							}
						}
					}
				}
			}

			if (!frequency_updated && ms_tdiff(&now, &last_frequency_check) > 20) {
				cgtime(&last_frequency_check);
				for (i = 0; i < info->chips; i++) {
					struct ASIC_INFO *asic = &info->asics[i];
					if (asic->frequency_updated) {
						asic->frequency_updated = 0;
						info->frequency_of = i;
						if (info->asic_type == BM1387) {
							unsigned char buffer[] = {0x44, 0x05, 0x00, 0x0C, 0x00};  // PLL_PARAMETER
							buffer[2] = (0x100 / info->chips) * info->frequency_of;
							compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);
							cgtime(&info->last_frequency_ping);
							cgtime(&asic->last_frequency_ping);
						} else if (info->asic_type == BM1384) {
							unsigned char buffer[] = {0x04, 0x00, 0x04, 0x00};
							buffer[1] = (0x100 / info->chips) * info->frequency_of;
							compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
							cgtime(&info->last_frequency_ping);
							cgtime(&asic->last_frequency_ping);
						}
						break;
					}
				}
			}

			work = get_queued(compac);

			if (work) {
				if (opt_gekko_noboost) {
					work->pool->vmask = 0;
				}
				info->job_id = (info->job_id + 1) % (info->max_job_id - 3);
				old_work = info->work[info->job_id];
				info->work[info->job_id] = work;
				info->active_work[info->job_id] = 1;
				info->vmask = work->pool->vmask;
				if (info->asic_type == BM1387) {
					if (!opt_gekko_noboost && info->vmask) {
						info->task_len = 54 + 32 * (info->midstates - 1);
					} else {
						info->task_len = 54;
					}
				}
				init_task(info);
			} else {
				struct pool *cp;
				cp = current_pool();
				busy_work(info);
				info->busy_work++;
				if (!cp->stratum_active)
					cgtime(&info->last_pool_lost);
				cgtime(&info->monitor_time);
			}

			err = usb_write(compac, (char *)info->task, info->task_len, &read_bytes, C_SENDWORK);
			//dumpbuffer(compac, LOG_WARNING, "TASK.TX", info->task, info->task_len);
			if (err != LIBUSB_SUCCESS) {
				applog(LOG_WARNING,"%d: %s %d - usb failure (%d)", compac->cgminer_id, compac->drv->name, compac->device_id, err);
				info->mining_state = MINER_RESET;
				continue;
			}
			if (read_bytes != info->task_len) {
				if (ms_tdiff(&now, &info->last_write_error) > (5 * 1000)) {
					applog(LOG_WARNING,"%d: %s %d - usb write error [%d:%d]", compac->cgminer_id, compac->drv->name, compac->device_id, read_bytes, info->task_len);
					cgtime(&info->last_write_error);
				}
			}

			//let the usb frame propagate
			cgsleep_ms(1);
			if (old_work) {
				mutex_lock(&info->lock);
				work_completed(compac, old_work);
				mutex_unlock(&info->lock);
				old_work = NULL;
			}

			info->task_ms = (info->task_ms * 9 + ms_tdiff(&now, &info->last_task)) / 10;
			cgtime(&info->last_task);
		} else {
			usleep(sleep_us);
		}
	}
}

static void *compac_handle_rx(void *object, int read_bytes, int path)
{
	struct cgpu_info *compac = (struct cgpu_info *)object;
	struct COMPAC_INFO *info = compac->device_data;
	struct ASIC_INFO *asic;
	int crc_ok, cmd_resp, i;
	struct timeval now;

	cgtime(&now);

	cmd_resp = (info->rx[read_bytes - 1] <= 0x1f && bmcrc(info->rx, 8 * read_bytes - 5) == info->rx[read_bytes - 1]) ? 1 : 0;

	int log_level = (cmd_resp) ? LOG_INFO : LOG_INFO;
	if (path) {
		dumpbuffer(compac, log_level, "RX1", info->rx, read_bytes);
	} else {
		dumpbuffer(compac, log_level, "RX0", info->rx, read_bytes);
	}

	if (cmd_resp && info->rx[0] == 0x80 && info->frequency_of != info->chips) {
		float frequency;
		int frequency_of = info->frequency_of;
		info->frequency_of = info->chips;
		cgtime(&info->last_frequency_report);

		if (info->asic_type == BM1387 && (info->rx[2] == 0 || (info->rx[3] >> 4) == 0 || (info->rx[3] & 0x0f) != 1 || (info->rx[4]) != 0 || (info->rx[5]) != 0)) {
			cgtime(&info->last_frequency_invalid);
			applog(LOG_INFO,"%d: %s %d - invalid frequency report", compac->cgminer_id, compac->drv->name, compac->device_id);
		} else {
			if (info->asic_type == BM1387) {
				frequency = 25.0 * info->rx[1] / (info->rx[2] * (info->rx[3] >> 4) * (info->rx[3] & 0x0f));
			} else if (info->asic_type == BM1384) {
				frequency = (info->rx[1] + 1) * 6.25 / (1 + info->rx[2] & 0x0f) * pow(2, (3 - info->rx[3])) + ((info->rx[2] >> 4) * 6.25);
			}

			if (frequency_of != info->chips) {
				asic = &info->asics[frequency_of];
				cgtime(&asic->last_frequency_reply);
				if (frequency != asic->frequency) {
					bool syncd = 1;
					if (frequency < asic->frequency && frequency != info->frequency_requested) {
						applog(LOG_INFO,"%d: %s %d - chip[%d] reported frequency at %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, info->frequency_of, frequency);
					} else {
						applog(LOG_INFO,"%d: %s %d - chip[%d] reported new frequency of %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, info->frequency_of, frequency);
					}
					asic->frequency = frequency;
					if (asic->frequency == asic->frequency_set) {
						asic->frequency_attempt = 0;
					}
					for (i = 1; i < info->chips; i++) {
						if (info->asics[i].frequency != info->asics[i - 1].frequency) {
							syncd = 0;
						}
					}
					if (info->frequency_syncd != syncd) {
						info->frequency_syncd = syncd;
						applog(LOG_INFO,"%d: %s %d - syncd [%d]", compac->cgminer_id, compac->drv->name, compac->device_id, syncd);
					}
				} else {
					applog(LOG_INFO,"%d: %s %d - chip[%d] reported frequency of %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, info->frequency_of, frequency);
				}
			} else {
				//applog(LOG_INFO,"%d: %s %d - [-1] reported frequency of %.2fMHz", compac->cgminer_id, compac->drv->name, compac->device_id, frequency);
				//if (frequency != info->frequency) {
				//	info->frequency = frequency;
				//}
			}
			compac_update_rates(compac);
		}
	}

	switch (info->mining_state) {
		case MINER_CHIP_COUNT:
		case MINER_CHIP_COUNT_XX:
			if (cmd_resp && info->rx[0] == 0x13) {
				struct ASIC_INFO *asic = &info->asics[info->chips];
				memset(asic, 0, sizeof(struct ASIC_INFO));
				asic->frequency = info->frequency_default;
				asic->frequency_attempt = 0;
				asic->last_frequency_ping = (struct timeval){0};
				asic->last_frequency_reply = (struct timeval){0};
				cgtime(&asic->last_nonce);
				info->chips++;
				info->mining_state = MINER_CHIP_COUNT_XX;
				compac_update_rates(compac);
			}
			break;
		case MINER_OPEN_CORE:
			if ((info->rx[0] == 0x72 && info->rx[1] == 0x03 && info->rx[2] == 0xEA && info->rx[3] == 0x83) ||
				(info->rx[0] == 0xE1 && info->rx[1] == 0x6B && info->rx[2] == 0xF8 && info->rx[3] == 0x09)) {
				//open core nonces = healthy chips.
				info->zero_check++;
			}
			break;
		case MINER_MINING:
			if (!cmd_resp) {
				sched_yield();
				mutex_lock(&info->lock);
				info->hashes += compac_check_nonce(compac);
				mutex_unlock(&info->lock);
			}
			break;
		default:
			break;
	}

/*
     FIXME: This function is defined to return void*, but did not return
     anything at all.  It simply flowed off the end, which is undefined
     behavior if the return value is used by the caller.  See C standard,
     6.9.1 paragraph 12.
     
     http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
     
     Clang warns on it, but this is the kind of thing that should be an error,
     not a warning.
     
     It's being passed as a function pointer parameter to thr_info_create,
     which is defined to take a "void *(*) (void *)",  Function pointer
     syntax is always a bit awkward, but if I'm interpreting that correctly,
     it takes a pointer to a function that takes a void pointer and returns a
     void pointer, which means this function's signature is correct, and not
     returning a value is incorrect.  I have no idea what the correct value
     to return would be, but invoking undefined behavior by not even having a
     return statement has got to be worse.  It may just be silently doing the
     wrong thing.  Returning NULL should force the issue and then it can be
     fixed to return whatever is the correct pointer.
     */
    return NULL;
}

static void *compac_listen(void *object)
{
	struct cgpu_info *compac = (struct cgpu_info *)object;
	struct COMPAC_INFO *info = compac->device_data;
	struct timeval now;
	unsigned char rx[BUFFER_MAX];
	unsigned char *prx = rx;
	int read_bytes, cmd_resp, i, j, pos, rx_bytes;
	uint32_t err = 0;

	memset(rx, 0, BUFFER_MAX);
	memset(info->rx, 0, BUFFER_MAX);

	pos = 0;
	rx_bytes = 0;

	while (info->mining_state != MINER_SHUTDOWN) {

		cgtime(&now);

		if (info->mining_state == MINER_CHIP_COUNT) {
			if (info->asic_type == BM1387) {
				unsigned char buffer[] = {0x54, 0x05, 0x00, 0x00, 0x00};
				compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 8);
			} else if (info->asic_type == BM1384) {
				unsigned char buffer[] = {0x84, 0x00, 0x00, 0x00};
				compac_send(compac, (char *)buffer, sizeof(buffer), 8 * sizeof(buffer) - 5);
			}
			err = usb_read_timeout(compac, rx, BUFFER_MAX, &read_bytes, 1000, C_GETRESULTS);
			dumpbuffer(compac, LOG_INFO, "CMD.RX", rx, read_bytes);

			rx_bytes = read_bytes;
			info->mining_state = MINER_CHIP_COUNT_XX;
		} else {

			if (rx_bytes >= (BUFFER_MAX - info->rx_len)) {
				applog(LOG_INFO, "%d: %s %d - Buffer not useful, dumping (b) %d bytes", compac->cgminer_id, compac->drv->name, compac->device_id, rx_bytes);
				dumpbuffer(compac, LOG_INFO, "NO-CRC-RX", rx, rx_bytes);
				pos = 0;
				rx_bytes = 0;
			}

			err = usb_read_timeout(compac, &rx[pos], info->rx_len, &read_bytes, 20, C_GETRESULTS);
			rx_bytes += read_bytes;
			pos = rx_bytes;
		}

		if (read_bytes > 0) {

			if (rx_bytes < info->rx_len) {
				applog(LOG_INFO, "%d: %s %d - Buffered %d bytes", compac->cgminer_id, compac->drv->name, compac->device_id, rx_bytes);
				dumpbuffer(compac, LOG_INFO, "Partial-RX", rx, rx_bytes);
				continue;
			}

			if (rx_bytes >= info->rx_len)
				cmd_resp = (rx[read_bytes - 1] <= 0x1f && bmcrc(prx, 8 * read_bytes - 5) == rx[read_bytes - 1]) ? 1 : 0;

			if (info->mining_state == MINER_CHIP_COUNT_XX || cmd_resp) {
				if (rx_bytes % info->rx_len == 2) {
					// fix up trial 1
					int shift = 0;
					int extra = 0;
					for (i = 0; i < (rx_bytes - shift); i++) {
						if (rx[i] == 0x01) {
							shift = 2;
							extra = rx[i + 1];
						}
						rx[i] = rx[i + shift];
					}
					rx_bytes -= shift;
					pos = rx_bytes;
					applog(LOG_INFO, "%d: %s %d - Extra Data - 0x01 0x%02x", compac->cgminer_id, compac->drv->name, compac->device_id, extra & 0xff);
				}
			}

			if (rx_bytes >= info->rx_len) {
				bool crc_match = false;
				int new_pos = 0;
				for (i = 0; i <= (int)(rx_bytes - info->rx_len); i++) {
					bool rx_okay = false;

					if (bmcrc(&rx[i], 8 * info->rx_len - 5) == (rx[i + info->rx_len - 1] & 0x1f)) {
						// crc checksum is good
						crc_match = true;
						rx_okay = true;
					}
					if (info->asic_type == BM1384 && rx[i + info->rx_len - 1] >= 0x80 && rx[i + info->rx_len - 1] <= 0x9f) {
						// bm1384 nonce
						rx_okay = true;
					}
					if (rx_okay) {
						memcpy(info->rx, &rx[i], info->rx_len);
						compac_handle_rx(compac, info->rx_len, 0);
						new_pos = i + info->rx_len;
					}
				}
				if (new_pos > rx_bytes) {
					rx_bytes = 0;
					pos = 0;
				} else if (new_pos > 0) {
					for (i = new_pos; i < rx_bytes; i++) {
						rx[i - new_pos] = rx[i];
					}
					rx_bytes -= new_pos;
					pos = rx_bytes;
				}
			}

/*
			if (bmcrc(&rx[rx_bytes - info->rx_len], 8 * info->rx_len - 5) != info->rx[rx_bytes - 1]) {
				int n_read_bytes;
				pos = rx_bytes;
				err = usb_read_timeout(compac, &rx[pos], BUFFER_MAX - pos, &n_read_bytes, 5, C_GETRESULTS);
				rx_bytes += n_read_bytes;

				if (rx_bytes % info->rx_len != 0 && rx_bytes >= info->rx_len) {
					int extra_bytes = rx_bytes % info->rx_len;
					for (i = extra_bytes; i < rx_bytes; i++) {
						rx[i - extra_bytes] = rx[i];
					}
					rx_bytes -= extra_bytes;
					applog(LOG_INFO, "%d: %s %d - Fixing buffer alignment, dumping initial %d bytes", compac->cgminer_id, compac->drv->name, compac->device_id, extra_bytes);
				}
			}
			if (rx_bytes % info->rx_len == 0) {
				for (i = 0; i < rx_bytes; i += info->rx_len) {
					memcpy(info->rx, &rx[i], info->rx_len);
					compac_handle_rx(compac, info->rx_len, 1);
				}
				pos = 0;
				rx_bytes = 0;
			}
*/
		} else {

			if (rx_bytes > 0) {
				applog(LOG_INFO, "%d: %s %d - Second read, no data dumping (c) %d bytes", compac->cgminer_id, compac->drv->name, compac->device_id, rx_bytes);
				dumpbuffer(compac, LOG_INFO, "EXTRA-RX", rx, rx_bytes);
			}

			pos = 0;
			rx_bytes = 0;

			// RX line is idle, let's squeeze in a command to the micro if needed.
			if (info->asic_type == BM1387) {
				if (ms_tdiff(&now, &info->last_micro_ping) > MS_SECOND_5 && ms_tdiff(&now, &info->last_task) > 1 && ms_tdiff(&now, &info->last_task) < 3) {
					compac_micro_send(compac, M1_GET_TEMP, 0x00, 0x00);
					cgtime(&info->last_micro_ping);
				}
			}

			switch (info->mining_state) {
				case MINER_CHIP_COUNT_XX:
					if (info->chips < info->expected_chips) {
						applog(LOG_INFO, "%d: %s %d - found %d/%d chip(s)", compac->cgminer_id, compac->drv->name, compac->device_id, info->chips, info->expected_chips);
						info->mining_state = MINER_RESET;
					} else {
						applog(LOG_INFO, "%d: %s %d - found %d chip(s)", compac->cgminer_id, compac->drv->name, compac->device_id, info->chips);
						if (info->chips > 0) {
							info->mining_state = MINER_CHIP_COUNT_OK;
							mutex_lock(&static_lock);
							(*init_count) = 0;
							info->init_count = 0;
							mutex_unlock(&static_lock);
						} else {
							info->mining_state = MINER_RESET;
						}
					}
					break;
				default:
					break;
			}
		}
	}

/*
     FIXME: This function is defined to return void*, but did not return
     anything at all.  It simply flowed off the end, which is undefined
     behavior if the return value is used by the caller.  See C standard,
     6.9.1 paragraph 12.
     
     http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
     
     Clang warns on it, but this is the kind of thing that should be an error,
     not a warning.
     
     It's being passed as a function pointer parameter to thr_info_create,
     which is defined to take a "void *(*) (void *)",  Function pointer
     syntax is always a bit awkward, but if I'm interpreting that correctly,
     it takes a pointer to a function that takes a void pointer and returns a
     void pointer, which means this function's signature is correct, and not
     returning a value is incorrect.  I have no idea what the correct value
     to return would be, but invoking undefined behavior by not even having a
     return statement has got to be worse.  It may just be silently doing the
     wrong thing.  Returning NULL should force the issue and then it can be
     fixed to return whatever is the correct pointer.
     */
    return NULL;

}

static bool compac_init(struct thr_info *thr)
{
	int i;
	struct cgpu_info *compac = thr->cgpu;
	struct COMPAC_INFO *info = compac->device_data;

	info->boosted = false;
	info->prev_nonce = 0;
	info->fail_count = 0;
	info->busy_work = 0;
	info->log_wide = (opt_widescreen) ? LOG_WARNING : LOG_INFO;
	info->plateau_reset = 0;
	info->low_eff_resets = 0;
	info->frequency = 200;
	info->frequency_default = 200;
	info->frequency_fail_high = 0;
	info->frequency_fail_low = 999;
	info->frequency_fo = info->chips;
	info->scanhash_ms = 10;

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
	cgtime(&info->last_computed_increase);
	cgtime(&info->last_low_eff_reset);
	info->last_frequency_invalid = (struct timeval){0};
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
			info->frequency_start = opt_gekko_start_freq;
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
			info->frequency_start = opt_gekko_start_freq;
			break;
		case IDENT_GSI:
			info->frequency_requested = opt_gekko_gsi_freq;
			if (opt_gekko_start_freq == 100) {
				info->frequency_start = 550;
			} else {
				info->frequency_start = opt_gekko_start_freq;
			}
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
		pthread_mutex_init(&info->rlock, NULL);

		if (thr_info_create(&(info->rthr), NULL, compac_listen, (void *)compac)) {
			applog(LOG_ERR, "%d: %s %d - read thread create failed", compac->cgminer_id, compac->drv->name, compac->device_id);
			return false;
		} else {
			applog(LOG_INFO, "%d: %s %d - read thread created", compac->cgminer_id, compac->drv->name, compac->device_id);
		}
		pthread_detach(info->rthr.pth);

		cgsleep_ms(100);

		if (thr_info_create(&(info->wthr), NULL, compac_mine, (void *)compac)) {
			applog(LOG_ERR, "%d: %s %d - write thread create failed", compac->cgminer_id, compac->drv->name, compac->device_id);
			return false;
		} else {
			applog(LOG_INFO, "%d: %s %d - write thread created", compac->cgminer_id, compac->drv->name, compac->device_id);
		}

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
	uint64_t xhashes = 0;
	uint32_t sleep_us = bound(info->scanhash_ms * 1000, 100, MS_SECOND_1 / 2 * 1000);

	if (info->chips == 0)
		cgsleep_ms(10);

	if (compac->usbinfo.nodev)
		return -1;

//	thread_yield();
	sched_yield();
	cgtime(&now);

	switch (info->mining_state) {
		case MINER_INIT:
			cgsleep_ms(50);
			compac_flush_buffer(compac);
			info->chips = 0;
			info->ramping = 0;
			info->frequency_syncd = 1;
			if (info->frequency_start > info->frequency_requested) {
				info->frequency_start = info->frequency_requested;
			}
			info->mining_state = MINER_CHIP_COUNT;
			return 0;
			break;
		case MINER_CHIP_COUNT:
			if (ms_tdiff(&now, &info->last_reset) > MS_SECOND_5) {
				applog(LOG_INFO, "%d: %s %d - found 0 chip(s)", compac->cgminer_id, compac->drv->name, compac->device_id);
				info->mining_state = MINER_RESET;
				return 0;
			}
			cgsleep_ms(10);
			break;
		case MINER_CHIP_COUNT_OK:
			cgsleep_ms(50);
			//compac_set_frequency(compac, info->frequency_start);
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
			applog(LOG_INFO, "%d: %s %d - start work", compac->cgminer_id, compac->drv->name, compac->device_id);
			cgtime(&info->start_time);
			cgtime(&info->monitor_time);
			cgtime(&info->last_frequency_adjust);
			info->last_dup_time = (struct timeval){0};
			cgtime(&info->last_frequency_report);
			cgtime(&info->last_micro_ping);
			cgtime(&info->last_nonce);
			compac_flush_buffer(compac);
			compac_update_rates(compac);
			info->update_work = 1;
			info->mining_state = MINER_MINING;
			return 0;
			break;
		case MINER_MINING:
			break;
		case MINER_RESET:
			compac_flush_work(compac);
			if (info->asic_type == BM1387) {
				compac_toggle_reset(compac);
			} else if (info->asic_type == BM1384) {
				compac_set_frequency(compac, info->frequency_default);
				//compac_send_chain_inactive(compac);
			}
			compac_prepare(thr);

			info->fail_count++;
			info->mining_state = MINER_INIT;
			cgtime(&info->last_reset);
			return 0;
			break;
		case MINER_MINING_DUPS:
			info->mining_state = MINER_MINING;
			break;
		default:
			break;
	}

	mutex_lock(&info->lock);
	hashes = info->hashes;
	xhashes = info->xhashes;
	info->hashes = 0;
	info->xhashes = 0;
	mutex_unlock(&info->lock);

	cgsleep_ms(1);
	return xhashes * 0xffffffffull;
	//return hashes;
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

	if (opt_gekko_gsc_detect || opt_gekko_gsd_detect || opt_gekko_gse_detect || opt_gekko_gsh_detect || opt_gekko_gsi_detect) {
		exclude_me  = (info->ident == IDENT_BSC && !opt_gekko_gsc_detect);
		exclude_me |= (info->ident == IDENT_GSC && !opt_gekko_gsc_detect);
		exclude_me |= (info->ident == IDENT_BSD && !opt_gekko_gsd_detect);
		exclude_me |= (info->ident == IDENT_GSD && !opt_gekko_gsd_detect);
		exclude_me |= (info->ident == IDENT_BSE && !opt_gekko_gse_detect);
		exclude_me |= (info->ident == IDENT_GSE && !opt_gekko_gse_detect);
		exclude_me |= (info->ident == IDENT_GSH && !opt_gekko_gsh_detect);
		exclude_me |= (info->ident == IDENT_GSI && !opt_gekko_gsi_detect);
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

			usb_transfer_data(compac, CP210X_TYPE_OUT, CP210X_REQUEST_IFC_ENABLE, CP210X_VALUE_UART_ENABLE, info->interface, NULL, 0, C_ENABLE_UART);
			usb_transfer_data(compac, CP210X_TYPE_OUT, CP210X_REQUEST_DATA, CP210X_VALUE_DATA, info->interface, NULL, 0, C_SETDATA);
			usb_transfer_data(compac, CP210X_TYPE_OUT, CP210X_REQUEST_BAUD, 0, info->interface, &baudrate, sizeof (baudrate), C_SETBAUD);
			usb_transfer_data(compac, CP210X_TYPE_OUT, CP210X_SET_LINE_CTL, bits, info->interface, NULL, 0, C_SETPARITY);
			break;
		case IDENT_GSH:
			info->asic_type = BM1387;
			info->expected_chips = 2;
			break;
		case IDENT_GSI:
			info->asic_type = BM1387;
			info->expected_chips = 12;
			break;
		default:
			quit(1, "%d: %s compac_detect_one() invalid %s ident=%d",
				compac->cgminer_id, compac->drv->dname, compac->drv->dname, info->ident);
	}

	switch (info->asic_type) {
		case BM1384:
			info->rx_len = 5;
			info->task_len = 64;
			info->cores = 55;
			info->max_job_id = 0x1f;
			break;
		case BM1387:
			info->rx_len = 7;
			info->task_len = 54;
			info->cores = 114;
			info->max_job_id = 0x7f;
			info->midstates = (opt_gekko_lowboost) ? 2 : 4;
			compac_toggle_reset(compac);
			break;
		default:
			break;
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
	int device = (compac->usbinfo.bus_number * 0xff + compac->usbinfo.device_address) % 0xffff;

	mutex_lock(&static_lock);
	init_count = &dev_init_count[device];
	(*init_count)++;
	info->init_count = (*init_count);
	mutex_unlock(&static_lock);

	if (info->init_count == 1) {
		applog(LOG_WARNING, "%d: %s %d - %s (%s)", compac->cgminer_id, compac->drv->name, compac->device_id, compac->usbdev->prod_string, compac->unique_id);
	} else {
		applog(LOG_INFO, "%d: %s %d - init_count %d", compac->cgminer_id, compac->drv->name, compac->device_id, info->init_count);
	}

	info->thr = thr;
	info->bauddiv = 0x19; // 115200
	//info->bauddiv = 0x0D; // 214286
	//info->bauddiv = 0x07; // 375000

	//Sanity check and abort to prevent miner thread from being created.
	if (info->asic_type == BM1387) {
		// Ping Micro
		info->micro_found = 0;
/*
		if (info->asic_type == BM1387) {
			info->vcore = bound(opt_gekko_gsh_vcore, 300, 810);
			info->micro_found = 1;
			if (!compac_micro_send(compac, M1_GET_TEMP, 0x00, 0x00)) {
				info->micro_found = 0;
				applog(LOG_INFO, "%d: %s %d - micro not found : dummy mode", compac->cgminer_id, compac->drv->name, compac->device_id);
			} else {
				uint8_t vcc = (info->vcore / 1000.0 - 0.3) / 0.002;
				applog(LOG_INFO, "%d: %s %d - requesting vcore of %dmV (%x)", compac->cgminer_id, compac->drv->name, compac->device_id, info->vcore, vcc);
				compac_micro_send(compac, M2_SET_VCORE, 0x00, vcc);   // Default 400mV
			}
		}
*/

	}

	if (info->init_count != 0 && info->init_count % 5 == 0) {
		applog(LOG_INFO, "%d: %s %d - forcing usb_nodev()", compac->cgminer_id, compac->drv->name, compac->device_id);
		usb_nodev(compac);
	} else if (info->init_count > 1) {
		if (info->init_count > 10) {
			compac->deven = DEV_DISABLED;
		} else {
			cgsleep_ms(MS_SECOND_5);
		}
	}

	return true;
}

static void compac_statline(char *buf, size_t bufsiz, struct cgpu_info *compac)
{
	struct COMPAC_INFO *info = compac->device_data;
	struct timeval now;
	int i;

	char ab[2];
	char asic_stat[64];
	char asic_statline[512];
	char ms_stat[64];
	char eff_stat[64];
	uint32_t len = 0;

	memset(asic_statline, 0, 512);
	memset(asic_stat, 0, 64);
	memset(ms_stat, 0, 64);
	memset(eff_stat, 0, 64);

	if (info->chips == 0) {
		if (info->init_count > 1) {
			sprintf(asic_statline, "found 0 chip(s)");
		}

		for (i = strlen(asic_statline); i < stat_len + 15; i++)
			asic_statline[i] = ' ';

		tailsprintf(buf, bufsiz, "%s", asic_statline);
		return;
	}

	ab[0] = (info->boosted) ? '+' : 0;
	ab[1] = 0;

	if (info->chips > chip_max)
		chip_max = info->chips;

	cgtime(&now);

	if (opt_widescreen) {
		asic_stat[0] = '[';

		for (i = 1; i <= info->chips; i++) {
			struct ASIC_INFO *asic = &info->asics[i - 1];

			switch (asic->state) {
				case ASIC_HEALTHY:
					asic_stat[i] = 'o';
					break;
				case ASIC_HALFDEAD:
					asic_stat[i] = '-';
					break;
				case ASIC_ALMOST_DEAD:
					asic_stat[i] = '!';
					break;
				case ASIC_DEAD:
					asic_stat[i] = 'x';
					break;
			}

		}
		asic_stat[info->chips + 1] = ']';
		for (i = 1; i <= (chip_max - info->chips) + 1; i++)
			asic_stat[info->chips + 1 + i] = ' ';
	}

	sprintf(ms_stat, "(%.0f:%.0f)", info->task_ms, info->fullscan_ms);

	uint8_t wuc = (ms_tdiff(&now, &info->last_wu_increase) > MS_MINUTE_1) ? 32 : 94;

	if (info->eff_gs >= 99.9 && info->eff_wu >= 98.9) {
		sprintf(eff_stat, "|  100%% WU:100%%");
	} else if (info->eff_gs >= 99.9) {
		sprintf(eff_stat, "|  100%% WU:%c%2.0f%%", wuc, info->eff_wu);
	} else if (info->eff_wu >= 98.9) {
		sprintf(eff_stat, "| %4.1f%% WU:100%%", info->eff_gs);
	} else {
		sprintf(eff_stat, "| %4.1f%% WU:%c%2.0f%%", info->eff_gs, wuc, info->eff_wu);
	}

	if (info->asic_type == BM1387) {
		if (info->micro_found) {
			sprintf(asic_statline, "BM1387:%02d%-1s %.2fMHz T:%.0f P:%.0f %s %.0fF", info->chips, ab, info->frequency, info->frequency_requested, info->frequency_computed, ms_stat, info->micro_temp);
		} else {
			if (opt_widescreen) {
				sprintf(asic_statline, "BM1387:%02d%-1s %.0f/%.0f/%3.0f %s %s", info->chips, ab, info->frequency, info->frequency_requested, info->frequency_computed, ms_stat, asic_stat);
			} else {
				sprintf(asic_statline, "BM1387:%02d%-1s %.2fMHz T:%-3.0f P:%-3.0f %s %s", info->chips, ab, info->frequency, info->frequency_requested, info->frequency_computed, ms_stat, asic_stat);
			}
		}
	} else {
		if (opt_widescreen) {
			sprintf(asic_statline, "BM1384:%02d  %.0f/%.0f/%.0f %s %s", info->chips, info->frequency, info->frequency_requested, info->frequency_computed, ms_stat, asic_stat);
		} else {
			sprintf(asic_statline, "BM1384:%02d  %.2fMHz T:%-3.0f P:%-3.0f %s %s", info->chips, info->frequency, info->frequency_requested, info->frequency_computed, ms_stat, asic_stat);
		}
	}

	len = strlen(asic_statline);
	if (len > stat_len || opt_widescreen != last_widescreen) {
		mutex_lock(&static_lock);
		stat_len = len;
		last_widescreen = opt_widescreen;
		mutex_unlock(&static_lock);
	}

	for (i = len; i < stat_len; i++)
		asic_statline[i] = ' ';

	strcat(asic_statline, eff_stat);
	asic_statline[63] = 0;

	tailsprintf(buf, bufsiz, "%s", asic_statline);
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
	applog(LOG_INFO, "%d: %s %d - shutting down", compac->cgminer_id, compac->drv->name, compac->device_id);
	if (!compac->usbinfo.nodev) {
		if (info->asic_type == BM1387) {
			compac_micro_send(compac, M2_SET_VCORE, 0x00, 0x00);   // 300mV
			compac_toggle_reset(compac);
		} else if (info->asic_type == BM1384 && info->frequency != info->frequency_default) {
			float frequency = info->frequency - 25;
			while (frequency > info->frequency_default) {
				compac_set_frequency(compac, frequency);
				frequency -= 25;
				cgsleep_ms(100);
			}
			compac_set_frequency(compac, info->frequency_default);
			compac_send_chain_inactive(compac);
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
