#
#  Nextview GUI in Tcl/Tk
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
#    Implements the core Tcl/Tk functionality for all windows.
#    This file is loaded upon start-up of the main program.
#    Additional Tcl code is dynamically created by the C code
#    in the epgui directory.
#
#  Author: Tom Zoerner <Tom.Zoerner@informatik.uni-erlangen.de>
#
#  $Id: epgui.tcl,v 1.37.1.1 2000/11/07 20:54:56 tom Exp $
#

frame     .all -relief flat -borderwidth 0
frame     .all.shortcuts -borderwidth 2
label     .all.shortcuts.clock -borderwidth 1 -text {}
pack      .all.shortcuts.clock -fill x -pady 5
button    .all.shortcuts.reset -text "Reset" -relief ridge -command {ResetFilterState; C_ResetFilter all; C_ResetPiListbox}
pack      .all.shortcuts.reset -side top -fill x

listbox   .all.shortcuts.list -exportselection false -setgrid true -height 12 -width 12 -selectmode extended -relief ridge
bind      .all.shortcuts.list <ButtonRelease-1> {+ SelectShortcut}
bind      .all.shortcuts.list <space> {+ SelectShortcut}
pack      .all.shortcuts.list -fill x
pack      .all.shortcuts -anchor nw -side left

frame     .all.netwops
listbox   .all.netwops.list -exportselection false -setgrid true -height 27 -width 8 -selectmode extended -relief ridge
.all.netwops.list insert end alle
.all.netwops.list selection set 0
bind      .all.netwops.list <ButtonRelease-1> {+ SelectNetwop}
bind      .all.netwops.list <space> {+ SelectNetwop}
pack      .all.netwops.list -side left -anchor n
pack      .all.netwops -side left -anchor n -pady 2 -padx 2

set textfont {Helvetica -12 normal}
set default_bg [.all cget -background]

frame     .all.pi
frame     .all.pi.list
scrollbar .all.pi.list.sc -orient vertical -command {C_PiListBox_Scroll}
pack      .all.pi.list.sc -fill y -side left
text      .all.pi.list.text -height 27 -width 70 \
                            -font $textfont -exportselection false \
                            -background $default_bg \
                            -cursor top_left_arrow \
			    -insertofftime 0
bindtags  .all.pi.list.text {.all.pi.list.text . all}
bind      .all.pi.list.text <Button-1> {C_PiListBox_SelectItem [GetSelectedItem %x %y]}
bind      .all.pi.list.text <Double-Button-1> {C_PiListBox_PopupPi %x %y}
bind      .all.pi.list.text <Button-3> {C_PiListBox_SelectItem [GetSelectedItem %x %y]; CreateContextMenu %x %y}
bind      .all.pi.list.text <Up>    {C_PiListBox_CursorUp}
bind      .all.pi.list.text <Down>  {C_PiListBox_CursorDown}
bind      .all.pi.list.text <Prior> {C_PiListBox_Scroll scroll -1 pages}
bind      .all.pi.list.text <Next>  {C_PiListBox_Scroll scroll 1 pages}
bind      .all.pi.list.text <Home>  {C_PiListBox_Scroll moveto 0.0; C_PiListBox_SelectItem 0}
bind      .all.pi.list.text <End>   {C_PiListBox_Scroll moveto 1.0; C_PiListBox_Scroll scroll 1 pages}
bind      .all.pi.list.text <Enter> {focus %W}
.all.pi.list.text tag configure now -background #c9c9df
.all.pi.list.text tag lower now
pack      .all.pi.list.text -fill x -fill y -side left
pack      .all.pi.list -side top

frame     .all.pi.info
scrollbar .all.pi.info.sc -orient vertical -command {.all.pi.info.text yview}
pack      .all.pi.info.sc -fill y -anchor e -side left
text      .all.pi.info.text -height 10 -width 70 -wrap word \
                            -font $textfont \
                            -background $default_bg \
                            -yscrollcommand {.all.pi.info.sc set} \
                            -cursor circle \
			    -insertofftime 0
.all.pi.info.text tag configure title -font {helvetica -14 bold} -justify center -spacing3 3
.all.pi.info.text tag configure bold -font {helvetica -12 bold}
.all.pi.info.text tag configure features -font {helvetica -12 bold} -justify center -spacing3 6
bindtags  .all.pi.info.text {.all.pi.info.text . all}
pack      .all.pi.info.text -side left -fill y -expand 1
pack      .all.pi.info -side top -fill y -expand 1

pack      .all.pi -side left -anchor n -fill y -expand 1
pack      .all


menu .menubar -relief ridge
. config -menu .menubar
.menubar add cascade -label "Control" -menu .menubar.ctrl -underline 0
.menubar add cascade -label "Configure" -menu .menubar.config -underline 1
#.menubar add cascade -label "Reminder" -menu .menubar.timer -underline 0
.menubar add cascade -label "Filter" -menu .menubar.filter -underline 0
.menubar add cascade -label "Navigate" -menu .menubar.ni_1 -underline 0
#.menubar add command -label "" -activebackground $default_bg -command {} -foreground #2e2e37
.menubar add cascade -label "Help" -menu .menubar.help -underline 0
# Control menu
menu .menubar.ctrl -tearoff 0 -postcommand C_SetControlMenuStates
.menubar.ctrl add checkbutton -label "Enable acquisition" -variable menuStatusStartAcq -command {C_ToggleAcq $menuStatusStartAcq}
.menubar.ctrl add checkbutton -label "Dump stream" -variable menuStatusDumpStream -command {C_ToggleDumpStream $menuStatusDumpStream}
.menubar.ctrl add command -label "Dump database..." -command PopupDumpDatabase
.menubar.ctrl add separator
.menubar.ctrl add checkbutton -label "View timescales..." -command {C_StatsWin_ToggleTimescale ui} -variable menuStatusTscaleOpen(ui)
.menubar.ctrl add checkbutton -label "View acq timescales..." -command {C_StatsWin_ToggleTimescale acq} -variable menuStatusTscaleOpen(acq)
.menubar.ctrl add checkbutton -label "Acq Statistics..." -command C_StatsWin_ToggleStatistics -variable menuStatusAcqStatOpen
.menubar.ctrl add separator
.menubar.ctrl add command -label "Quit" -command {destroy .; update}
# Config menu
menu .menubar.config -tearoff 0
.menubar.config add command -label "Select provider..." -command ProvWin_Create
.menubar.config add command -label "Provider scan..." -command PopupEpgScan
.menubar.config add command -label "Select networks..." -command PopupNetwopSelection
.menubar.config add command -label "Filter shortcuts..." -command EditFilterShortcuts
.menubar.config add separator
.menubar.config add checkbutton -label "Show shortcuts" -command ToggleShortcutListbox -variable showShortcutListbox
.menubar.config add checkbutton -label "Show networks" -command ToggleNetwopListbox -variable showNetwopListbox
# Reminder menu
#menu .menubar.timer -tearoff 0
#.menubar.timer add command -label "Add selected title" -state disabled
#.menubar.timer add command -label "Add selected series" -state disabled
#.menubar.timer add command -label "Add filter selection" -state disabled
#.menubar.timer add separator
#.menubar.timer add command -label "List reminders..." -state disabled
# Filter menu
menu .menubar.filter
.menubar.filter add cascade -menu .menubar.filter.features -label Features
.menubar.filter add cascade -menu .menubar.filter.p_rating -label "Parental Rating"
.menubar.filter add cascade -menu .menubar.filter.e_rating -label "Editorial Rating"
.menubar.filter add separator
.menubar.filter add cascade -menu .menubar.filter.themes -label Themes
.menubar.filter add cascade -menu .menubar.filter.series -label Series
.menubar.filter add cascade -menu .menubar.filter.progidx -label "Program index"
.menubar.filter add cascade -menu .menubar.filter.netwops -label "Networks"
.menubar.filter add command -label "Text search..." -command SubStrPopup
.menubar.filter add command -label "Start Time..." -command PopupTimeFilterSelection
.menubar.filter add command -label "Sorting Criterions..." -command PopupSortCritSelection
.menubar.filter add separator
.menubar.filter add command -label "Add filter shortcut..." -command AddFilterShortcut
.menubar.filter add command -label "Reset" -command {ResetFilterState; C_ResetFilter all; C_ResetPiListbox}
menu .menubar.filter.e_rating
.menubar.filter.e_rating add radio -label any -command SelectEditorialRating -variable editorial_rating -value 0
.menubar.filter.e_rating add radio -label "all rated programmes" -command SelectEditorialRating -variable editorial_rating -value 1
.menubar.filter.e_rating add radio -label "at least 2 of 7" -command SelectEditorialRating -variable editorial_rating -value 2
.menubar.filter.e_rating add radio -label "at least 3 of 7" -command SelectEditorialRating -variable editorial_rating -value 3
.menubar.filter.e_rating add radio -label "at least 4 of 7" -command SelectEditorialRating -variable editorial_rating -value 4
.menubar.filter.e_rating add radio -label "at least 5 of 7" -command SelectEditorialRating -variable editorial_rating -value 5
.menubar.filter.e_rating add radio -label "at least 6 of 7" -command SelectEditorialRating -variable editorial_rating -value 6
.menubar.filter.e_rating add radio -label "7 of 7" -command SelectEditorialRating -variable editorial_rating -value 7
menu .menubar.filter.p_rating
.menubar.filter.p_rating add radio -label any -command SelectParentalRating -variable parental_rating -value 0
.menubar.filter.p_rating add radio -label "ok for all ages" -command SelectParentalRating -variable parental_rating -value 1
.menubar.filter.p_rating add radio -label "ok for 4 years or elder" -command SelectParentalRating -variable parental_rating -value 2
.menubar.filter.p_rating add radio -label "ok for 6 years or elder" -command SelectParentalRating -variable parental_rating -value 3
.menubar.filter.p_rating add radio -label "ok for 8 years or elder" -command SelectParentalRating -variable parental_rating -value 4
.menubar.filter.p_rating add radio -label "ok for 10 years or elder" -command SelectParentalRating -variable parental_rating -value 5
.menubar.filter.p_rating add radio -label "ok for 12 years or elder" -command SelectParentalRating -variable parental_rating -value 6
.menubar.filter.p_rating add radio -label "ok for 14 years or elder" -command SelectParentalRating -variable parental_rating -value 7
.menubar.filter.p_rating add radio -label "ok for 16 years or elder" -command SelectParentalRating -variable parental_rating -value 8
menu .menubar.filter.features
.menubar.filter.features add cascade -menu .menubar.filter.features.sound -label Sound
.menubar.filter.features add cascade -menu .menubar.filter.features.format -label Format
.menubar.filter.features add cascade -menu .menubar.filter.features.digital -label Digital
.menubar.filter.features add cascade -menu .menubar.filter.features.encryption -label Encryption
.menubar.filter.features add cascade -menu .menubar.filter.features.live -label "Live/Repeat"
.menubar.filter.features add cascade -menu .menubar.filter.features.subtitles -label Subtitles
.menubar.filter.features add separator
.menubar.filter.features add cascade -menu .menubar.filter.features.featureclass -label "Feature class"
menu .menubar.filter.features.sound
.menubar.filter.features.sound add radio -label any -command {SelectFeatures 0 0 0x03} -variable feature_sound -value 0
.menubar.filter.features.sound add radio -label mono -command {SelectFeatures 0x03 0x00 0} -variable feature_sound -value 1
.menubar.filter.features.sound add radio -label stereo -command {SelectFeatures 0x03 0x02 0} -variable feature_sound -value 3
.menubar.filter.features.sound add radio -label "2-channel" -command {SelectFeatures 0x03 0x01 0} -variable feature_sound -value 2
.menubar.filter.features.sound add radio -label surround -command {SelectFeatures 0x03 0x03 0} -variable feature_sound -value 4
menu .menubar.filter.features.format
.menubar.filter.features.format add radio -label any -command {SelectFeatures 0 0 0x0c} -variable feature_format -value 0
.menubar.filter.features.format add radio -label full -command {SelectFeatures 0x0c 0x00 0x00} -variable feature_format -value 1
.menubar.filter.features.format add radio -label widescreen -command {SelectFeatures 0x04 0x04 0x08} -variable feature_format -value 2
.menubar.filter.features.format add radio -label "PAL+" -command {SelectFeatures 0x08 0x08 0x04} -variable feature_format -value 3
menu .menubar.filter.features.digital
.menubar.filter.features.digital add radio -label any -command {SelectFeatures 0 0 0x10} -variable feature_digital -value 0
.menubar.filter.features.digital add radio -label analog -command {SelectFeatures 0x10 0x00 0} -variable feature_digital -value 1
.menubar.filter.features.digital add radio -label digital -command {SelectFeatures 0x10 0x10 0} -variable feature_digital -value 2
menu .menubar.filter.features.encryption
.menubar.filter.features.encryption add radio -label any -command {SelectFeatures 0 0 0x20} -variable feature_encryption -value 0
.menubar.filter.features.encryption add radio -label free -command {SelectFeatures 0x20 0 0} -variable feature_encryption -value 1
.menubar.filter.features.encryption add radio -label encrypted -command {SelectFeatures 0x20 0x20 0} -variable feature_encryption -value 2
menu .menubar.filter.features.live
.menubar.filter.features.live add radio -label any -command {SelectFeatures 0 0 0xc0} -variable feature_live -value 0
.menubar.filter.features.live add radio -label live -command {SelectFeatures 0x40 0x40 0x80} -variable feature_live -value 2
.menubar.filter.features.live add radio -label new -command {SelectFeatures 0xc0 0 0} -variable feature_live -value 1
.menubar.filter.features.live add radio -label repeat -command {SelectFeatures 0x80 0x80 0x40} -variable feature_live -value 3
menu .menubar.filter.features.subtitles
.menubar.filter.features.subtitles add radio -label any -command {SelectFeatures 0 0 0x100} -variable feature_subtitles -value 0
.menubar.filter.features.subtitles add radio -label untitled -command {SelectFeatures 0x100 0 0} -variable feature_subtitles -value 1
.menubar.filter.features.subtitles add radio -label subtitle -command {SelectFeatures 0x100 0x100 0} -variable feature_subtitles -value 2
menu .menubar.filter.features.featureclass
menu .menubar.filter.themes
menu .menubar.filter.series -postcommand {PostDynamicMenu .menubar.filter.series C_CreateSeriesNetwopMenu}
menu .menubar.filter.progidx
.menubar.filter.progidx add radio -label "any" -command {SelectProgIdx -1 -1} -variable filter_progidx -value 0
.menubar.filter.progidx add radio -label "running now" -command {SelectProgIdx 0 0} -variable filter_progidx -value 1
.menubar.filter.progidx add radio -label "running next" -command {SelectProgIdx 1 1} -variable filter_progidx -value 2
.menubar.filter.progidx add radio -label "running now or next" -command {SelectProgIdx 0 1} -variable filter_progidx -value 3
.menubar.filter.progidx add radio -label "other..." -command ProgIdxPopup -variable filter_progidx -value 4
menu .menubar.filter.netwops
# Navigation menu
menu .menubar.ni_1 -postcommand {PostDynamicMenu .menubar.ni_1 C_CreateNi}
# Help menu
menu .menubar.help -tearoff 0
.menubar.help add command -label "About..." -command CreateAbout

