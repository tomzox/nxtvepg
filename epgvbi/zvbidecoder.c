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

/* ZVBI #Id: decoder.c,v 1.7 2002/10/22 04:42:40 mschimek Exp # */

/* nxtvepg $Id: zvbidecoder.c,v 1.1 2003/04/12 13:59:00 tom Exp tom $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "zvbidecoder.h"

#ifdef ZVBI_DECODER
/**
 * @addtogroup Rawdec Raw VBI decoder
 * @ingroup Raw
 * @brief Converting raw VBI samples to bits and bytes.
 *
 * The libzvbi already offers hardware interfaces to obtain sliced
 * VBI data for further processing. However if you want to write your own
 * interface or decode data services not covered by libzvbi you can use
 * these lower level functions.
 */

/*
 *  Bit Slicer
 */

#define OVERSAMPLING 4		/* 1, 2, 4, 8 */
#define THRESH_FRAC 9

/*
 * Note this is just a template. The code is inlined,
 * with bpp and endian being const.
 *
 * This function translates from the image format to
 * plain bytes, with linear interpolation of samples.
 * Could be further improved with a lowpass filter.
 */
static inline unsigned int
sample(uint8_t *raw, int offs, int bpp, int endian)
{
	unsigned char frac = offs;
	int raw0, raw1;

	switch (bpp) {
	case 14: /* 1:5:5:5 LE/BE */
		raw += (offs >> 8) * 2;
		raw0 = (raw[0 + endian] + raw[1 - endian] * 256) & 0x07C0;
		raw1 = (raw[2 + endian] + raw[3 - endian] * 256) & 0x07C0;
		return (raw1 - raw0) * frac + (raw0 << 8);

	case 15: /* 5:5:5:1 LE/BE */
		raw += (offs >> 8) * 2;
		raw0 = (raw[0 + endian] + raw[1 - endian] * 256) & 0x03E0;
		raw1 = (raw[2 + endian] + raw[3 - endian] * 256) & 0x03E0;
		return (raw1 - raw0) * frac + (raw0 << 8);

	case 16: /* 5:6:5 LE/BE */
		raw += (offs >> 8) * 2;
		raw0 = (raw[0 + endian] + raw[1 - endian] * 256) & 0x07E0;
		raw1 = (raw[2 + endian] + raw[3 - endian] * 256) & 0x07E0;
		return (raw1 - raw0) * frac + (raw0 << 8);

	default: /* 8 (intermediate bytes skipped by caller) */
		raw += (offs >> 8) * bpp;
		return (raw[bpp] - raw[0]) * frac + (raw[0] << 8);
	}
}

/*
 * Note this is just a template. The code is inlined,
 * with bpp being const.
 */
