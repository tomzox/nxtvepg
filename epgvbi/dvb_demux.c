/*
 *  libzvbi -- DVB VBI demultiplexer
 *
 *  Copyright (C) 2004, 2006, 2007 Michael H. Schimek
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

/* libzvbi #Id: dvb_demux.c,v 1.23 2008/02/19 00:35:15 mschimek Exp # */
/* $Id: dvb_demux.c,v 1.1 2020/06/19 18:47:18 tom Exp tom $ */

#if !defined (USE_LIBZVBI)

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"
#include "epgvbi/hamming.h"
#include "epgvbi/zvbidecoder.h"
#include "epgvbi/dvb_demux.h"


#undef MIN
#define MIN(x, y)							\
({									\
	typeof(x) _x = x;						\
	typeof(y) _y = y;						\
									\
	(void)(&_x == &_y); /* alert when type mismatch */		\
	(_x < _y) ? _x : _y;						\
})

#undef MAX
#define MAX(x, y)							\
({									\
	typeof(x) _x = x;						\
	typeof(y) _y = y;						\
									\
	(void)(&_x == &_y); /* alert when type mismatch */		\
	(_x > _y) ? _x : _y;						\
})

#if __GNUC__ < 3
/* Expect expression usually true/false, schedule accordingly. */
#  define likely(expr) (expr)
#  define unlikely(expr) (expr)
#else
#  define likely(expr) __builtin_expect(expr, 1)
#  define unlikely(expr) __builtin_expect(expr, 0)
#endif

#define CLEAR(var) memset (&(var), 0, sizeof (var))
#define N_ELEMENTS(array) (sizeof (array) / sizeof (*(array)))

static unsigned int
vbi_rev8 (uint8_t c)
{
        return reverse8Bits[c];
}

//#include "dvb.h"
/**
 * @internal
 * ISO 13818-1 section 2.4.3.7, Table 2-19 stream_id assignments.
 */
#define PRIVATE_STREAM_1 0xBD

/**
 * @internal
 * EN 301 775 section 4.3.2, Table 2 data_identifier.
 */
typedef enum {
	/* 0x00 ... 0x0F reserved. */

	/* Teletext combined with VPS and/or WSS and/or CC
	   and/or VBI sample data. (EN 300 472, 301 775) */
	DATA_ID_EBU_TELETEXT_BEGIN		= 0x10,
	DATA_ID_EBU_TELETEXT_END		= 0x20,

	/* 0x20 ... 0x7F reserved. */

	DATA_ID_USER_DEFINED1_BEGIN		= 0x80,
	DATA_ID_USER_DEFINED2_END		= 0x99,

	/* Teletext and/or VPS and/or WSS and/or CC and/or
	   VBI sample data. (EN 301 775) */
	DATA_ID_EBU_DATA_BEGIN			= 0x99,
	DATA_ID_EBU_DATA_END			= 0x9C,

	/* 0x9C ... 0xFF reserved. */
} data_identifier;

/**
 * @internal
 * EN 301 775 section 4.3.2, Table 3 data_unit_id.
 */
typedef enum {
	/* 0x00 ... 0x01 reserved. */

	DATA_UNIT_EBU_TELETEXT_NON_SUBTITLE	= 0x02,
	DATA_UNIT_EBU_TELETEXT_SUBTITLE,

	/* 0x04 ... 0x7F reserved. */

	DATA_UNIT_USER_DEFINED1_BEGIN		= 0x80,
	DATA_UNIT_USER_DEFINED1_END		= 0xC0,

	/* Libzvbi private, not defined in EN 301 775. */
	DATA_UNIT_ZVBI_WSS_CPR1204		= 0xB4,
	DATA_UNIT_ZVBI_CLOSED_CAPTION_525,
	DATA_UNIT_ZVBI_MONOCHROME_SAMPLES_525,

	DATA_UNIT_EBU_TELETEXT_INVERTED		= 0xC0,

	/* EN 301 775 Table 1 says this is Teletext data,
	   Table 3 says reserved. */
	DATA_UNIT_C1				= 0xC1,

	/* 0xC2 reserved. */

	DATA_UNIT_VPS				= 0xC3,
	DATA_UNIT_WSS,
	DATA_UNIT_CLOSED_CAPTION,
	DATA_UNIT_MONOCHROME_SAMPLES,

	DATA_UNIT_USER_DEFINED2_BEGIN		= 0xC7,
	DATA_UNIT_USER_DEFINED2_END		= 0xFE,

	DATA_UNIT_STUFFING			= 0xFF
} data_unit_id;

/**
 * @addtogroup DVBDemux DVB VBI demultiplexer
 * @ingroup LowDec
 * @brief Extracting VBI data from a DVB PES or TS stream.
 *
 * These functions extract raw and/or sliced VBI data from a DVB Packetized
 * Elementary Stream or Transport Stream as defined in EN 300 472 "Digital
 * Video Broadcasting (DVB); Specification for conveying ITU-R System B
 * Teletext in DVB bitstreams" and EN 301 775 "Digital Video Broadcasting
 * (DVB); Specification for the carriage of Vertical Blanking Information
 * (VBI) data in DVB bitstreams".
 *
 * Note EN 300 468 "Specification for Service Information (SI) in DVB
 * systems" defines another method to transmit VPS data in DVB streams.
 * Libzvbi does not provide functions to decode SI tables but the
 * vbi_decode_dvb_pdc_descriptor() function is available to convert a PDC
 * descriptor to a VPS PIL.
 */

/* XXX Preliminary. */
enum {
	VBI_ERR_BUFFER_OVERFLOW		= 0x7080600,
	VBI_ERR_SLICED_BUFFER_OVERFLOW	= 0x7080601,
	VBI_ERR_RAW_BUFFER_OVERFLOW,
	VBI_ERR_SYNC_LOST		= 0x7080700,
	VBI_ERR_SCRAMBLED		= 0x7080800,
	VBI_ERR_STREAM_SYNTAX		= 0x7080900,
	VBI_ERR_DU_OVERFLOW		= 0x7080901,
	VBI_ERR_DU_LENGTH,
	VBI_ERR_DU_LINE_NUMBER,
	VBI_ERR_DU_RAW_SEGMENT_POSITION,
	VBI_ERR_DU_RAW_SEGMENT_LOST,
	VBI_ERR_DU_RAW_DATA_INCOMPLETE,
	VBI_ERR_CALLBACK		= 0x7080a00,
};

struct wrap {
	/* Size must be >= maximum consume + maximum lookahead. */
	uint8_t	*		buffer;

	/* End of data in buffer (exclusive). */
	uint8_t *		bp;

	/* See below. */
	unsigned int		skip;
	unsigned int		consume;
	unsigned int		lookahead;

	/* Unconsumed data in the buffer, beginning at bp - leftover
	   and ending at bp. */
	unsigned int		leftover;
};

