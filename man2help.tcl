#!/usr/bin/tclsh

#
#  Tcl script to convert nroff manpage to help menu & popups
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
#    Reads the manpage in nroff format and creates a Tcl/Tk script
#    that inserts command buttons to the help menu and defines the
#    texts for the help popups. The script will be compiled into the
#    executable and eval'ed upon program start.
#
#  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
#
#  $Id: man2help.tcl,v 1.7 2000/12/26 20:25:13 tom Exp tom $
#

set started 0
set helpTitles {}
set helpTexts  {}
set index 0
set style ""
set indent 0
set indentFirst 0

# open the man page file, which is given as command line parameter
set manpage [open [lindex $argv 0]]

puts stdout "# This file is automatically generated - do not edit"
puts stdout "# Generated by $argv0 from [lindex $argv 0] at [clock format [clock seconds] -format %c]\n"

puts stdout ".menubar.help insert 0 separator"

# process every text line of the manpage
while {[gets $manpage line] >= 0} {
   if {[regexp {^\.\\\"} $line]} {
      # ignore comments
   } elseif {[regexp {^\.SH (.*)} $line foo title]} {
      # skip the last chapters
      if {[string compare "FILES" $title] == 0} {
         break
      }

      # skip all chapters until "Getting started"
      if {$started || [string compare "INTRODUCTION" $title] == 0} {
         if {$started} {
            # save the text of the last paragraph
            puts stdout [list set helpTexts($index) $paragraph]
            incr index
         }
         # initialize new paragraph
         set started 1
         set style ""
         if {$tcl_version >= 8.3} {
            set title [string totitle $title]
         } else {
            set title [string toupper [string index $title 0]][string tolower [string range $title 1 end]]
         }

         # append title to Help in the menubar
         puts stdout ".menubar.help insert $index command -label {$title} -command \{PopupHelp $index\}"

         # build array of chapter names for access from help buttons in popups
         puts stdout "set {helpIndex($title)} $index"

         # put chapter heading at the front of the paragraph
         set paragraph [list "$title\n" title]
      }
   } else {

      if {[regexp {^(\.PP)? *$} $line]} {
         # new paragraph
         set line "\n"
         set style ""
         set indent 0
      } elseif {[regexp {^\.B[RI]? +(.*)} $line foo rest]} {
         # bold text (TODO: argument grouping and format alternating for BI and BR)
         set style "bold"
         set line "$rest "
      } elseif {[regexp {^\.I[BR]? +(.*)} $line foo rest]} {
         # italic text (printed underlined though)
         set style "underlined"
         set line "$rest "
      } elseif {[regexp {^\.[TP]} $line]} {
         # indented paragraph with headline
         set indent 1
         set indentFirst 1
         set line ""
      } else {
         # normal text line
         set style ""
         string trimright line
         append line " "
      }

      if {[string length $line] > 0} {
         if {$indent && !$indentFirst && ([string length $style] > 0)} {
            set tag "$style indent"
         } elseif {$indent && !$indentFirst} {
            set tag "indent"
         } else {
            set tag $style
         }

         if {$indent && $indentFirst} {
            set indentFirst 0
            set line "\n$line\n"
         }
         # remove backslashes before blanks (e.g. used in .BI lines)
         regsub -all -- {\\ } $line { } line
         regsub -all -- {\\-} $line {-} line
         # remove quotes around complete argument to .BI
         regsub -all -- {^\"(.*)\"\s*$} $line {\1 } line

         lappend paragraph $line $tag
      }
   }
}

# save the text of the last paragraph
if {$started} {
   puts stdout [list set helpTexts($index) $paragraph]
}

close $manpage

