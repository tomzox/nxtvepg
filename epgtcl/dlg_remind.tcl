#
#  Nextview EPG reminder handling
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
#    Implements reminder event handling, message popup and dialogs for
#    maintaining reminders.
#
#    Reminder specs:
#    - Two types of reminders are supported:
#      + single (CNI+VPS[+Title] or CNI+Start+Stop+Title)
#      + filter shortcut
#    - Reminders can be used in the following ways:
#      + at start minus X,Y,Z: message AND/OR script;
#        with "snooze" offset for individual events
#      + [NOT: at stop +/- X,Y,Z: can be done by ext. script using start+duration]
#      + background search: collect & report new matches (TODO as of v2.6.0)
#      + filter, i.e. list only matching programmes (can NOT be saved in shortcuts)
#      + attribute (pre- or user-defined column), i.e. mark matching PI in listbox
#    - The above usage requires the following filter capabilities:
#      + search first match among all groups to install timer
#      + filter pibox for reminders; may be combined with other filters
#      + compound attribute display
#      + background search on incoming PI: only on text & shortcut types
#    - Reminder edit dialog:
#      + list all reminders (separated by types)
#      + add shortcut as reminder
#      + delete reminder (even if no match on database)
#      + edit priority
#    - Group config menu:
#      + add/delete group (make sure at least one is always defined)
#      + edit group names
#      + en-/disable message display & set pre-warn offsets & choose GUI/TV display
#      + en-/disable script & set command (with ${} var subst); offset to start;
#        ask OK/cancel before
#      + en-/disable messages & scripts
#      + en-/disable background match reporting
#      + en-/disable use of VPS to detect actual start (time off 0 only)
#        problems: need 2nd TV tuner; nxtvepg doesn't have channel table
#
#  Author: Tom Zoerner
#
#  $Id: dlg_remind.tcl,v 1.23 2007/12/29 21:09:16 tom Exp tom $
#
# import constants from other modules
#=INCLUDE= "epgtcl/dlg_udefcols.h"
#=INCLUDE= "epgtcl/shortcuts.h"

# persistant data (saved to/loaded from rc/ini file)
set reminders {}
set rem_msg_cnf_time  0
set rem_cmd_cnf_time  0
set remlist_winsize "700x320"
set rempilist_colsize {4 10 10 13 24 12}
set remsclist_colsize {10 20}

# temporary data
set remlist_popup 0
set remcfg_popup 0
set remalarm_popup 0
set rem_msg_disp_time 0
set rem_msg_base_time 0
set rem_timer_id {}
set remgroup_filter {}

# default format for pre-defined reminder pibox column: matches all reminders
# this list is constant except for the filter cache index
# note: user can create his own column with different markers per group
# {type=image, image=circle, format=bg-color, cache index=set dynamically, sc tag=pseudo}
set rem_col_fmt {{1 circle_red bg_RGBFFD0D0 -1 rgp_all}}

# constant data (substituted for variable references by tcl2c preprocessor)

# Reminders for single PI are stored in a plain list
# each element is again a list with the following elements:
# - group mask (1<<ID), - network CNI, - network index in current AI,
# - VPS/PDC PIL if available, - start/stop times, - programme title
# note: indices also used at C level in piremind.c module
#=CONST= ::rpi_grptag_idx    0
#=CONST= ::rpi_ctrl_idx      1
#=CONST= ::rpi_cni_idx       2
#=CONST= ::rpi_netwop_idx    3
#=CONST= ::rpi_pil_idx       4
#=CONST= ::rpi_start_idx     5
#=CONST= ::rpi_stop_idx      6
#=CONST= ::rpi_title_idx     7
#=CONST= ::rpi_idx_count     8

# control values stored in the lower byte of the reminder control element
# note: indices also used at C level in piremind.c module
#=CONST= ::rpi_ctrl_default  0
#=CONST= ::rpi_ctrl_new      1
#=CONST= ::rpi_ctrl_suppress 2
#=CONST= ::rpi_ctrl_repeat   3
#=CONST= ::rpi_ctrl_count    4
#=CONST= ::rpi_ctrl_mask     3

# group configuration is stored in a hash (max. 31; tag 0 reserved for suppress group)
# indices are used in filter bitmask, hence the limited index range
# note: indices also used at C level in piremind.c module
#=CONST= ::rgp_name_idx      0
#=CONST= ::rgp_msg_idx       1
#=CONST= ::rgp_cmd_idx       2
#=CONST= ::rgp_resvd_idx     3
#=CONST= ::rgp_acqreport_idx 4
#=CONST= ::rgp_disable_idx   5
#=CONST= ::rgp_sclist_idx    6
#=CONST= ::rgp_ctxcache_idx  7
#=CONST= ::rgp_idx_count     8

# messages and command parameters in group config are lists of three elements
#=CONST= ::ract_toffl_idx    0
#=CONST= ::ract_argv_idx     1
#=CONST= ::ract_ask_idx      2

# events are stored in a list of lists when we have to wait for confirmation
# note: idx #7 only present for cmd type; also idx #1 has different meaning
#=CONST= ::rev_type_idx      0
#=CONST= ::rev_islast_idx    1
#=CONST= ::rev_toffs_idx     1
#=CONST= ::rev_netwop_idx    2
#=CONST= ::rev_start_idx     3
#=CONST= ::rev_stop_idx      4
#=CONST= ::rev_title_idx     5
#=CONST= ::rev_grptag_idx    6
#=CONST= ::rev_argv_idx      7

## ---------------------------------------------------------------------------
##  Check and initialize with data loaded from RC/INI file
##  - called once during start-up
##  - note: check of reminders list done on C level upon database load or switch
##
proc Reminder_InitData {} {
   global remgroups remgroup_order reminders shortcuts

   # no reminder groups in RC file -> set default group
   if {[array size remgroups] == 0} {
      catch {unset remgroups}
      set remgroups(1000000000) {default {5} {{} {} 0} {} 0 0 {} -1}
      set remgroups(1000000010) {movies {{15 1}} {{} {} 0} {} 0 0 {} 1}
      set remgroup_order {1000000000 1000000010}
   }

   array set sc_cache {}
   set tmpl {}
   foreach grp_tag [array names remgroups] {
      set elem $remgroups($grp_tag)

      # remove references to undefined shortcuts (or duplicate references - failsafe only)
      set scl {}
      foreach sc_tag [lindex $elem $::rgp_sclist_idx] {
         if { [info exists shortcuts($sc_tag)] && \
             ![info exists sc_cache($sc_tag)]} {
            lappend scl $sc_tag
            set sc_cache($sc_tag) {}
         } elseif $::is_unix {
            puts stderr "Warning: removing obsolete/redundant shortcut reference $sc_tag from reminder group $group"
         }
      }
      set elem [lreplace $elem $::rgp_sclist_idx $::rgp_sclist_idx $scl]

      # clear group event disable flag
      set elem [lreplace $elem $::rgp_disable_idx $::rgp_disable_idx 0]

      set remgroups($grp_tag) $elem

      if {[lsearch $remgroup_order $grp_tag] == -1} {
         lappend remgroup_order $grp_tag
      }
   }

   # remove references to obsolete groups from order list
   set ltmp {}
   foreach grp_tag $remgroup_order {
      if [info exists remgroups($grp_tag)] {
         lappend ltmp $grp_tag
      }
   }
   set remgroup_order $ltmp
}

## ---------------------------------------------------------------------------
##  Load filter cache with shortcuts used in user-defined columns
##  - note reminder groups are inserted at the start of cache, hence
##    cache index is identical to reminder group index
##
proc Reminder_SetShortcuts {} {
   global remgroups remgroup_order
   global rem_col_fmt
   global shortcuts

   set all_sc {}
   set all_mask 0

   for {set group 0} {$group < [llength $remgroup_order]} {incr group} {
      set grp_tag [lindex $remgroup_order $group]

      C_PiFilter_ContextCacheCtl set $group

      # supress masekd out air times for each network
      C_PiFilter_SelectAirTimes

      # add filter to suppress unwanted shortcut reminders
      C_PiFilter_ForkContext and
      C_SelectReminderFilter [expr 1 << 31]
      C_InvertFilter custom

      # add filter for "single PI" reminders
      C_PiFilter_ForkContext or
      C_SelectReminderFilter [expr 1 << $group]
      set all_mask [expr $all_mask | (1 << $group)]

      foreach sc_tag [lindex $remgroups($grp_tag) $::rgp_sclist_idx] {
         if [info exists shortcuts($sc_tag)] {
            C_PiFilter_ForkContext or
            SelectSingleShortcut $sc_tag
            lappend all_sc $sc_tag
         }
      }

      # update filter cache index in group element
      set remgroups($grp_tag) [lreplace $remgroups($grp_tag) \
                                   $::rgp_ctxcache_idx $::rgp_ctxcache_idx $group]
   }

   # finally make a cache entry for an OR of all reminder groups
   set group [llength $remgroup_order]
   C_PiFilter_ContextCacheCtl set $group

   C_PiFilter_SelectAirTimes

   C_PiFilter_ForkContext and
   C_SelectReminderFilter [expr 1 << 31]
   C_InvertFilter custom

   C_PiFilter_ForkContext or
   C_SelectReminderFilter $all_mask

   foreach sc_tag [lsort -unique $all_sc] {
      C_PiFilter_ForkContext or
      SelectSingleShortcut $sc_tag
   }
   # update filter cache index in format for default reminder pibox column
   set rem_col_fmt [list [lreplace [lindex $rem_col_fmt 0] \
                                   $::ucf_ctxcache_idx $::ucf_ctxcache_idx $group]]
}

##
##  Collect list of required filter context cache entries
##  - called before the above "set" function to allocate cache entries;
##    the "cache" is used later to resolve reminder ref's in user-defined columns
##  - returns the number of required filter cache entries
##
proc Reminder_GetShortcuts {cache_var} {
   global remgroups remgroup_order
   upvar $cache_var cache

   for {set group 0} {$group < [llength $remgroup_order]} {incr group} {
      set grp_tag [lindex $remgroup_order $group]
      set cache(rgp_$grp_tag) $group
   }
   set cache(rgp_all) $group

   return [expr $group + 1]
}

