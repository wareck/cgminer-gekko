/*
 * Copyright 2012-2025 Andrew Smith
 * Copyright 2013-2015 Con Kolivas <kernel@kolivas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 3 of the License.
 * See COPYING for more details.
 */

#include "config.h"

#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>

#include "logging.h"
#include "miner.h"
#include "usbutils.h"

static pthread_mutex_t cgusb_lock;
static pthread_mutex_t cgusbres_lock;
static cglock_t cgusb_fd_lock;
static cgtimer_t usb11_cgt;

// allow debugging to ignore timeouts
int libusb_ign_tmo = 0;

#define NODEV(err) ((err) != LIBUSB_SUCCESS && (err) != LIBUSB_ERROR_TIMEOUT)

#define NOCONTROLDEV(err) ((err) < 0 && NODEV(err))

/*
 * WARNING - these assume DEVLOCK(cgpu, pstate) is called first and
 *  DEVUNLOCK(cgpu, pstate) in called in the same function with the same pstate
 *  given to DEVLOCK.
 *  You must call DEVUNLOCK(cgpu, pstate) before exiting the function or it will leave
 *  the thread Cancelability unrestored
 */
#define DEVWLOCK(cgpu, _pth_state) do { \
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &_pth_state); \
			cg_wlock(&cgpu->usbinfo.devlock); \
			} while (0)

#define DEVWUNLOCK(cgpu, _pth_state) do { \
			cg_wunlock(&cgpu->usbinfo.devlock); \
			pthread_setcancelstate(_pth_state, NULL); \
			} while (0)

#define DEVRLOCK(cgpu, _pth_state) do { \
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &_pth_state); \
			cg_rlock(&cgpu->usbinfo.devlock); \
			} while (0)

#define DEVRUNLOCK(cgpu, _pth_state) do { \
			cg_runlock(&cgpu->usbinfo.devlock); \
			pthread_setcancelstate(_pth_state, NULL); \
			} while (0)

#define USB_CONFIG 1

#define BITFURY_TIMEOUT_MS 999
#define DRILLBIT_TIMEOUT_MS 999
#define ICARUS_TIMEOUT_MS 999

// There is no windows version
#define ANT_S1_TIMEOUT_MS 200
#define ANT_S3_TIMEOUT_MS 200

#ifdef WIN32
#define BFLSC_TIMEOUT_MS 999
#define BITFORCE_TIMEOUT_MS 999
#define MODMINER_TIMEOUT_MS 999
#define AVALON_TIMEOUT_MS 999
#define AVALON4_TIMEOUT_MS 999
#define AVALON7_TIMEOUT_MS 999
#define AVALON8_TIMEOUT_MS 999
#define AVALON9_TIMEOUT_MS 999
#define AVALONLC3_TIMEOUT_MS 999
#define AVALONM_TIMEOUT_MS 999
#define KLONDIKE_TIMEOUT_MS 999
#define COINTERRA_TIMEOUT_MS 999
#define HASHFAST_TIMEOUT_MS 999
#define HASHRATIO_TIMEOUT_MS 999
#define BLOCKERUPTER_TIMEOUT_MS 999
#define COMPAC_TIMEOUT_MS 999
#define FLOW_TIMEOUT_MS 999

/* The safety timeout we use, cancelling async transfers on windows that fail
 * to timeout on their own. */
#define WIN_CALLBACK_EXTRA 40
#define WIN_WRITE_CBEXTRA 5000
#else
#define BFLSC_TIMEOUT_MS 300
#define BITFORCE_TIMEOUT_MS 200
#define MODMINER_TIMEOUT_MS 100
#define AVALON_TIMEOUT_MS 200
#define AVALON4_TIMEOUT_MS 200
#define AVALON7_TIMEOUT_MS 200
#define AVALON8_TIMEOUT_MS 200
#define AVALON9_TIMEOUT_MS 200
#define AVALONLC3_TIMEOUT_MS 200
#define AVALONM_TIMEOUT_MS 300
#define KLONDIKE_TIMEOUT_MS 200
#define COINTERRA_TIMEOUT_MS 200
#define HASHFAST_TIMEOUT_MS 500
#define HASHRATIO_TIMEOUT_MS 200
#define BLOCKERUPTER_TIMEOUT_MS 300
#define COMPAC_TIMEOUT_MS 300
#define FLOW_TIMEOUT_MS 300
#endif

#define USB_EPS(_intx, _epinfosx) { \
		.interface = _intx, \
		.ctrl_transfer = _intx, \
		.epinfo_count = ARRAY_SIZE(_epinfosx), \
		.epinfos = _epinfosx \
	}

#define USB_EPS_CTRL(_inty, _ctrlinty, _epinfosy) { \
		.interface = _inty, \
		.ctrl_transfer = _ctrlinty, \
		.epinfo_count = ARRAY_SIZE(_epinfosy), \
		.epinfos = _epinfosy \
	}

/* Linked list of all async transfers in progress. Protected by cgusb_fd_lock.
 * This allows us to not stop the usb polling thread till all are complete, and
 * to find cancellable transfers. */
static struct list_head ut_list;

#ifdef USE_BFLSC
static struct usb_epinfo bflsc_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	512,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	512,	EPO(2), 0, 0 }
};

static struct usb_intinfo bflsc_ints[] = {
	USB_EPS(0, bflsc_epinfos)
};
#endif

#ifdef USE_BITFORCE
// N.B. transfer size is 512 with USB2.0, but only 64 with USB1.1
static struct usb_epinfo bfl_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0, 0 }
};

static struct usb_intinfo bfl_ints[] = {
	USB_EPS(0, bfl_epinfos)
};
#endif

#ifdef USE_BITFURY
static struct usb_epinfo bfu0_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT,	8,	EPI(2), 0, 0 }
};

static struct usb_epinfo bfu1_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	16,	EPI(3), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	16,	EPO(4), 0, 0 }
};

/* Default to interface 1 */
static struct usb_intinfo bfu_ints[] = {
	USB_EPS(1,  bfu1_epinfos),
	USB_EPS(0,  bfu0_epinfos)
};

static struct usb_epinfo bxf0_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT,	8,	EPI(1), 0, 0 }
};

static struct usb_epinfo bxf1_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(2), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0, 0 }
};

static struct usb_intinfo bxf_ints[] = {
	USB_EPS(1,  bxf1_epinfos),
	USB_EPS(0,  bxf0_epinfos)
};

static struct usb_epinfo nfu_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT,	64,	EPO(1), 0, 0 },
};

static struct usb_intinfo nfu_ints[] = {
	USB_EPS(0, nfu_epinfos)
};

static struct usb_epinfo bxm_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	512,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	512,	EPO(2), 0, 0 }
};

static struct usb_intinfo bxm_ints[] = {
	USB_EPS(0, bxm_epinfos)
};
#endif

#ifdef USE_BLOCKERUPTER
// BlockErupter Device
static struct usb_epinfo bet_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0, 0 }
};

static struct usb_intinfo bet_ints[] = {
	USB_EPS(0, bet_epinfos)
};
#endif

#ifdef USE_FLOW
// CP2102N
static struct usb_epinfo flow1_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(2), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0, 0 }
};

static struct usb_intinfo flow1_ints[] = {
	USB_EPS(0, flow1_epinfos)
};
#endif

#ifdef USE_GEKKO
// CP210X Devices
static struct usb_epinfo gek1_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0, 0 }
};

static struct usb_intinfo gek1_ints[] = {
	USB_EPS(0, gek1_epinfos)
};

// FTDI Devices
static struct usb_epinfo gek2_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0, 0 }
};
static struct usb_intinfo gek2_ints[] = {
	USB_EPS(0, gek2_epinfos)
};

// CP2105 dual ep0
static struct usb_epinfo gek3a_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0, 0 }
};

// CP2105 dual ep1
static struct usb_epinfo gek3b_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	32,	EPI(2), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	32,	EPO(2), 0, 0 }
};

static struct usb_intinfo gek3_ints[] = {
	USB_EPS(0, gek3a_epinfos),
	USB_EPS(1, gek3b_epinfos)
};

// BF Devices
static struct usb_epinfo gekbf1_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT,	64,	EPO(1), 0, 0 }
};
static struct usb_intinfo gek4_ints[] = {
	USB_EPS(0, gekbf1_epinfos)
};

#endif

#ifdef USE_DRILLBIT
// Drillbit Bitfury devices
static struct usb_epinfo drillbit_int_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT,	8,	EPI(3), 0, 0 }
};

static struct usb_epinfo drillbit_bulk_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	16,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	16,	EPO(2), 0, 0 },
};

/* Default to interface 1 */
static struct usb_intinfo drillbit_ints[] = {
	USB_EPS(1,  drillbit_bulk_epinfos),
	USB_EPS(0,  drillbit_int_epinfos)
};
#endif

#ifdef USE_HASHFAST
#include "driver-hashfast.h"

static struct usb_epinfo hfa0_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT,	8,	EPI(3), 0, 0 }
};

static struct usb_epinfo hfa1_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0, 0 }
};

/* Default to interface 1 */
static struct usb_intinfo hfa_ints[] = {
	USB_EPS(1,  hfa1_epinfos),
	USB_EPS(0,  hfa0_epinfos)
};
#endif

#ifdef USE_HASHRATIO
static struct usb_epinfo hro_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0, 0 }
};

static struct usb_intinfo hro_ints[] = {
	USB_EPS(0, hro_epinfos)
};
#endif

#ifdef USE_MODMINER
static struct usb_epinfo mmq_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(3), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(3), 0, 0 }
};

static struct usb_intinfo mmq_ints[] = {
	USB_EPS(1, mmq_epinfos)
};
#endif

#ifdef USE_AVALON
static struct usb_epinfo ava_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0, 0 }
};

static struct usb_intinfo ava_ints[] = {
	USB_EPS(0, ava_epinfos)
};
#endif

#ifdef USE_AVALON2
static struct usb_epinfo ava2_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(3), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0, 0 }
};

static struct usb_intinfo ava2_ints[] = {
	USB_EPS(0, ava2_epinfos)
};
#endif

#ifdef USE_AVALON4
static struct usb_epinfo ava4_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0, 0 }
};

static struct usb_intinfo ava4_ints[] = {
	USB_EPS(1, ava4_epinfos)
};
#endif
#ifdef USE_AVALON7
static struct usb_epinfo ava7_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0, 0 }
};

static struct usb_intinfo ava7_ints[] = {
	USB_EPS(1, ava7_epinfos)
};
#endif
#ifdef USE_AVALON8
static struct usb_epinfo ava8_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0, 0 }
};

static struct usb_intinfo ava8_ints[] = {
	USB_EPS(1, ava8_epinfos)
};
#endif
#ifdef USE_AVALON9
static struct usb_epinfo ava9_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0, 0 }
};

static struct usb_intinfo ava9_ints[] = {
	USB_EPS(1, ava9_epinfos)
};
#endif
#ifdef USE_AVALONLC3
static struct usb_epinfo avalc3_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0, 0 }
};

static struct usb_intinfo avalc3_ints[] = {
	USB_EPS(1, avalc3_epinfos)
};
#endif
#ifdef USE_AVALON_MINER
static struct usb_epinfo avam_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT,	40,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT,	40,	EPO(1), 0, 0 }
};

static struct usb_intinfo avam_ints[] = {
	USB_EPS(1, avam_epinfos)
};
static struct usb_epinfo av3u_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT,	40,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT,	40,	EPO(1), 0, 0 }
};

static struct usb_intinfo av3u_ints[] = {
	USB_EPS(0, av3u_epinfos)
};
#endif
#ifdef USE_KLONDIKE
static struct usb_epinfo kln_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0, 0 }
};

static struct usb_intinfo kln_ints[] = {
	USB_EPS(0, kln_epinfos)
};

static struct usb_epinfo kli0_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT, 8,	EPI(1), 0, 0 }
};

static struct usb_epinfo kli1_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(2), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0, 0 }
};

static struct usb_intinfo kli_ints[] = {
	USB_EPS(1, kli1_epinfos),
	USB_EPS(0, kli0_epinfos)
};
#endif

#ifdef USE_ICARUS
static struct usb_epinfo ica_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(3), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0, 0 }
};

static struct usb_intinfo ica_ints[] = {
	USB_EPS(0, ica_epinfos)
};

static struct usb_epinfo ica1_epinfos0[] = {
	{ LIBUSB_TRANSFER_TYPE_INTERRUPT,	16,	EPI(0x82), 0, 0 }
};

static struct usb_epinfo ica1_epinfos1[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(0x81), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(0x01), 0, 0 }
};

static struct usb_intinfo ica1_ints[] = {
	USB_EPS(1, ica1_epinfos1),
	USB_EPS(0, ica1_epinfos0)
};

static struct usb_epinfo amu_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0, 0 }
};

static struct usb_intinfo amu_ints[] = {
	USB_EPS(0, amu_epinfos)
};

static struct usb_epinfo llt_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0, 0 }
};

static struct usb_intinfo llt_ints[] = {
	USB_EPS(0, llt_epinfos)
};

static struct usb_epinfo cmr1_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0, 0 }
};

static struct usb_intinfo cmr1_ints[] = {
	USB_EPS(0, cmr1_epinfos)
};

static struct usb_epinfo cmr2_epinfos0[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0, 0 }
};
static struct usb_epinfo cmr2_epinfos1[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(3), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(4), 0, 0 },
};
static struct usb_epinfo cmr2_epinfos2[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(5), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(6), 0, 0 },
};
static struct usb_epinfo cmr2_epinfos3[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(7), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(8), 0, 0 }
};

static struct usb_intinfo cmr2_ints[] = {
	USB_EPS_CTRL(0, 1, cmr2_epinfos0),
	USB_EPS_CTRL(1, 2, cmr2_epinfos1),
	USB_EPS_CTRL(2, 3, cmr2_epinfos2),
	USB_EPS_CTRL(3, 4, cmr2_epinfos3)
};
#endif

#ifdef USE_COINTERRA
static struct usb_epinfo cointerra_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0, 0 }
};

static struct usb_intinfo cointerra_ints[] = {
	USB_EPS(0, cointerra_epinfos)
};
#endif

#ifdef USE_ANT_S1
static struct usb_epinfo ants1_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0, 0 }
};

static struct usb_intinfo ants1_ints[] = {
	USB_EPS(0, ants1_epinfos)
};
#endif

#ifdef USE_ANT_S3
static struct usb_epinfo ants3_epinfos[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0, 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0, 0 }
};

static struct usb_intinfo ants3_ints[] = {
	USB_EPS(0, ants3_epinfos)
};
#endif

#define IDVENDOR_FTDI 0x0403

#define INTINFO(_ints) \
		.intinfo_count = ARRAY_SIZE(_ints), \
		.intinfos = _ints

#define USBEP(_usbdev, _intinfo, _epinfo) (_usbdev->found->intinfos[_intinfo].epinfos[_epinfo].ep)
#define THISIF(_found, _this) (_found->intinfos[_this].interface)
#define USBIF(_usbdev, _this) THISIF(_usbdev->found, _this)

