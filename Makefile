#
#  UNIX Makefile for nxtvepg - A XMLTV browser and Teletext EPG grabber
#
#  Copyright (C) 1999-2011, 2020-2023 T. Zoerner
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
#    Controls compilation of C and Tcl/Tk source into executables on
#    UNIX systems. For Windows a separate Makefile is launched (this
#    requires the Cygwin GNU port).  Tcl/Tk source is converted to a
#    C string by tcl2c.  Help and manpage are created by Perl utilities;
#    the releases comes with pre-generated manpage and help, so that
#    Perl is not required to install the package.  Dependencies are
#    generated into a separate file.
#
#    To compile the package type "make all", to compile and install
#    type "make install" (without the quotes).  If compilation fails
#    you may have to adapt some paths below.  Note that "make nxtvepg"
#    will not work because there are other targets which need to be
#    built first (read comments below for further explanations.)
#

ifeq ($(OS),Windows_NT)
# for Windows a separate makefile is used
include Makefile.win32
else
OS = $(shell uname)
ifeq ($(OS), FreeBSD)
include Makefile.bsd
else
ifeq ($(OS), NetBSD)
include Makefile.bsd
else

prefix  = /usr/local
exec_prefix = ${prefix}
res_prefix = /etc/X11

# below shall be used for install target only (ROOT path is used as temporary
# prefix during building packages - it must not be part of compiled-in paths)
ROOT    =
bindir  = $(ROOT)${exec_prefix}/bin
mandir  = $(ROOT)${prefix}/share/man/man1
pixdir  = $(ROOT)${prefix}/share/pixmaps
resdir  = $(ROOT)${res_prefix}

# if you have perl and/or flex set their path here, else just leave them alone
PERL    = /usr/bin/perl
FLEX    = /usr/bin/flex
YACC    = /usr/bin/yacc

# select Tcl/Tk version (8.5 recommended due to modernized widget appearence)
TCL_VER := $(shell echo 'puts [info tclversion]' | tclsh)
#TCL_VER := $(shell echo 'puts [package require Tcl]' | tclsh)
#TCL_VER = 8.5

ifeq ($(shell test -d /usr/include/tcl$(TCL_VER) && echo YES),YES)
INCS   += -I/usr/include/tcl$(TCL_VER)
endif

GUILIBS  = -ltk$(TCL_VER) -ltcl$(TCL_VER) -L/usr/X11R6/lib -lX11 -lXmu -ldl

# use TCL library for UTF-8 string matching (i.e. required even without GUI)
TCL_LIB  = -ltcl$(TCL_VER)

# use static libraries for debugging only
#GUILIBS += -Ldbglib -static

INCS   += -I. -I/usr/X11R6/include
DEFS   += -DX11_APP_DEFAULTS=\"$(res_prefix)/app-defaults/Nxtvepg\"
# path to Tcl/Tk headers, if not properly installed
#INCS   += -I/usr/local/tcl/tcl8.0/generic -I/usr/local/tcl/tk8.0/generic

# path to Tcl/Tk script library (note Tk is sometimes in X11/lib/tk#.#)
ifeq ($(shell test -d /usr/share/tk$(TCL_VER) && echo YES),YES)
TK_LIBRARY_PATH  = /usr/share/tk$(TCL_VER)
TCL_LIBRARY_PATH = /usr/share/tcl$(TCL_VER)
else
TCL_LIBRARY_PATH = /usr/share/tcltk/tcl$(TCL_VER)
TK_LIBRARY_PATH = /usr/share/tcltk/tk$(TCL_VER)
endif
DEFS   += -DTK_LIBRARY_PATH=\"$(TK_LIBRARY_PATH)\"
DEFS   += -DTCL_LIBRARY_PATH=\"$(TCL_LIBRARY_PATH)\"

# pthreads are required for capturing
ACQLIBS += -lpthread

# XMLTV parser needs to emit UTF-8 to match internal encoding
DEFS   += -DXMLTV_OUTPUT_UTF8

