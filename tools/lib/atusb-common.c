/*
 * lib/atusb-common.c - ATUSB access functions shared by all ATUSB drivers
 *
 * Written 2010-2011 by Werner Almesberger
 * Copyright 2010-2011 Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <usb.h>

#include "atusb/ep0.h"
#include "atusb/usb-ids.h"

#include "usbopen.h"
#include "driver.h"
#include "atusb-common.h"


/* ----- error handling ---------------------------------------------------- */


int atusb_error(void *handle)
{
	struct atusb_dsc *dsc = handle;

	return dsc->error;
}


int atusb_clear_error(void *handle)
{
	struct atusb_dsc *dsc = handle;
	int ret;

	ret = dsc->error;
	dsc->error = 0;
	return ret;
}


/* ----- open/close -------------------------------------------------------- */


void *atusb_open(const char *arg)
{
	usb_dev_handle *dev;
	struct atusb_dsc *dsc;

	usb_unrestrict();
	if (arg)
		restrict_usb_path(arg);
	dev = open_usb(USB_VENDOR, USB_PRODUCT);
	if (!dev) {
		fprintf(stderr, ":-(\n");
		return NULL;
	}

	dsc = malloc(sizeof(*dsc));
	if (!dsc) {
		perror("malloc");
		exit(1);
	}

	dsc->dev = dev;
	dsc->error = 0;

	return dsc;
}


void atusb_close(void *handle)
{
	/* to do */
}


/* ----- device mode ------------------------------------------------------- */


void atusb_reset(void *handle)
{
	struct atusb_dsc *dsc = handle;
	int res;

	if (dsc->error)
		return;

	res =
	    usb_control_msg(dsc->dev, TO_DEV, ATUSB_RESET, 0, 0, NULL, 0, 1000);
	if (res < 0) {
		fprintf(stderr, "ATUSB_RESET: %d\n", res);
		dsc->error = 1;
	}
}


void atusb_reset_rf(void *handle)
{
	struct atusb_dsc *dsc = handle;
	int res;

	if (dsc->error)
		return;

	res = usb_control_msg(dsc->dev, TO_DEV, ATUSB_RF_RESET, 0, 0, NULL,
	    0, 1000);
	if (res < 0) {
		fprintf(stderr, "ATUSB_RF_RESET: %d\n", res);
		dsc->error = 1;
	}
}


void atusb_test_mode(void *handle)
{
	struct atusb_dsc *dsc = handle;
	int res;

	if (dsc->error)
		return;

	res =
	    usb_control_msg(dsc->dev, TO_DEV, ATUSB_TEST, 0, 0, NULL, 0, 1000);
	if (res < 0) {
		fprintf(stderr, "ATUSB_TEST: %d\n", res);
		dsc->error = 1;
	}
}


/* ----- SLP_TR ------------------------------------------------------------ */


void atusb_slp_tr(void *handle, int on, int pulse)
{
	struct atusb_dsc *dsc = handle;
	int res;

	if (dsc->error)
		return;

	if (!on || !pulse) {
		fprintf(stderr,
		    "SLP_TR mode on=%d pulse=%d not supported\n", on, pulse);
		return;
	}

	res = usb_control_msg(dsc->dev, TO_DEV, ATUSB_SLP_TR, 0, 0, NULL, 0,
	    1000);
	if (res < 0) {
		fprintf(stderr, "ATUSB_SLP_TR: %d\n", res);
		dsc->error = 1;
	}
}


/* ----- RF interrupt ------------------------------------------------------ */


int atusb_interrupt(void *handle)
{
	struct atusb_dsc *dsc = handle;
	uint8_t buf;
	int res;

	if (dsc->error)
		return -1;
	
	res = usb_control_msg(dsc->dev, FROM_DEV, ATUSB_POLL_INT, 0, 0,
	    (void *) &buf, 1, 1000);
	if (res < 0) {
		fprintf(stderr, "ATUSB_POLL_INT: %d\n", res);
		dsc->error = 1;
		return -1;
	}

	return buf;
}


/* ----- CLKM handling ----------------------------------------------------- */


/*
 * ATmega32U2-based boards don't allow disabling CLKM, so we keep it at 8 MHz.
 * We could accommodate a choice between 8 MHz and 16 MHz, but that's for
 * later.
 */

int atusb_set_clkm(void *handle, int mhz)
{
	struct atusb_dsc *dsc = handle;
	uint8_t ids[3];
	int res;

	if (dsc->error)
		return 0;
	res = usb_control_msg(dsc->dev, FROM_DEV, ATUSB_ID, 0, 0,
	    (void *) ids, 3, 1000);
	if (res < 0) {
		fprintf(stderr, "ATUSB_ID: %s\n", usb_strerror());
		dsc->error = 1;
		return 0;
	}
	switch (ids[2]) {
	case HW_TYPE_100813:
	case HW_TYPE_101216:
		break;
	case HW_TYPE_110131:
		if (mhz == 0 || mhz == 8)
			return 1;
		fprintf(stderr, "this board only supports CLKM = 8 MHz\n");
		return 0;
	default:
		fprintf(stderr,
		    "atusb_set_clkm: unknown hardware type 0x%02x\n", ids[2]);
		return 0;
	}
	return atrf_set_clkm_generic(atusb_driver.reg_write, dsc, mhz);
}


/* ----- Driver-specific hacks --------------------------------------------- */


void *atusb_dev_handle(void *handle)
{
	struct atusb_dsc *dsc = handle;

	return dsc->dev;
}