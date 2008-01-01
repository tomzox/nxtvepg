#
#  GUI for TV interaction simulator
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
#  Description:
#
#    This Tcl/Tk script simulates the parts of a TV application
#    which are involved in interaction with an EPG application.
#    It contains a channel table and text fields to display the
#    current programme (title, start and stop times) on the
#    selected channel.
#    When the user changes the channel, the TV tuner is set to
#    the according frequency and the EPG application is notified.
#
#  Author: Tom Zoerner
#
#  $Id: tvsim_gui.tcl,v 1.16 2007/12/31 17:14:12 tom Exp tom $
#
#=CONST= ::pit_netwop_name 0
#=CONST= ::pit_netwop_idx 1
#=CONST= ::pit_Dstart 2
#=CONST= ::pit_Hstart 3
#=CONST= ::pit_Hstop 4
#=CONST= ::pit_vpspdc_pil 5
#=CONST= ::pit_prat 6
#=CONST= ::pit_erat 7
#=CONST= ::pit_sound 8
#=CONST= ::pit_is_wide 9
#=CONST= ::pit_is_palplus 10
#=CONST= ::pit_is_digital 11
#=CONST= ::pit_is_encrypted 12
#=CONST= ::pit_is_live 13
#=CONST= ::pit_is_repeat 14
#=CONST= ::pit_is_subtitled 15
#=CONST= ::pit_theme_0 16
#=CONST= ::pit_theme_1 17
#=CONST= ::pit_theme_2 18
#=CONST= ::pit_theme_3 19
#=CONST= ::pit_theme_4 20
#=CONST= ::pit_theme_5 21
#=CONST= ::pit_theme_6 22
#=CONST= ::pit_title 23
#=CONST= ::pit_descr 24
#=CONST= ::pit_count 25


set program_title {}
set program_times {}
set program_theme {}
set grant_tuner 0

set epg_pi_count 1

set cur_chan 0
set prev_chan 0

set text_bg    #E9E9EC
set extended_pi 1

if {($tcl_version >= 8.5) && $is_unix} {
   set font_family [font actual TkTextFont -family]
   set font_family_fixed [font actual TkFixedFont -family]
} else {
   set font_family "Helvetica"
   set font_family_fixed "Courier"
}
if $is_unix {
   option add *Dialog.msg.font [list $font_family -12 normal] userDefault
} else {
   option add *Dialog.msg.font [list ansi -12 normal] userDefault
}
option add *Listbox.background $text_bg userDefault
option add *Entry.background $text_bg userDefault
option add *Text.background $text_bg userDefault

# create a listbox for the channel name list
frame     .chan
listbox   .chan.cl -relief ridge -yscrollcommand {.chan.sb set} -exportselection 0
bind      .chan.cl <<ListboxSelect>> {+ TuneChan}
bind      .chan.cl <Enter> {focus %W}
pack      .chan.cl -side left -fill both -expand 1
scrollbar .chan.sb -orient vertical -command {.chan.cl yview} -takefocus 0
pack      .chan.sb -side left -fill y
pack      .chan -side top -padx 5 -fill both -expand 1

# create the three text fiels which hold EPG infos, i.e. title, times, themes
frame     .epgi
if $extended_pi {
   frame     .epgi.pi
   text      .epgi.pi.desc -wrap word -insertofftime 0 -width 50 -height 8 -yscrollcommand {.epgi.pi.sb set}
   pack      .epgi.pi.desc -side left -fill both -expand 1
   scrollbar .epgi.pi.sb -orient vertical -command {.epgi.pi.desc yview} -takefocus 0
   pack      .epgi.pi.sb -side left -fill y
   pack      .epgi.pi -side top -fill both -expand 1

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
   bindtags  .epgi.pi.desc {.epgi.pi.desc TextReadOnly . all}
   .epgi.pi.desc tag configure title -font [list $font_family -14 bold] -justify center -spacing1 2 -spacing3 4
   .epgi.pi.desc tag configure features -font [list $font_family -12 bold] -justify center -spacing3 6
   .epgi.pi.desc tag configure bold -font [list $font_family -12 bold]
   .epgi.pi.desc tag configure paragraph -font [list $font_family -12 normal] -spacing3 4

   # create a bitmap of an horizontal line for use as separator in the info window
   # inserted here manually from the file epgui/line.xbm
   image create bitmap bitmap_line -data "#define line_width 200\n#define line_height 2\n
   static unsigned char line_bits[] = {
      0xe7, 0xe7, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf3, 0xcf,
      0xe7, 0xe7, 0xe7, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf3,
      0xcf, 0xe7};";

   scale     .epgi.pi_cnt -from 0 -to 20 -orient horizontal -label "Number of programmes to show" \
                          -variable epg_pi_count
   pack      .epgi.pi_cnt -side top -fill x
   pack      .epgi -side top -padx 5 -pady 5 -fill both -expand 1

} else {
   entry     .epgi.ptitle -relief flat -borderwidth 1 -textvariable program_title
   pack      .epgi.ptitle -side top -pady 5 -fill x -expand 1
   entry     .epgi.ptimes -relief flat -borderwidth 1 -textvariable program_times
   entry     .epgi.ptheme -relief flat -borderwidth 1 -textvariable program_theme
   pack      .epgi.ptimes -side top -pady 5 -fill x -expand 1
   pack      .epgi.ptheme -side top -pady 5 -fill x -expand 1
   pack      .epgi -side top -padx 5 -fill x
}

