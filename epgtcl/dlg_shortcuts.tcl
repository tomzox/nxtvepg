#
#  Configuration dialogs for filter shortcuts
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
#    Implements configuration dialogs which allow to manage user-defined
#    filter shortcuts.
#
#  Author: Tom Zoerner
#
#  $Id: dlg_shortcuts.tcl,v 1.6 2002/12/08 19:59:00 tom Exp tom $
#
set fscupd_popup 0

set fscedit_label ""
set fscedit_popup 0

##  --------------------------------------------------------------------------
##  Append tags of hidden shortcuts
##
proc CompleteShortcutOrder {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global shortcuts shortcut_order

   set ltags $shortcut_order
   foreach sc_tag [lsort -integer [array names shortcuts]] {
      if [lindex $shortcuts($sc_tag) $fsc_hide_idx] {
         lappend ltags $sc_tag
      }
   }
   return $ltags
}

#=LOAD=UpdateFilterShortcut
#=LOAD=UpdateFilterShortcutByContext
#=LOAD=DeleteFilterShortcut
#=LOAD=AddFilterShortcut
#=LOAD=EditFilterShortcuts
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Compare settings of two shortcuts: return TRUE if identical
##  - NOTE: tags are not compared because they always differ between shortcuts
##  - NOTE: filter setting is not compared properly (see below)
##
proc CompareShortcuts {sc_a sc_b} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx

   set result 0
   # compare label
   if {[string compare [lindex $sc_a $fsc_name_idx] [lindex $sc_b $fsc_name_idx]] == 0} {
      # compare combination logic mode
      if {[string compare [lindex $sc_a $fsc_logi_idx] [lindex $sc_b $fsc_logi_idx]] == 0} {
         # compare mask
         if {[lsort [lindex $sc_a $fsc_mask_idx]] == [lsort [lindex $sc_b $fsc_mask_idx]]} {
            # compare invert list
            if {[lsort [lindex $sc_a $fsc_inv_idx]] == [lsort [lindex $sc_b $fsc_inv_idx]]} {
               # compare 'hide from main list' flag
               if {[lindex $sc_a $fsc_hide_idx] == [lindex $sc_b $fsc_hide_idx]} {
                  # compare filter settings
                  # NOTE: would need to be sorted for exact comparison!
                  #       currently not required, hence not implemented
                  if {[lindex $sc_a $fsc_filt_idx] == [lindex $sc_b $fsc_filt_idx]} {
                     set result 1
                  }
               }
            }
         }
      }
   }
   return $result
}

##  --------------------------------------------------------------------------
##  Ask for confirmation to update a shortcut via the context menu
##
proc UpdateFilterShortcutByContext {edit_idx} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global shortcuts shortcut_order

   set new_filt [DescribeCurrentFilter]
   set sc_tag [lindex $shortcut_order $edit_idx]

   # check if any filters are set at all
   if {[llength [lindex $new_filt 0]] == 0} {
      set answer [tk_messageBox -type okcancel -icon warning -parent .all.shortcuts.list \
                     -message "Currently no filters selected. Do you still want to continue?"]
      if {[string compare $answer "cancel"] == 0} {
         return
      }
   } elseif {([lindex $new_filt 1] == [lindex $shortcuts($sc_tag) $fsc_filt_idx]) && \
             ([lindex $new_filt 2] == [lindex $shortcuts($sc_tag) $fsc_inv_idx])} {
      tk_messageBox -type ok -icon info -parent .all.shortcuts.list \
                    -message "The current filter setting is identical to the one stored with the selected shortcut."
      return
   }

   set answer [tk_messageBox -type okcancel -default ok -icon warning -parent .all.shortcuts.list \
                 -message "Are you sure want to overwrite shortcut '[lindex $shortcuts($sc_tag) $fsc_name_idx]' with the current filter settings?"]
   if {[string compare $answer ok] == 0} {
      SaveUpdatedFilterShortcut {} $edit_idx
   }
}

