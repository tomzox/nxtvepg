#
#  Network selection dialog and management
#
#  Copyright (C) 1999-2011, 2020-2023 T. Zoerner
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
#    Implements a configuration dialog for network selection.
#
set netsel_popup 0

## ---------------------------------------------------------------------------
## Update network selection configuration after provider or AI change
## - note: all CNIs have to be in the format 0x%04X
## - generates mapping tables between AI order and user config order
##
proc UpdateProvCniTable {prov} {
   global netwop_ai2sel netwop_sel2ai

   # fetch CNI list from AI block in database
   set ailist [C_GetAiNetwopList "" {}]

   # fetch or initialize network selection
   set net_cf [C_GetProvCniConfig]
   set selist [lindex $net_cf 0]
   set suplist [lindex $net_cf 1]
   if {([llength $selist] == 0) && ([llength $suplist] == 0)} {
      set selist $ailist
   }

   set netwop_ai2sel {}
   set netwop_sel2ai {}
   set index 0
   foreach cni $ailist {
      set order($cni) $index
      incr index
   }

   set nlidx 0
   foreach cni $selist {
      if [info exists order($cni)] {
         # CNI still exists in actual AI -> add it
         lappend netwop_sel2ai $order($cni)
         set rev_order($cni) $nlidx
         # increment index (only when item is not deleted)
         incr nlidx
      }
   }

   # check for unreferenced CNIs and append them to the list
   foreach cni $ailist {
      if [info exists rev_order($cni)] {
         lappend netwop_ai2sel $rev_order($cni)
      } else {
         lappend netwop_ai2sel 255
      }
   }
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


#=LOAD=PopupNetwopSelection
#=LOAD=SelBoxCreate
#=LOAD=SelBoxShiftUpItem
#=LOAD=SelBoxShiftDownItem
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Network selection popup
##
proc PopupNetwopSelection {} {
   global entry_disabledforeground
   global netsel_popup netsel_provider
   global netsel_ailist netsel_selist netsel_names

   if {$netsel_popup == 0} {
      if {[C_IsDatabaseLoaded]} {
         CreateTransientPopup .netsel "Network Selection"
         set netsel_popup 1

         # remember provider to detect changes before saving
         set netsel_provider [C_GetProviderPath]

         # fetch CNI list from AI block in database
         # as a side effect this function stores all netwop names into the array netsel_names
         set netsel_ailist [C_GetAiNetwopList 0 netsel_names allmerged]

         # initialize list of user-selected CNIs
         set tmpl [C_GetProvCniConfig]
         if {([llength [lindex $tmpl 0]] == 0) && ([llength [lindex $tmpl 1]] == 0)} {
            set netsel_selist $netsel_ailist
         } else {
            set netsel_selist {}
            # filter out unknown CNIs (not part of the AI network table anymore)
            foreach cni [lindex $tmpl 0] {
               if {[info exists netsel_names($cni)]} {
                  lappend netsel_selist $cni
               }
            }
         }

         frame .netsel.lb
         SelBoxCreate .netsel.lb netsel_ailist netsel_selist netsel_names 20 1 \
                      "Available networks:" "Selected networks:"
         pack  .netsel.lb -side top -fill both -expand 1

         global cfnettimes netsel_times netsel_start netsel_stop netsel_cni
         global font_fixed
         labelframe .netsel.bottom -text "Configuration"
         label .netsel.bottom.lab_time -text "Air times:"
         grid  .netsel.bottom.lab_time -row 1 -column 0 -sticky w -padx 5
         frame .netsel.bottom.frm_time
         label .netsel.bottom.frm_time.lab_start -text "from"
         entry .netsel.bottom.frm_time.ent_start -textvariable netsel_start -font $font_fixed -width 5
         bind  .netsel.bottom.frm_time.ent_start <Enter> {SelectTextOnFocus %W}
         label .netsel.bottom.frm_time.lab_stop  -text "until"
         entry .netsel.bottom.frm_time.ent_stop  -textvariable netsel_stop -font $font_fixed -width 5
         bind  .netsel.bottom.frm_time.ent_stop  <Enter> {SelectTextOnFocus %W}
         pack  .netsel.bottom.frm_time.lab_start .netsel.bottom.frm_time.ent_start \
                                                 .netsel.bottom.frm_time.lab_stop \
                                                 .netsel.bottom.frm_time.ent_stop -side left -padx 5 -anchor w
         grid  .netsel.bottom.frm_time -row 1 -column 1 -sticky we -pady 2
         grid  columnconfigure .netsel.bottom 1 -weight 1
         pack  .netsel.bottom -side top -padx 10 -pady 5 -fill x

         frame  .netsel.cmd
         button .netsel.cmd.help -text "Help" -width 7 -command {PopupHelp $helpIndex(Configure menu) "Select networks"}
         button .netsel.cmd.abort -text "Abort" -width 7 -command {destroy .netsel}
         button .netsel.cmd.ok -text "Ok" -width 7 -command {SaveSelectedNetwopList} -default active
         pack  .netsel.cmd.help .netsel.cmd.abort .netsel.cmd.ok -side left -padx 10
         pack  .netsel.cmd -side top -pady 10

         bind  .netsel.lb.ai.ailist <<ListboxSelect>> [list + after idle [list NetselUpdateCni orig]]
         bind  .netsel.lb.sel.selist <<ListboxSelect>> [list + after idle [list NetselUpdateCni sel]]
         array unset netsel_times
         array set netsel_times [array get cfnettimes]
         set netsel_cni 0
         NetselUpdateCni orig

         bind .netsel <Key-F1> {PopupHelp $helpIndex(Configure menu) "Select networks"}
         bind .netsel <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]
         bind .netsel.cmd <Destroy> {+ set netsel_popup 0}
         bind .netsel.cmd.ok <Return> {tkButtonInvoke .netsel.cmd.ok}
         bind .netsel.cmd.ok <Escape> {tkButtonInvoke .netsel.cmd.ok}
         focus .netsel.cmd.ok

         wm resizable .netsel 1 1
         update
         wm minsize .netsel [winfo reqwidth .netsel] [winfo reqheight .netsel]
      } else {
         # no AI block in database
         tk_messageBox -type ok -default ok -icon error -parent . \
                       -message "You have to load an XMLTV file before configuring networks."
      }
   } else {
      raise .netsel
   }
}

