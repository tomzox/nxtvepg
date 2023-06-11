/*
 *  Helper module to map theme strings to PDC codes
 *
 *  Copyright (C) 2007-2011, 2020-2021 T. Zoerner
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
 *  Description: see C source file.
 */

#ifndef __XMLTV_THEMES_H
#define __XMLTV_THEMES_H


typedef struct
{
   uchar        cat;
   uchar        theme;
} HASHED_THEMES;


void Xmltv_ParseThemeStringGerman( HASHED_THEMES * pCache, const char * pStr );
void Xmltv_ParseThemeStringFrench( HASHED_THEMES * pCache, const char * pStr );
void Xmltv_ParseThemeStringEnglish( HASHED_THEMES * pCache, const char * pStr );
void Xmltv_ParseThemeStringPolish( HASHED_THEMES * pCache, const char * pStr );

#endif /* __XMLTV_THEMES_H */
