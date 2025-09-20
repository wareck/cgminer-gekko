/*
 * Copyright 2012-2024 Andrew Smith
 * Copyright 2013-2014 Con Kolivas <kernel@kolivas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef USBUTILS_H
#define USBUTILS_H

#include <libusb.h>

#include "util.h"

#define EPI(x) (LIBUSB_ENDPOINT_IN | (unsigned char)(x))
#define EPO(x) (LIBUSB_ENDPOINT_OUT | (unsigned char)(x))


// For 0x0403:0x6014/0x6001 FT232H (and possibly others?) - BFL, BAS, BLT, LLT, AVA
#define FTDI_TYPE_OUT (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT)
#define FTDI_TYPE_IN (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_IN)

#define FTDI_REQUEST_RESET ((uint8_t)0)
#define FTDI_REQUEST_MODEM ((uint8_t)1)
#define FTDI_REQUEST_FLOW ((uint8_t)2)
#define FTDI_REQUEST_BAUD ((uint8_t)3)
#define FTDI_REQUEST_DATA ((uint8_t)4)
#define FTDI_REQUEST_LATENCY ((uint8_t)9)
#define FTDI_REQUEST_BITMODE ((uint8_t)11)

#define FTDI_BITMODE_RESET   ((uint8_t)0x00)
#define FTDI_BITMODE_BITBANG ((uint8_t)0x01)
#define FTDI_BITMODE_MPSSE   ((uint8_t)0x02)
#define FTDI_BITMODE_SYNCBB  ((uint8_t)0x04)
#define FTDI_BITMODE_MCU     ((uint8_t)0x08)
#define FTDI_BITMODE_OPTO    ((uint8_t)0x10)
#define FTDI_BITMODE_CBUS    ((uint8_t)0x20)
#define FTDI_BITMODE_SYNCFF  ((uint8_t)0x40)
#define FTDI_BITMODE_FT1284  ((uint8_t)0x80)

#define FTDI_SETDTR_LOW  0x0100
#define FTDI_SETDTR_HIGH 0x0101
#define FTDI_SETRTS_LOW  0x0200
#define FTDI_SETRTS_HIGH 0x0202

#define FTDI_VALUE_RESET 0
#define FTDI_VALUE_PURGE_RX 1
#define FTDI_VALUE_PURGE_TX 2
#define FTDI_VALUE_LATENCY 1

// Baud
#define FTDI_VALUE_BAUD_BFL 0xc068
#define FTDI_INDEX_BAUD_BFL 0x0200
#define FTDI_VALUE_BAUD_BAS FTDI_VALUE_BAUD_BFL
#define FTDI_INDEX_BAUD_BAS FTDI_INDEX_BAUD_BFL
// LLT = BLT (same code)
#define FTDI_VALUE_BAUD_BLT 0x001a
#define FTDI_INDEX_BAUD_BLT 0x0000

// Avalon
#define FTDI_VALUE_BAUD_AVA 0x001A
#define FTDI_INDEX_BAUD_AVA 0x0000

#define FTDI_VALUE_DATA_AVA 8

// BitBurner
#define BITBURNER_REQUEST ((uint8_t)0x42)
#define BITBURNER_VALUE 0x4242
#define BITBURNER_INDEX_SET_VOLTAGE 1
#define BITBURNER_INDEX_GET_VOLTAGE 2
#define BITBURNER_INDEX_GET_VERSION 4

// CMR = 115200 & 57600
#define FTDI_VALUE_BAUD_CMR_115 0xc068
#define FTDI_INDEX_BAUD_CMR_115 0x0200

#define FTDI_VALUE_BAUD_CMR_57 0x80d0
#define FTDI_INDEX_BAUD_CMR_57 0x0200

// FTDI
#define FTDI_VALUE_DATA_BTS 8
#define FTDI_VALUE_BAUD_BTS 0x001A
#define FTDI_INDEX_BAUD_BTS 0x0000

// Data control
#define FTDI_VALUE_DATA_BFL 0
#define FTDI_VALUE_DATA_BAS FTDI_VALUE_DATA_BFL
// LLT = BLT (same code)
#define FTDI_VALUE_DATA_BLT 8

#define FTDI_VALUE_FLOW 0
#define FTDI_VALUE_MODEM 0x0303


// For 0x10c4:0xea60 USB cp210x chip - AMU
#define CP210X_TYPE_OUT 0x41

// all
#define CP210X_TYPE_HOST_TO_DEVICE 0x40
#define CP210X_TYPE_HOST_TO_INTERFACE 0x41
#define CP210X_TYPE_DEVICE_TO_HOST 0xc0
#define CP210X_TYPE_INTERFACE_TO_HOST 0xc1

#define CP210X_REQUEST_IFC_ENABLE 0x00
#define CP210X_REQUEST_DATA 0x07
#define CP210X_REQUEST_RESET 0x11
#define CP210X_REQUEST_GET_FLOW 0x13
#define CP210X_REQUEST_SET_FLOW 0x14
#define CP210X_REQUEST_BAUD 0x1e
#define CP210X_REQUEST_SET_MHS 0x07

#define CP210X_VENDOR_SPECIFIC	0xFF

#define CP210X_READ_LATCH 0x00C2
#define CP210X_WRITE_LATCH 0x37E1
#define CP210X_VALUE_RTS_ECI_ON 0x4000

#define CP210X_VALUE_UART_ENABLE 0x0001
#define CP210X_VALUE_DATA 0x0303
#define CP210X_DATA_BAUD 0x0001c200

#define CP210X_SET_LINE_CTL 0x03
#define CP210X_BITS_DATA_MASK 0x0f00
#define CP210X_BITS_DATA_8 0x0800
#define CP210X_BITS_PARITY_NONE 0x0000
#define CP210X_BITS_PARITY_ODD 0x0010
#define CP210X_BITS_PARITY_EVEN 0x0020
#define CP210X_BITS_PARITY_MARK 0x0030
#define CP210X_BITS_PARITY_SPACE 0x0040
#define CP210X_BITS_STOP_1 0x0000
#define CP210X_BITS_STOP_15 0x0001
#define CP210X_BITS_STOP_2 0x0002

typedef struct
{
	uint16_t Mode; // Push-Pull = 1, Open-Drain = 0
	uint16_t Reset_Latch; // Logic High = 1, Logic Low = =0
	uint16_t Suspend_Latch; // Logic High = 1, Logic Low = =0
	unsigned char EnhancedFxn_ECI;
	unsigned char EnhancedFxn_SCI;
	unsigned char EnhancedFxn_Device;
} CP2105_PORT_CONFIG;

// MCP2210 cmds
#define MCP2210_GET_STATUS 0x10
#define MCP2210_SPI_CANCEL 0x11
#define MCP2210_GET_INT_COUNT 0x12
#define MCP2210_GET_GPIO_SETTINGS 0x20
#define MCP2210_SET_SETTINGS 0x21
#define MCP2210_SET_GPIO_PIN_VAL 0x30
#define MCP2210_GET_GPIO_PIN_VAL 0x31
#define MCP2210_SET_GPIO_PIN_DIR 0x32
#define MCP2210_GET_GPIO_PIN_DIR 0x33
#define MCP2210_SET_SPI_SETTINGS 0x40
#define MCP2210_GET_SPI_SETTINGS 0x41
#define MCP2210_SPI_TRANSFER 0x42
#define MCP2210_READ_EEPROM 0x50
#define MCP2210_WRITE_EEPROM 0x51
#define MCP2210_SET_NVRAM_SETTINGS 0x60
#define MCP2210_GET_NVRAM_SETTINGS 0x61
#define MCP2210_SEND_ACCESS 0x70
#define MCP2210_SPI_BUS_RELEASE 0x80
// 2nd byte reply (1st byte = cmd sent)
#define MCP2210_CMD_OK 0x00
#define MCP2210_SPI_UNAVAILABLE 0xF7
#define MCP2210_SPI_BUSY 0xF8
#define MCP2210_CMD_UNKNOWN 0xF9
// SPI valid transfer reply status - 4th byte (3rd = size)
#define MCP2210_SPI_FINISHED 0x10
#define MCP2210_SPI_STARTED 0x20
#define MCP2210_SPI_PARTIAL 0x30

// SPI Setting Fields
// 4 bytes low->hi
#define MCP2210_BAUD_SPI 4
// 2 bytes low->hi
#define MCP2210_IDLE_CHIP_SEL 10
// 2 bytes low->hi
#define MCP2210_ACTIVE_CHIP_SEL 12
// 2 bytes low->hi
#define MCP2210_SPI_BYTES 18
// 1 byte
#define MCP2210_SPI_MODE 20

// For 0x067b:0x2303 Prolific PL2303 - ICA
#define PL2303_CTRL_DTR 0x01
#define PL2303_CTRL_RTS 0x02

#define PL2303_CTRL_OUT 0x21
#define PL2303_VENDOR_OUT 0x40

#define PL2303_REQUEST_CTRL 0x22
#define PL2303_REQUEST_LINE 0x20
#define PL2303_REQUEST_VENDOR 0x01

#define PL2303_REPLY_CTRL 0x21

#define PL2303_VALUE_CTRL (PL2303_CTRL_DTR | PL2303_CTRL_RTS)
#define PL2303_VALUE_LINE 0
#define PL2303_VALUE_LINE0 0x0001c200
#define PL2303_VALUE_LINE1 0x080000
#define PL2303_VALUE_LINE_SIZE 7
#define PL2303_VALUE_VENDOR 0

// Use the device defined timeout
#define DEVTIMEOUT 0

// The default intinfo structure used is the first one
#define DEFAULT_INTINFO 0

// For endpoints defined in usb_find_devices.intinfos.epinfos,
// the first two must be the default IN and OUT and both must always exist
#define DEFAULT_EP_IN 0
#define DEFAULT_EP_OUT 1

struct usb_epinfo {
	uint8_t att;
	uint16_t size;
	unsigned char ep;
	uint16_t wMaxPacketSize;
	bool found;
};

struct usb_intinfo {
	int interface;
	int ctrl_transfer;
	int epinfo_count;
	struct usb_epinfo *epinfos;
};

enum sub_ident {
	IDENT_UNK = 0,
	IDENT_AMU,
	IDENT_ANT,
	IDENT_ANU,
	IDENT_AS3,
	IDENT_AU3,
	IDENT_AVA,
	IDENT_AV2,
	IDENT_AV4,
	IDENT_AV7,
	IDENT_AV8,
	IDENT_AV9,
	IDENT_AVLC3,
	IDENT_AVM,
	IDENT_BAJ,
	IDENT_BAL,
	IDENT_BAM,
	IDENT_BAS,
	IDENT_BBF,
	IDENT_BET,
	IDENT_BF1,
	IDENT_BFL,
	IDENT_BLT,
	IDENT_BMA,
	IDENT_BSC,
	IDENT_BSD,
	IDENT_BSE,
	IDENT_BTB,
	IDENT_BXF,
	IDENT_BXM,
	IDENT_CMR1,
	IDENT_CMR2,
	IDENT_CTA,
	IDENT_DRB,
	IDENT_FLX,
	IDENT_GSC,
	IDENT_GSD,
	IDENT_GSE,
	IDENT_GSH,
	IDENT_GSI,
	IDENT_GSF,
	IDENT_GSFM,
	IDENT_GSA1,
	IDENT_GSA2,
	IDENT_GSK,
	IDENT_HFA,
	IDENT_HRO,
	IDENT_ICA,
	IDENT_KLN,
	IDENT_LIN,
	IDENT_LLT,
	IDENT_MMQ,
	IDENT_NFU,
	IDENT_OSM
};

struct usb_find_devices {
	int drv;
	const char *name;
	enum sub_ident ident;
	uint16_t idVendor;
	uint16_t idProduct;
	char *iManufacturer;
	char *iProduct;
	int config;
	unsigned int timeout;
	uint16_t latency;
	int intinfo_count;
	struct usb_intinfo *intinfos;
};

/* Latency is set to 32ms to prevent a transfer ever being more than 512 bytes
 * +2 bytes of status such as the ftdi chip, when the chips emulate a 115200
 * baud rate, to avoid status bytes being interleaved in larger transfers. */
