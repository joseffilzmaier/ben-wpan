#
# atrf-txrx/Makefile - Build the TX/RX test utility
#
# Written 2010, 2011 by Werner Almesberger
# Copyright 2010, 2011 Werner Almesberger
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#


MAIN = atrf-txrx

include ../Makefile.common

LDLIBS += -lm
CFLAGS += -g

all:		perdump

perdump:	perdump.o per-text.o

clean::
		rm -f perdump.o per-text.o