##  --------------------------------------------------------------------------
##  Open "Update filter shortcut" dialog window
##
proc UpdateFilterShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global shortcuts shortcut_order fsc_prevselection
   global fscupd_order
   global text_bg
   global fscupd_popup
   global fscedit_popup fscedit_order fscedit_sclist

   # check if any filters are set at all
   if {[llength [lindex [DescribeCurrentFilter] 0]] == 0} {
      set answer [tk_messageBox -type okcancel -icon warning -parent . \
                     -message "Currently no filters selected. Do you still want to continue?"]
      if {[string compare $answer "cancel"] == 0} {
         return
      }
   }

   if {$fscupd_popup == 0} {
      CreateTransientPopup .fscupd "Update shortcut"
      wm resizable .fscupd 1 1
      set fscupd_popup 1

      message .fscupd.msg -text "Please select which shortcut shall be updated with the current filter setting:" \
                          -aspect 520 -borderwidth 2 -relief ridge -background $text_bg -pady 5 -anchor w
      pack .fscupd.msg -side top -fill x

      ## first column: listbox with all shortcut labels
      frame     .fscupd.sc_list
      scrollbar .fscupd.sc_list.sb -orient vertical -command {.fscupd.sc_list.lb yview} -takefocus 0
      pack      .fscupd.sc_list.sb -side left -fill y
      listbox   .fscupd.sc_list.lb -exportselection false -height 12 -width 0 -selectmode single \
                                   -relief ridge -yscrollcommand {.fscupd.sc_list.sb set}
      pack      .fscupd.sc_list.lb -side left -fill both -expand 1
      pack      .fscupd.sc_list -side left -pady 10 -padx 10 -fill both -expand 1

      ## second column: command buttons and entry widget to rename shortcuts
      frame .fscupd.cmd
      button .fscupd.cmd.update -text "Update" -command {SaveUpdatedFilterShortcut no} -width 12
      button .fscupd.cmd.edit -text "Update & Edit" -command {SaveUpdatedFilterShortcut yes} -width 12
      button .fscupd.cmd.abort -text "Abort" -command {destroy .fscupd} -width 12
      button .fscupd.cmd.help -text "Help" -command {PopupHelp $helpIndex(Filter shortcuts)} -width 12
      pack .fscupd.cmd.help .fscupd.cmd.abort .fscupd.cmd.edit .fscupd.cmd.update -side bottom -anchor sw
      pack .fscupd.cmd -side left -anchor nw -pady 10 -padx 5 -anchor s

      bind .fscupd.cmd <Destroy> {+ set fscupd_popup 0}
      bind .fscupd <Key-F1> {PopupHelp $helpIndex(Filter shortcuts)}

   } else {
      raise .fscupd
      .fscupd.sc_list.lb delete 0 end
   }

   # fill the listbox with all shortcut labels
   set fscupd_order [CompleteShortcutOrder]
   if $fscedit_popup {
      # save any changed settings in the edit dialog (because we access the data below)
      CheckShortcutUpdatePending
      # append new, yet unsaved shortcuts
      foreach sc_tag $fscedit_order {
         if {[lsearch -exact $fscupd_order $sc_tag] == -1} {
            lappend fscupd_order $sc_tag
         }
      }
      foreach sc_tag $fscupd_order {
         .fscupd.sc_list.lb insert end [lindex $fscedit_sclist($sc_tag) $fsc_name_idx]
      }
   } else {
      # edit dialog not open -> use names from the main list
      foreach sc_tag $fscupd_order {
         .fscupd.sc_list.lb insert end [lindex $shortcuts($sc_tag) $fsc_name_idx]
      }
   }
   set lb_height [llength $fscupd_order]
   if {$lb_height > 25} {set lb_height 25}
   .fscupd.sc_list.lb configure -height $lb_height

   # preselect the shortcut currently in use in the browser
   if {[info exists fsc_prevselection] && ([llength $fsc_prevselection] > 0)} {
      set sel [lsearch -exact $fscupd_order [lindex $fsc_prevselection 0]]
   } else {
      set sel -1
   }
   if {$sel != -1} {
      .fscupd.sc_list.lb selection set $sel
      .fscupd.sc_list.lb see $sel
   }

   update
   wm minsize .fscupd [winfo reqwidth .fscupd] [winfo reqheight .fscupd]
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
proc SaveUpdatedFilterShortcut {call_edit {edit_idx -1}} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global shortcuts shortcut_order
   global fscupd_order fscedit_sclist fscedit_order
   global fscupd_popup fscedit_popup fscedit_idx

   if $fscupd_popup {
      set sel [.fscupd.sc_list.lb curselection]
      if {([llength $sel] == 1) && ($sel < [llength $fscupd_order])} {
         set sc_tag [lindex $fscupd_order $sel]
      }
      set parent .fscupd
   } else {
      set sc_tag [lindex $shortcut_order $edit_idx]
      set parent .
   }

   if [info exists sc_tag] {
      # determine current filter settings (discard mask)
      set new_sc [DescribeCurrentFilter]
      set copy_mask 0

      # compare filter categories before and after
      set old_ids [GetShortcutIdentList [lindex $shortcuts($sc_tag) $fsc_filt_idx]]
      set new_ids [GetShortcutIdentList [lindex $new_sc 1]]
      if {$old_ids != $new_ids} {
         # different categories -> ask user if he's sure he doesn't want to edit the mask
         set answer [tk_messageBox -icon warning -type yesnocancel -default yes -parent $parent \
                        -message "The current settings include different filter categories than the selected shortcut. Do you want to automatically adapt the filter mask?"]
         if {[string compare $answer cancel] == 0} {
            return
         } elseif {[string compare $answer yes] == 0} {
            set copy_mask 1
         }
      }

      if {([string compare $call_edit yes] == 0) || $fscedit_popup} {
         # invoke the "Edit filter shortcuts" dialog

         # close the popup window
         # (note: need to close before edit window is opened or tvtwm will crash)
         if $fscupd_popup {
            destroy .fscupd
         }

         if {$fscedit_popup == 0} {
            # dialog is not yet open -> do initial setup

            # copy the shortcuts into a temporary array
            if {[info exists fscedit_sclist]} {unset fscedit_sclist}
            array set fscedit_sclist [array get shortcuts]
            set fscedit_order [CompleteShortcutOrder]

            set fscedit_sclist($sc_tag) [lreplace $fscedit_sclist($sc_tag) $fsc_filt_idx $fsc_filt_idx [lindex $new_sc 1]]
            if $copy_mask {
               set fscedit_sclist($sc_tag) [lreplace $fscedit_sclist($sc_tag) $fsc_mask_idx $fsc_mask_idx [lindex $new_sc 0]]
            }
            set sel [lsearch -exact $fscedit_order $sc_tag]

         } else {
            # dialog already open

            # save any changed settings in the edit dialog (because we change the selection below)
            CheckShortcutUpdatePending

            # search for the shortcut in the edited list
            if [info exists fscedit_sclist($sc_tag)] {
               set fscedit_sclist($sc_tag) [lreplace $fscedit_sclist($sc_tag) $fsc_filt_idx $fsc_filt_idx [lindex $new_sc 1]]
               if $copy_mask {
                  set fscedit_sclist($sc_tag) [lreplace $fscedit_sclist($sc_tag) $fsc_mask_idx $fsc_mask_idx [lindex $new_sc 0]]
               }
               set sel [lsearch -exact $fscedit_order $sc_tag]
            } else {
               # not found in list (deleted by user in temporary list) -> append
               set fscedit_sclist($sc_tag) [lreplace $shortcuts($sc_tag) $fsc_filt_idx $fsc_filt_idx [lindex $new_sc 1]]
               set fscedit_order [linsert $fscedit_order 0 $sc_tag]
               set sel 0
            }
         }

         # create the popup (if neccessary) and fill the listbox with all shortcuts
         PopupFilterShortcuts
         # select the updated shortcut in the listbox
         if {$sel != -1} {
            .fscedit.list selection set $sel
            .fscedit.list see $sel
         }
         SelectEditedShortcut

      } else {
         if [info exists shortcuts($sc_tag)] {
            # update the selected shortcut
            set shortcuts($sc_tag) [lreplace $shortcuts($sc_tag) $fsc_filt_idx $fsc_filt_idx [lindex $new_sc 1]]

            # update filter & column cache with user-defined columns, then redisplay
            DownloadUserDefinedColumnFilters
            C_PiOutput_CfgColumns
            C_RefreshPiListbox

            # save the shortcuts config into the rc/ini file
            UpdateRcFile
         } else {
            tk_messageBox -icon error -type ok -parent $parent \
                          -message "The selected shortcut no longer exists - please add the filter as a new shortcut."
         }

         # close the popup window
         if $fscupd_popup {
            destroy $parent
         }
      }
   } else {
      tk_messageBox -icon error -type ok -parent $parent \
                    -message "Please select the shortcut which should be updated with the current filter setting in the list at the left of the dialog window."
   }
}