# finished -> save and apply the user selection
proc SaveSelectedNetwopList {} {
   global netsel_selist netsel_ailist netsel_names
   global cfnettimes netsel_times netsel_provider

   if {$netsel_provider != [C_GetProviderPath]} {
      tk_messageBox -type ok -default ok -icon error -parent .netsel \
                    -message "Provider has changed - cannot save."
      return
   }

   # close popup
   destroy .netsel
   update

   # make list of CNIs from AI which are missing in the user selection
   set sup {}
   foreach cni $netsel_ailist {
      if {[lsearch -exact $netsel_selist $cni] == -1} {
         lappend sup $cni
      }
   }
   C_UpdateProvCniConfig $netsel_selist $sup

   # store start & stop time of the last CNI and store the air times list
   NetselUpdateTimes
   array unset cfnettimes
   array set cfnettimes [array get netsel_times]
   array unset netsel_times
   array unset netsel_names

   # merged database: merge the requested networks
   if {[C_IsDatabaseMerged]} {
      C_ProvMerge_Start
   }

   # save list into rc/ini file
   UpdateRcFile

   # update network mapping table AI->selection and reverse
   # fill network filter menus according to the new network list
   # set network pre-filters
   # remove pre-filtered networks from the browser, or add them back
   C_UpdateNetwopList
}

# callback for selection in left or right list -> update description at the dialog bottom
proc NetselUpdateCni {which} {
   global netsel_times netsel_start netsel_stop netsel_cni
   global netsel_ailist netsel_selist 

   # save the start and stop times of the previously selected CNI
   NetselUpdateTimes

   # get the currently selected CNI
   set cni 0
   if {[string compare $which "orig"] == 0} {
      set nl [.netsel.lb.ai.ailist curselection]
      if {[llength $nl] == 1} {
         set cni [lindex $netsel_ailist $nl]
      }
   } else {
      set nl [.netsel.lb.sel.selist curselection]
      if {[llength $nl] == 1} {
         set cni [lindex $netsel_selist $nl]
      }
   }
   set netsel_cni $cni

   if {$cni != 0} {
      if [info exists netsel_times($cni)] {
         scan $netsel_times($cni) "%d,%d" start stop
         set netsel_start [format "%02d:%02d" [expr int($start / 60)] [expr $start % 60]]
         set netsel_stop [format "%02d:%02d" [expr int($stop / 60)] [expr $stop % 60]]
      } else {
         set netsel_start "00:00"
         set netsel_stop  "00:00"
      }
   } else {
      set netsel_start {}
      set netsel_stop {}
   }
}

