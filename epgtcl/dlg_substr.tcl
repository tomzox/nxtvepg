#
#  Text search dialog
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
#    Implements dialogs for maintaining the text search dialog. There's
#    the main dialog which consists of three main components: (i) list of
#    currently active search texts, (ii) entry field and checkbuttons for
#    new search text, (iii) history of previous search texts as popup
#    menu below entry field (ii + iii as combobox)
#
#    Then there's a sub-dialog which allows to paste in a text list or
#    load a list from a file (mainly intended for importing lists of
#    favorite movie titles.)
#
#  Author: Tom Zoerner
#
#  $Id: dlg_substr.tcl,v 1.2 2003/09/15 20:12:21 tom Exp tom $
#
set substr_grep_title 1
set substr_grep_descr 1
set substr_match_full 0
set substr_match_case 0
set substr_popup 0
set substr_paste_popup 0
set substr_pattern {}
set substr_stack {}
set substr_history {}


#=LOAD=SubStrPopup
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Text search pop-up window
##
proc SubStrPopup {} {
   global substr_grep_title substr_grep_descr
   global substr_popup
   global substr_pattern

   if {$substr_popup == 0} {
      CreateTransientPopup .substr "Text search"
      set substr_popup 1

      mclistbox_load
      combobox_load

      frame .substr.all
      label .substr.all.lab_stack -text "Currently active search texts:" -font $::font_normal
      pack  .substr.all.lab_stack -side top -anchor w
      frame .substr.all.stack -borderwidth 2 -relief ridge
      scrollbar .substr.all.stack.list_sb -orient vertical -command [list .substr.all.stack.selist yview] -takefocus 0
      pack      .substr.all.stack.list_sb -side left -fill y
      mclistbox::mclistbox .substr.all.stack.selist -relief ridge -columnrelief flat -labelanchor c \
                                -height 5 -selectmode browse -columnborderwidth 0 \
                                -exportselection 0 -font $::pi_font -labelfont [DeriveFont $::pi_font -2] \
                                -yscrollcommand [list .substr.all.stack.list_sb set] -fillcolumn text
      bind  .substr.all.stack.selist <Enter> {focus %W}
      bind  .substr.all.stack.selist <Key-Delete> {Substr_RemoveFromStack}
      bind  .substr.all.stack.selist <Escape> {after idle {tkButtonInvoke .substr.all.cmd.dismiss}}
      pack  .substr.all.stack.selist -side left -fill both -expand 1
      # define listbox columns: labels and width (resizable)
      .substr.all.stack.selist column add text -label "Text" -width 24
      .substr.all.stack.selist column add title -label "Title" -width 5
      .substr.all.stack.selist column add descr -label "Descr." -width 5
      .substr.all.stack.selist column add case -label "Case" -width 5
      .substr.all.stack.selist column add compl -label "Compl." -width 5
      bind  .substr.all.stack.selist <<ListboxSelect>> {+ after idle Substr_ListSelect}
      bind  .substr.all.stack.selist <Double-Button-1> {Substr_ToggleListColumn \
                                                          [::mclistbox::convert %W -W] \
                                                          [::mclistbox::convert %W -x %x] \
                                                          [::mclistbox::convert %W -y %y] \
                                                          %X %Y}
      bind  .substr.all.stack.selist <ButtonPress-3>   {Substr_OpenContextMenu \
                                                          [::mclistbox::convert %W -W] \
                                                          [::mclistbox::convert %W -x %x] \
                                                          [::mclistbox::convert %W -y %y] \
                                                          %X %Y}
      trace variable ::substr_stack w Substr_TraceStackVar
      pack  .substr.all.stack -side top -fill both -expand 1

      button .substr.all.but_paste -text "Click here to open search text copy & paste window" \
                -borderwidth 0 -relief flat -font [DeriveFont $::font_normal -2 underline] \
                -foreground blue -activeforeground blue -command Substr_OpenPasteDialog
      pack  .substr.all.but_paste -side top -anchor w

      frame .substr.all.name
      label .substr.all.name.prompt -text "Enter text:"
      pack .substr.all.name.prompt -side left
      combobox::combobox .substr.all.name.str -textvariable substr_pattern -width 35 \
                                              -editable 1 -opencommand SubStrPopupHistoryMenu
      pack .substr.all.name.str -side left -fill x -expand 1
      bind .substr.all.name.str <Enter> {focus %W}
      bind .substr.all.name.str <Return> {tkButtonInvoke .substr.all.cmd.apply}
      bind .substr.all.name.str <Escape> {after idle {tkButtonInvoke .substr.all.cmd.dismiss}}
      pack .substr.all.name -side top -pady 10 -fill x

      frame .substr.all.opt
      frame .substr.all.opt.scope
      checkbutton .substr.all.opt.scope.titles -text "titles" -variable substr_grep_title -command {SubstrCheckOptScope substr_grep_descr}
      pack .substr.all.opt.scope.titles -side top -anchor nw
      checkbutton .substr.all.opt.scope.descr -text "descriptions" -variable substr_grep_descr -command {SubstrCheckOptScope substr_grep_title}
      pack .substr.all.opt.scope.descr -side top -anchor nw
      pack .substr.all.opt.scope -side left -padx 5
      frame .substr.all.opt.match
      checkbutton .substr.all.opt.match.full -text "match complete text" -variable substr_match_full
      checkbutton .substr.all.opt.match.case -text "match case" -variable substr_match_case
      pack .substr.all.opt.match.full .substr.all.opt.match.case -anchor nw
      pack .substr.all.opt.match -side left -anchor nw -padx 5
      pack .substr.all.opt -side top -pady 5

      frame .substr.all.cmd
      button .substr.all.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Filtering) "Text search"}
      button .substr.all.cmd.clear -text "Clear" -width 5 -command {ResetSubstr; SubstrUpdateFilter 0}
      button .substr.all.cmd.apply -text "Apply" -width 5 -command {SubstrUpdateFilter 1}
      button .substr.all.cmd.dismiss -text "Dismiss" -width 5 -command Substr_DialogClose
      pack .substr.all.cmd.help .substr.all.cmd.clear .substr.all.cmd.apply .substr.all.cmd.dismiss -side left -padx 10
      pack .substr.all.cmd -side top -pady 10

      pack .substr.all -padx 5 -pady 5 -fill both -expand 1
      bind .substr.all <Destroy> {+ set substr_popup 0}
      bind .substr <Key-F1> {PopupHelp $helpIndex(Filtering) "Text search"}
      wm protocol .substr WM_DELETE_WINDOW Substr_DialogClose

      focus .substr.all.name.str
      wm resizable .substr 1 1
      update
      wm minsize .substr [winfo reqwidth .substr] [winfo reqheight .substr]
   } else {
      raise .substr
   }
   Substr_FillListbox
}

