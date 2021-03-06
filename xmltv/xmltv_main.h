/*
 *  XMLTV main module
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

#ifndef __XMLTV_MAIN_H
#define __XMLTV_MAIN_H

EPGDB_RELOAD_RESULT Xmltv_CheckHeader( const char * pFilename );
EPGDB_CONTEXT * Xmltv_Load( FILE * fp, uint provCni, const char * pProvName, time_t mtime, bool isPeek );
// FIXME CC pErrCode should be EPGDB_RELOAD_RESULT*
EPGDB_CONTEXT * Xmltv_CheckAndLoad( const char * pFilename, uint provCni, bool isPeek, uint * pErrCode, time_t * pMtime );
const char * Xmltv_TranslateErrorCode( uint detection );
bool Xmltv_IsXmlDocument( uint detection );

#endif // __XMLTV_MAIN_H