static struct usb_find_devices find_dev[] = {
#ifdef USE_BFLSC
	/* Wish these guys would be more consistent with setting these fields */
	{
		.drv = DRIVER_bflsc,
		.name = "BAS",
		.ident = IDENT_BAS,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6014,
		.iManufacturer = "FTDI",
		.iProduct = "BitFORCE SHA256 SC",
		.config = 1,
		.timeout = BFLSC_TIMEOUT_MS,
		.latency = LATENCY_STD,
		INTINFO(bflsc_ints) },
	{
		.drv = DRIVER_bflsc,
		.name = "BAS",
		.ident = IDENT_BAS,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6014,
		.iManufacturer = "Butterfly Labs",
		.iProduct = "BitFORCE SHA256 SC",
		.config = 1,
		.timeout = BFLSC_TIMEOUT_MS,
		.latency = LATENCY_STD,
		INTINFO(bflsc_ints) },
	{
		.drv = DRIVER_bflsc,
		.name = "BMA",
		.ident = IDENT_BMA,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6014,
		.iManufacturer = "BUTTERFLY LABS",
		.iProduct = "BitFORCE SHA256 SC",
		.config = 1,
		.timeout = BFLSC_TIMEOUT_MS,
		.latency = LATENCY_STD,
		INTINFO(bflsc_ints) },
	{
		.drv = DRIVER_bflsc,
		.name = "BMA",
		.ident = IDENT_BMA,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6014,
		.iManufacturer = "BUTTERFLY LABS",
		.iProduct = "BitFORCE SC-28nm",
		.config = 1,
		.timeout = BFLSC_TIMEOUT_MS,
		.latency = LATENCY_STD,
		INTINFO(bflsc_ints) },
	{
		.drv = DRIVER_bflsc,
		.name = "BMA",
		.ident = IDENT_BMA,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6014,
		.iManufacturer = "BUTTERFLY LABS",
		.iProduct = "BitFORCE SHA256",
		.config = 1,
		.timeout = BFLSC_TIMEOUT_MS,
		.latency = LATENCY_STD,
		INTINFO(bflsc_ints) },
#endif
#ifdef USE_BITFORCE
	{
		.drv = DRIVER_bitforce,
		.name = "BFL",
		.ident = IDENT_BFL,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6014,
		.iManufacturer = "Butterfly Labs Inc.",
		.iProduct = "BitFORCE SHA256",
		.config = 1,
		.timeout = BITFORCE_TIMEOUT_MS,
		.latency = LATENCY_STD,
		INTINFO(bfl_ints) },
#endif
#ifdef USE_BITFURY
	{
		.drv = DRIVER_bitfury,
		.name = "BF1",
		.ident = IDENT_BF1,
		.idVendor = 0x03eb,
		.idProduct = 0x204b,
		.config = 1,
		.timeout = BITFURY_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		//.iManufacturer = "BPMC",
		.iProduct = "Bitfury BF1",
		INTINFO(bfu_ints)
	},
	{
		.drv = DRIVER_bitfury,
		.name = "BXF",
		.ident = IDENT_BXF,
		.idVendor = 0x198c,
		.idProduct = 0xb1f1,
		.config = 1,
		.timeout = BITFURY_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		.iManufacturer = "c-scape",
		.iProduct = "bi?fury",
		INTINFO(bxf_ints)
	},
	{
		.drv = DRIVER_bitfury,
		.name = "OSM",
		.ident = IDENT_OSM,
		.idVendor = 0x198c,
		.idProduct = 0xb1f1,
		.config = 1,
		.timeout = BITFURY_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		.iManufacturer = "c-scape",
		.iProduct = "OneString",
		INTINFO(bxf_ints)
	},
	{
		.drv = DRIVER_bitfury,
		.name = "NFU",
		.ident = IDENT_NFU,
		.idVendor = 0x04d8,
		.idProduct = 0x00de,
		.config = 1,
		.timeout = BITFURY_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(nfu_ints)
	},
	{
		.drv = DRIVER_bitfury,
		.name = "BXM",
		.ident = IDENT_BXM,
		.idVendor = 0x0403,
		.idProduct = 0x6014,
		.config = 1,
		.timeout = BITFURY_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(bxm_ints)
	},
#endif
#ifdef USE_DRILLBIT
	{
		.drv = DRIVER_drillbit,
		.name = "DRB",
		.ident = IDENT_DRB,
		.idVendor = 0x03eb,
		.idProduct = 0x2404,
		.config = 1,
		.timeout = DRILLBIT_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		.iManufacturer = "Drillbit Systems",
		.iProduct = NULL, /* Can be Thumb or Eight, same driver */
		INTINFO(drillbit_ints)
	},
#endif
#ifdef USE_MODMINER
	{
		.drv = DRIVER_modminer,
		.name = "MMQ",
		.ident = IDENT_MMQ,
		.idVendor = 0x1fc9,
		.idProduct = 0x0003,
		.config = 1,
		.timeout = MODMINER_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(mmq_ints) },
#endif
#ifdef USE_AVALON
	{
		.drv = DRIVER_avalon,
		.name = "BTB",
		.ident = IDENT_BTB,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6001,
		.iManufacturer = "Burnin Electronics",
		.iProduct = "BitBurner",
		.config = 1,
		.timeout = AVALON_TIMEOUT_MS,
		.latency = 10,
		INTINFO(ava_ints) },
	{
		.drv = DRIVER_avalon,
		.name = "BBF",
		.ident = IDENT_BBF,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6001,
		.iManufacturer = "Burnin Electronics",
		.iProduct = "BitBurner Fury",
		.config = 1,
		.timeout = AVALON_TIMEOUT_MS,
		.latency = 10,
		INTINFO(ava_ints) },
	{
		.drv = DRIVER_avalon,
		.name = "AVA",
		.ident = IDENT_AVA,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6001,
		.config = 1,
		.timeout = AVALON_TIMEOUT_MS,
		.latency = 10,
		INTINFO(ava_ints) },
#endif
#ifdef USE_AVALON2
	{
		.drv = DRIVER_avalon2,
		.name = "AV2",
		.ident = IDENT_AV2,
		.idVendor = 0x067b,
		.idProduct = 0x2303,
		.config = 1,
		.timeout = AVALON_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(ava2_ints) },
#endif
#ifdef USE_AVALON4
	{
		.drv = DRIVER_avalon4,
		.name = "AV4",
		.ident = IDENT_AV4,
		.idVendor = 0x29f1,
		.idProduct = 0x33f2,
		.iManufacturer = "CANAAN",
		.iProduct = "USB2IIC Converter",
		.config = 1,
		.timeout = AVALON4_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(ava4_ints) },
#endif
#ifdef USE_AVALON7
	{
		.drv = DRIVER_avalon7,
		.name = "AV7",
		.ident = IDENT_AV7,
		.idVendor = 0x29f1,
		.idProduct = 0x33f2,
		.iManufacturer = "CANAAN",
		.iProduct = "USB2IIC Converter",
		.config = 1,
		.timeout = AVALON7_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(ava7_ints) },
#endif
#ifdef USE_AVALON8
	{
		.drv = DRIVER_avalon8,
		.name = "AV8",
		.ident = IDENT_AV8,
		.idVendor = 0x29f1,
		.idProduct = 0x33f2,
		.iManufacturer = "CANAAN",
		.iProduct = "USB2IIC Converter",
		.config = 1,
		.timeout = AVALON8_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(ava8_ints) },
#endif
#ifdef USE_AVALON9
	{
		.drv = DRIVER_avalon9,
		.name = "AV9",
		.ident = IDENT_AV9,
		.idVendor = 0x29f1,
		.idProduct = 0x33f2,
		.iManufacturer = "CANAAN",
		.iProduct = "USB2IIC Converter",
		.config = 1,
		.timeout = AVALON9_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(ava9_ints) },
#endif
#ifdef USE_AVALONLC3
	{
		.drv = DRIVER_avalonlc3,
		.name = "AVLC3",
		.ident = IDENT_AVLC3,
		.idVendor = 0x29f1,
		.idProduct = 0x33f2,
		.iManufacturer = "CANAAN",
		.iProduct = "USB2IIC Converter",
		.config = 1,
		.timeout = AVALONLC3_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(avalc3_ints) },
#endif
#ifdef USE_AVALON_MINER
	{
		.drv = DRIVER_avalonm,
		.name = "AV4M",
		.ident = IDENT_AVM,
		.idVendor = 0x29f1,
		.idProduct = 0x40f1,
		.iManufacturer = "CANAAN",
		.iProduct = "Avalon4 mini",
		.config = 1,
		.timeout = AVALONM_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(avam_ints) },
	{
		.drv = DRIVER_avalonm,
		.name = "AV3U",
		.ident = IDENT_AVM,
		.idVendor = 0x29f1,
		.idProduct = 0x33f3,
		.iManufacturer = "CANAAN",
		.iProduct = "Avalon nano",
		.config = 1,
		.timeout = AVALONM_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(av3u_ints) },

#endif
#ifdef USE_HASHFAST
	{
		.drv = DRIVER_hashfast,
		.name = "HFA",
		.ident = IDENT_HFA,
		.idVendor = HF_USB_VENDOR_ID,
		.idProduct = HF_USB_PRODUCT_ID_G1,
		.iManufacturer = "HashFast LLC",
		.iProduct = "M1 Module",
		.config = 1,
		.timeout = HASHFAST_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(hfa_ints) },
#endif
#ifdef USE_HASHRATIO
	{
		.drv = DRIVER_hashratio,
		.name = "HRO",
		.ident = IDENT_HRO,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6001,
		.config = 1,
		.timeout = HASHRATIO_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(hro_ints) },
#endif
#ifdef USE_KLONDIKE
	{
		.drv = DRIVER_klondike,
		.name = "KLN",
		.ident = IDENT_KLN,
		.idVendor = 0x04D8,
		.idProduct = 0xF60A,
		.config = 1,
		.timeout = KLONDIKE_TIMEOUT_MS,
		.latency = 10,
		INTINFO(kln_ints) },
	{
		.drv = DRIVER_klondike,
		.name = "KLI",
		.ident = IDENT_KLN,
		.idVendor = 0x04D8,
		.idProduct = 0xF60A,
		.config = 1,
		.timeout = KLONDIKE_TIMEOUT_MS,
		.latency = 10,
		INTINFO(kli_ints) },
#endif
#ifdef USE_ICARUS
	{
		.drv = DRIVER_icarus,
		.name = "ICA",
		.ident = IDENT_ICA,
		.idVendor = 0x067b,
		.idProduct = 0x2303,
		.config = 1,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(ica_ints) },
 	{
 		.drv = DRIVER_icarus,
 		.name = "ICA",
 		.ident = IDENT_AVA,
 		.idVendor = 0x1fc9,
 		.idProduct = 0x0083,
 		.config = 1,
 		.timeout = ICARUS_TIMEOUT_MS,
 		.latency = LATENCY_UNUSED,
 		INTINFO(ica1_ints) },
	{
		.drv = DRIVER_icarus,
		.name = "AMU",
		.ident = IDENT_AMU,
		.idVendor = 0x10c4,
		.idProduct = 0xea60,
		.config = 1,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(amu_ints) },
	{
		.drv = DRIVER_icarus,
		.name = "LIN",
		.ident = IDENT_LIN,
		.idVendor = 0x10c4,
		.idProduct = 0xea60,
		.config = 1,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(amu_ints) },
	{
		.drv = DRIVER_icarus,
		.name = "ANU",
		.ident = IDENT_ANU,
		.idVendor = 0x10c4,
		.idProduct = 0xea60,
		.config = 1,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(amu_ints) },
	{
		.drv = DRIVER_icarus,
		.name = "BLT",
		.ident = IDENT_BLT,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6001,
		.iProduct = "FT232R USB UART",
		.config = 1,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_STD,
		INTINFO(llt_ints) },
	// For any that don't match the above "BLT"
	{
		.drv = DRIVER_icarus,
		.name = "LLT",
		.ident = IDENT_LLT,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6001,
		.config = 1,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_STD,
		INTINFO(llt_ints) },
	{
		.drv = DRIVER_icarus,
		.name = "CMR",
		.ident = IDENT_CMR1,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6014,
		.iProduct = "Cairnsmore1",
		.config = 1,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_STD,
		INTINFO(cmr1_ints) },
	{
		.drv = DRIVER_icarus,
		.name = "CMR",
		.ident = IDENT_CMR2,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x8350,
		.iProduct = "Cairnsmore1",
		.config = 1,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_STD,
		INTINFO(cmr2_ints) },
#endif
#ifdef USE_COINTERRA
	{
		.drv = DRIVER_cointerra,
		.name = "CTA",
		.ident = IDENT_CTA,
		.idVendor = 0x1cbe,
		.idProduct = 0x0003,
		.config = 1,
		.timeout = COINTERRA_TIMEOUT_MS,
		.latency = LATENCY_STD,
		INTINFO(cointerra_ints) },
#endif
#ifdef USE_ANT_S1
	{
		.drv = DRIVER_ants1,
		.name = "ANT",
		.ident = IDENT_ANT,
		.idVendor = 0x4254,
		.idProduct = 0x4153,
		.config = 1,
		.timeout = ANT_S1_TIMEOUT_MS,
		.latency = LATENCY_ANTS1,
		INTINFO(ants1_ints) },
#endif
#ifdef USE_ANT_S3
	{
		.drv = DRIVER_ants3,
		.name = "AS3",
		.ident = IDENT_AS3,
		.idVendor = 0x4254,
		.idProduct = 0x4153,
		.config = 1,
		.timeout = ANT_S3_TIMEOUT_MS,
		.latency = LATENCY_ANTS3,
		INTINFO(ants3_ints) },
#endif
#ifdef USE_BLOCKERUPTER
	{
		.drv = DRIVER_blockerupter,
		.name = "BET",
		.ident = IDENT_BET,
		.idVendor = 0x10c4,
		.idProduct = 0xea60,
		.config = 1,
		.timeout = BLOCKERUPTER_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(bet_ints) },

#endif
#ifdef USE_FLOW
	{
		.drv = DRIVER_flow,
		.name = "FLX",
		.ident = IDENT_FLX,
		.idVendor = 0x10c4,
		.idProduct = 0xea60,
//		.iManufacturer = "flow",
		.iManufacturer = "Silicon Labs",
//		.iProduct = "bzm2",
		.iProduct = "VoltLink CP2102N USB to UART Bridge",
		.config = 1,
		.timeout = FLOW_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(flow1_ints) },
#endif
#ifdef USE_GEKKO
	{
		.drv = DRIVER_gekko,
		.name = "BSC",
		.ident = IDENT_BSC,
		.idVendor = 0x10c4,
		.idProduct = 0xea60,
		.iManufacturer = "bitshopperde",
		.iProduct = "Compac BM1384 Bitcoin Miner",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek1_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "BSD",
		.ident = IDENT_BSD,
		.idVendor = 0x10c4,
		.idProduct = 0xea60,
		.iManufacturer = "bitshopperde",
		.iProduct = "2Pac BM1384 Bitcoin Miner",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek1_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "BSE",
		.ident = IDENT_BSE,
		.idVendor = 0x10c4,
		.idProduct = 0xea60,
		.iManufacturer = "bitshopperde",
		.iProduct = "Terminus BM1384 Bitcoin Miner",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek1_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "GSC",
		.ident = IDENT_GSC,
		.idVendor = 0x10c4,
		.idProduct = 0xea60,
		.iManufacturer = "GekkoScience",
		.iProduct = "Compac BM1384 Bitcoin Miner",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek1_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "GSD",
		.ident = IDENT_GSD,
		.idVendor = 0x10c4,
		.idProduct = 0xea60,
		.iManufacturer = "GekkoScience",
		.iProduct = "2Pac BM1384 Bitcoin Miner",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek1_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "GSE",
		.ident = IDENT_GSE,
		.idVendor = 0x10c4,
		.idProduct = 0xea60,
		.iManufacturer = "GekkoScience",
		.iProduct = "Terminus BM1384 Bitcoin Miner",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek1_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "GSH",
		.ident = IDENT_GSH,
		.idVendor = 0x0403,
		.idProduct = 0x6015,
		.iManufacturer = "GekkoScience",
		.iProduct = "NewPac Bitcoin Miner",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek2_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "GSI",
		.ident = IDENT_GSI,
		.idVendor = 0x0403,
		.idProduct = 0x6015,
		.iManufacturer = "GekkoScience",
		.iProduct = "R606 Bitcoin Miner",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek2_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "GSF",
		.ident = IDENT_GSF,
		.idVendor = 0x0403,
		.idProduct = 0x6015,
		.iManufacturer = "GekkoScience",
		.iProduct = "CompacF Bitcoin Miner",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek2_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "GSF",
		.ident = IDENT_GSFM,
		.idVendor = 0x0403,
		.idProduct = 0x6015,
		.iManufacturer = "GekkoScience",
		.iProduct = "R909 Bitcoin Miner",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek2_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "GSA",
		.ident = IDENT_GSA1,
		.idVendor = 0x10c4,
		.idProduct = 0xea70,
		.iManufacturer = "Silicon Labs",
		.iProduct = "GekkoScience Compac A1",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek3_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "GSA",
		.ident = IDENT_GSA1,
		.idVendor = 0x10c4,
		.idProduct = 0xea70,
		.iManufacturer = "Silicon Labs",
		.iProduct = "GekkoScience Terminus A1",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek3_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "GSA",
		.ident = IDENT_GSA2,
		.idVendor = 0x10c4,
		.idProduct = 0xea70,
		.iManufacturer = "Silicon Labs",
		.iProduct = "GekkoScience Compac A2",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek3_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "GSA",
		.ident = IDENT_GSA2,
		.idVendor = 0x10c4,
		.idProduct = 0xea70,
		.iManufacturer = "Silicon Labs",
		.iProduct = "GekkoScience Terminus A2",
		.config = 1,
		.timeout = COMPAC_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek3_ints) },
	{
		.drv = DRIVER_gekko,
		.name = "GSK",
		.ident = IDENT_GSK,
		.idVendor = 0x04d8,
		.idProduct = 0x00de,
		.iManufacturer = "Microchip Technology Inc.",
		.iProduct = "MCP2210 USB to SPI Master",
		.config = 1,
		.timeout = BITFURY_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		INTINFO(gek4_ints) },

// extra dev iManufacturer/iProduct names not used in production
#include "usbgekdev.h"

#endif
	{ DRIVER_MAX, NULL, 0, 0, 0, NULL, NULL, 0, 0, 0, 0, NULL }
};

#define STRBUFLEN 256
static const char *BLANK = "";
static const char *space = " ";
static const char *nodatareturned = "no data returned ";

#if 0 // enable USBDEBUG - only during development testing
 static const char *debug_true_str = "true";
 static const char *debug_false_str = "false";
 static const char *nodevstr = "=NODEV";
 #define bool_str(boo) ((boo) ? debug_true_str : debug_false_str)
 #define isnodev(err) (NODEV(err) ? nodevstr : BLANK)
 #define USBDEBUG(fmt, ...) applog(LOG_WARNING, fmt, ##__VA_ARGS__)
#else
 #define USBDEBUG(fmt, ...)
#endif

// For device limits by driver
static struct driver_count {
	int count;
	int limit;
} drv_count[DRIVER_MAX];

// For device limits by list of bus/dev
static struct usb_busdev {
	int bus_number;
	int device_address;
#ifdef WIN32
	void *resource1;
	void *resource2;
#else
	int fd;
#endif
} *busdev;

static int busdev_count = 0;

// Total device limit
static int total_count = 0;
static int total_limit = 999999;

struct usb_in_use_list {
	struct usb_busdev in_use;
	struct usb_in_use_list *prev;
	struct usb_in_use_list *next;
};

// List of in use devices
static struct usb_in_use_list *in_use_head = NULL;
static struct usb_in_use_list *blacklist_head = NULL;

struct resource_work {
	bool lock;
	const char *dname;
	uint8_t bus_number;
	uint8_t device_address;
	struct resource_work *next;
};

// Pending work for the reslock thread
struct resource_work *res_work_head = NULL;

struct resource_reply {
	uint8_t bus_number;
	uint8_t device_address;
	bool got;
	struct resource_reply *next;
};

// Replies to lock requests
struct resource_reply *res_reply_head = NULL;

// Some stats need to always be defined
#define SEQ0 0
#define SEQ1 1

// NONE must be 0 - calloced
#define MODE_NONE 0
#define MODE_CTRL_READ (1 << 0)
#define MODE_CTRL_WRITE (1 << 1)
#define MODE_BULK_READ (1 << 2)
#define MODE_BULK_WRITE (1 << 3)

// Set this to 0 to remove stats processing
#define DO_USB_STATS 1

static bool stats_initialised = false;

#if DO_USB_STATS

#define MODE_SEP_STR "+"
#define MODE_NONE_STR "X"
#define MODE_CTRL_READ_STR "cr"
#define MODE_CTRL_WRITE_STR "cw"
#define MODE_BULK_READ_STR "br"
#define MODE_BULK_WRITE_STR "bw"

// One for each CMD, TIMEOUT, ERROR
struct cg_usb_stats_item {
	uint64_t count;
	double total_delay;
	double min_delay;
	double max_delay;
	struct timeval first;
	struct timeval last;
};

#define CMD_CMD 0
#define CMD_TIMEOUT 1
#define CMD_ERROR 2

// One for each C_CMD
struct cg_usb_stats_details {
	int seq;
	uint32_t modes;
	struct cg_usb_stats_item item[CMD_ERROR+1];
};

// One for each device
struct cg_usb_stats {
	char *name;
	int device_id;
	struct cg_usb_stats_details *details;
};

static struct cg_usb_stats *usb_stats = NULL;
static int next_stat = USB_NOSTAT;

#define SECTOMS(s) ((int)((s) * 1000))

