/*
 *  libzvbi - Raw vbi decoder
 *
 *  Copyright (C) 2000, 2001, 2002 Michael H. Schimek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef ZVBIDECODER_H
#define ZVBIDECODER_H

typedef int vbi_bool;
#define static_inline static inline

#undef ABS
#define ABS(n)								\
({									\
	register int _n = n, _t = _n;					\
									\
	_t >>= sizeof(_t) * 8 - 1;					\
	_n ^= _t;							\
	_n -= _t;							\
})

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

/*
#ifdef HAVE_CONFIG_H
#  include "../config.h"
#endif

#include "bcd.h"
#include "sliced.h"
*/

/* Public */

/* Bit slicer */

/* Attn: keep this in sync with rte, don't change order */
typedef enum {
	VBI_PIXFMT_YUV420 = 1,
	VBI_PIXFMT_YUYV,
	VBI_PIXFMT_YVYU,
	VBI_PIXFMT_UYVY,
	VBI_PIXFMT_VYUY,
	VBI_PIXFMT_RGBA32_LE = 32,
	VBI_PIXFMT_RGBA32_BE,
	VBI_PIXFMT_BGRA32_LE,
	VBI_PIXFMT_BGRA32_BE,
	VBI_PIXFMT_ABGR32_BE = 32, /* synonyms */
	VBI_PIXFMT_ABGR32_LE,
	VBI_PIXFMT_ARGB32_BE,
	VBI_PIXFMT_ARGB32_LE,
	VBI_PIXFMT_RGB24,
	VBI_PIXFMT_BGR24,
	VBI_PIXFMT_RGB16_LE,
	VBI_PIXFMT_RGB16_BE,
	VBI_PIXFMT_BGR16_LE,
	VBI_PIXFMT_BGR16_BE,
	VBI_PIXFMT_RGBA15_LE,
	VBI_PIXFMT_RGBA15_BE,
	VBI_PIXFMT_BGRA15_LE,
	VBI_PIXFMT_BGRA15_BE,
	VBI_PIXFMT_ARGB15_LE,
	VBI_PIXFMT_ARGB15_BE,
	VBI_PIXFMT_ABGR15_LE,
	VBI_PIXFMT_ABGR15_BE
} vbi_pixfmt;

/* Private */

#define VBI_PIXFMT_BPP(fmt)						\
	(((fmt) == VBI_PIXFMT_YUV420) ? 1 :				\
	 (((fmt) >= VBI_PIXFMT_RGBA32_LE				\
	   && (fmt) <= VBI_PIXFMT_BGRA32_BE) ? 4 :			\
	  (((fmt) == VBI_PIXFMT_RGB24					\
	    || (fmt) == VBI_PIXFMT_BGR24) ? 3 : 2)))

/* Public */

/**
 * @ingroup Rawdec
 * @brief Modulation used for VBI data transmission.
 */
typedef enum {
	/**
	 * The data is 'non-return to zero' coded, logical '1' bits
	 * are described by high sample values, logical '0' bits by
	 * low values. The data is last significant bit first transmitted.
	 */
	VBI_MODULATION_NRZ_LSB,
	/**
	 * 'Non-return to zero' coded, most significant bit first
	 * transmitted.
	 */
	VBI_MODULATION_NRZ_MSB,
	/**
	 * The data is 'bi-phase' coded. Each data bit is described
	 * by two complementary signalling elements, a logical '1'
	 * by a sequence of '10' elements, a logical '0' by a '01'
	 * sequence. The data is last significant bit first transmitted.
	 */
	VBI_MODULATION_BIPHASE_LSB,
	/**
	 * 'Bi-phase' coded, most significant bit first transmitted.
	 */
	VBI_MODULATION_BIPHASE_MSB
} vbi_modulation;

/**
 * @ingroup Rawdec
 * @brief Bit slicer context.
 *
 * The contents of this structure are private,
 * use vbi_bit_slicer_init() to initialize.
 */
