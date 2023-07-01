#
#  Filter dialogs
#
#  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
set piexpire_never 0

set themesel_popup 0


# called when filters are changed via shortcut
proc TimeFilterExternalChange {} {
   global timsel_popup

   if $timsel_popup {
      Timsel_UpdateWidgetState
      Timsel_UpdateEntryText
   }
}

proc PiExpTime_ExternalChange {} {
   global piexpire_slider piexpire_days piexpire_mins
   global piexpire_display
   global piexpire_popup

   if {$piexpire_popup} {
      set piexpire_slider $piexpire_display
      set piexpire_days [expr {int($piexpire_display / (24*60))}]
      set piexpire_mins [expr {$piexpire_display % (24*60)}]
      UpdateExpiryFilter 0
   }
}

# helper function to get weekday name in local language
# week days are specified as numbers 0..6 with 0=Sat...6=Fri
proc GetNameForWeekday {wday fmt} {
  # get arbirary time as base: use current time
  set anytime [C_ClockSeconds]
  # shift to 0:00 to avoid problems with daylight saving gaps at 2:00
  set anytime [expr $anytime - ($anytime % (60*60*24))]
  # calculate weekday index for the arbitrary timestamp
  set anyday [expr ([C_ClockFormat $anytime {%w}] + 1) % 7]
  # substract offset to the requested weekday index from the time
  set anytime [expr $anytime + (($wday - $anyday) * (24*60*60))]
  # finally return name of the resulting timestamp
  return [C_ClockFormat $anytime $fmt]
}

proc UpdateThemeCategories {} {
   global themesel_popup

   if {$themesel_popup} {
      ThemeSelectionRefill
   }
}