#define USB_STATS(sgpu_, sta_, fin_, err_, mode_, cmd_, seq_, tmo_) \
		stats(sgpu_, sta_, fin_, err_, mode_, cmd_, seq_, tmo_)
#define STATS_TIMEVAL(tv_) cgtime(tv_)
#define USB_REJECT(sgpu_, mode_) rejected_inc(sgpu_, mode_)

#else
#define USB_STATS(sgpu_, sta_, fin_, err_, mode_, cmd_, seq_, tmo_)
#define STATS_TIMEVAL(tv_)
#define USB_REJECT(sgpu_, mode_)

#endif // DO_USB_STATS

/* Create usb_commands array from USB_PARSE_COMMANDS macro in usbutils.h */
char *usb_commands[] = {
	USB_PARSE_COMMANDS(JUMPTABLE)
	"Null"
};

#ifdef EOL
#undef EOL
#endif
#define EOL "\n"

static const char *DESDEV = "Device";
static const char *DESCON = "Config";
static const char *DESSTR = "String";
static const char *DESINT = "Interface";
static const char *DESEP = "Endpoint";
static const char *DESHID = "HID";
static const char *DESRPT = "Report";
static const char *DESPHY = "Physical";
static const char *DESHUB = "Hub";

static const char *EPIN = "In: ";
static const char *EPOUT = "Out: ";
static const char *EPX = "?: ";

static const char *CONTROL = "Control";
static const char *ISOCHRONOUS_X = "Isochronous+?";
static const char *ISOCHRONOUS_N_X = "Isochronous+None+?";
static const char *ISOCHRONOUS_N_D = "Isochronous+None+Data";
static const char *ISOCHRONOUS_N_F = "Isochronous+None+Feedback";
static const char *ISOCHRONOUS_N_I = "Isochronous+None+Implicit";
static const char *ISOCHRONOUS_A_X = "Isochronous+Async+?";
static const char *ISOCHRONOUS_A_D = "Isochronous+Async+Data";
static const char *ISOCHRONOUS_A_F = "Isochronous+Async+Feedback";
static const char *ISOCHRONOUS_A_I = "Isochronous+Async+Implicit";
static const char *ISOCHRONOUS_D_X = "Isochronous+Adaptive+?";
static const char *ISOCHRONOUS_D_D = "Isochronous+Adaptive+Data";
static const char *ISOCHRONOUS_D_F = "Isochronous+Adaptive+Feedback";
static const char *ISOCHRONOUS_D_I = "Isochronous+Adaptive+Implicit";
static const char *ISOCHRONOUS_S_X = "Isochronous+Sync+?";
static const char *ISOCHRONOUS_S_D = "Isochronous+Sync+Data";
static const char *ISOCHRONOUS_S_F = "Isochronous+Sync+Feedback";
static const char *ISOCHRONOUS_S_I = "Isochronous+Sync+Implicit";
static const char *BULK = "Bulk";
static const char *INTERRUPT = "Interrupt";
static const char *UNKNOWN = "Unknown";

static const char *destype(uint8_t bDescriptorType)
{
	switch (bDescriptorType) {
		case LIBUSB_DT_DEVICE:
			return DESDEV;
		case LIBUSB_DT_CONFIG:
			return DESCON;
		case LIBUSB_DT_STRING:
			return DESSTR;
		case LIBUSB_DT_INTERFACE:
			return DESINT;
		case LIBUSB_DT_ENDPOINT:
			return DESEP;
		case LIBUSB_DT_HID:
			return DESHID;
		case LIBUSB_DT_REPORT:
			return DESRPT;
		case LIBUSB_DT_PHYSICAL:
			return DESPHY;
		case LIBUSB_DT_HUB:
			return DESHUB;
	}
	return UNKNOWN;
}

static const char *epdir(uint8_t bEndpointAddress)
{
	switch (bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) {
		case LIBUSB_ENDPOINT_IN:
			return EPIN;
		case LIBUSB_ENDPOINT_OUT:
			return EPOUT;
	}
	return EPX;
}

static const char *epatt(uint8_t bmAttributes)
{
	switch(bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) {
		case LIBUSB_TRANSFER_TYPE_CONTROL:
			return CONTROL;
		case LIBUSB_TRANSFER_TYPE_BULK:
			return BULK;
		case LIBUSB_TRANSFER_TYPE_INTERRUPT:
			return INTERRUPT;
		case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
			switch(bmAttributes & LIBUSB_ISO_SYNC_TYPE_MASK) {
				case LIBUSB_ISO_SYNC_TYPE_NONE:
					switch(bmAttributes & LIBUSB_ISO_USAGE_TYPE_MASK) {
						case LIBUSB_ISO_USAGE_TYPE_DATA:
							return ISOCHRONOUS_N_D;
						case LIBUSB_ISO_USAGE_TYPE_FEEDBACK:
							return ISOCHRONOUS_N_F;
						case LIBUSB_ISO_USAGE_TYPE_IMPLICIT:
							return ISOCHRONOUS_N_I;
					}
					return ISOCHRONOUS_N_X;
				case LIBUSB_ISO_SYNC_TYPE_ASYNC:
					switch(bmAttributes & LIBUSB_ISO_USAGE_TYPE_MASK) {
						case LIBUSB_ISO_USAGE_TYPE_DATA:
							return ISOCHRONOUS_A_D;
						case LIBUSB_ISO_USAGE_TYPE_FEEDBACK:
							return ISOCHRONOUS_A_F;
						case LIBUSB_ISO_USAGE_TYPE_IMPLICIT:
							return ISOCHRONOUS_A_I;
					}
					return ISOCHRONOUS_A_X;
				case LIBUSB_ISO_SYNC_TYPE_ADAPTIVE:
					switch(bmAttributes & LIBUSB_ISO_USAGE_TYPE_MASK) {
						case LIBUSB_ISO_USAGE_TYPE_DATA:
							return ISOCHRONOUS_D_D;
						case LIBUSB_ISO_USAGE_TYPE_FEEDBACK:
							return ISOCHRONOUS_D_F;
						case LIBUSB_ISO_USAGE_TYPE_IMPLICIT:
							return ISOCHRONOUS_D_I;
					}
					return ISOCHRONOUS_D_X;
				case LIBUSB_ISO_SYNC_TYPE_SYNC:
					switch(bmAttributes & LIBUSB_ISO_USAGE_TYPE_MASK) {
						case LIBUSB_ISO_USAGE_TYPE_DATA:
							return ISOCHRONOUS_S_D;
						case LIBUSB_ISO_USAGE_TYPE_FEEDBACK:
							return ISOCHRONOUS_S_F;
						case LIBUSB_ISO_USAGE_TYPE_IMPLICIT:
							return ISOCHRONOUS_S_I;
					}
					return ISOCHRONOUS_S_X;
			}
			return ISOCHRONOUS_X;
	}

	return UNKNOWN;
}

static void append(char **buf, char *append, size_t *off, size_t *len)
{
	int new = strlen(append);
	if ((new + *off) >= *len)
	{
		*len *= 2;
		*buf = cgrealloc(*buf, *len);
	}

	strcpy(*buf + *off, append);
	*off += new;
}

static bool setgetdes(ssize_t count, libusb_device *dev, struct libusb_device_handle *handle, struct libusb_config_descriptor **config, int cd, char **buf, size_t *off, size_t *len)
{
	char tmp[512];
	int err;

	err = libusb_set_configuration(handle, cd);
	if (err) {
		snprintf(tmp, sizeof(tmp), EOL "  ** dev %d: Failed to set config descriptor to %d, err %d",
				(int)count, cd, err);
		append(buf, tmp, off, len);
		return false;
	}

	err = libusb_get_active_config_descriptor(dev, config);
	if (err) {
		snprintf(tmp, sizeof(tmp), EOL "  ** dev %d: Failed to get active config descriptor set to %d, err %d",
				(int)count, cd, err);
		append(buf, tmp, off, len);
		return false;
	}

	snprintf(tmp, sizeof(tmp), EOL "  ** dev %d: Set & Got active config descriptor to %d, err %d",
			(int)count, cd, err);
	append(buf, tmp, off, len);
	return true;
}

static void usb_full(ssize_t *count, libusb_device *dev, char **buf, size_t *off, size_t *len, int level)
{
	struct libusb_device_descriptor desc;
	uint8_t bus_number;
	uint8_t device_address;
	struct libusb_device_handle *handle;
	struct libusb_config_descriptor *config;
	const struct libusb_interface_descriptor *idesc;
	const struct libusb_endpoint_descriptor *epdesc;
	unsigned char man[STRBUFLEN+1];
	unsigned char prod[STRBUFLEN+1];
	unsigned char ser[STRBUFLEN+1];
	char tmp[512];
	int err, i, j, k;

	err = libusb_get_device_descriptor(dev, &desc);
	if (opt_usb_list_all && err) {
		snprintf(tmp, sizeof(tmp), EOL ".USB dev %d: Failed to get descriptor, err %d",
					(int)(++(*count)), err);
		append(buf, tmp, off, len);
		return;
	}

	bus_number = libusb_get_bus_number(dev);
	device_address = libusb_get_device_address(dev);

	if (!opt_usb_list_all) {
		bool known = false;

		for (i = 0; find_dev[i].drv != DRIVER_MAX; i++)
			if ((find_dev[i].idVendor == desc.idVendor) &&
			    (find_dev[i].idProduct == desc.idProduct)) {
				known = true;
				break;
			}

		if (!known)
			return;
	}

	(*count)++;

	if (level == 0) {
		snprintf(tmp, sizeof(tmp), EOL ".USB dev %d: Bus %d Device %d ID: %04x:%04x",
				(int)(*count), (int)bus_number, (int)device_address,
				desc.idVendor, desc.idProduct);
	} else {
		snprintf(tmp, sizeof(tmp), EOL ".USB dev %d: Bus %d Device %d Device Descriptor:" EOL "\tLength: %d" EOL
			"\tDescriptor Type: %s" EOL "\tUSB: %04x" EOL "\tDeviceClass: %d" EOL
			"\tDeviceSubClass: %d" EOL "\tDeviceProtocol: %d" EOL "\tMaxPacketSize0: %d" EOL
			"\tidVendor: %04x" EOL "\tidProduct: %04x" EOL "\tDeviceRelease: %x" EOL
			"\tNumConfigurations: %d",
				(int)(*count), (int)bus_number, (int)device_address,
				(int)(desc.bLength), destype(desc.bDescriptorType),
				desc.bcdUSB, (int)(desc.bDeviceClass), (int)(desc.bDeviceSubClass),
				(int)(desc.bDeviceProtocol), (int)(desc.bMaxPacketSize0),
				desc.idVendor, desc.idProduct, desc.bcdDevice,
				(int)(desc.bNumConfigurations));
	}
	append(buf, tmp, off, len);

	err = libusb_open(dev, &handle);
	if (err) {
		snprintf(tmp, sizeof(tmp), EOL "  ** dev %d: Failed to open, err %d", (int)(*count), err);
		append(buf, tmp, off, len);
		return;
	}

	err = libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, man, STRBUFLEN);
	if (err < 0)
		snprintf((char *)man, sizeof(man), "** err:(%d) %s", err, libusb_error_name(err));

	err = libusb_get_string_descriptor_ascii(handle, desc.iProduct, prod, STRBUFLEN);
	if (err < 0)
		snprintf((char *)prod, sizeof(prod), "** err:(%d) %s", err, libusb_error_name(err));

	if (level == 0) {
		libusb_close(handle);
		snprintf(tmp, sizeof(tmp), EOL "  Manufacturer: '%s'" EOL "  Product: '%s'", man, prod);
		append(buf, tmp, off, len);
		return;
	}

	if (libusb_kernel_driver_active(handle, 0) == 1) {
		snprintf(tmp, sizeof(tmp), EOL "   * dev %d: kernel attached", (int)(*count));
		append(buf, tmp, off, len);
	}

	err = libusb_get_active_config_descriptor(dev, &config);
	if (err) {
		if (!setgetdes(*count, dev, handle, &config, 1, buf, off, len)
		&&  !setgetdes(*count, dev, handle, &config, 0, buf, off, len)) {
			libusb_close(handle);
			snprintf(tmp, sizeof(tmp), EOL "  ** dev %d: Failed to set config descriptor to %d or %d",
					(int)(*count), 1, 0);
			append(buf, tmp, off, len);
			return;
		}
	}

	snprintf(tmp, sizeof(tmp), EOL "     dev %d: Active Config:" EOL "\tDescriptorType: %s" EOL
			"\tNumInterfaces: %d" EOL "\tConfigurationValue: %d" EOL
			"\tAttributes: %d" EOL "\tMaxPower: %d",
				(int)(*count), destype(config->bDescriptorType),
				(int)(config->bNumInterfaces), (int)(config->iConfiguration),
				(int)(config->bmAttributes), (int)(config->MaxPower));
	append(buf, tmp, off, len);

	for (i = 0; i < (int)(config->bNumInterfaces); i++) {
		for (j = 0; j < config->interface[i].num_altsetting; j++) {
			idesc = &(config->interface[i].altsetting[j]);

			snprintf(tmp, sizeof(tmp), EOL "     _dev %d: Interface Descriptor %d:" EOL
					"\tDescriptorType: %s" EOL "\tInterfaceNumber: %d" EOL
					"\tNumEndpoints: %d" EOL "\tInterfaceClass: %d" EOL
					"\tInterfaceSubClass: %d" EOL "\tInterfaceProtocol: %d",
						(int)(*count), j, destype(idesc->bDescriptorType),
						(int)(idesc->bInterfaceNumber),
						(int)(idesc->bNumEndpoints),
						(int)(idesc->bInterfaceClass),
						(int)(idesc->bInterfaceSubClass),
						(int)(idesc->bInterfaceProtocol));
			append(buf, tmp, off, len);

			for (k = 0; k < (int)(idesc->bNumEndpoints); k++) {
				epdesc = &(idesc->endpoint[k]);

				snprintf(tmp, sizeof(tmp), EOL "     __dev %d: Interface %d Endpoint %d:" EOL
						"\tDescriptorType: %s" EOL
						"\tEndpointAddress: %s0x%x" EOL
						"\tAttributes: %s" EOL "\tMaxPacketSize: %d" EOL
						"\tInterval: %d" EOL "\tRefresh: %d",
							(int)(*count), (int)(idesc->bInterfaceNumber), k,
							destype(epdesc->bDescriptorType),
							epdir(epdesc->bEndpointAddress),
							(int)(epdesc->bEndpointAddress),
							epatt(epdesc->bmAttributes),
							epdesc->wMaxPacketSize,
							(int)(epdesc->bInterval),
							(int)(epdesc->bRefresh));
				append(buf, tmp, off, len);
			}
		}
	}

	libusb_free_config_descriptor(config);
	config = NULL;

	err = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, ser, STRBUFLEN);
	if (err < 0)
		snprintf((char *)ser, sizeof(ser), "** err:(%d) %s", err, libusb_error_name(err));

	snprintf(tmp, sizeof(tmp), EOL "     dev %d: More Info:" EOL "\tManufacturer: '%s'" EOL
			"\tProduct: '%s'" EOL "\tSerial '%s'",
				(int)(*count), man, prod, ser);
	append(buf, tmp, off, len);

	libusb_close(handle);
}

// Function to dump all USB devices
void usb_all(int level)
{
	libusb_device **list;
	ssize_t count, i, j;
	char *buf;
	size_t len, off;

	count = libusb_get_device_list(NULL, &list);
	if (count < 0) {
		applog(LOG_ERR, "USB all: failed, err:(%d) %s", (int)count, libusb_error_name((int)count));
		return;
	}

	if (count == 0)
		applog(LOG_WARNING, "USB all: found no devices");
	else
	{
		len = 10000;
		buf = malloc(len+1);
		if (unlikely(!buf))
			quit(1, "USB failed to malloc buf in usb_all");

		sprintf(buf, "USB all: found %d devices", (int)count);
		off = strlen(buf);

		if (!opt_usb_list_all)
			append(&buf, " - listing known devices", &off, &len);

		j = -1;
		for (i = 0; i < count; i++)
			usb_full(&j, list[i], &buf, &off, &len, level);

		_applog(LOG_WARNING, buf, false);

		free(buf);

		if (j == -1)
			applog(LOG_WARNING, "No known USB devices");
		else
			applog(LOG_WARNING, "%d %sUSB devices",
				(int)(++j), opt_usb_list_all ? BLANK : "known ");

	}

	libusb_free_device_list(list, 1);
}

static void cgusb_check_init()
{
	mutex_lock(&cgusb_lock);

	if (stats_initialised == false) {
		// N.B. environment LIBUSB_DEBUG also sets libusb_set_debug()
		if (opt_usbdump >= 0) {
#if LIBUSB_API_VERSION >= 0x01000106
			libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, opt_usbdump);
#else
			libusb_set_debug(NULL, opt_usbdump);
#endif
			usb_all(opt_usbdump);
		}
		stats_initialised = true;
	}

	mutex_unlock(&cgusb_lock);
}

const char *usb_cmdname(enum usb_cmds cmd)
{
	cgusb_check_init();

	return usb_commands[cmd];
}

void usb_applog(struct cgpu_info *cgpu, enum usb_cmds cmd, char *msg, int amount, int err)
{
	if (msg && !*msg)
		msg = NULL;

	if (!msg && amount == 0 && err == LIBUSB_SUCCESS)
		msg = (char *)nodatareturned;

        applog(LOG_ERR, "%s%i: %s failed%s%s (err=%d amt=%d)",
                        cgpu->drv->name, cgpu->device_id,
                        usb_cmdname(cmd),
                        msg ? space : BLANK, msg ? msg : BLANK,
                        err, amount);
}

#ifdef WIN32
static void in_use_store_ress(uint8_t bus_number, uint8_t device_address, void *resource1, void *resource2)
{
	struct usb_in_use_list *in_use_tmp;
	bool found = false, empty = true;

	mutex_lock(&cgusb_lock);
	in_use_tmp = in_use_head;
	while (in_use_tmp) {
		if (in_use_tmp->in_use.bus_number == (int)bus_number &&
			in_use_tmp->in_use.device_address == (int)device_address) {
			found = true;

			if (in_use_tmp->in_use.resource1)
				empty = false;
			in_use_tmp->in_use.resource1 = resource1;

			if (in_use_tmp->in_use.resource2)
				empty = false;
			in_use_tmp->in_use.resource2 = resource2;

			break;
		}
		in_use_tmp = in_use_tmp->next;
	}
	mutex_unlock(&cgusb_lock);

	if (found == false)
		applog(LOG_ERR, "FAIL: USB store_ress not found (%d:%d)",
				(int)bus_number, (int)device_address);

	if (empty == false)
		applog(LOG_ERR, "FAIL: USB store_ress not empty (%d:%d)",
				(int)bus_number, (int)device_address);
}

