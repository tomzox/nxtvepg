#
#  Filter dialogs
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
#    Implements dialogs for filter settings.
#
#  Author: Tom Zoerner
#
#  $Id: dlg_filter.tcl,v 1.4 2002/11/09 20:21:43 tom Exp tom $
#
set substr_grep_title 1
set substr_grep_descr 1
set substr_match_full 0
set substr_match_case 0
set substr_popup 0
set substr_pattern {}
set substr_history {}

set progidx_first 0
set progidx_last  0
set progidx_popup 0

set timsel_popup 0
set timsel_enabled  0
set timsel_relative 0
set timsel_nodate   0
set timsel_absstop  0
set timsel_stop [expr 23*60+59]

set dursel_popup 0
set dursel_min 0
set dursel_max 0

set sortcrit_popup 0


# helper proc to insert all indices in the selection list into the listbox
proc UpdateSortCritListbox {} {
   global sortcrit_popup
   global sortcrit_class sortcrit_class_sel

   if {$sortcrit_popup} {
      .sortcrit.fl.list delete 0 end
      foreach item [lsort -integer $sortcrit_class_sel($sortcrit_class)] {
         .sortcrit.fl.list insert end [format "0x%02x" $item]
      }
   }
}

# called when the timer filter is changed by the Navigate menu
proc TimeFilterExternalChange {} {
   global timsel_popup

   if $timsel_popup {
      UpdateTimeFilterRelStart
   }
}

#=LOAD=SubStrPopup
#=LOAD=ProgIdxPopup
#=LOAD=PopupTimeFilterSelection
#=LOAD=PopupDurationFilterSelection
#=LOAD=PopupSortCritSelection
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

      frame .substr.all

      frame .substr.all.name
      label .substr.all.name.prompt -text "Enter text:"
      pack .substr.all.name.prompt -side left
      entry .substr.all.name.str -textvariable substr_pattern -width 30
      pack .substr.all.name.str -side left -fill x -expand 1
      bind .substr.all.name.str <Enter> {SelectTextOnFocus %W}
      bind .substr.all.name.str <Return> {tkButtonInvoke .substr.all.cmd.apply}
      bind .substr.all.name.str <Escape> {tkButtonInvoke .substr.all.cmd.dismiss}
      bind .substr.all.name.str <Key-Up> SubStrPopupHistoryMenu
      bind .substr.all.name.str <Key-Down> SubStrPopupHistoryMenu
      menu .substr.all.name.hist -tearoff 0
      button .substr.all.name.but -bitmap "bitmap_ptr_down" -command SubStrPopupHistoryMenu
      pack .substr.all.name.but -side left
      pack .substr.all.name -side top -pady 10 -fill x -expand 1

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
      pack .substr.all.opt -side top -pady 10

      frame .substr.all.cmd
      button .substr.all.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Filtering) "Text search"}
      button .substr.all.cmd.clear -text "Clear" -width 5 -command {set substr_pattern {}; SubstrUpdateFilter}
      button .substr.all.cmd.apply -text "Apply" -width 5 -command SubstrUpdateFilter
      button .substr.all.cmd.dismiss -text "Dismiss" -width 5 -command {destroy .substr}
      pack .substr.all.cmd.help .substr.all.cmd.clear .substr.all.cmd.apply .substr.all.cmd.dismiss -side left -padx 10
      pack .substr.all.cmd -side top

      pack .substr.all -padx 10 -pady 10 -fill x -expand 1
      bind .substr.all <Destroy> {+ set substr_popup 0}
      bind .substr <Key-F1> {PopupHelp $helpIndex(Filtering) "Text search"}

      focus .substr.all.name.str
      wm resizable .substr 1 0
      update
      wm minsize .substr [winfo reqwidth .substr] [winfo reqheight .substr]
   } else {
      raise .substr
   }
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
   global substr_history

   # check if the history list is empty
   if {[llength $substr_history] > 0} {
      set widget .substr.all.name.hist

      # remove the previous content (in case the history changed since the last open)
      $widget delete 0 end

      # insert the previous search strings as menu commands
      foreach item $substr_history {
         $widget add command -label [lindex $item 4] -command [concat SubstrSetFilter $item]
      }

      # post (i.e. display) the menu
      tk_popup $widget [winfo rootx .substr.all.name.str] \
                       [expr [winfo rooty .substr.all.name.str] + [winfo reqheight .substr.all.name.str]]

      # set the cursor onto the first menu element
      $widget entryconfigure 0 -state active
   }
}