#=LOAD=ProgIdxPopup
#=LOAD=PopupTimeFilterSelection
#=LOAD=PopupDurationFilterSelection
#=LOAD=PopupExpireDelaySelection
#=LOAD=PopupThemesSelection
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
      button .progidx.cmd.clear -text "Abort" -width 5 -command {set filter_progidx 0; C_SelectProgIdx; C_PiBox_Refresh; destroy .progidx}
      button .progidx.cmd.dismiss -text "Ok" -width 5 -command {destroy .progidx} -default active
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

      frame .timsel.all.start -borderwidth 2 -relief groove
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

      frame .timsel.all.stop -borderwidth 2 -relief groove
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

      frame .timsel.all.date -borderwidth 2 -relief groove
      label .timsel.all.date.lab -text "Relative date:" -width 16 -anchor w
      label .timsel.all.date.str -width 7
      scale .timsel.all.date.val -orient hor -length 200 -command {UpdateTimeFilterEntry scale; UpdateTimeFilter 0} -variable timsel_date -from 0 -to 13 -showvalue 0
      pack  .timsel.all.date.lab .timsel.all.date.str -side left
      pack  .timsel.all.date.val -side left -fill x -expand 1
      pack  .timsel.all.date -side top -pady 10 -fill x -expand 1
      pack  .timsel.all -padx 10 -pady 10 -side top -fill x -expand 1

      frame .timsel.cmd
      button .timsel.cmd.help -text "Help" -command {PopupHelp $helpIndex(Filtering) "Start time"}
      button .timsel.cmd.undo -text "Abort" -command {Timsel_Quit undo}
      button .timsel.cmd.dismiss -text "Ok" -command {Timsel_Quit dismiss}
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
      button .dursel.cmd.undo -text "Abort" -command {destroy .dursel; set dursel_min 0; set dursel_max 0; SelectDurationFilter}
      button .dursel.cmd.dismiss -text "Ok" -command {destroy .dursel}
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
   global piexpire_slider piexpire_daystr piexpire_minstr piexpire_lastinput
   global piexpire_cut_daystr piexpire_cut_hourstr
   global piexpire_popup

   if {$piexpire_popup == 0} {
      CreateTransientPopup .piexpire "Expired programmes display selection"
      set piexpire_popup 1

      set piexpire_slider $piexpire_display
      set piexpire_days [expr {int($piexpire_display / (24*60))}]
      set piexpire_mins [expr {$piexpire_display % (24*60)}]
      set piexpire_daystr $piexpire_days
      set piexpire_minstr [Motd2HHMM $piexpire_mins]
      set piexpire_lastinput 0

      label  .piexpire.descr -text "This dialog allows enabling display of programmes ending the given\nnumber of days and hours in the past:" \
                             -justify left
      pack   .piexpire.descr -side top -pady 5 -padx 5 -anchor w

      set max_slider [expr 14 * 24*60]
      scale  .piexpire.slider -orient hor -length 300 \
                              -command {UpdateExpiryFromSlider; UpdateExpiryFilter 0} \
                              -variable piexpire_slider -from 0 -to $max_slider -showvalue 0
      pack   .piexpire.slider -side top -pady 5 -padx 5 -fill x -expand 1

      frame  .piexpire.nb
      label  .piexpire.nb.days_lab -text "Relative cut-off time: Days:"
      pack   .piexpire.nb.days_lab -side left
      entry  .piexpire.nb.days_str -text "00:00" -width 7 -textvariable piexpire_daystr
      trace  variable piexpire_daystr w PiExpTime_TraceDaysStr
      bind   .piexpire.nb.days_str <Enter> {SelectTextOnFocus %W}
      bind   .piexpire.nb.days_str <Return> {UpdateExpiryFromText bytext; UpdateExpiryFilter 0}
      bind   .piexpire.nb.days_str <Escape> {tkButtonInvoke .piexpire.cmd.dismiss}
      pack   .piexpire.nb.days_str -side left

      label  .piexpire.nb.hour_lab -text "Hours:"
      pack   .piexpire.nb.hour_lab -side left
      entry  .piexpire.nb.hour_str -text "00:00" -width 7 -textvariable piexpire_minstr
      trace  variable piexpire_minstr w PiExpTime_TraceHourStr
      bind   .piexpire.nb.hour_str <Enter> {SelectTextOnFocus %W}
      bind   .piexpire.nb.hour_str <Return> {UpdateExpiryFromText bytext; UpdateExpiryFilter 0}
      bind   .piexpire.nb.hour_str <Escape> {tkButtonInvoke .piexpire.cmd.dismiss}
      pack   .piexpire.nb.hour_str -side left
      pack   .piexpire.nb -side top -pady 5 -padx 5 -fill x -expand 1

      checkbutton .piexpire.chkb -text "Show all expired programmes" -command {UpdateExpiryFilter 0} -variable piexpire_never
      pack  .piexpire.chkb -side top -anchor w

      frame  .piexpire.time_frm
      label  .piexpire.time_frm.lab -text "Effective cut-off time:"
      pack   .piexpire.time_frm.lab -side left
      label  .piexpire.time_frm.str -text "" -font $::font_normal
      pack   .piexpire.time_frm.str -side left -padx 5
      pack   .piexpire.time_frm -side top -pady 5 -padx 5 -anchor w

      # initial value for cut-off time display
      UpdateExpiryFilter 0

      ##
      ##  Command row
      ##
      frame  .piexpire.cmd
      button .piexpire.cmd.help -text "Help" -command {PopupHelp $helpIndex(Filtering) "Expired Programmes Display"}
      button .piexpire.cmd.undo -text "Abort" -command {destroy .piexpire; ResetExpireDelay; SelectExpireDelayFilter}
      button .piexpire.cmd.dismiss -text "Ok" -command {destroy .piexpire}
      pack   .piexpire.cmd.help .piexpire.cmd.undo .piexpire.cmd.dismiss -side left -padx 10
      pack   .piexpire.cmd -side top -pady 5

      bind   .piexpire <Key-F1> {PopupHelp $helpIndex(Filtering) "Expired Programmes Display"}
      bind   .piexpire.cmd <Destroy> {+ set piexpire_popup 0}
      focus  .piexpire.nb.days_str

      wm resizable .piexpire 1 0
      update
      wm minsize .piexpire [winfo reqwidth .piexpire] [winfo reqheight .piexpire]

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
   global piexpire_slider piexpire_days piexpire_mins

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
   set piexpire_slider [expr {$piexpire_days * 24*60 + $piexpire_mins}]
   set piexpire_lastinput 0
}

