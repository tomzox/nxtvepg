#
#  Nextview main window handling: PI listbox and menubar
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
#    Implements the core functionality for the main window, i.e.
#    menu bar (including handling of filter setting), shortcut and
#    network filter lists, programme list (PI list) and short-info.
#
#  Author: Tom Zoerner
#
#  $Id: mainwin.tcl,v 1.200 2002/11/17 19:40:38 tom Exp tom $
#

##  ---------------------------------------------------------------------------
##  Set up global variables which define general widget parameters
##  - called once during start-up
##  - applies user specified values in the X resource file
##
proc LoadWidgetOptions {} {
   global font_normal font_fixed pi_font pi_bold_font xawtv_font help_font
   global text_bg default_bg pi_bg_now pi_bg_past help_bg
   global pi_cursor_bg pi_cursor_bg_now pi_cursor_bg_past
   global entry_disabledforeground
   global tcl_version is_unix

   if {$is_unix} {
      set font_pt_size  12
      # background for cursor in TV schedule
      set pi_cursor_bg      #c3c3c3
      # background for cursor when above a currently running programme
      set pi_cursor_bg_now  #b8b8df
      # background for cursor when above an expired programme
      set pi_cursor_bg_past #dfb8b8
   } else {
      if {$tcl_version >= 8.3} {
         set font_pt_size  12
      } else {
         set font_pt_size  15
      }
      set pi_cursor_bg      #d4d4d4
      set pi_cursor_bg_now  #d8d8ff
      set pi_cursor_bg_past #ffd8d8
   }

   # background color for all text and list in- and output fields
   set text_bg    #E9E9EC
   # background in TV schedule for currently running programmes
   set pi_bg_now  #d9d9ef
   # background in TV schedule for expired programmes
   set pi_bg_past #dfc9c9
   # background for help text
   set help_bg    #ffd840

   set font_normal [list helvetica [expr   0 - $font_pt_size] normal]
   set font_fixed  [list courier   [expr   0 - $font_pt_size] normal]

   # font for TV schedule and programme description text
   set pi_font      $font_normal
   set pi_bold_font [DeriveFont $font_normal 0 bold]
   # font for help text
   set help_font    $font_normal
   # font for xawtv popup window
   set xawtv_font   [DeriveFont $font_normal 2 bold]
   # font for message popups (e.g. warnings and error messages)
   set msgbox_font  [DeriveFont $font_normal 2 bold]

   # WIN32 only: load resources from a local file
   # (on UNIX these are stored in the X server and read during startup from .Xdefaults)
   if {$is_unix == 0} {
      catch {option readfile nxtvepg.ad userDefault}
   }
   # create temporary widget to check syntax of configuration options
   label .test_opt
   # load and check all color resources
   foreach opt {pi_cursor_bg pi_cursor_bg_now pi_cursor_bg_past text_bg pi_bg_now pi_bg_past help_bg} {
      set value [option get . $opt userDefault]
      if {([string length $value] > 0) && \
          ([catch [list .test_opt configure -foreground $value]] == 0)} {
         set $opt $value
      }
   }

   # load and check all font resources
   foreach opt {pi_font xawtv_font help_font msgbox_font} {
      set value [option get . $opt userDefault]
      if {([llength $value] == 3) && \
          ([catch [list .test_opt configure -font $value]] == 0)} {
         set $opt $value
      }
   }
   destroy .test_opt

   # starting with Tk8.4 an entry's text is grey when disabled and
   # this new option must be used where this is not desirable
   if {$tcl_version >= 8.4} {
      set entry_disabledforeground "-disabledforeground"
   } else {
      set entry_disabledforeground "-foreground"
   }

   if {[llength [info commands tkButtonInvoke]] == 0} {
      proc tkButtonInvoke {w} {tk::ButtonInvoke $w}
   }

   set default_bg [. cget -background]

   # map "artifical" resources onto internal Tk resources
   option add *Dialog.msg.font $msgbox_font userDefault
   option add *Listbox.background $text_bg userDefault
   option add *Entry.background $text_bg userDefault
   option add *Text.background $text_bg userDefault
}

# helper function to modify a font's size or appearance
proc DeriveFont {afont delta_size {style {}}} {
   if {[string length $style] == 0} {
      set style [lindex $afont 2]
   }
   set size [lindex $afont 1]
   if {$size < 0} {set delta_size [expr 0 - $delta_size]}
   incr size $delta_size

   return [list [lindex $afont 0] $size $style]
}