##  --------------------------------------------------------------------------
##  Program-Index pop-up
##
proc ProgIdxPopup {} {
   global progidx_first progidx_last
   global progidx_popup

   if {$progidx_popup == 0} {
      CreateTransientPopup .progidx "Program index selection"
      set progidx_popup 1

      frame .progidx.firstidx
      scale .progidx.firstidx.s -from 0 -to 99 -orient horizontal -label "Minimum index:" \
                                -command {ProgIdxSelection "firstidx"} -variable progidx_first
      pack .progidx.firstidx.s -side left -fill x -expand 1
      pack .progidx.firstidx -side top -fill x -expand 1

      frame .progidx.lastidx
      scale .progidx.lastidx.s -from 0 -to 99 -orient horizontal -label "Maximum index:" \
                               -command {ProgIdxSelection "lastidx"} -variable progidx_last
      pack .progidx.lastidx.s -side left -fill x -expand 1
      pack .progidx.lastidx -side top -fill x -expand 1

      frame .progidx.cmd
      button .progidx.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Filtering) "Program index"}
      button .progidx.cmd.clear -text "Undo" -width 5 -command {set filter_progidx 0; C_SelectProgIdx; C_RefreshPiListbox; destroy .progidx}
      button .progidx.cmd.dismiss -text "Dismiss" -width 5 -command {destroy .progidx} -default active
      pack .progidx.cmd.help .progidx.cmd.clear .progidx.cmd.dismiss -side left -padx 10
      pack .progidx.cmd -side top -pady 10

      bind .progidx.cmd <Destroy> {+ set progidx_popup 0}

      # define key stroke bindings
      bind .progidx.cmd.dismiss <Return> {tkButtonInvoke %W}
      bind .progidx.cmd.dismiss <Escape> {tkButtonInvoke .progidx.cmd.clear}
      bind .progidx <Key-F1> {PopupHelp $helpIndex(Filtering) "Program index"}
      focus .progidx.cmd.dismiss

   } else {
      raise .progidx
   }
}

proc ProgIdxSelection {which val} {
   global progidx_first progidx_last

   if {$progidx_last < $progidx_first} {
      if {[string compare $which "lastidx"] == 0} {
         set progidx_first $progidx_last
      } else {
         set progidx_last $progidx_first
      }
   }
   C_SelectProgIdx $progidx_first $progidx_last
   UpdateProgIdxMenuState
   C_RefreshPiListbox
}

