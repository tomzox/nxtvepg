/*
 *  Network codes and name tables
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
 *  $Id: cni_tables.h,v 1.4 2002/01/13 18:46:04 tom Exp tom $
 */

#ifndef __CNI_TABLES_H
#define __CNI_TABLES_H

const char * CniGetDescription( uint cni, const char ** ppCountry );
uint CniConvertP8301ToVps( uint cni );
uint CniConvertPdcToVps( uint cni );
bool CniIsKnownProvider( uint cni );

#endif  //__CNI_TABLES_H