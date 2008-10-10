#
#  Timescale and statistics popup windows
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
#    Implements support for drawing timescales and database statistics.
#    All calculations are done at C level, so this module just contains
#    display functions.
#
#  Author: Tom Zoerner
#
#  $Id: draw_stats.tcl,v 1.8 2008/01/22 22:11:41 tom Exp tom $
#

#=LOAD=TimeScale_Open
#=LOAD=DbStatsWin_Create
#=LOAD=StatsWinTtx_Create
#=DYNAMIC=

## ---------------------------------------------------------------------------
## Open or update a timescale popup window
##
proc TimeScale_Open {w cni key isMerged scaleWidth} {
   global default_bg
   global tsc_tail

   # add space for Now & Next boxes to scale width
   incr scaleWidth 40
   set canvasWidth [expr [winfo screenwidth "."] - 150]
   if {$canvasWidth > $scaleWidth} {
      set canvasWidth $scaleWidth
   }

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

      frame $w.top
      # create a frame in the top left which holds command buttons: help, zoom +/-
      frame  $w.top.cmd
      button $w.top.cmd.qmark -bitmap bitmap_qmark -cursor top_left_arrow -takefocus 0 \
                              -command {PopupHelp $helpIndex(Statistics) "Timescale popup windows"}
      relief_ridge_v84 $w.top.cmd.qmark
      pack   $w.top.cmd.qmark -side left -fill y
      bind   $w <Key-F1> {PopupHelp $helpIndex(Statistics) "Timescale popup windows"}
      button $w.top.cmd.zoom_in -bitmap bitmap_zoom_in -cursor top_left_arrow -takefocus 0 \
                                -command [list C_TimeScale_Zoom $key 1]
      relief_ridge_v84 $w.top.cmd.zoom_in
      pack   $w.top.cmd.zoom_in -side left
      button $w.top.cmd.zoom_out -bitmap bitmap_zoom_out -cursor top_left_arrow -takefocus 0 \
                                 -command [list C_TimeScale_Zoom $key -1]
      relief_ridge_v84 $w.top.cmd.zoom_out
      pack   $w.top.cmd.zoom_out -side left
      grid   $w.top.cmd -sticky we -column 0 -row 0

      # create a canvas at the top which holds markers for current time and date breaks
      canvas $w.top.now -bg $default_bg -height 12 -width $canvasWidth -scrollregion [list 0 0 12 $scaleWidth]
      grid $w.top.now -sticky we -column 1 -row 0
      grid columnconfigure $w.top 1 -weight 1

      # create a time scale for each network;  note: use numerical indices
      # instead of CNIs to be able to reuse the scales for another provider
      set idx 0
      foreach cni $netsel_ailist {
         TimeScale_CreateCanvas $w $idx $netnames($cni) $isMerged $scaleWidth
         incr idx
      }
      pack $w.top -padx 5 -pady 5 -side top -fill x

      # create frame with label for status line
      frame $w.bottom -borderwidth 2 -relief sunken
      label $w.bottom.l -text {}
      pack $w.bottom.l -side left -anchor w
      pack $w.bottom -side top -fill x

      # create notification callback in case the window is closed
      bind $w.bottom <Destroy> [list + C_TimeScale_Toggle $key 0]

      # clear information about existing acq tail shades
      array unset tsc_tail "${w}*"

   } else {

      # popup already open -> just update the scales

      set idx 0
      foreach cni $netsel_ailist {
         TimeScale_CreateCanvas $w $idx $netnames($cni) $isMerged $scaleWidth
         incr idx
      }

      # clear information about existing acq tail shades
      array unset tsc_tail "${w}*"

      # remove obsolete timescales at the bottom
      # e.g. after an AI update in case the new AI has less networks
      set tmp {}
      while {[string length [info commands $w.top.n${idx}_name]] != 0} {
         lappend tmp $w.top.n${idx}_name $w.top.n${idx}_stream
         incr idx
      }
      if {[llength $tmp] > 0} {
         eval [concat grid forget $tmp]
         eval [concat destroy $tmp]
      }
      # optionally display the resized window immediately because
      # filling the timescales takes a long time
      #update
   }
}