# create check button to toggle "grant tuner" mode on/off
checkbutton .granttv -text "Grant tuner to EPG" -variable grant_tuner -command GrantTuner
if {!$is_unix} {
   pack      .granttv -side top -padx 10 -pady 5
} else {
   .granttv configure -state disabled
}

# create check button which is used as connection status indicator
# (cannot be toggled by the user)
checkbutton .connect -text "" -disabledforeground black
bindtags  .connect {.connect .}
if {!$is_unix || $is_xiccc_proto} {
   pack   .connect -side top -padx 10
}


# create command buttons at the bottom of the window
frame     .cmd
button    .cmd.about -text "About" -width 5 -command CreateAbout
button    .cmd.quit -text "Quit" -width 5 -command {destroy .}
pack      .cmd.about .cmd.quit -side left -padx 10
pack      .cmd -side top -padx 5 -pady 10



# callback for channel table
proc TuneChan {} {
   global chan_table
   global grant_tuner
   global cur_chan prev_chan

   set idx [.chan.cl curselection]
   if {([llength $idx] == 1) && ($idx < [llength $chan_table])} {

      if {$grant_tuner} {
         # tuner was granted before -> clear grant
         set grant_tuner 0
         GrantTuner
      }

      set prev_chan $cur_chan
      set cur_chan $idx
      C_TuneChan $idx [lindex $chan_table $idx]
   }
}

# callback for "prev channel" button in EPG application
proc TuneChanPrev {} {
   global cur_chan prev_chan
   global chan_table

   set max [llength $chan_table]
   if {$cur_chan > 0} {
      .chan.cl selection clear 0 end
      .chan.cl selection set [expr $cur_chan - 1]
      TuneChan
   }
}

# callback for "next channel" button in EPG application
proc TuneChanNext {} {
   global cur_chan prev_chan
   global chan_table

   set max [llength $chan_table]
   if {$cur_chan + 1 < $max} {
      .chan.cl selection clear 0 end
      .chan.cl selection set [expr $cur_chan + 1]
      TuneChan
   }
}

# callback for "toggle channel" button in EPG application
proc TuneChanBack {} {
   global cur_chan prev_chan

   .chan.cl selection clear 0 end
   .chan.cl selection set $prev_chan
   TuneChan
}

# callback for "TuneTV" button in EPG application
proc TuneChanByName {sel_name} {
   global chan_table

   set found 0
   # compare the given string with all channel names (non case sensitive)
   set idx 0
   foreach chname $chan_table {
      if {[string compare -nocase $chname $sel_name] == 0} {
         set found 1
         break
      }
      incr idx
   }

   if {$found == 0} {
      # name not found in the channel table -> perform sub-string matches
      set idx 0
      foreach chname $chan_table {
         # the \y matches on word boundaries only, e.g. \yXXX\y matches on "a XXX b" but not on "aXXXb"
         if {[regexp -nocase "\\y$sel_name\\y" $chname]} {
            set found 1
            break
         }
         incr idx
      }
   }

   # found a match above -> tune to the channel
   if $found {
      .chan.cl selection clear 0 end
      .chan.cl selection set $idx
      .chan.cl see $idx
      TuneChan
   }
}

# callback for "grant tuner to EPG" checkbox
proc GrantTuner {} {
   global grant_tuner

   if {$grant_tuner} {
      # grant tuner -> clear channel selection
      .chan.cl selection clear 0 end
      ClearPiDescription
   }
   C_GrantTuner $grant_tuner
}

# callback for EPG application attach/detach
proc ConnectEpg {enable} {
   global grant_tuner chan_table

   if $enable {
      .connect configure -text "Connected to EPG" -selectcolor green
      .connect invoke

      # request name for the current channel
      if {$grant_tuner == 0} {
         set idx [.chan.cl curselection]
         if {([llength $idx] == 1) && ($idx < [llength $chan_table])} {
            C_TuneChan $idx [lindex $chan_table $idx]
         }
      }
   } else {
      .connect configure -text "Not connected to EPG" -selectcolor red
      .connect deselect
   }
}
ConnectEpg 0

