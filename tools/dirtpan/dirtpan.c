/*
 * dirtpan/dirtpan.c - Quick and dirty IPv4 over 802.15.4 tunnel
 *
 * Written 2011 by Werner Almesberger
 * Copyright 2011 Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#include <ieee802154.h>

#include "daemon.h"


/*
 * Control byte structure:
 *
 * +--7--+--6--+--5--+--4--+--3--+--2--+--1--+--0--+
 * |  0  |  0  |  0  |  0  |  0  | seq | pck_type  |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * =============
 *
 * Bits 6 and 7 must be zero to avoid conflicts with RFC 4944 (LoWPAN)
 */

#define	SEQ	4
#define	PT_MASK	3

enum packet_type {
	pt_first	= 0,
	pt_next		= 1,
	pt_ack		= 2,
};


#define	TUN_DEV	"/dev/net/tun"

#define	MAX_FRAG	(127-11-2-1)	/* MHDR, FCS, control byte */
#define	MAX_PACKET	2000
#define	MAX_TRIES	5
#define	T_REASS_MS	200
#define	T_ACK_MS	50


static struct {
	unsigned tx_pck;	/* packets enqueued */
	unsigned lost;		/* packets never acked */
	unsigned rx_pck;	/* packets successfully received */
	unsigned tx_frm;	/* frames sent */
	unsigned rx_frm;	/* frames received */
	unsigned tx_frst;	/* pt_first sent */
	unsigned rx_frst;	/* pt_first received */
	unsigned tx_nxt;	/* pt_next sent */
	unsigned rx_nxt;	/* pt_next received */
	unsigned tx_ack;	/* pt_ack sent */
	unsigned rx_ack;	/* pt_ack received */
	unsigned invalid;	/* invalid packet type */
	unsigned not_rx;	/* pt_next but not receiving */
	unsigned rx_seq;	/* wrong rx sequence */
	unsigned not_tx;	/* pt_ack but not sending */
	unsigned ack_seq;	/* wrong ack sequence */
	unsigned garbled;	/* IPv4 length problem */
	unsigned no_ack;	/* no ack response (giving up) */
	unsigned retry;		/* retransmit */
	unsigned reass;		/* reassembly timeout */
} stats;

static int tun, net;
static uint8_t rx_packet[MAX_PACKET], tx_packet[MAX_PACKET+1];
static void *rx_pos, *tx_pos;
static int rx_left, tx_left;
static int txing = 0, rxing = 0;
static int rx_seq, tx_seq = 0;
static int retries;
static int debug = 0;


/* ----- Debugging --------------------------------------------------------- */


static void debug_label(const char *label)
{
	fprintf(stderr, "%s(%c%c)",
	    label, txing ? 'T' : '-', rxing ? 'R' : '-');
}


static void dump(const void *buf, int size)
{
	const uint8_t *p = buf;

	while (size--) {
		fprintf(stderr, "%s%02x", p == buf ? "" : " ", *p);
		p++;
	}
}


static void debug_ip(const char *label, void *buf, int size)
{
	if (debug < 2)
		return;
	debug_label(label);
	fprintf(stderr, ", %d: ", size);
	dump(buf, size);
	fprintf(stderr, "\n");
}


static void debug_dirt(const char *label, void *buf, int size)
{
	const uint8_t *p = buf;

	if (!debug)
		return;
	if (debug == 1) {
		if (size) {
			fprintf(stderr, "%c%d",
			    (label[1] == '>' ? "FNA?" : "fna?")[*p & PT_MASK],
			    *p & SEQ ? 0 : 1);
		}
		return;
	}
	debug_label(label);
	fprintf(stderr, ", %d+1: ", size-1);
	if (size) {
		fprintf(stderr, "%02x(%c%d) | ",
		    *p, "FNA?"[*p & PT_MASK], *p & SEQ ? 0 : 1);
		dump(buf+1, size-1);
	}
	fprintf(stderr, "\n");

}


static void debug_timeout(const char *label)
{
	if (!debug)
		return;
	if (debug == 1) {
		fprintf(stderr, "*%c", *label);
		return;
	}
	debug_label(label);
	fprintf(stderr, "\n");
}


/* ----- Statistics -------------------------------------------------------- */