static inline vbi_bool
bit_slicer_tmpl(vbi_bit_slicer *d, uint8_t *raw,
		uint8_t *buf, int bpp, int endian)
{
	unsigned int i, j, k;
	unsigned int cl = 0, thresh0 = d->thresh, tr;
	unsigned int c = 0, t;
	unsigned char b, b1 = 0;
	int raw0, raw1, mask;

	raw += d->skip;

	if (bpp == 14)
		mask = 0x07C0;
	else if (bpp == 15)
		mask = 0x03E0;
	else if (bpp == 16)
		mask = 0x07E0;

	for (i = d->cri_bytes; i > 0; raw += (bpp >= 14 && bpp <= 16) ? 2 : bpp, i--) {
		if (bpp >= 14 && bpp <= 16) {
			raw0 = (raw[0 + endian] + raw[1 - endian] * 256) & mask;
			raw1 = (raw[2 + endian] + raw[3 - endian] * 256) & mask;
			tr = d->thresh >> THRESH_FRAC;
			d->thresh += ((raw0 - tr) * (int) ABS(raw1 - raw0)) >>
				((bpp == 15) ? 2 : 3);
			t = raw0 * OVERSAMPLING;
		} else {
			tr = d->thresh >> THRESH_FRAC;
			d->thresh += ((int) raw[0] - tr) * (int) ABS(raw[bpp] - raw[0]);
			t = raw[0] * OVERSAMPLING;
		}

		for (j = OVERSAMPLING; j > 0; j--) {
			b = ((t + (OVERSAMPLING / 2)) / OVERSAMPLING >= tr);

    			if (b ^ b1) {
				cl = d->oversampling_rate >> 1;
			} else {
				cl += d->cri_rate;

				if (cl >= (unsigned int) d->oversampling_rate) {
					cl -= d->oversampling_rate;

					c = c * 2 + b;

					if ((c & d->cri_mask) == d->cri) {
						i = d->phase_shift;
						tr *= 256;
						c = 0;

						for (j = d->frc_bits; j > 0; j--) {
							c = c * 2 + (sample(raw, i, bpp, endian) >= tr);
    							i += d->step;
						}

						if (c ^= d->frc)
							return FALSE;

						/* CRI/FRC found, now get the
						   payload and exit */
						
						switch (d->endian) {
						case 3:
							for (j = 0; j < (unsigned int) d->payload; j++) {
					    			c >>= 1;
								c += (sample(raw, i, bpp, endian) >= tr) << 7;
			    					i += d->step;

								if ((j & 7) == 7)
									*buf++ = c;
					    		}

							*buf = c >> ((8 - d->payload) & 7);
							break;

						case 2:
							for (j = 0; j < (unsigned int) d->payload; j++) {
								c = c * 2 + (sample(raw, i, bpp, endian) >= tr);
			    					i += d->step;

								if ((j & 7) == 7)
									*buf++ = c;
					    		}

							*buf = c & ((1 << (d->payload & 7)) - 1);
							break;

						case 1:
							for (j = d->payload; j > 0; j--) {
								for (k = 0; k < 8; k++) {
						    			c >>= 1;
									c += (sample(raw, i, bpp, endian) >= tr) << 7;
			    						i += d->step;
								}

								*buf++ = c;
					    		}

							break;

						case 0:
							for (j = d->payload; j > 0; j--) {
								for (k = 0; k < 8; k++) {
									c = c * 2 + (sample(raw, i, bpp, endian) >= tr);
			    						i += d->step;
								}

								*buf++ = c;
					    		}

							break;
						}

			    			return TRUE;
					}
				}
			}

			b1 = b;

			if (OVERSAMPLING > 1) {
				if (bpp >= 14 && bpp <= 16) {
					t += raw1;
					t -= raw0;
				} else {
					t += raw[bpp];
					t -= raw[0];
				}
			}
		}
	}

	d->thresh = thresh0;

	return FALSE;
}

static vbi_bool
bit_slicer_1(vbi_bit_slicer *d, uint8_t *raw, uint8_t *buf)
{
	return bit_slicer_tmpl(d, raw, buf, 1, 0);
}

static vbi_bool
bit_slicer_2(vbi_bit_slicer *d, uint8_t *raw, uint8_t *buf)
{
	return bit_slicer_tmpl(d, raw, buf, 2, 0);
}

static vbi_bool
bit_slicer_3(vbi_bit_slicer *d, uint8_t *raw, uint8_t *buf)
{
	return bit_slicer_tmpl(d, raw, buf, 3, 0);
}

static vbi_bool
bit_slicer_4(vbi_bit_slicer *d, uint8_t *raw, uint8_t *buf)
{
	return bit_slicer_tmpl(d, raw, buf, 4, 0);
}

static vbi_bool
bit_slicer_1555_le(vbi_bit_slicer *d, uint8_t *raw, uint8_t *buf)
{
	return bit_slicer_tmpl(d, raw, buf, 14, 0);
}

static vbi_bool
bit_slicer_5551_le(vbi_bit_slicer *d, uint8_t *raw, uint8_t *buf)
{
	return bit_slicer_tmpl(d, raw, buf, 15, 0);
}

static vbi_bool
bit_slicer_565_le(vbi_bit_slicer *d, uint8_t *raw, uint8_t *buf)
{
	return bit_slicer_tmpl(d, raw, buf, 16, 0);
}

static vbi_bool
bit_slicer_1555_be(vbi_bit_slicer *d, uint8_t *raw, uint8_t *buf)
{
	return bit_slicer_tmpl(d, raw, buf, 14, 1);
}

static vbi_bool
bit_slicer_5551_be(vbi_bit_slicer *d, uint8_t *raw, uint8_t *buf)
{
	return bit_slicer_tmpl(d, raw, buf, 15, 1);
}

static vbi_bool
bit_slicer_565_be(vbi_bit_slicer *d, uint8_t *raw, uint8_t *buf)
{
	return bit_slicer_tmpl(d, raw, buf, 16, 1);
}

