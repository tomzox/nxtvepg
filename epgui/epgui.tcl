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
#  Author: Tom Zoerner
#
#  $Id: epgui.tcl,v 1.135 2001/09/12 18:43:17 tom Exp tom $
#

set is_unix [expr [string compare $tcl_platform(platform) "unix"] == 0]

if {$is_unix} {
   set font_pt_size  12
   set cursor_bg      #c3c3c3
   set cursor_bg_now  #b8b8df
   set cursor_bg_past #dfb8b8
} else {
   if {$tcl_version >= 8.3} {
      set font_pt_size  12
   } else {
      set font_pt_size  15
   }
   set cursor_bg      #e2e2e2
   set cursor_bg_now  #d8d8ff
   set cursor_bg_past #ffd8d8
}
set textfont        [list helvetica [expr   0 - $font_pt_size] normal]
set font_small      [list helvetica [expr   2 - $font_pt_size] normal]
set font_bold       [list helvetica [expr   0 - $font_pt_size] bold]
set font_pl2_bold   [list helvetica [expr  -2 - $font_pt_size] bold]
set font_pl4_bold   [list helvetica [expr  -4 - $font_pt_size] bold]
set font_pl6_bold   [list helvetica [expr  -6 - $font_pt_size] bold]
set font_pl12_bold  [list helvetica [expr -12 - $font_pt_size] bold]
set font_fixed      [list courier   [expr   0 - $font_pt_size] normal]

if [info exists tk_version] {
   set default_bg [. cget -background]
}

proc CreateMainWindow {} {
   global is_unix default_bg
   global textfont font_small font_bold font_pl2_bold
   global fileImage

   frame     .all -relief flat -borderwidth 0
   frame     .all.shortcuts -borderwidth 2
   label     .all.shortcuts.clock -borderwidth 1 -text {}
   pack      .all.shortcuts.clock -fill x -pady 5

   button    .all.shortcuts.reset -text "Reset" -relief ridge -command {ResetFilterState; C_ResetFilter all; C_ResetPiListbox}
   pack      .all.shortcuts.reset -side top -fill x

   listbox   .all.shortcuts.list -exportselection false -height 12 -width 0 -selectmode extended -relief ridge -cursor top_left_arrow
   bind      .all.shortcuts.list <ButtonRelease-1> {+ SelectShortcut}
   bind      .all.shortcuts.list <space> {+ SelectShortcut}
   pack      .all.shortcuts.list -fill x
   pack      .all.shortcuts -anchor nw -side left

   frame     .all.netwops
   listbox   .all.netwops.list -exportselection false -height 25 -width 0 -selectmode extended -relief ridge -cursor top_left_arrow
   .all.netwops.list insert end "-all-"
   .all.netwops.list selection set 0
   bind      .all.netwops.list <ButtonRelease-1> {+ SelectNetwop}
   bind      .all.netwops.list <space> {+ SelectNetwop}
   pack      .all.netwops.list -side left -anchor n
   pack      .all.netwops -side left -anchor n -pady 2 -padx 2

   frame     .all.pi
   frame     .all.pi.colheads
   frame     .all.pi.colheads.spacer -width [expr $is_unix ? 22 : 16]
   pack      .all.pi.colheads.spacer -side left
   pack      .all.pi.colheads -side top -anchor w

   frame     .all.pi.list
   scrollbar .all.pi.list.sc -orient vertical -command {C_PiListBox_Scroll}
   pack      .all.pi.list.sc -fill y -side left
   text      .all.pi.list.text -width 50 -height 25 -wrap none \
                               -font $textfont -exportselection false \
                               -background $default_bg \
                               -cursor top_left_arrow \
                               -insertofftime 0
   bindtags  .all.pi.list.text {.all.pi.list.text . all}
   bind      .all.pi.list.text <Button-1> {SelectPi %x %y}
   bind      .all.pi.list.text <Double-Button-1> {C_PopupPi %x %y}
   bind      .all.pi.list.text <Button-3> {CreateContextMenu %x %y}
   bind      .all.pi.list.text <Up>    {C_PiListBox_CursorUp}
   bind      .all.pi.list.text <Down>  {C_PiListBox_CursorDown}
   bind      .all.pi.list.text <Prior> {C_PiListBox_Scroll scroll -1 pages}
   bind      .all.pi.list.text <Next>  {C_PiListBox_Scroll scroll 1 pages}
   bind      .all.pi.list.text <Home>  {C_PiListBox_Scroll moveto 0.0; C_PiListBox_SelectItem 0}
   bind      .all.pi.list.text <End>   {C_PiListBox_Scroll moveto 1.0; C_PiListBox_Scroll scroll 1 pages}
   bind      .all.pi.list.text <Enter> {focus %W}
   .all.pi.list.text tag configure sel -foreground black -relief raised -borderwidth 1
   .all.pi.list.text tag configure now -background #c9c9df
   .all.pi.list.text tag configure past -background #dfc9c9
   .all.pi.list.text tag lower now
   .all.pi.list.text tag lower past
   pack      .all.pi.list.text -side left -fill x -expand 1
   pack      .all.pi.list -side top -fill x

   button    .all.pi.panner -bitmap bitmap_pan_updown -cursor top_left_arrow -takefocus 0
   bind      .all.pi.panner <ButtonPress-1> {+ PanningControl 1}
   bind      .all.pi.panner <ButtonRelease-1> {+ PanningControl 0}
   pack      .all.pi.panner -side top -anchor e

   frame     .all.pi.info
   scrollbar .all.pi.info.sc -orient vertical -command {.all.pi.info.text yview}
   pack      .all.pi.info.sc -side left -fill y -anchor e
   text      .all.pi.info.text -width 50 -height 10 -wrap word \
                               -font $textfont \
                               -background $default_bg \
                               -yscrollcommand {.all.pi.info.sc set} \
                               -insertofftime 0
   .all.pi.info.text tag configure title -font $font_pl2_bold -justify center -spacing3 3
   .all.pi.info.text tag configure bold -font $font_bold
   .all.pi.info.text tag configure features -font $font_bold -justify center -spacing3 6
   bind      .all.pi.info.text <Configure> ShortInfoResized
   pack      .all.pi.info.text -side left -fill both -expand 1
   pack      .all.pi.info -side top -fill both -expand 1
   pack      .all.pi -side top -fill y -expand 1

   entry     .all.statusline -state disabled -relief flat -borderwidth 1 \
                             -font $font_small -background $default_bg \
                             -textvariable dbstatus_line
   pack      .all.statusline -side bottom -fill x
   pack      .all -side left -fill y -expand 1


   # create a bitmap of an horizontal line for use as separator in the info window
   # inserted here manually from the file epgui/line.xbm
   image create bitmap bitmap_line -data "#define line_width 200\n#define line_height 2\n
   static unsigned char line_bits[] = {
      0xe7, 0xe7, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf3, 0xcf,
      0xe7, 0xe7, 0xe7, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf3,
      0xcf, 0xe7};";

   # create an image of a folder
   set fileImage [image create photo -data {
   R0lGODlhEAAMAKEAAAD//wAAAPD/gAAAACH5BAEAAAAALAAAAAAQAAwAAAIghINhyycvVFsB
   QtmS3rjaH1Hg141WaT5ouprt2HHcUgAAOw==}]

   option add *Dialog.msg.font $font_pl2_bold userDefault
}


# initialize menu state
set menuStatusStartAcq 0
set menuStatusDumpStream 0
set menuStatusThemeClass 1
set menuStatusTscaleOpen(ui) 0
set menuStatusTscaleOpen(acq) 0
set menuStatusStatsOpen(ui) 0
set menuStatusStatsOpen(acq) 0

proc CreateMenubar {} {
   global is_unix

   menu .menubar -relief ridge
   . config -menu .menubar
   .menubar add cascade -label "Control" -menu .menubar.ctrl -underline 0
   .menubar add cascade -label "Configure" -menu .menubar.config -underline 1
   #.menubar add cascade -label "Reminder" -menu .menubar.timer -underline 0
   .menubar add cascade -label "Filter" -menu .menubar.filter -underline 0
   if {$is_unix} {
      .menubar add cascade -label "Navigate" -menu .menubar.ni_1 -underline 0
   }
   #.menubar add command -label "" -activebackground $default_bg -command {} -foreground #2e2e37
   .menubar add cascade -label "Help" -menu .menubar.help -underline 0
   # Control menu
   menu .menubar.ctrl -tearoff 0 -postcommand C_SetControlMenuStates
   .menubar.ctrl add checkbutton -label "Enable acquisition" -variable menuStatusStartAcq -command {C_ToggleAcq $menuStatusStartAcq}
   .menubar.ctrl add checkbutton -label "Dump stream" -variable menuStatusDumpStream -command {C_ToggleDumpStream $menuStatusDumpStream}
   .menubar.ctrl add command -label "Dump raw database..." -command PopupDumpDatabase
   .menubar.ctrl add command -label "Dump in HTML..." -command PopupDumpHtml
   .menubar.ctrl add separator
   .menubar.ctrl add checkbutton -label "View timescales..." -command {C_StatsWin_ToggleTimescale ui} -variable menuStatusTscaleOpen(ui)
   .menubar.ctrl add checkbutton -label "View statistics..." -command {C_StatsWin_ToggleDbStats ui} -variable menuStatusStatsOpen(ui)
   .menubar.ctrl add checkbutton -label "View acq timescales..." -command {C_StatsWin_ToggleTimescale acq} -variable menuStatusTscaleOpen(acq)
   .menubar.ctrl add checkbutton -label "View acq statistics..." -command {C_StatsWin_ToggleDbStats acq} -variable menuStatusStatsOpen(acq)
   .menubar.ctrl add separator
   .menubar.ctrl add command -label "Quit" -command {destroy .; update}
   # Config menu
   menu .menubar.config -tearoff 0
   .menubar.config add command -label "Select provider..." -command ProvWin_Create
   .menubar.config add command -label "Merge providers..." -command PopupProviderMerge
   .menubar.config add command -label "Acquisition mode..." -command PopupAcqMode
   .menubar.config add separator
   .menubar.config add command -label "Provider scan..." -command PopupEpgScan
   .menubar.config add command -label "TV card input..." -command PopupHardwareConfig
   #.menubar.config add command -label "Time zone..." -command PopupTimeZone
   .menubar.config add separator
   .menubar.config add command -label "Select columns..." -command PopupColumnSelection
   .menubar.config add command -label "Select networks..." -command PopupNetwopSelection
   .menubar.config add command -label "Network names..." -command NetworkNamingPopup
   .menubar.config add command -label "Context menu..." -command ContextMenuConfigPopup
   .menubar.config add command -label "Filter shortcuts..." -command EditFilterShortcuts
   .menubar.config add separator
   .menubar.config add checkbutton -label "Show shortcuts" -command ToggleShortcutListbox -variable showShortcutListbox
   .menubar.config add checkbutton -label "Show networks" -command ToggleNetwopListbox -variable showNetwopListbox
   .menubar.config add checkbutton -label "Show status line" -command ToggleStatusLine -variable showStatusLine
   .menubar.config add checkbutton -label "Show column headers" -command ToggleColumnHeader -variable showColumnHeader
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
   if {$is_unix} {
      .menubar.filter add cascade -menu .menubar.filter.series_bynet -label "Series by network..."
      .menubar.filter add cascade -menu .menubar.filter.series_alpha -label "Series alphabetically..."
   }
   .menubar.filter add cascade -menu .menubar.filter.progidx -label "Program index"
   .menubar.filter add cascade -menu .menubar.filter.netwops -label "Networks"
   if {!$is_unix} {
      .menubar.filter add command -label "Series by network..." -command {PostSeparateMenu .menubar.filter.series_bynet CreateSeriesNetworksMenu}
      .menubar.filter add command -label "Series alphabetically..." -command {PostSeparateMenu .menubar.filter.series_alpha undef}
   }
   .menubar.filter add command -label "Text search..." -command SubStrPopup
   .menubar.filter add command -label "Start Time..." -command PopupTimeFilterSelection
   .menubar.filter add command -label "Sorting Criterions..." -command PopupSortCritSelection
   .menubar.filter add separator
   .menubar.filter add command -label "Add filter shortcut..." -command AddFilterShortcut
   .menubar.filter add command -label "Update filter shortcut..." -command UpdateFilterShortcut
   if {!$is_unix} {
      .menubar.filter add command -label "Navigate" -command {PostSeparateMenu .menubar.filter.ni_1 C_CreateNi}
   }
   .menubar.filter add command -label "Reset" -command {ResetFilterState; C_ResetFilter all; C_ResetPiListbox}

   menu .menubar.filter.e_rating
   FilterMenuAdd_EditorialRating .menubar.filter.e_rating

   menu .menubar.filter.p_rating
   FilterMenuAdd_ParentalRating .menubar.filter.p_rating

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
   FilterMenuAdd_Sound .menubar.filter.features.sound

   menu .menubar.filter.features.format
   FilterMenuAdd_Format .menubar.filter.features.format

   menu .menubar.filter.features.digital
   FilterMenuAdd_Digital .menubar.filter.features.digital

   menu .menubar.filter.features.encryption
   FilterMenuAdd_Encryption .menubar.filter.features.encryption

   menu .menubar.filter.features.live
   FilterMenuAdd_LiveRepeat .menubar.filter.features.live

   menu .menubar.filter.features.subtitles
   FilterMenuAdd_Subtitles .menubar.filter.features.subtitles

   menu .menubar.filter.features.featureclass
   menu .menubar.filter.themes

   menu .menubar.filter.series_bynet -postcommand {PostDynamicMenu .menubar.filter.series_bynet CreateSeriesNetworksMenu}
   menu .menubar.filter.series_alpha
   if {!$is_unix} {
      .menubar.filter.series_bynet configure -tearoff 0
      .menubar.filter.series_alpha configure -tearoff 0
   }
   foreach letter {A B C D E F G H I J K L M N O P Q R S T U V W X Y Z Other} {
      set w letter_[string tolower $letter]_off_0
      .menubar.filter.series_alpha add cascade -label $letter -menu .menubar.filter.series_alpha.$w
      menu .menubar.filter.series_alpha.$w -postcommand [list PostDynamicMenu .menubar.filter.series_alpha.$w CreateSeriesLetterMenu]
   }

   menu .menubar.filter.progidx
   .menubar.filter.progidx add radio -label "any" -command {SelectProgIdx -1 -1} -variable filter_progidx -value 0
   .menubar.filter.progidx add radio -label "running now" -command {SelectProgIdx 0 0} -variable filter_progidx -value 1
   .menubar.filter.progidx add radio -label "running next" -command {SelectProgIdx 1 1} -variable filter_progidx -value 2
   .menubar.filter.progidx add radio -label "running now or next" -command {SelectProgIdx 0 1} -variable filter_progidx -value 3
   .menubar.filter.progidx add radio -label "other..." -command ProgIdxPopup -variable filter_progidx -value 4
   menu .menubar.filter.netwops
   # Navigation menu
   if {$is_unix} {
      menu .menubar.ni_1 -postcommand {PostDynamicMenu .menubar.ni_1 C_CreateNi}
   }
   # Help menu
   menu .menubar.help -tearoff 0
   .menubar.help add command -label "About..." -command CreateAbout

   # Context Menu
   menu .contextmenu -tearoff 0 -postcommand {PostDynamicMenu .contextmenu C_CreateContextMenu}
}

proc FilterMenuAdd_Themes {widget} {
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
         $widget add cascade -label $tlabel -menu ${widget}.$subtheme
         menu ${widget}.$subtheme
         ${widget}.$subtheme add checkbutton -label $pdc -command "SelectTheme $subtheme" -variable theme_sel($subtheme)
         ${widget}.$subtheme add separator
      } elseif {([string length $pdc] > 0) && ($subtheme > 0)} {
         ${widget}.$subtheme add checkbutton -label $pdc -command "SelectTheme $index" -variable theme_sel($index)
      }
   }
   # add sub-menu with one entry: all series on all networks
   $widget add cascade -menu ${widget}.series -label "series"
   menu ${widget}.series -tearoff 0
   ${widget}.series add checkbutton -label [C_GetPdcString 128] -command {SelectTheme 128} -variable theme_sel(128)
}

