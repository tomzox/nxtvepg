#
#  Configuration dialog for user-defined entries in the context menu
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
#    Implements a configuration dialog that allows to manage user-defined
#    entries in the context menu in the PI listbox.
#
#  Author: Tom Zoerner
#
#  $Id: dlg_ctxmencf.tcl,v 1.2 2002/11/03 16:02:42 tom Exp tom $
#
set ctxmencf_popup 0
set ctxmencf {}
set ctxmencf_wintv_vcr 0

## ---------------------------------------------------------------------------
## Add "record" menu entry to context menu
## - invoked when a TV app with record capabilities attaches (Windows only)
##
proc ContextMenuAddWintvVcr {} {
   global ctxmencf ctxmencf_wintv_vcr

   if {$ctxmencf_wintv_vcr == 0} {
      lappend ctxmencf {Record this show} {!wintv! record ${network} ${CNI} ${start} ${stop} ${VPS} ${title} ${themes:n}}

      # remember that this entry was automatically added
      # it will not be added again in the future (to allow the user to remove it)
      set ctxmencf_wintv_vcr 1

      UpdateRcFile
   }
}

#=LOAD=ContextMenuConfigPopup
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Creates the Context menu popup configuration dialog
##
proc ContextMenuConfigPopup {} {
   global ctxmencf_popup
   global default_bg font_fixed is_unix
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd
   global ctxmencf ctxmencf_ord ctxmencf_arr

   if {$ctxmencf_popup == 0} {
      CreateTransientPopup .ctxmencf "Context Menu Configuration"
      set ctxmencf_popup 1

      # load configuration into temporary array
      # note: the indices generated here are used as unique IDs and
      #       do not represent the order in the listbox (only initially)
      set ctxmencf_ord {}
      set idx 0
      foreach {title cmd} $ctxmencf {
         set ctxmencf_arr($idx) [list $title $cmd]
         lappend ctxmencf_ord $idx
         incr idx
      }

      frame .ctxmencf.all
      label .ctxmencf.all.lab_main -text "Add user-defined commands:"
      pack .ctxmencf.all.lab_main -side top -anchor w -pady 5

      # section #1: listbox with overview of all defined titles
      frame .ctxmencf.all.lbox 
      scrollbar .ctxmencf.all.lbox.sc -orient vertical -command {.ctxmencf.all.lbox.cmdlist yview}
      pack .ctxmencf.all.lbox.sc -side left -fill y
      listbox .ctxmencf.all.lbox.cmdlist -exportselection false -height 5 -width 0 \
                                         -selectmode single -relief ridge \
                                         -yscrollcommand {.ctxmencf.all.lbox.sc set}
      bind .ctxmencf.all.lbox.cmdlist <ButtonRelease-1> {ContextMenuConfigSelection 1}
      bind .ctxmencf.all.lbox.cmdlist <KeyRelease-space> {ContextMenuConfigSelection 1}
      pack .ctxmencf.all.lbox.cmdlist -side left -fill both -expand 1
      pack .ctxmencf.all.lbox -side top -fill both -expand 1

      # section #2: command buttons to manipulate the list
      frame  .ctxmencf.all.cmd
      button .ctxmencf.all.cmd.new -text "new" -command ContextMenuConfigAddNew -width 7
      pack   .ctxmencf.all.cmd.new -side left -anchor nw
      button .ctxmencf.all.cmd.upd -text "update" -command ContextMenuConfigUpdate -width 7
      pack   .ctxmencf.all.cmd.upd -side left -anchor nw
      button .ctxmencf.all.cmd.delcmd -text "delete" -command ContextMenuConfigDelete -width 7
      pack   .ctxmencf.all.cmd.delcmd -side left -anchor nw
      button .ctxmencf.all.cmd.up -bitmap "bitmap_ptr_up" -command ContextMenuConfigShiftUp
      pack   .ctxmencf.all.cmd.up -side left -fill y
      button .ctxmencf.all.cmd.down -bitmap "bitmap_ptr_down" -command ContextMenuConfigShiftDown
      pack   .ctxmencf.all.cmd.down -side left -fill y
      button .ctxmencf.all.cmd.clear -text "clear" -command ContextMenuConfigClearEntry -width 7
      pack   .ctxmencf.all.cmd.clear -side left -fill y

      menubutton .ctxmencf.all.cmd.examples -text "Copy example" -menu .ctxmencf.all.cmd.examples.men -borderwidth 2 -relief raised
      pack .ctxmencf.all.cmd.examples -side left -fill y
      menu .ctxmencf.all.cmd.examples.men -tearoff 0
      .ctxmencf.all.cmd.examples.men add command -label "Plan reminder" -command {
         set ctxmencf_title "Set reminder in plan";
         set ctxmencf_cmd   {plan ${start:%d.%m.%Y %H:%M} ${title}\ \(${network}\)}
      }
      .ctxmencf.all.cmd.examples.men add command -label "XAlarm timer" -command {
         set ctxmencf_title "Set alarm timer";
         set ctxmencf_cmd   {xalarm -time ${start:%H:%M} -date ${start:%b %d %Y} ${title}\ \(${network}\)}
      }
      .ctxmencf.all.cmd.examples.men add command -label "All variables" -command {
         set ctxmencf_title "Demo: echo all variables";
         set ctxmencf_cmd   {echo title=${title} network=${network} start=${start} stop=${stop} VPS/PDC=${VPS} duration=${duration} relstart=${relstart} CNI=${CNI} description=${description} themes=${themes} e_rating=${e_rating} p_rating=${p_rating} sound=${sound} format=${format} digital=${digital} encrypted=${encrypted} live=${live} repeat=${repeat} subtitle=${subtitle}}
      }
      if {!$is_unix} {
         .ctxmencf.all.cmd.examples.men add command -label "Send 'record' command to TV app." -command {
            set ctxmencf_title "Record this show";
            set ctxmencf_cmd   {!wintv! record ${network} ${CNI} ${start} ${stop} ${VPS} ${title} ${themes:n}}
         }
         .ctxmencf.all.cmd.examples.men add command -label "Change channel in connected TV app." -command {
            set ctxmencf_title "Tune this channel";
            set ctxmencf_cmd   {!wintv! setstation ${network}}
         }
      }
      pack .ctxmencf.all.cmd -side top -pady 10

      # section #3: entry field to modify or create commands
      frame .ctxmencf.all.edit -borderwidth 2 -relief ridge
      label .ctxmencf.all.edit.title_lab -text "Title:"
      entry .ctxmencf.all.edit.title_ent -textvariable ctxmencf_title -width 50 -font $font_fixed
      grid  .ctxmencf.all.edit.title_lab .ctxmencf.all.edit.title_ent -sticky we
      label .ctxmencf.all.edit.cmd_lab -text "Command:"
      entry .ctxmencf.all.edit.cmd_ent -textvariable ctxmencf_cmd -width 50 -font $font_fixed
      grid  .ctxmencf.all.edit.cmd_lab .ctxmencf.all.edit.cmd_ent -sticky we
      grid  columnconfigure .ctxmencf.all.edit 1 -weight 1
      pack  .ctxmencf.all.edit -side top -pady 5 -fill x
      pack  .ctxmencf.all -side top -pady 5 -padx 5 -fill both -expand 1

      # section #4: standard dialog buttons: Ok, Abort, Help
      frame .ctxmencf.cmd
      button .ctxmencf.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration) "Context menu"}
      button .ctxmencf.cmd.abort -text "Abort" -width 5 -command {ContextMenuConfigQuit 1}
      button .ctxmencf.cmd.save -text "Ok" -width 5 -command {ContextMenuConfigQuit 0}
      pack .ctxmencf.cmd.help .ctxmencf.cmd.abort .ctxmencf.cmd.save -side left -padx 10
      pack .ctxmencf.cmd -side top -pady 5

      bind .ctxmencf <Key-F1> {PopupHelp $helpIndex(Configuration) "Context menu"}
      bind .ctxmencf.cmd <Destroy> {+ set ctxmencf_popup 0}
      focus .ctxmencf.all.edit.title_ent

      # display the entries in the listbox
      foreach idx $ctxmencf_ord {
        .ctxmencf.all.lbox.cmdlist insert end [lindex $ctxmencf_arr($idx) 0]
      }
      set ctxmencf_title {}
      set ctxmencf_cmd {}
      set ctxmencf_selidx -1

      wm resizable .ctxmencf 1 1
      update
      wm minsize .ctxmencf [winfo reqwidth .ctxmencf] [winfo reqheight .ctxmencf]
   } else {
      raise .ctxmencf
   }
}