## ---------------------------------------------------------------------------
## Create or update the n'th timescale
##
proc TimeScale_CreateCanvas {w netwop netwopName isMerged scaleWidth} {
   global tscnowid default_bg

   set w $w.top.n$netwop

   set canvasWidth [expr [winfo screenwidth "."] - 150]
   if {$canvasWidth > $scaleWidth} {
      set canvasWidth $scaleWidth
   }

   # for merged databases make Now & Next boxes invisible
   if $isMerged {
      set now_bg $default_bg
   } else {
      set now_bg black
   }

   if {[string length [info commands ${w}_name]] == 0} {

      label ${w}_name -text $netwopName -anchor w
      grid ${w}_name -sticky we -column 0 -row [expr $netwop + 1]

      canvas ${w}_stream -bg $default_bg -height 13 -width $canvasWidth -cursor top_left_arrow -scrollregion [list 0 0 12 $scaleWidth]
      grid ${w}_stream -sticky we -column 1 -row [expr $netwop + 1]
      bind ${w}_stream <Button-1> [list TimeScale_GotoTime $netwop ${w}_stream %x]

      # TODO should use different variables for each canvas, because the IDs might differ
      set tscnowid(0) [${w}_stream create rect  1 4  5 12 -outline "" -fill $now_bg]
      set tscnowid(1) [${w}_stream create rect  8 4 12 12 -outline "" -fill $now_bg]
      set tscnowid(2) [${w}_stream create rect 15 4 19 12 -outline "" -fill $now_bg]
      set tscnowid(3) [${w}_stream create rect 22 4 26 12 -outline "" -fill $now_bg]
      set tscnowid(4) [${w}_stream create rect 29 4 33 12 -outline "" -fill $now_bg]

      set tscnowid(bg) [${w}_stream create rect 40 4 $scaleWidth 12 -outline "" -fill black]

   } else {
      # timescale already exists -> just update it

      # update the network name and undo highlighting
      ${w}_name configure -text $netwopName -bg $default_bg

      # update the canvas width
      ${w}_stream configure -width $canvasWidth -scrollregion [list 0 0 12 $scaleWidth]
      ${w}_stream coords $tscnowid(bg) 40 4 $scaleWidth 12

      # reset the Now & Next boxes
      for {set idx 0} {$idx < 5} {incr idx} {
         ${w}_stream itemconfigure $tscnowid($idx) -fill $now_bg
      }

      # clear all PI range info in the scale
      set id_bg $tscnowid(bg)
      foreach id [${w}_stream find overlapping 39 0 9999 12] {
         if {$id != $id_bg} {
            ${w}_stream delete $id
         }
      }
   }
}

## ---------------------------------------------------------------------------
## Display the timespan covered by a PI in its network timescale
##
proc TimeScale_AddRange {w netwop pos1 pos2 color hasShort hasLong isLast} {
   global tscnowid

   set wc ${w}.top.n${netwop}_stream

   set y0 4
   set y1 12
   if {$hasShort} {set y0 [expr $y0 - 2]}
   if {$hasLong}  {set y1 [expr $y1 + 2]}
   $wc create rect [expr 40 + $pos1] $y0 [expr 40 + $pos2] $y1 -fill $color -outline ""

   if $isLast {
      # this was the last block as defined in the AI block
      # -> remove any remaining blocks to the right, esp. the "PI missing" range
      set id_bg $tscnowid(bg)
      foreach id [$wc find overlapping [expr 41 + $pos2] 0 9999 12] {
         if {$id != $id_bg} {
            $wc delete $id
         }
      }
   }
}

