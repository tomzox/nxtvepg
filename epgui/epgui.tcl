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
#  $Id: epgui.tcl,v 1.17 2000/06/15 17:15:06 tom Exp tom $
#

frame     .all -relief flat -borderwidth 0
frame     .all.themes -borderwidth 2
label     .all.themes.clock -borderwidth 1 -text {}
pack      .all.themes.clock -fill x -pady 5
button    .all.themes.reset -text "Reset" -relief ridge -command {ResetFilterState; C_ResetFilter}
pack      .all.themes.reset -side top -fill x

listbox   .all.themes.list -exportselection false -setgrid true -height 12 -width 12 -selectmode extended -relief ridge
.all.themes.list insert end "alle"
.all.themes.list selection set 0
bind      .all.themes.list <ButtonRelease-1> {+ SelectThemeListboxItem}
bind      .all.themes.list <space> {+ SelectThemeListboxItem}
pack      .all.themes.list -fill x
pack      .all.themes -anchor nw -side left

frame     .all.netwops
listbox   .all.netwops.list -exportselection false -setgrid true -height 27 -width 8 -selectmode extended -relief ridge
.all.netwops.list insert end alle
.all.netwops.list selection set 0
bind      .all.netwops.list <ButtonRelease-1> {+ SelectNetwop}
bind      .all.netwops.list <space> {+ SelectNetwop}
pack      .all.netwops.list -side left -anchor n
pack      .all.netwops -side left -anchor n -pady 2 -padx 2

set textfont -*-helvetica-medium-r-*-*-12-*-*-*-*-*-iso8859-1
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
text      .all.pi.info.text -height 7 -width 70 -wrap word \
                            -font $textfont -exportselection false \
                            -background $default_bg \
                            -yscrollcommand {.all.pi.info.sc set} \
                            -cursor circle \
			    -insertofftime 0
bindtags  .all.pi.info.text {.all.pi.info.text . all}
pack      .all.pi.info.text -side left -fill y -expand 1
pack      .all.pi.info -side top -fill y -expand 1
pack      .all.pi -side left -anchor n -fill y -expand 1

pack .all
#bind .all q {bind .all q; destroy .}