static void in_use_get_ress(uint8_t bus_number, uint8_t device_address, void **resource1, void **resource2)
{
	struct usb_in_use_list *in_use_tmp;
	bool found = false, empty = false;

	mutex_lock(&cgusb_lock);
	in_use_tmp = in_use_head;
	while (in_use_tmp) {
		if (in_use_tmp->in_use.bus_number == (int)bus_number &&
			in_use_tmp->in_use.device_address == (int)device_address) {
			found = true;

			if (!in_use_tmp->in_use.resource1)
				empty = true;
			*resource1 = in_use_tmp->in_use.resource1;
			in_use_tmp->in_use.resource1 = NULL;

			if (!in_use_tmp->in_use.resource2)
				empty = true;
			*resource2 = in_use_tmp->in_use.resource2;
			in_use_tmp->in_use.resource2 = NULL;

			break;
		}
		in_use_tmp = in_use_tmp->next;
	}
	mutex_unlock(&cgusb_lock);

	if (found == false)
		applog(LOG_ERR, "FAIL: USB get_lock not found (%d:%d)",
				(int)bus_number, (int)device_address);

	if (empty == true)
		applog(LOG_ERR, "FAIL: USB get_lock empty (%d:%d)",
				(int)bus_number, (int)device_address);
}
#else

static void in_use_store_fd(uint8_t bus_number, uint8_t device_address, int fd)
{
	struct usb_in_use_list *in_use_tmp;
	bool found = false;

	mutex_lock(&cgusb_lock);
	in_use_tmp = in_use_head;
	while (in_use_tmp) {
		if (in_use_tmp->in_use.bus_number == (int)bus_number &&
			in_use_tmp->in_use.device_address == (int)device_address) {
			found = true;
			in_use_tmp->in_use.fd = fd;
			break;
		}
		in_use_tmp = in_use_tmp->next;
	}
	mutex_unlock(&cgusb_lock);

	if (found == false) {
		applog(LOG_ERR, "FAIL: USB store_fd not found (%d:%d)",
				(int)bus_number, (int)device_address);
	}
}

static int in_use_get_fd(uint8_t bus_number, uint8_t device_address)
{
	struct usb_in_use_list *in_use_tmp;
	bool found = false;
	int fd = -1;

	mutex_lock(&cgusb_lock);
	in_use_tmp = in_use_head;
	while (in_use_tmp) {
		if (in_use_tmp->in_use.bus_number == (int)bus_number &&
		    in_use_tmp->in_use.device_address == (int)device_address) {
			found = true;
			fd = in_use_tmp->in_use.fd;
			break;
		}
		in_use_tmp = in_use_tmp->next;
	}
	mutex_unlock(&cgusb_lock);

	if (found == false) {
		applog(LOG_ERR, "FAIL: USB get_lock not found (%d:%d)",
				(int)bus_number, (int)device_address);
	}
	return fd;
}
#endif

static bool _in_use(struct usb_in_use_list *head, uint8_t bus_number,
		    uint8_t device_address)
{
	struct usb_in_use_list *in_use_tmp;
	bool ret = false;

	in_use_tmp = head;
	while (in_use_tmp) {
		if (in_use_tmp->in_use.bus_number == (int)bus_number &&
		    in_use_tmp->in_use.device_address == (int)device_address) {
			ret = true;
			break;
		}
		in_use_tmp = in_use_tmp->next;
		if (in_use_tmp == head)
			break;
	}
	return ret;
}

static bool __is_in_use(uint8_t bus_number, uint8_t device_address)
{
	if (_in_use(in_use_head, bus_number, device_address))
		return true;
	if (_in_use(blacklist_head, bus_number, device_address))
		return true;
	return false;
}

static bool is_in_use_bd(uint8_t bus_number, uint8_t device_address)
{
	bool ret;

	mutex_lock(&cgusb_lock);
	ret = __is_in_use(bus_number, device_address);
	mutex_unlock(&cgusb_lock);
	return ret;
}

static bool is_in_use(libusb_device *dev)
{
	return is_in_use_bd(libusb_get_bus_number(dev), libusb_get_device_address(dev));
}

static bool how_in_use(uint8_t bus_number, uint8_t device_address, bool *blacklisted)
{
	bool ret;
	mutex_lock(&cgusb_lock);
	ret = _in_use(in_use_head, bus_number, device_address);
	if (!ret) {
		if (_in_use(blacklist_head, bus_number, device_address))
			*blacklisted = true;
	}
	mutex_unlock(&cgusb_lock);

	return ret;
}

void usb_list(void)
{
	struct libusb_device_descriptor desc;
	struct libusb_device_handle *handle;
	uint8_t bus_number;
	uint8_t device_address;
	libusb_device **list;
	ssize_t count, i, j;
	int err, total = 0;

	count = libusb_get_device_list(NULL, &list);
	if (count < 0) {
		applog(LOG_ERR, "USB list: failed, err:(%d) %s", (int)count, libusb_error_name((int)count));
		return;
	}
	if (count == 0) {
		applog(LOG_WARNING, "USB list: found no devices");
		return;
	}
	for (i = 0; i < count; i++) {
		bool known = false, blacklisted = false, active;
		unsigned char manuf[256], prod[256];
		libusb_device *dev = list[i];

		err = libusb_get_device_descriptor(dev, &desc);
		if (err) {
			applog(LOG_WARNING, "USB list: Failed to get descriptor %d", (int)i);
			break;
		}

		bus_number = libusb_get_bus_number(dev);
		device_address = libusb_get_device_address(dev);

		for (j = 0; find_dev[j].drv != DRIVER_MAX; j++) {
			if ((find_dev[j].idVendor == desc.idVendor) &&
			    (find_dev[j].idProduct == desc.idProduct)) {
				known = true;
				break;
			}
		}
		if (!known)
			continue;

		err = libusb_open(dev, &handle);
		if (err) {
			applog(LOG_WARNING, "USB list: Failed to open %d", (int)i);
			break;
		}
		libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, manuf, 255);
		libusb_get_string_descriptor_ascii(handle, desc.iProduct, prod, 255);
		total++;
		active = how_in_use(bus_number, device_address, &blacklisted);
		simplelog(LOG_WARNING, "Bus %u Device %u ID: %04x:%04x %s %s %sactive %s",
		       bus_number, device_address, desc.idVendor, desc.idProduct,
		       manuf, prod, active ? "" : "in", blacklisted ? "blacklisted" : "");
	}
	libusb_free_device_list(list, 1);
	simplelog(LOG_WARNING, "%d total known USB device%s", total, total > 1 ? "s": "");
}

static void add_in_use(uint8_t bus_number, uint8_t device_address, bool blacklist)
{
	struct usb_in_use_list *in_use_tmp, **head;
	bool found = false;

	mutex_lock(&cgusb_lock);
	if (unlikely(!blacklist && __is_in_use(bus_number, device_address))) {
		found = true;
		goto nofway;
	}
	if (blacklist)
		head = &blacklist_head;
	else
		head = &in_use_head;

	in_use_tmp = cgcalloc(1, sizeof(*in_use_tmp));
	in_use_tmp->in_use.bus_number = (int)bus_number;
	in_use_tmp->in_use.device_address = (int)device_address;
	in_use_tmp->next = in_use_head;
	if (*head)
		(*head)->prev = in_use_tmp;
	*head = in_use_tmp;
nofway:
	mutex_unlock(&cgusb_lock);

	if (found)
		applog(LOG_ERR, "FAIL: USB add already in use (%d:%d)",
				(int)bus_number, (int)device_address);
}

static void __remove_in_use(uint8_t bus_number, uint8_t device_address, bool blacklist)
{
	struct usb_in_use_list *in_use_tmp, **head;
	bool found = false;

	mutex_lock(&cgusb_lock);
	if (blacklist)
		head = &blacklist_head;
	else
		head = &in_use_head;

	in_use_tmp = *head;
	while (in_use_tmp) {
		if (in_use_tmp->in_use.bus_number == (int)bus_number &&
		    in_use_tmp->in_use.device_address == (int)device_address) {
			found = true;
			if (in_use_tmp == *head) {
				*head = (*head)->next;
				if (*head)
					(*head)->prev = NULL;
			} else {
				in_use_tmp->prev->next = in_use_tmp->next;
				if (in_use_tmp->next)
					in_use_tmp->next->prev = in_use_tmp->prev;
			}
			free(in_use_tmp);
			break;
		}
		in_use_tmp = in_use_tmp->next;
		if (in_use_tmp == *head)
			break;
	}

	mutex_unlock(&cgusb_lock);

	if (!found) {
		applog(LOG_ERR, "FAIL: USB remove not already in use (%d:%d)",
				(int)bus_number, (int)device_address);
	}
}

static void remove_in_use(uint8_t bus_number, uint8_t device_address)
{
	__remove_in_use(bus_number, device_address, false);
}

static bool cgminer_usb_lock_bd(struct device_drv *drv, uint8_t bus_number, uint8_t device_address)
{
	struct resource_work *res_work;
	bool ret;

	applog(LOG_DEBUG, "USB lock %s %d-%d", drv->dname, (int)bus_number, (int)device_address);

	res_work = cgcalloc(1, sizeof(*res_work));
	res_work->lock = true;
	res_work->dname = (const char *)(drv->dname);
	res_work->bus_number = bus_number;
	res_work->device_address = device_address;

	mutex_lock(&cgusbres_lock);
	res_work->next = res_work_head;
	res_work_head = res_work;
	mutex_unlock(&cgusbres_lock);

	cgsem_post(&usb_resource_sem);

	// TODO: add a timeout fail - restart the resource thread?
	while (true) {
		cgsleep_ms(50);

		mutex_lock(&cgusbres_lock);
		if (res_reply_head) {
			struct resource_reply *res_reply_prev = NULL;
			struct resource_reply *res_reply = res_reply_head;
			while (res_reply) {
				if (res_reply->bus_number == bus_number &&
					res_reply->device_address == device_address) {

					if (res_reply_prev)
						res_reply_prev->next = res_reply->next;
					else
						res_reply_head = res_reply->next;

					mutex_unlock(&cgusbres_lock);

					ret = res_reply->got;

					free(res_reply);

					return ret;
				}
				res_reply_prev = res_reply;
				res_reply = res_reply->next;
			}
		}
		mutex_unlock(&cgusbres_lock);
	}
}

static bool cgminer_usb_lock(struct device_drv *drv, libusb_device *dev)
{
	return cgminer_usb_lock_bd(drv, libusb_get_bus_number(dev), libusb_get_device_address(dev));
}

static void cgminer_usb_unlock_bd(struct device_drv *drv, uint8_t bus_number, uint8_t device_address)
{
	struct resource_work *res_work;

	applog(LOG_DEBUG, "USB unlock %s %d-%d", drv->dname, (int)bus_number, (int)device_address);

	res_work = cgcalloc(1, sizeof(*res_work));
	res_work->lock = false;
	res_work->dname = (const char *)(drv->dname);
	res_work->bus_number = bus_number;
	res_work->device_address = device_address;

	mutex_lock(&cgusbres_lock);
	res_work->next = res_work_head;
	res_work_head = res_work;
	mutex_unlock(&cgusbres_lock);

	cgsem_post(&usb_resource_sem);

	return;
}

static void cgminer_usb_unlock(struct device_drv *drv, libusb_device *dev)
{
	cgminer_usb_unlock_bd(drv, libusb_get_bus_number(dev), libusb_get_device_address(dev));
}

static struct cg_usb_device *free_cgusb(struct cg_usb_device *cgusb)
{
	applog(LOG_DEBUG, "USB free %s", cgusb->found->name);

	if (cgusb->serial_string && cgusb->serial_string != BLANK)
		free(cgusb->serial_string);

	if (cgusb->manuf_string && cgusb->manuf_string != BLANK)
		free(cgusb->manuf_string);

	if (cgusb->prod_string && cgusb->prod_string != BLANK)
		free(cgusb->prod_string);

	if (cgusb->descriptor)
		free(cgusb->descriptor);

	free(cgusb->found);

	free(cgusb);

	return NULL;
}

static void _usb_uninit(struct cgpu_info *cgpu)
{
	int ifinfo;

	// May have happened already during a failed initialisation
	//  if release_cgpu() was called due to a USB NODEV(err)
	if (!cgpu->usbdev)
		return;

	applog(LOG_DEBUG, "USB uninit %s%i",
			cgpu->drv->name, cgpu->device_id);

	if (cgpu->usbdev->handle) {
		for (ifinfo = cgpu->usbdev->found->intinfo_count - 1; ifinfo >= 0; ifinfo--) {
			libusb_release_interface(cgpu->usbdev->handle,
						 THISIF(cgpu->usbdev->found, ifinfo));
#ifdef LINUX
			libusb_attach_kernel_driver(cgpu->usbdev->handle, THISIF(cgpu->usbdev->found, ifinfo));
#endif
		}
		cg_wlock(&cgusb_fd_lock);
		libusb_close(cgpu->usbdev->handle);
		cgpu->usbdev->handle = NULL;
		cg_wunlock(&cgusb_fd_lock);
	}
	cgpu->usbdev = free_cgusb(cgpu->usbdev);
}

void usb_uninit(struct cgpu_info *cgpu)
{
	int pstate;

	DEVWLOCK(cgpu, pstate);

	_usb_uninit(cgpu);

	DEVWUNLOCK(cgpu, pstate);
}

/* We have dropped the read devlock before entering this function but we pick
 * up the write lock to prevent any attempts to work on dereferenced code once
 * the nodev flag has been set. */
static bool __release_cgpu(struct cgpu_info *cgpu)
{
	struct cg_usb_device *cgusb = cgpu->usbdev;
	bool initted = cgpu->usbinfo.initialised;
	struct cgpu_info *lookcgpu;
	int i;

	// It has already been done
	if (cgpu->usbinfo.nodev)
		return false;

	applog(LOG_DEBUG, "USB release %s%i",
			cgpu->drv->name, cgpu->device_id);

	if (initted) {
		zombie_devs++;
		total_count--;
		drv_count[cgpu->drv->drv_id].count--;
	}

	cgpu->usbinfo.nodev = true;
	cgpu->usbinfo.nodev_count++;
	cgtime(&cgpu->usbinfo.last_nodev);

	// Any devices sharing the same USB device should be marked also
	for (i = 0; i < total_devices; i++) {
		lookcgpu = get_a_device(i);
		if (lookcgpu != cgpu && lookcgpu->usbdev == cgusb) {
			if (initted) {
				total_count--;
				drv_count[lookcgpu->drv->drv_id].count--;
			}

			lookcgpu->usbinfo.nodev = true;
			lookcgpu->usbinfo.nodev_count++;
			cg_memcpy(&(lookcgpu->usbinfo.last_nodev),
				&(cgpu->usbinfo.last_nodev), sizeof(struct timeval));
			lookcgpu->usbdev = NULL;
		}
	}

	_usb_uninit(cgpu);
	return true;
}

static void release_cgpu(struct cgpu_info *cgpu)
{
	if (__release_cgpu(cgpu))
		cgminer_usb_unlock_bd(cgpu->drv, cgpu->usbinfo.bus_number, cgpu->usbinfo.device_address);
}

void blacklist_cgpu(struct cgpu_info *cgpu)
{
	if (cgpu->blacklisted) {
		applog(LOG_WARNING, "Device already blacklisted");
		return;
	}
	cgpu->blacklisted = true;
	add_in_use(cgpu->usbinfo.bus_number, cgpu->usbinfo.device_address, true);
	if (__release_cgpu(cgpu))
		cgminer_usb_unlock_bd(cgpu->drv, cgpu->usbinfo.bus_number, cgpu->usbinfo.device_address);
}

void whitelist_cgpu(struct cgpu_info *cgpu)
{
	if (!cgpu->blacklisted) {
		applog(LOG_WARNING, "Device not blacklisted");
		return;
	}
	__remove_in_use(cgpu->usbinfo.bus_number, cgpu->usbinfo.device_address, true);
	cgpu->blacklisted = false;
}

/*
 * Force a NODEV on a device so it goes back to hotplug
 */
void usb_nodev(struct cgpu_info *cgpu)
{
	int pstate;

	DEVWLOCK(cgpu, pstate);

	release_cgpu(cgpu);

	DEVWUNLOCK(cgpu, pstate);
}

/*
 * Use the same usbdev thus locking is across all related devices
 */
struct cgpu_info *usb_copy_cgpu(struct cgpu_info *orig)
{
	struct cgpu_info *copy;
	int pstate;

	DEVWLOCK(orig, pstate);

	copy = cgcalloc(1, sizeof(*copy));

	copy->name = orig->name;
	copy->drv = copy_drv(orig->drv);
	copy->deven = orig->deven;
	copy->threads = orig->threads;

	copy->usbdev = orig->usbdev;

	cg_memcpy(&(copy->usbinfo), &(orig->usbinfo), sizeof(copy->usbinfo));

	copy->usbinfo.nodev = (copy->usbdev == NULL);

	DEVWUNLOCK(orig, pstate);

	return copy;
}

struct cgpu_info *usb_alloc_cgpu(struct device_drv *drv, int threads)
{
	struct cgpu_info *cgpu = cgcalloc(1, sizeof(*cgpu));

	cgpu->drv = drv;
	cgpu->deven = DEV_ENABLED;
	cgpu->threads = threads;

	cgpu->usbinfo.nodev = true;

	cglock_init(&cgpu->usbinfo.devlock);

	return cgpu;
}

struct cgpu_info *usb_free_cgpu(struct cgpu_info *cgpu)
{
	if (cgpu->drv->copy)
		free(cgpu->drv);

	free(cgpu->device_path);

	free(cgpu);

	return NULL;
}

#define USB_INIT_FAIL 0
#define USB_INIT_OK 1
#define USB_INIT_IGNORE 2