##  ---------------------------------------------------------------------------
##  Create the main window and menu bar
##  - called once during start-up
##
proc CreateMainWindow {} {
   global is_unix entry_disabledforeground
   global text_bg pi_bg_now pi_bg_past default_bg
   global font_normal pi_font pi_bold_font
   global fileImage pi_img

   # copy event bindings which are required for scrolling and selection (outbound copy&paste)
   foreach event {<ButtonPress-1> <ButtonRelease-1> <B1-Motion> <Double-Button-1> <Shift-Button-1> \
                  <Triple-Button-1> <Triple-Shift-Button-1> <Button-2> <B2-Motion> <MouseWheel> \
                  <<Copy>> <<Clear>> <Shift-Key-Tab> <Control-Key-Tab> <Control-Shift-Key-Tab> \
                  <Key-Prior> <Key-Next> <Key-Down> <Key-Up> <Key-Left> <Key-Right> \
                  <Shift-Key-Left> <Shift-Key-Right> <Shift-Key-Up> <Shift-Key-Down> \
                  <Key-Home> <Key-End> <Shift-Key-Home> <Shift-Key-End> <Control-Key-slash>} {
      bind TextReadOnly $event [bind Text $event]
   }
   bind TextReadOnly <Key-Tab> [bind Text <Control-Key-Tab>]
   bind TextReadOnly <Control-Key-c> [bind Text <<Copy>>]

   frame     .all -relief flat -borderwidth 0
   frame     .all.shortcuts -borderwidth 2
   label     .all.shortcuts.clock -padx 7 -text {}
   #grid     .all.shortcuts.clock -row 0 -column 0 -pady 5 -sticky nwe

   button    .all.shortcuts.tune -text "Tune TV" -relief ridge -command TuneTV -takefocus 0
   bind      .all.shortcuts.tune <Button-3> {TuneTvPopupMenu 1 %x %y}
   #grid     .all.shortcuts.tune -row 1 -column 0 -sticky nwe

   button    .all.shortcuts.reset -text "Reset" -relief ridge -command {ResetFilterState; C_ResetPiListbox}
   #grid     .all.shortcuts.reset -row 2 -column 0 -sticky nwe

   menu      .tunetvcfg -tearoff 0
   .tunetvcfg add command -label "Start Capturing" -command {C_Tvapp_SendCmd capture on}
   .tunetvcfg add command -label "Stop Capturing" -command {C_Tvapp_SendCmd capture off}
   .tunetvcfg add command -label "Toggle mute" -command {C_Tvapp_SendCmd volume mute}
   .tunetvcfg add separator
   .tunetvcfg add command -label "Toggle TV station" -command {C_Tvapp_SendCmd setstation back}
   .tunetvcfg add command -label "Next TV station" -command {C_Tvapp_SendCmd setstation next}
   .tunetvcfg add command -label "Previous TV station" -command {C_Tvapp_SendCmd setstation prev}

   menu      .ctx_netsel -tearoff 0
   .ctx_netsel add checkbutton -label Invert -variable filter_invert(netwops) -command InvertFilter

   menu      .ctx_shortcuts -tearoff 0
   .ctx_shortcuts add command -label name -state disabled
   .ctx_shortcuts add command -label "Update" -command {UpdateFilterShortcutByContext $ctxmen_selidx}
   .ctx_shortcuts add command -label "Delete" -command {DeleteFilterShortcut $ctxmen_selidx}
   .ctx_shortcuts add separator
   .ctx_shortcuts add command -label "Edit..." -command {EditFilterShortcuts $ctxmen_selidx}
   .ctx_shortcuts add command -label "Add new..." -command {AddFilterShortcut $ctxmen_selidx}

   listbox   .all.shortcuts.list -exportselection false -height 12 -width 0 -selectmode extended \
                                 -relief ridge -cursor top_left_arrow
   bind      .all.shortcuts.list <<ListboxSelect>> {InvokeSelectedShortcuts}
   bind      .all.shortcuts.list <Key-Return> {InvokeSelectedShortcuts}
   bind      .all.shortcuts.list <Key-Escape> {.all.shortcuts.list selection clear 0 end; InvokeSelectedShortcuts}
   bind      .all.shortcuts.list <FocusOut> {InvokeSelectedShortcuts}
   bind      .all.shortcuts.list <Button-3> {CreateListboxContextMenu .ctx_shortcuts .all.shortcuts.list %x %y}
   #grid     .all.shortcuts.list -row 3 -column 0 -sticky nwe

   frame     .all.netwops
   listbox   .all.shortcuts.netwops -exportselection false -height 25 -width 0 -selectmode extended \
                               -relief ridge -cursor top_left_arrow
   .all.shortcuts.netwops insert end "-all-"
   .all.shortcuts.netwops selection set 0
   bind      .all.shortcuts.netwops <<ListboxSelect>> {SelectNetwop}
   bind      .all.shortcuts.netwops <Key-Return> {SelectNetwop}
   bind      .all.shortcuts.netwops <Key-Escape> {.all.shortcuts.netwops selection clear 0 end; SelectNetwop}
   bind      .all.shortcuts.netwops <FocusOut> {SelectNetwop}
   bind      .all.shortcuts.netwops <Button-3> {tk_popup .ctx_netsel %X %Y 0}
   #grid     .all.shortcuts.netwops -row 4 -column 0 -sticky nwe
   #pack     .all.shortcuts -anchor nw -side left

   frame     .all.pi
   #panedwindow .all.pi -orient vertical
   frame     .all.pi.list
   button    .all.pi.list.colcfg -command PopupColumnSelection -bitmap "bitmap_colsel" \
                                 -cursor top_left_arrow -borderwidth 1 -padx 0 -pady 0 -takefocus 0
   grid      .all.pi.list.colcfg -row 0 -column 0 -sticky news
   frame     .all.pi.list.colheads
   grid      .all.pi.list.colheads -row 0 -column 1 -sticky news

   scrollbar .all.pi.list.sc -orient vertical -command {C_PiListBox_Scroll} -takefocus 0
   grid      .all.pi.list.sc -row 1 -column 0 -sticky ns
   text      .all.pi.list.text -width 50 -height 25 -wrap none \
                               -font $pi_font -exportselection false \
                               -cursor top_left_arrow \
                               -insertofftime 0
   bindtags  .all.pi.list.text {.all.pi.list.text . all}
   #bind      .all.pi.list.text <Configure> PiListboxResized
   bind      .all.pi.list.text <Button-1> {SelectPi %x %y}
   bind      .all.pi.list.text <ButtonRelease-1> {SelectPiRelease}
   bind      .all.pi.list.text <Double-Button-2> {C_PopupPi %x %y}
   bind      .all.pi.list.text <Button-3> {CreateContextMenu mouse %x %y}
   bind      .all.pi.list.text <Button-4> {C_PiListBox_Scroll scroll -3 units}
   bind      .all.pi.list.text <Button-5> {C_PiListBox_Scroll scroll 3 units}
   bind      .all.pi.list.text <MouseWheel> {C_PiListBox_Scroll scroll [expr int((%D + 20) / -40)] units}
   bind      .all.pi.list.text <Up>    {C_PiListBox_CursorUp}
   bind      .all.pi.list.text <Down>  {C_PiListBox_CursorDown}
   bind      .all.pi.list.text <Control-Up>   {C_PiListBox_Scroll scroll -1 units}
   bind      .all.pi.list.text <Control-Down> {C_PiListBox_Scroll scroll 1 units}
   bind      .all.pi.list.text <Prior> {C_PiListBox_Scroll scroll -1 pages}
   bind      .all.pi.list.text <Next>  {C_PiListBox_Scroll scroll 1 pages}
   bind      .all.pi.list.text <Home>  {C_PiListBox_Scroll moveto 0.0; C_PiListBox_SelectItem 0}
   bind      .all.pi.list.text <End>   {C_PiListBox_Scroll moveto 1.0; C_PiListBox_Scroll scroll 1 pages}
   bind      .all.pi.list.text <Key-1> {ToggleShortcut 0}
   bind      .all.pi.list.text <Key-2> {ToggleShortcut 1}
   bind      .all.pi.list.text <Key-3> {ToggleShortcut 2}
   bind      .all.pi.list.text <Key-4> {ToggleShortcut 3}
   bind      .all.pi.list.text <Key-5> {ToggleShortcut 4}
   bind      .all.pi.list.text <Key-6> {ToggleShortcut 5}
   bind      .all.pi.list.text <Key-7> {ToggleShortcut 6}
   bind      .all.pi.list.text <Key-8> {ToggleShortcut 7}
   bind      .all.pi.list.text <Key-9> {ToggleShortcut 8}
   bind      .all.pi.list.text <Key-0> {ToggleShortcut 9}
   bind      .all.pi.list.text <Return> {if {[llength [info commands .all.shortcuts.tune]] != 0} {tkButtonInvoke .all.shortcuts.tune}}
   bind      .all.pi.list.text <Double-Button-1> {if {[llength [info commands .all.shortcuts.tune]] != 0} {tkButtonInvoke .all.shortcuts.tune}}
   bind      .all.pi.list.text <Escape> {tkButtonInvoke .all.shortcuts.reset}
   bind      .all.pi.list.text <Control-Key-f> {SubStrPopup}
   bind      .all.pi.list.text <Control-Key-c> {CreateContextMenu key 0 0}
   #bind    .all.pi.list.text <Control-Key-n> {tk_popup .all.pi.list.colheads.col_netname.b.men %X %Y 0}
   bind      .all.pi.list.text <Enter> {focus %W}
   .all.pi.list.text tag configure cur -relief raised -borderwidth 1
   .all.pi.list.text tag configure now -background $pi_bg_now
   .all.pi.list.text tag configure past -background $pi_bg_past
   .all.pi.list.text tag configure bold -font $pi_bold_font
   .all.pi.list.text tag configure underline -underline 1
   .all.pi.list.text tag configure black -foreground black
   .all.pi.list.text tag configure red -foreground #CC0000
   .all.pi.list.text tag configure blue -foreground #0000CC
   .all.pi.list.text tag configure green -foreground #00CC00
   .all.pi.list.text tag configure yellow -foreground #CCCC00
   .all.pi.list.text tag configure pink -foreground #CC00CC
   .all.pi.list.text tag configure cyan -foreground #00CCCC
   .all.pi.list.text tag lower now
   .all.pi.list.text tag lower past
   grid      .all.pi.list.text -row 1 -column 1 -sticky news
   grid      columnconfigure .all.pi.list 1 -weight 1
   grid      rowconfigure .all.pi.list 1 -weight 1
   pack      .all.pi.list -side top -fill x
   #.all.pi   add .all.pi.list -sticky news -minsize 40 -height 372

   button    .all.pi.panner -bitmap bitmap_pan_updown -cursor top_left_arrow -takefocus 0
   bind      .all.pi.panner <ButtonPress-1> {+ PanningControl 1}
   bind      .all.pi.panner <ButtonRelease-1> {+ PanningControl 0}
   pack      .all.pi.panner -side top -anchor e

   frame     .all.pi.info
   scrollbar .all.pi.info.sc -orient vertical -command {.all.pi.info.text yview} -takefocus 0
   pack      .all.pi.info.sc -side left -fill y -anchor e
   text      .all.pi.info.text -width 50 -height 10 -wrap word \
                               -font $pi_font \
                               -yscrollcommand {.all.pi.info.sc set} \
                               -insertofftime 0
   .all.pi.info.text tag configure title -font [DeriveFont $pi_font 2 bold] -justify center -spacing1 2 -spacing3 4
   .all.pi.info.text tag configure features -font [DeriveFont $pi_font 0 bold] -justify center -spacing3 6
   .all.pi.info.text tag configure bold -font [DeriveFont $pi_font 0 bold] -spacing3 4
   .all.pi.info.text tag configure paragraph -font $pi_font -spacing3 4
   # remove the regular text widget event bindings
   bindtags  .all.pi.info.text {.all.pi.info.text TextReadOnly . all}
   bind      .all.pi.info.text <Configure> ShortInfoResized
   bind      .all.pi.info.text <Button-4> {.all.pi.info.text yview scroll -3 units}
   bind      .all.pi.info.text <Button-5> {.all.pi.info.text yview scroll 3 units}
   pack      .all.pi.info.text -side left -fill both -expand 1
   pack      .all.pi.info -side top -fill both -expand 1
   #.all.pi   add .all.pi.info -sticky news -minsize 25 -height 150
   pack      .all.pi -side top -fill y -expand 1

   entry     .all.statusline -state disabled -relief flat -borderwidth 1 \
                             -font [DeriveFont $font_normal -2] -background $default_bg $entry_disabledforeground black \
                             -textvariable dbstatus_line
   pack      .all.statusline -side bottom -fill x
   pack      .all -side left -fill y -expand 1

   bind      . <Key-F1> {PopupHelp $helpIndex(Basic browsing) {}}
   focus     .all.pi.list.text


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

   # images used as markers in user-defined columns
   # (note: image data is base64 encoded; the original GIFs can be obtained by feeding the string into mmencode -u)
   set name [image create photo -data R0lGODlhCQAJAMIAAAAAAPj8+JiYmHB0cCgoKAAAAAAAAAAAACH5BAEAAAEALAAAAAAJAAkAAAMaGBqyLqA9MOKCFVisASENpH0S44EOg6aMkwAAOw==]
   set pi_img(diamond_black) [list $name [image width $name]]
   set name [image create photo -data R0lGODlhCQAJAMIAAAAAuPj8+Hh8+AAA+JiYmAAAAAAAAAAAACH5BAEAAAEALAAAAAAJAAkAAAMaGBq0TqO9IeKCdVisBwANpH0S44EOg6aMkwAAOw==]
   set pi_img(diamond_blue) [list $name [image width $name]]
   set name [image create photo -data R0lGODlhCQAJAMIAAAD8+Pj8+ADQ0JiYmACYmAAAAAAAAAAAACH5BAEAAAEALAAAAAAJAAkAAAMaGBqzPqI9AeKCVVisBSENpH0S44EOg6aMkwAAOw==]
   set pi_img(diamond_cyan) [list $name [image width $name]]
   set name [image create photo -data R0lGODlhCQAJAMIAAPj8+JiYmAD8AADUAACMAAAAAAAAAAAAACH5BAEAAAAALAAAAAAJAAkAAAMaCAqxHqO9IeKCdVisByENpH0S44EOg6aMkwAAOw==]
   set pi_img(diamond_green) [list $name [image width $name]]
   set name [image create photo -data R0lGODlhCQAJAMIAAPgAAPj8+LgAAPioqJiYmAAAAAAAAAAAACH5BAEAAAEALAAAAAAJAAkAAAMaGBq0TqA9MOKCFVisgRANpH0S44EOg6aMkwAAOw==]
   set pi_img(diamond_red) [list $name [image width $name]]
   set name [image create photo -data R0lGODlhCQAJAMIAAPj8+Phc+PgA+JiYmMgAyAAAAAAAAAAAACH5BAEAAAAALAAAAAAJAAkAAAMaCAqzPqI9EeKCVVisBSENpH0S44EOg6aMkwAAOw==]
   set pi_img(diamond_pink) [list $name [image width $name]]
   set name [image create photo -data R0lGODlhCQAJAMIAAODgALi4APj8+JiYmPj8AAAAAAAAAAAAACH5BAEAAAIALAAAAAAJAAkAAAMZKCqzPqA9QOKCFVisQQhN5oHXKF2ko5xCAgA7]
   set pi_img(diamond_yellow) [list $name [image width $name]]

   set name [image create photo -data R0lGODlhBwAHAMIAAAAAAPj8+JiYmHB0cCgoKAAAAAAAAAAAACH5BAEAAAEALAAAAAAHAAcAAAMUGCGsSwSIN0ZkpEIG4F1AOCnMmAAAOw==]
   set pi_img(circle_black) [list $name [image width $name]]
   set name [image create photo -data R0lGODlhBwAHAMIAAAAAuPj8+Hh8+JiYmDAw+AAAAAAAAAAAACH5BAEAAAEALAAAAAAHAAcAAAMUGDGsSwSMJ0RkpEIG4F2d5DBTkAAAOw==]
   set pi_img(circle_blue) [list $name [image width $name]]
   set name [image create photo -data R0lGODlhBwAHAMIAAAD8+Pj8+ADQ0JiYmACYmAAAAAAAAAAAACH5BAEAAAEALAAAAAAHAAcAAAMUGDGsK4KMB0BkokJG4F2d5DBTkAAAOw==]
   set pi_img(circle_cyan) [list $name [image width $name]]
   set name [image create photo -data R0lGODlhBwAHAMIAAPj8+JiYmAD8AADUAACMAAAAAAAAAAAAACH5BAEAAAAALAAAAAAHAAcAAAMUCBCsO4OEJ0Rko0JG4F2d5DATkAAAOw==]
   set pi_img(circle_green) [list $name [image width $name]]
   set name [image create photo -data R0lGODlhBwAHAMIAAPgAAPiUkPj8+LgAAJiYmAAAAAAAAAAAACH5BAEAAAIALAAAAAAHAAcAAAMUKEKsC2CQF0JkoEI24F2d5DCTkAAAOw==]
   set pi_img(circle_red) [list $name [image width $name]]
   set name [image create photo -data R0lGODlhBwAHAMIAAPj8+Phc+PgA+JiYmMgAyAAAAAAAAAAAACH5BAEAAAAALAAAAAAHAAcAAAMUCDCsK4KMF0JkokJG4F2d5DATkAAAOw==]
   set pi_img(circle_pink) [list $name [image width $name]]
   set name [image create photo -data R0lGODlhBwAHAMIAAPj8+JiYmMjMAKCgAPj8AAAAAAAAAAAAACH5BAEAAAAALAAAAAAHAAcAAAMUCBCsK2KER0hkokI24F2d5DATkAAAOw==]
   set pi_img(circle_yellow) [list $name [image width $name]]

   set name [image create photo -data R0lGODdhBQAFAMIAALjAwJiYmPj8+KissAAAAAAAAAAAAAAAACwAAAAABQAFAAADCwixKjLrkTXIVCwBADs=]
   set pi_img(dot_black) [list $name [image width $name]]
   set name [image create photo -data R0lGODdhBQAFAMIAALjAwJiYmAAA+AAAwAAAkAAAAAAAAAAAACwAAAAABQAFAAADCwixKjLrkTXIVCwBADs=]
   set pi_img(dot_blue) [list $name [image width $name]]
   set name [image create photo -data R0lGODdhBQAFAMIAALjAwJCMiAD8+ADMyACcmAAAAAAAAAAAACwAAAAABQAFAAADCwixKjLrkTXIVCwBADs=]
   set pi_img(dot_cyan) [list $name [image width $name]]
   set name [image create photo -data R0lGODdhBQAFAMIAALjAwJCwiAD8AADUAACMAAAAAAAAAAAAACwAAAAABQAFAAADCwixKjLrkTXIVCwBADs=]
   set pi_img(dot_green) [list $name [image width $name]]
   set name [image create photo -data R0lGODdhBQAFAMIAALjAwJCMiPgAAMgAAJgAAAAAAAAAAAAAACwAAAAABQAFAAADCwixKjLrkTXIVCwBADs=]
   set pi_img(dot_red) [list $name [image width $name]]
   set name [image create photo -data R0lGODlhBQAFAMIAAPj8+KiwsPhc+PgA+MgAyJCMiAAAAAAAACH5BAEAAAAALAAAAAAFAAUAAAMMCFGhImNBwgYhbbUEADs=]
   set pi_img(dot_pink) [list $name [image width $name]]
   set name  [image create photo -data R0lGODdhBQAFAMIAALjAwJCMiPj8AMjMAJicAAAAAAAAAAAAACwAAAAABQAFAAADCwixKjLrkTXIVCwBADs=]
   set pi_img(dot_yellow) [list $name [image width $name]]
}


