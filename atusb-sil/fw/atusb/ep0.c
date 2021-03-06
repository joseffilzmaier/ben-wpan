/*
 * atspi/ep0.c - EP0 extension protocol
 *
 * Written 2008-2010 by Werner Almesberger
 * Copyright 2008-2010 Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <stdint.h>

#ifndef NULL
#define NULL 0
#endif

#include "regs.h"
//#include "uart.h"
#include "usb.h"
#include "atusb/ep0.h"
#include "at86rf230.h"
#include "version.h"


extern void reset_rf(void);
extern void test_mode(void);


#define debug(...)
#define error(...)


/*
 * SDCC 2.8.0 had a number of code generation bugs that appeared in the big
 * switch statement of my_setup. SDCC_FORCE_UPDATE forced the value of the
 * "size" variable to be written to memory. This work-around doesn't seem
 * to be necessary with 2.9.0, but we keep it around, just in case.
 *
 * Unfortunately, the setup->bRequest garbling bug is still with us. Without
 * the evaluation forced with SDCC_FORCE_EVAL, sdcc gets confused about the
 * value of setup->bRequest and then rejects all SETUP requests.
 */

#define	SDCC_FORCE_EVAL(type, value)	\
    do {				\
	static volatile type foo;	\
	foo = value;			\
    } while (0)

#define	SDCC_FORCE_UPDATE(type, var)	\
    do {				\
	volatile type foo;		\
	foo = var;			\
	var = foo;			\
    } while (0)


static const uint8_t id[] = { EP0ATUSB_MAJOR, EP0ATUSB_MINOR, HW_TYPE };
static __xdata uint8_t buf[MAX_PSDU+3]; /* command, PHDR, and LQI */
static uint8_t size;


static void spi_send(uint8_t v)
{
	uint8_t mask;

	for (mask = 0x80; mask; mask >>= 1) {
		MOSI = !!(v & mask);
		SCLK = 1;
		SCLK = 0;
	}
}


static uint8_t spi_recv(void)
{
	uint8_t res = 0;
	uint8_t i;

	for (i = 0; i != 8; i++) {
		res = (res << 1) | MISO;
		SCLK = 1;
		SCLK = 0;
	}
	return res;
}


static void do_buf_write(void *user)
{
	uint8_t i;

	user; /* suppress warning */
	nSS = 0;
	for (i = 0; i != size; i++)
		spi_send(buf[i]);
	nSS = 1;
}


#define	BUILD_OFFSET	7	/* '#' plus "65535" plus ' ' */


static __bit my_setup(struct setup_request *setup) __reentrant
{
	unsigned tmp;
	uint8_t i;

	switch (setup->bmRequestType | setup->bRequest << 8) {
	case ATUSB_FROM_DEV(ATUSB_ID):
		debug("ATUSB_ID\n");
		if (setup->wLength > 3)
			return 0;
		usb_send(&ep0, id, setup->wLength, NULL, NULL);
		return 1;
	case ATUSB_FROM_DEV(ATUSB_BUILD):
		debug("ATUSB_BUILD\n");
		tmp = build_number;
		for (i = BUILD_OFFSET-2; tmp; i--) {
			buf[i] = (tmp % 10)+'0';
			tmp /= 10;
		}
		buf[i] = '#';
		buf[BUILD_OFFSET-1] = ' ';
		for (size = 0; build_date[size]; size++)
			buf[BUILD_OFFSET+size] = build_date[size];
		size += BUILD_OFFSET-i;
		SDCC_FORCE_EVAL(uint8_t, setup->bRequest);
		if (size > setup->wLength)
			return 0;
		usb_send(&ep0, buf+i, size, NULL, NULL);
		return 1;

	case ATUSB_TO_DEV(ATUSB_RESET):
		debug("ATUSB_RESET\n");
		RSTSRC = SWRSF;
		while (1);

	case ATUSB_TO_DEV(ATUSB_RF_RESET):
		debug("ATUSB_RF_RESET\n");
		reset_rf();
		return 1;

	case ATUSB_FROM_DEV(ATUSB_POLL_INT):
		debug("ATUSB_POLL_INT\n");
		if (setup->wLength < 1)
			return 0;
		*buf = IRQ_RF;
		usb_send(&ep0, buf, 1, NULL, NULL);
		return 1;

	case ATUSB_TO_DEV(ATUSB_TEST):
		debug("ATUSB_TEST\n");
		test_mode();
		return 1;

	case ATUSB_TO_DEV(ATUSB_REG_WRITE):
		debug("ATUSB_REG_WRITE\n");
		nSS = 0;
		spi_send(AT86RF230_REG_WRITE | setup->wIndex);
		spi_send(setup->wValue);
		nSS = 1;
		return 1;
	case ATUSB_FROM_DEV(ATUSB_REG_READ):
		debug("ATUSB_REG_READ\n");
		nSS = 0;
		spi_send(AT86RF230_REG_READ | setup->wIndex);
		*buf = spi_recv();
		nSS = 1;
		usb_send(&ep0, buf, 1, NULL, NULL);
		return 1;

	case ATUSB_TO_DEV(ATUSB_BUF_WRITE):
		debug("ATUSB_BUF_WRITE\n");
		if (setup->wLength < 1)
			return 0;
		if (setup->wLength > MAX_PSDU)
			return 0;
		buf[0] = AT86RF230_BUF_WRITE;
		buf[1] = setup->wLength;
		size = setup->wLength+2;
		usb_recv(&ep0, buf+2, setup->wLength, do_buf_write, NULL);
		return 1;
	case ATUSB_FROM_DEV(ATUSB_BUF_READ):
		debug("ATUSB_BUF_READ\n");
		if (setup->wLength < 2)			/* PHR+LQI */
			return 0;
		if (setup->wLength > MAX_PSDU+2)	/* PHR+PSDU+LQI */
			return 0;
		nSS = 0;
		spi_send(AT86RF230_BUF_READ);
		size = spi_recv();
		if (size >= setup->wLength)
			size = setup->wLength-1;
		for (i = 0; i != size+1; i++)
			buf[i] = spi_recv();
		nSS = 1;
		usb_send(&ep0, buf, size+1, NULL, NULL);
		return 1;

	case ATUSB_TO_DEV(ATUSB_SRAM_WRITE):
		debug("ATUSB_SRAM_WRITE\n");
		if (setup->wIndex > SRAM_SIZE)
			return 0;
		if (setup->wIndex+setup->wLength > SRAM_SIZE)
			return 0;
		buf[0] = AT86RF230_SRAM_WRITE;
		buf[1] = setup->wIndex;
		size = setup->wLength+2;
		usb_recv(&ep0, buf+2, setup->wLength, do_buf_write, NULL);
		return 1;
	case ATUSB_TO_DEV(ATUSB_SRAM_READ):
		debug("ATUSB_SRAM_READ\n");
		if (setup->wIndex > SRAM_SIZE)
			return 0;
		if (setup->wIndex+setup->wLength > SRAM_SIZE)
			return 0;
		nSS = 0;
		spi_send(AT86RF230_SRAM_READ);
		spi_send(setup->wIndex);
		for (i = 0; i != size; i++)
			buf[i] = spi_recv();
		nSS = 1;
		usb_send(&ep0, buf, size, NULL, NULL);
		return 1;

	default:
		error("Unrecognized SETUP: 0x%02x 0x%02x ...\n",
		    setup->bmRequestType, setup->bRequest);
		return 0;
	}
}


void ep0_init(void)
{
	user_setup = my_setup;
}
