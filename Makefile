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
#  $Id: Makefile,v 1.49 2002/11/10 19:57:05 tom Exp tom $
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
resdir  = $(ROOT)/usr/X11R6/lib/X11

# if you have perl set the path here, else just leave it alone
PERL    = /usr/bin/perl

# select Tcl/Tk version
TCL_VER = 8.3

LDLIBS  = -ltk$(TCL_VER) -ltcl$(TCL_VER) -L/usr/X11R6/lib -lX11 -lXmu -lm -ldl

# use static libraries for debugging only
#LDLIBS += -Ldbglib -static

INCS   += -I. -I/usr/X11R6/include
# path to Tcl/Tk headers, if not properly installed
#INCS   += -I/usr/local/tcl/tcl8.0/generic -I/usr/local/tcl/tk8.0/generic

# path to Tcl/Tk script library (Tk is usually in X11/lib/tk#.#)
DEFS   += -DTK_LIBRARY_PATH=\"/usr/lib/tk$(TCL_VER)\"
DEFS   += -DTCL_LIBRARY_PATH=\"/usr/lib/tcl$(TCL_VER)\"

# enable use of multi-threading
DEFS   += -DUSE_THREADS
LDLIBS += -lpthread

# enable use of daemon and client/server connection
DEFS   += -DUSE_DAEMON

# The database directory can be either in the user's $HOME (or relative to any
# other env variable) or at a global place like /var/spool (world-writable)
# -> uncomment 2 lines below to put the databases in the user's home
#USER_DBDIR  = .nxtvdb
#DEFS       += -DEPG_DB_ENV=\"HOME\" -DEPG_DB_DIR=\"$(USER_DBDIR)\"
ifndef USER_DBDIR
SYS_DBDIR    = /usr/tmp/nxtvdb
DEFS        += -DEPG_DB_DIR=\"$(SYS_DBDIR)\"
INST_DB_DIR  = $(ROOT)$(SYS_DBDIR)
INST_DB_PERM = 0777
endif

WARN    = -Wall -Wnested-externs -Wstrict-prototypes -Wmissing-prototypes
#WARN  += -Wpointer-arith -Werror
CC      = gcc
CFLAGS  = -pipe $(WARN) $(INCS) $(DEFS) -O6
#LDLIBS += -pg

# ----- don't change anything below ------------------------------------------

CSRC    = epgvbi/btdrv4linux epgvbi/vbidecode epgvbi/ttxdecode epgvbi/hamming \
          epgvbi/tvchan epgvbi/cni_tables epgvbi/syserrmsg \
          epgctl/debug epgctl/epgacqctl epgctl/epgscan epgctl/epgctxctl \
          epgctl/epgctxmerge epgctl/epgacqclnt epgctl/epgacqsrv \
          epgdb/epgstream epgdb/epgdbmerge epgdb/epgdbsav \
          epgdb/epgdbmgmt epgdb/epgdbif epgdb/epgdbfil epgdb/epgblock \
          epgdb/epgnetio epgdb/epgqueue epgdb/epgtscqueue \
          epgui/uictrl epgui/pilistbox epgui/pioutput epgui/pifilter \
          epgui/statswin epgui/timescale epgui/pdc_themes epgui/menucmd \
          epgui/epgmain epgui/loadtcl epgui/xawtv epgui/epgtxtdump epgui/epgtabdump
TCLSRC  = epgtcl/mainwin epgtcl/helptexts epgtcl/dlg_hwcfg epgtcl/dlg_xawtvcf \
          epgtcl/dlg_ctxmencf epgtcl/dlg_acqmode epgtcl/dlg_netsel \
          epgtcl/dlg_dump epgtcl/dlg_netname epgtcl/dlg_udefcols \
          epgtcl/shortcuts epgtcl/dlg_shortcuts epgtcl/draw_stats \
          epgtcl/dlg_filter epgtcl/dlg_prov epgtcl/rcfile

