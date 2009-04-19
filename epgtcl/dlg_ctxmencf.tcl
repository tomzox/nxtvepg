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
#  $Id: dlg_ctxmencf.tcl,v 1.16 2009/03/29 18:32:04 tom Exp tom $
#
set ctxmencf_popup 0
set ctxmencf [list {pi_context.addfilt {} {}} \
                   {menu_separator {} {}} \
                   {pi_context.undofilt {} {}} \
                   {menu_separator {} {}} \
                   {pi_context.reminder_ext {} {}} \
                   {pi_context.reminder_disable {} {}}]
set tunetv_cmd_unix {tvapp.xawtv "Tune TV" {setstation ${network}}}
set tunetv_cmd_win {tvapp.wintv "Tune TV" {setstation ${network}}}
set ctxmencf_wintv_vcr 0


# Configuration data is stored in a list of triplets
# triplets consist of: type, title, command (if applicable)
#=CONST= ::ctxcf_type_idx        0
#=CONST= ::ctxcf_title_idx       1
#=CONST= ::ctxcf_cmd_idx         2
#=CONST= ::ctxcf_idx_count       3

#=CONST= ::ctxcf_selidx_tune     -100

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
            foreach {title cmd} [C_PiFilter_ContextMenuAddFilter] {
               $w add command -label $title -command $cmd
               incr entry_count
            }
         }
         pi_context.undofilt {
            foreach {title cmd} [C_PiFilter_ContextMenuUndoFilter] {
               $w add command -label $title -command $cmd
               incr entry_count
            }
         }
         pi_context.reminder_short {
            incr entry_count [C_PiRemind_ContextMenuShort $w]
         }
         pi_context.reminder_ext {
            incr entry_count [C_PiRemind_ContextMenuExtended $w]
         }
         pi_context.reminder_disable {
            incr entry_count [C_PiRemind_ContextMenuDisable $w]
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
## Execute the action for "Tune TV" button in the main window and reminders
## - optional netwop and start time params may be used to identify a
##   specific PI; else the currently selected PI is used
##
proc ExecuteTuneTvCommand {{netidx -1} {start_time -1}} {
   global is_unix tunetv_cmd_unix tunetv_cmd_win

   if $is_unix {
      set cmd $tunetv_cmd_unix
   } else {
      set cmd $tunetv_cmd_win
   }
   set type [lindex $cmd $::ctxcf_type_idx]
   set cmd  [lindex $cmd $::ctxcf_cmd_idx]

   if {$netidx != -1} {
      C_ExecUserCmd $type $cmd $netidx $start_time
   } else {
      C_ExecUserCmd $type $cmd
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
   global tunetv_cmd_unix tunetv_cmd_win ctxmencf_tune

   if {$ctxmencf_popup == 0} {
      CreateTransientPopup .ctxmencf "Context Menu Configuration"
      wm geometry  .ctxmencf "=400x400"
      wm minsize   .ctxmencf 400 400
      wm resizable .ctxmencf 1 1
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
      # load Tune-TV configuration
      if $is_unix {
         set ctxmencf_tune $tunetv_cmd_unix
      } else {
         set ctxmencf_tune $tunetv_cmd_win
      }

      # load special widget libraries
      rnotebook_load

      frame  .ctxmencf.all
      Rnotebook:create .ctxmencf.all.tab -tabs {"Context Menu" "Tune TV"} -borderwidth 2
      pack   .ctxmencf.all.tab -side top -pady 10 -fill both -expand 1
      set frm1 [Rnotebook:frame .ctxmencf.all.tab 1]
      set frm2 [Rnotebook:frame .ctxmencf.all.tab 2]

      # tab #1: section #1: listbox with overview of all defined titles
      label  ${frm1}.lab_main -text "Select commands to include in the context menu:"
      pack   ${frm1}.lab_main -side top -anchor w -pady 5

      frame  ${frm1}.lbox 
      scrollbar ${frm1}.lbox.sc -orient vertical -command [list ${frm1}.lbox.cmdlist yview] -takefocus 0
      pack   ${frm1}.lbox.sc -side left -fill y
      listbox ${frm1}.lbox.cmdlist -exportselection false -height 8 -width 0 \
                                   -selectmode single -yscrollcommand [list ${frm1}.lbox.sc set]
      relief_listbox ${frm1}.lbox.cmdlist
      bind   ${frm1}.lbox.cmdlist <<ListboxSelect>> {+ ContextMenuConfigSelection}
      pack   ${frm1}.lbox.cmdlist -side left -fill both -expand 1
      pack   ${frm1}.lbox -side top -fill both -expand 1

      # tab #1 section #2: command buttons to manipulate the list
      frame  ${frm1}.cmd
      menubutton ${frm1}.cmd.new -text "Add new" -menu ${frm1}.cmd.new.men -indicatoron 1
      config_menubutton ${frm1}.cmd.new
      menu   ${frm1}.cmd.new.men -tearoff 0
      pack   ${frm1}.cmd.new -side left -fill y
      menubutton ${frm1}.cmd.examples -text "Add example" -menu ${frm1}.cmd.examples.men -indicatoron 1
      config_menubutton ${frm1}.cmd.examples
      menu   ${frm1}.cmd.examples.men -tearoff 0
      pack   ${frm1}.cmd.examples -side left -fill y
      button ${frm1}.cmd.delcmd -text "Delete" -command ContextMenuConfigDelete -width 7
      pack   ${frm1}.cmd.delcmd -side left
      button ${frm1}.cmd.up -bitmap "bitmap_ptr_up" -command ContextMenuConfigShiftUp
      pack   ${frm1}.cmd.up -side left -fill y
      button ${frm1}.cmd.down -bitmap "bitmap_ptr_down" -command ContextMenuConfigShiftDown
      pack   ${frm1}.cmd.down -side left -fill y
      pack   ${frm1}.cmd -side top -pady 10

      # fill the type selection menu
      foreach type [C_ContextMenu_GetTypeList] {
         ${frm1}.cmd.new.men add command -label [ContextMenuGetTypeDescription $type] \
                                               -command [concat ContextMenuConfigAddNew $type]
      }

      # fill the example selection menu
      set idx 0
      foreach example [ContextMenuGetExamples] {
         ${frm1}.cmd.examples.men add command -label [lindex $example $::ctxcf_idx_count] \
                                                    -command [list ContextMenuAddExample $idx]
         incr idx
      }

      # tab #2: section #1: Tune-TV button binding
      label  ${frm2}.lab_main -text "Select a command to execute for the 'Tune TV' button\nand for double-clicking on programme entries:" -justify left
      pack   ${frm2}.lab_main -side top -anchor w -pady 5
      # insert expandable dummy frame so that command buttons stick to the bottom of the parent frame
      frame  ${frm2}.dummy
      pack   ${frm2}.dummy -fill both -expand 1

      # tab #2 section #2: command buttons to manipulate the button binding
      frame  ${frm2}.cmd
      ${frm2}.cmd conf -background red
      menubutton ${frm2}.cmd.new -text "Select type" -menu ${frm2}.cmd.new.men -indicatoron 1
      config_menubutton ${frm2}.cmd.new
      menu   ${frm2}.cmd.new.men -tearoff 0
      pack   ${frm2}.cmd.new -side left -fill y
      menubutton ${frm2}.cmd.examples -text "Reset to default" -menu ${frm2}.cmd.examples.men -indicatoron 1
      config_menubutton ${frm2}.cmd.examples
      menu   ${frm2}.cmd.examples.men -tearoff 0
      pack   ${frm2}.cmd.examples -side left -fill y
      pack   ${frm2}.cmd -side top -pady 10

      # fill the type selection menu
      if $is_unix {
         set tmp_type_list [list "exec.unix" "tvapp.xawtv"]
      } else {
         set tmp_type_list [list "exec.win32" "tvapp.wintv"]
      }
      foreach type $tmp_type_list {
         ${frm2}.cmd.new.men add command -label [ContextMenuGetTypeDescription $type] \
                                         -command [concat ContextMenuSetTuneTvCmd $type]
      }
      ${frm2}.cmd.examples.men add command -label "Tune station" -command ContextMenuSetTuneTvReset

      # section #3: entry field to modify or create commands
      set row 0
      frame .ctxmencf.all.edit -borderwidth 2 -relief groove
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

      bind $frm1 <Map> {ContextMenuConfigTabSelection 1}
      bind $frm2 <Map> {ContextMenuConfigTabSelection 2}

      # insert currently defined menu entries in the listbox
      foreach tag $ctxmencf_ord {
        ${frm1}.lbox.cmdlist insert end [ContextMenuGetListTitle $tag]
      }
      # place cursor on the first entry and display its data in the entry field
      set ctxmencf_selidx -1
      ${frm1}.lbox.cmdlist selection set 0
      ContextMenuConfigSelection

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
               "Change channel in connected TV app."] \
         [list tvapp.xawtv "Mute TV channel" \
               {volume mute} \
               "Mute audio connected TV app."]
   } else {
      lappend examples \
         [list tvapp.wintv "Record this show" \
               {record ${network} ${CNI} ${start} ${stop} ${VPS} ${title} ${themes:n}} \
               "Send 'record' command to TV app."] \
         [list tvapp.wintv "Tune this TV channel" \
               {setstation ${network}} \
               "Change channel in connected TV app."] \
         [list tvapp.wintv "Mute TV channel" \
               {volume mute} \
               "Mute audio connected TV app."]
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

      set frm1 [Rnotebook:frame .ctxmencf.all.tab 1]
      ${frm1}.lbox.cmdlist insert $ctxmencf_selidx [ContextMenuGetListTitle $tag]
      ${frm1}.lbox.cmdlist delete [expr $ctxmencf_selidx + 1]
      ${frm1}.lbox.cmdlist selection set $ctxmencf_selidx
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
      pi_context.reminder_disable {return "Disable this reminder group"}
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
   global ctxmencf_tune

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

            set frm1 [Rnotebook:frame .ctxmencf.all.tab 1]
            set oldsel [${frm1}.lbox.cmdlist curselection]
            ${frm1}.lbox.cmdlist insert $idx [ContextMenuGetListTitle $tag]
            ${frm1}.lbox.cmdlist delete [expr $idx + 1]
            ${frm1}.lbox.cmdlist selection set $oldsel
         }
      }
   } elseif {$idx == $::ctxcf_selidx_tune} {
      set ctxmencf_tune [list $ctxmencf_type "Tune TV" $ctxmencf_cmd]
   }
}