## ---------------------------------------------------------------------------
##  Display reminder message popup
##
proc Reminder_DisplayMessage {topwid msg_list} {
   global default_bg text_bg pi_font pi_cursor_bg
   global remgroups
   global remalarm_popup remalarm_list

   set is_cmd [expr [string compare [lindex [lindex $msg_list 0] $::rev_type_idx] "cmd"] == 0]

   if {[llength [info commands $topwid]] == 0} {
      toplevel ${topwid}
      wm title ${topwid} "Reminder"
      if {[string compare [wm state .] normal] == 0} {
         wm geometry ${topwid} "+[expr [winfo rootx .] + 30]+[expr [winfo rooty .] + 30]"
      }

      frame  ${topwid}.f1 -borderwidth 1 -relief raised
      button ${topwid}.f1.logo -bitmap nxtv_small -borderwidth 0 -takefocus 0
      bindtags ${topwid}.f1.logo [list ${topwid} all]
      pack   ${topwid}.f1.logo -side left -anchor n -padx 10 -pady 10
      text   ${topwid}.f1.msg -wrap none -background $text_bg -cursor circle \
                              -height 5 -width 64 -font $pi_font -takefocus 0 \
                              -borderwidth 2 -highlightthickness 0 -highlightcolor $default_bg \
                              -cursor circle -spacing1 1 -spacing2 1 -spacing3 1
      bindtags ${topwid}.f1.msg [list ${topwid}.f1.msg ${topwid} all]
      pack   ${topwid}.f1.msg -side left -fill both -expand 1
      pack   ${topwid}.f1 -side top -fill both -expand 1

      frame  ${topwid}.f2 -borderwidth 1 -relief raised
      frame  ${topwid}.f2.f22
      if {$is_cmd == 0} {
         button ${topwid}.f2.f22.repeat -text "Repeat" -width 6 -command [list RemAlarm_Repeat $topwid ${topwid}_repoff]
         pack   ${topwid}.f2.f22.repeat -side left -padx 5
         entry  ${topwid}.f2.f22.ent_min -borderwidth 1 -textvariable ${topwid}_repoff -width 3 \
                                         -justify right -exportselection 0 -background $text_bg
         pack   ${topwid}.f2.f22.ent_min -side left
         bind   ${topwid}.f2.f22.ent_min <FocusIn>  [list ${topwid}.f2.f22.ent_min selection range 0 end]
         bind   ${topwid}.f2.f22.ent_min <FocusOut> [list ${topwid}.f2.f22.ent_min selection clear]
         bind   ${topwid}.f2.f22.ent_min <Return>   [concat RemAlarm_Repeat $topwid ${topwid}_repoff {;} break]
         bind   ${topwid}.f2.f22.ent_min <Key-Up>   [list RemAlarm_KeyUpDown $topwid ${topwid}_repoff 1]
         bind   ${topwid}.f2.f22.ent_min <Key-Down> [list RemAlarm_KeyUpDown $topwid ${topwid}_repoff -1]
         label  ${topwid}.f2.f22.lab_min -text "min"
         pack   ${topwid}.f2.f22.lab_min -side left
         button ${topwid}.f2.f22.suppress -text "Suppress" -width 6 -command [list RemAlarm_Suppress $topwid]
         pack   ${topwid}.f2.f22.suppress -side left -padx 10
         frame  ${topwid}.f2.f22.x_pad
         pack   ${topwid}.f2.f22.x_pad -side left -padx 20
         if {$::is_unix || ([string length [grid info .all.shortcuts.tune]] != 0)} {
            button ${topwid}.f2.f22.tunetv -text "Tune TV" -width 6 -command [list RemAlarm_TuneTvByHref $topwid]
            pack   ${topwid}.f2.f22.tunetv -side left -padx 15
         }
      } else {
         button ${topwid}.f2.f22.cancel -text "Cancel" -command [list RemAlarm_Quit ${topwid} {}] -width 6
         pack   ${topwid}.f2.f22.cancel -side left -padx 5
      }
      button ${topwid}.f2.f22.ok -text "Ok" -default active -width 6 \
                                 -command [list RemAlarm_Quit ${topwid} [lindex [lindex $msg_list 0] $::rev_argv_idx]]
      pack   ${topwid}.f2.f22.ok -side left -padx 5 -anchor e
      pack   ${topwid}.f2.f22 -anchor e -pady 5 -padx 10
      pack   ${topwid}.f2 -side top -fill x

      bind   ${topwid} <Return> [list tkButtonInvoke ${topwid}.f2.f22.ok]
      if $is_cmd {
         bind   ${topwid} <Escape> [list tkButtonInvoke ${topwid}.f2.f22.cancel]
      } else {
         bind   ${topwid} <Escape> [list tkButtonInvoke ${topwid}.f2.f22.ok]
         bind   ${topwid}.f2.f22 <Destroy> {+ set remalarm_popup 0}
         set remalarm_popup 1
      }
      bind   ${topwid} <Key-F1> {PopupHelp $helpIndex(Reminders) "Reminder Messages"}
      wm protocol ${topwid} WM_DELETE_WINDOW [list RemAlarm_Quit ${topwid} {}]
      focus  ${topwid}.f2.f22.ok
   } else {
      raise  ${topwid}
   }

   set old_tline [RemAlarm_GetSelected $topwid]
   ${topwid}.f1.msg delete 1.0 end

   set line_count [llength $msg_list]
   if $is_cmd {
      set group [lindex [lindex $msg_list 0] $::rev_grptag_idx]
      set grp_name [lindex $remgroups($group) $::rgp_name_idx]
      ${topwid}.f1.msg tag configure tag_bold -font [DeriveFont $pi_font 0 bold]
      ${topwid}.f1.msg tag configure href -lmargin1 10 -lmargin2 10 -rmargin 10
      ${topwid}.f1.msg insert end "\nExecute $grp_name script for this programme?\n" tag_bold
      incr line_count 2
   } else {
      ${topwid}.f1.msg tag configure cur -relief raised -borderwidth 1 -background $pi_cursor_bg
      ${topwid}.f1.msg tag configure href -lmargin1 10 -lmargin2 10 -rmargin 10
      if {[llength $msg_list] > 1} {
         ${topwid}.f1.msg tag bind href <Button-1>        [list RemAlarm_SelectItem $topwid %x %y 0x100]
         ${topwid}.f1.msg tag bind href <ButtonRelease-1> [list RemAlarm_SelectItem $topwid %x %y 0]
         ${topwid}.f1.msg tag bind href <Double-Button-1> [list RemAlarm_TuneTvByHref $topwid]
      }
      ${topwid}.f1.msg tag bind href <Enter> [list ${topwid}.f1.msg configure -cursor top_left_arrow]
      ${topwid}.f1.msg tag bind href <Leave> [list ${topwid}.f1.msg configure -cursor circle]
   }
   ${topwid}.f1.msg insert end "\n"

   foreach ev_desc $msg_list {
      set this_msg {}
      append this_msg [C_ClockFormat [lindex $ev_desc $::rev_start_idx] {%a %e.%m. %H:%M - }] \
                      [C_ClockFormat [lindex $ev_desc $::rev_stop_idx] {%H:%M}] { } \
                      [lindex $ev_desc $::rev_title_idx] \
                      " (" [RemAlarm_GetNetname [lindex $ev_desc $::rev_netwop_idx]] ")\n"

      ${topwid}.f1.msg insert end $this_msg href
   }
   # note: append newlines to make insertion cursor disappear
   ${topwid}.f1.msg insert end "\n\n\n\n\n"

   if {$is_cmd == 0} {
      # save message list for use in control command handlers (i.e. repeat, suppress, tunetv)
      set remalarm_list $msg_list

      if {($old_tline < 0) || ($old_tline >= [llength $msg_list])} {
         set old_tline 0
      }
      RemAlarm_SetSelection $topwid $old_tline
   }

   if {$line_count <= 2} {
      set line_count 4
   } else {
      set line_count [expr $line_count + 2]
   }
   ${topwid}.f1.msg configure -height $line_count

   # make the window stay on top, i.e. never get obscured by other windows
   # (for win32 this is equiv to tk8.4's "wm attributes ${topwid} -topmost 1")
   update
   C_Wm_StayOnTop ${topwid}
}

# helper function: determine selected item in message programme list
proc RemAlarm_GetSelected {topwid} {
   global remalarm_list

   # caution: may be called before list is set
   if [info exists remalarm_list] {
      set ltmp [${topwid}.f1.msg tag nextrange cur 1.0]
      if {[llength $ltmp] > 0} {
         scan [lindex $ltmp 0] "%d.%d" tline foo
         incr tline -2
         if {$tline >= [llength $remalarm_list]} {
            set tline -1
         }
      } elseif {[llength $remalarm_list] == 1} {
         set tline 0
      } else {
         set tline -1
      }
   } else {
      set tline -1
   }
   return $tline
}

# helper function: select new line and adapt button state
proc RemAlarm_SetSelection {topwid tline} {
   global remalarm_list

   # if there's more than one, mark the selected programme in the text widget
   if {[llength $remalarm_list] > 1} {
      ${topwid}.f1.msg tag remove cur 1.0 end
      ${topwid}.f1.msg tag add cur [expr $tline + 2].0 [expr $tline + 3].0
   }

   # disable "suppress" button if no more messages will follow
   set ev_desc [lindex $remalarm_list $tline]
   if [lindex $ev_desc $::rev_islast_idx] {
      ${topwid}.f2.f22.suppress configure -state disabled
   } else {
      ${topwid}.f2.f22.suppress configure -state normal
   }
}

# helper function: get network name for given AI netwop index
proc RemAlarm_GetNetname {netwop} {

   set netsel_ailist [C_GetAiNetwopList 0 netsel_names]

   set cni [lindex $netsel_ailist $netwop]
   if [info exists netsel_names($cni)] {
      set netname $netsel_names($cni)
   } else {
      set netname "network $cni"
   }
   return $netname
}

# callback for "Tune TV" button (or double-click in programme list) in message popup
proc RemAlarm_TuneTvByHref {topwid} {
   global remalarm_list

   set tline [RemAlarm_GetSelected $topwid]
   if {$tline >= 0} {
      set ev_desc [lindex $remalarm_list $tline]
      set netname [RemAlarm_GetNetname [lindex $ev_desc $::rev_netwop_idx]]

      C_Tvapp_SendCmd "setstation" $netname
   }
}

# callback for cursor up/down in "repeat" entry widget: increment/decrement value
proc RemAlarm_KeyUpDown {topwid repoff_var delta} {
   upvar $repoff_var repoff

   if {[catch {incr repoff $delta}] != 0} {
      set repoff 1
   } elseif {$repoff <= 0} {
      set repoff 1
   }
}

# callback for "suppress" button in reminder popup
# -> display another message at the given minute offset (and none before that time)
proc RemAlarm_Repeat {topwid repoff_var} {
   global remalarm_list
   upvar $repoff_var repoff

   set tline [RemAlarm_GetSelected $topwid]
   if {$tline >= 0} {
      if {([scan $repoff "%u%n" minute len] == 2) && \
          ($len == [string length $repoff]) && \
          ($repoff > 0)} {

         # determine specs of the selected event
         set ev_desc [lindex $remalarm_list $tline]
         C_PiRemind_SetControl $::rpi_ctrl_repeat $repoff \
                               [lindex $ev_desc $::rev_grptag_idx] \
                               [lindex $ev_desc $::rev_netwop_idx] \
                               [lindex $ev_desc $::rev_start_idx]

         Reminder_AlarmTimer

      } else {
         tk_messageBox -type ok -default ok -icon error -parent $topwid \
                       -message "Invalid value for minute offset: must enter a non-zero numerical value"
      }
   }
}

# callback for "suppress" button in reminder popup
# -> suppress further messages for the selected reminder
proc RemAlarm_Suppress {topwid} {
   global remalarm_list

   set tline [RemAlarm_GetSelected $topwid]
   if {$tline >= 0} {
      set ev_desc [lindex $remalarm_list $tline]
      C_PiRemind_SetControl $::rpi_ctrl_suppress 0 \
                            [lindex $ev_desc $::rev_grptag_idx] \
                            [lindex $ev_desc $::rev_netwop_idx] \
                            [lindex $ev_desc $::rev_start_idx]

      Reminder_AlarmTimer
   }
}

# callback for single click in programme list: set cursor onto the line
proc RemAlarm_SelectItem {topwid xcoo ycoo but_state} {
   global remalarm_list
   global remalarm_button_state

   if {![info exists remalarm_button_state]} {
      set remalarm_button_state 0
   }

   if {(($but_state & 0x100) != 0) && ($remalarm_button_state == 0)} {
      # install a handler for motion events while the mouse button remains pressed
      bind ${topwid}.f1.msg <Motion> [list RemAlarm_SelectItem $topwid %x %y %s]
      set remalarm_button_state 1

   } elseif {(($but_state & 0x100) == 0) && ($remalarm_button_state != 0)} {
      # button no longer pressed -> remove the motion event handler
      bind ${topwid}.f1.msg <Motion> {}
      set remalarm_button_state 0
   }

   scan [${topwid}.f1.msg index "@$xcoo,$ycoo"] "%d.%d" tline foo
   if {($tline >= 2) && ($tline - 2 < [llength $remalarm_list])} {
      incr tline -2
      RemAlarm_SetSelection $topwid $tline
   }
}

# callback for "OK" and "Cancel" buttons in message popup
proc RemAlarm_Quit {topwid cmd_spec} {
   global rem_msg_disp_time rem_msg_cnf_time
   global remalarm_list

   catch {destroy $topwid}

   if {[string compare $topwid .remalarm] == 0} {
      # set new cut-off time for subsequent events
      set rem_msg_cnf_time $rem_msg_disp_time
      catch {unset remalarm_list}

      # save new reminder threshold in rc/ini file
      UpdateRcFile

      # update reminder list in dialog (if open) to clear "repeat" state
      Reminder_ExternalChange -1
   }

   if {[llength $cmd_spec] == 2} {
      C_ExecParsedScript [lindex $cmd_spec 0] [lindex $cmd_spec 1]
   }
}


# helper function to sort events by start time and network
# note: index #2 in event list is network, index #3 start time
proc SortReminderEvents {ev_a ev_b} {
   global reminders

   if {[lindex $ev_a 3] < [lindex $ev_b 3]} {
      return -1
   } elseif {[lindex $ev_a 3] > [lindex $ev_b 3]} {
      return 1;
   } else {
      # start times equal -> compare network indices (not CNI)
      if {[lindex $ev_a 2] < [lindex $ev_b 2]} {
         return -1
      } elseif {[lindex $ev_a 2] > [lindex $ev_b 2]} {
         return 1
      } else {
         # events refer to the same PI
         return 0
      }
   }
}