##  ---------------------------------------------------------------------------
##  Generate the themes & feature menues
##
proc GenerateFilterMenues {tcc fcc} {
   global theme_class_count feature_class_count
   set theme_class_count   $tcc
   set feature_class_count $fcc

   FilterMenuAdd_Themes .menubar.filter.themes
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
##  Create the button for xawtv control and it's popup menu
##
proc CreateTuneTvButton {} {
   global is_unix

   if {$is_unix} {
      if {[string length [info commands .all.shortcuts.tune]] == 0} {
         button .all.shortcuts.tune -text "Tune TV" -relief ridge -command TuneTV
         bind   .all.shortcuts.tune <Button-3> {TuneTvPopupMenu 1 %x %y}

         menu .tunetvcfg -tearoff 0
         .tunetvcfg add command -label "Start Capturing" -command {C_Xawtv_SendCmd capture on}
         .tunetvcfg add command -label "Stop Capturing" -command {C_Xawtv_SendCmd capture off}
         .tunetvcfg add command -label "Toggle mute" -command {C_Xawtv_SendCmd volume mute}
         .tunetvcfg add separator
         .tunetvcfg add command -label "Toggle TV station" -command {C_Xawtv_SendCmd setstation back}
         .tunetvcfg add command -label "Next TV station" -command {C_Xawtv_SendCmd setstation next}
         .tunetvcfg add command -label "Previous TV station" -command {C_Xawtv_SendCmd setstation prev}
      }

      if {[lsearch -exact [pack slaves .all.shortcuts] .all.shortcuts.tune] == -1} {
         pack .all.shortcuts.tune -side top -fill x -before .all.shortcuts.reset
      }
   }
}

proc RemoveTuneTvButton {} {
   if {[lsearch -exact [pack slaves .all.shortcuts] .all.shortcuts.tune] >= 0} {
      pack forget .all.shortcuts.tune
   }
}

##  ---------------------------------------------------------------------------
##  Create a popup menu below the xawtv window
##  - params #1-#4 are the coordinates and dimensions of the xawtv window
##  - params #5-#7 are the info to be displayed in the popup
##
proc Create_XawtvPopup {xcoo ycoo width height rperc rtime ptitle} {
   global font_pl2_bold

   set ww1 [font measure $font_pl2_bold $rtime]
   set ww2 [font measure $font_pl2_bold $ptitle]
   set ww [expr ($ww1 >= $ww2) ? $ww1 : $ww2]
   set wh [expr 2 * [font metrics $font_pl2_bold -linespace] + 3 + 4+4]

   if {[string length [info commands .xawtv_epg]] == 0} {
      toplevel .xawtv_epg -class InputOutput
      wm overrideredirect .xawtv_epg 1
      wm withdraw .xawtv_epg

      frame .xawtv_epg.f -borderwidth 2 -relief raised
      frame .xawtv_epg.f.rperc -borderwidth 1 -relief sunken -height 5 -width $ww
      pack propagate .xawtv_epg.f.rperc 0
      frame .xawtv_epg.f.rperc.bar -background blue -height 5 -width [expr int($ww * $rperc)]
      pack propagate .xawtv_epg.f.rperc.bar 0
      pack .xawtv_epg.f.rperc.bar -anchor w -side left -fill y -expand 1

      label .xawtv_epg.f.lines -text "$rtime\n$ptitle" -font $font_pl2_bold
      pack .xawtv_epg.f.rperc -side top -fill x -expand 1 -padx 5 -pady 4
      pack .xawtv_epg.f.lines -side top -padx 5
      pack .xawtv_epg.f

   } else {
      wm withdraw .xawtv_epg

      .xawtv_epg.f.rperc configure -width $ww
      .xawtv_epg.f.rperc.bar configure -width [expr int($ww * $rperc)]
      .xawtv_epg.f.lines configure -text "$rtime\n$ptitle"
   }

   # add X-padding and border width
   set ww [expr $ww + 5 + 5 + 2 + 2]

   # compute X-position: center
   set wxcoo [expr $xcoo + ($width - $ww) / 2]
   if {$wxcoo < 0} {
      set wxcoo 0
   } elseif {$wxcoo + $ww > [winfo screenwidth .xawtv_epg]} {
      set wxcoo [expr [winfo screenwidth .xawtv_epg] - $ww]
      if {$wxcoo < 0} {set wxcoo 0}
   }

   # compute Y-position: below the xawtv window; above if too low
   set wycoo [expr $ycoo + $height + 10]
   if {$wycoo + $wh > [winfo screenheight .xawtv_epg]} {
      set wycoo [expr $ycoo - 20 - $wh]
      if {$wycoo < 0} {set wycoo 0}
   }

   # must build the popup before the geometry request or it will flicker
   update

   wm geometry .xawtv_epg "+$wxcoo+$wycoo"
   wm deiconify .xawtv_epg
   raise .xawtv_epg
}

##  ---------------------------------------------------------------------------
##  Create "Demo-Mode" menu with warning labels and disable some menu commands
##
proc CreateDemoModePseudoMenu {} {

   # create menu with warning labels
   .menubar insert last cascade -label "   ** Demo-Mode **" -menu .menubar.demodb -foreground red -activeforeground red
   menu .menubar.demodb -tearoff 0
   .menubar.demodb add command -label "Please note:"
   .menubar.demodb add command -label "- entries' start times are not real!"
   .menubar.demodb add command -label "- no acquisition possible!"
   .menubar.demodb add command -label "- no provider selection possible!"

   # acq not possible since start time of all PI in the db were modified during reload
   .menubar.ctrl entryconfigure "Enable acquisition" -state disabled
   # since acq is not possible, dump stream not possible either
   .menubar.ctrl entryconfigure "Dump stream" -state disabled

   # provider change not possible, since -demo db is always reloaded
   .menubar.config entryconfigure "Select provider*" -state disabled
   # providers can not be merged because there is only one db given with -demo
   .menubar.config entryconfigure "Merge providers*" -state disabled
   # acq not possible, hence no scan also
   .menubar.config entryconfigure "Provider scan*" -state disabled
   # TV hardware/TV input config not required, since acq disabled
   .menubar.config entryconfigure "TV card input*" -state disabled
   # acq not possible, hence no mode change
   .menubar.config entryconfigure "Acquisition mode*" -state disabled
}

##  ---------------------------------------------------------------------------
##  Show or hide shortcut listbox, network filter listbox and db status line
##
set showNetwopListbox 0
set showShortcutListbox 1
set showStatusLine 1
set showColumnHeader 1

proc ToggleNetwopListbox {} {
   global showNetwopListbox

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
   global showShortcutListbox

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

proc ToggleStatusLine {} {
   global showStatusLine

   if {[lsearch -exact [pack slaves .all] .all.statusline] == -1} {
      # status line is currently not visible -> pack below "short info" text widget
      pack .all.statusline -after .all.pi -side top -fill x
      set showStatusLine 1
   } else {
      # status line is visible -> unpack
      pack forget .all.statusline
      set showStatusLine 0
   }
   UpdateRcFile
}

proc ToggleColumnHeader {} {
   global showColumnHeader

   ApplySelectedColumnList initial
   UpdateRcFile
}

##  ---------------------------------------------------------------------------
##  Create the features filter menus
##
proc FilterMenuAdd_EditorialRating {widget} {
   $widget add radio -label any -command SelectEditorialRating -variable editorial_rating -value 0
   $widget add radio -label "all rated programmes" -command SelectEditorialRating -variable editorial_rating -value 1
   $widget add radio -label "at least 2 of 7" -command SelectEditorialRating -variable editorial_rating -value 2
   $widget add radio -label "at least 3 of 7" -command SelectEditorialRating -variable editorial_rating -value 3
   $widget add radio -label "at least 4 of 7" -command SelectEditorialRating -variable editorial_rating -value 4
   $widget add radio -label "at least 5 of 7" -command SelectEditorialRating -variable editorial_rating -value 5
   $widget add radio -label "at least 6 of 7" -command SelectEditorialRating -variable editorial_rating -value 6
   $widget add radio -label "7 of 7" -command SelectEditorialRating -variable editorial_rating -value 7
}

proc FilterMenuAdd_ParentalRating {widget} {
   $widget add radio -label any -command SelectParentalRating -variable parental_rating -value 0
   $widget add radio -label "ok for all ages" -command SelectParentalRating -variable parental_rating -value 1
   $widget add radio -label "ok for 4 years or elder" -command SelectParentalRating -variable parental_rating -value 2
   $widget add radio -label "ok for 6 years or elder" -command SelectParentalRating -variable parental_rating -value 3
   $widget add radio -label "ok for 8 years or elder" -command SelectParentalRating -variable parental_rating -value 4
   $widget add radio -label "ok for 10 years or elder" -command SelectParentalRating -variable parental_rating -value 5
   $widget add radio -label "ok for 12 years or elder" -command SelectParentalRating -variable parental_rating -value 6
   $widget add radio -label "ok for 14 years or elder" -command SelectParentalRating -variable parental_rating -value 7
   $widget add radio -label "ok for 16 years or elder" -command SelectParentalRating -variable parental_rating -value 8
}

proc FilterMenuAdd_Sound {widget} {
   $widget add radio -label any -command {SelectFeatures 0 0 0x03} -variable feature_sound -value 0
   $widget add radio -label mono -command {SelectFeatures 0x03 0x00 0} -variable feature_sound -value 1
   $widget add radio -label stereo -command {SelectFeatures 0x03 0x02 0} -variable feature_sound -value 3
   $widget add radio -label "2-channel" -command {SelectFeatures 0x03 0x01 0} -variable feature_sound -value 2
   $widget add radio -label surround -command {SelectFeatures 0x03 0x03 0} -variable feature_sound -value 4
}

proc FilterMenuAdd_Format {widget} {
   $widget add radio -label any -command {SelectFeatures 0 0 0x0c} -variable feature_format -value 0
   $widget add radio -label full -command {SelectFeatures 0x0c 0x00 0x00} -variable feature_format -value 1
   $widget add radio -label widescreen -command {SelectFeatures 0x04 0x04 0x08} -variable feature_format -value 2
   $widget add radio -label "PAL+" -command {SelectFeatures 0x08 0x08 0x04} -variable feature_format -value 3
}

proc FilterMenuAdd_Digital {widget} {
   $widget add radio -label any -command {SelectFeatures 0 0 0x10} -variable feature_digital -value 0
   $widget add radio -label analog -command {SelectFeatures 0x10 0x00 0} -variable feature_digital -value 1
   $widget add radio -label digital -command {SelectFeatures 0x10 0x10 0} -variable feature_digital -value 2
}

proc FilterMenuAdd_Encryption {widget} {
   $widget add radio -label any -command {SelectFeatures 0 0 0x20} -variable feature_encryption -value 0
   $widget add radio -label free -command {SelectFeatures 0x20 0 0} -variable feature_encryption -value 1
   $widget add radio -label encrypted -command {SelectFeatures 0x20 0x20 0} -variable feature_encryption -value 2
}

proc FilterMenuAdd_LiveRepeat {widget} {
   $widget add radio -label any -command {SelectFeatures 0 0 0xc0} -variable feature_live -value 0
   $widget add radio -label live -command {SelectFeatures 0x40 0x40 0x80} -variable feature_live -value 2
   $widget add radio -label new -command {SelectFeatures 0xc0 0 0} -variable feature_live -value 1
   $widget add radio -label repeat -command {SelectFeatures 0x80 0x80 0x40} -variable feature_live -value 3
}

proc FilterMenuAdd_Subtitles {widget} {
   $widget add radio -label any -command {SelectFeatures 0 0 0x100} -variable feature_subtitles -value 0
   $widget add radio -label untitled -command {SelectFeatures 0x100 0 0} -variable feature_subtitles -value 1
   $widget add radio -label subtitle -command {SelectFeatures 0x100 0x100 0} -variable feature_subtitles -value 2
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
   global fsc_prevselection

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
   set fsc_prevselection {}
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
##  - the parameter is the index into the network menu,
##    i.e. into the user network selection table
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
##  Select network in context menu
##  - the parameter is the network index in the AI block
##
proc SelectNetwopByIdx {netwop is_on} {
   global netwop_map netselmenu

   # search to which menu index this network was mapped
   foreach {idx val} [array get netwop_map] {
      if {$val == $netwop} {
         # simulate a click into the netwop listbox
         if $is_on {
            .all.netwops.list selection clear 0
            .all.netwops.list selection set [expr $idx + 1]
         } else {
            # deselect the netwop
            .all.netwops.list selection clear [expr $idx + 1]
         }
         SelectNetwop
         return
      }
   }

   # not found in the netwop menus (not part of user network selection)
   # -> just set the filter & deselect the "All netwops" entry
   .all.netwops.list selection clear 0
   if $is_on {
      C_SelectNetwops $netwop
   } else {
      set temp [C_GetNetwopFilterList]
      set idx [lsearch -exact $temp $netwop]
      if {$idx >= 0} {
         C_SelectNetwops $netwop
      } else {
         C_SelectNetwops [concat [lrange $temp 0 [expr $idx - 1]] [lrange $temp [expr $idx + 1] end]]
      }
   }
   C_RefreshPiListbox
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
      if {$empty} {C_ResetFilter series
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
##  Callback for selection of a program item
##
proc SelectPi {xcoo ycoo} {
   # first unpost the context menu
   global dynmenu_posted
   if {[info exists dynmenu_posted(.contextmenu)] && ($dynmenu_posted(.contextmenu) > 0)} {
      set dynmenu_posted(.contextmenu) 1
      .contextmenu unpost
   }

   C_PiListBox_SelectItem [GetSelectedItem $xcoo $ycoo]

   # binding for motion event to follow the mouse with the selection
   bind .all.pi.list.text <Motion> {SelectPiMotion %s %x %y}
}

# callback for Motion event in the PI listbox while the first button is pressed
proc SelectPiMotion {state xcoo ycoo} {
   if {$state & 0x100} {
      # mouse position has changed -> check if new item is selected
      C_PiListBox_SelectItem [GetSelectedItem $xcoo $ycoo]
   } else {
      # button no longer pressed -> remove the motion binding
      bind .all.pi.list.text <Motion> {}
   }
}

##  ---------------------------------------------------------------------------
##  Callback for right-click on a PiListBox item
##
proc CreateContextMenu {xcoo ycoo} {
   global contextMenuItem

   set xcoo [expr $xcoo + [winfo rootx .all.pi.list.text]]
   set ycoo [expr $ycoo + [winfo rooty .all.pi.list.text]]

   tk_popup .contextmenu $xcoo $ycoo 0
}

##  ---------------------------------------------------------------------------
##  Menu command to post a separate sub-menu
##  - Complex dynamic menus need to be separated on Windows, because the whole
##    menu tree is created every time the top-level menu is mapped. After a
##    few mappings the application gets extremely slow! (Tk or Windows bug!?)
##
proc PostSeparateMenu {wmenu func} {
   set xcoo [expr [winfo rootx .all.pi.list.text] - 5]
   set ycoo [expr [winfo rooty .all.pi.list.text] - 5]

   if {[string length [info commands $wmenu]] == 0} {
      menu $wmenu -postcommand [list PostDynamicMenu $wmenu $func] -tearoff 0
   }

   $wmenu post $xcoo $ycoo
}

##  ---------------------------------------------------------------------------
##  Callback for double-click on a PiListBox item
##
set poppedup_pi {}

proc Create_PopupPi {ident xcoo ycoo} {
   global textfont font_pl6_bold default_bg
   global poppedup_pi

   if {[string length $poppedup_pi] > 0} {destroy $poppedup_pi}
   set poppedup_pi $ident

   toplevel $poppedup_pi
   bind $poppedup_pi <Leave> {destroy %W; set poppedup_pi ""}
   wm overrideredirect $poppedup_pi 1
   set xcoo [expr $xcoo + [winfo rootx .all.pi.list.text] - 200]
   set ycoo [expr $ycoo + [winfo rooty .all.pi.list.text] - 5]
   wm geometry $poppedup_pi "+$xcoo+$ycoo"
   text $poppedup_pi.text -relief ridge -width 55 -height 20 -cursor circle \
                          -background $default_bg -font $textfont
   $poppedup_pi.text tag configure title \
                     -justify center -spacing3 12 -font $font_pl6_bold
   $poppedup_pi.text tag configure body -tabs 35m

   #C_PopupPi $poppedup_pi.text $netwop $blockno
   #$poppedup_pi.text configure -state disabled
   #$poppedup_pi.text configure -height [expr 1 + [$poppedup_pi.text index end]]
   #pack $poppedup_pi.text
}

##  ---------------------------------------------------------------------------
##  Sort a list of series titles alphabetically
##
proc CompareSeriesMenuEntries {a b} {
   return [string compare [lindex $a 0] [lindex $b 0]]
}

##  ---------------------------------------------------------------------------
##  Create the series sub-menu for a given network
##  - with a list of all series on this network, sorted by title
## 
proc CreateSeriesMenu {w} {
   regexp {net_(0x[A-Fa-f0-9]+)_off_(\d+)$} $w foo cni min_index

   # fetch the list of series codes and titles
   set slist [C_GetNetwopSeriesList $cni]

   # sort the list of commands by series title and then create the entries in that order
   set all {}
   foreach {series title} $slist {
      lappend all [list $title $series]
   }
   set all [lsort -unique -command CompareSeriesMenuEntries $all]

   set max_index [expr $min_index + 25]
   if {[expr $max_index + 3] >= [llength $all]} {
      incr max_index 3
   }
   set index 0
   foreach item $all {
      incr index
      if {$index >= $min_index} {
         if {$index <= $max_index} {
            set series [lindex $item 1]
            $w add checkbutton -label [lindex $item 0] -variable series_sel($series) -command [list SelectSeries $series]
         } else {
            set child "$w.net_${cni}_off_$index"
            $w add separator
            $w add cascade -label "more..." -menu $child
            if {[string length [info commands $child]] == 0} {
               menu $child -postcommand [list PostDynamicMenu $child CreateSeriesMenu]
            }
            break
         }
      }
   }
   if {$index == 0} {
      $w add command -label "none" -state disabled
   }
}

##  ---------------------------------------------------------------------------
##  Create root of Series menu
##  - post-command for the "Series" entry in various filter menus
##  - consists of a list of all netwops with PI that have assigned PDC themes > 0x80
##  - each entry is a sub-menu (cascade) that list all found series in that netwop
##
proc CreateSeriesNetworksMenu {w} {
   global cfnetwops

   # fetch a list of CNIs of netwops which broadcast series programmes
   set series_nets [C_GetNetwopsWithSeries]

   # fetch all network names from AI into an array
   C_GetAiNetwopList 0 netsel_names
   ApplyUserNetnameCfg netsel_names

   # get the CNI of the currently selected db
   set prov [C_GetCurrentDatabaseCni]
   if {$prov != 0} {

      # sort the cni list according to the user network selection
      if [info exists cfnetwops($prov)] {
         set tmp {}
         foreach cni [lindex $cfnetwops($prov) 0] {
            if {[lsearch -exact $series_nets $cni] >= 0} {
               lappend tmp $cni
            }
         }
         set series_nets $tmp
      }

      # insert the names into the menu
      foreach cni $series_nets {
         set child "$w.net_${cni}_off_0"
         $w add cascade -label $netsel_names($cni) -menu $child
         if {[string length [info commands $child]] == 0} {
            menu $child -postcommand [list PostDynamicMenu $child CreateSeriesMenu]
         }
      }
   }
}

##  ---------------------------------------------------------------------------
##  Create series title menu for one starting letter
##
proc CreateSeriesLetterMenu {w} {

   regexp {letter_([^_]+)_off_(\d+)$} $w foo letter min_index

   # sort the list of commands by series title and then create the entries in that order
   set all {}
   foreach {series title} [C_GetSeriesByLetter $letter] {
      # force the new first title character to be uppercase (for sorting)
      lappend all [list $title $series]
   }
   set all [lsort -unique -command CompareSeriesMenuEntries $all]

   set max_index [expr $min_index + 25]
   if {[expr $max_index + 3] >= [llength $all]} {
      incr max_index 3
   }
   set index 0
   foreach item $all {
      incr index
      if {$index >= $min_index} {
         if {$index <= $max_index} {
            set series [lindex $item 1]
            $w add checkbutton -label [lindex $item 0] -variable series_sel($series) -command [list SelectSeries $series]
         } else {
            set child "${w}.letter_${letter}_off_$index"
            $w add separator
            $w add cascade -label "more..." -menu $child
            if {[string length [info commands $child]] == 0} {
               menu $child -postcommand [list PostDynamicMenu $child CreateSeriesLetterMenu]
            }
            break
         }
      }
   }
   if {$index == 0} {
      $w add command -label "none" -state disabled
   }
}

##  ---------------------------------------------------------------------------
##  Handling of dynamic menus
##  - the content and sub-menus are not created before they are posted
##  - several instances of the menu might be posted by use of tear-off,
##    but we must not destroy the menu until the last instance is unposted
##
proc PostDynamicMenu {menu cmd} {
   global is_unix dynmenu_posted dynmenu_created

   if {![info exist dynmenu_posted($menu)]} {
      set dynmenu_posted($menu) 0
   }
   if {($dynmenu_posted($menu) == 0) || !$is_unix} {
      # delete the previous menu content
      if {[string compare "none" [$menu index end]] != 0} {
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

      if {($dynmenu_posted($menu) == 0) && ([string length [info commands $menu]] > 0)} {
         bind $menu <Destroy> {}
      }
   }

   #puts stdout "$menu unmapped: ref $dynmenu_posted($menu)"
}

##  --------------------------------------------------------------------------
##  Create a popup toplevel window
##
proc CreateTransientPopup {wname title} {
   global is_unix

   toplevel $wname
   wm title $wname $title
   # disable the resize button in the window frame
   wm resizable $wname 0 0
   if {!$is_unix} {
      # make it a slave of the parent, usually without decoration
      wm transient $wname .
      # place it in the upper left corner of the parent
      if {[regexp "\\+(\\d+)\\+(\\d+)" [wm geometry .] foo root_x root_y]} {
         wm geometry $wname "+[expr $root_x + 30]+[expr $root_y + 30]"
      }
   } else {
      wm group $wname .
   }
}

##  --------------------------------------------------------------------------
##  Count number of visible lines in a text widget, e.g. after toplevel resize
##  -- currently unused --
##
proc CountVisibleTextLines {w} {
   # remember the position of the last character in the text widget
   set prevEnd [$w index end]
   scan $prevEnd "%d." prevLines
   incr prevLines -1

   # loop over all lines of text (appending new ones if needed)
   set count 0
   set lastLHeight 0
   while 1 {
      # if the text doesn't fill the widget, append empty lines for measurement
      if {$prevLines <= $count} {
        $w insert end "\n"
      }
      # determine the height of the current line
      set bbox [$w bbox "[expr $count + 1].0"]
      set lheight [lindex $bbox 3]
      # check if its invisible or only partially visible (assuming all lines have equal height)
      if {([string length $bbox] == 0) ||
          (($lheight < $lastLHeight) && ($lastLHeight != 0))} {
         # reached the bottom border of visibility
         break
      }
      set lastLHeight $lheight
      incr count
   }

   # remove the appended lines
   $w delete $prevEnd end

   return $count
}

##  --------------------------------------------------------------------------
##  Handling of "Panning"
##  - i.e. adjusting the proportions of the listbox and info text widgets
##  - the total height of the main window shall remain unchanged
##  - by dragging the panning button the user can reduce the height of the
##    listbox and enlarge the info window below by the same amount -
##    or the other way around

set pibox_height 25
set pibox_resized 0
set shortinfo_height 10

proc AdjustTextWidgetProportions {delta} {
   global panning_list_height panning_info_height
   global pibox_height
   global pibox_resized

   pack propagate .all.pi.list 1
   set hl [winfo height .all.pi.list]
   set hi [winfo height .all.pi.info]

   .all.pi.list.text configure -height [expr $panning_list_height - $delta]

   # Workaround for UNIX window management
   # - Before the user has resized the toplevel window, the size of the window
   #   is controlled entirely by the size of the enclosed widgets.
   # - Afterwards, the window height "sticks" to the configured height.
   #   Under Windows any resize of expanded widgets is ignored then, but
   #   UNIX also adds the offset by which the toplevel was resized
   if {$pibox_resized == 0} {
      .all.pi.info.text configure -height [expr $panning_info_height + $delta]
   }
   #.all.pi.info configure -height [expr $hi + ($hl - [winfo height .all.pi.list])]

   set pibox_height [expr $panning_list_height - $delta]
   C_ResizePiListbox

   wm minsize . 0 [expr [winfo height .] - [winfo height .all.pi.info.text] + 80]
}

# callback for mouse motion while the panning button is pressed
proc PanningMotion {w state ypos} {
   global panning_root_y1 panning_root_y2 panning_list_height panning_info_height
   global textfont

   if {$state != 0} {
      set cheight [font metrics $textfont -linespace]

      # compute how far the mouse pointer has been moved
      # - it's not enough to move one line at a time, because the widget size might "freeze"
      #   before the pointer position is fully reached when the pointer is no longer moved
      if {$ypos <= $panning_root_y1} {
         #puts "$panning_root_y1 $panning_root_y2 $ypos [.all.pi.list.text cget -height] [expr 1 + int(($panning_root_y1 - $ypos) / $cheight)]"
         AdjustTextWidgetProportions [expr 1 + int(($panning_root_y1 - $ypos) / $cheight)]
      } elseif {$ypos >= $panning_root_y2} {
         #puts "$panning_root_y1 $panning_root_y2 $ypos [.all.pi.list.text cget -height] [expr -1 - int(($ypos - $panning_root_y2) / $cheight)]"
         AdjustTextWidgetProportions [expr -1 - int(($ypos - $panning_root_y2) / $cheight)]
      }
   }
}

# callback for button press an release on the panning button
# sets up or stops the panning
proc PanningControl {start} {
   global panning_root_y1 panning_root_y2 panning_list_height panning_info_height
   global pibox_resized
   global shortinfo_height textfont
   global is_unix

   if {$start} {
      set panning_root_y1 [winfo rooty .all.pi.panner]
      set panning_root_y2 [expr $panning_root_y1 + [winfo height .all.pi.panner]]
      set panning_list_height [.all.pi.list.text cget -height]
      set panning_info_height [.all.pi.info.text cget -height]
      # check if the toplevel window was resized by the user
      set pibox_resized [expr !$is_unix || ([winfo height .all.pi.info.text] != [winfo reqheight .all.pi.info.text])]

      bind .all.pi.panner <Motion> {PanningMotion .all.pi.panner %s %Y}
   } else {
      unset panning_root_y1 panning_root_y2
      unset panning_list_height panning_info_height
      bind .all.pi.panner <Motion> {}

      # save the new listbox and shortinfo geometry to the rc/ini file
      set shortinfo_height [expr int([winfo height .all.pi.info.text] / [font metrics $textfont -linespace])]
      UpdateRcFile
   }
}

# callback for Configure (aka resize) event in short info text widget
proc ShortInfoResized {} {
   global textfont shortinfo_height

   set new_height [expr int([winfo height .all.pi.info.text] / [font metrics $textfont -linespace])]

   if {$new_height != $shortinfo_height} {
      set shortinfo_height $new_height
      UpdateRcFile
   }
}

##  --------------------------------------------------------------------------
##  Tune the station of the currently selected programme
##  - callback command of the "Tune TV" button in the main window
##  - pops up a warning if network names have not been sync'ed with xawtv yet.
##    this popup is shown just once after each program start.
##
proc TuneTV {} {
   global tunetv_msg_nocfg
   global cfnetnames

   # warn if network names have not been sync'ed with xawtv yet
   if {![array exists cfnetnames] && ![info exists tunetv_msg_nocfg]} {
      set tunetv_msg_nocfg 1

      set answer [tk_messageBox -type okcancel -default ok -icon info \
                     -message "Please synchronize the nextwork names with xawtv in the Network Name Configuration dialog. You need to do this just once."]
      if {[string compare $answer "ok"] == 0} {
         # invoke the network name configuration dialog
         NetworkNamingPopup
         return
      }
   }

   # retrieve the name of the network of the currently selected PI
   set selnet [C_PiListBox_GetSelectedNetwop]
   if {[llength $selnet] > 0} {
      set cni [lindex $selnet 0]
      # use user-configured network name OR provider name from AI
      if [info exists cfnetnames($cni)] {
         set name $cfnetnames($cni)
      } else {
         set name [lindex $selnet 1]
      }
      C_Xawtv_SendCmd "setstation" $name
   }
}

# callback for right-click onto the "Tune TV" button
proc TuneTvPopupMenu {state xcoo ycoo} {
   set xcoo [expr $xcoo + [winfo rootx .all.shortcuts.tune]]
   set ycoo [expr $ycoo + [winfo rooty .all.shortcuts.tune]]

   tk_popup .tunetvcfg $xcoo $ycoo 0
}

##  --------------------------------------------------------------------------
##  Text search pop-up window
##
set substr_grep_title 1
set substr_grep_descr 1
set substr_match_full 0
set substr_match_case 0
set substr_popup 0
set substr_pattern {}
set substr_history {}

# check that at lease one of the {title descr} options remains checked
proc SubstrCheckOptScope {varname} {
   global substr_grep_title substr_grep_descr

   if {($substr_grep_title == 0) && ($substr_grep_descr == 0)} {
      set $varname 1
   }
}

# update the filter context and refresh the PI listbox
proc SubstrUpdateFilter {} {
   global substr_grep_title substr_grep_descr substr_match_case substr_match_full
   global substr_pattern
   global substr_history

   C_SelectSubStr $substr_grep_title $substr_grep_descr $substr_match_case $substr_match_full $substr_pattern
   C_RefreshPiListbox
   CheckShortcutDeselection

   if {[string length $substr_pattern] > 0} {
      set new [list $substr_grep_title $substr_grep_descr $substr_match_case $substr_match_full $substr_pattern]
      set idx 0
      foreach item $substr_history {
         if {[string compare $substr_pattern [lindex $item 4]] == 0} {
            set substr_history [lreplace $substr_history $idx $idx]
            break
         }
         incr idx
      }
      set substr_history [linsert $substr_history 0 $new]
      if {[llength $substr_history] > 10} {
         set substr_history [lreplace $substr_history 10 end]
      }
      UpdateRcFile
   }
}

# set the parameters saved in the history
proc SubstrSetFilter {grep_title grep_descr match_case match_full pattern} {
   global substr_grep_title substr_grep_descr substr_match_case substr_match_full
   global substr_pattern

   set substr_grep_title  $grep_title
   set substr_grep_descr  $grep_descr
   set substr_match_case  $match_case
   set substr_match_full  $match_full
   set substr_pattern     $pattern
   SubstrUpdateFilter
}

# open the popup window
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

      pack .substr.all -padx 10 -pady 10
      bind .substr.all <Destroy> {+ set substr_popup 0}
   } else {
      raise .substr
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
      button .progidx.cmd.dismiss -text "Dismiss" -width 5 -command {destroy .progidx}
      pack .progidx.cmd.help .progidx.cmd.clear .progidx.cmd.dismiss -side left -padx 10
      pack .progidx.cmd -side top -pady 10

      bind .progidx.cmd <Destroy> {+ set progidx_popup 0}
   } else {
      raise .progidx
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
      CreateTransientPopup .timsel "Time filter selection"
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
      button .timsel.cmd.help -text "Help" -command {PopupHelp $helpIndex(Filtering) "Start Time"}
      button .timsel.cmd.undo -text "Undo" -command {destroy .timsel; UndoTimeFilter}
      button .timsel.cmd.dismiss -text "Dismiss" -command {destroy .timsel}
      pack .timsel.cmd.help .timsel.cmd.undo .timsel.cmd.dismiss -side left -padx 10
      pack .timsel.cmd -side top -pady 5

      bind .timsel.all <Destroy> {+ set timsel_popup 0}

      set $timsel_enabled 1
      if {$timsel_relative} {
         SelectTimeFilterRelStart
      }
   } else {
      raise .timsel
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
      tk_messageBox -type ok -default ok -icon error -parent .timsel \
                    -message "Invalid input format in \"$val\"; use \"HH:MM\""
   }
}

# small helper function to convert time format from "minutes of day" to HH:MM
proc Motd2HHMM {motd} {
   return [format "%02d:%02d" [expr $motd / 60] [expr $motd % 60]]
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
      set timsel_startstr [Motd2HHMM $timsel_start]
   }
   if {$timsel_absstop} {
      .timsel.all.stop.str configure -state normal
      set timsel_stopstr "23:59"
      .timsel.all.stop.str configure -state disabled
   } else {
      set timsel_stopstr [Motd2HHMM $timsel_stop]
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
      CreateTransientPopup .sortcrit "Sorting criterion selection"
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
      button .sortcrit.all.scadd -text "Add" -command AddSortCritSelection -width 5
      button .sortcrit.all.scdel -text "Delete" -command DeleteSortCritSelection -width 5
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
      button .sortcrit.all.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Filtering) "Sorting Criterions"}
      button .sortcrit.all.clear -text "Clear" -width 5 -command ClearSortCritSelection
      button .sortcrit.all.dismiss -text "Dismiss" -width 5 -command {destroy .sortcrit}
      pack .sortcrit.all.dismiss .sortcrit.all.clear .sortcrit.all.help -side bottom -anchor w
      pack .sortcrit.all -side left -anchor n -fill y -expand 1 -padx 5 -pady 5

      bind .sortcrit.all <Destroy> {+ set sortcrit_popup 0}

      UpdateSortCritListbox
   } else {
      raise .sortcrit
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
         default {tk_messageBox -type ok -default ok -icon error -parent .sortcrit -message "Invalid entry item \"$item\"; expected format: nn\[-nn\]{,nn-...}"}
      }
      if {$to != -1} {
         if {$to < $from} {set swap $to; set to $from; set from $swap}
         if {$to >= 256} {
            tk_messageBox -type ok -default ok -icon error -parent .sortcrit -message "Invalid range; values must be lower than 0x100"
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
set dumpdb_xi 1
set dumpdb_ai 1
set dumpdb_ni 1
set dumpdb_oi 1
set dumpdb_mi 1
set dumpdb_li 1
set dumpdb_ti 1
set dumpdb_filename {}
set dumpdb_popup 0

proc PopupDumpDatabase {} {
   global dumpdb_pi dumpdb_xi dumpdb_ai dumpdb_ni dumpdb_filename
   global dumpdb_oi dumpdb_mi dumpdb_li dumpdb_ti
   global font_fixed fileImage
   global dumpdb_popup

   if {$dumpdb_popup == 0} {
      CreateTransientPopup .dumpdb "Dump raw database"
      set dumpdb_popup 1

      frame .dumpdb.all

      frame .dumpdb.all.name
      label .dumpdb.all.name.prompt -text "File name:"
      pack .dumpdb.all.name.prompt -side left
      entry .dumpdb.all.name.filename -textvariable dumpdb_filename -font $font_fixed -width 30
      pack .dumpdb.all.name.filename -side left -padx 5
      bind .dumpdb.all.name.filename <Enter> {focus %W}
      bind .dumpdb.all.name.filename <Return> DoDbDump
      bind .dumpdb.all.name.filename <Escape> {destroy .dumpdb}
      button .dumpdb.all.name.dlgbut -image $fileImage -command {
         set tmp [tk_getSaveFile -parent .dumpdb \
                     -initialfile [file tail $dumpdb_filename] \
                     -initialdir [file dirname $dumpdb_filename]]
         if {[string length $tmp] > 0} {
            set dumpdb_filename $tmp
         }
         unset tmp
      }
      pack .dumpdb.all.name.dlgbut -side left -padx 5
      pack .dumpdb.all.name -side top -pady 10

      frame .dumpdb.all.opt
      frame .dumpdb.all.opt.one
      checkbutton .dumpdb.all.opt.one.pi -text "Programme Info" -variable dumpdb_pi
      checkbutton .dumpdb.all.opt.one.xi -text "Defective PI" -variable dumpdb_xi
      checkbutton .dumpdb.all.opt.one.ai -text "Application Info" -variable dumpdb_ai
      checkbutton .dumpdb.all.opt.one.ni -text "Navigation Info" -variable dumpdb_ni
      pack .dumpdb.all.opt.one.pi -side top -anchor nw
      pack .dumpdb.all.opt.one.xi -side top -anchor nw
      pack .dumpdb.all.opt.one.ai -side top -anchor nw
      pack .dumpdb.all.opt.one.ni -side top -anchor nw
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
      button .dumpdb.all.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Control) "Dump raw database"}
      pack .dumpdb.all.cmd.help -side left -padx 10
      button .dumpdb.all.cmd.clear -text "Abort" -width 5 -command {destroy .dumpdb}
      pack .dumpdb.all.cmd.clear -side left -padx 10
      button .dumpdb.all.cmd.apply -text "Ok" -width 5 -command DoDbDump
      pack .dumpdb.all.cmd.apply -side left -padx 10
      pack .dumpdb.all.cmd -side top

      pack .dumpdb.all -padx 10 -pady 10
      bind .dumpdb.all <Destroy> {+ set dumpdb_popup 0}
   } else {
      raise .dumpdb
   }
}

# callback for Ok button
proc DoDbDump {} {
   global dumpdb_pi dumpdb_xi dumpdb_ai dumpdb_ni dumpdb_filename
   global dumpdb_oi dumpdb_mi dumpdb_li dumpdb_ti

   if {([string length $dumpdb_filename] > 0) && [file exists $dumpdb_filename]} {

      set answer [tk_messageBox -type okcancel -icon warning -parent .dumpdb \
                                -message "This file already exists - overwrite it?"]
      if {[string compare $answer "ok"] != 0} {
         return
      }
   }
   C_DumpDatabase $dumpdb_filename \
                  $dumpdb_pi $dumpdb_xi $dumpdb_ai $dumpdb_ni \
                  $dumpdb_oi $dumpdb_mi $dumpdb_li $dumpdb_ti

   # close the popup window
   destroy .dumpdb

   # save the settings to the rc/ini file
   UpdateRcFile
}

##  --------------------------------------------------------------------------
##  Popup "Dump HTML" dialog
##
set dumphtml_filename {}
set dumphtml_type 3
set dumphtml_sel_only 1
set dumphtml_hyperlinks 1
set dumphtml_file_append 1
set dumphtml_file_overwrite 0
set dumphtml_popup 0

proc PopupDumpHtml {} {
   global dumphtml_filename dumphtml_type dumphtml_file_append dumphtml_file_overwrite
   global dumphtml_sel_only dumphtml_hyperlinks
   global font_fixed fileImage
   global dumphtml_popup

   if {$dumphtml_popup == 0} {
      CreateTransientPopup .dumphtml "Dump database in HTML"
      set dumphtml_popup 1

      frame .dumphtml.opt1 -borderwidth 1 -relief raised
      frame .dumphtml.opt1.name
      label .dumphtml.opt1.name.prompt -text "File name:"
      pack .dumphtml.opt1.name.prompt -side left
      entry .dumphtml.opt1.name.filename -textvariable dumphtml_filename -font $font_fixed -width 30
      pack .dumphtml.opt1.name.filename -side left -padx 5
      bind .dumphtml.opt1.name.filename <Enter> {focus %W}
      bind .dumphtml.opt1.name.filename <Return> DoDumpHtml
      bind .dumphtml.opt1.name.filename <Escape> {destroy .dumphtml}
      button .dumphtml.opt1.name.dlgbut -image $fileImage -command {
         set tmp [tk_getSaveFile -parent .dumphtml \
                     -initialfile [file tail $dumphtml_filename] \
                     -initialdir [file dirname $dumphtml_filename] \
                     -defaultextension html -filetypes {{HTML {*.html *.htm}} {all {*.*}}}]
         if {[string length $tmp] > 0} {
            set dumphtml_filename $tmp
         }
         unset tmp
      }
      pack .dumphtml.opt1.name.dlgbut -side left -padx 5
      pack .dumphtml.opt1.name -side top -pady 10

      checkbutton .dumphtml.opt1.sel0 -text "Append to file (if exists)" -variable dumphtml_file_append
      checkbutton .dumphtml.opt1.sel1 -text "Overwrite without asking" -variable dumphtml_file_overwrite
      pack .dumphtml.opt1.sel0 .dumphtml.opt1.sel1 -side top -anchor nw
      pack .dumphtml.opt1 -side top -pady 5 -padx 10 -fill x

      frame .dumphtml.opt2 -borderwidth 1 -relief raised
      checkbutton .dumphtml.opt2.chk0 -text "Selected programme only" -variable dumphtml_sel_only
      checkbutton .dumphtml.opt2.chk1 -text "Add hyperlinks to titles" -variable dumphtml_hyperlinks
      pack .dumphtml.opt2.chk0 .dumphtml.opt2.chk1 -side top -anchor nw
      pack .dumphtml.opt2 -side top -pady 5 -padx 10 -fill x

      frame .dumphtml.opt3 -borderwidth 1 -relief raised
      radiobutton .dumphtml.opt3.sel0 -text "Write titles and descriptions" -variable dumphtml_type -value 3
      radiobutton .dumphtml.opt3.sel1 -text "Write titles only" -variable dumphtml_type -value 1
      radiobutton .dumphtml.opt3.sel2 -text "Write descriptions only" -variable dumphtml_type -value 2
      pack .dumphtml.opt3.sel0 .dumphtml.opt3.sel1 .dumphtml.opt3.sel2 -side top -anchor nw
      pack .dumphtml.opt3 -side top -pady 5 -padx 10 -fill x

      frame .dumphtml.cmd
      button .dumphtml.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Control) "Dump in HTML"}
      pack .dumphtml.cmd.help -side left -padx 10
      button .dumphtml.cmd.dismiss -text "Dismiss" -width 5 -command {destroy .dumphtml}
      pack .dumphtml.cmd.dismiss -side left -padx 10
      button .dumphtml.cmd.write -text "Write" -width 5 -command DoDumpHtml
      pack .dumphtml.cmd.write -side left -padx 10
      pack .dumphtml.cmd -side top -pady 10

      bind .dumphtml.cmd <Destroy> {+ set dumphtml_popup 0}
   } else {
      raise .dumphtml
   }
}