# enable support for teletext EPG grabber
DEFS   += -DUSE_TTX_GRABBER

# enable use of daemon and client/server connection
# this option may only be enabled when teletext EPG grabber is enabled
DEFS   += -DUSE_DAEMON

# enable if you have both 32-bit and 64-bit systems
#DEFS   += -DUSE_32BIT_COMPAT

#WARN  += -Werror
WARN    = -Wall
WARN   += -Wextra -Wno-sign-compare -Wno-unused-parameter
WARN   += -Wcast-align -Wpointer-arith -Wwrite-strings
#WARN  += -Wcast-qual -Wshadow
CWARN   = -Wnested-externs -Wstrict-prototypes -Wmissing-prototypes
CC      = gcc
CPP     = g++
# the following flags can be overridden by an environment variable with the same name
CFLAGS ?= -pipe -g -O2
CFLAGS += $(WARN) $(CWARN) $(INCS) $(DEFS)
CPPFLAGS = -pipe -g -O2 $(WARN) $(INCS) $(DEFS)
LDFLAGS += -lm

#CFLAGS += -ftest-coverage -fprofile-arcs
#LDFLAGS += -ftest-coverage -fprofile-arcs
#LDFLAGS += -pg

#CFLAGS += -fsanitize=address
#LDFLAGS += -fsanitize=address
#setenv ASAN_OPTIONS suppressions=/tom/work/EPG/pc-git/.ASAN-mem.supp
#setenv LSAN_OPTIONS suppressions=/tom/work/EPG/pc-git/.ASAN-leak.supp
#unsetenv LD_PRELOAD

BUILD_DIR  = build-$(shell uname -m | sed -e 's/i.86/i386/' -e 's/ppc/powerpc/')
INCS      += -I$(BUILD_DIR)

# formerly required for O2 and Werror
#$(BUILD_DIR)/epgui/rcfile.o : CFLAGS += -fno-strict-aliasing

# end Linux specific part
endif
endif

# ----- don't change anything below ------------------------------------------

SUBDIRS = epgvbi epgdb epgctl epgui epgtcl tvsim xmltv epgttx
EPGSRC  = epgvbi/btdrv4linux epgvbi/vbidecode epgvbi/zvbidecoder \
          epgvbi/ttxdecode epgvbi/hamming epgvbi/cni_tables epgvbi/tvchan \
          epgvbi/dvb_demux epgvbi/dvb_scan_pmt epgvbi/syserrmsg \
          epgdb/epgdbmerge epgdb/epgdbmgmt epgdb/epgdbif epgdb/epgdbfil \
          epgdb/epgblock epgdb/epgnetio epgdb/epgtscqueue epgdb/ttxgrab \
          epgctl/debug epgctl/epgacqctl epgctl/epgacqttx \
          epgctl/epgscan epgctl/epgctxctl epgctl/epgctxmerge \
          epgctl/epgacqclnt epgctl/epgacqsrv
XMLSRC  = xmltv/xml_prolog.tab xmltv/xml_scan.yy xmltv/xml_cdata \
          xmltv/xml_hash xmltv/xmltv_tags xmltv/xmltv_db xmltv/xmltv_cni \
          xmltv/xmltv_timestamp xmltv/xmltv_main
TTXSRC  = epgttx/ttx_scrape epgttx/ttx_date epgttx/ttx_ov_fmt \
          epgttx/ttx_feat epgttx/ttx_pg_ref epgttx/ttx_db epgttx/ttx_util \
          epgttx/ttx_xmltv epgttx/ttx_cif
GUISRC  = epgui/pibox epgui/pilistbox epgui/pinetbox epgui/piremind \
          epgui/uictrl epgui/pioutput epgui/pidescr epgui/pifilter \
          epgui/statswin epgui/timescale epgui/menucmd \
          epgui/epgmain epgui/loadtcl epgui/xawtv epgui/wintvcfg \
          epgui/wintvui epgui/epgsetup epgui/cmdline epgui/rcfile \
          epgui/dumptext epgui/dumphtml epgui/dumpxml epgui/uidump \
          epgui/epgquery epgui/shellcmd epgui/wmhooks epgui/xiccc \
          epgui/daemon