typedef struct vbi_bit_slicer {
	vbi_bool	(* func)(struct vbi_bit_slicer *slicer,
				 uint8_t *raw, uint8_t *buf);
	unsigned int	cri;
	unsigned int	cri_mask;
	int		thresh;
	int		cri_bytes;
	int		cri_rate;
	int		oversampling_rate;
	int		phase_shift;
	int		step;
	unsigned int	frc;
	int		frc_bits;
	int		payload;
	int		endian;
	int		skip;
} vbi_bit_slicer;

/**
 * @addtogroup Rawdec
 * @{
 */
static_inline vbi_bool
vbi_bit_slice(vbi_bit_slicer *slicer, uint8_t *raw, uint8_t *buf)
{
	return slicer->func(slicer, raw, buf);
}
/** @} */

/**
 * @ingroup Rawdec
 * @brief Raw vbi decoder context.
 *
 * Only the sampling parameters are public. See
 * vbi_raw_decoder_parameters() and vbi_raw_decoder_add_services()
 * for usage.
 */
struct _vbi_raw_decoder_job {
       unsigned int		id;
       int			offset;
       vbi_bit_slicer		slicer;
};
typedef struct vbi_raw_decoder {
	/* Sampling parameters */

	/**
	 * Either 525 (M/NTSC, M/PAL) or 625 (PAL, SECAM), describing the
	 * scan line system all line numbers refer to.
	 */
	int			scanning;
	/**
	 * Format of the raw vbi data.
	 */
	vbi_pixfmt		sampling_format;
	/**
	 * Sampling rate in Hz, the number of samples or pixels
	 * captured per second.
	 */
	int			sampling_rate;		/* Hz */
	/**
	 * Number of samples or pixels captured per scan line,
	 * in bytes. This determines the raw vbi image width and you
	 * want it large enough to cover all data transmitted in the line (with
	 * headroom).
	 */
	int			bytes_per_line;
	/**
	 * The distance from 0H (leading edge hsync, half amplitude point)
	 * to the first sample (pixel) captured, in samples (pixels). You want
	 * an offset small enough not to miss the start of the data
	 * transmitted.
	 */
	int			offset;			/* 0H, samples */
	/**
	 * First scan line to be captured, first and second field
	 * respectively, according to the ITU-R line numbering scheme
	 * (see vbi_sliced). Set to zero if the exact line number isn't
	 * known.
	 */
	int			start[2];		/* ITU-R numbering */
	/**
	 * Number of scan lines captured, first and second
	 * field respectively. This can be zero if only data from one
	 * field is required. The sum @a count[0] + @a count[1] determines the
	 * raw vbi image height.
	 */
	int			count[2];		/* field lines */
	/**
	 * In the raw vbi image, normally all lines of the second
	 * field are supposed to follow all lines of the first field. When
	 * this flag is set, the scan lines of first and second field
	 * will be interleaved in memory. This implies @a count[0] and @a count[1]
	 * are equal.
	 */
	vbi_bool		interlaced;
	/**
	 * Fields must be stored in temporal order, i. e. as the
	 * lines have been captured. It is assumed that the first field is
	 * also stored first in memory, however if the hardware cannot reliable
	 * distinguish fields this flag shall be cleared, which disables
	 * decoding of data services depending on the field number.
	 */
	vbi_bool		synchronous;

	/*< private >*/

	/*pthread_mutex_t		mutex;*/

	unsigned int		services;
	int			num_jobs;

	int8_t *		pattern;
	struct _vbi_raw_decoder_job jobs[8];
} vbi_raw_decoder;

/**
 * @anchor VBI_SLICED_
 * No data service, blank vbi_sliced structure.
 */
