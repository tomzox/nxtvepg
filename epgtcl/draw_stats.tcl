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
#  $Id: draw_stats.tcl,v 1.10 2008/10/19 18:58:13 tom Exp tom $
#

#=CONST= ::tsc_cv_label_x0        2
#=CONST= ::tsc_cv_label_w        96
#=CONST= ::tsc_cv_stream_x0     100

#=CONST= ::tsc_cv_scale_y0        0
#=CONST= ::tsc_cv_scale_h        18
#=CONST= ::tsc_cv_label_yoff     -2
#=CONST= ::tsc_cv_stream_y0      20
#=CONST= ::tsc_cv_stream_h        8
#=CONST= ::tsc_cv_tail_y0        22
#=CONST= ::tsc_cv_tail_h          4

#=LOAD=TimeScale_Open
#=LOAD=DbStatsWin_Create
#=LOAD=StatsWinTtx_Create
#=DYNAMIC=

## ---------------------------------------------------------------------------
## Open or update a timescale popup window
##
proc TimeScale_Open {w cni scaleWidth} {
   global default_bg pi_font
   global tsc_tail tsc_id

   # fetch network list from AI block in database
   set netsel_ailist [C_GetAiNetwopList $cni netnames]

   # add space for Now & Next boxes to scale width
   incr scaleWidth $::tsc_cv_stream_x0
   set canvasWidth [expr [winfo screenwidth .] - 150]
   if {$canvasWidth > $scaleWidth} {
      set canvasWidth $scaleWidth
   }
   set lh [expr [font metrics $pi_font -linespace] + 2]
   set scaleHeight [expr $::tsc_cv_stream_y0 + $lh * [llength $netsel_ailist]]
   set canvasHeight [expr [winfo screenheight .] - 150]
   if {$canvasHeight > $scaleHeight} {
      set canvasHeight $scaleHeight
   }

   if {[string length [info commands $w]] == 0} {
      toplevel $w
      wm title $w "XMLTV coverage timescales"
      wm resizable $w 1 1
      wm group $w .

      frame $w.top
      # create a frame in the top left which holds command buttons: help, zoom +/-
      button $w.top.qmark -bitmap bitmap_qmark -cursor top_left_arrow -takefocus 0 \
                              -command {PopupHelp $helpIndex(Statistics) "Timescale popup windows"}
      relief_ridge_v84 $w.top.qmark
      pack   $w.top.qmark -side left -fill y
      bind   $w <Key-F1> {PopupHelp $helpIndex(Statistics) "Timescale popup windows"}
      button $w.top.zoom_in -bitmap bitmap_zoom_in -cursor top_left_arrow -takefocus 0 \
                                -command [list C_TimeScale_Zoom 1]
      relief_ridge_v84 $w.top.zoom_in
      pack   $w.top.zoom_in -side left
      button $w.top.zoom_out -bitmap bitmap_zoom_out -cursor top_left_arrow -takefocus 0 \
                                 -command [list C_TimeScale_Zoom -1]
      relief_ridge_v84 $w.top.zoom_out
      pack   $w.top.zoom_out -side left

      # create frame with label for status line
      label $w.top.title -text {} -anchor w -borderwidth 1 -relief flat
      pack  $w.top.title -side left -padx 10 -expand 1 -fill both
      pack $w.top -padx 2 -side top -fill x

      # create canvas for the content area (size set via toplevel dimensions)
      frame $w.middle -borderwidth 1 -relief sunken
      canvas $w.middle.cv -bg $default_bg -width 0 -height 0 \
                                       -scrollregion [list 0 0 $scaleWidth $scaleHeight] \
                                       -yscrollcommand [list $w.middle.sbv set] \
                                       -xscrollcommand [list $w.middle_sbh set] \
                                       -cursor top_left_arrow
      bind $w.middle.cv <Button-1>     [list TimeScale_GotoTimeStart $w %x %y]
      bind $w.middle.cv <Button-4>     {%W yview scroll -1 units}
      bind $w.middle.cv <Button-5>     {%W yview scroll 1 units}
      bind $w.middle.cv <MouseWheel>   {%W yview scroll [expr {- (%D / 40)}] units}

      pack $w.middle.cv -side left -fill both -expand 1
      scrollbar $w.middle.sbv -orient vertical -command [list $w.middle.cv yview]
      pack $w.middle.sbv -side left -fill y
      pack $w.middle -padx 2 -side top -fill both -expand 1

      scrollbar $w.middle_sbh -orient horizontal -command [list $w.middle.cv xview]
      pack $w.middle_sbh -padx 2 -side top -fill x

      # create notification callback in case the window is closed
      bind $w.top <Destroy> {C_TimeScale_Toggle 0}

   } else {
      # canvas already exists - clear all content
      $w.middle.cv delete all
      $w.middle.cv configure -scrollregion [list 0 0 $scaleWidth $scaleHeight]
   }
   # clear information about existing acq tail shades
   array unset tsc_tail "${w}*"
   array unset tsc_id "${w}*"

   # create a time scale for each network;  note: use numerical indices
   # instead of CNIs to be able to reuse the scales for another provider
   set idx 0
   foreach cni $netsel_ailist {
      TimeScale_CreateCanvas $w $idx $netnames($cni) $scaleWidth $canvasWidth
      incr idx
   }

   #set whoff [expr [winfo reqheight $w.top] + [winfo reqheight $w.middle_sbh]]
   wm geometry $w "=[expr $canvasWidth + 20]x[expr $canvasHeight + 35]"
   wm sizefrom $w user
}

