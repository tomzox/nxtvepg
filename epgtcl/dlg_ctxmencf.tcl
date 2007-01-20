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
#  $Id: dlg_ctxmencf.tcl,v 1.11 2006/09/10 11:50:56 tom Exp $
#
set ctxmencf_popup 0
set ctxmencf [list {pi_context.addfilt {} {}} \
                   {menu_separator {} {}} \
                   {pi_context.undofilt {} {}} \
                   {menu_separator {} {}} \
                   {pi_context.reminder_ext {} {}}]
set ctxmencf_wintv_vcr 0


# Configuration data is stored in a list of triplets
# triplets consist of: type, title, command (if applicable)
#=CONST= ::ctxcf_type_idx        0
#=CONST= ::ctxcf_title_idx       1
#=CONST= ::ctxcf_cmd_idx         2
#=CONST= ::ctxcf_idx_count       3

## ---------------------------------------------------------------------------
## Create the context menu (after click with the right mouse button onto a PI)
##
proc PopupDynamicContextMenu {w unused} {
   global ctxmencf
   global is_unix

   set entry_count 0
   set idx 0
   foreach elem $ctxmencf {
      set type [lindex $elem $::ctxcf_type_idx]
      switch -exact $type {
         menu_separator {
            if {$entry_count > 0} {
               $w add separator
               set entry_count 0
            }
         }
         menu_title {
            $w add command -label [lindex $elem $::ctxcf_title_idx] -state disabled
            incr entry_count
         }
         pi_context.addfilt {
            incr entry_count [C_PiFilter_ContextMenuAddFilter $w]
         }
         pi_context.undofilt {
            incr entry_count [C_PiFilter_ContextMenuUndoFilter $w]
         }
         pi_context.reminder_short {
            incr entry_count [C_PiRemind_ContextMenuShort $w]
         }
         pi_context.reminder_ext {
            incr entry_count [C_PiRemind_ContextMenuExtended $w]
         }
         tvapp.wintv {
            if {!$is_unix && [C_Tvapp_IsConnected]} {
               $w add command -label [lindex $elem $::ctxcf_title_idx] \
                              -command [list C_ExecUserCmd $type [lindex $elem $::ctxcf_cmd_idx]]
               incr entry_count
            }
         }
         exec.win32 {
            if {!$is_unix} {
               $w add command -label [lindex $elem $::ctxcf_title_idx] \
                              -command [list C_ExecUserCmd $type [lindex $elem $::ctxcf_cmd_idx]]
               incr entry_count
            }
         }
         tvapp.xawtv -
         exec.unix {
            if $is_unix {
               $w add command -label [lindex $elem $::ctxcf_title_idx] \
                              -command [list C_ExecUserCmd $type [lindex $elem $::ctxcf_cmd_idx]]
               incr entry_count
            }
         }
      }
      incr idx
   }
}