# save the start and stop times of the currently selected CNI
proc NetselUpdateTimes {} {
   global netsel_cni netsel_times netsel_start netsel_stop

   if {$netsel_cni != 0} {
      if { ([scan $netsel_start "%u:%u" s1 s2] == 2) &&
           ([scan $netsel_stop "%u:%u" s3 s4] == 2) } {
         if {$s1 > 23} {set s1 0}
         if {$s2 > 59} {set s1 59}
         if {$s3 > 23} {set s3 0}
         if {$s4 > 59} {set s4 59}
         set s1 [expr (60 * $s1) + $s2]
         set s3 [expr (60 * $s3) + $s4]

         if {($s1 == $s3) || \
             ($s1 == (($s3 + 1) % 1440))} {
            # range 00:00 - 00:00  OR  00:00 - 23:59 -> remove restriction
            if [info exists netsel_times($netsel_cni)] {
               unset netsel_times($netsel_cni)
            }
         } else {
            set netsel_times($netsel_cni) [format "%d,%d" $s1 $s3]
         }
      }
   }
}

##  --------------------------------------------------------------------------
##  Helper functions for selecting and ordering list items
##
proc SelBoxCreate {lbox arr_ailist arr_selist arr_names lwidth do_scrollbar lab_ai lab_sel} {
   upvar $arr_ailist ailist
   upvar $arr_selist selist
   upvar $arr_names names

   # determine listbox height and check if scrollbar is required
   set lbox_height [llength $ailist]
   if {[llength $selist] > $lbox_height} {
      set lbox_height [llength $selist]
   }
   #set do_scrollbar 0
   if {$lbox_height > 20} {
      set lbox_height 20
      set do_scrollbar 1
   } elseif {$lbox_height < 4} {
      # must not set height 0 - else listbox height is unbound in case of later updates
      set lbox_height 4
   }

   ## first column: listbox with all netwops in AI order
   frame $lbox.ai
   label $lbox.ai.lab -text $lab_ai
   grid $lbox.ai.lab -row 0 -column 0 -columnspan 2 -sticky we
   scrollbar $lbox.ai.sb -orient vertical -command [list $lbox.ai.ailist yview] -takefocus 0
   if {$do_scrollbar} {
      grid $lbox.ai.sb -row 1 -column 0 -sticky ns
   }
   listbox $lbox.ai.ailist -exportselection false -height $lbox_height -width $lwidth \
                           -selectmode extended -yscrollcommand [list $lbox.ai.sb set]
   relief_listbox $lbox.ai.ailist
   grid $lbox.ai.ailist -row 1 -column 1 -sticky news
   grid columnconfigure $lbox.ai 1 -weight 1
   grid rowconfigure $lbox.ai 1 -weight 1
   pack $lbox.ai -anchor nw -side left -pady 10 -padx 10 -fill both -expand 1
   bind $lbox.ai.ailist <<ListboxSelect>> [list + after idle [list SelBoxButtonPress $lbox orig]]
   bind $lbox.ai.ailist <Return> [list tkButtonInvoke $lbox.cmd.add]
   bind $lbox.ai.ailist <Enter> {focus %W}

   ## second column: command buttons
   frame $lbox.cmd
   button $lbox.cmd.add -text "add" -command [list SelBoxAddItem $lbox $arr_ailist $arr_selist $arr_names] -width 7
   pack $lbox.cmd.add -side top -anchor c -pady 5
   frame $lbox.cmd.updown
   button $lbox.cmd.updown.up -bitmap "bitmap_ptr_up" -command [list SelBoxShiftUpItem $lbox.sel.selist $arr_selist $arr_names]
   pack $lbox.cmd.updown.up -side left -fill x -expand 1
   button $lbox.cmd.updown.down -bitmap "bitmap_ptr_down" -command [list SelBoxShiftDownItem $lbox.sel.selist $arr_selist $arr_names]
   pack $lbox.cmd.updown.down -side left -fill x -expand 1
   pack $lbox.cmd.updown -side top -anchor c -fill x
   button $lbox.cmd.delnet -text "delete" -command [list SelBoxRemoveItem $lbox $arr_selist] -width 7
   pack $lbox.cmd.delnet -side top -anchor c -pady 5
   pack $lbox.cmd -side left -anchor nw -pady 10 -padx 5 -fill y

   ## third column: selected providers in selected order
   frame $lbox.sel
   label $lbox.sel.lab -text $lab_sel
   grid $lbox.sel.lab -row 0 -column 0 -columnspan 2 -sticky we
   scrollbar $lbox.sel.sb -orient vertical -command [list $lbox.sel.selist yview] -takefocus 0
   listbox $lbox.sel.selist -exportselection false -height $lbox_height -width $lwidth \
                            -selectmode extended -yscrollcommand [list $lbox.sel.sb set]
   relief_listbox $lbox.sel.selist
   if {$do_scrollbar} {
      grid $lbox.sel.sb -row 1 -column 0 -sticky ns
   }
   grid $lbox.sel.selist -row 1 -column 1 -sticky news
   grid columnconfigure $lbox.sel 1 -weight 1
   grid rowconfigure $lbox.sel 1 -weight 1
   pack $lbox.sel -anchor nw -side left -pady 10 -padx 10 -fill both -expand 1
   bind $lbox.sel.selist <<ListboxSelect>> [list + after idle [list SelBoxButtonPress $lbox sel]]
   bind $lbox.sel.selist <Key-Delete> [list tkButtonInvoke $lbox.cmd.delnet]
   bind $lbox.sel.selist <Control-Key-Up> [concat tkButtonInvoke $lbox.cmd.updown.up {;} break]
   bind $lbox.sel.selist <Control-Key-Down> [concat tkButtonInvoke $lbox.cmd.updown.down {;} break]
   bind $lbox.sel.selist <Enter> {focus %W}

   # fill the listboxes
   foreach item $ailist { $lbox.ai.ailist insert end $names($item) }
   foreach item $selist { $lbox.sel.selist insert end $names($item) }

   # initialize command button state
   # (all disabled until an item is selected from either the left or right list)
   event generate $lbox.sel.selist <<ListboxSelect>>
}