CLDSRC  = epgui/daemon_main epgui/daemon epgui/epgsetup epgui/cmdline \
          epgui/rcfile epgui/wintvcfg epgui/dumptext epgui/dumpxml \
          epgui/pidescr epgui/epgquery
TCLSRC  = epgtcl/mainwin epgtcl/dlg_hwcfg epgtcl/dlg_xawtvcf \
          epgtcl/dlg_ctxmencf epgtcl/dlg_acqmode epgtcl/dlg_netsel \
          epgtcl/dlg_dump epgtcl/dlg_netname epgtcl/dlg_udefcols \
          epgtcl/shortcuts epgtcl/dlg_shortcuts epgtcl/draw_stats \
          epgtcl/dlg_filter epgtcl/dlg_substr epgtcl/dlg_remind \
          epgtcl/dlg_prov epgtcl/rcfile epgtcl/helptexts epgtcl/helptexts_de \
          epgtcl/mclistbox epgtcl/combobox epgtcl/rnotebook epgtcl/htree

TVSIM_CSRC    = tvsim/tvsim_main
TVSIM_CSRC2   = epgctl/debug epgvbi/tvchan epgvbi/syserrmsg \
                epgvbi/cni_tables epgui/wintvcfg epgui/wintvui epgui/xiccc \
                epgui/rcfile
TVSIM_TCLSRC  = tvsim/tvsim_gui
VBIREC_CSRC   = tvsim/vbirec_main
VBIREC_CSRC2  = epgvbi/btdrv4linux epgvbi/vbidecode epgvbi/zvbidecoder \
                epgvbi/ttxdecode epgvbi/hamming epgvbi/cni_tables \
                epgvbi/syserrmsg epgctl/debug epgui/xawtv epgui/xiccc \
                epgui/dumptext epgui/pidescr \
                epgvbi/dvb_demux epgvbi/dvb_scan_pmt
VBIREC_TCLSRC = tvsim/vbirec_gui epgtcl/combobox

NXTV_OBJS     = $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(EPGSRC) $(GUISRC) $(TCLSRC) $(XMLSRC)))
DAEMON_OBJS   = $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(EPGSRC) $(CLDSRC) $(XMLSRC)))
TVSIM_OBJS    = $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(TVSIM_CSRC) $(TVSIM_CSRC2) $(TVSIM_TCLSRC)))
VBIREC_OBJS   = $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(VBIREC_CSRC) $(VBIREC_CSRC2) $(VBIREC_TCLSRC)))

ifneq (,$(findstring USE_TTX_GRABBER,$(DEFS)))
NXTV_OBJS    += $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(TTXSRC)))
DAEMON_OBJS  += $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(TTXSRC)))
endif

.PHONY: devel daemon manuals tvsim tvmans all
devel   :: build_dir tcl_headers $(BUILD_DIR)/nxtvepg manuals
daemon  :: build_dir tcl_headers $(BUILD_DIR)/nxtvepgd manuals
manuals :: nxtvepg.1 nxtvepgd.1 manual.html manual-de.html
tvsim   :: build_dir tcl_headers_tvsim $(BUILD_DIR)/tvsimu $(BUILD_DIR)/vbirec tvmans
tvmans  :: build_dir tvsim/tvsim.html tvsim/vbirec.html tvsim/vbiplay.html
all     :: devel daemon tvsim tvmans verifyxml

$(BUILD_DIR)/nxtvepg: $(NXTV_OBJS)
	$(CPP) -o $@ $(NXTV_OBJS) $(GUILIBS) $(ACQLIBS) $(LDFLAGS)

$(BUILD_DIR)/nxtvepgd: $(DAEMON_OBJS)
	$(CPP) -o $@ $(DAEMON_OBJS) $(LDFLAGS) $(ACQLIBS) $(TCL_LIB)