## ---------------------------------------------------------------------------
##  Check for pending events and show reminder message or execute scripts
##
proc Reminder_AlarmTimer {} {
   global rem_msg_cnf_time rem_msg_disp_time rem_msg_base_time rem_cmd_cnf_time
   global rem_timer_id remalarm_popup
   global reminders remgroups remgroup_order

   if {$rem_timer_id != {}} {
      after cancel $rem_timer_id
      set rem_timer_id {}
   }
   after cancel Reminder_UpdateTimer

   # remember the time of this query
   # - for messages: when OK button in alarm popup is pressed this time will be copied
   #   into the "confirmed" time, i.e. all events before this time are marked "done";
   #   the confirmed time is also stored in the rc/ini file to prevent repetitions
   # - round up to next minute because checks should be done closely after minute changes
   set rem_msg_disp_time [C_ClockSeconds]
   if {($rem_msg_disp_time % 60) != 0} {
      set rem_msg_disp_time [expr $rem_msg_disp_time - ($rem_msg_disp_time % 60) + 60]
   }

   if {$remalarm_popup == 0} {
      # ignore all programmes which have expired before this time;
      # this threshold is not updated while popup is displayed so that programmes
      # remain listed even if thex expire while the popup is open and new events occur
      set rem_msg_base_time [expr $rem_msg_disp_time - 60]
   }

   # loop across all reminder groups and search for events
   # note: searching separately for groups because of different start time offsets
   set event_list {}
   foreach grp_tag $remgroup_order {
      set group_elem $remgroups($grp_tag)
      if {![lindex $group_elem $::rgp_disable_idx]} {
         # retrieve start time offsets for messages and scripts
         set msgl [lindex [lindex $group_elem $::rgp_msg_idx] $::ract_toffl_idx]
         set cmdl [lindex [lindex $group_elem $::rgp_cmd_idx] $::ract_toffl_idx]
         # retrieve command line: variables (e.g. ${title}) are substituted for matching PI
         set cmdv [lindex [lindex $group_elem $::rgp_cmd_idx] $::ract_argv_idx]
         # index of reminder filter in filter context cache
         set cache_idx [lindex $group_elem $::rgp_ctxcache_idx]

         # Query database for matching programmes: returns list of events
         # - for messages return all events since the last reminder "confirm" (OK pressed in popup)
         # - for commands return only events since last query (to make sure they are processed only once)
         set ev [ \
           C_PiRemind_GetReminderEvents $cache_idx \
                                        $rem_cmd_cnf_time \
                                        $rem_msg_cnf_time \
                                        $rem_msg_base_time \
                                        $rem_msg_disp_time \
                                        [lsort -integer -unique $msgl] \
                                        [lsort -integer -unique $cmdl] \
                                        $cmdv ]
         foreach ev_elem $ev {
            lappend event_list [linsert $ev_elem $::rev_grptag_idx $grp_tag]
         }
      }
   }

   # add reminders which were manually set to "repeat"
   foreach elem $reminders {
      set ctrl [lindex $elem $::rpi_ctrl_idx]
      if {($ctrl & $::rpi_ctrl_mask) == $::rpi_ctrl_repeat} {
         set event_abs_time [expr $ctrl & ~$::rpi_ctrl_mask]
         if {($event_abs_time >= $rem_msg_cnf_time) && \
             ($event_abs_time < $rem_msg_disp_time) && \
             ([lindex $elem $::rpi_stop_idx] >= $rem_msg_base_time)} {
            set toffs [expr [lindex $elem $::rpi_start_idx] - [C_ClockSeconds]]
            set group_elem $remgroups([lindex $elem $::rpi_grptag_idx])
            set is_latest 1
            foreach off [lindex [lindex $group_elem $::rgp_msg_idx] $::ract_toffl_idx] {
               if {$off < $toffs} {
                  set is_latest 0
                  break;
               }
            }
            set ev_elem [list msg $is_latest \
                                  [lindex $elem $::rpi_netwop_idx] \
                                  [lindex $elem $::rpi_start_idx] \
                                  [lindex $elem $::rpi_stop_idx] \
                                  [lindex $elem $::rpi_title_idx] \
                                  [lindex $elem $::rpi_grptag_idx]]
            lappend event_list $ev_elem
         }
      }
   }

   # sort events by start time and network
   set event_list [lsort -command SortReminderEvents $event_list]

   set msg_list {}
   for {set ev_idx 0} {$ev_idx < [llength $event_list]} {incr ev_idx} {
      set ev_desc [lindex $event_list $ev_idx]

      if {[string compare [lindex $ev_desc $::rev_type_idx] "msg"] == 0} {

         # check if the following event refers to the same PI
         set ev_idx_1 [expr $ev_idx + 1]
         if {$ev_idx_1 < [llength $event_list]} {
            set ev_next [lindex $event_list $ev_idx_1]
            if {([lindex $ev_desc $::rev_netwop_idx] == [lindex $ev_next $::rev_netwop_idx]) &&
                ([lindex $ev_desc $::rev_start_idx] == [lindex $ev_next $::rev_start_idx])} {
               # next event refers to same PI -> merge
               set is_latest [expr [lindex $ev_desc $::rev_islast_idx] || \
                                   [lindex $ev_next $::rev_islast_idx]]
               set event_list [lreplace $event_list $ev_idx_1 $ev_idx_1 \
                                        [lreplace $ev_next 1 1 $is_latest]]
               # skip this event
               continue
            }
         }

         # add title info to message popup (note all titles are collected in one popup)
         lappend msg_list $ev_desc

      } elseif {[string compare [lindex $ev_desc $::rev_type_idx] "cmd"] == 0} {
         set group_elem $remgroups([lindex $ev_desc $::rev_grptag_idx])
         set cmd_spec [lindex $ev_desc $::rev_argv_idx]
         if {[lindex [lindex $group_elem $::rgp_cmd_idx] $::ract_ask_idx]} {

            # ask user if the script shall be executed
            set wid ".remalarm_[lindex $ev_desc $::rev_netwop_idx]_[lindex $ev_desc $::rev_start_idx]_[lindex $ev_desc $::rev_islast_idx]"
            Reminder_DisplayMessage $wid [list $ev_desc]
         } else {

            # execute script immediately
            C_ExecParsedScript [lindex $cmd_spec 0] [lindex $cmd_spec 1]
         }
      } else {
         bgerror "Reminder-AlarmTimer: invalid event keyword [lindex $ev_desc $::rev_type_idx]"
      }
   }

   if {[llength $msg_list] > 0} {
      Reminder_DisplayMessage .remalarm $msg_list
   } else {
      # nothing to display -> just mark message interval as done
      # close window in case obsolete messages are still displayed (after control change)
      catch {destroy .remalarm}
      set rem_msg_cnf_time $rem_msg_disp_time
      catch {unset remalarm_list}
      UpdateRcFile
   }

   # commands are immediately considered "confirmed", i.e. executed
   set rem_cmd_cnf_time $rem_msg_disp_time

   # save new script threshold in rc/ini file
   UpdateRcFile

   # set timer to update message upon next event following the currently displayed ones
   # note: use "after" to avoid recursion
   after idle Reminder_UpdateTimer
}

## ---------------------------------------------------------------------------
##  Set timer for the next reminder event
##  - search across all reminder groups for the first match
##
proc Reminder_UpdateTimer {} {
   global rem_timer_id remalarm_popup
   global rem_msg_cnf_time rem_msg_disp_time rem_cmd_cnf_time
   global reminders remgroups remgroup_order

   if {$rem_timer_id != {}} {
      after cancel $rem_timer_id
      set rem_timer_id {}
   }
   after cancel Reminder_UpdateTimer

   # for fail-safety check for pending events at least every 15 minutes
   set min_delta [expr 15*60]
   set now [C_ClockSeconds]

   if {$remalarm_popup == 0} {
      set msg_min_time $rem_msg_cnf_time
   } else {
      set msg_min_time $rem_msg_disp_time
   }

   foreach grp_tag $remgroup_order {
      set group_elem $remgroups($grp_tag)
      if {![lindex $group_elem $::rgp_disable_idx]} {
         set msgl [lindex [lindex $group_elem $::rgp_msg_idx] $::ract_toffl_idx]
         if {[llength $msgl] != 0} {
            set msgl [lsort -integer -unique $msgl]
         }
         set cmdl [lindex [lindex $group_elem $::rgp_cmd_idx] $::ract_toffl_idx]
         if {[llength $cmdl] != 0} {
            set cmdl [lsort -integer -unique $cmdl]
         }

         if {[llength $msgl] + [llength $cmdl] > 0} {
            set cache_idx [lindex $group_elem $::rgp_ctxcache_idx]

            set event_abs_time [ \
               C_PiRemind_MatchFirstReminder $cache_idx \
                                             $msg_min_time \
                                             $rem_cmd_cnf_time \
                                             [expr $now + $min_delta] \
                                             $msgl \
                                             $cmdl ]
            if {$event_abs_time != 0} {
               set delta [expr $event_abs_time - $now]

               if {$delta < $min_delta} {
                  set min_delta $delta

                  if {$min_delta <= 0} {
                     break
                  }
               }
            }
         }
      }
   }

   foreach elem $reminders {
      set event_abs_time [lindex $elem $::rpi_ctrl_idx] 
      if {($event_abs_time >= $::rpi_ctrl_repeat) && \
          ($event_abs_time > $msg_min_time) && \
          ($event_abs_time < [expr $now + $min_delta])} {
         set min_delta [expr $event_abs_time - $now]
      }
   }

   if {$min_delta > 0} {
      set rem_timer_id [after [expr $min_delta * 1000] Reminder_AlarmTimer]
   } else {
      # event already pending -> start action immediately
      Reminder_AlarmTimer
   }
}

## ---------------------------------------------------------------------------
## Display group sub-menu in main menubar
## - used as postcommand for "group" cascade menu
## - supports 2 modes:
##   (i)  adding new reminders (reminder index parameter == -1)
##   (ii) changing group of existing reminders: uses radio buttons to be able
##        to reflect the current group selection
##
proc Reminder_PostGroupMenu {wid rem_idx} {
   global fooMenubarRemGroup
   global reminders remgroups remgroup_order

   foreach grp_tag $remgroup_order {
      set name [lindex $remgroups($grp_tag) $::rgp_name_idx]
      if {$rem_idx == -1} {
         $wid add command -label $name -command [list C_PiRemind_AddPi $grp_tag]
      } else {
         $wid add radiobutton -label $name -variable fooMenubarRemGroup -value $grp_tag \
                              -command [list C_PiRemind_PiSetGroup $grp_tag]
      }
   }
   if {$rem_idx != -1} {
      # for mode (ii): set current group in dummy variable
      set fooMenubarRemGroup [lindex [lindex $reminders $rem_idx] $::rpi_grptag_idx]
   }
}

## ---------------------------------------------------------------------------
## Handle external addition, removal or suppression of reminders
## - it's mandatory that this function is called after all changes in the
##   reminder list to keep the dialog in sync
##
proc Reminder_ExternalChange {add_idx} {
   global remlist_lastpi_start remlist_lastpi_netwop
   global remlist_popup remlist_pilist
   global reminders

   # refresh in case display shows markers for reminders
   if {$::pibox_type != 0} {
      C_PiNetBox_Invalidate
   }
   C_PiBox_Refresh

   # save the new reminder in the rc/ini file
   UpdateRcFile

   if $remlist_popup {
      if {$add_idx != -1} {
         # place cursor onto the added/modified element
         set elem [lindex $reminders $add_idx]
         set remlist_lastpi_start [lindex $elem $::rpi_start_idx]
         set remlist_lastpi_netwop [lindex $elem $::rpi_netwop_idx]
      } else {
         # keep cursor on the currently selected element
         set frm1 [Rnotebook:frame .remlist.nb 1]
         set sel_idx [${frm1}.selist curselection]
         if {([llength $sel_idx] == 1) && ($sel_idx < [llength $remlist_pilist])} {
            set rem_idx [lindex $remlist_pilist $sel_idx]
            if {$rem_idx < [llength $reminders]} {
               set elem [lindex $reminders $rem_idx]
               set remlist_lastpi_start [lindex $elem $::rpi_start_idx]
               set remlist_lastpi_netwop [lindex $elem $::rpi_netwop_idx]
            }
         }
      }
      RemPiList_FillPi pi
   }

   # set timer to trigger messages
   Reminder_UpdateTimer
}