# replace the complete contents of the AI list
proc SelBoxUpdateList {lbox arr_ailist arr_selist arr_names} {
   upvar $arr_ailist ailist
   upvar $arr_selist selist
   upvar $arr_names names

   $lbox.ai.ailist delete 0 end

   foreach item $ailist {
      $lbox.ai.ailist insert end $names($item)
   }

   set prev_height [$lbox.ai.ailist cget -height]
   set lbox_height [llength $ailist]
   if {[llength $selist] > $lbox_height} {
      set lbox_height [llength $selist]
   }
   if {$lbox_height > $prev_height} {
      set do_scrollbar 0
      if {$lbox_height > 20} {
         set lbox_height 20
         set do_scrollbar 1
      }
      $lbox.ai.ailist configure -height $lbox_height
      $lbox.sel.selist configure -height $lbox_height
      if {$do_scrollbar} { grid $lbox.ai.sb -row 1 -column 0 -sticky ns }
      if {$do_scrollbar} { grid $lbox.sel.sb -row 1 -column 0 -sticky ns }
   }
}

# selected items in the AI CNI list are appended to the selection list
proc SelBoxAddItem {lbox arr_ailist arr_selist arr_names} {
   upvar $arr_ailist ailist
   upvar $arr_selist selist
   upvar $arr_names names

   foreach index [$lbox.ai.ailist curselection] {
      set cni [lindex $ailist $index]
      if {[lsearch -exact $selist $cni] == -1} {
         # append the selected item to the right listbox
         lappend selist $cni
         $lbox.sel.selist insert end $names($cni)
         # select the newly inserted item
         $lbox.sel.selist selection set end
      }
   }

   # update command button states and clear selection in the left listbox
   event generate $lbox.sel.selist <<ListboxSelect>>
}

# all selected items are removed from the list
proc SelBoxRemoveItem {lbox arr_selist} {
   upvar $arr_selist selist

   foreach index [lsort -integer -decreasing [$lbox.sel.selist curselection]] {
      $lbox.sel.selist delete $index
      set selist [lreplace $selist $index $index]
   }

   # update command button states (no selection -> all disabled)
   event generate $lbox.sel.selist <<ListboxSelect>>
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
   if {[llength [info commands $lbox]] == 0} {
      return
   }
   # clear the selection in the opposite listbox
   if {[string compare $which "orig"] == 0} {
      $lbox.sel.selist selection clear 0 end
   } else {
      $lbox.ai.ailist selection clear 0 end
   }

   # selection in the left box <--> "add" enabled
   if {[llength [$lbox.ai.ailist curselection]] > 0} {
      $lbox.cmd.add configure -state normal
   } else {
      $lbox.cmd.add configure -state disabled
   }

   # selection in the right box <--> "delete" & "shift up/down" enabled
   if {[llength [$lbox.sel.selist curselection]] > 0} {
      $lbox.cmd.updown.up configure -state normal
      $lbox.cmd.updown.down configure -state normal
      $lbox.cmd.delnet configure -state normal
   } else {
      $lbox.cmd.updown.up configure -state disabled
      $lbox.cmd.updown.down configure -state disabled
      $lbox.cmd.delnet configure -state disabled
   }
}