# callback for Write button
proc DoDumpHtml {} {
   global dumphtml_filename dumphtml_type dumphtml_file_append dumphtml_file_overwrite
   global dumphtml_sel_only dumphtml_hyperlinks

   if {[string length $dumphtml_filename] > 0} {
      if {!$dumphtml_file_append && !$dumphtml_file_overwrite && \
         [file exists $dumphtml_filename]} {

         set answer [tk_messageBox -type okcancel -icon warning -parent .dumphtml \
                                   -message "This file already exists - overwrite it?"]
         if {[string compare $answer "ok"] != 0} {
            return
         }
      }
      C_DumpHtml $dumphtml_filename \
                 [expr ($dumphtml_type & 1) != 0] [expr ($dumphtml_type & 2) != 0] \
                 $dumphtml_file_append $dumphtml_sel_only $dumphtml_hyperlinks

      # save the settings to the rc/ini file
      UpdateRcFile

   } else {
      # the file name entry field is still empty -> abort (stdout not allowed here)
      tk_messageBox -type ok -icon error -parent .dumphtml -message "Please enter a file name."
   }
}

##  --------------------------------------------------------------------------
##  Handling of help popup
##
set help_popup 0

proc PopupHelp {index {subheading {}}} {
   global textfont font_bold font_pl4_bold
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
      text   .help.disp.text -width 60 -wrap word -setgrid true -background #ffd840 \
                             -font $textfont -spacing3 6 \
                             -yscrollcommand {.help.disp.sb set}
      pack   .help.disp.text -side left -fill both -expand 1
      scrollbar .help.disp.sb -orient vertical -command {.help.disp.text yview}
      pack   .help.disp.sb -fill y -anchor e -side left
      pack   .help.disp -side top -fill both -expand 1
      # define tags for various nroff text formats
      .help.disp.text tag configure title -font $font_pl4_bold -spacing3 10
      .help.disp.text tag configure indent -lmargin1 40 -lmargin2 40
      .help.disp.text tag configure bold -font $font_bold
      .help.disp.text tag configure underlined -underline 1
      .help.disp.text tag configure href -underline 1 -foreground blue
      .help.disp.text tag bind href <Button-1> {FollowHelpHyperlink}

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
      # raise the popup above all others in the window stacking order
      raise .help
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

   # bring the given subheading into view
   if {[string length $subheading] != 0} {
      set subheading [.help.disp.text search -- $subheading 1.0]
      if {[string length $subheading] != 0} {
         .help.disp.text see $subheading
      }
   }
}

proc FollowHelpHyperlink {} {
   global helpIndex

   # the text under the mouse carries the mark 'current'
   set curidx [.help.disp.text index {current + 1 char}]

   # determine the range of the 'href' tag under the mouse
   set range [.help.disp.text tag prevrange href $curidx]

   # cut out the text in that range
   set hlink [eval [concat .help.disp.text get $range]]

   if {[info exists helpIndex($hlink)]} {
      PopupHelp $helpIndex($hlink)
   }
}

##  --------------------------------------------------------------------------
##  Handling of the About pop-up
##
set about_popup 0

proc CreateAbout {} {
   global EPG_VERSION tcl_patchLevel font_fixed
   global about_popup

   if {$about_popup == 0} {
      CreateTransientPopup .about "About nxtvepg"
      set about_popup 1

      label .about.name -text "nexTView EPG Decoder - nxtvepg v$EPG_VERSION"
      pack .about.name -side top -pady 8
      #label .about.tcl_version -text " Tcl/Tk version $tcl_patchLevel"
      #pack .about.tcl_version -side top

      label .about.copyr1 -text "Copyright © 1999, 2000, 2001 by Tom Zörner"
      label .about.copyr2 -text "tomzo@nefkom.net"
      label .about.copyr3 -text "http://nxtvepg.tripod.com/" -font $font_fixed -foreground blue
      pack .about.copyr1 .about.copyr2 -side top
      pack .about.copyr3 -side top -padx 10 -pady 10

      label .about.logo -bitmap nxtv_logo
      pack .about.logo -side top -pady 5
      # disclaimer that the author is not member of the ETSI should be placed right beneath the logo

      message .about.m -text {
The Nextview standard was developed by the major European consumer electronics manufacturers under the hood of the European Telecommunications Standards Institute (http://www.etsi.org/) in 1995-1997. The author of nxtvepg has no connections to the ETSI.

If you publish any information that was acquired by use of this software, please do always include a note about the source of your information and where to obtain a copy of this software.

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License Version 2 as published by the Free Software Foundation. You find a copy of this license in the file COPYRIGHT in the root directory of this release.

THIS PROGRAM IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL, BUT WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
      }
      pack .about.m -side top
      bind .about.m <Destroy> {+ set about_popup 0}

      button .about.dismiss -text "Dismiss" -command {destroy .about}
      pack .about.dismiss -pady 10
   } else {
      raise .about
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
   global provwin_ailist

   if {$provwin_popup == 0} {

      # build list of available providers
      set provwin_ailist {}
      foreach {cni name} [C_GetProvCnisAndNames] {
         # build list of CNIs
         lappend provwin_ailist $cni
         # build array of provider network names
         set provwin_names($cni) $name
      }
      # sort CNI list according to user preferences
      set provwin_ailist [SortProvList $provwin_ailist]

      if {[llength $provwin_ailist] == 0} {
         # no providers found -> abort
         tk_messageBox -type ok -icon info -message "There are no providers available yet.\nPlease start a provider scan from the Configure menu."
         return
      }

      CreateTransientPopup .provwin "Provider Selection"
      set provwin_popup 1

      # list of providers at the left side of the window
      frame .provwin.n
      frame .provwin.n.b
      scrollbar .provwin.n.b.sb -orient vertical -command {.provwin.n.b.list yview}
      listbox .provwin.n.b.list -relief ridge -selectmode single -exportselection 0 \
                                -width 12 -height 5 -yscrollcommand {.provwin.n.b.sb set}
      pack .provwin.n.b.sb .provwin.n.b.list -side left -fill y
      pack .provwin.n.b -side left -fill y
      bind .provwin.n.b.list <ButtonRelease-1> {+ ProvWin_Select}
      bind .provwin.n.b.list <KeyRelease-space> {+ ProvWin_Select}

      # provider info at the right side of the window
      if {[info exists provwin_servicename]} {unset provwin_servicename}
      frame .provwin.n.info
      label .provwin.n.info.serviceheader -text "Name of service"
      entry .provwin.n.info.servicename -state disabled -textvariable provwin_servicename -width 45
      pack .provwin.n.info.serviceheader .provwin.n.info.servicename -side top -anchor nw
      frame .provwin.n.info.net
      label .provwin.n.info.net.header -text "List of networks"
      text .provwin.n.info.net.list -width 45 -height 4 -wrap word \
                                    -font $textfont -background $default_bg \
                                    -insertofftime 0 -state disabled -exportselection 1
      pack .provwin.n.info.net.header .provwin.n.info.net.list -side top -anchor nw
      pack .provwin.n.info.net -side top -anchor nw

      # OI block header and message
      label .provwin.n.info.oiheader -text "OSD header and message"
      text .provwin.n.info.oimsg -width 45 -height 6 -wrap word \
                                 -font $textfont -background $default_bg \
                                 -insertofftime 0 -state disabled -exportselection 1
      pack .provwin.n.info.oiheader .provwin.n.info.oimsg -side top -anchor nw
      pack .provwin.n.info -side left -fill y -padx 10

      # buttons at the bottom of the window
      frame .provwin.cmd
      button .provwin.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration) "Select provider"}
      button .provwin.cmd.abort -text "Abort" -width 5 -command {destroy .provwin}
      button .provwin.cmd.ok -text "Ok" -width 5 -command ProvWin_Exit
      pack .provwin.cmd.help .provwin.cmd.abort .provwin.cmd.ok -side left -padx 10

      pack .provwin.n .provwin.cmd -side top -pady 10
      bind .provwin.n <Destroy> {+ set provwin_popup 0}

      # fill the listbox with the provider's network names
      foreach cni $provwin_ailist {
         .provwin.n.b.list insert end $provwin_names($cni)
      }

      # select the entry of the currently opened provider (if any)
      set index [lsearch -exact $provwin_ailist [C_GetCurrentDatabaseCni]]
      if {$index >= 0} {
         .provwin.n.b.list selection set $index
         ProvWin_Select
      }
   } else {
      raise .provwin
   }
}

# callback for provider selection in listbox: display infos on the right
proc ProvWin_Select {} {
   global provwin_ailist provwin_servicename

   # remove the old service name and netwop list
   set provwin_servicename {}
   .provwin.n.info.net.list configure -state normal
   .provwin.n.info.net.list delete 1.0 end
   .provwin.n.info.oimsg configure -state normal
   .provwin.n.info.oimsg delete 1.0 end

   set index [.provwin.n.b.list curselection]
   if {[string length $index] > 0} {
      set names [C_GetProvServiceInfos [lindex $provwin_ailist $index]]
      # display service name in entry widget
      set provwin_servicename [lindex $names 0]
      # display OI strings in text widget
      .provwin.n.info.oimsg insert end "[lindex $names 1]\n[lindex $names 2]"

      # display all netwops from the AI, separated by commas
      .provwin.n.info.net.list insert end [lindex $names 3]
      if {[llength $names] > 4} {
         foreach netwop [lrange $names 4 end] {
            .provwin.n.info.net.list insert end ", $netwop"
         }
      }
   }
   .provwin.n.info.net.list configure -state disabled
   .provwin.n.info.oimsg configure -state disabled
}

# callback for OK button: activate the selected provider
proc ProvWin_Exit {} {
   global provwin_ailist

   set index [.provwin.n.b.list curselection]
   if {[string length $index] > 0} {
      C_ChangeProvider [lindex $provwin_ailist $index]
   }
   destroy .provwin
}

##  --------------------------------------------------------------------------
##  Create EPG scan popup-window
##
set epgscan_popup 0
set epgscan_timeout 2
set epgscan_opt_slow 0
set epgscan_opt_refresh 0
set epgscan_opt_xawtv $is_unix

proc PopupEpgScan {} {
   global hwcfg hwcfg_default is_unix env
   global prov_freqs
   global epgscan_popup
   global epgscan_opt_slow epgscan_opt_refresh epgscan_opt_xawtv

   if {$epgscan_popup == 0} {
      if {!$is_unix} {
         if {![info exists hwcfg] || (([lindex $hwcfg 0] == 0) && ([lindex $hwcfg 1] == 0))} {
            # tuner type has not been configured yet -> abort
            tk_messageBox -type ok -icon info -message "Before you start the scan, please do configure your card's tuner type in the 'TV card input' sub-menu of the Configure menu.\nIf no channels are found during the scan, try to enable the tuner PLL initialization in that menu."
            return
         } elseif {[lindex $hwcfg 0] != 0} {
            # tuner not configured as input -> no scan possible or needed
            tk_messageBox -type ok -icon info -message "You have not selected the TV tuner as video input source. Hence a scan is neither needed nor possible.\nIf you want to use the TV tuner for input, you can change this setting in the 'TV card input' menu."
            return
         }
      }

      CreateTransientPopup .epgscan "Scan for Nextview EPG providers"
      set epgscan_popup 1

      frame  .epgscan.cmd
      # control commands
      button .epgscan.cmd.start -text "Start scan" -width 12 -command {if {[info exists hwcfg]} {C_StartEpgScan [lindex $hwcfg 0] $epgscan_opt_slow $epgscan_opt_refresh $epgscan_opt_xawtv} else {C_StartEpgScan [lindex $hwcfg_default 0] $epgscan_opt_slow $epgscan_opt_refresh $epgscan_opt_xawtv}}
      button .epgscan.cmd.stop -text "Abort scan" -width 12 -command C_StopEpgScan -state disabled
      button .epgscan.cmd.help -text "Help" -width 12 -command {PopupHelp $helpIndex(Configuration) "Provider scan"}
      button .epgscan.cmd.dismiss -text "Dismiss" -width 12 -command {destroy .epgscan}
      pack .epgscan.cmd.start .epgscan.cmd.stop .epgscan.cmd.help .epgscan.cmd.dismiss -side top -padx 10 -pady 10
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
      text .epgscan.all.fmsg.msg -width 40 -height 7 -yscrollcommand {.epgscan.all.fmsg.sb set} -wrap none
      pack .epgscan.all.fmsg.msg -side left -expand 1 -fill y
      scrollbar .epgscan.all.fmsg.sb -orient vertical -command {.epgscan.all.fmsg.msg yview}
      pack .epgscan.all.fmsg.sb -side left -fill y
      pack .epgscan.all.fmsg -side top -padx 10 -fill y -expand 1

      # mode buttons
      frame .epgscan.all.opt
      checkbutton .epgscan.all.opt.slow -text "Slow" -variable epgscan_opt_slow -command {C_SetEpgScanSpeed $epgscan_opt_slow}
      checkbutton .epgscan.all.opt.refresh -text "Refresh only" -variable epgscan_opt_refresh
      pack .epgscan.all.opt.slow .epgscan.all.opt.refresh -side left -padx 5
      if {$is_unix} {
         checkbutton .epgscan.all.opt.xawtv -text "Use .xawtv" -variable epgscan_opt_xawtv
         pack .epgscan.all.opt.xawtv -side left -padx 5
         if {![C_Xawtv_Enabled]} {
            set epgscan_opt_xawtv 0
            .epgscan.all.opt.xawtv configure -state disabled
         }
      }
      pack .epgscan.all.opt -side top -padx 10 -pady 5
      if {[llength $prov_freqs] == 0} {
         .epgscan.all.opt.refresh configure -state disabled
      }

      pack .epgscan.all -side top -fill y -expand 1
      bind .epgscan.all <Destroy> {+ set epgscan_popup 0; C_StopEpgScan}

      .epgscan.all.fmsg.msg insert end "Press the <Start scan> button\n"
   } else {
      raise .epgscan
   }
}

# called after start or stop of EPG scan to update button states
proc EpgScanButtonControl {is_start} {
   global is_unix env prov_freqs

   if {[string compare $is_start "start"] == 0} {
      # grab input focus to prevent any interference with the scan
      grab .epgscan
      # disable options and command buttons, enable the "Abort" button
      .epgscan.cmd.start configure -state disabled
      .epgscan.cmd.stop configure -state normal
      .epgscan.cmd.help configure -state disabled
      .epgscan.cmd.dismiss configure -state disabled
      .epgscan.all.opt.refresh configure -state disabled
      if {$is_unix} {
         .epgscan.all.opt.xawtv configure -state disabled
      }
   } else {
      # check if the popup window still exists
      if {[string length [info commands .epgscan.cmd]] > 0} {
         # release input focus
         grab release .epgscan
         # disable "Abort" button, re-enable others
         .epgscan.cmd.start configure -state normal
         .epgscan.cmd.stop configure -state disabled
         .epgscan.cmd.help configure -state normal
         .epgscan.cmd.dismiss configure -state normal
         # enable option checkboxes only if they were enabled before the scan
         if {[llength $prov_freqs] > 0} {
            .epgscan.all.opt.refresh configure -state normal
         }
         if {$is_unix && [C_Xawtv_Enabled]} {
            .epgscan.all.opt.xawtv configure -state normal
         }
      }
   }
}

##  --------------------------------------------------------------------------
##  Helper functions for selecting and ordering list items
##

proc SelBoxCreate {lbox arr_ailist arr_selist arr_names} {
   upvar $arr_ailist ailist
   upvar $arr_selist selist
   upvar $arr_names names

   ## first column: listbox with all netwops in AI order
   listbox $lbox.ailist -exportselection false -setgrid true -height 10 -width 12 -selectmode extended -relief ridge
   pack $lbox.ailist -fill x -anchor nw -side left -pady 10 -padx 10 -fill y -expand 1
   bind $lbox.ailist <ButtonPress-1> [list + after idle [list SelBoxButtonPress $lbox orig]]

   ## second column: command buttons
   frame $lbox.cmd
   button $lbox.cmd.add -text "add" -command [list SelBoxAddItem $lbox $arr_ailist $arr_selist $arr_names] -width 7
   pack $lbox.cmd.add -side top -anchor nw -pady 10
   frame $lbox.cmd.updown
   button $lbox.cmd.updown.up -bitmap "bitmap_ptr_up" -command [list SelBoxShiftUpItem $lbox.selist $arr_selist $arr_names]
   pack $lbox.cmd.updown.up -side left -fill x -expand 1
   button $lbox.cmd.updown.down -bitmap "bitmap_ptr_down" -command [list SelBoxShiftDownItem $lbox.selist $arr_selist $arr_names]
   pack $lbox.cmd.updown.down -side left -fill x -expand 1
   pack $lbox.cmd.updown -side top -anchor nw -fill x
   button $lbox.cmd.delnet -text "delete" -command [list SelBoxRemoveItem $lbox $arr_selist] -width 7
   pack $lbox.cmd.delnet -side top -anchor nw -pady 10
   pack $lbox.cmd -side left -anchor nw -pady 10 -padx 5 -fill y

   ## third column: selected providers in selected order
   listbox $lbox.selist -exportselection false -setgrid true -height 10 -width 12 -selectmode extended -relief ridge
   pack $lbox.selist -fill x -anchor nw -side left -pady 10 -padx 10 -fill y -expand 1
   bind $lbox.selist <ButtonPress-1> [list + after idle [list SelBoxButtonPress $lbox sel]]

   # fill the listboxes
   foreach item $ailist { $lbox.ailist insert end $names($item) }
   foreach item $selist { $lbox.selist insert end $names($item) }

   # adjust the listbox size to fit exactly the max. number of entries
   $lbox.ailist configure -height [llength $ailist]
   $lbox.selist configure -height [llength $ailist]

   # initialize command button state
   # (all disabled until an item is selected from either the left or right list)
   after idle [list SelBoxButtonPress $lbox none]
}

