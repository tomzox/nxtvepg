#
#  GUI for VBI recording tool and shared memory monitor
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
#    This Tcl/Tk script creates and manages the graphical user interface
#    of the VBI recording tool.  It opens a window which contains a table
#    with the constantly updated contents of all TV application controled
#    values in shared memory (the updates are done entirely on C level).
#
#    In the lower part there's an entry field and a checkbutton that
#    allows to record teletext packets (with EPG data) which are forwarded
#    by the TV app through shared memory.  To enable recording the
#    the checkbutton has to be enabled (this is caught on C level by
#    a Tcl variable trace)
#
#  Author: Tom Zoerner
#
#  $Id: vbirec_gui.tcl,v 1.2 2002/05/02 17:37:35 tom Exp tom $
#

set dumpttx_filename {ttx.dat}
set dumpttx_enable 0

proc InitGuiVars {} {
   global ttx_pkg ttx_drop epg_pag epg_pkg
   global cni_vps cni_pdc cni_p8301 cni_name ttx_pgno ttx_head
   global tvChanName tvChanCni tvCurInput tvCurFreq tvGrantTuner

   set ttx_pkg {0}
   set ttx_drop {0}
   set epg_pag {0}
   set epg_pkg {0}
   set cni_vps {---}
   set cni_pdc {---}
   set cni_p8301 {---}
   set cni_name {}
   set ttx_pgno {1DF}
   set ttx_head {}
   set tvChanName {}
   set tvChanCni {---}
   set tvCurInput {undefined}
   set tvCurFreq {undefined}
   set tvGrantTuner {no}
}
InitGuiVars

# create an image of a folder
set fileImage [image create photo -data {
R0lGODlhEAAMAKEAAAD//wAAAPD/gAAAACH5BAEAAAAALAAAAAAQAAwAAAIghINhyycvVFsB
QtmS3rjaH1Hg141WaT5ouprt2HHcUgAAOw==}]


frame     .stats -borderwidth 1 -relief sunken
pack      .stats -side top
label     .stats.lab_ttx_pkg -text "TTX pkg count:"
label     .stats.val_ttx_pkg -textvariable ttx_pkg
grid      .stats.lab_ttx_pkg .stats.val_ttx_pkg -sticky w -padx 5

label     .stats.lab_ttx_drop -text "TTX pkg dropped:"
label     .stats.val_ttx_drop -textvariable ttx_drop
grid      .stats.lab_ttx_drop .stats.val_ttx_drop -sticky w -padx 5

label     .stats.lab_epg_pag -text "EPG page count:"
label     .stats.val_epg_pag -textvariable epg_pag
grid      .stats.lab_epg_pag .stats.val_epg_pag -sticky w -padx 5

label     .stats.lab_epg_pkg -text "EPG packet count:"
label     .stats.val_epg_pkg -textvariable epg_pkg
grid      .stats.lab_epg_pkg .stats.val_epg_pkg -sticky w -padx 5

label     .stats.lab_vps -text "VPS CNI and PIL:"
label     .stats.val_vps -textvariable cni_vps
grid      .stats.lab_vps .stats.val_vps -sticky w -padx 5

label     .stats.lab_pdc -text "PDC CNI and PIL:"
label     .stats.val_pdc -textvariable cni_pdc
grid      .stats.lab_pdc .stats.val_pdc -sticky w -padx 5

label     .stats.lab_p8301 -text "Packet 8/30/1:"
label     .stats.val_p8301 -textvariable cni_p8301
grid      .stats.lab_p8301 .stats.val_p8301 -sticky w -padx 5

label     .stats.lab_ttx_pgno -text "EPG teletext page no:"
label     .stats.val_ttx_pgno -textvariable ttx_pgno
grid      .stats.lab_ttx_pgno .stats.val_ttx_pgno -sticky w -padx 5

label     .stats.lab_ttx_head -text "Teletext header:"
label     .stats.val_ttx_head -textvariable ttx_head
grid      .stats.lab_ttx_head .stats.val_ttx_head -sticky w -padx 5

label     .stats.lab_cni_name -text "Network by VPS/PDC/NI:"
entry     .stats.val_cni_name -textvariable cni_name -width 32 -state disabled \
             -borderwidth 0 -background [.stats.lab_cni_name cget -background] -cursor arrow
grid      .stats.lab_cni_name .stats.val_cni_name -sticky w -padx 5

label     .stats.separate -text ""
grid      .stats.separate -columnspan 2

label     .stats.lab_tvChanName -text "TV app. channel name:"
label     .stats.val_tvChanName -textvariable tvChanName
grid      .stats.lab_tvChanName .stats.val_tvChanName -sticky w -padx 5

label     .stats.lab_tvChanCni -text "TV app. channel CNI:"
label     .stats.val_tvChanCni -textvariable tvChanCni
grid      .stats.lab_tvChanCni .stats.val_tvChanCni -sticky w -padx 5

label     .stats.lab_tvCurInput -text "TV app. input source:"
label     .stats.val_tvCurInput -textvariable tvCurInput
grid      .stats.lab_tvCurInput .stats.val_tvCurInput -sticky w -padx 5

label     .stats.lab_tvCurFreq -text "TV app. tuner freq.:"
label     .stats.val_tvCurFreq -textvariable tvCurFreq
grid      .stats.lab_tvCurFreq .stats.val_tvCurFreq -sticky w -padx 5

label     .stats.lab_tvGrantTuner -text "TV app. grants tuner:"
label     .stats.val_tvGrantTuner -textvariable tvGrantTuner
grid      .stats.lab_tvGrantTuner .stats.val_tvGrantTuner -sticky w -padx 5
pack      .stats -side top -fill x

frame     .connect -borderwidth 1 -relief sunken
checkbutton .connect.but_con -text "" -disabledforeground black
bindtags  .connect.but_con {.connect.but_con .}
pack      .connect.but_con -side top -padx 10
pack      .connect -side top -fill x

# create entry field and command button to configure TV app directory
frame     .dumpttx -borderwidth 1 -relief sunken
frame     .dumpttx.name
label     .dumpttx.name.prompt -text "File name:"
pack      .dumpttx.name.prompt -side left
entry     .dumpttx.name.filename -textvariable dumpttx_filename -font {courier -12 normal} -width 20
pack      .dumpttx.name.filename -side left -padx 5 -fill x -expand 1
bind      .dumpttx.name.filename <Enter> {focus %W}
button    .dumpttx.name.dlgbut -image $fileImage -command {
   set tmp [tk_getSaveFile \
               -initialfile [file tail $dumpttx_filename] \
               -initialdir [file dirname $dumpttx_filename]]
   if {[string length $tmp] > 0} {
      set dumpttx_filename $tmp
   }
   unset tmp
}
pack      .dumpttx.name.dlgbut -side left -padx 5
pack      .dumpttx.name -side top -padx 5 -pady 5 -fill x -expand 1

checkbutton .dumpttx.lab_ena -text "Enable teletext recorder" -variable dumpttx_enable
pack      .dumpttx.lab_ena -side top -padx 5 -anchor w
pack      .dumpttx -side top -fill x

frame     .cmd
button    .cmd.quit -text "Quit" -width 5 -command {destroy .}
pack      .cmd.quit -side left -padx 10
pack      .cmd -side top -padx 5 -pady 10


# callback for EPG application attach/detach
proc ConnectEpg {enable name} {
   if $enable {
      .connect.but_con configure -text "Connected to $name" -selectcolor green
      .connect.but_con invoke
   } else {
      .connect.but_con configure -text "Not connected" -selectcolor red
      .connect.but_con deselect
   }
}
ConnectEpg 0 {}

