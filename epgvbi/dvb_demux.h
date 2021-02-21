/*
 *  libzvbi -- DVB VBI demultiplexer
 *
 *  Copyright (C) 2004, 2007 Michael H. Schimek
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the 
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 *  Boston, MA  02110-1301  USA.
 */

/* libzvbi #Id: dvb_demux.h,v 1.11 2008/02/24 14:17:54 mschimek Exp # */

#ifndef __ZVBI_DVB_DEMUX_H__
#define __ZVBI_DVB_DEMUX_H__

#include <stdint.h>		/* uintN_t */

//#include "sliced.h"


/**
 * DVB VBI demultiplexer.
 *
 * The contents of this structure are private.
 * Call vbi_dvb_pes_demux_new() to allocate a DVB demultiplexer.
 */
typedef struct _vbi_dvb_demux vbi_dvb_demux;

extern void
vbi_dvb_demux_reset		(vbi_dvb_demux *	dx);
extern unsigned int
vbi_dvb_demux_cor		(vbi_dvb_demux *	dx,
				 vbi_sliced *		sliced,
				 unsigned int 		sliced_lines,
				 int64_t *		pts,
				 const uint8_t **	buffer,
				 unsigned int *		buffer_left);
extern void
vbi_dvb_demux_delete		(vbi_dvb_demux *	dx);
extern vbi_dvb_demux *
vbi_dvb_pes_demux_new		(void);

#endif /* __ZVBI_DVB_DEMUX_H__ */
