#
#  UNIX Makefile for Nextview EPG decoder
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License Version 2 as
#  published by the Free Software Foundation. You find a copy of this
#  license in the file COPYRIGHT in the root directory of this release.
#
#  THIS PROGRAM IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL,
#  BUT WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED WARRANTY OF
#  MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#
#  Description:
#
#    Controls compilation of C and Tcl/Tk source to an executable on
#    UNIX systems. Tcl/Tk source is converted to a C string by tcl2c.
#
#  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
#
#  $Id: Makefile,v 1.13 2000/10/29 00:07:57 tom Exp tom $
#

IROOT   = /usr/local
BINDIR  = $(IROOT)/bin
MANDIR   =$(IROOT)/man/man1

LDLIBS  = -ltk8.3 -ltcl8.3 -L/usr/X11R6/lib -lX11 -lm -ldl
# use old TCL library version
#LDLIBS  = -ltk4.2i -ltcl7.6i -L/usr/X11R6/lib -lX11 -lm -ldl
#LDLIBS  = -ltk8.0 -ltcl8.0 -L/usr/X11R6/lib -lX11 -lm -ldl
# use static libraries
#LDLIBS  = libtk8.0.a libtcl8.0.a -lX11 -lm -ldl -L/usr/X11R6/lib

INCS   += -I. -I/usr/X11R6/include
# path to Tcl/Tk headers, if not properly installed
INCS   += -I/tom/tcl/tcl8.0/generic -I/tom/tcl/tk8.0/generic

# path to Tcl/Tk script library (Tk is usually in X11/lib/tk#.#)
DEFS   += -DTK_LIBRARY_PATH=\"/usr/lib/tk8.3\"
DEFS   += -DTCL_LIBRARY_PATH=\"/usr/lib/tcl8.3\"


WARN    = -Wall -Wpointer-arith -Wnested-externs \
          -Werror -Wstrict-prototypes -Wmissing-prototypes
CC      = gcc
CFLAGS  = -pipe $(WARN) $(INCS) $(DEFS) -O6

# ----- don't change anything below ------------------------------------------

OBJS    = epgctl/epgmain.o epgctl/debug.o epgctl/vbidecode.o epgctl/epgacqctl.o \
          epgdb/epgdbacq.o epgdb/hamming.o epgdb/epgstream.o epgdb/epgtxtdump.o \
          epgdb/epgdbmgmt.o epgdb/epgdbif.o epgdb/epgdbsav.o epgdb/epgdbfil.o \
          epgdb/epgblock.o \
          epgui/pilistbox.o epgui/epgui.o epgui/help.o epgui/pifilter.o \
          epgui/statswin.o epgui/pdc_themes.o epgui/menucmd.o

all: nxtvepg

nxtvepg: $(OBJS)
	$(CC) $(CFLAGS) $(INCS) $(LDFLAGS) -o nxtvepg $(OBJS) $(LDLIBS)

install: all
	test -d $(BINDIR) || mkdirhier $(BINDIR)
	test -d $(MANDIR) || mkdirhier $(MANDIR)
	install -c -m 0755 nxtvepg     $(BINDIR)
	install -c -m 0644 nxtvepg.1x  $(MANDIR)

##%.o: %.c
##	$(CC) $(CFLAGS) -c *.c -o *.o

tcl2c: tcl2c.c
	$(CC) -o tcl2c tcl2c.c

epgui/epgui.c: epgui/epgui.tcl tcl2c
	egrep -v '^ *#' epgui/epgui.tcl | ./tcl2c epgui_tcl_script > epgui/epgui.c

epgui/help.c: epgui/help.tcl tcl2c
	egrep -v '^ *#' epgui/help.tcl | ./tcl2c help_tcl_script > epgui/help.c

epgui/help.tcl: nxtvepg.1x man2help.tcl
	./man2help.tcl nxtvepg.1x > epgui/help.tcl

clean:
	-rm -f *.o epg*/*.o core a.out tcl2c nxtvepg
	-rm -f epgui/epgui.c epgui/help.c epgui/help.tcl

depend:
	makedepend $(INCS) -f Makefile epg*/*.[ch]


# DO NOT DELETE THIS LINE -- make depend depends on it.