# check that at lease one of the {title descr} options remains checked
proc SubstrCheckOptScope {varname} {
   global substr_grep_title substr_grep_descr

   if {($substr_grep_title == 0) && ($substr_grep_descr == 0)} {
      set $varname 1
   }
}

# callback for up/down keys in entry widget: open search history menu
proc SubStrPopupHistoryMenu {} {
   global substr_stack substr_history
   global substr_hist_arr1

   # check if the history list is empty
   if {[llength $substr_history] > 0} {
      set widget .substr.all.name.str

      # remove the previous content (in case the history changed since the last open)
      $widget list delete 0 end

      # build cache of active search texts to skip them in the history list
      array unset substr_hist_arr1
      foreach item $substr_stack {
         set substr_hist_arr1($item) {}
      }

      # add the search history as menu commands, if not in the stack
      foreach item $substr_history {
         if {! [info exists substr_hist_arr1($item)]} {
            set substr_hist_arr1($item) 0
            $widget list insert end [lindex $item 0]
         }
      }
   }
}

# fill listbox with currently active substr filters
proc Substr_FillListbox {} {
   global substr_pattern
   global substr_stack substr_history

   set sel_idx -1
   set idx 0
   .substr.all.stack.selist delete 0 end
   for {set idx 0} {$idx < [llength $substr_stack]} {incr idx} {
      set elem [lindex $substr_stack $idx]
      set ltmp [list [lindex $elem 0]]
      foreach bool_val [lrange $elem 1 4] {
         if $bool_val {
            lappend ltmp yes
         } else {
            lappend ltmp no
         }
      }
      .substr.all.stack.selist insert end $ltmp

      if {[string compare [lindex $elem 0] $substr_pattern] == 0} {
         set sel_idx $idx
      }
   }
   if {$sel_idx != -1} {
      .substr.all.stack.selist selection set $sel_idx
      .substr.all.stack.selist see $sel_idx
   }
}

