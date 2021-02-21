/*
 *  Debug service module
 *
 *  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
 *    Provides macros for debug output and to throw exceptions upon
 *    serious errors. Debug output can be switched on and off for each
 *    source module by DPRINTF_OFF which must be defined before including
 *    this header file.  Other debug options can be controlled centrally
 *    in mytypes.h.  For general releases all debug options should be
 *    switched off.
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
#define assert(X) do{if(!(X)){sprintf(debugStr,"assertion (" #X ") failed in %s, line %d\n",__FILE__,__LINE__);DebugLogLine(TRUE);}}while(0)

// same as above, but with a constant negative result
// this macro is obsolete: fatal0() should be used instead, which allows to print an explanation
#define SHOULD_NOT_BE_REACHED do{sprintf(debugStr,"branch should not have been reached in %s, line %d\n",__FILE__,__LINE__);DebugLogLine(TRUE);}while(0)

#else
#define assert(X)
#define SHOULD_NOT_BE_REACHED
#endif

#if defined(__GNUC__)
#define ATTRIBUTE_USED   __attribute__((used))
#define ATTRIBUTE_UNUSED __attribute__((unused))
#else
#define ATTRIBUTE_USED
#define ATTRIBUTE_UNUSED
#endif

// Note:
// the "do{...}while(0)" wrapper has the advantage that the macro consistantly
// behaves like a function call, e.g. in an if/else construct and regarding ";"
// following the macro call.

// report error conditions to stdout and the log file
// mainly intended for else branches after error checks,
// e.g. if (pointer != NULL) { /* do some work */ } else debug0("oops: NULL pointer!")
// note: this macro automatically appends filename, line number and newline after the message
#if DEBUG_SWITCH == ON
#define debug0(S)               do{sprintf(debugStr,S " in %s, line %d\n",__FILE__,__LINE__);DebugLogLine(FALSE);}while(0)
#define debug1(S,A)             do{sprintf(debugStr,S " in %s, line %d\n",(A),__FILE__,__LINE__);DebugLogLine(FALSE);}while(0)
#define debug2(S,A,B)           do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),__FILE__,__LINE__);DebugLogLine(FALSE);}while(0)
#define debug3(S,A,B,C)         do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),__FILE__,__LINE__);DebugLogLine(FALSE);}while(0)
#define debug4(S,A,B,C,D)       do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),__FILE__,__LINE__);DebugLogLine(FALSE);}while(0)
#define debug5(S,A,B,C,D,E)     do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),__FILE__,__LINE__);DebugLogLine(FALSE);}while(0)
#define debug6(S,A,B,C,D,E,F)   do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),__FILE__,__LINE__);DebugLogLine(FALSE);}while(0)
#define debug7(S,A,B,C,D,E,F,G) do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),__FILE__,__LINE__);DebugLogLine(FALSE);}while(0)
#define debug8(S,A,B,C,D,E,F,G,H) do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),(H),__FILE__,__LINE__);DebugLogLine(FALSE);}while(0)
#define debug9(S,A,B,C,D,E,F,G,H,I) do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),(H),(I),__FILE__,__LINE__);DebugLogLine(FALSE);}while(0)
#define debug10(S,A,B,C,D,E,F,G,H,I,J) do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),(H),(I),(J),__FILE__,__LINE__);DebugLogLine(FALSE);}while(0)
#define debug11(S,A,B,C,D,E,F,G,H,I,J,K) do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),(H),(I),(J),(K),__FILE__,__LINE__);DebugLogLine(FALSE);}while(0)