# selected items in the AI CNI list are appended to the selection list
proc SelBoxAddItem {lbox arr_ailist arr_selist arr_names} {
   upvar $arr_ailist ailist
   upvar $arr_selist selist
   upvar $arr_names names

   foreach index [$lbox.ailist curselection] {
      set cni [lindex $ailist $index]
      if {[lsearch -exact $selist $cni] == -1} {
         # append the selected item to the right listbox
         lappend selist $cni
         $lbox.selist insert end $names($cni)
         # select the newly inserted item
         $lbox.selist selection set end
      }
   }

   # update command button states and clear selection in the left listbox
   after idle [list SelBoxButtonPress $lbox sel]
}

# all selected items are removed from the list
proc SelBoxRemoveItem {lbox arr_selist} {
   upvar $arr_selist selist

   foreach index [lsort -integer -decreasing [$lbox.selist curselection]] {
      $lbox.selist delete $index
      set selist [lreplace $selist $index $index]
   }

   # update command button states (no selection -> all disabled)
   after idle [list SelBoxButtonPress $lbox none]
}

# move all selected items up by one row
# - the selected items may be non-consecutive
# - the first row must not be selected
proc SelBoxShiftUpItem {lbox arr_selist arr_names} {
   upvar $arr_selist selist
   upvar $arr_names names

   set el [lsort -integer -increasing [$lbox curselection]]
   if {[lindex $el 0] > 0} {
      foreach index $el {
         # remove the item in the listbox widget above the shifted one
         $lbox delete [expr $index - 1]
         # re-insert the just removed item below the shifted one
         $lbox insert $index $names([lindex $selist [expr $index - 1]])

         # perform the same exchange in the associated list
         set selist [lreplace $selist [expr $index - 1] $index \
                              [lindex $selist $index] \
                              [lindex $selist [expr $index - 1]]]
      }
   }
}

# move all selected items down by one row
proc SelBoxShiftDownItem {lbox arr_selist arr_names} {
   upvar $arr_selist selist
   upvar $arr_names names

   set el [lsort -integer -decreasing [$lbox curselection]]
   if {[lindex $el 0] < [expr [llength $selist] - 1]} {
      foreach index $el {
         $lbox delete [expr $index + 1]
         $lbox insert $index $names([lindex $selist [expr $index + 1]])
         set selist [lreplace $selist $index [expr $index + 1] \
                              [lindex $selist [expr $index + 1]] \
                              [lindex $selist $index]]
      }
   }
}

# called after button press in left or right listbox
proc SelBoxButtonPress {lbox which} {
   # clear the selection in the opposite listbox
   if {[string compare $which "orig"] == 0} {
      $lbox.selist selection clear 0 end
   } else {
      $lbox.ailist selection clear 0 end
   }

   # selection in the left box <--> "add" enabled
   if {[llength [$lbox.ailist curselection]] > 0} {
      $lbox.cmd.add configure -state normal
   } else {
      $lbox.cmd.add configure -state disabled
   }

   # selection in the right box <--> "delete" & "shift up/down" enabled
   if {[llength [$lbox.selist curselection]] > 0} {
      $lbox.cmd.updown.up configure -state normal
      $lbox.cmd.updown.down configure -state normal
      $lbox.cmd.delnet configure -state normal
   } else {
      $lbox.cmd.updown.up configure -state disabled
      $lbox.cmd.updown.down configure -state disabled
      $lbox.cmd.delnet configure -state disabled
   }
}

##  --------------------------------------------------------------------------
##  Network selection popup
##
set netsel_popup 0

proc PopupNetwopSelection {} {
   global netsel_popup
   global netsel_prov netsel_ailist netsel_selist netsel_names
   global cfnetwops

   if {$netsel_popup == 0} {
      # get CNI of currently selected provider (or 0 if db is empty)
      set netsel_prov [C_GetCurrentDatabaseCni]
      if {$netsel_prov != 0} {
         CreateTransientPopup .netsel "Network Selection"
         set netsel_popup 1

         # fetch CNI list from AI block in database
         # as a side effect this function stores all netwop names into the array netsel_names
         set netsel_ailist [C_GetAiNetwopList 0 netsel_names allmerged]
         ApplyUserNetnameCfg netsel_names
         # initialize list of user-selected CNIs
         if {[info exists cfnetwops($netsel_prov)]} {
            set netsel_selist [lindex $cfnetwops($netsel_prov) 0]
         } else {
            set netsel_selist $netsel_ailist
         }

         SelBoxCreate .netsel netsel_ailist netsel_selist netsel_names

         menubutton .netsel.cmd.copy -text "copy" -menu .netsel.cmd.copy.men -relief raised -borderwidth 2
         pack .netsel.cmd.copy -side top -anchor nw -pady 20 -fill x
         menu .netsel.cmd.copy.men -tearoff 0 -postcommand {PostDynamicMenu .netsel.cmd.copy.men NetselCreateCopyMenu}

         button .netsel.cmd.help -text "Help" -width 7 -command {PopupHelp $helpIndex(Configuration) "Select networks"}
         button .netsel.cmd.abort -text "Abort" -width 7 -command {destroy .netsel}
         button .netsel.cmd.save -text "Save" -width 7 -command {SaveSelectedNetwopList}
         pack .netsel.cmd.help .netsel.cmd.abort .netsel.cmd.save -side bottom -anchor sw
         bind .netsel.cmd <Destroy> {+ set netsel_popup 0}
      } else {
         # no AI block in database
         tk_messageBox -type ok -default ok -icon error -message "Cannot configure networks without a provider selected."
      }
   } else {
      raise .netsel
   }
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

   # merged database: merge the requested networks
   if {$netsel_prov == 0x00FF} {
      C_ProvMerge_Start
   }

   # save list into rc/ini file
   UpdateRcFile
   after idle C_UpdateNetwopList

   # close popup
   destroy .netsel
}

# post the menu
proc NetselCreateCopyMenu {widget} {
   global netsel_popup netsel_prov cfnetwops

   if {$netsel_popup} {
      set provlist [C_GetProvCnisAndNames]
      lappend provlist 0x00FF "Merged"
      set count 0
      foreach {cni name} $provlist {
         if {($cni != $netsel_prov) && [array exists cfnetwops] && [info exists cfnetwops($cni)]} {
            $widget add command -label "Copy from $name" -command [list NetselCopyNetwopList $cni]
            incr count
         }
      }
   }
   # if the menu is empty, add a note
   if {$count == 0} {
      $widget add command -label "No other providers available" -state disabled
   }
}

# copy the netwop selection and order from another provider
proc NetselCopyNetwopList {copycni} {
   global netsel_popup netsel_prov cfnetwops
   global netsel_selist netsel_ailist netsel_names

   if {$netsel_popup} {
      if {($copycni != $netsel_prov) && [array exists cfnetwops] && [info exists cfnetwops($copycni)]} {

         set netsel_selist {}
         .netsel.selist delete 0 end
         foreach cni [lindex $cfnetwops($copycni) 0] {
            if {[lsearch -exact $netsel_ailist $cni] != -1} {
               lappend netsel_selist $cni
               .netsel.selist insert end $netsel_names($cni)
            }
         }
         set suppressed [lindex $cfnetwops($copycni) 1]
         foreach cni $netsel_ailist {
            if {([lsearch -exact $suppressed $cni] == -1) && ([lsearch -exact $netsel_selist $cni] == -1)} {
               lappend netsel_selist $cni
               .netsel.selist insert end $netsel_names($cni)
            }
         }
      }
   }
}

## ---------------------------------------------------------------------------
## Update network selection configuration after AI update
## - returns list of indices of suppressed netwops (for prefiltering)
## - note: all CNIs have to be in the format 0x%04X
##
proc UpdateProvCniTable {prov} {
   global cfnetwops netwop_map netselmenu

   # fetch CNI list from AI block in database
   set ailist [C_GetAiNetwopList 0 {}]
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
      set unused($cni) 1
      incr index
   }

   # step #1: collect user-selected networks in the user-defined order
   set nlidx 0
   foreach cni $selist {
      if {[info exists unused($cni)]} {
         # CNI still exists in actual AI -> add it
         set netwop_map($nlidx) $order($cni)
         unset unused($cni)
         # increment index (only when item is not deleted)
         incr nlidx
      } else {
         # CNI no longer exists -> remove from selection, do not add to filter bar
         set selist [lreplace $selist $nlidx $nlidx]
      }
   }

   # step #2: remove suppressed CNIs from the list
   set index 0
   foreach cni $suplist {
      if {[info exists unused($cni)]} {
         unset unused($cni)
         incr index
      } else {
         # CNI no longer exists -> remove from suppressed list
         set suplist [lreplace $suplist $index $index]
      }
   }

   # step #3: check for unreferenced CNIs and append them to the list
   foreach cni $ailist {
      if [info exists unused($cni)] {
         lappend selist $cni
         set netwop_map($nlidx) $order($cni)
         incr nlidx
      }
   }

   # save the new config to the rc file
   set cfnetwops($prov) [list $selist $suplist]
   UpdateRcFile

   # fill network filter menus according to the new network list
   UpdateNetwopFilterBar

   # return list of indices of suppressed netwops
   set supidx {}
   foreach cni $suplist {
      lappend supidx $order($cni)
   }
   return $supidx
}

# update the network filter menus
proc UpdateNetwopFilterBar {} {
   global cfnetwops

   # fetch CNI of current db (may be 0 if none available)
   set prov [C_GetCurrentDatabaseCni]

   # fetch CNI list and netwop names from AI block in database
   set ailist [C_GetAiNetwopList 0 netnames]
   ApplyUserNetnameCfg netnames

   if [info exists cfnetwops($prov)] {
      set ailist [lindex $cfnetwops($prov) 0]
   }

   .all.netwops.list delete 1 end
   .all.netwops.list selection set 0
   .menubar.filter.netwops delete 1 end

   set nlidx 0
   foreach cni $ailist {
      .all.netwops.list insert end $netnames($cni)
      .menubar.filter.netwops add checkbutton -label $netnames($cni) -variable netselmenu($nlidx) -command [list SelectNetwopMenu $nlidx]
      incr nlidx
   }
   .all.netwops.list configure -height [expr [llength $ailist] + 1]
}

# remove elements from a list that are not member of a reference list
proc RemoveObsoleteCnisFromList {alist ref_list} {
   upvar $alist cni_list

   set idx 0
   foreach cni $cni_list {
      if {[lsearch -exact $ref_list $cni] >= 0} {
         incr idx
      } else {
         set cni_list [lreplace $cni_list $idx $idx]
      }
   }
}


##  --------------------------------------------------------------------------
##  Configure individual names for networks
##
set netname_popup 0

proc NetworkNamingPopup {} {
   global cfnetnames prov_selection cfnetwops
   global netname_ailist netname_names netname_idx netname_xawtv netname_automatch
   global netname_prov_cnis netname_prov_names netname_provnets
   global netname_entry
   global netname_popup font_fixed is_unix

   if {$netname_popup == 0} {
      set netname_popup 1

      # build list of available providers
      set netname_prov_cnis {}
      foreach {cni name} [C_GetProvCnisAndNames] {
         # build list of CNIs
         lappend netname_prov_cnis $cni
         # build array of provider network names
         set netname_prov_names($cni) $name
      }
      # sort provider list according to user preferences
      set netname_prov_cnis [SortProvList $netname_prov_cnis]

      if {[llength $netname_prov_cnis] == 0} {
         # no providers found -> abort
         tk_messageBox -type ok -icon info -message "There are no providers available yet.\nPlease start a provider scan from the Configure menu."
         return
      }

      set netname_ailist {}
      # retrieve all networks from all providers
      foreach prov $netname_prov_cnis {
         array unset tmparr
         set cnilist [C_GetAiNetwopList $prov tmparr]
         foreach cni $cnilist {
            set netname_provnets($prov,$cni) $tmparr($cni)
         }

         if [info exists cfnetwops($prov)] {
            foreach cni [lindex $cfnetwops($prov) 0] {
               if [info exists tmparr($cni)] {
                  if {[lsearch -exact $netname_ailist $cni] == -1} {
                     lappend netname_ailist $cni
                  }
                  unset tmparr($cni)
               }
            }
         }
         foreach cni $cnilist {
            if [info exists tmparr($cni)] {
               if {[lsearch -exact $netname_ailist $cni] == -1} {
                  lappend netname_ailist $cni
               }
            }
         }
      }
      array unset tmparr

      # re-sort CNI list for the merged database
      if {[info exists cfnetwops(0x00FF)] && ([C_GetCurrentDatabaseCni] == 0x00FF)} {
         set tmp {}
         foreach cni [lindex $cfnetwops(0x00FF) 0] {
            set idx [lsearch -exact $netname_ailist $cni]
            if {$idx >= 0} {
               lappend tmp $cni
               set netname_ailist [lreplace $netname_ailist $idx $idx]
            }
         }
         set netname_ailist [concat $tmp $netname_ailist]
      }

      # build array with all names from .xawtv rc file
      if $is_unix {
         set xawtv_list [C_Xawtv_GetStationNames]
         foreach name $xawtv_list {
            set netname_xawtv($name) $name
            regsub -all -- {[^a-zA-Z0-9]*} $name {} tmp
            set netname_xawtv([string tolower $tmp]) $name
         }
      } else {
         set xawtv_list {}
      }

      # copy or build an array with the currently configured network names
      set netname_automatch {}
      set xawtv_auto_match 0
      foreach cni $netname_ailist {
         if [info exists cfnetnames($cni)] {
            set netname_names($cni) $cfnetnames($cni)
         } else {
            # no name yet defined -> search best among all providers
            lappend netname_automatch $cni
            foreach prov $netname_prov_cnis {
               if [info exists netname_provnets($prov,$cni)] {
                  regsub -all -- {[^a-zA-Z0-9]*} $netname_provnets($prov,$cni) {} name
                  set name [string tolower $name]
                  if [info exists netname_xawtv($netname_provnets($prov,$cni))] {
                     incr xawtv_auto_match
                     set netname_names($cni) $netname_xawtv($netname_provnets($prov,$cni))
                     break
                  } elseif [info exists netname_xawtv($name)] {
                     incr xawtv_auto_match
                     set netname_names($cni) $netname_xawtv($name)
                     break
                  } elseif {![info exists netname_names($cni)]} {
                     set netname_names($cni) $netname_provnets($prov,$cni)
                  }
               }
            }
            if {![info exists netname_names($cni)]} {
               # should never happen
               set netname_names($cni) "undefined"
            }
         }
      }

      CreateTransientPopup .netname "Network name configuration"

      ## first column: listbox with all netwops in AI order
      frame .netname.list
      listbox .netname.list.ailist -exportselection false -setgrid true -height 20 -width 0 -selectmode single -relief ridge -yscrollcommand {.netname.list.sc set}
      pack .netname.list.ailist -anchor nw -side left -fill both -expand 1
      scrollbar .netname.list.sc -orient vertical -command {.netname.list.ailist yview}
      pack .netname.list.sc -side left -fill y
      pack .netname.list -side left -fill both -pady 10 -padx 10
      bind .netname.list.ailist <ButtonPress-1> [list + after idle NetworkNameSelection]
      bind .netname.list.ailist <space>         [list + after idle NetworkNameSelection]

      ## second column: info and commands for the selected network
      frame .netname.cmd
      # first row: entry field
      entry .netname.cmd.myname -textvariable netname_entry -font $font_fixed
      pack .netname.cmd.myname -side top -anchor nw -fill x
      trace variable netname_entry w NetworkNameEdited

      # second row: xawtv selection
      if $is_unix {
         frame .netname.cmd.fx
         label .netname.cmd.fx.lab -text "In .xawtv:  "
         pack .netname.cmd.fx.lab -side left -anchor w
         menubutton .netname.cmd.fx.mb -menu .netname.cmd.fx.mb.men -takefocus 1 \
                                       -relief raised -borderwidth 2
         if [array exists netname_xawtv] {
            menu .netname.cmd.fx.mb.men -tearoff 0
         } else {
            .netname.cmd.fx.mb configure -state disabled -text "none"
         }
         pack .netname.cmd.fx.mb -side right -anchor e
         pack .netname.cmd.fx -side top -fill x -pady 5

         # third row: closest match
         frame .netname.cmd.fm
         label .netname.cmd.fm.lab -text "Closest match: "
         pack .netname.cmd.fm.lab -side left -anchor w
         button .netname.cmd.fm.match -command NetworkNameUseMatch
         pack .netname.cmd.fm.match -side left -anchor e -fill x -expand 1
         pack .netname.cmd.fm -side top -fill x -pady 5
         if {![array exists netname_xawtv]} {
            .netname.cmd.fm.match configure -state disabled -text "none"
         }
      }

      # fourth row: provider name selection
      label .netname.cmd.lprovnams -text "Names used by providers:"
      pack .netname.cmd.lprovnams -side top -anchor nw -pady 10
      listbox .netname.cmd.provnams -exportselection false -height [llength $netname_prov_cnis] -width 0 -selectmode single
      pack .netname.cmd.provnams -side top -anchor n -fill x
      bind .netname.cmd.provnams <ButtonPress-1> [list + after idle NetworkNameProvSelection]
      bind .netname.cmd.provnams <space>         [list + after idle NetworkNameProvSelection]

      # bottom row: command buttons
      button .netname.cmd.save -text "Save" -width 7 -command NetworkNamesSave
      button .netname.cmd.abort -text "Abort" -width 7 -command NetworkNamesAbort
      button .netname.cmd.help -text "Help" -width 7 -command {PopupHelp $helpIndex(Configuration) "Network names"}
      pack .netname.cmd.help .netname.cmd.abort .netname.cmd.save -side bottom -anchor sw

      pack .netname.cmd -side left -fill y -anchor n -pady 10 -padx 10

      bind .netname.cmd <Destroy> {+ set netname_popup 0}
      focus .netname.cmd.myname

      if {$is_unix && [array exists netname_xawtv]} {
         # insert xawtv station names to popup menu & measure max. name width
         set mbfont [.netname.cmd.fx.mb cget -font]
         set mbwidth 0
         foreach name $xawtv_list {
            .netname.cmd.fx.mb.men add command -label $name -command [list NetworkNameXawtvSelection $name]
            set mbwidthc [font measure $mbfont $name]
            if {$mbwidthc > $mbwidth} {set mbwidth $mbwidthc}
         }
         # set width of xawtv menu buttons to max. width of all station names
         # convert pixel width to character count
         set mbwidth [expr 1 + $mbwidth / [font measure $mbfont "0"]]
         .netname.cmd.fx.mb configure -width [expr $mbwidth + 2]
         .netname.cmd.fm.match configure -width $mbwidth
      }

      # insert network names from all providers into listbox on the left
      foreach cni $netname_ailist {
         .netname.list.ailist insert end $netname_names($cni)
         if {![NetworkNameIsInXawtv $netname_names($cni)]} {
            .netname.list.ailist itemconfigure end -foreground red -selectforeground red
         }
      }
      # set the cursor onto the first network in the listbox
      set netname_idx -1
      .netname.list.ailist selection set 0
      NetworkNameSelection

      # notify the user if names have been automatically modified
      if [array exists netname_xawtv] {
         set automatch_count $xawtv_auto_match
      } else {
         set automatch_count [llength $netname_automatch]
      }
      if {$automatch_count > 0} {
         if {$automatch_count > 1} {
            set plural "s of $automatch_count new networks have"
         } else {
            set plural " of one new network has"
         }
         update
         tk_messageBox -type ok -icon info -parent .netname \
            -message "The name$plural been automatically selected among the names in the available provider databases. Leave the dialog with 'Save' to keep the new list."
      }

   } else {
      raise .netname
   }
}

# Save changed name for the currently selected network
proc NetworkNameSaveEntry {} {
   global netname_ailist netname_names netname_idx netname_automatch
   global netname_entry

   if {$netname_idx != -1} {
      set cni [lindex $netname_ailist $netname_idx]

      if {[string compare $netname_names($cni) $netname_entry] != 0} {

         # save the selected name
         set netname_names($cni) $netname_entry

         # update the network names listbox
         set sel [.netname.list.ailist curselection]
         .netname.list.ailist delete $netname_idx
         .netname.list.ailist insert $netname_idx $netname_names($cni)
         .netname.list.ailist selection set $sel
         if {![NetworkNameIsInXawtv $netname_names($cni)]} {
            .netname.list.ailist itemconfigure $netname_idx -foreground red -selectforeground red
         }

         # check if the same network was previously auto-matched
         set sel [lsearch -exact $netname_automatch $cni]
         if {$sel != -1} {
            # remove the CNI from the auto list
            set netname_automatch [concat [lrange $netname_automatch 0 [expr $sel - 1]] \
                                          [lrange $netname_automatch [expr $sel + 1] end]]
         }
      }
   }
}

# callback (variable trace) for change in the entry field
proc NetworkNameEdited {trname trops trcmd} {
   global netname_entry netname_ailist netname_idx netname_names netname_xawtv
   global is_unix

   if { $is_unix && [array exists netname_xawtv]} {

      if {$netname_idx != -1} {
         set cni [lindex $netname_ailist $netname_idx]

         regsub -all -- {[^a-zA-Z0-9]*} $netname_entry {} name
         set name [string tolower $name]

         if {[info exists netname_xawtv($netname_entry)]} {
            .netname.cmd.fm.match configure -text $netname_xawtv($netname_entry)
            if {[string compare $netname_xawtv($netname_entry) $netname_entry] == 0} {
               .netname.cmd.fm.match configure -foreground black -activeforeground black
            } else {
               .netname.cmd.fm.match configure -foreground red -activeforeground red
            }
         } elseif {[info exists netname_xawtv($name)]} {
            .netname.cmd.fm.match configure -foreground red -activeforeground red -text $netname_xawtv($name)
         } else {
            .netname.cmd.fm.match configure -foreground red -activeforeground red -text "none"
         }
      }
   }
}

# callback for "closest match" button
proc NetworkNameUseMatch {} {
   global netname_entry

   set netname_entry [.netname.cmd.fm.match cget -text]
}

# callback for selection of a network in the ailist
# -> requires update of the information displayed on the right
proc NetworkNameSelection {} {
   global netname_ailist netname_names netname_idx netname_xawtv
   global netname_prov_cnis netname_prov_names netname_provnets netname_provlist
   global netname_entry
   global is_unix

   set sel [.netname.list.ailist curselection]
   if {([string length $sel] > 0) && ($sel < [llength $netname_ailist]) && ($sel != $netname_idx)} {
      # save name of the previously selected network, if changed
      NetworkNameSaveEntry

      set netname_idx $sel

      # copy the name of the currently selected network into the entry field
      set cni [lindex $netname_ailist $netname_idx]
      set netname_entry $netname_names($cni)

      # display the matched name in the .xawtv file
      if {$is_unix && [array exists netname_xawtv]} {
         if {[info exists netname_xawtv($netname_names($cni))]} {
            .netname.cmd.fx.mb configure -text $netname_xawtv($netname_names($cni))
            .netname.cmd.fm.match configure -text "$netname_xawtv($netname_names($cni))" -foreground black
         } else {
            .netname.cmd.fx.mb configure -text "select"
            .netname.cmd.fm.match configure -text "none" -foreground red
         }
      }

      # rebuild the list of provider's network names
      set netname_provlist {}
      .netname.cmd.provnams delete 0 end
      foreach prov $netname_prov_cnis {
         if [info exists netname_provnets($prov,$cni)] {
            # the netname_provlist keeps track which providers are listed in the box
            lappend netname_provlist $prov
            .netname.cmd.provnams insert end "\[$netname_prov_names($prov)\]  $netname_provnets($prov,$cni)"
         }
      }
   }
}

# callback for selection of a name in the provider listbox
proc NetworkNameProvSelection {} {
   global netname_ailist netname_names netname_idx
   global netname_prov_cnis netname_prov_names netname_provnets netname_provlist
   global netname_entry

   set cni [lindex $netname_ailist $netname_idx]
   set sel [.netname.cmd.provnams curselection]
   if {([string length $sel] > 0) && ($sel < [llength $netname_provlist])} {
      set prov [lindex $netname_provlist $sel]

      if [info exists netname_provnets($prov,$cni)] {
         set netname_entry $netname_provnets($prov,$cni)
         .netname.list.ailist delete $netname_idx
         .netname.list.ailist insert $netname_idx $netname_entry
         .netname.list.ailist selection set $netname_idx
         if {![NetworkNameIsInXawtv $netname_names($cni)]} {
            .netname.list.ailist itemconfigure $netname_idx -foreground red -selectforeground red
         }
      }
   }
}

# callback for selection of a name in the xawtv menu
proc NetworkNameXawtvSelection {name} {
   global netname_entry netname_idx
   global is_unix

   set netname_entry $name
   if $is_unix {
      .netname.cmd.fx.mb configure -text $name
   }
   .netname.list.ailist delete $netname_idx
   .netname.list.ailist insert $netname_idx $netname_entry
   .netname.list.ailist selection set $netname_idx
}