##  --------------------------------------------------------------------------
##  Time filter selection popup
##
proc PopupTimeFilterSelection {} {
   global timsel_enabled timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop timsel_nodate
   global timsel_startstr timsel_stopstr
   global timsel_popup

   if {$timsel_popup == 0} {
      CreateTransientPopup .timsel "Time filter selection"
      set timsel_popup 1

      frame .timsel.all
      checkbutton .timsel.all.relstart -text "Start at current time" -command UpdateTimeFilterRelStart -variable timsel_relative
      pack  .timsel.all.relstart -side top -anchor w

      frame .timsel.all.start -bd 2 -relief ridge
      label .timsel.all.start.lab -text "Min. start time:" -width 16 -anchor w
      entry .timsel.all.start.str -width 7 -textvariable timsel_startstr
      bind  .timsel.all.start.str <Enter> {SelectTextOnFocus %W}
      bind  .timsel.all.start.str <Return> {UpdateTimeFilterEntry timsel_start $timsel_startstr}
      bind  .timsel.all.start.str <Escape> {tkButtonInvoke .timsel.cmd.dismiss}
      scale .timsel.all.start.val -orient hor -length 200 -command {UpdateTimeFilter 1} -variable timsel_start -from 0 -to 1439 -showvalue 0
      pack  .timsel.all.start.lab .timsel.all.start.str -side left
      pack  .timsel.all.start.val -side left -fill x -expand 1
      pack  .timsel.all.start -side top -pady 10 -fill x -expand 1

      checkbutton .timsel.all.absstop -text "Stop at end of day" -command UpdateTimeFilterRelStart -variable timsel_absstop
      pack  .timsel.all.absstop -side top -anchor w

      frame .timsel.all.stop -bd 2 -relief ridge
      label .timsel.all.stop.lab -text "Max. start time:" -width 16 -anchor w
      entry .timsel.all.stop.str -text "00:00" -width 7 -textvariable timsel_stopstr
      bind  .timsel.all.stop.str <Enter> {SelectTextOnFocus %W}
      bind  .timsel.all.stop.str <Return> {UpdateTimeFilterEntry timsel_stop $timsel_stopstr}
      bind  .timsel.all.stop.str <Escape> {tkButtonInvoke .timsel.cmd.dismiss}
      scale .timsel.all.stop.val -orient hor -length 200 -command {UpdateTimeFilter 2} -variable timsel_stop -from 0 -to 1439 -showvalue 0
      pack  .timsel.all.stop.lab .timsel.all.stop.str -side left
      pack  .timsel.all.stop.val -side left -fill x -expand 1
      pack  .timsel.all.stop -side top -pady 10 -fill x -expand 1

      checkbutton .timsel.all.nodate -text "Ignore date" -command UpdateTimeFilterRelStart -variable timsel_nodate
      pack  .timsel.all.nodate -side top -anchor w

      frame .timsel.all.date -bd 2 -relief ridge
      label .timsel.all.date.lab -text "Rel. date:" -width 16 -anchor w
      label .timsel.all.date.str -width 7
      scale .timsel.all.date.val -orient hor -length 200 -command {UpdateTimeFilter 0} -variable timsel_date -from 0 -to 6 -showvalue 0
      pack  .timsel.all.date.lab .timsel.all.date.str -side left
      pack  .timsel.all.date.val -side left -fill x -expand 1
      pack  .timsel.all.date -side top -pady 10 -fill x -expand 1
      pack  .timsel.all -padx 10 -pady 10 -side top -fill x -expand 1

      frame .timsel.cmd
      button .timsel.cmd.help -text "Help" -command {PopupHelp $helpIndex(Filtering) "Start Time"}
      button .timsel.cmd.undo -text "Undo" -command {destroy .timsel; UndoTimeFilter}
      button .timsel.cmd.dismiss -text "Dismiss" -command {destroy .timsel}
      pack .timsel.cmd.help .timsel.cmd.undo .timsel.cmd.dismiss -side left -padx 10
      pack .timsel.cmd -side top -pady 5

      bind .timsel <Key-F1> {PopupHelp $helpIndex(Filtering) "Start Time"}
      bind .timsel.all <Destroy> {+ set timsel_popup 0}
      focus .timsel.all.start.str

      set $timsel_enabled 1
      UpdateTimeFilterRelStart

      wm resizable .timsel 1 0
      update
      wm minsize .timsel [winfo reqwidth .timsel] [winfo reqheight .timsel]
   } else {
      raise .timsel
   }
}

