/*
 * include/cntr/ep0.h - EP0 extension protocol
 *
 * Written 2008-2010 by Werner Almesberger
 * Copyright 2008-2010 Werner Almesberger
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
 * ->host	CNTR_ID		-		-	3
 * ->host	CNTR_BUILD		-		-	#bytes
 * host->	CNTR_RESET		-		-	0
 *
 * ->host	CNTR_READ		-		0	12
 */

/*
 * EP0 protocol:
 *
 * 0.0	initial release
 */

#define EP0CNTR_MAJOR	0	/* EP0 protocol, major revision */
#define EP0CNTR_MINOR	0	/* EP0 protocol, minor revision */


/*
 * bmRequestType:
 *
 * D7 D6..5 D4...0
 * |  |     |
 * direction (0 = host->dev)
 *    type (2 = vendor)
 *          recipient (0 = device)
 */


#define	CNTR_TO_DEV(req)	(0x40 | (req) << 8)
#define	CNTR_FROM_DEV(req)	(0xc0 | (req) << 8)


enum cntr_requests {
	CNTR_ID			= 0x00,
	CNTR_BUILD,
	CNTR_RESET,
	CNTR_READ		= 0x10,
};


void ep0_init(void);

#endif /* !EP0_H */