menu .menubar -relief ridge
. config -menu .menubar
.menubar add cascade -label "Control" -menu .menubar.ctrl
.menubar add cascade -label "Configure" -menu .menubar.config
.menubar add cascade -label "Filter" -menu .menubar.filter
.menubar add cascade -label "Navigate" -menu .menubar.ni_1
#.menubar add command -label "" -activebackground $default_bg -command {} -foreground #2e2e37
.menubar add cascade -label "Help" -menu .menubar.help
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
.menubar.config add command -label "Networks..." -state disabled
.menubar.config add command -label "Filter shortcuts..." -state disabled
# Filter menu
menu .menubar.filter
.menubar.filter add cascade -menu .menubar.filter.sound -label Sound
.menubar.filter add cascade -menu .menubar.filter.format -label Format
.menubar.filter add cascade -menu .menubar.filter.digital -label Digital
.menubar.filter add cascade -menu .menubar.filter.encryption -label Encryption
.menubar.filter add cascade -menu .menubar.filter.live -label "Live/Repeat"
.menubar.filter add cascade -menu .menubar.filter.subtitles -label Subtitles
.menubar.filter add cascade -menu .menubar.filter.featureclass -label "Feature class"
.menubar.filter add separator
.menubar.filter add cascade -menu .menubar.filter.p_rating -label "Parental Rating"
.menubar.filter add cascade -menu .menubar.filter.e_rating -label "Editorial Rating"
.menubar.filter add separator
.menubar.filter add cascade -menu .menubar.filter.themes -label Themes
.menubar.filter add cascade -menu .menubar.filter.themeclass -label "Theme class"
.menubar.filter add separator
.menubar.filter add cascade -menu .menubar.filter.series -label Series
.menubar.filter add cascade -menu .menubar.filter.progidx -label "Program index"
.menubar.filter add command -label "Text search..." -command SubStrPopup
.menubar.filter add separator
.menubar.filter add command -label "Add filter shortcut..." -state disabled
.menubar.filter add command -label "Reset" -command {ResetFilterState; C_ResetFilter}
menu .menubar.filter.sound
.menubar.filter.sound add radio -label any -command {SelectFeatures 0 0 0x03} -variable feature_sound -value 0
.menubar.filter.sound add radio -label mono -command {SelectFeatures 0x03 0x00 0} -variable feature_sound -value 1
.menubar.filter.sound add radio -label stereo -command {SelectFeatures 0x03 0x02 0} -variable feature_sound -value 3
.menubar.filter.sound add radio -label "2-channel" -command {SelectFeatures 0x03 0x01 0} -variable feature_sound -value 2
.menubar.filter.sound add radio -label surround -command {SelectFeatures 0x03 0x03 0} -variable feature_sound -value 4
menu .menubar.filter.format
.menubar.filter.format add radio -label any -command {SelectFeatures 0 0 0x0c} -variable feature_format -value 0
.menubar.filter.format add radio -label full -command {SelectFeatures 0x0c 0x00 0x00} -variable feature_format -value 1
.menubar.filter.format add radio -label widescreen -command {SelectFeatures 0x04 0x04 0x08} -variable feature_format -value 2
.menubar.filter.format add radio -label "PAL+" -command {SelectFeatures 0x08 0x08 0x04} -variable feature_format -value 3
menu .menubar.filter.digital
.menubar.filter.digital add radio -label any -command {SelectFeatures 0 0 0x10} -variable feature_digital -value 0
.menubar.filter.digital add radio -label analog -command {SelectFeatures 0x10 0x00 0} -variable feature_digital -value 1
.menubar.filter.digital add radio -label digital -command {SelectFeatures 0x10 0x10 0} -variable feature_digital -value 2
menu .menubar.filter.encryption
.menubar.filter.encryption add radio -label any -command {SelectFeatures 0 0 0x20} -variable feature_encryption -value 0
.menubar.filter.encryption add radio -label free -command {SelectFeatures 0x20 0 0} -variable feature_encryption -value 1
.menubar.filter.encryption add radio -label encrypted -command {SelectFeatures 0x20 0x20 0} -variable feature_encryption -value 2
menu .menubar.filter.live
.menubar.filter.live add radio -label any -command {SelectFeatures 0 0 0xc0} -variable feature_live -value 0
.menubar.filter.live add radio -label live -command {SelectFeatures 0x40 0x40 0x80} -variable feature_live -value 2
.menubar.filter.live add radio -label new -command {SelectFeatures 0xc0 0 0} -variable feature_live -value 1
.menubar.filter.live add radio -label repeat -command {SelectFeatures 0x80 0x80 0x40} -variable feature_live -value 3
menu .menubar.filter.subtitles
.menubar.filter.subtitles add radio -label any -command {SelectFeatures 0 0 0x100} -variable feature_subtitles -value 0
.menubar.filter.subtitles add radio -label untitled -command {SelectFeatures 0x100 0 0} -variable feature_subtitles -value 1
.menubar.filter.subtitles add radio -label subtitle -command {SelectFeatures 0x100 0x100 0} -variable feature_subtitles -value 2
menu .menubar.filter.e_rating
.menubar.filter.e_rating add radio -label any -command SelectRating -variable editorial_rating -value 0
.menubar.filter.e_rating add radio -label "all rated programmes" -command SelectRating -variable editorial_rating -value 1
.menubar.filter.e_rating add radio -label "at least 2 of 7" -command SelectRating -variable editorial_rating -value 2
.menubar.filter.e_rating add radio -label "at least 3 of 7" -command SelectRating -variable editorial_rating -value 3
.menubar.filter.e_rating add radio -label "at least 4 of 7" -command SelectRating -variable editorial_rating -value 4
.menubar.filter.e_rating add radio -label "at least 5 of 7" -command SelectRating -variable editorial_rating -value 5
.menubar.filter.e_rating add radio -label "at least 6 of 7" -command SelectRating -variable editorial_rating -value 6
.menubar.filter.e_rating add radio -label "7 of 7" -command SelectRating -variable editorial_rating -value 7
menu .menubar.filter.p_rating
.menubar.filter.p_rating add radio -label any -command SelectRating -variable parental_rating -value 0
.menubar.filter.p_rating add radio -label "ok for all ages" -command SelectRating -variable parental_rating -value 1
.menubar.filter.p_rating add radio -label "ok for 4 years or elder" -command SelectRating -variable parental_rating -value 2
.menubar.filter.p_rating add radio -label "ok for 6 years or elder" -command SelectRating -variable parental_rating -value 3
.menubar.filter.p_rating add radio -label "ok for 8 years or elder" -command SelectRating -variable parental_rating -value 4
.menubar.filter.p_rating add radio -label "ok for 10 years or elder" -command SelectRating -variable parental_rating -value 5
.menubar.filter.p_rating add radio -label "ok for 12 years or elder" -command SelectRating -variable parental_rating -value 6
.menubar.filter.p_rating add radio -label "ok for 14 years or elder" -command SelectRating -variable parental_rating -value 7
.menubar.filter.p_rating add radio -label "ok for 16 years or elder" -command SelectRating -variable parental_rating -value 8
menu .menubar.filter.themes
menu .menubar.filter.series -postcommand {PostDynamicMenu .menubar.filter.series C_CreateSeriesNetwopMenu}
menu .menubar.filter.progidx
.menubar.filter.progidx add radio -label "any" -command {SelectProgIdx -1 -1} -variable filter_progidx -value 0
.menubar.filter.progidx add radio -label "running now" -command {SelectProgIdx 0 0} -variable filter_progidx -value 1
.menubar.filter.progidx add radio -label "running next" -command {SelectProgIdx 1 1} -variable filter_progidx -value 2
.menubar.filter.progidx add radio -label "running now or next" -command {SelectProgIdx 0 1} -variable filter_progidx -value 3
.menubar.filter.progidx add radio -label "other..." -command ProgIdxPopup -variable filter_progidx -value 4
menu .menubar.filter.themeclass
menu .menubar.filter.featureclass
# Navigation menu
menu .menubar.ni_1 -postcommand {PostDynamicMenu .menubar.ni_1 C_CreateNi}
# Help menu
menu .menubar.help -tearoff 0
.menubar.help add command -label "About" -command CreateAbout