# Context Menu
menu .contextmenu -tearoff 0 -postcommand {PostDynamicMenu .contextmenu C_CreateContextMenu}

# initialize menu state
set menuStatusStartAcq 0
set menuStatusDumpStream 0
set menuStatusThemeClass 1
set menuStatusTscaleOpen(ui) 0
set menuStatusTscaleOpen(acq) 0
set menuStatusAcqStatOpen 0


##  ---------------------------------------------------------------------------
##  Generate the themes & feature menues
##
proc GenerateFilterMenues {tcc fcc} {
   global theme_class_count feature_class_count
   set theme_class_count   $tcc
   set feature_class_count $fcc

   # create the themes and sub-themes menues from the PDC table
   set subtheme 0
   for {set index 0} {$index < 0x80} {incr index} {
      set pdc [C_GetPdcString $index]
      if {[string match "* - general" $pdc]} {
         # remove appendix "general" for menu label
         set len [string length $pdc]
         set tlabel [string range $pdc 0 [expr $len - 10]]
         # create new sub-menu and add checkbutton
         set subtheme $index
         .menubar.filter.themes add cascade -label $tlabel -menu .menubar.filter.themes.$subtheme
         menu .menubar.filter.themes.$subtheme
         .menubar.filter.themes.$subtheme add checkbutton -label $pdc -command "SelectTheme $subtheme" -variable theme_sel($subtheme)
         .menubar.filter.themes.$subtheme add separator
      } elseif {([string length $pdc] > 0) && ($subtheme > 0)} {
         .menubar.filter.themes.$subtheme add checkbutton -label $pdc -command "SelectTheme $index" -variable theme_sel($index)
      }
   }
   # add sub-menu with one entry: all series on all networks
   .menubar.filter.themes add cascade -menu .menubar.filter.themes.series -label "series"
   menu .menubar.filter.themes.series -tearoff 0
   .menubar.filter.themes.series add checkbutton -label [C_GetPdcString 128] -command {SelectTheme 128} -variable theme_sel(128)
   # add theme-class sub-menu
   .menubar.filter.themes add separator
   .menubar.filter.themes add cascade -menu .menubar.filter.themes.themeclass -label "Theme class"
   menu .menubar.filter.themes.themeclass

   for {set index 1} {$index <= $theme_class_count} {incr index} {
      .menubar.filter.themes.themeclass add radio -label $index -command SelectThemeClass -variable menuStatusThemeClass -value $index
   }
   for {set index 1} {$index <= $feature_class_count} {incr index} {
      .menubar.filter.features.featureclass add radio -label $index -command {SelectFeatureClass $current_feature_class} -variable current_feature_class -value $index
   }

}

##  ---------------------------------------------------------------------------
##  Show or hide shortcuts and network filter listboxes
##
set showNetwopListbox 1
set showShortcutListbox 1

proc ToggleNetwopListbox {} {
   if {[lsearch -exact [pack slaves .all] .all.netwops] == -1} {
      # netwop listbox is currently not visible -> pack left of PI listbox
      pack .all.netwops -before .all.pi -side left -anchor n -pady 2 -padx 2
      set showNetwopListbox 1
   } else {
      # visible -> unpack
      pack forget .all.netwops
      set showNetwopListbox 0
   }
   UpdateRcFile
}

proc ToggleShortcutListbox {} {
   if {[lsearch -exact [pack slaves .all] .all.shortcuts] == -1} {
      # shortcuts listbox is currently not visible -> pack left of netwop bar or PI listbox
      if {[lsearch -exact [pack slaves .all] .all.netwops] == -1} {
         pack .all.shortcuts -before .all.pi -side left -anchor nw
      } else {
         pack .all.shortcuts -before .all.netwops -side left -anchor nw
      }
      set showShortcutListbox 1
   } else {
      # shortcuts listbox is visible -> unpack
      pack forget .all.shortcuts
      set showShortcutListbox 0
   }
   UpdateRcFile
}

##  ---------------------------------------------------------------------------
##  Set the feature radio button states according to the mask/value pair of the current class
##
proc UpdateFeatureMenuState {} {
   global feature_sound
   global feature_format
   global feature_digital
   global feature_encryption
   global feature_live
   global feature_subtitles
   global feature_class_mask feature_class_value
   global current_feature_class

   set mask $feature_class_mask($current_feature_class)
   set value $feature_class_value($current_feature_class)

   if {[expr ($mask & 3) != 0]} { set feature_sound [expr ($value & 3) + 1]} else {set feature_sound 0}
   if {[expr ($mask & 0x0c) != 0]} { set feature_format [expr (($value & 0x0c) >> 2) + 1] } else {set feature_format 0}
   if {[expr ($mask & 0x10) != 0]} { set feature_digital [expr (($value & 0x10) >> 4) + 1]} else {set feature_digital 0}
   if {[expr ($mask & 0x20) != 0]} { set feature_encryption [expr (($value & 0x20) >> 5) + 1]} else {set feature_encryption 0}
   if {[expr ($mask & 0xc0) != 0]} { set feature_live [expr (($value & 0xc0) >> 6) + 1]} else {set feature_live 0}
   if {[expr ($mask & 0x100) != 0]} { set feature_subtitles [expr (($value & 0x100) >> 8) + 1]} else {set feature_subtitles 0}
}

##  ---------------------------------------------------------------------------
##  Set the progidx radio button states according to the first/last prog indices
##
proc UpdateProgIdxMenuState {} {
   global progidx_first progidx_last
   global filter_progidx

         if {($progidx_first == 0) && ($progidx_last == 0)} { set filter_progidx 1
   } elseif {($progidx_first == 1) && ($progidx_last == 1)} { set filter_progidx 2
   } elseif {($progidx_first == 0) && ($progidx_last == 1)} { set filter_progidx 3
   } else                                                   { set filter_progidx 4
   }
}

##  ---------------------------------------------------------------------------
##  Select netwops in filter bar and menu checkbuttons
##  - given indices are in original AI order and have to be mapped to user order
##
proc UpdateNetwopMenuState {netlist} {
   global netwop_map netselmenu

   if {[llength $netlist] > 0} {
      # get reverse netwop mapping, i.e. user-selected netwop ordering
      foreach {idx val} [array get netwop_map] {
         set order($val) $idx
      }

      # deselect netwop listbox and menu checkbuttons
      .all.netwops.list selection clear 0 end
      if {[info exists netselmenu]} { unset netselmenu }

      foreach netwop $netlist {
         if {[info exists order($netwop)]} {
            set netselmenu($order($netwop)) 1
            .all.netwops.list selection set [expr $order($netwop) + 1]
         }
      }
   } else {
      .all.netwops.list selection clear 1 end
      .all.netwops.list selection set 0
      if {[info exists netselmenu]} { unset netselmenu }
   }
}

##  ---------------------------------------------------------------------------
##  Reset the state of all filter menus
##
proc ResetThemes {} {
   global theme_class_count current_theme_class menuStatusThemeClass
   global theme_sel theme_class_sel

   if {[info exists theme_sel]} {
      unset theme_sel
   }
   for {set index 1} {$index <= $theme_class_count} {incr index} {
      set theme_class_sel($index) {}
   }
   set current_theme_class 1
   set menuStatusThemeClass $current_theme_class
}

proc ResetSortCrits {} {
   global theme_class_count sortcrit_class sortcrit_class_sel

   for {set index 1} {$index <= $theme_class_count} {incr index} {
      set sortcrit_class_sel($index) {}
   }
   set sortcrit_class 1

   UpdateSortCritListbox
}

proc ResetFeatures {} {
   global feature_class_mask feature_class_value
   global feature_class_count current_feature_class

   for {set index 1} {$index <= $feature_class_count} {incr index} {
      set feature_class_mask($index)  0
      set feature_class_value($index) 0
   }
   set current_feature_class 1

   UpdateFeatureMenuState
}

proc ResetSeries {} {
   global series_sel

   foreach index [array names series_sel] {
      unset series_sel($index)
   }
}

proc ResetProgIdx {} {
   global filter_progidx

   set filter_progidx 0
}

proc ResetTimSel {} {
   global timsel_enabled

   set timsel_enabled 0
}

proc ResetParentalRating {} {
   global parental_rating editorial_rating

   set parental_rating 0
}

proc ResetEditorialRating {} {
   global parental_rating editorial_rating

   set editorial_rating 0
}

proc ResetSubstr {} {
   global substr_pattern

   set substr_pattern {}
}

proc ResetNetwops {} {
   global netselmenu

   # reset all checkbuttons in the netwop filter menu
   if {[info exists netselmenu]} { unset netselmenu }

   # reset the netwop filter bar
   .all.netwops.list selection clear 1 end
   .all.netwops.list selection set 0
}

proc ResetFilterState {} {
   ResetThemes
   ResetSortCrits
   ResetFeatures
   ResetSeries
   ResetProgIdx
   ResetTimSel
   ResetParentalRating
   ResetEditorialRating
   ResetSubstr
   ResetNetwops

   # reset the filter shortcut bar
   .all.shortcuts.list selection clear 0 end
}

##  --------------------- F I L T E R   C A L L B A C K S ---------------------

##
##  Callback for button-release on netwop listbox
##
proc SelectNetwop {} {
   global netwop_map netselmenu

   if {[info exists netselmenu]} { unset netselmenu }

   if {[.all.netwops.list selection includes 0]} {
      .all.netwops.list selection clear 1 end
      C_SelectNetwops {}
   } else {
      if {[info exists netwop_map]} {
         set all {}
         foreach idx [.all.netwops.list curselection] {
            set netidx [expr $idx - 1]
            set netselmenu($netidx) 1
            if {[info exists netwop_map($netidx)]} {
               lappend all $netwop_map($netidx)
            }
         }
      } else {
         foreach idx [.all.netwops.list curselection] {
            set netselmenu([expr $idx - 1]) 1
         }
         set all {}
         foreach idx [.all.netwops.list curselection] {
            lappend all [expr $idx - 1]
         }
      }
      # when all netwops have been deselected, select "all" item
      if {[llength $all] == 0} {
         .all.netwops.list selection set 0
      }
      # set filters in context
      C_SelectNetwops $all
   }
   C_RefreshPiListbox
   CheckShortcutDeselection
}

##
##  Select network in filter menu
##
proc SelectNetwopMenu {netidx} {
   global netselmenu

   if {$netselmenu($netidx)} {
      .all.netwops.list selection clear 0
      .all.netwops.list selection set [expr $netidx + 1]
   } else {
      .all.netwops.list selection clear [expr $netidx + 1]
   }
   SelectNetwop
}

##
##  Update the filter context and refresh the listbox
##
proc SelectTheme {index} {
   global theme_sel current_theme_class

   set all {}
   foreach {index value} [array get theme_sel] {
      if {$value != 0} {
         lappend all $index
      }
   }
   C_SelectThemes $current_theme_class $all
   C_RefreshPiListbox
   CheckShortcutDeselection
}

##
##  Disable theme in any class - used by Context Menu
##
proc DeselectTheme {class theme} {
   global theme_sel theme_class_sel current_theme_class

   if {$class == $current_theme_class} {
      # selected class is the current one -> handled just like normal theme menu button
      set theme_sel($theme) 0
      SelectTheme $theme
   } elseif {[info exists theme_class_sel($class)]} {
      # not the current class: have to work with array
      set arridx [lsearch -exact $theme_class_sel($class) $theme]
      if {$arridx != -1} {
         set theme_class_sel($class) [lreplace $theme_class_sel($class) $arridx $arridx]
         C_SelectThemes $class $theme_class_sel($class)
         C_RefreshPiListbox
         CheckShortcutDeselection
      }
   }
}

##
##  Callback for theme class radio buttons
##
proc SelectThemeClass {} {
   global theme_class_count current_theme_class menuStatusThemeClass
   global theme_class_sel
   global theme_sel

   # assert that the class index is valid
   if {$menuStatusThemeClass > $theme_class_count} {
      set menuStatusThemeClass $current_theme_class
   }

   if {$menuStatusThemeClass != $current_theme_class} {
      # save settings of current class into list variable
      set all {}
      foreach {index value} [array get theme_sel "*"] {
         if {[expr $value != 0]} {
            lappend all $index
         }
      }
      set theme_class_sel($current_theme_class) $all

      # remove the settings of the old class
      if {[info exists theme_sel]} {
         unset theme_sel
      }
      # change to the new class
      set current_theme_class $menuStatusThemeClass
      # copy the new classes' settings into the array, thereby setting the menu state
      foreach index $theme_class_sel($current_theme_class) {
         set theme_sel($index) 1
      }
   }
}

##
##  Callback for feature menus
##
proc SelectFeatures {mask value unmask} {
   global feature_class_mask feature_class_value
   global feature_class_count current_feature_class

   ## fetch the current state into local variables (for readability)
   set curmask $feature_class_mask($current_feature_class)
   set curvalue $feature_class_value($current_feature_class)

   ## change the state of the feature group given by mask
   set curmask [expr ($curmask & ~$unmask) | $mask]
   set curvalue [expr ($curvalue & ~($mask | $unmask)) | $value]

   ## save the new state into the global array
   set feature_class_mask($current_feature_class) $curmask
   set feature_class_value($current_feature_class) $curvalue

   ## concatenate the filters of all classes into one string
   set all {}
   for {set class 1} {$class <= $feature_class_count} {incr class} {
      if {[expr $feature_class_mask($class) != 0]} {
         lappend all $feature_class_mask($class) $feature_class_value($class)
      }
   }

   ## update the database filter context and re-display the listbox
   C_SelectFeatures $all
   C_RefreshPiListbox
   CheckShortcutDeselection
}