# callback for double mouse-click into "action" column
proc Substr_ToggleListColumn {w x y rootx rooty} {
   global substr_grep_title substr_grep_descr substr_match_case substr_match_full
   global substr_pattern
   global substr_stack

   set sel_idx [.substr.all.stack.selist curselection]
   if {([llength $sel_idx] == 1) && ($sel_idx < [llength $substr_stack])} {
      # check which column was clicked into
      set col_tag [$w column nearest $x]
      set col_idx [lsearch [$w column names] $col_tag]
      if {($col_idx >= 1) && ($col_idx <= 4)} {
         # one of the boolean value columns -> toggle it's value
         set elem [lindex $substr_stack $sel_idx]
         set val [expr ! [lindex $elem $col_idx]]
         set elem [lreplace $elem $col_idx $col_idx $val]

         # check that at least one of "title" or "description" remains enabled
         if {([lindex $elem 1] == 0) && ([lindex $elem 2] == 0)} {
            # both disabled -> toggle the one which was not toggled above
            set idx [expr ($col_idx == 1) ? 2 : 1]
            set elem [lreplace $elem $idx $idx 1]
         }

         # update search stack, apply the filter and re-fill the listbox (triggered via trace)
         set substr_stack [lreplace $substr_stack $sel_idx $sel_idx $elem]

         set substr_pattern     [lindex $elem 0]
         set substr_grep_title  [lindex $elem 1]
         set substr_grep_descr  [lindex $elem 2]
         set substr_match_case  [lindex $elem 3]
         set substr_match_full  [lindex $elem 4]

         SubstrUpdateFilter 0
      }
   }
}

# callback for double mouse-click into "action" column
proc Substr_OpenContextMenu {w x y rootx rooty} {
   global substr_stack

   set sel_idx [.substr.all.stack.selist curselection]
   if {([llength $sel_idx] == 1) && ($sel_idx < [llength $substr_stack])} {
      catch {destroy .substr_ctxmen}
      menu .substr_ctxmen -tearoff 0

      # check which column was clicked into
      set col_tag [$w column nearest $x]
      set col_idx [lsearch [$w column names] $col_tag]
      if {$col_idx == 0} {
         .substr_ctxmen add command -label "Remove" -command Substr_RemoveFromStack
         tk_popup .substr_ctxmen $rootx $rooty
      } elseif {($col_idx >= 1) && ($col_idx <= 4)} {
         .substr_ctxmen add command -label "Toggle" -command [list Substr_ToggleListColumn $w $x $y $rootx $rooty]
         tk_popup .substr_ctxmen $rootx $rooty
      }
   }
}

# callback for Delete key in substr listbox
proc Substr_RemoveFromStack {} {
   global substr_stack

   set sel_idx [.substr.all.stack.selist curselection]
   if {([llength $sel_idx] == 1) && ($sel_idx < [llength $substr_stack])} {
      set substr_stack [lreplace $substr_stack $sel_idx $sel_idx]

      if {[llength $substr_stack] > 0} {
         # select next element in stack (note: doing this only to fill text entry field
         # with the respective text so that refill proc sets the cursor on the element)
         if {$sel_idx >= [llength $substr_stack]} {
            incr sel_idx -1
         }
         .substr.all.stack.selist selection set $sel_idx
         .substr.all.stack.selist see $sel_idx
         Substr_ListSelect
      }
      SubstrUpdateFilter 0
   }
}