static int _usb_init(struct cgpu_info *cgpu, struct libusb_device *dev, struct usb_find_devices *found)
{
	unsigned char man[STRBUFLEN+1], prod[STRBUFLEN+1];
	struct cg_usb_device *cgusb = NULL;
	struct libusb_config_descriptor *config = NULL;
	const struct libusb_interface_descriptor *idesc;
	const struct libusb_endpoint_descriptor *epdesc;
	unsigned char strbuf[STRBUFLEN+1];
	char devpath[32];
	char devstr[STRBUFLEN+1];
	int err, ifinfo, epinfo, alt, epnum, pstate;
	int bad = USB_INIT_FAIL;
	int cfg, claimed = 0, i;

	DEVWLOCK(cgpu, pstate);

	cgpu->usbinfo.bus_number = libusb_get_bus_number(dev);
	cgpu->usbinfo.device_address = libusb_get_device_address(dev);

	if (found->intinfo_count > 1) {
		snprintf(devpath, sizeof(devpath), "%d:%d-i%d",
			(int)(cgpu->usbinfo.bus_number),
			(int)(cgpu->usbinfo.device_address),
			THISIF(found, 0));
	} else {
		snprintf(devpath, sizeof(devpath), "%d:%d",
			(int)(cgpu->usbinfo.bus_number),
			(int)(cgpu->usbinfo.device_address));
	}

	cgpu->device_path = strdup(devpath);

	snprintf(devstr, sizeof(devstr), "- %s device %s", found->name, devpath);

	cgusb = cgcalloc(1, sizeof(*cgusb));
	cgusb->found = found;

	if (found->idVendor == IDVENDOR_FTDI)
		cgusb->usb_type = USB_TYPE_FTDI;

	cgusb->ident = found->ident;

	cgusb->descriptor = cgcalloc(1, sizeof(*(cgusb->descriptor)));

	err = libusb_get_device_descriptor(dev, cgusb->descriptor);
	if (err) {
		applog(LOG_DEBUG,
			"USB init failed to get descriptor, err %d %s",
			err, devstr);
		goto dame;
	}

	cg_wlock(&cgusb_fd_lock);
	err = libusb_open(dev, &(cgusb->handle));
	cg_wunlock(&cgusb_fd_lock);
	if (err) {
		switch (err) {
			case LIBUSB_ERROR_ACCESS:
				applog(LOG_ERR,
					"USB init, open device failed, err %d, "
					"you don't have privilege to access %s",
					err, devstr);
				applog(LOG_ERR, "See README file included for help");
				break;
#ifdef WIN32
			// Windows specific message
			case LIBUSB_ERROR_NOT_SUPPORTED:
				applog(LOG_ERR, "USB init, open device failed, err %d, ", err);
				applog(LOG_ERR, "You need to install a WinUSB driver for %s", devstr);
				applog(LOG_ERR, "And associate %s with WinUSB using zadig", devstr);
				applog(LOG_ERR, "See README.txt file included for help");
				break;
#endif
			default:
				applog(LOG_DEBUG,
					"USB init, open failed, err %d %s",
					err, devstr);
		}
		goto dame;
	}

#ifdef LINUX
	for (ifinfo = 0; ifinfo < found->intinfo_count; ifinfo++) {
		if (libusb_kernel_driver_active(cgusb->handle, THISIF(found, ifinfo)) == 1) {
			applog(LOG_DEBUG, "USB init, kernel attached ... %s", devstr);
			err = libusb_detach_kernel_driver(cgusb->handle, THISIF(found, ifinfo));
			if (err == 0) {
				applog(LOG_DEBUG,
					"USB init, kernel detached ifinfo %d interface %d"
					" successfully %s",
					ifinfo, THISIF(found, ifinfo), devstr);
			} else {
				applog(LOG_WARNING,
					"USB init, kernel detach ifinfo %d interface %d failed,"
					" err %d in use? %s",
					ifinfo, THISIF(found, ifinfo), err, devstr);
				goto nokernel;
			}
		}
	}
#endif

	err = libusb_get_string_descriptor_ascii(cgusb->handle,
							cgusb->descriptor->iManufacturer,
							man, STRBUFLEN);
	if (err < 0) {
		applog(LOG_DEBUG,
			"USB init, failed to get iManufacturer, err %d %s",
			err, devstr);
		goto cldame;
	}
	if (found->iManufacturer) {
		if (strcmp((char *)man, found->iManufacturer)) {
			applog(LOG_DEBUG, "USB init, iManufacturer mismatch %s",
			       devstr);
			applog(LOG_DEBUG, "Found %s vs %s", man, found->iManufacturer);
			bad = USB_INIT_IGNORE;
			goto cldame;
		}
	} else {
		for (i = 0; find_dev[i].drv != DRIVER_MAX; i++) {
			const char *iManufacturer = find_dev[i].iManufacturer;
			/* If other drivers has an iManufacturer set that match,
			 * don't try to claim this device. */

			if (!iManufacturer)
				continue;
			/* If the alternative driver also has an iProduct, only
			 * use that for comparison. */
			if (find_dev[i].iProduct)
				continue;
			if (!strcmp((char *)man, iManufacturer)) {
				applog(LOG_DEBUG, "USB init, alternative iManufacturer match %s",
				       devstr);
				applog(LOG_DEBUG, "Found %s", iManufacturer);
				bad = USB_INIT_IGNORE;
				goto cldame;
			}
		}
	}

	err = libusb_get_string_descriptor_ascii(cgusb->handle,
							cgusb->descriptor->iProduct,
							prod, STRBUFLEN);
	if (err < 0) {
		applog(LOG_DEBUG,
			"USB init, failed to get iProduct, err %d %s",
			err, devstr);
		goto cldame;
	}
	if (found->iProduct) {
		if (strcasecmp((char *)prod, found->iProduct)) {
			applog(LOG_DEBUG, "USB init, iProduct mismatch %s",
			       devstr);
			applog(LOG_DEBUG, "Found %s vs %s", prod, found->iProduct);
			bad = USB_INIT_IGNORE;
			goto cldame;
		}
	} else {
		for (i = 0; find_dev[i].drv != DRIVER_MAX; i++) {
			const char *iProduct = find_dev[i].iProduct;
			/* Do same for iProduct as iManufacturer above */

			if (!iProduct)
				continue;
			if (!strcasecmp((char *)prod, iProduct)) {
				applog(LOG_DEBUG, "USB init, alternative iProduct match %s",
				       devstr);
				applog(LOG_DEBUG, "Found %s", iProduct);
				bad = USB_INIT_IGNORE;
				goto cldame;
			}
		}
	}

	cfg = -1;
	err = libusb_get_configuration(cgusb->handle, &cfg);
	if (err)
		cfg = -1;

	// Try to set it if we can't read it or it's different
	if (cfg != found->config) {
		err = libusb_set_configuration(cgusb->handle, found->config);
		if (err) {
			switch(err) {
				case LIBUSB_ERROR_BUSY:
					applog(LOG_WARNING,
						"USB init, set config %d in use %s",
						found->config, devstr);
					break;
				default:
					applog(LOG_DEBUG,
						"USB init, failed to set config to %d, err %d %s",
						found->config, err, devstr);
			}
			goto cldame;
		}
	}

	err = libusb_get_active_config_descriptor(dev, &config);
	if (err) {
		applog(LOG_DEBUG,
			"USB init, failed to get config descriptor, err %d %s",
			err, devstr);
		goto cldame;
	}

	int imax = -1;
	for (ifinfo = 0; ifinfo < found->intinfo_count; ifinfo++)
		if (found->intinfos[ifinfo].interface > imax)
			imax = found->intinfos[ifinfo].interface;

	if ((int)(config->bNumInterfaces) <= imax) {
		applog(LOG_DEBUG, "USB init bNumInterfaces %d <= interface max %d for %s",
		       (int)(config->bNumInterfaces), imax, devstr);
		goto cldame;
	}

	for (ifinfo = 0; ifinfo < found->intinfo_count; ifinfo++)
		for (epinfo = 0; epinfo < found->intinfos[ifinfo].epinfo_count; epinfo++)
			found->intinfos[ifinfo].epinfos[epinfo].found = false;

	for (ifinfo = 0; ifinfo < found->intinfo_count; ifinfo++) {
		int interface = found->intinfos[ifinfo].interface;
		for (alt = 0; alt < config->interface[interface].num_altsetting; alt++) {
			idesc = &(config->interface[interface].altsetting[alt]);
			for (epnum = 0; epnum < (int)(idesc->bNumEndpoints); epnum++) {
				struct usb_epinfo *epinfos = found->intinfos[ifinfo].epinfos;
				epdesc = &(idesc->endpoint[epnum]);
				for (epinfo = 0; epinfo < found->intinfos[ifinfo].epinfo_count; epinfo++) {
					if (!epinfos[epinfo].found) {
						if (epdesc->bmAttributes == epinfos[epinfo].att
						&&  epdesc->wMaxPacketSize >= epinfos[epinfo].size
						&&  epdesc->bEndpointAddress == epinfos[epinfo].ep) {
							epinfos[epinfo].found = true;
							epinfos[epinfo].wMaxPacketSize = epdesc->wMaxPacketSize;
							break;
						}
					}
				}
			}
		}
	}

	for (ifinfo = 0; ifinfo < found->intinfo_count; ifinfo++)
		for (epinfo = 0; epinfo < found->intinfos[ifinfo].epinfo_count; epinfo++)
			if (found->intinfos[ifinfo].epinfos[epinfo].found == false) {
				applog(LOG_DEBUG, "USB init found (%d,%d) == false %s",
				       ifinfo, epinfo, devstr);
				goto cldame;
			}

	claimed = 0;
	for (ifinfo = 0; ifinfo < found->intinfo_count; ifinfo++) {
		err = libusb_claim_interface(cgusb->handle, THISIF(found, ifinfo));
		if (err == 0)
			claimed++;
		else {
			switch(err) {
				case LIBUSB_ERROR_BUSY:
					applog(LOG_WARNING,
						"USB init, claim ifinfo %d interface %d in use %s",
						ifinfo, THISIF(found, ifinfo), devstr);
					break;
				default:
					applog(LOG_DEBUG,
						"USB init, claim ifinfo %d interface %d failed,"
						" err %d %s",
						ifinfo, THISIF(found, ifinfo), err, devstr);
			}
			goto reldame;
		}
	}

	cfg = -1;
	err = libusb_get_configuration(cgusb->handle, &cfg);
	if (err)
		cfg = -1;
	if (cfg != found->config) {
		applog(LOG_WARNING,
			"USB init, incorrect config (%d!=%d) after claim of %s",
			cfg, found->config, devstr);
		goto reldame;
	}

	cgusb->usbver = cgusb->descriptor->bcdUSB;
	if (cgusb->usbver < 0x0200) {
		cgusb->usb11 = true;
		cgusb->tt = true;
	}

// TODO: allow this with the right version of the libusb include and running library
//	cgusb->speed = libusb_get_device_speed(dev);

	err = libusb_get_string_descriptor_ascii(cgusb->handle,
				cgusb->descriptor->iProduct, strbuf, STRBUFLEN);
	if (err > 0)
		cgusb->prod_string = strdup((char *)strbuf);
	else
		cgusb->prod_string = (char *)BLANK;

	err = libusb_get_string_descriptor_ascii(cgusb->handle,
				cgusb->descriptor->iManufacturer, strbuf, STRBUFLEN);
	if (err > 0)
		cgusb->manuf_string = strdup((char *)strbuf);
	else
		cgusb->manuf_string = (char *)BLANK;

	err = libusb_get_string_descriptor_ascii(cgusb->handle,
				cgusb->descriptor->iSerialNumber, strbuf, STRBUFLEN);
	if (err > 0)
		cgusb->serial_string = strdup((char *)strbuf);
	else
		cgusb->serial_string = (char *)BLANK;

// TODO: ?
//	cgusb->fwVersion <- for temp1/temp2 decision? or serial? (driver-modminer.c)
//	cgusb->interfaceVersion

	applog(LOG_DEBUG,
		"USB init %s usbver=%04x prod='%s' manuf='%s' serial='%s'",
		devstr, cgusb->usbver, cgusb->prod_string,
		cgusb->manuf_string, cgusb->serial_string);

	cgpu->usbdev = cgusb;
	cgpu->usbinfo.nodev = false;

	libusb_free_config_descriptor(config);

	// Allow a name change based on the idVendor+idProduct
	// N.B. must be done before calling add_cgpu()
	if (strcasecmp(cgpu->drv->name, found->name)) {
		if (!cgpu->drv->copy)
			cgpu->drv = copy_drv(cgpu->drv);
		cgpu->drv->name = (char *)(found->name);
	}

	bad = USB_INIT_OK;
	goto out_unlock;

reldame:

	ifinfo = claimed;
	while (ifinfo-- > 0)
		libusb_release_interface(cgusb->handle, THISIF(found, ifinfo));

cldame:
#ifdef LINUX
	libusb_attach_kernel_driver(cgusb->handle, THISIF(found, ifinfo));

nokernel:
#endif
	cg_wlock(&cgusb_fd_lock);
	libusb_close(cgusb->handle);
	cgusb->handle = NULL;
	cg_wunlock(&cgusb_fd_lock);

dame:

	if (config)
		libusb_free_config_descriptor(config);

	cgusb = free_cgusb(cgusb);

out_unlock:
	DEVWUNLOCK(cgpu, pstate);

	return bad;
}

bool usb_init(struct cgpu_info *cgpu, struct libusb_device *dev, struct usb_find_devices *found_match)
{
	struct usb_find_devices *found_use = NULL;
	int uninitialised_var(ret);
	int i;

	for (i = 0; find_dev[i].drv != DRIVER_MAX; i++) {
		if (find_dev[i].drv == found_match->drv &&
		    find_dev[i].idVendor == found_match->idVendor &&
		    find_dev[i].idProduct == found_match->idProduct) {
			found_use = cgmalloc(sizeof(*found_use));
			cg_memcpy(found_use, &(find_dev[i]), sizeof(*found_use));

			ret = _usb_init(cgpu, dev, found_use);

			if (ret != USB_INIT_IGNORE)
				break;
		}
	}

	if (ret == USB_INIT_FAIL) {
		applog(LOG_ERR, "%s detect (%d:%d) failed to initialise (incorrect device?), resetting",
				cgpu->drv->dname,
				(int)(cgpu->usbinfo.bus_number),
				(int)(cgpu->usbinfo.device_address));
		if (cgpu->usbdev && cgpu->usbdev->handle)
			libusb_reset_device(cgpu->usbdev->handle);
	}

	return (ret == USB_INIT_OK);
}

static bool usb_check_device(struct device_drv *drv, struct libusb_device *dev, struct usb_find_devices *look)
{
	struct libusb_device_descriptor desc;
	int bus_number, device_address;
	int err, i;
	bool ok;

	err = libusb_get_device_descriptor(dev, &desc);
	if (err) {
		applog(LOG_DEBUG, "USB check device: Failed to get descriptor, err %d", err);
		return false;
	}

	if (desc.idVendor != look->idVendor || desc.idProduct != look->idProduct) {
		applog(LOG_DEBUG, "%s looking for %s %04x:%04x but found %04x:%04x instead",
			drv->name, look->name, look->idVendor, look->idProduct, desc.idVendor, desc.idProduct);

		return false;
	}

	if (busdev_count > 0) {
		bus_number = (int)libusb_get_bus_number(dev);
		device_address = (int)libusb_get_device_address(dev);
		ok = false;
		for (i = 0; i < busdev_count; i++) {
			if (bus_number == busdev[i].bus_number) {
				if (busdev[i].device_address == -1 ||
				    device_address == busdev[i].device_address) {
					ok = true;
					break;
				}
			}
		}
		if (!ok) {
			applog(LOG_DEBUG, "%s rejected %s %04x:%04x with bus:dev (%d:%d)",
				drv->name, look->name, look->idVendor, look->idProduct,
				bus_number, device_address);
			return false;
		}
	}

	applog(LOG_DEBUG, "%s looking for and found %s %04x:%04x",
		drv->name, look->name, look->idVendor, look->idProduct);

	return true;
}

static struct usb_find_devices *usb_check_each(int drvnum, struct device_drv *drv, struct libusb_device *dev)
{
	struct usb_find_devices *found;
	int i;

	for (i = 0; find_dev[i].drv != DRIVER_MAX; i++)
		if (find_dev[i].drv == drvnum) {
			if (usb_check_device(drv, dev, &(find_dev[i]))) {
				found = cgmalloc(sizeof(*found));
				cg_memcpy(found, &(find_dev[i]), sizeof(*found));
				return found;
			}
		}

	return NULL;
}

