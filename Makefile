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
#    Controls compilation of C and Tcl/Tk source to an executable.
#    Tcl/Tk source has to be converted to C by tcl2c.
#
#  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
#
#  $Id: Makefile,v 1.8 2000/06/14 20:49:48 tom Exp tom $
#

CC      = gcc
WARN    = -Wall -Wpointer-arith -Wnested-externs \
          -Werror -Wstrict-prototypes -Wmissing-prototypes
LDFLAGS = 
#LDLIBS  = -ltk4.2i -ltcl7.6i -L/usr/X11R6/lib -lX11 -lm -ldl
LDLIBS  = -ltk8.0 -ltcl8.0 -L/usr/X11R6/lib -lX11 -lm -ldl
#LDLIBS  = libtk8.0.a libtcl8.0.a -lX11 -lm -ldl -L/usr/X11R6/lib
INCS   += -I. -I/tom/tv/bttv/driver -I/usr/X11R6/include -Itcl
DEFS   += -DTK_LIBRARY_PATH=\"/usr/X11R6/lib/tk8.0\"
DEFS   += -DTCL_LIBRARY_PATH=\"/usr/lib/tcl8.0\"
CFLAGS  = -pipe $(WARN) $(INCS) $(DEFS) -g -O2

OBJS    = epgctl/epgmain.o epgctl/debug.o epgctl/vbidecode.o epgctl/epgacqctl.o \
          epgdb/epgdbacq.o epgdb/hamming.o epgdb/epgstream.o epgdb/epgtxtdump.o \
          epgdb/epgdbmgmt.o epgdb/epgdbif.o epgdb/epgdbsav.o epgdb/epgdbfil.o \
          epgdb/epgblock.o \
          epgui/pilistbox.o epgui/epgui.o epgui/pifilter.o \
          epgui/statswin.o epgui/pdc_themes.o epgui/menucmd.o

all: nxtvepg

nxtvepg: $(OBJS)
	$(CC) $(CFLAGS) $(INCS) $(LDFLAGS) -o nxtvepg $(OBJS) $(LDLIBS)

##%.o: %.c
##	$(CC) $(CFLAGS) -c *.c -o *.o

tcl2c: tcl2c.c
	$(CC) -o tcl2c tcl2c.c

epgui/epgui.c: epgui/epgui.tcl tcl2c
	cat epgui/epgui.tcl | ./tcl2c epgui_tcl_script > epgui/epgui.c

clean:
	-rm -f *.o epg*/*.o core a.out tcl2c nxtvepg epgui/epgui.c

depend:
	makedepend $(INCS) -f Makefile epg*/*.[ch]

# DO NOT DELETE