# initialize menu state
set menuStatusStartAcq 0
set menuStatusDaemon 0
set menuStatusDumpStream 0
set menuStatusThemeClass 1
set menuStatusTscaleOpen(ui) 0
set menuStatusTscaleOpen(acq) 0
set menuStatusStatsOpen(ui) 0
set menuStatusStatsOpen(acq) 0

array set pi_attr_labels [list \
   features Features \
   parental {Parental rating} \
   editorial {Editorial rating} \
   progidx {Program index} \
   timsel {Start time} \
   dursel Duration \
   themes Themes \
   series Series \
   sortcrits {Sorting Criteria} \
   netwops Networks \
   substr {Text search} \
   vps_pdc VPS/PDC \
   invert_all {Global invert} \
]

proc CreateMenubar {} {
   global helpIndex pi_attr_labels
   global is_unix

   menu .menubar -relief ridge
   . config -menu .menubar
   .menubar add cascade -label "Control" -menu .menubar.ctrl -underline 0
   .menubar add cascade -label "Configure" -menu .menubar.config -underline 1
   #.menubar add cascade -label "Reminder" -menu .menubar.timer -underline 0
   .menubar add cascade -label "Shortcuts" -menu .menubar.shortcuts -underline 0
   .menubar add cascade -label "Filter" -menu .menubar.filter -underline 0
   if {$is_unix} {
      .menubar add cascade -label "Navigate" -menu .menubar.ni_1 -underline 0
   }
   .menubar add cascade -label "Help" -menu .menubar.help -underline 0
   # Control menu
   menu .menubar.ctrl -tearoff 0 -postcommand C_SetControlMenuStates
   .menubar.ctrl add checkbutton -label "Enable acquisition" -variable menuStatusStartAcq -command {C_ToggleAcq $menuStatusStartAcq $menuStatusDaemon}
   .menubar.ctrl add checkbutton -label "Connect to acq. daemon" -variable menuStatusDaemon -command {C_ToggleAcq $menuStatusStartAcq $menuStatusDaemon}
   .menubar.ctrl add separator
   .menubar.ctrl add checkbutton -label "Dump stream" -variable menuStatusDumpStream -command {C_ToggleDumpStream $menuStatusDumpStream}
   .menubar.ctrl add command -label "Dump raw database..." -command PopupDumpDatabase
   .menubar.ctrl add command -label "Export as text..." -command PopupDumpDbTabs
   .menubar.ctrl add command -label "Export as XMLTV..." -command PopupDumpXml
   .menubar.ctrl add command -label "Export as HTML..." -command PopupDumpHtml
   .menubar.ctrl add separator
   .menubar.ctrl add checkbutton -label "View timescales..." -command {C_TimeScale_Toggle ui} -variable menuStatusTscaleOpen(ui)
   .menubar.ctrl add checkbutton -label "View statistics..." -command {C_StatsWin_ToggleDbStats ui} -variable menuStatusStatsOpen(ui)
   .menubar.ctrl add checkbutton -label "View acq timescales..." -command {C_TimeScale_Toggle acq} -variable menuStatusTscaleOpen(acq)
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
   .menubar.config add command -label "TV app. interaction..." -command XawtvConfigPopup
   .menubar.config add command -label "Client/Server..." -command PopupNetAcqConfig
   #.menubar.config add command -label "Time zone..." -command PopupTimeZone
   .menubar.config add separator
   .menubar.config add command -label "Select columns..." -command PopupColumnSelection
   .menubar.config add command -label "User-defined columns..." -command PopupUserDefinedColumns
   .menubar.config add command -label "Select networks..." -command PopupNetwopSelection
   .menubar.config add command -label "Network names..." -command NetworkNamingPopup
   .menubar.config add command -label "Context menu..." -command ContextMenuConfigPopup
   .menubar.config add separator
   .menubar.config add cascade -label "Themes language" -menu .menubar.config.lang
   .menubar.config add cascade -label "Show/Hide" -menu .menubar.config.show_hide
   # Lanugage sub-menu
   menu .menubar.config.lang
   .menubar.config.lang add radiobutton -label "automatic" -command UpdateUserLanguage -variable menuUserLanguage -value 7
   .menubar.config.lang add separator
   .menubar.config.lang add radiobutton -label "English" -command UpdateUserLanguage -variable menuUserLanguage -value 0
   .menubar.config.lang add radiobutton -label "German" -command UpdateUserLanguage -variable menuUserLanguage -value 1
   .menubar.config.lang add radiobutton -label "French" -command UpdateUserLanguage -variable menuUserLanguage -value 4
   menu .menubar.config.show_hide
   .menubar.config.show_hide add checkbutton -label "Show shortcuts" -command {ShowOrHideShortcutList; UpdateRcFile} -variable showShortcutListbox
   .menubar.config.show_hide add checkbutton -label "Show networks (left)" -command {ShowOrHideShortcutList; UpdateRcFile} -variable showNetwopListboxLeft
   .menubar.config.show_hide add checkbutton -label "Show networks (middle)" -command {ShowOrHideShortcutList; UpdateRcFile} -variable showNetwopListbox
   .menubar.config.show_hide add checkbutton -label "Show status line" -command ToggleStatusLine -variable showStatusLine
   .menubar.config.show_hide add checkbutton -label "Show column headers" -command ToggleColumnHeader -variable showColumnHeader
   if {!$is_unix} {
      .menubar.config.show_hide add checkbutton -label "Hide on minimize" -command ToggleHideOnMinimize -variable hideOnMinimize
   }
   # Reminder menu
   #menu .menubar.timer -tearoff 0
   #.menubar.timer add command -label "Add selected title" -state disabled
   #.menubar.timer add command -label "Add selected series" -state disabled
   #.menubar.timer add command -label "Add filter selection" -state disabled
   #.menubar.timer add separator
   #.menubar.timer add command -label "List reminders..." -state disabled
   # Filter menu
   menu .menubar.filter
   .menubar.filter add cascade -menu .menubar.filter.themes -label Themes
   if {$is_unix} {
      .menubar.filter add cascade -menu .menubar.filter.series_bynet -label "Series by network..."
      .menubar.filter add cascade -menu .menubar.filter.series_alpha -label "Series alphabetically..."
   }
   .menubar.filter add cascade -menu .menubar.filter.netwops -label "Networks"
   .menubar.filter add separator
   .menubar.filter add cascade -menu .menubar.filter.features -label Features
   .menubar.filter add cascade -menu .menubar.filter.p_rating -label "Parental Rating"
   .menubar.filter add cascade -menu .menubar.filter.e_rating -label "Editorial Rating"
   .menubar.filter add cascade -menu .menubar.filter.vps_pdc -label "VPS/PDC"
   .menubar.filter add cascade -menu .menubar.filter.progidx -label "Program index"
   if {!$is_unix} {
      .menubar.filter add command -label "Series by network..." -command {PostSeparateMenu .menubar.filter.series_bynet CreateSeriesNetworksMenu {}}
      .menubar.filter add command -label "Series alphabetically..." -command {PostSeparateMenu .menubar.filter.series_alpha undef {}}
   }
   .menubar.filter add command -label "Text search..." -command SubStrPopup
   .menubar.filter add command -label "Start Time..." -command PopupTimeFilterSelection
   .menubar.filter add command -label "Duration..." -command PopupDurationFilterSelection
   .menubar.filter add command -label "Sorting Criteria..." -command PopupSortCritSelection
   if {!$is_unix} {
      .menubar.filter add command -label "Navigate" -command {PostSeparateMenu .menubar.filter.ni_1 C_CreateNi 1}
      .menubar.filter configure -postcommand {.menubar.filter entryconfigure "Navigate" -state [if [C_IsNavigateMenuEmpty] {set tmp "disabled"} else {set tmp "normal"}]}
   }
   .menubar.filter add separator
   .menubar.filter add cascade -menu .menubar.filter.invert -label "Invert"
   .menubar.filter add command -label "Reset" -command {ResetFilterState; C_ResetPiListbox}

   menu .menubar.filter.invert
   .menubar.filter.invert add checkbutton -label Global -variable filter_invert(all) -command InvertFilter
   .menubar.filter.invert add separator
   .menubar.filter.invert add cascade -menu .menubar.filter.invert.themes -label Themes
   .menubar.filter.invert add cascade -menu .menubar.filter.invert.sortcrits -label "Sorting Criteria"
   foreach filt {netwops series substr features parental editorial progidx timsel dursel vps_pdc} {
      .menubar.filter.invert add checkbutton -label $pi_attr_labels($filt) -variable filter_invert($filt) -command InvertFilter
   }

   menu .menubar.filter.e_rating
   FilterMenuAdd_EditorialRating .menubar.filter.e_rating 0

   menu .menubar.filter.p_rating
   FilterMenuAdd_ParentalRating .menubar.filter.p_rating 0

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
   FilterMenuAdd_Sound .menubar.filter.features.sound 0

   menu .menubar.filter.features.format
   FilterMenuAdd_Format .menubar.filter.features.format 0

   menu .menubar.filter.features.digital
   FilterMenuAdd_Digital .menubar.filter.features.digital 0

   menu .menubar.filter.features.encryption
   FilterMenuAdd_Encryption .menubar.filter.features.encryption 0

   menu .menubar.filter.features.live
   FilterMenuAdd_LiveRepeat .menubar.filter.features.live 0

   menu .menubar.filter.features.subtitles
   FilterMenuAdd_Subtitles .menubar.filter.features.subtitles 0

   menu .menubar.filter.features.featureclass
   menu .menubar.filter.themes

   menu .menubar.filter.series_bynet -postcommand {PostDynamicMenu .menubar.filter.series_bynet CreateSeriesNetworksMenu {}}
   menu .menubar.filter.series_alpha
   if {!$is_unix} {
      .menubar.filter.series_bynet configure -tearoff 0
      .menubar.filter.series_alpha configure -tearoff 0
   }
   foreach letter {A B C D E F G H I J K L M N O P Q R S T U V W X Y Z Other} {
      set w letter_${letter}_off_0
      .menubar.filter.series_alpha add cascade -label $letter -menu .menubar.filter.series_alpha.$w
      menu .menubar.filter.series_alpha.$w -postcommand [list PostDynamicMenu .menubar.filter.series_alpha.$w CreateSeriesLetterMenu [list $letter 0]]
   }

   menu .menubar.filter.progidx
   FilterMenuAdd_ProgIdx .menubar.filter.progidx 0

   menu .menubar.filter.vps_pdc
   FilterMenuAdd_VpsPdc .menubar.filter.vps_pdc 0

   menu .menubar.filter.netwops
   # Navigation menu
   if {$is_unix} {
      menu .menubar.ni_1 -postcommand {PostDynamicMenu .menubar.ni_1 C_CreateNi 1}
   }
   # Shortcuts menu
   menu .menubar.shortcuts -tearoff 0
   .menubar.shortcuts add command -label "Edit shortcut list..." -command EditFilterShortcuts
   .menubar.shortcuts add separator
   .menubar.shortcuts add command -label "Update filter shortcut..." -command UpdateFilterShortcut
   .menubar.shortcuts add command -label "Add filter shortcut..." -command AddFilterShortcut
   # Help menu
   menu .menubar.help -tearoff 0
   foreach {title idx} [array get helpIndex] {
      set helpTitle($idx) $title
   }
   foreach idx [lsort -integer [array names helpTitle]] {
      .menubar.help add command -label $helpTitle($idx) -command [list PopupHelp $idx]
   }
   .menubar.help add separator
   .menubar.help add command -label "About..." -command CreateAbout

   # Context Menu
   menu .contextmenu -tearoff 0 -postcommand {PostDynamicMenu .contextmenu C_CreateContextMenu {}}

   # System tray popup menu (only used if "hide on minimize" is enabled)
   menu .systray -tearoff 0 -postcommand C_SetControlMenuStates
   .systray add command -label "Show window" -command {wm deiconify .; C_SystrayIcon 0}
   .systray add separator
   .systray add checkbutton -label "Enable acquisition" -variable menuStatusStartAcq -command {C_ToggleAcq $menuStatusStartAcq $menuStatusDaemon}
   .systray add checkbutton -label "Connect to acq. daemon" -variable menuStatusDaemon -command {C_ToggleAcq $menuStatusStartAcq $menuStatusDaemon}
   .systray add separator
   .systray add command -label "Quit" -command {destroy .}
}