#define LATENCY_UNUSED 0
#define LATENCY_STD 32
#define LATENCY_ANTS1 10
#define LATENCY_ANTS3 10

enum usb_types {
	USB_TYPE_STD = 0,
	USB_TYPE_FTDI
};

#define USB_MAX_READ 8192
/*
 * We add 4: 1 for null, 2 for FTDI status and 1 to round to 4 bytes
 * If a single device ever has multiple end points then it will need
 * multiple of these
 */
#define USB_READ_BUFSIZE (USB_MAX_READ + 4)

struct cg_usb_device {
	struct usb_find_devices *found;
	libusb_device_handle *handle;
	pthread_mutex_t *mutex;
	struct libusb_device_descriptor *descriptor;
	enum usb_types usb_type;
	enum sub_ident ident;
	uint16_t usbver;
	char *prod_string;
	char *manuf_string;
	char *serial_string;
	unsigned char fwVersion;	// ??
	unsigned char interfaceVersion;	// ??
	char buffer[USB_MAX_READ];
	uint32_t bufsiz;
	uint32_t bufamt;
	bool usb11; // USB 1.1 flag for convenience
	bool tt; // Enable the transaction translator
};

#define USB_NOSTAT 0

#define USB_TMO_0 50
#define USB_TMO_1 100
#define USB_TMO_2 500
#define USB_TMOS 3