proc UpdateExpiryFromSlider {} {
   global piexpire_daystr piexpire_minstr piexpire_lastinput
   global piexpire_slider piexpire_days piexpire_mins

   set piexpire_days [expr {int($piexpire_slider / (24*60))}]
   set piexpire_mins [expr {$piexpire_slider % (24*60)}]
   set piexpire_lastinput 0
}

proc UpdateExpiryFilter { round {val 0} } {
   global piexpire_daystr piexpire_minstr piexpire_lastinput
   global piexpire_never piexpire_display piexpire_days piexpire_mins

   if {$round == 2} {
      if {$piexpire_mins != 1439} {
         set piexpire_mins  [expr $piexpire_mins  - ($piexpire_mins  % 5)]
      }
   }

   set piexpire_display [expr ($piexpire_days * 24*60) + $piexpire_mins]
   set piexpire_daystr [expr $piexpire_display / (24*60)]
   set piexpire_minstr [Motd2HHMM [expr $piexpire_display % (24*60)]]
   set piexpire_lastinput 0

   if {$piexpire_never} {
      .piexpire.time_frm.str configure -text "---"
   } else {
      set threshold [expr [C_ClockSeconds] - ($piexpire_display * 60)]
      .piexpire.time_frm.str configure -text [C_ClockFormat $threshold {%A %d.%m. %H:%M}]
   }

   SelectExpireDelayFilter
}

##  --------------------------------------------------------------------------
##  Themes filter selection popup
##
proc PopupThemesSelection {} {
   global menuStatusThemeClass
   global themesel_list themesel_class
   global themesel_popup

   if {$themesel_popup == 0} {
      CreateTransientPopup .themesel "Theme category filter selection"
      set themesel_popup 1

      ThemeSelectionInitList
      set themesel_class $menuStatusThemeClass

      # determine listbox height
      set lbox_height [llength $themesel_list]
      if {$lbox_height > 25} {
         set lbox_height 25
      }
      # determine listbox width
      set lbox_width 25
      foreach theme $themesel_list {
         set l [string length $theme]
         if {$l > $lbox_width} { set lbox_width $l }
      }
      if {$lbox_width > 50} { set lbox_width 50 }

      ## first row: listbox with all themes
      frame .themesel.lbox
      listbox .themesel.lbox.themes -height $lbox_height -width $lbox_width -selectmode extended \
                                    -yscrollcommand [list .themesel.lbox.sbv set] \
                                    -xscrollcommand [list .themesel.lbox.sbh set]
      grid .themesel.lbox.themes -row 0 -column 0 -sticky news
      scrollbar .themesel.lbox.sbv -orient vertical -command [list .themesel.lbox.themes yview] -takefocus 0
      grid .themesel.lbox.sbv -row 0 -column 1 -sticky ns
      scrollbar .themesel.lbox.sbh -orient horizontal -command [list .themesel.lbox.themes xview] -takefocus 0
      grid .themesel.lbox.sbh -row 1 -column 0 -sticky we
      grid columnconfigure .themesel.lbox 0 -weight 1
      grid rowconfigure .themesel.lbox 0 -weight 1
      pack .themesel.lbox -expand 1 -fill both -side top -padx 5 -pady 5

      ## second row: class selection
      frame .themesel.tclass
      label .themesel.tclass.lab -text "Editing theme class:"
      pack .themesel.tclass.lab -side left -padx 5
      spinbox .themesel.tclass.val -from 1 -to 8 -increment 1 -width 2 \
                                   -textvariable themesel_class -validate key \
                                   -validatecommand [list ThemeSelectionClassChange %P]
      pack .themesel.tclass.val -side left
      pack .themesel.tclass -fill x -side top

      ##
      ##  Command row
      ##
      frame  .themesel.cmd
      button .themesel.cmd.help -text "Help" -command {PopupHelp $helpIndex(Filtering) "Theme categories"}
      button .themesel.cmd.undo -text "Abort" -command {ThemeSelectionQuit; ResetThemes}
      button .themesel.cmd.dismiss -text "Ok" -command {ThemeSelectionQuit}
      pack   .themesel.cmd.help .themesel.cmd.undo .themesel.cmd.dismiss -side left -padx 10
      pack   .themesel.cmd -side top -pady 5

      wm protocol .themesel WM_DELETE_WINDOW {ThemeSelectionQuit}
      bind   .themesel <Key-F1> {PopupHelp $helpIndex(Filtering) "Theme categories"}
      bind   .themesel.lbox.themes <<ListboxSelect>> ThemeSelectionChange
      bind   .themesel.cmd <Destroy> {+ set themesel_popup 0}
      focus  .themesel.lbox.themes

      foreach theme $themesel_list {
         .themesel.lbox.themes insert end [lindex $theme 1]
      }
      ThemeSelectionUpdate

      global theme_sel
      trace variable theme_sel w ThemeSelectionTrace

      wm resizable .themesel 1 1
      update
      wm minsize .themesel [winfo reqwidth .themesel] [winfo reqheight .themesel]

   } else {
      raise .themesel
   }
}