# helper function: check if there are unsaved changes in the entry fields
# returns TRUE if user wants to cancel the operation
proc ContextMenuCompareInput {} {
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd
   global ctxmencf_arr ctxmencf_ord

   set title_change 0
   set cmd_change 0
   set result 0

   if {([string length $ctxmencf_title] != 0) ||
       ([string length $ctxmencf_cmd] != 0)} {
      set idx $ctxmencf_selidx
      if {($idx >= 0) && ($idx < [llength $ctxmencf_ord])} {
         # one item is selected -> compare entry fiels content with its title and cmd
         set id [lindex $ctxmencf_ord $idx]
         if [info exists ctxmencf_arr($id)] {
            if {([string compare $ctxmencf_title [lindex $ctxmencf_arr($id) 0]] != 0) ||
                ([string compare $ctxmencf_cmd [lindex $ctxmencf_arr($id) 1]] != 0)} {
               # title and/or command string have been changed
               set answer [tk_messageBox -type okcancel -icon question -parent .ctxmencf \
                             -message "Discard unsaved changes in the title and command entry fields?\nNote: You can use button 'Update' to save them into the selected context menu entry."]
               set result [expr [string compare $answer "ok"] != 0]
            }
         }
      } else {
         # no item is currently selected
         set answer [tk_messageBox -type okcancel -icon question -parent .ctxmencf \
                       -message "Discard unsaved changes in the title and command entry fields?\nNote: You can use button 'New' to create a new context menu entry."]
         set result [expr [string compare $answer "ok"] != 0]
      }
   }
   return $result
}