# callback for change of selection in substr listbox
proc Substr_ListSelect {} {
   global substr_grep_title substr_grep_descr substr_match_case substr_match_full
   global substr_pattern
   global substr_stack

   set sel_idx [.substr.all.stack.selist curselection]
   if {[llength $sel_idx] == 1} {
      if {$sel_idx < [llength $substr_stack]} {
         set elem [lindex $substr_stack $sel_idx]

         set substr_pattern     [lindex $elem 0]
         set substr_grep_title  [lindex $elem 1]
         set substr_grep_descr  [lindex $elem 2]
         set substr_match_case  [lindex $elem 3]
         set substr_match_full  [lindex $elem 4]

         .substr.all.name.str icursor end
         .substr.all.name.str selection clear
      }
   }
}

proc Substr_TraceStackVar {n1 n2 v} {
   after cancel Substr_FillListbox
   after idle Substr_FillListbox
}

proc Substr_DialogClose {} {
   trace vdelete ::substr_stack w Substr_TraceStackVar
   destroy .substr
   catch {destroy .substr_paste}
}

# open sub-dialog with a text widget to allow paste
proc Substr_OpenPasteDialog {} {
   global substr_paste_popup substr_paste_fload
   global substr_stack

   if {$substr_paste_popup == 0} {
      CreateTransientPopup .substr_paste "Search text paste window" .substr
      wm resizable .substr_paste 1 1
      set substr_paste_popup 1

      frame  .substr_paste.top
      frame  .substr_paste.top.ftext
      scrollbar .substr_paste.top.ftext.sb -orient vertical -command {.substr_paste.top.ftext.text yview} -takefocus 0
      pack   .substr_paste.top.ftext.sb -side left -fill y
      text   .substr_paste.top.ftext.text -width 50 -height 20 -wrap none -font $::pi_font \
                                      -yscrollcommand {.substr_paste.top.ftext.sb set}
      pack   .substr_paste.top.ftext.text -side left -fill both -expand 1
      pack   .substr_paste.top.ftext -side top -fill both -expand 1

      frame  .substr_paste.top.topcmd
      button .substr_paste.top.topcmd.but_paste -text "Paste from clipboard" \
                                                -command {event generate .substr_paste.top.ftext.text <<Paste>>}
      pack   .substr_paste.top.topcmd.but_paste -side left -padx 5
      button .substr_paste.top.topcmd.but_clear -text "Clear" \
                                                -command {.substr_paste.top.ftext.text delete 1.0 end}
      pack   .substr_paste.top.topcmd.but_clear
      pack   .substr_paste.top.topcmd -side top -pady 5

      frame  .substr_paste.top.fload
      label  .substr_paste.top.fload.prompt -text "Load from file:"
      pack   .substr_paste.top.fload.prompt -side left
      entry  .substr_paste.top.fload.filename -textvariable substr_paste_fload -font $::font_fixed -width 30
      pack   .substr_paste.top.fload.filename -side left -padx 5 -fill x -expand 1
      bind   .substr_paste.top.fload.filename <Return> Substr_PasteFromFile
      bind   .substr_paste.top.fload.filename <Escape> {tkButtonInvoke .substr_paste.top.cmd.cancel}
      button .substr_paste.top.fload.dlgbut -image $::fileImage -command {
         if $::is_unix {set tmp_substr_ext {all {*}}} else {set tmp_substr_ext {all {*.*}}}
         set tmp_substr_fload \
                  [tk_getOpenFile -parent .substr_paste.top \
                     -initialfile [file tail $substr_paste_fload] \
                     -initialdir [file dirname $substr_paste_fload] \
                     -filetypes [list {TXT {*.txt}} $tmp_substr_ext]]
         if {[string length $tmp_substr_fload] > 0} {
            set substr_paste_fload $tmp_substr_fload
            Substr_PasteFromFile
         }
         unset tmp_substr_fload tmp_substr_ext
      }
      pack   .substr_paste.top.fload.dlgbut -side left -padx 5
      pack   .substr_paste.top.fload -side top -pady 5 -fill x
      pack   .substr_paste.top -side top -padx 5 -pady 5 -expand 1

      frame  .substr_paste.cmd
      button .substr_paste.cmd.cancel -text "Cancel" -command {destroy .substr_paste}
      button .substr_paste.cmd.ok -text "Ok" -command {Substr_AddTextToStack; destroy .substr_paste} -default active
      pack   .substr_paste.cmd.cancel .substr_paste.cmd.ok -side left -padx 10
      pack   .substr_paste.cmd -side top -pady 5

      bind   .substr_paste.cmd <Destroy> {+ set substr_paste_popup 0}
      bind   .substr_paste <Key-F1>      {PopupHelp $helpIndex(Filtering) "Text search"}
      bind   .substr_paste <Key-Escape>  {tkButtonInvoke .substr_paste.cmd.cancel}
      focus  .substr_paste.top.ftext.text

   } else {
      raise .substr_paste
   }

   .substr_paste.top.ftext.text delete 1.0 end
   foreach elem $substr_stack {
      .substr_paste.top.ftext.text insert end [lindex $elem 0]
      .substr_paste.top.ftext.text insert end "\n"
   }
   .substr_paste.top.ftext.text tag add sel 1.0 {end - 1 lines}
}