/**
 * @param slicer Pointer to vbi_bit_slicer object to be initialized. 
 * @param raw_samples Number of samples or pixels in one raw vbi line
 *   later passed to vbi_bit_slice(). This limits the number of
 *   bytes read from the sample buffer.
 * @param sampling_rate Raw vbi sampling rate in Hz, that is the number of
 *   samples or pixels sampled per second by the hardware. 
 * @param cri_rate The Clock Run In is a NRZ modulated
 *   sequence of '0' and '1' bits prepending most data transmissions to
 *   synchronize data acquisition circuits. This parameter gives the CRI bit
 *   rate in Hz, that is the number of CRI bits transmitted per second.
 * @param bit_rate The transmission bit rate of all data bits following the CRI
 *   in Hz.
 * @param cri_frc The FRaming Code usually following the CRI is a bit sequence
 *   identifying the data service, and per libzvbi definition modulated
 *   and transmitted at the same bit rate as the payload (however nothing
 *   stops you from counting all nominal CRI and FRC bits as CRI).
 *   The bit slicer compares the bits in this word, lsb last transmitted,
 *   against the transmitted CRI and FRC. Decoding of payload starts
 *   with the next bit after a match.
 * @param cri_mask Of the CRI bits in @c cri_frc, only these bits are
 *   actually significant for a match. For instance it is wise
 *   not to rely on the very first CRI bits transmitted. Note this
 *   mask is not shifted left by @a frc_bits.
 * @param cri_bits 
 * @param frc_bits Number of CRI and FRC bits in @a cri_frc, respectively.
 *   Their sum is limited to 32.
 * @param payload Number of payload <em>bits</em>. Only this data
 *   will be stored in the vbi_bit_slice() output. If this number
 *   is no multiple of eight, the most significant bits of the
 *   last byte are undefined.
 * @param modulation Modulation of the vbi data, see vbi_modulation.
 * @param fmt Format of the raw data, see vbi_pixfmt.
 * 
 * Initializes vbi_bit_slicer object. Usually you will not use this
 * function but vbi_raw_decode(), the vbi image decoder which handles
 * all these details.
 */