##  --------------------------------------------------------------------------
##  Delete a shortcut via the context menu in the main window's shortcut list
##
proc DeleteFilterShortcut {edit_idx} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global shortcuts shortcut_order
   global fscedit_sclist fscedit_order fscedit_idx
   global fscedit_popup

   set sc_tag [lindex $shortcut_order $edit_idx]

   if $fscedit_popup {
      # search for the shortcut in the edited list
      if [info exists fscedit_sclist($sc_tag)] {
         set sel [lsearch -exact $fscedit_order $sc_tag]
         if {$sel != -1} {
            # raise the popup dialog window
            raise .fscedit

            # save any changed settings in the edit dialog (because we change the selection below)
            CheckShortcutUpdatePending

            .fscedit.list selection set $sel
            .fscedit.list see $sel

            DeleteEditedShortcut
         }
      } else {
         raise .fscedit
         tk_messageBox -type ok -icon error -parent .fscedit \
                       -message "Shortcut '[lindex $shortcuts($sc_tag) $fsc_name_idx]' is already deleted in the edited shortcut list. Quit the dialog with save to copy the changed list into the main window."
      }
   } else {
      set answer [tk_messageBox -type okcancel -default ok -icon warning -parent . \
                    -message "Are you sure you want to irrecoverably delete shortcut '[lindex $shortcuts($sc_tag) $fsc_name_idx]'?"]
      if {[string compare $answer ok] == 0} {
         # remove the element from the global variables
         set shortcut_order [lreplace $shortcut_order $edit_idx $edit_idx]
         unset shortcuts($sc_tag)
         # remove the element from listbox in the main window
         .all.shortcuts.list delete $edit_idx
         .all.shortcuts.list configure -height [llength $shortcut_order]
      }
   }
}