# initialize menu status
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
         .menubar.filter.themes.$subtheme add checkbutton -label $pdc -command "SelectThemeMenuItem $subtheme" -variable theme_sel($subtheme)
         .menubar.filter.themes.$subtheme add separator
      } elseif {([string length $pdc] > 0) && ($subtheme > 0)} {
         .menubar.filter.themes.$subtheme add checkbutton -label $pdc -command "SelectThemeMenuItem $index" -variable theme_sel($index)
      }
   }
   # add sub-menu with one entry: all series on all networks
   .menubar.filter.themes add cascade -menu .menubar.filter.themes.series -label "series"
   menu .menubar.filter.themes.series -tearoff 0
   .menubar.filter.themes.series add checkbutton -label [C_GetPdcString 128] -command {SelectThemeMenuItem 128} -variable theme_sel(128)

   for {set index 1} {$index <= $theme_class_count} {incr index} {
      .menubar.filter.themeclass add radio -label $index -command SelectThemeClass -variable menuStatusThemeClass -value $index
   }
   for {set index 1} {$index <= $feature_class_count} {incr index} {
      .menubar.filter.featureclass add radio -label $index -command {SelectFeatureClass $current_feature_class} -variable current_feature_class -value $index
   }

}

##  ---------------------------------------------------------------------------
##  Predefined filter settings in the theme listbox
##
set preDefThemeName(0)   "spielfilme"
set preDefThemeValue(0)  [list 0x10 0x11 0x12 0x13 0x14 0x15 0x16 0x17 0x18]
set preDefThemeName(1)   "sport"
set preDefThemeValue(1)  [list 0x40 0x41 0x42 0x43 0x44 0x45 0x46 0x47 0x48 0x49 0x4a 0x4b 0x4c]
set preDefThemeName(2)   "serien"
set preDefThemeValue(2)  [list 0x80]
set preDefThemeName(3)   "kinder"
set preDefThemeValue(3)  [list 0x50 0x51 0x52 0x53 0x54 0x55]
set preDefThemeName(4)   "shows"
set preDefThemeValue(4)  [list 0x30 0x31 0x32 0x33]
set preDefThemeName(5)   "news"
set preDefThemeValue(5)  [list 0x20 0x21 0x22 0x23 0x24]
set preDefThemeName(6)   "sozial"
set preDefThemeValue(6)  [list 0x25 0x26 0x27 0x28]
set preDefThemeName(7)   "wissenschaft"
set preDefThemeValue(7)  [list 0x56 0x57 0x58 0x59 0x5a 0x5b 0x5c 0x5d]
set preDefThemeName(8)   "hobby"
set preDefThemeValue(8)  [list 0x34 0x35 0x36 0x37 0x38 0x39 0x3a]
set preDefThemeName(9)   "musik"
set preDefThemeValue(9)  [list 0x60 0x61 0x62 0x63 0x64 0x65 0x66]
set preDefThemeName(10)  "kultur"
set preDefThemeValue(10) [list 0x70 0x71 0x72 0x73 0x74 0x75 0x76 0x77 0x78 0x79 0x7a 0x7b]
set preDefThemeName(11)  "erotik"
set preDefThemeValue(11) [list 0x18]
set preDefThemeCount 12