epgctl/debug.o: epgctl/mytypes.h /usr/include/sys/types.h
epgctl/debug.o: /usr/include/features.h /usr/include/sys/cdefs.h
epgctl/debug.o: /usr/include/gnu/stubs.h /usr/include/gnu/types.h
epgctl/debug.o: /usr/include/time.h
epgctl/debug.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgctl/debug.o: /usr/include/endian.h /usr/include/bytesex.h
epgctl/debug.o: /usr/include/sys/select.h /usr/include/selectbits.h
epgctl/debug.o: /usr/include/stdio.h /usr/include/libio.h
epgctl/debug.o: /usr/include/features.h /usr/include/sys/cdefs.h
epgctl/debug.o: /usr/include/gnu/stubs.h /usr/include/_G_config.h
epgctl/debug.o: /usr/include/gnu/types.h
epgctl/debug.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgctl/debug.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgctl/debug.o: /usr/include/stdio_lim.h
epgctl/epgacqctl.o: /usr/include/string.h /usr/include/features.h
epgctl/epgacqctl.o: /usr/include/sys/cdefs.h /usr/include/gnu/stubs.h
epgctl/epgacqctl.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgctl/epgacqctl.o: /usr/include/time.h /usr/include/gnu/types.h
epgctl/epgacqctl.o: /usr/include/tcl.h /usr/include/stdio.h
epgctl/epgacqctl.o: /usr/include/libio.h /usr/include/_G_config.h
epgctl/epgacqctl.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgctl/epgacqctl.o: /usr/include/stdio_lim.h epgctl/mytypes.h
epgctl/epgacqctl.o: /usr/include/sys/types.h /usr/include/endian.h
epgctl/epgacqctl.o: /usr/include/bytesex.h /usr/include/sys/select.h
epgctl/epgacqctl.o: /usr/include/selectbits.h epgctl/debug.h epgdb/epgblock.h
epgctl/epgacqctl.o: epgdb/epgstream.h epgdb/epgdbfil.h epgdb/epgdbif.h
epgctl/epgacqctl.o: epgdb/epgdbsav.h epgdb/epgdbmgmt.h epgdb/epgdbacq.h
epgctl/epgacqctl.o: epgui/pifilter.h epgui/menucmd.h epgui/statswin.h
epgctl/epgacqctl.o: epgctl/epgmain.h epgctl/vbidecode.h epgctl/epgacqctl.h
epgctl/epgmain.o: /usr/include/unistd.h /usr/include/features.h
epgctl/epgmain.o: /usr/include/sys/cdefs.h /usr/include/gnu/stubs.h
epgctl/epgmain.o: /usr/include/posix_opt.h /usr/include/gnu/types.h
epgctl/epgmain.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgctl/epgmain.o: /usr/include/confname.h /usr/include/sys/param.h
epgctl/epgmain.o: /usr/include/limits.h /usr/include/posix1_lim.h
epgctl/epgmain.o: /usr/include/local_lim.h /usr/include/linux/limits.h
epgctl/epgmain.o: /usr/include/posix2_lim.h /usr/include/linux/param.h
epgctl/epgmain.o: /usr/include/asm/param.h /usr/include/sys/types.h
epgctl/epgmain.o: /usr/include/time.h /usr/include/endian.h
epgctl/epgmain.o: /usr/include/bytesex.h /usr/include/sys/select.h
epgctl/epgmain.o: /usr/include/selectbits.h /usr/include/pwd.h
epgctl/epgmain.o: /usr/include/stdio.h /usr/include/libio.h
epgctl/epgmain.o: /usr/include/_G_config.h
epgctl/epgmain.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgctl/epgmain.o: /usr/include/stdio_lim.h /usr/include/signal.h
epgctl/epgmain.o: /usr/include/sigset.h /usr/include/signum.h
epgctl/epgmain.o: /usr/include/sigaction.h /usr/include/sigcontext.h
epgctl/epgmain.o: /usr/include/asm/sigcontext.h /usr/include/sigstack.h
epgctl/epgmain.o: /usr/include/math.h /usr/include/huge_val.h
epgctl/epgmain.o: /usr/include/mathcalls.h
epgctl/epgmain.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/float.h
epgctl/epgmain.o: /usr/include/malloc.h /usr/include/errno.h
epgctl/epgmain.o: /usr/include/errnos.h /usr/include/linux/errno.h
epgctl/epgmain.o: /usr/include/asm/errno.h /usr/include/fcntl.h
epgctl/epgmain.o: /usr/include/fcntlbits.h /usr/include/stdlib.h
epgctl/epgmain.o: /usr/include/alloca.h /usr/include/string.h
epgctl/epgmain.o: /usr/include/ctype.h /usr/include/sys/stat.h
epgctl/epgmain.o: /usr/include/statbuf.h /usr/include/tcl.h
epgctl/epgmain.o: /usr/X11R6/include/tk.h /usr/X11R6/include/X11/Xlib.h
epgctl/epgmain.o: /usr/X11R6/include/X11/X.h
epgctl/epgmain.o: /usr/X11R6/include/X11/Xfuncproto.h
epgctl/epgmain.o: /usr/X11R6/include/X11/Xosdefs.h epgctl/mytypes.h
epgctl/epgmain.o: epgctl/debug.h epgdb/epgblock.h epgdb/epgstream.h
epgctl/epgmain.o: epgdb/epgdbfil.h epgdb/epgdbif.h epgdb/epgdbmgmt.h
epgctl/epgmain.o: epgdb/epgdbacq.h epgctl/epgacqctl.h epgui/statswin.h
epgctl/epgmain.o: epgui/menucmd.h epgui/pilistbox.h epgui/pifilter.h
epgctl/epgmain.o: epgui/nxtv_logo.xbm epgctl/vbidecode.h epgctl/epgmain.h
epgctl/mytypes.o: /usr/include/sys/types.h /usr/include/features.h
epgctl/mytypes.o: /usr/include/sys/cdefs.h /usr/include/gnu/stubs.h
epgctl/mytypes.o: /usr/include/gnu/types.h /usr/include/time.h
epgctl/mytypes.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgctl/mytypes.o: /usr/include/endian.h /usr/include/bytesex.h
epgctl/mytypes.o: /usr/include/sys/select.h /usr/include/selectbits.h
epgctl/vbidecode.o: /usr/include/sys/ioctl.h /usr/include/features.h
epgctl/vbidecode.o: /usr/include/sys/cdefs.h /usr/include/gnu/stubs.h
epgctl/vbidecode.o: /usr/include/ioctls.h /usr/include/asm/ioctls.h
epgctl/vbidecode.o: /usr/include/asm/ioctl.h /usr/include/ioctl-types.h
epgctl/vbidecode.o: /usr/include/sys/ttydefaults.h /usr/include/sys/types.h
epgctl/vbidecode.o: /usr/include/gnu/types.h /usr/include/time.h
epgctl/vbidecode.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgctl/vbidecode.o: /usr/include/endian.h /usr/include/bytesex.h
epgctl/vbidecode.o: /usr/include/sys/select.h /usr/include/selectbits.h
epgctl/vbidecode.o: /usr/include/fcntl.h /usr/include/fcntlbits.h
epgctl/vbidecode.o: /usr/include/stdio.h /usr/include/libio.h
epgctl/vbidecode.o: /usr/include/_G_config.h
epgctl/vbidecode.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgctl/vbidecode.o: /usr/include/stdio_lim.h /usr/include/stdlib.h
epgctl/vbidecode.o: /usr/include/alloca.h /usr/include/unistd.h
epgctl/vbidecode.o: /usr/include/posix_opt.h /usr/include/confname.h
epgctl/vbidecode.o: /usr/include/errno.h /usr/include/errnos.h
epgctl/vbidecode.o: /usr/include/linux/errno.h /usr/include/asm/errno.h
epgctl/vbidecode.o: /usr/include/sys/stat.h /usr/include/statbuf.h
epgctl/vbidecode.o: /usr/include/sys/time.h /usr/include/timebits.h
epgctl/vbidecode.o: /usr/include/signal.h /usr/include/sigset.h
epgctl/vbidecode.o: /usr/include/signum.h /usr/include/sigaction.h
epgctl/vbidecode.o: /usr/include/sigcontext.h /usr/include/asm/sigcontext.h
epgctl/vbidecode.o: /usr/include/sigstack.h /usr/include/sys/param.h
epgctl/vbidecode.o: /usr/include/limits.h /usr/include/posix1_lim.h
epgctl/vbidecode.o: /usr/include/local_lim.h /usr/include/linux/limits.h
epgctl/vbidecode.o: /usr/include/posix2_lim.h /usr/include/linux/param.h
epgctl/vbidecode.o: /usr/include/asm/param.h /usr/include/sys/ipc.h
epgctl/vbidecode.o: /usr/include/sys/ipc_buf.h /usr/include/sys/shm.h
epgctl/vbidecode.o: /usr/include/sys/shm_buf.h /usr/include/sys/resource.h
epgctl/vbidecode.o: /usr/include/resourcebits.h /usr/include/asm/resource.h
epgctl/vbidecode.o: /usr/include/sys/wait.h /usr/include/waitflags.h
epgctl/vbidecode.o: /usr/include/waitstatus.h /usr/include/string.h
epgctl/vbidecode.o: /usr/include/malloc.h epgctl/mytypes.h epgctl/debug.h
epgctl/vbidecode.o: epgdb/hamming.h epgdb/epgblock.h epgdb/epgdbif.h
epgctl/vbidecode.o: epgdb/epgdbacq.h epgctl/vbidecode.h
epgctl/vbidecode.o: /tom/tv/bttv/driver/bttv.h /usr/include/linux/types.h
epgctl/vbidecode.o: /usr/include/linux/posix_types.h
epgctl/vbidecode.o: /usr/include/asm/posix_types.h /usr/include/asm/types.h
epgctl/vbidecode.o: /usr/include/linux/wait.h /tom/tv/bttv/driver/i2c.h
epgctl/vbidecode.o: /tom/tv/bttv/driver/msp3400.h /tom/tv/bttv/driver/bt848.h
epgctl/vbidecode.o: /tom/tv/bttv/driver/videodev.h
epgdb/epgblock.o: /usr/include/time.h /usr/include/gnu/types.h
epgdb/epgblock.o: /usr/include/features.h /usr/include/sys/cdefs.h
epgdb/epgblock.o: /usr/include/gnu/stubs.h /usr/include/string.h
epgdb/epgblock.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgdb/epgblock.o: /usr/include/malloc.h epgctl/mytypes.h
epgdb/epgblock.o: /usr/include/sys/types.h /usr/include/endian.h
epgdb/epgblock.o: /usr/include/bytesex.h /usr/include/sys/select.h
epgdb/epgblock.o: /usr/include/selectbits.h epgctl/debug.h
epgdb/epgblock.o: /usr/include/stdio.h /usr/include/libio.h
epgdb/epgblock.o: /usr/include/_G_config.h
epgdb/epgblock.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgdb/epgblock.o: /usr/include/stdio_lim.h epgctl/epgmain.h epgdb/epgblock.h
epgdb/epgblock.o: epgdb/epgdbif.h
epgdb/epgdbacq.o: /usr/include/string.h /usr/include/features.h
epgdb/epgdbacq.o: /usr/include/sys/cdefs.h /usr/include/gnu/stubs.h
epgdb/epgdbacq.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgdb/epgdbacq.o: /usr/include/malloc.h /usr/include/stdio.h
epgdb/epgdbacq.o: /usr/include/libio.h /usr/include/_G_config.h
epgdb/epgdbacq.o: /usr/include/gnu/types.h
epgdb/epgdbacq.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgdb/epgdbacq.o: /usr/include/stdio_lim.h epgctl/mytypes.h
epgdb/epgdbacq.o: /usr/include/sys/types.h /usr/include/time.h
epgdb/epgdbacq.o: /usr/include/endian.h /usr/include/bytesex.h
epgdb/epgdbacq.o: /usr/include/sys/select.h /usr/include/selectbits.h
epgdb/epgdbacq.o: epgctl/debug.h epgdb/hamming.h epgdb/epgblock.h
epgdb/epgdbacq.o: epgdb/epgdbif.h epgdb/epgdbmgmt.h epgdb/epgdbacq.h
epgdb/epgdbacq.o: epgctl/vbidecode.h epgdb/epgstream.h epgdb/epgtxtdump.h
epgdb/epgdbacq.o: epgui/statswin.h epgctl/epgacqctl.h
epgdb/epgdbfil.o: /usr/include/string.h /usr/include/features.h
epgdb/epgdbfil.o: /usr/include/sys/cdefs.h /usr/include/gnu/stubs.h
epgdb/epgdbfil.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgdb/epgdbfil.o: /usr/include/malloc.h /usr/include/ctype.h
epgdb/epgdbfil.o: /usr/include/endian.h /usr/include/bytesex.h
epgdb/epgdbfil.o: /usr/include/time.h /usr/include/gnu/types.h
epgdb/epgdbfil.o: epgctl/mytypes.h /usr/include/sys/types.h
epgdb/epgdbfil.o: /usr/include/sys/select.h /usr/include/selectbits.h
epgdb/epgdbfil.o: epgctl/debug.h /usr/include/stdio.h /usr/include/libio.h
epgdb/epgdbfil.o: /usr/include/_G_config.h
epgdb/epgdbfil.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgdb/epgdbfil.o: /usr/include/stdio_lim.h epgctl/epgmain.h epgdb/epgblock.h
epgdb/epgdbfil.o: epgdb/epgstream.h epgdb/epgdbacq.h epgdb/epgdbfil.h
epgdb/epgdbfil.o: epgdb/epgdbif.h
epgdb/epgdbif.o: /usr/include/time.h /usr/include/gnu/types.h
epgdb/epgdbif.o: /usr/include/features.h /usr/include/sys/cdefs.h
epgdb/epgdbif.o: /usr/include/gnu/stubs.h /usr/include/string.h
epgdb/epgdbif.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgdb/epgdbif.o: epgctl/mytypes.h /usr/include/sys/types.h
epgdb/epgdbif.o: /usr/include/endian.h /usr/include/bytesex.h
epgdb/epgdbif.o: /usr/include/sys/select.h /usr/include/selectbits.h
epgdb/epgdbif.o: epgctl/debug.h /usr/include/stdio.h /usr/include/libio.h
epgdb/epgdbif.o: /usr/include/_G_config.h
epgdb/epgdbif.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgdb/epgdbif.o: /usr/include/stdio_lim.h epgdb/epgblock.h epgdb/epgdbfil.h
epgdb/epgdbif.o: epgdb/epgdbif.h epgdb/epgdbmgmt.h epgdb/epgstream.h
epgdb/epgdbmgmt.o: /usr/include/string.h /usr/include/features.h
epgdb/epgdbmgmt.o: /usr/include/sys/cdefs.h /usr/include/gnu/stubs.h
epgdb/epgdbmgmt.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgdb/epgdbmgmt.o: /usr/include/time.h /usr/include/gnu/types.h
epgdb/epgdbmgmt.o: /usr/include/malloc.h epgctl/mytypes.h
epgdb/epgdbmgmt.o: /usr/include/sys/types.h /usr/include/endian.h
epgdb/epgdbmgmt.o: /usr/include/bytesex.h /usr/include/sys/select.h
epgdb/epgdbmgmt.o: /usr/include/selectbits.h epgctl/debug.h
epgdb/epgdbmgmt.o: /usr/include/stdio.h /usr/include/libio.h
epgdb/epgdbmgmt.o: /usr/include/_G_config.h
epgdb/epgdbmgmt.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgdb/epgdbmgmt.o: /usr/include/stdio_lim.h epgdb/epgblock.h epgdb/epgdbfil.h
epgdb/epgdbmgmt.o: epgdb/epgdbif.h epgdb/epgdbacq.h epgui/pilistbox.h
epgdb/epgdbmgmt.o: epgui/statswin.h epgdb/epgdbmgmt.h
epgdb/epgdbsav.o: /usr/include/unistd.h /usr/include/features.h
epgdb/epgdbsav.o: /usr/include/sys/cdefs.h /usr/include/gnu/stubs.h
epgdb/epgdbsav.o: /usr/include/posix_opt.h /usr/include/gnu/types.h
epgdb/epgdbsav.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgdb/epgdbsav.o: /usr/include/confname.h /usr/include/dirent.h
epgdb/epgdbsav.o: /usr/include/direntry.h /usr/include/posix1_lim.h
epgdb/epgdbsav.o: /usr/include/local_lim.h /usr/include/linux/limits.h
epgdb/epgdbsav.o: /usr/include/stdio.h /usr/include/libio.h
epgdb/epgdbsav.o: /usr/include/_G_config.h
epgdb/epgdbsav.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgdb/epgdbsav.o: /usr/include/stdio_lim.h /usr/include/errno.h
epgdb/epgdbsav.o: /usr/include/errnos.h /usr/include/linux/errno.h
epgdb/epgdbsav.o: /usr/include/asm/errno.h /usr/include/sys/types.h
epgdb/epgdbsav.o: /usr/include/time.h /usr/include/endian.h
epgdb/epgdbsav.o: /usr/include/bytesex.h /usr/include/sys/select.h
epgdb/epgdbsav.o: /usr/include/selectbits.h /usr/include/sys/stat.h
epgdb/epgdbsav.o: /usr/include/statbuf.h /usr/include/fcntl.h
epgdb/epgdbsav.o: /usr/include/fcntlbits.h /usr/include/string.h
epgdb/epgdbsav.o: /usr/include/malloc.h epgctl/mytypes.h epgctl/debug.h
epgdb/epgdbsav.o: epgdb/epgblock.h epgdb/epgdbacq.h epgdb/epgdbmgmt.h
epgdb/epgdbsav.o: epgdb/epgdbfil.h epgdb/epgdbif.h epgdb/epgdbsav.h
epgdb/epgstream.o: /usr/include/stdio.h /usr/include/libio.h
epgdb/epgstream.o: /usr/include/features.h /usr/include/sys/cdefs.h
epgdb/epgstream.o: /usr/include/gnu/stubs.h /usr/include/_G_config.h
epgdb/epgstream.o: /usr/include/gnu/types.h
epgdb/epgstream.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgdb/epgstream.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgdb/epgstream.o: /usr/include/stdio_lim.h /usr/include/malloc.h
epgdb/epgstream.o: /usr/include/string.h epgctl/mytypes.h
epgdb/epgstream.o: /usr/include/sys/types.h /usr/include/time.h
epgdb/epgstream.o: /usr/include/endian.h /usr/include/bytesex.h
epgdb/epgstream.o: /usr/include/sys/select.h /usr/include/selectbits.h
epgdb/epgstream.o: epgctl/debug.h epgdb/hamming.h epgdb/epgblock.h
epgdb/epgstream.o: epgdb/epgdbif.h epgdb/epgtxtdump.h epgdb/epgstream.h
epgdb/epgtxtdump.o: /usr/include/string.h /usr/include/features.h
epgdb/epgtxtdump.o: /usr/include/sys/cdefs.h /usr/include/gnu/stubs.h
epgdb/epgtxtdump.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgdb/epgtxtdump.o: /usr/include/stdio.h /usr/include/libio.h
epgdb/epgtxtdump.o: /usr/include/_G_config.h /usr/include/gnu/types.h
epgdb/epgtxtdump.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgdb/epgtxtdump.o: /usr/include/stdio_lim.h /usr/include/time.h
epgdb/epgtxtdump.o: epgctl/mytypes.h /usr/include/sys/types.h
epgdb/epgtxtdump.o: /usr/include/endian.h /usr/include/bytesex.h
epgdb/epgtxtdump.o: /usr/include/sys/select.h /usr/include/selectbits.h
epgdb/epgtxtdump.o: epgctl/debug.h epgdb/epgblock.h epgdb/epgdbfil.h
epgdb/epgtxtdump.o: epgdb/epgdbif.h epgdb/epgtxtdump.h epgui/pdc_themes.h
epgdb/hamming.o: epgctl/mytypes.h /usr/include/sys/types.h
epgdb/hamming.o: /usr/include/features.h /usr/include/sys/cdefs.h
epgdb/hamming.o: /usr/include/gnu/stubs.h /usr/include/gnu/types.h
epgdb/hamming.o: /usr/include/time.h
epgdb/hamming.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgdb/hamming.o: /usr/include/endian.h /usr/include/bytesex.h
epgdb/hamming.o: /usr/include/sys/select.h /usr/include/selectbits.h
epgdb/hamming.o: epgctl/debug.h /usr/include/stdio.h /usr/include/libio.h
epgdb/hamming.o: /usr/include/_G_config.h
epgdb/hamming.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgdb/hamming.o: /usr/include/stdio_lim.h epgdb/hamming.h
epgui/menucmd.o: /usr/include/stdio.h /usr/include/libio.h
epgui/menucmd.o: /usr/include/features.h /usr/include/sys/cdefs.h
epgui/menucmd.o: /usr/include/gnu/stubs.h /usr/include/_G_config.h
epgui/menucmd.o: /usr/include/gnu/types.h
epgui/menucmd.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgui/menucmd.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgui/menucmd.o: /usr/include/stdio_lim.h /usr/include/string.h
epgui/menucmd.o: /usr/include/time.h /usr/include/errno.h
epgui/menucmd.o: /usr/include/errnos.h /usr/include/linux/errno.h
epgui/menucmd.o: /usr/include/asm/errno.h /usr/include/tcl.h
epgui/menucmd.o: /usr/X11R6/include/tk.h /usr/X11R6/include/X11/Xlib.h
epgui/menucmd.o: /usr/include/sys/types.h /usr/include/endian.h
epgui/menucmd.o: /usr/include/bytesex.h /usr/include/sys/select.h
epgui/menucmd.o: /usr/include/selectbits.h /usr/X11R6/include/X11/X.h
epgui/menucmd.o: /usr/X11R6/include/X11/Xfuncproto.h
epgui/menucmd.o: /usr/X11R6/include/X11/Xosdefs.h epgctl/mytypes.h
epgui/menucmd.o: epgctl/debug.h epgdb/epgblock.h epgdb/epgdbmgmt.h
epgui/menucmd.o: epgdb/epgdbfil.h epgdb/epgdbif.h epgdb/epgdbsav.h
epgui/menucmd.o: epgdb/epgtxtdump.h epgctl/epgmain.h epgctl/epgacqctl.h
epgui/menucmd.o: epgui/pilistbox.h epgui/pifilter.h epgui/statswin.h
epgui/menucmd.o: epgui/menucmd.h
epgui/pifilter.o: /usr/include/time.h /usr/include/gnu/types.h
epgui/pifilter.o: /usr/include/features.h /usr/include/sys/cdefs.h
epgui/pifilter.o: /usr/include/gnu/stubs.h /usr/include/stdio.h
epgui/pifilter.o: /usr/include/libio.h /usr/include/_G_config.h
epgui/pifilter.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgui/pifilter.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgui/pifilter.o: /usr/include/stdio_lim.h /usr/include/stdlib.h
epgui/pifilter.o: /usr/include/sys/types.h /usr/include/endian.h
epgui/pifilter.o: /usr/include/bytesex.h /usr/include/sys/select.h
epgui/pifilter.o: /usr/include/selectbits.h /usr/include/alloca.h
epgui/pifilter.o: /usr/include/string.h /usr/include/ctype.h
epgui/pifilter.o: /usr/include/tcl.h /usr/X11R6/include/tk.h
epgui/pifilter.o: /usr/X11R6/include/X11/Xlib.h /usr/X11R6/include/X11/X.h
epgui/pifilter.o: /usr/X11R6/include/X11/Xfuncproto.h
epgui/pifilter.o: /usr/X11R6/include/X11/Xosdefs.h epgctl/mytypes.h
epgui/pifilter.o: epgctl/debug.h epgdb/epgblock.h epgdb/epgdbfil.h
epgui/pifilter.o: epgdb/epgdbif.h epgctl/epgmain.h epgui/pdc_themes.h
epgui/pifilter.o: epgui/pilistbox.h epgui/pifilter.h
epgui/pilistbox.o: /usr/include/time.h /usr/include/gnu/types.h
epgui/pilistbox.o: /usr/include/features.h /usr/include/sys/cdefs.h
epgui/pilistbox.o: /usr/include/gnu/stubs.h /usr/include/stdio.h
epgui/pilistbox.o: /usr/include/libio.h /usr/include/_G_config.h
epgui/pilistbox.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgui/pilistbox.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgui/pilistbox.o: /usr/include/stdio_lim.h /usr/include/stdlib.h
epgui/pilistbox.o: /usr/include/sys/types.h /usr/include/endian.h
epgui/pilistbox.o: /usr/include/bytesex.h /usr/include/sys/select.h
epgui/pilistbox.o: /usr/include/selectbits.h /usr/include/alloca.h
epgui/pilistbox.o: /usr/include/string.h /usr/include/tcl.h
epgui/pilistbox.o: /usr/X11R6/include/tk.h /usr/X11R6/include/X11/Xlib.h
epgui/pilistbox.o: /usr/X11R6/include/X11/X.h
epgui/pilistbox.o: /usr/X11R6/include/X11/Xfuncproto.h
epgui/pilistbox.o: /usr/X11R6/include/X11/Xosdefs.h epgctl/mytypes.h
epgui/pilistbox.o: epgctl/debug.h epgdb/epgblock.h epgdb/epgdbfil.h
epgui/pilistbox.o: epgdb/epgdbif.h epgctl/epgmain.h epgui/pdc_themes.h
epgui/pilistbox.o: epgui/pifilter.h epgui/pilistbox.h
epgui/statswin.o: /usr/include/string.h /usr/include/features.h
epgui/statswin.o: /usr/include/sys/cdefs.h /usr/include/gnu/stubs.h
epgui/statswin.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stddef.h
epgui/statswin.o: /usr/include/time.h /usr/include/gnu/types.h
epgui/statswin.o: /usr/include/tcl.h /usr/include/stdio.h
epgui/statswin.o: /usr/include/libio.h /usr/include/_G_config.h
epgui/statswin.o: /usr/lib/gcc-lib/i486-linux/2.7.2.3/include/stdarg.h
epgui/statswin.o: /usr/include/stdio_lim.h /usr/X11R6/include/tk.h
epgui/statswin.o: /usr/X11R6/include/X11/Xlib.h /usr/include/sys/types.h
epgui/statswin.o: /usr/include/endian.h /usr/include/bytesex.h
epgui/statswin.o: /usr/include/sys/select.h /usr/include/selectbits.h
epgui/statswin.o: /usr/X11R6/include/X11/X.h
epgui/statswin.o: /usr/X11R6/include/X11/Xfuncproto.h
epgui/statswin.o: /usr/X11R6/include/X11/Xosdefs.h epgctl/mytypes.h
epgui/statswin.o: epgctl/debug.h epgdb/epgblock.h epgdb/epgdbacq.h
epgui/statswin.o: epgdb/epgdbfil.h epgdb/epgdbif.h epgctl/epgmain.h
epgui/statswin.o: epgctl/epgacqctl.h epgui/statswin.h