##  --------------------------------------------------------------------------
##  Popup window to add the current filter setting as new shortcut
##
proc AddFilterShortcut {{edit_idx -1}} {
   global shortcuts shortcut_order
   global fscedit_desc
   global fscedit_sclist fscedit_order
   global fscedit_popup

   if {$fscedit_popup == 0} {
      # copy the shortcuts into a temporary array
      if {[info exists fscedit_sclist]} {unset fscedit_sclist}
      array set fscedit_sclist [array get shortcuts]
      set fscedit_order [CompleteShortcutOrder]
   }

   # determine current filter settings and a default mask
   set temp [DescribeCurrentFilter]
   set mask [lindex $temp 0]
   set filt [lindex $temp 1]
   set inv  [lindex $temp 2]
   set name "new shortcut"
   set sc_tag  [GenerateShortcutTag]

   # check if any filters are set at all
   if {[string length $mask] == 0} {
      set answer [tk_messageBox -type okcancel -icon warning -message "Currently no filters selected. Do you still want to continue?"]
      if {[string compare $answer "cancel"] == 0} {
         return
      }
   }

   # store the new shortcut
   set fscedit_sclist($sc_tag) [list $name $mask $filt $inv merge 0]

   # append the new shortcut or insert it at the requested position
   if {($edit_idx < 0) || ($edit_idx > [llength $fscedit_order])} {
      set edit_idx [llength $fscedit_order]
      lappend fscedit_order $sc_tag
   } else {
      # increment index so that the new element is placed after the selected element
      incr edit_idx
      set fscedit_order [linsert $fscedit_order $edit_idx $sc_tag]
   }

   # create the popup and fill the shortcut listbox
   PopupFilterShortcuts

   # select the new entry in the listbox
   .fscedit.list selection set $edit_idx
   .fscedit.list see $edit_idx
   SelectEditedShortcut
}

##  --------------------------------------------------------------------------
##  Popup window to edit the shortcut list
##
proc EditFilterShortcuts {{edit_idx -1}} {
   global shortcuts shortcut_order
   global fscedit_sclist fscedit_order
   global fscedit_idx
   global fscedit_popup
   global fsc_prevselection

   if {$fscedit_popup == 0} {

      # copy the shortcuts into a temporary array
      if [info exists fscedit_sclist] {unset fscedit_sclist}
      array set fscedit_sclist [array get shortcuts]
      set fscedit_order [CompleteShortcutOrder]

      # create the popup and fill the shortcut listbox
      PopupFilterShortcuts

      # select the currently used shortcut in the list, or the first if none selected
      set sel -1
      if {($edit_idx != -1) && ([llength $shortcut_order] > 0)} {
         set sel [lsearch -exact $fscedit_order [lindex $shortcut_order $edit_idx]]
      } elseif {[info exists fsc_prevselection] && ([llength $fsc_prevselection] > 0)} {
         set sel [lsearch -exact $fscedit_order [lindex $fsc_prevselection 0]]
      }
      if {$sel == -1} {
         set sel 0
      }
      .fscedit.list selection set $sel
      .fscedit.list see $sel

      # display the definition for the selected shortcut
      SelectEditedShortcut

   } else {
      raise .fscedit

      if {($edit_idx != -1) && ([llength $shortcut_order] > 0)} {
         set sel [lsearch -exact $fscedit_order [lindex $shortcut_order $edit_idx]]
         if {$sel != -1} {
            .fscedit.list selection clear 0 end
            .fscedit.list selection set $sel
            SelectEditedShortcut
         }
      }
   }
}