## ---------------------------------------------------------------------------
## Set PI listbox filter to show reminders of given groups
## - note: filter gets group indices: indices are derived here from group order
##   hence the filter must be updated when group ordering is changed
##
proc Reminder_Match {group_list} {
   global remgroups remgroup_order shortcuts
   global remgroup_filter

   C_PiFilter_ForkContext reset

   set remgroup_filter {}
   set mask 0
   foreach grp_tag $group_list {
      # convert group tag to index (b/c filter function uses bitmap of indices)
      set idx [lsearch $remgroup_order $grp_tag]
      if {$idx != -1} {
         set mask [expr $mask | (1 << $idx)]

         foreach sc_tag [lindex $remgroups($grp_tag) $::rgp_sclist_idx] {
            if [info exists shortcuts($sc_tag)] {
               C_PiFilter_ForkContext or
               SelectSingleShortcut $sc_tag
            }
         }
         lappend remgroup_filter $grp_tag
      }
   }

   if {$mask != 0} {
      C_PiFilter_ForkContext or
      C_SelectReminderFilter $mask

      C_PiFilter_ForkContext and
      C_SelectReminderFilter [expr 1 << 31]
      C_InvertFilter custom
   }

   C_PiFilter_ForkContext close
}

proc Reminder_ShowAll {} {
   global remgroups remgroup_order

   Reminder_Match $remgroup_order

   C_PiBox_Refresh
}

proc Reminder_SelectFromTagList {group_list grp_tag} {
   global remgroups remgroup_order
   global remgroup_filter

   # note 1: group_list is unused because all previously selected groups are deselected
   # note 2: special value tag=0 used to indicate "all groups" mode

   if {$grp_tag != 0} {
      # check which reminders are currently selected and keep those which are not in the group list
      set enable_list {}
      foreach prev_group $remgroup_filter {
         if {[lsearch -exact $group_list $prev_group] == -1} {
            lappend enable_list $prev_group
         }
      }
      if {([lsearch $remgroup_filter $grp_tag] == -1) && \
           [info exists remgroups($grp_tag)]} {
         # given goup is not yet enabled -> enable it
         lappend enable_list $grp_tag
      }

      if {[llength $enable_list] != 0} {
         Reminder_Match $enable_list
         C_PiBox_Refresh
      } else {
         # no groups to enable -> disable all
         set remgroup_filter {}
         C_PiFilter_ForkContext reset
         C_PiBox_Refresh
      }
   } else {
      # special mode: tag zero -> all groups
      if {[llength $remgroup_filter] < [llength $remgroup_order]} {
         Reminder_ShowAll
      } else {
         # already all selected -> deselect all groups
         set remgroup_filter {}
         C_PiFilter_ForkContext reset
         C_PiBox_Refresh
      }
   }
}

#=LOAD=PopupReminderList
#=LOAD=PopupReminderConfig
#=DYNAMIC=

## ---------------------------------------------------------------------------
## Open dialog to manage list of reminders
##
proc PopupReminderList {tab_idx} {
   global pi_font
   global remlist_popup
   global remlist_winsize rempilist_colsize remsclist_colsize

   if {$remlist_popup == 0} {
      CreateTransientPopup .remlist "Edit reminder list"
      wm geometry .remlist $remlist_winsize
      wm resizable .remlist 1 1
      set remlist_popup 1

      # load special widget libraries
      rnotebook_load
      mclistbox_load

      Rnotebook:create .remlist.nb -tabs {"Single programmes" "Shortcuts"} -borderwidth 2
      pack .remlist.nb -side left -pady 10 -padx 5 -fill both -expand 1
      set frm1 [Rnotebook:frame .remlist.nb 1]
      set frm2 [Rnotebook:frame .remlist.nb 2]

      ## first column: listbox with all shortcut labels
      scrollbar  ${frm1}.list_sb -orient vertical -command [list ${frm1}.selist yview] -takefocus 0
      pack       ${frm1}.list_sb -side left -pady 10 -fill y
      mclistbox::mclistbox ${frm1}.selist -relief ridge -columnrelief flat -labelanchor c \
                                -selectmode browse -columnborderwidth 0 \
                                -exportselection 0 -font $pi_font -labelfont [DeriveFont $pi_font -2] \
                                -yscrollcommand [list ${frm1}.list_sb set] -fillcolumn title

      # define listbox columns: labels and width (resizable)
      ${frm1}.selist column add state -label "State" -width [lindex $rempilist_colsize 0]
      ${frm1}.selist column add group -label "Group" -width [lindex $rempilist_colsize 1]
      ${frm1}.selist column add date -label "Date" -width [lindex $rempilist_colsize 2]
      ${frm1}.selist column add time -label "Start/Stop" -width [lindex $rempilist_colsize 3]
      ${frm1}.selist column add title -label "Title"
      ${frm1}.selist column add netwop -label "Network" -width [lindex $rempilist_colsize 5]

      bind       ${frm1}.selist <<ListboxSelect>> RemPiList_Select
      bind       ${frm1}.selist <Key-Up>          [concat {+} [bind Mclistbox <Key-Up>] {;} [bind Mclistbox <Key-space>] {;} break]
      bind       ${frm1}.selist <Key-Down>        [concat {+} [bind Mclistbox <Key-Down>] {;} [bind Mclistbox <Key-space>] {;} break]
      bind       ${frm1}.selist <Double-Button-1> RemPiList_GotoPi
      bind       ${frm1}.selist <Key-Return>      RemPiList_GotoPi
      bind       ${frm1}.selist <ButtonPress-3>   {RemPiList_OpenContextMenu \
                                                    [::mclistbox::convert %W -W] \
                                                    [::mclistbox::convert %W -x %x] \
                                                    [::mclistbox::convert %W -y %y] \
                                                    %X %Y}
      menu       ${frm1}.remlist_ctx_state -tearoff 0 -postcommand [list PostDynamicMenu ${frm1}.remlist_ctx_state RemList_PostStateCtxMenu $frm1]

      bind       ${frm1}.selist <Enter> {focus %W}
      bind       ${frm1}.selist <Key-Delete> [list tkButtonInvoke ${frm1}.cmd.delete]
      bind       ${frm1}.selist <Escape> [list after idle [list tkButtonInvoke ${frm1}.cmd.close]]
      pack       ${frm1}.selist -side left -pady 10 -fill both -expand 1

      ## second column: command buttons
      frame      ${frm1}.cmd
      menubutton ${frm1}.cmd.mb_grp -text "Set group" -menu ${frm1}.cmd.mb_grp.men \
                                    -indicatoron 1 -direction flush
      config_menubutton ${frm1}.cmd.mb_grp
      pack       ${frm1}.cmd.mb_grp -side top -anchor nw -fill x
      menu       ${frm1}.cmd.mb_grp.men -tearoff 0 -postcommand [list PostDynamicMenu ${frm1}.cmd.mb_grp.men RemList_PostGroupMenu 1]
      button     ${frm1}.cmd.delete -text "Delete" -command RemPiList_Delete
      pack       ${frm1}.cmd.delete -side top -anchor nw -fill x

      frame      ${frm1}.cmd.pady20
      pack       ${frm1}.cmd.pady20 -side top -pady 20
      menubutton ${frm1}.cmd.mb_disp -text "Display" -menu ${frm1}.cmd.mb_disp.men \
                                     -indicatoron 1 -direction flush
      config_menubutton ${frm1}.cmd.mb_disp
      pack       ${frm1}.cmd.mb_disp -side top -anchor nw -fill x
      menu       ${frm1}.cmd.mb_disp.men -tearoff 0
      ${frm1}.cmd.mb_disp.men add checkbutton -label "Pending reminders" -variable remlist_disp_pend -command RemPiList_CheckDispFlags
      ${frm1}.cmd.mb_disp.men add checkbutton -label "Expired reminders" -variable remlist_disp_exp -command RemPiList_CheckDispFlags
      ${frm1}.cmd.mb_disp.men add checkbutton -label "Suppressed reminders" -variable remlist_disp_sup -command RemPiList_CheckDispFlags
      button     ${frm1}.cmd.show -text "Goto" -command RemPiList_GotoPi
      pack       ${frm1}.cmd.show -side top -fill x

      button     ${frm1}.cmd.close -text "Dismiss" -command RemList_WindowClose
      button     ${frm1}.cmd.help -text "Help" -command {PopupHelp $helpIndex(Reminders) "Edit reminder list"} -width 9
      pack       ${frm1}.cmd.help ${frm1}.cmd.close -side bottom -anchor sw -fill x

      pack       ${frm1}.cmd -side left -anchor nw -pady 10 -padx 5 -fill y
      bind       ${frm1}.cmd <Destroy> {+ set remlist_popup 0}

      # second tab
      scrollbar  ${frm2}.list_sb -orient vertical -command [list ${frm2}.selist yview] -takefocus 0
      pack       ${frm2}.list_sb -side left -pady 10 -fill y
      mclistbox::mclistbox ${frm2}.selist -relief ridge -columnrelief flat -labelanchor c \
                                -selectmode browse -columnborderwidth 0 \
                                -exportselection 0 -font $pi_font -labelfont [DeriveFont $pi_font -2] \
                                -yscrollcommand [list ${frm2}.list_sb set] -fillcolumn shortcut
      # define listbox columns: labels and width (resizable)
      ${frm2}.selist column add group -label "Group" -width [lindex $remsclist_colsize 0]
      ${frm2}.selist column add shortcut -label "Shortcut"

      bind       ${frm2}.selist <<ListboxSelect>> {+ RemScList_Select}
      bind       ${frm2}.selist <Key-Up>          [concat {+} [bind Mclistbox <Key-Up>] {;} [bind Mclistbox <Key-space>] {;} break]
      bind       ${frm2}.selist <Key-Down>        [concat {+} [bind Mclistbox <Key-Down>] {;} [bind Mclistbox <Key-space>] {;} break]
      bind       ${frm2}.selist <ButtonPress-3> {RemScList_OpenContextMenu \
                                                    [::mclistbox::convert %W -W] \
                                                    [::mclistbox::convert %W -x %x] \
                                                    [::mclistbox::convert %W -y %y] \
                                                    %X %Y}
      bind       ${frm2}.selist <Enter> {focus %W}
      bind       ${frm2}.selist <Key-Delete> [list tkButtonInvoke ${frm2}.cmd.delete]
      bind       ${frm2}.selist <Escape> [list after idle [list tkButtonInvoke ${frm2}.cmd.close]]
      pack       ${frm2}.selist -side left -pady 10 -fill both -expand 1

      ## second column: command buttons
      frame      ${frm2}.cmd
      menubutton ${frm2}.cmd.mb_scl -text "Add shortcut" -menu ${frm2}.cmd.mb_scl.men \
                                    -indicatoron 1 -direction flush
      config_menubutton ${frm2}.cmd.mb_scl
      pack       ${frm2}.cmd.mb_scl -side top -anchor nw -fill x
      menu       ${frm2}.cmd.mb_scl.men -tearoff 0 \
                    -postcommand [list PostDynamicMenu ${frm2}.cmd.mb_scl.men RemScList_PostScMenu 0]
      menubutton ${frm2}.cmd.mb_grp -text "Set group" -menu ${frm2}.cmd.mb_grp.men \
                                    -indicatoron 1 -direction flush
      config_menubutton ${frm2}.cmd.mb_grp
      pack       ${frm2}.cmd.mb_grp -side top -anchor nw -fill x
      menu       ${frm2}.cmd.mb_grp.men -tearoff 0 -postcommand [list PostDynamicMenu ${frm2}.cmd.mb_grp.men RemList_PostGroupMenu 0]
      button     ${frm2}.cmd.delete -text "Delete" -command RemScList_Delete
      pack       ${frm2}.cmd.delete -side top -anchor nw -fill x
      button     ${frm2}.cmd.close -text "Dismiss" -command RemList_WindowClose
      button     ${frm2}.cmd.help -text "Help" -command {PopupHelp $helpIndex(Reminders) "Edit reminder list"} -width 9
      pack       ${frm2}.cmd.help ${frm2}.cmd.close -side bottom -anchor sw -fill x

      pack       ${frm2}.cmd -side left -anchor nw -pady 10 -padx 5 -fill y

      bind       ${frm2}.cmd <Destroy> {+ set remlist_popup 0}
      bind       .remlist <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]
      bind       .remlist <Key-F1> {PopupHelp $helpIndex(Reminders) "Edit reminder list"}
      bind       .remlist <Configure> {RemList_WindowResized %W}
      wm protocol .remlist WM_DELETE_WINDOW RemList_WindowClose

      # fill the shortcuts reminders listbox and update button states
      RemScList_FillShortcuts
      RemScList_Select

      # fill the "single PI" reminders listbox and update button states
      RemPiList_CheckDispFlags
      RemPiList_FillPi init
      RemPiList_Select

      Rnotebook:raise .remlist.nb [expr $tab_idx + 1]

   } else {
      raise .remlist
   }
}