##
##  Set features in all classes - used by Context Menu
##
proc SelectFeaturesAllClasses {mask value unmask} {
   global feature_class_mask feature_class_value
   global feature_class_count current_feature_class

   set found 0
   for {set class 1} {$class <= $feature_class_count} {incr class} {
      if {$feature_class_mask($class) != 0} {
         if {$class == $current_feature_class} {
            SelectFeatures $mask $value $unmask
            UpdateFeatureMenuState
         } else {
            set saveClass $current_feature_class
            SelectFeatures $mask $value $unmask
            set current_feature_class $saveClass
         }
         set found 1
      }
   }

   # if there are no features set yet, use the current class
   if {$found == 0} {
      SelectFeatures $mask $value $unmask
      UpdateFeatureMenuState
   }
}

##
##  Callback for feature class radio buttons
##
proc SelectFeatureClass {new_class} {
   global feature_class_mask feature_class_value
   global current_feature_class

   set current_feature_class $new_class

   if {[info exists feature_class_mask($current_feature_class)]} {
   } else {
      set $feature_class_mask($current_feature_class) 0
      set $feature_class_value($current_feature_class) 0
   }
   UpdateFeatureMenuState
}


##
##  Callback for parental rating radio buttons
##
proc SelectParentalRating {} {
   global parental_rating

   C_SelectParentalRating $parental_rating
   C_RefreshPiListbox
   CheckShortcutDeselection
}

##
##  Callback for parental rating radio buttons
##
proc SelectEditorialRating {} {
   global editorial_rating

   C_SelectEditorialRating $editorial_rating
   C_RefreshPiListbox
   CheckShortcutDeselection
}

##
##  Callback for series checkbuttons
##
proc SelectSeries {series} {
   global series_sel

   # upon deselection, check if any other series remains selected
   if {$series_sel($series) == 0} {
      set id [array startsearch series_sel]
      set empty 1
      while {[string length [set index [array nextelement series_sel $id]]] > 0} {
         if {$series_sel($index) != 0} {
            set empty 0
            break
         }
      }
      array donesearch series_sel $id
      if {$empty} {C_SelectSeries
      } else      {C_SelectSeries $series 0}
   } else {
      C_SelectSeries $series 1
   }
   C_RefreshPiListbox
   CheckShortcutDeselection
}

##
##  Callback for program index checkbuttons
##
proc SelectProgIdx {first last} {
   global progidx_first progidx_last
   global filter_progidx

   # set vars for the sliders in the progidx popup
   if {$filter_progidx > 0} {
      set progidx_first $first
      set progidx_last  $last
      C_SelectProgIdx $first $last
   } else {
      C_SelectProgIdx
   }
   C_RefreshPiListbox
   CheckShortcutDeselection
}

##  ---------------------------------------------------------------------------
##  Determine item index below the mouse pointer
##
proc GetSelectedItem {xcoo ycoo} {
   scan [.all.pi.list.text index "@$xcoo,$ycoo"] "%d.%d" new_line char
   expr $new_line - 1
}

##  ---------------------------------------------------------------------------
##  Callback for right-click on a PiListBox item
##
proc CreateContextMenu {xcoo ycoo} {
   global contextMenuItem

   set xcoo [expr $xcoo + [winfo rootx .all.pi.list.text] - 5]
   set ycoo [expr $ycoo + [winfo rooty .all.pi.list.text] - 5]

   # remember index of selected listbox item
   #set contextMenuItem GetSelectedItem $xcoo $ycoo

   .contextmenu post $xcoo $ycoo
}

##  ---------------------------------------------------------------------------
##  Callback for double-click on a PiListBox item
##
set poppedup_pi {}

proc Create_PopupPi {ident xcoo ycoo} {
   global poppedup_pi
   if {[string length $poppedup_pi] > 0} {destroy $poppedup_pi}
   set poppedup_pi $ident

   toplevel $poppedup_pi
   bind $poppedup_pi <Leave> {destroy %W; set poppedup_pi ""}
   wm overrideredirect $poppedup_pi 1
   set xcoo [expr $xcoo + [winfo rootx .all.pi.list.text] - 200]
   set ycoo [expr $ycoo + [winfo rooty .all.pi.list.text] - 5]
   wm geometry $poppedup_pi "+$xcoo+$ycoo"
   text $poppedup_pi.text -relief ridge -width 55 -height 20 -cursor circle
   $poppedup_pi.text tag configure title \
                     -justify center -spacing3 12 -font {helvetica -18 bold}

   #C_PopupPi $poppedup_pi.text $netwop $blockno
   #$poppedup_pi.text configure -state disabled
   #$poppedup_pi.text configure -height [expr 1 + [$poppedup_pi.text index end]]
   #pack $poppedup_pi.text
}

## ---------------------------------------------------------------------------
## Sort a list of network names and append them to the series networks menu
##
proc CompareSeriesMenuNetwops {ordarr a b} {
   upvar $ordarr order

   if {[info exists order([lindex $a 0])]} {
      set ord_a $order([lindex $a 0])
   } else {
      set ord_a 0xff
   }

   if {[info exists order([lindex $b 0])]} {
      set ord_b $order([lindex $b 0])
   } else {
      set ord_b 0xff
   }

     if     {[expr $ord_a < $ord_b]} { return -1
   } elseif {[expr $ord_a > $ord_b]} { return  1
   } else                            { return  0
   }
}

proc FillSeriesMenu {parent prov netlist} {
   global netwop_map

   # get reverse netwop mapping, i.e. user-selected netwop ordering
   foreach {idx val} [array get netwop_map] {
      set order($val) $idx
   }

   foreach item [lsort -command {CompareSeriesMenuNetwops order} $netlist] {
      set netwop [lindex $item 0]
      # check if netwop is suppressed by user config
      if {[info exists order($netwop)]} {
         set child $parent.netwop_$netwop

         $parent add cascade -label [lindex $item 1] -menu $child
         if {![info exist dynmenu_posted($child)] || ($dynmenu_posted($child) == 0)} {
            menu $child -postcommand [list PostDynamicMenu $child C_CreateSeriesMenu]
         }
      }
   }
}

##  ---------------------------------------------------------------------------
##  Sort a list of programme titles and append them to a series menu
##
proc CompareSeriesMenuEntries {a b} {
   return [string compare [lindex $a 0] [lindex $b 0]]
}

proc CreateSeriesMenuEntries {menu_name tmp_list lang} {
   set all {}
   foreach {title series} $tmp_list {
      switch $lang {
         0 {  # English
            regsub -nocase -- "^(the|a) (.*)" $title {\2, \1} result
         }
         1 {  # German
            regsub -nocase -- "^(der|die|das|ein|eine) (.*)" $title {\2, \1} result
         }
         3 {  # Italian
            regsub -nocase -- "^(una|uno|la) (.*)" $title {\2, \1} result
         }
         4 {  # French
            regsub -nocase -- "^(un |une |la |le |les |l')(.*)" $title {\2, \1} result
         }
         default {
            set result $title
         }
      }
      # force the new first title character to be uppercase (for sorting)
      lappend all [list [string toupper [string index $result 0]][string range $result 1 end] $series]
   }
   foreach item [lsort -command CompareSeriesMenuEntries $all] {
      $menu_name add checkbutton -label [lindex $item 0] -variable series_sel([lindex $item 1]) -command [list SelectSeries [lindex $item 1]]
   }
}

##  ---------------------------------------------------------------------------
##  Handling of dynamic menus
##  - the content and sub-menus are not created before they are posted
##  - several instances of the menu might be posted by use of tear-off,
##    but we must not destroy the menu until the last instance is unposted
##
proc PostDynamicMenu {menu cmd} {
   global dynmenu_posted dynmenu_created

   if {![info exist dynmenu_posted($menu)]} {
      set dynmenu_posted($menu) 0
   }
   if {$dynmenu_posted($menu) == 0} {
      # delete the previous menu content
      if {![string equal "none" [$menu index end]]} {
         for {set i [$menu index end]} {$i >= 0} {incr i -1} {
            #puts stdout "post $menu -> check index $i"
            if {[string compare [$menu type $i] "cascade"] == 0} {
               set subname [$menu entrycget $i -menu]
               if {![info exist dynmenu_posted($subname)] || ($dynmenu_posted($subname) == 0)} {
                  #puts stdout "post $menu -> destroy $subname"
                  destroy $subname
               }
            }
            $menu delete $i
         }
      }
      # invoke the individual constructor procedure
      if {[string length $cmd] > 0} {
         $cmd $menu
      }
      # empty the menu and destroy all submenus when unposted
      bind $menu <Unmap> "+ UnpostDynamicMenu $menu"
      # same binding for tear-off menus, which are never unposted
      bind $menu <Destroy> "+ UnpostDynamicMenu $menu"
   }
   # increment the reference counter
   incr dynmenu_posted($menu)

   #puts stdout "$menu posted: ref $dynmenu_posted($menu)"
   #foreach {index value} [array get dynmenu_posted] {puts stdout "MENU $index: ref=$value"}
}

proc UnpostDynamicMenu {menu} {
   global dynmenu_posted

   if {[info exist dynmenu_posted($menu)] && ($dynmenu_posted($menu) > 0)} {
      # decrement the reference counter
      incr dynmenu_posted($menu) -1

      if {($dynmenu_posted($menu) == 0) && ([string length [info command $menu]] > 0)} {
         bind $menu <Destroy> {}
      }
   }

   #puts stdout "$menu unmapped: ref $dynmenu_posted($menu)"
}

##  --------------------------------------------------------------------------
##  Text search pop-up window
##
set substr_grep_title 1
set substr_grep_descr 1
set substr_ignore_case 0
set substr_popup 0
set substr_pattern {}

# check that at lease one of the {title descr} options remains checked
proc SubstrCheckOptScope {varname} {
   global substr_grep_title substr_grep_descr

   if {($substr_grep_title == 0) && ($substr_grep_descr == 0)} {
      set $varname 1
   }
}

# update the filter context and refresh the PI listbox
proc SubstrUpdateFilter {} {
   global substr_grep_title substr_grep_descr substr_ignore_case
   global substr_pattern

   C_SelectSubStr $substr_grep_title $substr_grep_descr $substr_ignore_case $substr_pattern
   C_RefreshPiListbox
   CheckShortcutDeselection
}

proc SubStrPopup {} {
   global substr_grep_title substr_grep_descr
   global substr_popup
   global substr_pattern

   if {$substr_popup == 0} {
      toplevel .substr
      wm title .substr "Text Search"
      wm resizable .substr 0 0
      wm group .substr .
      set substr_popup 1

      frame .substr.all

      frame .substr.all.name
      label .substr.all.name.prompt -text "Enter text:"
      pack .substr.all.name.prompt -side left
      entry .substr.all.name.str -textvariable substr_pattern
      pack .substr.all.name.str -side left
      bind .substr.all.name.str <Enter> {focus %W}
      bind .substr.all.name.str <Return> SubstrUpdateFilter
      bind .substr.all.name.str <Escape> {destroy .substr}
      pack .substr.all.name -side top -pady 10

      frame .substr.all.opt
      frame .substr.all.opt.scope
      checkbutton .substr.all.opt.scope.titles -text "titles" -variable substr_grep_title -command {SubstrCheckOptScope substr_grep_descr}
      pack .substr.all.opt.scope.titles -side top -anchor nw
      checkbutton .substr.all.opt.scope.descr -text "descriptions" -variable substr_grep_descr -command {SubstrCheckOptScope substr_grep_title}
      pack .substr.all.opt.scope.descr -side top -anchor nw
      pack .substr.all.opt.scope -side left -padx 5
      frame .substr.all.opt.case
      checkbutton .substr.all.opt.case.ignore -text "ignore case" -variable substr_ignore_case
      pack .substr.all.opt.case.ignore -anchor nw
      pack .substr.all.opt.case -side left -anchor nw -padx 5
      pack .substr.all.opt -side top -pady 10

      frame .substr.all.cmd
      button .substr.all.cmd.apply -text "Apply" -command SubstrUpdateFilter
      pack .substr.all.cmd.apply -side left -padx 10
      button .substr.all.cmd.clear -text "Clear" -command {set substr_pattern {}; SubstrUpdateFilter}
      pack .substr.all.cmd.clear -side left -padx 10
      button .substr.all.cmd.dismiss -text "Dismiss" -command {destroy .substr}
      pack .substr.all.cmd.dismiss -side left -padx 10
      pack .substr.all.cmd -side top

      pack .substr.all -padx 10 -pady 10
      bind .substr.all <Destroy> {+ set substr_popup 0}
   }
}

##  --------------------------------------------------------------------------
##  Program-Index pop-up
##
set progidx_first 0
set progidx_last  0
set progidx_popup 0

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

proc ProgIdxPopup {} {
   global progidx_first progidx_last
   global progidx_popup

   if {$progidx_popup == 0} {
      toplevel .progidx
      wm title .progidx "Program index selection"
      wm resizable .progidx 0 0
      wm group .progidx .
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
      button .progidx.cmd.clear -text "Undo" -command {set filter_progidx 0; C_SelectProgIdx; C_RefreshPiListbox; destroy .progidx}
      pack .progidx.cmd.clear -side left -padx 20
      button .progidx.cmd.dismiss -text "Dismiss" -command {destroy .progidx}
      pack .progidx.cmd.dismiss -side left -padx 20
      pack .progidx.cmd -side top -pady 10

      bind .progidx.cmd <Destroy> {+ set progidx_popup 0}
   }
}

##  --------------------------------------------------------------------------
##  Time filter selection popup
##
set timsel_popup 0
set timsel_enabled  0
set timsel_relative 0
set timsel_absstop  0
set timsel_stop     [expr 23*60+59]