## ---------------------------------------------------------------------------
## Add "record" menu entry to context menu
## - invoked when a TV app with record capabilities attaches (Windows only)
##
proc ContextMenuAddWintvVcr {} {
   global ctxmencf ctxmencf_wintv_vcr

   if {$ctxmencf_wintv_vcr == 0} {
      lappend ctxmencf [list tvapp.wintv \
                             {Record this show} \
                             {record ${network} ${CNI} ${start} ${stop} ${VPS} ${title} ${themes:n}}]

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
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd ctxmencf_type
   global ctxmencf ctxmencf_ord ctxmencf_arr

   if {$ctxmencf_popup == 0} {
      CreateTransientPopup .ctxmencf "Context Menu Configuration"
      set ctxmencf_popup 1

      # load configuration into temporary array
      # note: the indices generated here are used as unique IDs and
      #       do not represent the order in the listbox (only initially)
      set ctxmencf_ord {}
      set idx 0
      foreach elem $ctxmencf {
         set ctxmencf_arr($idx) $elem
         lappend ctxmencf_ord $idx
         incr idx
      }

      frame .ctxmencf.all
      label .ctxmencf.all.lab_main -text "Select commands to include in the context menu:"
      pack .ctxmencf.all.lab_main -side top -anchor w -pady 5

      # section #1: listbox with overview of all defined titles
      frame .ctxmencf.all.lbox 
      scrollbar .ctxmencf.all.lbox.sc -orient vertical -command {.ctxmencf.all.lbox.cmdlist yview} -takefocus 0
      pack .ctxmencf.all.lbox.sc -side left -fill y
      listbox .ctxmencf.all.lbox.cmdlist -exportselection false -height 10 -width 0 \
                                         -selectmode single -relief ridge \
                                         -yscrollcommand {.ctxmencf.all.lbox.sc set}
      bind .ctxmencf.all.lbox.cmdlist <<ListboxSelect>> {+ ContextMenuConfigSelection}
      pack .ctxmencf.all.lbox.cmdlist -side left -fill both -expand 1
      pack .ctxmencf.all.lbox -side top -fill both -expand 1

      # section #2: command buttons to manipulate the list
      frame  .ctxmencf.all.cmd
      menubutton .ctxmencf.all.cmd.new -takefocus 1 -relief raised -borderwidth 2 -indicatoron 1 \
                                       -highlightthickness 1 -highlightcolor $::win_frm_fg \
                                       -text "Add new" -menu .ctxmencf.all.cmd.new.men 
      menu   .ctxmencf.all.cmd.new.men -tearoff 0
      pack   .ctxmencf.all.cmd.new -side left -fill y
      menubutton .ctxmencf.all.cmd.examples -takefocus 1 -relief raised -borderwidth 2 -indicatoron 1 \
                                            -highlightthickness 1 -highlightcolor $::win_frm_fg \
                                            -text "Add example" -menu .ctxmencf.all.cmd.examples.men
      menu   .ctxmencf.all.cmd.examples.men -tearoff 0
      pack   .ctxmencf.all.cmd.examples -side left -fill y
      button .ctxmencf.all.cmd.delcmd -text "Delete" -command ContextMenuConfigDelete -width 7
      pack   .ctxmencf.all.cmd.delcmd -side left
      button .ctxmencf.all.cmd.up -bitmap "bitmap_ptr_up" -command ContextMenuConfigShiftUp
      pack   .ctxmencf.all.cmd.up -side left -fill y
      button .ctxmencf.all.cmd.down -bitmap "bitmap_ptr_down" -command ContextMenuConfigShiftDown
      pack   .ctxmencf.all.cmd.down -side left -fill y
      pack   .ctxmencf.all.cmd -side top -pady 10

      # fill the type selection menu
      foreach type [C_ContextMenu_GetTypeList] {
         .ctxmencf.all.cmd.new.men add command -label [ContextMenuGetTypeDescription $type] \
                                               -command [concat ContextMenuConfigAddNew $type]
      }

      # fill the example selection menu
      set idx 0
      foreach example [ContextMenuGetExamples] {
         .ctxmencf.all.cmd.examples.men add command -label [lindex $example $::ctxcf_idx_count] \
                                                    -command [list ContextMenuAddExample $idx]
         incr idx
      }

      # section #3: entry field to modify or create commands
      set row 0
      frame .ctxmencf.all.edit -borderwidth 2 -relief ridge
      label .ctxmencf.all.edit.type_lab -text "Type:"
      label .ctxmencf.all.edit.type_sel
      grid  .ctxmencf.all.edit.type_lab .ctxmencf.all.edit.type_sel -sticky w -row $row
      incr row
      label .ctxmencf.all.edit.title_lab -text "Title:"
      grid  .ctxmencf.all.edit.title_lab -sticky w -column 0 -row $row
      entry .ctxmencf.all.edit.title_ent -textvariable ctxmencf_title -width 50 -font $font_fixed
      bind  .ctxmencf.all.edit.title_ent <Enter> {SelectTextOnFocus %W}
      bind  .ctxmencf.all.edit.title_ent <Return> ContextMenuConfigUpdate
      grid  .ctxmencf.all.edit.title_ent -sticky we -column 1 -row $row
      incr row
      label .ctxmencf.all.edit.cmd_lab -text "Command:"
      grid  .ctxmencf.all.edit.cmd_lab -sticky w -column 0 -row $row
      entry .ctxmencf.all.edit.cmd_ent -textvariable ctxmencf_cmd -width 50 -font $font_fixed
      bind  .ctxmencf.all.edit.cmd_ent <Enter> {SelectTextOnFocus %W}
      bind  .ctxmencf.all.edit.cmd_ent <Return> ContextMenuConfigUpdate
      grid  .ctxmencf.all.edit.cmd_ent -sticky we -column 1 -row $row
      grid  columnconfigure .ctxmencf.all.edit 1 -weight 1
      pack  .ctxmencf.all.edit -side top -pady 5 -fill x
      pack  .ctxmencf.all -side top -pady 5 -padx 5 -fill both -expand 1

      # section #4: standard dialog buttons: Ok, Abort, Help
      frame .ctxmencf.cmd
      button .ctxmencf.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration) "Context menu configuration"}
      button .ctxmencf.cmd.abort -text "Abort" -width 5 -command {ContextMenuConfigQuit 1}
      button .ctxmencf.cmd.save -text "Ok" -width 5 -command {ContextMenuConfigQuit 0}
      pack .ctxmencf.cmd.help .ctxmencf.cmd.abort .ctxmencf.cmd.save -side left -padx 10
      pack .ctxmencf.cmd -side top -pady 5

      bind .ctxmencf <Key-F1> {PopupHelp $helpIndex(Configuration) "Context menu configuration"}
      bind .ctxmencf <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]
      bind .ctxmencf.cmd <Destroy> {+ set ctxmencf_popup 0}
      wm protocol .ctxmencf WM_DELETE_WINDOW {ContextMenuConfigQuit 1}
      focus .ctxmencf.all.edit.title_ent

      # display the entries in the listbox
      foreach tag $ctxmencf_ord {
        .ctxmencf.all.lbox.cmdlist insert end [ContextMenuGetListTitle $tag]
      }
      set ctxmencf_selidx -1
      .ctxmencf.all.lbox.cmdlist selection set 0
      ContextMenuConfigSelection

      wm resizable .ctxmencf 1 1
      update
      wm minsize .ctxmencf [winfo reqwidth .ctxmencf] [winfo reqheight .ctxmencf]
   } else {
      raise .ctxmencf
   }
}