proc UpdateTimeFilterRelStart {} {
   global timsel_relative timsel_absstop timsel_nodate

   if {$timsel_relative} {
      .timsel.all.start.str configure -state disabled
      .timsel.all.start.val configure -state disabled
   } else {
      .timsel.all.start.str configure -state normal
      .timsel.all.start.val configure -state normal
   }
   if {$timsel_absstop} {
      .timsel.all.stop.str configure -state disabled
      .timsel.all.stop.val configure -state disabled
   } else {
      .timsel.all.stop.str configure -state normal
      .timsel.all.stop.val configure -state normal
   }
   if {$timsel_relative && !$timsel_absstop} {
      .timsel.all.stop.lab configure -text "Time span:"
   } else {
      .timsel.all.stop.lab configure -text "Max. start time:"
   }
   if {$timsel_nodate} {
      .timsel.all.date.str configure -state disabled
      .timsel.all.date.val configure -state disabled
   } else {
      .timsel.all.date.str configure -state normal
      .timsel.all.date.val configure -state normal
   }
   UpdateTimeFilter 0
}

proc UpdateTimeFilterEntry {varname val} {
   global timsel_start timsel_startstr
   global timsel_stop timsel_stopstr
   upvar $varname timspec

   if {[scan $val "%u:%u" hour minute] == 2} {
      set timspec [expr $hour * 60 + $minute]
      UpdateTimeFilter 0
   } else {
      tk_messageBox -type ok -default ok -icon error -parent .timsel \
                    -message "Invalid input format in \"$val\"; use \"HH:MM\""
   }
}

proc UpdateTimeFilter { round {val 0} } {
   global timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop timsel_nodate
   global timsel_startstr timsel_stopstr
   global timsel_enabled

   set timsel_enabled 1
   if {$round == 1} {
      set timsel_start [expr $timsel_start - ($timsel_start % 5)]
   } elseif {$round == 2} {
      set timsel_stop  [expr $timsel_stop  - ($timsel_stop  % 5)]
   }
   if {$timsel_relative} {
      .timsel.all.start.str configure -state normal
      set timsel_startstr "now"
      .timsel.all.start.str configure -state disabled
   } else {
      set timsel_startstr [Motd2HHMM $timsel_start]
   }
   if {$timsel_absstop} {
      .timsel.all.stop.str configure -state normal
      set timsel_stopstr "23:59"
      .timsel.all.stop.str configure -state disabled
   } else {
      set timsel_stopstr [Motd2HHMM $timsel_stop]
   }
   if {$timsel_nodate} {
      .timsel.all.date.str configure -text "any day"
   } else {
      .timsel.all.date.str configure -text [format "+%d days" $timsel_date]
   }

   SelectTimeFilter
}

# callback for "undo" button
proc UndoTimeFilter {} {
   global timsel_enabled

   set timsel_enabled 0
   SelectTimeFilter
}