struct cg_usb_tmo {
	uint32_t count;
	uint32_t min_tmo;
	uint32_t max_tmo;
	uint64_t total_over;
	uint64_t total_tmo;
};

struct cg_usb_info {
	uint8_t bus_number;
	uint8_t device_address;
	int usbstat;
	bool nodev;
	bool initialised;
	int nodev_count;
	struct timeval last_nodev;
	uint32_t ioerr_count;
	uint32_t continuous_ioerr_count;

	/*
	 * for nodev and cgusb access (read and write)
	 * it's a pointer so MMQ can have it in multiple devices
	 *
	 * N.B. general mining code doesn't need to use the read
	 * lock for 'nodev' if it calls a usb_read/write/etc function
	 * that uses the lock - however, all usbutils code MUST use it
	 * to avoid devices disappearing while in use by multiple threads
	 */
	cglock_t devlock;

	time_t last_pipe;
	uint64_t pipe_count;
	uint64_t clear_err_count;
	uint64_t retry_err_count;
	uint64_t clear_fail_count;

	uint64_t read_delay_count;
	double total_read_delay;
	uint64_t write_delay_count;
	double total_write_delay;

	uint64_t tmo_count;
	struct cg_usb_tmo usb_tmo[USB_TMOS];
};

#define ENUMERATION(a,b) a,
#define JUMPTABLE(a,b) b,