#define VBI_SLICED_NONE			0
#define VBI_SLICED_TELETEXT_B_L10_625	0x00000001
#define VBI_SLICED_TELETEXT_B_L25_625	0x00000002
/**
 * Teletext System B.
 *
 * Note this is separated into Level 1.0 and Level 2.5+ since the latter
 * permits occupation of PAL/SECAM scan line 6 which is frequently out of
 * range of raw VBI capture drivers. Clients should request decoding of both,
 * may then verify Level 2.5 is covered. Also sliced data can be tagged
 * as both Level 1.0 and 2.5+, i. e. VBI_SLICED_TELETEXT_B.
 *
 * Reference: <a href="http://www.etsi.org">ETS 300 706
 * "Enhanced Teletext specification"</a>.
 *
 * vbi_sliced payload: Last 42 of the 45 byte Teletext packet, that is
 * without clock run-in and framing code, lsb first transmitted.
 */
#define VBI_SLICED_TELETEXT_B		(VBI_SLICED_TELETEXT_B_L10_625 | VBI_SLICED_TELETEXT_B_L25_625)
/**
 * Video Program System.
 *
 * Reference: <a href="http://www.etsi.org">ETS 300 231
 * "Specification of the domestic video Programme Delivery Control system (PDC)"
 * </a>.
 *
 * vbi_sliced payload: Byte number 3 to 15 according to Figure 9,
 * lsb first transmitted.
 */
#define VBI_SLICED_VPS			0x00000004

#define VBI_SLICED_VPS_F2               0x00001000

#define VBI_SLICED_CAPTION_625_F1       0x00000008
#define VBI_SLICED_CAPTION_625_F2       0x00000010
#define VBI_SLICED_CAPTION_625          (VBI_SLICED_CAPTION_625_F1 | \
                                         VBI_SLICED_CAPTION_625_F2)
#define VBI_SLICED_CAPTION_525_F1       0x00000020
#define VBI_SLICED_CAPTION_525_F2       0x00000040
#define VBI_SLICED_CAPTION_525          (VBI_SLICED_CAPTION_525_F1 | \
                                         VBI_SLICED_CAPTION_525_F2)
#define VBI_SLICED_WSS_625              0x00000400
#define VBI_SLICED_WSS_CPR1204          0x00000800
#define VBI_SLICED_VBI_625              0x20000000
#define VBI_SLICED_VBI_525              0x40000000


/**
 * @ingroup Sliced
 * @brief This structure holds one scan line of sliced vbi data.
 *
 * For example the contents of NTSC line 21, two bytes of Closed Caption
 * data. Usually an array of vbi_sliced is used, covering all
 * VBI lines of the two fields of a video frame.
 */
typedef struct {
	/**
	 * A @ref VBI_SLICED_ symbol identifying the data service. Under cirumstances
	 * (see VBI_SLICED_TELETEXT_B) this can be a set of VBI_SLICED_ symbols.
	 */
	uint32_t		id;
	/**
	 * Source line number according to the ITU-R line numbering scheme,
	 * a value of @c 0 if the exact line number is unknown. Note that some
	 * data services cannot be reliable decoded without line number.
	 *
	 * @image html zvbi_625.gif "ITU-R PAL/SECAM line numbering scheme"
	 * @image html zvbi_525.gif "ITU-R NTSC line numbering scheme"
	 */
	uint32_t		line;
	/**
	 * The actual payload. See the documentation of @ref VBI_SLICED_ symbols
	 * for details.
	 */
	uint8_t			data[56];
} vbi_sliced;
/**
 * @addtogroup Rawdec
 * @{
 */
extern unsigned int	vbi_raw_decoder_add_services(vbi_raw_decoder *rd,
						     unsigned int services,
						     int strict);
extern int		vbi_raw_decode(vbi_raw_decoder *rd, uint8_t *raw, vbi_sliced *out);

extern void             vbi_raw_decoder_destroy(vbi_raw_decoder *rd);
/** @} */

/* Private */
bool ZvbiSliceAndProcess( vbi_raw_decoder *rd, uint8_t *raw, uint32_t frame_no );

#endif /* ZVBIDECODER_H */