# callback for selection -> display title and command in the entry widgets
proc ContextMenuConfigSelection {} {
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd ctxmencf_type
   global ctxmencf_arr ctxmencf_ord

   ContextMenuConfigUpdate

   if {[llength $ctxmencf_ord] != 0} {
      set frm1 [Rnotebook:frame .ctxmencf.all.tab 1]
      set idx [${frm1}.lbox.cmdlist curselection]
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
   set frm1 [Rnotebook:frame .ctxmencf.all.tab 1]
   set idx [${frm1}.lbox.cmdlist curselection]
   if {([llength $idx] != 1) || ($idx < 0) || ($idx >= [llength $ctxmencf_ord])} {
      set idx [llength $ctxmencf_ord]
   } else {
      incr idx
   }

   if {$idx >= [llength $ctxmencf_ord]} {
      ${frm1}.lbox.cmdlist insert end [ContextMenuGetListTitle $new_tag]
      lappend ctxmencf_ord $new_tag
   } else {
      ${frm1}.lbox.cmdlist insert $idx [ContextMenuGetListTitle $new_tag]
      set ctxmencf_ord [linsert $ctxmencf_ord $idx $new_tag]
   }
   ${frm1}.lbox.cmdlist selection clear 0 end
   ${frm1}.lbox.cmdlist selection set $idx
   ${frm1}.lbox.cmdlist see $idx

   set ctxmencf_selidx -1
   ContextMenuConfigSelection

   return $new_tag
}

# callback for "Delete" button -> remove the selected entry
proc ContextMenuConfigDelete {} {
   global ctxmencf_selidx ctxmencf_arr ctxmencf_ord

   set frm1 [Rnotebook:frame .ctxmencf.all.tab 1]
   set idx [${frm1}.lbox.cmdlist curselection]
   if {($idx >= 0) && ($idx < [llength $ctxmencf_ord])} {
      set tag [lindex $ctxmencf_ord $idx]
      if [info exists ctxmencf_arr($tag)] {
         unset ctxmencf_arr($tag)
      }
      set ctxmencf_ord [lreplace $ctxmencf_ord $idx $idx]
      ${frm1}.lbox.cmdlist delete $idx

      # select the entry following the deleted one
      if {[llength $ctxmencf_ord] > 0} {
         if {$idx >= [llength $ctxmencf_ord]} {
            set idx [expr [llength $ctxmencf_ord] - 1]
         }
         ${frm1}.lbox.cmdlist selection set $idx
      }
      set ctxmencf_selidx -1
      ContextMenuConfigSelection
   }
}

# callback for "Up" button -> swap the selected entry with the one above it
proc ContextMenuConfigShiftUp {} {
   global ctxmencf_selidx ctxmencf_arr ctxmencf_ord

   ContextMenuConfigUpdate

   set frm1 [Rnotebook:frame .ctxmencf.all.tab 1]
   set idx [${frm1}.lbox.cmdlist curselection]
   if {($idx >= 0) && ($idx < [llength $ctxmencf_ord])} {
      set tag [lindex $ctxmencf_ord $idx]
      set swap_idx [expr $idx - 1]
      set swap_tag [lindex $ctxmencf_ord $swap_idx]
      if {($swap_idx >= 0) && ($swap_idx < [llength $ctxmencf_ord]) &&
          [info exists ctxmencf_arr($tag)] && [info exists ctxmencf_arr($swap_tag)]} {
         # remove the item in the listbox widget above the shifted one
         ${frm1}.lbox.cmdlist delete $swap_idx
         # re-insert the just removed title below the shifted one
         ${frm1}.lbox.cmdlist insert $idx [ContextMenuGetListTitle $swap_tag]
         ${frm1}.lbox.cmdlist see $swap_idx
         set ctxmencf_selidx $swap_idx

         # perform the same exchange in the associated list
         set ctxmencf_ord [lreplace $ctxmencf_ord $swap_idx $idx $tag $swap_tag]
      }
   }
}

# callback for "Down" button -> swap the selected entry with the one below it
proc ContextMenuConfigShiftDown {} {
   global ctxmencf_selidx ctxmencf_arr ctxmencf_ord
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd ctxmencf_type

   ContextMenuConfigUpdate

   set frm1 [Rnotebook:frame .ctxmencf.all.tab 1]
   set idx [${frm1}.lbox.cmdlist curselection]
   if {($idx >= 0) && ($idx < [llength $ctxmencf_ord])} {
      set tag [lindex $ctxmencf_ord $idx]
      set swap_idx [expr $idx + 1]
      set swap_tag [lindex $ctxmencf_ord $swap_idx]
      if {($swap_idx < [llength $ctxmencf_ord]) &&
          [info exists ctxmencf_arr($tag)] && [info exists ctxmencf_arr($swap_tag)]} {
         # remove the item in the listbox widget
         ${frm1}.lbox.cmdlist delete $swap_idx
         # re-insert the just removed title below the shifted one
         ${frm1}.lbox.cmdlist insert $idx [ContextMenuGetListTitle $swap_tag]
         ${frm1}.lbox.cmdlist see $swap_idx
         set ctxmencf_selidx $swap_idx

         # perform the same exchange in the associated list
         set ctxmencf_ord [lreplace $ctxmencf_ord $idx $swap_idx $swap_tag $tag]
      }
   }
}

# callback for "Select type" button in Tune-TV tab -> change type
proc ContextMenuSetTuneTvCmd {type} {
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd ctxmencf_type

   set ctxmencf_cmd {}
   set ctxmencf_type $type
   .ctxmencf.all.edit.type_sel configure -text [ContextMenuGetTypeDescription $ctxmencf_type]
}

# callback for "reset" button in Tune-TV tab: reset to default
proc ContextMenuSetTuneTvReset {} {
   global ctxmencf_title ctxmencf_cmd ctxmencf_type
   global is_unix

   if $is_unix {
      set ctxmencf_type "tvapp.xawtv"
   } else {
      set ctxmencf_type "tvapp.wintv"
   }
   set ctxmencf_cmd {setstation ${network}}

   .ctxmencf.all.edit.type_sel configure -text [ContextMenuGetTypeDescription $ctxmencf_type]
}

# callback for switch between the notebook pages
proc ContextMenuConfigTabSelection {tab_no} {
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd ctxmencf_type
   global ctxmencf_tune

   ContextMenuConfigUpdate

   if {$tab_no == 1} {
      set ctxmencf_selidx -1
      ContextMenuConfigSelection
   } else {
      set ctxmencf_selidx $::ctxcf_selidx_tune

      set ctxmencf_type   [lindex $ctxmencf_tune $::ctxcf_type_idx]
      .ctxmencf.all.edit.type_sel configure -text [ContextMenuGetTypeDescription $ctxmencf_type]

      set ctxmencf_title  {}
      .ctxmencf.all.edit.title_ent configure -state disabled

      .ctxmencf.all.edit.cmd_ent configure -state normal
      set ctxmencf_cmd    [lindex $ctxmencf_tune $::ctxcf_cmd_idx]
   }
}

# callback for Abort and OK buttons
proc ContextMenuConfigQuit {is_abort} {
   global ctxmencf ctxmencf_ord ctxmencf_arr
   global ctxmencf_selidx ctxmencf_title ctxmencf_cmd ctxmencf_type
   global tunetv_cmd_unix tunetv_cmd_win ctxmencf_tune is_unix

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
      if $is_unix {
         set old_tune_cf $tunetv_cmd_unix
      } else {
         set old_tune_cf $tunetv_cmd_win
      }
      if {([string compare $ctxmencf $newcf] != 0) || \
          ([string compare $old_tune_cf $ctxmencf_tune] != 0)} {
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

      # store Tune-TV configuration
      if $is_unix {
         set tunetv_cmd_unix $ctxmencf_tune
      } else {
         set tunetv_cmd_win $ctxmencf_tune
      }

      UpdateRcFile
   }

   if $do_quit {
      # free memory of global variables
      catch {unset ctxmencf_arr ctxmencf_ord}
      catch {unset ctxmencf_title ctxmencf_cmd ctxmencf_type}
      catch {unset ctxmencf_tune}
      # close the dialog window
      destroy .ctxmencf
   }
}