OBJS    = $(addsuffix .o, $(CSRC)) $(addsuffix .o, $(TCLSRC))

all: nxtvepg nxtvepg.1
.PHONY: all

nxtvepg: $(OBJS)
	$(CC) $(LDFLAGS) -o nxtvepg $(OBJS) $(LDLIBS)

install: nxtvepg nxtvepg.1
	test -d $(bindir) || mkdirhier $(bindir)
	test -d $(mandir) || mkdirhier $(mandir)
ifndef USER_DBDIR
	test -d $(INST_DB_DIR) || mkdirhier $(INST_DB_DIR)
	chmod $(INST_DB_PERM) $(INST_DB_DIR)
endif
	install -c -m 0755 nxtvepg     $(bindir)
	install -c -m 0644 nxtvepg.1   $(mandir)
	install -c -m 0644 Nxtvepg.ad  $(resdir)/app-defaults/Nxtvepg
	rm -f $(mandir)/nxtvepg.1x

.SUFFIXES: .c .o .tcl

%.c: %.tcl tcl2c
	./tcl2c $*.tcl

tcl2c: tcl2c.c
	$(CC) -O -o tcl2c tcl2c.c

epgui/loadtcl.c :: $(addsuffix .c, $(TCLSRC))

epgui/loadtcl.c :: $(addsuffix .h, $(TCLSRC))

nxtvepg.1 manual.html epgtcl/helptexts.tcl: nxtvepg.pod pod2help.pl
	@if test -x $(PERL); then \
	  EPG_VERSION_STR=`egrep '[ \t]*#[ \t]*define[ \t]*EPG_VERSION_STR' epgctl/epgversion.h | head -1 | cut -d\" -f2`; \
	  echo "$(PERL) pod2help.pl nxtvepg.pod > epgtcl/helptexts.tcl"; \
	  $(PERL) pod2help.pl nxtvepg.pod > epgtcl/helptexts.tcl; \
	  echo "pod2man nxtvepg.pod > nxtvepg.1"; \
	  pod2man -date " " -center "Nextview EPG Decoder" -section "1" \
	          -release "nxtvepg "$$EPG_VERSION_STR" (C) 1999-2002 Tom Zoerner" \
	     nxtvepg.pod > nxtvepg.1; \
	  echo "pod2html nxtvepg.pod > manual.html"; \
	  pod2html nxtvepg.pod | $(PERL) -p -e 's/HREF="#[^:]+: +/HREF="#/g;' > manual.html; \
	  rm -f pod2htm?.x~~ pod2html-{dircache,itemcache}; \
	elif test -f epgtcl/helptexts.tcl; then \
	  touch epgtcl/helptexts.tcl; \
	else \
	  echo "ERROR: cannot generate epgtcl/helptexts.tcl or nxtvepg.1 without Perl"; \
	  false; \
	fi

.PHONY: clean depend bak
clean:
	-rm -f *.o epg*/*.o core a.out tcl2c nxtvepg
	-rm -f epgtcl/*.[ch]

depend:
	-:>Makefile.dep
	DIRLIST=`(for cmod in $(CSRC); do echo $$cmod; done) | cut -d/ -f1 | sort -u`; \
	for dir in $$DIRLIST; do \
	  CLIST=`(for src in $(addsuffix .c, $(CSRC)); do echo $$src; done) | grep $$dir/`; \
	  gcc -MM $(INCS) $(DEFS) $$CLIST | \
	  sed -e 's#^.*\.o#'$$dir/'&#g' >> Makefile.dep; \
	done

bak:
	cd .. && tar cf /c/pc.tar pc -X pc/tar-ex-win
	cd .. && tar cf pc1.tar pc -X pc/tar-ex && bzip2 -f -9 pc1.tar
	tar cf ../pc2.tar www ATTIC dsdrv?* tk8* tcl8* && bzip2 -f -9 ../pc2.tar

include Makefile.dep

# end UNIX specific part
endif