## ---------------------------------------------------------------------------
## Mark the last added ranges, i.e. the tail of acquisition
## - stream 1 and 2 have separate tails
## - every time a new tail element is added, the older ones are faded out
##   by 10%, i.e. the initial color is white and then fades to the normal
##   stream color, i.e. red or blue
##
proc TimeScale_AddTail {w netwop pos1 pos2 stream} {
   global tsc_tail

   foreach {tel col} [array get tsc_tail "${w}*"] {
      set wo [lindex $tel 0]
      set id [lindex $tel 1]
      set si [lindex $tel 2]
      # skip tail elements of the other stream
      if {$si == $stream} {
         # fade the color (25 is 10% of the maximum 255)
         set col [expr $col - 25]
         if {$col >= 40} {
            # update the tail element's color in the array
            set tsc_tail($tel) $col
            # generate color code in #RRGGBB hex format
            if {$si == 0} {
               set colstr [format "#ff%02x%02x" $col $col]
            } else {
               set colstr [format "#%02x%02xff" $col $col]
            }
            # repaint the element in the canvas
            $wo itemconfigure $id -fill $colstr
         } else {
            # remove this tail element from the tail array and the canvas
            unset tsc_tail($tel)
            $wo delete $id
         }
      }
   }

   set wc ${w}.top.n${netwop}_stream
   set id [$wc create rect [expr 40 + $pos1] 6 [expr 40 + $pos2] 10 -fill "#ffffff" -outline ""]
   set tsc_tail([list $wc $id $stream]) 255
}

## ---------------------------------------------------------------------------
## Remove tail from all canvas widgets in a timescale popup
## - called for the UI timescales when acquisition is stopped
##
proc TimeScale_ClearTail {w} {
   global tsc_tail

   foreach tel [array names tsc_tail "${w}*"] {
      set wo [lindex $tel 0]
      set id [lindex $tel 1]
      $wo delete $id
   }
}

## ---------------------------------------------------------------------------
## Shift all content to the right
##
proc TimeScale_ShiftRight {w dist} {
   global tscnowid

   set idx 0
   while {[string length [info commands $w.top.n$idx]] != 0} {

      set wc $w.top.n$idx_stream
      set id_bg $tscnowid(bg)

      foreach id [$wc find overlapping 40 0 9999 12] {
         if {$id != $id_bg} {
            $wc move $id $dist 0
         }
      }
      incr idx
   }
}

## ---------------------------------------------------------------------------
## Callback for right-click in a timescale
##
proc TimeScale_GotoTime {netwop w xcoo} {

   # translate screen coordinates to canvas coordinates
   set xcoo [expr int([$w canvasx $xcoo])]

   if {$xcoo >= 40} {
      set xcoo [expr $xcoo - 40]
   } else {
      # click onto the Now&Next rectangles -> jump to Now
      set xcoo 0
   }
   set pi_time [C_TimeScale_GetTime $w $xcoo]
   if {$pi_time != 0} {
      # set filter: show the selected network only
      ResetFilterState
      SelectNetwopByIdx $netwop 1
      # set cursor onto the first PI starting after the selected time
      C_PiBox_GotoTime 0 $pi_time
   }
}

## ---------------------------------------------------------------------------
## Move the "NOW" slider to the position which reflects the current time
## - the slider is drawn as a small arrow which points downwards
##
proc TimeScale_DrawDateScale {frame scaleWidth nowoff daybrklist} {
   global font_normal

   TimeScale_ClearDateScale $frame $scaleWidth
   incr scaleWidth 40

   set font_small [DeriveFont $font_normal -2]
   set font_bold  [DeriveFont $font_normal 0 bold]

   # draw daybreak markers
   for {set idx 0} {$idx < [llength $daybrklist]} {incr idx 2} {
      set xcoo [lindex $daybrklist $idx]
      set lab  [lindex $daybrklist [expr $idx + 1]]

      # calculate the space between the current and next markers, or end of canvas
      if {$idx + 2 < [llength $daybrklist]} {
         set labspace [expr [lindex $daybrklist [expr $idx + 2]] - $xcoo]
      } else {
         set labspace [expr $scaleWidth - ($xcoo + 40)]
      }

      # check if the designated label text fits in the allocated space
      if {[font measure $font_small $lab] + 2 > $labspace} {
         # too long -> remove the month number from the date string (starting with the dot between day and month)
         regsub {\..*} $lab {} lab2
         set lab $lab2

         if {[font measure $font_small $lab] >= $labspace} {
            # still too long -> discard text completely
            set lab {}
         }
      }

      # finally draw the elements
      incr xcoo 40
      $frame.top.now create line $xcoo 1 $xcoo 12 -arrow none
      $frame.top.now create text [expr $xcoo + ($labspace / 2)] 6 -anchor c -text $lab -font $font_small
   }

   # draw "now" label and arrow
   if {[string compare "off" $nowoff] != 0} {
      # now lies somewhere in the displayed time range -> draw vertical arrow
      set x1 [expr 40 + $nowoff - 5]
      set x2 [expr 40 + $nowoff]
      set id_lab [$frame.top.now create text $x1 1 -anchor ne -text "now" -font $font_bold]
      set id_arr [$frame.top.now create line $x2 1 $x2 12 -arrow last]

   } else {
      # "now" lies beyond the end of the scale
      # -> draw horizontal arrow, pointing outside of the window
      set x1 [expr $scaleWidth - 18]
      set x2 [expr $scaleWidth - 2]
      set id_arr [$frame.top.now create line $x1 6 $x2 6 -arrow last]

      set x1 [expr $scaleWidth - 25]
      set id_lab [$frame.top.now create text $x1 0 -anchor ne -text "now" -font $font_bold]
   }

   # remove daybreak markers which are overlapped by "now" text or the arrow
   set bbox [$frame.top.now bbox $id_arr $id_lab]
   foreach id [$frame.top.now find overlapping [lindex $bbox 0] 0 [lindex $bbox 2] 12] {
      if {($id != $id_arr) && ($id != $id_lab)} {
         $frame.top.now delete $id
      }
   }
}