/**
 * @internal
 * @param w Wrap-around context.
 * @param dst Wrapped data pointer.
 * @param scan_end End of lookahead range.
 * @param src Source buffer pointer, will be incremented.
 * @param src_left Bytes left in source buffer, will be decremented.
 * @param src_size Size of source buffer.
 *
 * A buffer is assumed in memory at *src + *src_left - src_size, with
 * src_size. This function reads at most *src_left bytes from this
 * buffer starting at *src, incrementing *src and decrementing *src_left
 * by the number of bytes read. NOTE *src_left must be equal to src_size
 * when you change buffers.
 *
 * It removes (reads) w->skip bytes from the buffer and sets w->skip to
 * zero, then removes w->consume bytes (not implemented at this time,
 * assumed to be zero), copying the data AND the following w->lookahead
 * bytes to an output buffer. In other words, *src is incremented by
 * at most w->skip + w->consume bytes.
 *
 * On success TRUE is returned, *dst will point to the begin of the
 * copied data (w->consume + w->lookahead), *scan_end to the end.
 * However *scan_end - *dst can be greater than w->consume + w->lookahead
 * if *src_left permits this. NOTE if copying can be avoided *dst and
 * *scan_end may point into the source buffer, so don't free /
 * overwrite it prematurely. *src_left will be >= 0.
 *
 * w->skip, w->consume and w->lookahead can change between successful
 * calls.
 *
 * If more data is needed the function returns FALSE, and *src_left
 * will be 0.
 */
static vbi_bool
wrap_around			(struct wrap *		w,
				 const uint8_t **	dst,
				 const uint8_t **	scan_end,
				 const uint8_t **	src,
				 unsigned int *		src_left,
				 unsigned int		src_size)
{
	unsigned int available;
	unsigned int required;

	if (w->skip > 0) {
		/* w->skip is not w->consume to save copying. */

		if (w->skip > w->leftover) {
			w->skip -= w->leftover;
			w->leftover = 0;

			if (w->skip > *src_left) {
				w->skip -= *src_left;

				*src += *src_left;
				*src_left = 0;

				return FALSE;
			}

			*src += w->skip;
			*src_left -= w->skip;
		} else {
			w->leftover -= w->skip;
		}

		w->skip = 0;
	}

	available = w->leftover + *src_left;
	required = /* w->consume + */ w->lookahead;

	if (required > available || available > src_size) {
		/* Not enough data at src, or we have bytes left
		   over from the previous buffer, must wrap. */

		if (required > w->leftover) {
			/* Need more data in the wrap_buffer. */

			memmove (w->buffer, w->bp - w->leftover, w->leftover);
			w->bp = w->buffer + w->leftover;

			required -= w->leftover;

			if (required > *src_left) {
				memcpy (w->bp, *src, *src_left);
				w->bp += *src_left;

				w->leftover += *src_left;

				*src += *src_left;
				*src_left = 0;

				return FALSE;
			}

			memcpy (w->bp, *src, required);
			w->bp += required;

			w->leftover = w->lookahead;

			*src += required;
			*src_left -= required;

			*dst = w->buffer;
			*scan_end = w->bp - w->lookahead;
		} else {
			*dst = w->bp - w->leftover;
			*scan_end = w->bp - w->lookahead;

			/* w->leftover -= w->consume; */
		}
	} else {
		/* All the required bytes are in this frame and
		   we have a complete copy of the w->buffer
		   leftover bytes before src. */

		*dst = *src - w->leftover;
		*scan_end = *src + *src_left - w->lookahead;

		/* if (w->consume > w->leftover) {
			unsigned int advance;

			advance = w->consume - w->leftover;

			*src += advance;
			*src_left -= advance;

			w->leftover = 0;
		} else {
			w->leftover -= w->consume;
		} */
	}

	return TRUE;
}

/** @internal */
struct frame {
	/**
	 * Buffer for decoded sliced VBI data. As usual @a sliced_end
	 * is exclusive. Can be @c NULL if no sliced data is needed.
	 */
	vbi_sliced *		sliced_begin;
	vbi_sliced *		sliced_end;

	/** Next free (current) element in the sliced data buffer. */
	vbi_sliced *		sp;

	/**
	 * Buffer for decoded raw VBI data. This is an array of
	 * @a raw_count[0] + @a raw_count[1] lines, with 720 8 bit
	 * luma samples in each line (13.5 MHz sampling rate). Can be
	 * @c NULL if no raw data is needed.
	 */
	uint8_t *		raw;

	/**
	 * The frame lines covered by the raw array, first and second
	 * field respectively.
	 * XXX to be replaced by struct vbi_sampling_par.
	 */
	unsigned int		raw_start[2];
	unsigned int		raw_count[2];

	/**
	 * Pointer to the start of the current line in the @a raw
	 * VBI buffer.
	 */
	uint8_t *		rp;

	/**
	 * Data units can contain at most 251 bytes of payload,
	 * so raw VBI data is transmitted in segments. This field
	 * contains the number of raw VBI samples extracted so
	 * far, is zero before the first and after the last segment
	 * was extracted.
	 */
	unsigned int		raw_offset;

	/**
	 * The field (0 = first or 1 = second) and line (0 = unknown,
	 * 1 ... 31) number found in the last data unit (field_parity,
	 * line_offset).
	 */
	unsigned int		last_field;
	unsigned int		last_field_line;

	/**
	 * A frame line number calculated from @a last_field and
	 * @a last_field_line, or the next available line if
	 * @a last_field_line is zero. Initially zero.
	 */
	unsigned int		last_frame_line;

	/**
	 * The data_unit_id found in the last data unit. Initially
	 * zero.
	 */
	unsigned int		last_data_unit_id;

	/**
	 * The number of data units which have been extracted
	 * from the current PES packet.
	 */
	unsigned int		n_data_units_extracted_from_packet;
};

/* Minimum lookahead required to identify the packet header. */
#define PES_HEADER_LOOKAHEAD 48u
#define TS_HEADER_LOOKAHEAD 10u

/* Minimum lookahead required for a TS sync_byte search. */
#define TS_SYNC_SEARCH_LOOKAHEAD (188u + TS_HEADER_LOOKAHEAD - 1u)

/* Round x up to a cache friendly value. */
#define ALIGN(x) ((x + 15) & ~15)

typedef vbi_bool
demux_packet_fn			(vbi_dvb_demux *	dx,
				 const uint8_t **	src,
				 unsigned int *		src_left);

/** @internal */
struct _vbi_dvb_demux {
	/**
	 * PES wrap-around buffer. Must hold one PES packet,
	 * at most 6 + 65535 bytes (start_code[24], stream_id[8],
	 * PES_packet_length[16], max. PES_packet_length).
	 */
	uint8_t			pes_buffer[ALIGN (6 + 65536)];

	/**
	 * TS wrap-around buffer. Must hold one TS packet for
	 * sync_byte search (188 bytes), plus 9 bytes so we
         * can safely examine the header of the contained PES packet.
	 */
	uint8_t			ts_buffer[ALIGN (TS_SYNC_SEARCH_LOOKAHEAD)];

	/** Output buffer for vbi_dvb_demux_demux(). */
	vbi_sliced		sliced[64];

	/** Wrap-around state. */
	struct wrap		pes_wrap;
	struct wrap		ts_wrap;

	/** Data unit demux state. */
	struct frame		frame;

	/** PTS of current frame. */
	int64_t			frame_pts;

	/** PTS of current PES packet. */
	int64_t			packet_pts;