#define USB_PARSE_COMMANDS(USB_ADD_COMMAND) \
	USB_ADD_COMMAND(C_REJECTED, "RejectedNoDevice") \
	USB_ADD_COMMAND(C_PING, "Ping") \
	USB_ADD_COMMAND(C_CLEAR, "Clear") \
	USB_ADD_COMMAND(C_REQUESTVERSION, "RequestVersion") \
	USB_ADD_COMMAND(C_GETVERSION, "GetVersion") \
	USB_ADD_COMMAND(C_REQUESTFPGACOUNT, "RequestFPGACount") \
	USB_ADD_COMMAND(C_GETFPGACOUNT, "GetFPGACount") \
	USB_ADD_COMMAND(C_STARTPROGRAM, "StartProgram") \
	USB_ADD_COMMAND(C_STARTPROGRAMSTATUS, "StartProgramStatus") \
	USB_ADD_COMMAND(C_PROGRAM, "Program") \
	USB_ADD_COMMAND(C_PROGRAMSTATUS, "ProgramStatus") \
	USB_ADD_COMMAND(C_PROGRAMSTATUS2, "ProgramStatus2") \
	USB_ADD_COMMAND(C_FINALPROGRAMSTATUS, "FinalProgramStatus") \
	USB_ADD_COMMAND(C_SETCLOCK, "SetClock")			    \
	USB_ADD_COMMAND(C_SETPARITY, "SetParity") \
	USB_ADD_COMMAND(C_REPLYSETCLOCK, "ReplySetClock") \
	USB_ADD_COMMAND(C_SETVOLT, "SetVolt") \
	USB_ADD_COMMAND(C_REPLYSETVOLT, "ReplySetVolt") \
	USB_ADD_COMMAND(C_REQUESTUSERCODE, "RequestUserCode") \
	USB_ADD_COMMAND(C_GETUSERCODE, "GetUserCode") \
	USB_ADD_COMMAND(C_REQUESTTEMPERATURE, "RequestTemperature") \
	USB_ADD_COMMAND(C_GETTEMPERATURE, "GetTemperature") \
	USB_ADD_COMMAND(C_SENDWORK, "SendWork") \
	USB_ADD_COMMAND(C_SENDWORKSTATUS, "SendWorkStatus") \
	USB_ADD_COMMAND(C_REQUESTWORKSTATUS, "RequestWorkStatus") \
	USB_ADD_COMMAND(C_GETWORKSTATUS, "GetWorkStatus") \
	USB_ADD_COMMAND(C_REQUESTIDENTIFY, "RequestIdentify") \
	USB_ADD_COMMAND(C_GETIDENTIFY, "GetIdentify") \
	USB_ADD_COMMAND(C_REQUESTFLASH, "RequestFlash") \
	USB_ADD_COMMAND(C_REQUESTSENDWORK, "RequestSendWork") \
	USB_ADD_COMMAND(C_REQUESTSENDWORKSTATUS, "RequestSendWorkStatus") \
	USB_ADD_COMMAND(C_RESET, "Reset") \
	USB_ADD_COMMAND(C_SETBAUD, "SetBaud") \
	USB_ADD_COMMAND(C_SETDATA, "SetDataCtrl") \
	USB_ADD_COMMAND(C_SETFLOW, "SetFlowCtrl") \
	USB_ADD_COMMAND(C_SETMODEM, "SetModemCtrl") \
	USB_ADD_COMMAND(C_PURGERX, "PurgeRx") \
	USB_ADD_COMMAND(C_PURGETX, "PurgeTx") \
	USB_ADD_COMMAND(C_FLASHREPLY, "FlashReply") \
	USB_ADD_COMMAND(C_REQUESTDETAILS, "RequestDetails") \
	USB_ADD_COMMAND(C_GETDETAILS, "GetDetails") \
	USB_ADD_COMMAND(C_REQUESTRESULTS, "RequestResults") \
	USB_ADD_COMMAND(C_GETRESULTS, "GetResults") \
	USB_ADD_COMMAND(C_REQUESTQUEJOB, "RequestQueJob") \
	USB_ADD_COMMAND(C_REQUESTQUEJOBSTATUS, "RequestQueJobStatus") \
	USB_ADD_COMMAND(C_QUEJOB, "QueJob") \
	USB_ADD_COMMAND(C_QUEJOBSTATUS, "QueJobStatus") \
	USB_ADD_COMMAND(C_QUEFLUSH, "QueFlush") \
	USB_ADD_COMMAND(C_QUEFLUSHREPLY, "QueFlushReply") \
	USB_ADD_COMMAND(C_REQUESTVOLTS, "RequestVolts") \
	USB_ADD_COMMAND(C_GETVOLTS, "GetVolts") \
	USB_ADD_COMMAND(C_SENDTESTWORK, "SendTestWork") \
	USB_ADD_COMMAND(C_LATENCY, "SetLatency") \
	USB_ADD_COMMAND(C_SETLINE, "SetLine") \
	USB_ADD_COMMAND(C_VENDOR, "Vendor") \
	USB_ADD_COMMAND(C_SETFAN, "SetFan") \
	USB_ADD_COMMAND(C_FANREPLY, "GetFan") \
	USB_ADD_COMMAND(C_SETLED, "SetLED") \
	USB_ADD_COMMAND(C_SETPOWER, "SetPower") \
	USB_ADD_COMMAND(C_AVALON_TASK, "AvalonTask") \
	USB_ADD_COMMAND(C_AVALON_READ, "AvalonRead") \
	USB_ADD_COMMAND(C_GET_AVALON_READY, "AvalonReady") \
	USB_ADD_COMMAND(C_AVALON_RESET, "AvalonReset") \
	USB_ADD_COMMAND(C_GET_AVALON_RESET, "GetAvalonReset") \
	USB_ADD_COMMAND(C_FTDI_STATUS, "FTDIStatus") \
	USB_ADD_COMMAND(C_ENABLE_UART, "EnableUART") \
	USB_ADD_COMMAND(C_ANU_SEND_CMD, "ANUSendcmd") \
	USB_ADD_COMMAND(C_ANU_SEND_RDREG, "ANUSendrdreg") \
	USB_ADD_COMMAND(C_ANU_SEND_VOLT, "ANUSendvolt") \
	USB_ADD_COMMAND(C_BB_SET_VOLTAGE, "SetCoreVoltage") \
	USB_ADD_COMMAND(C_BB_GET_VOLTAGE, "GetCoreVoltage") \
	USB_ADD_COMMAND(C_BF_RESET, "BFReset") \
	USB_ADD_COMMAND(C_BF_OPEN, "BFOpen") \
	USB_ADD_COMMAND(C_BF_INIT, "BFInit") \
	USB_ADD_COMMAND(C_BF_CLOSE, "BFClose") \
	USB_ADD_COMMAND(C_BF_REQINFO, "BFRequestInfo") \
	USB_ADD_COMMAND(C_BF_GETINFO, "BFGetInfo") \
	USB_ADD_COMMAND(C_BF_REQRESET, "BFRequestReset") \
	USB_ADD_COMMAND(C_BF_GETRESET, "BFGetReset") \
	USB_ADD_COMMAND(C_BF_REQWORK, "BFRequestWork") \
	USB_ADD_COMMAND(C_BF_GETWORK, "BFGetWork") \
	USB_ADD_COMMAND(C_BF_GETRES, "BFGetResults") \
	USB_ADD_COMMAND(C_BF_FLUSH, "BFFlush") \
	USB_ADD_COMMAND(C_BF_IFLUSH, "BFInterruptFlush") \
	USB_ADD_COMMAND(C_BF_IDENTIFY, "BFIdentify") \
	USB_ADD_COMMAND(C_BF_DETECTCHIPS, "BFDetectChips") \
	USB_ADD_COMMAND(C_BF_CONFIG, "BFConfig") \
	USB_ADD_COMMAND(C_BF_GETTEMP, "BFGetTemp") \
	USB_ADD_COMMAND(C_BF_AUTOTUNE, "BFAutoTune") \
	USB_ADD_COMMAND(C_ATMEL_RESET, "AtmelReset") \
	USB_ADD_COMMAND(C_ATMEL_OPEN, "AtmelOpen") \
	USB_ADD_COMMAND(C_ATMEL_INIT, "AtmelInit") \
	USB_ADD_COMMAND(C_ATMEL_CLOSE, "AtmelClose") \
	USB_ADD_COMMAND(C_AVA2_READ, "Ava2Read") \
	USB_ADD_COMMAND(C_AVA2_WRITE, "Ava2Write") \
	USB_ADD_COMMAND(C_AVA4_READ, "Ava4Read") \
	USB_ADD_COMMAND(C_AVA4_WRITE, "Ava4Write") \
	USB_ADD_COMMAND(C_AVA7_READ, "Ava7Read") \
	USB_ADD_COMMAND(C_AVA7_WRITE, "Ava7Write") \
	USB_ADD_COMMAND(C_AVA8_READ, "Ava8Read") \
	USB_ADD_COMMAND(C_AVA8_WRITE, "Ava8Write") \
	USB_ADD_COMMAND(C_AVA9_READ, "Ava9Read") \
	USB_ADD_COMMAND(C_AVA9_WRITE, "Ava9Write") \
	USB_ADD_COMMAND(C_AVALC3_READ, "Avalc3Read") \
	USB_ADD_COMMAND(C_AVALC3_WRITE, "Avalc3Write") \
	USB_ADD_COMMAND(C_AVAM_READ, "AvamRead") \
	USB_ADD_COMMAND(C_AVAM_WRITE, "AvamWrite") \
        USB_ADD_COMMAND(C_BET_WRITE, "BlockErupterWrite") \
        USB_ADD_COMMAND(C_BET_READ, "BlockErupterRead") \
	USB_ADD_COMMAND(C_BF1_REQINFO, "BF1RequestInfo") \
	USB_ADD_COMMAND(C_BF1_GETINFO, "BF1GetInfo") \
	USB_ADD_COMMAND(C_BF1_REQRESET, "BF1RequestReset") \
	USB_ADD_COMMAND(C_BF1_GETRESET, "BF1GetReset") \
	USB_ADD_COMMAND(C_BF1_REQWORK, "BF1RequestWork") \
	USB_ADD_COMMAND(C_BF1_GETWORK, "BF1GetWork") \
	USB_ADD_COMMAND(C_BF1_GETRES, "BF1GetResults") \
	USB_ADD_COMMAND(C_BF1_FLUSH, "BF1Flush") \
	USB_ADD_COMMAND(C_BF1_IFLUSH, "BF1InterruptFlush") \
	USB_ADD_COMMAND(C_BF1_IDENTIFY, "BF1Identify") \
	USB_ADD_COMMAND(C_BXF_READ, "BXFRead") \
	USB_ADD_COMMAND(C_BXF_WORK, "BXFWork") \
	USB_ADD_COMMAND(C_BXF_TARGET, "BXFTarget") \
	USB_ADD_COMMAND(C_BXF_VERSION, "BXFVersion") \
	USB_ADD_COMMAND(C_BXF_MAXROLL, "BXFMaxRoll") \
	USB_ADD_COMMAND(C_BXF_FLUSH, "BXFFlush") \
	USB_ADD_COMMAND(C_BXF_CLOCK, "BXFClock") \
	USB_ADD_COMMAND(C_BXF_LEDMODE, "BXFLedMode") \
	USB_ADD_COMMAND(C_BXF_DEBUGMODE, "BXFDebugMode") \
	USB_ADD_COMMAND(C_BXM_FLUSH, "BXMFlush") \
	USB_ADD_COMMAND(C_BXM_SRESET, "BXMSReset") \
	USB_ADD_COMMAND(C_BXM_SETLATENCY, "BXMSetLatency") \
	USB_ADD_COMMAND(C_BXM_SECR, "BXMSetEventCharRequest") \
	USB_ADD_COMMAND(C_BXM_SETBITMODE, "BXMSetBitmodeRequest") \
	USB_ADD_COMMAND(C_BXM_CLOCK, "BXMClock") \
	USB_ADD_COMMAND(C_BXM_CLOCKDIV, "BXMClockDiv") \
	USB_ADD_COMMAND(C_BXM_LOOP, "BXMLoop") \
	USB_ADD_COMMAND(C_BXM_ADBUS, "BXMADBus") \
	USB_ADD_COMMAND(C_BXM_ACBUS, "BXMACBus") \
	USB_ADD_COMMAND(C_BXM_PURGERX, "BXMPurgeRX") \
	USB_ADD_COMMAND(C_BXM_PURGETX, "BXMPurgeTX") \
	USB_ADD_COMMAND(C_BXM_CSLOW, "BXMCSLow") \
	USB_ADD_COMMAND(C_BXM_CSHIGH, "BXMCSHigh") \
	USB_ADD_COMMAND(C_BXM_RESET, "BXMReset") \
	USB_ADD_COMMAND(C_BXM_SPITX, "BXMSPITX") \
	USB_ADD_COMMAND(C_BXM_SPIRX, "BXMSPIRX") \
	USB_ADD_COMMAND(C_HF_RESET, "HFReset") \
	USB_ADD_COMMAND(C_HF_PLL_CONFIG, "HFPLLConfig") \
	USB_ADD_COMMAND(C_HF_ADDRESS, "HFAddress") \
	USB_ADD_COMMAND(C_HF_BAUD, "HFBaud") \
	USB_ADD_COMMAND(C_HF_HASH, "HFHash") \
	USB_ADD_COMMAND(C_HF_NONCE, "HFNonce") \
	USB_ADD_COMMAND(C_HF_ABORT, "HFAbort") \
	USB_ADD_COMMAND(C_HF_STATUS, "HFStatus") \
	USB_ADD_COMMAND(C_HF_CONFIG, "HFConfig") \
	USB_ADD_COMMAND(C_HF_STATISTICS, "HFStatistics") \
	USB_ADD_COMMAND(C_HF_CLOCKGATE, "HFClockGate") \
	USB_ADD_COMMAND(C_HF_USB_INIT, "HFUSBInit") \
	USB_ADD_COMMAND(C_HF_DFU, "HFDFU") \
	USB_ADD_COMMAND(C_HF_DIE_STATUS, "HFDieStatus") \
	USB_ADD_COMMAND(C_HF_GWQ_STATUS, "HFGWQStatus") \
	USB_ADD_COMMAND(C_HF_WORK_RESTART, "HFWorkRestart") \
	USB_ADD_COMMAND(C_HF_GWQSTATS, "HFGWQStats") \
	USB_ADD_COMMAND(C_HF_NOTICE, "HFNotice") \
	USB_ADD_COMMAND(C_HF_PING, "HFPing") \
	USB_ADD_COMMAND(C_HF_FAN, "HFFan") \
	USB_ADD_COMMAND(C_HRO_WRITE, "HROWrite") \
	USB_ADD_COMMAND(C_HRO_READ, "HRORead") \
	USB_ADD_COMMAND(C_OP_NAME, "HFName") \
	USB_ADD_COMMAND(C_HF_GETHEADER, "HFGetHeader") \
	USB_ADD_COMMAND(C_HF_GETDATA, "HFGetData") \
	USB_ADD_COMMAND(C_HF_CLEAR_READ, "HFClearRead") \
	USB_ADD_COMMAND(C_CTA_READ, "CTARead") \
	USB_ADD_COMMAND(C_CTA_WRITE, "CTAWrite") \
	USB_ADD_COMMAND(C_MCP_GETGPIOSETTING, "MCPGetGPIOSetting") \
	USB_ADD_COMMAND(C_MCP_SETGPIOSETTING, "MCPSetGPIOSetting") \
	USB_ADD_COMMAND(C_MCP_GETGPIOPINVAL, "MCPGetGPIOPinVal") \
	USB_ADD_COMMAND(C_MCP_SETGPIOPINVAL, "MCPSetGPIOPinVal") \
	USB_ADD_COMMAND(C_MCP_GETGPIOPINDIR, "MCPGetGPIOPinDir") \
	USB_ADD_COMMAND(C_MCP_SETGPIOPINDIR, "MCPSetGPIOPinDir") \
	USB_ADD_COMMAND(C_MCP_SETSPISETTING, "MCPSetSPISetting") \
	USB_ADD_COMMAND(C_MCP_GETSPISETTING, "MCPGetSPISetting") \
	USB_ADD_COMMAND(C_MCP_SPITRANSFER, "MCPSPITransfer") \
	USB_ADD_COMMAND(C_MCP_SPICANCEL, "MCPSPICancel") \
	USB_ADD_COMMAND(C_BITMAIN_SEND, "BitmainSend") \
	USB_ADD_COMMAND(C_BITMAIN_READ, "BitmainRead") \
	USB_ADD_COMMAND(C_BITMAIN_TOKEN_TXCONFIG, "BitmainTokenTxConfig") \
	USB_ADD_COMMAND(C_BITMAIN_TOKEN_TXTASK, "BitmainTokenTxTask") \
	USB_ADD_COMMAND(C_BITMAIN_TOKEN_RXSTATUS, "BitmainTokenRxStatus") \
	USB_ADD_COMMAND(C_BITMAIN_DATA_RXSTATUS, "BitmainDataRxStatus") \
	USB_ADD_COMMAND(C_BITMAIN_DATA_RXNONCE, "BitmainDataRxNonce")

