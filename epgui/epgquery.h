/*
 *  Process query message for remote EPG client
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
 *  $Id: epgquery.h,v 1.2 2008/10/12 20:00:29 tom Exp tom $
 */

#ifndef _EPGQUERY_H
#define _EPGQUERY_H

/* interface to command line parser */
bool EpgQuery_CheckSyntax( const char * pArg, char ** pErrStr );

/* interface to dump control */
FILTER_CONTEXT * EpgQuery_Parse( EPGDB_CONTEXT * pDbContext, const char ** pQuery );


#endif  // _EPGQUERY_H

