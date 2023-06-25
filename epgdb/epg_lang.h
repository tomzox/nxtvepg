/*
 *  Nextview EPG block database
 *
 *  Copyright (C) 2023 T. Zoerner
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
 *  Description:
 *
 *    Defines language codes imported from "lang" attribute in XMLTV.
 */

#ifndef __EPG_LANG_H
#define __EPG_LANG_H

typedef uint16_t EPG_LANG_CODE;

#define EPG_SET_LANG(C1,C2)     ((EPG_LANG_CODE)(((C1)<<8)|(C2)))
#define EPG_LANG_DE             EPG_SET_LANG('D','E')
#define EPG_LANG_FR             EPG_SET_LANG('F','R')
#define EPG_LANG_EN             EPG_SET_LANG('E','N')
#define EPG_LANG_IT             EPG_SET_LANG('I','T')
#define EPG_LANG_PL             EPG_SET_LANG('P','L')
#define EPG_LANG_UNKNOWN        0

#endif // __EPG_LANG_H
