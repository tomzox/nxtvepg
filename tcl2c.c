/*
	Netvideo version 3.2
	Written by Ron Frederick <frederick@parc.xerox.com>

	Simple hack to translate a Tcl/Tk init file into a C string constant
*/

/*
 * Copyright (c) Xerox Corporation 1992. All rights reserved.
 *  
 * License is granted to copy, to use, and to make and to use derivative
 * works for research and evaluation purposes, provided that Xerox is
 * acknowledged in all documentation pertaining to any such copy or derivative
 * work. Xerox grants no other licenses expressed or implied. The Xerox trade
 * name should not be used in any advertising without its written permission.
 *  
 * XEROX CORPORATION MAKES NO REPRESENTATIONS CONCERNING EITHER THE
 * MERCHANTABILITY OF THIS SOFTWARE OR THE SUITABILITY OF THIS SOFTWARE
 * FOR ANY PARTICULAR PURPOSE.  The software is provided "as is" without
 * express or implied warranty of any kind.
 *  
 * These notices must be retained in any copies of any part of this software.
 */

#include <stdio.h>

#ifndef WIN32

main(int argc, char **argv)
{
    int c;

    if (argc < 2) {
	fprintf(stderr, "Usage: %s stringname\n", argv[0]);
	exit(1);
    }

    printf("char %s[] = \"\\\n", argv[1]);
    while ((c = getchar()) != EOF) {
	switch (c) {
	case '\n':
	    printf("\\n\\\n");
	    break;
	case '\"':
	    printf("\\\"");
	    break;
	case '\\':
	    printf("\\\\");
	    break;
	default:
	    putchar(c);
	}
    }
    printf("\";\n");
    exit(0);
    /*NOTREACHED*/
}

#else /* WIN32 */

/* Windows has a maximum length for strings */
/* so we create a char array instead of a string */

main(int argc, char **argv)
{
    int c, n;

    if (argc < 2) {
	fprintf(stderr, "Usage: %s stringname\n", argv[0]);
	exit(1);
    }

    printf("char %s[] = {\n", argv[1]);
    for (n = 0; (c = getchar()) != EOF;)
	printf("%u,%c", c, ((++n & 0xf) == 0) ? '\n' : ' ');

    printf("};\n");
    exit(0);
    /*NOTREACHED*/
}

#endif /* WIN32 */
