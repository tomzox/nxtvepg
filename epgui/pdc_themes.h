/*
 *  PDC themes definition
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2 as
 *  published by the Free Software Foundation. You find a copy of this
 *  license in the file COPYRIGHT in the root directory of this release.
 *
 *  THIS PROGRAM IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL,
 *  BUT WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED WARRANTY OF
 *  MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *
 *  Description: see according C source file.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: pdc_themes.h,v 1.3 2001/02/25 16:03:47 tom Exp tom $
 */

#ifndef __PDC_THEMES_H
#define __PDC_THEMES_H


extern const uchar * const pdc_themes[];
extern const uchar * const pdc_series;
extern const uchar * const pdc_undefined_theme;
extern const uchar         pdc_categories[];

uchar PdcThemeGetCategory( uchar theme );

#endif //__PDC_THEMES_H