## ---------------------------------------------------------------------------
## Create or update the n'th timescale
##
proc TimeScale_CreateCanvas {w netwop netwopName scaleWidth canvasWidth} {
   global tsc_id default_bg pi_font

   set wc $w.middle.cv

   set lh [expr [font metrics $pi_font -linespace] + 2]
   set y0 [expr $::tsc_cv_stream_y0 + $lh * $netwop]
   set y1 [expr $y0 + $::tsc_cv_stream_h]

   # make sure network label fits into the name column
   while {([font measure $pi_font $netwopName] + 4 >= $::tsc_cv_stream_x0) &&
          ([string length $netwopName] > 0)} {
      set netwopName [string replace $netwopName end end]
   }

   set tsc_id("$w.label.$netwop") [$wc create text $::tsc_cv_label_x0 [expr $y0 + 2] \
                                               -text $netwopName -font $pi_font \
                                               -anchor w -width [expr $::tsc_cv_stream_x0 - 4]]

   set tsc_id("$w.bg.$netwop") [$wc create rect $::tsc_cv_stream_x0 $y0 $scaleWidth $y1 \
                                            -outline "" -fill black]
}

## ---------------------------------------------------------------------------
## Display the timespan covered by a PI in its network timescale
##
proc TimeScale_AddRange {w netwop pos1 pos2 color hasShort hasLong isLast} {
   global tsc_id pi_font

   set wc ${w}.middle.cv

   set lh [expr [font metrics $pi_font -linespace] + 2]
   set y0 [expr $::tsc_cv_stream_y0 + $lh * $netwop]
   set y1 [expr $y0 + $::tsc_cv_stream_h]

   if {$hasShort} {incr y0 -2}
   if {$hasLong}  {incr y1  2}
   $wc create rect [expr $::tsc_cv_stream_x0 + $pos1] $y0 [expr $::tsc_cv_stream_x0 + $pos2] $y1 \
                   -fill $color -outline ""

   if $isLast {
      # this was the last block as defined in the AI block
      # -> remove any remaining blocks to the right, esp. the "PI missing" range
      set id_bg $tsc_id("$w.bg.$netwop")
      foreach id [$wc find overlapping [expr $::tsc_cv_stream_x0 + $pos2] $y0 9999 $y1] {
         if {$id != $id_bg} {
            if {![info exists tsc_tail([list $w $id 0])] &&
                ![info exists tsc_tail([list $w $id 1])]} {

               $wc delete $id
            }
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
proc TimeScale_AddTail {w netwop pos1 pos2} {
   global tsc_tail pi_font
   #TODO remove stream
   set stream 0

   foreach {tel col} [array get tsc_tail "${w}*"] {
      set id [lindex $tel 1]
      set si [lindex $tel 2]
      # skip tail elements of the other stream
      if {$si == $stream} {
         # fade the color (20 is ~8% of the maximum 255)
         incr col -20
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
            ${w}.middle.cv itemconfigure $id -fill $colstr
         } else {
            # remove this tail element from the tail array and the canvas
            unset tsc_tail($tel)
            ${w}.middle.cv delete $id
         }
      }
   }

   set lh [expr [font metrics $pi_font -linespace] + 2]
   set y0 [expr $::tsc_cv_tail_y0 + $lh * $netwop]
   set y1 [expr $y0 + $::tsc_cv_tail_h]

   set id [${w}.middle.cv create rect [expr $::tsc_cv_stream_x0 + $pos1] $y0 \
                                      [expr $::tsc_cv_stream_x0 + $pos2] $y1 \
                                      -fill "#ffffff" -outline ""]
   set tsc_tail([list $w $id $stream]) 255
}

## ---------------------------------------------------------------------------
## Raise all tail chunks
## - called after a display refresh to keep tail visible
##
proc TimeScale_RaiseTail {w} {
   global tsc_tail

   foreach tel [array names tsc_tail "${w}*"] {
      set id [lindex $tel 1]
      ${w}.middle.cv raise $id
   }
}

## ---------------------------------------------------------------------------
## Remove tail from all canvas widgets in a timescale popup
## - called for the UI timescales when acquisition is stopped
##
proc TimeScale_ClearTail {w} {
   global tsc_tail

   foreach tel [array names tsc_tail "${w}*"] {
      set id [lindex $tel 1]
      ${w}.middle.cv delete $id
   }
}

## ---------------------------------------------------------------------------
## Shift all content to the right
##
proc TimeScale_ShiftRight {w dist} {
   global tsc_id

   set wc $w.middle.cv

   # make a temporary list of stream backgrounds, which shall not be shifted
   foreach {key id} [array names tsc_id "$w.bg.*"] {
      set exclude($id) 0
   }

   foreach id [$wc find overlapping $::tsc_cv_stream_x0 $::tsc_cv_stream_y0 9999999 9999999] {
      if {![info exists exclude($id)]} {
         $wc move $id $dist 0
      }
   }
}

## ---------------------------------------------------------------------------
## Callback for right-click in a timescale
##
proc TimeScale_GotoTime {w xcoo ycoo} {
   global pi_font

   # translate screen coordinates to canvas coordinates
   set xcoo [expr int([$w.middle.cv canvasx $xcoo])]
   set ycoo [expr int([$w.middle.cv canvasy $ycoo])]

   if {$xcoo >= $::tsc_cv_stream_x0} {
      set xcoo [expr $xcoo - $::tsc_cv_stream_x0]
   } else {
      # click onto the Now&Next rectangles -> jump to Now
      set xcoo 0
   }
   if {$ycoo >= $::tsc_cv_stream_y0} {
      set ycoo [expr $ycoo - $::tsc_cv_stream_y0]
   } else {
      set ycoo 0
   }

   set lh [expr [font metrics $pi_font -linespace] + 2]
   set netwop [expr int($ycoo / $lh)]

   set pi_time [C_TimeScale_GetTime $w $xcoo]
   if {$pi_time != 0} {
      # set filter: show the selected network only
      ResetFilterState
      C_EnableExpirePreFilter 0
      SelectNetwopByIdx $netwop 1
      # set cursor onto the first PI starting after the selected time
      C_PiBox_GotoTime 0 $pi_time
   }
}

proc TimeScale_GotoTimeStart {w xcoo ycoo} {
   # add events for following the mouse while the button is pressed
   bind $w.middle.cv <Motion> [list TimeScale_GotoTimeMotion $w %x %y]
   bind $w.middle.cv <ButtonRelease-1> [list TimeScale_GotoTimeStop $w]

   TimeScale_GotoTime $w $xcoo $ycoo
}

proc TimeScale_GotoTimeMotion {w xcoo ycoo} {
   TimeScale_GotoTime $w $xcoo $ycoo
}

proc TimeScale_GotoTimeStop {w} {
   bind $w.middle.cv <Motion> {}
   bind $w.middle.cv <ButtonRelease-1> {}
}

## ---------------------------------------------------------------------------
## Move the "NOW" slider to the position which reflects the current time
## - the slider is drawn as a small arrow which points downwards
##
proc TimeScale_DrawDateScale {frame scaleWidth nowoff daybrklist} {
   global font_normal

   incr scaleWidth $::tsc_cv_stream_x0

   TimeScale_ClearDateScale $frame $scaleWidth

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
      incr xcoo $::tsc_cv_stream_x0
      $frame.middle.cv create line $xcoo 1 $xcoo 12 -arrow none
      $frame.middle.cv create text [expr $xcoo + ($labspace / 2)] 6 -anchor c -text $lab -font $font_small
   }

   # draw "now" label and arrow
   if {[string compare "off" $nowoff] != 0} {
      # now lies somewhere in the displayed time range -> draw vertical arrow
      set x1 [expr $::tsc_cv_stream_x0 + $nowoff - 5]
      set x2 [expr $::tsc_cv_stream_x0 + $nowoff]
      set id_lab [$frame.middle.cv create text $x1 1 -anchor ne -text "now" -font $font_bold]
      set id_arr [$frame.middle.cv create line $x2 1 $x2 12 -arrow last]

   } else {
      # "now" lies beyond the end of the scale
      # -> draw horizontal arrow, pointing outside of the window
      set x1 [expr $scaleWidth - 18]
      set x2 [expr $scaleWidth - 2]
      set id_arr [$frame.middle.cv create line $x1 6 $x2 6 -arrow last]

      set x1 [expr $scaleWidth - 25]
      set id_lab [$frame.middle.cv create text $x1 0 -anchor ne -text "now" -font $font_bold]
   }

   # remove daybreak markers which are overlapped by "now" text or the arrow
   set bbox [$frame.middle.cv bbox $id_arr $id_lab]
   foreach id [$frame.middle.cv find overlapping [lindex $bbox 0] 0 [lindex $bbox 2] 12] {
      if {($id != $id_arr) && ($id != $id_lab)} {
         $frame.middle.cv delete $id
      }
   }
}

proc TimeScale_ClearDateScale {frame scaleWidth} {
   foreach id [$frame.middle.cv find overlapping $::tsc_cv_stream_x0 0 9999999 $::tsc_cv_scale_h] {
      $frame.middle.cv delete $id
   }

   #$frame.middle.cv configure -width $canvasWidth -scrollregion [list 0 0 $scaleWidth $scaleHeight]
}

## ---------------------------------------------------------------------------
## Mark a network label for which a PI was received
##
proc TimeScale_MarkNet {w netwop color} {
   global tsc_id

   ${w}.middle.cv itemconfigure $tsc_id("$w.label.$netwop") -fill $color
}

## ---------------------------------------------------------------------------
## Create the acq stats window with histogram and summary text
##
proc DbStatsWin_Create {wname} {
   global font_fixed

   toplevel $wname
   wm title $wname {XMLTV database statistics}
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
   bind $wname.browser <Destroy> [list + C_StatsWin_ToggleDbStats 0]
}

## ---------------------------------------------------------------------------
## Paint the pie which reflects the current database percentages
##
proc DbStatsWin_PaintPie {wname val1exp val1cur val1all val1total} {

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

#   if {$val1total < 359.9} {
#      # paint the slice of stream 2 in blue
#      $wname.browser.pie create arc 1 1 127 127 -start $val1total -extent [expr 359.999 - $val1total] -fill blue
#
#      if {$val2exp > 0} {
#         # mark expired and defective percentage in yellow
#         $wname.browser.pie create arc 1 1 127 127 -start $val1total -extent [expr $val2exp - $val1total] -fill yellow -outline {} -stipple gray75
#      }
#      if {$val2cur < $val2all} {
#         # mark percentage of old version in shaded blue
#         $wname.browser.pie create arc 1 1 127 127 -start $val2cur -extent [expr $val2all - $val2cur] -fill #483D8B -outline {} -stipple gray50
#      }
#      if {$val2all < 359.9} {
#         # mark percentage of missing stream 1 data in slight blue
#         $wname.browser.pie create arc 1 1 127 127 -start $val2all -extent [expr 359.9 - $val2all] -fill #DDDDFF -outline {} -stipple gray75
#      }
#   }
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

