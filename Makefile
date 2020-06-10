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
#  Author: Tom Zoerner
#
#  $Id: Makefile,v 1.40 2002/03/02 12:52:01 tom Exp tom $
#

ifeq ($(OS),Windows_NT)
# for Windows a separate makefile is used
include Makefile.win32
else

ROOT    =
prefix  = /usr/local
exec_prefix = ${prefix}
bindir  = $(ROOT)${exec_prefix}/bin
mandir  = $(ROOT)${prefix}/man/man1

# if you have perl set the path here, else just leave it alone
PERL    = /usr/bin/perl

LDLIBS  = -ltk8.5 -ltcl8.5 -L/usr/X11R6/lib -lX11 -lXmu -lm -ldl
# use static libraries for debugging only
#LDLIBS  = dbglib/libtk8.3.a dbglib/libtcl8.3.a -lX11 -lm -ldl -L/usr/X11R6/lib

INCS   += -I. -I/usr/X11R6/include
INCS   += -I. -I/usr/include/tcl8.5
# path to Tcl/Tk headers, if not properly installed
#INCS   += -I/usr/local/tcl/tcl8.0/generic -I/usr/local/tcl/tk8.0/generic

# path to Tcl/Tk script library (Tk is usually in X11/lib/tk#.#)
DEFS   += -DTK_LIBRARY_PATH=\"/usr/share/tcltk/tk8.5\"
DEFS   += -DTCL_LIBRARY_PATH=\"/usr/share/tcltk/tcl8.5\"

# enable use of multi-threading
DEFS   += -DUSE_THREADS
LDLIBS += -lpthread

# enable use of daemon and client/server connection
DEFS   += -DUSE_DAEMON

# path to the directory where the provider database files are kept
DB_DIR  = /tmp/nxtvdb
DEFS   += -DEPG_DB_DIR=\"$(DB_DIR)\"
INST_DB_DIR = $(ROOT)$(DB_DIR)

WARN    = -Wall -Wnested-externs -Wstrict-prototypes -Wmissing-prototypes
#WARN  += -Wpointer-arith -Werror
CC      = gcc
CFLAGS  = -pipe $(WARN) $(INCS) $(DEFS) -O6

# ----- don't change anything below ------------------------------------------

CSRC    = epgvbi/vbidecode epgvbi/tvchan epgvbi/btdrv4dummy epgvbi/hamming \
          epgvbi/cni_tables \
          epgctl/debug epgctl/epgacqctl epgctl/epgscan epgctl/epgctxctl \
          epgctl/epgctxmerge epgctl/epgacqclnt epgctl/epgacqsrv \
          epgdb/epgdbacq epgdb/epgstream epgdb/epgdbmerge epgdb/epgdbsav \
          epgdb/epgdbmgmt epgdb/epgdbif epgdb/epgdbfil epgdb/epgblock \
          epgdb/epgnetio epgdb/epgqueue epgdb/epgtscqueue \
          epgui/uictrl epgui/pilistbox epgui/pioutput epgui/pifilter \
          epgui/statswin epgui/timescale epgui/pdc_themes epgui/menucmd \
          epgui/epgmain epgui/xawtv epgui/epgtxtdump
CGEN    = epgui/epgui epgui/help

SRCS    = $(addsuffix .c, $(CSRC)) $(addsuffix .c, $(CGEN))
OBJS    = $(addsuffix .o, $(CSRC)) $(addsuffix .o, $(CGEN))

all: nxtvepg nxtvepg.1

nxtvepg: $(OBJS)
	$(CC) $(CFLAGS) $(INCS) $(LDFLAGS) -o nxtvepg $(OBJS) $(LDLIBS)

install: nxtvepg nxtvepg.1
	test -d $(bindir) || mkdirhier $(bindir)
	test -d $(mandir) || mkdirhier $(mandir)
	test -d $(INST_DB_DIR) || mkdirhier $(INST_DB_DIR)
	chmod 0777 $(INST_DB_DIR)
	install -c -m 0755 nxtvepg     $(bindir)
	install -c -m 0644 nxtvepg.1   $(mandir)
	rm -f $(mandir)/nxtvepg.1x

##%.o: %.c
##	$(CC) $(CFLAGS) -c *.c -o *.o

tcl2c: tcl2c.c
	$(CC) -O -o tcl2c tcl2c.c

epgui/epgui.c: epgui/epgui.tcl tcl2c
	egrep -v '^ *#' epgui/epgui.tcl | ./tcl2c epgui_tcl_script > epgui/epgui.c

epgui/help.c: epgui/help.tcl tcl2c
	egrep -v '^ *#' epgui/help.tcl | ./tcl2c help_tcl_script > epgui/help.c

nxtvepg.1 man.html epgui/help.tcl: nxtvepg.pod pod2help.pl
	@if test -x $(PERL); then \
	  EPG_VERSION_STR=`egrep '[ \t]*#[ \t]*define[ \t]*EPG_VERSION_STR' epgctl/epgversion.h | head -1 | cut -d\" -f2`; \
	  echo "./pod2help.pl nxtvepg.pod > epgui/help.tcl"; \
	  ./pod2help.pl nxtvepg.pod > epgui/help.tcl; \
	  echo "pod2man nxtvepg.pod > nxtvepg.1"; \
	  pod2man -date " " -center "Nextview EPG Decoder" -section "1" \
	          -release "nxtvepg "$$EPG_VERSION_STR" (C) 1999-2001 Tom Zoerner" \
	     nxtvepg.pod > nxtvepg.1; \
	  echo "pod2html nxtvepg.pod > man.html"; \
	  pod2html nxtvepg.pod > man.html; \
	  rm -f pod2html-{dircache,itemcache}; \
	elif test -f epgui/help.tcl; then \
	  touch epgui/help.tcl; \
	else \
	  echo "ERROR: cannot generate epgui/help.tcl or nxtvepg.1 without Perl"; \
	  false; \
	fi

clean:
	-rm -f *.o epg*/*.o core a.out tcl2c nxtvepg
	-rm -f epgui/epgui.c epgui/help.c

depend:
	-:>Makefile.dep
	DIRLIST=`(for cmod in $(CSRC); do echo $$cmod; done) | cut -d/ -f1 | sort -u`; \
	for dir in $$DIRLIST; do \
	  CLIST=`(for src in $(addsuffix .c, $(CSRC)); do echo $$src; done) | grep $$dir/`; \
	  gcc -MM $(INCS) $(DEFS) $$CLIST | \
	  sed -e 's#^.*\.o#'$$dir/'&#g' >> Makefile.dep; \
	done

bak:
	cd .. && tar cf pc.tar pc -X pc/tar-ex && bzip2 -f -9 pc.tar
	cd .. && tar cf /e/pc.tar pc -X pc/tar-ex-win

include Makefile.dep

# end UNIX specific part
endif