proc PopupTimeFilterSelection {} {
   global timsel_enabled timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop
   global timsel_startstr timsel_stopstr
   global timsel_popup

   if {$timsel_popup == 0} {
      toplevel .timsel
      wm title .timsel "Time filter selection"
      wm resizable .timsel 0 0
      wm group .timsel .
      set timsel_popup 1

      frame .timsel.all
      checkbutton .timsel.all.relstart -text "Start at current time" -command SelectTimeFilterRelStart -variable timsel_relative
      pack  .timsel.all.relstart -side top -anchor w

      frame .timsel.all.start -bd 1 -relief ridge
      label .timsel.all.start.lab -text "Start time:" -width 12
      entry .timsel.all.start.str -width 7 -textvariable timsel_startstr
      bind  .timsel.all.start.str <Enter> {focus %W}
      bind  .timsel.all.start.str <Return> {SelectTimeFilterEntry timsel_start $timsel_startstr}
      bind  .timsel.all.start.str <Escape> {destroy .timsel}
      scale .timsel.all.start.val -orient hor -length 200 -command {SelectTimeFilter 1} -variable timsel_start -from 0 -to 1439 -showvalue 0
      pack  .timsel.all.start.lab .timsel.all.start.str .timsel.all.start.val -side left
      pack  .timsel.all.start -side top -pady 10

      checkbutton .timsel.all.absstop -text "Stop at end of day" -command SelectTimeFilterRelStart -variable timsel_absstop
      pack  .timsel.all.absstop -side top -anchor w

      frame .timsel.all.stop -bd 1 -relief ridge
      label .timsel.all.stop.lab -text "Time span:" -width 12
      entry .timsel.all.stop.str -text "00:00" -width 7 -textvariable timsel_stopstr
      bind  .timsel.all.stop.str <Enter> {focus %W}
      bind  .timsel.all.stop.str <Return> {SelectTimeFilterEntry timsel_stop $timsel_stopstr}
      bind  .timsel.all.stop.str <Escape> {destroy .timsel}
      scale .timsel.all.stop.val -orient hor -length 200 -command {SelectTimeFilter 2} -variable timsel_stop -from 0 -to 1439 -showvalue 0
      pack  .timsel.all.stop.lab .timsel.all.stop.str .timsel.all.stop.val -side left
      pack  .timsel.all.stop -side top -pady 10

      frame .timsel.all.date -bd 1 -relief ridge
      label .timsel.all.date.lab -text "Rel. date:" -width 12
      label .timsel.all.date.str -width 7
      scale .timsel.all.date.val -orient hor -length 200 -command {SelectTimeFilter 0} -variable timsel_date -from 0 -to 6 -showvalue 0
      pack  .timsel.all.date.lab .timsel.all.date.str .timsel.all.date.val -side left
      pack  .timsel.all.date -side top -pady 10
      pack  .timsel.all -padx 10 -pady 10 -side top

      frame .timsel.cmd
      button .timsel.cmd.undo -text "Undo" -command {destroy .timsel; UndoTimeFilter}
      pack .timsel.cmd.undo -side left -padx 10
      button .timsel.cmd.dismiss -text "Dismiss" -command {destroy .timsel}
      pack .timsel.cmd.dismiss -side left -padx 10
      pack .timsel.cmd -side top -pady 5

      bind .timsel.all <Destroy> {+ set timsel_popup 0}

      set $timsel_enabled 1
      if {$timsel_relative} {
         SelectTimeFilterRelStart
      }
   }
}

proc SelectTimeFilterRelStart {} {
   global timsel_relative timsel_absstop

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
      .timsel.all.stop.lab configure -text "Stop time:"
   }
   SelectTimeFilter 0
}

proc SelectTimeFilterEntry {varname val} {
   global timsel_start timsel_startstr
   global timsel_stop timsel_stopstr
   upvar $varname timspec

   if {[scan $val "%d:%d" hour minute] == 2} {
      set timspec [expr $hour * 60 + $minute]
      SelectTimeFilter 0
   } else {
      tk_messageBox -type ok -default ok -icon error -message "Invalid input format in \"$val\"; use \"HH:MM\""
   }
}

proc SelectTimeFilter { round {val 0} } {
   global timsel_start timsel_stop timsel_date
   global timsel_startstr timsel_stopstr
   global timsel_relative timsel_absstop
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
      set timsel_startstr [format "%02d:%02d" [expr $timsel_start / 60] [expr $timsel_start % 60]]
   }
   if {$timsel_absstop} {
      .timsel.all.stop.str configure -state normal
      set timsel_stopstr "23:59"
      .timsel.all.stop.str configure -state disabled
   } else {
      set timsel_stopstr [format "%02d:%02d" [expr $timsel_stop / 60] [expr $timsel_stop % 60]]
   }
   .timsel.all.date.str configure -text [format "+%d days" $timsel_date]

   C_SelectStartTime $timsel_relative $timsel_absstop $timsel_start $timsel_stop $timsel_date
   C_RefreshPiListbox
   CheckShortcutDeselection
}

proc UndoTimeFilter {} {
   global timsel_enabled

   set timsel_enabled 0

   C_SelectStartTime
   C_RefreshPiListbox
   CheckShortcutDeselection
}

##  --------------------------------------------------------------------------
##  Sorting criterion selection popup
##
set sortcrit_popup 0

proc PopupSortCritSelection {} {
   global sortcrit_str sortcrit_class sortcrit_class_sel
   global sortcrit_popup

   if {$sortcrit_popup == 0} {
      toplevel .sortcrit
      wm title .sortcrit "Sorting criterion selection"
      wm resizable .sortcrit 0 0
      wm group .sortcrit .
      set sortcrit_popup 1

      frame .sortcrit.fl
      listbox .sortcrit.fl.list -exportselection false -setgrid true -height 15 -width 10 -selectmode extended -relief ridge -yscrollcommand {.sortcrit.fl.sb set}
      scrollbar .sortcrit.fl.sb -orient vertical -command {.sortcrit.fl.list yview}
      pack .sortcrit.fl.sb .sortcrit.fl.list -side left -expand 1 -fill y
      pack .sortcrit.fl -padx 5 -pady 5 -side left

      # entry field for additions
      frame .sortcrit.all
      frame .sortcrit.all.inp -bd 2 -relief ridge
      label .sortcrit.all.inp.lab -text "0x"
      pack  .sortcrit.all.inp.lab -side left
      entry .sortcrit.all.inp.str -width 14 -textvariable sortcrit_str
      bind  .sortcrit.all.inp.str <Enter> {focus %W}
      bind  .sortcrit.all.inp.str <Return> AddSortCritSelection
      bind  .sortcrit.all.inp.str <Escape> {destroy .sortcrit}
      pack  .sortcrit.all.inp.str -side left -fill x -expand 1
      pack  .sortcrit.all.inp -side top -pady 10 -anchor nw -fill x -expand 1

      # buttons Add and Delete below entry field
      button .sortcrit.all.scadd -text "Add" -command AddSortCritSelection -width 7
      button .sortcrit.all.scdel -text "Delete" -command DeleteSortCritSelection -width 7
      pack .sortcrit.all.scadd .sortcrit.all.scdel -side top -anchor nw

      # class selection array
      frame .sortcrit.all.sccl -bd 2 -relief ridge
      label .sortcrit.all.sccl.lab -text "Class:"
      pack  .sortcrit.all.sccl.lab -side top
      frame .sortcrit.all.sccl.t
      frame .sortcrit.all.sccl.b
      radiobutton .sortcrit.all.sccl.t.c1 -text "1" -variable sortcrit_class -value 1 -command UpdateSortCritListbox
      radiobutton .sortcrit.all.sccl.t.c2 -text "2" -variable sortcrit_class -value 2 -command UpdateSortCritListbox
      radiobutton .sortcrit.all.sccl.t.c3 -text "3" -variable sortcrit_class -value 3 -command UpdateSortCritListbox
      radiobutton .sortcrit.all.sccl.t.c4 -text "4" -variable sortcrit_class -value 4 -command UpdateSortCritListbox
      radiobutton .sortcrit.all.sccl.b.c5 -text "5" -variable sortcrit_class -value 5 -command UpdateSortCritListbox
      radiobutton .sortcrit.all.sccl.b.c6 -text "6" -variable sortcrit_class -value 6 -command UpdateSortCritListbox
      radiobutton .sortcrit.all.sccl.b.c7 -text "7" -variable sortcrit_class -value 7 -command UpdateSortCritListbox
      radiobutton .sortcrit.all.sccl.b.c8 -text "8" -variable sortcrit_class -value 8 -command UpdateSortCritListbox
      pack .sortcrit.all.sccl.t.c1 .sortcrit.all.sccl.t.c2 .sortcrit.all.sccl.t.c3 .sortcrit.all.sccl.t.c4 -side left
      pack .sortcrit.all.sccl.b.c5 .sortcrit.all.sccl.b.c6 .sortcrit.all.sccl.b.c7 .sortcrit.all.sccl.b.c8 -side left
      pack .sortcrit.all.sccl.t .sortcrit.all.sccl.b -side top
      pack .sortcrit.all.sccl -side top -anchor w -pady 5

      # Buttons at bottom: Clear and Dismiss
      button .sortcrit.all.apply -text "Dismiss" -command {destroy .sortcrit} -width 7
      pack .sortcrit.all.apply -side bottom -anchor w
      button .sortcrit.all.clear -text "Clear" -command ClearSortCritSelection -width 7
      pack .sortcrit.all.clear -side bottom -pady 5 -anchor w
      pack .sortcrit.all -side left -anchor n -fill y -expand 1 -padx 5 -pady 5

      bind .sortcrit.all <Destroy> {+ set sortcrit_popup 0}

      UpdateSortCritListbox
   }
}