$(BUILD_DIR)/tvsimu: $(TVSIM_OBJS)
	$(CPP) -o $@ $(TVSIM_OBJS) $(LDFLAGS) $(GUILIBS) $(ACQLIBS)

$(BUILD_DIR)/vbirec: $(VBIREC_OBJS)
	$(CPP) -o $@ $(VBIREC_OBJS) $(LDFLAGS) $(GUILIBS) $(ACQLIBS)

.PHONY: install
install: daemon Nxtvepg.ad nxtvepgd.1
	test -d $(bindir) || install -d $(bindir)
	test -d $(mandir) || install -d $(mandir)
	test -d $(resdir)/app-defaults || install -d $(resdir)/app-defaults
	test -d $(pixdir) || install -d $(pixdir)
	install -c -m 0755 -s $(BUILD_DIR)/nxtvepg $(bindir)
	install -c -m 0755 -s $(BUILD_DIR)/nxtvepgd $(bindir)
	install -c -m 0644 nxtvepg.1   $(mandir)
	install -c -m 0644 nxtvepgd.1  $(mandir)
	install -c -m 0644 Nxtvepg.ad  $(resdir)/app-defaults/Nxtvepg
	install -c -m 0644 images/nxtvepg.xpm $(pixdir)/nxtvepg.xpm

.PHONY: deb
deb: all
	rm -rf deb
	perl mkdebian.pl

.SUFFIXES: .cc .c .o .tcl

$(BUILD_DIR)/%.o: %.cc
	$(CPP) $(CPPFLAGS) -Wp,-MMD,$@.dep.tmp -c -o $@ $<
	@sed -e "s#^[^ \t].*\.o:#$@:#" < $@.dep.tmp > $@.dep && \
	   rm -f $@.dep.tmp

$(BUILD_DIR)/%.o: %.c | tcl_headers tcl_headers_tvsim
	$(CC) $(CFLAGS) -Wp,-MMD,$@.dep.tmp -c -o $@ $<
	@sed -e "s#^[^ \t].*\.o:#$@:#" < $@.dep.tmp > $@.dep && \
	   rm -f $@.dep.tmp

$(BUILD_DIR)/%.c: %.tcl $(BUILD_DIR)/tcl2c
	$(BUILD_DIR)/tcl2c -d -c -h -p $(BUILD_DIR) $*.tcl

# %.h rule disabled because tcl2c doesn't update timestamp of generated headers
# unless they changed, to avoid excessive re-compilation after internal script
# changes. note: this also requires to add a rule for all .tcl sources in front
# of objects and depend targets to make sure all headers are already generated
# when required, since there's no rule to generate them from Tcl scripts.
#
%.h: %.tcl $(BUILD_DIR)/tcl2c
	@if [ ! -e epgtcl/dlg_remind.h ] ; then \
	  echo "Appearently you typed 'make nxtvepg'" ; \
	  echo "Don't do that. Instead use simply 'make'" ; \
          false ; \
	fi

.SUFFIXES: .yy .lex

%.tab.c %.tab.h: %.yy
	@if test -x $(YACC); then \
	  echo "$(YACC) -d -b $(basename $<) $<" ; \
	  $(YACC) -d -b $(basename $<) $< ; \
	elif test -f $<; then \
	  touch $< ; \
	else \
	  echo "ERROR: cannot generate $< without $(YACC)" ; \
	  false; \
	fi

%.yy.c: %.lex
	@if test -x $(FLEX); then \
	  echo "$(FLEX) -o$@ $<" ; \
	  $(FLEX) -o$@ $< ; \
	elif test -f $<; then \
	  touch $< ; \
	else \
	  echo "ERROR: cannot generate $< without $(FLEX)" ; \
	  false; \
	fi

.PHONY: verifyxml parsexmltv
# standalone XML parser for validation purposes
XMLVERISRC = xmltv/xml_prolog.tab xmltv/xml_scan.yy xmltv/xml_verify \
             xmltv/xml_cdata xmltv/xml_hash epgctl/debug