proc InitThemesListbox {} {
   global preDefThemeName preDefThemeValue
   global preDefThemeCount

   for {set index 0} {$index < $preDefThemeCount} {incr index} {
      .all.themes.list insert end $preDefThemeName($index)
   }
   .all.themes.list configure -height [expr $preDefThemeCount + 1]
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
   } else                                                    { set filter_progidx 4
   }
}

##  ---------------------------------------------------------------------------
##  Reset the filter state
##
proc ResetFilterState {} {
   global feature_class_mask feature_class_value
   global parental_rating editorial_rating
   global theme_class_count current_theme_class menuStatusThemeClass
   global feature_class_count current_feature_class
   global theme_sel theme_class_sel
   global series_sel
   global grepvar
   global filter_progidx

   # initialize feature filter state
   set current_feature_class 1
   for {set index 1} {$index <= $feature_class_count} {incr index} {
      set feature_class_mask($index) 0
      set feature_class_value($index) 0
   }
   UpdateFeatureMenuState

   set parental_rating 0
   set editorial_rating 0
   set current_theme_class 1
   set menuStatusThemeClass $current_theme_class
   set grepvar {}
   set filter_progidx 0

   # initialize theme filter and menu state
   foreach index [array names theme_sel] {
      unset theme_sel($index)
   }
   for {set index 1} {$index <= $theme_class_count} {incr index} {
      set theme_class_sel($index) {}
   }

   # reset the theme and netwop quick filter bars
   .all.themes.list selection clear 1 end
   .all.themes.list selection set 0
   .all.netwops.list selection clear 1 end
   .all.netwops.list selection set 0

   # initialize series filter and menu state
   foreach index [array names series_sel] {
      unset series_sel($index)
   }
}

##  --------------------- F I L T E R   C A L L B A C K S ---------------------

##
##  Callback for button-release on netwop listbox
##
proc SelectNetwop {} {
   if {[.all.netwops.list selection includes 0]} {
      .all.netwops.list selection clear 1 end
      C_SelectNetwops {}
   } else {
      C_SelectNetwops [.all.netwops.list curselection]
   }
}

##
##  Update the filter context and refresh the listbox
##
proc UpdateThemeFilters {} {
   global current_theme_class
   global theme_sel

   set all {}
   foreach {index value} [array get theme_sel "*"] {
      if {[expr $value != 0]} {
         lappend all $index
      }
   }
   C_SelectThemes $current_theme_class $all
}

##
##  Callback for button-release on theme listbox
##
proc SelectThemeListboxItem {} {
   global preDefThemeValue
   global theme_sel

   if {[llength [.all.themes.list curselection]] == 0} {
      .all.themes.list selection set 0
   }

   # reset all theme indices and menu state
   for {set index 1} {$index <= 0x80} {incr index} {
      set theme_sel($index) 0
   }
   if {[.all.themes.list selection includes 0]} {
      .all.themes.list selection clear 1 end
   } else {
      # then set the selected themes
      foreach theme [.all.themes.list curselection] {
         foreach index $preDefThemeValue([expr $theme - 1]) {
            # need to convert index to decimal value
            set theme_sel([expr $index + 0]) 1
         }
      }
   }

   UpdateThemeFilters
}

proc SelectThemeMenuItem {index} {
   UpdateThemeFilters
}

