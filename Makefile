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
#    UNIX systems. For Windows a separate Makefile is launched (this
#    requires the CygWin GNU port).  Tcl/Tk source is converted to a
#    C string by tcl2c.  Help and manpage are created by Perl utilities;
#    the releases comes with pre-generated manpage and help, so that
#    Perl is not required to install the package.  Dependencies are
#    generated into a separate file.
#
#  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
#
#  $Id: Makefile,v 1.19 2001/01/09 20:48:44 tom Exp tom $
#

ifeq ($(OS),Windows_NT)
# for Windows a separate makefile is used
include Makefile.win32
else

IROOT   = /usr/local
BINDIR  = $(IROOT)/bin
MANDIR  = $(IROOT)/man/man1

# if you have perl set the path here, else just leave it alone
PERL    = /usr/bin/perl

LDLIBS  = -ltk8.3 -ltcl8.3 -L/usr/X11R6/lib -lX11 -lm -ldl
# use static libraries for debugging only
#LDLIBS  = dbglib/libtk8.3.a dbglib/libtcl8.3.a -lX11 -lm -ldl -L/usr/X11R6/lib

INCS   += -I. -I/usr/X11R6/include
# path to Tcl/Tk headers, if not properly installed
#INCS   += -I/usr/local/tcl/tcl8.0/generic -I/usr/local/tcl/tk8.0/generic

# path to Tcl/Tk script library (Tk is usually in X11/lib/tk#.#)
DEFS   += -DTK_LIBRARY_PATH=\"/usr/lib/tk8.3\"
DEFS   += -DTCL_LIBRARY_PATH=\"/usr/lib/tcl8.3\"


#WARN    = -Wall -Wpointer-arith -Wnested-externs \
#          -Werror -Wstrict-prototypes -Wmissing-prototypes
WARN    = -Wall
CC      = gcc
CFLAGS  = -pipe $(WARN) $(INCS) $(DEFS) -g -O2

# ----- don't change anything below ------------------------------------------

MODS    = epgctl/epgmain epgctl/debug epgctl/epgacqctl epgctl/epgctxctl \
          epgvbi/vbidecode epgvbi/tvchan epgvbi/btdrv4linux epgvbi/hamming \
          epgdb/epgdbacq epgdb/epgstream epgdb/epgtxtdump epgdb/epgdbsav \
          epgdb/epgdbmgmt epgdb/epgdbif epgdb/epgdbfil epgdb/epgblock \
          epgdb/epgdbmerge \
          epgui/uictrl epgui/pilistbox epgui/pifilter epgui/epgui epgui/help \
          epgui/statswin epgui/pdc_themes epgui/menucmd

SRCS    = $(addsuffix .c, $(MODS))
OBJS    = $(addsuffix .o, $(MODS))

all: nxtvepg

nxtvepg: $(OBJS)
	$(CC) $(CFLAGS) $(INCS) $(LDFLAGS) -o nxtvepg $(OBJS) $(LDLIBS)

install: nxtvepg nxtvepg.1x
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

nxtvepg.1x man.html epgui/help.tcl: nxtvepg.pod pod2help.pl
	@if test -x $(PERL); then \
	  EPG_VERSION_STR=`egrep '[ \t]*#[ \t]*define[ \t]*EPG_VERSION_STR' epgctl/epgversion.h | cut -d\" -f2`; \
	  echo "./pod2help.pl nxtvepg.pod > epgui/help.tcl"; \
	  ./pod2help.pl nxtvepg.pod > epgui/help.tcl; \
	  echo "pod2man nxtvepg.pod > nxtvepg.1x"; \
	  pod2man -date " " -center "Nextview EPG Decoder" -section "1x" \
	          -release "nxtvepg "$$EPG_VERSION_STR" (C) 1999-2001 Tom Zoerner" \
	     nxtvepg.pod > nxtvepg.1x; \
	  echo "pod2html nxtvepg.pod > man.html"; \
	  pod2html nxtvepg.pod > man.html; \
	  rm -f pod2html-{dircache,itemcache}; \
	elif test -f epgui/help.tcl; then \
	  touch epgui/help.tcl; \
	else \
	  echo "ERROR: cannot generate epgui/help.tcl or nxtvepg.1x without Perl"; \
	  false; \
	fi

clean:
	-rm -f *.o epg*/*.o core a.out tcl2c nxtvepg
	-rm -f epgui/epgui.c epgui/help.c

depend:
	-rm -f Makefile.dep
	makedepend $(INCS) -f Makefile.dep $(SRCS)

include Makefile.dep

# end UNIX specific part
endif