#define DRIVER_USB_CHECK_EACH(X) 	if (drv->drv_id == DRIVER_##X) \
		return usb_check_each(DRIVER_##X, drv, dev);

static struct usb_find_devices *usb_check(__maybe_unused struct device_drv *drv, __maybe_unused struct libusb_device *dev)
{
	if (drv_count[drv->drv_id].count >= drv_count[drv->drv_id].limit) {
		applog(LOG_DEBUG,
			"USB scan devices3: %s limit %d reached",
			drv->dname, drv_count[drv->drv_id].limit);
		return NULL;
	}

	DRIVER_PARSE_COMMANDS(DRIVER_USB_CHECK_EACH)

	return NULL;
}

void __usb_detect(struct device_drv *drv, struct cgpu_info *(*device_detect)(struct libusb_device *, struct usb_find_devices *),
		  bool single)
{
	libusb_device **list;
	ssize_t count, i;
	struct usb_find_devices *found;
	struct cgpu_info *cgpu;

	applog(LOG_DEBUG, "USB scan devices: checking for %s devices", drv->name);

	if (total_count >= total_limit) {
		applog(LOG_DEBUG, "USB scan devices: total limit %d reached", total_limit);
		return;
	}

	if (drv_count[drv->drv_id].count >= drv_count[drv->drv_id].limit) {
		applog(LOG_DEBUG,
			"USB scan devices: %s limit %d reached",
			drv->dname, drv_count[drv->drv_id].limit);
		return;
	}

	count = libusb_get_device_list(NULL, &list);
	if (count < 0) {
		applog(LOG_DEBUG, "USB scan devices: failed, err %d", (int)count);
		return;
	}

	if (count == 0)
		applog(LOG_DEBUG, "USB scan devices: found no devices");
	else
		cgsleep_ms(166);

	for (i = 0; i < count; i++) {
		if (total_count >= total_limit) {
			applog(LOG_DEBUG, "USB scan devices2: total limit %d reached", total_limit);
			break;
		}

		if (drv_count[drv->drv_id].count >= drv_count[drv->drv_id].limit) {
			applog(LOG_DEBUG,
				"USB scan devices2: %s limit %d reached",
				drv->dname, drv_count[drv->drv_id].limit);
			break;
		}

		found = usb_check(drv, list[i]);
		if (found != NULL) {
			bool new_dev = false;

			if (is_in_use(list[i]) || cgminer_usb_lock(drv, list[i]) == false)
				free(found);
			else {
				cgpu = device_detect(list[i], found);
				if (!cgpu)
					cgminer_usb_unlock(drv, list[i]);
				else {
					new_dev = true;
					cgpu->usbinfo.initialised = true;
					total_count++;
					drv_count[drv->drv_id].count++;
				}
				free(found);
			}
			if (single && new_dev)
				break;
		}
	}

	libusb_free_device_list(list, 1);
}

#if DO_USB_STATS
static void modes_str(char *buf, uint32_t modes)
{
	bool first;

	*buf = '\0';

	if (modes == MODE_NONE)
		strcpy(buf, MODE_NONE_STR);
	else {
		first = true;

		if (modes & MODE_CTRL_READ) {
			strcpy(buf, MODE_CTRL_READ_STR);
			first = false;
		}

		if (modes & MODE_CTRL_WRITE) {
			if (!first)
				strcat(buf, MODE_SEP_STR);
			strcat(buf, MODE_CTRL_WRITE_STR);
			first = false;
		}

		if (modes & MODE_BULK_READ) {
			if (!first)
				strcat(buf, MODE_SEP_STR);
			strcat(buf, MODE_BULK_READ_STR);
			first = false;
		}

		if (modes & MODE_BULK_WRITE) {
			if (!first)
				strcat(buf, MODE_SEP_STR);
			strcat(buf, MODE_BULK_WRITE_STR);
			first = false;
		}
	}
}
#endif

// The stat data can be spurious due to not locking it before copying it -
// however that would require the stat() function to also lock and release
// a mutex every time a usb read or write is called which would slow
// things down more
struct api_data *api_usb_stats(__maybe_unused int *count)
{
#if DO_USB_STATS
	struct cg_usb_stats_details *details;
	struct cg_usb_stats *sta;
	struct api_data *root = NULL;
	int device;
	int cmdseq;
	char modes_s[32];

	if (next_stat == USB_NOSTAT)
		return NULL;

	while (*count < next_stat * C_MAX * 2) {
		device = *count / (C_MAX * 2);
		cmdseq = *count % (C_MAX * 2);

		(*count)++;

		sta = &(usb_stats[device]);
		details = &(sta->details[cmdseq]);

		// Only show stats that have results
		if (details->item[CMD_CMD].count == 0 &&
		    details->item[CMD_TIMEOUT].count == 0 &&
		    details->item[CMD_ERROR].count == 0)
			continue;

		root = api_add_string(root, "Name", sta->name, false);
		root = api_add_int(root, "ID", &(sta->device_id), false);
		root = api_add_const(root, "Stat", usb_commands[cmdseq/2], false);
		root = api_add_int(root, "Seq", &(details->seq), true);
		modes_str(modes_s, details->modes);
		root = api_add_string(root, "Modes", modes_s, true);
		root = api_add_uint64(root, "Count",
					&(details->item[CMD_CMD].count), true);
		root = api_add_double(root, "Total Delay",
					&(details->item[CMD_CMD].total_delay), true);
		root = api_add_double(root, "Min Delay",
					&(details->item[CMD_CMD].min_delay), true);
		root = api_add_double(root, "Max Delay",
					&(details->item[CMD_CMD].max_delay), true);
		root = api_add_uint64(root, "Timeout Count",
					&(details->item[CMD_TIMEOUT].count), true);
		root = api_add_double(root, "Timeout Total Delay",
					&(details->item[CMD_TIMEOUT].total_delay), true);
		root = api_add_double(root, "Timeout Min Delay",
					&(details->item[CMD_TIMEOUT].min_delay), true);
		root = api_add_double(root, "Timeout Max Delay",
					&(details->item[CMD_TIMEOUT].max_delay), true);
		root = api_add_uint64(root, "Error Count",
					&(details->item[CMD_ERROR].count), true);
		root = api_add_double(root, "Error Total Delay",
					&(details->item[CMD_ERROR].total_delay), true);
		root = api_add_double(root, "Error Min Delay",
					&(details->item[CMD_ERROR].min_delay), true);
		root = api_add_double(root, "Error Max Delay",
					&(details->item[CMD_ERROR].max_delay), true);
		root = api_add_timeval(root, "First Command",
					&(details->item[CMD_CMD].first), true);
		root = api_add_timeval(root, "Last Command",
					&(details->item[CMD_CMD].last), true);
		root = api_add_timeval(root, "First Timeout",
					&(details->item[CMD_TIMEOUT].first), true);
		root = api_add_timeval(root, "Last Timeout",
					&(details->item[CMD_TIMEOUT].last), true);
		root = api_add_timeval(root, "First Error",
					&(details->item[CMD_ERROR].first), true);
		root = api_add_timeval(root, "Last Error",
					&(details->item[CMD_ERROR].last), true);

		return root;
	}
#endif
	return NULL;
}

#if DO_USB_STATS
static void newstats(struct cgpu_info *cgpu)
{
	int i;

	mutex_lock(&cgusb_lock);

	cgpu->usbinfo.usbstat = next_stat + 1;

	usb_stats = cgrealloc(usb_stats, sizeof(*usb_stats) * (next_stat+1));
	usb_stats[next_stat].name = cgpu->drv->name;
	usb_stats[next_stat].device_id = -1;
	usb_stats[next_stat].details = cgcalloc(2, sizeof(struct cg_usb_stats_details) * (C_MAX + 1));

	for (i = 1; i < C_MAX * 2; i += 2)
		usb_stats[next_stat].details[i].seq = 1;

	next_stat++;

	mutex_unlock(&cgusb_lock);
}
#endif

void update_usb_stats(__maybe_unused struct cgpu_info *cgpu)
{
#if DO_USB_STATS
	if (cgpu->usbinfo.usbstat < 1)
		newstats(cgpu);

	// we don't know the device_id until after add_cgpu()
	usb_stats[cgpu->usbinfo.usbstat - 1].device_id = cgpu->device_id;
#endif
}

#if DO_USB_STATS
static void stats(struct cgpu_info *cgpu, struct timeval *tv_start, struct timeval *tv_finish, int err, int mode, enum usb_cmds cmd, int seq, int timeout)
{
	struct cg_usb_stats_details *details;
	double diff;
	int item, extrams;

	if (cgpu->usbinfo.usbstat < 1)
		newstats(cgpu);

	cgpu->usbinfo.tmo_count++;

	// timeout checks are only done when stats are enabled
	extrams = SECTOMS(tdiff(tv_finish, tv_start)) - timeout;
	if (extrams >= USB_TMO_0) {
		uint32_t totms = (uint32_t)(timeout + extrams);
		int offset = 0;

		if (extrams >= USB_TMO_2) {
			applog(LOG_INFO, "%s%i: TIMEOUT %s took %dms but was %dms",
					cgpu->drv->name, cgpu->device_id,
					usb_cmdname(cmd), totms, timeout) ;
			offset = 2;
		} else if (extrams >= USB_TMO_1)
			offset = 1;

		cgpu->usbinfo.usb_tmo[offset].count++;
		cgpu->usbinfo.usb_tmo[offset].total_over += extrams;
		cgpu->usbinfo.usb_tmo[offset].total_tmo += timeout;
		if (cgpu->usbinfo.usb_tmo[offset].min_tmo == 0) {
			cgpu->usbinfo.usb_tmo[offset].min_tmo = totms;
			cgpu->usbinfo.usb_tmo[offset].max_tmo = totms;
		} else {
			if (cgpu->usbinfo.usb_tmo[offset].min_tmo > totms)
				cgpu->usbinfo.usb_tmo[offset].min_tmo = totms;
			if (cgpu->usbinfo.usb_tmo[offset].max_tmo < totms)
				cgpu->usbinfo.usb_tmo[offset].max_tmo = totms;
		}
	}

	details = &(usb_stats[cgpu->usbinfo.usbstat - 1].details[cmd * 2 + seq]);
	details->modes |= mode;

	diff = tdiff(tv_finish, tv_start);

	switch (err) {
		case LIBUSB_SUCCESS:
			item = CMD_CMD;
			break;
		case LIBUSB_ERROR_TIMEOUT:
			item = CMD_TIMEOUT;
			break;
		default:
			item = CMD_ERROR;
			break;
	}

	if (details->item[item].count == 0) {
		details->item[item].min_delay = diff;
		cg_memcpy(&(details->item[item].first), tv_start, sizeof(*tv_start));
	} else if (diff < details->item[item].min_delay)
		details->item[item].min_delay = diff;

	if (diff > details->item[item].max_delay)
		details->item[item].max_delay = diff;

	details->item[item].total_delay += diff;
	cg_memcpy(&(details->item[item].last), tv_start, sizeof(*tv_start));
	details->item[item].count++;
}

static void rejected_inc(struct cgpu_info *cgpu, uint32_t mode)
{
	struct cg_usb_stats_details *details;
	int item = CMD_ERROR;

	if (cgpu->usbinfo.usbstat < 1)
		newstats(cgpu);

	details = &(usb_stats[cgpu->usbinfo.usbstat - 1].details[C_REJECTED * 2 + 0]);
	details->modes |= mode;
	details->item[item].count++;
}
#endif

#define USB_RETRY_MAX 5

struct usb_transfer {
	cgsem_t cgsem;
	struct libusb_transfer *transfer;
	bool cancellable;
	struct list_head list;
};

bool async_usb_transfers(void)
{
	bool ret;

	cg_rlock(&cgusb_fd_lock);
	ret = !list_empty(&ut_list);
	cg_runlock(&cgusb_fd_lock);

	return ret;
}

/* Cancellable transfers should only be labelled as such if it is safe for them
 * to effectively mimic timing out early. This flag is usually used to signify
 * a read is waiting on a non-critical response that takes a long time and the
 * driver wishes it be aborted if work restart message has been sent. */
void cancel_usb_transfers(void)
{
	struct usb_transfer *ut;
	int cancellations = 0;

	cg_wlock(&cgusb_fd_lock);
	list_for_each_entry(ut, &ut_list, list) {
		if (ut->cancellable) {
			ut->cancellable = false;
			libusb_cancel_transfer(ut->transfer);
			cancellations++;
		}
	}
	cg_wunlock(&cgusb_fd_lock);

	if (cancellations)
		applog(LOG_DEBUG, "Cancelled %d USB transfers", cancellations);
}

static void init_usb_transfer(struct usb_transfer *ut)
{
	cgsem_init(&ut->cgsem);
	ut->transfer = libusb_alloc_transfer(0);
	if (unlikely(!ut->transfer))
		quit(1, "Failed to libusb_alloc_transfer");
	ut->transfer->user_data = ut;
	ut->cancellable = false;
}

static void complete_usb_transfer(struct usb_transfer *ut)
{
	cg_wlock(&cgusb_fd_lock);
	list_del(&ut->list);
	cg_wunlock(&cgusb_fd_lock);

	cgsem_destroy(&ut->cgsem);
	libusb_free_transfer(ut->transfer);
}

static void LIBUSB_CALL transfer_callback(struct libusb_transfer *transfer)
{
	struct usb_transfer *ut = transfer->user_data;

	ut->cancellable = false;
	cgsem_post(&ut->cgsem);
}

static int usb_transfer_toerr(int ret)
{
	if (ret <= 0)
		return ret;

	switch (ret) {
		default:
		case LIBUSB_TRANSFER_COMPLETED:
			ret = LIBUSB_SUCCESS;
			break;
		case LIBUSB_TRANSFER_ERROR:
			ret = LIBUSB_ERROR_IO;
			break;
		case LIBUSB_TRANSFER_TIMED_OUT:
		case LIBUSB_TRANSFER_CANCELLED:
			ret = LIBUSB_ERROR_TIMEOUT;
			break;
		case LIBUSB_TRANSFER_STALL:
			ret = LIBUSB_ERROR_PIPE;
			break;
		case LIBUSB_TRANSFER_NO_DEVICE:
			ret = LIBUSB_ERROR_NO_DEVICE;
			break;
		case LIBUSB_TRANSFER_OVERFLOW:
			ret = LIBUSB_ERROR_OVERFLOW;
			break;
	}
	return ret;
}

/* Wait for callback function to tell us it has finished the USB transfer, but
 * use our own timer to cancel the request if we go beyond the timeout. */
static int callback_wait(struct usb_transfer *ut, int *transferred, unsigned int timeout)
{
	struct libusb_transfer *transfer= ut->transfer;
	int ret;

	ret = cgsem_mswait(&ut->cgsem, timeout);
	if (ret == ETIMEDOUT) {
		/* We are emulating a timeout ourself here */
		libusb_cancel_transfer(transfer);

		/* Now wait for the callback function to be invoked. */
		cgsem_wait(&ut->cgsem);
	}
	ret = transfer->status;
	ret = usb_transfer_toerr(ret);

	/* No need to sort out mutexes here since they won't be reused */
	*transferred = transfer->actual_length;

	return ret;
}

static int usb_submit_transfer(struct usb_transfer *ut, struct libusb_transfer *transfer,
			       bool cancellable, bool tt)
{
	int err;

	INIT_LIST_HEAD(&ut->list);

	cg_wlock(&cgusb_fd_lock);
	/* Imitate a transaction translator for writes to usb1.1 devices */
	if (tt)
		cgsleep_ms_r(&usb11_cgt, 1);
	err = libusb_submit_transfer(transfer);
	if (likely(!err))
		ut->cancellable = cancellable;
	list_add(&ut->list, &ut_list);
	if (tt)
		cgtimer_time(&usb11_cgt);
	cg_wunlock(&cgusb_fd_lock);

	return err;
}

static int
usb_perform_transfer(struct cgpu_info *cgpu, struct cg_usb_device *usbdev, int intinfo,
		  int epinfo, unsigned char *data, int length, int *transferred,
		  unsigned int timeout, __maybe_unused int mode, enum usb_cmds cmd,
		  __maybe_unused int seq, bool cancellable, bool tt)
{
	int bulk_timeout, callback_timeout = timeout, err_retries = 0;
	struct libusb_device_handle *dev_handle = usbdev->handle;
	struct usb_epinfo *usb_epinfo;
	struct usb_transfer ut;
	unsigned char endpoint;
	bool interrupt;
	int err, errn;
#if DO_USB_STATS
	struct timeval tv_start, tv_finish;
#endif
	unsigned char buf[512];
#ifdef WIN32
	/* On windows the callback_timeout is a safety mechanism only. */
	bulk_timeout = timeout;
	callback_timeout += WIN_CALLBACK_EXTRA;
#else
	/* We give the transfer no timeout since we manage timeouts ourself on
	 * non windows. */
	bulk_timeout = 0;
#endif

	usb_epinfo = &(usbdev->found->intinfos[intinfo].epinfos[epinfo]);
	interrupt = usb_epinfo->att == LIBUSB_TRANSFER_TYPE_INTERRUPT;
	endpoint = usb_epinfo->ep;

	if (unlikely(!data)) {
		applog(LOG_ERR, "USB error: usb_perform_transfer sent NULL data (%s,intinfo=%d,epinfo=%d,length=%d,timeout=%u,mode=%d,cmd=%s,seq=%d) endpoint=%d",
		       cgpu->drv->name, intinfo, epinfo, length, timeout, mode, usb_cmdname(cmd), seq, (int)endpoint);
		err = LIBUSB_ERROR_IO;
		goto out_fail;
	}
	/* Avoid any async transfers during shutdown to allow the polling
	 * thread to be shut down after all existing transfers are complete */
	if (opt_lowmem || cgpu->shutdown)
		return libusb_bulk_transfer(dev_handle, endpoint, data, length, transferred, timeout);
err_retry:
	init_usb_transfer(&ut);

	if ((endpoint & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT) {
		cg_memcpy(buf, data, length);
#ifndef HAVE_LIBUSB
		/* Older versions may not have this feature so only enable it
		 * when we know we're compiling with included static libusb. We
		 * only do this for bulk transfer, not interrupt. */
		if (!cgpu->nozlp && !interrupt)
			ut.transfer->flags |= LIBUSB_TRANSFER_ADD_ZERO_PACKET;
#endif
#ifdef WIN32
		/* Writes on windows really don't like to be cancelled, but
		 * are prone to timeouts under heavy USB traffic, so make this
		 * a last resort cancellation delayed long after the write
		 * would have timed out on its own. */
		callback_timeout += WIN_WRITE_CBEXTRA;
#endif
	}

	USBDEBUG("USB debug: @usb_perform_transfer(%s (nodev=%s),intinfo=%d,epinfo=%d,data=%p,length=%d,timeout=%u,mode=%d,cmd=%s,seq=%d) endpoint=%d", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), intinfo, epinfo, data, length, timeout, mode, usb_cmdname(cmd), seq, (int)endpoint);

	if (interrupt) {
		libusb_fill_interrupt_transfer(ut.transfer, dev_handle, endpoint,
					       buf, length, transfer_callback, &ut,
				 bulk_timeout);
	} else {
		libusb_fill_bulk_transfer(ut.transfer, dev_handle, endpoint, buf,
					  length, transfer_callback, &ut, bulk_timeout);
	}
	STATS_TIMEVAL(&tv_start);
	err = usb_submit_transfer(&ut, ut.transfer, cancellable, tt);
	errn = errno;
	if (!err)
		err = callback_wait(&ut, transferred, callback_timeout);
	else
		err = usb_transfer_toerr(err);
	complete_usb_transfer(&ut);

	STATS_TIMEVAL(&tv_finish);
	USB_STATS(cgpu, &tv_start, &tv_finish, err, mode, cmd, seq, timeout);

	if (err < 0) {
		applog(LOG_DEBUG, "%s%i: %s (amt=%d err=%d ern=%d)",
				cgpu->drv->name, cgpu->device_id,
				usb_cmdname(cmd), *transferred, err, errn);
	}

	if (err == LIBUSB_ERROR_PIPE) {
		int pipeerr, retries = 0;

		do {
			cgpu->usbinfo.last_pipe = time(NULL);
			cgpu->usbinfo.pipe_count++;
			applog(LOG_INFO, "%s%i: libusb pipe error, trying to clear",
				cgpu->drv->name, cgpu->device_id);
			pipeerr = libusb_clear_halt(dev_handle, endpoint);
			applog(LOG_DEBUG, "%s%i: libusb pipe error%scleared",
				cgpu->drv->name, cgpu->device_id, err ? " not " : " ");

			if (pipeerr)
				cgpu->usbinfo.clear_fail_count++;
		} while (pipeerr && ++retries < USB_RETRY_MAX);
		if (!pipeerr && ++err_retries < USB_RETRY_MAX)
			goto err_retry;
	}
	if (err == LIBUSB_ERROR_IO && ++err_retries < USB_RETRY_MAX)
		goto err_retry;
out_fail:
	if (NODEV(err))
		*transferred = 0;
	else if ((endpoint & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN && *transferred)
		cg_memcpy(data, buf, *transferred);

	return err;
}

void usb_reset(struct cgpu_info *cgpu)
{
	int pstate, err = 0;

	DEVWLOCK(cgpu, pstate);
	if (!cgpu->usbinfo.nodev)
	{
		err = libusb_reset_device(cgpu->usbdev->handle);
		if (err == LIBUSB_SUCCESS)
		{
			applog(LOG_WARNING, "%s %i reset succeess",
				cgpu->drv->name, cgpu->device_id);
		}
		else
		{
			applog(LOG_WARNING, "%s %i attempted reset got err:(%d) %s",
				cgpu->drv->name, cgpu->device_id, err, libusb_error_name(err));
		}
	}
	if (NODEV(err))
		release_cgpu(cgpu);
	DEVWUNLOCK(cgpu, pstate);
}

int _usb_read(struct cgpu_info *cgpu, int intinfo, int epinfo, char *buf, size_t bufsiz,
	      int *processed, int timeout, const char *end, enum usb_cmds cmd, bool readonce, bool cancellable)
{
	unsigned char *ptr, usbbuf[USB_READ_BUFSIZE];
	struct timeval read_start, tv_finish;
	int bufleft, err, got, tot, pstate, tried_reset;
	struct cg_usb_device *usbdev;
	unsigned int initial_timeout;
	bool first = true;
	size_t usbbufread;
	int endlen = 0;
	char *eom = NULL;
	double done;
	bool ftdi;

	memset(usbbuf, 0, USB_READ_BUFSIZE);
	memset(buf, 0, bufsiz);

	if (end)
		endlen = strlen(end);

	DEVRLOCK(cgpu, pstate);
	if (cgpu->usbinfo.nodev) {
		*processed = 0;
		USB_REJECT(cgpu, MODE_BULK_READ);

		err = LIBUSB_ERROR_NO_DEVICE;
		goto out_noerrmsg;
	}

	usbdev = cgpu->usbdev;
	/* Interrupt transfers are guaranteed to be of an expected size (we hope) */
	if (usbdev->found->intinfos[intinfo].epinfos[epinfo].att == LIBUSB_TRANSFER_TYPE_INTERRUPT)
		usbbufread = bufsiz;
	else
		usbbufread = 512;

	ftdi = (usbdev->usb_type == USB_TYPE_FTDI);

	USBDEBUG("USB debug: _usb_read(%s (nodev=%s),intinfo=%d,epinfo=%d,buf=%p,bufsiz=%d,proc=%p,timeout=%u,end=%s,cmd=%s,ftdi=%s,readonce=%s)", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), intinfo, epinfo, buf, (int)bufsiz, processed, timeout, end ? (char *)str_text((char *)end) : "NULL", usb_cmdname(cmd), bool_str(ftdi), bool_str(readonce));

	if (bufsiz > USB_MAX_READ)
		quit(1, "%s USB read request %d too large (max=%d)", cgpu->drv->name, (int)bufsiz, USB_MAX_READ);

	if (timeout == DEVTIMEOUT)
		timeout = usbdev->found->timeout;

	tot = usbdev->bufamt;
	bufleft = bufsiz - tot;
	if (tot)
		cg_memcpy(usbbuf, usbdev->buffer, tot);
	ptr = usbbuf + tot;
	usbdev->bufamt = 0;

	err = LIBUSB_SUCCESS;
	if (end != NULL)
		eom = strstr((const char *)usbbuf, end);

	initial_timeout = timeout;
	cgtime(&read_start);
	tried_reset = 0;
	while (bufleft > 0 && !eom) {
		err = usb_perform_transfer(cgpu, usbdev, intinfo, epinfo, ptr, usbbufread,
					&got, timeout, MODE_BULK_READ, cmd,
					first ? SEQ0 : SEQ1, cancellable, false);
		if (NODEV(err))
			goto out_noerrmsg;

		cgtime(&tv_finish);
		ptr[got] = '\0';

		USBDEBUG("USB debug: @_usb_read(%s (nodev=%s)) first=%s err=%d%s got=%d ptr='%s' usbbufread=%d", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), bool_str(first), err, isnodev(err), got, (char *)str_text((char *)ptr), (int)usbbufread);

		if (ftdi) {
			// first 2 bytes returned are an FTDI status
			if (got > 2) {
				got -= 2;
				memmove(ptr, ptr+2, got+1);
			} else {
				got = 0;
				*ptr = '\0';
			}
		}

		tot += got;
		if (end != NULL)
			eom = strstr((const char *)usbbuf, end);

		/* Attempt a usb reset for an error that will otherwise cause
		 * this device to drop out provided we know the device still
		 * might exist. */
		if (err && err != LIBUSB_ERROR_TIMEOUT)
		{
			applog(LOG_WARNING, "%s %i %s usb read err:(%d) %s", cgpu->drv->name,
			       cgpu->device_id, usb_cmdname(cmd), err, libusb_error_name(err));
			if (err != LIBUSB_ERROR_NO_DEVICE && !tried_reset)
			{
				err = libusb_reset_device(usbdev->handle);
				tried_reset = 1; // don't call reset twice in a row
				if (err == LIBUSB_SUCCESS)
				{
					applog(LOG_WARNING, "%s %i reset succeess",
						cgpu->drv->name, cgpu->device_id);
				}
				else
				{
					applog(LOG_WARNING, "%s %i attempted reset got err:(%d) %s",
						cgpu->drv->name, cgpu->device_id, err, libusb_error_name(err));
				}
			}
		}
		else
			tried_reset = 0;

		if (NODEV(err))
			goto out_noerrmsg;

		ptr += got;
		bufleft -= got;
		if (bufleft < 1)
			err = LIBUSB_SUCCESS;

		if (err || readonce)
			break;


		first = false;

		done = tdiff(&tv_finish, &read_start);
		// N.B. this is: return last err with whatever size has already been read
		timeout = initial_timeout - (done * 1000);
		if (timeout <= 0)
			break;
	}

	/* If we found the end of message marker, just use that data and
	 * return success. */
	if (eom) {
		size_t eomlen = (void *)eom - (void *)usbbuf + endlen;

		if (eomlen < bufsiz) {
			bufsiz = eomlen;
			err = LIBUSB_SUCCESS;
		}
	}

	// N.B. usbdev->buffer was emptied before the while() loop
	if (tot > (int)bufsiz) {
		usbdev->bufamt = tot - bufsiz;
		cg_memcpy(usbdev->buffer, usbbuf + bufsiz, usbdev->bufamt);
		tot -= usbdev->bufamt;
		usbbuf[tot] = '\0';
		applog(LOG_DEBUG, "USB: %s%i read1 buffering %d extra bytes",
			cgpu->drv->name, cgpu->device_id, usbdev->bufamt);
	}

	*processed = tot;
	cg_memcpy((char *)buf, (const char *)usbbuf, (tot < (int)bufsiz) ? tot + 1 : (int)bufsiz);

out_noerrmsg:
	if (NODEV(err)) {
		cg_ruwlock(&cgpu->usbinfo.devlock);
		release_cgpu(cgpu);
		DEVWUNLOCK(cgpu, pstate);
	} else
		DEVRUNLOCK(cgpu, pstate);

	return err;
}

