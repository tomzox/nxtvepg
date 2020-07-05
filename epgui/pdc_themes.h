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
 *  $Id: pdc_themes.h,v 1.6 2020/06/17 19:32:20 tom Exp tom $
 */

#ifndef __PDC_THEMES_H
#define __PDC_THEMES_H


#define PDC_THEME_LAST  0x80

// ----------------------------------------------------------------------------
// Declaration of service interface functions
//
void PdcThemeSetLanguage( uchar lang );
const char * PdcThemeGet( uchar theme );
const char * PdcThemeGetByLang( uchar theme, uchar lang );
const char * PdcThemeGetWithGeneral( uchar theme, const char ** pGeneralStr, bool withUndef );
bool PdcThemeIsDefined( uchar theme );
uchar PdcThemeGetCategory( uchar theme );

#endif //__PDC_THEMES_H