# this proc returns a list of example commands
proc ContextMenuGetExamples {} {
   global is_unix

   set examples {}

   if $is_unix {
      lappend examples \
         [list exec.unix "Set reminder in plan" \
               {plan ${start:%d.%m.%Y %H:%M} ${title}\ \(${network}\)} \
               "Plan reminder"] \
         [list exec.unix "Set alarm timer" \
               {xalarm -time ${start:%H:%M} -date ${start:%b %d %Y} ${title}\ \(${network}\)} \
               "XAlarm timer"] \
         [list exec.unix "Demo: echo all variables" \
               {echo title=${title} network=${network} start=${start} stop=${stop} VPS/PDC=${VPS} duration=${duration} relstart=${relstart} CNI=${CNI} description=${description} themes=${themes} e_rating=${e_rating} p_rating=${p_rating} sound=${sound} format=${format} digital=${digital} encrypted=${encrypted} live=${live} repeat=${repeat} subtitle=${subtitle}} \
               "All variables"] \
         [list tvapp.xawtv "Tune this channel" \
               {setstation ${network}} \
               "Change channel in connected TV app."]
   } else {
      lappend examples \
         [list tvapp.wintv "Record this show" \
               {record ${network} ${CNI} ${start} ${stop} ${VPS} ${title} ${themes:n}} \
               "Send 'record' command to TV app."] \
         [list tvapp.wintv "Tune this TV channel" \
               {setstation ${network}} \
               "Change channel in connected TV app."]
   }
   return $examples
}