int _usb_write(struct cgpu_info *cgpu, int intinfo, int epinfo, char *buf, size_t bufsiz, int *processed, int timeout, enum usb_cmds cmd)
{
	struct timeval write_start, tv_finish;
	struct cg_usb_device *usbdev;
	unsigned int initial_timeout;
	int err, sent, tot, pstate, tried_reset;
	bool first = true;
	double done;

	DEVRLOCK(cgpu, pstate);

	USBDEBUG("USB debug: _usb_write(%s (nodev=%s),intinfo=%d,epinfo=%d,buf='%s',bufsiz=%d,proc=%p,timeout=%u,cmd=%s)", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), intinfo, epinfo, (char *)str_text(buf), (int)bufsiz, processed, timeout, usb_cmdname(cmd));

	*processed = 0;

	if (cgpu->usbinfo.nodev) {
		USB_REJECT(cgpu, MODE_BULK_WRITE);

		err = LIBUSB_ERROR_NO_DEVICE;
		goto out_noerrmsg;
	}

	usbdev = cgpu->usbdev;
	if (timeout == DEVTIMEOUT)
		timeout = usbdev->found->timeout;

	tot = 0;
	err = LIBUSB_SUCCESS;
	initial_timeout = timeout;
	cgtime(&write_start);
	tried_reset = 0;
	while (bufsiz > 0) {
		int tosend = bufsiz;

		/* USB 1.1 devices don't handle zero packets well so split them
		 * up to not have the final transfer equal to the wMaxPacketSize
		 * or they will stall waiting for more data. */
		if (usbdev->usb11) {
			struct usb_epinfo *ue = &usbdev->found->intinfos[intinfo].epinfos[epinfo];

			if (tosend == ue->wMaxPacketSize) {
				tosend >>= 1;
				if (unlikely(!tosend))
					tosend = 1;
			}
		}
		err = usb_perform_transfer(cgpu, usbdev, intinfo, epinfo, (unsigned char *)buf,
					tosend, &sent, timeout, MODE_BULK_WRITE,
					cmd, first ? SEQ0 : SEQ1, false, usbdev->tt);
		if (NODEV(err))
			goto out_noerrmsg;

		cgtime(&tv_finish);

		USBDEBUG("USB debug: @_usb_write(%s (nodev=%s)) err=%d%s sent=%d", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), err, isnodev(err), sent);

		tot += sent;

		/* Unlike reads, even a timeout error is unrecoverable on
		 * writes. */
		if (err)
		{
			applog(LOG_WARNING, "%s %i %s usb write err:(%d) %s", cgpu->drv->name,
			       cgpu->device_id, usb_cmdname(cmd), err, libusb_error_name(err));
			if (err != LIBUSB_ERROR_NO_DEVICE && !tried_reset)
			{
				err = libusb_reset_device(usbdev->handle);
				tried_reset = 1; // don't try reset twice in a row
				if (err == LIBUSB_SUCCESS)
				{
					applog(LOG_WARNING, "%s %i reset succeess",
						cgpu->drv->name, cgpu->device_id);
				}
				else
				{
					applog(LOG_WARNING, "%s %i attempted reset got err:(%d) %s",
						cgpu->drv->name, cgpu->device_id, err, libusb_error_name(err));
				}
			}
		}
		else
			tried_reset = 0;

		if (err)
			break;

		buf += sent;
		bufsiz -= sent;

		first = false;

		done = tdiff(&tv_finish, &write_start);
		// N.B. this is: return last err with whatever size was written
		timeout = initial_timeout - (done * 1000);
		if (timeout <= 0)
			break;
	}

	*processed = tot;

out_noerrmsg:
	if (NODEV(err)) {
		cg_ruwlock(&cgpu->usbinfo.devlock);
		release_cgpu(cgpu);
		DEVWUNLOCK(cgpu, pstate);
	} else
		DEVRUNLOCK(cgpu, pstate);

	return err;
}

/* As we do for bulk reads, emulate a sync function for control transfers using
 * our own timeouts that takes the same parameters as libusb_control_transfer.
 */
static int usb_control_transfer(struct cgpu_info *cgpu, libusb_device_handle *dev_handle, uint8_t bmRequestType,
				uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
				unsigned char *buffer, uint16_t wLength, unsigned int timeout)
{
	struct usb_transfer ut;
	unsigned char buf[70];
	int err, transferred;
	bool tt = false;

	if (unlikely(cgpu->shutdown))
		return libusb_control_transfer(dev_handle, bmRequestType, bRequest, wValue, wIndex, buffer, wLength, timeout);

	init_usb_transfer(&ut);
	libusb_fill_control_setup(buf, bmRequestType, bRequest, wValue,
				  wIndex, wLength);
	if ((bmRequestType & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT) {
		if (wLength)
			cg_memcpy(buf + LIBUSB_CONTROL_SETUP_SIZE, buffer, wLength);
		if (cgpu->usbdev->descriptor->bcdUSB < 0x0200)
			tt = true;
	}
	libusb_fill_control_transfer(ut.transfer, dev_handle, buf, transfer_callback,
				     &ut, 0);
	err = usb_submit_transfer(&ut, ut.transfer, false, tt);
	if (!err)
		err = callback_wait(&ut, &transferred, timeout);
	if (err == LIBUSB_SUCCESS && transferred) {
		if ((bmRequestType & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
			cg_memcpy(buffer, libusb_control_transfer_get_data(ut.transfer),
			       transferred);
		err = transferred;
		goto out;
	}
	err = usb_transfer_toerr(err);
out:
	complete_usb_transfer(&ut);
	return err;
}

int __usb_transfer(struct cgpu_info *cgpu, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint32_t *data, int siz, unsigned int timeout, __maybe_unused enum usb_cmds cmd)
{
	struct cg_usb_device *usbdev;
#if DO_USB_STATS
	struct timeval tv_start, tv_finish;
#endif
	unsigned char buf[64];
	uint32_t *buf32 = (uint32_t *)buf;
	int err, i, bufsiz;

	USBDEBUG("USB debug: _usb_transfer(%s (nodev=%s),type=%"PRIu8",req=%"PRIu8",value=%"PRIu16",index=%"PRIu16",siz=%d,timeout=%u,cmd=%s)", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), request_type, bRequest, wValue, wIndex, siz, timeout, usb_cmdname(cmd));

	if (cgpu->usbinfo.nodev) {
		USB_REJECT(cgpu, MODE_CTRL_WRITE);

		err = LIBUSB_ERROR_NO_DEVICE;
		goto out_;
	}
	usbdev = cgpu->usbdev;
	if (timeout == DEVTIMEOUT)
		timeout = usbdev->found->timeout;

	USBDEBUG("USB debug: @_usb_transfer() data=%s", bin2hex((unsigned char *)data, (size_t)siz));

	if (siz > 0) {
		bufsiz = siz - 1;
		bufsiz >>= 2;
		bufsiz++;
		for (i = 0; i < bufsiz; i++)
			buf32[i] = htole32(data[i]);
	}

	USBDEBUG("USB debug: @_usb_transfer() buf=%s", bin2hex(buf, (size_t)siz));

	STATS_TIMEVAL(&tv_start);
	err = usb_control_transfer(cgpu, usbdev->handle, request_type, bRequest,
				   wValue, wIndex, buf, (uint16_t)siz, timeout);
	STATS_TIMEVAL(&tv_finish);
	USB_STATS(cgpu, &tv_start, &tv_finish, err, MODE_CTRL_WRITE, cmd, SEQ0, timeout);

	USBDEBUG("USB debug: @_usb_transfer(%s (nodev=%s)) err=%d%s", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), err, isnodev(err));

	if (err < 0 && err != LIBUSB_ERROR_TIMEOUT) {
		applog(LOG_WARNING, "%s %i usb transfer err:(%d) %s", cgpu->drv->name, cgpu->device_id,
		       err, libusb_error_name(err));
	}
out_:
	return err;
}

/* We use the write devlock for control transfers since some control transfers
 * are rare but may be changing settings within the device causing problems
 * if concurrent transfers are happening. Using the write lock serialises
 * any transfers. */
int _usb_transfer(struct cgpu_info *cgpu, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint32_t *data, int siz, unsigned int timeout, enum usb_cmds cmd)
{
	int pstate, err;

	DEVWLOCK(cgpu, pstate);

	err = __usb_transfer(cgpu, request_type, bRequest, wValue, wIndex, data, siz, timeout, cmd);

	if (NOCONTROLDEV(err))
		release_cgpu(cgpu);

	DEVWUNLOCK(cgpu, pstate);

	return err;
}

int _usb_transfer_read(struct cgpu_info *cgpu, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, char *buf, int bufsiz, int *amount, unsigned int timeout, __maybe_unused enum usb_cmds cmd)
{
	struct cg_usb_device *usbdev;
#if DO_USB_STATS
	struct timeval tv_start, tv_finish;
#endif
	unsigned char tbuf[64];
	int err, pstate;

	DEVWLOCK(cgpu, pstate);

	USBDEBUG("USB debug: _usb_transfer_read(%s (nodev=%s),type=%"PRIu8",req=%"PRIu8",value=%"PRIu16",index=%"PRIu16",bufsiz=%d,timeout=%u,cmd=%s)", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), request_type, bRequest, wValue, wIndex, bufsiz, timeout, usb_cmdname(cmd));

	if (cgpu->usbinfo.nodev) {
		USB_REJECT(cgpu, MODE_CTRL_READ);

		err = LIBUSB_ERROR_NO_DEVICE;
		goto out_noerrmsg;
	}
	usbdev = cgpu->usbdev;
	if (timeout == DEVTIMEOUT)
		timeout = usbdev->found->timeout;

	*amount = 0;

	memset(tbuf, 0, 64);
	STATS_TIMEVAL(&tv_start);
	err = usb_control_transfer(cgpu, usbdev->handle, request_type, bRequest,
				   wValue, wIndex, tbuf, (uint16_t)bufsiz, timeout);
	STATS_TIMEVAL(&tv_finish);
	USB_STATS(cgpu, &tv_start, &tv_finish, err, MODE_CTRL_READ, cmd, SEQ0, timeout);
	cg_memcpy(buf, tbuf, bufsiz);

	USBDEBUG("USB debug: @_usb_transfer_read(%s (nodev=%s)) amt/err=%d%s%s%s", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), err, isnodev(err), err > 0 ? " = " : BLANK, err > 0 ? bin2hex((unsigned char *)buf, (size_t)err) : BLANK);

	if (err > 0) {
		*amount = err;
		err = 0;
	}
	if (err < 0 && err != LIBUSB_ERROR_TIMEOUT) {
		applog(LOG_WARNING, "%s %i usb transfer read err:(%d) %s", cgpu->drv->name, cgpu->device_id,
		       err, libusb_error_name(err));
	}
out_noerrmsg:
	if (NOCONTROLDEV(err))
		release_cgpu(cgpu);

	DEVWUNLOCK(cgpu, pstate);

	return err;
}