# Clear all sorting criterion selections (all classes)
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
         default {tk_messageBox -type ok -default ok -icon error -message "Invalid entry item \"$item\"; expected format: nn\[-nn\]{,nn-...}"}
      }
      if {$to != -1} {
         if {$to < $from} {set swap $to; set to $from; set from $swap}
         if {$to >= 256} {
            tk_messageBox -type ok -default ok -icon error -message "Invalid range; values must be lower than 0x100"
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

   .sortcrit.all.inp.str delete 0 end
   UpdateSortCritListbox

   C_SelectSortCrits $sortcrit_class $sortcrit_class_sel($sortcrit_class)
   C_RefreshPiListbox
}

proc UpdateSortCritListbox {} {
   global sortcrit_popup
   global sortcrit_class sortcrit_class_sel

   set all {}
   if {$sortcrit_popup} {
      .sortcrit.fl.list delete 0 end
      foreach item [lsort -integer $sortcrit_class_sel($sortcrit_class)] {
         .sortcrit.fl.list insert end [format "0x%02x" $item]
         lappend all $item
      }
   }
   return $all
}

##  --------------------------------------------------------------------------
##  DumpDatabase pop-up window
##
set dumpdb_pi 1
set dumpdb_ai 1
set dumpdb_bi 1
set dumpdb_ni 1
set dumpdb_oi 1
set dumpdb_mi 1
set dumpdb_li 1
set dumpdb_ti 1
set dumpdb_popup 0

proc PopupDumpDatabase {} {
   global dumpdb_pi dumpdb_ai dumpdb_bi dumpdb_ni
   global dumpdb_oi dumpdb_mi dumpdb_li dumpdb_ti
   global dumpdb_popup

   if {$dumpdb_popup == 0} {
      toplevel .dumpdb
      wm title .dumpdb "Dump Database"
      wm resizable .dumpdb 0 0
      wm group .dumpdb .
      set dumpdb_popup 1

      frame .dumpdb.all

      frame .dumpdb.all.name
      label .dumpdb.all.name.prompt -text "Enter file name:"
      pack .dumpdb.all.name.prompt -side left -padx 20
      entry .dumpdb.all.name.filename -textvariable dumpdb_filename
      pack .dumpdb.all.name.filename -side left
      bind .dumpdb.all.name.filename <Enter> {focus %W}
      bind .dumpdb.all.name.filename <Return> {C_DumpDatabase $dumpdb_filename $dumpdb_pi $dumpdb_ai $dumpdb_bi $dumpdb_ni $dumpdb_oi $dumpdb_mi $dumpdb_li $dumpdb_ti; destroy .dumpdb}
      bind .dumpdb.all.name.filename <Escape> {destroy .dumpdb}
      pack .dumpdb.all.name -side top -pady 10

      frame .dumpdb.all.opt
      frame .dumpdb.all.opt.one
      checkbutton .dumpdb.all.opt.one.pi -text "Programme Info" -variable dumpdb_pi
      checkbutton .dumpdb.all.opt.one.ni -text "Navigation Info" -variable dumpdb_ni
      checkbutton .dumpdb.all.opt.one.ai -text "Application Info" -variable dumpdb_ai
      checkbutton .dumpdb.all.opt.one.bi -text "Bundle Info" -variable dumpdb_bi
      pack .dumpdb.all.opt.one.pi -side top -anchor nw
      pack .dumpdb.all.opt.one.ni -side top -anchor nw
      pack .dumpdb.all.opt.one.ai -side top -anchor nw
      pack .dumpdb.all.opt.one.bi -side top -anchor nw
      pack .dumpdb.all.opt.one -side left -anchor nw -padx 5

      frame .dumpdb.all.opt.two
      checkbutton .dumpdb.all.opt.two.oi -text "OSD Info" -variable dumpdb_oi
      checkbutton .dumpdb.all.opt.two.mi -text "Message Info" -variable dumpdb_mi
      checkbutton .dumpdb.all.opt.two.li -text "Language Info" -variable dumpdb_li
      checkbutton .dumpdb.all.opt.two.ti -text "Subtitles Info" -variable dumpdb_ti
      pack .dumpdb.all.opt.two.oi -side top -anchor nw
      pack .dumpdb.all.opt.two.mi -side top -anchor nw
      pack .dumpdb.all.opt.two.li -side top -anchor nw
      pack .dumpdb.all.opt.two.ti -side top -anchor nw
      pack .dumpdb.all.opt.two -side left -anchor nw -padx 5
      pack .dumpdb.all.opt -side top -pady 10

      frame .dumpdb.all.cmd
      button .dumpdb.all.cmd.apply -text "Ok" -command {C_DumpDatabase $dumpdb_filename $dumpdb_pi $dumpdb_ai $dumpdb_bi $dumpdb_ni $dumpdb_oi $dumpdb_mi $dumpdb_li $dumpdb_ti; destroy .dumpdb}
      pack .dumpdb.all.cmd.apply -side left -padx 10
      button .dumpdb.all.cmd.clear -text "Abort" -command {destroy .dumpdb}
      pack .dumpdb.all.cmd.clear -side left -padx 10
      pack .dumpdb.all.cmd -side top

      pack .dumpdb.all -padx 10 -pady 10
      bind .dumpdb.all <Destroy> {+ set dumpdb_popup 0}
   }
}

##  --------------------------------------------------------------------------
##  Handling of help popup
##
set help_popup 0

proc PopupHelp {index} {
   global help_popup helpTexts

   if {$help_popup == 0} {
      set help_popup 1
      toplevel .help
      wm title .help "Nextview EPG Help"
      wm group .help .

      # command buttons to close help window or to switch chapter
      frame  .help.cmd
      button .help.cmd.dismiss -text "Dismiss" -command {destroy .help}
      button .help.cmd.prev -text "Previous"
      button .help.cmd.next -text "Next"
      pack   .help.cmd.dismiss -side left -padx 20
      pack   .help.cmd.prev .help.cmd.next -side left
      pack   .help.cmd -side top
      bind   .help.cmd <Destroy> {+ set help_popup 0}

      frame  .help.disp
      scrollbar .help.disp.sb -orient vertical -command {.help.disp.text yview}
      pack   .help.disp.sb -fill y -anchor e -side left
      text   .help.disp.text -width 60 -wrap word -setgrid true -background #ffd030 \
                             -font {helvetica -12 normal} -spacing3 6 \
                             -yscrollcommand {.help.disp.sb set}
      pack   .help.disp.text -side left -fill both -expand 1
      pack   .help.disp -side top -fill both -expand 1
      # define tags for various nroff text formats
      .help.disp.text tag configure title -font {helvetica -16 bold} -spacing3 10
      .help.disp.text tag configure indent -lmargin1 40 -lmargin2 40
      .help.disp.text tag configure bold -font {helvetica -12 bold}
      .help.disp.text tag configure underlined -underline 1

      # allow to scroll the text with the cursor keys
      bindtags .help.disp.text {.help.disp.text . all}
      bind   .help.disp.text <Up>    {.help.disp.text yview scroll -1 unit}
      bind   .help.disp.text <Down>  {.help.disp.text yview scroll 1 unit}
      bind   .help.disp.text <Prior> {.help.disp.text yview scroll -1 pages}
      bind   .help.disp.text <Next>  {.help.disp.text yview scroll 1 pages}
      bind   .help.disp.text <Home>  {.help.disp.text yview moveto 0.0}
      bind   .help.disp.text <End>   {.help.disp.text yview moveto 1.0}
      bind   .help.disp.text <Enter> {focus %W}

   } else {
      # when the popup is already open, just exchange the text
      .help.disp.text configure -state normal
      .help.disp.text delete 1.0 end
      .help.disp.sb set 0 1
   }

   # fill the widget with the formatted text
   eval [concat .help.disp.text insert end $helpTexts($index)]
   .help.disp.text configure -state disabled

   # define/update bindings for left/right command buttons
   if {[info exists helpTexts([expr $index - 1])]} {
      .help.cmd.prev configure -command "PopupHelp [expr $index - 1]" -state normal
      bind .help.disp.text <Left> "PopupHelp [expr $index - 1]"
   } else {
      .help.cmd.prev configure -command {} -state disabled
      bind .help.disp.text <Left> {}
   }
   if {[info exists helpTexts([expr $index + 1])]} {
      .help.cmd.next configure -command "PopupHelp [expr $index + 1]" -state normal
      bind .help.disp.text <Right> "PopupHelp [expr $index + 1]"
   } else {
      .help.cmd.next configure -command {} -state disabled
      bind .help.disp.text <Right> {}
   }
}

##  --------------------------------------------------------------------------
##  Handling of the About pop-up
##
set about_popup 0

proc CreateAbout {} {
   global about_popup

   if {$about_popup == 0} {
      set about_popup 1
      toplevel .about
      wm title .about "About Nextview EPG"
      wm resizable .about 0 0
      wm group .about .
      label .about.name -text "Nextview EPG Decoder / Browser / Analyzer"
      pack .about.name -side top -pady 8
      label .about.logo -bitmap nxtv_logo
      pack .about.logo -side top -pady 8
      label .about.version -text "v0.3.3"
      pack .about.version -side top
      label .about.copyr1 -text "Copyright © 1999, 2000 by Tom Zörner"
      label .about.copyr2 -text "Tom.Zoerner@informatik.uni-erlangen.de"
      label .about.copyr3 -text "http://nxtvepg.tripod.com/" -font {courier -12 normal} -foreground blue
      pack .about.copyr1 .about.copyr2 -side top
      pack .about.copyr3 -side top -padx 10 -pady 10
      message .about.m -text {
If you publish any information that was acquired by use of this software, please do always include a note about the source of your information and where to obtain a copy of this software.

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License Version 2 as published by the Free Software Foundation. You find a copy of this license in the file COPYRIGHT in the root directory of this release.

THIS PROGRAM IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL, BUT WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
      }
      pack .about.m -side top
      bind .about.m <Destroy> {+ set about_popup 0}

      button .about.dismiss -text "Dismiss" -command {destroy .about}
      pack .about.dismiss -pady 10
   }
}

##  --------------------------------------------------------------------------
##  Handling of the Provider Selection Pop-up
##
set provwin_popup 0

proc ProvWin_Create {} {
   global provwin_popup
   global default_bg textfont
   global provwin_servicename
   global provwin_list

   if {$provwin_popup == 0} {
      set provwin_popup 1

      toplevel .provwin
      wm title .provwin "Provider Selection"
      wm resizable .provwin 0 0
      wm group .provwin .

      # list of providers at the left side of the window
      frame .provwin.n
      frame .provwin.n.b
      scrollbar .provwin.n.b.sb -orient vertical -command {.provwin.n.b.list yview}
      listbox .provwin.n.b.list -relief ridge -selectmode single -width 15 -height 5 -yscrollcommand {.provwin.n.b.sb set}
      pack .provwin.n.b.sb .provwin.n.b.list -side left -fill y
      bind .provwin.n.b.list <ButtonRelease-1> {+ C_ProvWin_Select [.provwin.n.b.list curselection]}
      bind .provwin.n.b.list <KeyRelease-space> {+ C_ProvWin_Select [.provwin.n.b.list curselection]}

      # provider info at the right side of the window
      frame .provwin.n.info
      label .provwin.n.info.serviceheader -text "Name of service"
      entry .provwin.n.info.servicename -state disabled -textvariable provwin_servicename -width 35
      pack .provwin.n.info.serviceheader .provwin.n.info.servicename -side top -anchor nw
      frame .provwin.n.info.net
      label .provwin.n.info.net.header -text "List of networks"
      text .provwin.n.info.net.list -width 35 -height 5 -wrap word \
                                    -font $textfont -background $default_bg \
                                    -selectborderwidth 0 -selectbackground $default_bg \
                                    -insertofftime 0 -exportselection false
      pack .provwin.n.info.net.header .provwin.n.info.net.list -side top -anchor nw
      pack .provwin.n.info.net -side top -anchor nw

      pack .provwin.n.b -side left -fill y
      pack .provwin.n.info -side left -fill y -padx 10

      # buttons at the bottom of the window
      frame .provwin.cmd
      button .provwin.cmd.apply -text "Ok" -command {C_ProvWin_Exit [.provwin.n.b.list curselection]; destroy .provwin}
      pack .provwin.cmd.apply -side left -padx 10
      button .provwin.cmd.clear -text "Abort" -command {C_ProvWin_Exit; destroy .provwin}
      pack .provwin.cmd.clear -side left -padx 10

      pack .provwin.n .provwin.cmd -side top -pady 10
      bind .provwin.n <Destroy> {+ set provwin_popup 0; C_ProvWin_Exit}

      # empty the list of providers
      if {[info exists provwin_list]} {
         unset provwin_list
      }

      # fill the window
      C_ProvWin_Open
   }
}

##  --------------------------------------------------------------------------
##  Create EPG scan popup-window
##
set epgscan_popup 0
set epgscan_timeout 2

proc PopupEpgScan {} {
   global epgscan_popup

   if {$epgscan_popup == 0} {
      toplevel .epgscan
      wm title .epgscan "Scan for Nextview EPG providers"
      wm resizable .epgscan 0 0
      wm group .epgscan .
      set epgscan_popup 1

      frame  .epgscan.cmd
      # control commands
      button .epgscan.cmd.start -text "Start scan" -width 12 -command C_StartEpgScan
      button .epgscan.cmd.stop -text "Abort scan" -width 12 -command C_StopEpgScan -state disabled
      button .epgscan.cmd.dismiss -text "Dismiss" -width 12 -command {destroy .epgscan}
      pack .epgscan.cmd.start .epgscan.cmd.stop .epgscan.cmd.dismiss -side top -padx 10 -pady 10
      pack .epgscan.cmd -side left

      frame .epgscan.all -relief raised -bd 2
      # progress bar
      frame .epgscan.all.baro -width 140 -height 15 -relief sunken -borderwidth 2
      pack propagate .epgscan.all.baro 0
      frame .epgscan.all.baro.bari -width 0 -height 11 -background blue
      pack propagate .epgscan.all.baro.bari 0
      pack .epgscan.all.baro.bari -padx 2 -pady 2 -side left -anchor w
      pack .epgscan.all.baro -pady 5

      # message window to inform about the scanning state
      frame .epgscan.all.fmsg
      text .epgscan.all.fmsg.msg -width 35 -height 7 -yscrollcommand {.epgscan.all.fmsg.sb set} -wrap none
      pack .epgscan.all.fmsg.msg -side left
      scrollbar .epgscan.all.fmsg.sb -orient vertical -command {.epgscan.all.fmsg.msg yview}
      pack .epgscan.all.fmsg.sb -side left -fill y
      pack .epgscan.all.fmsg -side top -padx 10 -pady 10

      pack .epgscan.all -side top -fill y -expand 1
      bind .epgscan.all <Destroy> {+ set epgscan_popup 0; C_StopEpgScan}

      .epgscan.all.fmsg.msg insert end "Press the <Start scan> button\n"

      # force input focus into the popup
      grab .epgscan
   }
}


##  --------------------------------------------------------------------------
##  Network selection popup
##
set netsel_popup 0

proc PopupNetwopSelection {} {
   global netsel_popup
   global netsel_prov netsel_selist netsel_ailist netsel_names
   global cfnetwops

   if {$netsel_popup == 0} {
      # get CNI of currently selected provider (or 0 if db is empty)
      set netsel_prov [C_GetProvCni]
      if {$netsel_prov != 0} {
         toplevel .netsel
         wm title .netsel "Network Selection"
         wm resizable .netsel 0 0
         wm group .netsel .
         set netsel_popup 1

         ## first column: listbox with all netwops in AI order
         listbox .netsel.ailist -exportselection false -setgrid true -height 27 -width 12 -selectmode extended -relief ridge
         pack .netsel.ailist -fill x -anchor nw -side left -pady 10 -padx 10 -fill y -expand 1
         bind .netsel.ailist <ButtonPress-1> {+ after idle {ButtonPressNetwopList orig}}

         ## second column: command buttons
         frame .netsel.cmd
         button .netsel.cmd.addnet -text "add" -command {AddSelectedNetwop} -width 7
         pack .netsel.cmd.addnet -side top -anchor nw -pady 10

         button .netsel.cmd.up -text "up" -command {ShiftUpSelectedNetwop} -width 7
         pack .netsel.cmd.up -side top -anchor nw
         button .netsel.cmd.down -text "down" -command {ShiftDownSelectedNetwop} -width 7
         pack .netsel.cmd.down -side top -anchor nw
         button .netsel.cmd.delnet -text "delete" -command {RemoveSelectedNetwop} -width 7
         pack .netsel.cmd.delnet -side top -anchor nw -pady 10

         button .netsel.cmd.save -text "Save" -command {SaveSelectedNetwopList} -width 7
         pack .netsel.cmd.save -side bottom -anchor sw
         button .netsel.cmd.abort -text "Abort" -command {destroy .netsel} -width 7
         pack .netsel.cmd.abort -side bottom -anchor sw

         pack .netsel.cmd -side left -anchor nw -pady 10 -padx 5 -fill y -expand 1
         bind .netsel.cmd <Destroy> {+ set netsel_popup 0}

         ## third column: selected netwops in selected order
         listbox .netsel.selist -exportselection false -setgrid true -height 27 -width 12 -selectmode extended -relief ridge
         pack .netsel.selist -fill x -anchor nw -side left -pady 10 -padx 10 -fill y -expand 1
         bind .netsel.selist <ButtonPress-1> {+ after idle {ButtonPressNetwopList sel}}

         # fetch CNI list from AI block in database
         # as a side effect this function stores all netwop names into the array netsel_names
         set netsel_ailist [C_GetAiNetwopList]
         # initialize list of user-selected CNIs
         if {[info exists cfnetwops($netsel_prov)]} {
            set netsel_selist [lindex $cfnetwops($netsel_prov) 0]
         } else {
            set netsel_selist $netsel_ailist
         }

         foreach cni $netsel_ailist {
            .netsel.ailist insert end $netsel_names($cni)
         }
         foreach cni $netsel_selist {
            .netsel.selist insert end $netsel_names($cni)
         }
         .netsel.ailist configure -height [llength $netsel_ailist]
         .netsel.selist configure -height [llength $netsel_ailist]

         # initialize button state
         # (all disabled until a netwop is selected from either the left or right list)
         after idle {ButtonPressNetwopList orig}
      } else {
         # no AI block in database
         tk_messageBox -type ok -default ok -icon error -message "Cannot configure networks without a provider selected."
      }
   }
}

# called after button press in left or right listbox
proc ButtonPressNetwopList {which} {
   if {[string equal $which "orig"]} {
      .netsel.selist selection clear 0 end
   } else {
      .netsel.ailist selection clear 0 end
   }

   if {[llength [.netsel.ailist curselection]] > 0} {
      .netsel.cmd.addnet configure -state normal
   } else {
      .netsel.cmd.addnet configure -state disabled
   }

   if {[llength [.netsel.selist curselection]] > 0} {
      .netsel.cmd.up configure -state normal
      .netsel.cmd.down configure -state normal
      .netsel.cmd.delnet configure -state normal
   } else {
      .netsel.cmd.up configure -state disabled
      .netsel.cmd.down configure -state disabled
      .netsel.cmd.delnet configure -state disabled
   }
}

# selected items in the AI CNI list are appended to the selection list
proc AddSelectedNetwop {} {
   global netsel_prov netsel_selist netsel_ailist netsel_names

   foreach index [.netsel.ailist curselection] {
      set cni [lindex $netsel_ailist $index]
      if {[lsearch -exact $netsel_selist $cni] == -1} {
         lappend netsel_selist $cni
         .netsel.selist insert end $netsel_names($cni)
      }
   }
   .netsel.selist selection clear 0 end
}

# all selected items are removed from the list
proc RemoveSelectedNetwop {} {
   global netsel_prov netsel_selist netsel_ailist netsel_names

   foreach index [lsort -integer -decreasing [.netsel.selist curselection]] {
      .netsel.selist delete $index
      set netsel_selist [lreplace $netsel_selist $index $index]
   }
   .netsel.ailist selection clear 0 end
}

# move all selected items up by one row
# - the selected items may be non-consecutive
# - the first row must not be selected
proc ShiftUpSelectedNetwop {} {
   global netsel_prov netsel_selist netsel_ailist netsel_names

   set el [lsort -integer -increasing [.netsel.selist curselection]]
   if {[lindex $el 0] > 0} {
      foreach index $el {
         .netsel.selist delete [expr $index - 1]
         .netsel.selist insert $index $netsel_names([lindex $netsel_selist [expr $index - 1]])
         set netsel_selist [lreplace $netsel_selist [expr $index - 1] $index \
                                   [lindex $netsel_selist $index] \
                                   [lindex $netsel_selist [expr $index - 1]]]
      }
   }
   .netsel.ailist selection clear 0 end
}

# move all selected items down by one row
proc ShiftDownSelectedNetwop {} {
   global netsel_prov netsel_selist netsel_ailist netsel_names

   set el [lsort -integer -decreasing [.netsel.selist curselection]]
   if {[lindex $el 0] < [expr [llength $netsel_selist] - 1]} {
      foreach index $el {
         .netsel.selist delete [expr $index + 1]
         .netsel.selist insert $index $netsel_names([lindex $netsel_selist [expr $index + 1]])
         set netsel_selist [lreplace $netsel_selist $index [expr $index + 1] \
                                   [lindex $netsel_selist [expr $index + 1]] \
                                   [lindex $netsel_selist $index]]
      }
   }
   .netsel.ailist selection clear 0 end
}

# finished -> save and apply the user selection
proc SaveSelectedNetwopList {} {
   global netsel_prov netsel_selist netsel_ailist netsel_names
   global cfnetwops

   # make list of CNIs from AI which are missing in the user selection
   set sup {}
   foreach cni $netsel_ailist {
      if {[lsearch -exact $netsel_selist $cni] == -1} {
         lappend sup $cni
      }
   }
   set cfnetwops($netsel_prov) [list $netsel_selist $sup]

   # save list into rc/ini file
   UpdateRcFile
   after idle C_UpdateNetwopList

   # close popup
   destroy .netsel
}

## ---------------------------------------------------------------------------
## Update netwop filter bar according to filter selection
## - note: all CNIs have to be in the format 0x%04X
##
proc UpdateNetwopFilterBar {prov} {
   global netsel_names cfnetwops netwop_map netselmenu

   # fetch CNI list from AI block in database
   # as a side effect this function stores all netwop names into the array netsel_names
   if {[array exists netsel_names]} { unset netsel_names }
   set ailist [C_GetAiNetwopList]
   # initialize list of user-selected CNIs
   if {[info exists cfnetwops($prov)]} {
      set selist [lindex $cfnetwops($prov) 0]
      set suplist [lindex $cfnetwops($prov) 1]
   } else {
      set selist $ailist
      set suplist {}
   }

   if {[info exists netwop_map]} { unset netwop_map }
   set index 0
   foreach cni $ailist {
      set order($cni) $index
      incr index
   }

   .all.netwops.list delete 1 end
   .all.netwops.list selection set 0
   .menubar.filter.netwops delete 1 end
   set nlidx 0
   foreach cni $selist {
      if {[info exists netsel_names($cni)]} {
         # CNI still exists in actual AI -> add to filter bar
         .all.netwops.list insert end $netsel_names($cni)
         .menubar.filter.netwops add checkbutton -label $netsel_names($cni) -variable netselmenu($nlidx) -command [list SelectNetwopMenu $nlidx]
         set netwop_map($nlidx) $order($cni)
         unset netsel_names($cni)
      } else {
         # CNI no longer exists -> remove from selection, do not add to filter bar
         set selist [lreplace $selist $nlidx $nlidx]
      }
      incr nlidx
   }

   set index 0
   foreach cni $suplist {
      if {[info exists netsel_names($cni)]} {
         unset netsel_names($cni)
      } else {
         # CNI no longer exists -> remove from suppressed list
         set suplist [lreplace $suplist $index $index]
      }
      incr index
   }

   foreach cni [array names netsel_names] {
      lappend selist $netsel_names($cni)
      .all.netwops.list insert end $netsel_names($cni)
      .menubar.filter.netwops add checkbutton -label $netsel_names($cni) -variable netselmenu($nlidx) -command [list SelectNetwopMenu $nlidx]
      set netwop_map($nlidx) $order($cni)
      incr nlidx
   }
   .all.netwops.list configure -height [expr [llength $selist] + 1]

   set cfnetwops($prov) [list $selist $suplist]
   UpdateRcFile

   # return list of indices of suppressed netwops
   set supidx {}
   foreach cni $suplist {
      lappend supidx $order($cni)
   }
   return $supidx
}


## ---------------------------------------------------------------------------
## Create help text header inside text listbox
##
proc PiListBox_PrintHelpHeader {text} {

   .all.pi.list.text delete 1.0 end

   if {[string length [info command .all.pi.list.text.nxtvlogo]] > 0} {
      destroy .all.pi.list.text.nxtvlogo
   }
   button .all.pi.list.text.nxtvlogo -bitmap nxtv_logo -relief flat

   .all.pi.list.text tag configure centerTag -justify center
   .all.pi.list.text tag configure bold24Tag -font {helvetica -24 bold} -spacing1 15 -spacing3 10
   .all.pi.list.text tag configure bold16Tag -font {helvetica -16 bold} -spacing3 10
   .all.pi.list.text tag configure bold12Tag -font {helvetica -12 bold}
   .all.pi.list.text tag configure wrapTag   -wrap word
   .all.pi.list.text tag configure redTag    -background #ffff50

   .all.pi.list.text insert end "Nextview EPG\n" bold24Tag
   .all.pi.list.text insert end "An Electronic TV Programme Guide for Your PC\n" bold16Tag
   .all.pi.list.text window create end -window .all.pi.list.text.nxtvlogo
   .all.pi.list.text insert end "\n\nCopyright © 1999, 2000 by Tom Zörner\n" bold12Tag
   .all.pi.list.text insert end "Tom.Zoerner@informatik.uni-erlangen.de\n\n" bold12Tag
   .all.pi.list.text tag add centerTag 1.0 {end - 1 lines}
   .all.pi.list.text insert end "This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License Version 2 as published by the Free Software Foundation. This program is distributed in the hope that it will be useful, but without any warranty. See the GPL2 for more details.\n\n" wrapTag

   .all.pi.list.text insert end $text {wrapTag redTag}
}

## ---------------------------------------------------------------------------
##                 S T A T I S T I C S   W I N D O W S
## ---------------------------------------------------------------------------

## ---------------------------------------------------------------------------
## Create the timescale for one network
##
proc TimeScale_Create {frame netwopName} {
   frame $frame
   set default_bg [$frame cget -background]

   label $frame.name -text $netwopName -anchor w
   pack $frame.name -side left -fill x -expand true

   frame $frame.now
   for {set i 0} {$i < 5} {incr i} {
      frame $frame.now.n$i -width 4 -height 8 -bg black
      pack $frame.now.n$i -side left -padx 1
   }
   pack $frame.now -side left -padx 5

   canvas $frame.stream -bg $default_bg -height 12 -width 256
   pack $frame.stream -side left
   $frame.stream create rect 0 4 258 12 -outline "" -fill black

   pack $frame -fill x -expand true
}

## ---------------------------------------------------------------------------
## Display the timespan covered by a PI in its network timescale
##
proc TimeScale_AddPi {frame pos1 pos2 color hasShort hasLong} {
   set y0 4
   set y1 12
   if {$hasShort} {set y0 [expr $y0 - 2]}
   if {$hasLong}  {set y1 [expr $y1 + 2]}
   $frame.stream create rect $pos1 $y0 $pos2 $y1 -fill $color -outline ""
}

## ---------------------------------------------------------------------------
## Mark a network label for which a PI was received
##
proc TimeScale_MarkNow {frame num color} {
   $frame.now.n$num config -bg $color
}

## ---------------------------------------------------------------------------
## Create the acq stats window with histogram and summary text
##
set acqstatsfont     -*-courier-medium-r-*-*-12-*-*-*-*-*-iso8859-1

proc AcqStat_Create {} {
   global acqstatsfont

   toplevel .acqstat
   wm title .acqstat {Nextview acquisition statistics}
   wm resizable .acqstat 0 0
   wm group .acqstat .

   canvas .acqstat.hist -bg white -height 128 -width 128
   pack .acqstat.hist -side left

   message .acqstat.statistics -font $acqstatsfont -aspect 2000 -justify left -anchor nw
   pack .acqstat.statistics -expand 1 -fill both -side left
   #set width [$frame.stat cget -width]
   #$frame.stat create line 0 64 $width 64 

   # inform the control code when the window is destroyed
   bind .acqstat <Destroy> {+ C_StatsWin_ToggleStatistics 0}
}

## ---------------------------------------------------------------------------
## Add a line to the history graph
##
proc AcqStat_AddHistory {pos valEx val1c val1o val2c val2o} {

   .acqstat.hist addtag "TBDEL" enclosed $pos 0 [expr $pos + 2] 128
   catch [ .acqstat.hist delete "TBDEL" ]

   if {$valEx > 0} {
      .acqstat.hist create line $pos 128 $pos [expr 128 - $valEx] -fill yellow
   }
   if {$val1c > $valEx} {
      .acqstat.hist create line $pos [expr 128 - $valEx] $pos [expr 128 - $val1c] -fill red
   }
   if {$val1o > $val1c} {
      .acqstat.hist create line $pos [expr 128 - $val1c] $pos [expr 128 - $val1o] -fill #A52A2A
   }
   if {$val2c > $val1o} {
      .acqstat.hist create line $pos [expr 128 - $val1o] $pos [expr 128 - $val2c] -fill blue
   }
   if {$val2o > $val2c} {
      .acqstat.hist create line $pos [expr 128 - $val2c] $pos [expr 128 - $val2o] -fill #483D8B
   }
}

## ---------------------------------------------------------------------------
## Clear the histogram (e.g. after provider change)
##
proc AcqStat_ClearHistory {} {
   # the tag "all" is automatically assigned to every item in the canvas
   catch [ .acqstat.hist delete all ]
}


## ---------------------------------------------------------------------------
##                    F I L T E R   S H O R T C U T S
## ---------------------------------------------------------------------------

set fsc_name_idx 0
set fsc_mask_idx 1
set fsc_filt_idx 2
set fsc_logi_idx 3

##
##  Predefined filter shortcuts
##
proc PreloadShortcuts {} {
   global shortcuts shortcut_count

   set shortcuts(0)  {spielfilme themes {theme_class1 16} merge}
   set shortcuts(1)  {sport themes {theme_class1 64} merge}
   set shortcuts(2)  {serien themes {theme_class1 128} merge}
   set shortcuts(3)  {kinder themes {theme_class1 80} merge}
   set shortcuts(4)  {shows themes {theme_class1 48} merge}
   set shortcuts(5)  {news themes {theme_class1 32} merge}
   set shortcuts(6)  {sozial themes {theme_class1 37} merge}
   set shortcuts(7)  {wissenschaft themes {theme_class1 86} merge}
   set shortcuts(8)  {hobby themes {theme_class1 52} merge}
   set shortcuts(9)  {musik themes {theme_class1 96} merge}
   set shortcuts(10) {kultur themes {theme_class1 112} merge}
   set shortcuts(11) {erotik themes {theme_class1 24} merge}
   set shortcut_count 12
}

##
##  Process list of theme filters: convert to decimal and expand ranges
##
#proc ProcessThemeSpec {tlist} {
#   set result {}
#   foreach theme $tlist {
#      if {[scan $theme "%d-%d" start stop]==2 || [scan $theme "0x%x-0x%x" start stop]==2} {
#         for {} {$start <= $stop} {incr start} {
#            lappend result $start
#         }
#      } else {
#         lappend result [expr $theme + 0]
#      }
#   }
#   return $result
#}

##  --------------------------------------------------------------------------
##  Check if shortcut should be deselected after manual filter modification
##
proc CheckShortcutDeselection {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx
   global shortcuts shortcut_count
   global parental_rating editorial_rating
   global theme_sel theme_class_sel current_theme_class theme_class_count
   global sortcrit_class sortcrit_class_sel
   global series_sel
   global feature_class_count feature_class_mask feature_class_value
   global progidx_first progidx_last filter_progidx
   global substr_pattern substr_grep_title substr_grep_descr substr_ignore_case
   global timsel_enabled timsel_start timsel_stop timsel_date timsel_relative timsel_absstop
   global fsc_prevselection

   foreach index [.all.shortcuts.list curselection] {
      set undo 0
      foreach {ident valist} [lindex $shortcuts($index) $fsc_filt_idx] {
         switch -glob $ident {
            theme_class* {
               scan $ident "theme_class%d" class
               if {$class == $current_theme_class} {
                  foreach theme $valist {
                     if {![info exists theme_sel($theme)] || ($theme_sel($theme) == 0)} {
                        set undo 1
                        break
                     }
                  }
               } elseif {[info exists theme_class_sel($class)]} {
                  foreach theme $valist {
                     if {[lsearch -exact $theme_class_sel($class) $theme] == -1} {
                        set undo 1
                        break
                     }
                  }
               } else {
                  set undo 1
               }
            }
            sortcrit_class* {
               scan $ident "sortcrit_class%d" class
               if {$class == $current_sortcrit_class} {
                  foreach sortcrit $valist {
                     if {![info exists sortcrit_sel($sortcrit)] || ($sortcrit_sel($sortcrit) == 0)} {
                        set undo 1
                        break
                     }
                  }
               } elseif {[info exists sortcrit_class_sel($class)]} {
                  foreach sortcrit $valist {
                     if {[lsearch -exact $sortcrit_class_sel($class) $sortcrit] == -1} {
                        set undo 1
                        break
                     }
                  }
               } else {
                  set undo 1
               }
            }
            series {
               foreach series $valist {
                  if {![info exists series_sel($series)] || ($series_sel($series) == 0)} {
                     set undo 1
                     break
                  }
               }
            }
            netwops {
               if {[.all.netwops.list selection includes 0]} {
                  set undo 1
               } else {
                  # check if all netwops from the shortcut are still enabled in the filter
                  set selcnis [C_GetNetwopFilterList]
                  foreach cni $valist {
                     if {[lsearch -exact $selcnis $cni] == -1} {
                        set undo 1
                        break
                     }
                  }
               }
            }
            features {
               foreach {mask value} $valist {
                  # search for a matching mask/value pair
                  for {set class 1} {$class <= $feature_class_count} {incr class} {
                     if {($feature_class_mask($class) & $mask) == $mask} {
                        if {($feature_class_value($class) & $mask) == $value} {
                           break
                        }
                     }
                  }
                  if {$class > $feature_class_count} {
                     set undo 1
                     break
                  }
               }
            }
            parental {
               set undo [expr ($valist < $parental_rating) || ($parental_rating == 0)]
            }
            editorial {
               set undo [expr ($valist > $editorial_rating) || ($editorial_rating == 0)]
            }
            progidx {
               set undo [expr ($filter_progidx == 0) || \
                              ($progidx_first > [lindex $valist 0]) || \
                              ($progidx_last < [lindex $valist 1]) ]
            }
            timsel {
               set undo [expr ($timsel_enabled  != 1) || \
                              ($timsel_relative != [lindex $valist 0]) || \
                              ($timsel_absstop  != [lindex $valist 1]) || \
                              ($timsel_start    != [lindex $valist 2]) || \
                              ($timsel_stop     != [lindex $valist 3]) || \
                              ($timsel_date     != [lindex $valist 4]) ]
            }
            substr {
               set undo [expr ($substr_grep_title != [lindex $valist 0]) || \
                              ($substr_grep_descr != [lindex $valist 1]) || \
                              ($substr_ignore_case != [lindex $valist 2]) || \
                              ![string equal $substr_pattern [lindex $valist 3]] ]
            }
         }
         if {$undo} {
            .all.shortcuts.list selection clear $index
            break
         }
      }
   }
}

##  --------------------------------------------------------------------------
##  Callback for button-release on shortcuts listbox
##
proc SelectShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx
   global shortcuts shortcut_count
   global parental_rating editorial_rating
   global theme_sel theme_class_sel current_theme_class theme_class_count
   global sortcrit_class sortcrit_class_sel
   global series_sel
   global feature_class_count feature_class_mask feature_class_value
   global progidx_first progidx_last filter_progidx
   global substr_pattern substr_grep_title substr_grep_descr substr_ignore_case
   global timsel_enabled timsel_start timsel_stop timsel_date timsel_relative timsel_absstop
   global fsc_prevselection

   # determine which shortcuts are no longer selected
   set fsc_curselection [.all.shortcuts.list curselection]
   if {[info exists fsc_prevselection]} {
      foreach index $fsc_prevselection {
         set deleted($index) 1
      }
      foreach index $fsc_curselection {
         if {[info exists deleted($index)]} {
            unset deleted($index)
         }
      }
   }
   set fsc_prevselection $fsc_curselection

   # reset all filters in the combined mask
   foreach index $fsc_curselection {
      foreach index [lindex $shortcuts($index) $fsc_mask_idx] {
         # reset filter menu state; remember which types were reset
         switch -exact $index {
            themes     {ResetThemes;          set reset(themes) 1}
            sortcrits  {ResetSortCrits;       set reset(sortcrits) 1}
            features   {ResetFeatures;        set reset(features) 1}
            series     {ResetSeries;          set reset(series) 1}
            parental   {ResetParentalRating;  set reset(parental) 1}
            editorial  {ResetEditorialRating; set reset(editorial) 1}
            progidx    {ResetProgIdx;         set reset(progidx) 1}
            timsel     {ResetTimSel;          set reset(timsel) 1}
            substr     {ResetSubstr;          set reset(substr) 1}
            netwops    {ResetNetwops;         set reset(netwops) 1}
         }
         # disable the filter in the filter context
         C_ResetFilter $index
      }
   }

   # clear all filters of deselected shortcuts
   foreach index [array names deleted] {
      foreach {ident valist} [lindex $shortcuts($index) $fsc_filt_idx] {
         if {![info exists reset($ident)]} {
            switch -glob $ident {
               theme_class*   {
                  scan $ident "theme_class%d" class
                  if {[info exists tcdesel($class)]} {
                     set tcdesel($class) [concat $tcdesel($class) $valist]
                  } else {
                     set tcdesel($class) $valist
                  }
               }
               sortcrit_class*   {
                  scan $ident "sortcrit_class%d" class
                  foreach item $valist {
                     set index [lsearch -exact $sortcrit_class_sel($class) $item]
                     if {$index != -1} {
                        set sortcrit_class_sel($class) [lreplace $sortcrit_class_sel($class) $index $index]
                     }
                  }
               }
               features {
                  foreach {mask value} $valist {
                     for {set class 1} {$class <= $feature_class_count} {incr class} {
                        if {($feature_class_mask($class) == $mask) && ($feature_class_value($class) == $value)} {
                           set feature_class_mask($class)  0
                           set feature_class_value($class) 0
                        }
                     }
                  }
               }
               parental {
                  set parental_rating 0
                  C_SelectParentalRating 0
               }
               editorial {
                  set editorial_rating 0
                  C_SelectEditorialRating 0
               }
               substr {
                  set substr_pattern {}
                  C_SelectSubStr 0 0 0 {}
               }
               progidx {
                  set filter_progidx 0
                  C_SelectProgIdx
               }
               timsel {
                  set timsel_enabled 0
                  C_SelectStartTime
               }
               series {
                  foreach index $valist {
                     set series_sel($index) 0
                     C_SelectSeries $index 0
                  }
               }
               netwops {
                  if {[info exists netdesel]} {
                     set netdesel [concat $netdesel $valist]
                  } else {
                     set netdesel $valist
                  }
               }
               default {
               }
            }
         }
      }
   }

   # set all filters in all selected shortcuts (merge)
   foreach index $fsc_curselection {
      foreach {ident valist} [lindex $shortcuts($index) $fsc_filt_idx] {
         switch -glob $ident {
            theme_class*   {
               scan $ident "theme_class%d" class
               if {[info exists tcsel($class)]} {
                  set tcsel($class) [concat $tcsel($class) $valist]
               } else {
                  set tcsel($class) $valist
               }
            }
            sortcrit_class*   {
               scan $ident "sortcrit_class%d" class
               set sortcrit_class_sel($class) [lsort -int [concat $sortcrit_class_sel($class) $valist]]
            }
            features {
               foreach {mask value} $valist {
                  # search the first unused class; if none found, drop the filter
                  for {set class 1} {$class <= $feature_class_count} {incr class} {
                     if {$feature_class_mask($class) == 0} break
                  }
                  if {$class <= $feature_class_count} {
                     set feature_class_mask($class)  $mask
                     set feature_class_value($class) $value
                  }
               }
            }
            parental {
               set rating [lindex $valist 0]
               if {($rating < $parental_rating) || ($parental_rating == 0)} {
                  set parental_rating $rating
                  C_SelectParentalRating $parental_rating
               }
            }
            editorial {
               set rating [lindex $valist 0]
               if {($rating > $editorial_rating) || ($editorial_rating == 0)} {
                  set editorial_rating [lindex $valist 0]
                  C_SelectEditorialRating $editorial_rating
               }
            }
            substr {
               set substr_grep_title  [lindex $valist 0]
               set substr_grep_descr  [lindex $valist 1]
               set substr_ignore_case [lindex $valist 2]
               set substr_pattern     [lindex $valist 3]
               C_SelectSubStr $substr_grep_title $substr_grep_descr $substr_ignore_case $substr_pattern
            }
            progidx {
               if {($progidx_first > [lindex $valist 0]) || ($filter_progidx == 0)} {
                  set progidx_first [lindex $valist 0]
               }
               if {($progidx_last < [lindex $valist 1]) || ($filter_progidx == 0)} {
                  set progidx_last  [lindex $valist 1]
               }
               C_SelectProgIdx $progidx_first $progidx_last
               UpdateProgIdxMenuState
            }
            timsel {
               set timsel_enabled  1
               set timsel_relative [lindex $valist 0]
               set timsel_absstop  [lindex $valist 1]
               set timsel_start    [lindex $valist 2]
               set timsel_stop     [lindex $valist 3]
               set timsel_date     [lindex $valist 4]
               C_SelectStartTime $timsel_relative $timsel_absstop $timsel_start $timsel_stop $timsel_date
            }
            series {
               foreach series $valist {
                  set series_sel($series) 1
                  C_SelectSeries $series 1
               }
            }
            netwops {
               if {[info exists netsel]} {
                  set netsel [concat $netsel $valist]
               } else {
                  set netsel $valist
               }
            }
            default {
            }
         }
      }
   }

   # set the collected themes filter
   for {set class 1} {$class < $theme_class_count} {incr class} {
      if {$class == $current_theme_class} {
         if {[info exists tcdesel($class)]} {
            foreach theme $tcdesel($class) {
               set theme_sel($theme) 0
            }
         }
         if {[info exists tcsel($class)]} {
            foreach theme $tcsel($class) {
               set theme_sel($theme) 1
            }
         }
         set theme_class_sel($class) {}
         foreach {index value} [array get theme_sel] {
            if {$value != 0} {
               set theme_class_sel($class) [concat $theme_class_sel($class) $index]
            }
         }
      } else {
         if {[info exists tcsel($class)]} {
            set theme_class_sel($class) $tcsel($class)
            # XXX TODO: substract tcdesel
         }
      }
      C_SelectThemes $class $theme_class_sel($class)
   }

   # unset/set the collected netwop filters
   if {[info exists netdesel] || [info exists netsel]} {
      # get previous netwop filter state
      if {[.all.netwops.list selection includes 0]} {
         set selcnis {}
      } else {
         set selcnis [C_GetNetwopFilterList]
      }
      # remove deselected CNIs from filter state
      if {[info exists netdesel]} {
         foreach netwop $netdesel {
            set index [lsearch -exact $selcnis $netwop]
            if {$index >= 0} {
               set selcnis [lreplace $selcnis $index $index]
            }
         }
      }
      # append newly selected CNIs
      if {[info exists netsel]} {
         set selcnis [concat $selcnis $netsel]
      }
      # convert CNIs to netwop indices
      set all {}
      set index 0
      foreach cni [C_GetAiNetwopList] {
         if {[lsearch -exact $selcnis $cni] != -1} {
            lappend all $index
         }
         incr index
      }
      # set the new filter and menu state
      C_SelectNetwops $all
      UpdateNetwopMenuState $all
   }

   # set or unset the sortcrit filters
   for {set class 1} {$class < $theme_class_count} {incr class} {
      C_SelectSortCrits $class $sortcrit_class_sel($class)
   }

   # set the collected feature filters
   set all {}
   for {set class 1} {$class <= $feature_class_count} {incr class} {
      if {[expr $feature_class_mask($class) != 0]} {
         lappend all $feature_class_mask($class) $feature_class_value($class)
      }
   }
   UpdateFeatureMenuState
   C_SelectFeatures $all

   # disable series filter when all deselected
   if {[info exists series_sel]} {
      set clearSeries 1
      foreach index [array names series_sel] {
         if {$series_sel($index) == 1} {
            set clearSeries 0
            break
         }
      }
      if {$clearSeries} {
         C_SelectSeries
      }
   }

   # finally display the PI selected by the new filter setting
   C_RefreshPiListbox
}