/* Create usb_cmds enum from USB_PARSE_COMMANDS macro */
enum usb_cmds {
	USB_PARSE_COMMANDS(ENUMERATION)
	C_MAX
};

struct device_drv;
struct cgpu_info;

bool async_usb_transfers(void);
void cancel_usb_transfers(void);
void usb_all(int level);
void usb_list(void);
const char *usb_cmdname(enum usb_cmds cmd);
void usb_applog(struct cgpu_info *cgpu, enum usb_cmds cmd, char *msg, int amount, int err);
void blacklist_cgpu(struct cgpu_info *cgpu);
void whitelist_cgpu(struct cgpu_info *cgpu);
void usb_nodev(struct cgpu_info *cgpu);
struct cgpu_info *usb_copy_cgpu(struct cgpu_info *orig);
struct cgpu_info *usb_alloc_cgpu(struct device_drv *drv, int threads);
struct cgpu_info *usb_free_cgpu(struct cgpu_info *cgpu);
void usb_uninit(struct cgpu_info *cgpu);
bool usb_init(struct cgpu_info *cgpu, struct libusb_device *dev, struct usb_find_devices *found);
void __usb_detect(struct device_drv *drv, struct cgpu_info *(*device_detect)(struct libusb_device *, struct usb_find_devices *),
		  bool single);