// same as above, but with an internal precondition,
// e.g. ifdebug0(result==FALSE, "function failed")
#define ifdebug0(COND,S)           do{if(COND){sprintf(debugStr,S " in %s, line %d\n",__FILE__,__LINE__);DebugLogLine(FALSE);}}while(0)
#define ifdebug1(COND,S,A)         do{if(COND){sprintf(debugStr,S " in %s, line %d\n",(A),__FILE__,__LINE__);DebugLogLine(FALSE);}}while(0)
#define ifdebug2(COND,S,A,B)       do{if(COND){sprintf(debugStr,S " in %s, line %d\n",(A),(B),__FILE__,__LINE__);DebugLogLine(FALSE);}}while(0)
#define ifdebug3(COND,S,A,B,C)     do{if(COND){sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),__FILE__,__LINE__);DebugLogLine(FALSE);}}while(0)
#define ifdebug4(COND,S,A,B,C,D)   do{if(COND){sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),__FILE__,__LINE__);DebugLogLine(FALSE);}}while(0)
#define ifdebug5(COND,S,A,B,C,D,E) do{if(COND){sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),__FILE__,__LINE__);DebugLogLine(FALSE);}}while(0)
#define ifdebug6(COND,S,A,B,C,D,E,F)   do{if(COND){sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),__FILE__,__LINE__);DebugLogLine(FALSE);}}while(0)
#define ifdebug7(COND,S,A,B,C,D,E,F,G) do{if(COND){sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),__FILE__,__LINE__);DebugLogLine(FALSE);}}while(0)
#define ifdebug8(COND,S,A,B,C,D,E,F,G,H) do{if(COND){sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),(H),__FILE__,__LINE__);DebugLogLine(FALSE);}}while(0)
#define ifdebug9(COND,S,A,B,C,D,E,F,G,H,I) do{if(COND){sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),(H),(I),__FILE__,__LINE__);DebugLogLine(FALSE);}}while(0)

// same as the debugX macros, but execution is halted and a core dumped after printing the message
// should be used for severe errors
// e.g. for default branches in switch(enum):
//    switch (my_enum_value) {
//       case ENUM_0: /* ... */ break;
//       default:     fatal1("illegal value for enum %d", my_enum_value); break;
//    }
#define fatal0(S)               do{sprintf(debugStr,S " in %s, line %d\n",__FILE__,__LINE__);DebugLogLine(TRUE);}while(0)
#define fatal1(S,A)             do{sprintf(debugStr,S " in %s, line %d\n",(A),__FILE__,__LINE__);DebugLogLine(TRUE);}while(0)
#define fatal2(S,A,B)           do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),__FILE__,__LINE__);DebugLogLine(TRUE);}while(0)
#define fatal3(S,A,B,C)         do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),__FILE__,__LINE__);DebugLogLine(TRUE);}while(0)
#define fatal4(S,A,B,C,D)       do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),__FILE__,__LINE__);DebugLogLine(TRUE);}while(0)
#define fatal5(S,A,B,C,D,E)     do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),__FILE__,__LINE__);DebugLogLine(TRUE);}while(0)
#define fatal6(S,A,B,C,D,E,F)   do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),__FILE__,__LINE__);DebugLogLine(TRUE);}while(0)
#define fatal7(S,A,B,C,D,E,F,G)  do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),__FILE__,__LINE__);DebugLogLine(TRUE);}while(0)
#define fatal8(S,A,B,C,D,E,F,G,H)  do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),(H),__FILE__,__LINE__);DebugLogLine(TRUE);}while(0)
#define fatal9(S,A,B,C,D,E,F,G,H,I)  do{sprintf(debugStr,S " in %s, line %d\n",(A),(B),(C),(D),(E),(F),(G),(H),(I),__FILE__,__LINE__);DebugLogLine(TRUE);}while(0)

// wrapper for debug statements
#define DBGONLY(X)              X
// attribute for variables used only in debug statements
#define __DBGONLY_ATT__

#else  // DEBUG_SWITCH == OFF
#define debug0(S) do{}while(0)
#define debug1(S,A) do{}while(0)
#define debug2(S,A,B) do{}while(0)
#define debug3(S,A,B,C) do{}while(0)
#define debug4(S,A,B,C,D) do{}while(0)
#define debug5(S,A,B,C,D,E) do{}while(0)
#define debug6(S,A,B,C,D,E,F) do{}while(0)
#define debug7(S,A,B,C,D,E,F,G) do{}while(0)
#define debug8(S,A,B,C,D,E,F,G,H) do{}while(0)
#define debug9(S,A,B,C,D,E,F,G,H,I) do{}while(0)
#define debug10(S,A,B,C,D,E,F,G,H,I,J) do{}while(0)
#define debug11(S,A,B,C,D,E,F,G,H,I,J,K) do{}while(0)
#define ifdebug0(COND,S) do{}while(0)
#define ifdebug1(COND,S,A) do{}while(0)
#define ifdebug2(COND,S,A,B) do{}while(0)
#define ifdebug3(COND,S,A,B,C) do{}while(0)
#define ifdebug4(COND,S,A,B,C,D) do{}while(0)
#define ifdebug5(COND,S,A,B,C,D,E) do{}while(0)
#define ifdebug6(COND,S,A,B,C,D,E,F) do{}while(0)
#define ifdebug7(COND,S,A,B,C,D,E,F,G) do{}while(0)
#define ifdebug8(COND,S,A,B,C,D,E,F,G,H) do{}while(0)
#define ifdebug9(COND,S,A,B,C,D,E,F,G,H,I) do{}while(0)
#define fatal0(S) do{}while(0)
#define fatal1(S,A) do{}while(0)
#define fatal2(S,A,B) do{}while(0)
#define fatal3(S,A,B,C) do{}while(0)
#define fatal4(S,A,B,C,D) do{}while(0)
#define fatal5(S,A,B,C,D,E) do{}while(0)
#define fatal6(S,A,B,C,D,E,F) do{}while(0)
#define fatal7(S,A,B,C,D,E,F,G) do{}while(0)
#define fatal8(S,A,B,C,D,E,F,G,H) do{}while(0)
#define fatal9(S,A,B,C,D,E,F,G,H,I) do{}while(0)
#define DBGONLY(X)
#define __DBGONLY_ATT__ ATTRIBUTE_UNUSED
#endif