# helper function to sort list of reminder indices by reminder's start times
proc SortPiReminders {a b} {
   global reminders

   set el_a [lindex $reminders $a]
   set el_b [lindex $reminders $b]

   if {[lindex $el_a $::rpi_start_idx] < [lindex $el_b $::rpi_start_idx]} {
      return -1
   } elseif {[lindex $el_a $::rpi_start_idx] > [lindex $el_b $::rpi_start_idx]} {
      return 1;
   } else {
      # start times equal -> compare network indices (not CNI)
      if {[lindex $el_a $::rpi_netwop_idx] < [lindex $el_b $::rpi_netwop_idx]} {
         return -1
      } elseif {[lindex $el_a $::rpi_netwop_idx] > [lindex $el_b $::rpi_netwop_idx]} {
         return 1
      } else {
         # this case should never be reached (same start time, same network)
         return 0
      }
   }
}

# fill listbox with all single-PI reminders (of all groups except suppress group)
proc RemPiList_FillPi {mode} {
   global remlist_popup remlist_pilist
   global remlist_disp_pend remlist_disp_exp remlist_disp_sup
   global remlist_lastpi_start remlist_lastpi_netwop
   global reminders remgroups

   if $remlist_popup {
      set frm1 [Rnotebook:frame .remlist.nb 1]

      # generate sorted index list into reminder list (sort by start time & network)
      set now [C_ClockSeconds]
      set remlist_pilist {}
      for {set idx 0} {$idx < [llength $reminders]} {incr idx} {
         set elem [lindex $reminders $idx]
         set grp_tag [lindex $elem $::rpi_grptag_idx]
         # skip reminders not selected by "display" flags
         if { (($grp_tag != 0) ? $remlist_disp_pend : $remlist_disp_sup) && \
              (([lindex $elem $::rpi_stop_idx] > $now) || $remlist_disp_exp)} {
            lappend remlist_pilist $idx
         }
      }
      set remlist_pilist [lsort -command SortPiReminders $remlist_pilist]

      # fetch CNI list and netwop names from AI block in database
      set ailist [C_GetAiNetwopList 0 netnames]

      ${frm1}.selist delete 0 end

      foreach rem_idx $remlist_pilist {
         set elem [lindex $reminders $rem_idx]
         set grp_tag [lindex $elem $::rpi_grptag_idx]
         set cni [lindex $elem $::rpi_cni_idx]
         if [info exists netnames($cni)] {
            set netname $netnames($cni)
         } else {
            set netname $cni
         }
         if {(([lindex $elem $::rpi_ctrl_idx] & $::rpi_ctrl_mask) == $::rpi_ctrl_repeat) && \
             (([lindex $elem $::rpi_ctrl_idx] & ~ $::rpi_ctrl_mask) >= $now)} {
            set ev_state "rep."
         } elseif {([lindex $elem $::rpi_ctrl_idx] & $::rpi_ctrl_mask) == $::rpi_ctrl_suppress} {
            set ev_state "sup."
         } else {
            set ev_state {}
         }
         if {$grp_tag != 0} {
            set group_name [lindex $remgroups($grp_tag) $::rgp_name_idx]
         } else {
            set group_name "suppress"
            set ev_state {}
         }
         ${frm1}.selist insert end [list \
                   $ev_state \
                   $group_name \
                   [C_ClockFormat [lindex $elem $::rpi_start_idx] {%a %e.%m.}] \
                   [concat [C_ClockFormat [lindex $elem $::rpi_start_idx] {%H:%M -}] \
                           [C_ClockFormat [lindex $elem $::rpi_stop_idx] {%H:%M}]] \
                   [lindex $elem $::rpi_title_idx] \
                   $netname]
         # (note: display order affects updates by goup selection callback)
      }

      # place cursor as near as possible to old position
      if {[llength $remlist_pilist] != 0} {
         if {[info exists remlist_lastpi_start] && [info exists remlist_lastpi_netwop]} {
            for {set sel_idx 0} {$sel_idx < [llength $remlist_pilist]} {incr sel_idx} {
               set rem_idx [lindex $remlist_pilist $sel_idx]
               set elem [lindex $reminders $rem_idx]
               if {([lindex $elem $::rpi_start_idx] > $remlist_lastpi_start ) || \
                   ( ([lindex $elem $::rpi_start_idx] == $remlist_lastpi_start) &&
                     ([lindex $elem $::rpi_netwop_idx] >= $remlist_lastpi_netwop))} {
                  break
               }
            }
            if {$sel_idx >= [llength $remlist_pilist]} {
               set sel_idx [expr [llength $remlist_pilist] - 1]
            }
         } else {
            set sel_idx 0
         }
         ${frm1}.selist selection set $sel_idx
         ${frm1}.selist see $sel_idx
      }

      if {[string compare $mode "pi"] == 0} {
         Rnotebook:raise .remlist.nb 1
         RemPiList_Select
      } elseif {[string compare $mode "cfg"] == 0} {
         RemPiList_Select
      }
   }
}

# update dialog state after change in reminder selection
proc RemPiList_Select {} {
   global remlist_pilist remlist_cfpigrp
   global remlist_lastpi_start remlist_lastpi_netwop
   global reminders

   catch {unset remlist_lastpi_start}
   catch {unset remlist_lastpi_netwop}

   set frm1 [Rnotebook:frame .remlist.nb 1]
   set sel_idx [${frm1}.selist curselection]
   if {[llength $sel_idx] == 1} {
      if {$sel_idx < [llength $remlist_pilist]} {
         # set "delete" button state
         ${frm1}.cmd.delete configure -state normal
         ${frm1}.cmd.mb_grp configure -state normal
         # set current group in group menu
         set rem_idx [lindex $remlist_pilist $sel_idx]
         if {$rem_idx < [llength $reminders]} {
            set elem [lindex $reminders $rem_idx]
            set remlist_cfpigrp [lindex $elem $::rpi_grptag_idx]
            set remlist_lastpi_start [lindex $elem $::rpi_start_idx]
            set remlist_lastpi_netwop [lindex $elem $::rpi_netwop_idx]
         }
      } else {
         bgerror "RemPiList-Select: invalid cursor index $rem_idx"
      }
   } else {
      ${frm1}.cmd.delete configure -state disabled
      ${frm1}.cmd.mb_grp configure -state disabled
   }
}

# callback for selection in reminder group popup menu
proc RemPiList_SetGroup {} {
   global remlist_pilist remlist_cfpigrp
   global reminders remgroups
   global remgroup_filter

   set frm1 [Rnotebook:frame .remlist.nb 1]
   set sel_idx [${frm1}.selist curselection]
   if {[llength $sel_idx] == 1} {
      set rem_idx [lindex $remlist_pilist $sel_idx]
      if {$rem_idx < [llength $reminders]} {
         if [info exists remgroups($remlist_cfpigrp)] {
            C_PiRemind_PiSetGroup $remlist_cfpigrp $rem_idx

            # update reminder list display
            set elem [lreplace [${frm1}.selist get $sel_idx] 1 1 \
                               [lindex $remgroups($remlist_cfpigrp) $::rgp_name_idx]]
            ${frm1}.selist delete $sel_idx
            ${frm1}.selist insert $sel_idx $elem
            ${frm1}.selist selection set $sel_idx

            if {$::pibox_type != 0} {
               C_PiNetBox_Invalidate
            }
            C_PiBox_Refresh
            UpdateRcFile
            Reminder_UpdateTimer
         } else {
            bgerror "RemPiList-SetGroup: invalid group tag $remlist_cfpigrp"
         }
      } else {
         bgerror "RemPiList-SetGroup: invalid reminder index $rem_idx"
      }
   }
}

# callback for mouse-click with button #3 into list: context menu
proc RemPiList_OpenContextMenu {w x y rootx rooty} {
   set frm1 [Rnotebook:frame .remlist.nb 1]
   set sel_idx [${frm1}.selist curselection]
   if {[llength $sel_idx] == 1} {
      # check which column was clicked into
      set coltag [$w column nearest $x]
      if {[string compare $coltag "group"] == 0} {
         # group column -> display group selection menu
         tk_popup ${frm1}.cmd.mb_grp.men $rootx $rooty
      } elseif {[string compare $coltag "state"] == 0} {
         tk_popup ${frm1}.remlist_ctx_state $rootx $rooty
      }
   }
}

# context menu for "state" column
proc RemList_PostStateCtxMenu {wid frm1} {
   global remlist_pilist
   global reminders

   set sel_idx [${frm1}.selist curselection]
   if {[llength $sel_idx] == 1} {
      set rem_idx [lindex $remlist_pilist $sel_idx]
      if {$rem_idx < [llength $reminders]} {
         set state_reset disabled
         set state_suppress disabled
         set state_repeat disabled

         set elem [lindex $reminders $rem_idx]
         if {(([lindex $elem $::rpi_ctrl_idx] & $::rpi_ctrl_mask) == $::rpi_ctrl_repeat) && \
             (([lindex $elem $::rpi_ctrl_idx] & ~ $::rpi_ctrl_mask) >= [C_ClockSeconds])} {
            set state_reset normal
            set state_suppress normal
         } elseif {([lindex $elem $::rpi_ctrl_idx] & $::rpi_ctrl_mask) == $::rpi_ctrl_suppress} {
            set state_reset normal
            set state_repeat normal
         } else {
            set state_suppress normal
            set state_repeat normal
         }

         set grp_tag [lindex $elem $::rpi_grptag_idx]
         set netwop [lindex $elem $::rpi_netwop_idx]
         set start [lindex $elem $::rpi_start_idx]

         $wid add command -label "Reset state" -state $state_reset -command \
                          [list C_PiRemind_SetControl $::rpi_ctrl_default 0 $grp_tag $netwop $start]
         $wid add separator
         $wid add command -label "Suppress messages" -state $state_suppress -command \
                          [list C_PiRemind_SetControl $::rpi_ctrl_suppress 0 $grp_tag $netwop $start]
         $wid add command -label "Repeat message" -state $state_repeat -command \
                          [list C_PiRemind_SetControl $::rpi_ctrl_repeat 0 $grp_tag $netwop $start]
      }
   }
}

# callback for "delete" button
proc RemPiList_Delete {} {
   global remlist_pilist
   global reminders
   global remgroup_filter

   set frm1 [Rnotebook:frame .remlist.nb 1]
   set sel_idx [${frm1}.selist curselection]
   if {[llength $sel_idx] == 1} {
      set rem_idx [lindex $remlist_pilist $sel_idx]
      if {$rem_idx < [llength $reminders]} {

         # remove the reminder from the listbox
         ${frm1}.selist delete $sel_idx
         set tmpl {}
         for {set idx 0} {$idx < [llength $remlist_pilist]} {incr idx} {
            set tmp_idx [lindex $remlist_pilist $idx]
            if {$tmp_idx > $rem_idx} {
               incr tmp_idx -1
               lappend tmpl $tmp_idx
            } elseif {$tmp_idx < $rem_idx} {
               lappend tmpl $tmp_idx
            }
         }
         set remlist_pilist $tmpl
         if {$sel_idx < [llength $remlist_pilist]} {
            ${frm1}.selist selection set $sel_idx
         } elseif {$sel_idx > 0} {
            ${frm1}.selist selection set [expr $sel_idx - 1]
         }
         RemPiList_Select

         # remove the reminder data
         C_PiRemind_RemovePi $rem_idx

         # refresh the listbox to remove possible markers
         if {$::pibox_type != 0} {
            C_PiNetBox_Invalidate
         }
         C_PiBox_Refresh

         # save the updated reminder list in the rc/ini file
         UpdateRcFile

         # remove events for this reminder
         Reminder_UpdateTimer

      } else {
         bgerror "RemPiList-Delete: invalid reminder index $rem_idx"
      }
   }
}