# callback for example popup menu: copy title and setting into entry fields
proc ContextMenuAddExample {index} {
   global ctxmencf_title ctxmencf_cmd ctxmencf_type
   global ctxmencf_arr ctxmencf_ord
   global ctxmencf_selidx

   ContextMenuConfigUpdate

   set examples [ContextMenuGetExamples]
   if {$index < [llength $examples]} {
      set example [lindex $examples $index]

      set tag [ContextMenuConfigAddNew [lindex $example $::ctxcf_type_idx]]

      set ctxmencf_arr($tag) [lrange $example 0 [expr $::ctxcf_idx_count - 1]]

      set ctxmencf_selidx -1
      ContextMenuConfigSelection

      .ctxmencf.all.lbox.cmdlist insert $ctxmencf_selidx [ContextMenuGetListTitle $tag]
      .ctxmencf.all.lbox.cmdlist delete [expr $ctxmencf_selidx + 1]
      .ctxmencf.all.lbox.cmdlist selection set $ctxmencf_selidx
   }
}

# get description for the given type
proc ContextMenuGetTypeDescription {type} {
   switch -exact $type {
      exec.unix {return "External command (UNIX)"}
      exec.win32 {return "External command (Windows)"}
      tvapp.xawtv {return "TV app. remote command (UNIX)"}
      tvapp.wintv {return "TV app. remote command (Windows)"}
      menu_separator {return "Menu separator"}
      menu_title {return "Menu title"}
      pi_context.addfilt {return "Add programme filters"}
      pi_context.undofilt {return "Undo programme filters"}
      pi_context.reminder_short {return "Add/remove reminder (short)"}
      pi_context.reminder_ext {return "Add/remove reminder (extended)"}
   }
}

# derive a title for the config menu selection listbox
proc ContextMenuGetListTitle {tag} {
   global ctxmencf_arr

   set type [lindex $ctxmencf_arr($tag) $::ctxcf_type_idx]
   set title [lindex $ctxmencf_arr($tag) $::ctxcf_title_idx]

   switch -glob $type {
      menu_separator {
         return "-- Separator --"
      }
      menu_title {
         return "-- $title --"
      }
      pi_context.* {
         return [ContextMenuGetTypeDescription $type]
      }
      default {
         return $title
      }
   }
}

# save options for the currently selected item
proc ContextMenuConfigUpdate {} {
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd ctxmencf_type
   global ctxmencf_arr ctxmencf_ord

   set idx $ctxmencf_selidx
   if {($idx >= 0) && ($idx < [llength $ctxmencf_ord])} {
      set tag [lindex $ctxmencf_ord $idx]

      if {([string compare $ctxmencf_title [lindex $ctxmencf_arr($tag) $::ctxcf_title_idx]] != 0) ||
          ([string compare $ctxmencf_cmd [lindex $ctxmencf_arr($tag) $::ctxcf_cmd_idx]] != 0)} {
         if [info exists ctxmencf_arr($tag)] {
            switch -exact $ctxmencf_type {
               exec.win32 -
               exec.unix -
               tvapp.xawtv -
               tvapp.wintv {
                  if [regexp {^!(xawtv|wintv)! *(.*)} $ctxmencf_cmd foo type_str cmd_str] {
                     set answer [tk_messageBox -type okcancel -icon warning -parent .ctxmencf \
                                               -message "Note use of prefixes such as !$type_str! is obsolete: select 'TV app. remote command' instead when creating new entries. Automatically remove the prefix now?"]
                     if {[string compare $answer "ok"] == 0} {
                        set ctxmencf_cmd $cmd_str
                        set ctxmencf_type "tvapp.$type_str"
                        set ctxmencf_arr($tag) [lreplace $ctxmencf_arr($tag) \
                                                         $::ctxcf_type_idx $::ctxcf_type_idx \
                                                         $ctxmencf_type]
                        .ctxmencf.all.edit.type_sel configure -text [ContextMenuGetTypeDescription $ctxmencf_type]
                     }
                  }
                  set ctxmencf_arr($tag) [lreplace $ctxmencf_arr($tag) \
                                                   $::ctxcf_title_idx $::ctxcf_title_idx \
                                                   $ctxmencf_title]
                  set ctxmencf_arr($tag) [lreplace $ctxmencf_arr($tag) \
                                                   $::ctxcf_cmd_idx $::ctxcf_cmd_idx \
                                                   $ctxmencf_cmd]
               }
               menu_title {
                  set ctxmencf_arr($tag) [lreplace $ctxmencf_arr($tag) \
                                                   $::ctxcf_title_idx $::ctxcf_title_idx \
                                                   $ctxmencf_title]
               }
            }

            set oldsel [.ctxmencf.all.lbox.cmdlist curselection]
            .ctxmencf.all.lbox.cmdlist insert $idx [ContextMenuGetListTitle $tag]
            .ctxmencf.all.lbox.cmdlist delete [expr $idx + 1]
            .ctxmencf.all.lbox.cmdlist selection set $oldsel
         }
      }
   }
}

