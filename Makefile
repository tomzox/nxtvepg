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
#  $Id: Makefile,v 1.17 2000/12/26 20:06:50 tom Exp tom $
#

ifeq ($(OS),Windows_NT)
# for Windows a separate makefile is used
include Makefile.win32
else

IROOT   = /usr/local
BINDIR  = $(IROOT)/bin
MANDIR   =$(IROOT)/man/man1

LDLIBS  = -ltk8.3 -ltcl8.3 -L/usr/X11R6/lib -lX11 -lm -ldl
# use static libraries
#LDLIBS  = dbglib/libtk8.3.a dbglib/libtcl8.3.a -lX11 -lm -ldl -L/usr/X11R6/lib

INCS   += -I. -I/usr/X11R6/include
# path to Tcl/Tk headers, if not properly installed
#INCS   += -I/usr/local/tcl/tcl8.0/generic -I/usr/local/tcl/tk8.0/generic

# path to Tcl/Tk script library (Tk is usually in X11/lib/tk#.#)
DEFS   += -DTK_LIBRARY_PATH=\"/usr/lib/tk8.3\"
DEFS   += -DTCL_LIBRARY_PATH=\"/usr/lib/tcl8.3\"


#WARN    = -Wall -Wpointer-arith -Wnested-externs \
#          -Werror -Wstrict-prototypes -Wmissing-prototypes
WARN    = 
CC      = gcc
CFLAGS  = -pipe $(WARN) $(INCS) $(DEFS) -O

# ----- don't change anything below ------------------------------------------

MODS    = epgctl/epgmain epgctl/debug epgctl/epgacqctl epgctl/epgctxctl \
          epgvbi/vbidecode epgvbi/tvchan epgvbi/btdrv4linux epgvbi/hamming \
          epgdb/epgdbacq epgdb/epgstream epgdb/epgtxtdump epgdb/epgdbsav \
          epgdb/epgdbmgmt epgdb/epgdbif epgdb/epgdbfil epgdb/epgblock \
          epgdb/epgdbmerge \
          epgui/pilistbox epgui/epgui epgui/help epgui/pifilter \
          epgui/statswin epgui/pdc_themes epgui/menucmd

SRCS    = $(addsuffix .c, $(MODS))
OBJS    = $(addsuffix .o, $(MODS))

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
	$(CC) -O -o tcl2c tcl2c.c

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
	-rm -f Makefile.dep
	makedepend $(INCS) -f Makefile.dep $(SRCS)

include Makefile.dep

# end UNIX specific part
endif