# callback for "show" button: make the selected PI displayed in PI listbox
proc RemPiList_GotoPi {} {
   global remlist_pilist
   global reminders

   set frm1 [Rnotebook:frame .remlist.nb 1]
   set sel_idx [${frm1}.selist curselection]
   if {[llength $sel_idx] == 1} {
      set rem_idx [lindex $remlist_pilist $sel_idx]
      if {$rem_idx < [llength $reminders]} {

         set elem [lindex $reminders $rem_idx]

         ResetFilterState
         SelectNetwopByIdx [lindex $elem $::rpi_netwop_idx] 1
         C_PiBox_GotoTime 0 [lindex $elem $::rpi_start_idx]

      } else {
         bgerror "RemPiList-GotoPi: invalid reminder index $rem_idx"
      }
   }
}

# callback for changes in "display" checkbuttons
proc RemPiList_CheckDispFlags {} {
   global remlist_disp_pend remlist_disp_exp remlist_disp_sup

   # make sure at least one flag is set
   if {($remlist_disp_pend == 0) && ($remlist_disp_exp == 0) && ($remlist_disp_sup == 0)} {
      set remlist_disp_pend 1
   }
   RemPiList_FillPi pi
}

# callback for Configure (aka resize) event on the toplevel window
proc RemList_WindowResized {w} {
   global remlist_winsize

   if {[string compare $w ".remlist"] == 0} {
      set new_size "[winfo width .remlist]x[winfo height .remlist]"

      if {![info exists remlist_winsize] || \
          ([string compare $new_size $remlist_winsize] != 0)} {
         set remlist_winsize $new_size

         UpdateRcFile
      }
   }
}

proc RemList_WindowClose {} {
   global remlist_pilist remlist_cfpigrp
   global remlist_sclist remlist_scgrplist remlist_cfscgrp
   global rempilist_colsize remsclist_colsize pi_font

   catch {
      set cwid [font measure $pi_font 0]

      # save user-configured column sizes in both listboxes
      set frm [Rnotebook:frame .remlist.nb 1]
      set pi_colsize {}
      foreach pixwid [${frm}.selist column widths] {
         lappend pi_colsize [expr int(($pixwid + $cwid/2) / $cwid)]
      }
      set frm [Rnotebook:frame .remlist.nb 2]
      set sc_colsize {}
      foreach pixwid [${frm}.selist column widths] {
         lappend sc_colsize [expr int(($pixwid + $cwid/2) / $cwid)]
      }
      if {([llength $pi_colsize] == 6) && ([llength $sc_colsize] == 2)} {
         if {($pi_colsize != $rempilist_colsize) || ($sc_colsize != $remsclist_colsize)} {
            set rempilist_colsize $pi_colsize
            set remsclist_colsize $sc_colsize
            UpdateRcFile
         }
      } else {
         bgerror "RemList-WindowClose: invalid colsizes: '$pi_colsize', '$sc_colsize'"
      }
   }

   destroy .remlist

   # unset all temporary variables (catch errors as not all are always defined)
   foreach var_name {remlist_pilist remlist_cfpigrp \
                     remlist_lastpi_start remlist_lastpi_netwop \
                     remlist_sclist remlist_scgrplist remlist_cfscgrp} {
      catch [list unset $var_name]
   }
}

# helper function to fill reminder group menu when it pops up
# used for the "set group" popup & context menus both in PI and shortcut lists
proc RemList_PostGroupMenu {wid is_pi} {
   global remgroups remgroup_order

   if $is_pi {
      set rbvar remlist_cfpigrp
      set rbcmd RemPiList_SetGroup
   } else {
      set rbvar remlist_cfscgrp
      set rbcmd RemScList_SetGroup
   }

   # populate group selection menu with group names
   foreach grp_tag $remgroup_order {
      set name [lindex $remgroups($grp_tag) $::rgp_name_idx]
      $wid add radiobutton -label $name -variable $rbvar -value $grp_tag -command $rbcmd
   }
   $wid add separator
   $wid add command -label "Group config..." -command PopupReminderConfig
}

# helper function to sort list of reminder indices by reminder's start times
proc SortScReminders {a b} {
   return [string compare -nocase [lindex $a 2] [lindex $b 2]]
}

# fill listbox with all shortcut reminders (of all groups)
proc RemScList_FillShortcuts {} {
   global remlist_popup remlist_sclist remlist_scgrplist
   global reminders remgroups remgroup_order shortcuts

   if $remlist_popup {
      set ltmp {}
      foreach grp_tag $remgroup_order {
         foreach sc_tag [lindex $remgroups($grp_tag) $::rgp_sclist_idx] {
            if [info exists shortcuts($sc_tag)] {
               lappend ltmp [list $sc_tag $grp_tag [lindex $shortcuts($sc_tag) $::fsc_name_idx]]
            }
         }
      }
      set ltmp [lsort -command SortScReminders $ltmp]

      set frm2 [Rnotebook:frame .remlist.nb 2]
      ${frm2}.selist delete 0 end

      set remlist_sclist {}
      set remlist_scgrplist {}
      for {set sel_idx 0} {$sel_idx < [llength $ltmp]} {incr sel_idx} {
         set elem [lindex $ltmp $sel_idx]
         set sc_tag [lindex $elem 0]
         set grp_tag [lindex $elem 1]
         lappend remlist_sclist $sc_tag
         lappend remlist_scgrplist $grp_tag

         ${frm2}.selist insert end [list \
                   [lindex $remgroups($grp_tag) $::rgp_name_idx] \
                   [lindex $shortcuts($sc_tag) $::fsc_name_idx]]
      }

      if {($sel_idx > 0) && ([llength [${frm2}.selist curselection]] == 0)} {
         ${frm2}.selist selection set 0
         ${frm2}.selist see 0
      }
   }
}

# update dialog state after change in shortcut selection
proc RemScList_Select {} {
   global remlist_sclist remlist_scgrplist remlist_cfscgrp
   global reminders

   set frm2 [Rnotebook:frame .remlist.nb 2]
   set sel_idx [${frm2}.selist curselection]
   if {([llength $sel_idx] == 1) && ($sel_idx < [llength $remlist_sclist])} {
      # set "delete" button state
      ${frm2}.cmd.delete configure -state normal
      ${frm2}.cmd.mb_grp configure -state normal
      # set current group in group menu
      set remlist_cfscgrp [lindex $remlist_scgrplist $sel_idx]
   } else {
      ${frm2}.cmd.delete configure -state disabled
      ${frm2}.cmd.mb_grp configure -state disabled
   }
}

# helper function to fill popup menu with list of all shortcuts
proc RemScList_PostScMenu {wid is_stand_alone} {
   global remlist_sclist remlist_scgrplist
   global shortcuts shortcut_tree

   ShortcutTree_MenuFill .all.shortcuts.list $wid $shortcut_tree shortcuts \
                         RemScList_AddShortcut RemScList_PostScMenu_StateCb
}

# callback for filling shortcut menu: disable shortcuts which are already in the list
proc RemScList_PostScMenu_StateCb {sc_tag} {
   global remlist_sclist
   global shortcuts

   if {[info exists shortcuts($sc_tag)] &&
       ([lsearch $remlist_sclist $sc_tag] == -1)} {
      return "normal"
   } else {
      return "disabled"
   }
}

# helper function to update shortcut list in the given group or in all groups
proc RemScList_SaveList {{group_list {}}} {
   global remlist_sclist remlist_scgrplist
   global remgroups

   if {[llength $group_list] == 0} {
      set group_list [array names remgroups]
   }

   foreach grp_tag $group_list {
      if [info exists remgroups($grp_tag)] {
         set ltmp {}
         for {set idx 0} {$idx < [llength $remlist_sclist]} {incr idx} {
            if {[lindex $remlist_scgrplist $idx] == $grp_tag} {
               lappend ltmp [lindex $remlist_sclist $idx]
            }
         }
         set remgroups($grp_tag) [lreplace $remgroups($grp_tag) \
                                        $::rgp_sclist_idx $::rgp_sclist_idx $ltmp]]
      }
   }
}

# callback for shortcut addition menu
proc RemScList_AddShortcut {sc_tag} {
   global remlist_sclist remlist_scgrplist
   global remgroups remgroup_order
   global shortcuts
   global remgroup_filter

   set grp_tag [lindex $remgroup_order 0]

   if [info exists shortcuts($sc_tag)] {

      lappend remlist_sclist $sc_tag
      lappend remlist_scgrplist $grp_tag

      RemScList_SaveList $grp_tag

      set frm2 [Rnotebook:frame .remlist.nb 2]
      ${frm2}.selist insert end [list \
                [lindex $remgroups($grp_tag) $::rgp_name_idx] \
                [lindex $shortcuts($sc_tag) $::fsc_name_idx]]
      ${frm2}.selist selection clear 0 end
      ${frm2}.selist selection set end

      RemScList_Select
      DownloadUserDefinedColumnFilters
      UpdatePiListboxColumParams
      # if reminder groups are currently filtered for, update filter
      if {[llength $remgroup_filter] > 0} {
         Reminder_Match $remgroup_filter
      }
      if {$::pibox_type != 0} {
         C_PiNetBox_Invalidate
      }
      C_PiBox_Refresh
      UpdateRcFile
      Reminder_UpdateTimer
   }
}

# callback for selection in reminder group popup menu
proc RemScList_SetGroup {} {
   global remlist_sclist remlist_scgrplist remlist_cfscgrp
   global remgroups
   global remgroup_filter

   set frm2 [Rnotebook:frame .remlist.nb 2]
   set sel_idx [${frm2}.selist curselection]
   if {[llength $sel_idx] == 1} {
      if {$sel_idx < [llength $remlist_sclist]} {
         if [info exists remgroups($remlist_cfscgrp)] {

            set remlist_scgrplist [lreplace $remlist_scgrplist $sel_idx $sel_idx $remlist_cfscgrp]
            RemScList_SaveList

            # update reminder list display
            set elem [lreplace [${frm2}.selist get $sel_idx] 0 0 \
                               [lindex $remgroups($remlist_cfscgrp) $::rgp_name_idx]]
            ${frm2}.selist delete $sel_idx
            ${frm2}.selist insert $sel_idx $elem
            ${frm2}.selist selection set $sel_idx

            DownloadUserDefinedColumnFilters
            UpdatePiListboxColumParams
            if {[llength $remgroup_filter] > 0} {
               Reminder_Match $remgroup_filter
            }
            if {$::pibox_type != 0} {
               C_PiNetBox_Invalidate
            }
            C_PiBox_Refresh
            UpdateRcFile
            Reminder_UpdateTimer
         } else {
            bgerror "RemScList-SetGroup: invalid group tag $remlist_cfscgrp"
         }
      } else {
         bgerror "RemScList-SetGroup: invalid index $sel_idx"
      }
   }
}

# callback for mouse-click with button #3 into list: context menu
proc RemScList_OpenContextMenu {w x y rootx rooty} {
   set frm2 [Rnotebook:frame .remlist.nb 2]
   set sel_idx [${frm2}.selist curselection]
   if {[llength $sel_idx] == 1} {
      # check which column was clicked into
      set coltag [$w column nearest $x]
      if {[string compare $coltag "group"] == 0} {
         # group column -> display group selection menu
         tk_popup ${frm2}.cmd.mb_grp.men $rootx $rooty
      }
   }
}

# callback for "delete" button
proc RemScList_Delete {} {
   global remlist_sclist remlist_scgrplist remlist_cfscgrp
   global remgroup_filter

   set frm2 [Rnotebook:frame .remlist.nb 2]
   set sel_idx [${frm2}.selist curselection]
   if {[llength $sel_idx] == 1} {
      if {$sel_idx < [llength $remlist_sclist]} {

         set grp_tag [lindex $remlist_scgrplist $sel_idx]
         set remlist_sclist [lreplace $remlist_sclist $sel_idx $sel_idx]
         set remlist_scgrplist [lreplace $remlist_scgrplist $sel_idx $sel_idx]
         RemScList_SaveList $grp_tag

         # remove the reminder from the listbox
         ${frm2}.selist delete $sel_idx
         if {$sel_idx < [llength $remlist_sclist]} {
            ${frm2}.selist selection set $sel_idx
         } elseif {$sel_idx > 0} {
            ${frm2}.selist selection set [expr $sel_idx - 1]
         }
         RemScList_Select
         DownloadUserDefinedColumnFilters
         UpdatePiListboxColumParams
         if {$remgroup_filter != {}} {
            Reminder_Match $remgroup_filter
         }
         if {$::pibox_type != 0} {
            C_PiNetBox_Invalidate
         }
         C_PiBox_Refresh
         UpdateRcFile
         Reminder_UpdateTimer
      } else {
         bgerror "RemScList-Delete: invalid index $sel_idx"
      }
   }
}