# "Save" command button
proc NetworkNamesSave {} {
   global netname_ailist netname_names netname_idx netname_xawtv netname_automatch
   global netname_prov_cnis netname_prov_names netname_provnets netname_provlist
   global netname_entry
   global cfnetnames

   # save name of the currently selected network, if changed
   NetworkNameSaveEntry

   # save names to the rc/ini file
   array unset cfnetnames
   foreach cni $netname_automatch {
      # do not save failed auto-matches - a new provider's name for the CNI might match
      if {![NetworkNameIsInXawtv $netname_names($cni)]} {
         array unset netname_names $cni
      }
   }
   array set cfnetnames [array get netname_names]
   UpdateRcFile

   # update the network menus with the new names
   UpdateNetwopFilterBar

   # Redraw the PI listbox with the new network names
   C_RefreshPiListbox

   # close the window
   destroy .netname

   # free memory
   foreach var {netname_ailist netname_names netname_idx netname_xawtv netname_automatch
                netname_prov_cnis netname_prov_names netname_provnets netname_provlist
                netname_entry} {
      if [info exists $var] {unset $var}
   }
}

# "Abort" command button
proc NetworkNamesAbort {} {
   global netname_automatch
   global cfnetnames netname_names

   # save name of the currently selected network, if changed
   NetworkNameSaveEntry

   set changed 0
   if [array exists cfnetnames] {
      # check if any names have changed (or if there are new CNIs)
      foreach cni [array names netname_names] {
         # ignore automatic name config
         if {[lsearch -exact $netname_automatch $cni] == -1} {
            if {![info exists cfnetnames($cni)] || ([string compare $cfnetnames($cni) $netname_names($cni)] != 0)} {
               set changed 1
               break
            }
         }
      }
   }

   if $changed {
      set answer [tk_messageBox -type okcancel -icon warning -parent .netname -message "Discard all changes?"]
      if {[string compare $answer cancel] == 0} {
         return
      }
   } elseif {([llength $netname_automatch] > 0) || ![array exists cfnetnames]} {
      # failed auto-matches would not have been saved
      set auto 0
      foreach cni $netname_automatch {
         if [NetworkNameIsInXawtv $netname_names($cni)] {
            # there's at least one auto-matched name that would get saved
            set auto 1
            break
         }
      }
      if $auto {
         set answer [tk_messageBox -type okcancel -icon warning -parent .netname -message "Network names have been configured automatically. Really discard them?"]
         if {[string compare $answer cancel] == 0} {
            return
         }
      }
   }

   # close the popup window
   destroy .netname
}

# check if a user-define or auto-matched name is equivalent to .xawtv
proc NetworkNameIsInXawtv {name} {
   global netname_xawtv netname_names

   if [array exists netname_xawtv] {
      set result 0
      # check if the exact or similar (non-alphanums removed) name is known in xawtv
      if [info exists netname_xawtv($name)] {
         # check if the name in xawtv is exactly the same
         if {[string compare $name $netname_xawtv($name)] == 0} {
            set result 1
         }
      }
   } else {
      # no .xawtv found -> no checking
      set result 1
   }
   return $result
}


##  --------------------------------------------------------------------------
##  Replace provider-supplied network names with user-configured names
##
proc ApplyUserNetnameCfg {name_arr} {
   upvar $name_arr netnames
   global cfnetnames

   foreach cni [array names netnames] {
      if [info exists cfnetnames($cni)] {
         set netnames($cni) $cfnetnames($cni)
      }
   }
}

##  --------------------------------------------------------------------------
##  Browser columns selection popup
##

# this array defines the width of the columns (in pixels), the column
# heading, dropdown menu and the description in the config listbox.

array set colsel_tabs {
   title          {266 Title    FilterMenuAdd_Title      "Title"} \
   netname        {71  Network  FilterMenuAdd_Networks   "Network name"} \
   time           {83  Time     FilterMenuAdd_Time       "Running time"} \
   weekday        {30  Day      FilterMenuAdd_Date       "Day of week"} \
   day            {27  Date     FilterMenuAdd_Date       "Day of month"} \
   day_month      {41  Date     FilterMenuAdd_Date       "Day and month"} \
   day_month_year {71  Date     FilterMenuAdd_Date       "Day, month and year"} \
   pil            {74  VPS/PDC  none                     "VPS/PDC code"} \
   theme          {74  Theme    FilterMenuAdd_Themes     "Theme"} \
   sound          {44  Sound    FilterMenuAdd_Sound      "Sound"} \
   format         {41  Format   FilterMenuAdd_Format     "Format"} \
   ed_rating      {30  ER       FilterMenuAdd_EditorialRating "Editorial rating"} \
   par_rating     {30  PR       FilterMenuAdd_ParentalRating  "Parental rating"} \
   live_repeat    {44  L/R      FilterMenuAdd_LiveRepeat "Live or repeat"} \
   description    {15  I        none                     "Flag description"} \
   subtitles      {15  ST       FilterMenuAdd_Subtitles  "Flag subtitles"} \
}

# define presentation order for configuration listbox
set colsel_ailist [list \
   title netname time weekday day day_month day_month_year \
   pil theme sound format ed_rating par_rating live_repeat description subtitles]

# define default column configuration - is overridden by rc/ini config
set pilistbox_cols [list \
   weekday day_month time title netname]

set colsel_popup 0

# create the configuration popup window
proc PopupColumnSelection {} {
   global colsel_tabs colsel_ailist colsel_selist colsel_names
   global colsel_popup
   global pilistbox_cols

   if {$colsel_popup == 0} {
      CreateTransientPopup .colsel "Browser Columns Selection"
      set colsel_popup 1

      if {![array exists colsel_names]} {
         foreach name [array names colsel_tabs] {
            set colsel_names($name) [lindex $colsel_tabs($name) 3]
         }
      }
      set colsel_selist $pilistbox_cols

      SelBoxCreate .colsel colsel_ailist colsel_selist colsel_names
      .colsel.ailist configure -width 20
      .colsel.selist configure -width 20

      button .colsel.cmd.help -text "Help" -width 7 -command {PopupHelp $helpIndex(Configuration) "Select columns"}
      button .colsel.cmd.apply -text "Apply" -width 7 -command {ApplySelectedColumnList normal}
      button .colsel.cmd.quit -text "Dismiss" -width 7 -command {destroy .colsel}
      pack .colsel.cmd.help .colsel.cmd.quit .colsel.cmd.apply -side bottom -anchor sw
      bind .colsel.cmd <Destroy> {+ set colsel_popup 0}
   } else {
      raise .colsel
   }
}

##
##  Apply the settings to the PI browser listbox
##
proc ApplySelectedColumnList {mode} {
   global colsel_selist colsel_names colsel_tabs
   global showColumnHeader font_small
   global pilistbox_cols
   global is_unix

   if {[string compare $mode "initial"] == 0} {
      # not called from the popup -> load the current config into the temp var
      set colsel_selist $pilistbox_cols
   } elseif {![info exists colsel_selist]} {
      set colsel_selist $pilistbox_cols
   }

   # remove previous colum headers
   foreach head [info commands .all.pi.colheads.c*] {
      destroy $head
   }

   set tab_pos 0
   set tabs {}
   if {$showColumnHeader} {
      # create colum header menu buttons
      foreach col $colsel_selist {
         frame .all.pi.colheads.col_$col -width [lindex $colsel_tabs($col) 0] -height 14
         menubutton .all.pi.colheads.col_${col}.b -width 1 -cursor top_left_arrow \
                                                  -text [lindex $colsel_tabs($col) 1] -font $font_small
         if {$is_unix} {
            .all.pi.colheads.col_${col}.b configure -borderwidth 1 -relief raised
         } else {
            .all.pi.colheads.col_${col}.b configure -borderwidth 2 -relief ridge
         }
         pack .all.pi.colheads.col_${col}.b -fill x
         pack propagate .all.pi.colheads.col_${col} 0
         pack .all.pi.colheads.col_${col} -side left -anchor w

         if {[string compare [lindex $colsel_tabs($col) 2] "none"] != 0} {
            # create the drop-down menu below the header (the menu items are added dynamically)
            .all.pi.colheads.col_${col}.b configure -menu .all.pi.colheads.col_${col}.b.men
            .all.pi.colheads.col_${col}.b configure -menu .all.pi.colheads.col_${col}.b.men
            menu .all.pi.colheads.col_${col}.b.men -postcommand [list PostDynamicMenu .all.pi.colheads.col_${col}.b.men [lindex $colsel_tabs($col) 2]]
         }

         incr tab_pos [lindex $colsel_tabs($col) 0]
         lappend tabs ${tab_pos}
      }
      if {[info exists col] && ([string length [info commands .all.pi.colheads.col_${col}]] > 0)} {
         # increase width of the last header button to accomodate for text widget borderwidth
         .all.pi.colheads.col_${col} configure -width [expr 5 + [.all.pi.colheads.col_${col} cget -width]]
      }
   } else {
      # create an invisible frame to set the width of the text widget
      foreach col $colsel_selist {
         incr tab_pos [lindex $colsel_tabs($col) 0]
         lappend tabs ${tab_pos}
      }
      frame .all.pi.colheads.c0 -width "[expr $tab_pos + 2]"
      pack .all.pi.colheads.c0 -side left -anchor w
   }

   # configure tab-stops in text widget
   .all.pi.list.text tag configure past -tab $tabs
   .all.pi.list.text tag configure now -tab $tabs
   .all.pi.list.text tag configure then -tab $tabs

   if {[string compare $mode "initial"] != 0} {
      # unless suppressed, update the settings in the listbox module and then redraw the content
      C_PiOutput_CfgColumns
      C_RefreshPiListbox
      # save the config to the rc-file
      set pilistbox_cols $colsel_selist
      UpdateRcFile
   }
}

##
##  Creating menus for the column heads
##
proc FilterMenuAdd_Time {widget} {
   $widget add command -label "Now" -command {C_PiListBox_GotoTime now}
   $widget add command -label "Next" -command {C_PiListBox_GotoTime next}

   set now        [clock seconds] 
   set start_time [expr $now - ($now % (2*60*60)) + (2*60*60)]
   set hour       [expr ($start_time % (24*60*60)) / (60*60)]

   for {set i 0} {$i < 24} {incr i 2} {
      $widget add command -label "$hour:00" -command [list C_PiListBox_GotoTime $start_time]
      incr start_time [expr 2*60*60]
      set hour [expr ($hour + 2) % 24]
   }
}

proc FilterMenuAdd_Date {widget} {
   set start_time [clock seconds]
   $widget add command -label "Today, [clock format $start_time -format {%d. %b. %Y}]" -command {C_PiListBox_GotoTime now}
   incr start_time [expr 24*60*60]
   $widget add command -label "Tomorrow, [clock format $start_time -format {%d. %b. %Y}]" -command [list C_PiListBox_GotoTime $start_time]

   for {set i 2} {$i < 5} {incr i} {
      incr start_time [expr 24*60*60]
      $widget add command -label [clock format $start_time -format {%A, %d. %b. %Y}] -command [list C_PiListBox_GotoTime $start_time]
   }
}

proc FilterMenuAdd_Networks {widget} {
   global cfnetwops netselmenu

   $widget add command -label "All networks" -command {ResetNetwops; SelectNetwop}

   # fetch CNI list from AI block in database
   # as a side effect this function stores all netwop names into the array netsel_names
   set netsel_prov [C_GetCurrentDatabaseCni]
   set netsel_ailist [C_GetAiNetwopList 0 netsel_names]
   ApplyUserNetnameCfg netsel_names

   if {[info exists cfnetwops($netsel_prov)]} {
      set netsel_selist [lindex $cfnetwops($netsel_prov) 0]
   } else {
      set netsel_selist $netsel_ailist
   }

   # Add currently selected network as command button (unless it is already filtered)
   set cni [lindex [C_PiListBox_GetSelectedNetwop] 0]
   set nlidx [lsearch -exact $netsel_selist $cni]
   if {($nlidx != -1) && (![info exists netselmenu($nlidx)] || ($netselmenu($nlidx) == 0))} {
      $widget add command -label "Only $netsel_names($cni)" -command "ResetNetwops; set netselmenu($nlidx) 1; SelectNetwopMenu $nlidx"
   }

   # Add all networks as radio buttons
   $widget add separator
   set nlidx 0
   foreach cni $netsel_selist {
      $widget add checkbutton -label $netsel_names($cni) -variable netselmenu($nlidx) -command [list SelectNetwopMenu $nlidx]
      incr nlidx
   }
}

proc FilterMenuAdd_Title {widget} {
   global substr_grep_title substr_grep_descr substr_match_case substr_match_full
   global substr_pattern
   global substr_history

   $widget add command -label "Text search..." -command SubStrPopup
   if {[string length $substr_pattern] > 0} {
      $widget add command -label "Undo text search" -command {set substr_pattern {}; SubstrUpdateFilter}
   }

   # append the series menu (same as from the filter menu)
   $widget add cascade -label "Series" -menu ${widget}.series
   if {[string length [info commands ${widget}.series]] == 0} {
      menu ${widget}.series -postcommand [list PostDynamicMenu ${widget}.series CreateSeriesNetworksMenu]
   }

   # append shortcuts for the last 10 substring searches
   if {[llength $substr_history] > 0} {
      $widget add separator
      foreach item $substr_history {
         $widget add command -label [lindex $item 4] -command [concat SubstrSetFilter $item]
      }
      $widget add separator
      $widget add command -label "Clear search history" -command {set substr_history {}; UpdateRcFile}
   }
}

##  --------------------------------------------------------------------------
##  Provider merge popup
##
set provmerge_popup 0

proc PopupProviderMerge {} {
   global provmerge_popup
   global provmerge_ailist provmerge_selist provmerge_names provmerge_cf
   global prov_merge_cnis prov_merge_cf
   global ProvmergeOptLabels
   global cfnetwops

   if {$provmerge_popup == 0} {
      # get CNIs and names of all known providers
      set provmerge_ailist {}
      foreach {cni name} [C_GetProvCnisAndNames] {
         lappend provmerge_ailist $cni
         set provmerge_names($cni) $name
      }
      set provmerge_ailist [SortProvList $provmerge_ailist]
      if {[info exists prov_merge_cnis]} {
         set provmerge_selist {}
         foreach cni $prov_merge_cnis {
            if {[lsearch -exact $provmerge_ailist $cni] != -1} {
               lappend provmerge_selist $cni
            }
         }
      } else {
         set provmerge_selist $provmerge_ailist
      }
      if {[info exists prov_merge_cf]} {
         array set provmerge_cf $prov_merge_cf
      }

      if {[llength $provmerge_ailist] == 0} {
         # no providers found -> abort
         tk_messageBox -type ok -icon info -message "There are no providers available yet.\nPlease start a provider scan from the Configure menu."
         return
      }

      # create the popup window
      CreateTransientPopup .provmerge "Merging provider databases"
      set provmerge_popup 1

      message .provmerge.msg -aspect 800 -text "Select the providers who's databases you want to merge and their priority:"
      pack .provmerge.msg -side top -expand 1 -fill x -pady 5

      # create the two listboxes for database selection
      frame .provmerge.lb
      SelBoxCreate .provmerge.lb provmerge_ailist provmerge_selist provmerge_names
      pack .provmerge.lb -side top

      # create menu for option sub-windows
      array set ProvmergeOptLabels {
         cftitle "Title"
         cfdescr "Description"
         cfthemes "Themes"
         cfseries "Series codes"
         cfsortcrit "Sorting Criteria"
         cfeditorial "Editorial rating"
         cfparental "Parental rating"
         cfsound "Sound format"
         cfformat "Picture format"
         cfrepeat "Repeat flag"
         cfsubt "Subtitle flag"
         cfmisc "misc. features"
         cfvps "VPS/PDC label"
      }
      menubutton .provmerge.mb -menu .provmerge.mb.men -relief raised -borderwidth 1 -text "Configure"
      pack .provmerge.mb -side top
      menu .provmerge.mb.men -tearoff 0
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cftitle} -label $ProvmergeOptLabels(cftitle)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfdescr} -label $ProvmergeOptLabels(cfdescr)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfthemes} -label $ProvmergeOptLabels(cfthemes)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfseries} -label $ProvmergeOptLabels(cfseries)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfsortcrit} -label $ProvmergeOptLabels(cfsortcrit)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfeditorial} -label $ProvmergeOptLabels(cfeditorial)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfparental} -label $ProvmergeOptLabels(cfparental)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfsound} -label $ProvmergeOptLabels(cfsound)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfformat} -label $ProvmergeOptLabels(cfformat)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfrepeat} -label $ProvmergeOptLabels(cfrepeat)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfsubt} -label $ProvmergeOptLabels(cfsubt)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfmisc} -label $ProvmergeOptLabels(cfmisc)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfvps} -label $ProvmergeOptLabels(cfvps)
      .provmerge.mb.men add separator
      .provmerge.mb.men add command -command {ProvMerge_Reset} -label "Reset"

      # create cmd buttons at bottom
      frame .provmerge.cmd
      button .provmerge.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Merged databases)}
      button .provmerge.cmd.abort -text "Abort" -width 5 -command {ProvMerge_Quit Abort}
      button .provmerge.cmd.ok -text "Ok" -width 5 -command {ProvMerge_Quit Ok}
      pack .provmerge.cmd.help .provmerge.cmd.abort .provmerge.cmd.ok -side left -padx 10
      pack .provmerge.cmd -side top -pady 10
      bind .provmerge.cmd <Destroy> {+ set provmerge_popup 0; ProvMerge_Quit Abort}
   } else {
      raise .provmerge
   }
}

# create menu for option selection
set provmergeopt_popup 0

proc PopupProviderMergeOpt {cfoption} {
   global provmerge_selist provmerge_cf provmerge_names
   global provmerge_popup provmergeopt_popup
   global ProvmergeOptLabels

   if {$provmerge_popup == 1} {
      if {$provmergeopt_popup == 0} {
         CreateTransientPopup .provmergeopt "Database Selection for $ProvmergeOptLabels($cfoption)"
         set provmergeopt_popup 1

         # initialize the result array: default is all databases
         if {![info exists provmerge_cf($cfoption)]} {
            set provmerge_cf($cfoption) $provmerge_selist
         }

         message .provmergeopt.msg -aspect 800 -text "Select the databases from which the [string tolower $ProvmergeOptLabels($cfoption)] shall be extracted:"
         pack .provmergeopt.msg -side top -expand 1 -fill x -pady 5

         # create the two listboxes for database selection
         frame .provmergeopt.lb
         SelBoxCreate .provmergeopt.lb provmerge_selist provmerge_cf($cfoption) provmerge_names
         pack .provmergeopt.lb -side top

         # create cmd buttons at bottom
         frame .provmergeopt.cb
         button .provmergeopt.cb.ok -text "Ok" -width 7 -command {destroy .provmergeopt}
         pack .provmergeopt.cb.ok -side left -padx 10
         pack .provmergeopt.cb -side top -pady 10
         bind .provmergeopt.cb <Destroy> {+ set provmergeopt_popup 0}
      } else {
         # the popup is already opened -> just exchange the contents
         wm title .provmergeopt "Database Selection for $ProvmergeOptLabels($cfoption)"

         if {![info exists provmerge_cf($cfoption)]} {
            set provmerge_cf($cfoption) $provmerge_selist
         }

         .provmergeopt.msg configure -text "Select the databases from which the [string tolower $ProvmergeOptLabels($cfoption)] shall be extracted:"

         # create the two listboxes for database selection
         foreach widget [info commands .provmergeopt.lb.*] {
            destroy $widget
         }
         SelBoxCreate .provmergeopt.lb provmerge_selist provmerge_cf($cfoption) provmerge_names

         # make the sure the popup is not obscured by other windows
         raise .provmergeopt
      }
   }
}

# Reset the attribute configuration
proc ProvMerge_Reset {} {
   global provmerge_cf

   if {[info exists provmerge_cf]} {
      array unset provmerge_cf
   }
}

# close the provider merge popups and free the state variables
proc ProvMerge_Quit {cause} {
   global provmerge_popup provmergeopt_popup
   global provmerge_selist provmerge_cf
   global prov_merge_cnis prov_merge_cf
   global provmerge_names ProvmergeOptLabels

   # check the configuration parameters for consistancy
   if {[string compare $cause "Ok"] == 0} {

      # check if the provider list is empty
      if {[llength $provmerge_selist] == 0} {
         tk_messageBox -type ok -icon error -parent .provmerge -message "You have to add at least one provider from the left to the listbox on the right."
         return
      }

      # compare the new configuration with the one stored in global variables
      set tmp_cf {}
      if {[info exists provmerge_cf]} {
         foreach {name vlist} [array get provmerge_cf] {
            if {[string compare $vlist $provmerge_selist] != 0} {
               foreach cni $vlist {
                  if {[lsearch -exact $provmerge_selist $cni] == -1} {
                     if {[info exists provmerge_names($cni)]} {
                        set provname " ($provmerge_names($cni))"
                     } else {
                        set provname {}
                     }
                     tk_messageBox -type ok -icon error -parent .provmerge -message "The provider list for [string tolower $ProvmergeOptLabels($name)] contains a database$provname that's not in the main selection. Either remove the provider or use 'Reset' to reset the configuration."
                     return
                  }
               }
               lappend tmp_cf $name $vlist
            }
         }
      }
   }

   # close database selection popup (main)
   if {$provmerge_popup} {
      set provmerge_popup 0
      # undo binding to avoid recursive call to this proc
      bind .provmerge.cmd <Destroy> {}
      destroy .provmerge
   }
   # close attribute selection popup (slave)
   if {$provmergeopt_popup} {
      destroy .provmergeopt
   }
   # free space from arrays
   if {[info exists ProvmergeOptLabels]} {
      unset ProvmergeOptLabels
   }

   # if closed with OK button, start the merge
   if {[string compare $cause "Ok"] == 0} {
      # save the configuration into global variables
      set prov_merge_cnis $provmerge_selist 
      set prov_merge_cf $tmp_cf

      # save the configuration into the rc/ini file
      UpdateRcFile

      # perform the merge and load the new database into the browser
      C_ProvMerge_Start
   }
}

## ---------------------------------------------------------------------------
## Sort a list of provider CNIs according to user preference
##
proc SortProvList {ailist} {
   global prov_selection sortProvListArr

   if {[info exists prov_selection] && ([llength $prov_selection] > 0)} {
      set idx 0
      foreach cni $prov_selection {
         set sortProvListArr($cni) $idx
         incr idx
      }
      foreach cni $ailist {
         if {![info exists sortProvListArr($cni)]} {
            set sortProvListArr($cni) $idx
            incr idx
         }
      }
      set result [lsort -command SortProvList_cmd $ailist]
      unset sortProvListArr
   } else {
      set result $ailist
   }
   return $result
}

# helper procedur for sorting the provider list
proc SortProvList_cmd {a b} {
   global sortProvListArr

   if {[info exists sortProvListArr($a)] && [info exists sortProvListArr($b)]} {
      if       {$sortProvListArr($a) < $sortProvListArr($b)} {
         return -1
      } elseif {$sortProvListArr($a) > $sortProvListArr($b)} {
         return  1
      } else {
         return 0
      }
   } else {
      return 0
   }
}

## ---------------------------------------------------------------------------
## Open popup for acquisition mode selection
## - can not be popped up if /dev/video is busy
##
set acqmode_popup 0

proc PopupAcqMode {} {
   global acqmode_popup
   global acqmode_ailist acqmode_selist acqmode_names
   global acqmode_sel
   global acq_mode acq_mode_cnis
   global is_unix

   if {$acqmode_popup == 0} {
      CreateTransientPopup .acqmode "Acquisition mode selection"
      set acqmode_popup 1

      # load list of providers
      set acqmode_ailist {}
      foreach {cni name} [C_GetProvCnisAndNames] {
         lappend acqmode_ailist $cni
         set acqmode_names($cni) $name
      }
      set acqmode_ailist [SortProvList $acqmode_ailist]
      set acqmode_selist $acqmode_ailist

      # initialize popup with current settings
      if {[info exists acq_mode]} {
         set acqmode_sel $acq_mode
      } else {
         set acqmode_sel "follow-ui"
      }

      # checkbuttons for modes
      frame .acqmode.mode
      if $is_unix {
         radiobutton .acqmode.mode.mode0 -text "Passive (no input selection)" -variable acqmode_sel -value "passive" -command UpdateAcqModePopup
         pack .acqmode.mode.mode0 -side top -anchor w
      }
      radiobutton .acqmode.mode.mode1 -text "External (don't touch TV tuner)" -variable acqmode_sel -value "external" -command UpdateAcqModePopup
      radiobutton .acqmode.mode.mode2 -text "Follow browser database" -variable acqmode_sel -value "follow-ui" -command UpdateAcqModePopup
      radiobutton .acqmode.mode.mode3 -text "Manually selected" -variable acqmode_sel -value "cyclic_2" -command UpdateAcqModePopup
      radiobutton .acqmode.mode.mode4 -text "Cyclic: Now->Near->All" -variable acqmode_sel -value "cyclic_012" -command UpdateAcqModePopup
      radiobutton .acqmode.mode.mode5 -text "Cyclic: Now->All" -variable acqmode_sel -value "cyclic_02" -command UpdateAcqModePopup
      radiobutton .acqmode.mode.mode6 -text "Cyclic: Near->All" -variable acqmode_sel -value "cyclic_12" -command UpdateAcqModePopup
      pack .acqmode.mode.mode1 .acqmode.mode.mode2 .acqmode.mode.mode3 .acqmode.mode.mode4 .acqmode.mode.mode5 .acqmode.mode.mode6 -side top -anchor w
      pack .acqmode.mode -side top -pady 10 -padx 10 -anchor w

      # create two listboxes for provider database selection
      frame .acqmode.lb
      UpdateAcqModePopup
      pack .acqmode.lb -side top

      # command buttons at the bottom of the window
      frame .acqmode.cmd
      button .acqmode.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Acquisition modes)}
      button .acqmode.cmd.abort -text "Abort" -width 5 -command {destroy .acqmode}
      button .acqmode.cmd.apply -text "Ok" -width 5 -command {QuitAcqModePopup}
      pack .acqmode.cmd.help .acqmode.cmd.abort .acqmode.cmd.apply -side left -padx 10
      pack .acqmode.cmd -side top -pady 10
      bind .acqmode.cmd <Destroy> {+ set acqmode_popup 0}
   } else {
      raise .acqmode
   }
}

