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
#  $Id: dlg_filter.tcl,v 1.14 2004/09/25 16:23:10 tom Exp tom $
#
set progidx_first 0
set progidx_last  0
set progidx_popup 0

set timsel_popup 0
set timsel_enabled  0
set timsel_relative 0
set timsel_datemode ignore
set timsel_absstop  0
set timsel_stop [expr 23*60+59]
set timsel_date 0

set dursel_popup 0
set dursel_min 0
set dursel_max 0

set piexpire_popup 0
set piexpire_display 0

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

# called when filters are changed via "Navigate" menu or shortcut
proc TimeFilterExternalChange {} {
   global timsel_popup

   if $timsel_popup {
      Timsel_UpdateWidgetState
      Timsel_UpdateEntryText
   }
}

proc PiExpTime_ExternalChange {} {
   global piexpire_days piexpire_mins
   global piexpire_display
   global piexpire_popup

   if $piexpire_popup {
      set piexpire_days [expr int($piexpire_display / (60*60))]
      set piexpire_mins [expr $piexpire_display % (60*60)]
      UpdateExpiryFilter 0
   }
}

# helper function to get weekday name in local language
# week days are specified as numbers 0..6 with 0=Sat...6=Fri
proc GetNameForWeekday {wday fmt} {
  # get arbirary time as base: use current time
  set anytime [clock seconds]
  # shift to 0:00 to avoid problems with daylight saving gaps at 2:00
  set anytime [expr $anytime - ($anytime % (60*60*24))]
  # calculate weekday index for the arbitrary timestamp
  set anyday [expr ([C_ClockFormat $anytime {%w}] + 1) % 7]
  # substract offset to the requested weekday index from the time
  set anytime [expr $anytime + (($wday - $anyday) * (24*60*60))]
  # finally return name of the resulting timestamp
  return [C_ClockFormat $anytime $fmt]
}

#=LOAD=ProgIdxPopup
#=LOAD=PopupTimeFilterSelection
#=LOAD=PopupDurationFilterSelection
#=LOAD=PopupExpireDelaySelection
#=LOAD=PopupSortCritSelection
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Program-Index pop-up
##  - restrict listing to the <n>th to <m>th programme of each network (n <= m)
##  - index 0 refers to currently running programme, 1 to next etc.
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
      button .progidx.cmd.clear -text "Undo" -width 5 -command {set filter_progidx 0; C_SelectProgIdx; C_PiBox_Refresh; destroy .progidx}
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
   C_PiBox_Refresh
}

