/*
 *  Tcl script inlining tool for the make process
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
 *    array (i.e. a string) which can then by compiled into a C program.
 *    The reduce file size, comment-only lines are skipped.
 *
 *    These strings are then sourced into the Tcl interpreter at program
 *    start.  To reduce startup time, the scripts can by divided into a
 *    static and dynamic part: only the static part is loaded upon startup,
 *    the dynamic part is only loaded when one of the procedured in it is
 *    referenced (e.g. when a config dialog is opened)
 *
 *    Usage:  tcl2c script.tcl
 *
 *    The input filename must have the .tcl ending, because  the file names
 *    for the C And H output files are derived from the input file name by
 *    replacing .tcl with .c and .h respectively.
 *
 *  Author:
 *
 *    Originally based on a tool with the same name, which was part of
 *    Netvideo version 3.2 by Ron Frederick <frederick@parc.xerox.com>
 *    Copyright (c) Xerox Corporation 1992. All rights reserved.
 *
 *    Completely rewritten and functionality added by Tom Zoerner
 *
 *  $Id: tcl2c.c,v 1.4 2002/11/03 12:15:10 tom Exp tom $
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define FALSE 0
#define TRUE  1
typedef unsigned char  bool;

static void PrintLine( FILE * fp, const char * pLine )
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

int main(int argc, char **argv)
{
    FILE * fpTcl;
    FILE * fpC;
    FILE * fpH;
    char * outNameC;
    char * outNameH;
    char * baseName;
    char * scriptName;
    time_t now;
    char line[1024];
    char proc_name[128];
    char * p;
    int comments;
    int fileNameLen;
    int c;

    if (argc < 2)
    {
	fprintf(stderr, "Usage: %s stringname\n", argv[0]);
	exit(1);
    }

    fileNameLen = strlen(argv[1]);
    if ((fileNameLen < 5) || (strcmp(argv[1] + fileNameLen - 4, ".tcl") != 0))
    {
        fprintf(stderr, "Unexpected file name extension (not '.tcl'): %s\n", argv[1]);
	exit(1);
    }

    fpTcl = fopen(argv[1], "r");
    if (fpTcl == NULL)
    {
        fprintf(stderr, "Cannot open Tcl input file '%s': %s\n", argv[1], strerror(errno));
	exit(1);
    }

    outNameC = malloc(fileNameLen + 1);
    outNameH = malloc(fileNameLen + 1);
    scriptName = malloc(fileNameLen + 1);
    strncpy(outNameC, argv[1], fileNameLen - 4);
    strncpy(outNameH, argv[1], fileNameLen - 4);
    baseName = (char *)strrchr(argv[1], '/');
    if (baseName != NULL)
    {
        baseName += 1;
        strcpy(scriptName, baseName);
        scriptName[strlen(scriptName) - 4] = 0;
    }
    else
    {
        strncpy(scriptName, argv[1], fileNameLen - 4);
        baseName = argv[1];
    }
    strcat(outNameC, ".c");
    strcat(outNameH, ".h");
    strcat(scriptName, "_tcl");

    fpC = fopen(outNameC, "w");
    if (fpC == NULL)
    {
        fprintf(stderr, "Cannot create C output file '%s': %s\n", outNameC, strerror(errno));
	exit(1);
    }

    fpH = fopen(outNameH, "w");
    if (fpH == NULL)
    {
        fprintf(stderr, "Cannot create H output file '%s': %s\n", outNameH, strerror(errno));
	exit(1);
    }

    now = time(NULL);
    fprintf(fpC, "/*\n** This file was automatically generated - do not edit\n"
                 "** Generated from %s at %s*/\n\n"
                 "#include \"%s\"\n\n",
                 baseName, ctime(&now), outNameH);
    fprintf(fpH, "/*\n** This file was automatically generated - do not edit\n"
                 "** Generated from %s at %s*/\n\n", baseName, ctime(&now));

    #ifndef WIN32
    fprintf(fpC, "unsigned const char %s_static[] = \"\\\n", scriptName);
    fprintf(fpH, "extern unsigned const char %s_static[];\n", scriptName);
    #else
    fprintf(fpC, "unsigned const char %s_static[] = {\n", scriptName);
    fprintf(fpH, "extern unsigned const char %s_static[];\n", scriptName);
    #endif

    while (fgets(line, sizeof(line) - 1, fpTcl) != NULL)
    {
        comments = 0;
        sscanf(line, " #%n", &comments);
        if (comments == 0)
        {
           PrintLine(fpC, line);
        }
        else
        {
            if (sscanf(line, "#=LOAD=%127[^\n]", proc_name) == 1)
            {
                char buf[256];
                sprintf(buf, "proc %s args {\n"
                       "   C_LoadTclScript %s\n"
                       "   uplevel 1 %s $args\n"
                       "}\n",
                       proc_name, scriptName, proc_name);
               PrintLine(fpC, buf);
            }
            else if (strcmp(line, "#=DYNAMIC=\n") == 0)
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

    exit(0);
    /*NOTREACHED*/
}