##  --------------------------------------------------------------------------
##  Generate a list that describes all current filter settings
##
proc DescribeCurrentFilter {} {
   global feature_class_mask feature_class_value
   global parental_rating editorial_rating
   global theme_class_count current_theme_class theme_sel theme_class_sel
   global sortcrit_class sortcrit_class_sel
   global series_sel
   global feature_class_count current_feature_class
   global substr_grep_title substr_grep_descr substr_ignore_case substr_pattern
   global filter_progidx progidx_first progidx_last
   global fscedit_desc fscedit_mask
   global timsel_enabled timsel_start timsel_stop timsel_date timsel_relative timsel_absstop

   # save the setting of the current theme class into the array
   set all {}
   foreach {index value} [array get theme_sel] {
      if {[expr $value != 0]} {
         lappend all $index
      }
   }
   set theme_class_sel($current_theme_class) $all

   set all {}
   if {[info exists fscedit_mask]} {unset fscedit_mask}

   # dump all theme classes
   for {set class 1} {$class <= $theme_class_count} {incr class} {
      if {[string length $theme_class_sel($class)] > 0} {
         lappend all "theme_class$class" $theme_class_sel($class)
         set fscedit_mask(themes) 1
      }
   }

   # dump all sortcrit classes
   for {set class 1} {$class <= $theme_class_count} {incr class} {
      if {[string length $sortcrit_class_sel($class)] > 0} {
         lappend all "sortcrit_class$class" $sortcrit_class_sel($class)
         set fscedit_mask(sortcrits) 1
      }
   }

   # dump feature filter state
   set temp {}
   for {set class 1} {$class <= $feature_class_count} {incr class} {
      if {[expr $feature_class_mask($class) != 0]} {
         lappend temp $feature_class_mask($class) $feature_class_value($class)
      }
   }
   if {[string length $temp] > 0} {
      lappend all "features" $temp
      set fscedit_mask(features) 1
   }

   if {$parental_rating > 0} {
      lappend all "parental" $parental_rating
      set fscedit_mask(parental) 1
   }
   if {$editorial_rating > 0} {
      lappend all "editorial" $editorial_rating
      set fscedit_mask(editorial) 1
   }

   # dump text substring filter state
   if {[string length $substr_pattern] > 0} {
      lappend all "substr" [list $substr_grep_title $substr_grep_descr $substr_ignore_case $substr_pattern]
      set fscedit_mask(substr) 1
   }

   # dump program index filter state
   if {$filter_progidx > 0} {
      lappend all "progidx" [list $progidx_first $progidx_last]
      set fscedit_mask(progidx) 1
   }

   # dump series array
   set temp {}
   foreach index [array names series_sel] {
      if {$series_sel($index) != 0} {
         lappend temp $index
      }
   }
   if {[string length $temp] > 0} {
      lappend all "series" $temp
      set fscedit_mask(series) 1
   }

   # dump start time filter
   if {$timsel_enabled} {
      lappend all "timsel" [list $timsel_relative $timsel_absstop $timsel_start $timsel_stop $timsel_date]
      set fscedit_mask(timsel) 1
   }

   # dump CNIs of selected netwops
   # - Upload filters from filter context, so that netwops that are not in the current
   #   netwop bar can be saved too (might have been set through the Navigate menu).
   set temp [C_GetNetwopFilterList]
   if {[llength $temp] > 0} {
      lappend all "netwops" $temp
      set fscedit_mask(netwops) 1
   }

   return $all
}