static void handle_usr1(int sig)
{
	fprintf(stderr, "\n"
	    "tx_pck\t%6u\n"  "tx_lost\t%6u\n" "rx_pck\t%6u\n"
	    "tx_frm\t%6u\n"  "rx_frm\t%6u\n"  "tx_frst\t%6u\n"
	    "rx_frst\t%6u\n" "tx_nxt\t%6u\n"  "rx_nxt\t%6u\n"
	    "tx_ack\t%6u\n"  "rx_ack\t%6u\n"  "invalid\t%6u\n"
	    "not_rx\t%6u\n"  "rx_seq\t%6u\n"  "not_tx\t%6u\n"
	    "ack_seq\t%6u\n" "garbled\t%6u\n" "no_ack\t%6u\n"
	    "retry\t%6u\n"   "reass\t%6u\n",
	    stats.tx_pck, stats.lost, stats.rx_pck,
	    stats.tx_frm, stats.rx_frm, stats.tx_frst,
	    stats.rx_frst, stats.tx_nxt, stats.rx_nxt,
	    stats.tx_ack, stats.rx_ack, stats.invalid,
	    stats.not_rx, stats.rx_seq, stats.not_tx,
	    stats.ack_seq, stats.garbled, stats.no_ack,
	    stats.retry, stats.reass);
}


static void handle_usr2(int sig)
{
	memset(&stats, 0, sizeof(stats));
}


/* ----- Timers ------------------------------------------------------------ */


static struct timeval t_reass, t_ack;


static void start_timer(struct timeval *t, int ms)
{
	assert(!t->tv_sec && !t->tv_usec);
	gettimeofday(t, NULL);
	t->tv_usec += 1000*ms;
	while (t->tv_usec >= 1000000) {
		t->tv_sec++;
		t->tv_usec -= 1000000;
	}
}


static void stop_timer(struct timeval *t)
{
	assert(t->tv_sec || t->tv_usec);
	t->tv_sec = 0;
	t->tv_usec = 0;
}


static const struct timeval *next_timer(int n, ...)
{
	va_list ap;
	const struct timeval *next = NULL;
	const struct timeval *t;

	va_start(ap, n);
	while (n--) {
		t = va_arg(ap, const struct timeval *);
		if (!t->tv_sec && !t->tv_usec)
			continue;
		if (next) {
			if (next->tv_sec < t->tv_sec)
				continue;
			if (next->tv_sec == t->tv_sec &&
			    next->tv_usec < t->tv_usec)
				continue;
		}
		next = t;
	}
	va_end(ap);
	return next;
}


static struct timeval *timer_delta(const struct timeval *t)
{
	static struct timeval d;

	if (!t)
		return NULL;

	gettimeofday(&d, NULL);
	d.tv_sec = t->tv_sec-d.tv_sec;
	d.tv_usec = t->tv_usec-d.tv_usec;

	while (d.tv_usec < 0) {
		d.tv_sec--;
		d.tv_usec += 1000000;
	}
	if (d.tv_sec < 0)
		d.tv_sec = d.tv_usec = 0;

	return &d;
}


/* ----- Packet/frame delivery --------------------------------------------- */


static inline int send_size(void)
{
	return tx_left > MAX_FRAG ? MAX_FRAG : tx_left;
}


static void write_buf(int fd, void *buf, int size)
{
	ssize_t wrote;

	wrote = write(fd, buf, size);
	if (wrote < 0) {
		perror("write");
		return;
	}
	if (wrote != size)
		fprintf(stderr, "short write: %d < %d\n", (int) wrote, size);
}


static void send_frame(void *buf, int size)
{
	debug_dirt("->", buf, size);
	write_buf(net, buf, size);
	stats.tx_frm++;
}


static void send_more(void)
{
	uint8_t *p = tx_pos-1;

	if (tx_pos == tx_packet+1) {
		*p = pt_first;
		stats.tx_frst++;
	} else {
		*p = pt_next;
		stats.tx_nxt++;
	}
	*p |= tx_seq ? SEQ : 0;
	send_frame(p, send_size()+1);
	start_timer(&t_ack, T_ACK_MS);
}


static void send_ack(int seq)
{
	uint8_t ack = pt_ack | (seq ? SEQ : 0);

	send_frame(&ack, 1);
	stats.tx_ack++;
}