# add or remove database selection popups after mode change
proc UpdateAcqModePopup {} {
   global acqmode_ailist acqmode_selist acqmode_names
   global acqmode_sel
   global acq_mode acq_mode_cnis

   if {[string compare -length 6 "cyclic" $acqmode_sel] == 0} {
      # manual selection -> add listboxes to allow selection of multiple databases and priority
      if {[string length [info commands .acqmode.lb.ailist]] == 0} {
         # listbox does not yet exist
         foreach widget [info commands .acqmode.lb.*] {
            destroy $widget
         }
         if {[info exists acq_mode_cnis]} {
            set acqmode_selist $acq_mode_cnis
            RemoveObsoleteCnisFromList acqmode_selist $acqmode_ailist
         } else {
            set acqmode_selist $acqmode_ailist
         }
         SelBoxCreate .acqmode.lb acqmode_ailist acqmode_selist acqmode_names
      }
   } else {
      foreach widget [info commands .acqmode.lb.*] {
         destroy $widget
      }
      # passive or ui mode -> remove listboxes
      # (the frame is added to trigger the shrink of the window around the remaining widgets)
      frame .acqmode.lb.foo
      pack .acqmode.lb.foo
   }
}

# extract, apply and save the settings
proc QuitAcqModePopup {} {
   global acqmode_ailist acqmode_selist acqmode_names
   global acqmode_sel
   global acq_mode acq_mode_cnis

   if {[string compare -length 6 "cyclic" $acqmode_sel] == 0} {
      if {[llength $acqmode_selist] == 0} {
         tk_messageBox -type ok -default ok -icon error -parent .acqmode -message "You have not selected any providers."
         return
      }
      set acq_mode_cnis $acqmode_selist
   }
   set acq_mode $acqmode_sel

   unset acqmode_ailist acqmode_selist acqmode_sel
   if {[info exists acqmode_names]} {unset acqmode_names}

   C_UpdateAcquisitionMode
   UpdateRcFile

   # close the popup window
   destroy .acqmode
}

## ---------------------------------------------------------------------------
## Create help text header inside text listbox
##
proc PiListBox_PrintHelpHeader {text} {
   global font_bold font_pl4_bold font_pl12_bold

   .all.pi.list.text delete 1.0 end

   if {[string length [info commands .all.pi.list.text.nxtvlogo]] > 0} {
      destroy .all.pi.list.text.nxtvlogo
   }
   button .all.pi.list.text.nxtvlogo -bitmap nxtv_logo -relief flat
   bindtags .all.pi.list.text.nxtvlogo {all .}

   .all.pi.list.text tag configure centerTag -justify center
   .all.pi.list.text tag configure bold24Tag -font $font_pl12_bold -spacing1 15 -spacing3 10
   .all.pi.list.text tag configure bold16Tag -font $font_pl4_bold -spacing3 10
   .all.pi.list.text tag configure bold12Tag -font $font_bold
   .all.pi.list.text tag configure wrapTag   -wrap word
   .all.pi.list.text tag configure redTag    -background #ffff50

   .all.pi.list.text insert end "Nextview EPG\n" bold24Tag
   .all.pi.list.text insert end "An Electronic TV Programme Guide for Your PC\n" bold16Tag
   .all.pi.list.text window create end -window .all.pi.list.text.nxtvlogo
   .all.pi.list.text insert end "\n\nCopyright © 1999, 2000, 2001 by Tom Zörner\n" bold12Tag
   .all.pi.list.text insert end "tomzo@nefkom.net\n\n" bold12Tag
   .all.pi.list.text tag add centerTag 1.0 {end - 1 lines}
   .all.pi.list.text insert end "This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License Version 2 as published by the Free Software Foundation. This program is distributed in the hope that it will be useful, but without any warranty. See the GPL2 for more details.\n\n" wrapTag

   .all.pi.list.text insert end $text {wrapTag redTag}
}

##  --------------------------------------------------------------------------
##  TV card hardware configuration popup (Win-32 only)
##  - Tuner
##  - PLL init 0/1
##  - thread priority normal/high/real-time
##  - card index 0-4
##  - frequency table index (Western Europe/France)
##
set hwcfg_default {0 0 0 0 0 0}
set hwcfg_popup 0

proc PopupHardwareConfig {} {
   global is_unix
   global hwcfg_input_sel
   global hwcfg_tuner_sel hwcfg_tuner_list hwcfg_card_list
   global hwcfg_pll_sel hwcfg_prio_sel hwcfg_cardidx_sel hwcfg_ftable_sel
   global hwcfg_popup hwcfg hwcfg_default

   if {$hwcfg_popup == 0} {
      CreateTransientPopup .hwcfg "TV card input configuration"
      set hwcfg_popup 1

      set hwcfg_tuner_list [C_HwCfgGetTunerList]
      set hwcfg_card_list [C_HwCfgGetTvCardList]
      if {![info exists hwcfg]} {
         set hwcfg $hwcfg_default
      }
      set hwcfg_input_sel [lindex $hwcfg 0]
      set hwcfg_tuner_sel [lindex $hwcfg 1]
      set hwcfg_pll_sel [lindex $hwcfg 2]
      set hwcfg_prio_sel [lindex $hwcfg 3]
      set hwcfg_cardidx_sel [lindex $hwcfg 4]
      set hwcfg_ftable_sel [lindex $hwcfg 5]

      # create menu to select video input
      frame .hwcfg.input
      label .hwcfg.input.curname -text "Video source: "
      menubutton .hwcfg.input.mb -text "Configure" -menu .hwcfg.input.mb.menu -relief raised -borderwidth 1
      menu .hwcfg.input.mb.menu -tearoff 0 -postcommand {PostDynamicMenu .hwcfg.input.mb.menu HardwareCreateInputMenu}
      pack .hwcfg.input.curname -side left -padx 10 -anchor w -expand 1
      pack .hwcfg.input.mb -side left -padx 10 -anchor e
      pack .hwcfg.input -side top -pady 10 -anchor w -fill x

      if {!$is_unix} {
         # create menu for tuner selection
         frame .hwcfg.tuner
         label .hwcfg.tuner.curname -text "Tuner: [lindex $hwcfg_tuner_list $hwcfg_tuner_sel]"
         menubutton .hwcfg.tuner.mb -text "Configure" -menu .hwcfg.tuner.mb.menu -relief raised -borderwidth 1
         menu .hwcfg.tuner.mb.menu -tearoff 0
         set idx 0
         foreach name $hwcfg_tuner_list {
            .hwcfg.tuner.mb.menu add radiobutton -variable hwcfg_tuner_sel -value $idx -label $name \
                                                 -command {.hwcfg.tuner.curname configure -text "Tuner: [lindex $hwcfg_tuner_list $hwcfg_tuner_sel]"}
            incr idx
         }
         pack .hwcfg.tuner.curname -side left -padx 10 -anchor w -expand 1
         pack .hwcfg.tuner.mb -side left -padx 10 -anchor e
         pack .hwcfg.tuner -side top -pady 10 -anchor w -fill x

         # create radiobuttons to choose PLL initialization
         frame .hwcfg.pll
         radiobutton .hwcfg.pll.pll_none -text "No PLL" -variable hwcfg_pll_sel -value 0
         radiobutton .hwcfg.pll.pll_28 -text "PLL 28 MHz" -variable hwcfg_pll_sel -value 1
         radiobutton .hwcfg.pll.pll_35 -text "PLL 35 MHz" -variable hwcfg_pll_sel -value 2
         pack .hwcfg.pll.pll_none .hwcfg.pll.pll_28 .hwcfg.pll.pll_35 -side left
         pack .hwcfg.pll -side top -padx 10 -anchor w

         # create checkbuttons to select acquisition priority
         frame .hwcfg.prio
         label .hwcfg.prio.label -text "Priority:"
         radiobutton .hwcfg.prio.normal -text "normal" -variable hwcfg_prio_sel -value 0
         radiobutton .hwcfg.prio.high   -text "high" -variable hwcfg_prio_sel -value 1
         radiobutton .hwcfg.prio.crit   -text "real-time" -variable hwcfg_prio_sel -value 2
         pack .hwcfg.prio.label -side left -padx 10
         pack .hwcfg.prio.normal .hwcfg.prio.high .hwcfg.prio.crit -side left
         pack .hwcfg.prio -side top -anchor w
      }

      # create checkbuttons to select frequency table
      frame .hwcfg.ftable
      label .hwcfg.ftable.label -text "Frequency table:"
      radiobutton .hwcfg.ftable.tab0 -text "Western Europe" -variable hwcfg_ftable_sel -value 0
      radiobutton .hwcfg.ftable.tab1 -text "France" -variable hwcfg_ftable_sel -value 1
      pack .hwcfg.ftable.label -side left -padx 10
      pack .hwcfg.ftable.tab0 .hwcfg.ftable.tab1 -side left
      pack .hwcfg.ftable -side top -anchor w

      # create menu or checkbuttons to select TV card
      frame .hwcfg.card
      label .hwcfg.card.label -text "TV card: "
      pack .hwcfg.card.label -side left -padx 10
      if {!$is_unix} {
         radiobutton .hwcfg.card.idx0 -text "0" -variable hwcfg_cardidx_sel -value 0 -command HardwareConfigCard
         radiobutton .hwcfg.card.idx1 -text "1" -variable hwcfg_cardidx_sel -value 1 -command HardwareConfigCard
         radiobutton .hwcfg.card.idx2 -text "2" -variable hwcfg_cardidx_sel -value 2 -command HardwareConfigCard
         radiobutton .hwcfg.card.idx3 -text "3" -variable hwcfg_cardidx_sel -value 3 -command HardwareConfigCard
         pack .hwcfg.card.idx0 .hwcfg.card.idx1 .hwcfg.card.idx2 .hwcfg.card.idx3 -side left
      } else {
         menubutton .hwcfg.card.mb -text "Configure" -menu .hwcfg.card.mb.menu -relief raised -borderwidth 1
         menu .hwcfg.card.mb.menu -tearoff 0
         pack .hwcfg.card.mb -side left -padx 10 -anchor e
         set idx 0
         foreach name $hwcfg_card_list {
            .hwcfg.card.mb.menu add radiobutton -variable hwcfg_cardidx_sel -value $idx -label $name -command HardwareConfigCard
            incr idx
         }
      }
      pack .hwcfg.card -side top -anchor w -pady 5

      # display current card name and input source
      HardwareConfigCard

      # create standard command buttons
      frame .hwcfg.cmd
      button .hwcfg.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration) "TV card input"}
      button .hwcfg.cmd.abort -text "Abort" -width 5 -command {destroy .hwcfg}
      button .hwcfg.cmd.ok -text "Ok" -width 5 -command {HardwareConfigQuit}
      pack .hwcfg.cmd.help .hwcfg.cmd.abort .hwcfg.cmd.ok -side left -padx 10
      pack .hwcfg.cmd -side top -pady 10
      bind .hwcfg.cmd <Destroy> {+ set hwcfg_popup 0}
   } else {
      raise .hwcfg
   }
}

# Card index has changed - update video source name
proc HardwareConfigCard {} {
   global is_unix
   global hwcfg_input_sel hwcfg_input_list
   global hwcfg_cardidx_sel hwcfg_card_list

   set hwcfg_input_list [C_HwCfgGetInputList $hwcfg_cardidx_sel]

   if {$is_unix} {
      .hwcfg.card.label configure -text "TV card: [lindex $hwcfg_card_list $hwcfg_cardidx_sel]"
   }

   if {$hwcfg_input_sel >= [lindex $hwcfg_input_list $hwcfg_input_sel]} {
      set hwcfg_input_sel 0
   }

   if {[llength $hwcfg_input_list] > 0} {
      .hwcfg.input.curname configure -text "Video source: [lindex $hwcfg_input_list $hwcfg_input_sel]"
   } else {
      .hwcfg.input.curname configure -text "Video source: #$hwcfg_input_sel (video device busy)"
   }
}

# callback for dynamic input source menu
proc HardwareCreateInputMenu {widget} {
   global hwcfg_input_sel hwcfg_input_list hwcfg_cardidx_sel

   # get list of source names directly from the driver
   set hwcfg_input_list [C_HwCfgGetInputList $hwcfg_cardidx_sel]

   if {[llength $hwcfg_input_list] > 0} {
      set idx 0
      foreach name $hwcfg_input_list {
         .hwcfg.input.mb.menu add radiobutton -variable hwcfg_input_sel -value $idx -label $name \
                                              -command {.hwcfg.input.curname configure -text "Video source: [lindex $hwcfg_input_list $hwcfg_input_sel]"}
         incr idx
      }
   } else {
      .hwcfg.input.mb.menu add command -label "Video device not available:" -state disabled
      .hwcfg.input.mb.menu add command -label "cannot switch video source" -state disabled
   }
}

# Leave popup with OK button
proc HardwareConfigQuit {} {
   global is_unix
   global hwcfg_input_sel hwcfg_tuner_sel
   global hwcfg_pll_sel hwcfg_prio_sel hwcfg_cardidx_sel hwcfg_ftable_sel
   global hwcfg hwcfg_default

   if { !$is_unix && ($hwcfg_input_sel == 0) && ($hwcfg_tuner_sel == 0) } {
      set answer [tk_messageBox -type okcancel -default cancel -icon warning -parent .hwcfg -message "You haven't selected a tuner - acquisition will not be possible!"]
   } else {
      set answer "ok"
   }

   if {[string compare $answer "ok"] == 0} {
      set hwcfg [list $hwcfg_input_sel $hwcfg_tuner_sel $hwcfg_pll_sel $hwcfg_prio_sel $hwcfg_cardidx_sel $hwcfg_ftable_sel]
      UpdateRcFile

      C_UpdateHardwareConfig

      destroy .hwcfg
   }
}

# save a TV card index that was selected via the command line
proc HardwareConfigUpdateCardIdx {cardidx} {
   global hwcfg hwcfg_default

   if {![info exists hwcfg]} {
      set hwcfg $hwcfg_default
   }
   set hwcfg [lreplace $hwcfg 4 4 $cardidx]
   UpdateRcFile
}


## ---------------------------------------------------------------------------
## Time zone configuration popup
##
set tzcfg_default {1 60 0}
set timezone_popup 0

proc PopupTimeZone {} {
   global tzcfg tzcfg_default
   global tz_auto_sel tz_lto_sel tz_daylight_sel
   global timezone_popup

   if {$timezone_popup == 0} {
      CreateTransientPopup .timezone "Configure time zone"
      set timezone_popup 1

      if {![info exists tzcfg]} {
         set tzcfg $tzcfg_default
      }
      set tz_auto_sel [lindex $tzcfg 0]
      set tz_lto_sel [lindex $tzcfg 1]
      set tz_daylight_sel [lindex $tzcfg 2]

      frame .timezone.f1
      message .timezone.msg -aspect 400 -text \
"All times and dates in Nextview are transmitted in UTC \
(Universal Coordinated Time, formerly known as Greenwhich Mean Time) \
and have to be converted to your local time by adding the local time offset. \
If the offset given below is not correct, switch to manual mode and enter \
the correct value."
      pack .timezone.msg -side top -pady 5 -padx 5
      radiobutton .timezone.f1.auto -text "automatic" -variable tz_auto_sel -value 1 -command {ToggleTimeZoneAuto 1}
      radiobutton .timezone.f1.manu -text "manual" -variable tz_auto_sel -value 0 -command {ToggleTimeZoneAuto 0}
      pack .timezone.f1.auto .timezone.f1.manu -side left -padx 10
      pack .timezone.f1 -side top -pady 10

      frame .timezone.f2 -borderwidth 1 -relief raised
      label .timezone.f2.name -text ""
      frame .timezone.f2.ft
      label .timezone.f2.ft.l1 -text "Local time offset: "
      entry .timezone.f2.ft.entry -width 4 -textvariable tz_lto_sel
      bind  .timezone.f2.ft.entry <Enter> {focus %W}
      bind  .timezone.f2.ft.entry <Return> ApplyTimeZone
      label .timezone.f2.ft.l2 -text " minutes"
      pack .timezone.f2.ft.l1 .timezone.f2.ft.entry .timezone.f2.ft.l2 -side left
      checkbutton .timezone.f2.daylight -text "Daylight saving time (+60 minutes)" -variable tz_daylight_sel
      pack .timezone.f2.name .timezone.f2.ft .timezone.f2.daylight -side top -pady 5 -padx 5
      pack .timezone.f2 -side top -pady 10 -fill x

      frame .timezone.cmd
      button .timezone.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration)}
      button .timezone.cmd.abort -text "Abort" -width 5 -command {destroy .timezone}
      button .timezone.cmd.ok -text "Ok" -width 5 -command ApplyTimeZone
      pack .timezone.cmd.help .timezone.cmd.abort .timezone.cmd.ok -side left -padx 10
      bind .timezone.cmd <Destroy> {+ set timezone_popup 0}

      pack .timezone.cmd -side top -pady 10

      ToggleTimeZoneAuto $tz_auto_sel
   } else {
      raise .timezone
   }
}

# called after auto/manual radio was toggled
proc ToggleTimeZoneAuto {newval} {
   global tz_auto_sel tz_lto_sel tz_daylight_sel

   if {$newval} {
      # fetch default values
      set tzarr [C_GetTimeZone]
      set tz_lto_sel [lindex $tzarr 0]
      set tz_daylight_sel [lindex $tzarr 1]
      .timezone.f2.name configure -text "Time zone name: [lindex $tzarr 2]"

      .timezone.f2.ft.entry configure -state disabled
      .timezone.f2.daylight configure -state disabled
      .timezone.f2.ft.l1 configure -state disabled
      .timezone.f2.ft.l2 configure -state disabled
   } else {
      .timezone.f2.ft.entry configure -state normal
      .timezone.f2.daylight configure -state normal
      .timezone.f2.ft.l1 configure -state normal
      .timezone.f2.ft.l2 configure -state normal
   }
}

# called after the OK button was pressed
proc ApplyTimeZone {} {
   global tz_auto_sel tz_lto_sel tz_daylight_sel
   global tzcfg

   if {$tz_auto_sel == 0} {
      # check if the manually configured LTO is a valid integer
      if {[scan $tz_lto_sel "%d" lto] != 1} {
         tk_messageBox -type ok -default ok -icon error -parent .timezone -message "Invalid input format in LTO '$tz_lto_sel'\nMust be positive or negative decimal integer."
         return
      }
      # check if the value is within bounds of +/- 12 hours
      if {($lto < -12*60) || ($lto > 12*60)} {
         tk_messageBox -type ok -default ok -icon error -parent .timezone -message "Invalid LTO: $lto\nThe maximum absolute value is 12 hours."
         return
      }
      # warn if it's not a half-hour value
      # then the user probably has mistakenly entered hours instead of minutes
      if {($lto % 30) != 0} {
         set answer [tk_messageBox -type okcancel -icon warning -parent .timezone -message "Warning: the given LTO value '$lto minutes' is not a complete hour or half hour.\nMaybe you entered an hour value instead of minutes?\nDo you still want to set this LTO?"]
         if {[string compare $answer "ok"] != 0} {
            return
         }
      }
   }

   set tzcfg [list $tz_auto_sel $tz_lto_sel $tz_daylight_sel]

   destroy .timezone
}

##  --------------------------------------------------------------------------
##  Creates the Xawtv popup configuration dialog
##
set xawtvcf_popup 0

# option defaults
set xawtvcf {tunetv 1 follow 1 dopop 1 poptype 0 duration 7}

proc XawtvConfigPopup {} {
   global xawtvcf_popup
   global xawtvcf xawtv_tmpcf

   if {$xawtvcf_popup == 0} {
      CreateTransientPopup .xawtvcf "Xawtv Connection Configuration"
      set xawtvcf_popup 1

      # load configuration into temporary array
      foreach {opt val} $xawtvcf {
         set xawtv_tmpcf($opt) $val
      }

      frame .xawtvcf.all
      label .xawtvcf.all.lab_main -text "En-/Disable Xawtv connection features:"
      pack .xawtvcf.all.lab_main -side top -anchor w -pady 5
      checkbutton .xawtvcf.all.tunetv -text "Tune-TV button" -variable xawtv_tmpcf(tunetv)
      pack .xawtvcf.all.tunetv -side top -anchor w
      checkbutton .xawtvcf.all.follow -text "Cursor follows channel changes" -variable xawtv_tmpcf(follow)
      pack .xawtvcf.all.follow -side top -anchor w
      checkbutton .xawtvcf.all.dopop -text "Display EPG info in xawtv" \
                                     -variable xawtv_tmpcf(dopop) -command XawtvConfigSelected
      pack .xawtvcf.all.dopop -side top -anchor w

      label .xawtvcf.all.lab_poptype -text "How to display EPG info:"
      pack .xawtvcf.all.lab_poptype -side top -anchor w -pady 5
      frame .xawtvcf.all.poptype -borderwidth 2 -relief ridge
      radiobutton .xawtvcf.all.poptype.t0 -text "Separate popup" -variable xawtv_tmpcf(poptype) -value 0 -command XawtvConfigSelected
      radiobutton .xawtvcf.all.poptype.t1 -text "Video overlay (subtitles)" -variable xawtv_tmpcf(poptype) -value 1 -command XawtvConfigSelected
      radiobutton .xawtvcf.all.poptype.t2 -text "Video overlay, 2 lines" -variable xawtv_tmpcf(poptype) -value 2 -command XawtvConfigSelected
      radiobutton .xawtvcf.all.poptype.t3 -text "Xawtv window title" -variable xawtv_tmpcf(poptype) -value 3 -command XawtvConfigSelected
      pack .xawtvcf.all.poptype.t0 .xawtvcf.all.poptype.t1 .xawtvcf.all.poptype.t2 .xawtvcf.all.poptype.t3 -side top -anchor w
      pack .xawtvcf.all.poptype -side top -anchor w -fill x

      frame .xawtvcf.all.duration
      scale .xawtvcf.all.duration.s -from 0 -to 60 -orient horizontal -label "Popup duration \[seconds\]" \
                                -variable xawtv_tmpcf(duration)
      pack .xawtvcf.all.duration.s -side left -fill x -expand 1
      pack .xawtvcf.all.duration -side top -fill x -expand 1 -pady 5
      pack .xawtvcf.all -side top -padx 10 -pady 10

      frame .xawtvcf.cmd
      button .xawtvcf.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration) "Xawtv connection"}
      button .xawtvcf.cmd.abort -text "Abort" -width 5 -command {array unset xawtv_tmpcf; destroy .xawtvcf}
      button .xawtvcf.cmd.save -text "Ok" -width 5 -command {XawtvConfigSave; destroy .xawtvcf}
      pack .xawtvcf.cmd.help .xawtvcf.cmd.abort .xawtvcf.cmd.save -side left -padx 10
      pack .xawtvcf.cmd -side top -pady 5

      # set widget states according to initial option settings
      XawtvConfigSelected

      bind .xawtvcf.cmd <Destroy> {+ set xawtvcf_popup 0}
   } else {
      raise .xawtvcf
   }
}

# callback for changes in popup options: adjust state of depending widgets
proc XawtvConfigSelected {} {
   global xawtv_tmpcf

   if $xawtv_tmpcf(dopop) {
      set state "normal"
   } else {
      set state "disabled"
   }
   .xawtvcf.all.poptype.t0 configure -state $state
   .xawtvcf.all.poptype.t1 configure -state $state
   .xawtvcf.all.poptype.t2 configure -state $state
   .xawtvcf.all.poptype.t3 configure -state $state

   if {$xawtv_tmpcf(dopop) && ($xawtv_tmpcf(poptype) != 2)} {
      set state "normal"
      set fg black
   } else {
      set state "disabled"
      set fg [.xawtvcf.all.lab_main cget -disabledforeground]
   }
   .xawtvcf.all.duration.s configure -state $state -foreground $fg
}

# callback for OK button: save config into variables
proc XawtvConfigSave {} {
   global xawtvcf xawtv_tmpcf

   set xawtvcf [list "tunetv" $xawtv_tmpcf(tunetv) \
                     "follow" $xawtv_tmpcf(follow) \
                     "dopop" $xawtv_tmpcf(dopop) \
                     "poptype" $xawtv_tmpcf(poptype) \
                     "duration" $xawtv_tmpcf(duration)]
   array unset xawtv_tmpcf

   # save options
   UpdateRcFile
   # load config vars into the C module
   C_Xawtv_InitConfig
}