# callback for "OK" button in search text paste dialog: insert all text as sibstr stack
proc Substr_AddTextToStack {} {
   global substr_grep_title substr_grep_descr substr_match_case substr_match_full
   global substr_pattern
   global substr_stack

   if {![catch {.substr_paste.top.ftext.text get 1.0 end} lines]} {
      set ltmp {}
      while {[string length $lines] > 0} {
         set line_end [string first "\n" $lines]
         if {$line_end == -1} {
            set line_end [expr [string length $lines] - 1]
         } else {
            incr line_end -1
         }
         # compress white-space & remove space from beginning and end of line
         regsub -all {[\t ]+} [string range $lines 0 $line_end] { } line
         regsub {^[\t ]+} $line {} line
         regsub {[\t ]+$} $line {} line
         # skip empty lines
         if {[string length $line] > 0} {
            lappend ltmp [list $line $substr_grep_title $substr_grep_descr \
                               $substr_match_case $substr_match_full 0 0]
         }
         set lines [string range $lines [expr $line_end + 2] end]
      }
      set substr_stack $ltmp
      set substr_pattern [lindex [lindex $ltmp 0] 0]
      SubstrUpdateFilter 0
   }
}

# callback for "Return" in file name entry field: read file into text widget
proc Substr_PasteFromFile {} {
   global substr_paste_fload

   if {[string length $substr_paste_fload] > 0} {
      # attempt to open the given file for reading
      if {[catch {set fhandle [open $substr_paste_fload "r"]} errmsg] == 0} {
         set ttmp {}
         # read all text lines from the file
         while {[gets $fhandle line] >= 0} {
            # compress white-space & remove space from beginning and end of line
            regsub -all {[\t ]+} $line { } line
            regsub {^[\t ]+} $line {} line
            regsub {[\t ]+$} $line {} line
            # skip empty lines and comments (i.e. lines beginning with # as in UNIX scripts)
            if {([string length $line] > 0) && ![regexp -- {[ \t]*#} $line]} {
               append ttmp $line "\n"
            }
         }
         close $fhandle

         .substr_paste.top.ftext.text delete 1.0 end
         .substr_paste.top.ftext.text insert end $ttmp

      } else {
         tk_messageBox -parent .substr_paste -type ok -default ok -icon error \
                       -message "Cannot read file: $errmsg"
      }
   } else {
      tk_messageBox -parent .substr_paste -type ok -default ok -icon warning \
                    -message "Enter a file name first."
   }
}