/* ----- Main events ------------------------------------------------------- */


static void rx_pck(void *buf, int size)
{
	const uint8_t *p = buf;
	uint8_t ctrl, type, seq;

	debug_dirt("-<", buf, size);

	stats.rx_frm++;
	if (size < 1) {
		stats.invalid++;
		return;
	}

	ctrl = *p;
	type = ctrl & PT_MASK;
	seq = !!(ctrl & SEQ);

	switch (type) {
	case pt_first:
		stats.rx_frst++;
		send_ack(seq);
		if (rxing) {
			stop_timer(&t_reass);
			rxing = 0;
		}
		break;
	case pt_next:
		stats.rx_nxt++;
		send_ack(seq);
		if (!rxing) {
			stats.not_rx++;
			return;
		}
		if (seq == rx_seq) {
			stats.rx_seq++;
			return; /* retransmission */
		}
		break;
	case pt_ack:
		stats.rx_ack++;
		if (!txing) {
			stats.not_tx++;
			return;
		}
		if (seq != tx_seq) {
			stats.ack_seq++;
			return;
		}
		stop_timer(&t_ack);
		tx_pos += send_size();
		tx_left -= send_size();
		if (!tx_left) {
			stats.lost--;
			txing = 0;
			return;
		}
		tx_seq = !tx_seq;
		retries = 0;
		send_more();
		return;
	default:
		stats.invalid++;
		return;
	}

	if (!rxing) {
		if (size < 5) {
			stats.garbled++;
			return;
		}
		rx_left = p[3] << 8 | p[4];
		if (rx_left > MAX_PACKET) {
			stats.garbled++;
			return;
		}
		start_timer(&t_reass, T_REASS_MS);
		rxing = 1;
		rx_pos = rx_packet;
	}

	if (rx_left < size-1) {
		stats.garbled++;
		stop_timer(&t_reass);
		rxing = 0;
		return;
	}
	memcpy(rx_pos, buf+1, size-1);
	rx_pos += size-1;
	rx_left -= size-1;
	rx_seq = seq;

	if (!rx_left) {
		debug_ip("<-", rx_packet, rx_pos-(void *) rx_packet);
		write_buf(tun, rx_packet, rx_pos-(void *) rx_packet);
		stop_timer(&t_reass);
		rxing = 0;
		stats.rx_pck++;
	}
}


static void tx_pck(void *buf, int size)
{
	const uint8_t *p = buf;

	debug_ip(">-", buf, size);
	assert(!txing);
	txing = 1;
	tx_pos = tx_packet+1;
	tx_left = p[2] << 8 | p[3];
	assert(tx_left <= MAX_PACKET);
	assert(tx_left == size);
	/*
	 * @@@ We could avoid the memcpy by reading directly into "tx_packet"
	 */
	memcpy(tx_pos, buf, size);
	tx_seq = !tx_seq;
	retries = 0;
	send_more();
	stats.tx_pck++;
	stats.lost++;
}


static void ack_timeout(void)
{
	debug_timeout("ACK-TO");
	assert(txing);
	stop_timer(&t_ack);
	if (++retries == MAX_TRIES) {
		txing = 0;
		stats.no_ack++;
	} else {
		send_more();
		stats.retry++;
	}
}


static void reass_timeout(void)
{
	debug_timeout("REASS-TO");
	assert(rxing);
	stop_timer(&t_reass);
	stats.reass++;
	rxing = 0;
}


/* ----- Event dispatcher -------------------------------------------------- */


static void event(void)
{
	uint8_t buf[MAX_PACKET];
	const struct timeval *to;
	fd_set rset;
	int res;
	ssize_t got;

	FD_ZERO(&rset);
	FD_SET(net, &rset);

	/* only accept more work if we're idle */
	if (!txing && !rxing)
		FD_SET(tun, &rset);

	to = next_timer(2, &t_reass, &t_ack);

	res = select(net > tun ? net+1 : tun+1, &rset, NULL, NULL,
	    timer_delta(to));
	if (res < 0) {
		if (errno != EINTR)
			perror("select");
		return;
	}
	if (!res) {
		assert(to);
		if (to == &t_reass)
			reass_timeout();
		else
			ack_timeout();
	}
	if (FD_ISSET(tun, &rset)) {
		got = read(tun, buf, sizeof(buf));
		if (got < 0) {
			perror("read tun");
			return;
		}
		tx_pck(buf, got);
	}
	if (FD_ISSET(net, &rset)) {
		got = read(net, buf, sizeof(buf));
		if (got < 0) {
			perror("read net");
			return;
		}
		rx_pck(buf, got);
	}
}