## ---------------------------------------------------------------------------
## Open dialog to configure reminder groups
##
proc PopupReminderConfig {} {
   global remgroups remgroup_order
   global remcfg_selist remcfg_selidx
   global remcfg_popup pi_font

   if {$remcfg_popup == 0} {
      CreateTransientPopup .remcfg "Reminder group configuration"
      wm resizable .remcfg 1 1
      set remcfg_popup 1

      mclistbox_load

      # column one: listbox with group overview
      frame      .remcfg.frm1 -borderwidth 2 -relief raised
      scrollbar  .remcfg.frm1.list_sb -orient vertical -command {.remcfg.frm1.selist yview} -takefocus 0
      grid       .remcfg.frm1.list_sb -row 0 -column 0 -sticky ns -pady 10
      mclistbox::mclistbox .remcfg.frm1.selist -relief ridge -columnrelief flat -labelanchor c \
                                -width 26 -selectmode browse -columnborderwidth 0 \
                                -exportselection 0 -font $pi_font -labelfont [DeriveFont $pi_font -2] \
                                -yscrollcommand {.remcfg.frm1.list_sb set} \
                                -fillcolumn action
      .remcfg.frm1.selist column add level -label "" -width 3
      .remcfg.frm1.selist column add name -label "Group Name" -width 10
      .remcfg.frm1.selist column add action -label "Actions" -width 12

      bind       .remcfg.frm1.selist <<ListboxSelect>> {+ RemCfg_GroupSelect}
      bind       .remcfg.frm1.selist <Double-Button-1> {RemCfg_ToggleGroupEnable \
                                                          [::mclistbox::convert %W -W] \
                                                          [::mclistbox::convert %W -x %x] \
                                                          [::mclistbox::convert %W -y %y] \
                                                          %X %Y}
      bind       .remcfg.frm1.selist <Enter> {focus %W}
      bind       .remcfg.frm1.selist <Key-Delete> {tkButtonInvoke .remcfg.frm1.frm11.delete}
      bind       .remcfg.frm1.selist <Escape> {after idle {tkButtonInvoke .remcfg.cmd.abort}}
      grid       .remcfg.frm1.selist -row 0 -column 1 -sticky news -pady 10

      # column two: command buttons
      frame      .remcfg.frm1.frm11
      button     .remcfg.frm1.frm11.new -text "New" -command RemCfg_GroupCreate
      pack       .remcfg.frm1.frm11.new -side top -fill x
      frame      .remcfg.frm1.frm11.updown
      button     .remcfg.frm1.frm11.updown.up -bitmap "bitmap_ptr_up" -command RemCfg_GroupShiftUp
      pack       .remcfg.frm1.frm11.updown.up -side left -fill x -expand 1
      button     .remcfg.frm1.frm11.updown.down -bitmap "bitmap_ptr_down" -command RemCfg_GroupShiftDown
      pack       .remcfg.frm1.frm11.updown.down -side left -fill x -expand 1
      pack       .remcfg.frm1.frm11.updown -side top -anchor nw -fill x
      button     .remcfg.frm1.frm11.delete -text "Delete" -command RemCfg_GroupDelete
      pack       .remcfg.frm1.frm11.delete -side top -fill x

      button     .remcfg.frm1.frm11.show -text "Show" -command RemCfg_ShowMatch
      pack       .remcfg.frm1.frm11.show -side top -fill x -pady 25
      grid       .remcfg.frm1.frm11 -row 0 -column 2 -sticky ns -pady 10 -padx 10

      # column three: options
      frame      .remcfg.frm1.frm12 -borderwidth 2 -relief groove
      label      .remcfg.frm1.frm12.lab_name -text "Label:"
      grid       .remcfg.frm1.frm12.lab_name -row 0 -column 0 -sticky w
      entry      .remcfg.frm1.frm12.ent_name -textvariable remcfg_label -width 15 -exportselection 0
      grid       .remcfg.frm1.frm12.ent_name -row 0 -column 1 -sticky we -padx 5
      bind       .remcfg.frm1.frm12.ent_name <Enter> {SelectTextOnFocus %W}
      bind       .remcfg.frm1.frm12.ent_name <Return> RemCfg_OptionsUpdate
      checkbutton .remcfg.frm1.frm12.chk_ena -text "Disable all group events" -variable remcfg_disable -command RemCfg_OptionsUpdate
      grid       .remcfg.frm1.frm12.chk_ena -row 1 -column 0 -columnspan 2 -sticky w -pady 5
      label      .remcfg.frm1.frm12.lab_msgh -text "Display messages:"
      grid       .remcfg.frm1.frm12.lab_msgh -row 2 -column 0 -columnspan 2 -sticky w
      label      .remcfg.frm1.frm12.lab_msgl -text "At minutes before start:" -font $::font_normal
      grid       .remcfg.frm1.frm12.lab_msgl -row 3 -column 0 -sticky w
      entry      .remcfg.frm1.frm12.ent_msgl -textvariable remcfg_msgl -width 15 -exportselection 0
      grid       .remcfg.frm1.frm12.ent_msgl -row 3 -column 1 -sticky we -padx 5
      bind       .remcfg.frm1.frm12.ent_msgl <Enter> {SelectTextOnFocus %W}
      bind       .remcfg.frm1.frm12.ent_msgl <Return> RemCfg_OptionsUpdate
      label      .remcfg.frm1.frm12.lab_cmdh -text "Invoke script:"
      grid       .remcfg.frm1.frm12.lab_cmdh -row 4 -column 0 -columnspan 2 -sticky w
      label      .remcfg.frm1.frm12.lab_cmdl -text "At minutes before start:" -font $::font_normal
      grid       .remcfg.frm1.frm12.lab_cmdl -row 5 -column 0 -sticky w
      entry      .remcfg.frm1.frm12.ent_cmdl -textvariable remcfg_cmdl -width 15 -exportselection 0
      grid       .remcfg.frm1.frm12.ent_cmdl -row 5 -column 1 -sticky we -padx 5
      bind       .remcfg.frm1.frm12.ent_cmdl <Enter> {SelectTextOnFocus %W}
      bind       .remcfg.frm1.frm12.ent_cmdl <Return> RemCfg_OptionsUpdate
      label      .remcfg.frm1.frm12.lab_cmdv -text "Command line:" -font $::font_normal
      grid       .remcfg.frm1.frm12.lab_cmdv -row 6 -column 0 -sticky w
      entry      .remcfg.frm1.frm12.ent_cmdv -textvariable remcfg_cmdv -width 15 -exportselection 0
      grid       .remcfg.frm1.frm12.ent_cmdv -row 6 -column 1 -sticky we -padx 5
      bind       .remcfg.frm1.frm12.ent_cmdv <Enter> {SelectTextOnFocus %W}
      bind       .remcfg.frm1.frm12.ent_cmdv <Return> RemCfg_OptionsUpdate
      checkbutton .remcfg.frm1.frm12.chk_cmdask -text "Ask before executing script" -variable remcfg_cmdask -font $::font_normal
      grid       .remcfg.frm1.frm12.chk_cmdask -row 7 -column 0 -columnspan 2 -sticky w
      grid       columnconfigure .remcfg.frm1.frm12 1 -weight 1
      grid       .remcfg.frm1.frm12 -row 0 -column 3 -sticky news -pady 10 -padx 5

      grid       columnconfigure .remcfg.frm1 1 -weight 1
      grid       columnconfigure .remcfg.frm1 3 -weight 2
      grid       rowconfigure .remcfg.frm1 0 -weight 1
      pack       .remcfg.frm1 -side top -fill both -expand 1

      ## bottom frame: command buttons
      frame      .remcfg.cmd
      button     .remcfg.cmd.help -text "Help" -width 7 -command {PopupHelp $helpIndex(Reminders) "Reminder group configuration"} -width 9
      button     .remcfg.cmd.abort -text "Abort" -width 7 -command {RemCfg_WindowClose abort}
      button     .remcfg.cmd.ok -text "Ok" -width 7 -command {RemCfg_WindowClose ok} -default active
      pack       .remcfg.cmd.help .remcfg.cmd.abort .remcfg.cmd.ok -side left -padx 10 -pady 5
      pack       .remcfg.cmd -side top

      focus      .remcfg.frm1.frm12.ent_name
      bind       .remcfg.cmd <Destroy> {+ set remcfg_popup 0}
      bind       .remcfg <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]
      bind       .remcfg <Key-F1> {PopupHelp $helpIndex(Reminders) "Reminder group configuration"}
      wm protocol .remcfg WM_DELETE_WINDOW {RemCfg_WindowClose abort}

      # make a copy of the original group configuration list
      set remcfg_selist {}
      foreach grp_tag $remgroup_order {
         set elem $remgroups($grp_tag)

         # save the group tag in place of the filter context cache index
         lappend remcfg_selist [lreplace $elem $::rgp_ctxcache_idx $::rgp_ctxcache_idx $grp_tag]

         # display the data in the listbox
         RemCfg_InsertGroupElement end $elem
      }
      set remcfg_selidx -1
      .remcfg.frm1.selist selection set 0
      RemCfg_GroupSelect

      update
      wm minsize .remcfg [winfo reqwidth .remcfg] [winfo reqheight .remcfg]

   } else {
      raise .remcfg
   }
}

# helper function which formats and displays a group element in the listbox
proc RemCfg_InsertGroupElement {lidx elem} {
   if {! [lindex $elem $::rgp_disable_idx]} {
      set actl {}
      foreach toff [lindex [lindex $elem $::rgp_msg_idx] $::ract_toffl_idx] {
         lappend actl $toff
      }
      foreach toff [lindex [lindex $elem $::rgp_cmd_idx] $::ract_toffl_idx] {
         lappend actl $toff
      }
      catch {set actl [lsort -integer -decreasing -unique $actl]}

      set action_text [join $actl ","]

   } else {
      set action_text "disabled"
   }
   .remcfg.frm1.selist insert $lidx [list \
                [.remcfg.frm1.selist index $lidx] \
                [lindex $elem $::rgp_name_idx] \
                $action_text]
}

proc RemCfg_OptionsUpdate {} {
   global remcfg_selist remcfg_selidx
   global remcfg_disable remcfg_label remcfg_msgl remcfg_cmdl remcfg_cmdv remcfg_cmdask

   if {$remcfg_selidx != -1} {
      # collect message start offsets from entry field content
      set msgl {}
      foreach off [split $remcfg_msgl { ,;}] {
         if {[regexp {^\-?[0-9]+$} $off]} {
            lappend msgl $off
         }
      }
      set msgl [lsort -integer -decreasing -unique $msgl]
      set cmdl {}
      foreach off [split $remcfg_cmdl { ,;}] {
         if {[regexp {^\-?[0-9]+$} $off]} {
            lappend cmdl $off
         }
      }
      set cmdl [lsort -integer -decreasing -unique $cmdl]

      # update the data in the list
      set elem [lindex $remcfg_selist $remcfg_selidx]
      set elem [lreplace $elem $::rgp_name_idx $::rgp_name_idx $remcfg_label]
      # note: list assembly implies indices: $::ract_toffl_idx, $::ract_argv_idx, $::ract_ask_idx
      set elem [lreplace $elem $::rgp_msg_idx $::rgp_msg_idx [list $msgl]]
      set elem [lreplace $elem $::rgp_cmd_idx $::rgp_cmd_idx [list $cmdl $remcfg_cmdv $remcfg_cmdask]]
      set elem [lreplace $elem $::rgp_disable_idx $::rgp_disable_idx $remcfg_disable]
      set remcfg_selist [lreplace $remcfg_selist $remcfg_selidx $remcfg_selidx $elem]

      # update the listbox content
      set oldsel [.remcfg.frm1.selist curselection]
      .remcfg.frm1.selist delete $remcfg_selidx
      RemCfg_InsertGroupElement $remcfg_selidx $elem
      .remcfg.frm1.selist selection set $oldsel
   }
}