##  --------------------------------------------------------------------------
##  Popup window to add the current filter setting as new shortcut
##
proc AddFilterShortcut {} {
   global shortcuts shortcut_count
   global fscedit_desc fscedit_mask
   global fscedit_sclist fscedit_count
   global fscedit_popup

   if {$fscedit_popup == 0} {

      # copy the shortcuts into a temporary array
      if {[info exists fscedit_sclist]} {unset fscedit_sclist}
      array set fscedit_sclist [array get shortcuts]
      set fscedit_count $shortcut_count

      # determine current filter settings and a default mask
      set filt [DescribeCurrentFilter]
      set mask {}
      foreach index [array names fscedit_mask] {
         if {$fscedit_mask($index) != 0} {
            lappend mask $index
         }
      }
      set name "shortcut #$fscedit_count"

      set fscedit_sclist($fscedit_count) [list $name $mask $filt merge]
      incr fscedit_count

      if {[string length $mask] == 0} {
         set answer [tk_messageBox -type okcancel -icon warning -message "Currently no filters selected. Do you still want to continue?"]
         if {[string compare $answer "cancel"] == 0} {
            return
         }
      }

      PopupFilterShortcuts
      # select the new entry in the listbox
      .fscedit.list selection set [expr $fscedit_count - 1]
      SelectEditedShortcut
   }
}