#define usb_detect(drv, cgpu) __usb_detect(drv, cgpu, false)
#define usb_detect_one(drv, cgpu) __usb_detect(drv, cgpu, true)
struct api_data *api_usb_stats(int *count);
void update_usb_stats(struct cgpu_info *cgpu);
void usb_reset(struct cgpu_info *cgpu);
int _usb_read(struct cgpu_info *cgpu, int intinfo, int epinfo, char *buf, size_t bufsiz, int *processed, int timeout, const char *end, enum usb_cmds cmd, bool readonce, bool cancellable);
int _usb_write(struct cgpu_info *cgpu, int intinfo, int epinfo, char *buf, size_t bufsiz, int *processed, int timeout, enum usb_cmds);
int _usb_transfer(struct cgpu_info *cgpu, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint32_t *data, int siz, unsigned int timeout, enum usb_cmds cmd);
int _usb_transfer_read(struct cgpu_info *cgpu, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, char *buf, int bufsiz, int *amount, unsigned int timeout, enum usb_cmds cmd);
int usb_ftdi_cts(struct cgpu_info *cgpu);
int _usb_ftdi_set_latency(struct cgpu_info *cgpu, int intinfo);
#define usb_ftdi_set_latency(_cgpu) _usb_ftdi_set_latency(_cgpu, DEFAULT_INTINFO)
void usb_buffer_clear(struct cgpu_info *cgpu);
uint32_t usb_buffer_size(struct cgpu_info *cgpu);
int _usb_interface(struct cgpu_info *cgpu, int intinfo);
#define usb_interface(_cgpu) _usb_interface(_cgpu, DEFAULT_INTINFO)
enum sub_ident usb_ident(struct cgpu_info *cgpu);
void usb_set_dev_start(struct cgpu_info *cgpu);
void usb_cleanup();
void usb_initialise();
void *usb_resource_thread(void *userdata);
void initialise_usblocks(void);