##  --------------------------------------------------------------------------
##  Filter shortcut configuration pop-up window
##
proc PopupFilterShortcuts {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global fscedit_sclist fscedit_order
   global fscedit_desc fscedit_mask fscedit_hide fscedit_label fscedit_logic fscedit_idx
   global font_normal
   global pi_attr_labels
   global fscedit_popup

   # initialize all state variables
   if {[info exists fscedit_desc]} {unset fscedit_desc}
   if {[info exists fscedit_mask]} {unset fscedit_mask}
   if {[info exists fscedit_label]} {unset fscedit_label}
   if {[info exists fscedit_logic]} {set fscedit_logic 0}
   if {[info exists fscedit_hide]} {set fscedit_hide 0}
   # set invalid index since no item is selected in the listbox yet (must be done by caller)
   set fscedit_idx -1

   if {$fscedit_popup == 0} {
      CreateTransientPopup .fscedit "Edit shortcut list"
      set fscedit_popup 1

      ## first column: listbox with all shortcut labels
      scrollbar .fscedit.list_sb -orient vertical -command {.fscedit.list yview} -takefocus 0
      listbox   .fscedit.list -exportselection false -height 12 -width 0 -selectmode single \
                              -relief ridge -yscrollcommand {.fscedit.list_sb set}
      bind .fscedit.list <Enter> {focus %W}
      bind .fscedit.list <<ListboxSelect>> {+ SelectEditedShortcut}
      bind .fscedit.list <Control-Key-Up> [concat tkButtonInvoke .fscedit.cmd.updown.up {;} break]
      bind .fscedit.list <Control-Key-Down> [concat tkButtonInvoke .fscedit.cmd.updown.down {;} break]
      bind .fscedit.list <Key-Delete> [list tkButtonInvoke .dscedit.cmd.delete]
      pack .fscedit.list_sb .fscedit.list -side left -pady 10 -fill y

      ## second column: command buttons
      frame .fscedit.cmd
      frame .fscedit.cmd.updown
      button .fscedit.cmd.updown.up -bitmap "bitmap_ptr_up" -command ShiftUpEditedShortcut
      pack .fscedit.cmd.updown.up -side left -fill x -expand 1
      button .fscedit.cmd.updown.down -bitmap "bitmap_ptr_down" -command ShiftDownEditedShortcut
      pack .fscedit.cmd.updown.down -side left -fill x -expand 1
      pack .fscedit.cmd.updown -side top -anchor nw -fill x
      button .fscedit.cmd.delete -text "Delete" -command DeleteEditedShortcut -width 7
      pack .fscedit.cmd.delete -side top -anchor nw

      frame .fscedit.cmd.filt
      button .fscedit.cmd.filt.invoke -text "Invoke" -command InvokeEditedShortcutFilter -width 7
      button .fscedit.cmd.filt.update -text "Update" -command UpdateEditedShortcutFilter -width 7
      pack .fscedit.cmd.filt.invoke .fscedit.cmd.filt.update -side top
      pack .fscedit.cmd.filt -side top -pady 20

      button .fscedit.cmd.save -text "Save" -command SaveEditedShortcuts -width 7
      button .fscedit.cmd.help -text "Help" -command {PopupHelp $helpIndex(Filter shortcuts)} -width 7
      button .fscedit.cmd.abort -text "Abort" -command {AbortEditedShortcuts} -width 7
      pack .fscedit.cmd.abort .fscedit.cmd.help .fscedit.cmd.save -side bottom -anchor sw

      pack .fscedit.cmd -side left -anchor nw -pady 10 -padx 5 -fill y
      bind .fscedit.cmd <Destroy> {+ set fscedit_popup 0}
      bind .fscedit <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]
      bind .fscedit <Key-F1> {PopupHelp $helpIndex(Filter shortcuts)}

      ## third column: attributes
      frame .fscedit.flags
      label .fscedit.flags.lab_name -text "Label:"
      grid  .fscedit.flags.lab_name -row 0 -column 0 -sticky w
      entry .fscedit.flags.ent_name -textvariable fscedit_label -width 15
      grid  .fscedit.flags.ent_name -row 0 -column 1 -sticky we
      bind  .fscedit.flags.ent_name <Enter> {SelectTextOnFocus %W}
      bind  .fscedit.flags.ent_name <Return> CheckShortcutUpdatePending
      focus .fscedit.flags.ent_name

      label .fscedit.flags.ld -text "Filter setting:"
      grid  .fscedit.flags.ld -row 1 -column 0 -sticky w
      frame .fscedit.flags.desc
      text  .fscedit.flags.desc.tx -width 35 -height 12 -wrap none -font $font_normal -insertofftime 0 \
                                   -yscrollcommand {.fscedit.flags.desc.sc_vert set} \
                                   -xscrollcommand {.fscedit.flags.sc_hor set}
      # remove all bindings that allow to modify the text
      bindtags .fscedit.flags.desc.tx {.fscedit.flags.desc.tx TextReadOnly . all}
      pack .fscedit.flags.desc.tx -side left -fill both -expand 1
      scrollbar .fscedit.flags.desc.sc_vert -orient vertical -command {.fscedit.flags.desc.tx yview} -takefocus 0
      pack .fscedit.flags.desc.sc_vert -side left -fill y
      grid .fscedit.flags.desc -row 2 -column 0 -columnspan 2 -sticky news
      scrollbar .fscedit.flags.sc_hor -orient horizontal -command {.fscedit.flags.desc.tx xview} -takefocus 0
      grid .fscedit.flags.sc_hor -row 3 -column 0 -columnspan 2 -sticky ew
      grid  columnconfigure .fscedit.flags 1 -weight 1
      grid  rowconfigure .fscedit.flags 2 -weight 1

      label .fscedit.flags.lab_mask -text "Filter mask:"
      grid  .fscedit.flags.lab_mask -row 4 -column 0 -sticky w
      menubutton .fscedit.flags.mb_mask -text "Select" -menu .fscedit.flags.mb_mask.men \
                                        -underline 0 -direction flush -relief raised -borderwidth 2
      menu  .fscedit.flags.mb_mask.men -tearoff 0
      grid  .fscedit.flags.mb_mask -row 4 -column 1 -sticky we -pady 5

      # fill filter mask menu
      foreach filt {features parental editorial progidx timsel dursel themes series \
                    sortcrits netwops substr vps_pdc invert_all} {
         .fscedit.flags.mb_mask.men add checkbutton -label $pi_attr_labels($filt) -variable fscedit_mask($filt)
      }

      checkbutton .fscedit.flags.cb_hide -text "Hide in main window list" -variable fscedit_hide
      grid  .fscedit.flags.cb_hide -row 5 -column 0 -columnspan 2 -sticky w


      #label .fscedit.flags.ll -text "Combination with other shortcuts:"
      #pack .fscedit.flags.ll -side top -anchor w
      #frame .fscedit.flags.logic -relief ridge -bd 1
      #radiobutton .fscedit.flags.logic.merge -text "merge" -variable fscedit_logic -value "merge"
      #radiobutton .fscedit.flags.logic.or -text "logical OR" -variable fscedit_logic -value "or" -state disabled
      #radiobutton .fscedit.flags.logic.and -text "logical AND" -variable fscedit_logic -value "and" -state disabled
      #pack .fscedit.flags.logic.merge .fscedit.flags.logic.or .fscedit.flags.logic.and -side top -anchor w -padx 5
      #pack .fscedit.flags.logic -side top -pady 10 -fill x
      pack .fscedit.flags -side left -pady 10 -padx 10 -expand 1 -fill both
   } else {
      raise .fscedit
   }

   # fill the listbox with all shortcut labels
   .fscedit.list delete 0 end
   foreach sc_tag $fscedit_order {
      .fscedit.list insert end [lindex $fscedit_sclist($sc_tag) $fsc_name_idx]
   }
   set lb_height [llength $fscedit_order]
   if {$lb_height > 25} {set lb_height 25}
   .fscedit.list configure -height $lb_height

   wm resizable .fscedit 1 1
   update
   wm minsize .fscedit [winfo reqwidth .fscedit] [winfo reqheight .fscedit]
}