XMLVERIOBJ  = $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(XMLVERISRC)))
$(BUILD_DIR)/verifyxml : $(XMLVERIOBJ)
	$(CC) -o $@ $(XMLVERIOBJ) $(LDFLAGS)
verifyxml : $(BUILD_DIR)/verifyxml

# standalone XMLTV processor for performance tests
# note: requires uncommenting main() in source module xmltv_db
XMLTESTSRC  = $(XMLSRC) $(EPGSRC) epgui/pidescr epgui/pdc_themes
XMLTESTOBJ  = $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(XMLTESTSRC)))
$(BUILD_DIR)/parsexmltv : $(XMLTESTOBJ)
	$(CC) -o $@ $(XMLTESTOBJ) $(LDFLAGS)
parsexmltv : $(BUILD_DIR)/parsexmltv

.PHONY: tcl_headers tcl_headers_tvsim
tcl_headers: $(addprefix $(BUILD_DIR)/, $(addsuffix .c, $(TCLSRC)))
tcl_headers_tvsim: $(addprefix $(BUILD_DIR)/, $(addsuffix .c, $(TVSIM_TCLSRC) $(VBIREC_TCLSRC)))

$(BUILD_DIR)/tcl2c: tcl2c.c
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/tcl2c tcl2c.c

$(TCL_LIBRARY_PATH)/tclIndex $(TK_LIBRARY_PATH)/tclIndex :
	@if [ ! -f $(TCL_LIBRARY_PATH) -o ! -f $(TK_LIBRARY_PATH) ] ; then \
	  echo "$(@D) is not a valid Tcl/Tk library directory"; \
	  echo "Check the definitions of TCL_LIBRARY_PATH and TK_LIBRARY_PATH"; \
	  false; \
	fi

epgui/loadtcl.c :: $(TCL_LIBRARY_PATH)/tclIndex $(TK_LIBRARY_PATH)/tclIndex

.PHONY: covstats
covstats:
	geninfo -b `pwd` $(BUILD_DIR)/epg{db,ctl,ui,vbi}/. $(BUILD_DIR)/xmltv/.
	genhtml --output-directory $(BUILD_DIR)/html --legend -t "nxtvepg" $(BUILD_DIR)/*/*.gcda.info

.PHONY: bak
bak:
	cd .. && tar cvf /tmp/pc.tar -X pc/tar-ex pc
	bzip2 -9f /tmp/pc.tar

.PHONY: build_dir
build_dir:
	@mkdir -p $(addprefix $(BUILD_DIR)/,$(SUBDIRS))

# include dependencies (each object has one dependency file)
-include $(BUILD_DIR)/*/*.dep

# end UNIX specific part
endif

# ----- shared between all platforms -----------------------------------------