#define usb_read(cgpu, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_INTINFO, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, NULL, cmd, false, false)

#define usb_read_cancellable(cgpu, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_INTINFO, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, NULL, cmd, false, true)

#define usb_read_ii(cgpu, intinfo, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, intinfo, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, NULL, cmd, false, false)

#define usb_read_once(cgpu, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_INTINFO, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, NULL, cmd, true, false)

#define usb_read_ii_once(cgpu, intinfo, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, intinfo, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, NULL, cmd, true, false)

#define usb_read_once_timeout(cgpu, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, DEFAULT_INTINFO, DEFAULT_EP_IN, buf, bufsiz, read, timeout, NULL, cmd, true, false)

#define usb_read_once_timeout_cancellable(cgpu, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, DEFAULT_INTINFO, DEFAULT_EP_IN, buf, bufsiz, read, timeout, NULL, cmd, true, true)

#define usb_read_ii_once_timeout(cgpu, intinfo, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, intinfo, DEFAULT_EP_IN, buf, bufsiz, read, timeout, NULL, cmd, true, false)

#define usb_read_nl(cgpu, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_INTINFO, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, "\n", cmd, false, false)

#define usb_read_nl_timeout(cgpu, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, DEFAULT_INTINFO, DEFAULT_EP_IN, buf, bufsiz, read, timeout, "\n", cmd, false, false)