# read channel table from TV app ini file during startup
proc LoadChanTable {} {
   global chan_table

   # clear the listbox
   .chan.cl delete 0 end

   # load the TV application's channel table from the configured directory
   set chan_table [C_Tvapp_GetStationNames 0]

   if {[llength $chan_table] > 0} {
      # fill the channel listbox with the names
      eval [concat .chan.cl insert 0 $chan_table]

      # automatically select the first channel
      .chan.cl selection set 0
      TuneChan

   } else {
      # channel table is empty -> issue a warning
      append msg \
         "The channel table is empty!  Without TV channels you " \
         "can't do anything useful with this program. You should " \
         "configure which TV app's channel table to load with " \
         "nxtvepg in the TV app. interaction dialog (in the 'Configure' " \
         "menu) and make sure to use the same rc/INI file here."
      tk_messageBox -type ok -icon info -message $msg
   }
}

# dummy for wintvcfg.c
proc UpdateTvappName {} {
}


##  --------------------------------------------------------------------------
##  Display of prgramme information
##
proc ClearPiDescription {} {
   global program_title program_times program_theme
   global extended_pi

   if $extended_pi {
      .epgi.pi.desc delete 1.0 end

   } else {
      set program_title {}
      set program_times {}
      set program_theme {}
   }
}

proc DisplayPiDescriptionSimple {title times theme} {
   global program_title program_times program_theme
   global extended_pi

   if $extended_pi {
      .epgi.pi.desc delete 1.0 end
      .epgi.pi.desc insert 1.0 "$title\n$times\n$theme"

   } else {
      set program_title $title
      set program_times $times
      set program_theme $theme
   }
}

proc DisplayPiDescription {pi_list} {
   global program_title program_times program_theme
   global extended_pi

   if $extended_pi {
      .epgi.pi.desc delete 1.0 end
      set first_line 1

      foreach tabs [split $pi_list "\n"] {
         set tmpl [split $tabs "\t"]
         if {[llength $tmpl] == $::pit_count} {

            if {!$first_line} {
               .epgi.pi.desc insert end "\n\n" title
               .epgi.pi.desc image create {end - 2 line} -image bitmap_line
            }
            set first_line 0

            set start [clock scan "[lindex $tmpl $::pit_Dstart] [lindex $tmpl $::pit_Hstart]"]
            regexp {\d+:\d+} [lindex $tmpl $::pit_Hstop] stop
            regsub -all { //%// } [lindex $tmpl $::pit_descr] "\n\n" tmps
            regsub -all { // } $tmps "\n" pi_desc

            .epgi.pi.desc insert end "[lindex $tmpl $::pit_title]\n" title
            if {[lindex $tmpl $::pit_theme_0] != 0} {
               .epgi.pi.desc insert end "[C_GetPdcString [lindex $tmpl $::pit_theme_0]]\n" features
            }
            .epgi.pi.desc insert end "[lindex $tmpl $::pit_netwop_name], " bold
            .epgi.pi.desc insert end "[clock format $start -format {%a %d.%m., %H:%M}] - " bold
            .epgi.pi.desc insert end "$stop: " bold
            .epgi.pi.desc insert end $pi_desc paragraph
         }
      }
   }
}


##  --------------------------------------------------------------------------
##  INI file handling
##
proc Tvsim_LoadRcFile {filename} {
   global wintvapp_path wintvapp_idx

   set error 0
   set line_no 0

   if {[catch {set rcfile [open $filename "r"]} errmsg] == 0} {
      while {[gets $rcfile line] >= 0} {
         incr line_no
         if {[regexp {^set wintvapp} $line] && \
             ([catch $line] != 0) && !$error} {
            tk_messageBox -type ok -default ok -icon error \
               -message "Syntax error in INI file, line #$line_no: $line"
            set error 1
         }
      }
      close $rcfile
   } else {
      append msg \
         "Failed to load the INI file '$filename': $errmsg" \
         "\nYou can copy the nxtvepg.ini file into the tvsim directory, " \
         "or specify it's location on the command line after -rcfile"
      tk_messageBox -type ok -default ok -icon error -message $msg
   }
}

##  --------------------------------------------------------------------------
##  About window with the obligatory Copyright and License information
##
set about_popup 0

proc CreateAbout {} {
   global TVSIM_VERSION font_family_fixed about_popup

   if {$about_popup == 0} {
      toplevel .about
      wm title .about "About TV app. simulator"
      wm resizable .about 0 0
      wm transient .about .
      set about_popup 1

      label .about.name -text "TV application interaction simulator - tvsim v$TVSIM_VERSION"
      pack .about.name -side top -pady 8

      label .about.copyr1 -text "Copyright (C) 2002,2004,2005,2007 by Th. \"Tom\" Zörner"
      label .about.copyr2 -text "tomzo@users.sourceforge.net"
      label .about.copyr3 -text "http://nxtvepg.sourceforge.net/" -font [list $font_family_fixed -12 normal] -foreground blue
      pack .about.copyr1 .about.copyr2 -side top
      pack .about.copyr3 -side top -padx 10 -pady 10

      message .about.m -text {
For documentation of this software please refer to the HTML document 'tvsim.html' which you should have received together with the software.

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

## dummy procedures for standalone operation without C modules
#proc C_TuneChan {idx name} {puts "$idx $name"}
#proc C_GrantTuner {doGrant} {}
#proc C_Tvapp_GetStationNames {} {}
