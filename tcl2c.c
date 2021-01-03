/*
 *  Build tool for inlining Tcl scripts into the executable
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
 *    This tool is used to convert a Tcl/Tk script file into a character
 *    array (i.e. a string) which can then be compiled into a C program.
 *    To reduce file size, comment-only lines are skipped.
 *
 *    These strings are then sourced into the Tcl interpreter at program
 *    start.  To reduce startup time, the scripts can be divided into a
 *    static and dynamic part: the static part is loaded upon startup,
 *    the dynamic part is only loaded when one of it's pre-defined stub
 *    procedures is referenced (usually when a dialog window is opened)
 *
 *    For performance optimization the tool includes a pre-processor
 *    which can replace occurences of global Tcl variables with integer
 *    or string constants.  The constant definitions are also written into
 *    the header file to make them available to other Tcl/Tk and C modules.
 *
 *    Usage:  tcl2c [-d] [-h] [-c] [-p path-prefix] script.tcl
 *
 *    The input filename must have the .tcl ending, because  the file names
 *    for the C And H output files are derived from the input file name by
 *    replacing .tcl with .c and .h respectively.  Use option -? for short
 *    explanations of command line switches.
 *
 *  Author:
 *
 *    Originally based on a tool with the same name, which was part of
 *    Netvideo version 3.2 by Ron Frederick <frederick@parc.xerox.com>
 *    Copyright (c) Xerox Corporation 1992. All rights reserved.
 *
 *    Completely rewritten and functionality added by Tom Zoerner
 *
 *  $Id: tcl2c.c,v 1.17 2021/01/03 12:20:42 tom Exp tom $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>

// command line options
static int optStubsForDynamic = 0;
static int optSubstConsts = 0;
static int optPreserveHeaders = 0;
static char * optOutPrefix = "";

// struct to hold a substitution list member
typedef struct
{
    int         len;
    char        str[128];
    char        define[128];
} SUBST_DEF;

#define SUBST_MAX     1000
#define SUBST_PREFIX  "EPGTCL_"
#define SUBST_POSTFIX "_STR"
static SUBST_DEF SubstList[SUBST_MAX];
static int       SubstCount = 0;
static char    * outNameHtmp = NULL;

#define IS_TCL_ALNUM(C) ( (((C) >= 'A') && ((C) <= 'Z')) || \
                          (((C) >= 'a') && ((C) <= 'z')) || \
                          (((C) >= '0') && ((C) <= '9')) || \
                          ((C) == '_') )

// ---------------------------------------------------------------------------
// Convert a text string and append it to a C string or array
//
static void PrintText( FILE * fp, const char * pLine )
{
    unsigned char c;

    while ((c = *(pLine++)) != 0)
    {
        #ifndef WIN32
        switch (c)
        {
            case '\n':
                fprintf(fp, "\\n\\\n");
                break;
            case '\"':
                fprintf(fp, "\\\"");
                break;
            case '\\':
                fprintf(fp, "\\\\");
                break;
            default:
                fputc(c, fp);
        }
        #else
        // some Microsoft compilers don't support very long strings,
        // so we dump the text as an array of unsigned char instead
	fprintf(fp, "%u,", (unsigned int) c);
        #endif
    }
    #ifdef WIN32
    fprintf(fp, "\n");
    #endif
}

// ---------------------------------------------------------------------------
// Append a CONST substitution to the list
// - called for every CONST in the main and any included scripts
// - a declaration is also written to the header file (unless handle is NULL)
//
static void AddSubstitution( char *var_name,  char *subst, FILE * fpH )
{
    char *p, *s;
    char isInteger;
    long substIntVal;
    int  substLen;

    if (SubstCount < SUBST_MAX)
    {
        // remove double-quotes around strings
        substLen = strlen(subst);
        if ((substLen >= 2) && (subst[0] == '"') && (subst[substLen - 1] == '"'))
        {
           subst[substLen - 1] = 0;
           subst += 1;
           isInteger = 0;
           substIntVal = 0;  // dummy
        }
        else
        {
           // check if substituted value is an integer
           substIntVal = strtol(subst, &p, 0);
           isInteger = ((*subst != 0) && (*p == 0));
        }

        strcpy(SubstList[SubstCount].str, var_name);
        SubstList[SubstCount].len = strlen(var_name);

        // derive name for define from tcl var name: only use letters, digits and underscore
        // (in particular crop :: prefix)
        p = var_name;
        s = SubstList[SubstCount].define;
        while (*p != 0)
        {
            if ( IS_TCL_ALNUM(*p) )
            {
                *(s++) = toupper(*p);
            }
            p++;
        }
        *s = 0;

        if (fpH != NULL)
        {
           // add define to header file: as string for concat into Tcl/Tk script
           #ifndef WIN32
           fprintf(fpH, "#define %s%s%s \"", SUBST_PREFIX, SubstList[SubstCount].define, SUBST_POSTFIX);
           PrintText(fpH, subst);
           fprintf(fpH, "\"\n");
           #else
           fprintf(fpH, "#define %s%s%s ", SUBST_PREFIX, SubstList[SubstCount].define, SUBST_POSTFIX);
           PrintText(fpH, subst);
           #endif

           // add integer define for use in C modules
           if (isInteger)
           {
              fprintf(fpH, "#define %s%s %ld\n", SUBST_PREFIX, SubstList[SubstCount].define, substIntVal);
           }
        }
        SubstCount += 1;
    }
    else
    {
        fprintf(stderr, "Constant substitution buffer overflow (max. %d)\n", SUBST_MAX);
        exit(1);
    }
}

// ---------------------------------------------------------------------------
//  Read constants definitions from another Tcl module
//
static void IncludeConstants( const char * hFileName )
{
    char * tclFileName;
    int fileNameLen;
    char line[1024];
    char proc_name[128];
    char var_name[128];
    FILE * fpTcl;

    fileNameLen = strlen(hFileName);
    if ((fileNameLen < 2) || (hFileName[0] != '"') || (hFileName[fileNameLen - 1] != '"'))
    {
        fprintf(stderr, "Include file name not enclosed in quotes (\"): %s\n", hFileName);
	exit(1);
    }
    if ((fileNameLen < 1+3+1) || (strncmp(hFileName + fileNameLen - 2 - 1, ".h", 2) != 0))
    {
        fprintf(stderr, "Unexpected file name extension in include (not '.h'): %s\n", hFileName);
	exit(1);
    }
    tclFileName = malloc(fileNameLen + 2 + 1);
    strncpy(tclFileName, hFileName + 1, fileNameLen - 2);
    strcpy(tclFileName + fileNameLen - (1 + 2 + 1), ".tcl");

    fpTcl = fopen(tclFileName, "r");
    if (fpTcl == NULL)
    {
        fprintf(stderr, "Cannot open included Tcl modules '%s': %s\n", tclFileName, strerror(errno));
	exit(1);
    }

    // read the complete file, ignoring everything except constant definitions
    while (fgets(line, sizeof(line) - 1, fpTcl) != NULL)
    {
        if (sscanf(line, "#=CONST= %127s %127[^\n]", var_name, proc_name) == 2)
        {
            AddSubstitution(var_name, proc_name, NULL);
        }
    }
    fclose(fpTcl);
    free(tclFileName);
}

// ---------------------------------------------------------------------------
// Substitute constants and append the output to a C string
//
static void PrintLine( FILE * fp, const char * pLine )
{
    const char *pNext;
    char *pMatch;
    int subIdx;

    if (SubstCount > 0)
    {
       pNext = pLine;
       while ((pMatch = strchr(pNext, '$')) != NULL)
       {
           for (subIdx = 0; subIdx < SubstCount; subIdx++)
           {
               if ( (strncmp(pMatch + 1, SubstList[subIdx].str, SubstList[subIdx].len) == 0) &&
                    !IS_TCL_ALNUM(pMatch[1 + SubstList[subIdx].len]) )
               {
                  // print the text before the substitution
                  *pMatch = 0;
                  PrintText(fp, pLine);

                  #ifndef WIN32
                  fprintf(fp, "\" %s%s%s \"", SUBST_PREFIX, SubstList[subIdx].define, SUBST_POSTFIX);
                  #else
                  fprintf(fp, "%s%s%s\n", SUBST_PREFIX, SubstList[subIdx].define, SUBST_POSTFIX);
                  #endif

                  pLine = pMatch + 1 + SubstList[subIdx].len;
                  pNext = pLine;
                  break;
               }
           }
           if (subIdx >= SubstCount)
              pNext = pMatch + 1;
       }
    }
    PrintText(fp, pLine);
}

// ---------------------------------------------------------------------------
// Remove temporary files upon interruption
//
static void RemoveOutputHtmp( void )
{
    if (optPreserveHeaders && (outNameHtmp[0] != 0))
    {
        unlink(outNameHtmp);
    }
}

#ifndef WIN32
static void TermSignal( int sigval )
{
    RemoveOutputHtmp();
}
#endif

// ---------------------------------------------------------------------------
//  Compare content of two text files, return 1 if identical
//
static int CompareHeaderFiles( char * pNewFile, char * pOldFile )
{
    char line1[1000];
    char line2[1000];
    FILE * fp1;
    FILE * fp2;
    int result;

    fp2 = fopen(pNewFile, "r");
    if (fp2 == NULL)
    {
        fprintf(stderr, "Cannot open header file '%s': %s\n", pNewFile, strerror(errno));
        exit(1);
    }

    fp1 = fopen(pOldFile, "r");
    if (fp1 == NULL)
    {
	result = 0;
        fclose(fp2);
    }
    else
    {
        // compare the two files line by line
        result = 1;
        while (fgets(line1, sizeof(line1) - 1, fp1) != NULL)
        {
            if ( (fgets(line2, sizeof(line2) - 1, fp2) == NULL) ||
                 (strcmp(line1, line2) != 0) )
            {
                result = 0;
                break;
            }

        }
        // check if the 2nd file is longer than the first
        if (fgets(line2, sizeof(line2) - 1, fp2) != NULL)
            result = 0;

        fclose(fp1);
        fclose(fp2);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Print Usage and exit
//
static void Usage( const char *argv0, const char *argvn, const char * reason )
{
   fprintf(stderr, "%s: %s: %s\n"
                   "Usage: %s [options] script.tcl\n"
                   "       -?\t: this message\n"
                   "       -d\t: generate stubs for procs in =DYNAMIC= tags\n"
                   "       -c\t: generate cpp macros for =CONST= assignments\n"
                   "       -h\t: write header file only if changed\n"
                   "       -p <prefix>\t: prepend this to output file names\n",
                   argv0, reason, argvn, argv0);

   exit(1);
}

// ---------------------------------------------------------------------------
// Parse command line options
//
static void ParseArgv( int argc, char * argv[] )
{
    int argIdx = 1;

    while ( (argIdx < argc) && (argv[argIdx][0] == '-') )
    {
        if (!strcmp(argv[argIdx], "-?"))
        {
            Usage(argv[0], "", "the following command line options are available");
        }
        else if (!strcmp(argv[argIdx], "-d"))
        {
            optStubsForDynamic = 1;
            argIdx++;
        }
        else if (!strcmp(argv[argIdx], "-h"))
        {
            optPreserveHeaders = 1;
            argIdx++;
        }
        else if (!strcmp(argv[argIdx], "-c"))
        {
            optSubstConsts = 1;
            argIdx++;
        }
        else if (!strcmp(argv[argIdx], "-p"))
        {
            if (argIdx + 1 < argc)
            {
                int len;

                optOutPrefix = argv[argIdx + 1];
                len = strlen(optOutPrefix);
                if ((len > 0) && (optOutPrefix[len - 1] != '/'))
                {
                    optOutPrefix = malloc(len + 1 + 1);
                    strcpy(optOutPrefix, argv[argIdx + 1]);
                    strcat(optOutPrefix, "/");
                }
                argIdx += 2;
            }
            else
                Usage(argv[0], argv[argIdx], "missing prefix argument after");
        }
        else
            Usage(argv[0], argv[argIdx], "unknown option");
    }
    if (argIdx + 1 < argc)
    {
        Usage(argv[0], "", "Too many arguments");
    }
    else if (argIdx + 1 > argc)
    {
        Usage(argv[0], "", "Missing script file argument");
    }
}

// ---------------------------------------------------------------------------
// Main loop
//
int main(int argc, char **argv)
{
    FILE * fpTcl;
    FILE * fpC;
    FILE * fpH;
    char * inFileName;
    char * outNameC;
    char * outNameH;
    char * baseName;
    char * scriptName;
    time_t now;
    char line[1024];
    char proc_name[128];
    char var_name[128];
    int comments;
    int fileNameLen;
    int prefixLen;
    int inBody;

    ParseArgv(argc, argv);
    inFileName = argv[argc - 1];

    fileNameLen = strlen(inFileName);
    if ((fileNameLen < 5) || (strcmp(inFileName + fileNameLen - 4, ".tcl") != 0))
    {
        Usage(argv[0], "Unexpected file name extension (not '.tcl')", inFileName);
	exit(1);
    }

    fpTcl = fopen(inFileName, "r");
    if (fpTcl == NULL)
    {
        fprintf(stderr, "Cannot open Tcl input file '%s': %s\n", inFileName, strerror(errno));
	exit(1);
    }

    /* build paths for .c and .h output files */
    prefixLen = strlen(optOutPrefix);
    outNameC = malloc(prefixLen + fileNameLen + 1);
    outNameH = malloc(prefixLen + fileNameLen + 1);
    strcpy(outNameC, optOutPrefix);
    strcpy(outNameH, optOutPrefix);
    strcpy(outNameC + prefixLen, inFileName);
    strcpy(outNameH + prefixLen, inFileName);
    strcpy(outNameC + prefixLen + fileNameLen - 4, ".c");
    strcpy(outNameH + prefixLen + fileNameLen - 4, ".h");

    /* derive name of char array from input file name (excluding the path) */
    scriptName = malloc(fileNameLen + 1);
    baseName = (char *)strrchr(inFileName, '/');
    if (baseName != NULL)
    {
        baseName += 1;
        strcpy(scriptName, baseName);
        scriptName[strlen(scriptName) - 4] = 0;
    }
    else
    {
        strncpy(scriptName, inFileName, fileNameLen - 4);
        scriptName[fileNameLen - 4] = 0;
        baseName = inFileName;
    }
    strcat(scriptName, "_tcl");

    fpC = fopen(outNameC, "w");
    if (fpC == NULL)
    {
        fprintf(stderr, "Cannot create C output file '%s': %s\n", outNameC, strerror(errno));
	exit(1);
    }

    if (optPreserveHeaders)
    {
        outNameHtmp = malloc(prefixLen + fileNameLen + 1 + 4);
        strcpy(outNameHtmp, outNameH);
        strcat(outNameHtmp, ".tmp");

        atexit(RemoveOutputHtmp);
        fpH = fopen(outNameHtmp, "w");
    }
    else
        fpH = fopen(outNameH, "w");
    if (fpH == NULL)
    {
        fprintf(stderr, "Cannot create H output file '%s': %s\n",
                        ((outNameHtmp != NULL) ?  outNameHtmp : outNameH), strerror(errno));
	exit(1);
    }
    #ifndef WIN32
    signal(SIGINT, TermSignal);
    signal(SIGTERM, TermSignal);
    signal(SIGHUP, TermSignal);
    #endif

    now = time(NULL);
    fprintf(fpC, "/*\n** This file was automatically generated - do not edit\n"
                 "** Generated from %s at %s*/\n\n"
                 "#include \"%s\"\n",
                 baseName, ctime(&now), outNameH);
    fprintf(fpH, "/*\n** This file was automatically generated - do not edit\n"
                 "** Generated from %s\n*/\n\n", baseName);
    inBody = 0;

    while (fgets(line, sizeof(line) - 1, fpTcl) != NULL)
    {
        comments = 0;
        sscanf(line, " #%n", &comments);
        if (comments == 0)
        {
            if (inBody == 0)
            {
                inBody = 1;
                #ifndef WIN32
                fprintf(fpC, "\nunsigned const char %s_static[] = \"\\\n", scriptName);
                fprintf(fpH, "\nextern unsigned const char %s_static[];\n", scriptName);
                #else
                fprintf(fpC, "\nunsigned const char %s_static[] = {\n", scriptName);
                fprintf(fpH, "\nextern unsigned const char %s_static[];\n", scriptName);
                #endif
            }
            PrintLine(fpC, line);
        }
        else
        {
            if ( optStubsForDynamic &&
                 (sscanf(line, "#=LOAD=%127[^\n]", proc_name) == 1) )
            {
                char buf[1024];
                int slen = snprintf(buf, sizeof(buf),
                       "proc %s args {\n"
                       "   C_LoadTclScript %s\n"
                       "   uplevel 1 %s $args\n"
                       "}\n",
                       proc_name, scriptName, proc_name);
               assert(slen < sizeof(buf));
               PrintText(fpC, buf);
            }
            else if ( optStubsForDynamic &&
                      (strcmp(line, "#=DYNAMIC=\n") == 0) )
            {
                #ifndef WIN32
                fprintf(fpC, "\";\n\n\n");
                fprintf(fpC, "unsigned const char %s_dynamic[] = \"\\\n", scriptName);
                fprintf(fpH, "extern unsigned const char %s_dynamic[];\n", scriptName);
                #else
                fprintf(fpC, "0\n};\n");
                fprintf(fpC, "unsigned const char %s_dynamic[] = {\n", scriptName);
                fprintf(fpH, "extern unsigned const char %s_dynamic[];\n", scriptName);
                #endif
            }
            else if ( optSubstConsts &&
                      (sscanf(line, "#=INCLUDE= %127[^\n]", proc_name) == 1) )
            {
                if (inBody == 0)
                {
                    fprintf(fpC, "#include %s\n", proc_name);
                    IncludeConstants(proc_name);
                }
                else
                    fprintf(stderr, "ERROR: #=INCLUDE= directive after start of body\n");
            }
            else if ( optSubstConsts &&
                      (sscanf(line, "#=CONST= %127s %127[^\n]", var_name, proc_name) == 2) )
            {
                AddSubstitution(var_name, proc_name, fpH);
            }
            else if (strncmp(line, "#=IF=", 5) == 0)
            {
                fprintf(fpC, "\"\n#if %s\"\\\n", line + 5);
            }
            else if (strncmp(line, "#=ELSE=", 8) == 0)
            {
                fprintf(fpC, "\"\n#else\n\"\\\n");
            }
            else if (strncmp(line, "#=ENDIF=", 8) == 0)
            {
                fprintf(fpC, "\"\n#endif\n\"\\\n");
            }
            // else: it's a comment, discard it
        }
    }
    #ifndef WIN32
    fprintf(fpC, "\";\n");
    #else
    fprintf(fpC, "0\n};\n");
    #endif

    fclose(fpC);
    fclose(fpH);
    fclose(fpTcl);

    if (optPreserveHeaders)
    {
        // if the newly generated header file is identical to the previous one, discard it
        if (CompareHeaderFiles(outNameHtmp, outNameH) == 0)
        {
            if (rename(outNameHtmp, outNameH) != 0)
            {
                fprintf(stderr, "failed to rename new header file '%s' into '%s': %s\n", outNameHtmp, outNameH, strerror(errno));
                outNameHtmp[0] = 0;
                exit(1);
            }
        }
        else
        {
            if (unlink(outNameHtmp) != 0)
            {
                fprintf(stderr, "failed to unlink new, identical header file '%s'': %s\n", outNameHtmp, strerror(errno));
                outNameHtmp[0] = 0;
                exit(1);
            }
        }
        outNameHtmp[0] = 0;
    }
    free(outNameC);
    free(outNameH);

    exit(0);
    /*NOTREACHED*/
}