##  ---------------------------------------------------------------------------
##  Apply seetings loaded from rc/ini file to menu and PI listbox
##
proc ApplyRcSettingsToMenu {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global showStatusLine hideOnMinimize
   global pibox_height shortinfo_height
   global shortcuts shortcut_order

   ShowOrHideShortcutList

   if {$showStatusLine == 0} {
      pack forget .all.statusline
   }
   if $hideOnMinimize {
      ToggleHideOnMinimize
   }

   # set the height of the listbox text widget
   if {[info exists pibox_height]} {
      .all.pi.list.text configure -height $pibox_height
   }
   if {[info exists shortinfo_height]} {
      .all.pi.info.text configure -height $shortinfo_height
   }

   if {[array size shortcuts] == 0} {
      PreloadShortcuts
   }

   # fill the shortcut listbox
   foreach index $shortcut_order {
      .all.shortcuts.list insert end [lindex $shortcuts($index) $fsc_name_idx]
   }
   .all.shortcuts.list configure -height [llength $shortcut_order]
}

##  ---------------------------------------------------------------------------
##  Fill the themes filter menu with PDC theme table
##  - called once during startup and after theme language switches
##  - if the menu items already exists, only the labels are updated
##
proc FilterMenuAdd_Themes {widget is_stand_alone} {
   global current_theme_class

   # check if the menu is already created
   # if yes, only labels are updated (upon language switch)
   set is_new [expr [string length [info commands ${widget}.*]] == 0]

   # create the themes and sub-themes menues from the PDC table
   set subtheme 0
   set menidx 0
   for {set index 0} {$index < 0x80} {incr index} {
      set pdc [C_GetPdcString $index]
      if {[regexp {(.*) - } $pdc {} tlabel]} {
         # create new sub-menu and add checkbutton
         set subtheme $index
         if $is_new {
            $widget add cascade -label $tlabel -menu ${widget}.$subtheme
            menu ${widget}.$subtheme
            ${widget}.$subtheme add checkbutton -label $pdc -command "SelectTheme $subtheme" -variable theme_sel($subtheme)
            ${widget}.$subtheme add separator
         } else {
            incr menidx
            $widget entryconfigure $menidx -label $tlabel
            ${widget}.$subtheme entryconfigure 1 -label $pdc
            set submenidx 3
         }
      } elseif {([regexp {^#\d*$} $pdc] == 0) && ($subtheme > 0)} {
         if $is_new {
            ${widget}.$subtheme add checkbutton -label $pdc -command "SelectTheme $index" -variable theme_sel($index)
         } else {
            ${widget}.$subtheme entryconfigure $submenidx -label $pdc
            incr submenidx
         }
      }
   }
   # add sub-menu with one entry: all series on all networks
   if {[string length [info commands ${widget}.series]] == 0} {
      $widget add cascade -menu ${widget}.series -label [C_GetPdcString 128]
      menu ${widget}.series -tearoff 0
      ${widget}.series add checkbutton -label [C_GetPdcString 128] -command {SelectTheme 128} -variable theme_sel(128)
   } else {
      incr menidx
      $widget entryconfigure $menidx -label [C_GetPdcString 128]
      ${widget}.series entryconfigure 1 -label [C_GetPdcString 128]
   }
}

##  ---------------------------------------------------------------------------
##  Generate the themes & feature menues
##
proc GenerateFilterMenues {tcc fcc} {
   global theme_class_count feature_class_count
   global pi_attr_labels

   set theme_class_count   $tcc
   set feature_class_count $fcc

   FilterMenuAdd_Themes .menubar.filter.themes 0

   # add theme-class sub-menu
   .menubar.filter.themes add cascade -menu .menubar.filter.themes.themeclass -label "Theme class"
   menu .menubar.filter.themes.themeclass

   for {set index 1} {$index <= $theme_class_count} {incr index} {
      .menubar.filter.themes.themeclass add radio -label $index -command SelectThemeClass -variable menuStatusThemeClass -value $index
   }
   for {set index 1} {$index <= $feature_class_count} {incr index} {
      .menubar.filter.features.featureclass add radio -label $index -command {SelectFeatureClass $current_feature_class} -variable current_feature_class -value $index
   }

   AddInvertMenuForClasses .menubar.filter.invert.themes theme
   AddInvertMenuForClasses .menubar.filter.invert.sortcrits sortcrits
}

proc AddInvertMenuForClasses {widget filt} {
   global theme_class_count

   menu ${widget}
   for {set i 1} {$i <= $theme_class_count} {incr i} {
      ${widget} add checkbutton -label $i -variable filter_invert(${filt}_class${i}) -command InvertFilter
   }
}

##  ---------------------------------------------------------------------------
##  Create a popup menu below the xawtv window
##  - params #1-#4 are the coordinates and dimensions of the xawtv window
##  - params #5-#7 are the info to be displayed in the popup
##
proc Create_XawtvPopup {xcoo ycoo width height rperc rtime ptitle} {
   global xawtv_font

   set ww1 [font measure $xawtv_font $rtime]
   set ww2 [font measure $xawtv_font $ptitle]
   set ww [expr ($ww1 >= $ww2) ? $ww1 : $ww2]
   set wh [expr 2 * [font metrics $xawtv_font -linespace] + 3 + 4+4]

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

      label .xawtv_epg.f.lines -text "$rtime\n$ptitle" -font $xawtv_font
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
   .menubar.ctrl entryconfigure "Connect to acq. daemon" -state disabled
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
   # acq not possible, hence no client/server config
   .menubar.config entryconfigure "Client/Server*" -state disabled
}

##  ---------------------------------------------------------------------------
##  Map or unmap the button for TV app control
##  - on UNIX: called when a .xawtv rc file is found in the user's home
##  - on Windows: only called when a TV app is connected
##  - note that the "show" flag is not saved in the nxtvepg rc/ini file
##
set showTuneTvButton 0

proc CreateTuneTvButton {} {
   global showTuneTvButton

   if {[lsearch -exact [grid slaves .all.shortcuts] .all.shortcuts.tune] == -1} {
      grid .all.shortcuts.tune -row 1 -column 0 -columnspan 2 -sticky news
   }
   set showTuneTvButton 1
}

proc RemoveTuneTvButton {} {
   global showTuneTvButton

   if {[lsearch -exact [grid slaves .all.shortcuts] .all.shortcuts.tune] != -1} {
      grid forget .all.shortcuts.tune
   }
   set showTuneTvButton 0
}

##  ---------------------------------------------------------------------------
##  Show or hide shortcut listbox, network filter listbox and db status line
##
set showNetwopListbox 0
set showNetwopListboxLeft 0
set showShortcutListbox 1
set showStatusLine 1
set showColumnHeader 1
set hideOnMinimize 1
set menuUserLanguage 7

proc ShowOrHideShortcutList {} {
   global showShortcutListbox showNetwopListbox showNetwopListboxLeft showTuneTvButton

   if {$showNetwopListboxLeft && $showNetwopListbox} {
      set showNetwopListboxLeft 0
   }

   # first unmap everything
   set slaves [grid slaves .all.shortcuts]
   if {[llength $slaves] > 0} {
      eval [concat grid forget $slaves]
   }
   if {[lsearch -exact [pack slaves .all] .all.shortcuts] != -1} {
      pack forget .all.shortcuts
   }

   if {$showShortcutListbox || $showNetwopListboxLeft} {
      grid .all.shortcuts.clock -row 0 -column 0 -pady 5 -sticky news
      grid .all.shortcuts.reset -row 2 -column 0 -sticky news
      if $showNetwopListbox {
         grid configure .all.shortcuts.clock .all.shortcuts.reset -columnspan 2
      }
   }
   if $showTuneTvButton {
      grid .all.shortcuts.tune -row 1 -column 0 -sticky news
      if $showNetwopListbox {
         grid configure .all.shortcuts.tune -columnspan 2 
      }
   }
   if $showShortcutListbox {
      grid .all.shortcuts.list -row 3 -column 0 -sticky news
   }
   if $showNetwopListbox {
      grid .all.shortcuts.netwops -row 3 -column 1 -rowspan 5 -sticky news
   }
   if $showNetwopListboxLeft {
      grid .all.shortcuts.netwops -row 4 -column 0 -sticky news
   }

   if {$showShortcutListbox || $showNetwopListboxLeft || $showNetwopListbox} {
      pack .all.shortcuts -anchor nw -side left -before .all.pi
   }
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

   UpdatePiListboxColumns
   UpdateRcFile
}

proc ToggleHideOnMinimize {} {
   global hideOnMinimize
   global is_unix

   if {!$is_unix} {
      if $hideOnMinimize {
         bind . <Unmap> {if {([string compare %W "."] == 0) && [C_SystrayIcon 1]} {wm withdraw .}}
      } else {
         bind . <Unmap> {}
      }
   }
}

proc UpdateUserLanguage {} {
   # trigger language update in the C modules
   C_UpdateLanguage

   # redisplay the PI listbox content with the new language
   C_RefreshPiListbox

   UpdateRcFile
}

##  ---------------------------------------------------------------------------
##  Create the features filter menus
##
proc FilterMenuAdd_EditorialRating {widget is_stand_alone} {
   $widget add radio -label any -command SelectEditorialRating -variable editorial_rating -value 0
   $widget add radio -label "all rated programmes" -command SelectEditorialRating -variable editorial_rating -value 1
   $widget add radio -label "at least 2 of 7" -command SelectEditorialRating -variable editorial_rating -value 2
   $widget add radio -label "at least 3 of 7" -command SelectEditorialRating -variable editorial_rating -value 3
   $widget add radio -label "at least 4 of 7" -command SelectEditorialRating -variable editorial_rating -value 4
   $widget add radio -label "at least 5 of 7" -command SelectEditorialRating -variable editorial_rating -value 5
   $widget add radio -label "at least 6 of 7" -command SelectEditorialRating -variable editorial_rating -value 6
   $widget add radio -label "7 of 7" -command SelectEditorialRating -variable editorial_rating -value 7
   if $is_stand_alone {
      $widget add separator
      $widget add checkbutton -label Invert -variable filter_invert(editorial) -command InvertFilter
   }
}

proc FilterMenuAdd_ParentalRating {widget is_stand_alone} {
   $widget add radio -label any -command SelectParentalRating -variable parental_rating -value 0
   $widget add radio -label "ok for all ages" -command SelectParentalRating -variable parental_rating -value 1
   $widget add radio -label "ok for 4 years or elder" -command SelectParentalRating -variable parental_rating -value 2
   $widget add radio -label "ok for 6 years or elder" -command SelectParentalRating -variable parental_rating -value 3
   $widget add radio -label "ok for 8 years or elder" -command SelectParentalRating -variable parental_rating -value 4
   $widget add radio -label "ok for 10 years or elder" -command SelectParentalRating -variable parental_rating -value 5
   $widget add radio -label "ok for 12 years or elder" -command SelectParentalRating -variable parental_rating -value 6
   $widget add radio -label "ok for 14 years or elder" -command SelectParentalRating -variable parental_rating -value 7
   $widget add radio -label "ok for 16 years or elder" -command SelectParentalRating -variable parental_rating -value 8
   if $is_stand_alone {
      $widget add separator
      $widget add checkbutton -label Invert -variable filter_invert(parental) -command InvertFilter
   }
}

proc FilterMenuAdd_Sound {widget is_stand_alone} {
   $widget add radio -label any -command {SelectFeatures 0 0 0x03} -variable feature_sound -value 0
   $widget add radio -label mono -command {SelectFeatures 0x03 0x00 0} -variable feature_sound -value 1
   $widget add radio -label stereo -command {SelectFeatures 0x03 0x02 0} -variable feature_sound -value 3
   $widget add radio -label "2-channel" -command {SelectFeatures 0x03 0x01 0} -variable feature_sound -value 2
   $widget add radio -label surround -command {SelectFeatures 0x03 0x03 0} -variable feature_sound -value 4
}

proc FilterMenuAdd_Format {widget is_stand_alone} {
   $widget add radio -label any -command {SelectFeatures 0 0 0x0c} -variable feature_format -value 0
   $widget add radio -label full -command {SelectFeatures 0x0c 0x00 0x00} -variable feature_format -value 1
   $widget add radio -label widescreen -command {SelectFeatures 0x04 0x04 0x08} -variable feature_format -value 2
   $widget add radio -label "PAL+" -command {SelectFeatures 0x08 0x08 0x04} -variable feature_format -value 3
}

proc FilterMenuAdd_Digital {widget is_stand_alone} {
   $widget add radio -label any -command {SelectFeatures 0 0 0x10} -variable feature_digital -value 0
   $widget add radio -label analog -command {SelectFeatures 0x10 0x00 0} -variable feature_digital -value 1
   $widget add radio -label digital -command {SelectFeatures 0x10 0x10 0} -variable feature_digital -value 2
}

proc FilterMenuAdd_Encryption {widget is_stand_alone} {
   $widget add radio -label any -command {SelectFeatures 0 0 0x20} -variable feature_encryption -value 0
   $widget add radio -label free -command {SelectFeatures 0x20 0 0} -variable feature_encryption -value 1
   $widget add radio -label encrypted -command {SelectFeatures 0x20 0x20 0} -variable feature_encryption -value 2
}

proc FilterMenuAdd_LiveRepeat {widget is_stand_alone} {
   $widget add radio -label any -command {SelectFeatures 0 0 0xc0} -variable feature_live -value 0
   $widget add radio -label live -command {SelectFeatures 0x40 0x40 0x80} -variable feature_live -value 2
   $widget add radio -label new -command {SelectFeatures 0xc0 0 0} -variable feature_live -value 1
   $widget add radio -label repeat -command {SelectFeatures 0x80 0x80 0x40} -variable feature_live -value 3
}

proc FilterMenuAdd_Subtitles {widget is_stand_alone} {
   $widget add radio -label any -command {SelectFeatures 0 0 0x100} -variable feature_subtitles -value 0
   $widget add radio -label untitled -command {SelectFeatures 0x100 0 0} -variable feature_subtitles -value 1
   $widget add radio -label subtitle -command {SelectFeatures 0x100 0x100 0} -variable feature_subtitles -value 2
}

proc FilterMenuAdd_ProgIdx {widget is_stand_alone} {
   $widget add radio -label "any" -command {SelectProgIdx -1 -1} -variable filter_progidx -value 0
   $widget add radio -label "running now" -command {SelectProgIdx 0 0} -variable filter_progidx -value 1
   $widget add radio -label "running next" -command {SelectProgIdx 1 1} -variable filter_progidx -value 2
   $widget add radio -label "running now or next" -command {SelectProgIdx 0 1} -variable filter_progidx -value 3
   $widget add radio -label "other..." -command ProgIdxPopup -variable filter_progidx -value 4
   if $is_stand_alone {
      $widget add separator
      $widget add checkbutton -label Invert -variable filter_invert(progidx) -command InvertFilter
   }
}

proc FilterMenuAdd_VpsPdc {widget is_stand_alone} {
   $widget add radio -label "all shows" -command SelectVpsPdcFilter -variable vpspdc_filt -value 0
   $widget add radio -label "with VPS/PDC start time" -command SelectVpsPdcFilter -variable vpspdc_filt -value 1
   $widget add radio -label "with VPS/PDC differing" -command SelectVpsPdcFilter -variable vpspdc_filt -value 2
   if $is_stand_alone {
      $widget add separator
      $widget add checkbutton -label Invert -variable filter_invert(vps_pdc) -command InvertFilter
   }
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
      .all.shortcuts.netwops selection clear 0 end
      if {[info exists netselmenu]} { unset netselmenu }

      foreach netwop $netlist {
         if {[info exists order($netwop)]} {
            set netselmenu($order($netwop)) 1
            .all.shortcuts.netwops selection set [expr $order($netwop) + 1]
         }
      }
   } else {
      .all.shortcuts.netwops selection clear 1 end
      .all.shortcuts.netwops selection set 0
      if {[info exists netselmenu]} { unset netselmenu }
   }
}

##  ---------------------------------------------------------------------------
##  Fill the network filter menu and listbox with the network names
##  - called after provider change, AI update or network selection config change
##
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

   .all.shortcuts.netwops delete 1 end
   .all.shortcuts.netwops selection set 0
   .menubar.filter.netwops delete 1 end

   if {[llength $ailist] != 0} {
      set nlidx 0
      foreach cni $ailist {
         .all.shortcuts.netwops insert end $netnames($cni)
         .menubar.filter.netwops add checkbutton -label $netnames($cni) -variable netselmenu($nlidx) -command [list SelectNetwopMenu $nlidx]
         incr nlidx
      }
      .all.shortcuts.netwops configure -height [expr [llength $ailist] + 1]
      .menubar.filter.netwops add separator
      .menubar.filter.netwops add checkbutton -label Invert -variable filter_invert(netwops) -command InvertFilter
   } else {
      # no provider selected yet or empty network list
      .menubar.filter.netwops add command -label "none" -state disabled
   }
}

##  ---------------------------------------------------------------------------
##  Small helper function to convert time format from "minutes of day" to HH:MM
##
proc Motd2HHMM {motd} {
   return [format "%02d:%02d" [expr $motd / 60] [expr $motd % 60]]
}

##  ---------------------------------------------------------------------------
##  Reset the state of all filter menus
##
proc ResetThemes {} {
   global theme_class_count current_theme_class menuStatusThemeClass
   global theme_sel theme_class_sel
   global filter_invert

   if {[info exists theme_sel]} {
      unset theme_sel
   }
   for {set index 1} {$index <= $theme_class_count} {incr index} {
      set theme_class_sel($index) {}
   }
   set current_theme_class 1
   set menuStatusThemeClass $current_theme_class
   array unset filter_invert theme_class*
}

proc ResetSortCrits {} {
   global theme_class_count sortcrit_class sortcrit_class_sel
   global filter_invert

   for {set index 1} {$index <= $theme_class_count} {incr index} {
      set sortcrit_class_sel($index) {}
   }
   set sortcrit_class 1
   array unset filter_invert sortcrit_class*

   UpdateSortCritListbox
}

proc ResetFeatures {} {
   global feature_class_mask feature_class_value
   global feature_class_count current_feature_class
   global filter_invert

   for {set index 1} {$index <= $feature_class_count} {incr index} {
      set feature_class_mask($index)  0
      set feature_class_value($index) 0
   }
   set current_feature_class 1
   array unset filter_invert features

   UpdateFeatureMenuState
}

proc ResetSeries {} {
   global series_sel
   global filter_invert

   foreach index [array names series_sel] {
      unset series_sel($index)
   }
   array unset filter_invert series
}

proc ResetProgIdx {} {
   global filter_progidx
   global filter_invert

   set filter_progidx 0
   array unset filter_invert proxidx
}

proc ResetTimSel {} {
   global timsel_enabled
   global filter_invert

   set timsel_enabled 0
   array unset filter_invert timsel
}

proc ResetMinMaxDuration {} {
   global dursel_min dursel_max
   global filter_invert

   set dursel_min 0
   set dursel_max 0
   array unset filter_invert dursel
}

proc ResetParentalRating {} {
   global parental_rating editorial_rating
   global filter_invert

   set parental_rating 0
   array unset filter_invert parental
}

proc ResetEditorialRating {} {
   global parental_rating editorial_rating
   global filter_invert

   set editorial_rating 0
   array unset filter_invert editorial
}

proc ResetSubstr {} {
   global substr_pattern
   global filter_invert

   set substr_pattern {}
   array unset filter_invert substr
}

proc ResetNetwops {} {
   global netselmenu
   global filter_invert

   # reset all checkbuttons in the netwop filter menu
   if {[info exists netselmenu]} { unset netselmenu }
   array unset filter_invert netwops

   # reset the netwop filter bar
   .all.shortcuts.netwops selection clear 1 end
   .all.shortcuts.netwops selection set 0
}

proc ResetVpsPdcFilt {} {
   global vpspdc_filt
   global filter_invert

   set vpspdc_filt 0
   array unset filter_invert vps_pdc
}

##
##  Reset all filters both in GUI and on C level
##
proc ResetFilterState {} {
   global fsc_prevselection
   global filter_invert

   ResetThemes
   ResetSortCrits
   ResetFeatures
   ResetSeries
   ResetProgIdx
   ResetTimSel
   ResetMinMaxDuration
   ResetParentalRating
   ResetEditorialRating
   ResetSubstr
   ResetNetwops
   ResetVpsPdcFilt

   array unset filter_invert all

   # reset the filter shortcut bar
   .all.shortcuts.list selection clear 0 end
   set fsc_prevselection {}

   C_ResetFilter all
   C_InvertFilter {}
}

##  ---------------------------------------------------------------------------
##  Apply filter inversion setting to the PI listbox
##
proc InvertFilter {} {
   global filter_invert

   set all {}
   foreach {key val} [array get filter_invert] {
      if $val {
         lappend all $key
      }
   }

   C_InvertFilter $all

   C_RefreshPiListbox
   CheckShortcutDeselection
}

##  --------------------- F I L T E R   C A L L B A C K S ---------------------

##
##  Callback for button-release on netwop listbox
##
proc SelectNetwop {} {
   global netwop_map netselmenu

   if {[info exists netselmenu]} { unset netselmenu }

   if {[.all.shortcuts.netwops selection includes 0]} {
      .all.shortcuts.netwops selection clear 1 end
      C_SelectNetwops {}
   } else {
      if {[info exists netwop_map]} {
         set all {}
         foreach idx [.all.shortcuts.netwops curselection] {
            set netidx [expr $idx - 1]
            set netselmenu($netidx) 1
            if {[info exists netwop_map($netidx)]} {
               lappend all $netwop_map($netidx)
            }
         }
      } else {
         foreach idx [.all.shortcuts.netwops curselection] {
            set netselmenu([expr $idx - 1]) 1
         }
         set all {}
         foreach idx [.all.shortcuts.netwops curselection] {
            lappend all [expr $idx - 1]
         }
      }
      # when all netwops have been deselected, select "all" item
      if {[llength $all] == 0} {
         .all.shortcuts.netwops selection set 0
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
      .all.shortcuts.netwops selection clear 0
      .all.shortcuts.netwops selection set [expr $netidx + 1]
   } else {
      .all.shortcuts.netwops selection clear [expr $netidx + 1]
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
            .all.shortcuts.netwops selection clear 0
            .all.shortcuts.netwops selection set [expr $idx + 1]
         } else {
            # deselect the netwop
            .all.shortcuts.netwops selection clear [expr $idx + 1]
         }
         SelectNetwop
         return
      }
   }

   # not found in the netwop menus (not part of user network selection)
   # -> just set the filter & deselect the "All netwops" entry
   .all.shortcuts.netwops selection clear 0
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

##
##  Callback for time filter changes
##
proc SelectTimeFilter {} {
   global timsel_relative timsel_absstop timsel_nodate
   global timsel_start timsel_stop timsel_date
   global timsel_enabled

   if $timsel_enabled {
      C_SelectStartTime $timsel_relative $timsel_absstop $timsel_nodate \
                        $timsel_start $timsel_stop $timsel_date
   } else {
      C_SelectStartTime
   }
   C_RefreshPiListbox
   CheckShortcutDeselection
}

##
##  Callback for VPS/PDC filter radiobuttons
##
set vpspdc_filt 0
proc SelectVpsPdcFilter {} {
   global vpspdc_filt

   C_SelectVpsPdcFilter $vpspdc_filt
   C_RefreshPiListbox
   CheckShortcutDeselection
}

##
##  Callback for duration sliders and entry fields
##
proc SelectDurationFilter {} {
   global dursel_minstr dursel_maxstr
   global dursel_min dursel_max

   if {$dursel_max < $dursel_min} {
      if {$dursel_max == 0} {
         set dursel_max 1439
      } else {
         set dursel_max $dursel_min
      }
   }

   set dursel_minstr [Motd2HHMM $dursel_min]
   set dursel_maxstr [Motd2HHMM $dursel_max]

   C_SelectMinMaxDuration $dursel_min $dursel_max
   C_RefreshPiListbox
   CheckShortcutDeselection
}

##
##  Update the filter context and refresh the PI listbox
##
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
   UpdatePiMotionScrollTimer 0
}