##  --------------------------------------------------------------------------
##  Popup window to edit the shortcut list
##
proc EditFilterShortcuts {} {
   global shortcuts shortcut_count
   global fscedit_desc fscedit_mask
   global fscedit_sclist fscedit_count
   global fscedit_popup

   if {$fscedit_popup == 0} {

      # copy the shortcuts into a temporary array
      if {[info exists fscedit_sclist]} {unset fscedit_sclist}
      array set fscedit_sclist [array get shortcuts]
      set fscedit_count $shortcut_count

      PopupFilterShortcuts
      # select the first listbox entry
      .fscedit.list selection set 0
      SelectEditedShortcut
   }
}

##  --------------------------------------------------------------------------
##  Edit filter shortcut pop-up window
##
set fscedit_label ""
set fscedit_popup 0

proc PopupFilterShortcuts {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx
   global fscedit_sclist fscedit_count
   global fscedit_popup fscedit_label

   if {$fscedit_popup == 0} {
      toplevel .fscedit
      wm title .fscedit "Edit shortcuts"
      wm resizable .fscedit 0 0
      wm group .fscedit .
      set fscedit_popup 1

      ## first column: listbox with all shortcut labels
      listbox .fscedit.list -exportselection false -setgrid true -height 12 -width 12 -selectmode single -relief ridge
      bind .fscedit.list <ButtonRelease-1> {+ SelectEditedShortcut}
      bind .fscedit.list <KeyRelease-space> {+ SelectEditedShortcut}
      pack .fscedit.list -fill x -anchor nw -side left -pady 10 -padx 10 -fill y -expand 1

      ## second column: command buttons and entry widget to rename shortcuts
      frame .fscedit.cmd
      entry .fscedit.cmd.label -textvariable fscedit_label -width 15
      pack .fscedit.cmd.label -side top -anchor nw
      bind .fscedit.cmd.label <Enter> {focus %W}
      bind .fscedit.cmd.label <Return> {+ UpdateEditedShortcut}
      focus .fscedit.cmd.label
      button .fscedit.cmd.rename -text "update" -command {UpdateEditedShortcut} -width 7
      pack .fscedit.cmd.rename -side top -anchor nw -pady 10

      button .fscedit.cmd.up -text "up" -command {ShiftUpEditedShortcut} -width 7
      pack .fscedit.cmd.up -side top -anchor nw
      button .fscedit.cmd.down -text "down" -command {ShiftDownEditedShortcut} -width 7
      pack .fscedit.cmd.down -side top -anchor nw
      button .fscedit.cmd.delete -text "delete" -command {DeleteEditedShortcut} -width 7
      pack .fscedit.cmd.delete -side top -anchor nw -pady 10

      button .fscedit.cmd.save -text "Save" -command {SaveEditedShortcuts} -width 7
      pack .fscedit.cmd.save -side bottom -anchor sw
      button .fscedit.cmd.abort -text "Abort" -command {destroy .fscedit} -width 7
      pack .fscedit.cmd.abort -side bottom -anchor sw

      pack .fscedit.cmd -side left -anchor nw -pady 10 -padx 5 -fill y -expand 1
      bind .fscedit.cmd <Destroy> {+ set fscedit_popup 0}

      ## third column: shortcut flags
      frame .fscedit.flags
      label .fscedit.flags.lm -text "Filter mask:"
      pack .fscedit.flags.lm -side top -anchor w
      frame .fscedit.flags.mask -relief ridge -bd 1
      frame .fscedit.flags.mask.one
      checkbutton .fscedit.flags.mask.one.features  -text "Features" -variable fscedit_mask(features)
      checkbutton .fscedit.flags.mask.one.parental  -text "Parental rating" -variable fscedit_mask(parental)
      checkbutton .fscedit.flags.mask.one.editorial -text "Editorial rating" -variable fscedit_mask(editorial)
      checkbutton .fscedit.flags.mask.one.progidx   -text "Program index" -variable fscedit_mask(progidx)
      checkbutton .fscedit.flags.mask.one.timsel    -text "Start time" -variable fscedit_mask(timsel)
      pack .fscedit.flags.mask.one.features  -side top -anchor nw
      pack .fscedit.flags.mask.one.parental  -side top -anchor nw
      pack .fscedit.flags.mask.one.editorial -side top -anchor nw
      pack .fscedit.flags.mask.one.progidx   -side top -anchor nw
      pack .fscedit.flags.mask.one.timsel    -side top -anchor nw
      pack .fscedit.flags.mask.one -side left -anchor nw -padx 5

      frame .fscedit.flags.mask.two
      checkbutton .fscedit.flags.mask.two.themes    -text "Themes" -variable fscedit_mask(themes)
      checkbutton .fscedit.flags.mask.two.series    -text "Series" -variable fscedit_mask(series)
      checkbutton .fscedit.flags.mask.two.sortcrit  -text "Sorting Criteria" -variable fscedit_mask(sortcrits)
      checkbutton .fscedit.flags.mask.two.netwops   -text "Networks" -variable fscedit_mask(netwops)
      checkbutton .fscedit.flags.mask.two.substr    -text "Text search" -variable fscedit_mask(substr)
      pack .fscedit.flags.mask.two.themes    -side top -anchor nw
      pack .fscedit.flags.mask.two.series    -side top -anchor nw
      pack .fscedit.flags.mask.two.sortcrit  -side top -anchor nw
      pack .fscedit.flags.mask.two.netwops   -side top -anchor nw
      pack .fscedit.flags.mask.two.substr    -side top -anchor nw
      pack .fscedit.flags.mask.two -side left -anchor nw -padx 5
      pack .fscedit.flags.mask -side top -pady 10 -fill x

      label .fscedit.flags.ll -text "Combination with other shortcuts:"
      pack .fscedit.flags.ll -side top -anchor w
      frame .fscedit.flags.logic -relief ridge -bd 1
      radiobutton .fscedit.flags.logic.merge -text "merge" -variable fscedit_logic -value "merge"
      radiobutton .fscedit.flags.logic.or -text "logical OR" -variable fscedit_logic -value "or" -state disabled
      radiobutton .fscedit.flags.logic.and -text "logical AND" -variable fscedit_logic -value "and" -state disabled
      pack .fscedit.flags.logic.merge .fscedit.flags.logic.or .fscedit.flags.logic.and -side top -anchor w -padx 5
      pack .fscedit.flags.logic -side top -pady 10 -fill x
      pack .fscedit.flags -side left -anchor nw -pady 10 -padx 10

      # fill the listbox with all shortcut labels
      for {set index 0} {$index < $fscedit_count} {incr index} {
         .fscedit.list insert end [lindex $fscedit_sclist($index) $fsc_name_idx]
      }
      .fscedit.list configure -height $fscedit_count
   }
}

proc SaveEditedShortcuts {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx
   global fscedit_sclist fscedit_count
   global shortcuts shortcut_count

   # copy temporary array back into the shortcuts array
   unset shortcuts
   array set shortcuts [array get fscedit_sclist]
   set shortcut_count $fscedit_count

   # close the popup window
   destroy .fscedit

   # save the shortcuts config into the rc/ini file
   UpdateRcFile

   # update the shortcut listbox
   .all.shortcuts.list delete 0 end
   for {set index 0} {$index < $shortcut_count} {incr index} {
      .all.shortcuts.list insert end [lindex $shortcuts($index) $fsc_name_idx]
   }
   .all.shortcuts.list configure -height $shortcut_count
}

proc SelectEditedShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx
   global fscedit_label fscedit_mask fscedit_logic
   global fscedit_sclist fscedit_count

   set sel [.fscedit.list curselection]
   if {([string length $sel] > 0) && ($sel < $fscedit_count)} {
      # set label displayed in entry widget
      set fscedit_label [lindex $fscedit_sclist($sel) $fsc_name_idx]
      .fscedit.cmd.label selection range 0 end

      # set combination logic radiobuttons
      set fscedit_logic [lindex $fscedit_sclist($sel) $fsc_logi_idx]

      # set mask checkbuttons
      unset fscedit_mask
      foreach index [lindex $fscedit_sclist($sel) $fsc_mask_idx] {
         set fscedit_mask($index) 1
      }
   }
}

proc UpdateEditedShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx
   global fscedit_sclist fscedit_count
   global fscedit_mask fscedit_logic fscedit_label

   set sel [.fscedit.list curselection]
   if {([string length $sel] > 0) && ($sel < $fscedit_count)} {
      set mask {}
      foreach index [array names fscedit_mask] {
         if {$fscedit_mask($index) != 0} {
            lappend mask $index
         }
      }
      # update mask setting
      set fscedit_sclist($sel) [lreplace $fscedit_sclist($sel) $fsc_mask_idx $fsc_mask_idx $mask]

      # update combination logic setting
      set fscedit_sclist($sel) [lreplace $fscedit_sclist($sel) $fsc_logi_idx $fsc_logi_idx $fscedit_logic]

      # update description label
      if {[string length $fscedit_label] > 0} {
         set fscedit_sclist($sel) [lreplace $fscedit_sclist($sel) $fsc_name_idx $fsc_name_idx $fscedit_label]

         .fscedit.list delete $sel
         .fscedit.list insert $sel $fscedit_label
         .fscedit.list selection set $sel
      }
   }
}

proc DeleteEditedShortcut {} {
   global fscedit_sclist fscedit_count

   set sel [.fscedit.list curselection]
   if {([string length $sel] > 0) && ($sel < $fscedit_count)} {
      for {set index [expr $sel + 1]} {$index < $fscedit_count} {incr index} {
         set fscedit_sclist([expr $index - 1]) $fscedit_sclist($index)
      }
      incr fscedit_count -1

      .fscedit.list delete $sel
      if {$sel < $fscedit_count} {
         .fscedit.list selection set $sel
      } elseif {$fscedit_count > 0} {
         .fscedit.list selection set [expr $sel - 1]
      }
   }
}

proc ShiftUpEditedShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx
   global fscedit_sclist fscedit_count

   set sel [.fscedit.list curselection]
   if {([string length $sel] > 0) && ($sel > 0) && ($sel < $fscedit_count)} {
      # swap with the item above
      set tmp $fscedit_sclist($sel)
      set fscedit_sclist($sel) $fscedit_sclist([expr $sel - 1])
      set fscedit_sclist([expr $sel - 1]) $tmp

      .fscedit.list delete $sel
      incr sel -1
      .fscedit.list insert $sel [lindex $fscedit_sclist($sel) $fsc_name_idx]
      .fscedit.list selection set $sel
   }
}

proc ShiftDownEditedShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx
   global fscedit_sclist fscedit_count

   set sel [.fscedit.list curselection]
   if {([string length $sel] > 0) && ($fscedit_count > 1) && ($sel < [expr $fscedit_count - 1])} {
      # swap with the item below
      set tmp $fscedit_sclist($sel)
      set fscedit_sclist($sel) $fscedit_sclist([expr $sel + 1])
      set fscedit_sclist([expr $sel + 1]) $tmp

      .fscedit.list delete $sel
      incr sel
      .fscedit.list insert $sel [lindex $fscedit_sclist($sel) $fsc_name_idx]
      .fscedit.list selection set $sel
   }
}

## ---------------------------------------------------------------------------
##                    R C - F I L E   H A N D L I N G
## ---------------------------------------------------------------------------
set myrcfile ""

proc LoadRcFile {filename} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx
   global shortcuts shortcut_count
   global prov_selection cfnetwops
   global showNetwopListbox showShortcutListbox
   global myrcfile

   set myrcfile $filename
   set shortcut_count 0
   set error 0
   set line_no 0

   if {[catch {set rcfile [open $filename "r"]}] == 0} {
      while {[gets $rcfile line] >= 0} {
         incr line_no
         if {([catch $line] != 0) && !$error} {
            tk_messageBox -type ok -default ok -icon error -message "error in rc/ini file, line #$line_no: $line"
            set error 1
         }
      }
      close $rcfile

      if {$showShortcutListbox == 0} {pack forget .all.shortcuts}
      if {$showNetwopListbox == 0} {pack forget .all.netwops}

   } else {
      # ini file does not exist -> dump default settings
      PreloadShortcuts
      UpdateRcFile
   }

   for {set index 0} {$index < $shortcut_count} {incr index} {
      .all.shortcuts.list insert end [lindex $shortcuts($index) $fsc_name_idx]
   }
   .all.shortcuts.list configure -height $shortcut_count
}

##
##  Write all config variables into the rc/ini file
##
proc UpdateRcFile {} {
   global shortcuts shortcut_count
   global prov_selection cfnetwops
   global showNetwopListbox showShortcutListbox
   global myrcfile

   if {[catch {set rcfile [open $myrcfile "w"]}] == 0} {
      puts $rcfile "#"
      puts $rcfile "# Nextview EPG configuration file"
      puts $rcfile "#"
      puts $rcfile "# This file is automatically generated - do not edit"
      puts $rcfile "# Written at: [clock format [clock seconds] -format %c]"
      puts $rcfile "#"

      # dump filter shortcuts
      for {set index 0} {$index < $shortcut_count} {incr index} {
         puts $rcfile [list set shortcuts($index) $shortcuts($index)]
      }
      puts $rcfile [list set shortcut_count $shortcut_count]

      # dump provider selection order
      puts $rcfile [list set prov_selection $prov_selection]

      # dump network selection for all providers
      foreach index [array names cfnetwops] {
         puts $rcfile [list set cfnetwops($index) $cfnetwops($index)]
      }

      # dump shortcuts & network listbox visibility
      puts $rcfile [list set showNetwopListbox $showNetwopListbox]
      puts $rcfile [list set showShortcutListbox $showShortcutListbox]

      close $rcfile
   } else {
      tk_messageBox -type ok -default ok -icon error -message "could not write to ini file $myrcfile"
   }
}

##
##  Update the provider selection order: move the last selected CNI to the front
##
set prov_selection {}

proc UpdateProvSelection {cni} {
   global prov_selection

   # delete the cni in the old list
   set index 0
   foreach prov $prov_selection {
      if {$prov == $cni} {
         set prov_selection [lreplace $prov_selection $index $index]
         break
      }
      incr index
   }
   # prepend the cni to the selection list
   set prov_selection [linsert $prov_selection 0 $cni]

   # save the new list into the ini/rc file
   UpdateRcFile
}