/* ----- Setup ------------------------------------------------------------- */


static int open_net(uint16_t pan, uint16_t me, uint16_t peer)
{
	struct sockaddr_ieee802154 addr;
	int zero = 0;
	int s;

	s = socket(PF_IEEE802154, SOCK_DGRAM, 0);
	if (s < 0) {
		perror("socket 802.15.4");
		exit(1);
	}

	addr.family = AF_IEEE802154;
	addr.addr.addr_type = IEEE802154_ADDR_SHORT;
	addr.addr.pan_id = pan;
	addr.addr.short_addr = me;

	if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind 802.15.4");
		exit(1);
	}

	addr.addr.short_addr = peer;
	if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("connect 802.15.4");
		exit(1);
	}

	if (setsockopt(s, SOL_IEEE802154, WPAN_WANTACK, &zero, sizeof(zero))
	    < 0) {
		perror("setsockopt SOL_IEEE802154 WPAN_WANTACK");
		exit(1);
	}

	return s;
}


static int open_tun(const char *cmd)
{
	struct ifreq ifr;
	int fd, res;

	fd = open(TUN_DEV, O_RDWR);
	if (fd < 0) {
		perror(TUN_DEV);
		exit(1);
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

	if (ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) {
		perror("ioctl TUNSETIFF");
		exit(1);
	}

	if (!cmd) {
		fprintf(stderr, "%s\n", ifr.ifr_name);
		return fd;
	}

	if (setenv("ITF", ifr.ifr_name, 1) < 0) {
		perror("setenv");
		exit(1);
	}

	res = system(cmd);
	if (res < 0) {
		perror("system");
		exit(1);
	}
	if (WIFEXITED(res)) {
		if (!WEXITSTATUS(res))
			return fd;
		exit(WEXITSTATUS(res));
	}
	if (WIFSIGNALED(res)) {
		raise(WTERMSIG(res));
		exit(1);
	}

	fprintf(stderr, "cryptic exit status %d\n", res);
	exit(1);
}


/* ----- Command-line processing ------------------------------------------- */


static void usage(const char *name)
{
	fprintf(stderr,
"usage: %s [-b] [-d [-d]] pan_id src_addr dst_addr [command]\n\n"
"  pan_id    PAN (network) identifier\n"
"  src_addr  source short address\n"
"  dst_addr  destination short address\n"
"  command   configuration command to run after creating the TUN interface.\n"
"            The environment variable $ITF is set to the interface name.\n\n"
"  -b        background the process after initialization\n"
"  -d ...    increase verbosity of debug output\n"
    , name);
	exit(1);
}


static uint16_t addr(const char *name, const char *s)
{
	char *end;
	unsigned long n;

	n = strtoul(s, &end, 16);
	if (*end)
		usage(name);
	if (n > 0xffff)
		usage(name);
	return n;
}


int main(int argc, char **argv)
{
	const char *cmd = NULL;
	uint16_t pan, src, dst;
	int foreground = 1;
	int c;

	while ((c = getopt(argc, argv, "bd")) != EOF)
		switch (c) {
		case 'b':
			foreground = 0;
			break;
		case 'd':
			debug++;
			break;
		default:
			usage(*argv);
		}

	switch (argc-optind) {
	case 4:
		cmd = argv[optind+3];
		/* fall through */
	case 3:
		pan = addr(*argv, argv[optind]);
		src = addr(*argv, argv[optind+1]);
		dst = addr(*argv, argv[optind+2]);
		break;
	default:
		usage(*argv);
	}

	net = open_net(pan, src, dst);
	tun = open_tun(cmd);


	if (foreground) {
		signal(SIGUSR1, handle_usr1);
		signal(SIGUSR2, handle_usr2);
	} else {
		if (daemonize())
			return 0;
	}

	while (1)
		event();
}