# "Invoke" command button: apply the filter settings to the main window
proc InvokeEditedShortcutFilter {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global shortcuts shortcut_order fsc_prevselection
   global fscedit_sclist fscedit_order

   # store settings of the currently selected shortcut
   CheckShortcutUpdatePending

   set sel [.fscedit.list curselection]
   if {([llength $sel] == 1) && ($sel < [llength $fscedit_order])} {
      set sc_tag [lindex $fscedit_order $sel]

      # clear all filter settings
      ResetFilterState
      .all.shortcuts.list selection clear 0 end

      # if the shortcut exists in the global list and has the same filters, select it
      set idx [lsearch -exact $shortcut_order $sc_tag]
      if {($idx != -1) && [CompareShortcuts $fscedit_sclist($sc_tag) $shortcuts($sc_tag)]} {
         .all.shortcuts.list selection set $idx
         set fsc_prevselection $sc_tag
      }

      SelectShortcuts $sc_tag fscedit_sclist
      C_RefreshPiListbox
   }
}

# "Update" command button: load the filter settings from the main window into the selected shortcut
proc UpdateEditedShortcutFilter {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global fscedit_sclist fscedit_order
   global fscedit_idx

   set new_sc [DescribeCurrentFilter]

   # check if any filters are set at all
   if {[llength [lindex $new_sc 0]] == 0} {
      set answer [tk_messageBox -type okcancel -icon warning -parent .fscedit \
                     -message "Currently no filters selected in the browser. Do you still want to replace this shortcut's filters (with nothing)?"]
      if {[string compare $answer "cancel"] == 0} {
         return
      }
   }

   # store settings of the currently selected shortcut
   CheckShortcutUpdatePending

   set sel [.fscedit.list curselection]
   if {([llength $sel] == 1) && ($sel < [llength $fscedit_order])} {
      set sc_tag [lindex $fscedit_order $sel]

      set old_ids [GetShortcutIdentList [lindex $fscedit_sclist($sc_tag) $fsc_filt_idx]]
      set new_ids [GetShortcutIdentList [lindex $new_sc 1]]
      if {$old_ids != $new_ids} {
         # different categories -> ask user if he's sure he doesn't want to edit the mask
         set answer [tk_messageBox -icon warning -type yesnocancel -default yes -parent .fscedit \
                            -message "The current settings include different filter categories than the selected shortcut. Do you want to automatically adapt the filter mask?"]
         if {[string compare $answer cancel] == 0} {
            return
         } elseif {[string compare $answer yes] == 0} {
            # replace the mask
            set fscedit_sclist($sc_tag) [lreplace $fscedit_sclist($sc_tag) $fsc_mask_idx $fsc_mask_idx \
                                                  [lindex $new_sc 0]]
         }
      }

      # replace the filter setting of the selected shortcut
      set fscedit_sclist($sc_tag) [lreplace $fscedit_sclist($sc_tag) $fsc_filt_idx $fsc_filt_idx \
                                            [lindex $new_sc 1]]

      # display the new filter setting
      set fscedit_idx -1
      SelectEditedShortcut
   }
}