	/**
	 * A new frame commences in the current PES packet. We remember
	 * this for the next call and return, cannot reset immediately
	 * due to the coroutine design.
	 */
	vbi_bool		new_frame;

	/**
	 * The TS demuxer synchonized in the last iteration. The next
	 * incomming byte should be a sync_byte.
	 */
	vbi_bool		ts_in_sync;

	/** Data units to be extracted from the pes_buffer. */
	const uint8_t *		ts_frame_bp;
	unsigned int		ts_frame_todo;

	/** Payload to be copied from TS to pes_buffer. */
	uint8_t *		ts_pes_bp;
	unsigned int		ts_pes_todo;

	/**
	 * Next expected transport_packet continuity_counter.
	 * Value may be greater than 15, so you must compare
	 * modulo 16. -1 if unknown.
	 */
	int			ts_continuity;

	/** PID of VBI data to be filtered out of a TS. */
	unsigned int		ts_pid;
};

enum systems {
	SYSTEM_525 = 0,
	SYSTEM_625
};

#if 0
static void
log_block			(vbi_dvb_demux *	dx,
				 const uint8_t *	src,
				 unsigned int		n_bytes)
{
	char buffer[16 * 3 + 1];
	unsigned int i;

	if (0 == n_bytes)
		return;

	for (;;) {
		for (i = 0; i < MIN (n_bytes, 16u); ++i)
			snprintf (buffer + i * 3, 4, "%02x ", src[i]);

		dprintf2("%p: %s\n", src, buffer);

		if (n_bytes < 16)
			break;

		src += 16;
		n_bytes -= 16;
	}
}
#endif

/**
 * @internal
 * @param field The field number (0 == first, 1 == second) will be
 *   stored here.
 * @param field_line The field line number or 0 == undefined will
 *   be stored here.
 * @param frame_line The frame line number or 0 == undefined will
 *   be stored here.
 * @param lofp line_offset / field_parity byte of the data unit
 *   in question.
 * @param system SYSTEM_525 or SYSTEM_625. Used to calculate the
 *   frame line number.
 *
 * Converts the line_offset / field_parity byte of a VBI data unit.
 * Caller must validate the line numbers.
 */
static void
lofp_to_line			(unsigned int *		field,
				 unsigned int *		field_line,
				 unsigned int *		frame_line,
				 unsigned int		lofp,
				 enum systems		system)
{
	unsigned int line_offset;

	/* field_parity */
	*field = !(lofp & (1 << 5));

	line_offset = lofp & 31;

	if (line_offset > 0) {
		static const unsigned int field_start [2][2] = {
			{ 0, 263 },
			{ 0, 313 },
		};

		*field_line = line_offset;
		*frame_line = field_start[system][*field] + line_offset;
	} else {
		/* EN 300 775 section 4.5.2: Unknown line. (This is
		   only permitted for Teletext data units.) */

		*field_line = 0;
		*frame_line = 0;
	}
}

/**
 * @internal
 * @param f VBI data unit decoding context.
 * @param spp A pointer to a vbi_sliced structure will be stored here.
 * @param rpp If not @c NULL, a pointer to a raw VBI line (720 bytes)
 *   will be stored here.
 * @param lofp line_offset / field_parity byte of the data unit
 *   in question.
 * @param system SYSTEM_525 or SYSTEM_625. Used to calculate frame
 *   line numbers.
 *
 * Decodes the line_offset / field_parity (lofp) byte of a VBI data
 * unit, allocating a vbi_sliced (and if requested raw VBI data)
 * slot where the data can be stored.
 *
 * Side effects: On success f->sp will be incremented, the field
 * number (0 == first, 1 == second) will be stored in f->last_field,
 * the field line number or 0 == undefined in f->last_field_line,
 * a frame line number or 0 == undefined in (*spp)->line. The
 * caller must validate the line numbers.
 *
 * @returns
 * - @c 0 Success.
 * - @c -1 A line number wrap-around has occurred, i.e. a new frame
 *   commences at the start of this PES packet. No slots have been
 *   allocated.
 * - @c VBI_ERR_SLICED_BUFFER_OVERFLOW
 *   - Insufficient space in the sliced VBI buffer.
 * - @c VBI_ERR_RAW_BUFFER_OVERFLOW
 *   - The line number is outside the area covered by the raw VBI buffer.
 * - @c VBI_ERR_DU_LINE_NUMBER
 *   - Duplicate line number.
 *   - Wrong order of line numbers.
 *   - Illegal line_offset.
 */
static int
line_address			(struct frame *		f,
				 vbi_sliced **		spp,
				 uint8_t **		rpp,
				 unsigned int		lofp,
				 enum systems		system)
{
	unsigned int field;
	unsigned int field_line;
	unsigned int frame_line;

	if (unlikely (f->sp >= f->sliced_end)) {
		debug1("Out of sliced VBI buffer space (%d lines)",
		       (int)(f->sliced_end - f->sliced_begin));

		return VBI_ERR_SLICED_BUFFER_OVERFLOW;
	}

	lofp_to_line (&field, &field_line, &frame_line,
		      lofp, system);

	dprintf3("Line %u/%u=%u\n",
		field, field_line, frame_line);

	/* EN 301 775 section 4.1: A VBI PES packet contains data of
	   one and only one video frame. (But multiple packets may
	   contain data of one frame. In particular I encountered
	   streams where the first and second field lines were
	   transmitted in separate successive packets.)

	   Section 4.1 and 4.5.2: Lines in a frame must be transmitted
	   in ascending order of line numbers, with no duplicates,
	   except for lines with line_offset 0 = undefined and monochrome
	   4:2:2 samples which are transmitted in multiple data units.

	   Section 4.5.2: The toggling of the field_parity flag
	   indicates a new field. (Actually this wouldn't work if
	   Teletext data for only one field is transmitted, or just
	   one line of Teletext, VPS, WSS or CC data. So we take
	   the line_offset into account as well.) */
	if (0 != frame_line) {
		if (frame_line <= f->last_frame_line) {
			if (f->n_data_units_extracted_from_packet > 0) {
				debug2("Illegal line order: %u <= %u",
					frame_line, f->last_frame_line);

				return VBI_ERR_DU_LINE_NUMBER;
			}

			if (frame_line < f->last_frame_line)
				return -1; /* new_frame */

			/* Not raw VBI or
			   first_segment_flag set? */
			if (NULL == rpp || (int8_t) lofp < 0)
				return -1; /* new_frame */
		}

		if (NULL != rpp) {
			unsigned int raw_start;
			unsigned int raw_end;
			unsigned int offset;

			raw_start = f->raw_start[field];
			raw_end = raw_start + f->raw_count[field];

			if (frame_line < raw_start
			    || frame_line >= raw_end) {
				debug7("Raw line %u/%u=%u outside "
					"sampling range %u ... %u, "
					"%u ... %u",
					field,
					field_line,
					frame_line,
					f->raw_start[0],
					f->raw_start[0] + f->raw_count[0],
					f->raw_start[1],
					f->raw_start[1] + f->raw_count[1]);

				return VBI_ERR_RAW_BUFFER_OVERFLOW;
			}

			offset = frame_line - raw_start;
			if (field > 0)
				offset += f->raw_count[0];

			*rpp = f->raw + offset * 720;
		}

		f->last_field = field;
		f->last_field_line = field_line;
		f->last_frame_line = frame_line;

		*spp = f->sp++;
		(*spp)->line = frame_line;
	} else {
		/* Undefined line. */

		if (NULL != rpp) {
			/* EN 301 775 section 4.9.2. */
			debug0("Illegal raw VBI line_offset=0");

			return VBI_ERR_DU_LINE_NUMBER;
		}

		if (0 == f->last_data_unit_id) {
			/* Nothing to do. */
		} else if (field != f->last_field) {
			if (0 == f->n_data_units_extracted_from_packet)
				return -1; /* new frame */

			if (unlikely (field < f->last_field)) {
				debug2("Illegal line order: %u/x <= %u/x",
					field, f->last_field);

				return VBI_ERR_DU_LINE_NUMBER;
			}
		}

		f->last_field = field;
		f->last_field_line = field_line;

		*spp = f->sp++;
		(*spp)->line = 0;
	}

