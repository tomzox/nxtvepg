#!/usr/bin/perl
#
#  Perl script to convert POD manpage to help menu & popups
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
#
#  Description:
#
#    Reads the manpage in POD format and creates a Tcl/Tk script
#    that inserts command buttons to the help menu and defines the
#    texts for the help popups. The script will be compiled into the
#    executable and eval'ed upon program start. See the Perl manual
#    page 'perlpod' for details on the POD syntax.
#
#  Author: Tom Zoerner
#
#  $Id: pod2help.pl,v 1.12 2001/09/02 15:12:57 tom Exp tom $
#

require "ctime.pl";

$started = 0;
$helpTitles = "";
$helpTexts = "";
$index = 0;

print "# This file is automatically generated - do not edit\n";
print "# Generated by $ARGV[0] from lindex $ARGV[1] at " . &ctime(time) . "\n";

print ".menubar.help insert 0 separator\n";

# process every text line of the manpage
while(1)
{
   $line = "";
   # read the complete paragraph, i.e. until an empty line
   do
   {
      $_ = <>;
      last unless defined $_;
      chomp;
      $line .= "$_ ";
   } while ((($_ ne "") || ($line eq "")) && ($line !~ /^\s+\S/));

   die "ran into EOF - where's section 'FILES'?" if ($line eq "");

   # compress white space
   $line =~ s/\s\s+$//;

   # check for command paragraphs and process its command
   if ($line =~ /^\=head1 (.*)/)
   {
      $title = $1;

      # skip UNIX manpage specific until 'DESCRIPTION' chapter
      if ($started || ($title eq "DESCRIPTION"))
      {
         if ($started)
         {
            # Pre-process POD formatting expressions, e.g. I<some text>: replace pair
            # of opening and closing bracket with uniform separation character '#' and
            # appended format mode, which will be replaced with a tag later
            $chapter =~ s/S<([^>]*)>/$1/g;
            $chapter =~ s/T<([^>]*)>/##$1##T##/g;
            $chapter =~ s/[IF]<([^>]*)>/##$1##I##/g;
            $chapter =~ s/L<"([^>]*)">/##$1##L##/g;
            $chapter =~ s/L<([^>]*)>/##$1##L##/g;
            $chapter =~ s/B<([^>]*)>/##$1##B##/g;
            $chapter .= "####";
            $chapter =~ s/^#+//;

            # save the text of the last chapter into the help text array
            print "set helpTexts($index) {";

            # replace preprocessed format description with pairs of text and tags:
            # [list "text" underlined] to be inserted into a text widget
            while ($chapter =~ /([\x00-\xff]*?)##+(([IBTL])#+)?/sg)
            {
               if    ($3 eq "B") { print "{$1} bold "; }
               elsif ($3 eq "I") { print "{$1} underlined "; }
               elsif ($3 eq "T") { print "{$1} title "; }
               elsif ($3 eq "L") { my $tmp = $1; $tmp =~ s/(.)(.*)/$1\L$2/; print "{$tmp} href "; }
               elsif ($1 ne "")  { print "{$1} {} "; }
            }
            print "}\n";
            $index += 1;
         }

         # skip the last chapters
         if ($title eq "FILES")
         {
            last;
         }

         # initialize new chapter
         $started = 1;
         $title =~ s/^(.)(.*)/\u$1\L$2/;

         # append title to Help in the menubar
         print ".menubar.help insert $index command -label {$title} -command {PopupHelp $index}\n";

         # build array of chapter names for access from help buttons in popups
         print "set {helpIndex($title)} $index\n";

         # put chapter heading at the front of the chapter
         $chapter = "T<$title>\n";
      }
   }
   elsif ($line =~ /^\=head2 (.*)/)
   {  # sub-header: handle like a regular paragraph, just in bold
      $chapter .= "B<$1>\n";
   }
   elsif ($line =~ /^\=over/)
   {  # start of an indented paragraph or a list
      $over = 1;
   }
   elsif ($line =~ /^\=back/)
   {  # end of an indented paragraph or list
      $over = 0;
   }
   elsif ($line =~ /^\=item (.*)/)
   {  # start a new list item, with a bullet at start of line or a title
      $chapter .= "$1\n" if ($1 ne "*");
   }
   elsif ($line =~ /^\s+(\S.*)/)
   {
      $chapter .= "   $1\n";
   }
   else
   {  # append text of an ordinary paragraph to the current chapter
      $chapter .= "$line\n" if ($line =~ /\S/);
   }
}

