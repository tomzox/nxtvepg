#! /bin/sh
#
#  Shell-Script to validate XML parser against testsuite
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License Version 2 as
#  published by the Free Software Foundation. You find a copy of this
#  license in the file COPYRIGHT in the root directory of this release.
#
#  THIS PROGRAM IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL,
#  BUT WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED WARRANTY OF
#  MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  Author: Tom Zoerner
#
#  $Id: validate.sh,v 1.1 2005/06/30 16:20:22 tom Exp tom $
#

# set this path to point to the root of the xmlconf package
export BASE=../xmltest
export VALID=$BASE/valid/sa
export NOTWF=$BASE/not-wf/sa

# set this path to point to the test executable ("make verifyxml")
export SUBJECT=./build-i386/verifyxml

# the following tests should pass, i.e. result in "OK"
echo "$VALID"
echo

for v in `ls $VALID | egrep '\.xml$'` ; do
   if $SUBJECT $VALID/$v > out ; then
      if cmp -s out $VALID/out/$v; then
         echo "OK: $v"
      else
         echo "OUTPUT FAIL: $VALID/$v"
      fi
   else
      echo "PARSER FAIL: $VALID/$v"
   fi
done

# the following tests should fail, i.e. result in "ERROR"
echo
echo "$NOTWF"
echo

for v in `ls $NOTWF | egrep '\.xml$'` ; do
   if $SUBJECT $NOTWF/$v > out ; then
      echo "PARSED: $NOTWF/$v"
   else
      echo "ERROR: $NOTWF/$v"
   fi
done