##  --------------------------------------------------------------------------
##  Creates the Context menu popup configuration dialog
##
set ctxmencf_popup 0
set ctxmencf {}

proc ContextMenuConfigPopup {} {
   global ctxmencf_popup
   global default_bg font_fixed
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
      pack .ctxmencf.all.lbox.cmdlist -side left -expand 1 -fill x
      pack .ctxmencf.all.lbox  -fill x -side top

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
         set ctxmencf_cmd   {xalarm -time ${start} -date ${start:%b %d %Y} ${title}\ \(${network}\)}
      }
      .ctxmencf.all.cmd.examples.men add command -label "All variables" -command {
         set ctxmencf_title "Demo: echo all variables";
         set ctxmencf_cmd   {echo title=${title} network=${network} start=${start} stop=${stop} VPS/PDC=${VPS} duration=${duration} relstart=${relstart} CNI=${CNI} description=${description} themes=${themes} e_rating=${e_rating} p_rating=${p_rating} sound=${sound} format=${format} digital=${digital} encrypted=${encrypted} live=${live} repeat=${repeat} subtitle=${subtitle}}
      }
      pack .ctxmencf.all.cmd -side top -pady 10

      # section #3: entry field to modify or create commands
      frame .ctxmencf.all.edit -borderwidth 2 -relief ridge
      frame .ctxmencf.all.edit.fr_title
      entry .ctxmencf.all.edit.fr_title.ent -textvariable ctxmencf_title -width 55 -font $font_fixed
      pack  .ctxmencf.all.edit.fr_title.ent -side right
      label .ctxmencf.all.edit.fr_title.lab -text "Title:"
      pack  .ctxmencf.all.edit.fr_title.lab -side right
      pack  .ctxmencf.all.edit.fr_title -side top -fill x
      frame .ctxmencf.all.edit.fr_cmd
      entry .ctxmencf.all.edit.fr_cmd.ent -textvariable ctxmencf_cmd -width 55 -font $font_fixed
      pack  .ctxmencf.all.edit.fr_cmd.ent -side right
      label .ctxmencf.all.edit.fr_cmd.lab -text "Command:"
      pack  .ctxmencf.all.edit.fr_cmd.lab -side right
      pack  .ctxmencf.all.edit.fr_cmd -side top -fill x
      pack  .ctxmencf.all.edit -side top -pady 5
      pack  .ctxmencf.all -side top -pady 5 -padx 5

      # section #4: standard dialog buttons: Ok, Abort, Help
      frame .ctxmencf.cmd
      button .ctxmencf.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration) "Context menu"}
      button .ctxmencf.cmd.abort -text "Abort" -width 5 -command {ContextMenuConfigQuit 1}
      button .ctxmencf.cmd.save -text "Ok" -width 5 -command {ContextMenuConfigQuit 0}
      pack .ctxmencf.cmd.help .ctxmencf.cmd.abort .ctxmencf.cmd.save -side left -padx 10
      pack .ctxmencf.cmd -side top -pady 5

      bind .ctxmencf.cmd <Destroy> {+ set ctxmencf_popup 0}

      # display the entries in the listbox
      foreach idx $ctxmencf_ord {
        .ctxmencf.all.lbox.cmdlist insert end [lindex $ctxmencf_arr($idx) 0]
      }
      set ctxmencf_title {}
      set ctxmencf_cmd {}
      set ctxmencf_selidx -1
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


## ---------------------------------------------------------------------------
##                 S T A T I S T I C S   W I N D O W S
## ---------------------------------------------------------------------------

## ---------------------------------------------------------------------------
## Open or update a timescale popup window
##
proc TimeScale_Open {w cni key isMerged} {

   # fetch network list from AI block in database
   set netsel_ailist [C_GetAiNetwopList $cni netnames]

   if {[string length [info commands $w]] == 0} {
      if {[string compare $key ui] == 0} {
         set wtitle "Nextview browser database time scales"
      } else {
         set wtitle "Nextview acquisition database time scales"
      }

      toplevel $w
      wm title $w $wtitle
      wm resizable $w 0 0
      wm group $w .

      # create a time scale for each network;  note: use numerical indices
      # instead of CNIs to be able to reuse the scales for another provider
      frame $w.top
      set idx 0
      foreach cni $netsel_ailist {
         TimeScale_Create $w $idx $netnames($cni) $isMerged
         incr idx
      }
      pack $w.top -padx 5 -pady 5 -side top -fill x

      # create frame with label for status line
      frame $w.bottom -borderwidth 2 -relief sunken
      label $w.bottom.l -text {}
      pack $w.bottom.l -side left -anchor w
      pack $w.bottom -side top -fill x

      # create notification callback in case the window is closed
      bind $w.bottom <Destroy> [list + C_StatsWin_ToggleTimescale $key 0]

   } else {

      # popup already open -> just update the scales

      set idx 0
      foreach cni $netsel_ailist {
         TimeScale_Create $w $idx $netnames($cni) $isMerged
         incr idx
      }

      # remove obsolete timescales at the bottom
      # e.g. after an AI update in case the new AI has less networks
      while {[string length [info commands $w.top.n$idx]] != 0} {
         pack forget $w.top.n$idx
         destroy $w.top.n$idx
         incr idx
      }
   }
}

## ---------------------------------------------------------------------------
## Create or update the n'th timescale
##
proc TimeScale_Create {w netwop netwopName isMerged} {
   global tscnowid default_bg

   set w $w.top.n$netwop

   # for merged databases make Now & Next boxes invisible
   if $isMerged {
      set now_bg $default_bg
   } else {
      set now_bg black
   }

   if {[string length [info commands $w]] == 0} {
      frame $w

      label $w.name -text $netwopName -anchor w
      pack $w.name -side left -fill x -expand true

      canvas $w.stream -bg $default_bg -height 12 -width 298
      pack $w.stream -side left

      set tscnowid(0) [$w.stream create rect  1 4  5 12 -outline "" -fill $now_bg]
      set tscnowid(1) [$w.stream create rect  8 4 12 12 -outline "" -fill $now_bg]
      set tscnowid(2) [$w.stream create rect 15 4 19 12 -outline "" -fill $now_bg]
      set tscnowid(3) [$w.stream create rect 22 4 26 12 -outline "" -fill $now_bg]
      set tscnowid(4) [$w.stream create rect 29 4 33 12 -outline "" -fill $now_bg]

      set tscnowid(bg) [$w.stream create rect 40 4 298 12 -outline "" -fill black]

      pack $w -fill x -expand true

   } else {
      # timescale already exists -> just update it

      # update the network name
      $w.name configure -text $netwopName

      # reset the "Now" buttons
      for {set idx 0} {$idx < 5} {incr idx} {
         $w.stream itemconfigure $tscnowid($idx) -fill $now_bg
      }

      # clear the scale
      set id_bg $tscnowid(bg)
      foreach id [$w.stream find overlapping 39 0 299 12] {
         if {$id != $id_bg} {
            $w.stream delete $id
         }
      }
   }
}

## ---------------------------------------------------------------------------
## Display the timespan covered by a PI in its network timescale
##
proc TimeScale_AddPi {w pos1 pos2 color hasShort hasLong isLast} {
   global tscnowid

   set y0 4
   set y1 12
   if {$hasShort} {set y0 [expr $y0 - 2]}
   if {$hasLong}  {set y1 [expr $y1 + 2]}
   $w.stream create rect [expr 40 + $pos1] $y0 [expr 40 + $pos2] $y1 -fill $color -outline ""

   if $isLast {
      # this was the last block as defined in the AI block
      # -> remove any remaining blocks to the right, esp. the "PI missing" range
      set id_bg $tscnowid(bg)
      foreach id [$w.stream find overlapping [expr 41 + $pos2] 0 299 12] {
         if {$id != $id_bg} {
            $w.stream delete $id
         }
      }
   }
}

## ---------------------------------------------------------------------------
## Mark a network label for which a PI was received
##
proc TimeScale_MarkNow {frame num color} {
   global tscnowid
   $frame.stream itemconfigure $tscnowid($num) -fill $color
}

## ---------------------------------------------------------------------------
## Create the acq stats window with histogram and summary text
##
proc DbStatsWin_Create {wname} {
   global font_fixed

   toplevel $wname
   wm title $wname {Nextview database statistics}
   wm resizable $wname 0 0
   wm group $wname .

   frame $wname.browser -relief sunken -borderwidth 2
   canvas $wname.browser.pie -height 128 -width 128
   pack $wname.browser.pie -side left -anchor w

   message $wname.browser.stat -font $font_fixed -aspect 2000 -justify left -anchor nw
   pack $wname.browser.stat -expand 1 -fill both -side left
   pack $wname.browser -side top -anchor nw -fill both

   frame $wname.acq -relief sunken -borderwidth 2
   canvas $wname.acq.hist -bg white -height 128 -width 128
   pack $wname.acq.hist -side left -anchor s -anchor w

   message $wname.acq.stat -font $font_fixed -aspect 2000 -justify left -anchor nw
   pack $wname.acq.stat -expand 1 -fill both -side left

   # this frame is intentionally not packed
   #pack $wname.acq -side top -anchor nw -fill both

   button $wname.browser.qmark -bitmap bitmap_qmark -cursor top_left_arrow -takefocus 0 -relief ridge
   bind   $wname.browser.qmark <ButtonRelease-1> {PopupHelp $helpIndex(Statistics) "Database statistics popup windows"}
   pack   $wname.browser.qmark -side top

   # inform the control code when the window is destroyed
   bind $wname.browser <Destroy> [list + C_StatsWin_ToggleDbStats $wname 0]
}

## ---------------------------------------------------------------------------
## Paint the pie which reflects the current database percentages
##
proc DbStatsWin_PaintPie {wname val1exp val1cur val1all val1total val2exp val2cur val2all} {

   catch [ $wname.browser.pie delete all ]

   if {$val1total > 0} {
      # paint the slice of stream 1 in red
      $wname.browser.pie create arc 1 1 127 127 -start 0 -extent $val1total -fill red

      if {$val1exp > 0} {
         # mark expired and defective percentage in yellow
         $wname.browser.pie create arc 1 1 127 127 -start 0 -extent $val1exp -fill yellow -outline {} -stipple gray75
      }
      if {$val1cur < $val1all} {
         # mark percentage of old version in shaded red
         $wname.browser.pie create arc 1 1 127 127 -start $val1cur -extent [expr $val1all - $val1cur] -fill #A52A2A -outline {} -stipple gray50
      }
      if {$val1all < $val1total} {
         # mark percentage of missing stream 1 data in slight red
         $wname.browser.pie create arc 1 1 127 127 -start $val1all -extent [expr $val1total - $val1all] -fill #FFDDDD -outline {} -stipple gray75
      }
   }

   if {$val1total < 359.9} {
      # paint the slice of stream 2 in blue
      $wname.browser.pie create arc 1 1 127 127 -start $val1total -extent [expr 359.999 - $val1total] -fill blue

      if {$val2exp > 0} {
         # mark expired and defective percentage in yellow
         $wname.browser.pie create arc 1 1 127 127 -start $val1total -extent [expr $val2exp - $val1total] -fill yellow -outline {} -stipple gray75
      }
      if {$val2cur < $val2all} {
         # mark percentage of old version in shaded blue
         $wname.browser.pie create arc 1 1 127 127 -start $val2cur -extent [expr $val2all - $val2cur] -fill #483D8B -outline {} -stipple gray50
      }
      if {$val2all < 359.9} {
         # mark percentage of missing stream 1 data in slight blue
         $wname.browser.pie create arc 1 1 127 127 -start $val2all -extent [expr 359.9 - $val2all] -fill #DDDDFF -outline {} -stipple gray75
      }
   }
}

## ---------------------------------------------------------------------------
## Clear the pie chart for an empty database
##
proc DbStatsWin_ClearPie {wname} {
   catch [ $wname.browser.pie delete all ]
   $wname.browser.pie create oval 1 1 127 127 -outline black -fill white
}

## ---------------------------------------------------------------------------
## Add a line to the history graph
##
proc DbStatsWin_AddHistory {wname pos valEx val1c val1o val2c val2o} {

   $wname.acq.hist addtag "TBDEL" enclosed $pos 0 [expr $pos + 2] 128
   catch [ $wname.acq.hist delete "TBDEL" ]

   if {$valEx > 0} {
      $wname.acq.hist create line $pos 128 $pos [expr 128 - $valEx] -fill yellow
   }
   if {$val1c > $valEx} {
      $wname.acq.hist create line $pos [expr 128 - $valEx] $pos [expr 128 - $val1c] -fill red
   }
   if {$val1o > $val1c} {
      $wname.acq.hist create line $pos [expr 128 - $val1c] $pos [expr 128 - $val1o] -fill #A52A2A
   }
   if {$val2c > $val1o} {
      $wname.acq.hist create line $pos [expr 128 - $val1o] $pos [expr 128 - $val2c] -fill blue
   }
   if {$val2o > $val2c} {
      $wname.acq.hist create line $pos [expr 128 - $val2c] $pos [expr 128 - $val2o] -fill #483D8B
   }
}

## ---------------------------------------------------------------------------
## Clear the histogram (e.g. after provider change)
##
proc DbStatsWin_ClearHistory {wname} {
   # the tag "all" is automatically assigned to every item in the canvas
   catch [ $wname.acq.hist delete all ]
}


## ---------------------------------------------------------------------------
##                    F I L T E R   S H O R T C U T S
## ---------------------------------------------------------------------------

set fsc_name_idx 0
set fsc_mask_idx 1
set fsc_filt_idx 2
set fsc_logi_idx 3
set fsc_tag_idx  4

##
##  Predefined filter shortcuts
##
proc PreloadShortcuts {} {
   global shortcuts shortcut_count

   set shortcuts(0)  {movies themes {theme_class1 16} merge 0}
   set shortcuts(1)  {sports themes {theme_class1 64} merge 1}
   set shortcuts(2)  {series themes {theme_class1 128} merge 2}
   set shortcuts(3)  {kids themes {theme_class1 80} merge 3}
   set shortcuts(4)  {shows themes {theme_class1 48} merge 4}
   set shortcuts(5)  {news themes {theme_class1 32} merge 5}
   set shortcuts(6)  {social themes {theme_class1 37} merge 6}
   set shortcuts(7)  {science themes {theme_class1 86} merge 7}
   set shortcuts(8)  {hobbies themes {theme_class1 52} merge 8}
   set shortcuts(9)  {music themes {theme_class1 96} merge 9}
   set shortcuts(10) {culture themes {theme_class1 112} merge 10}
   set shortcuts(11) {adult themes {theme_class1 24} merge 11}
   set shortcut_count 12
}

##
##  Generate a new, unique tag
##
proc GenerateShortcutTag {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global shortcuts shortcut_count

   set tag [clock seconds]
   foreach idx [array names shortcuts] {
      set stag [lindex $shortcuts($idx) $fsc_tag_idx]
      if {([string length $tag] > 0) && ($tag <= $stag)} {
         set tag [expr $stag + 1]
      }
   }
   return $tag
}

##  --------------------------------------------------------------------------
##  Generate a text that describes a given filter setting
##
proc ShortcutPrettyPrint {filter} {

   # fetch CNI list from AI block in database
   set netsel_ailist [C_GetAiNetwopList 0 netnames]
   ApplyUserNetnameCfg netnames

   set out {}
   foreach {ident valist} $filter {
      switch -glob $ident {
         theme_class1 {
            foreach theme $valist {
               append out "Theme: [C_GetPdcString $theme]\n"
            }
         }
         theme_class* {
            scan $ident "theme_class%d" class
            foreach theme $valist {
               append out "Theme, class $class: [C_GetPdcString $theme]\n"
            }
         }
         sortcrit_class* {
            scan $ident "sortcrit_class%d" class
            foreach sortcrit $valist {
               append out "Sort.crit., class $class: [format 0x%02X $sortcrit]\n"
            }
         }
         series {
            set titnet_list [C_GetSeriesTitles $valist]
            set title_list {}
            foreach {title netwop} $titnet_list {
               lappend title_list [list $title $netwop]
            }
            foreach title [lsort -command CompareSeriesMenuEntries $title_list] {
               append out "Series: '[lindex $title 0]' on [lindex $title 1]\n"
            }
         }
         netwops {
            foreach cni $valist {
               if [info exists netnames($cni)] {
                  append out "Network: $netnames($cni)\n"
               } else {
                  append out "Network: CNI $cni\n"
               }
            }
         }
         features {
            foreach {mask value} $valist {
               if {($mask & 0x03) == 0x03} {
                  append out "Feature: " [switch -exact [expr $value & 0x03] {
                     0 {format "mono"}
                     1 {format "2-channel"}
                     2 {format "stereo"}
                     3 {format "surround"}
                  }] " sound\n"
               }
               if {$mask & 0x04} {
                  append out "Feature: widescreen picture\n"
               }
               if {$mask & 0x08} {
                  append out "Feature: PAL+ picture\n"
               }
               if {$mask & 0x10} {
                  append out "Feature: digital\n"
               }
               if {$mask & 0x20} {
                  append out "Feature: encrypted\n"
               }
               if {$mask & 0x40} {
                  append out "Feature: live transmission\n"
               }
               if {$mask & 0x80} {
                  append out "Feature: repeat\n"
               }
               if {$mask & 0x100} {
                  append out "Feature: subtitled\n"
               }
            }
         }
         parental {
            if {$valist == 1} {
               append out "Parental rating: general (all ages)\n"
            } elseif {$valist > 0} {
               append out "Parental rating: age [expr $valist * 2] and up\n"
            }
         }
         editorial {
            append out "Editorial rating: $valist of 1..7\n"
         }
         progidx {
            set start [lindex $valist 0]
            set stop  [lindex $valist 1]
            if {($start == 0) && ($stop == 0)} {
               append out "Program running NOW\n"
            } elseif {($start == 0) && ($stop == 1)} {
               append out "Program running NOW or NEXT\n"
            } elseif {($start == 1) && ($stop == 1)} {
               append out "Program running NEXT\n"
            } else {
               append out "Program indices #$start..#$stop\n"
            }
         }
         timsel {
            if {[lindex $valist 4] == 0} {
               set date "today"
            } elseif {[lindex $valist 4] == 1} {
               set date "tomorrow"
            } else {
               set date "in [lindex $valist 4] days"
            }
            if {[lindex $valist 0] == 0} {
               if {[lindex $valist 1] == 0} {
                  append out "Time span: $date from [Motd2HHMM [lindex $valist 2]] til [Motd2HHMM [lindex $valist 3]]\n"
               } else {
                  append out "Time span: $date from [Motd2HHMM [lindex $valist 2]] til midnight\n"
               }
            } else {
               if {[lindex $valist 1] == 0} {
                  append out "Time span: $date from NOW til [Motd2HHMM [lindex $valist 3]]\n"
               } else {
                  append out "Time span: $date from NOW til midnight\n"
               }
            }
         }
         substr {
            set grep_title [lindex $valist 0]
            set grep_descr [lindex $valist 1]
            if {$grep_title && !$grep_descr} {
               append out "Title"
            } elseif {!$grep_title && $grep_descr} {
               append out "Description"
            } else {
               append out "Title or Description"
            }
            append out " containing '[lindex $valist 4]'"
            if {[lindex $valist 2] && [lindex $valist 3]} {
               append out " (match case & complete)"
            } elseif [lindex $valist 2] {
               append out " (match case)"
            } elseif [lindex $valist 3] {
               append out " (match complete)"
            }
            append out "\n"
         }
      }
   }
   return $out
}

##  --------------------------------------------------------------------------
##  Check if shortcut should be deselected after manual filter modification
##
proc CheckShortcutDeselection {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global shortcuts shortcut_count
   global parental_rating editorial_rating
   global theme_sel theme_class_sel current_theme_class theme_class_count
   global sortcrit_class sortcrit_class_sel
   global series_sel
   global feature_class_count feature_class_mask feature_class_value
   global progidx_first progidx_last filter_progidx
   global substr_pattern substr_grep_title substr_grep_descr substr_match_case substr_match_full
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
                              ($substr_match_case != [lindex $valist 2]) || \
                              ($substr_match_full != [lindex $valist 3]) || \
                              ([string compare $substr_pattern [lindex $valist 4]] != 0) ]
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
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global shortcuts shortcut_count
   global parental_rating editorial_rating
   global theme_sel theme_class_sel current_theme_class theme_class_count
   global sortcrit_class sortcrit_class_sel
   global series_sel
   global feature_class_count feature_class_mask feature_class_value
   global progidx_first progidx_last filter_progidx
   global substr_pattern substr_grep_title substr_grep_descr substr_match_case substr_match_full
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
                  C_SelectSubStr 0 0 0 0 {}
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
               set substr_match_case  [lindex $valist 2]
               set substr_match_full  [lindex $valist 3]
               set substr_pattern     [lindex $valist 4]
               C_SelectSubStr $substr_grep_title $substr_grep_descr $substr_match_case $substr_match_full $substr_pattern
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
      foreach cni [C_GetAiNetwopList 0 {}] {
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
         C_ResetFilter series
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
   global substr_grep_title substr_grep_descr substr_match_case substr_match_full substr_pattern
   global filter_progidx progidx_first progidx_last
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
   set mask {}

   # dump all theme classes
   for {set class 1} {$class <= $theme_class_count} {incr class} {
      if {[string length $theme_class_sel($class)] > 0} {
         lappend all "theme_class$class" $theme_class_sel($class)
         lappend mask themes
      }
   }

   # dump all sortcrit classes
   for {set class 1} {$class <= $theme_class_count} {incr class} {
      if {[string length $sortcrit_class_sel($class)] > 0} {
         lappend all "sortcrit_class$class" $sortcrit_class_sel($class)
         lappend mask sortcrits
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
      lappend mask features
   }

   if {$parental_rating > 0} {
      lappend all "parental" $parental_rating
      lappend mask parental
   }
   if {$editorial_rating > 0} {
      lappend all "editorial" $editorial_rating
      lappend mask editorial
   }

   # dump text substring filter state
   if {[string length $substr_pattern] > 0} {
      lappend all "substr" [list $substr_grep_title $substr_grep_descr $substr_match_case $substr_match_full $substr_pattern]
      lappend mask substr
   }

   # dump program index filter state
   if {$filter_progidx > 0} {
      lappend all "progidx" [list $progidx_first $progidx_last]
      lappend mask progidx
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
      lappend mask series
   }

   # dump start time filter
   if {$timsel_enabled} {
      lappend all "timsel" [list $timsel_relative $timsel_absstop $timsel_start $timsel_stop $timsel_date]
      lappend mask timsel
   }

   # dump CNIs of selected netwops
   # - Upload filters from filter context, so that netwops that are not in the current
   #   netwop bar can be saved too (might have been set through the Navigate menu).
   set temp [C_GetNetwopFilterList]
   if {[llength $temp] > 0} {
      lappend all "netwops" $temp
      lappend mask netwops
   }

   return [list $mask $all]
}

##  --------------------------------------------------------------------------
##  Compare settings of two shortcuts: return TRUE if identical
##  - NOTE: tags are not compared because they always differ between shortcuts
##  - NOTE: filter setting is not compared properly (see below)
##
proc CompareShortcuts {sc_a sc_b} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx

   set result 0
   # compare label
   if {[string compare [lindex $sc_a $fsc_name_idx] [lindex $sc_b $fsc_name_idx]] == 0} {
      # compare combination logic mode
      if {[string compare [lindex $sc_a $fsc_logi_idx] [lindex $sc_b $fsc_logi_idx]] == 0} {
         # compare combination logic mode
         if {[string compare [lsort [lindex $sc_a $fsc_mask_idx]] [lsort [lindex $sc_b $fsc_mask_idx]]] == 0} {
            # compare filter settings
            # NOTE: would need to be sorted for exact comparison!
            #       currently not required, hence not implemented
            if {[string compare [lindex $sc_a $fsc_filt_idx] [lindex $sc_b $fsc_filt_idx]] == 0} {
               set result 1
            }
         }
      }
   }
   return $result
}

##  --------------------------------------------------------------------------
##  Update filter shortcut pop-up window
##
set fscupd_popup 0

proc UpdateFilterShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global shortcuts shortcut_count
   global fscupd_popup fscedit_popup
   global fsc_prevselection


   if {$fscupd_popup == 0} {
      CreateTransientPopup .fscupd "Update shortcut"
      set fscupd_popup 1

      message .fscupd.msg -aspect 400 -text "Please select which shortcut shall be updated with the current filter setting:"
      pack .fscupd.msg -side top -padx 10

      ## first column: listbox with all shortcut labels
      listbox .fscupd.list -exportselection false -setgrid true -height 12 -width 0 -selectmode single -relief ridge
      #bind .fscupd.list <Double-Button-1> {+ SelectEditedShortcut}
      pack .fscupd.list -fill x -anchor nw -side left -pady 10 -padx 10 -fill y -expand 1

      ## second column: command buttons and entry widget to rename shortcuts
      frame .fscupd.cmd
      button .fscupd.cmd.update -text "Update" -command {SaveUpdatedFilterShortcut no} -width 12
      button .fscupd.cmd.edit -text "Update & Edit" -command {SaveUpdatedFilterShortcut yes} -width 12
      button .fscupd.cmd.abort -text "Abort" -command {destroy .fscupd} -width 12
      button .fscupd.cmd.help -text "Help" -command {PopupHelp $helpIndex(Filter shortcuts)} -width 12
      pack .fscupd.cmd.help .fscupd.cmd.abort .fscupd.cmd.edit .fscupd.cmd.update -side bottom -anchor sw
      pack .fscupd.cmd -side left -anchor nw -pady 10 -padx 5 -fill y -expand 1
      bind .fscupd.cmd <Destroy> {+ set fscupd_popup 0}

      # fill the listbox with all shortcut labels
      for {set index 0} {$index < $shortcut_count} {incr index} {
         .fscupd.list insert end [lindex $shortcuts($index) $fsc_name_idx]
      }
      .fscupd.list configure -height $shortcut_count

      # preselect the currently selected shortcut
      if {[info exists fsc_prevselection] && ([llength $fsc_prevselection] == 1)} {
         .fscupd.list selection set $fsc_prevselection
      }
   } else {
      raise .fscupd
   }
}