proc ThemeSelectionQuit {} {
   global themesel_list
   global theme_sel

   destroy .themesel
   unset themesel_list
   trace vdelete theme_sel w ThemeSelectionTrace
}

# Returns the selected themes (as DB indices, not list indices)
proc ThemeSelectionGetThemes {} {
   global themesel_list

   set sel {}
   foreach idx [.themesel.lbox.themes curselection] {
      lappend sel [lindex $themesel_list $idx 0]
   }
   return $sel
}

# Callback for external change of the filter setting
proc ThemeSelectionTrace {name1 name2 op} {
   global themesel_popup themesel_ignore_trace
   global theme_sel

   if {$themesel_popup && ![info exists themesel_ignore_trace]} {
      if {[lsort -integer [ThemeSelectionGetThemes]] != [lsort -integer $theme_sel]} {
         ThemeSelectionUpdate
      }
   }
}

# Callback for selection change within the listbox
proc ThemeSelectionChange {} {
   global themesel_ignore_trace

   set themesel_ignore_trace 1
   SelectThemeList [ThemeSelectionGetThemes]
   unset themesel_ignore_trace
}

# Update listbox selection after external changes to the theme filter status
proc ThemeSelectionUpdate {} {
   global themesel_popup themesel_list themesel_ignore_trace
   global theme_sel

   if {$themesel_popup} {
      array set revmap {}
      set idx 0
      foreach theme $themesel_list {
         set revmap([lindex $theme 0]) $idx
         incr idx
      }

      set themesel_ignore_trace 1
      .themesel.lbox.themes selection clear 0 end

      foreach index $theme_sel {
         if {[info exists revmap($index)]} {
            .themesel.lbox.themes selection set $revmap($index)
         }
      }
      unset themesel_ignore_trace
   }
}

proc ThemeSelectionInitList {} {
   global themesel_list

   set idx 0
   set themesel_list {}
   foreach name [C_GetAllThemesStrings] {
      lappend themesel_list [list $idx $name]
      incr idx
   }
   set themesel_list [lsort -dictionary -index 1 $themesel_list]
}

# Callback for provider change: Reset and refill the listbox content
proc ThemeSelectionRefill {} {
   global themesel_list

   .themesel.lbox.themes delete 0 end
   ThemeSelectionInitList

   foreach theme $themesel_list {
      .themesel.lbox.themes insert end [lindex $theme 1]
   }
}

proc ThemeSelectionClassChange {val} {
   global menuStatusThemeClass

   if {$val == ""} {
      return 1
   }
   if {[catch {set int_val [expr {int($val)}]}] == 0} {
      if {($int_val >= 1) && ($int_val <= 8)} {
         if {$int_val != $menuStatusThemeClass} {
            set menuStatusThemeClass $int_val
            SelectThemeClass
         }
         return 1
      }
   }
   return 0
}
