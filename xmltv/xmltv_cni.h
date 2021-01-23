/*
 *  XMLTV country and network identification mapping
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
 *
 *  Author: Tom Zoerner
 *
 *  $Id: xmltv_cni.h,v 1.4 2009/03/29 18:19:00 tom Exp tom $
 */

#ifndef __XMLTV_CNI_H
#define __XMLTV_CNI_H

// ----------------------------------------------------------------------------
// Context for creating a provider CNI table
//
#include "xmltv/xml_hash.h"

typedef struct
{
   uint         provCni;
   uint         freeCni;
   XML_HASH_PTR nameHash;
} XMLTV_CNI_CTX;

typedef struct
{
   char * pName;
   uint cni;
} XMLTV_CNI_REV_MAP;

typedef struct
{
   XMLTV_CNI_REV_MAP * pMap;
   uint         maxMapLen;
   uint         mapLen;
} XMLTV_CNI_REV_CTX;

// ----------------------------------------------------------------------------
// Interface functions
//
void XmltvCni_MapInit( XMLTV_CNI_CTX * pCniCtx, uint provCni,
                       const char * pSourceName, const char * pSourceUrl );
void XmltvCni_MapDestroy( XMLTV_CNI_CTX * pCniCtx );
uint XmltvCni_MapNetCni( XMLTV_CNI_CTX * pCniCtx, const char * pChannelId );

uint XmltvCni_MapProvider( const char * pXmlPath );
const char * XmltvCni_LookupProviderPath( uint provCni );

void XmltvCni_Destroy( void );
void XmltvCni_Init( void );

#endif // __XMLTV_CNI_H
