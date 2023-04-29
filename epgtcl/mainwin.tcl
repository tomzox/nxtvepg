#
#  Nextview main window handling: PI listbox and menubar
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
#    Implements the core functionality for the main window, i.e.
#    menu bar (including handling of filter setting), shortcut and
#    network filter lists, programme list (PI list) and short-info.
#
# import constants from other modules
#=INCLUDE= "epgtcl/shortcuts.h"
#=INCLUDE= "epgtcl/dlg_udefcols.h"
#=INCLUDE= "epgtcl/dlg_remind.h"

##  ---------------------------------------------------------------------------
##  Set up global variables which define general widget parameters
##  - called once during start-up
##  - applies user specified values in the X resource file
##
proc LoadWidgetOptions {} {
   global font_normal font_fixed pi_font pi_bold_font help_font
   global text_fg text_bg default_bg win_frm_fg help_fg help_bg
   global pi_fg_now pi_bg_now pi_fg_past pi_bg_past pi_cursor_fg pi_cursor_bg
   global pi_cursor_fg_now pi_cursor_bg_now pi_cursor_fg_past pi_cursor_bg_past
   global xawtv_font xawtv_overlay_fg xawtv_overlay_bg
   global dscale_cols dscale_font dscale_width dscale_scwidth dscale_date_fmt
   global sctree_font sctree_selfg sctree_selbg
   global entry_disabledforeground entry_disabledbackground x11_appdef_path
   global tcl_version is_unix

   if {$is_unix} {
      if {$tcl_version >= 8.3} {
         set font_pt_size  [font actual TkTextFont -size]
      } else {
         set font_pt_size  12
      }
      # background for cursor in TV schedule
      set pi_cursor_fg      black
      set pi_cursor_bg      #c3c3c3
      # background for cursor when above a currently running programme
      set pi_cursor_fg_now  black
      set pi_cursor_bg_now  #b8b8df
      # background for cursor when above an expired programme
      set pi_cursor_fg_past black
      set pi_cursor_bg_past #d7d7c7
   } else {
      if {$tcl_version >= 8.3} {
         set font_pt_size  [font actual TkTextFont -size]
      } else {
         set font_pt_size  12
      }
      if {$tcl_version >= 8.3} {
         set font_pt_size  12
      } else {
         set font_pt_size  15
      }
      set pi_cursor_fg      black
      set pi_cursor_bg      #d4d4d4
      set pi_cursor_fg_now  black
      set pi_cursor_bg_now  #d8d8ff
      set pi_cursor_fg_past black
      set pi_cursor_bg_past #e0e0c9
   }
   # Tcl 8.5.6 returns a negative point size, while 8.5.0 returned it positive
   if {$font_pt_size < 0} {
      set font_pt_size [expr 0 - $font_pt_size]
   }

   # background color for all text and list in- and output fields
   set text_fg    black
   set text_bg    #e9e9ec
   # background in TV schedule for currently running programmes
   set pi_fg_now  black
   set pi_bg_now  #d9d9ef
   # background in TV schedule for expired programmes
   set pi_fg_past black
   set pi_bg_past #ede7dd
   # background for help text
   set help_fg    black
   set help_bg    #ffd840
   # background colors for PI date scale (red, orange, yellow, green, cyan, blue, purple)
   set dscale_cols {#ff3d3d #ff9d3c #fdff4f #4eff57 #07d1c1 #5b73ff #eb41ff}

   if {($tcl_version >= 8.5) && $is_unix} {
      set font_normal [list [font actual TkTextFont -family] [expr 0 - $font_pt_size] normal]
      set font_fixed  [list [font actual TkFixedFont -family] [expr 0 - $font_pt_size] normal]
   } else {
      set font_normal [list helvetica [expr 0 - $font_pt_size] normal]
      set font_fixed  [list courier   [expr 0 - $font_pt_size] normal]
   }

   # font for TV schedule and programme description text
   set pi_font      $font_normal
   set pi_bold_font [DeriveFont $font_normal 0 bold]
   # font for help text
   set help_font    $font_normal
   # font and colors for xawtv popup window
   set xawtv_font   [DeriveFont $font_normal 2 bold]
   set xawtv_overlay_fg white
   set xawtv_overlay_bg black
   # font for message popups (e.g. warnings and error messages)
   set msgbox_font  [DeriveFont $font_normal 2 bold]
   # font for PI date scale
   set dscale_font  $pi_bold_font
   set dscale_width 30
   set dscale_scwidth 20
   set dscale_date_fmt {%a}

   if $is_unix {
      # UNIX: load defaults from app-defaults file
      # these have lower priority and can be overridden by .Xdefaults
      catch {option readfile $x11_appdef_path startupFile}
   } else {
      # WIN32: load resources from local file
      catch {option readfile nxtvepg.ad userDefault}
   }
   # create temporary listbox widget to query parameters for the shortcut tree widget
   listbox .tmp_lb
   set sctree_font [.tmp_lb cget -font]
   set select_fg [.tmp_lb cget -selectforeground]
   set select_bg [.tmp_lb cget -selectbackground]
   destroy .tmp_lb

   # create temporary widget to check syntax of configuration options
   label .test_opt
   # load and check all color resources
   foreach opt {pi_cursor_fg pi_cursor_bg pi_cursor_fg_now pi_cursor_bg_now
                pi_cursor_fg_past pi_cursor_bg_past text_fg text_bg help_fg \
                pi_fg_now pi_bg_now pi_fg_past pi_bg_past select_fg select_bg \
                help_fg help_bg xawtv_overlay_fg xawtv_overlay_bg} {
      set value [option get . $opt userDefault]
      if {([string length $value] > 0) && \
          ([catch {.test_opt configure -foreground $value}] == 0)} {
         set $opt $value
      }
   }
   # load 7 colors for date scale
   set value [option get . dscale_cols userDefault]
   if {[string length $value] > 0} {
      set ltmp [split $value { ,;}]
      if {[llength $ltmp] == 7} {
         set idx 0
         foreach value $ltmp {
            if {[catch {.test_opt configure -foreground $value}] == 0} {
               incr idx
            }
         }
         if {$idx == 7} {
            set dscale_cols $ltmp
         }
      }
   }
   destroy .test_opt

   # load and check all font resources (only font syntax is checked, font may not exist)
   foreach opt {pi_font xawtv_font help_font msgbox_font dscale_font} {
      set value [option get . $opt userDefault]
      if {[string length $value] > 0} {
         if {([llength $value] >= 2) &&
             ([catch {DeriveFont $value 0}] == 0) &&
             ([catch {font metrics $value -linespace}] == 0)} {
            set $opt $value
         }
      }
   }

   # date format and width of weekday scale
   set value [option get . dscale_date_fmt userDefault]
   if {([string length $value] > 0) && \
       ([string length [C_ClockFormat 0 $value]] > 0)} {
      regsub -all {\\n} $value "\n" dscale_date_fmt
   }
   set value [option get . dscale_width userDefault]
   if {([string length $value] > 0) && [regexp {^\d+$} $value] && ($value > 10)} {
      set dscale_width $value
   }
   set value [option get . dscale_scwidth userDefault]
   if {([string length $value] > 0) && [regexp {^\d+$} $value] && \
       ($value >= 1) && ($value < $dscale_width)} {
      set dscale_scwidth $value
   }

   # starting with Tk8.4 an entry's text is grey when disabled and
   # this new option must be used where this is not desirable
   if {$tcl_version >= 8.4} {
      set entry_disabledforeground "-disabledforeground"
      set entry_disabledbackground "-disabledbackground"
   } else {
      set entry_disabledforeground "-foreground"
      set entry_disabledbackground "-background"
   }

   if {[llength [info commands tkButtonInvoke]] == 0} {
      proc tkButtonInvoke {w} {tk::ButtonInvoke $w}
   }

   set default_bg [. cget -background]
   set win_frm_fg [. cget -highlightcolor]
   set sctree_selfg $select_fg
   set sctree_selbg $select_bg

   # map "artifical" resources onto internal Tk resources
   option add *Dialog.msg.font $msgbox_font userDefault
   option add *Listbox.foreground $text_fg userDefault
   option add *Listbox.background $text_bg userDefault
   option add *Entry.foreground $text_fg userDefault
   option add *Entry.background $text_bg userDefault
   option add *Text.foreground $text_fg userDefault
   option add *Text.background $text_bg userDefault
   option add *Listbox.selectForeground $select_fg userDefault
   option add *Listbox.selectBackground $select_bg userDefault
   option add *Entry.selectForeground $select_fg userDefault
   option add *Entry.selectBackground $select_bg userDefault
   option add *Text.selectForeground $select_fg userDefault
   option add *Text.selectBackground $select_bg userDefault
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

# helper functions to modify default widget appearences
if {$tcl_version >= 8.5} {
   # no longer needed in 8.5 and later
   proc relief_ridge_v84 {wid} {}
   proc relief_listbox {wid} {
      $wid configure -relief sunken -borderwidth 1
   }
   # make menubuttons look like a normal button widget
   proc config_menubutton {wid} {
      global win_frm_fg
      $wid configure -relief raised -borderwidth 1 \
                     -highlightthickness 1 -highlightcolor $win_frm_fg \
                     -takefocus 1
   }
} else {
   # on Tcl 8.4 and earlier replace kludgy 2-pixel button frames (main window only)
   proc relief_ridge_v84 {wid} {
      $wid configure -relief ridge -borderwidth 2
   }
   proc relief_listbox {wid} {
      $wid configure -relief ridge -borderwidth 2
   }
   # make menubuttons look like a normal button widget
   proc config_menubutton {wid} {
      global win_frm_fg
      $wid configure -relief raised -borderwidth 2 \
                     -highlightthickness 1 -highlightcolor $win_frm_fg \
                     -takefocus 1
   }
}

##  ---------------------------------------------------------------------------
##  Create the main window
##  - called once during start-up
##
set pibox_type 0
set pinetbox_col_count 4
set pinetbox_col_width 125

#=CONST=  ::pimg_name_idx   0
#=CONST=  ::pimg_width_idx  1
#=CONST=  ::pimg_idx_count  2

proc CreateMainWindow {} {
   global is_unix entry_disabledforeground
   global text_fg text_bg pi_bg_now pi_bg_past default_bg
   global font_normal pi_font pi_bold_font
   global sctree_font sctree_selfg sctree_selbg
   global fileImage pi_img
   global pibox_type pinetbox_col_count pinetbox_col_width
   global dscale_width

   # copy event bindings which are required for scrolling and selection (outbound copy&paste)
   foreach event {<ButtonPress-1> <ButtonRelease-1> <B1-Motion> <Double-Button-1> <Shift-Button-1> \
                  <Triple-Button-1> <Triple-Shift-Button-1> <Button-2> <B2-Motion> <MouseWheel> \
                  <<Copy>> <<Clear>> <Shift-Key-Tab> <Control-Key-Tab> <Control-Shift-Key-Tab> \
                  <Key-Prior> <Key-Next> <Key-Down> <Key-Up> <Key-Left> <Key-Right> \
                  <Shift-Key-Left> <Shift-Key-Right> <Shift-Key-Up> <Shift-Key-Down> \
                  <Key-Home> <Key-End> <Shift-Key-Home> <Shift-Key-End> <Control-Key-slash>} {
      bind TextReadOnly $event [bind Text $event]
   }
   # allow to scroll the text with a wheel mouse
   bind TextReadOnly <Button-4>     {%W yview scroll -3 units}
   bind TextReadOnly <Button-5>     {%W yview scroll 3 units}
   bind TextReadOnly <MouseWheel>   {%W yview scroll [expr {- (%D / 120) * 3}] units}
   bind TextReadOnly <Key-Tab> [bind Text <Control-Key-Tab>]
   bind TextReadOnly <Control-Key-c> [bind Text <<Copy>>]

   frame     .all
   frame     .all.shortcuts -borderwidth 2
   label     .all.shortcuts.clock -padx 7 -text {}
   #grid     .all.shortcuts.clock -row 0 -column 0 -pady 5 -sticky nwe
   if {!$is_unix} {
      .all.shortcuts.clock configure -font [DeriveFont $font_normal 0 bold]
   }

   button    .all.shortcuts.tune -text "Tune TV" -command TuneTV_ButtonPress -state disabled -takefocus 0
   relief_ridge_v84 .all.shortcuts.tune
   bind      .all.shortcuts.tune <Button-3> {TuneTvPopupMenu 1 %x %y}
   #grid     .all.shortcuts.tune -row 1 -column 0 -sticky nwe

   button    .all.shortcuts.reset -text "Reset" -command {ResetFilterState; C_PiBox_Reset}
   relief_ridge_v84 .all.shortcuts.reset
   #grid     .all.shortcuts.reset -row 2 -column 0 -sticky nwe

   menu      .tunetvcfg -tearoff 0
   .tunetvcfg add command -label "Show EPG info" -command {C_Tvapp_ShowEpg}
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
   .ctx_shortcuts add command -label "Update" -command {UpdateFilterShortcutByContext $ctxmen_sctag}
   .ctx_shortcuts add command -label "Delete" -command {DeleteFilterShortcut $ctxmen_sctag}
   .ctx_shortcuts add separator
   .ctx_shortcuts add command -label "Edit..." -command {EditFilterShortcuts $ctxmen_sctag}
   .ctx_shortcuts add command -label "Add new..." -command {AddFilterShortcut $ctxmen_sctag}

   Tree:create .all.shortcuts.list -height 2 -width 0 -cursor top_left_arrow \
                                   -font $sctree_font -selectbackground $sctree_selbg \
                                   -selectmode extended -selectforeground $sctree_selfg \
                                   -foreground $text_fg -background $text_bg
   relief_listbox .all.shortcuts.list

   bind      .all.shortcuts.list <<TreeSelect>> {ShortcutTree_Invoke}
   bind      .all.shortcuts.list <<TreeOpenClose>> {ShortcutTree_OpenCloseEvent .all.shortcuts.list shortcuts $shortcut_tree}
   bind      .all.shortcuts.list <Key-Escape> {Tree:selection .all.shortcuts.list clear first end; ShortcutTree_Invoke}
   #bind      .all.shortcuts.list <Key-Return> {ShortcutTree_Invoke}
   #bind      .all.shortcuts.list <FocusOut> {ShortcutTree_Invoke}
   bind      .all.shortcuts.list <Button-3> {CreateShortcutContextMenu %x %y}
   #pack     .all.shortcuts.list -side left -fill y
   #grid     .all.shortcuts.list -row 3 -column 0 -sticky nwe

   frame     .all.netwops
   listbox   .all.shortcuts.netwops -exportselection false -height 2 -width 0 \
                                    -selectmode extended -cursor top_left_arrow
   relief_ridge_v84 .all.shortcuts.netwops
   .all.shortcuts.netwops insert end "-all-"
   .all.shortcuts.netwops selection set 0
   bind      .all.shortcuts.netwops <<ListboxSelect>> {SelectNetwop}
   bind      .all.shortcuts.netwops <Key-Return> {SelectNetwop}
   bind      .all.shortcuts.netwops <Key-Escape> {.all.shortcuts.netwops selection clear 0 end; SelectNetwop}
   bind      .all.shortcuts.netwops <FocusOut> {SelectNetwop}
   bind      .all.shortcuts.netwops <Button-3> {tk_popup .ctx_netsel %X %Y 0}
   #grid     .all.shortcuts.netwops -row 4 -column 0 -sticky nwe
   #pack     .all.shortcuts -anchor nw -side left
   checkbutton .all.shortcuts.pitype -text "Grid\nLayout" -justify left -command Toggle_PiBoxType -variable pibox_type
   #grid     .all.shortcuts.pitype -row 5 -column 0 -sticky nwe

   frame     .all.pi
   #panedwindow .all.pi -orient vertical
   frame     .all.pi.list
   button    .all.pi.list.colcfg -command PopupColumnSelection -bitmap "bitmap_colsel" \
                                 -cursor top_left_arrow -borderwidth 1 -padx 0 -pady 0 -takefocus 0
   grid      .all.pi.list.colcfg -row 0 -column 0 -sticky news
   button    .all.pi.list.col_pl -command {NetboxColumnRecount 1} -bitmap "bitmap_col_plus" \
                                 -cursor top_left_arrow -borderwidth 1 -padx 0 -pady 0 -takefocus 0
   button    .all.pi.list.col_mi -command {NetboxColumnRecount -1} -bitmap "bitmap_col_minus" \
                                 -cursor top_left_arrow -borderwidth 1 -padx 0 -pady 0 -takefocus 0
   frame     .all.pi.list.colheads
   grid      .all.pi.list.colheads -row 0 -column 1 -columnspan 2 -sticky news

   scrollbar .all.pi.list.sc -orient vertical -command {C_PiBox_Scroll} -takefocus 0
   grid      .all.pi.list.sc -row 3 -column 0 -sticky ns
   bind      .all.pi.list.sc <Button-4>        {C_PiBox_Scroll scroll -1 pages; break}
   bind      .all.pi.list.sc <Button-5>        {C_PiBox_Scroll scroll 1 pages; break}
   bind      .all.pi.list.sc <MouseWheel>      {C_PiBox_Scroll scroll [expr int(%D / %D)] pages; break}
   text      .all.pi.list.text -width 50 -height 25 -wrap none \
                               -font $pi_font -exportselection false \
                               -cursor top_left_arrow \
                               -insertofftime 0
   AssignPiTextTags .all.pi.list.text
   #bind     TextPiBox <Configure>       PiListboxResized
   bind      TextPiBox <ButtonPress-1>   {SelectPi %W %x %y}
   bind      TextPiBox <ButtonRelease-1> {SelectPiRelease %W}
   bind      TextPiBox <ButtonPress-2>   {DragListboxStart %W %X %Y}
   bind      TextPiBox <ButtonRelease-2> {DragListboxStop %W}
   bind      TextPiBox <Double-Button-2> {Create_PopupPi %W %x %y}
   bind      TextPiBox <Button-3>        {SelectPi %W %x %y; CreateContextMenu mouse %W %x %y}
   bind      TextPiBox <Button-4>        {C_PiBox_Scroll scroll -3 units}
   bind      TextPiBox <Button-5>        {C_PiBox_Scroll scroll 3 units}
   bind      TextPiBox <MouseWheel>      {C_PiBox_Scroll scroll [expr int((%D + 20) / -40)] units}
   bind      TextPiBox <Key-Up>          {C_PiBox_CursorUp}
   bind      TextPiBox <Key-Down>        {C_PiBox_CursorDown}
   bind      TextPiBox <Key-Left>        {C_PiBox_CursorLeft}
   bind      TextPiBox <Key-Right>       {C_PiBox_CursorRight}
   bind      TextPiBox <Control-Up>      {C_PiBox_Scroll scroll -1 units}
   bind      TextPiBox <Control-Down>    {C_PiBox_Scroll scroll 1 units}
   bind      TextPiBox <Control-Left>    {C_PiBox_ScrollHorizontal scroll -1 units}
   bind      TextPiBox <Control-Right>   {C_PiBox_ScrollHorizontal scroll 1 units}
   bind      TextPiBox <Key-Prior>       {C_PiBox_Scroll scroll -1 pages}
   bind      TextPiBox <Key-Next>        {C_PiBox_Scroll scroll 1 pages}
   bind      TextPiBox <Key-Home>        {C_PiBox_Scroll moveto 0.0; C_PiBox_SelectItem 0 -1}
   bind      TextPiBox <Key-End>         {C_PiBox_Scroll moveto 1.0; C_PiBox_Scroll scroll 1 pages}
   bind      TextPiBox <Shift-Prior>     {C_PiBox_ScrollHorizontal scroll -$pinetbox_col_count units}
   bind      TextPiBox <Shift-Next>      {C_PiBox_ScrollHorizontal scroll $pinetbox_col_count units}
   bind      TextPiBox <Shift-Home>      {C_PiBox_ScrollHorizontal moveto 0.0}
   bind      TextPiBox <Shift-End>       {C_PiBox_ScrollHorizontal moveto 1.0}
   bind      TextPiBox <Key-1>           {ToggleShortcut 0}
   bind      TextPiBox <Key-2>           {ToggleShortcut 1}
   bind      TextPiBox <Key-3>           {ToggleShortcut 2}
   bind      TextPiBox <Key-4>           {ToggleShortcut 3}
   bind      TextPiBox <Key-5>           {ToggleShortcut 4}
   bind      TextPiBox <Key-6>           {ToggleShortcut 5}
   bind      TextPiBox <Key-7>           {ToggleShortcut 6}
   bind      TextPiBox <Key-8>           {ToggleShortcut 7}
   bind      TextPiBox <Key-9>           {ToggleShortcut 8}
   bind      TextPiBox <Key-0>           {ToggleShortcut 9}
   bind      TextPiBox <Key-i>           {C_Tvapp_ShowEpg}
   bind      TextPiBox <Return>          {TuneTV_KeyPress}
   bind      TextPiBox <Double-Button-1> {TuneTV_KeyPress}
   bind      TextPiBox <Escape>          {tkButtonInvoke .all.shortcuts.reset}
   bind      TextPiBox <Control-Key-f>   {SubStrPopup}
   bind      TextPiBox <Control-Key-c>   {CreateContextMenu key .all.pi.list.text 0 0}
   bind      TextPiBox <Enter>           {focus %W}
   bindtags  .all.pi.list.text {.all.pi.list.text TextPiBox . all}

   if {$pibox_type == 0} {
      grid   .all.pi.list.text -row 1 -rowspan 3 -column 1 -columnspan 2 -sticky news
   }
   canvas    .all.pi.list.dscale -width $dscale_width -background $default_bg -cursor top_left_arrow
   bind      .all.pi.list.dscale <ButtonPress-1>   {PiDateScale_Goto %y 0x100}
   bind      .all.pi.list.dscale <ButtonRelease-1> {PiDateScale_Goto %y 0}
   bind      .all.pi.list.dscale <Button-4>        {C_PiBox_Scroll scroll -1 pages; break}
   bind      .all.pi.list.dscale <Button-5>        {C_PiBox_Scroll scroll 1 pages; break}
   bind      .all.pi.list.dscale <MouseWheel>      {C_PiBox_Scroll scroll [expr int(%D / %D)] pages; break}
   grid      .all.pi.list.dscale -row 1 -rowspan 3 -column 3 -sticky n -pady 2


   ###########################################################################

   frame     .all.pi.list.nets
   for {set idx 0} {$idx < $pinetbox_col_count} {incr idx} {
      CreateListboxNetCol $idx
   }
   scrollbar .all.pi.list.hsc -orient horizontal -command {C_PiBox_ScrollHorizontal} -takefocus 0
   if {$pibox_type == 1} {
      grid   .all.pi.list.col_pl -row 1 -column 0 -sticky news
      grid   .all.pi.list.col_mi -row 2 -column 0 -sticky news
      grid   .all.pi.list.nets -row 0 -rowspan 4 -column 1 -columnspan 2 -sticky news
      grid   .all.pi.list.hsc -row 4 -column 1 -sticky ew
   }
   ###########################################################################

   button    .all.pi.list.panner -bitmap bitmap_pan_updown -cursor top_left_arrow -takefocus 0
   bind      .all.pi.list.panner <ButtonPress-1> {+ PanningControl 1}
   bind      .all.pi.list.panner <ButtonRelease-1> {+ PanningControl 0}
   grid      .all.pi.list.panner -row 4 -column 2 -columnspan 2 -sticky ens

   grid      columnconfigure .all.pi.list 1 -weight 1
   grid      rowconfigure .all.pi.list 3 -weight 1
   pack      .all.pi.list -side top -fill x
   #.all.pi   add .all.pi.list -sticky news -minsize 40 -height 372

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
   bind      .all.pi.info.text <Button-4> {.all.pi.info.text yview scroll -3 units}
   bind      .all.pi.info.text <Button-5> {.all.pi.info.text yview scroll 3 units}
   pack      .all.pi.info.text -side left -fill both -expand 1
   pack      .all.pi.info -side top -fill both -expand 1
   #.all.pi   add .all.pi.info -sticky news -minsize 25 -height 150
   pack      .all.pi -side top -fill both -expand 1

   entry     .all.statusline -state disabled -relief flat -borderwidth 1 \
                             -font [DeriveFont $font_normal -2] -background $default_bg $entry_disabledforeground black \
                             -textvariable dbstatus_line
   pack      .all.statusline -side bottom -fill x
   pack      .all -side left -fill both -expand 1

   bind      . <Key-F1> {PopupHelp $helpIndex(Basic browsing) {}}
   if {$pibox_type == 0} {
      focus  .all.pi.list.text
   } else {
      focus  .all.pi.list.nets.n_0
   }


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

##  ---------------------------------------------------------------------------
##  Define text tags for PI listbox & netbox output
##
proc AssignPiTextTags {wid} {
   global pi_fg_now pi_bg_now pi_fg_past pi_bg_past
   global pi_cursor_fg pi_cursor_bg
   global pi_bold_font dscale_cols

   # tags to mark currently running programmes
   # note: background color of "cur" is changed dynamically at time of display
   $wid tag configure cur -relief raised -borderwidth 1
   $wid tag configure now -foreground $pi_fg_now -background $pi_bg_now
   $wid tag configure past -foreground $pi_fg_past -background $pi_bg_past
   $wid tag configure cur_pseudo -relief ridge -foreground $pi_cursor_fg \
                                 -background $pi_cursor_bg \
                                 -borderwidth 2 -lmargin1 2 -rmargin 2
   $wid tag configure cur_pseudo_overlay -relief ridge -foreground $pi_cursor_fg \
                                 -background $pi_cursor_bg \
                                 -borderwidth 2 -bgstipple bitmap_hatch

   $wid tag lower now
   $wid tag lower past

   # background color tags for weekday colors column type
   for {set wday_idx 0} {$wday_idx < 7} {incr wday_idx} {
      $wid tag configure ag_day$wday_idx -bgstipple bitmap_gray \
                                         -background [lindex $dscale_cols $wday_idx]
   }

   # text font tags used in user-defined columns
   $wid tag configure bold -font $pi_bold_font
   $wid tag configure underline -underline 1
   $wid tag configure overstrike -overstrike 1

   # foreground color tags used in user-defined columns
   UserCols_SetPiBoxTextColors $wid
}

##  ---------------------------------------------------------------------------
##  Update listbox color tags after changes in user-defined column definitions
##
proc UpdateListboxColorTags {} {
   global pinetbox_col_count

   UserCols_SetPiBoxTextColors .all.pi.list.text

   for {set idx 0} {$idx < $pinetbox_col_count} {incr idx} {
      if {[llength [info commands .all.pi.list.nets.n_$idx]] != 0} {
         UserCols_SetPiBoxTextColors .all.pi.list.nets.n_$idx
      }
   }
}

##  ---------------------------------------------------------------------------
##  Create text widget for PI netbox column
##
proc CreateListboxNetCol {idx} {
   global pi_font pi_bg_now
   global pibox_height pinetbox_col_width
   global is_unix

   set colhead_font   [DeriveFont $pi_font -2]
   set colhead_height [expr [font metrics $colhead_font -linespace] + 2]

   frame     .all.pi.list.nets.h_$idx -width $pinetbox_col_width -height $colhead_height
   menubutton .all.pi.list.nets.h_${idx}.b -width 1 -cursor top_left_arrow -text {} -font $colhead_font \
                                           -menu .all.pi.list.nets.h_${idx}.b.men
   menu      .all.pi.list.nets.h_${idx}.b.men -postcommand [list PostDynamicMenu .all.pi.list.nets.h_${idx}.b.men NetboxColumnHeaderMenu 1]
   if {$is_unix} {
      .all.pi.list.nets.h_${idx}.b configure -borderwidth 1 -relief raised
   } else {
      .all.pi.list.nets.h_${idx}.b configure -borderwidth 2 -relief ridge
   }
   pack      .all.pi.list.nets.h_${idx}.b -fill x
   pack      propagate .all.pi.list.nets.h_${idx} 0
   # add bindings to allow manual resizing
   bind      .all.pi.list.nets.h_${idx}.b <Motion> [concat ColumnHeaderMotion $idx %s %x {;} {if {$colsel_resize_phase > 0} break}]
   bind      .all.pi.list.nets.h_${idx}.b <ButtonPress-1> [concat ColumnHeaderButtonPress $idx %s %x {;} {if {$colsel_resize_phase > 0} break}]
   bind      .all.pi.list.nets.h_${idx}.b <ButtonRelease-1> [concat ColumnHeaderButtonRel $idx]
   bind      .all.pi.list.nets.h_${idx}.b <Leave> [concat ColumnHeaderLeave $idx]

   text      .all.pi.list.nets.n_$idx -width 1 -height $pibox_height -wrap none \
                                      -font $pi_font -exportselection false \
                                      -cursor top_left_arrow -insertofftime 0 \
                                      -highlightthickness 0 -takefocus [expr $idx == 0]
   AssignPiTextTags .all.pi.list.nets.n_$idx
   bind      .all.pi.list.nets.n_$idx <Button-1> [list SelectPi %W %x %y $idx]
   bindtags  .all.pi.list.nets.n_$idx [list .all.pi.list.nets.n_$idx TextPiBox . all]

   grid      .all.pi.list.nets.h_$idx -row 0 -column $idx -sticky news
   grid      .all.pi.list.nets.n_$idx -row 1 -column $idx -sticky news
   grid      columnconfigure .all.pi.list.nets $idx -weight 1
}

##  ---------------------------------------------------------------------------
##  Initialize menu state variables
##
set menuStatusStartAcq 0
set menuStatusDaemon 0
set menuStatusDumpStream 0
set menuStatusThemeClass 1
set menuStatusTscaleOpen 0
set menuStatusDbStatsOpen 0
set menuStatusAcqStatsOpen 0

array set pi_attr_labels [list \
   features Features \
   parental {Parental rating} \
   editorial {Editorial rating} \
   progidx {Program index} \
   timsel {Start time} \
   dursel Duration \
   themes Themes \
   netwops Networks \
   substr {Text search} \
   vps_pdc VPS/PDC \
   piexpire {Expire time} \
   invert_all {Global invert} \
]

##  ---------------------------------------------------------------------------
##  Create and populate the main window's menu bar (including sub-menus)
##  - called once during start-up
##
proc CreateMenubar {} {
   global helpIndex pi_attr_labels
   global is_unix

   menu .menubar
   relief_ridge_v84 .menubar
   . config -menu .menubar
   .menubar add cascade -label "Control" -menu .menubar.ctrl -underline 0
   .menubar add cascade -label "Configure" -menu .menubar.config -underline 1
   .menubar add cascade -label "Reminder" -menu .menubar.reminder -underline 0
   .menubar add cascade -label "Shortcuts" -menu .menubar.shortcuts -underline 0
   .menubar add cascade -label "Filter" -menu .menubar.filter -underline 0
   .menubar add cascade -label "Help" -menu .menubar.help -underline 0
   # Control menu
   menu .menubar.ctrl -tearoff 0 -postcommand C_SetControlMenuStates
   .menubar.ctrl add command -label "Load XMLTV file..." -command ProvWin_Create
   .menubar.ctrl add command -label "Merge XMLTV files..." -command PopupProviderMerge
#=IF=defined(USE_TTX_GRABBER)
   .menubar.ctrl add command -label "Load Teletext EPG" -command ProvLoadTeletext
#=ENDIF=
   .menubar.ctrl add separator
   .menubar.ctrl add checkbutton -label "Enable acquisition" -variable menuStatusStartAcq -command {C_ToggleAcq $menuStatusStartAcq $menuStatusDaemon}
   .menubar.ctrl add checkbutton -label "Connect to acq. daemon" -variable menuStatusDaemon -command {C_ToggleAcq $menuStatusStartAcq $menuStatusDaemon}
   .menubar.ctrl add separator
   .menubar.ctrl add checkbutton -label "View coverage timescales..." -command {C_TimeScale_Toggle} -variable menuStatusTscaleOpen
   .menubar.ctrl add checkbutton -label "View database statistics..." -command {C_StatsWin_ToggleDbStats} -variable menuStatusDbStatsOpen
#=IF=defined(USE_TTX_GRABBER)
   .menubar.ctrl add checkbutton -label "Teletext grabber statistics..." -command {C_StatsWin_ToggleTtxStats} -variable menuStatusAcqStatsOpen
#=ENDIF=
   .menubar.ctrl add separator
   .menubar.ctrl add command -label "Export as text..." -command PopupDumpDbTabs
   .menubar.ctrl add command -label "Export as XMLTV..." -command PopupDumpXml
   .menubar.ctrl add command -label "Export as HTML..." -command PopupDumpHtml
   .menubar.ctrl add separator
   .menubar.ctrl add command -label "Quit" -command {destroy .; update}
   # Config menu
   menu .menubar.config -tearoff 0
#=IF=defined(USE_TTX_GRABBER)
   .menubar.config add command -label "Teletext grabber..." -command PopupTtxGrab
#=ENDIF=
   .menubar.config add command -label "TV card input..." -command PopupHardwareConfig
   .menubar.config add command -label "TV app. interaction..." -command XawtvConfigPopup
   .menubar.config add command -label "Client/Server..." -command PopupNetAcqConfig
   .menubar.config add command -label "TV channel scan..." -command PopupEpgScan
   .menubar.config add separator
   .menubar.config add command -label "Select attributes..." -command PopupColumnSelection
   .menubar.config add command -label "Attribute composition..." -command PopupUserDefinedColumns
   .menubar.config add command -label "Select networks..." -command PopupNetwopSelection
   .menubar.config add command -label "Network names..." -command NetworkNamingPopup
   .menubar.config add command -label "Context menu..." -command ContextMenuConfigPopup
   .menubar.config add separator
   .menubar.config add cascade -label "Themes language" -menu .menubar.config.lang
   .menubar.config add cascade -label "Show/Hide" -menu .menubar.config.show_hide
   .menubar.config add cascade -label "List layout" -menu .menubar.config.layout
   # Lanugage sub-menu
   menu .menubar.config.lang
   .menubar.config.lang add radiobutton -label "automatic" -command UpdateUserLanguage -variable menuUserLanguage -value 7
   .menubar.config.lang add separator
   .menubar.config.lang add radiobutton -label "English" -command UpdateUserLanguage -variable menuUserLanguage -value 0
   .menubar.config.lang add radiobutton -label "German" -command UpdateUserLanguage -variable menuUserLanguage -value 1
   .menubar.config.lang add radiobutton -label "French" -command UpdateUserLanguage -variable menuUserLanguage -value 4
   menu .menubar.config.show_hide
   .menubar.config.show_hide add checkbutton -label "Show shortcuts" -command {ShowOrHideShortcutList showShortcutListbox; UpdateRcFile} -variable showShortcutListbox
   .menubar.config.show_hide add checkbutton -label "Show networks (left)" -command {ShowOrHideShortcutList showNetwopListboxLeft; UpdateRcFile} -variable showNetwopListboxLeft
   .menubar.config.show_hide add checkbutton -label "Show networks (middle)" -command {ShowOrHideShortcutList showNetwopListbox; UpdateRcFile} -variable showNetwopListbox
   .menubar.config.show_hide add checkbutton -label "Show \"Grid layout\" button" -command {ShowOrHideShortcutList showLayoutButton; UpdateRcFile} -variable showLayoutButton
   .menubar.config.show_hide add checkbutton -label "Show \"Tune TV\" button" -command {ShowOrHideShortcutList showTuneTvButton; UpdateRcFile} -variable showTuneTvButton
   .menubar.config.show_hide add checkbutton -label "Show status line" -command ToggleStatusLine -variable showStatusLine
   .menubar.config.show_hide add checkbutton -label "Show column headers" -command ToggleColumnHeader -variable showColumnHeader
   .menubar.config.show_hide add checkbutton -label "Show weekday scale" -command ToggleDateScale -variable showDateScale
   menu .menubar.config.layout
   .menubar.config.layout add radiobutton -label "Single list for all networks" -command Toggle_PiBoxType -variable pibox_type -value 0
   .menubar.config.layout add radiobutton -label "Separate columns for each network" -command Toggle_PiBoxType -variable pibox_type -value 1
   if {!$is_unix} {
      .menubar.config.show_hide add checkbutton -label "Hide on minimize" -command ToggleHideOnMinimize -variable hideOnMinimize
   }

   # Reminder menu
   # (note: first three entries' label, state and command binding are overridden by post command)
   menu .menubar.reminder -tearoff 0 -postcommand C_SetReminderMenuStates
   .menubar.reminder add command -label "Add reminder" -state disabled
   .menubar.reminder add cascade -label "Add reminder to group" -menu .menubar.reminder.group -state disabled
   .menubar.reminder add command -label "Remove reminder" -state disabled
   .menubar.reminder add separator
   .menubar.reminder add command -label "Show reminder matches" -command Reminder_ShowAll
   .menubar.reminder add command -label "Edit reminder list..." -command {PopupReminderList 0}
   .menubar.reminder add command -label "Edit filter-based reminders..." -command {PopupReminderList 1}
   .menubar.reminder add separator
   .menubar.reminder add command -label "Configure reminder groups..." -command PopupReminderConfig
   menu .menubar.reminder.group -tearoff 0
   # Filter menu
   menu .menubar.filter
   .menubar.filter add command -label "Text search..." -command SubStrPopup
   if {$is_unix} {
      .menubar.filter add cascade -label "Recurring titles..." -menu .menubar.filter.titles_alpha
#=IF=0
      .menubar.filter add cascade -label "Rec. titles by network..." -menu .menubar.filter.titles_bynet
#=ENDIF=
   } else {
      .menubar.filter add command -label "Recurring titles..." -command {PostSeparateMenu .menubar.filter.titles_alpha undef {}}
#=IF=0
      .menubar.filter add command -label "Rec. titles by network..." -command {PostSeparateMenu .menubar.filter.titles_bynet CreateSeriesNetworksMenu {}}
#=ENDIF=
   }
   .menubar.filter add cascade -label "Networks" -menu .menubar.filter.netwops
   .menubar.filter add separator
   .menubar.filter add cascade -label "Program index" -menu .menubar.filter.progidx
   .menubar.filter add command -label "Start Time..." -command PopupTimeFilterSelection
   .menubar.filter add command -label "Duration..." -command PopupDurationFilterSelection
   .menubar.filter add cascade -label "VPS/PDC" -menu .menubar.filter.vps_pdc
   .menubar.filter add command -label "Expired display..." -command PopupExpireDelaySelection
   .menubar.filter add separator
   .menubar.filter add cascade -menu .menubar.filter.themes -label "Themes"
   .menubar.filter add cascade -menu .menubar.filter.features -label "Features"
   .menubar.filter add cascade -menu .menubar.filter.p_rating -label "Parental Rating"
   .menubar.filter add cascade -menu .menubar.filter.e_rating -label "Editorial Rating"
   .menubar.filter add separator
   .menubar.filter add cascade -label "Invert" -menu .menubar.filter.invert
   .menubar.filter add command -label "Reset" -command {ResetFilterState; C_PiBox_Reset}

   menu .menubar.filter.invert
   .menubar.filter.invert add checkbutton -label "Global" -variable filter_invert(all) -command InvertFilter
   .menubar.filter.invert add separator
   .menubar.filter.invert add cascade -label "Themes" -menu .menubar.filter.invert.themes
   foreach filt {netwops substr features parental editorial progidx timsel dursel vps_pdc} {
      .menubar.filter.invert add checkbutton -label $pi_attr_labels($filt) -variable filter_invert($filt) -command InvertFilter
   }

   menu .menubar.filter.e_rating
   FilterMenuAdd_EditorialRating .menubar.filter.e_rating 0

   menu .menubar.filter.p_rating
   FilterMenuAdd_ParentalRating .menubar.filter.p_rating 0

   menu .menubar.filter.features
   .menubar.filter.features add cascade -menu .menubar.filter.features.sound -label "Sound"
   .menubar.filter.features add cascade -menu .menubar.filter.features.format -label "Format"
   .menubar.filter.features add cascade -menu .menubar.filter.features.digital -label "Digital"
   .menubar.filter.features add cascade -menu .menubar.filter.features.encryption -label "Encryption"
   .menubar.filter.features add cascade -menu .menubar.filter.features.live -label "Live/Repeat"
   .menubar.filter.features add cascade -menu .menubar.filter.features.subtitles -label "Subtitles"
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

#=IF=0
   menu .menubar.filter.titles_bynet -postcommand {PostDynamicMenu .menubar.filter.titles_bynet CreateSeriesNetworksMenu {}}
#=ENDIF=
   menu .menubar.filter.titles_alpha
   if {!$is_unix} {
#=IF=0
      .menubar.filter.titles_bynet configure -tearoff 0
#=ENDIF=
      .menubar.filter.titles_alpha configure -tearoff 0
   }
   foreach letter {A B C D E F G H I J K L M N O P Q R S T U V W X Y Z Other} {
      set w letter_${letter}_off_0
      .menubar.filter.titles_alpha add cascade -label $letter -menu .menubar.filter.titles_alpha.$w
      menu .menubar.filter.titles_alpha.$w -postcommand [list PostDynamicMenu .menubar.filter.titles_alpha.$w CreateSeriesLetterMenu [list $letter 0]]
   }

   menu .menubar.filter.progidx
   FilterMenuAdd_ProgIdx .menubar.filter.progidx 0

   menu .menubar.filter.vps_pdc
   FilterMenuAdd_VpsPdc .menubar.filter.vps_pdc 0

   menu .menubar.filter.netwops
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
   menu .contextmenu -tearoff 0 -postcommand {PostDynamicMenu .contextmenu PopupDynamicContextMenu {}}

   # System tray popup menu (only used if "hide on minimize" is enabled)
   menu .systray -tearoff 0 -postcommand C_SetControlMenuStates
   .systray add command -label "Show window" -command {wm deiconify .; C_SystrayIcon 0}
   .systray add separator
   .systray add checkbutton -label "Enable acquisition" -variable menuStatusStartAcq -command {C_ToggleAcq $menuStatusStartAcq $menuStatusDaemon}
   .systray add checkbutton -label "Connect to acq. daemon" -variable menuStatusDaemon -command {C_ToggleAcq $menuStatusStartAcq $menuStatusDaemon}
   .systray add separator
   .systray add command -label "Close this menu" -command {.systray unpost}
   .systray add command -label "Quit" -command {destroy .}
}

##  ---------------------------------------------------------------------------
##  Apply settings loaded from rc/ini file to menu and PI listbox
##
proc ApplyRcSettingsToMenu {} {
   global showColumnHeader showStatusLine showDateScale hideOnMinimize
   global pibox_type pibox_height shortinfo_height dscale_width
   global shortcuts shortcut_tree
   global usercols

   ShowOrHideShortcutList

   if {$pibox_type != 0} {
      # option not available for netbox layout
      .menubar.config.show_hide entryconfigure "Show column headers" -state disable
   } elseif {$showColumnHeader == 0} {
      # use "remove" instead of "forget" so that grid parameters are not lost
      grid remove .all.pi.list.colcfg
   }

   if {$showStatusLine == 0} {
      pack forget .all.statusline
   }
   if $hideOnMinimize {
      ToggleHideOnMinimize
   }
   if {$showDateScale == 0} {
      grid remove .all.pi.list.dscale
   }
   .all.pi.list.dscale configure -width $dscale_width


   # set the height of the listbox text widget
   if {[info exists pibox_height]} {
      .all.pi.list.text configure -height $pibox_height
      foreach w [info commands .all.pi.list.nets.n_*] {
         $w configure -height $pibox_height
      }
   }
   if {[info exists shortinfo_height]} {
      .all.pi.info.text configure -height $shortinfo_height
   }

   if {[array size shortcuts] == 0} {
      PreloadShortcuts
   }

   # fill the shortcut listbox
   ShortcutTree_Fill .all.shortcuts.list {} $shortcut_tree shortcuts 0

   Reminder_InitData

   if {[array size usercols] == 0} {
      PreloadUserDefinedColumns
   }
}

##  ---------------------------------------------------------------------------
##  After all steps during start-up are completed, finally map the window
##
proc DisplayMainWindow {iconified} {
   global is_unix pibox_type

   # the "netbox" layout allows horizontal resizing, except on WIN32
   # (because on WIN32 resizability cannot be changed dynamically)
   if {$is_unix && ($pibox_type != 0)} {
      wm resizable . 1 1
   }
   if {! $iconified} {
      wm deiconify .
   }

   update
   after idle {bind  .all <Configure> ShortInfoResized}

   wm minsize . 0 [expr [winfo height .] - [winfo height .all.pi.info.text] + 80]
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
   for {set index 0} {$index <= 0x80} {incr index} {
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
}

proc AddInvertMenuForClasses {widget filt} {
   global theme_class_count

   menu ${widget}
   for {set i 1} {$i <= $theme_class_count} {incr i} {
      ${widget} add checkbutton -label $i -variable filter_invert(${filt}_class${i}) -command InvertFilter
   }
}

##  ---------------------------------------------------------------------------
##  Create a popup menu below the xawtv window (UNIX only)
##  - param #1 is the TV app's X11 display name (given by -tvdisplay command line option)
##  - param #2 is the user-configured popup format
##  - params #3-#6 are the coordinates and dimensions of the xawtv window
##  - params #7-#9 are the EPG info to be displayed in the popup
##
proc Create_XawtvPopup {dpyname mode xcoo ycoo width height rperc rtime ptitle} {
   global xawtv_font xawtv_overlay_fg xawtv_overlay_bg

   if {$mode == 0} {
      set text_line1 $rtime
      set text_line2 $ptitle
   } elseif {$mode == 1} {
      set text_line1 "$rtime  $ptitle"
      set text_line2 {}
   } else {
      set text_line1 "$rtime ([expr int($rperc * 100)]%)"
      set text_line2 $ptitle
   }

   # calculate max. of line lengths
   set ww1 [font measure $xawtv_font $text_line1]
   set ww2 [font measure $xawtv_font $text_line2]
   set ww [expr ($ww1 >= $ww2) ? $ww1 : $ww2]

   if {[string length [info commands .xawtv_epg]] == 0} {
      toplevel .xawtv_epg -class InputOutput -screen $dpyname
      wm overrideredirect .xawtv_epg 1
      wm withdraw .xawtv_epg

      frame .xawtv_epg.f -borderwidth 2 -relief raised
      if {$mode == 0} {
         # two line format: build run-time percentage bar
         frame .xawtv_epg.f.rperc -borderwidth 1 -relief sunken -height 5 -width $ww
         pack propagate .xawtv_epg.f.rperc 0
         frame .xawtv_epg.f.rperc.bar -background blue -height 5 -width [expr int($ww * $rperc)]
         pack propagate .xawtv_epg.f.rperc.bar 0
         pack .xawtv_epg.f.rperc.bar -anchor w -side left -fill y -expand 1
         pack .xawtv_epg.f.rperc -side top -fill x -expand 1 -padx 5 -pady 4
      }

      label .xawtv_epg.f.lines -font $xawtv_font
      pack .xawtv_epg.f.lines -side top -padx 5
      pack .xawtv_epg.f

      if {$mode != 0} {
         # set different options for overlay
         .xawtv_epg configure -relief flat -background $xawtv_overlay_bg
         .xawtv_epg.f configure -relief flat -background $xawtv_overlay_bg
         .xawtv_epg.f.lines configure -background $xawtv_overlay_bg -foreground $xawtv_overlay_fg \
                                      -justify left
      }
   } else {
      wm withdraw .xawtv_epg

      if {[llength [info commands .xawtv_epg.f.rperc]] > 0} {
         .xawtv_epg.f.rperc configure -width $ww
         .xawtv_epg.f.rperc.bar configure -width [expr int($ww * $rperc)]
      }
   }

   if {[string length $text_line2] > 0} {
      .xawtv_epg.f.lines configure -text "$text_line1\n$text_line2"
   } else {
      .xawtv_epg.f.lines configure -text "$text_line1"
   }

   # add X-padding and border width
   set ww [expr $ww + 5 + 5 + 2 + 2]

   # calculate X-position: center
   set wxcoo [expr $xcoo + ($width - $ww) / 2]
   if {$wxcoo < 0} {
      set wxcoo 0
   } elseif {$wxcoo + $ww > [winfo screenwidth .xawtv_epg]} {
      set wxcoo [expr [winfo screenwidth .xawtv_epg] - $ww]
      if {$wxcoo < 0} {set wxcoo 0}
   }

   # calculate Y-position
   if {$mode == 0} {
      # separate two-line popup: below the xawtv window; above if too low
      set wh [expr 2 * [font metrics $xawtv_font -linespace] + 3 + 4+4]
      set wycoo [expr $ycoo + $height + 10]
      if {$wycoo + $wh > [winfo screenheight .xawtv_epg]} {
         set wycoo [expr $ycoo - 20 - $wh]
         if {$wycoo < 0} {set wycoo 0}
      }
   } elseif {$mode == 2} {
      # two-line overlay: above the xawtv window, aligned with the bottom border
      set wh [expr 2 * [font metrics $xawtv_font -linespace] + 3]
      set wycoo [expr $ycoo + $height - 15 - $wh]
   } else {
      # single-line overlay
      set wh [expr [font metrics $xawtv_font -linespace] + 3 + 4+4]
      set wycoo [expr $ycoo + $height - 5 - $wh]
   }

   # must build the popup before the geometry request or it will flicker
   update

   wm geometry .xawtv_epg "+$wxcoo+$wycoo"
   wm deiconify .xawtv_epg
   raise .xawtv_epg
}

##  ---------------------------------------------------------------------------
##  Map or unmap the button for TV app control
##  - on UNIX: called when a .xawtv rc file is found in the user's home
##  - on Windows: only called when a TV app is connected
##  - note that the "show" flag is not saved in the nxtvepg rc/ini file
##
proc CreateTuneTvButton {} {
   .all.shortcuts.tune configure -state normal
}

proc RemoveTuneTvButton {} {
   .all.shortcuts.tune configure -state disabled
}

##  ---------------------------------------------------------------------------
##  Show or hide shortcut listbox, network filter listbox and db status line
##
set showNetwopListbox 0
set showNetwopListboxLeft 0
set showShortcutListbox 1
set showStatusLine 1
set showColumnHeader 1
set showTuneTvButton 1
set showLayoutButton 1
set showDateScale 1
set hideOnMinimize 1
set menuUserLanguage 7

proc ShowOrHideShortcutList {{changed {}}} {
   global showShortcutListbox showNetwopListbox showNetwopListboxLeft
   global showTuneTvButton showLayoutButton

   if {$showNetwopListboxLeft && $showNetwopListbox} {
      if {[string compare $changed showNetwopListbox] == 0} {
         set showNetwopListboxLeft 0
      } else {
         set showNetwopListbox 0
      }
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
      if $showLayoutButton {
         grid .all.shortcuts.pitype -row 5 -column 0 -sticky news
      }
      if $showNetwopListbox {
         grid configure .all.shortcuts.clock .all.shortcuts.reset -columnspan 2
      }
   } elseif $showLayoutButton {
      grid .all.shortcuts.pitype -row 5 -column 1 -sticky news
   }
   if $showTuneTvButton {
      grid .all.shortcuts.tune -row 1 -column 0 -sticky news
      if $showNetwopListbox {
         grid configure .all.shortcuts.tune -columnspan 2 
      }
   }
   if $showShortcutListbox {
      grid .all.shortcuts.list -row 3 -column 0 -sticky news
      grid rowconfigure .all.shortcuts 3 -weight 1
   }
   if $showNetwopListbox {
      grid .all.shortcuts.netwops -row 3 -column 1 -rowspan 2 -sticky news
   }
   if $showNetwopListboxLeft {
      grid .all.shortcuts.netwops -row 4 -column 0 -sticky news
      grid rowconfigure .all.shortcuts 4 -weight 1
   }

   if {$showShortcutListbox || $showNetwopListboxLeft || $showNetwopListbox} {
      pack .all.shortcuts -anchor nw -fill y -side left -before .all.pi -expand 1
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

   # show of hide the "config" button (it's save to do this even if already shown or hidden)
   # (note: this option is only enabled in "single list" layout)
   if $showColumnHeader {
      # note: grid position is alread configured during start-up, and need not be set again
      grid .all.pi.list.colcfg
   } else {
      grid remove .all.pi.list.colcfg
   }

   UpdatePiListboxColumns
   UpdateRcFile
}

proc ToggleDateScale {} {
   global showDateScale

   if $showDateScale {
      grid .all.pi.list.dscale
   } else {
      grid remove .all.pi.list.dscale
   }
   UpdateRcFile
   # refresh PI listbox to trigger drawing the scale
   C_PiBox_Refresh
}

proc ToggleHideOnMinimize {} {
   global hideOnMinimize
   global is_unix

   if {!$is_unix} {
      if $hideOnMinimize {
         bind . <Unmap> {if {([string compare %W "."] == 0) && [C_SystrayIcon 1]} {wm withdraw .}}
         bind . <Map> {if {([string compare %W "."] == 0)} {C_SystrayIcon 0}}
      } else {
         bind . <Unmap> {}
         bind . <Map> {}
      }
   }
}

proc Toggle_PiBoxType {} {
   global is_unix pibox_type showColumnHeader

   # make sure pending events are processed before the windows are unmapped
   update

   if {$pibox_type == 0} {
      C_PiBox_Toggle
      grid forget .all.pi.list.nets
      grid forget .all.pi.list.hsc
      grid        .all.pi.list.colheads
      grid forget .all.pi.list.col_pl
      grid forget .all.pi.list.col_mi
      grid        .all.pi.list.text -row 1 -rowspan 3 -column 1 -columnspan 2 -sticky news
      focus       .all.pi.list.text
      .menubar.config.show_hide entryconfigure "Show column headers" -state normal
      if {$showColumnHeader == 0} {
         grid remove .all.pi.list.colcfg
      }
      set hor_resizable 0
   } else {
      C_PiBox_Toggle
      grid forget .all.pi.list.text
      grid remove .all.pi.list.colheads
      grid        .all.pi.list.colcfg
      grid        .all.pi.list.col_pl -row 1 -column 0 -sticky news
      grid        .all.pi.list.col_mi -row 2 -column 0 -sticky news
      grid        .all.pi.list.nets -row 0 -rowspan 4 -column 1 -columnspan 2 -sticky news
      grid        .all.pi.list.hsc -row 4 -column 1 -sticky ew
      focus       .all.pi.list.nets.n_0
      .menubar.config.show_hide entryconfigure "Show column headers" -state disable
      set hor_resizable 1
   }

   if $is_unix {
      wm resizable . $hor_resizable 1
   }
   UpdatePiListboxColumns
   PiListboxColsel_ColUpdate
   C_PiBox_Reset
   UpdateRcFile
}

proc UpdateUserLanguage {} {
   # trigger language update in the C modules
   C_UpdateLanguage

   # redisplay the PI listbox content with the new language
   C_PiBox_Refresh

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
   global netwop_ai2sel netselmenu

   if {[llength $netlist] > 0} {

      # deselect netwop listbox and menu checkbuttons
      .all.shortcuts.netwops selection clear 0 end
      if {[info exists netselmenu]} { unset netselmenu }

      foreach netwop $netlist {
         if {$netwop < [llength $netwop_ai2sel]} {
            set sel_idx [lindex $netwop_ai2sel $netwop]

            set netselmenu($sel_idx) 1
            .all.shortcuts.netwops selection set [expr $sel_idx + 1]
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

   # fetch CNI of current db (may be 0 if none available)
   set prov [C_GetCurrentDatabaseCni]

   # fetch CNI list and netwop names from AI block in database
   set ailist [C_GetAiNetwopList 0 netnames]

   set tmpl [lindex [C_GetProvCniConfig $prov] 0]
   if {[llength $tmpl] != 0} {
      set ailist $tmpl
   }

   .all.shortcuts.netwops delete 1 end
   .all.shortcuts.netwops selection set 0
   .menubar.filter.netwops delete 1 end

   if {[llength $ailist] != 0} {
      set nlidx 0
      foreach cni $ailist {
         .all.shortcuts.netwops insert end $netnames($cni)
         .menubar.filter.netwops add checkbutton -label $netnames($cni) -variable netselmenu($nlidx) -command [list SelectNetwopMenu $nlidx]
         if {[llength [info commands .all.pi.list.nets.h_${nlidx}.b]] != 0} {
            .all.pi.list.nets.h_${nlidx}.b configure -text $netnames($cni)
         }
         incr nlidx
      }
      #.all.shortcuts.netwops configure -height [expr [llength $ailist] + 1]
      .all.shortcuts.netwops configure -height 2
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

proc ResetProgIdx {} {
   global filter_progidx
   global filter_invert

   set filter_progidx 0
   array unset filter_invert progidx
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
   global substr_stack substr_pattern
   global filter_invert

   set substr_stack {}
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

proc ResetExpireDelay {} {
   global piexpire_display piexpire_never

   set piexpire_never 0
   set piexpire_display 0
   C_SelectExpiredPiDisplay
}

proc ResetGlobalInvert {} {
   global filter_invert

   array unset filter_invert all
}

##
##  Reset all filters both in GUI and on C level
##
proc ResetFilterState {} {
   global fsc_prevselection
   global remgroup_filter
   global filter_invert

   ResetThemes
   ResetFeatures
   ResetProgIdx
   ResetTimSel
   ResetMinMaxDuration
   ResetParentalRating
   ResetEditorialRating
   ResetSubstr
   ResetNetwops
   ResetVpsPdcFilt
   ResetExpireDelay

   array unset filter_invert all

   # reset the filter shortcut bar
   Tree:selection .all.shortcuts.list clear first end
   set fsc_prevselection {}
   set remgroup_filter {}

   C_ResetFilter all
   C_InvertFilter {}
   C_PiFilter_ForkContext reset
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

   C_PiBox_Refresh
   CheckShortcutDeselection
}

##  --------------------- F I L T E R   C A L L B A C K S ---------------------

##
##  Callback for button-release on netwop listbox
##
proc SelectNetwop {} {
   global netwop_sel2ai netselmenu

   if {[info exists netselmenu]} { unset netselmenu }

   if {[.all.shortcuts.netwops selection includes 0]} {
      .all.shortcuts.netwops selection clear 1 end
      C_SelectNetwops {}
   } else {
      if {[info exists netwop_sel2ai]} {
         set all {}
         foreach idx [.all.shortcuts.netwops curselection] {
            set netidx [expr $idx - 1]
            set netselmenu($netidx) 1
            if {$netidx < [llength $netwop_sel2ai]} {
               lappend all [lindex $netwop_sel2ai $netidx]
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
   C_PiBox_Refresh
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
   global netwop_sel2ai netwop_ai2sel netselmenu

   if {$netwop < [llength $netwop_ai2sel]} {

      set sel_idx [lindex $netwop_ai2sel $netwop]

      # check if this netwop is part of the user selection
      if {$sel_idx < [llength $netwop_sel2ai]} {
         # simulate a click into the netwop listbox
         if $is_on {
            .all.shortcuts.netwops selection clear 0
            .all.shortcuts.netwops selection set [expr $sel_idx + 1]
         } else {
            # deselect the netwop
            .all.shortcuts.netwops selection clear [expr $sel_idx + 1]
         }
         SelectNetwop
      } else {

         # not found in the netwop menus (not part of user network selection)
         # -> just set the filter & deselect the "All netwops" entry
         .all.shortcuts.netwops selection clear 0
         if $is_on {
            C_SelectNetwops $netwop
         } else {
            set ailist [C_GetAiNetwopList 0 dummy]
            set temp [C_GetNetwopFilterList]
            set idx [lsearch -exact $temp [lindex $ailist $netwop]]
            if {$idx != -1} {
               C_SelectNetwops $netwop
            } else {
               C_SelectNetwops [lreplace $temp $idx $idx {}]
            }
         }
         C_PiBox_Refresh
      }
   }
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
   C_PiBox_Refresh
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
         C_PiBox_Refresh
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
   C_PiBox_Refresh
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
   C_PiBox_Refresh
   CheckShortcutDeselection
}

##
##  Callback for parental rating radio buttons
##
proc SelectEditorialRating {} {
   global editorial_rating

   C_SelectEditorialRating $editorial_rating
   C_PiBox_Refresh
   CheckShortcutDeselection
}

##
##  Callback for series checkbuttons
##
proc SelectSeries {series_title complete} {
   SubstrSetFilter [list $series_title 1 0 1 $complete]
   C_PiBox_Refresh
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
   C_PiBox_Refresh
   CheckShortcutDeselection
}

##
##  Callback for time filter changes
##
proc SelectTimeFilter {} {
   global timsel_relative timsel_absstop timsel_datemode
   global timsel_start timsel_stop timsel_date
   global timsel_enabled

   if $timsel_enabled {
      C_SelectStartTime $timsel_relative $timsel_absstop $timsel_datemode \
                        $timsel_start $timsel_stop $timsel_date
   } else {
      C_SelectStartTime
   }
   C_PiBox_Refresh
   CheckShortcutDeselection
}

##
##  Callback for VPS/PDC filter radiobuttons
##
set vpspdc_filt 0
proc SelectVpsPdcFilter {} {
   global vpspdc_filt

   C_SelectVpsPdcFilter $vpspdc_filt
   C_PiBox_Refresh
   CheckShortcutDeselection
}

##
##  Callback for duration sliders and entry fields
##
proc SelectDurationFilter {} {
   global dursel_min dursel_max

   if {$dursel_max < $dursel_min} {
      if {$dursel_max == 0} {
         set dursel_max 1439
      } else {
         set dursel_max $dursel_min
      }
   }

   C_SelectMinMaxDuration $dursel_min $dursel_max
   C_PiBox_Refresh
   CheckShortcutDeselection
}

##
##  Callback for expiration delay popup
##
proc SelectExpireDelayFilter {} {

   C_SelectExpiredPiDisplay
   C_PiBox_Refresh
}

#
# Interface for disabling programme expiry pre-filter
#
proc SelectExpireNeverFilter {never} {
   global piexpire_never

   set piexpire_never $never
   SelectExpireDelayFilter
}

##
##  Update the filter context and refresh the PI listbox
##  - boolean param indicates if current substr dialog settings should be
##    copied (or if substr_stack was already modified by the caller)
##
proc SubstrUpdateFilter {append_current} {
   global substr_grep_title substr_grep_descr substr_match_case substr_match_full
   global substr_pattern
   global substr_stack substr_history

   # if the boolean param is 1, apply the current dialog settings
   if {$append_current && ([string length $substr_pattern] > 0)} {

      set new [list $substr_pattern $substr_grep_title $substr_grep_descr \
                    $substr_match_case $substr_match_full 0 0]

      # remove identical items from the stack
      set idx 0
      foreach item $substr_stack {
         if {$item == $new} {
            set substr_stack [lreplace $substr_stack $idx $idx]
            break
         }
         incr idx
      }

      set substr_stack [linsert $substr_stack 0 $new]

      # push the parameters onto the history stack (or move them to the top)
      set idx 0
      foreach item $substr_history {
         if {[string compare $substr_pattern [lindex $item 0]] == 0} {
            set substr_history [lreplace $substr_history $idx $idx]
            break
         }
         incr idx
      }
      set substr_history [linsert $substr_history 0 $new]
      if {[llength $substr_history] > 50} {
         set substr_history [lreplace $substr_history 50 end]
      }
      UpdateRcFile
   }

   C_SelectSubStr $substr_stack
   C_PiBox_Refresh
   CheckShortcutDeselection
}

# set the text search parameters saved in the history
proc SubstrSetFilter {parlist} {
   global substr_grep_title substr_grep_descr substr_match_case substr_match_full
   global substr_pattern

   set substr_pattern     [lindex $parlist 0]
   set substr_grep_title  [lindex $parlist 1]
   set substr_grep_descr  [lindex $parlist 2]
   set substr_match_case  [lindex $parlist 3]
   set substr_match_full  [lindex $parlist 4]

   SubstrUpdateFilter 1
}

# undo the given text search filter
proc SubstrUndoFilter {parlist} {
   global substr_stack

   set idx 0
   # search for the given search parameters on the stack
   foreach item $substr_stack {
      if {$item == $parlist} {
         # found -> remove this parameter set
         set substr_stack [lreplace $substr_stack $idx $idx]
         break
      }
      incr idx
   }

   # update substr search filter with the new stack
   SubstrUpdateFilter 0
}


##  ---------------------------------------------------------------------------
##  Callback for selection of a program item
##
proc SelectPi {wid xcoo ycoo {column -1}} {
   global pibox_selected_col

   # first unpost the context menu
   global dynmenu_posted
   if {[info exists dynmenu_posted(.contextmenu)] && ($dynmenu_posted(.contextmenu) > 0)} {
      set dynmenu_posted(.contextmenu) 1
      .contextmenu unpost
   }

   # determine item index below the mouse pointer
   scan [$wid index "@$xcoo,$ycoo"] "%d.%d" new_line char
   incr new_line -1

   C_PiBox_SelectItem $new_line $column

   # remember for motion in which pibox widget the mouse was clicked
   set pibox_selected_col $column

   # binding for motion event to follow the mouse with the selection
   bind $wid <Motion> {SelectPiMotion %s %X %Y}
   UpdatePiMotionScrollTimer 0 0
}

##  ---------------------------------------------------------------------------
##  Callback for Motion event in the PI listbox while mouse button #1 is pressed
##  - this callback is only installed while the left mouse button is pressed
##  - follow the mouse with the cursor, i.e. select the line under the cursor
##
proc SelectPiMotion {state xcoo ycoo} {
   global pibox_type pinetbox_col_count pinetbox_col_width
   global pibox_selected_col

   if {$state & 0x100} {

      if {$pibox_type != 0} {
         set maxcol [expr $pinetbox_col_count - 1]
         set x1 [winfo rootx .all.pi.list.nets.n_0]
         set x2 [expr [winfo rootx .all.pi.list.nets.n_$maxcol] + [winfo width .all.pi.list.nets.n_$maxcol]]

         # check if the mouse has left the listbox horizontally (pinetbox layout only)
         if {$xcoo < $x1} {
            set step_size_x [expr ($xcoo - $x1) / ($pinetbox_col_width / 4)]
            set pibox_selected_col 0
         } elseif {$xcoo >= $x2} {
            set step_size_x [expr ($xcoo - $x2) / ($pinetbox_col_width / 4)]
            set pibox_selected_col $maxcol
         } else {
            # mouse horizontally inside listbox -> determine column
            for {set col 0} {$col < $pinetbox_col_count} {incr col} {
               set wid ".all.pi.list.nets.n_$col"
               set widx [winfo rootx $wid]
               if {($xcoo >= $widx) && ($xcoo < $widx + [winfo width $wid])} {
                  set pibox_selected_col $col
                  break
               }
            }
            set step_size_x 0
         }
         set wid ".all.pi.list.nets.n_$pibox_selected_col"
      } else {
         set wid .all.pi.list.text
         set step_size_x 0
      }
      set xcoo [expr $xcoo - [winfo rootx $wid]]
      set ycoo [expr $ycoo - [winfo rooty $wid]]

      # mouse position has changed -> get the index of the selected text line
      scan [$wid index "@$xcoo,$ycoo"] "%d.%d" line_idx char
      # move the cursor onto the selected line
      C_PiBox_SelectItem [expr $line_idx - 1] $pibox_selected_col

      # check if the cursor is outside the window (or below the last line of a partially filled window)
      # get coordinates of the selected line: x, y, width, height, baseline
      set tmp [$wid dlineinfo "@$xcoo,$ycoo"]
      set line_h  [lindex $tmp 3]
      set line_y1 [lindex $tmp 1]
      set line_y2 [expr $line_y1 + $line_h]

      if {$ycoo < $line_y1 - $line_h} {
         # mouse pointer is above the line: calculate how far away in units of line height (negative)
         set step_size_y [expr ($ycoo - $line_y1) / $line_h]
         if {$step_size_y < -5} {set step_size_y -5}
      } elseif {$ycoo > $line_y2 + $line_h} {
         # mouse pointer is below the line
         set step_size_y [expr ($ycoo - $line_y2) / $line_h]
         if {$step_size_y > 5} {set step_size_y 5}
      } else {
         # mouse inside the line boundaries (or less then a line height outside)
         # -> remove autoscroll timer
         set step_size_y 0
      }
      UpdatePiMotionScrollTimer $step_size_x $step_size_y
   }
}

# callback for button release -> remove the motion binding
proc SelectPiRelease {wid} {

   bind $wid <Motion> {}

   # remove autoscroll timer
   UpdatePiMotionScrollTimer 0 0
}

set pilist_autoscroll_step {0 0}

# called after mouse movements: update scrolling speed or remove timer event
proc UpdatePiMotionScrollTimer {step_size_x step_size_y} {
   global pilist_autoscroll_step pilist_autoscroll_id

   if {$pilist_autoscroll_step != [list $step_size_x $step_size_y]} {

      set pilist_autoscroll_step [list $step_size_x $step_size_y]

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

   set step_size_x [lindex $pilist_autoscroll_step 0]
   set step_size_y [lindex $pilist_autoscroll_step 1]

   # calculate scrolling speed = delay between line scrolling
   if {abs($step_size_x) > abs($step_size_y)} {
      set step_max $step_size_x
   } else {
      set step_max $step_size_y
   }
   set ms [expr (5 - abs($step_max) + 1) * 100]

   # horizontal scrolling
   if {$step_size_x < 0} {
      C_PiBox_CursorLeft
   } elseif {$step_size_x > 0} {
      C_PiBox_CursorRight
   }
   
   # vertical scrolling
   if {$step_size_y < 0} {
      C_PiBox_CursorUp
   } elseif {$step_size_y > 0} {
      C_PiBox_CursorDown
   }

   # delete or renew timer handler
   if {($step_size_x == 0) && ($step_size_y == 0)} {
      if {[info exists pilist_autoscroll_id]} {
         unset pilist_autoscroll_id
      }
   } else {
      set pilist_autoscroll_id [after $ms PiMotionScrollTimer]
   }
}

##  ---------------------------------------------------------------------------
##  Callback for scrolling the listbox by dragging with the middle mouse button
##
proc DragListboxStart {wid xcoo ycoo} {
   global drag_listbox_x drag_listbox_y

   set drag_listbox_x $xcoo
   set drag_listbox_y $ycoo

   # binding for motion event to follow the mouse with the view
   bind $wid <Motion> {DragListboxMotion %s %X %Y}
}

proc DragListboxMotion {state xcoo ycoo} {
   global pibox_type pinetbox_col_width pinetbox_col_count
   global pi_font
   global drag_listbox_x drag_listbox_y

   if {[info exists drag_listbox_x] && ($pibox_type != 0)} {

      set delta [expr round(double($drag_listbox_x - $xcoo) / $pinetbox_col_width)]

      # check if we're already at the left or right border
      set tmpl [C_PiNetBox_GetCniList]
      set net_off [lindex $tmpl 0]
      set net_cnt [expr [llength $tmpl] - 1]
      if {$pinetbox_col_count < $net_cnt} {
         if {$delta < -$net_off} {
            # left border will be reached
            set delta [expr 0 - $net_off]
         } elseif {($delta > 0) && \
                   ($net_off + $pinetbox_col_count + $delta > $net_cnt)} {
            # right border will be reached
            set delta [expr ($net_cnt - $pinetbox_col_count) - $net_off]
         }

         if {abs($delta) >= 1} {
            C_PiBox_ScrollHorizontal scroll $delta units
            incr drag_listbox_x [expr -$delta * $pinetbox_col_width]
         }
      }
   }

   if [info exists drag_listbox_y] {
      set cheight [font metrics $pi_font -linespace]
      set delta [expr round(double($drag_listbox_y - $ycoo) / $cheight)]
      if {abs($delta) >= 1} {
         # note: move twice as far as the mouse was moved
         C_PiBox_Scroll scroll [expr $delta * 2] units
         incr drag_listbox_y [expr -$delta * $cheight]
      }
   }
}

proc DragListboxStop {wid} {

   bind $wid <Motion> {}
}

##  ---------------------------------------------------------------------------
##  Callback for opening the context menu
##  invoked either after click with right mouse button or CTRL-C
##
proc CreateContextMenu {mode wid xcoo ycoo} {

   if {[string compare $mode key] == 0} {
      # context menu to be opened via keypress

      # get position of the text cursor in the PI listbox
      set curpos [$wid tag nextrange cur 1.0]
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
   set xcoo [expr $xcoo + [winfo rootx $wid]]
   set ycoo [expr $ycoo + [winfo rooty $wid]]

   tk_popup .contextmenu $xcoo $ycoo 0
}

##  ---------------------------------------------------------------------------
##  Open a context menu below the current listbox entry
##
proc CreateShortcutContextMenu {coord_x coord_y} {
   global shortcuts
   global ctxmen_sctag

   set wmen .ctx_shortcuts
   set wlist .all.shortcuts.list 

   set ctxmen_sctag [ShortcutTree_Element $wlist @$coord_x,$coord_y]
   if [info exists shortcuts($ctxmen_sctag)] {
      set name [lindex $shortcuts($ctxmen_sctag) $::fsc_name_idx]
      set node [lindex $shortcuts($ctxmen_sctag) $::fsc_node_idx]
      switch -glob -- $node {
         separator {set title "Separator"; set state disabled}
         ?dir {set title "Folder '$name'"; set state disabled}
         default {set title "Shortcut '$name'"; set state normal}
      }
      $wmen entryconfigure 0 -label $title

      .ctx_shortcuts entryconfigure "Update" -state $state

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
   set xcoo [expr [winfo rootx .all.pi.list] - 5]
   set ycoo [expr [winfo rooty .all.pi.list] - 5]

   if {[string length [info commands $wmenu]] == 0} {
      menu $wmenu -postcommand [list PostDynamicMenu $wmenu $func $param] -tearoff 1
   }

   tk_popup $wmenu $xcoo $ycoo 1
}

##  ---------------------------------------------------------------------------
##  Callback for double-click on a PiListBox item
##
set poppedup_pi {}

proc Create_PopupPi {wid xcoo ycoo} {
   global font_normal
   global poppedup_pi

   set tmpl [C_Dump_GetRawPi]
   catch {destroy $poppedup_pi}
   if {[llength $tmpl] >= 3} {
      # list param #1 & #2: PI network and start time -> derive unique toplevel name
      set poppedup_pi ".poppi_[lindex $tmpl 0]_[lindex $tmpl 1]"

      toplevel $poppedup_pi
      wm overrideredirect $poppedup_pi 1
      set xcoo [expr $xcoo + [winfo rootx $wid] - 200]
      set ycoo [expr $ycoo + [winfo rooty $wid] - 5]
      wm geometry $poppedup_pi "+$xcoo+$ycoo"
      wm resizable $poppedup_pi 0 0
      bind $poppedup_pi <Leave> {destroy %W}
      bind $poppedup_pi <FocusOut> {destroy %W}

      text ${poppedup_pi}.text -relief groove -borderwidth 2 -width 55 -height 20 -cursor circle -font $font_normal
      pack ${poppedup_pi}.text

      ${poppedup_pi}.text tag configure title -justify center -spacing3 12 \
                                              -font [DeriveFont $font_normal 6 bold]
      ${poppedup_pi}.text tag configure body -tabs 35m

      $poppedup_pi.text insert end [lindex $tmpl 2] title

      foreach line [lrange $tmpl 3 end] {
         $poppedup_pi.text insert end $line body
      }

      ${poppedup_pi}.text configure -state disabled -height [expr 1 + [$poppedup_pi.text index end]]
   }
}

##  ---------------------------------------------------------------------------
##  Sort a list of series titles alphabetically
##
proc CompareSeriesMenuEntries {a b} {
   return [string compare [lindex $a 1] [lindex $b 1]]
}

#=IF=0
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
   foreach {title desc} $slist {
      lappend all [list $title $desc]
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
            $w add command -label [lindex $item 0] -command [list SelectSeries [lindex $item 1] 1]
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

   # fetch a list of CNIs of netwops which broadcast series programmes
   set series_nets [C_GetNetwopsWithSeries]

   if {[llength $series_nets] != 0} {

      # fetch all network names from AI into an array
      C_GetAiNetwopList 0 netsel_names

      # get the CNI of the currently selected db
      set prov [C_GetCurrentDatabaseCni]
      if {$prov != 0} {

         # sort the cni list according to the user network selection
         set cni_selist [lindex [C_GetProvCniConfig $prov] 0]
         if {[llength $cni_selist] != 0} {
            set tmp {}
            foreach cni $cni_selist {
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
#=ENDIF=

##  ---------------------------------------------------------------------------
##  Create series title menu for one starting letter
##
proc CreateSeriesLetterMenu {w param_list} {

   set letter [string tolower [lindex $param_list 0]]
   set min_index [lindex $param_list 1]

   # sort the list of commands by series title and then create the entries in that order
   set all {}
   foreach {desc title flag} [C_GetSeriesByLetter $letter] {
      # force the new first title character to be uppercase (for sorting)
      lappend all [list $desc $title $flag]
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
            $w add command -label [lindex $item 0] -command [list SelectSeries [lindex $item 1] [lindex $item 2]]
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
proc CreateTransientPopup {wname title {parent .}} {
   global is_unix

   toplevel $wname
   wm title $wname $title
   # disable the resize button in the window frame
   wm resizable $wname 0 0
   if {!$is_unix} {
      # make it a slave of the parent, usually without decoration
      wm transient $wname $parent
      # place it in the upper left corner of the parent (unless iconified)
      if {[string compare [wm state $parent] normal] == 0} {
         wm geometry $wname "+[expr [winfo rootx $parent] + 30]+[expr [winfo rooty $parent] + 30]"
      }
   } else {
      wm group $wname $parent
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
set shortinfo_height 10
set mainwin_zoomed 0

proc AdjustTextWidgetProportions {delta} {
   global panning_list_height panning_info_height
   global pibox_type pibox_height

   pack propagate .all.pi.list 1
   set hl [winfo height .all.pi.list]
   set hi [winfo height .all.pi.info]

   # set height for both layouts
   .all.pi.list.text configure -height [expr $panning_list_height - $delta]
   foreach wid [info commands .all.pi.list.nets.n_*] {
      $wid configure -height [expr $panning_list_height - $delta]
   }

   .all.pi.info.text configure -height [expr $panning_info_height + $delta]

   set pibox_height [expr $panning_list_height - $delta]
   C_PiBox_Resize

   update idletasks
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
   global shortinfo_height pi_font pibox_type

   if {$start} {
      if {$pibox_type == 0} {
         set wid .all.pi.list.text
      } else {
         set wid .all.pi.list.nets.n_0
      }
      set panning_root_y1 [winfo rooty .all.pi.list.panner]
      set panning_root_y2 [expr $panning_root_y1 + [winfo height .all.pi.list.panner]]
      set panning_list_height [$wid cget -height]
      set panning_info_height [.all.pi.info.text cget -height]

      bind .all.pi.list.panner <Motion> {PanningMotion .all.pi.list.panner %s %Y}
   } else {
      unset panning_root_y1 panning_root_y2
      unset panning_list_height panning_info_height
      bind .all.pi.list.panner <Motion> {}

      # save the new listbox and shortinfo geometry to the rc/ini file
      set shortinfo_height [expr int([winfo height .all.pi.info.text] / \
                                     [font metrics $pi_font -linespace])]
      UpdateRcFile
   }
}

# callback for Configure (aka resize) event in short info text widget
proc ShortInfoResized {} {
   global is_unix pi_font shortinfo_height mainwin_zoomed showStatusLine
   global short_info_resized_recurse

   if {![info exists short_info_resized_recurse]} {
      set short_info_resized_recurse 1

      update idletasks

      if $is_unix {
         set is_maximized 0
      } else {
         set is_maximized [expr [string compare [wm state .] "zoomed"] == 0]
      }

      if {!$mainwin_zoomed && $is_maximized} {
         # WIN32: window was maximized -> store last height
         set mainwin_zoomed 1

      } elseif {$mainwin_zoomed && !$is_maximized} {
         # window was un-maximized -> set old height again
         set mainwin_zoomed 0
         .all.pi.info.text configure -height $shortinfo_height

      } else {

         if {[winfo viewable .all.pi.info.text]} {
            set new_height [winfo height .all.pi.info.text]
            if $showStatusLine {
               if {[winfo viewable .all.statusline]} {
                  incr new_height [expr [winfo height .all.statusline] - [winfo reqheight .all.statusline]]
               } else {
                  incr new_height [expr 0 - [winfo reqheight .all.statusline]]
               }
            }
            set new_height [expr int($new_height / [font metrics $pi_font -linespace])]

            if {$new_height != $shortinfo_height} {
               .all.pi.info.text configure -height $new_height
               set shortinfo_height $new_height
               set update_rc 1
            }
         }

         # horizontal resizing (grid layout only)
         global pibox_type pinetbox_col_width pinetbox_col_count
         if {($pibox_type != 0)} {
            if {[winfo viewable .all.pi.list.nets]} {
               set new_width [winfo width .all.pi.list.nets]
               set pinetbox_col_width [expr $new_width / $pinetbox_col_count]
               for {set idx 0} {$idx < $pinetbox_col_count} {incr idx} {
                  .all.pi.list.nets.h_$idx configure -width $pinetbox_col_width
               }
            }
         }
      }

      wm geometry . ""
      update
      unset short_info_resized_recurse
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
#      C_PiBox_Resize
#      UpdateRcFile
#   }
#}


##  --------------------------------------------------------------------------
##  Date scale to the right of the programme list
##  - redraw function is called with start times of the first and last PI
##    matching the current filter upon each refresh or reset
##  - slider position is updated whenever the PI list is scrolled
##  - the user can click or drag in the scale to jump around in the list
##
set dscale_tfirst 0
set dscale_tlen 0
set dscale_button_state 0

##
##  Called when PI list changes (filter changed or PI inserted during acquisition)
##  - parameters: start times of first and last matching PI and GMT offset of first PI
##
proc PiDateScale_Redraw {t_first t_last lto} {
   global dscale_width dscale_scwidth dscale_height
   global dscale_cols dscale_font dscale_date_fmt
   global dscale_tfirst dscale_tlen
   global default_bg pibox_height pi_font
   global showDateScale

   catch {.all.pi.list.dscale delete all}

   if $showDateScale {
      set dscale_height [expr $pibox_height * [font metrics $pi_font -linespace]]
      .all.pi.list.dscale configure -height $dscale_height

      set t_root [expr $t_first - (($t_first + $lto) % (24*60*60))]
      set first_wday [expr ([C_ClockFormat $t_first {%w}] + 1) % 7]

      set dscale_tfirst $t_first
      set dscale_tlen [expr $t_last - $t_first]

      if {$t_first != 0} {
         set th_asc [font metrics $dscale_font -ascent]
         set th_min [font metrics $dscale_font -linespace]
         set wday_idx $first_wday
         set base_y 0
         set t_cur $t_root

         # assert minimum height for each date on the scale, so that weekday names fit in
         # (additionally put a limit on the stop time to avoid painting 100s of invisible days)
         set wday_count [expr ($dscale_tlen + 24*60*60-1) / (24*60*60)]
         if {($wday_count != 0) && ($dscale_height / $wday_count < ($th_min + 6))} {
            set t_last [expr $t_cur + ($dscale_height / ($th_min + 6) + 2) * 24*60*60]
            set dscale_height [expr $wday_count * ($th_min + 6)]
         }

         while {$t_cur < $t_last} {
            # determine weekday name (add 12h to avoid problems with DST change)
            set date_str [C_ClockFormat [expr $t_cur + 12*60*60] $dscale_date_fmt]
            set t_cur [expr $t_cur + (24*60*60)]
            if {$dscale_tlen != 0} {
               set next_y [expr double($t_cur - $t_first) * $dscale_height / $dscale_tlen]
            } else {
               set next_y $dscale_height
            }
            set scmarg [expr ($dscale_width - $dscale_scwidth) / 2]
            .all.pi.list.dscale create rectangle $scmarg $base_y [expr $dscale_width - $scmarg] $next_y \
                                                 -fill [lindex $dscale_cols $wday_idx] \
                                                 -outline white -stipple bitmap_gray
            set cur_y $base_y
            foreach line_str [split $date_str "\n"] {
               if {($cur_y + $th_min + 6 < $next_y) && \
                   ($cur_y + $th_min + 6 < $dscale_height)} {
                  .all.pi.list.dscale create text [expr $dscale_width / 2] \
                                                  [expr $cur_y + $th_asc + 2] \
                                                  -anchor c -font $dscale_font -text $line_str
               }
               set cur_y [expr $cur_y + $th_min + 2]
            }
            set base_y $next_y
            incr wday_idx
            if {$wday_idx >= 7} {set wday_idx 0}
         }
      }
   }
}

##
##  Called by PI listbox when list is scrolled
##  - parameters: start times of first, selected and last visible PI
##
proc PiDateScale_SetSlider {t_first t_cursor t_last} {
   global dscale_width dscale_scwidth dscale_height dscale_font
   global dscale_tfirst dscale_tlen
   global showDateScale

   if {$showDateScale && ($dscale_tfirst != 0)} {
      catch {.all.pi.list.dscale delete slider}

      if {$dscale_tlen != 0} {
         set y0 [expr double($t_first - $dscale_tfirst) * $dscale_height / $dscale_tlen]
         set y1 [expr double($t_last - $dscale_tfirst) * $dscale_height / $dscale_tlen]
         if {$y0 < 2} {
            set y0 2
         }
      } else {
         set y0 2
         set y1 $dscale_height
      }
      set scmarg [expr ($dscale_width - $dscale_scwidth) / 2 - 2]
      .all.pi.list.dscale create rectangle $scmarg $y0 [expr $dscale_width - $scmarg + 1] $y1 \
                                           -fill white -outline black -width 2 -tags slider
      .all.pi.list.dscale lower slider
      if {$::dscale_button_state != 0} {
         .all.pi.list.dscale create rectangle $scmarg $y0 [expr $dscale_width - $scmarg + 1] $y1 \
                                              -fill {} -outline black -width 2 -tags slider
      }
   }
}

# callback for button #1 press, motion or release in the date scale
proc PiDateScale_Goto {ycoo_rel but_state} {
   global dscale_height dscale_font
   global dscale_tfirst dscale_tlen
   global dscale_button_state

   if {(($but_state & 0x100) != 0) && ($dscale_button_state == 0)} {
      # install a handler for motion events while the mouse button remains pressed
      bind .all.pi.list.dscale <Motion> {PiDateScale_Goto %y %s}
      set dscale_button_state 1

   } elseif {(($but_state & 0x100) == 0) && ($dscale_button_state != 0)} {
      # button no longer pressed -> remove the motion event handler
      bind .all.pi.list.dscale <Motion> {}
      set dscale_button_state 0
   }

   # jump in the programme list to the first PI starting after the selected time
   # note: intentionally not using "running at" mode because then very long
   # transmission cause large "jumps" (e.g. Arte "Sendeschluss" 2:35 - 14:00)
   C_PiBox_GotoTime 1 [expr $dscale_tfirst + ($ycoo_rel * $dscale_tlen / $dscale_height)]
}


##  --------------------------------------------------------------------------
##  Tune the station of the currently selected programme
##  - callback command of the "Tune TV" button in the main window
##  - pops up a warning if network names have not been sync'ed with TV app yet.
##    this popup is shown just once after each program start.
##
proc TuneTV_ButtonPress {} {
   global tunetv_msg_nocfg

   # warn if network names have not been sync'ed with TV app yet
   if {([llength [C_GetNetwopNames]] == 0) && ![info exists tunetv_msg_nocfg]} {
      set tunetv_msg_nocfg 1

      set tvapp_name [TvAppGetName]
      set answer [tk_messageBox -type okcancel -default ok -icon info \
                     -message "Please synchronize network names with $tvapp_name in the Network Name Configuration dialog. You need to do this just once."]
      if {[string compare $answer "ok"] == 0} {
         # invoke the network name configuration dialog
         NetworkNamingPopup
         return
      }
   }

   # invoke the user-configured command (stored in context menu config)
   ExecuteTuneTvCommand
}

# callback for double-click or "Return" key on PI
proc TuneTV_KeyPress {} {
   if {[llength [info commands .all.shortcuts.tune]] != 0} {
      tkButtonInvoke .all.shortcuts.tune
   } else {
      TuneTV_ButtonPress
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
   # check if the widget already has the focus
   if {[string compare [focus -displayof $wid] $wid] != 0} {
      # check if the widget is disabled or read-only
      if {([catch [list $wid cget -state] state] != 0) || \
          ($state eq "normal")} {
         $wid selection range 0 end
         focus $wid
      }
   }
}

##  --------------------------------------------------------------------------
##  Handling of help popup
##
set help_popup 0
set help_lang "en"

proc PopupHelp {index {subheading {}} {subrange {}}} {
   global help_fg help_bg help_font font_fixed
   global help_popup help_winsize helpTexts helpIndex help_lang

   if {$help_popup == 0} {
      set help_popup 1
      toplevel .help
      wm title .help "nxtvepg help"
      wm group .help .

      # command buttons to close help window or to switch chapter
      frame  .help.cmd
      button .help.cmd.dismiss -text "Dismiss" -command {destroy .help} -width 7
      menubutton .help.cmd.lang -text "Language" -menu .help.cmd.lang.men -takefocus 1 \
                                -direction below -indicatoron 1 \
                                -cursor top_left_arrow
      menubutton .help.cmd.chpt -text "Chapters" -menu .help.cmd.chpt.men -takefocus 1 \
                                -direction below -indicatoron 1 -cursor top_left_arrow
      config_menubutton .help.cmd.lang
      config_menubutton .help.cmd.chpt
      button .help.cmd.prev -text "Previous" -width 7
      button .help.cmd.next -text "Next" -width 7
      button .help.cmd.back -text "Back" -width 7 -command HelpHistoryBack -state disabled
      pack   .help.cmd.dismiss -side left -padx 5
      pack   .help.cmd.lang -side left -padx 5
      pack   .help.cmd.chpt -side left -padx 5
      pack   .help.cmd.prev .help.cmd.next -side left
      pack   .help.cmd.back -side left -padx 5
      pack   .help.cmd -side top
      bind   .help.cmd <Destroy> {+ set help_popup 0}

      menu .help.cmd.lang.men -tearoff 0
      .help.cmd.lang.men add radiobutton -label "English" -variable help_lang -value "en" -command HelpLanguageChange
      .help.cmd.lang.men add radiobutton -label "Deutsch" -variable help_lang -value "de" -command HelpLanguageChange

      foreach {title idx} [array get helpIndex] {
         set helpTitle($idx) $title
      }
      menu .help.cmd.chpt.men -tearoff 0
      foreach idx [lsort -integer [array names helpTitle]] {
         .help.cmd.chpt.men add command -label $helpTitle($idx) -command [list PopupHelp $idx]
      }

      frame  .help.disp
      text   .help.disp.text -width 60 -wrap word -foreground $help_fg -background $help_bg \
                             -font $help_font -spacing3 6 -cursor circle \
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
      .help.disp.text tag bind href <Enter> {.help.disp.text configure -cursor top_left_arrow}
      .help.disp.text tag bind href <Leave> {.help.disp.text configure -cursor circle}

      # allow to scroll the text with the cursor keys
      bindtags .help.disp.text {.help.disp.text TextReadOnly . all}
      bind   .help.disp.text <Up>    [concat .help.disp.text yview scroll -1 unit {;} break]
      bind   .help.disp.text <Down>  [concat .help.disp.text yview scroll 1 unit {;} break]
      bind   .help.disp.text <Prior> [concat .help.disp.text yview scroll -1 pages {;} break]
      bind   .help.disp.text <Next>  [concat .help.disp.text yview scroll 1 pages {;} break]
      bind   .help.disp.text <Home>  [concat .help.disp.text yview moveto 0.0 {;} break]
      bind   .help.disp.text <End>   [concat .help.disp.text yview moveto 1.0 {;} break]
      bind   .help.disp.text <Enter> {focus %W}
      bind   .help.disp.text <Escape> {tkButtonInvoke .help.cmd.dismiss}
      # save width and height when the window is resized by the user
      bind   .help <Configure> {HelpWindowResized %W}

      # set user-modified window size
      if [info exists help_winsize] {
         wm geometry .help $help_winsize
      }
      # initialize history stack
      set ::helpStack {}

      # load help texts
      catch {LoadHelpTexts_$help_lang}

   } else {
      # save old text position on the stack
      if {[llength $subrange] == 0} {
         HelpHistoryUpdate
      }
      # when the popup is already open, just exchange the text
      .help.disp.text configure -state normal
      .help.disp.text delete 1.0 end
      .help.disp.sb set 0 1
      # raise the popup above all others in the window stacking order
      raise .help
   }

   # fill the widget with the formatted text (note help texts are authored in Latin1 encoding)
   foreach {htext tlabel} $helpTexts($help_lang,$index) {
      .help.disp.text insert end [encoding convertfrom "iso8859-1" $htext] $tlabel
   }
   .help.disp.text configure -state disabled

   # define/update bindings for left/right command buttons
   if {[info exists helpTexts($help_lang,[expr $index - 1])]} {
      .help.cmd.prev configure -command "PopupHelp [expr $index - 1]" -state normal
      bind .help.disp.text <Left> "PopupHelp [expr $index - 1]"
   } else {
      .help.cmd.prev configure -command {} -state disabled
      bind .help.disp.text <Left> {}
   }
   if {[info exists helpTexts($help_lang,[expr $index + 1])]} {
      .help.cmd.next configure -command "PopupHelp [expr $index + 1]" -state normal
      bind .help.disp.text <Right> "PopupHelp [expr $index + 1]"
   } else {
      .help.cmd.next configure -command {} -state disabled
      bind .help.disp.text <Right> {}
   }

   # bring the given text section into view
   if {([llength $subrange] == 2) && ([string length [lindex $subrange 0]] > 0)} {
      .help.disp.text see [lindex $subrange 1]
      .help.disp.text see [lindex $subrange 0]
   } elseif {[string length $subheading] != 0} {
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
   HelpHistoryPush $index
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

proc HelpHistoryUpdate {} {
   global helpStack

   if {[llength $helpStack] > 0} {
      set min_idx [.help.disp.text index @0,0]
      set max_idx [.help.disp.text index @0,[winfo height .help.disp.text]]

      set sect_idx [lindex [lindex $helpStack end] 0 ]
      set helpStack [lreplace $helpStack end end \
                              [list $sect_idx $min_idx $max_idx]]
   }
}

proc HelpHistoryPush {sect_idx} {
   global helpStack

   lappend helpStack [list $sect_idx {} {}]

   if {[llength $helpStack] > 1} {
      .help.cmd.back configure -state normal
   } else {
      .help.cmd.back configure -state disabled
   }
}

proc HelpHistoryBack {} {
   global helpStack

   set helpStack [lreplace $helpStack end end]
   set elem [lindex $helpStack end]
   set helpStack [lreplace $helpStack end end]
   PopupHelp [lindex $elem 0] {} [lrange $elem 1 2]
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

# callback for language menu entries
proc HelpLanguageChange {} {
   global helpStack help_lang

   catch {LoadHelpTexts_$help_lang}

   UpdateRcFile

   HelpHistoryUpdate
   set elem [lindex $helpStack end]
   set helpStack [lreplace $helpStack end end]
   PopupHelp [lindex $elem 0] {} [lrange $elem 1 2]
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

      label .about.name -text "nxtvepg v$EPG_VERSION"
      pack .about.name -side top -pady 8
      #label .about.tcl_version -text " Tcl/Tk version $tcl_patchLevel"
      #pack .about.tcl_version -side top

      label .about.copyr1 -text "Copyright (C) 1999 - 2011, 2020 - 2021 by T. Zörner"
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
#=CONST= ::cod_width_idx   0
#=CONST= ::cod_head_idx    1
#=CONST= ::cod_menu_idx    2
#=CONST= ::cod_label_idx   3

# this array defines the width of the columns (in pixels), the column
# heading, dropdown menu and the description in the config listbox.

array set colsel_tabs {
   title          {283 Title    FilterMenuAdd_Title      "Title"} \
   netname        {73  Network  FilterMenuAdd_Networks   "Network name"} \
   time           {96  Time     FilterMenuAdd_Time       "Running time"} \
   duration       {48  Duration FilterMenuAdd_Duration   "Duration"} \
   weekday        {37  Day      FilterMenuAdd_Date       "Day of week"} \
   day            {30  Date     FilterMenuAdd_Date       "Day of month"} \
   day_month      {51  Date     FilterMenuAdd_Date       "Day and month"} \
   day_month_year {84  Date     FilterMenuAdd_Date       "Day, month and year"} \
   pil            {91  VPS/PDC  FilterMenuAdd_VpsPdc     "VPS/PDC code"} \
   theme          {74  Theme    FilterMenuAdd_Themes     "Theme"} \
   sound          {71  Sound    FilterMenuAdd_Sound      "Sound"} \
   format         {41  Format   FilterMenuAdd_Format     "Format"} \
   ed_rating      {30  ER       FilterMenuAdd_EditorialRating "Editorial rating"} \
   par_rating     {30  PR       FilterMenuAdd_ParentalRating  "Parental rating"} \
   live_repeat    {47  L/R      FilterMenuAdd_LiveRepeat "Live or repeat"} \
   description    {18  I        none                     "Flag description"} \
   subtitles      {25  ST       FilterMenuAdd_Subtitles  "Flag subtitles"} \
   weekcol        {20  Day      FilterMenuAdd_Date       "Weekday colors"} \
   reminder       {30  Mark     FilterMenuAdd_AllReminders "Reminder marks"} \
}

# define presentation order for configuration listbox
set colsel_ailist_predef [list \
   title netname time duration weekday day day_month day_month_year \
   pil theme sound format ed_rating par_rating live_repeat description \
   subtitles weekcol reminder]

# define default column configuration - is overridden by rc/ini config
set pilistbox_cols {weekcol day_month time title netname reminder}
set pinetbox_rows {reminder weekday time title theme}
set pinetbox_rows_nonl(weekday) 1
set pinetbox_rows_nonl(reminder) 1

set colsel_popup 0

# callback for configure menu: create the configuration dialog
proc PopupColumnSelection {} {
   global colsel_ailist colsel_selist colsel_names colsel_nonl
   global pibox_type pilistbox_cols pinetbox_rows pinetbox_rows_nonl
   global colsel_popup
   global text_fg text_bg

   if {$colsel_popup == 0} {
      CreateTransientPopup .colsel "Programme attribute display selection"
      wm resizable .colsel 1 1
      set colsel_popup 1

      if {$pibox_type == 0} {
         FillColumnSelectionDialog .colsel.all colsel_ailist colsel_selist $pilistbox_cols 1
      } else {
         FillColumnSelectionDialog .colsel.all colsel_ailist colsel_selist $pinetbox_rows 1
      }
      array set colsel_nonl [array get pinetbox_rows_nonl]

      message .colsel.intromsg -text "Select which types of attributes you want\nto have displayed for each TV programme:" \
                               -aspect 2500 -borderwidth 2 -relief ridge -foreground $text_fg -background $text_bg -pady 5
      pack .colsel.intromsg -side top -fill x

      frame .colsel.all
      SelBoxCreate .colsel.all colsel_ailist colsel_selist colsel_names 12 0
      .colsel.all.ai.ailist configure -width 20
      .colsel.all.sel.selist configure -width 20

      button .colsel.all.cmd.help -text "Help" -width 7 -command {PopupHelp $helpIndex(Configure menu) "Select attributes"}
      button .colsel.all.cmd.quit -text "Dismiss" -width 7 -command {destroy .colsel}
      button .colsel.all.cmd.apply -text "Apply" -width 7 -command ColSelDlg_Apply
      pack .colsel.all.cmd.help .colsel.all.cmd.quit .colsel.all.cmd.apply -side bottom -anchor c

      checkbutton .colsel.all.cmd.nonl -text "No new line\nafter element" -justify left -variable {} -state disabled
      pack .colsel.all.cmd.nonl -side top -after .colsel.all.cmd.delnet -pady 10 -anchor c
      # following line is required as work-around for bug in pack:
      # the button forgets it's previous anchor setting due to above -after
      pack configure .colsel.all.cmd.delnet -anchor c
      bind .colsel.all.sel.selist <<ListboxSelect>> [list + ColSelDlg_SelChange]
      pack .colsel.all -side top -fill both -expand 1

      frame   .colsel.tailmsg -borderwidth 2 -relief ridge -background $text_bg
      message .colsel.tailmsg.msg -text "Note: you can extend the above list with user-defined\nattributes which allow to choose colors and images" \
                                  -aspect 2500 -foreground $text_fg -background $text_bg
      pack    .colsel.tailmsg.msg -side left -fill x -expand 1
      button  .colsel.tailmsg.goto -text "Open..." -borderwidth 0 -relief flat \
                                   -foreground $text_fg -background $text_bg \
                                   -command PopupUserDefinedColumns
      pack    .colsel.tailmsg.goto  -side right -anchor e
      pack    .colsel.tailmsg  -side top -fill x

      bind .colsel <Key-F1> {PopupHelp $helpIndex(Configure menu) "Select attributes"}
      bind .colsel.all.cmd <Destroy> {+ set colsel_popup 0}
      bind  .colsel.all.cmd.apply <Return> {tkButtonInvoke .colsel.all.cmd.apply}
      bind  .colsel.all.cmd.apply <Escape> {tkButtonInvoke .colsel.all.cmd.quit}
      focus .colsel.all.cmd.apply

      update
      wm minsize .colsel [winfo reqwidth .colsel] [winfo reqheight .colsel]
   } else {
      raise .colsel
   }
}

# callback for "Apply" button in the config dialog
proc ColSelDlg_Apply {} {
   global pibox_type pilistbox_cols pinetbox_rows pinetbox_rows_nonl
   global colsel_selist colsel_nonl

   # save the new list
   if {$pibox_type == 0} {
      set pilistbox_cols $colsel_selist
   } else {
      set pinetbox_rows $colsel_selist
   }

   # save the list of joined columns
   array unset pinetbox_rows_nonl
   foreach tag [array names colsel_nonl] {
      if {$colsel_nonl($tag) != 0} {
         set pinetbox_rows_nonl($tag) 1
      }
   }

   UpdateRcFile

   # redraw the PI listbox with the new settings
   UpdatePiListboxColumns
   C_PiBox_Refresh
}

# callback for selection change
proc ColSelDlg_SelChange {} {
   global pibox_type
   global colsel_selist colsel_nonl

   set sel [.colsel.all.sel.selist curselection]
   if {($pibox_type == 1) && \
       ([llength $sel] == 1) && ($sel < [llength $colsel_selist])} {
      set item [lindex $colsel_selist $sel]
      .colsel.all.cmd.nonl configure -variable colsel_nonl($item) -state normal
   } else {
      .colsel.all.cmd.nonl configure -variable {} -state disabled
   }
}

# notification by user-defined columns config dialog: columns addition or deletion
proc PiListboxColsel_ColUpdate {} {
   global colsel_ailist colsel_selist
   global pibox_type pilistbox_cols pinetbox_rows
   global colsel_popup

   if {$colsel_popup} {
      if {$pibox_type == 0} {
         FillColumnSelectionDialog .colsel.all colsel_ailist colsel_selist $pilistbox_cols 0
      } else {
         FillColumnSelectionDialog .colsel.all colsel_ailist colsel_selist $pinetbox_rows 0
      }
   }
}

# helper function: fill the column-selection lists
proc FillColumnSelectionDialog {wselbox var_ailist var_selist old_selist is_initial} {
   global colsel_tabs colsel_names usercols
   global colsel_ailist_predef
   global colsel_popup
   upvar $var_ailist ailist
   upvar $var_selist selist

   array unset colsel_names
   foreach name [array names colsel_tabs] {
      set colsel_names($name) [lindex $colsel_tabs($name) $::cod_label_idx]
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

      ColSelDlg_SelChange
   }
}

##  ---------------------------------------------------------------------------
##  Apply the settings to the PI browser listbox
##  - called when a column was added or removed or a header changed
##
proc UpdatePiListboxColumns {} {
   global colsel_tabs
   global showColumnHeader pi_font
   global pilistbox_cols
   global pibox_type
   global is_unix

   if {$pibox_type != 0} {
      C_PiNetBox_Invalidate
      C_PiOutput_CfgColumns
      return
   }

   # remove previous colum headers
   foreach head [info commands .all.pi.list.colheads.c*] {
      if { [regexp {.all.pi.list.colheads.col_[^.]*$} $head]  || \
           ([string compare $head .all.pi.list.colheads.c0] == 0)} {
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
         frame .all.pi.list.colheads.col_$col -width [lindex $colsel_tabs($col) $::cod_width_idx] -height $colhead_height
         menubutton .all.pi.list.colheads.col_${col}.b -width 1 -cursor top_left_arrow \
                                                  -text [lindex $colsel_tabs($col) $::cod_head_idx] -font $colhead_font
         if {$is_unix} {
            .all.pi.list.colheads.col_${col}.b configure -borderwidth 1 -relief raised
         } else {
            .all.pi.list.colheads.col_${col}.b configure -borderwidth 2 -relief ridge
         }
         pack .all.pi.list.colheads.col_${col}.b -fill x
         pack propagate .all.pi.list.colheads.col_${col} 0
         pack .all.pi.list.colheads.col_${col} -side left -anchor w

         if {[string compare [lindex $colsel_tabs($col) $::cod_menu_idx] "none"] != 0} {
            # create the drop-down menu below the header (the menu items are added dynamically)
            set func [lindex $colsel_tabs($col) $::cod_menu_idx]
            if {[string compare -length 1 $func "&"] == 0} {
               if {[string compare $func "&user_def"] == 0} {
                  # special case: menu derived from shortuts used in user-defined column
                  set func FilterMenuAdd_RefdShortcuts
               } else {
                  # indirect function reference by a user-defined column
                  set func [lindex $colsel_tabs([string range $func 1 end]) $::cod_menu_idx]
               }
            }
            .all.pi.list.colheads.col_${col}.b configure -menu .all.pi.list.colheads.col_${col}.b.men
            menu .all.pi.list.colheads.col_${col}.b.men -postcommand [list PostDynamicMenu .all.pi.list.colheads.col_${col}.b.men $func 1]
         }

         # add bindings to allow manual resizing
         bind .all.pi.list.colheads.col_${col}.b <Motion> [concat ColumnHeaderMotion $col %s %x {;} {if {$colsel_resize_phase > 0} break}]
         bind .all.pi.list.colheads.col_${col}.b <ButtonPress-1> [concat ColumnHeaderButtonPress $col %s %x {;} {if {$colsel_resize_phase > 0} break}]
         bind .all.pi.list.colheads.col_${col}.b <ButtonRelease-1> [concat ColumnHeaderButtonRel $col]
         bind .all.pi.list.colheads.col_${col}.b <Leave> [concat ColumnHeaderLeave $col]

         incr tab_pos [lindex $colsel_tabs($col) $::cod_width_idx]
         lappend tabs ${tab_pos}
      }
      if {[info exists col] && ([string length [info commands .all.pi.list.colheads.col_${col}]] > 0)} {
         # increase width of the last header button to accomodate for text widget borderwidth
         .all.pi.list.colheads.col_${col} configure -width [expr 5 + [.all.pi.list.colheads.col_${col} cget -width]]
      }
   } else {
      # create an invisible frame to set the width of the text widget
      foreach col $pilistbox_cols {
         incr tab_pos [lindex $colsel_tabs($col) $::cod_width_idx]
         lappend tabs ${tab_pos}
      }
      frame .all.pi.list.colheads.c0 -width "[expr $tab_pos + 2]"
      pack .all.pi.list.colheads.c0 -side left -anchor w
   }

   # configure tab-stops in text widget
   .all.pi.list.text tag configure past -tabs $tabs
   .all.pi.list.text tag configure now -tabs $tabs
   .all.pi.list.text tag configure then -tabs $tabs

   # update the settings in the listbox module
   C_PiOutput_CfgColumns
}

##  ---------------------------------------------------------------------------
##  Update the column config cache in the PI browser listbox
##  - called after internal changes in the column definition,
##    i.e. when a shortcut definition was changed
##
proc UpdatePiListboxColumParams {} {
   global pibox_type

   # force a redraw of all currently displayed elements
   if {$pibox_type != 0} {
      C_PiNetBox_Invalidate
   }

   # flush and rebuild the column config cache
   C_PiOutput_CfgColumns
}

##  ---------------------------------------------------------------------------
##
##  Creating menus for the column heads
##
proc FilterMenuAdd_Time {widget is_stand_alone} {
   $widget add command -label "Now" -command {C_PiBox_GotoTime 1 now}
   $widget add command -label "Next" -command {C_PiBox_GotoTime 1 next}

   set now        [C_ClockSeconds] 
   set start_time [expr $now - ($now % (2*60*60)) + (2*60*60)]
   set hour       [expr ($start_time % (24*60*60)) / (60*60)]

   for {set i 0} {$i < 24} {incr i 2} {
      $widget add command -label [C_ClockFormat $start_time {%H:%M}] -command [list C_PiBox_GotoTime 1 $start_time]
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
   set start_time [C_ClockSeconds]
   set last_time [C_GetLastPiTime]

   $widget add command -label "Today, [C_ClockFormat $start_time {%d. %b. %Y}]" -command {C_PiBox_GotoTime 1 now}
   incr start_time [expr 24*60*60]
   $widget add command -label "Tomorrow, [C_ClockFormat $start_time {%d. %b. %Y}]" -command [list C_PiBox_GotoTime 1 $start_time]
   incr start_time [expr 24*60*60]

   while {$start_time <= $last_time} {
      $widget add command -label [C_ClockFormat $start_time {%A, %d. %b. %Y}] -command [list C_PiBox_GotoTime 1 $start_time]
      incr start_time [expr 24*60*60]
   }
}

proc FilterMenuAdd_Networks {widget is_stand_alone} {
   global netselmenu

   $widget add command -label "All networks" -command {ResetNetwops; SelectNetwop}

   # fetch CNI list from AI block in database
   # as a side effect this function stores all netwop names into the array netsel_names
   set netsel_prov [C_GetCurrentDatabaseCni]
   set netsel_ailist [C_GetAiNetwopList 0 netsel_names]

   set netsel_selist [lindex [C_GetProvCniConfig $netsel_prov] 0]
   if {[llength $netsel_selist] == 0} {
      set netsel_selist $netsel_ailist
   }

   # Add currently selected network as command button (unless it is already filtered)
   set cni [C_PiBox_GetSelectedNetCni]
   set nlidx [lsearch -exact $netsel_selist $cni]
   if {($nlidx != -1) &&
       (![info exists netselmenu($nlidx)] || ($netselmenu($nlidx) == 0)) &&
       [info exists netsel_names($cni)]} {
      $widget add command -label "Only $netsel_names($cni)" -command "ResetNetwops; set netselmenu($nlidx) 1; SelectNetwopMenu $nlidx"
   }

   # Add all networks as radio buttons
   $widget add separator
   set nlidx 0
   foreach cni $netsel_selist {
      if [info exists netsel_names($cni)] {
         $widget add checkbutton -label $netsel_names($cni) -variable netselmenu($nlidx) -command [list SelectNetwopMenu $nlidx]
      }
      incr nlidx
   }

   # append invert checkbutton
   $widget add separator
   $widget add checkbutton -label Invert -variable filter_invert(netwops) -command InvertFilter
}

proc FilterMenuAdd_Title {widget is_stand_alone} {
   global substr_grep_title substr_grep_descr substr_match_case substr_match_full
   global substr_stack
   global substr_hist_arr2
   global substr_history

   $widget add command -label "Text search..." -command SubStrPopup
   if {[llength $substr_stack] > 0} {
      $widget add command -label "Undo text search" -command {ResetSubstr; SubstrUpdateFilter 0}
   }

   # append the series menu (same as from the filter menu)
   $widget add cascade -label "Series" -menu ${widget}.series
   if {[string length [info commands ${widget}.series]] == 0} {
      menu ${widget}.series -postcommand [list PostDynamicMenu ${widget}.series CreateSeriesNetworksMenu {}]
   }

   # append shortcuts for the last 10 (of 50) substring searches
   if {[llength $substr_history] + [llength $substr_stack] > 0} {
      $widget add separator

      # add the search stack as menu commands
      set idx 0
      array unset substr_hist_arr2
      foreach item $substr_stack {
         set substr_hist_arr2($item) 1

         $widget add checkbutton -label [lindex $item 0] -variable substr_hist_arr2($item) \
                                 -command [list SubstrUndoFilter $item]
         incr idx
         if {$idx >= 10} break
      }

      # add the search history as menu commands, if not in the stack
      if {$idx < 10} {
         foreach item $substr_history {
            if {! [info exists substr_hist_arr2($item)]} {
               set substr_hist_arr2($item) 0
               $widget add checkbutton -label [lindex $item 0] -variable substr_hist_arr2($item) \
                                       -command [list SubstrSetFilter $item]
            }
            incr idx
            if {$idx >= 10} break
         }
      }

      if {[llength $substr_history] > 0} {
         $widget add separator
         $widget add command -label "Clear search history" -command {set substr_history {}; UpdateRcFile}
      }
   }
}

proc FilterMenuAdd_AllReminders {widget is_stand_alone} {
   global remgroups remgroup_order
   global remgroup_filter

   foreach gtag $remgroup_order {
      upvar #0 foo_rgp_sel_$gtag cbval
      set cbval [expr [lsearch $remgroup_filter $gtag] != -1]
      set cblab [lindex $remgroups($gtag) $::rgp_name_idx]

      $widget add checkbutton -label $cblab -variable foo_rgp_sel_$gtag \
                              -command [list Reminder_SelectFromTagList $remgroup_order $gtag]
   }

   set ::foo_rgp_sel_all [expr [llength $remgroup_filter] >= [llength $remgroup_order]]
   $widget add separator
   $widget add checkbutton -label "All reminders" -variable foo_rgp_sel_all \
                           -command [list Reminder_SelectFromTagList $remgroup_order 0]
}

# create a popup menu derived from shortcuts used in a user-defined column
proc FilterMenuAdd_RefdShortcuts {widget is_stand_alone} {
   global colsel_tabs shortcuts usercols remgroups remgroup_order
   global fsc_prevselection remgroup_filter

   # derive user-def-column tag from menu widget name
   if {[scan $widget {.all.pi.list.colheads.col_user_def_%[^.].b.men} col] == 1} {
      if [info exists usercols($col)] {
         # loop across all shortcuts used by this column
         set sc_list {}
         set rgp_list {}
         foreach filt $usercols($col) {
            set sc_tag [lindex $filt $::ucf_sctag_idx]
            set gtag [UserColsDlg_IsReminderPseudoTag $sc_tag]
            if {($gtag == 0) || [info exists remgroups($gtag)]} {
               lappend rgp_list $gtag
            } elseif {($sc_tag != -1) && [info exists shortcuts($sc_tag)]} {
               lappend sc_list $sc_tag
            }
         }

         # add a menu entry to invoke this shortcut and deselect the others
         foreach sc_tag $sc_list {
            $widget add checkbutton -label [lindex $shortcuts($sc_tag) $::fsc_name_idx] \
                                    -command [list SelectShortcutFromTagList $sc_list $sc_tag] \
                                    -variable foo_sc_tag_sel_$sc_tag
            set ::foo_sc_tag_sel_$sc_tag [expr [lsearch $fsc_prevselection $sc_tag] != -1]
         }

         if {([llength $sc_list] > 0) && ([llength $rgp_list] > 0)} {
            $widget add separator
         }

         foreach gtag $rgp_list {
            if {$gtag != 0} {
               upvar #0 foo_rgp_sel_$gtag cbval
               set cbval [expr [lsearch $remgroup_filter $gtag] != -1]
               set cblab [lindex $remgroups($gtag) $::rgp_name_idx]

               $widget add checkbutton -label $cblab -variable foo_rgp_sel_$gtag \
                                       -command [list Reminder_SelectFromTagList $rgp_list $gtag]
            } else {
               set ::foo_rgp_sel_all [expr [llength $remgroup_filter] >= [llength $remgroup_order]]
               $widget add checkbutton -label "all reminders" -variable foo_rgp_sel_all \
                                       -command [list Reminder_SelectFromTagList $rgp_list $gtag]
            }
         }
      }
   }
}

##  ---------------------------------------------------------------------------
##  Drop-down menu for PI listbox column headers in "netbox" layout
##
proc NetboxColumnHeaderMenu {widget is_stand_alone} {
   global pinetbox_col_count

   # add "Goto time X" sub-menu (same as in pilistbox)
   $widget add cascade -label "Time" -menu ${widget}.time
   if {[string length [info commands ${widget}.time]] == 0} {
      menu ${widget}.time -postcommand [list PostDynamicMenu ${widget}.time FilterMenuAdd_Time 0]
   }

   # add "Goto date X" sub-menu (same as in pilistbox)
   $widget add cascade -label "Date" -menu ${widget}.date
   if {[string length [info commands ${widget}.date]] == 0} {
      menu ${widget}.date -postcommand [list PostDynamicMenu ${widget}.date FilterMenuAdd_Date 0]
   }

   # add sub-menu with netbox control functions
   $widget add separator
   $widget add cascade -label "Control" -menu ${widget}.control
   if {[string length [info commands ${widget}.control]] == 0} {
      menu ${widget}.control -tearoff 0
      ${widget}.control add command -label "Add one more network column" -command {NetboxColumnRecount 1}
      ${widget}.control add command -label "Remove one network column" -command {NetboxColumnRecount -1}
      ${widget}.control add command -label "Join column with network to the right" -command [list NetboxColumnJoin $widget]
      ${widget}.control add command -label "Undo joined columns" -command [list NetboxColumnDejoin $widget]
   } else {
      ${widget}.control entryconfigure 1 -state normal
      ${widget}.control entryconfigure 2 -state normal
      ${widget}.control entryconfigure 3 -state normal
   }

   if {$pinetbox_col_count <= 1} {
      # only one column left -> disable column removal
      ${widget}.control entryconfigure 1 -state disabled
   }
   # get column index from widget name
   if {[scan $widget ".all.pi.list.nets.h_%d" col_idx] == 1} {
      set tmpl [C_PiNetBox_GetCniList]
      # first element in list is the current netwop offset into the list
      # i.e. the index of the first visible network in the list
      incr col_idx [lindex $tmpl 0]

      # check if the selected column is the last one -> not joinable
      if {$col_idx + 1 + 1 >= [llength $tmpl]} {
         ${widget}.control entryconfigure 2 -state disabled
      }

      # check if the selected column is already joined -> offer to undo joining
      # note: no check is done if the right column is currently invisible
      #       (i.e. scrolled out; useful e.g. if netbox column count is set to 1)
      if {[llength [lindex $tmpl [expr $col_idx + 1]]] <= 1} {
         ${widget}.control entryconfigure 3 -state disabled
      }
   }
}

# add or remove a column from the netbox
proc NetboxColumnRecount {delta} {
   global pinetbox_col_count

   incr pinetbox_col_count $delta
   if {$pinetbox_col_count < 1} {set pinetbox_col_count 1}

   # create new column widgets if necessary
   for {set idx 0} {$idx < $pinetbox_col_count} {incr idx} {
      if {[llength [info commands .all.pi.list.nets.n_$idx]] == 0} {
         CreateListboxNetCol $idx
      }
   }

   # remove obsolete columns
   set ltmp {}
   while {[llength [info commands .all.pi.list.nets.n_$idx]] != 0} {
      lappend ltmp .all.pi.list.nets.n_$idx .all.pi.list.nets.h_$idx
      incr idx
   }
   if {[llength $ltmp] > 0} {
      eval [concat destroy $ltmp]
   }

   C_PiBox_Resize
   UpdateRcFile
}

##  ---------------------------------------------------------------------------
##  Drop-down menu for PI listbox column headers in "netbox" layout
##
set cfnetjoin {}

proc NetboxColumnJoin {widget} {
   global cfnetjoin

   if {[scan $widget ".all.pi.list.nets.h_%d" col_idx] == 1} {

      # get list of CNIs per listbox column
      set tmpl [C_PiNetBox_GetCniList]

      # first element in list is the current netwop offset into the list
      # i.e. the index of the first visible network in the list
      incr col_idx [lindex $tmpl 0]

      if {$col_idx + 2 < [llength $tmpl]} {
         set cni_left  [lindex $tmpl [expr $col_idx + 1]]
         set cni_right [lindex $tmpl [expr $col_idx + 2]]

         set joined_cni {}
         set tmpl {}
         # loop across all join lists (i.e. the list of CNI lists)
         foreach joinl $cfnetjoin {
            set found 0
            # search if any to-be-joined CNIs are already in this join list
            foreach cni [concat $cni_left $cni_right] {
               foreach cni_j $joinl {
                  if {[string compare $cni $cni_j] == 0} {
                     # this CNI is already in a join list
                     set done($cni) 1
                     set found 1
                     break
                  }
               }
            }
            if $found {
               # this join list contains at least one of the to-be-joined ones
               # -> include it completely tothe new join list
               set joined_cni [concat $joined_cni $joinl]
            } else {
               # no CNI matched -> keep join list unchanged
               lappend tmpl $joinl
            }
         }

         # check for CNIs that were not already part of the join list
         foreach cni [concat $cni_left $cni_right] {
            if {![info exists done($cni)]} {
               # append this CNI to the new join list
               lappend joined_cni $cni
            }
         }
         # write new list of join lists: first all unchanged lists
         set cfnetjoin $tmpl
         # append new CNI list
         lappend cfnetjoin $joined_cni

         C_UpdateNetwopList
         UpdateRcFile
      }
   }
}

proc NetboxColumnDejoin {widget} {
   global cfnetjoin

   if [info exists cfnetjoin] {
      if {[scan $widget ".all.pi.list.nets.h_%d" col_idx] == 1} {
         set tmpl [C_PiNetBox_GetCniList]
         incr col_idx [lindex $tmpl 0]
         if {$col_idx + 1 < [llength $tmpl]} {
            set cnil  [lindex $tmpl [expr $col_idx + 1]]
            set cni0  [lindex $cnil 0]

            set tmpl {}
            set found -1
            set idx 0
            # loop across all join lists (i.e. the list of CNI lists)
            foreach joinl $cfnetjoin {
               foreach cni $joinl {
                  if {[string compare $cni $cni0] == 0} {
                     # found one of the joined CNIs in this list
                     set found $idx
                     break
                  }
               }
               if {$found != -1} break
               incr idx
            }
            if {$found != -1} {
               # remove the corresponding CNI list from the join list
               set cfnetjoin [lreplace $cfnetjoin $found $found]

               C_UpdateNetwopList
               UpdateRcFile
            }
         }
      }
   }
}

##  ---------------------------------------------------------------------------
##  Callbacks for PI listbox column resizing
##
set colsel_resize_phase 0

proc ColumnHeaderMotion {col state xcoo} {
   global pilistbox_cols colsel_tabs
   global colsel_resize_phase
   global pibox_type pinetbox_col_width pinetbox_col_count

   if {$pibox_type == 0} {
      set wid .all.pi.list.colheads.col_$col
   } else {
      set wid .all.pi.list.nets.h_$col
   }

   set cur_width [$wid cget -width]

   if {$colsel_resize_phase == 0} {
      # cursor was not yet in proximity
      if {$xcoo + 7 >= $cur_width} {
         # check if popdown menu is currently displayed
         if {([string length [info commands ${wid}.b.men]] == 0) || \
             ([winfo ismapped ${wid}.b.men] == 0)} {
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

      if {$pibox_type == 0} {
         # standard listbox: "linear list" layout

         if {$xcoo != [lindex $colsel_tabs($col) $::cod_width_idx]} {

            # configure width of the column header
            $wid configure -width $xcoo

            # configure tab-stops in text widget
            set colsel_tabs($col) [concat $xcoo [lrange $colsel_tabs($col) 1 end]]
            set tab_pos 0
            set tabs {}
            foreach col $pilistbox_cols {
               incr tab_pos [lindex $colsel_tabs($col) $::cod_width_idx]
               lappend tabs ${tab_pos}
            }
            .all.pi.list.text tag configure past -tabs $tabs
            .all.pi.list.text tag configure now -tabs $tabs
            .all.pi.list.text tag configure then -tabs $tabs

            # redraw the PI listbox in case strings must be shortened
            UpdatePiListboxColumParams
            C_PiBox_Refresh
         }
      } else {
         # pinetbox layout

         if {$xcoo != $pinetbox_col_width} {
            # all columns have the same width, but we cannot resize all of them
            # here because that would change the position of the widget, hence the
            # relative pointer position, which in the end leads to extreme jittering
            set pinetbox_col_width $xcoo
            $wid configure -width $pinetbox_col_width
         }
      }
   }
}

# callback for mouse leaving the button
proc ColumnHeaderLeave {col} {
   global colsel_resize_phase
   global pibox_type

   if {$pibox_type == 0} {
      set wid .all.pi.list.colheads.col_$col
   } else {
      set wid .all.pi.list.nets.h_$col
   }

   if {$colsel_resize_phase == 1} {
      # pointer was in proximity -> change cursor form back to normal
      # note: do not change pointer form if mouse button is still pressed, i.e. currently resizing
      ${wid}.b configure -cursor top_left_arrow
      set colsel_resize_phase 0
   }
}

# callback for mouse button release event
proc ColumnHeaderButtonPress {col state xcoo} {
   global colsel_resize_phase

   # work-around for either tcl/tk 8.4.* or XFree 4.4: sends Leave event before button press!?
   # so we have to check here if the pointer is actually still inside the resizable area
   ColumnHeaderMotion $col $state $xcoo

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
   global pinetbox_col_count pinetbox_col_width
   global pibox_type

   if {$colsel_resize_phase == 2} {
      # save the configuration into the rc/ini file
      UpdateRcFile
   }

   if {$pibox_type == 0} {
      set wid .all.pi.list.colheads.col_$col
   } else {
      # pinetbox layout: make all columns the same width now
      for {set idx 0} {$idx < $pinetbox_col_count} {incr idx} {
         .all.pi.list.nets.h_$idx configure -width $pinetbox_col_width
      }

      set wid .all.pi.list.nets.h_$col
   }
   ${wid}.b configure -cursor top_left_arrow
   set colsel_resize_phase 0
}


## ---------------------------------------------------------------------------
## Display error message & help text inside PI listbox
## - e.g. shown when the database is empty or no provider available yet
## - for the netbox column layout we need to pack a text widget on top
##   to allow to display a text across all columns
##
proc PiBox_DisplayErrorMessage {text} {
   global pi_font text_fg text_bg NXTVEPG_MAILTO
   global pibox_type

   if {[string length $text] > 0} {
      if {$pibox_type == 0} {
         set wid .all.pi.list.text
      } else {
         set wid .all.pi.list.net_err_text
         if {[llength [info commands $wid]] == 0} {
            text $wid -height 1 -width 1 -wrap none -font $pi_font \
                      -exportselection false -cursor top_left_arrow -insertofftime 0
         }
         # note: stack the new text widget on top of the columns' text widgets
         grid $wid -row 0 -rowspan 4 -column 1 -columnspan 2 -sticky news
      }

      $wid delete 1.0 end

      if {[llength [info commands ${wid}.nxtvlogo]] == 0} {
         button ${wid}.nxtvlogo -bitmap nxtv_logo \
                                -foreground $text_fg -background $text_bg \
                                -borderwidth 0 -highlightthickness 0
         bindtags ${wid}.nxtvlogo {all .}
      }

      $wid tag configure centerTag -justify center
      $wid tag configure bold24Tag -font [DeriveFont $pi_font 12 bold] -spacing1 15 -spacing3 10
      $wid tag configure bold16Tag -font [DeriveFont $pi_font 4 bold] -spacing3 10
      $wid tag configure bold12Tag -font [DeriveFont $pi_font 0 bold]
      $wid tag configure wrapTag   -wrap word
      $wid tag configure yellowBg  -background #ffff50

      $wid insert end "nxtvepg\n" bold24Tag
      $wid insert end "Receiving and Browsing TV Programme Listings on your PC\n" bold16Tag
      $wid window create end -window ${wid}.nxtvlogo
      $wid insert end "\n\nCopyright (C) 1999 - 2011, 2020 - 2021 by T. Zoerner\n" bold12Tag
      $wid insert end "$NXTVEPG_MAILTO\n\n" bold12Tag
      $wid tag add centerTag 1.0 {end - 1 lines}
      $wid insert end "This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation. This program is distributed in the hope that it will be useful, but without any warranty. See the GPL for more details.\n\n" wrapTag

      $wid insert end $text {wrapTag yellowBg}

   } else {
      catch [list destroy .all.pi.list.net_err_text]
   }
}