static bool
vbi_bit_slicer_init(vbi_bit_slicer *slicer,
		    int raw_samples, int sampling_rate,
		    int cri_rate, int bit_rate,
		    unsigned int cri_frc, unsigned int cri_mask,
		    int cri_bits, int frc_bits, int payload,
		    vbi_modulation modulation, vbi_pixfmt fmt)
{
	unsigned int c_mask = (unsigned int)(-(cri_bits > 0)) >> (32 - cri_bits);
	unsigned int f_mask = (unsigned int)(-(frc_bits > 0)) >> (32 - frc_bits);
	int gsh = 0;

	slicer->func = bit_slicer_1;

	switch (fmt) {
	case VBI_PIXFMT_RGB24:
	case VBI_PIXFMT_BGR24:
	        slicer->func = bit_slicer_3;
		slicer->skip = 1;
		break;

	case VBI_PIXFMT_RGBA32_LE:
	case VBI_PIXFMT_BGRA32_LE:
		slicer->func = bit_slicer_4;
		slicer->skip = 1;
		break;

	case VBI_PIXFMT_RGBA32_BE:
	case VBI_PIXFMT_BGRA32_BE:
		slicer->func = bit_slicer_4;
		slicer->skip = 2;
		break;

	case VBI_PIXFMT_RGB16_LE:
	case VBI_PIXFMT_BGR16_LE:
		slicer->func = bit_slicer_565_le;
		gsh = 3; /* (green << 3) & 0x07E0 */
		slicer->skip = 0;
		break;

	case VBI_PIXFMT_RGBA15_LE:
	case VBI_PIXFMT_BGRA15_LE:
		slicer->func = bit_slicer_5551_le;
		gsh = 2; /* (green << 2) & 0x03E0 */
		slicer->skip = 0;
		break;

	case VBI_PIXFMT_ARGB15_LE:
	case VBI_PIXFMT_ABGR15_LE:
		slicer->func = bit_slicer_1555_le;
		gsh = 3; /* (green << 2) & 0x07C0 */
		slicer->skip = 0;
		break;

	case VBI_PIXFMT_RGB16_BE:
	case VBI_PIXFMT_BGR16_BE:
		slicer->func = bit_slicer_565_be;
		gsh = 3; /* (green << 3) & 0x07E0 */
		slicer->skip = 0;
		break;

	case VBI_PIXFMT_RGBA15_BE:
	case VBI_PIXFMT_BGRA15_BE:
		slicer->func = bit_slicer_5551_be;
		gsh = 2; /* (green << 2) & 0x03E0 */
		slicer->skip = 0;
		break;

	case VBI_PIXFMT_ARGB15_BE:
	case VBI_PIXFMT_ABGR15_BE:
		slicer->func = bit_slicer_1555_be;
		gsh = 3; /* (green << 2) & 0x07C0 */
		slicer->skip = 0;
		break;

	case VBI_PIXFMT_YUV420:
		slicer->func = bit_slicer_1;
		slicer->skip = 0;
		break;

	case VBI_PIXFMT_YUYV:
	case VBI_PIXFMT_YVYU:
		slicer->func = bit_slicer_2;
		slicer->skip = 0;
		break;

	case VBI_PIXFMT_UYVY:
	case VBI_PIXFMT_VYUY:
		slicer->func = bit_slicer_2;
		slicer->skip = 1;
		break;

	default:
		debug1("vbi_bit_slicer_init: unknown pixfmt %d", fmt);
		return FALSE;
	}

	slicer->cri_mask		= cri_mask & c_mask;
	slicer->cri		 	= (cri_frc >> frc_bits) & slicer->cri_mask;
	/* We stop searching for CRI/FRC when the payload
	   cannot possibly fit anymore. */
	slicer->cri_bytes		= raw_samples
		- ((long long) sampling_rate * (payload + frc_bits)) / bit_rate;
	slicer->cri_rate		= cri_rate;
	/* Raw vbi data is oversampled to account for low sampling rates. */
	slicer->oversampling_rate	= sampling_rate * OVERSAMPLING;
	/* 0/1 threshold */
	slicer->thresh			= 105 << (THRESH_FRAC + gsh);
	slicer->frc			= cri_frc & f_mask;
	slicer->frc_bits		= frc_bits;
	/* Payload bit distance in 1/256 raw samples. */
	slicer->step			= (int)(sampling_rate * 256.0 / bit_rate);

	if (payload & 7) {
		slicer->payload	= payload;
		slicer->endian	= 3;
	} else {
		slicer->payload	= payload >> 3;
		slicer->endian	= 1;
	}

	switch (modulation) {
	case VBI_MODULATION_NRZ_MSB:
		slicer->endian--;
	case VBI_MODULATION_NRZ_LSB:
		slicer->phase_shift = (int)
			(sampling_rate * 256.0 / cri_rate * .5
			 + sampling_rate * 256.0 / bit_rate * .5 + 128);
		break;

	case VBI_MODULATION_BIPHASE_MSB:
		slicer->endian--;
	case VBI_MODULATION_BIPHASE_LSB:
		/* Phase shift between the NRZ modulated CRI and the rest */
		slicer->phase_shift = (int)
			(sampling_rate * 256.0 / cri_rate * .5
			 + sampling_rate * 256.0 / bit_rate * .25 + 128);
		break;
	}
        return TRUE;
}

/*
 * Data Service Table
 */

#define MAX_JOBS		(sizeof(((vbi_raw_decoder *) 0)->jobs)	\
				 / sizeof(((vbi_raw_decoder *) 0)->jobs[0]))
#define MAX_WAYS		8

struct vbi_service_par {
	unsigned int	id;		/* VBI_SLICED_ */
	char *		label;
	int		first[2];	/* scanning lines (ITU-R), max. distribution; */
	int		last[2];	/*  zero: no data from this field, requires field sync */
	int		offset;		/* leading edge hsync to leading edge first CRI one bit
					    half amplitude points, nanoseconds */
	int		cri_rate;	/* Hz */
	int		bit_rate;	/* Hz */
	int		scanning;	/* scanning system: 525 (FV = 59.94 Hz, FH = 15734 Hz),
							    625 (FV = 50 Hz, FH = 15625 Hz) */
	unsigned int	cri_frc;	/* Clock Run In and FRaming Code, LSB last txed bit of FRC */
	unsigned int	cri_mask;	/* cri bits significant for identification, */
	char		cri_bits;
	char		frc_bits;	/* cri_bits at cri_rate, frc_bits at bit_rate */
	short		payload;	/* in bits */
	char		modulation;	/* payload modulation */
};