proc GetShortcutIdentList {shortcut} {
   set idl {}
   foreach {ident valist} $shortcut {
      switch -glob $ident {
         theme_class*   {
            lappend idl "themes"
         }
         sortcrit_class*   {
            lappend idl "sortcrits"
         }
         default {
            lappend idl $ident
         }
      }
   }
   return $idl
}

# "Update" and "Update & Edit" command buttons
proc SaveUpdatedFilterShortcut {call_edit} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global shortcuts shortcut_count
   global fscedit_sclist fscedit_count
   global fscedit_popup

   # determine current filter settings (discard mask)
   set filt [lindex [DescribeCurrentFilter] 1]

   set sel [.fscupd.list curselection]
   if {([llength $sel] == 1) && ($sel < $shortcut_count)} {
      if {[string compare $call_edit yes] != 0} {
         # compare filter categories before and after
         set old_ids [GetShortcutIdentList [lindex $shortcuts($sel) $fsc_filt_idx]]
         set new_ids [GetShortcutIdentList $filt]
         if {$old_ids != $new_ids} {
            # different categories -> ask user if he's sure he doesn't want to edit the mask
            set call_edit [tk_messageBox -icon warning -type yesnocancel -default yes -parent .fscupd \
                               -message "The current settings include different filter categories than the selected shortcut. Do you want to adapt the filter mask?"]
            if {[string compare $call_edit cancel] == 0} {
               return
            }
         }
      }

      if {([string compare $call_edit yes] == 0) || $fscedit_popup} {
         # close the popup window
         # (note: need to close before edit window is opened or tvtwm will crash)
         destroy .fscupd

         if {$fscedit_popup == 0} {
            # copy the shortcuts into a temporary array
            if {[info exists fscedit_sclist]} {unset fscedit_sclist}
            array set fscedit_sclist [array get shortcuts]
            set fscedit_count $shortcut_count
            set fscedit_sclist($sel) [lreplace $fscedit_sclist($sel) $fsc_filt_idx $fsc_filt_idx $filt]
         } else {
            # search for the shortcut in the edited list
            set tag [lindex $shortcuts($sel) $fsc_tag_idx]
            for {set sel_tmp 0} {$sel_tmp < $fscedit_count} {incr sel_tmp} {
               if {[lindex $shortcuts($sel_tmp) $fsc_tag_idx] == $tag} {
                  set fscedit_sclist($sel_tmp) [lreplace $fscedit_sclist($sel_tmp) $fsc_filt_idx $fsc_filt_idx $filt]
                  break
               }
            }
            if {$sel_tmp >= $fscedit_count} {
               # not found in list (deleted by user in temporary list) -> append
               set fscedit_sclist($fscedit_count) [lreplace $shortcuts($sel) $fsc_filt_idx $fsc_filt_idx $filt]
               incr fscedit_count
            }
            set sel $sel_tmp
            raise .fscedit
         }

         # create the popup (if neccessary) and fill the listbox with all shortcuts
         PopupFilterShortcuts
         # select the updated shortcut in the listbox
         .fscedit.list selection set $sel
         SelectEditedShortcut

      } else {
         # update the selected shortcut
         set shortcuts($sel) [lreplace $shortcuts($sel) $fsc_filt_idx $fsc_filt_idx $filt]
         UpdateRcFile
         # close the popup window
         destroy .fscupd
      }
   }
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
   } else {
      # popup already open -> keep the existing temporary array
      raise .fscedit
   }

   # determine current filter settings and a default mask
   set temp [DescribeCurrentFilter]
   set mask [lindex $temp 0]
   set filt [lindex $temp 1]
   set name "shortcut #$fscedit_count"
   set tag  [GenerateShortcutTag]

   # check if any filters are set at all
   if {[string length $mask] == 0} {
      set answer [tk_messageBox -type okcancel -icon warning -message "Currently no filters selected. Do you still want to continue?"]
      if {[string compare $answer "cancel"] == 0} {
         return
      }
   }

   # append the new shortcut to the temporary array
   set fscedit_sclist($fscedit_count) [list $name $mask $filt merge $tag]
   incr fscedit_count

   # create the popup and fill the shortcut listbox
   PopupFilterShortcuts
   # select the new entry in the listbox
   .fscedit.list selection set [expr $fscedit_count - 1]
   SelectEditedShortcut
}

##  --------------------------------------------------------------------------
##  Popup window to edit the shortcut list
##  - Invoked from the configuration popup
##
proc EditFilterShortcuts {} {
   global shortcuts shortcut_count
   global fscedit_sclist fscedit_count
   global fscedit_idx
   global fscedit_popup

   if {$fscedit_popup == 0} {

      # copy the shortcuts into a temporary array
      if {[info exists fscedit_sclist]} {unset fscedit_sclist}
      array set fscedit_sclist [array get shortcuts]
      set fscedit_count $shortcut_count

      # create the popup and fill the shortcut listbox
      PopupFilterShortcuts
      # select the first listbox entry
      .fscedit.list selection set 0
      SelectEditedShortcut

   } else {
      raise .fscedit
   }
}

##  --------------------------------------------------------------------------
##  Filter shortcut configuration pop-up window
##
set fscedit_label ""
set fscedit_popup 0

proc PopupFilterShortcuts {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global fscedit_sclist fscedit_count
   global fscedit_desc fscedit_mask fscedit_label fscedit_logic fscedit_idx
   global textfont default_bg
   global fscedit_popup

   # initialize all state variables
   if {[info exists fscedit_desc]} {unset fscedit_desc}
   if {[info exists fscedit_mask]} {unset fscedit_mask}
   if {[info exists fscedit_label]} {unset fscedit_label}
   if {[info exists fscedit_logic]} {set fscedit_logic 0}

   if {$fscedit_popup == 0} {
      CreateTransientPopup .fscedit "Edit shortcuts"
      set fscedit_popup 1

      ## first column: listbox with all shortcut labels
      listbox .fscedit.list -exportselection false -setgrid true -height 12 -width 0 -selectmode single -relief ridge
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
      pack .fscedit.cmd.rename -side top -anchor nw
      frame .fscedit.cmd.updown
      button .fscedit.cmd.updown.up -bitmap "bitmap_ptr_up" -command ShiftUpEditedShortcut -width 30
      pack .fscedit.cmd.updown.up -side left
      button .fscedit.cmd.updown.down -bitmap "bitmap_ptr_down" -command ShiftDownEditedShortcut -width 30
      pack .fscedit.cmd.updown.down -side left
      pack .fscedit.cmd.updown -side top -anchor nw
      button .fscedit.cmd.delete -text "delete" -command {DeleteEditedShortcut} -width 7
      pack .fscedit.cmd.delete -side top -anchor nw

      button .fscedit.cmd.save -text "Save" -command {SaveEditedShortcuts} -width 7
      button .fscedit.cmd.help -text "Help" -command {PopupHelp $helpIndex(Filter shortcuts)} -width 7
      button .fscedit.cmd.abort -text "Abort" -command {AbortEditedShortcuts} -width 7
      pack .fscedit.cmd.abort .fscedit.cmd.help .fscedit.cmd.save -side bottom -anchor sw

      pack .fscedit.cmd -side left -anchor nw -pady 10 -padx 5 -fill y -expand 1
      bind .fscedit.cmd <Destroy> {+ set fscedit_popup 0}

      ## third column: shortcut flags
      frame .fscedit.flags
      label .fscedit.flags.ld -text "Filter setting:"
      pack .fscedit.flags.ld -side top -anchor w
      frame .fscedit.flags.desc
      text .fscedit.flags.desc.tx -width 1 -height 6 -wrap none -yscrollcommand {.fscedit.flags.desc.sc set} \
                                  -font $textfont -background $default_bg \
                                  -insertofftime 0 -state disabled -exportselection 1
      pack .fscedit.flags.desc.tx -side left -fill x -expand 1
      scrollbar .fscedit.flags.desc.sc -orient vertical -command {.fscedit.flags.desc.tx  yview}
      pack .fscedit.flags.desc.sc -side left -fill y
      pack .fscedit.flags.desc -side top -anchor w -fill x

      label .fscedit.flags.lm -text "Filter mask:"
      pack .fscedit.flags.lm -side top -anchor w -pady 5
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
      pack .fscedit.flags.mask -side top -pady 5 -fill x

      #label .fscedit.flags.ll -text "Combination with other shortcuts:"
      #pack .fscedit.flags.ll -side top -anchor w
      #frame .fscedit.flags.logic -relief ridge -bd 1
      #radiobutton .fscedit.flags.logic.merge -text "merge" -variable fscedit_logic -value "merge"
      #radiobutton .fscedit.flags.logic.or -text "logical OR" -variable fscedit_logic -value "or" -state disabled
      #radiobutton .fscedit.flags.logic.and -text "logical AND" -variable fscedit_logic -value "and" -state disabled
      #pack .fscedit.flags.logic.merge .fscedit.flags.logic.or .fscedit.flags.logic.and -side top -anchor w -padx 5
      #pack .fscedit.flags.logic -side top -pady 10 -fill x
      pack .fscedit.flags -side left -anchor nw -pady 10 -padx 10
   }

   # fill the listbox with all shortcut labels
   .fscedit.list delete 0 end
   for {set index 0} {$index < $fscedit_count} {incr index} {
      .fscedit.list insert end [lindex $fscedit_sclist($index) $fsc_name_idx]
   }
   .fscedit.list configure -height $fscedit_count
   # set invalid index since no item is selected in the listbox yet (must be done by caller)
   set fscedit_idx -1
}

# "Abort" command button - ask to confirm before changes are lost
proc AbortEditedShortcuts {} {
   global shortcuts shortcut_count
   global fscedit_sclist fscedit_count

   if {$shortcut_count != $fscedit_count} {
      set changed 1
   } else {
      set changed 0
      foreach {idx val} [array get fscedit_sclist] {
         if {![info exists shortcuts($idx)] || ([string compare $val $shortcuts($idx)] != 0)} {
            set changed 1
            break
         }
      }
   }
   if {$changed} {
      set answer [tk_messageBox -type okcancel -icon warning -parent .fscedit -message "Discard all changes?"]
      if {[string compare $answer cancel] == 0} {
         return
      }
   }
   destroy .fscedit
}

# "Save" command button - copy the temporary array onto the global shortcuts
proc SaveEditedShortcuts {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global fscedit_sclist fscedit_count
   global shortcuts shortcut_count

   # check if there are changed settings that have not been saved
   if [CheckShortcutUpdatePending yesnocancel] {
      # user pressed the "Cancel" button
      return
   }

   # copy the temporary array back into the shortcuts array
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

# selection of a shortcut in the listbox - update all displayed info in the popup
proc SelectEditedShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global fscedit_label fscedit_mask fscedit_logic fscedit_idx
   global fscedit_sclist fscedit_count

   # check if there are changed settings that have not been saved
   CheckShortcutUpdatePending yesno

   set sel [.fscedit.list curselection]
   if {([string length $sel] > 0) && ($sel < $fscedit_count)} {
      # remember the index of the currently selected shortcut
      set fscedit_idx $sel

      # set label displayed in entry widget
      set fscedit_label [lindex $fscedit_sclist($sel) $fsc_name_idx]
      .fscedit.cmd.label selection range 0 end

      # display description
      .fscedit.flags.desc.tx configure -state normal
      .fscedit.flags.desc.tx delete 1.0 end
      .fscedit.flags.desc.tx insert end [ShortcutPrettyPrint [lindex $fscedit_sclist($sel) $fsc_filt_idx]]
      .fscedit.flags.desc.tx configure -state disabled

      # set combination logic radiobuttons
      set fscedit_logic [lindex $fscedit_sclist($sel) $fsc_logi_idx]

      # set mask checkbuttons
      if {[info exists fscedit_mask]} {unset fscedit_mask}
      foreach index [lindex $fscedit_sclist($sel) $fsc_mask_idx] {
         set fscedit_mask($index) 1
      }
   }
}

# Subroutine: Collect shortcut settings from entry and checkbutton widgets
proc GetUpdatedShortcut {new_sc} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global fscedit_sclist fscedit_count
   global fscedit_mask fscedit_logic fscedit_label

   # update filter mask from checkbuttons
   set mask {}
   foreach index [array names fscedit_mask] {
      if {$fscedit_mask($index) != 0} {
         lappend mask $index
      }
   }
   set new_sc [lreplace $new_sc $fsc_mask_idx $fsc_mask_idx $mask]

   # update combination logic setting
   set new_sc [lreplace $new_sc $fsc_logi_idx $fsc_logi_idx $fscedit_logic]

   # update description label from entry widget
   if {[string length $fscedit_label] > 0} {
      set new_sc [lreplace $new_sc $fsc_name_idx $fsc_name_idx $fscedit_label]
   }

   return $new_sc
}

# "Update" command button
proc UpdateEditedShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global fscedit_sclist fscedit_count
   global fscedit_idx

   if [info exists fscedit_sclist($fscedit_idx)] {

      set fscedit_sclist($fscedit_idx) [GetUpdatedShortcut $fscedit_sclist($fscedit_idx)]

      # update the label in the shortcut listbox
      set old_sel [.fscedit.list curselection]
      .fscedit.list delete $fscedit_idx
      .fscedit.list insert $fscedit_idx [lindex $fscedit_sclist($fscedit_idx) $fsc_name_idx]
      if {$old_sel == $fscedit_idx} {
         # put the cursor onto the updated item again
         .fscedit.list selection set $fscedit_idx
      }
   }
}

# Subroutine: Check if user has changed settings for the curently selected shortcut
proc CheckShortcutUpdatePending {type} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global fscedit_sclist fscedit_count
   global fscedit_idx

   set cancel 0
   if [info exists fscedit_sclist($fscedit_idx)] {

      set new_sc [GetUpdatedShortcut $fscedit_sclist($fscedit_idx)]
      if {![CompareShortcuts $new_sc $fscedit_sclist($fscedit_idx)]} {
         set answer [tk_messageBox -type $type -default yes -icon question -parent .fscedit \
                        -message "Update the shortcut '[lindex $fscedit_sclist($fscedit_idx) $fsc_name_idx]' with the changed settings?"]
         if {[string compare $answer yes] == 0} {
            UpdateEditedShortcut
         } elseif {[string compare $answer cancel] == 0} {
            set cancel 1
         }
      }
   }
   return $cancel
}

# "Delete" command button
proc DeleteEditedShortcut {} {
   global fscedit_sclist fscedit_count
   global fscedit_idx

   set sel [.fscedit.list curselection]
   if {([string length $sel] > 0) && ($sel < $fscedit_count)} {
      for {set index [expr $sel + 1]} {$index < $fscedit_count} {incr index} {
         set fscedit_sclist([expr $index - 1]) $fscedit_sclist($index)
      }
      incr fscedit_count -1

      set fscedit_idx -1
      .fscedit.list delete $sel
      if {$sel < $fscedit_count} {
         .fscedit.list selection set $sel
         SelectEditedShortcut
      } elseif {$fscedit_count > 0} {
         .fscedit.list selection set [expr $sel - 1]
         SelectEditedShortcut
      }
   }
}

# "Up-Arrow" command button
proc ShiftUpEditedShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global fscedit_sclist fscedit_count
   global fscedit_idx

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
      set fscedit_idx $sel
   }
}

# "Down-Arrow" command button
proc ShiftDownEditedShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global fscedit_sclist fscedit_count
   global fscedit_idx

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
      set fscedit_idx $sel
   }
}

## ---------------------------------------------------------------------------
##                    R C - F I L E   H A N D L I N G
## ---------------------------------------------------------------------------
set myrcfile ""

proc LoadRcFile {filename} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_logi_idx fsc_tag_idx
   global shortcuts shortcut_count
   global prov_selection prov_freqs cfnetwops cfnetnames
   global showNetwopListbox showShortcutListbox showStatusLine showColumnHeader
   global prov_merge_cnis prov_merge_cf
   global acq_mode acq_mode_cnis
   global pibox_height pilistbox_cols shortinfo_height
   global hwcfg xawtvcf ctxmencf
   global substr_history
   global dumpdb_filename
   global dumpdb_pi dumpdb_xi dumpdb_ai dumpdb_ni dumpdb_oi dumpdb_mi dumpdb_li dumpdb_ti
   global dumphtml_filename dumphtml_type dumphtml_sel_only
   global dumphtml_hyperlinks dumphtml_file_append dumphtml_file_overwrite
   global EPG_VERSION_NO
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

      # for backwards compatibility append freq table index to hw cfg
      if {[info exists hwcfg] && ([llength $hwcfg] == 5)} {lappend hwcfg 0}

      if {![info exists nxtvepg_version] || ($nxtvepg_version < 0x000600)} {
         set substr_history {}
         for {set index 0} {$index < $shortcut_count} {incr index} {
            # starting with version 0.5.0 an ID tag was appended
            if {[llength $shortcuts($index)] == 4} {
               lappend shortcuts($index) [GenerateShortcutTag]
            }
            # since version 0.6.0 text search has an additional parameter "match full" and "ignore case" was inverted to "match case"
            set filt [lindex $shortcuts($index) $fsc_filt_idx]
            set newfilt {}
            foreach {tag val} $filt {
               if {([string compare $tag "substr"] == 0) && ([llength $val] == 4)} {
                  set val [list [lindex $val 0] [lindex $val 1] [expr 1 - [lindex $val 2]] 0 [lindex $val 3]]
               }
               lappend newfilt $tag $val
            }
            set shortcuts($index) [list \
               [lindex $shortcuts($index) $fsc_name_idx] \
               [lindex $shortcuts($index) $fsc_mask_idx] \
               $newfilt \
               [lindex $shortcuts($index) $fsc_logi_idx] \
               [lindex $shortcuts($index) $fsc_tag_idx] ]
         }
      }
   }

   if {$showShortcutListbox == 0} {pack forget .all.shortcuts}
   if {$showNetwopListbox == 0} {pack forget .all.netwops}
   if {$showStatusLine == 0} {pack forget .all.statusline}

   # create the column headers above the PI browser text widget
   ApplySelectedColumnList initial

   # set the height of the listbox text widget
   if {[info exists pibox_height]} {
      .all.pi.list.text configure -height $pibox_height
   }
   if {[info exists shortinfo_height]} {
      .all.pi.info.text configure -height $shortinfo_height
   }

   if {$shortcut_count == 0} {
      PreloadShortcuts
   }

   # fill the shortcut listbox
   for {set index 0} {$index < $shortcut_count} {incr index} {
      .all.shortcuts.list insert end [lindex $shortcuts($index) $fsc_name_idx]
   }
   .all.shortcuts.list configure -height $shortcut_count

   if {[info exists EPG_VERSION_NO] && [info exists nxtvepg_version]} {
      if {$nxtvepg_version > $EPG_VERSION_NO} {
         tk_messageBox -type ok -default ok -icon warning -message "The config file is from a newer version of nxtvepg. Some settings may get lost."
      }
   }
}

##
##  Write all config variables into the rc/ini file
##
proc UpdateRcFile {} {
   global shortcuts shortcut_count
   global prov_selection prov_freqs cfnetwops cfnetnames
   global showNetwopListbox showShortcutListbox showStatusLine showColumnHeader
   global prov_merge_cnis prov_merge_cf
   global acq_mode acq_mode_cnis
   global pibox_height pilistbox_cols shortinfo_height
   global hwcfg xawtvcf ctxmencf
   global substr_history
   global dumpdb_filename
   global dumpdb_pi dumpdb_xi dumpdb_ai dumpdb_ni dumpdb_oi dumpdb_mi dumpdb_li dumpdb_ti
   global dumphtml_filename dumphtml_type dumphtml_sel_only
   global dumphtml_hyperlinks dumphtml_file_append dumphtml_file_overwrite
   global EPG_VERSION EPG_VERSION_NO
   global myrcfile

   if {[catch {set rcfile [open $myrcfile "w"]}] == 0} {
      puts $rcfile "#"
      puts $rcfile "# Nextview EPG configuration file"
      puts $rcfile "#"
      puts $rcfile "# This file is automatically generated - do not edit"
      puts $rcfile "# Written at: [clock format [clock seconds] -format %c]"
      puts $rcfile "#"

      # dump software version
      puts $rcfile [list set nxtvepg_version $EPG_VERSION_NO]
      puts $rcfile [list set nxtvepg_version_str $EPG_VERSION]

      # dump filter shortcuts
      for {set index 0} {$index < $shortcut_count} {incr index} {
         puts $rcfile [list set shortcuts($index) $shortcuts($index)]
      }
      puts $rcfile [list set shortcut_count $shortcut_count]

      # dump provider selection order
      puts $rcfile [list set prov_selection $prov_selection]

      # dump provider frequency list
      puts $rcfile [list set prov_freqs $prov_freqs]

      # dump network selection for all providers
      foreach index [array names cfnetwops] {
         puts $rcfile [list set cfnetwops($index) $cfnetwops($index)]
      }
      # dump network names
      if [array exists cfnetnames] {
         puts $rcfile [list array set cfnetnames [array get cfnetnames]]
      }

      # dump shortcuts & network listbox visibility
      puts $rcfile [list set showNetwopListbox $showNetwopListbox]
      puts $rcfile [list set showShortcutListbox $showShortcutListbox]
      puts $rcfile [list set showStatusLine $showStatusLine]
      puts $rcfile [list set showColumnHeader $showColumnHeader]

      # dump provider database merge CNIs and configuration
      if {[info exists prov_merge_cnis]} {puts $rcfile [list set prov_merge_cnis $prov_merge_cnis]}
      if {[info exists prov_merge_cf]} {puts $rcfile [list set prov_merge_cf $prov_merge_cf]}

      # dump browser listbox configuration
      if {[info exists pibox_height]} {puts $rcfile [list set pibox_height $pibox_height]}
      if {[info exists pilistbox_cols]} {puts $rcfile [list set pilistbox_cols $pilistbox_cols]}
      if {[info exists shortinfo_height]} {puts $rcfile [list set shortinfo_height $shortinfo_height]}

      # dump acquisition mode and provider CNIs
      if {[info exists acq_mode]} { puts $rcfile [list set acq_mode $acq_mode] }
      if {[info exists acq_mode_cnis]} {puts $rcfile [list set acq_mode_cnis $acq_mode_cnis]}

      if {[info exists hwcfg]} {puts $rcfile [list set hwcfg $hwcfg]}

      puts $rcfile [list set xawtvcf $xawtvcf]
      puts $rcfile [list set ctxmencf $ctxmencf]

      if {[info exists substr_history]} {puts $rcfile [list set substr_history $substr_history]}

      puts $rcfile [list set dumpdb_filename $dumpdb_filename]
      puts $rcfile [list set dumpdb_pi $dumpdb_pi]
      puts $rcfile [list set dumpdb_xi $dumpdb_xi]
      puts $rcfile [list set dumpdb_ai $dumpdb_ai]
      puts $rcfile [list set dumpdb_ni $dumpdb_ni]
      puts $rcfile [list set dumpdb_oi $dumpdb_oi]
      puts $rcfile [list set dumpdb_mi $dumpdb_mi]
      puts $rcfile [list set dumpdb_li $dumpdb_li]
      puts $rcfile [list set dumpdb_ti $dumpdb_ti]

      puts $rcfile [list set dumphtml_filename $dumphtml_filename]
      puts $rcfile [list set dumphtml_type $dumphtml_type]
      puts $rcfile [list set dumphtml_sel_only $dumphtml_sel_only]
      puts $rcfile [list set dumphtml_hyperlinks $dumphtml_hyperlinks]
      puts $rcfile [list set dumphtml_file_append $dumphtml_file_append]
      puts $rcfile [list set dumphtml_file_overwrite $dumphtml_file_overwrite]

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

   # check if an update is required
   if {($cni != [lindex $prov_selection 0]) && ($cni != 0)} {

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
}

##
##  Update the frequency for a given provider
##
set prov_freqs {}

proc UpdateProvFrequency {cni freq} {
   global prov_freqs

   # search the list for the given CNI
   set idx 0
   foreach {rc_cni rc_freq} $prov_freqs {
      if {$rc_cni == $cni} {
         if {$rc_freq != $freq} {
            # CNI already in the list with a different frequency -> update
            set prov_freqs [lreplace $prov_freqs $idx [expr $idx + 1] $cni $freq]
            UpdateRcFile
         }
         return
      }
      incr idx 2
   }
   # not found in the list -> append new pair to the list
   lappend prov_freqs $cni $freq
   UpdateRcFile
}