proc RemCfg_GroupSelect {} {
   global remcfg_selist remcfg_selidx
   global remcfg_disable remcfg_label remcfg_msgl remcfg_cmdl remcfg_cmdv remcfg_cmdask
   global remgroups

   RemCfg_OptionsUpdate

   set idx [.remcfg.frm1.selist curselection]
   if {[llength $idx] == 1} {
      set remcfg_selidx $idx

      set elem [lindex $remcfg_selist $remcfg_selidx]
      set remcfg_label [lindex $elem $::rgp_name_idx]
      set remcfg_disable [lindex $elem $::rgp_disable_idx]
      set remcfg_msgl [join [lindex [lindex $elem $::rgp_msg_idx] $::ract_toffl_idx] ","]
      set remcfg_cmdl [join [lindex [lindex $elem $::rgp_cmd_idx] $::ract_toffl_idx] ","]
      set remcfg_cmdv [lindex [lindex $elem $::rgp_cmd_idx] $::ract_argv_idx]
      set remcfg_cmdask [lindex [lindex $elem $::rgp_cmd_idx] $::ract_ask_idx]

      if {[llength $remcfg_selist] > 1} {
         .remcfg.frm1.frm11.delete configure -state normal
      } else {
         .remcfg.frm1.frm11.delete configure -state disabled
      }
      if {[llength $remcfg_selist] < 31} {
         .remcfg.frm1.frm11.new configure -state normal
      } else {
         .remcfg.frm1.frm11.new configure -state disabled
      }
      if [info exists remgroups([lindex $elem $::rgp_ctxcache_idx])] {
         .remcfg.frm1.frm11.show configure -state normal
      } else {
         .remcfg.frm1.frm11.show configure -state disabled
      }
   }
}

# callback for double mouse-click into "action" column
proc RemCfg_ToggleGroupEnable {w x y rootx rooty} {
   global remcfg_label remcfg_msgl remcfg_disable

   set sel_idx [.remcfg.frm1.selist curselection]
   if {[llength $sel_idx] == 1} {
      # check which column was clicked into
      set coltag [$w column nearest $x]
      if {[string compare $coltag "action"] == 0} {
         # "actions" column -> toggle disable flag
         set remcfg_disable [expr !$remcfg_disable]
         RemCfg_GroupSelect
      }
   }
}

# callback for "new" button: create new group with dummy parameters
proc RemCfg_GroupCreate {} {
   global remcfg_selist remcfg_selidx

   RemCfg_OptionsUpdate

   # max. 31 groups can be defined (total 32, one group is reserved for "suppress")
   # because a bitmask is used for group matches in PI filtering
   if {[llength $remcfg_selist] < 31} {

      # generate "random", unique new tag
      # (note: deleted tags shouldn't be re-used either; use of wall-clock time makes this probable)
      set grp_tag [C_ClockSeconds]
      set ltmp {}
      foreach elem $remcfg_selist {
         set cmp_tag [lindex $elem $::rgp_ctxcache_idx]
         if {![info exists remgroups($cmp_tag)]} {
            lappend ltmp $cmp_tag
         }
      }
      while { [info exists remgroups($grp_tag)] || \
             ([lsearch $ltmp $grp_tag] != -1)} {
         incr grp_tag
      }

      set selidx [llength $remcfg_selist]
      lappend remcfg_selist [list {*unnamed*} {{}} {{} {} 0} {} 0 0 {} $grp_tag]

      RemCfg_InsertGroupElement $selidx [lindex $remcfg_selist $selidx]

      .remcfg.frm1.selist selection clear 0 end
      .remcfg.frm1.selist selection set $selidx
      .remcfg.frm1.selist see $selidx
      RemCfg_GroupSelect

   } else {
      bgerror "RemCfg-GroupCreate: already max. number of groups reached"
   }
}

proc RemCfg_GroupShiftUp {} {
   global remcfg_selist remcfg_selidx

   set sel_idx [.remcfg.frm1.selist curselection]
   if {([llength $sel_idx] == 1) && ($sel_idx > 0)} {
      set idx_above [expr $sel_idx - 1]
      # remove the item in the listbox widget above the shifted one
      set lbx_copy [.remcfg.frm1.selist get $idx_above]
      .remcfg.frm1.selist delete $idx_above
      # re-insert the just removed item below the shifted one
      .remcfg.frm1.selist insert $sel_idx $lbx_copy

      # perform the same exchange in the associated list
      set remcfg_selist [lreplace $remcfg_selist [expr $sel_idx - 1] $sel_idx \
                                  [lindex $remcfg_selist $sel_idx] \
                                  [lindex $remcfg_selist $idx_above]]
      incr remcfg_selidx -1
   }
}

proc RemCfg_GroupShiftDown {} {
   global remcfg_selist remcfg_selidx

   set sel_idx [.remcfg.frm1.selist curselection]
   if {([llength $sel_idx] == 1) && \
       ($sel_idx < [expr [llength $remcfg_selist] - 1])} {
      set idx_below [expr $sel_idx + 1]

      set lbx_copy [.remcfg.frm1.selist get $idx_below]
      .remcfg.frm1.selist delete $idx_below
      .remcfg.frm1.selist insert $sel_idx $lbx_copy

      set remcfg_selist [lreplace $remcfg_selist $sel_idx $idx_below \
                                  [lindex $remcfg_selist $idx_below] \
                                  [lindex $remcfg_selist $sel_idx]]
      incr remcfg_selidx
   }
}

proc RemCfg_GroupDelete {} {
   global remcfg_selist remcfg_selidx
   global reminders

   RemCfg_OptionsUpdate

   if {[llength $remcfg_selist] > 1} {
      set group [.remcfg.frm1.selist curselection]
      if {[llength $group] == 1} {
         if {$group < [llength $remcfg_selist]} {

            # copy shortcut list from original group list (might have been changed in parallel)
            RemCfg_MergeScIntoTempList
            set elem [lindex $remcfg_selist $group]
            set grp_tag [lindex $elem $::rgp_ctxcache_idx]
            set sc_count [llength [lindex $elem $::rgp_sclist_idx]]

            set pi_count 0
            for {set idx 0} {$idx < [llength $reminders]} {incr idx} {
               if {[lindex [lindex $reminders $idx] $::rpi_grptag_idx] == $grp_tag} {
                  incr pi_count
               }
            }

            set verb "is"
            if {$sc_count + $pi_count > 0} {
               set msg {}
               if {$sc_count > 0} {
                  if {$sc_count == 1} {
                     set msg "a shortcut"
                  } else {
                     set msg "$sc_count shortcuts"
                     set verb "are"
                  }
                  if {$pi_count > 0} {
                     append msg " and "
                     set verb "are"
                  }
               }
               if {$pi_count > 0} {
                  if {$pi_count == 1} {
                     set msg "a reminder"
                  } else {
                     set msg "$pi_count reminders"
                     set verb "are"
                  }
               }
               set answer [tk_messageBox -type okcancel -icon warning -parent .remcfg \
                              -message "There $verb still $msg assigned to this group which will be removed. Do you still want to continue?"]
               if {[string compare $answer "cancel"] == 0} {
                  return
               }
            }

            set remcfg_selist [lreplace $remcfg_selist $group $group]

            set remcfg_selidx -1
            .remcfg.frm1.selist delete $group

            # place cursor on the group below, or above if none below
            if {$group < [llength $remcfg_selist]} {
               .remcfg.frm1.selist selection set $group
            } elseif {[llength $remcfg_selist] > 0} {
               .remcfg.frm1.selist selection set [expr $group - 1]
            }
            RemCfg_GroupSelect

         } else {
            bgerror "RemCfg-GroupDelete: invalid index $group"
         }
      }
   } else {
      bgerror "RemCfg-GroupDelete: only one group left"
   }
}

# callback for "Show" button: display all reminders for the selected group in the PI listbox
proc RemCfg_ShowMatch {} {
   global remgroups
   global remcfg_selist

   set sel_idx [.remcfg.frm1.selist curselection]
   if {[llength $sel_idx] == 1} {
      if {$sel_idx < [llength $remcfg_selist]} {
         set elem [lindex $remcfg_selist $sel_idx]
         set grp_tag [lindex $elem $::rgp_ctxcache_idx]
         # check if it's a new group (can't have any assignments yet)
         if [info exists remgroups($grp_tag)] {

            Reminder_Match $grp_tag
            C_PiBox_Refresh
         }
      } else {
         bgerror "RemCfg-ShowMatch: invalid index $sel_idx"
      }
   }
}

# helper function to merge shortcuts lists from the global group list into the temp list
# - required because the user may change shortcuts assignments via "Edit reminders" dialog
#   while the group config dialog is open, which maintains a separate copy of group config
proc RemCfg_MergeScIntoTempList {} {
   global remgroups
   global remcfg_selist

   set ltmp {}
   foreach new_elem $remcfg_selist {
      set grp_tag [lindex $new_elem $::rgp_ctxcache_idx]
      if [info exists remgroups($grp_tag)] {
         lappend ltmp [lreplace $new_elem $::rgp_sclist_idx $::rgp_sclist_idx \
                                [lindex $remgroups($grp_tag) $::rgp_sclist_idx]]
      } else {
         lappend ltmp $new_elem
      }
   }
   set remcfg_selist $ltmp
}

# callback for "Ok" and "Abort" buttons and dialog window close via window manager
proc RemCfg_WindowClose {mode} {
   global remcfg_selist remcfg_selidx
   global remcfg_disable remcfg_label remcfg_msgl remcfg_cmdl remcfg_cmdv remcfg_cmdask
   global remgroups remgroup_order reminders
   global remgroup_filter

   RemCfg_OptionsUpdate

   if {[string compare $mode "ok"] != 0} {
      # abort -> check if there were any changes
      if {[llength $remcfg_selist] == [llength $remgroup_order]} {
         set changed 0
         set grp_idx 0
         foreach elem $remcfg_selist {
            set grp_tag [lindex $elem $::rgp_ctxcache_idx]
            if {![info exists remgroups($grp_tag)] || \
                ([lindex $remgroup_order $grp_idx] != $grp_tag) || \
                ([lrange $remgroups($grp_tag) 0 [expr $::rgp_ctxcache_idx - 1]] != \
                 [lrange $elem 0 [expr $::rgp_ctxcache_idx - 1]])} {
               set changed 1
               break
            }
            incr grp_idx
         }
      } else {
         set changed 1
      }
      if $changed {
         set answer [tk_messageBox -type okcancel -icon warning -parent .remcfg \
                       -message "Discard all changes to reminder groups?"]
         if {[string compare $answer "cancel"] == 0} {
            return
         }
      }
   }

   if {[string compare $mode "ok"] == 0} {

      # copy shortcut list from original group list (might have been changed in parallel)
      RemCfg_MergeScIntoTempList

      # save new group config (copy from config list into global array)
      array unset remgroups
      set remgroup_order {}
      foreach elem $remcfg_selist {
         set grp_tag [lindex $elem $::rgp_ctxcache_idx]
         # make sure the tag is unique
         if {! [info exists remgroups($grp_tag)]} {
            # reset abused filter-cache index to -1, updated in download call below
            set remgroups($grp_tag) [lreplace $elem $::rgp_ctxcache_idx $::rgp_ctxcache_idx -1]
            lappend remgroup_order $grp_tag
         } else {
            bgerror "Reminder-WindowClose: non-unique tag $grp_tag"
         }
      }

      # remove reminders which refer to undefined group tags
      set ltmp {}
      foreach elem $reminders {
         if [info exists remgroups([lindex $elem $::rpi_grptag_idx])] {
            lappend ltmp $elem
         }
      }
      set reminders $ltmp
      unset ltmp

      # after changing the reminder list the C level cache must be reloaded
      C_PiRemind_PiReload

      RemPiList_FillPi cfg
      RemScList_FillShortcuts

      DownloadUserDefinedColumnFilters
      UpdatePiListboxColumParams
      if {[llength $remgroup_filter] > 0} {
         Reminder_Match $remgroup_filter
      }
      if {$::pibox_type != 0} {
         C_PiNetBox_Invalidate
      }
      C_PiBox_Refresh
      UpdateRcFile
      Reminder_UpdateTimer
   }

   destroy .remcfg

   # unset all temporary variables (catch errors as not all are always defined)
   foreach var_name {remcfg_selist remcfg_selidx \
                     remcfg_disable remcfg_label remcfg_msgl \
                     remcfg_cmdl remcfg_cmdv remcfg_cmdask} {
      catch {unset $var_name}
   }
}