##  --------------------------------------------------------------------------
##  Duration filter selection popup
##
proc PopupDurationFilterSelection {} {
   global dursel_min dursel_max
   global dursel_popup

   if {$dursel_popup == 0} {
      CreateTransientPopup .dursel "Duration filter selection"
      set dursel_popup 1

      frame .dursel.all
      label .dursel.all.min_lab -text "Minimum:"
      grid  .dursel.all.min_lab -row 0 -column 0 -sticky w
      entry .dursel.all.min_str -text "00:00" -width 7 -textvariable dursel_minstr
      bind  .dursel.all.min_str <Enter> {SelectTextOnFocus %W}
      bind  .dursel.all.min_str <Return> {SelectDurationFilterEntry dursel_min $dursel_minstr}
      bind  .dursel.all.min_str <Escape> {tkButtonInvoke .dursel.cmd.dismiss}
      grid  .dursel.all.min_str -row 0 -column 1 -sticky we
      scale .dursel.all.min_val -orient hor -length 300 -command {UpdateDurationFilter 1} -variable dursel_min -from 0 -to 1439 -showvalue 0
      grid  .dursel.all.min_val -row 0 -column 2 -sticky we

      label .dursel.all.max_lab -text "Maximum:"
      grid  .dursel.all.max_lab -row 1 -column 0 -sticky w
      entry .dursel.all.max_str -text "00:00" -width 7 -textvariable dursel_maxstr
      bind  .dursel.all.max_str <Enter> {SelectTextOnFocus %W}
      bind  .dursel.all.max_str <Return> {SelectDurationFilterEntry dursel_max $dursel_maxstr}
      bind  .dursel.all.max_str <Escape> {tkButtonInvoke .dursel.cmd.dismiss}
      grid  .dursel.all.max_str -row 1 -column 1 -sticky we
      scale .dursel.all.max_val -orient hor -length 200 -command {UpdateDurationFilter 2} -variable dursel_max -from 0 -to 1439 -showvalue 0
      grid  .dursel.all.max_val -row 1 -column 2 -sticky we
      grid  columnconfigure .dursel.all 1 -weight 1
      grid  columnconfigure .dursel.all 2 -weight 2
      pack  .dursel.all -side top -pady 5 -padx 5 -fill x -expand 1

      frame .dursel.cmd
      button .dursel.cmd.help -text "Help" -command {PopupHelp $helpIndex(Filtering) "Duration"}
      button .dursel.cmd.undo -text "Undo" -command {destroy .dursel; set dursel_min 0; set dursel_max 0; SelectDurationFilter}
      button .dursel.cmd.dismiss -text "Dismiss" -command {destroy .dursel}
      pack .dursel.cmd.help .dursel.cmd.undo .dursel.cmd.dismiss -side left -padx 10
      pack .dursel.cmd -side top -pady 5

      bind .dursel <Key-F1> {PopupHelp $helpIndex(Filtering) "Duration"}
      bind .dursel.all <Destroy> {+ set dursel_popup 0}
      focus .dursel.all.min_str

      wm resizable .dursel 1 0
      update
      wm minsize .dursel [winfo reqwidth .dursel] [winfo reqheight .dursel]

   } else {
      raise .dursel
   }
}

# callback for <Return> key in min and max duration entry widgets
proc SelectDurationFilterEntry {varname val} {
   upvar $varname timspec

   if {([scan $val "%2u:%2u%n" hour minute len] == 3) && ($len == [string length $val])} {
      set timspec [expr $hour * 60 + $minute]
      UpdateDurationFilter 0
   } elseif {([scan $val "%2u%n" minute len] == 2) && ($len == [string length $val])} {
      set timspec $minute
      UpdateDurationFilter 0
   } else {
      tk_messageBox -type ok -default ok -icon error -parent .dursel \
                    -message "Invalid input format in \"$val\"; use \"HH:MM\""
   }
}

proc UpdateDurationFilter { round {val 0} } {
   global dursel_min dursel_max

   if {$round == 1} {
      set dursel_min [expr $dursel_min - ($dursel_min % 5)]
      if {$dursel_min > $dursel_max} {
         set dursel_max $dursel_min
      }
   } elseif {$round == 2} {
      set dursel_max  [expr $dursel_max  - ($dursel_max  % 5)]
      if {$dursel_max < $dursel_min} {
         set dursel_min $dursel_max
      }
   }
   SelectDurationFilter
}