# callback for selection -> display title and command in the entry widgets
proc ContextMenuConfigSelection {do_check} {
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd
   global ctxmencf_arr ctxmencf_ord

   if {$do_check && [ContextMenuCompareInput]} return

   set idx [.ctxmencf.all.lbox.cmdlist curselection]
   if {([llength $idx] > 0) && ($idx < [llength $ctxmencf_ord])} {
      set id [lindex $ctxmencf_ord $idx]
      if [info exists ctxmencf_arr($id)] {
         set ctxmencf_selidx $idx
         set ctxmencf_title  [lindex $ctxmencf_arr($id) 0]
         set ctxmencf_cmd    [lindex $ctxmencf_arr($id) 1]
      }
   }
}

# helper function for add and update: check if the input in the entry fields are ok
proc ContextMenuConfigCheckInput {is_update} {
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd
   global ctxmencf_arr ctxmencf_ord
   set result 1

   if {([string length $ctxmencf_title] == 0) || \
       ([string length $ctxmencf_cmd] == 0)} {
      tk_messageBox -type ok -default ok -icon error -parent .ctxmencf \
                    -message "You need to type a title and command into the entry fields."
      set result 0
   } elseif {!$is_update} {
      set found 0
      foreach id $ctxmencf_ord {
         if {[info exists ctxmencf_arr($id)] &&
             ([string compare $ctxmencf_title [lindex $ctxmencf_arr($id) 0]] == 0)} {
            incr found
         }
      }
      if $found {
         set answer [tk_messageBox -type okcancel -default ok -icon warning -parent .ctxmencf \
                        -message "You already have defined an entry with the same title!"]
         set result [expr [string compare $answer "ok"] == 0]
      }
   }
   return $result
}

# callback for "Update" button -> replace the selected entry witht the entry's content
proc ContextMenuConfigUpdate {} {
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd
   global ctxmencf_arr ctxmencf_ord

   set idx $ctxmencf_selidx
   if {($idx >= 0) && ($idx < [llength $ctxmencf_ord])} {
      set id [lindex $ctxmencf_ord $idx]
      if [info exists ctxmencf_arr($id)] {
         if [ContextMenuConfigCheckInput 1] {
            set ctxmencf_arr($id) [list $ctxmencf_title $ctxmencf_cmd]
            .ctxmencf.all.lbox.cmdlist delete $idx
            .ctxmencf.all.lbox.cmdlist insert $idx $ctxmencf_title
            .ctxmencf.all.lbox.cmdlist selection set $idx
         }
      }
   }
}

# callback for "New" button -> append the text from the entry fields to the list
proc ContextMenuConfigAddNew {} {
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd
   global ctxmencf_arr ctxmencf_ord

   if [ContextMenuConfigCheckInput 0] {
      set id [llength $ctxmencf_ord]
      lappend ctxmencf_ord $id
      set ctxmencf_arr($id) [list $ctxmencf_title $ctxmencf_cmd]
      .ctxmencf.all.lbox.cmdlist insert end $ctxmencf_title
      .ctxmencf.all.lbox.cmdlist selection clear 0 end
      .ctxmencf.all.lbox.cmdlist selection set $id
      set ctxmencf_selidx $id
   }
}

# callback for "Delete" button -> remove the selected entry
proc ContextMenuConfigDelete {} {
   global ctxmencf_selidx ctxmencf_arr ctxmencf_ord

   set idx $ctxmencf_selidx
   if {($idx >= 0) && ($idx < [llength $ctxmencf_ord])} {
      set id [lindex $ctxmencf_ord $idx]
      if [info exists ctxmencf_arr($id)] {
         unset ctxmencf_arr($id)
      }
      set ctxmencf_ord [lreplace $ctxmencf_ord $idx $idx]
      .ctxmencf.all.lbox.cmdlist delete $idx

      # select the entry following the deleted one
      if {[llength $ctxmencf_ord] > 0} {
         if {$idx >= [llength $ctxmencf_ord]} {
            set idx [expr [llength $ctxmencf_ord] - 1]
         }
         .ctxmencf.all.lbox.cmdlist selection set $idx
         set ctxmencf_selidx $idx
         ContextMenuConfigSelection 0
      } else {
         # no item left to select -> clear the entry fields
         ContextMenuConfigClearEntry
      }
   }
}