##  ---------------------------------------------------------------------------
##  Callback for Motion event in the PI listbox while mouse button #1 is pressed
##  - this callback is only installed while the left mouse button is pressed
##  - follow the mouse with the cursor, i.e. select the line under the cursor
##
proc SelectPiMotion {state xcoo ycoo} {
   if {$state & 0x100} {
      # mouse position has changed -> get the index of the selected text line
      scan [.all.pi.list.text index "@$xcoo,$ycoo"] "%d.%d" line_idx char
      # move the cursor onto the selected line
      C_PiListBox_SelectItem [expr $line_idx - 1]

      # check if the cursor is outside the window (or below the last line of a partially filled window)
      # get coordinates of the selected line: x, y, width, height, baseline
      set tmp [.all.pi.list.text dlineinfo "@$xcoo,$ycoo"]
      set line_h  [lindex $tmp 3]
      set line_y1 [lindex $tmp 1]
      set line_y2 [expr $line_y1 + $line_h]

      if {$ycoo < $line_y1 - $line_h} {
         # mouse pointer is above the line: calculate how far away in units of line height (negative)
         set step_size [expr ($ycoo - $line_y1) / $line_h]
         if {$step_size < -5} {set step_size -5}
      } elseif {$ycoo > $line_y2 + $line_h} {
         # mouse pointer is below the line
         set step_size [expr ($ycoo - $line_y2) / $line_h]
         if {$step_size > 5} {set step_size 5}
      } else {
         # mouse inside the line boundaries (or less then a line height outside)
         # -> remove autoscroll timer
         set step_size 0
      }
      UpdatePiMotionScrollTimer $step_size
   }
}