##  --------------------------------------------------------------------------
##  Sorting criterion selection popup
##
proc PopupSortCritSelection {} {
   global sortcrit_str sortcrit_class sortcrit_class_sel
   global sortcrit_popup

   if {$sortcrit_popup == 0} {
      CreateTransientPopup .sortcrit "Sorting criterion selection"
      set sortcrit_popup 1

      frame .sortcrit.fl
      listbox .sortcrit.fl.list -exportselection false -height 15 -width 10 -relief ridge \
                                -yscrollcommand {.sortcrit.fl.sb set} -selectmode extended
      bind .sortcrit.fl.list <Key-Delete> DeleteSortCritSelection
      pack .sortcrit.fl.list -side left -fill both -expand 1
      scrollbar .sortcrit.fl.sb -orient vertical -command {.sortcrit.fl.list yview} -takefocus 0
      pack .sortcrit.fl.sb -side left -fill y
      pack .sortcrit.fl -padx 5 -pady 5 -side left -fill both -expand 1

      # entry field for additions
      frame .sortcrit.all
      frame .sortcrit.all.inp -bd 2 -relief ridge
      label .sortcrit.all.inp.lab -text "0x"
      pack  .sortcrit.all.inp.lab -side left
      entry .sortcrit.all.inp.str -width 14 -textvariable sortcrit_str
      bind  .sortcrit.all.inp.str <Enter> {SelectTextOnFocus %W}
      bind  .sortcrit.all.inp.str <Return> AddSortCritSelection
      bind  .sortcrit.all.inp.str <Escape> {tkButtonInvoke .sortcrit.all.dismiss}
      pack  .sortcrit.all.inp.str -side left -fill x -expand 1
      pack  .sortcrit.all.inp -side top -pady 10 -anchor nw -fill x

      # buttons Add and Delete below entry field
      button .sortcrit.all.scadd -text "Add" -command AddSortCritSelection -width 5
      button .sortcrit.all.scdel -text "Delete" -command DeleteSortCritSelection -width 5
      pack .sortcrit.all.scadd .sortcrit.all.scdel -side top -anchor nw

      # invert & class selection array
      frame   .sortcrit.all.sccl -bd 2 -relief ridge
      label   .sortcrit.all.sccl.lab_class -text "Class:"
      grid    .sortcrit.all.sccl.lab_class -sticky w -row 0 -column 0
      tk_optionMenu .sortcrit.all.sccl.mb_class sortcrit_class 1 2 3 4 5 6 7 8
      trace variable sortcrit_class w UpdateSortCritClass
      grid    .sortcrit.all.sccl.mb_class -sticky we -row 0 -column 1
      label   .sortcrit.all.sccl.lab_inv -text "Invert class:"
      grid    .sortcrit.all.sccl.lab_inv -sticky w -row 1 -column 0
      menubutton .sortcrit.all.sccl.mb_inv -menu .sortcrit.all.sccl.mb_inv.men -text "Select" \
                                    -indicatoron 1 -direction flush -relief raised -borderwidth 2
      AddInvertMenuForClasses .sortcrit.all.sccl.mb_inv.men sortcrit
      .sortcrit.all.sccl.mb_inv.men configure -tearoff 0
      grid    .sortcrit.all.sccl.mb_inv -sticky we -row 1 -column 1
      grid    columnconfigure .sortcrit.all.sccl 0 -weight 1
      grid    columnconfigure .sortcrit.all.sccl 1 -weight 1
      pack    .sortcrit.all.sccl -side top -anchor w -pady 15 -fill x -expand 1

      # Special command button
      button .sortcrit.all.load -text "Load all used" -command LoadAllUsedSortCrits
      pack .sortcrit.all.load -side top -anchor w

      # Buttons at bottom: Help, Clear and Dismiss
      button .sortcrit.all.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Filtering) "Sorting Criteria"}
      button .sortcrit.all.clear -text "Clear" -width 5 -command ClearSortCritSelection
      button .sortcrit.all.dismiss -text "Dismiss" -width 5 -command {destroy .sortcrit}
      pack .sortcrit.all.dismiss .sortcrit.all.clear .sortcrit.all.help -side bottom -anchor w
      pack .sortcrit.all -side left -anchor n -fill both -expand 1 -padx 5 -pady 5

      bind .sortcrit <Key-F1> {PopupHelp $helpIndex(Filtering) "Sorting Criteria"}
      bind .sortcrit.all <Destroy> {+ set sortcrit_popup 0}
      focus .sortcrit.all.inp.str

      wm resizable .sortcrit 1 1
      update
      wm minsize .sortcrit [winfo reqwidth .sortcrit] [winfo reqheight .sortcrit]

      UpdateSortCritListbox
   } else {
      raise .sortcrit
   }
}