	++f->n_data_units_extracted_from_packet;

	return 0; /* success */
}

static void
discard_raw			(struct frame *		f)
{
	dprintf0("Discarding raw VBI line\n");

	memset (f->rp, 0, 720);

	--f->sp;

	f->raw_offset = 0;
}

static int
demux_samples			(struct frame *		f,
				 const uint8_t *	p,
				 enum systems		system)
{
	unsigned int first_pixel_position;
	unsigned int n_pixels;

	first_pixel_position = p[3] * 256 + p[4];
	n_pixels = p[5];

	dprintf6(
		"Raw VBI data unit first_segment=%u last_segment=%u "
		"field_parity=%u line_offset=%u "
		"first_pixel_position=%u n_pixels=%u\n",
		!!(p[2] & (1 << 7)),
		!!(p[2] & (1 << 6)),
		!!(p[2] & (1 << 5)),
		p[2] & 0x1F,
		first_pixel_position,
		n_pixels);

	/* EN 301 775 section 4.9.1: first_pixel_position 0 ... 719,
	   n_pixels 1 ... 251. (n_pixels <= 251 has been checked by
	   caller.) */
	if (unlikely (0 == n_pixels || first_pixel_position >= 720)) {
		debug3("Illegal raw VBI segment size "
			"%u ... %u (%u pixels)",
			first_pixel_position,
			first_pixel_position + n_pixels,
			n_pixels);

		discard_raw (f);

		return VBI_ERR_DU_RAW_SEGMENT_POSITION;
	}

	/* first_segment_flag */
	if ((int8_t) p[2] < 0) {
		vbi_sliced *s;
		int err;

		if (unlikely (f->raw_offset > 0)) {
			s = f->sp - 1;

			dprintf2(
				"Raw VBI segment missing in "
				"line %u at offset %u\n",
				s->line, f->raw_offset);

			discard_raw (f);

			return VBI_ERR_DU_RAW_DATA_INCOMPLETE;
		}

		err = line_address (f, &s, &f->rp, p[2], system);
		if (unlikely (0 != err))
			return err;

		if (unlikely (f->last_field_line - 7 >= 24 - 7)) {
			--f->sp;

			debug1("Illegal raw VBI line_offset=%u",
				f->last_field_line);

			return VBI_ERR_DU_LINE_NUMBER;
		}

		s->id = (SYSTEM_525 == system) ?
			VBI_SLICED_VBI_525 : VBI_SLICED_VBI_625;
	} else {
		unsigned int field;
		unsigned int field_line;
		unsigned int frame_line;
		vbi_sliced *s;

		lofp_to_line (&field, &field_line, &frame_line,
			      p[2], system);

		if (unlikely (0 == f->raw_offset)) {
			/* Don't complain if we just jumped into the
			   stream or discarded the previous segments. */
			switch (f->last_data_unit_id) {
			case 0:
			case DATA_UNIT_ZVBI_MONOCHROME_SAMPLES_525:
			case DATA_UNIT_MONOCHROME_SAMPLES:
				return 0; /* success, skip the data unit */

			default:
				break;
			}

			dprintf2(
				"First raw VBI segment missing in "
				"line %u before offset %u\n",
				frame_line, first_pixel_position);

			return VBI_ERR_DU_RAW_SEGMENT_LOST;
		}

		s = f->sp - 1;

		/* EN 301 775 section 4.9.2. */
		if (unlikely (frame_line != s->line
			      || first_pixel_position != f->raw_offset)) {
			dprintf4(
				"Raw VBI segment(s) missing or "
				"out of order. Expected data for "
				"line %u offset %u, "
				"got line %u offset %u\n",
				s->line, f->raw_offset,
				frame_line, first_pixel_position);

			discard_raw (f);

			return VBI_ERR_DU_RAW_SEGMENT_LOST;
		}
	}

	/* EN 301 775 section 4.9 defines a video line as 720
	   luminance samples, but doesn't actually require the
	   transmission of exactly 720 samples starting at offset 0.
	   We discard any samples beyond offset 719. */
	n_pixels = MIN (n_pixels, 720 - first_pixel_position);

	memcpy (f->rp + first_pixel_position, p + 6, n_pixels);

	/* last_segment_flag */
	if (0 != (p[2] & (1 << 6))) {
		f->raw_offset = 0;
	} else {
		f->raw_offset = first_pixel_position + n_pixels;
	}

	return 0; /* success */
}

#if 0
static void
log_du_ttx			(struct frame *		f,
				 const vbi_sliced *	s)
{
	uint8_t buffer[43];
	unsigned int i;

	for (i = 0; i < 42; ++i)
		buffer[i] = _vbi_to_ascii (s->data[i]);
	buffer[i] = 0;

	dprintf2("DU-TTX %u >%s<\n", s->line, buffer);
}
#endif

