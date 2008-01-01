# init.tcl --
#
# Default system startup file for Tcl-based applications.  Defines
# "unknown" procedure and auto-load facilities.
#
# RCS: @(#) $Id: init.tcl,v 1.100 2007/12/13 15:26:03 dgp Exp $
#
# Copyright (c) 1991-1993 The Regents of the University of California.
# Copyright (c) 1994-1996 Sun Microsystems, Inc.
# Copyright (c) 1998-1999 Scriptics Corporation.
# Copyright (c) 2004 by Kevin B. Kenny.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#

if {[info commands package] == ""} {
    error "version mismatch: library\nscripts expect Tcl version 7.5b1 or later but the loaded version is\nonly [info patchlevel]"
}
package require -exact Tcl 8.5.0

