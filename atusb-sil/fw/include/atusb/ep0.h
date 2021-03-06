/*
 * include/atusb/ep0.h - EP0 extension protocol
 *
 * Written 2008-2011 by Werner Almesberger
 * Copyright 2008-2011 Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifndef EP0_H
#define EP0_H

/*
 * Direction	bRequest		wValue		wIndex	wLength
 *
 * ->host	ATUSB_ID		-		-	3
 * ->host	ATUSB_BUILD		-		-	#bytes
 * host->	ATUSB_RESET		-		-	0
 *
 * host->	ATUSB_RF_RESET		-		-	0
 * ->host	ATUSB_POLL_INT		-		-	1
 * host->	ATUSB_TEST		-		-	0
 *
 * host->	ATUSB_REG_WRITE		value		addr	0
 * ->host	ATUSB_REG_READ		-		addr	1
 * host->	ATUSB_BUF_WRITE		-		-	#bytes
 * ->host	ATUSB_BUF_READ		-		-	#bytes
 * host->	ATUSB_SRAM_WRITE	-		addr	#bytes
 * ->host	ATUSB_SRAM_READ		-		addr	#bytes
 */

/*
 * EP0 protocol:
 *
 * 0.0	initial release
 * 0.1  addition of ATUSB_TEST
 */

#define EP0ATUSB_MAJOR	0	/* EP0 protocol, major revision */
#define EP0ATUSB_MINOR	1	/* EP0 protocol, minor revision */

#define	HW_TYPE_100813	0	/* 2010-08-13 */
#define	HW_TYPE_101216	1	/* 2010-12-16 */
#define	HW_TYPE_110131	2	/* 2011-01-31, ATmega32U2-based */


/*
 * bmRequestType:
 *
 * D7 D6..5 D4...0
 * |  |     |
 * direction (0 = host->dev)
 *    type (2 = vendor)
 *          recipient (0 = device)
 */


#define	ATUSB_TO_DEV(req)	(0x40 | (req) << 8)
#define	ATUSB_FROM_DEV(req)	(0xc0 | (req) << 8)


enum atspi_requests {
	ATUSB_ID			= 0x00,
	ATUSB_BUILD,
	ATUSB_RESET,
	ATUSB_RF_RESET			= 0x10,
	ATUSB_POLL_INT,
	ATUSB_TEST,
	ATUSB_REG_WRITE			= 0x20,
	ATUSB_REG_READ,
	ATUSB_BUF_WRITE,
	ATUSB_BUF_READ,
	ATUSB_SRAM_WRITE,
	ATUSB_SRAM_READ,
};


void ep0_init(void);

#endif /* !EP0_H */