const struct vbi_service_par
vbi_services[] = {
	{
		VBI_SLICED_TELETEXT_B_L10_625, "Teletext System B Level 1.5, 625",
		{ 7, 320 },
		{ 22, 335 },
		10300, 6937500, 6937500, /* 444 x FH */
		625, 0x00AAAAE4, ~0, 10, 6, 42 * 8, VBI_MODULATION_NRZ_LSB
	}, {
		VBI_SLICED_TELETEXT_B, "Teletext System B, 625",
		{ 6, 318 },
		{ 22, 335 },
		10300, 6937500, 6937500, /* 444 x FH */
		625, 0x00AAAAE4, ~0, 10, 6, 42 * 8, VBI_MODULATION_NRZ_LSB
	}, {
		VBI_SLICED_VPS, "Video Programming System",
		{ 16, 0 },
		{ 16, 0 },
		12500, 5000000, 2500000, /* 160 x FH */
		625, 0xAAAA8A99, ~0, 24, 0, 13 * 8, VBI_MODULATION_BIPHASE_MSB
	},
	{ 0 }
};


#ifndef DECODER_PATTERN_DUMP
#define DECODER_PATTERN_DUMP 0
#endif

/**
 * @param rd Initialized vbi_raw_decoder structure.
 * @param raw A raw vbi image as defined in the vbi_raw_decoder structure
 *   (rd->sampling_format, rd->bytes_per_line, rd->count[0] + rd->count[1]
 *    scan lines).
 * @param out Buffer to store the decoded vbi_sliced data. Since every
 *   vbi scan line may contain data, this must be an array of vbi_sliced
 *   with the same number of entries as scan lines in the raw image
 *   (rd->count[0] + rd->count[1]).
 * 
 * Decode a raw vbi image, consisting of several scan lines of raw vbi data,
 * into sliced vbi data. The output is sorted by line number.
 * 
 * Note this function attempts to learn which lines carry which data
 * service, or none, to speed up decoding. You should avoid using the same
 * vbi_raw_decoder structure for different sources.
 *
 * @bug This function ignores the sampling_format field in struct
 * vbi_raw_decoder, always assuming VBI_PIXFMT_YUV420.
 *
 * @return
 * The number of lines decoded, i. e. the number of vbi_sliced records
 * written.
 */
/* XXX bit_slicer_1() */
int
vbi_raw_decode(vbi_raw_decoder *rd, uint8_t *raw, vbi_sliced *out)
{
	static int readj = 1;
	int pitch = rd->bytes_per_line << rd->interlaced;
	int8_t *pat, *pattern = rd->pattern;
	uint8_t *raw1 = raw;
	vbi_sliced *out1 = out;
	struct _vbi_raw_decoder_job *job;
	int i, j;

	//pthread_mutex_lock(&rd->mutex);

	if (!rd->services) {
		//pthread_mutex_unlock(&rd->mutex);
		return 0;
	}

	for (i = 0; i < rd->count[0] + rd->count[1]; i++) {
		if (rd->interlaced && i == rd->count[0])
			raw = raw1 + rd->bytes_per_line;

		if (DECODER_PATTERN_DUMP) {
			fprintf(stderr, "L%02d ", i);
			for (j = 0; j < MAX_WAYS; j++)
				if (pattern[j] < 1 || pattern[j] > 8)
					fprintf(stderr, "0x%02x       ",
						pattern[j] & 0xFF);
				else
					fprintf(stderr, "0x%08x ",
						rd->jobs[pattern[j] - 1].id);
			fprintf(stderr, "\n");
		}

		for (pat = pattern;; pat++) {
			j = *pat; /* data service n, blank 0, or counter -n */

			if (j > 0) {
				job = rd->jobs + (j - 1);

				if (!bit_slicer_1(&job->slicer, raw + job->offset, out->data))
					continue; /* no match, try next data service */

				/* Positive match, output decoded line */

				out->id = job->id;

				if (i >= rd->count[0])
					out->line = (rd->start[1] > 0) ? rd->start[1] - rd->count[0] + i : 0;
				else
					out->line = (rd->start[0] > 0) ? rd->start[0] + i : 0;
				out++;

				/* Predict line as non-blank, force testing for
				   data services in the next 128 frames */
				pattern[MAX_WAYS - 1] = -128;

			} else if (pat == pattern) {
				/* Line was predicted as blank, once in 16
				   frames look for data services */
				if (readj == 0) {
					j = pattern[0];
					memmove (&pattern[0], &pattern[1],
						 sizeof (*pattern) * (MAX_WAYS - 1));
					pattern[MAX_WAYS - 1] = j;
				}

				break;
			} else if ((j = pattern[MAX_WAYS - 1]) < 0) {
				/* Increment counter, when zero predict line as
				   blank and stop looking for data services */
				pattern[MAX_WAYS - 1] = j + 1;
    				break;
			} else {
				/* found nothing, j = 0 */
			}

			/* Try the found data service first next time */
			*pat = pattern[0];
			pattern[0] = j;

			break; /* line done */
		}

		raw += pitch;
		pattern += MAX_WAYS;
	}

	readj = (readj + 1) & 15;

	//pthread_mutex_unlock(&rd->mutex);

	return out - out1;
}