/**
 * @internal
 * @param f VBI data unit decoding context.
 * @param src *src must point to the first byte (data_unit_id) of
 *   a VBI data unit. Initially this should be the first data unit
 *   in the PES packet, immediately after the data_indentifier byte.
 *   @a *src will be incremented by the size of the successfully
 *   converted data units, pointing to the end of the buffer on
 *   success, or the data_unit_id byte of the offending data unit
 *   on failure.
 * @param src_left *src_left is the number of bytes left in the
 *   @a src buffer. It will be decremented by the size of the
 *   successfully converted data units. When all data units in the
 *   buffer have been successfully converted it will be zero.
 *
 * Converts the data units in a VBI PES packet to vbi_sliced data
 * stored in f->sliced_begin (if not @c NULL) or raw VBI samples
 * stored in f->raw (if not @c NULL).
 *
 * The function skips over unknown data units, stuffing bytes and
 * data units which contain data which was not requested. It aborts
 * and returns an error code when it encounters a non-standard
 * conforming data unit. All errors are recoverable, you can just call
 * the function again, perhaps after calling _vbi_dvb_skip_data_unit().
 *
 * You must set f->n_data_units_extracted_from_packet to zero
 * at the beginning of a new PES packet.
 *
 * @returns
 * - @c 0 Success, next PES packet please.
 * - @c -1 A new frame commences at the start of this PES packet.
 *   No data units were extracted. Flush the output buffers, then
 *   call reset_frame() and then call this function again to convert
 *   the remaining data units.
 * - @c VBI_ERR_SLICED_BUFFER_OVERFLOW
 *   - Insufficient space in the sliced VBI buffer.
 * - @c VBI_ERR_RAW_BUFFER_OVERFLOW
 *   - The line number in the data unit is outside the area covered
 *     by the raw VBI buffer.
 * - @c VBI_ERR_DU_OVERFLOW
 *   - The data unit crosses the end of the buffer.
 * - @c VBI_ERR_DU_LENGTH
 *   - The data_unit_length is too small for the data this unit is
 *     supposed to contain.
 * - @c VBI_ERR_DU_LINE_NUMBER
 *   - Duplicate line number.
 *   - Wrong order of line numbers.
 *   - Illegal line_offset.
 * - @c VBI_ERR_DU_RAW_SEGMENT_POSITION
 *   - Illegal first_pixel_position or n_pixels field in a
 *     monochrome 4:2:2 samples data unit. (Only if raw VBI data
 *     was requested.)
 * - @c VBI_ERR_DU_RAW_SEGMENT_LOST
 *   - The first or following segments of a monochrome 4:2:2 samples
 *     line are missing or out of order. (Only if raw VBI data
 *     was requested.)
 * - @c VBI_ERR_DU_RAW_DATA_INCOMPLETE
 *   - The last segment of a monochrome 4:2:2 samples line is missing.
 *     (Only if raw VBI data was requested.) DO NOT SKIP over this
 *     data unit, it may be valid.
 *
 * @bugs
 * Raw VBI conversion is untested.
 */
static int
extract_data_units		(struct frame *		f,
				 const uint8_t **	src,
				 unsigned int *		src_left)
{
	const uint8_t *p;
	const uint8_t *p_end_m2;
	int err = 0;

	assert (*src_left >= 2);

	p = *src;
	p_end_m2 = p + *src_left
		- 2; /* data_unit_id, data_unit_length */

	while (p < p_end_m2) {
		unsigned int data_unit_id;
		unsigned int data_unit_length;
		vbi_sliced *s;
		unsigned int i;

		data_unit_id = p[0];
		data_unit_length = p[1];

		dprintf2(
			"data_unit_id=0x%02x data_unit_length=%u\n",
			data_unit_id, data_unit_length);

		/* Data units must not cross PES packet
		   boundaries, as is evident from
		   EN 301 775 table 1. */
		if (unlikely (p + data_unit_length > p_end_m2)) {
			err = VBI_ERR_DU_OVERFLOW;
			goto failed;
		}

		switch (data_unit_id) {
		case DATA_UNIT_STUFFING:
			break;

		case DATA_UNIT_EBU_TELETEXT_NON_SUBTITLE:
		case DATA_UNIT_EBU_TELETEXT_SUBTITLE:
			if (NULL == f->sliced_begin)
				break;

			if (unlikely (f->raw_offset > 0))
				goto raw_missing;

			if (unlikely (data_unit_length < 1 + 1 + 42))
				goto bad_length;

			/* FIXME */
			if (unlikely (0xE4 != p[3])) { /* vbi_rev8 (0x27) */
			        debug0("Libzvbi does not support "
					 "Teletext services with "
					 "custom framing code");
				break;
			}

			err = line_address (f, &s, /* rpp */ NULL,
					    p[2], SYSTEM_625);
			if (unlikely (0 != err))
				goto failed;

			if (unlikely (f->last_field_line > 0
				      && (f->last_field_line - 7
					  >= 23 - 7)))
				goto bad_line;

			/* XXX the data will always be in correct order,
			   but if line_offset is 0 (undefined) we cannot
			   pass the (always valid) field number. */
			s->id = VBI_SLICED_TELETEXT_B;

			for (i = 0; i < 42; ++i)
				s->data[i] = vbi_rev8 (p[4 + i]);

#if 0
			if (f->log.mask & VBI_LOG_DEBUG2)
				log_du_ttx (f, s);
#endif

			break;

		case DATA_UNIT_VPS:
			if (NULL == f->sliced_begin)
				break;

			if (unlikely (f->raw_offset > 0))
				goto raw_missing;

			if (unlikely (data_unit_length < 1 + 13))
				goto bad_length;

			err = line_address (f, &s, /* rpp */ NULL,
					    p[2], SYSTEM_625);
			if (unlikely (0 != err))
				goto failed;

			if (unlikely (16 != s->line))
				goto bad_line;

			s->id = (0 == f->last_field) ?
				VBI_SLICED_VPS : VBI_SLICED_VPS_F2;

			memcpy (s->data, p + 3, 13);

			break;

		case DATA_UNIT_WSS:
			if (NULL == f->sliced_begin)
				break;

			if (unlikely (f->raw_offset > 0))
				goto raw_missing;

			if (unlikely (data_unit_length < 1 + 2))
				goto bad_length;

			err = line_address (f, &s, /* rpp */ NULL,
					    p[2], SYSTEM_625);
			if (unlikely (0 != err))
				goto failed;

			if (unlikely (23 != s->line))
				goto bad_line;

			s->id = VBI_SLICED_WSS_625;

			s->data[0] = vbi_rev8 (p[3]);
			s->data[1] = vbi_rev8 (p[4]);

			break;

		case DATA_UNIT_ZVBI_WSS_CPR1204:
			if (NULL == f->sliced_begin)
				break;

			if (unlikely (f->raw_offset > 0))
				goto raw_missing;

			if (unlikely (data_unit_length < 1 + 3))
				goto bad_length;

			err = line_address (f, &s, /* rpp */ NULL,
					    p[2], SYSTEM_525);
			if (unlikely (0 != err))
				goto failed;

			s->id = VBI_SLICED_WSS_CPR1204;

			s->data[0] = p[3];
			s->data[1] = p[4];
			s->data[2] = p[5];

			break;

		case DATA_UNIT_ZVBI_CLOSED_CAPTION_525:
			if (NULL == f->sliced_begin)
				break;

			if (unlikely (f->raw_offset > 0))
				goto raw_missing;

			if (unlikely (data_unit_length < 1 + 2))
				goto bad_length;

			err = line_address (f, &s, /* rpp */ NULL,
					    p[2], SYSTEM_525);
			if (unlikely (0 != err))
				goto failed;

			s->id = (0 == f->last_field) ?
				VBI_SLICED_CAPTION_525_F1 :
				VBI_SLICED_CAPTION_525_F2;

			s->data[0] = vbi_rev8 (p[3]);
			s->data[1] = vbi_rev8 (p[4]);

			break;

		case DATA_UNIT_CLOSED_CAPTION:
			if (NULL == f->sliced_begin)
				break;

			if (unlikely (f->raw_offset > 0)) {
			raw_missing:
				s = f->sp - 1;

				dprintf2(
					"Raw VBI segment missing in "
					"line %u at offset %u\n",
					s->line, f->raw_offset);

				discard_raw (f);

				return VBI_ERR_DU_RAW_DATA_INCOMPLETE;
				goto failed;
			}

			if (unlikely (data_unit_length < 1 + 2)) {
			bad_length:
				debug2("data_unit_length=%u too small "
					"for data_unit_id=%u",
					data_unit_length, data_unit_id);

				err = VBI_ERR_DU_LENGTH;
				goto failed;
			}

			err = line_address (f, &s, /* rpp */ NULL,
					    p[2], SYSTEM_625);
			if (unlikely (0 != err))
				goto failed;

			if (unlikely (21 != s->line)) {
			bad_line:
				--f->sp;

				debug3("Illegal field_parity=%u or "
					"line_offset=%u for "
					"data_unit_id=%u",
					!f->last_field,
					f->last_field_line,
					data_unit_id);

				err = VBI_ERR_DU_LINE_NUMBER;
				goto failed;
			}

			s->id = (0 == f->last_field) ?
				VBI_SLICED_CAPTION_625_F1 :
				VBI_SLICED_CAPTION_625_F2;

			s->data[0] = vbi_rev8 (p[3]);
			s->data[1] = vbi_rev8 (p[4]);

			break;

		case DATA_UNIT_ZVBI_MONOCHROME_SAMPLES_525:
			if (NULL == f->raw)
				break;

			if (unlikely (data_unit_length
				      < (unsigned int)(1 + 2 + 1 + p[5])))
				goto bad_sample_length;

			err = demux_samples (f, p, SYSTEM_525);
			if (unlikely (0 != err))
				goto failed;

			break;

		case DATA_UNIT_MONOCHROME_SAMPLES:
			if (NULL == f->raw)
				break;

			if (unlikely (data_unit_length <
				      (unsigned int)(1 + 2 + 1 + p[5]))) {
			bad_sample_length:
				debug3("data_unit_length=%u too small "
					"for data_unit_id=%u with %u "
					"samples",
					data_unit_length,
					data_unit_id, p[5]);

				err = VBI_ERR_DU_LENGTH;
				goto failed;
			}

			/* Actually EN 301 775 section 4.9: "The data
			   is intended to be transcoded into the VBI
			   of either 525 or 625-line video." What's
			   that supposed to mean? */
			err = demux_samples (f, p, SYSTEM_625);
			if (unlikely (0 != err))
				goto failed;

			break;

		default:
			debug1("Unknown data_unit_id=%u", data_unit_id);
			break;
		}

		f->last_data_unit_id = data_unit_id;

		p += data_unit_length + 2;
	}

	*src = p;
	*src_left = 0;

	return 0; /* success */

 failed:
	/* Also called with err = -1 when a new frame begins in
	   this packet, before any data units were extracted. */

	*src_left = p_end_m2 + 2 - p;
	*src = p;

	return err;
}