# callback for selection -> display title and command in the entry widgets
proc ContextMenuConfigSelection {} {
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd ctxmencf_type
   global ctxmencf_arr ctxmencf_ord

   ContextMenuConfigUpdate

   if {[llength $ctxmencf_ord] != 0} {
      set idx [.ctxmencf.all.lbox.cmdlist curselection]
      if {([llength $idx] > 0) && ($idx < [llength $ctxmencf_ord])} {
         set tag [lindex $ctxmencf_ord $idx]
         if [info exists ctxmencf_arr($tag)] {
            set ctxmencf_selidx $idx
            set ctxmencf_type   [lindex $ctxmencf_arr($tag) $::ctxcf_type_idx]
            set ctxmencf_title  [lindex $ctxmencf_arr($tag) $::ctxcf_title_idx]
            set ctxmencf_cmd    [lindex $ctxmencf_arr($tag) $::ctxcf_cmd_idx]

            .ctxmencf.all.edit.type_sel configure -text [ContextMenuGetTypeDescription $ctxmencf_type]

            switch -glob $ctxmencf_type {
               menu_separator -
               pi_context.* {
                  .ctxmencf.all.edit.title_ent configure -state disabled
                  .ctxmencf.all.edit.cmd_ent configure -state disabled
               }
               menu_title {
                  .ctxmencf.all.edit.title_ent configure -state normal
                  .ctxmencf.all.edit.cmd_ent configure -state disabled
               }
               default {
                  .ctxmencf.all.edit.title_ent configure -state normal
                  .ctxmencf.all.edit.cmd_ent configure -state normal
               }
            }
         }
      }
   } else {
      set ctxmencf_selidx -1
      set ctxmencf_title {}
      set ctxmencf_cmd {}
      .ctxmencf.all.edit.title_ent configure -state disabled
      .ctxmencf.all.edit.cmd_ent configure -state disabled
   }
}

# callback for "New" button -> append new, empty entry of the given type
proc ContextMenuConfigAddNew {type} {
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd ctxmencf_type
   global ctxmencf_arr ctxmencf_ord

   # search an unique ID for the new element
   set new_tag 0
   foreach tag [array names ctxmencf_arr] {
      if {$tag >= $new_tag} {
         set new_tag [expr $tag + 1]
      }
   }

   set ctxmencf_type $type
   set ctxmencf_title {}
   set ctxmencf_cmd {}
   set ctxmencf_arr($new_tag) [list $ctxmencf_type $ctxmencf_title $ctxmencf_cmd]

   # get current cursor position
   set idx [.ctxmencf.all.lbox.cmdlist curselection]
   if {([llength $idx] != 1) || ($idx < 0) || ($idx >= [llength $ctxmencf_ord])} {
      set idx [llength $ctxmencf_ord]
   } else {
      incr idx
   }

   if {$idx >= [llength $ctxmencf_ord]} {
      .ctxmencf.all.lbox.cmdlist insert end [ContextMenuGetListTitle $new_tag]
      lappend ctxmencf_ord $new_tag
   } else {
      .ctxmencf.all.lbox.cmdlist insert $idx [ContextMenuGetListTitle $new_tag]
      set ctxmencf_ord [linsert $ctxmencf_ord $idx $new_tag]
   }
   .ctxmencf.all.lbox.cmdlist selection clear 0 end
   .ctxmencf.all.lbox.cmdlist selection set $idx
   .ctxmencf.all.lbox.cmdlist see $idx

   set ctxmencf_selidx -1
   ContextMenuConfigSelection

   return $new_tag
}