#define usb_read_ok(cgpu, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_INTINFO, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, "OK\n", cmd, false, false)

#define usb_read_ok_timeout(cgpu, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, DEFAULT_INTINFO, DEFAULT_EP_IN, buf, bufsiz, read, timeout, "OK\n", cmd, false, false)

#define usb_read_ep(cgpu, ep, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_INTINFO, ep, buf, bufsiz, read, DEVTIMEOUT, NULL, cmd, false, false)

#define usb_read_timeout(cgpu, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, DEFAULT_INTINFO, DEFAULT_EP_IN, buf, bufsiz, read, timeout, NULL, cmd, false, false)

#define usb_read_timeout_cancellable(cgpu, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, DEFAULT_INTINFO, DEFAULT_EP_IN, buf, bufsiz, read, timeout, NULL, cmd, false, true)

#define usb_read_ii_timeout(cgpu, intinfo, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, intinfo, DEFAULT_EP_IN, buf, bufsiz, read, timeout, NULL, cmd, false, false)

#define usb_read_ii_timeout_cancellable(cgpu, intinfo, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, intinfo, DEFAULT_EP_IN, buf, bufsiz, read, timeout, NULL, cmd, false, true)

#define usb_read_ep_timeout(cgpu, ep, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, DEFAULT_INTINFO, ep, buf, bufsiz, read, timeout, NULL, cmd, false, false)

#define usb_write(cgpu, buf, bufsiz, wrote, cmd) \
	_usb_write(cgpu, DEFAULT_INTINFO, DEFAULT_EP_OUT, buf, bufsiz, wrote, DEVTIMEOUT, cmd)

#define usb_write_ii(cgpu, intinfo, buf, bufsiz, wrote, cmd) \
	_usb_write(cgpu, intinfo, DEFAULT_EP_OUT, buf, bufsiz, wrote, DEVTIMEOUT, cmd)

#define usb_write_ep(cgpu, ep, buf, bufsiz, wrote, cmd) \
	_usb_write(cgpu, DEFAULT_INTINFO, ep, buf, bufsiz, wrote, DEVTIMEOUT, cmd)

#define usb_write_timeout(cgpu, buf, bufsiz, wrote, timeout, cmd) \
	_usb_write(cgpu, DEFAULT_INTINFO, DEFAULT_EP_OUT, buf, bufsiz, wrote, timeout, cmd)

#define usb_write_ep_timeout(cgpu, ep, buf, bufsiz, wrote, timeout, cmd) \
	_usb_write(cgpu, DEFAULT_INTINFO, ep, buf, bufsiz, wrote, timeout, cmd)

#define usb_transfer(cgpu, typ, req, val, idx, cmd) \
	_usb_transfer(cgpu, typ, req, val, idx, NULL, 0, DEVTIMEOUT, cmd)

#define usb_transfer_data(cgpu, typ, req, val, idx, data, len, cmd) \
	_usb_transfer(cgpu, typ, req, val, idx, data, len, DEVTIMEOUT, cmd)

#define usb_transfer_read(cgpu, typ, req, val, idx, buf, bufsiz, read, cmd) \
	_usb_transfer_read(cgpu, typ, req, val, idx, buf, bufsiz, read, DEVTIMEOUT, cmd)

#endif