.PHONY: clean
clean:
	-rm -rf build-* deb
	-rm -f nxtvepg_*.deb
	-rm -f core debug.out tags
	-rm -f epgtcl/helptexts*.tcl manual*.html nxtvepg*.1 tvsim/*.html tvsim/*.1

.PHONY: depend
depend:
	@echo "'make depend' is no longer required:"
	@echo "dependencies are generated and updated while compiling."

.PHONY: ctags
ctags:
	ctags -R

epgtcl/helptexts.tcl: nxtvepg.pod pod2help.pl
	$(PERL) pod2help.pl -lang en nxtvepg.pod > epgtcl/helptexts.tcl

epgtcl/helptexts_de.tcl: nxtvepg-de.pod pod2help.pl
	$(PERL) pod2help.pl -lang de nxtvepg-de.pod > epgtcl/helptexts_de.tcl

nxtvepg.1: nxtvepg.pod epgctl/epgversion.h
	EPG_VERSION_STR=`egrep '[ \t]*#[ \t]*define[ \t]*EPG_VERSION_STR' epgctl/epgversion.h | head -1 | sed -e 's#.*"\(.*\)".*#\1#'`; \
	pod2man -date " " -center "nxtvepg XMLTV browser" -section "1" \
	        -release "nxtvepg "$$EPG_VERSION_STR" (C) 2023 T. Zoerner" \
	   nxtvepg.pod > nxtvepg.1

nxtvepgd.1: nxtvepg.1
	echo ".so man1/nxtvepg.1" > nxtvepgd.1

manual.html: nxtvepg.pod
	pod2html nxtvepg.pod | $(PERL) -p -e 's/(HREF=\"#)([^:"]+: |[^_"]+(_[^_"]+)?__)+/$$1/gi;' > manual.html
	rm -f pod2htm?.* pod2html-{dircache,itemcache}

manual-de.html: nxtvepg-de.pod epgctl/epgversion.h
	EPG_VERSION_STR=`egrep '[ \t]*#[ \t]*define[ \t]*EPG_VERSION_STR' epgctl/epgversion.h | head -1 | sed -e 's#.*"\(.*\)".*#\1#'`; \
	pod2html nxtvepg-de.pod | $(PERL) -p -e 's/(HREF=\"#)([^:"]+: |[^_"]+(_[^_"]+)?__)+/$$1/gi;' > manual-de.html; \
	rm -f pod2htm?.* pod2html-{dircache,itemcache}

tvsim/tvsim.1: tvsim/tvsim.pod tvsim/tvsim_version.h
	TVSIM_VERSION_STR=`egrep '[ \t]*#[ \t]*define[ \t]*TVSIM_VERSION_STR' tvsim/tvsim_version.h | head -1 | sed -e 's#.*"\(.*\)".*#\1#'`; \
	pod2man -date " " -center "TV app interaction simulator" -section "1" \
	        -release "tvsim "$$TVSIM_VERSION_STR" (C) 2002-2007 T. Zoerner" \
	        tvsim/tvsim.pod > tvsim/tvsim.1; \

tvsim/tvsim.html: tvsim/tvsim.pod tvsim/tvsim_version.h
	pod2html tvsim/tvsim.pod | $(PERL) -p -e 's/(HREF=\"#)([^:"]+: |[^_"]+(_[^_"]+)?__)+/$$1/gi;' > tvsim/tvsim.html
	rm -f pod2htm?.* pod2html-{dircache,itemcache}

tvsim/vbirec.1: tvsim/vbirec.pod tvsim/tvsim_version.h
	TVSIM_VERSION_STR=`egrep '[ \t]*#[ \t]*define[ \t]*TVSIM_VERSION_STR' tvsim/tvsim_version.h | head -1 | sed -e 's#.*"\(.*\)".*#\1#'`; \
	pod2man -date " " -center "VBI recorder" -section "1" \
	        -release "vbirec (C) 2002-2007 T. Zoerner" \
	        tvsim/vbirec.pod > tvsim/vbirec.1;

tvsim/vbirec.html: tvsim/vbirec.pod tvsim/tvsim_version.h
	pod2html tvsim/vbirec.pod | $(PERL) -p -e 's/(HREF=\"#)([^:"]+: |[^_"]+(_[^_"]+)?__)+/$$1/gi;' > tvsim/vbirec.html
	rm -f pod2htm?.* pod2html-{dircache,itemcache}

tvsim/vbiplay.1: tvsim/vbiplay.pod tvsim/tvsim_version.h
	TVSIM_VERSION_STR=`egrep '[ \t]*#[ \t]*define[ \t]*TVSIM_VERSION_STR' tvsim/tvsim_version.h | head -1 | sed -e 's#.*"\(.*\)".*#\1#'`; \
	pod2man -date " " -center "VBI playback tool" -section "1" \
	        -release "vbiplay (C) 2002,2006 T. Zoerner" \
	        tvsim/vbiplay.pod > tvsim/vbiplay.1

tvsim/vbiplay.html: tvsim/vbiplay.pod tvsim/tvsim_version.h
	pod2html tvsim/vbiplay.pod | $(PERL) -p -e 's/(HREF=\"#)([^:"]+: |[^_"]+(_[^_"]+)?__)+/$$1/gi;' > tvsim/vbiplay.html; \
	rm -f pod2htm?.* pod2html-{dircache,itemcache}