# callback for "Delete" button -> remove the selected entry
proc ContextMenuConfigDelete {} {
   global ctxmencf_selidx ctxmencf_arr ctxmencf_ord

   set idx [.ctxmencf.all.lbox.cmdlist curselection]
   if {($idx >= 0) && ($idx < [llength $ctxmencf_ord])} {
      set tag [lindex $ctxmencf_ord $idx]
      if [info exists ctxmencf_arr($tag)] {
         unset ctxmencf_arr($tag)
      }
      set ctxmencf_ord [lreplace $ctxmencf_ord $idx $idx]
      .ctxmencf.all.lbox.cmdlist delete $idx

      # select the entry following the deleted one
      if {[llength $ctxmencf_ord] > 0} {
         if {$idx >= [llength $ctxmencf_ord]} {
            set idx [expr [llength $ctxmencf_ord] - 1]
         }
         .ctxmencf.all.lbox.cmdlist selection set $idx
      }
      set ctxmencf_selidx -1
      ContextMenuConfigSelection
   }
}

# callback for "Up" button -> swap the selected entry with the one above it
proc ContextMenuConfigShiftUp {} {
   global ctxmencf_selidx ctxmencf_arr ctxmencf_ord

   ContextMenuConfigUpdate

   set idx [.ctxmencf.all.lbox.cmdlist curselection]
   if {($idx >= 0) && ($idx < [llength $ctxmencf_ord])} {
      set tag [lindex $ctxmencf_ord $idx]
      set swap_idx [expr $idx - 1]
      set swap_tag [lindex $ctxmencf_ord $swap_idx]
      if {($swap_idx >= 0) && ($swap_idx < [llength $ctxmencf_ord]) &&
          [info exists ctxmencf_arr($tag)] && [info exists ctxmencf_arr($swap_tag)]} {
         # remove the item in the listbox widget above the shifted one
         .ctxmencf.all.lbox.cmdlist delete $swap_idx
         # re-insert the just removed title below the shifted one
         .ctxmencf.all.lbox.cmdlist insert $idx [ContextMenuGetListTitle $swap_tag]
         .ctxmencf.all.lbox.cmdlist see $swap_idx
         set ctxmencf_selidx $swap_idx

         # perform the same exchange in the associated list
         set ctxmencf_ord [lreplace $ctxmencf_ord $swap_idx $idx $tag $swap_tag]
      }
   }
}

# callback for "Down" button -> swap the selected entry with the one below it
proc ContextMenuConfigShiftDown {} {
   global ctxmencf_selidx ctxmencf_arr ctxmencf_ord

   ContextMenuConfigUpdate

   set idx [.ctxmencf.all.lbox.cmdlist curselection]
   if {($idx >= 0) && ($idx < [llength $ctxmencf_ord])} {
      set tag [lindex $ctxmencf_ord $idx]
      set swap_idx [expr $idx + 1]
      set swap_tag [lindex $ctxmencf_ord $swap_idx]
      if {($swap_idx < [llength $ctxmencf_ord]) &&
          [info exists ctxmencf_arr($tag)] && [info exists ctxmencf_arr($swap_tag)]} {
         # remove the item in the listbox widget
         .ctxmencf.all.lbox.cmdlist delete $swap_idx
         # re-insert the just removed title below the shifted one
         .ctxmencf.all.lbox.cmdlist insert $idx [ContextMenuGetListTitle $swap_tag]
         .ctxmencf.all.lbox.cmdlist see $swap_idx
         set ctxmencf_selidx $swap_idx

         # perform the same exchange in the associated list
         set ctxmencf_ord [lreplace $ctxmencf_ord $idx $swap_idx $swap_tag $tag]
      }
   }
}

# callback for Abort and OK buttons
proc ContextMenuConfigQuit {is_abort} {
   global ctxmencf ctxmencf_ord ctxmencf_arr
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd ctxmencf_type

   ContextMenuConfigUpdate

   set do_quit 1
   # create config array from the listbox content
   set newcf {}
   foreach idx $ctxmencf_ord {
      if [info exists ctxmencf_arr($idx)] {
         lappend newcf $ctxmencf_arr($idx)
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
      catch {unset ctxmencf_arr ctxmencf_ord}
      catch {unset ctxmencf_title ctxmencf_cmd ctxmencf_type}
      # close the dialog window
      destroy .ctxmencf
   }
}