/**
 * @internal
 * @param f VBI data unit decoding context.
 *
 * Reset the VBI data unit decoding context at the beginning of a
 * new frame (after extract_data_units() returned -1).
 */
static void
reset_frame			(struct frame *		f)
{
	f->sp = f->sliced_begin;

	/* Take a shortcut if no raw data was ever stored here. */
	if (f->rp > f->raw) {
		unsigned int n_lines;

		n_lines = f->raw_count[0] + f->raw_count[1];
		memset (f->raw, 0, n_lines * 720);
	}

	f->rp = f->raw;
	f->raw_offset = 0;

	f->last_field = 0;
	f->last_field_line = 0;
	f->last_frame_line = 0;

	f->last_data_unit_id = 0;

	f->n_data_units_extracted_from_packet = 0;
}

static vbi_bool
decode_timestamp		(vbi_dvb_demux *	dx,
				 int64_t *		pts,
				 unsigned int		mark,
				 const uint8_t *	p)
{
	unsigned int t;

	if (mark != (p[0] & 0xF1u)) {
		dprintf1("Invalid PTS/DTS byte[0]=0x%02x\n", p[0]);
		return FALSE;
	}

	t  = p[1] << 22;
	t |= (p[2] & ~1) << 14;
	t |= p[3] << 7;
	t |= p[4] >> 1;

#if 0
	if (dx->frame.log.mask & VBI_LOG_DEBUG) {
		int64_t old_pts;
		int64_t new_pts;

		old_pts = *pts;
		new_pts = t | (((int64_t) p[0] & 0x0E) << 29);

		dprintf3("TS%x 0x%" PRIx64 " (%+" PRId64 ")\n",
			mark, new_pts, new_pts - old_pts);
	}
#endif

	*pts = t | (((int64_t) p[0] & 0x0E) << 29);

	return TRUE;
}

static vbi_bool
valid_vbi_pes_packet_header	(vbi_dvb_demux *	dx,
				 const uint8_t *	p)
{
	unsigned int header_length;
	unsigned int data_identifier;

	/* PES_header_data_length [8] */
	header_length = p[8];

	dprintf2("PES_header_length=%u (%s)\n",
		header_length,
		(36 == header_length) ? "ok" : "bad");

	/* EN 300 472 section 4.2: Must be 0x24. */
	if (36 != header_length)
		return FALSE;

	data_identifier = p[9 + 36];

	/* data_identifier (EN 301 775 section 4.3.2) */
	switch (data_identifier) {
	case 0x10 ... 0x1F:
	case 0x99 ... 0x9B:
		dprintf1("data_identifier=%u (ok)\n",
			data_identifier);
		break;

	default:
		dprintf1("data_identifier=%u (bad)\n",
			data_identifier);
		return FALSE;
	}

	/* '10',
	   PES_scrambling_control [2] == '00' (not scrambled),
	   PES_priority,
	   data_alignment_indicator == '1' (data unit
	     starts immediately after header),
	   copyright,
	   original_or_copy */
	if (0x84 != (p[6] & 0xF4)) {
		dprintf1("Invalid PES header byte[6]=0x%02x\n",
			p[6]);
		return FALSE;
	}

	/* PTS_DTS_flags [2],
	   ESCR_flag,
	   ES_rate_flag,
	   DSM_trick_mode_flag,
	   additional_copy_info_flag,
	   PES_CRC_flag,
	   PES_extension_flag */
	switch (p[7] >> 6) {
	case 2:	/* PTS 0010 xxx 1 ... */
		if (!decode_timestamp (dx, &dx->packet_pts, 0x21, p + 9))
			return FALSE;
		break;

	case 3:	/* PTS 0011 xxx 1 ... DTS ... */
		if (!decode_timestamp (dx, &dx->packet_pts, 0x31, p + 9))
			return FALSE;
		break;

	default:
		/* EN 300 472 section 4.2: a VBI PES packet [...]
		   always carries a PTS. (But we don't need one
		   if this packet continues the previous frame.) */
		dprintf0("PTS missing in PES header\n");

		/* XXX make this optional to handle broken sources. */
		if (dx->new_frame)
			return FALSE;

		break;
	}

	/* FIXME if this is not the first packet of a frame, and a PTS
	   is present, check if we lost any packets. */

	return TRUE;
}