// report status messages
// this is a simple printf() but with the advantage that it can be easily disabled
#if !defined(DPRINTF_OFF) && (DEBUG_SWITCH == ON)
#ifndef WIN32
#define dprintf0(S) fprintf(stderr,S)
#define dprintf1(S,A) fprintf(stderr,S,A)
#define dprintf2(S,A,B) fprintf(stderr,S,A,B)
#define dprintf3(S,A,B,C) fprintf(stderr,S,A,B,C)
#define dprintf4(S,A,B,C,D) fprintf(stderr,S,A,B,C,D)
#define dprintf5(S,A,B,C,D,E) fprintf(stderr,S,A,B,C,D,E)
#define dprintf6(S,A,B,C,D,E,F) fprintf(stderr,S,A,B,C,D,E,F)
#define dprintf7(S,A,B,C,D,E,F,G) fprintf(stderr,S,A,B,C,D,E,F,G)
#define dprintf8(S,A,B,C,D,E,F,G,H) fprintf(stderr,S,A,B,C,D,E,F,G,H)
#define dprintf9(S,A,B,C,D,E,F,G,H,I) fprintf(stderr,S,A,B,C,D,E,F,G,H,I)
#else  // WIN32
// M$ Windows debug output uses OS internal debug features; the output can be captured 
// with the DebugView tool from http://www.sysinternals.com/
#include <windows.h>
#define dprintf0(S) do{sprintf(debugStr,S);OutputDebugString(debugStr);}while(0)
#define dprintf1(S,A) do{sprintf(debugStr,S,A);OutputDebugString(debugStr);}while(0)
#define dprintf2(S,A,B) do{sprintf(debugStr,S,A,B);OutputDebugString(debugStr);}while(0)
#define dprintf3(S,A,B,C) do{sprintf(debugStr,S,A,B,C);OutputDebugString(debugStr);}while(0)
#define dprintf4(S,A,B,C,D) do{sprintf(debugStr,S,A,B,C,D);OutputDebugString(debugStr);}while(0)
#define dprintf5(S,A,B,C,D,E) do{sprintf(debugStr,S,A,B,C,D,E);OutputDebugString(debugStr);}while(0)
#define dprintf6(S,A,B,C,D,E,F) do{sprintf(debugStr,S,A,B,C,D,E,F);OutputDebugString(debugStr);}while(0)
#define dprintf7(S,A,B,C,D,E,F,G) do{sprintf(debugStr,S,A,B,C,D,E,F,G);OutputDebugString(debugStr);}while(0)
#define dprintf8(S,A,B,C,D,E,F,G,H) do{sprintf(debugStr,S,A,B,C,D,E,F,G,H);OutputDebugString(debugStr);}while(0)
#define dprintf9(S,A,B,C,D,E,F,G,H,I) do{sprintf(debugStr,S,A,B,C,D,E,F,G,H,I);OutputDebugString(debugStr);}while(0)
#endif  // WIN32
// attribute for variables used only in dprintf statements
#define __DPRINTF_ONLY_ATT__