# callback for "Clear" button: Clear all sorting criterion selections (all classes)
proc ClearSortCritSelection {} {
   # clear sort-crit entry field
   .sortcrit.all.inp.str delete 0 end
   # empty sort-crit listbox
   ResetSortCrits
   # disable database filters
   C_ResetFilter sortcrits
   # redisplay PI with new filter setting
   C_RefreshPiListbox
}

# callback for "Add" button: parse the entry field and add the given index to the selection
proc AddSortCritSelection {} {
   global sortcrit_str
   global sortcrit_class sortcrit_class_sel

   # copy list into temporary array
   foreach item $sortcrit_class_sel($sortcrit_class) {
      set all($item) 1
   }
   foreach item [split $sortcrit_str ","] {
      set to -1
      switch -exact [scan $item "%x-%x" from to] {
         1 {set to $from}
         2 {}
         default {tk_messageBox -type ok -default ok -icon error -parent .sortcrit -message "Invalid entry item \"$item\"; expected format: nn\[-nn\]{,nn-...} with <nn> hexadecimal numbers (i.e. digits 0-9,a-f)"}
      }
      if {$to != -1} {
         if {$to < $from} {set swap $to; set to $from; set from $swap}
         if {$to >= 256} {
            tk_messageBox -type ok -default ok -icon error -parent .sortcrit -message "Invalid range; values must be lower than hexadecimal 100"
         } else {
            for {} {$from <= $to} {incr from} {
               set all($from) 1
            }
         }
      }
   }
   # build new list from temporary array
   set sortcrit_class_sel($sortcrit_class) [lsort -integer [array names all]]

   # select complete input string and display new list in box
   .sortcrit.all.inp.str selection range 0 end
   UpdateSortCritListbox

   # apply the new filter and redisplay the PI
   C_SelectSortCrits $sortcrit_class $sortcrit_class_sel($sortcrit_class)
   C_RefreshPiListbox
}

# callback for "Delete" button: remove the selected index from the selection
proc DeleteSortCritSelection {} {
   global sortcrit_class sortcrit_class_sel

   # copy list into temporary array
   foreach item $sortcrit_class_sel($sortcrit_class) {
      set all($item) 1
   }
   foreach index [lsort -integer -decreasing [.sortcrit.fl.list curselection]] {
      if {[scan [.sortcrit.fl.list get $index] "0x%x" sortcrit] == 1} {
         unset all($sortcrit)
      }
      .sortcrit.fl.list delete $index
   }
   # build new list from temporary array
   set sortcrit_class_sel($sortcrit_class) [lsort -integer [array names all]]

   #.sortcrit.all.inp.str delete 0 end
   UpdateSortCritListbox

   C_SelectSortCrits $sortcrit_class $sortcrit_class_sel($sortcrit_class)
   C_RefreshPiListbox
}

# callback for "Load all used" button: replace selection of current class with all possible crits
proc LoadAllUsedSortCrits {} {
   global sortcrit_class sortcrit_class_sel

   # query the database for a list of all found sorting criteria
   set all [C_GetAllUsedSortCrits]

   if {[llength $all] == 0} {
      # no crits found -> abort
      tk_messageBox -type ok -default ok -icon info -parent .sortcrit \
         -message "There are no programmes which have a sorting criterion attached in the database."
   } else {
      if {[llength $sortcrit_class_sel($sortcrit_class)] > 0} {
         # ask the user if the current list should really be discarded
         set answer [tk_messageBox -type okcancel -default ok -icon warning -parent .sortcrit \
                        -message "This will replace your current selection with the [llength $all] sorting criteria found in the database."]
         if {[string compare $answer ok] != 0} return;
      }

      # replace the current selection with the generated list
      set sortcrit_class_sel($sortcrit_class) $all

      UpdateSortCritListbox
      C_SelectSortCrits $sortcrit_class $sortcrit_class_sel($sortcrit_class)
      C_RefreshPiListbox
   }
}

# helper proc for changes of sortcrit class via radiobuttons
proc UpdateSortCritClass {n1 n2 v} {
   UpdateSortCritListbox
}