/**
 * @internal
 * @param dx DVB demultiplexer context.
 * @param src *src must point to the first data unit
 *   in the PES packet, immediately after the data_indentifier byte.
 *   @a *src will be incremented by the size of the successfully
 *   converted data units, pointing to the end of the buffer on
 *   success, or the data_unit_id byte of the offending data unit
 *   on failure.
 * @param src_left *src_left is the number of bytes left in the
 *   @a src buffer. It will be decremented by the size of the
 *   successfully converted data units. When all data units in the
 *   buffer have been successfully converted it will be zero.
 *
 * Converts the data units in a VBI PES packet to vbi_sliced data
 * (if dx->frame.sliced_begin != @c NULL) or raw VBI samples (if
 * dx->frame.raw != @c NULL). When a frame is complete, the function
 * calls dx->callback.
 *
 * You must set f->n_data_units_extracted_from_packet to zero
 * at the beginning of a new PES packet.
 *
 * @returns
 * - @c 0 Success, next PES packet please.
 * - @c VBI_ERR_CALLBACK
 *   - A frame is complete, but dx->callback == @c NULL (not an error,
 *     see vbi_dvb_demux_cor() coroutine) or the callback function
 *     returned @c FALSE. No data units were extracted from this
 *     PES packet. This error is recoverable. Just call this function
 *     again with the returned @a src and @a src_left values to
 *     continue after the failed call.
 * - @c VBI_ERR_SLICED_BUFFER_OVERFLOW
 *   - Insufficient space in the sliced VBI buffer. This error is
 *     recoverable; Make room and call this function again with the
 *     returned @a src and @a src_left values to continue where it
 *     left off.
 * - @c VBI_ERR_RAW_BUFFER_OVERFLOW
 *   - The line number in the data unit is outside the area covered
 *     by the raw VBI buffer. (Only if raw VBI data was requested.)
 *     This error is recoverable. Make room or skip the data unit with
 *     _vbi_dvb_skip_data_unit(), then call again as above.
 * - @c Error in the VBI_ERR_STREAM_SYNTAX range
 *   - The data unit is broken. This error is recoverable. You can
 *     skip the data unit and call again as above.
 *
 * To discard a PES packet after an error occurred, call again with
 * a new @a src pointer. To discard all data units collected before
 * the error occurred, set dx->new_frame to TRUE. VBI_ERR_CALLBACK
 * implies that @a src points to a new PES packet and dx->new_frame
 * will be already TRUE.
 */
static int
demux_pes_packet_frame		(vbi_dvb_demux *	dx,
				 const uint8_t **	src,
				 unsigned int *		src_left)
{
	for (;;) {
		int err;

		if (dx->new_frame) {
			/* New frame commences in this packet. */

			reset_frame (&dx->frame);

			dx->frame_pts = dx->packet_pts;
			dx->new_frame = FALSE;
		}

		err = extract_data_units (&dx->frame, src, src_left);

		if (likely (err >= 0)) {
			/* Data unit extraction successful, packet
			   continues a previous frame, or an error
			   occurred and *src points at the offending
			   data unit. */
			return err;
		}

		dprintf0("New frame\n");

		/* A new frame commences in this packet. We must
		   flush dx->frame before we extract data units from
		   this packet. */

		dx->new_frame = TRUE;

		//if (NULL == dx->callback)
			return VBI_ERR_CALLBACK;

#if 0
		unsigned n_lines = dx->frame.sp - dx->frame.sliced_begin;

		if (!dx->callback (dx,
				   dx->user_data,
				   dx->frame.sliced_begin,
				   n_lines,
				   dx->frame_pts)) {
			return VBI_ERR_CALLBACK;
		}
#endif
	}

	assert (0);
	return 0;
}

/**
 * @internal
 * @param src *src points to DVB PES data, will be incremented by the
 *   number of bytes read from the buffer. This pointer need not align
 *   with PES packet boundaries.
 * @param src_left *src_left is the number of bytes left in @a src
 *   buffer, will be decremented by the number of bytes read.
 *   *src_left need not align with PES packet boundaries.
 *
 * DVB VBI demultiplexer coroutine for MPEG-2 Packetized Elementary
 * Streams. This function extracts data units from DVB VBI PES packets
 * and calls dx->callback when a frame is complete.
 *
 * The function silently discards all VBI PES packets which contain
 * non-standard conforming data. dx->callback (or the caller if
 * dx->callback == @c NULL) must examine the passed PTS
 * (or dx->frame_PTS) to detect data loss.
 *
 * @returns
 * - @c 0 Success, need more data. *src_left will be zero.
 * - @c VBI_ERR_CALLBACK
 *   - A frame is complete, but dx->callback == @c NULL or the callback
 *     function returned @c FALSE. This error is recoverable. Just
 *     call the function again with the returned @a src and @a
 *     src_left values to continue after the failed call.
 */
static int
demux_pes_packet		(vbi_dvb_demux *	dx,
				 const uint8_t **	src,
				 unsigned int *		src_left)
{
	const uint8_t *s;
	unsigned int s_left;
	int err = 0;

	s = *src;
	s_left = *src_left;

	for (;;) {
		const uint8_t *p;
		const uint8_t *scan_begin;
		const uint8_t *scan_end;
		unsigned int packet_length;

		if (!wrap_around (&dx->pes_wrap,
				  &p, &scan_end,
				  &s, &s_left, *src_left))
			break; /* out of data */

		/* Data units */

		if (dx->pes_wrap.lookahead > PES_HEADER_LOOKAHEAD) {
			unsigned int left;

			/* We have a new PES packet in the wrap-around
			   buffer. p points just after the data_identifier
			   byte and lookahead is >= packet length minus
			   header and data_identifier byte. */
			left = dx->pes_wrap.lookahead;

			dx->frame.n_data_units_extracted_from_packet = 0;

			err = demux_pes_packet_frame (dx, &p, &left);

			if (VBI_ERR_CALLBACK == err) {
				goto failed;
			} else if (unlikely (err < 0)) {
				/* For compatibility with older
				   versions just discard the data
				   collected so far for this frame. */
				dx->new_frame = TRUE;
			}

			/* Skip this packet and request enough data
			   to look at the next PES header. */
			dx->pes_wrap.skip = dx->pes_wrap.lookahead;
			dx->pes_wrap.lookahead = PES_HEADER_LOOKAHEAD;

			continue;
		}

		/* Start code scan */

		scan_begin = p;

		for (;;) {
			/* packet_start_code_prefix [24] == 0x000001,
			   stream_id [8] == PRIVATE_STREAM_1 */

			dprintf4("packet_start_code=%02x%02x%02x%02x\n",
				p[0], p[1], p[2], p[3]);

			if (p[2] & ~1) {
				/* Not 000001 or xx0000 or xxxx00. */
				p += 3;
			} else if (0 != (p[0] | p[1]) || 1 != p[2]) {
				++p;
			} else if (PRIVATE_STREAM_1 == p[3]) {
				break;
			} else if (p[3] < 0xBC) {
				++p;
			} else {
				/* ISO/IEC 13818-1 Table 2-19 stream_id
				   assignments: 0xBC ... 0xFF. */

				/* XXX We shouldn't take this shortcut
				   unless we're sure this is a PES packet
				   header and not some random junk, so we
				   don't miss any data. */

				packet_length = p[4] * 256 + p[5];

				/* Not a VBI PES packet, skip it. */
				dx->pes_wrap.skip = (p - scan_begin)
					+ 6 + packet_length;

				goto outer_continue;
			}

			if (unlikely (p >= scan_end)) {
				/* Start code not found within
				   lookahead bytes. Skip the data and
				   request more. */
				dx->pes_wrap.skip = p - scan_begin;
				goto outer_continue;
			}
		}

		/* Packet header */

		packet_length = p[4] * 256 + p[5];

		dprintf1("PES_packet_length=%u\n",
			packet_length);

		/* Skip this PES packet if the following checks fail. */
		dx->pes_wrap.skip = (p - scan_begin) + 6 + packet_length;

		/* EN 300 472 section 4.2: N x 184 - 6. (We'll read
		   46 bytes without further checks and need at least
		   one data unit to function properly, be that all
		   stuffing bytes.) */
		if (packet_length < 178)
			continue;

		if (!valid_vbi_pes_packet_header (dx, p))
			continue;

		/* Habemus packet. Skip all data up to the header,
		   the PES packet header itself and the data_identifier
		   byte. Request access to the payload bytes. */
		dx->pes_wrap.skip = (p - scan_begin) + 9 + 36 + 1;
		dx->pes_wrap.lookahead = packet_length - 3 - 36 - 1;

 outer_continue:
		;
	}

	*src = s;
	*src_left = s_left;

	return 0; /* need more data */

 failed:
	*src = s;
	*src_left = s_left;

	return err;
}