#else  //DPRINTF_OFF
#define dprintf0(S) do{}while(0)
#define dprintf1(S,A) do{}while(0)
#define dprintf2(S,A,B) do{}while(0)
#define dprintf3(S,A,B,C) do{}while(0)
#define dprintf4(S,A,B,C,D) do{}while(0)
#define dprintf5(S,A,B,C,D,E) do{}while(0)
#define dprintf6(S,A,B,C,D,E,F) do{}while(0)
#define dprintf7(S,A,B,C,D,E,F,G) do{}while(0)
#define dprintf8(S,A,B,C,D,E,F,G,H) do{}while(0)
#define dprintf9(S,A,B,C,D,E,F,G,H,I) do{}while(0)
#define __DPRINTF_ONLY_ATT__ ATTRIBUTE_UNUSED
#endif //DPRINTF_OFF


#if DEBUG_SWITCH == ON

#define DEBUGSTR_LEN 512
extern char debugStr[DEBUGSTR_LEN];

void DebugLogLine( bool doHalt );
void DebugSetError( void );
#endif

// memory allocation debugging
#if CHK_MALLOC == ON
void * chk_malloc( size_t size, const char * pCallerFile, int callerLine );
void * chk_realloc( void * ptr, size_t size, const char * pCallerFile, int callerLine );
void chk_free( void * ptr, const char * pCallerFile, int callerLine );
void chk_memleakage( void );
char * chk_strdup( const char * pSrc, const char * pCallerFile, int callerLine );
#define xmalloc(SIZE)  chk_malloc((SIZE),__FILE__,__LINE__)
#define xrealloc(PTR,SIZE) chk_realloc((PTR),(SIZE),__FILE__,__LINE__)
#define xfree(PTR)     chk_free(PTR,__FILE__,__LINE__)
#define xstrdup(PTR)   chk_strdup((PTR),__FILE__,__LINE__)
#else
void * xmalloc( size_t size );
void * xrealloc( void * ptr, size_t size );
char * xstrdup( const char * pSrc );
#define xfree(PTR)     free(PTR)
#endif


#ifdef _TCL
// Tcl/Tk script failure debugging
#if DEBUG_SWITCH == ON
#if DEBUG_SWITCH_TCL_BGERR == ON
#define eval_check(INTERP,CMD) \
   do { \
      assert(comm[sizeof(comm) - 1] == 0); \
      if (Tcl_EvalEx((INTERP), (CMD), -1, 0) != TCL_OK) \
         Tcl_BackgroundError(INTERP); \
   } while(0)
#define eval_global(INTERP,CMD) \
   do { \
      assert(comm[sizeof(comm) - 1] == 0); \
      if (Tcl_EvalEx((INTERP), (CMD), -1, TCL_EVAL_GLOBAL) != TCL_OK) \
         Tcl_BackgroundError(INTERP); \
   } while(0)
#define debugTclErr(INTERP,STR) \
   do { \
      debug2("Tcl/Tk error: %s: %s", (STR), Tcl_GetStringResult(INTERP)); \
      Tcl_BackgroundError(INTERP); \
   } while (0)
#else  // DEBUG_SWITCH_TCL_BGERR != ON
#define eval_check(INTERP,CMD) \
   do { \
      assert(comm[sizeof(comm) - 1] == 0); \
      if (Tcl_EvalEx((INTERP), (CMD), -1, 0) != TCL_OK) \
         debug2("Command: %s\nError: %s", (CMD), Tcl_GetStringResult(interp)); \
   } while(0)
#define eval_global(INTERP,CMD) \
   do { \
      assert(comm[sizeof(comm) - 1] == 0); \
      if (Tcl_EvalEx((INTERP), (CMD), -1, TCL_EVAL_GLOBAL) != TCL_OK) \
         debug2("Command: %s\nError: %s", (CMD), Tcl_GetStringResult(interp)); \
   } while(0)
#define debugTclErr(INTERP,STR) \
   debug2("Tcl/Tk error: %s: %s", (STR), Tcl_GetStringResult(INTERP));
#endif
#else  // DEBUG_SWITCH != ON
#define eval_check(INTERP,CMD) \
   Tcl_EvalEx((INTERP), (CMD), -1, 0)
#define eval_global(INTERP,CMD) \
   Tcl_EvalEx((INTERP), (CMD), -1, TCL_EVAL_GLOBAL)
#define debugTclErr(INTERP,STR)
#endif
#endif

#endif  /* not __DEBUG_H */