# callback for button release -> remove the motion binding
proc SelectPiRelease {} {

   bind .all.pi.list.text <Motion> {}

   # remove autoscroll timer
   UpdatePiMotionScrollTimer 0
}

set pilist_autoscroll_step 0

# called after mouse movements: update scrolling speed or remove timer event
proc UpdatePiMotionScrollTimer {step_size} {
   global pilist_autoscroll_step pilist_autoscroll_id

   if {$pilist_autoscroll_step != $step_size} {

      set pilist_autoscroll_step $step_size

      # remove previous autoscroll timer
      if {[info exists pilist_autoscroll_id]} {
         after cancel $pilist_autoscroll_id
         unset pilist_autoscroll_id
      }

      # install timer & execute motion for the 1st time
      PiMotionScrollTimer
   }
}

# timer event: auto-scroll main window by one line
proc PiMotionScrollTimer {} {
   global pilist_autoscroll_step pilist_autoscroll_id

   # calculate scrolling speed = delay between line scrolling
   set ms [expr 600 - abs($pilist_autoscroll_step) * 100]

   if {$pilist_autoscroll_step < 0} {
      C_PiListBox_CursorUp

      set pilist_autoscroll_id [after $ms PiMotionScrollTimer]
   } elseif {$pilist_autoscroll_step > 0} {
      C_PiListBox_CursorDown

      set pilist_autoscroll_id [after $ms PiMotionScrollTimer]
   } else {
      if {[info exists pilist_autoscroll_id]} {
         unset pilist_autoscroll_id
      }
   }
}

##  ---------------------------------------------------------------------------
##  Callback for opening the context menu
##  invoked either after click with right mouse button or CTRL-C
##
proc CreateContextMenu {mode xcoo ycoo} {

   if {[string compare $mode key] == 0} {
      # context menu to be opened via keypress

      # get position of the text cursor in the PI listbox
      set curpos [.all.pi.list.text tag nextrange cur 1.0]
      if {[llength $curpos] > 0} {
         set dli [.all.pi.list.text dlineinfo [lindex $curpos 0]]
         # calculate position of menu: in the middle of the selected line
         set xcoo [expr [lindex $dli 0] + ([lindex $dli 2] / 2)]
         set ycoo [lindex $dli 1]
      } else {
         # currently no item selected -> abort
         return
      }
   }

   # calculate absolute on-screen position from relative position in the text widget
   set xcoo [expr $xcoo + [winfo rootx .all.pi.list.text]]
   set ycoo [expr $ycoo + [winfo rooty .all.pi.list.text]]

   tk_popup .contextmenu $xcoo $ycoo 0
}

##  ---------------------------------------------------------------------------
##  Open a context menu below the current listbox entry
##
proc CreateListboxContextMenu {wmen wlist coord_x coord_y} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global shortcuts shortcut_order
   global ctxmen_selidx

   set ctxmen_selidx [$wlist index @$coord_x,$coord_y]
   if {$ctxmen_selidx < [llength $shortcut_order]} {
      set sc_tag [lindex $shortcut_order $ctxmen_selidx]
      $wmen entryconfigure 0 -label "Shortcut '[lindex $shortcuts($sc_tag) $fsc_name_idx]'"

      set rooty [expr [winfo rooty $wlist] + $coord_y]
      set rootx [expr [winfo rootx $wlist] + $coord_x]

      tk_popup $wmen $rootx $rooty 0
   }
}

##  ---------------------------------------------------------------------------
##  Menu command to post a separate sub-menu
##  - Complex dynamic menus need to be separated on Windows, because the whole
##    menu tree is created every time the top-level menu is mapped. After a
##    few mappings the application gets extremely slow! (Tk or Windows bug!?)
##
proc PostSeparateMenu {wmenu func param} {
   set xcoo [expr [winfo rootx .all.pi.list.text] - 5]
   set ycoo [expr [winfo rooty .all.pi.list.text] - 5]

   if {[string length [info commands $wmenu]] == 0} {
      menu $wmenu -postcommand [list PostDynamicMenu $wmenu $func $param] -tearoff 1
   }

   tk_popup $wmenu $xcoo $ycoo 1
}

##  ---------------------------------------------------------------------------
##  Callback for double-click on a PiListBox item
##
set poppedup_pi {}

proc Create_PopupPi {ident xcoo ycoo} {
   global font_normal
   global poppedup_pi

   if {[string length $poppedup_pi] > 0} {destroy $poppedup_pi}
   set poppedup_pi $ident

   toplevel $poppedup_pi
   bind $poppedup_pi <Leave> {destroy %W; set poppedup_pi ""}
   wm overrideredirect $poppedup_pi 1
   set xcoo [expr $xcoo + [winfo rootx .all.pi.list.text] - 200]
   set ycoo [expr $ycoo + [winfo rooty .all.pi.list.text] - 5]
   wm geometry $poppedup_pi "+$xcoo+$ycoo"
   text $poppedup_pi.text -relief ridge -width 55 -height 20 -cursor circle -font $font_normal
   $poppedup_pi.text tag configure title \
                     -justify center -spacing3 12 -font [DeriveFont $font_normal 6 bold]
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
proc CreateSeriesMenu {w param_list} {
   set cni [lindex $param_list 0]
   set min_index [lindex $param_list 1]

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
               menu $child -postcommand [list PostDynamicMenu $child CreateSeriesMenu [list $cni $index]]
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
proc CreateSeriesNetworksMenu {w dummy} {
   global cfnetwops

   # fetch a list of CNIs of netwops which broadcast series programmes
   set series_nets [C_GetNetwopsWithSeries]

   if {[llength $series_nets] != 0} {

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
               menu $child -postcommand [list PostDynamicMenu $child CreateSeriesMenu [list $cni 0]]
            }
         }
      }
   }

   if {[llength $series_nets] == 0} {
      $w add command -label "none" -state disabled
   }
}

