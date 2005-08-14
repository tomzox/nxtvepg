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
#  $Id: pod2help.pl,v 1.20 2005/07/17 17:52:12 tom Exp tom $
#

require "ctime.pl";

$started = 0;
$sectIndex = 0;

sub PrintParagraph {
   local($str, $indent) = @_;

   # Pre-process POD formatting expressions, e.g. I<some text>: replace pair
   # of opening and closing bracket with uniform separation character '#' and
   # appended format mode, which will be replaced with a tag later
   $str =~ s/S<([^>]*)>/$1/g;
   $str =~ s/T<([^>]*)>/##$1##T##/g;
   $str =~ s/H<([^>]*)>/##$1##H##/g;  # non-POD, internal format
   $str =~ s/[IF]<([^>]*)>/##$1##I##/g;
   $str =~ s/C<([^>]*)>/##$1##C##/g;
   $str =~ s/L<"([^>]*)">/##$1##L##/g;
   $str =~ s/L<([^>]*)>/##$1##L##/g;
   $str =~ s/B<([^>]*)>/##$1##B##/g;
   $str =~ s/P<"([\x00-\xff]*?)">/##$1##P##/g;
   $str .= "####";
   $str =~ s/^#+//;

   # replace preprocessed format description with pairs of text and tags
   # - e.g. [list "text" underlined] to be inserted into a text widget
   # - note to hyperlinks: sections names are converted to lowercase;
   #   character ':' is a sub-section separator; see proc PopupHelp
   while ($str =~ /([\x00-\xff]*?)##+(([IBCTHLP])#+)?/sg)
   {
      $chunk = $1;
      $tag   = $3;

      if ($chunk ne "")
      {
         if    ($tag eq "B") { $tag = "bold"; }
         elsif ($tag eq "I") { $tag = "underlined"; }
         elsif ($tag eq "C") { $tag = "fixed"; }
         elsif ($tag eq "P") { $tag = "pfixed"; }
         elsif ($tag eq "T") { $tag = "title1"; }
         elsif ($tag eq "H") { $tag = "title2"; }
         elsif ($tag eq "L") { $tag = "href"; $chunk =~ s/(.)([^:]*)/$1\L$2/; }
         else                { $tag = "{}"; }

         if ($indent)
         {
            $tag = ($tag ne "{}") ? "{$tag indent}" : "indent";
         }
         $helpTexts[$sectIndex] .= "{$chunk} $tag ";
      }
   }
}

# read the complete paragraph, i.e. until an empty line
sub ReadParagraph
{
   my $line = "";
   do
   {
      $_ = <>;
      last unless defined $_;
      chomp;
      $line .= $_;
      # insert whitespace to separate lines inside the paragraph,
      # except for pre-formatted paragraphs in which the newline is kept
      $line .= (($line =~ /^\s+\S/) ? "\n" : " ");

   } while (($_ ne "") || ($line eq ""));

   # remove white space at line end
   $line =~ s/\s\s+$//;

   return $line;
}

# read the language option
if ($ARGV[0] eq "-lang")
{
   shift @ARGV;
   $lang = shift @ARGV;
}
die "Language not defined: must use option -lang\n" if !defined $lang;

# process every text line of the manpage
while(1)
{
   $line = ReadParagraph();

   die "ran into EOF - where's section 'FILES'?" if ($line eq "");

   # check for command paragraphs and process its command
   if ($line =~ /^\=head1 (.*)/)
   {
      $title = $1;

      # skip UNIX manpage specific until 'DESCRIPTION' chapter
      if ($started || ($title eq "DESCRIPTION"))
      {
         if ($started)
         {
            # close the string of the previous chapter
            $helpTexts[$sectIndex] .= "}\n";
            $sectIndex += 1;
         }

         # skip the last chapters
         if ($title eq "SEE ALSO")
         {
            last;
         }

         # initialize new chapter
         $started = 1;
         $title =~ s/^(.)(.*)/\u$1\L$2/;

         # build array of chapter names for access from help buttons in popups
         $helpIndex[$sectIndex] = "set {::helpIndex($title)} $sectIndex\n";

         # put chapter heading at the front of the chapter
         $helpTexts[$sectIndex] = "set ::helpTexts($lang,$sectIndex) {";
         PrintParagraph("T<$title>\n", 0);
      }
   }
   elsif ($started)
   {
      if ($line =~ /^\=head2 (.*)/)
      {  # sub-header: handle like a regular paragraph, just in bold
         PrintParagraph("H<$1>\n", 0);
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
         PrintParagraph("$1\n", 0) if ($1 ne "*");
      }
      else
      {
         # this is a regular paragraph
         # check for a pre-formatted paragraph: starts with white-space
         if ($line =~ /^\s+(\S.*)/)
         {
            # add space after backslashes before newlines
            # to prevent interpretation by Tcl/Tk
            $line =~ s/\\\n/\\ \n/g;
            PrintParagraph("P<\"$line\n\n\">", $over);
         }
         else
         {  # append text of an ordinary paragraph to the current chapter
            PrintParagraph("$line\n", $over) if ($line =~ /\S/);
         }
      }
   }
}

print "# This file is automatically generated - do not edit\n";
print "# Generated by $0 from $ARGV[0] at " . &ctime(time) . "\n";

for ($idx=0; $idx < $sectIndex; $idx++)
{
   print $helpIndex[$idx];
}

print "\n#=LOAD=LoadHelpTexts_$lang\n";
print "#=DYNAMIC=\n\n";
print "proc LoadHelpTexts_$lang {} {\n";

for ($idx=0; $idx < $sectIndex; $idx++)
{
   print $helpTexts[$idx];
}
print "}\n";