##  --------------------------------------------------------------------------
##  Time filter selection popup
##
proc PopupTimeFilterSelection {} {
   global timsel_enabled timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop timsel_datemode
   global timsel_startstr timsel_stopstr timsel_lastinput
   global timsel_popup

   if {$timsel_popup == 0} {
      CreateTransientPopup .timsel "Time filter selection"
      set timsel_popup 1

      if {![info exists timsel_lastinput]} {
         set timsel_lastinput 0
         set timsel_startstr {}
         set timsel_stopstr {}
      }

      frame .timsel.all
      checkbutton .timsel.all.relstart -text "Current time as minimum start time" -command UpdateTimeFilterRelStart -variable timsel_relative
      pack  .timsel.all.relstart -side top -anchor w

      frame .timsel.all.start -bd 2 -relief ridge
      label .timsel.all.start.lab -text "Min. start time:" -width 16 -anchor w
      entry .timsel.all.start.str -width 7 -textvariable timsel_startstr
      trace variable timsel_startstr w Timsel_TraceStartStr
      bind  .timsel.all.start.str <Enter> {SelectTextOnFocus %W}
      bind  .timsel.all.start.str <Return> {UpdateTimeFilterEntry start; UpdateTimeFilter 0}
      bind  .timsel.all.start.str <Escape> {tkButtonInvoke .timsel.cmd.dismiss}
      scale .timsel.all.start.val -orient hor -length 200 -command {UpdateTimeFilterEntry scale; UpdateTimeFilter 1} -variable timsel_start -from 0 -to 1439 -showvalue 0
      pack  .timsel.all.start.lab .timsel.all.start.str -side left
      pack  .timsel.all.start.val -side left -fill x -expand 1
      pack  .timsel.all.start -side top -pady 10 -fill x -expand 1

      checkbutton .timsel.all.absstop -text "End of day as maximum start time" -command UpdateTimeFilterRelStart -variable timsel_absstop
      pack  .timsel.all.absstop -side top -anchor w

      frame .timsel.all.stop -bd 2 -relief ridge
      label .timsel.all.stop.lab -text "Max. start time:" -width 16 -anchor w
      entry .timsel.all.stop.str -text "00:00" -width 7 -textvariable timsel_stopstr
      trace variable timsel_stopstr w Timsel_TraceStopStr
      bind  .timsel.all.stop.str <Enter> {SelectTextOnFocus %W}
      bind  .timsel.all.stop.str <Return> {UpdateTimeFilterEntry stop; UpdateTimeFilter 0}
      bind  .timsel.all.stop.str <Escape> {tkButtonInvoke .timsel.cmd.dismiss}
      scale .timsel.all.stop.val -orient hor -length 200 -command {UpdateTimeFilterEntry scale; UpdateTimeFilter 2} -variable timsel_stop -from 0 -to 1439 -showvalue 0
      pack  .timsel.all.stop.lab .timsel.all.stop.str -side left
      pack  .timsel.all.stop.val -side left -fill x -expand 1
      pack  .timsel.all.stop -side top -pady 10 -fill x -expand 1

      frame .timsel.all.datemode
      radiobutton .timsel.all.datemode.dm_ign -text "Ignore date" -command UpdateTimeFilterRelStart -variable timsel_datemode -value ignore
      radiobutton .timsel.all.datemode.dm_rel -text "Relative date" -command UpdateTimeFilterRelStart -variable timsel_datemode -value rel
      radiobutton .timsel.all.datemode.dm_wday -text "Weekday" -command UpdateTimeFilterRelStart -variable timsel_datemode -value wday
      radiobutton .timsel.all.datemode.dm_mday -text "Day of month" -command UpdateTimeFilterRelStart -variable timsel_datemode -value mday
      pack  .timsel.all.datemode.dm_ign .timsel.all.datemode.dm_rel \
            .timsel.all.datemode.dm_wday .timsel.all.datemode.dm_mday -side left
      pack  .timsel.all.datemode -side top -anchor w

      frame .timsel.all.date -bd 2 -relief ridge
      label .timsel.all.date.lab -text "Relative date:" -width 16 -anchor w
      label .timsel.all.date.str -width 7
      scale .timsel.all.date.val -orient hor -length 200 -command {UpdateTimeFilterEntry scale; UpdateTimeFilter 0} -variable timsel_date -from 0 -to 13 -showvalue 0
      pack  .timsel.all.date.lab .timsel.all.date.str -side left
      pack  .timsel.all.date.val -side left -fill x -expand 1
      pack  .timsel.all.date -side top -pady 10 -fill x -expand 1
      pack  .timsel.all -padx 10 -pady 10 -side top -fill x -expand 1

      frame .timsel.cmd
      button .timsel.cmd.help -text "Help" -command {PopupHelp $helpIndex(Filtering) "Start time"}
      button .timsel.cmd.undo -text "Undo" -command {Timsel_Quit undo}
      button .timsel.cmd.dismiss -text "Dismiss" -command {Timsel_Quit dismiss}
      pack .timsel.cmd.help .timsel.cmd.undo .timsel.cmd.dismiss -side left -padx 10
      pack .timsel.cmd -side top -pady 5

      wm protocol .timsel WM_DELETE_WINDOW {Timsel_Quit dismiss}
      bind .timsel <Key-F1> {PopupHelp $helpIndex(Filtering) "Start time"}
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
   # this variable decides if input is taken from the scales or text fields
   set timsel_lastinput 0
}

# callback for variable trace on timsel_startstr
proc Timsel_TraceStartStr {n1 n2 v} {
   global timsel_start timsel_startstr
   global timsel_relative
   global timsel_lastinput

   if {($timsel_relative == 0) && ([string compare $timsel_startstr [Motd2HHMM $timsel_start]] != 0)} {
      set timsel_lastinput [expr $timsel_lastinput | 1]
   }
}

# callback for variable trace on timsel_stopstr
proc Timsel_TraceStopStr {n1 n2 v} {
   global timsel_stop timsel_stopstr
   global timsel_absstop
   global timsel_lastinput

   if {($timsel_absstop == 0) && ([string compare $timsel_stopstr [Motd2HHMM $timsel_stop]] != 0)} {
      set timsel_lastinput [expr $timsel_lastinput | 2]
   }
}

proc Timsel_UpdateEntryText {} {
   global timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop timsel_datemode
   global timsel_startstr timsel_stopstr

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
   switch -exact $timsel_datemode {
      ignore {set vtext "any day"}
      rel    {set vtext [format "+%d days" $timsel_date]}
      wday   {set vtext [GetNameForWeekday $timsel_date {%A}]}
      mday   {set vtext $timsel_date}
   }
   .timsel.all.date.str configure -text $vtext
}

proc Timsel_UpdateWidgetState {} {
   global timsel_relative timsel_absstop timsel_datemode

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
   if {[string compare $timsel_datemode ignore] == 0} {
      .timsel.all.date.str configure -state disabled
      .timsel.all.date.val configure -state disabled
   } else {
      .timsel.all.date.str configure -state normal
      .timsel.all.date.val configure -state normal
      switch -exact $timsel_datemode {
         rel    {.timsel.all.date.lab conf -text "Day offset:"
                 .timsel.all.date.val conf -from 0 -to 13}
         wday   {.timsel.all.date.lab conf -text "Weekday:"
                 .timsel.all.date.val conf -from 0 -to 6}
         mday   {.timsel.all.date.lab conf -text "Day of month:"
                 .timsel.all.date.val conf -from 1 -to 31}
      }
   }
}

# callback for change of time or date modes (check- and radiobuttons)
proc UpdateTimeFilterRelStart {} {
   Timsel_UpdateWidgetState
   UpdateTimeFilter 0
}

# Convert text input in entry fields into time value (MoD format) or report error
proc ParseTimeValue {var val errstr} {
   upvar $var timspec

   if {(([scan $val "%u:%u%n" hour minute len] == 3) && ($len == [string length $val])) || \
       (([scan $val "%1u%2u%n" hour minute len] == 3) && ($len == [string length $val])) || \
       (([scan $val "%2u%2u%n" hour minute len] == 3) && ($len == [string length $val]))} {

      if {($hour < 24) && ($minute < 60)} {
         set timspec [expr $hour * 60 + $minute]

      } else {
         tk_messageBox -type ok -default ok -icon error -parent .timsel \
                       -message "Invalid value for $errstr time: [format %02d:%02d $hour $minute] (hour must be in range 0-23; minute in 0-59)"
      }
   } else {
      tk_messageBox -type ok -default ok -icon error -parent .timsel \
                    -message "Invalid format in $errstr time: \"$val\"; use \"HH:MM\" (HH is hour 0-23; MM is minute 0-59)"
   }
}

# callback for "Return" key in text entry field
proc UpdateTimeFilterEntry {which_first} {
   global timsel_start timsel_startstr
   global timsel_stop timsel_stopstr
   global timsel_relative timsel_absstop timsel_datemode
   global timsel_lastinput

   if {[string compare $which_first "start"] == 0} {
      # Return was pressed while focus inside start time entry field
      ParseTimeValue timsel_start $timsel_startstr "start"
      ParseTimeValue timsel_stop $timsel_stopstr "stop"
   } elseif {[string compare $which_first "stop"] == 0} {
      # Return was pressed while focus inside stop entry field
      ParseTimeValue timsel_stop $timsel_stopstr "stop"
      ParseTimeValue timsel_start $timsel_startstr "start"
   } else {
      if {(($timsel_lastinput & 1) != 0) && ($timsel_relative == 0)} {
         # text was previously changed (without pressing Return)
         ParseTimeValue timsel_start $timsel_startstr "start"
      }
      if {(($timsel_lastinput & 2) != 0) && ($timsel_absstop == 0)} {
         ParseTimeValue timsel_stop $timsel_stopstr "stop"
      }
   }
   set timsel_lastinput 0
}

# callback for Rturn in entry fields or scale movements
proc UpdateTimeFilter { round {val 0} } {
   global timsel_start timsel_stop
   global timsel_enabled

   set timsel_enabled 1
   if {$round == 1} {
      if {$timsel_start != 23*60+59} {
         set timsel_start [expr $timsel_start - ($timsel_start % 5)]
      }
   } elseif {$round == 2} {
      if {$timsel_stop != 23*60+59} {
         set timsel_stop [expr $timsel_stop  - ($timsel_stop  % 5)]
      }
   }
   Timsel_UpdateEntryText

   set timsel_lastinput 0
   SelectTimeFilter
}

# callback for "undo" button
proc Timsel_Quit {mode} {
   global timsel_stopstr timsel_startstr
   global timsel_enabled

   destroy .timsel

   if {[string compare $mode undo] == 0} {
      # undo button -> reset the filter
      set timsel_enabled 0
      SelectTimeFilter
   }

   trace vdelete timsel_stopstr w Timsel_TraceStopStr
   trace vdelete timsel_startstr w Timsel_TraceStartStr
}

##  --------------------------------------------------------------------------
##  Duration filter selection popup
##
proc PopupDurationFilterSelection {} {
   global dursel_min dursel_max
   global dursel_minstr dursel_maxstr dursel_lastinput
   global dursel_popup

   if {$dursel_popup == 0} {
      CreateTransientPopup .dursel "Duration filter selection"
      set dursel_popup 1

      set dursel_minstr [Motd2HHMM $dursel_min]
      set dursel_maxstr [Motd2HHMM $dursel_max]
      set dursel_lastinput 0

      frame .dursel.all
      label .dursel.all.min_lab -text "Minimum:"
      grid  .dursel.all.min_lab -row 0 -column 0 -sticky w
      entry .dursel.all.min_str -text "00:00" -width 7 -textvariable dursel_minstr
      trace variable dursel_minstr w DurSel_TraceMinStr
      bind  .dursel.all.min_str <Enter> {SelectTextOnFocus %W}
      bind  .dursel.all.min_str <Return> {UpdateDurationFromText min; UpdateDurationFilter 0}
      bind  .dursel.all.min_str <Escape> {tkButtonInvoke .dursel.cmd.dismiss}
      grid  .dursel.all.min_str -row 0 -column 1 -sticky we
      scale .dursel.all.min_val -orient hor -length 300 -command {UpdateDurationFromText min-scale; UpdateDurationFilter 1} \
                                -variable dursel_min -from 0 -to 1439 -showvalue 0
      grid  .dursel.all.min_val -row 0 -column 2 -sticky we

      label .dursel.all.max_lab -text "Maximum:"
      grid  .dursel.all.max_lab -row 1 -column 0 -sticky w
      entry .dursel.all.max_str -text "00:00" -width 7 -textvariable dursel_maxstr
      trace variable dursel_maxstr w DurSel_TraceMaxStr
      bind  .dursel.all.max_str <Enter> {SelectTextOnFocus %W}
      bind  .dursel.all.max_str <Return> {UpdateDurationFromText max; UpdateDurationFilter 0}
      bind  .dursel.all.max_str <Escape> {tkButtonInvoke .dursel.cmd.dismiss}
      grid  .dursel.all.max_str -row 1 -column 1 -sticky we
      scale .dursel.all.max_val -orient hor -length 200 -command {UpdateDurationFromText max-scale; UpdateDurationFilter 2} \
                                -variable dursel_max -from 0 -to 1439 -showvalue 0
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

# trace changes in the minimum value entry field
# - invoked when the user edits the text OR if anyone assigns a value to it
proc DurSel_TraceMinStr {n1 n1 v} {
   global dursel_min dursel_minstr
   global dursel_lastinput

   if {[string compare $dursel_minstr [Motd2HHMM $dursel_min]] != 0} {
      set dursel_lastinput [expr $dursel_lastinput | 1]
   }
}

proc DurSel_TraceMaxStr {n1 n1 v} {
   global dursel_max dursel_maxstr
   global dursel_lastinput

   if {[string compare $dursel_maxstr [Motd2HHMM $dursel_max]] != 0} {
      set dursel_lastinput [expr $dursel_lastinput | 2]
   }
}

# callback for <Return> key in min and max duration entry widgets
proc ParseDurationValue {var newval errstr wparent} {
   upvar $var timspec

   if {([scan $newval "%2u:%2u%n" hour minute len] == 3) && ($len == [string length $newval]) && \
       ($hour < 24) && ($minute < 60)} {
      set timspec [expr $hour * 60 + $minute]
      set result 1
   } elseif {([scan $newval "%2d%n" minute len] == 2) && ($len == [string length $newval]) && \
             ($minute >= 0)} {
      set timspec $minute
      set result 1
   } else {
      tk_messageBox -type ok -default ok -icon error -parent $wparent \
                    -message "Invalid format in $errstr = \"$newval\"; use \"HH:MM\" or \"MM\" (HH is hour 0-23; MM is minute 0-59)"
      set result 0
   }
   return $result
}

proc UpdateDurationFromText {which_first} {
   global dursel_minstr dursel_maxstr dursel_lastinput
   global dursel_min dursel_max

   if {[string compare $which_first "min"] == 0} {
      # Return was pressed while focus inside start time entry field
      ParseDurationValue dursel_min $dursel_minstr "minimum duration" .dursel
      ParseDurationValue dursel_max $dursel_maxstr "maximum duration" .dursel
   } elseif {[string compare $which_first "max"] == 0} {
      # Return was pressed while focus inside stop time entry field
      ParseDurationValue dursel_max $dursel_maxstr "maximum duration" .dursel
      ParseDurationValue dursel_min $dursel_minstr "minimum duration" .dursel
   } else {
      # if text was previously changed (without pressing Return), apply the changes now
      if {(($dursel_lastinput & 1) != 0) && ([string compare $which_first "min-scale"] != 0)} {
         ParseDurationValue dursel_min $dursel_minstr "minimum duration" .dursel
      }
      if {(($dursel_lastinput & 2) != 0) && ([string compare $which_first "max-scale"] != 0)} {
         ParseDurationValue dursel_max $dursel_maxstr "maximum duration" .dursel
      }
   }
   set dursel_lastinput 0
}

proc UpdateDurationFilter { round {val 0} } {
   global dursel_minstr dursel_maxstr dursel_lastinput
   global dursel_min dursel_max

   if {$round == 1} {
      if {$dursel_min != 1439} {
         set dursel_min [expr $dursel_min - ($dursel_min % 5)]
      }
      if {$dursel_min > $dursel_max} {
         set dursel_max $dursel_min
      }
   } elseif {$round == 2} {
      if {$dursel_max != 1439} {
         set dursel_max  [expr $dursel_max  - ($dursel_max  % 5)]
      }
      if {$dursel_max < $dursel_min} {
         set dursel_min $dursel_max
      }
   }

   set dursel_minstr [Motd2HHMM $dursel_min]
   set dursel_maxstr [Motd2HHMM $dursel_max]
   set dursel_lastinput 0

   SelectDurationFilter
}


##  --------------------------------------------------------------------------
##  Expired PI display selection popup
##
proc PopupExpireDelaySelection {} {
   global piexpire_display piexpire_days piexpire_mins
   global piexpire_daystr piexpire_minstr piexpire_lastinput
   global piexpire_cutoff
   global piexpire_cut_daystr piexpire_cut_hourstr
   global piexpire_popup

   if {$piexpire_popup == 0} {
      CreateTransientPopup .piexpire "Expired programmes display selection"
      wm geometry  .piexpire "=400x200"
      wm minsize   .piexpire 400 200
      wm resizable .piexpire 1 1
      set piexpire_popup 1

      # load special widget libraries
      rnotebook_load

      Rnotebook:create .piexpire.nb -tabs {"Filter" "Configuration"} -borderwidth 2
      pack .piexpire.nb -side left -pady 10 -padx 5 -fill both -expand 1
      set frm1 [Rnotebook:frame .piexpire.nb 1]
      set frm2 [Rnotebook:frame .piexpire.nb 2]

      ##
      ##  Tab 1: Expire time filter
      ##
      set piexpire_daystr [expr $piexpire_display / (24*60)]
      set piexpire_minstr [Motd2HHMM [expr $piexpire_display % (24*60)]]
      set piexpire_lastinput 0

      frame  ${frm1}.time_frm
      label  ${frm1}.time_frm.lab -text "Filter programmes ending before:"
      pack   ${frm1}.time_frm.lab -side left
      label  ${frm1}.time_frm.str -text "" -font $::font_normal
      pack   ${frm1}.time_frm.str -side left -padx 5
      grid   ${frm1}.time_frm -row 0 -column 0 -columnspan 3 -pady 10 -sticky w

      label  ${frm1}.days_lab -text "Days:"
      grid   ${frm1}.days_lab -row 1 -column 0 -sticky w
      entry  ${frm1}.days_str -text "00:00" -width 7 -textvariable piexpire_daystr
      trace  variable piexpire_daystr w PiExpTime_TraceDaysStr
      bind   ${frm1}.days_str <Enter> {SelectTextOnFocus %W}
      bind   ${frm1}.days_str <Return> {UpdateExpiryFromText bytext; UpdateExpiryFilter 0}
      bind   ${frm1}.days_str <Escape> {tkButtonInvoke .piexpire.cmd.dismiss}
      grid   ${frm1}.days_str -row 1 -column 1 -sticky we
      scale  ${frm1}.days_val -orient hor -length 300 -command {UpdateExpiryFromText days; UpdateExpiryFilter 1} \
                                   -variable piexpire_days -from 0 -to 7 -showvalue 0
      grid   ${frm1}.days_val -row 1 -column 2 -sticky we

      label  ${frm1}.hour_lab -text "Hours:"
      grid   ${frm1}.hour_lab -row 2 -column 0 -sticky w
      entry  ${frm1}.hour_str -text "00:00" -width 7 -textvariable piexpire_minstr
      trace  variable piexpire_minstr w PiExpTime_TraceHourStr
      bind   ${frm1}.hour_str <Enter> {SelectTextOnFocus %W}
      bind   ${frm1}.hour_str <Return> {UpdateExpiryFromText bytext; UpdateExpiryFilter 0}
      bind   ${frm1}.hour_str <Escape> {tkButtonInvoke .piexpire.cmd.dismiss}
      grid   ${frm1}.hour_str -row 2 -column 1 -sticky we
      scale  ${frm1}.max_val -orient hor -length 200 -command {UpdateExpiryFromText hours; UpdateExpiryFilter 2} \
                                  -variable piexpire_mins -from 0 -to 1439 -showvalue 0
      grid   ${frm1}.max_val -row 2 -column 2 -sticky we
      grid   columnconfigure ${frm1} 1 -weight 1
      grid   columnconfigure ${frm1} 2 -weight 2

      ##
      ##  Tab 2: Cut-off time configuration
      ##
      set piexpire_cut_daystr [expr $piexpire_cutoff / (24*60)]
      set piexpire_cut_hourstr [expr ($piexpire_cutoff % (24*60)) / 60]

      label  ${frm2}.head_lab -text "Delay before removing programmes from database:"
      grid   ${frm2}.head_lab -row 0 -column 0 -columnspan 3 -pady 10 -sticky w

      label  ${frm2}.days_lab -text "Days:"
      grid   ${frm2}.days_lab -row 1 -column 0 -sticky w
      entry  ${frm2}.days_str -text "00:00" -width 7 -textvariable piexpire_cut_daystr
      bind   ${frm2}.days_str <Enter> {SelectTextOnFocus %W}
      bind   ${frm2}.days_str <Escape> {tkButtonInvoke .piexpire.cmd.dismiss}
      grid   ${frm2}.days_str -row 1 -column 1 -sticky we

      label  ${frm2}.hour_lab -text "Hours:"
      grid   ${frm2}.hour_lab -row 2 -column 0 -sticky w
      entry  ${frm2}.hour_str -text "00:00" -width 7 -textvariable piexpire_cut_hourstr
      bind   ${frm2}.hour_str <Enter> {SelectTextOnFocus %W}
      bind   ${frm2}.hour_str <Escape> {tkButtonInvoke .piexpire.cmd.dismiss}
      grid   ${frm2}.hour_str -row 2 -column 1 -sticky we

      button ${frm2}.upd_but -text "Update" -command PiExpTime_UpdateCutOff
      grid   ${frm2}.upd_but -row 1 -rowspan 2 -column 2 -padx 20 -sticky w
      grid   columnconfigure ${frm2} 2 -weight 1
      pack   .piexpire.nb -side top

      ##
      ##  Command row
      ##
      frame  .piexpire.cmd
      button .piexpire.cmd.help -text "Help" -command {PopupHelp $helpIndex(Filtering) "Expired Programmes Display"}
      button .piexpire.cmd.undo -text "Undo" -command {destroy .piexpire; set piexpire_display 0; SelectExpireDelayFilter}
      button .piexpire.cmd.dismiss -text "Dismiss" -command {destroy .piexpire}
      pack   .piexpire.cmd.help .piexpire.cmd.undo .piexpire.cmd.dismiss -side left -padx 10
      pack   .piexpire.cmd -side top -pady 5

      bind   .piexpire <Key-F1> {PopupHelp $helpIndex(Filtering) "Expired Programmes Display"}
      bind   .piexpire.cmd <Destroy> {+ set piexpire_popup 0}
      focus  ${frm1}.days_str

   } else {
      raise .piexpire
   }
}

# trace changes in the minimum value entry field
# - invoked when the user edits the text OR if anyone assigns a value to it
proc PiExpTime_TraceDaysStr {n1 n1 v} {
   global piexpire_days piexpire_daystr
   global piexpire_lastinput

   if {[string compare $piexpire_daystr $piexpire_days] != 0} {
      set piexpire_lastinput [expr $piexpire_lastinput | 1]
   }
}

proc PiExpTime_TraceHourStr {n1 n1 v} {
   global piexpire_mins piexpire_minstr
   global piexpire_lastinput

   if {[string compare $piexpire_minstr [Motd2HHMM $piexpire_mins]] != 0} {
      set piexpire_lastinput [expr $piexpire_lastinput | 2]
   }
}

# callback for <Return> key in expire time hours entry widget
proc ParseExpireDaysValue {var newval errstr} {
   upvar $var timspec

   if {([scan $newval "%d%n" days len] == 2) && ($len == [string length $newval]) && ($days >= 0)} {
      set timspec $days
      set result 1
   } else {
      tk_messageBox -type ok -default ok -icon error -parent .piexpire \
                    -message "Invalid value for $errstr \"$newval\": must be positive numeric."
      set result 0
   }
   return $result
}

proc UpdateExpiryFromText {which_first} {
   global piexpire_daystr piexpire_minstr piexpire_lastinput
   global piexpire_days piexpire_mins

   if {[string compare $which_first "bytext"] == 0} {
      # Return was pressed while focus inside entry fields
      ParseExpireDaysValue piexpire_days $piexpire_daystr "days"
      ParseDurationValue piexpire_mins $piexpire_minstr "hour delta" .piexpire
   } else {
      # if text was previously changed (without pressing Return), apply the changes now
      if {(($piexpire_lastinput & 1) != 0) && ([string compare $which_first "day-scale"] != 0)} {
         ParseExpireDaysValue piexpire_days $piexpire_daystr "day count"
      }
      if {(($piexpire_lastinput & 2) != 0) && ([string compare $which_first "hour-scale"] != 0)} {
         ParseDurationValue piexpire_mins $piexpire_minstr "hour delta" .piexpire
      }
   }
   set piexpire_lastinput 0
}

proc UpdateExpiryFilter { round {val 0} } {
   global piexpire_daystr piexpire_minstr piexpire_lastinput
   global piexpire_display piexpire_days piexpire_mins

   if {$round == 2} {
      if {$piexpire_mins != 1439} {
         set piexpire_mins  [expr $piexpire_mins  - ($piexpire_mins  % 5)]
      }
   }

   set piexpire_display [expr ($piexpire_days * 24*60) + $piexpire_mins]
   set piexpire_daystr [expr $piexpire_display / (24*60)]
   set piexpire_minstr [Motd2HHMM [expr $piexpire_display % (24*60)]]
   set piexpire_lastinput 0

   set frm1 [Rnotebook:frame .piexpire.nb 1]
   set threshold [expr [clock seconds] - ($piexpire_display * 60)]
   ${frm1}.time_frm.str configure -text [C_ClockFormat $threshold {%A %d.%m. %H:%M}]

   SelectExpireDelayFilter
}

proc PiExpTime_UpdateCutOff {} {
   global piexpire_cut_daystr piexpire_cut_hourstr
   global piexpire_cutoff

   set ok [ParseExpireDaysValue days $piexpire_cut_daystr "days"]
   if $ok {
      set ok [ParseExpireDaysValue hours $piexpire_cut_hourstr "hours"]
      if $ok {

         set new_cutoff [expr ($days * 24*60) + ($hours * 60)]
         set pi_count [C_CountExpiredPi $new_cutoff]

         if {[C_IsNetAcqActive default]} {
            # warn that params do not affect acquisition running remotely
            set answer [tk_messageBox -type okcancel -icon info -parent .piexpire \
                           -message "Please note this setting should be applied to the acquisition daemon too."]
            if {[string compare $answer "ok"] != 0} {
               return
            }
         } elseif {$pi_count > 0} {
            # warn that PI will be removed
            set answer [tk_messageBox -type okcancel -icon warning -parent .piexpire \
                           -message "You're about to irrecoverably remove $pi_count programmes from the current database."]
            if {[string compare $answer "ok"] != 0} {
               return
            }
         }

         set piexpire_cutoff $new_cutoff
         C_UpdatePiExpireDelay
         UpdateRcFile

         # update display (including format corrections)
         set piexpire_cut_daystr [expr $piexpire_cutoff / (24*60)]
         set piexpire_cut_hourstr [expr ($piexpire_cutoff % (24*60)) / 60]
      }
   }
}


##  --------------------------------------------------------------------------
##  Sorting criterion selection popup
##
proc PopupSortCritSelection {} {
   global win_frm_fg
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
      .sortcrit.all.sccl.mb_class configure -takefocus 1 -highlightthickness 1 -highlightcolor $win_frm_fg
      trace variable sortcrit_class w UpdateSortCritClass
      grid    .sortcrit.all.sccl.mb_class -sticky we -row 0 -column 1
      label   .sortcrit.all.sccl.lab_inv -text "Invert class:"
      grid    .sortcrit.all.sccl.lab_inv -sticky w -row 1 -column 0
      menubutton .sortcrit.all.sccl.mb_inv -menu .sortcrit.all.sccl.mb_inv.men -text "Select" \
                                    -indicatoron 1 -direction flush -relief raised -borderwidth 2 \
                                    -takefocus 1 -highlightthickness 1 -highlightcolor $win_frm_fg
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
      bind .sortcrit <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]
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
   C_PiBox_Refresh
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
   C_PiBox_Refresh
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
   C_PiBox_Refresh
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
      C_PiBox_Refresh
   }
}

# helper proc for changes of sortcrit class via radiobuttons
proc UpdateSortCritClass {n1 n2 v} {
   UpdateSortCritListbox
}

