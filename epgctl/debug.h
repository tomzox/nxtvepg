/*
 *  Debug service module
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
 *  Description: see below.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: debug.h,v 1.9 2001/05/06 17:30:35 tom Exp tom $
 */

#ifndef __DEBUG_H
#define __DEBUG_H

#include <stdio.h>

#if DEBUG_GLOBAL_SWITCH != ON
# ifdef DEBUG_SWITCH
#  undef DEBUG_SWITCH
# endif
# define DEBUG_SWITCH OFF
#endif

#if DEBUG_SWITCH == ON
// assert() declares preconditions, e.g. for function arguments
// if the conditions are not met by the actual parameters, execution is halted
#define assert(X) {if(!(X)) {sprintf(debugStr,"assertion (" #X ") failed in %s, line %d\n",__FILE__,__LINE__);DebugLogLine(TRUE);}}
// same as above, but with a constant negative result
#define SHOULD_NOT_BE_REACHED {sprintf(debugStr,"branch should not have been reached in %s, line %d\n",__FILE__,__LINE__);DebugLogLine(TRUE);}
#else
#define assert(X)
#define SHOULD_NOT_BE_REACHED
#endif

// report error conditions to stdout and the log file
#if DEBUG_SWITCH == ON
#define debug0(S)               {sprintf(debugStr,S " in %s, line %d\n",__FILE__,__LINE__);DebugLogLine(FALSE);}
#define debug1(S,A)             {sprintf(debugStr,S " in %s, line %d\n",(A),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define debug2(S,A,B)           {sprintf(debugStr,S " in %s, line %d\n",(A),(B),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define debug3(S,A,B,C)         {sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define debug4(S,A,B,C,D)       {sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define debug5(S,A,B,C,D,E)     {sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define debug6(S,A,B,C,D,E,F)   {sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define debug7(S,A,B,C,D,E,F,G) {sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define debug8(S,A,B,C,D,E,F,G,H) {sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),(H),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define debug9(S,A,B,C,D,E,F,G,H,I) {sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),(H),(I),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define debug10(S,A,B,C,D,E,F,G,H,I,J) {sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),(H),(I),(J),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define debug11(S,A,B,C,D,E,F,G,H,I,J,K) {sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),(H),(I),(J),(K),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define ifdebug0(I,S)           if(I){sprintf(debugStr,S " in %s, line %d\n",__FILE__,__LINE__);DebugLogLine(FALSE);}
#define ifdebug1(I,S,A)         if(I){sprintf(debugStr,S " in %s, line %d\n",(A),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define ifdebug2(I,S,A,B)       if(I){sprintf(debugStr,S " in %s, line %d\n",(A),(B),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define ifdebug3(I,S,A,B,C)     if(I){sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define ifdebug4(I,S,A,B,C,D)   if(I){sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define ifdebug5(I,S,A,B,C,D,E) if(I){sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define ifdebug6(I,S,A,B,C,D,E,F)   if(I){sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define ifdebug7(I,S,A,B,C,D,E,F,G) if(I){sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),__FILE__,__LINE__);DebugLogLine(FALSE);}
#define DBGONLY(X)              X
#else  // DEBUG_SWITCH == OFF
#define debug0(S)
#define debug1(S,A)
#define debug2(S,A,B)
#define debug3(S,A,B,C)
#define debug4(S,A,B,C,D)
#define debug5(S,A,B,C,D,E)
#define debug6(S,A,B,C,D,E,F)
#define debug7(S,A,B,C,D,E,F,G)
#define debug11(S,A,B,C,D,E,F,G,H,I,J,K)
#define ifdebug0(I,S)
#define ifdebug1(I,S,A)
#define ifdebug2(I,S,A,B)
#define ifdebug3(I,S,A,B,C)
#define ifdebug4(I,S,A,B,C,D)
#define ifdebug5(I,S,A,B,C,D,E)
#define ifdebug6(I,S,A,B,C,D,E,F)
#define ifdebug7(I,S,A,B,C,D,E,F,G)
#define DBGONLY(X)
#endif


// report status messages
#if !defined(DPRINTF_OFF) && (DEBUG_SWITCH == ON)
#define dprintf0(S) {printf(S);}
#define dprintf1(S,A) {printf(S,A);}
#define dprintf2(S,A,B) {printf(S,A,B);}
#define dprintf3(S,A,B,C) {printf(S,A,B,C);}
#define dprintf4(S,A,B,C,D) {printf(S,A,B,C,D);}
#define dprintf5(S,A,B,C,D,E) {printf(S,A,B,C,D,E);}
#define dprintf6(S,A,B,C,D,E,F) {printf(S,A,B,C,D,E,F);}
#define dprintf7(S,A,B,C,D,E,F,G) {printf(S,A,B,C,D,E,F,G);}
#define dprintf8(S,A,B,C,D,E,F,G,H) {printf(S,A,B,C,D,E,F,G,H);}
#else  //DPRINTF_OFF
#define dprintf0(S)
#define dprintf1(S,A)
#define dprintf2(S,A,B)
#define dprintf3(S,A,B,C)
#define dprintf4(S,A,B,C,D)
#define dprintf5(S,A,B,C,D,E)
#define dprintf6(S,A,B,C,D,E,F)
#define dprintf7(S,A,B,C,D,E,F,G)
#define dprintf8(S,A,B,C,D,E,F,G,H)
#endif //DPRINTF_OFF


#if DEBUG_SWITCH == ON

#define DEBUGSTR_LEN 512
extern char debugStr[DEBUGSTR_LEN];

void DebugLogLine( bool doHalt );
void DebugSetError( void );
#endif


#if CHK_MALLOC == ON
void * chk_malloc( size_t size, const char * pFileName, int line );
void chk_free( void * ptr );
void chk_memleakage( void );
#define xmalloc(SIZE)  chk_malloc((SIZE),__FILE__,__LINE__)
#define xfree(PTR)     chk_free(PTR)
#else
#include <malloc.h>
void * xmalloc( size_t size );
#define xfree(PTR)     free(PTR)
#endif

#endif  /* not __DEBUG_H */