##  ---------------------------------------------------------------------------
##  Create series title menu for one starting letter
##
proc CreateSeriesLetterMenu {w param_list} {

   set letter [string tolower [lindex $param_list 0]]
   set min_index [lindex $param_list 1]

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
               menu $child -postcommand [list PostDynamicMenu $child CreateSeriesLetterMenu [list $letter $index]]
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
proc PostDynamicMenu {menu cmd param} {
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
         $cmd $menu $param
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
   global pi_font

   if {$state != 0} {
      set cheight [font metrics $pi_font -linespace]

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

# callback for button press and release on the panning button
# sets up or stops the panning
proc PanningControl {start} {
   global panning_root_y1 panning_root_y2 panning_list_height panning_info_height
   global pibox_resized
   global shortinfo_height pi_font
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
      set shortinfo_height [expr int([winfo height .all.pi.info.text] / \
                                     [font metrics $pi_font -linespace])]
      UpdateRcFile
   }
}

# callback for Configure (aka resize) event in short info text widget
proc ShortInfoResized {} {
   global pi_font shortinfo_height

   set new_height [expr int([winfo height .all.pi.info.text] / \
                            [font metrics $pi_font -linespace])]

   if {$new_height != $shortinfo_height} {
      set shortinfo_height $new_height
      UpdateRcFile
   }
}

# callback for Configure (aka resize) event in PI listbox text widget
# NOTE: only used with panedwindow
#proc PiListboxResized {} {
#   global pi_font pibox_height
#
#   set new_height [expr int(([winfo height .all.pi.list.text] - 6) / \
#                            [font metrics $pi_font -linespace])]
#
#   if {$new_height != $pibox_height} {
#      set pibox_height $new_height
#      C_ResizePiListbox
#      UpdateRcFile
#   }
#}

##  --------------------------------------------------------------------------
##  Tune the station of the currently selected programme
##  - callback command of the "Tune TV" button in the main window
##  - pops up a warning if network names have not been sync'ed with TV app yet.
##    this popup is shown just once after each program start.
##
proc TuneTV {} {
   global tvapp_name
   global tunetv_msg_nocfg
   global cfnetnames

   # warn if network names have not been sync'ed with TV app yet
   if {![array exists cfnetnames] && ![info exists tunetv_msg_nocfg]} {
      set tunetv_msg_nocfg 1

      set answer [tk_messageBox -type okcancel -default ok -icon info \
                     -message "Please synchronize the nextwork names with $tvapp_name in the Network Name Configuration dialog. You need to do this just once."]
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
      C_Tvapp_SendCmd "setstation" $name
   }
}

# callback for right-click onto the "Tune TV" button
proc TuneTvPopupMenu {state xcoo ycoo} {
   set xcoo [expr $xcoo + [winfo rootx .all.shortcuts.tune]]
   set ycoo [expr $ycoo + [winfo rooty .all.shortcuts.tune]]

   tk_popup .tunetvcfg $xcoo $ycoo 0
}

##  --------------------------------------------------------------------------
##  Callback for <Enter> event on entry fields
##  - select the entire text in the widget so that the user needs not select
##    and clear the old text, but can just start typing
##  - only useful for small fields where the old content is frequently discarded
##
proc SelectTextOnFocus {wid} {
   if {[string compare [focus -displayof $wid] $wid] != 0} {
      $wid selection range 0 end
      focus $wid
   }
}

##  --------------------------------------------------------------------------
##  Handling of help popup
##
set help_popup 0

proc PopupHelp {index {subheading {}}} {
   global help_bg help_font font_fixed
   global help_popup help_winsize helpTexts

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
      text   .help.disp.text -width 60 -wrap word -background $help_bg \
                             -font $help_font -spacing3 6 -cursor top_left_arrow \
                             -yscrollcommand {.help.disp.sb set}
      pack   .help.disp.text -side left -fill both -expand 1
      scrollbar .help.disp.sb -orient vertical -command {.help.disp.text yview}
      pack   .help.disp.sb -fill y -anchor e -side left
      pack   .help.disp -side top -fill both -expand 1
      # define tags for various nroff text formats
      .help.disp.text tag configure title1 -font [DeriveFont $help_font 4 bold] -spacing3 10
      .help.disp.text tag configure title2 -font [DeriveFont $help_font 2 bold] -spacing1 20 -spacing3 10
      .help.disp.text tag configure indent -lmargin1 30 -lmargin2 30
      .help.disp.text tag configure bold -font [DeriveFont $help_font 0 bold]
      .help.disp.text tag configure underlined -underline 1
      .help.disp.text tag configure fixed -font $font_fixed
      .help.disp.text tag configure pfixed -font $font_fixed -spacing1 0 -spacing2 0 -spacing3 0
      .help.disp.text tag configure href -underline 1 -foreground blue
      .help.disp.text tag bind href <ButtonRelease-1> {FollowHelpHyperlink}

      # allow to scroll the text with the cursor keys
      bindtags .help.disp.text {.help.disp.text . all}
      bind   .help.disp.text <Up>    {.help.disp.text yview scroll -1 unit}
      bind   .help.disp.text <Down>  {.help.disp.text yview scroll 1 unit}
      bind   .help.disp.text <Prior> {.help.disp.text yview scroll -1 pages}
      bind   .help.disp.text <Next>  {.help.disp.text yview scroll 1 pages}
      bind   .help.disp.text <Home>  {.help.disp.text yview moveto 0.0}
      bind   .help.disp.text <End>   {.help.disp.text yview moveto 1.0}
      bind   .help.disp.text <Enter> {focus %W}
      bind   .help.disp.text <Escape> {tkButtonInvoke .help.cmd.dismiss}
      # allow to scroll the text with a wheel mouse
      bind   .help.disp.text <Button-4> {.help.disp.text yview scroll -3 units}
      bind   .help.disp.text <Button-5> {.help.disp.text yview scroll 3 units}
      bind   .help.disp.text <MouseWheel> {%W yview scroll [expr {- (%D / 120) * 3}] units}
      # save width and height when the window is resized by the user
      bind   .help <Configure> {HelpWindowResized %W}

      # set user-modified window size
      if [info exists help_winsize] {
         wm geometry .help $help_winsize
      }

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
      # search for the string at the beginning of the line only (prevents matches on hyperlinks)
      append pattern {^} $subheading
      set idx_match [.help.disp.text search -regexp -- $pattern 1.0]
      if {[string length $idx_match] != 0} {
         .help.disp.text see $idx_match
         # make sure the header is at the top of the page
         if {[scan [.help.disp.text bbox $idx_match] "%d %d %d %d" x y w h] == 4} {
            .help.disp.text yview scroll [expr int($y / $h)] units
            .help.disp.text see $idx_match
         }
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

   # check if the text contains a sub-section specification
   if {[regexp {(.*): *(.*)} $hlink dummy sect subsect]} {
      set hlink $sect
   } else {
      set subsect {}
   }

   if {[info exists helpIndex($hlink)]} {
      PopupHelp $helpIndex($hlink) $subsect
   }
}

# callback for Configure (aka resize) event on the toplevel window
proc HelpWindowResized {w} {
   global help_winsize

   if {[string compare $w ".help"] == 0} {
      set new_size "[winfo width .help]x[winfo height .help]"

      if {![info exists help_winsize] || \
          ([string compare $new_size $help_winsize] != 0)} {
         set help_winsize $new_size
         UpdateRcFile
      }
   }
}

##  --------------------------------------------------------------------------
##  Handling of the About pop-up
##
set about_popup 0

proc CreateAbout {} {
   global EPG_VERSION NXTVEPG_MAILTO NXTVEPG_URL
   global tcl_patchLevel font_fixed
   global about_popup

   if {$about_popup == 0} {
      CreateTransientPopup .about "About nxtvepg"
      set about_popup 1

      label .about.name -text "nexTView EPG Decoder - nxtvepg v$EPG_VERSION"
      pack .about.name -side top -pady 8
      #label .about.tcl_version -text " Tcl/Tk version $tcl_patchLevel"
      #pack .about.tcl_version -side top

      label .about.copyr1 -text "Copyright © 1999 - 2002 by Tom Zörner"
      label .about.copyr2 -text $NXTVEPG_MAILTO
      label .about.copyr3 -text $NXTVEPG_URL -font $font_fixed -foreground blue
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

      button .about.dismiss -text "Dismiss" -command {destroy .about} -default active
      pack .about.dismiss -pady 10

      bind  .about.dismiss <Destroy> {+ set about_popup 0}
      bind  .about.dismiss <Return> {tkButtonInvoke .about.dismiss}
      bind  .about.dismiss <Escape> {tkButtonInvoke .about.dismiss}
      focus .about.dismiss

   } else {
      raise .about
   }
}

##  --------------------------------------------------------------------------
##  Browser columns selection popup
##

# this array defines the width of the columns (in pixels), the column
# heading, dropdown menu and the description in the config listbox.

array set colsel_tabs {
   title          {266 Title    FilterMenuAdd_Title      "Title"} \
   netname        {60  Network  FilterMenuAdd_Networks   "Network name"} \
   time           {78  Time     FilterMenuAdd_Time       "Running time"} \
   duration       {43  Duration FilterMenuAdd_Duration   "Duration"} \
   weekday        {35  Day      FilterMenuAdd_Date       "Day of week"} \
   day            {27  Date     FilterMenuAdd_Date       "Day of month"} \
   day_month      {44  Date     FilterMenuAdd_Date       "Day and month"} \
   day_month_year {74  Date     FilterMenuAdd_Date       "Day, month and year"} \
   pil            {76  VPS/PDC  FilterMenuAdd_VpsPdc     "VPS/PDC code"} \
   theme          {74  Theme    FilterMenuAdd_Themes     "Theme"} \
   sound          {71  Sound    FilterMenuAdd_Sound      "Sound"} \
   format         {41  Format   FilterMenuAdd_Format     "Format"} \
   ed_rating      {30  ER       FilterMenuAdd_EditorialRating "Editorial rating"} \
   par_rating     {30  PR       FilterMenuAdd_ParentalRating  "Parental rating"} \
   live_repeat    {47  L/R      FilterMenuAdd_LiveRepeat "Live or repeat"} \
   description    {15  I        none                     "Flag description"} \
   subtitles      {18  ST       FilterMenuAdd_Subtitles  "Flag subtitles"} \
}

# define presentation order for configuration listbox
set colsel_ailist_predef [list \
   title netname time duration weekday day day_month day_month_year \
   pil theme sound format ed_rating par_rating live_repeat description subtitles]

# define default column configuration - is overridden by rc/ini config
set pilistbox_cols [list \
   weekday day_month time title netname]

set colsel_popup 0

# callback for configure menu: create the configuration dialog
proc PopupColumnSelection {} {
   global colsel_ailist colsel_selist colsel_names
   global colsel_popup
   global pilistbox_cols
   global text_bg

   if {$colsel_popup == 0} {
      CreateTransientPopup .colsel "Browser Columns Selection"
      set colsel_popup 1

      FillColumnSelectionDialog .colsel.all colsel_ailist colsel_selist $pilistbox_cols 1

      message .colsel.intromsg -text "Select which types of attributes you want\nto have displayed for each TV programme:" \
                               -aspect 2500 -borderwidth 2 -relief ridge -background $text_bg -pady 5
      pack .colsel.intromsg -side top -fill x -expand 1

      frame .colsel.all
      SelBoxCreate .colsel.all colsel_ailist colsel_selist colsel_names
      .colsel.all.ai.ailist configure -width 20
      .colsel.all.sel.selist configure -width 20

      button .colsel.all.cmd.help -text "Help" -width 7 -command {PopupHelp $helpIndex(Configuration) "Select columns"}
      button .colsel.all.cmd.quit -text "Dismiss" -width 7 -command {destroy .colsel}
      button .colsel.all.cmd.apply -text "Apply" -width 7 -command ApplyColumnSelection
      pack .colsel.all.cmd.help .colsel.all.cmd.quit .colsel.all.cmd.apply -side bottom -anchor sw
      pack .colsel.all -side top

      bind .colsel <Key-F1> {PopupHelp $helpIndex(Configuration) "Select columns"}
      bind .colsel.all.cmd <Destroy> {+ set colsel_popup 0}
      bind  .colsel.all.cmd.apply <Return> {tkButtonInvoke .colsel.all.cmd.apply}
      bind  .colsel.all.cmd.apply <Escape> {tkButtonInvoke .colsel.all.cmd.quit}
      focus .colsel.all.cmd.apply
   } else {
      raise .colsel
   }
}

# callback for "Apply" button in the config dialog
proc ApplyColumnSelection {} {
   global colsel_selist pilistbox_cols

   # save the config to the rc-file
   set pilistbox_cols $colsel_selist
   UpdateRcFile

   # redraw the PI listbox with the new settings
   UpdatePiListboxColumns
   C_RefreshPiListbox
}

# notification by user-defined columns config dialog: columns addition or deletion
proc PiListboxColsel_ColUpdate {} {
   global colsel_ailist colsel_selist
   global pilistbox_cols
   global colsel_popup

   if {$colsel_popup} {
      FillColumnSelectionDialog .colsel.all colsel_ailist colsel_selist $pilistbox_cols 0
   }
}

# helper function: fill the column-selection lists
proc FillColumnSelectionDialog {wselbox var_ailist var_selist old_selist is_initial} {
   global colsel_tabs colsel_names usercols
   global pilistbox_cols colsel_ailist_predef
   global colsel_popup
   upvar $var_ailist ailist
   upvar $var_selist selist

   array unset colsel_names
   foreach name [array names colsel_tabs] {
      set colsel_names($name) [lindex $colsel_tabs($name) 3]
   }
   set ailist $colsel_ailist_predef
   foreach tag [array names usercols] {
      lappend ailist user_def_$tag
   }
   set selist {}
   foreach name $old_selist {
      if [info exists colsel_tabs($name)] {
         lappend selist $name
      }
   }

   if {!$is_initial} {
      # re-fill the listboxes
      ${wselbox}.ai.ailist delete 0 end
      ${wselbox}.sel.selist delete 0 end
      foreach item $ailist { ${wselbox}.ai.ailist insert end $colsel_names($item) }
      foreach item $selist { ${wselbox}.sel.selist insert end $colsel_names($item) }
      ${wselbox}.ai.ailist configure -height [llength $ailist]
      ${wselbox}.sel.selist configure -height [llength $ailist]
   }
}

##  ---------------------------------------------------------------------------
##  Apply the settings to the PI browser listbox
##
proc UpdatePiListboxColumns {} {
   global colsel_tabs
   global showColumnHeader pi_font
   global pilistbox_cols
   global is_unix

   # remove previous colum headers
   foreach head [info commands .all.pi.list.colheads.c*] {
      if { [regexp {.all.pi.list.colheads.col_[^.]*$} $head]  || \
           ([string compare $head ".all.pi.list.colheads.c0"] == 0)} {
         pack forget $head
         destroy $head
      }
   }

   set colhead_font   [DeriveFont $pi_font -2]
   set colhead_height [expr [font metrics $colhead_font -linespace] + 2]

   set tab_pos 0
   set tabs {}
   if {$showColumnHeader} {
      # create colum header menu buttons
      foreach col $pilistbox_cols {
         frame .all.pi.list.colheads.col_$col -width [lindex $colsel_tabs($col) 0] -height $colhead_height
         menubutton .all.pi.list.colheads.col_${col}.b -width 1 -cursor top_left_arrow \
                                                  -text [lindex $colsel_tabs($col) 1] -font $colhead_font
         if {$is_unix} {
            .all.pi.list.colheads.col_${col}.b configure -borderwidth 1 -relief raised
         } else {
            .all.pi.list.colheads.col_${col}.b configure -borderwidth 2 -relief ridge
         }
         pack .all.pi.list.colheads.col_${col}.b -fill x
         pack propagate .all.pi.list.colheads.col_${col} 0
         pack .all.pi.list.colheads.col_${col} -side left -anchor w

         if {[string compare [lindex $colsel_tabs($col) 2] "none"] != 0} {
            # create the drop-down menu below the header (the menu items are added dynamically)
            .all.pi.list.colheads.col_${col}.b configure -menu .all.pi.list.colheads.col_${col}.b.men
            menu .all.pi.list.colheads.col_${col}.b.men -postcommand [list PostDynamicMenu .all.pi.list.colheads.col_${col}.b.men [lindex $colsel_tabs($col) 2] 1]
         }

         # add bindings to allow manual resizing
         bind .all.pi.list.colheads.col_${col}.b <Motion> [concat ColumnHeaderMotion $col %s %x {;} {if {$colsel_resize_phase > 0} break}]
         bind .all.pi.list.colheads.col_${col}.b <ButtonPress-1> [concat ColumnHeaderButtonPress $col {;} {if {$colsel_resize_phase > 0} break}]
         bind .all.pi.list.colheads.col_${col}.b <ButtonRelease-1> [concat ColumnHeaderButtonRel $col]

         incr tab_pos [lindex $colsel_tabs($col) 0]
         lappend tabs ${tab_pos}
      }
      if {[info exists col] && ([string length [info commands .all.pi.list.colheads.col_${col}]] > 0)} {
         # increase width of the last header button to accomodate for text widget borderwidth
         .all.pi.list.colheads.col_${col} configure -width [expr 5 + [.all.pi.list.colheads.col_${col} cget -width]]
      }
   } else {
      # create an invisible frame to set the width of the text widget
      foreach col $pilistbox_cols {
         incr tab_pos [lindex $colsel_tabs($col) 0]
         lappend tabs ${tab_pos}
      }
      frame .all.pi.list.colheads.c0 -width "[expr $tab_pos + 2]"
      pack .all.pi.list.colheads.c0 -side left -anchor w
   }

   # configure tab-stops in text widget
   .all.pi.list.text tag configure past -tab $tabs
   .all.pi.list.text tag configure now -tab $tabs
   .all.pi.list.text tag configure then -tab $tabs

   # update the settings in the listbox module
   C_PiOutput_CfgColumns
}

##
##  Creating menus for the column heads
##
proc FilterMenuAdd_Time {widget is_stand_alone} {
   $widget add command -label "Now" -command {C_PiListBox_GotoTime 1 now}
   $widget add command -label "Next" -command {C_PiListBox_GotoTime 1 next}

   set now        [clock seconds] 
   set start_time [expr $now - ($now % (2*60*60)) + (2*60*60)]
   set hour       [expr ($start_time % (24*60*60)) / (60*60)]

   for {set i 0} {$i < 24} {incr i 2} {
      $widget add command -label [clock format $start_time -format {%H:%M}] -command [list C_PiListBox_GotoTime 1 $start_time]
      incr start_time [expr 2*60*60]
      set hour [expr ($hour + 2) % 24]
   }
}

proc FilterMenuAdd_Duration {widget is_stand_alone} {
   $widget add command -label "all durations" -command {set dursel_min 0; SelectDurationFilter}
   $widget add command -label "min. 15 minutes" -command {set dursel_min 15; SelectDurationFilter}
   $widget add command -label "min. 30 minutes" -command {set dursel_min 30; SelectDurationFilter}
   $widget add command -label "min. 1 hour" -command {set dursel_min 60; SelectDurationFilter}
   $widget add command -label "min. 1.5 hours" -command {set dursel_min 90; SelectDurationFilter}
   $widget add separator
   $widget add command -label "Select duration..." -command PopupDurationFilterSelection
}

proc FilterMenuAdd_Date {widget is_stand_alone} {
   set start_time [clock seconds]
   set last_time [C_GetLastPiTime]

   $widget add command -label "Today, [clock format $start_time -format {%d. %b. %Y}]" -command {C_PiListBox_GotoTime 1 now}
   incr start_time [expr 24*60*60]
   $widget add command -label "Tomorrow, [clock format $start_time -format {%d. %b. %Y}]" -command [list C_PiListBox_GotoTime 1 $start_time]
   incr start_time [expr 24*60*60]

   while {$start_time <= $last_time} {
      $widget add command -label [clock format $start_time -format {%A, %d. %b. %Y}] -command [list C_PiListBox_GotoTime 1 $start_time]
      incr start_time [expr 24*60*60]
   }
}

proc FilterMenuAdd_Networks {widget is_stand_alone} {
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

   # append invert checkbutton
   $widget add separator
   $widget add checkbutton -label Invert -variable filter_invert(netwops) -command InvertFilter
}

proc FilterMenuAdd_Title {widget is_stand_alone} {
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
      menu ${widget}.series -postcommand [list PostDynamicMenu ${widget}.series CreateSeriesNetworksMenu {}]
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

##  ---------------------------------------------------------------------------
##  Callbacks for PI listbox column resizing
##
set colsel_resize_phase 0

proc ColumnHeaderMotion {col state xcoo} {
   global pilistbox_cols colsel_tabs
   global colsel_resize_phase

   set wid .all.pi.list.colheads.col_$col

   set cur_width [$wid cget -width]

   if {$colsel_resize_phase == 0} {
      # cursor was not yet in proximity
      if {$xcoo + 7 >= $cur_width} {
         # check if popdown menu is currently displayed
         if {([string length [info commands .all.pi.list.colheads.col_${col}.b.men]] == 0) || \
             ([winfo ismapped .all.pi.list.colheads.col_${col}.b.men] == 0)} {
            # mouse pointer entered proximity (7 pixels) of right margin -> change cursor form
            ${wid}.b configure -cursor right_side
            # set flag for phase #1: waiting for button press
            set colsel_resize_phase 1
         }
      }
   } elseif {$colsel_resize_phase == 1} {
      # cursor was in proximity
      if {$xcoo + 7 < $cur_width} {
         # left the area (and currently not in resize mode)
         ${wid}.b configure -cursor top_left_arrow
         set colsel_resize_phase 0
      }
   } elseif {$colsel_resize_phase == 2} {
      # mouse button is pressed and in resize mode

      # restrict to minimum column width
      if {$xcoo < 15} {set xcoo 15}

      if {$xcoo != [lindex $colsel_tabs($col) 0]} {

         # configure width of the column header
         $wid configure -width $xcoo

         # configure tab-stops in text widget
         set colsel_tabs($col) [concat $xcoo [lrange $colsel_tabs($col) 1 end]]
         set tab_pos 0
         set tabs {}
         foreach col $pilistbox_cols {
            incr tab_pos [lindex $colsel_tabs($col) 0]
            lappend tabs ${tab_pos}
         }
         .all.pi.list.text tag configure past -tab $tabs
         .all.pi.list.text tag configure now -tab $tabs
         .all.pi.list.text tag configure then -tab $tabs

         # redraw the PI listbox in case strings must be shortened
         C_PiOutput_CfgColumns
         C_RefreshPiListbox
      }
   }
}


# callback for mouse button release event
proc ColumnHeaderButtonPress {col} {
   global colsel_resize_phase

   if {$colsel_resize_phase == 1} {
      # button was pressed while in proximity of the right button border
      # set flag for phase #2: the actual resize (until button is released)
      set colsel_resize_phase 2
   } else {
      # button was pressed outside of proximity -> forward events to popdown menu
      # set flag that resizing is locked until button release
      set colsel_resize_phase -1
   }
}

# callback for mouse button release event
proc ColumnHeaderButtonRel {col} {
   global colsel_resize_phase colsel_tabs

   if {$colsel_resize_phase == 2} {
      # save the configuration into the rc/ini file
      UpdateRcFile
   }

   set wid .all.pi.list.colheads.col_$col
   ${wid}.b configure -cursor top_left_arrow
   set colsel_resize_phase 0
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
## Create help text header inside text listbox
##
proc PiListBox_PrintHelpHeader {text} {
   global pi_font text_bg

   .all.pi.list.text delete 1.0 end

   if {[string length [info commands .all.pi.list.text.nxtvlogo]] > 0} {
      destroy .all.pi.list.text.nxtvlogo
   }
   button .all.pi.list.text.nxtvlogo -bitmap nxtv_logo -background $text_bg \
                                     -borderwidth 0 -highlightthickness 0
   bindtags .all.pi.list.text.nxtvlogo {all .}

   .all.pi.list.text tag configure centerTag -justify center
   .all.pi.list.text tag configure bold24Tag -font [DeriveFont $pi_font 12 bold] -spacing1 15 -spacing3 10
   .all.pi.list.text tag configure bold16Tag -font [DeriveFont $pi_font 4 bold] -spacing3 10
   .all.pi.list.text tag configure bold12Tag -font [DeriveFont $pi_font 0 bold]
   .all.pi.list.text tag configure wrapTag   -wrap word
   .all.pi.list.text tag configure yellowBg  -background #ffff50

   .all.pi.list.text insert end "Nextview EPG\n" bold24Tag
   .all.pi.list.text insert end "An Electronic TV Programme Guide for Your PC\n" bold16Tag
   .all.pi.list.text window create end -window .all.pi.list.text.nxtvlogo
   .all.pi.list.text insert end "\n\nCopyright © 1999, 2000, 2001, 2002 by Tom Zörner\n" bold12Tag
   .all.pi.list.text insert end "tomzo@nefkom.net\n\n" bold12Tag
   .all.pi.list.text tag add centerTag 1.0 {end - 1 lines}
   .all.pi.list.text insert end "This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License Version 2 as published by the Free Software Foundation. This program is distributed in the hope that it will be useful, but without any warranty. See the GPL2 for more details.\n\n" wrapTag

   .all.pi.list.text insert end $text {wrapTag yellowBg}
}