##
##  Callback for theme class radio buttons
##
proc SelectThemeClass {} {
   global theme_class_count current_theme_class menuStatusThemeClass
   global theme_class_sel
   global theme_sel

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

      .all.themes.list selection clear 1 end
      .all.themes.list selection set 0

      # copy the new classes settings into the array, thereby setting the menu state
      set current_theme_class $menuStatusThemeClass
      for {set index 1} {$index <= 0x80} {incr index} {
         set theme_sel($index) 0
      }
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
   set curmask [expr ($mask & ~$unmask) | $mask]
   set curvalue [expr ($value & ~($mask | $unmask)) | $value]

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
##  Callback for parental and editorial rating radio buttons
##
proc SelectRating {} {
   global parental_rating editorial_rating

   C_SelectRating $parental_rating $editorial_rating
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
}

##  ---------------------------------------------------------------------------
##  Determine item index below the mouse pointer
##
proc GetSelectedItem {xcoo ycoo} {
   scan [.all.pi.list.text index "@$xcoo,$ycoo"] "%d.%d" new_line char
   expr $new_line - 1
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
   set xcoo [expr $xcoo + [winfo rootx .all] + [winfo x .all.pi.list.text]]
   set ycoo [expr $ycoo + [winfo rooty .all] + [winfo y .all.pi.list.text] - 5]
   wm geometry $poppedup_pi "+$xcoo+$ycoo"
   text $poppedup_pi.text -relief ridge -width 55 -height 20 -cursor circle
   $poppedup_pi.text tag configure title -justify center -spacing3 12 \
      -font -*-helvetica-bold-r-*-*-18-*-*-*-*-*-iso8859-1

   #C_PopupPi $poppedup_pi.text $netwop $blockno
   #$poppedup_pi.text configure -state disabled
   #$poppedup_pi.text configure -height [expr 1 + [$poppedup_pi.text index end]]
   #pack $poppedup_pi.text
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
set grepvar {}

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
   global grepvar

   C_SelectSubStr $substr_grep_title $substr_grep_descr $substr_ignore_case $grepvar
}

proc SubStrPopup {} {
   global substr_grep_title substr_grep_descr
   global substr_popup
   global grepvar

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
      entry .substr.all.name.str -textvariable grepvar
      pack .substr.all.name.str -side left
      bind .substr.all.name.str <Enter> {focus %W}
      bind .substr.all.name.str <Return> SubstrUpdateFilter
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
      button .substr.all.cmd.clear -text "Clear" -command {set grepvar {}; SubstrUpdateFilter}
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
      button .progidx.cmd.clear -text "Undo" -command {set filter_progidx 0; C_SelectProgIdx; destroy .progidx}
      pack .progidx.cmd.clear -side left -padx 20
      button .progidx.cmd.dismiss -text "Dismiss" -command {destroy .progidx}
      pack .progidx.cmd.dismiss -side left -padx 20
      pack .progidx.cmd -side top -pady 10

      bind .progidx.cmd <Destroy> {+ set progidx_popup 0}
   }
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
##  Handling of the About pop-up
##
set about_popup 0

proc CreateAbout {} {
   global about_popup

   if {$about_popup == 0} {
      set about_popup 1
      toplevel .about
      wm title .about "About Nextview"
      wm resizable .about 0 0
      wm group .about .
      message .about.m -text {
         Nextview EPG Decoder & Analyzer
         v0.01alpha / pre-release

         Copyright © 1999,2000 by Tom Zörner
         <Tom.Zoerner@informatik.uni-erlangen.de>

If you publish any information that was acquired by use of this software, please do always include a note about the source of your information and where to obtain a copy of this software.

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License Version 2 as published by the Free Software Foundation. You find a copy of this license in the file COPYRIGHT in the root directory of this release.

THIS PROGRAM IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL, BUT WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
      }
      pack .about.m
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
      foreach index [array names provwin_list] {
         unset provwin_list($index)
      }

      # fill the window
      C_ProvWin_Open
   }
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
proc AcqStat_AddHistory {pos val1c val1o val2c val2o} {

   .acqstat.hist addtag "TBDEL" enclosed $pos 0 [expr $pos + 2] 128
   catch [ .acqstat.hist delete "TBDEL" ]

   .acqstat.hist create line $pos 128 $pos [expr 128 - $val1c] -fill red
   .acqstat.hist create line $pos [expr 128 - $val1c] $pos [expr 128 - $val1o] -fill #A52A2A
   .acqstat.hist create line $pos [expr 128 - $val1o] $pos [expr 128 - $val2c] -fill blue
   .acqstat.hist create line $pos [expr 128 - $val2c] $pos [expr 128 - $val2o] -fill #483D8B
}

## ---------------------------------------------------------------------------
## Clear the histogram (e.g. after provider change)
##
proc AcqStat_ClearHistory {} {
   # the tag "all" is automatically assigned to every item in the canvas
   catch [ .acqstat.hist delete all ]
}