# "Abort" command button - ask to confirm before changes are lost
proc AbortEditedShortcuts {} {
   global shortcuts shortcut_order
   global fscedit_sclist fscedit_order

   # store settings of the currently selected shortcut
   CheckShortcutUpdatePending

   # check if shortcuts were added or removed
   if {[array size fscedit_sclist] != [array size shortcuts]} {
      set changed 1
   } else {
      # compare all shortcuts
      set changed 0
      foreach sc_tag [array names fscedit_sclist] {
         if { ![info exists shortcuts($sc_tag)] || \
              ([CompareShortcuts $fscedit_sclist($sc_tag) $shortcuts($sc_tag)] == 0) } {
            set changed 1
            break
         }
      }
      if {$changed == 0} {
         # check if the shortcut order was changed
         for {set idx 0} {$idx < [llength $shortcut_order]} {incr idx} {
            if {[lindex $fscedit_order $idx] != [lindex $shortcut_order $idx]} {
               set changed 1
               break
            }
         }
      }
   }
   if {$changed} {
      set answer [tk_messageBox -type okcancel -icon warning -parent .fscedit \
                    -message "Discard all changes?"]
      if {[string compare $answer cancel] == 0} {
         return
      }
   }
   destroy .fscedit
}

# "Save" command button - copy the temporary array onto the global shortcuts
proc SaveEditedShortcuts {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global fscedit_sclist fscedit_order
   global shortcuts shortcut_order

   # store settings of the currently selected shortcut
   CheckShortcutUpdatePending

   # copy the temporary array back into the shortcuts array
   unset shortcuts
   array set shortcuts [array get fscedit_sclist]
   set shortcut_order {}
   foreach sc_tag $fscedit_order {
      if {[lindex $fscedit_sclist($sc_tag) $fsc_hide_idx] == 0} {
         lappend shortcut_order $sc_tag
      }
   }

   # close the popup window
   destroy .fscedit

   # save the shortcuts config into the rc/ini file
   UpdateRcFile

   # update the shortcut listbox
   .all.shortcuts.list delete 0 end
   foreach sc_tag $shortcut_order {
      .all.shortcuts.list insert end [lindex $shortcuts($sc_tag) $fsc_name_idx]
   }
   .all.shortcuts.list configure -height [llength $shortcut_order]

   # update filter & column cache with user-defined columns, then redisplay
   DownloadUserDefinedColumnFilters
   C_PiOutput_CfgColumns
   C_RefreshPiListbox
}