/**
 * @brief DVB VBI demux coroutine.
 * @param dx DVB demultiplexer context allocated with vbi_dvb_pes_demux_new().
 * @param sliced Demultiplexed sliced data will be stored here.
 * @param max_lines At most this number of sliced lines will be stored
 *   at @a sliced.
 * @param pts If not @c NULL the Presentation Time Stamp associated with the
 *   first line of the demultiplexed frame will be stored here.
 * @param buffer *buffer points to DVB PES data, will be incremented by the
 *   number of bytes read from the buffer. This pointer need not align with
 *   packet boundaries.
 * @param buffer_left *buffer_left is the number of bytes left in @a buffer,
 *   will be decremented by the number of bytes read. *buffer_left need not
 *   align with packet size. The packet filter works faster with larger
 *   buffers. When you read from an MPEG file, mapping the file into memory
 *   and passing pointers to the mapped data will be fastest.
 *
 * This function consumes an arbitrary number of bytes from a DVB
 * Packetized Elementary Stream (PES), filters
 * out PRIVATE_STREAM_1 PES packets, filters out valid VBI data units,
 * converts them to vbi_sliced format and stores the sliced data at
 * @a sliced.
 *
 * You must not call this function when you passed a callback function to
 * vbi_dvb_pes_demux_new(). Call vbi_dvb_demux_feed() instead.
 *
 * @returns
 * When a frame is complete, the function returns the number of elements
 * stored in the @a sliced array. When more data is needed (@a
 * *buffer_left is zero) or an error occurred it returns the value zero.
 *
 * @bug
 * Demultiplexing of raw VBI data is not supported yet,
 * raw data will be discarded.
 *
 * @since 0.2.10
 */
unsigned int
vbi_dvb_demux_cor		(vbi_dvb_demux *	dx,
				 vbi_sliced *		sliced,
				 unsigned int 		max_lines,
				 int64_t *		pts,
				 const uint8_t **	buffer,
				 unsigned int *		buffer_left)
{
	assert (NULL != dx);
	assert (NULL != sliced);
	assert (NULL != buffer);
	assert (NULL != buffer_left);

	/* FIXME in future version:
	   buffer_left ought to be an unsigned long. */

	/* Doesn't work with TS, and isn't safe in any case. */
	/* dx->frame.sliced_begin = sliced;
	   dx->frame.sliced_end = sliced + max_lines; */

	if (0 != demux_pes_packet (dx, buffer, buffer_left)) {
		unsigned int n_lines;

		if (pts)
			*pts = dx->frame_pts;

		n_lines = dx->frame.sp - dx->frame.sliced_begin;
		n_lines = MIN (n_lines, max_lines); /* XXX error msg */

		if (n_lines > 0) {
			memcpy (sliced, dx->frame.sliced_begin,
				n_lines * sizeof (*sliced));

			dx->frame.sp = dx->frame.sliced_begin;
		}

		return n_lines;
	}

	return 0; /* need more data */
}

/**
 * @brief Resets DVB VBI demux.
 * @param dx DVB demultiplexer context allocated with vbi_dvb_pes_demux_new().
 *
 * Resets the DVB demux to the initial state as after vbi_dvb_pes_demux_new(),
 * useful for example after a channel change.
 *
 * @since 0.2.10
 */
void
vbi_dvb_demux_reset		(vbi_dvb_demux *	dx)
{
	assert (NULL != dx);

	CLEAR (dx->pes_wrap);

	dx->pes_wrap.buffer = dx->pes_buffer;
	dx->pes_wrap.bp = dx->pes_buffer;

	dx->pes_wrap.lookahead = PES_HEADER_LOOKAHEAD;

	CLEAR (dx->ts_wrap);

	dx->ts_wrap.buffer = dx->ts_buffer;
	dx->ts_wrap.bp = dx->ts_buffer;

	dx->ts_wrap.lookahead = TS_SYNC_SEARCH_LOOKAHEAD;

	CLEAR (dx->frame);

	dx->frame.sliced_begin = dx->sliced;
	dx->frame.sliced_end = dx->sliced + N_ELEMENTS (dx->sliced);

	dx->frame.sp = dx->sliced;

	/* Raw data ignored for now. */

	dx->frame_pts = 0;
	dx->packet_pts = 0;

	dx->new_frame = TRUE;

	dx->ts_in_sync = FALSE;

	dx->ts_frame_bp = NULL;
	dx->ts_frame_todo = 0;

	dx->ts_pes_bp = NULL;
	dx->ts_pes_todo = 0;

	dx->ts_continuity = -1; /* unknown */
}

/**
 * @brief Deletes DVB VBI demux.
 * @param dx DVB demultiplexer context allocated with
 *   vbi_dvb_pes_demux_new(), can be @c NULL.
 *
 * Frees all resources associated with @a dx.
 *
 * @since 0.2.10
 */
void
vbi_dvb_demux_delete		(vbi_dvb_demux *	dx)
{
	if (NULL == dx)
		return;

	CLEAR (*dx);

	xfree (dx);		
}

/**
 * @brief Allocates DVB VBI demux.
 *
 * Allocates a new DVB VBI (EN 301 472, EN 301 775) demultiplexer taking
 * a PES stream as input.
 *
 * @returns
 * Pointer to newly allocated DVB demux context which must be
 * freed with vbi_dvb_demux_delete() when done. @c NULL on failure
 * (out of memory).
 *
 * @since 0.2.10
 */
vbi_dvb_demux *
vbi_dvb_pes_demux_new(void)
{
	vbi_dvb_demux *dx;

	dx = xmalloc (sizeof (*dx));
	if (NULL == dx) {
		errno = ENOMEM;
		return NULL;
	}

	memset (dx, 0, sizeof(*dx));

	vbi_dvb_demux_reset (dx);

	return dx;
}

#endif /* USE_LIBZVBI */
