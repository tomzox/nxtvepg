/////////////////////////////////////////////////////////////////////////////
// DebugLog.h
/////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2000-2002 Quenotte  All rights reserved.
/////////////////////////////////////////////////////////////////////////////
//
//  This file is subject to the terms of the GNU General Public License as
//  published by the Free Software Foundation.  A copy of this license is
//  included with this software distribution in the file COPYING.  If you
//  do not have a copy, you may obtain a copy by writing to the Free
//  Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//
//  This software is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details
/////////////////////////////////////////////////////////////////////////////
// nxtvepg $Id: debuglog.h,v 1.2 2002/05/06 11:42:58 tom Exp tom $
/////////////////////////////////////////////////////////////////////////////

#ifndef __DEBUGLOG_H__
#define __DEBUGLOG_H__

void LOG(int DebugLevel, char * Format, ...);
void HwDrv_SetLogging( BOOL dsdrvLog );

#endif