# selection of a shortcut in the listbox - update all displayed info in the popup
proc SelectEditedShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global fscedit_label fscedit_mask fscedit_hide fscedit_logic fscedit_idx
   global fscedit_sclist fscedit_order

   # store settings of the previously selected shortcut
   CheckShortcutUpdatePending

   set sel [.fscedit.list curselection]
   if {([llength $sel] == 1) && ($sel < [llength $fscedit_order])} {
      # remember the index of the currently selected shortcut
      set fscedit_idx $sel
      set sc_tag [lindex $fscedit_order $sel]

      # set label displayed in entry widget
      set fscedit_label [lindex $fscedit_sclist($sc_tag) $fsc_name_idx]
      .fscedit.flags.ent_name selection range 0 end

      # display description
      .fscedit.flags.desc.tx delete 1.0 end
      .fscedit.flags.desc.tx insert end [ShortcutPrettyPrint [lindex $fscedit_sclist($sc_tag) $fsc_filt_idx] \
                                                             [lindex $fscedit_sclist($sc_tag) $fsc_inv_idx]]

      # set combination logic radiobuttons
      set fscedit_logic [lindex $fscedit_sclist($sc_tag) $fsc_logi_idx]
      set fscedit_hide [lindex $fscedit_sclist($sc_tag) $fsc_hide_idx]

      # set mask checkbuttons
      if {[info exists fscedit_mask]} {unset fscedit_mask}
      foreach index [lindex $fscedit_sclist($sc_tag) $fsc_mask_idx] {
         set fscedit_mask($index) 1
      }
   }
}

# Subroutine: Collect shortcut settings from entry and checkbutton widgets
proc GetUpdatedShortcut {new_sc} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global fscedit_sclist fscedit_order
   global fscedit_mask fscedit_hide fscedit_logic fscedit_label

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

   # update hide flag
   set new_sc [lreplace $new_sc $fsc_hide_idx $fsc_hide_idx $fscedit_hide]

   # update description label from entry widget
   if {[string length $fscedit_label] > 0} {
      set new_sc [lreplace $new_sc $fsc_name_idx $fsc_name_idx $fscedit_label]
   }

   return $new_sc
}

# helper function: store (possibly modified) settings of the selected shortcut
proc CheckShortcutUpdatePending {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global fscedit_sclist fscedit_order
   global fscedit_idx

   if {$fscedit_idx != -1} {
      set sc_tag [lindex $fscedit_order $fscedit_idx]

      set fscedit_sclist($sc_tag) [GetUpdatedShortcut $fscedit_sclist($sc_tag)]

      # update the label in the shortcut listbox
      set old_sel [.fscedit.list curselection]
      .fscedit.list delete $fscedit_idx
      .fscedit.list insert $fscedit_idx [lindex $fscedit_sclist($sc_tag) $fsc_name_idx]

      if {$old_sel == $fscedit_idx} {
         # put the cursor onto the updated item again
         .fscedit.list selection set $fscedit_idx
      }
   }
}

# "Delete" command button
proc DeleteEditedShortcut {} {
   global fscedit_sclist fscedit_order
   global fscedit_idx

   set sel [.fscedit.list curselection]
   if {([llength $sel] == 1) && ($sel < [llength $fscedit_order])} {
      set sc_tag [lindex $fscedit_order $sel]
      set fscedit_order [lreplace $fscedit_order $sel $sel]
      unset fscedit_sclist($sc_tag)

      set fscedit_idx -1
      .fscedit.list delete $sel
      if {$sel < [llength $fscedit_order]} {
         .fscedit.list selection set $sel
         SelectEditedShortcut
      } elseif {[llength $fscedit_order] > 0} {
         .fscedit.list selection set [expr $sel - 1]
         SelectEditedShortcut
      }
   }
}

# "Up-Arrow" command button
proc ShiftUpEditedShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global fscedit_sclist fscedit_order
   global fscedit_idx

   set index [.fscedit.list curselection]
   if {([llength $index] == 1) && ($index > 0)} {
      # remove the item in the listbox widget above the shifted one
      .fscedit.list delete [expr $index - 1]
      # re-insert the just removed item below the shifted one
      set tag_2 [lindex $fscedit_order [expr $index - 1]]
      .fscedit.list insert $index [lindex $fscedit_sclist($tag_2) $fsc_name_idx]

      # perform the same exchange in the associated list
      set fscedit_order [lreplace $fscedit_order [expr $index - 1] $index \
                           [lindex $fscedit_order $index] \
                           [lindex $fscedit_order [expr $index - 1]]]
      incr fscedit_idx -1
   }

}

# "Down-Arrow" command button
proc ShiftDownEditedShortcut {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global fscedit_sclist fscedit_order
   global fscedit_idx

   set index [.fscedit.list curselection]
   if {([llength $index] == 1) && \
       ($index < [expr [llength $fscedit_order] - 1])} {

      .fscedit.list delete [expr $index + 1]
      set tag_2 [lindex $fscedit_order [expr $index + 1]]
      .fscedit.list insert $index [lindex $fscedit_sclist($tag_2) $fsc_name_idx]
      set col [expr [lindex $fscedit_sclist($tag_2) $fsc_hide_idx] ? {"#602222"} : {"black"}]

      set fscedit_order [lreplace $fscedit_order $index [expr $index + 1] \
                           [lindex $fscedit_order [expr $index + 1]] \
                           [lindex $fscedit_order $index]]
      incr fscedit_idx
   }
}