/**
 * @param rd Initialized vbi_raw_decoder structure.
 * @param services Set of @ref VBI_SLICED_ symbols.
 * @param strict A value of 0, 1 or 2 requests loose, reliable or strict
 *  matching of sampling parameters. For example if the data service
 *  requires knowledge of line numbers while they are not known, @c 0
 *  will accept the service (which may work if the scan lines are
 *  populated in a non-confusing way) but @c 1 or @c 2 will not. If the
 *  data service <i>may</i> use more lines than are sampled, @c 1 will
 *  accept but @c 2 will not. If unsure, set to @c 1.
 * 
 * After you initialized the sampling parameters in @a rd (according to
 * the abilities of your raw vbi source), this function adds one or more
 * data services to be decoded. The libzvbi raw vbi decoder can decode up
 * to eight data services in parallel. You can call this function while
 * already decoding, it does not change sampling parameters and you must
 * not change them either after calling this.
 * 
 * @return
 * Set of @ref VBI_SLICED_ symbols describing the data services that actually
 * will be decoded. This excludes those services not decodable given
 * the sampling parameters in @a rd.
 */
unsigned int
vbi_raw_decoder_add_services(vbi_raw_decoder *rd, unsigned int services, int strict)
{
	double off_min = (rd->scanning == 525) ? 7.9e-6 : 8.0e-6;
	int row[2], count[2], way;
	struct _vbi_raw_decoder_job *job;
	int8_t *pattern;
	int i, j, k;

	//pthread_mutex_lock(&rd->mutex);

	if (!rd->pattern)
		rd->pattern = (int8_t *) calloc((rd->count[0] + rd->count[1])
						* MAX_WAYS, sizeof(rd->pattern[0]));

	for (i = 0; vbi_services[i].id; i++) {
		double signal;
		int skip = 0;

		if (rd->num_jobs >= (int) MAX_JOBS)
			break;

		if (!(vbi_services[i].id & services))
			continue;

		if (vbi_services[i].scanning != rd->scanning)
			goto eliminate;

		signal = vbi_services[i].cri_bits / (double) vbi_services[i].cri_rate
			 + (vbi_services[i].frc_bits + vbi_services[i].payload)
			   / (double) vbi_services[i].bit_rate;

		if (rd->offset > 0 && strict > 0) {
			double offset = rd->offset / (double) rd->sampling_rate;
			double samples_end = (rd->offset + rd->bytes_per_line)
					     / (double) rd->sampling_rate;

			if (offset > (vbi_services[i].offset / 1e9 - 0.5e-6)) {
                                debug4("skipping service 0x%08X: H-Off %d = %f > %f", vbi_services[i].id, rd->offset, offset, (vbi_services[i].offset / 1e9 - 0.5e-6));
				goto eliminate;
                        }

			if (samples_end < (vbi_services[i].offset / 1e9
					   + signal + 0.5e-6)) {
                                debug1("skipping service 0x%08X: sampling window too short", vbi_services[i].id);
				goto eliminate;
                        }

			if (offset < off_min) /* skip colour burst */
				skip = (int)(off_min * rd->sampling_rate);
		} else {
			double samples = rd->bytes_per_line
				         / (double) rd->sampling_rate;

			if (samples < (signal + 1.0e-6)) {
                                debug1("skipping service 0x%08X: not enough samples", vbi_services[i].id);
				goto eliminate;
                        }
		}

		for (j = 0, job = rd->jobs; j < rd->num_jobs; job++, j++) {
			unsigned int id = job->id | vbi_services[i].id;

			if ((id & ~(VBI_SLICED_TELETEXT_B_L10_625 | VBI_SLICED_TELETEXT_B_L25_625)) == 0)
				break;
			/*
			 *  General form implies the special form. If both are
			 *  available from the device, decode() will set both
			 *  bits in the id field for the respective line. 
			 */
		}

		for (j = 0; j < 2; j++) {
			int start = rd->start[j];
			int end = start + rd->count[j] - 1;

			if (!rd->synchronous) {
                                debug1("skipping service 0x%08X: not sync'ed", vbi_services[i].id);
				goto eliminate; /* too difficult */
                        }

			if (!(vbi_services[i].first[j] && vbi_services[i].last[j])) {
				count[j] = 0;
				continue;
			}

			if (rd->count[j] == 0) {
                                debug1("skipping service 0x%08X: zero count", vbi_services[i].id);
				goto eliminate;
                        }

			if (rd->start[j] > 0 && strict > 0) {
				/*
				 *  May succeed if not all scanning lines
				 *  available for the service are actually used.
				 */
				if (strict > 1
				    || (vbi_services[i].first[j] ==
					vbi_services[i].last[j]))
					if (start > vbi_services[i].first[j] ||
					    end < vbi_services[i].last[j]) {
                                                debug5("skipping service 0x%08X: lines not available have %d-%d, need %d-%d", vbi_services[i].id, start, end, vbi_services[i].first[j], vbi_services[i].last[j]);
						goto eliminate;
                                        }

				row[j] = MAX(0, (int) vbi_services[i].first[j] - start);
				count[j] = MIN(end, vbi_services[i].last[j]) - (start + row[j]) + 1;  /* XXX TZ FIX */
			} else {
				row[j] = 0;
				count[j] = rd->count[j];
			}

			row[1] += rd->count[0];

			for (pattern = rd->pattern + row[j] * MAX_WAYS, k = count[j];
			     k > 0; pattern += MAX_WAYS, k--) {
				int free = 0;

				for (way = 0; way < MAX_WAYS; way++)
					free += (pattern[way] <= 0
						 || ((pattern[way] - 1)
						     == job - rd->jobs));

				if (free <= 1) { /* reserve one NULL way */
                                        debug1("skipping service 0x%08X: no more patterns free", vbi_services[i].id);
					goto eliminate;
                                }
			}
		}

		for (j = 0; j < 2; j++) {
			for (pattern = rd->pattern + row[j] * MAX_WAYS, k = count[j];
			     k > 0; pattern += MAX_WAYS, k--) {
				for (way = 0; pattern[way] > 0
				      && (pattern[way] - 1) != (job - rd->jobs); way++);

                                assert((pattern + MAX_WAYS - rd->pattern) <= (rd->count[0] + rd->count[1]) * MAX_WAYS);

				pattern[way] = (job - rd->jobs) + 1;
				pattern[MAX_WAYS - 1] = -128;
			}
                }

		job->id |= vbi_services[i].id;
		job->offset = skip;

		if (vbi_bit_slicer_init(&job->slicer,
                                        rd->bytes_per_line - skip, // XXX * bpp?
                                        rd->sampling_rate,
                                        vbi_services[i].cri_rate,
                                        vbi_services[i].bit_rate,
                                        vbi_services[i].cri_frc,
                                        vbi_services[i].cri_mask,
                                        vbi_services[i].cri_bits,
                                        vbi_services[i].frc_bits,
                                        vbi_services[i].payload,
                                        vbi_services[i].modulation,
                                        rd->sampling_format) == FALSE)
                {
                   rd->services = 0;
                   break;
                }

		if (job >= rd->jobs + rd->num_jobs)
			rd->num_jobs++;

		rd->services |= vbi_services[i].id;
eliminate:
		;
	}

	//pthread_mutex_unlock(&rd->mutex);

	return rd->services;
}

#endif  // ZVBI_DECODER
