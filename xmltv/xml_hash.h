/*
 *  Helper module implementing a hash array for strings
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

#ifndef __XML_HASH_H
#define __XML_HASH_H


typedef void * XML_HASH_PTR;
typedef void * XML_HASH_PAYLOAD;
typedef void (* XML_HASH_FREE_CB)( XML_HASH_PTR pHash, char * pPayload );
typedef bool (* XML_HASH_ENUM_CB)( XML_HASH_PTR pHash, void * pData,
                                   const char * pKeyStr, XML_HASH_PAYLOAD pPayload );

// ----------------------------------------------------------------------------
// Function interface

XML_HASH_PAYLOAD XmlHash_SearchEntry( XML_HASH_PTR pHash, const char * pStr );
XML_HASH_PAYLOAD XmlHash_CreateEntry( XML_HASH_PTR pHash, const char * pStr, bool * pIsNew );
const char * XmlHash_Enum( XML_HASH_PTR pHashRef, XML_HASH_ENUM_CB pCb, void * pData );

void XmlHash_Destroy( XML_HASH_PTR pHash, XML_HASH_FREE_CB pCb );
XML_HASH_PTR XmlHash_Init( void );

#endif /* __XML_HASH_H */