proc TimeScale_ClearDateScale {frame scaleWidth} {
   catch [ $frame.top.now delete all ]

   incr scaleWidth 40
   set canvasWidth [expr [winfo screenwidth "."] - 150]
   if {$canvasWidth > $scaleWidth} {
      set canvasWidth $scaleWidth
   }

   $frame.top.now configure -width $canvasWidth -scrollregion [list 0 0 12 $scaleWidth]
}

## ---------------------------------------------------------------------------
## Mark a network label for which a PI was received
##
proc TimeScale_MarkNow {w num color} {
   global tscnowid
   ${w}_stream itemconfigure $tscnowid($num) -fill $color
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

   label $wname.browser.stat -font $font_fixed -justify left -anchor nw
   pack $wname.browser.stat -expand 1 -fill both -side left -padx 5
   pack $wname.browser -side top -anchor nw -fill both

   frame $wname.acq -relief sunken -borderwidth 2
   canvas $wname.acq.hist -bg white -height 128 -width 128
   pack $wname.acq.hist -side left -anchor s -anchor w

   label $wname.acq.stat -font $font_fixed -justify left -anchor nw
   pack $wname.acq.stat -expand 1 -fill both -side left -padx 5

   # this frame is intentionally not packed
   #pack $wname.acq -side top -anchor nw -fill both

   button $wname.browser.qmark -bitmap bitmap_qmark -cursor top_left_arrow -takefocus 0
   relief_ridge_v84 $wname.browser.qmark
   bind   $wname.browser.qmark <ButtonRelease-1> {PopupHelp $helpIndex(Statistics) "Database statistics"}
   pack   $wname.browser.qmark -side top
   bind   $wname <Key-F1> {PopupHelp $helpIndex(Statistics) "Database statistics"}

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
## Create the acq stats window with histogram and summary text
##
proc StatsWinTtx_Create {wname} {
   global font_fixed

   toplevel $wname
   wm title $wname {Teletext grabber statistics}
   wm resizable $wname 0 0
   wm group $wname .

   frame $wname.acq -relief sunken -borderwidth 2
   label $wname.acq.stat -font $font_fixed -justify left -anchor nw
   pack $wname.acq.stat -expand 1 -fill both -side left -padx 5
   pack $wname.acq -side top -anchor nw -fill both

   # TODO help refers to config dialog, which doesn't even mention the stats window
   button $wname.acq.qmark -bitmap bitmap_qmark -cursor top_left_arrow -takefocus 0
   relief_ridge_v84 $wname.acq.qmark
   bind   $wname.acq.qmark <ButtonRelease-1> {PopupHelp $helpIndex(Configuration) "Teletext grabber"}
   pack   $wname.acq.qmark -side top
   bind   $wname <Key-F1> {PopupHelp $helpIndex(Configuration) "Teletext grabber"}

   # inform the control code when the window is destroyed
   bind $wname.acq <Destroy> [list + C_StatsWin_ToggleTtxStats 0]
}