#define FTDI_STATUS_B0_MASK     (FTDI_RS0_CTS | FTDI_RS0_DSR | FTDI_RS0_RI | FTDI_RS0_RLSD)
#define FTDI_RS0_CTS    (1 << 4)
#define FTDI_RS0_DSR    (1 << 5)
#define FTDI_RS0_RI     (1 << 6)
#define FTDI_RS0_RLSD   (1 << 7)

/* Clear to send for FTDI */
int usb_ftdi_cts(struct cgpu_info *cgpu)
{
	char buf[2], ret;
	int err, amount;

	err = _usb_transfer_read(cgpu, (uint8_t)FTDI_TYPE_IN, (uint8_t)5,
				 (uint16_t)0, (uint16_t)0, buf, 2,
				 &amount, DEVTIMEOUT, C_FTDI_STATUS);
	/* We return true in case drivers are waiting indefinitely to try and
	 * write to something that's not there. */
	if (err)
		return true;

	ret = buf[0] & FTDI_STATUS_B0_MASK;
	return (ret & FTDI_RS0_CTS);
}

int _usb_ftdi_set_latency(struct cgpu_info *cgpu, int intinfo)
{
	int err = 0;
	int pstate;

	DEVWLOCK(cgpu, pstate);

	if (cgpu->usbdev) {
		if (cgpu->usbdev->usb_type != USB_TYPE_FTDI) {
			applog(LOG_ERR, "%s: cgid %d latency request on non-FTDI device",
				cgpu->drv->name, cgpu->cgminer_id);
			err = LIBUSB_ERROR_NOT_SUPPORTED;
		} else if (cgpu->usbdev->found->latency == LATENCY_UNUSED) {
			applog(LOG_ERR, "%s: cgid %d invalid latency (UNUSED)",
				cgpu->drv->name, cgpu->cgminer_id);
			err = LIBUSB_ERROR_NOT_SUPPORTED;
		}

		if (!err)
			err = __usb_transfer(cgpu, FTDI_TYPE_OUT, FTDI_REQUEST_LATENCY,
						cgpu->usbdev->found->latency,
						USBIF(cgpu->usbdev, intinfo),
						NULL, 0, DEVTIMEOUT, C_LATENCY);
	}

	DEVWUNLOCK(cgpu, pstate);

	applog(LOG_DEBUG, "%s: cgid %d %s got err %d",
				cgpu->drv->name, cgpu->cgminer_id,
				usb_cmdname(C_LATENCY), err);

	return err;
}

void usb_buffer_clear(struct cgpu_info *cgpu)
{
	int pstate;

	DEVWLOCK(cgpu, pstate);

	if (cgpu->usbdev)
		cgpu->usbdev->bufamt = 0;

	DEVWUNLOCK(cgpu, pstate);
}

uint32_t usb_buffer_size(struct cgpu_info *cgpu)
{
	uint32_t ret = 0;
	int pstate;

	DEVRLOCK(cgpu, pstate);

	if (cgpu->usbdev)
		ret = cgpu->usbdev->bufamt;

	DEVRUNLOCK(cgpu, pstate);

	return ret;
}

/*
 * The value returned (0) when usbdev is NULL
 * doesn't matter since it also means the next call to
 * any usbutils function will fail with a nodev
 * N.B. this is to get the interface number to use in a control_transfer
 * which for some devices isn't actually the interface number
 */
int _usb_interface(struct cgpu_info *cgpu, int intinfo)
{
	int interface = 0;
	int pstate;

	DEVRLOCK(cgpu, pstate);

	if (cgpu->usbdev)
		interface = cgpu->usbdev->found->intinfos[intinfo].ctrl_transfer;

	DEVRUNLOCK(cgpu, pstate);

	return interface;
}

enum sub_ident usb_ident(struct cgpu_info *cgpu)
{
	enum sub_ident ident = IDENT_UNK;
	int pstate;

	DEVRLOCK(cgpu, pstate);

	if (cgpu->usbdev)
		ident = cgpu->usbdev->ident;

	DEVRUNLOCK(cgpu, pstate);

	return ident;
}

// Need to set all devices with matching usbdev
void usb_set_dev_start(struct cgpu_info *cgpu)
{
	struct cg_usb_device *cgusb;
	struct cgpu_info *cgpu2;
	struct timeval now;
	int pstate;

	DEVWLOCK(cgpu, pstate);

	cgusb = cgpu->usbdev;

	// If the device wasn't dropped
	if (cgusb != NULL) {
		int i;

		cgtime(&now);

		for (i = 0; i < total_devices; i++) {
			cgpu2 = get_a_device(i);
			if (cgpu2->usbdev == cgusb)
				copy_time(&(cgpu2->dev_start_tv), &now);
		}
	}

	DEVWUNLOCK(cgpu, pstate);
}

void usb_cleanup(void)
{
	struct cgpu_info *cgpu;
	int count, pstate;
	int i;

	hotplug_time = 0;

	cgsleep_ms(10);

	count = 0;
	for (i = 0; i < total_devices; i++) {
		cgpu = devices[i];
		switch (cgpu->drv->drv_id) {
			case DRIVER_bflsc:
			case DRIVER_bitforce:
			case DRIVER_bitfury:
			case DRIVER_cointerra:
			case DRIVER_drillbit:
			case DRIVER_gekko:
			case DRIVER_modminer:
			case DRIVER_icarus:
			case DRIVER_avalon:
			case DRIVER_avalon2:
			case DRIVER_avalon4:
			case DRIVER_avalon7:
			case DRIVER_avalon8:
			case DRIVER_avalon9:
			case DRIVER_avalonlc3:
			case DRIVER_avalonm:
			case DRIVER_klondike:
			case DRIVER_hashfast:
				DEVWLOCK(cgpu, pstate);
				release_cgpu(cgpu);
				DEVWUNLOCK(cgpu, pstate);
				count++;
				break;
			default:
				break;
		}
	}

	/*
	 * Must attempt to wait for the resource thread to release coz
	 * during a restart it won't automatically release them in linux
	 */
	if (count) {
		struct timeval start, now;

		cgtime(&start);
		while (42) {
			cgsleep_ms(50);

			mutex_lock(&cgusbres_lock);

			if (!res_work_head)
				break;

			cgtime(&now);
			if (tdiff(&now, &start) > 0.366) {
				applog(LOG_WARNING,
					"usb_cleanup gave up waiting for resource thread");
				break;
			}

			mutex_unlock(&cgusbres_lock);
		}
		mutex_unlock(&cgusbres_lock);
	}

	cgsem_destroy(&usb_resource_sem);
}

#define DRIVER_COUNT_FOUND(X) if (X##_drv.name && strcasecmp(ptr, X##_drv.name) == 0) { \
	drv_count[X##_drv.drv_id].limit = lim; \
	found = true; \
	}
void usb_initialise(void)
{
	char *fre, *ptr, *comma, *colon;
	int bus, dev, lim, i;
	bool found;

	INIT_LIST_HEAD(&ut_list);

	for (i = 0; i < DRIVER_MAX; i++) {
		drv_count[i].count = 0;
		drv_count[i].limit = 999999;
	}

	cgusb_check_init();

	if (opt_usb_select && *opt_usb_select) {
		// Absolute device limit
		if (*opt_usb_select == ':') {
			total_limit = atoi(opt_usb_select+1);
			if (total_limit < 0)
				quit(1, "Invalid --usb total limit");
		// Comma list of bus:dev devices to match
		} else if (isdigit(*opt_usb_select)) {
			fre = ptr = strdup(opt_usb_select);
			do {
				comma = strchr(ptr, ',');
				if (comma)
					*(comma++) = '\0';

				colon = strchr(ptr, ':');
				if (!colon)
					quit(1, "Invalid --usb bus:dev missing ':'");

				*(colon++) = '\0';

				if (!isdigit(*ptr))
					quit(1, "Invalid --usb bus:dev - bus must be a number");

				if (!isdigit(*colon) && *colon != '*')
					quit(1, "Invalid --usb bus:dev - dev must be a number or '*'");

				bus = atoi(ptr);
				if (bus <= 0)
					quit(1, "Invalid --usb bus:dev - bus must be > 0");

				if (*colon == '*')
					dev = -1;
				else {
					dev = atoi(colon);
					if (dev <= 0)
						quit(1, "Invalid --usb bus:dev - dev must be > 0 or '*'");
				}

				busdev = cgrealloc(busdev, sizeof(*busdev) * (++busdev_count));
				busdev[busdev_count-1].bus_number = bus;
				busdev[busdev_count-1].device_address = dev;

				ptr = comma;
			} while (ptr);
			free(fre);
		// Comma list of DRV:limit
		} else {
			fre = ptr = strdup(opt_usb_select);
			do {
				comma = strchr(ptr, ',');
				if (comma)
					*(comma++) = '\0';

				colon = strchr(ptr, ':');
				if (!colon)
					quit(1, "Invalid --usb DRV:limit missing ':'");

				*(colon++) = '\0';

				if (!isdigit(*colon))
					quit(1, "Invalid --usb DRV:limit - limit must be a number");

				lim = atoi(colon);
				if (lim < 0)
					quit(1, "Invalid --usb DRV:limit - limit must be >= 0");

				found = false;
				/* Use the DRIVER_PARSE_COMMANDS macro to iterate
				 * over all the drivers. */
				DRIVER_PARSE_COMMANDS(DRIVER_COUNT_FOUND)
				if (!found)
					quit(1, "Invalid --usb DRV:limit - unknown DRV='%s'", ptr);

				ptr = comma;
			} while (ptr);
			free(fre);
		}
	}
}

#ifndef WIN32
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>

#ifndef __APPLE__
union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
	struct seminfo *__buf;
};
#endif

#else
static LPSECURITY_ATTRIBUTES unsec(LPSECURITY_ATTRIBUTES sec)
{
	SECURITY_DESCRIPTOR *sd = (PSECURITY_DESCRIPTOR)(sec->lpSecurityDescriptor);
	FreeSid(sd->Group);
	free(sec->lpSecurityDescriptor);
	free(sec);
	return NULL;
}

static LPSECURITY_ATTRIBUTES mksec(const char *dname, uint8_t bus_number, uint8_t device_address)
{
	SID_IDENTIFIER_AUTHORITY SIDAuthWorld = {SECURITY_WORLD_SID_AUTHORITY};
	PSID gsid = NULL;
	LPSECURITY_ATTRIBUTES sec_att = NULL;
	PSECURITY_DESCRIPTOR sec_des = NULL;

	sec_des = cgmalloc(SECURITY_DESCRIPTOR_MIN_LENGTH);

	if (!InitializeSecurityDescriptor(sec_des, SECURITY_DESCRIPTOR_REVISION)) {
		applog(LOG_ERR,
			"MTX: %s (%d:%d) USB failed to init secdes err (%d)",
			dname, (int)bus_number, (int)device_address,
			(int)GetLastError());
		free(sec_des);
		return NULL;
	}

	if (!SetSecurityDescriptorDacl(sec_des, TRUE, NULL, FALSE)) {
		applog(LOG_ERR,
			"MTX: %s (%d:%d) USB failed to secdes dacl err (%d)",
			dname, (int)bus_number, (int)device_address,
			(int)GetLastError());
		free(sec_des);
		return NULL;
	}

	if(!AllocateAndInitializeSid(&SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &gsid)) {
		applog(LOG_ERR,
			"MTX: %s (%d:%d) USB failed to create gsid err (%d)",
			dname, (int)bus_number, (int)device_address,
			(int)GetLastError());
		free(sec_des);
		return NULL;
	}

	if (!SetSecurityDescriptorGroup(sec_des, gsid, FALSE)) {
		applog(LOG_ERR,
			"MTX: %s (%d:%d) USB failed to secdes grp err (%d)",
			dname, (int)bus_number, (int)device_address,
			(int)GetLastError());
		FreeSid(gsid);
		free(sec_des);
		return NULL;
	}

	sec_att = cgmalloc(sizeof(*sec_att));

	sec_att->nLength = sizeof(*sec_att);
	sec_att->lpSecurityDescriptor = sec_des;
	sec_att->bInheritHandle = FALSE;

	return sec_att;
}
#endif

// Any errors should always be printed since they will rarely if ever occur
// and thus it is best to always display them
static bool resource_lock(const char *dname, uint8_t bus_number, uint8_t device_address)
{
	applog(LOG_DEBUG, "USB res lock %s %d-%d", dname, (int)bus_number, (int)device_address);
#ifdef WIN32
	struct cgpu_info *cgpu;
	LPSECURITY_ATTRIBUTES sec;
	HANDLE usbMutex;
	char name[64];
	DWORD res;
	int i;

	if (is_in_use_bd(bus_number, device_address))
		return false;

	snprintf(name, sizeof(name), "cg-usb-%d-%d", (int)bus_number, (int)device_address);

	sec = mksec(dname, bus_number, device_address);
	if (!sec)
		return false;

	usbMutex = CreateMutex(sec, FALSE, name);
	if (usbMutex == NULL) {
		applog(LOG_ERR,
			"MTX: %s USB failed to get '%s' err (%d)",
			dname, name, (int)GetLastError());
		sec = unsec(sec);
		return false;
	}

	res = WaitForSingleObject(usbMutex, 0);

	switch(res) {
		case WAIT_OBJECT_0:
		case WAIT_ABANDONED:
			// Am I using it already?
			for (i = 0; i < total_devices; i++) {
				cgpu = get_a_device(i);
				if (cgpu->usbinfo.bus_number == bus_number &&
				    cgpu->usbinfo.device_address == device_address &&
				    cgpu->usbinfo.nodev == false) {
					if (ReleaseMutex(usbMutex)) {
						applog(LOG_WARNING,
							"MTX: %s USB can't get '%s' - device in use",
							dname, name);
						goto fail;
					}
					applog(LOG_ERR,
						"MTX: %s USB can't get '%s' - device in use - failure (%d)",
						dname, name, (int)GetLastError());
					goto fail;
				}
			}
			break;
		case WAIT_TIMEOUT:
			if (!hotplug_mode)
				applog(LOG_WARNING,
					"MTX: %s USB failed to get '%s' - device in use",
					dname, name);
			goto fail;
		case WAIT_FAILED:
			applog(LOG_ERR,
				"MTX: %s USB failed to get '%s' err (%d)",
				dname, name, (int)GetLastError());
			goto fail;
		default:
			applog(LOG_ERR,
				"MTX: %s USB failed to get '%s' unknown reply (%d)",
				dname, name, (int)res);
			goto fail;
	}

	add_in_use(bus_number, device_address, false);
	in_use_store_ress(bus_number, device_address, (void *)usbMutex, (void *)sec);

	return true;
fail:
	CloseHandle(usbMutex);
	sec = unsec(sec);
	return false;
#else
	char name[64];
	int fd;

	if (is_in_use_bd(bus_number, device_address))
		return false;

	snprintf(name, sizeof(name), "/tmp/cgminer-usb-%d-%d", (int)bus_number, (int)device_address);
	fd = open(name, O_CREAT|O_RDONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		applog(LOG_ERR, "%s USB open failed '%s' err (%d) %s",
		       dname, name, errno, strerror(errno));
		return false;
	}
	if (flock(fd, LOCK_EX | LOCK_NB)) {
		applog(LOG_INFO, "%s USB failed to get '%s' - device in use",
		       dname, name);
		close(fd);
		return false;
	}

	add_in_use(bus_number, device_address, false);
	in_use_store_fd(bus_number, device_address, fd);
	return true;
#endif
}

// Any errors should always be printed since they will rarely if ever occur
// and thus it is best to always display them
static void resource_unlock(const char *dname, uint8_t bus_number, uint8_t device_address)
{
	applog(LOG_DEBUG, "USB res unlock %s %d-%d", dname, (int)bus_number, (int)device_address);

#ifdef WIN32
	LPSECURITY_ATTRIBUTES sec = NULL;
	HANDLE usbMutex = NULL;
	char name[64];

	snprintf(name, sizeof(name), "cg-usb-%d-%d", (int)bus_number, (int)device_address);

	in_use_get_ress(bus_number, device_address, (void **)(&usbMutex), (void **)(&sec));

	if (!usbMutex || !sec)
		goto fila;

	if (!ReleaseMutex(usbMutex))
		applog(LOG_ERR,
			"MTX: %s USB failed to release '%s' err (%d)",
			dname, name, (int)GetLastError());

fila:

	if (usbMutex)
		CloseHandle(usbMutex);
	if (sec)
		unsec(sec);
	remove_in_use(bus_number, device_address);
	return;
#else
	char name[64];
	int fd;

	snprintf(name, sizeof(name), "/tmp/cgminer-usb-%d-%d", (int)bus_number, (int)device_address);

	fd = in_use_get_fd(bus_number, device_address);
	if (fd < 0)
		return;
	remove_in_use(bus_number, device_address);
	close(fd);
	unlink(name);
	return;
#endif
}

static void resource_process()
{
	struct resource_work *res_work = NULL;
	struct resource_reply *res_reply = NULL;
	bool ok;

	applog(LOG_DEBUG, "RES: %s (%d:%d) lock=%d",
			res_work_head->dname,
			(int)res_work_head->bus_number,
			(int)res_work_head->device_address,
			res_work_head->lock);

	if (res_work_head->lock) {
		ok = resource_lock(res_work_head->dname,
					res_work_head->bus_number,
					res_work_head->device_address);

		applog(LOG_DEBUG, "RES: %s (%d:%d) lock ok=%d",
				res_work_head->dname,
				(int)res_work_head->bus_number,
				(int)res_work_head->device_address,
				ok);

		res_reply = cgcalloc(1, sizeof(*res_reply));

		res_reply->bus_number = res_work_head->bus_number;
		res_reply->device_address = res_work_head->device_address;
		res_reply->got = ok;
		res_reply->next = res_reply_head;

		res_reply_head = res_reply;
	}
	else
		resource_unlock(res_work_head->dname,
				res_work_head->bus_number,
				res_work_head->device_address);

	res_work = res_work_head;
	res_work_head = res_work_head->next;
	free(res_work);
}

void *usb_resource_thread(void __maybe_unused *userdata)
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	RenameThread("USBResource");

	applog(LOG_DEBUG, "RES: thread starting");

	while (42) {
		/* Wait to be told we have work to do */
		cgsem_wait(&usb_resource_sem);

		mutex_lock(&cgusbres_lock);
		while (res_work_head)
			resource_process();
		mutex_unlock(&cgusbres_lock);
	}

	return NULL;
}

void initialise_usblocks(void)
{
	mutex_init(&cgusb_lock);
	mutex_init(&cgusbres_lock);
	cglock_init(&cgusb_fd_lock);
}