# callback for "Up" button -> swap the selected entry with the one above it
proc ContextMenuConfigShiftUp {} {
   global ctxmencf_selidx ctxmencf_arr ctxmencf_ord

   if [ContextMenuCompareInput] return

   set idx $ctxmencf_selidx
   if {($idx >= 0) && ($idx < [llength $ctxmencf_ord])} {
      set id [lindex $ctxmencf_ord $idx]
      set swap_idx [expr $idx - 1]
      set swap_id [lindex $ctxmencf_ord $swap_idx]
      if {($swap_idx >= 0) && ($swap_idx < [llength $ctxmencf_ord]) &&
          [info exists ctxmencf_arr($id)] && [info exists ctxmencf_arr($swap_id)]} {
         # remove the item in the listbox widget above the shifted one
         .ctxmencf.all.lbox.cmdlist delete $swap_idx
         # re-insert the just removed title below the shifted one
         .ctxmencf.all.lbox.cmdlist insert $idx [lindex $ctxmencf_arr($swap_id) 0]
         .ctxmencf.all.lbox.cmdlist selection set $swap_idx
         set ctxmencf_selidx $swap_idx

         # perform the same exchange in the associated list
         set ctxmencf_ord [lreplace $ctxmencf_ord $swap_idx $idx $id $swap_id]
      }
   }
}

# callback for "Down" button -> swap the selected entry with the one below it
proc ContextMenuConfigShiftDown {} {
   global ctxmencf_selidx ctxmencf_arr ctxmencf_ord

   if [ContextMenuCompareInput] return

   set idx $ctxmencf_selidx
   if {($idx >= 0) && ($idx < [llength $ctxmencf_ord])} {
      set id [lindex $ctxmencf_ord $idx]
      set swap_idx [expr $idx + 1]
      set swap_id [lindex $ctxmencf_ord $swap_idx]
      if {($swap_idx < [llength $ctxmencf_ord]) &&
          [info exists ctxmencf_arr($id)] && [info exists ctxmencf_arr($swap_id)]} {
         # remove the item in the listbox widget
         .ctxmencf.all.lbox.cmdlist delete $idx
         # re-insert the just removed title below the shifted one
         .ctxmencf.all.lbox.cmdlist insert $swap_idx [lindex $ctxmencf_arr($id) 0]
         .ctxmencf.all.lbox.cmdlist selection clear 0 end
         .ctxmencf.all.lbox.cmdlist selection set $swap_idx
         set ctxmencf_selidx $swap_idx

         # perform the same exchange in the associated list
         set ctxmencf_ord [lreplace $ctxmencf_ord $idx $swap_idx $swap_id $id]
      }
   }
}

# callback for "Clear" button -> clear the entry fields
proc ContextMenuConfigClearEntry {} {
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd

   set ctxmencf_title {}
   set ctxmencf_cmd   {}
   set ctxmencf_selidx -1
   .ctxmencf.all.lbox.cmdlist selection clear 0 end
}

# callback for Abort and OK buttons
proc ContextMenuConfigQuit {is_abort} {
   global ctxmencf ctxmencf_ord ctxmencf_arr
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd

   if [ContextMenuCompareInput] return

   set do_quit 1
   # create config array from the listbox content
   set newcf {}
   foreach idx $ctxmencf_ord {
      if [info exists ctxmencf_arr($idx)] {
         lappend newcf [lindex $ctxmencf_arr($idx) 0] [lindex $ctxmencf_arr($idx) 1]
      }
   }

   if $is_abort {
      # Abort button: compare the new config with the previous one
      if {[string compare $ctxmencf $newcf] != 0} {
         # ask the user for confirmation to discard any changes he made
         set answer [tk_messageBox -type okcancel -icon warning -parent .ctxmencf \
                                   -message "Discard changes?"]
         if {[string compare $answer "ok"] != 0} {
            set do_quit 0
         }
      }
   } else {
      # Ok button: save the new config into the global variable and the rc/ini file
      set ctxmencf $newcf
      UpdateRcFile
   }

   if $do_quit {
      # free memory of global variables
      if [info exists ctxmencf_arr] {unset ctxmencf_arr}
      unset ctxmencf_ord
      unset ctxmencf_title ctxmencf_cmd
      # close the dialog window
      destroy .ctxmencf
   }
}

