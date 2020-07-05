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
#  $Id: dlg_shortcuts.tcl,v 1.19 2020/07/05 19:01:22 tom Exp tom $
#
# import constants from other modules
#=INCLUDE=  "epgtcl/shortcuts.h"

set fscupd_popup 0

set fscedit_label ""
set fscedit_popup 0


#=LOAD=UpdateFilterShortcut
#=LOAD=UpdateFilterShortcutByContext
#=LOAD=DeleteFilterShortcut
#=LOAD=AddFilterShortcut
#=LOAD=EditFilterShortcuts
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Apply shortcut changes -> save in rc/ini file and notify other modules
##
proc ApplyShortcutChanges {} {

   # save changed shortcut config into the rc/ini file
   UpdateRcFile

   # update filter & column cache with user-defined columns
   DownloadUserDefinedColumnFilters
   UpdatePiListboxColumParams
   # refresh PI listbox in case shortcut is used for column attributes
   if {$::pibox_type != 0} {
      C_PiNetBox_Invalidate
   }
   C_PiBox_Refresh

   # re-calculate time of next event in case shortcuts are used in reminders
   Reminder_UpdateTimer

   # notify user-def column dialog (in case it's open, to add new shortcuts)
   UserCols_ShortcutsChanged
}

##  --------------------------------------------------------------------------
##  Compare settings of two shortcuts: return TRUE if identical
##  - NOTE: tags are not compared because they always differ between shortcuts
##  - NOTE: filter setting is not compared properly (see below)
##
proc CompareShortcuts {sc_a sc_b} {

   set result 0
   # compare label
   if {[string compare [lindex $sc_a $::fsc_name_idx] [lindex $sc_b $::fsc_name_idx]] == 0} {
      # compare combination logic mode
      if {[string compare [lindex $sc_a $::fsc_logi_idx] [lindex $sc_b $::fsc_logi_idx]] == 0} {
         # compare node param
         if {[string compare [lindex $sc_a $::fsc_node_idx] [lindex $sc_b $::fsc_node_idx]] == 0} {
            # compare mask
            if {[lsort [lindex $sc_a $::fsc_mask_idx]] == [lsort [lindex $sc_b $::fsc_mask_idx]]} {
               # compare invert list
               if {[lsort [lindex $sc_a $::fsc_inv_idx]] == [lsort [lindex $sc_b $::fsc_inv_idx]]} {
                  # compare filter settings
                  # NOTE: would need to be sorted for exact comparison!
                  #       currently not required, hence not implemented
                  if {[lindex $sc_a $::fsc_filt_idx] == [lindex $sc_b $::fsc_filt_idx]} {
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
proc UpdateFilterShortcutByContext {sc_tag} {
   global shortcuts

   if [info exists shortcuts($sc_tag)] {

      set new_filt [DescribeCurrentFilter]

      # check if any filters are set at all
      if {[llength [lindex $new_filt $::scdesc_mask_idx]] == 0} {
         set answer [tk_messageBox -type okcancel -icon warning -parent .all.shortcuts.list \
                        -message "Currently no filters selected. Do you still want to continue?"]
         if {[string compare $answer "cancel"] == 0} {
            return
         }
      } elseif {([lindex $new_filt $::scdesc_filt_idx] == [lindex $shortcuts($sc_tag) $::fsc_filt_idx]) && \
                ([lindex $new_filt $::scdesc_inv_idx] == [lindex $shortcuts($sc_tag) $::fsc_inv_idx])} {
         tk_messageBox -type ok -icon info -parent .all.shortcuts.list \
                       -message "The current filter setting is identical to the one stored with the selected shortcut."
         return
      }

      set answer [tk_messageBox -type okcancel -default ok -icon warning -parent .all.shortcuts.list \
                    -message "Are you sure want to overwrite shortcut '[lindex $shortcuts($sc_tag) $::fsc_name_idx]' with the current filter settings?"]
      if {[string compare $answer ok] == 0} {
         SaveUpdatedFilterShortcut {} $sc_tag
      }
   }
}

##  --------------------------------------------------------------------------
##  Open "Update filter shortcut" dialog window
##
proc UpdateFilterShortcut {} {
   global shortcuts shortcut_tree fsc_prevselection
   global text_fg text_bg sctree_font sctree_selfg sctree_selbg
   global fscupd_popup fscupd_tree
   global fscedit_popup fscedit_tree fscedit_sclist

   # check if any filters are set at all
   if {[llength [lindex [DescribeCurrentFilter] $::scdesc_mask_idx]] == 0} {
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
                          -aspect 520 -borderwidth 2 -relief groove \
                          -foreground $text_fg -background $text_bg -pady 5 -padx 1 -anchor w
      pack .fscupd.msg -side top -fill x

      ## first column: listbox with all shortcut labels
      frame     .fscupd.sc_list
      scrollbar .fscupd.sc_list.sb -orient vertical -command {.fscupd.sc_list.lb yview} -takefocus 0
      pack      .fscupd.sc_list.sb -side left -fill y
      Tree:create .fscupd.sc_list.lb -width 0 -maxheight 25 -minheight 12 -minwidth 5 \
                                     -font $sctree_font -selectbackground $sctree_selbg \
                                     -selectforeground $sctree_selfg -cursor top_left_arrow \
                                     -foreground $text_fg -background $text_bg \
                                     -selectmode browse -yscrollcommand {.fscupd.sc_list.sb set}
      relief_listbox .fscupd.sc_list.lb
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
   }

   # fill the listbox with all shortcut labels
   set fscupd_tree $shortcut_tree
   if $fscedit_popup {
      # save any changed settings in the edit dialog (because we access the data below)
      CheckShortcutUpdatePending

      ShortcutTree_Fill .fscupd.sc_list.lb {} $fscupd_tree shortcuts 0
      .fscupd.cmd.update configure -state disabled

   } else {
      # edit dialog not open -> use names from the main list
      ShortcutTree_Fill .fscupd.sc_list.lb {} $fscupd_tree shortcuts 0
   }

   # preselect the shortcut currently in use in the browser
   if {[info exists fsc_prevselection] && ([llength $fsc_prevselection] > 0)} {
      set sc_tag [lindex $fsc_prevselection 0]
   } else {
      set sc_tag -1
   }
   if {$sc_tag != -1} {
      Tree:selection .fscupd.sc_list.lb set $sc_tag
      Tree:see .fscupd.sc_list.lb $sc_tag
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
         default {
            lappend idl $ident
         }
      }
   }
   return $idl
}

# "Update" and "Update & Edit" command buttons
proc SaveUpdatedFilterShortcut {call_edit {sc_tag -1}} {
   global shortcuts shortcut_tree
   global fscupd_tree fscedit_sclist fscedit_tree
   global fscupd_popup fscedit_popup fscedit_tag

   if {($sc_tag == -1) && $fscupd_popup} {
      set sc_tag [ShortcutTree_Curselection .fscupd.sc_list.lb]
      if {([llength $sc_tag] != 1) || ![info exists shortcuts($sc_tag)]} {
         set sc_tag -1
      }
      set parent .fscupd
   } else {
      set parent .
   }

   if {$sc_tag != -1} {
      # determine current filter settings (discard mask)
      set new_sc [DescribeCurrentFilter]
      set copy_mask 0

      # compare filter categories before and after
      set old_ids [GetShortcutIdentList [lindex $shortcuts($sc_tag) $::fsc_filt_idx]]
      set new_ids [GetShortcutIdentList [lindex $new_sc $::scdesc_filt_idx]]
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
            set fscedit_tree $shortcut_tree

            set elem $fscedit_sclist($sc_tag)
            set elem [lreplace $elem $::fsc_filt_idx $::fsc_filt_idx [lindex $new_sc $::scdesc_filt_idx]]
            set elem [lreplace $elem $::fsc_inv_idx $::fsc_inv_idx [lindex $new_sc $::scdesc_inv_idx]]
            if $copy_mask {
               set elem [lreplace $elem $::fsc_mask_idx $::fsc_mask_idx [lindex $new_sc $::scdesc_mask_idx]]
            }
            set fscedit_sclist($sc_tag) $elem

         } else {
            # dialog already open

            # search for the shortcut in the edited list
            if [info exists fscedit_sclist($sc_tag)] {

               set elem $fscedit_sclist($sc_tag)
               set elem [lreplace $elem $::fsc_filt_idx $::fsc_filt_idx [lindex $new_sc $::scdesc_filt_idx]]
               set elem [lreplace $elem $::fsc_inv_idx $::fsc_inv_idx [lindex $new_sc $::scdesc_inv_idx]]
               if $copy_mask {
                  set elem [lreplace $elem $::fsc_mask_idx $::fsc_mask_idx [lindex $new_sc $::scdesc_mask_idx]]
               }
               set fscedit_sclist($sc_tag) $elem

            } else {
               # not found in list (deleted by user in temporary list) -> insert at top
               set elem $fscedit_sclist($sc_tag)
               set elem [lreplace $elem $::fsc_filt_idx $::fsc_filt_idx [lindex $new_sc $::scdesc_filt_idx]]
               set elem [lreplace $elem $::fsc_inv_idx $::fsc_inv_idx [lindex $new_sc $::scdesc_inv_idx]]
               set fscedit_sclist($sc_tag) $elem

               set fscedit_tree [linsert $fscedit_tree 0 $sc_tag]
            }
         }

         # create the popup (if neccessary) and fill the listbox with all shortcuts
         PopupFilterShortcuts
         # select the updated shortcut in the listbox
         Tree:selection .fscedit.list set $sc_tag
         Tree:see .fscedit.list $sc_tag

         SelectEditedShortcut

      } else {
         # update the selected shortcut without opening the edit dialog

         if [info exists shortcuts($sc_tag)] {
            set elem $shortcuts($sc_tag)
            set elem [lreplace $elem $::fsc_filt_idx $::fsc_filt_idx [lindex $new_sc $::scdesc_filt_idx]]
            set elem [lreplace $elem $::fsc_inv_idx $::fsc_inv_idx [lindex $new_sc $::scdesc_inv_idx]]
            set shortcuts($sc_tag) $elem

            # save shortcuts into the rc/ini & notify other modules
            ApplyShortcutChanges

         } else {
            tk_messageBox -icon error -type ok -parent $parent \
                          -message "The selected shortcut no longer exists - please add the filter as a new shortcut."
         }

         # close the popup window
         if $fscupd_popup {
            destroy .fscupd
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
proc DeleteFilterShortcut {sc_tag} {
   global shortcuts shortcut_tree
   global fscedit_sclist fscedit_tree fscedit_tag
   global fscedit_popup

   set sc_name [lindex $shortcuts($sc_tag) $::fsc_name_idx]
   set node [lindex $shortcuts($sc_tag) $::fsc_node_idx]
   switch -glob -- [lindex $shortcuts($sc_tag) $::fsc_node_idx] {
      separator {set sc_name "this separator"}
      ?dir {set sc_name "folder '$sc_name'"}
      default {set sc_name "shortcut '$sc_name'"}
   }

   if $fscedit_popup {
      # store settings of the currently selected shortcut
      CheckShortcutUpdatePending

      # search for the shortcut in the edited list
      if [info exists fscedit_sclist($sc_tag)] {
         # raise the popup dialog window
         raise .fscedit

         # save any changed settings in the edit dialog (because we change the selection below)
         CheckShortcutUpdatePending

         Tree:selection .fscedit.list set $sc_tag
         Tree:see .fscedit.list $sc_tag

         ShortcutTree_DeleteSelected

      } else {
         raise .fscedit
         tk_messageBox -type ok -icon error -parent .fscedit \
                       -message "$sc_name is already deleted in the edited shortcut list. Quit the dialog with save to copy the changed list into the main window."
      }
   } else {
      set answer [tk_messageBox -type okcancel -default ok -icon warning -parent .all.shortcuts.list \
                    -message "Are you sure you want to irrecoverably delete $sc_name?"]
      if {[string compare $answer ok] == 0} {
         # remove the element from the global variables
         ShortcutTree_Delete .all.shortcuts.list $sc_tag shortcuts shortcut_tree

         # save shortcuts into the rc/ini & notify other modules
         ApplyShortcutChanges
      }
   }
}

##  --------------------------------------------------------------------------
##  Menu command to add the current filter setting as new shortcut
##
proc AddFilterShortcut {{prev_sc_tag -1}} {
   global shortcuts shortcut_tree
   global fscedit_desc
   global fscedit_sclist fscedit_tree
   global fscedit_popup

   if {$fscedit_popup == 0} {
      # copy the shortcuts into temporary shortcut edit array
      array set fscedit_sclist [array get shortcuts]
      set fscedit_tree $shortcut_tree
   }

   # determine current filter settings and a default mask
   set temp [DescribeCurrentFilter]
   set mask [lindex $temp $::scdesc_mask_idx]
   set filt [lindex $temp $::scdesc_filt_idx]
   set inv  [lindex $temp $::scdesc_inv_idx]
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
   set fscedit_sclist($sc_tag) [list $name $mask $filt $inv merge {}]

   # create the popup and fill the shortcut listbox
   PopupFilterShortcuts

   # append the new shortcut or insert it at the requested position
   if {$prev_sc_tag == -1} {
      lappend fscedit_tree $sc_tag
   } else {
      ShortcutTree_InsertRec .fscedit.list $sc_tag $prev_sc_tag fscedit_sclist fscedit_tree
   }
   ShortcutTree_Fill .fscedit.list {} $fscedit_tree fscedit_sclist 1

   # select the new entry in the listbox
   Tree:selection .fscedit.list clear first end
   Tree:selection .fscedit.list set $sc_tag
   Tree:see .fscedit.list $sc_tag
   SelectEditedShortcut
}

##  --------------------------------------------------------------------------
##  Popup window to edit the shortcut list
##
proc EditFilterShortcuts {{sc_tag -1}} {
   global shortcuts shortcut_tree
   global fscedit_sclist fscedit_tree
   global fscedit_tag
   global fscedit_popup
   global fsc_prevselection

   if {$fscedit_popup == 0} {

      # copy the shortcuts into a temporary array
      array set fscedit_sclist [array get shortcuts]
      set fscedit_tree $shortcut_tree

      # create the popup and fill the shortcut listbox
      PopupFilterShortcuts

      # select the currently used shortcut in the list, or the first if none selected
      if {($sc_tag != -1) && [info exists shortcuts($sc_tag)]} {
         # use given tag
      } elseif {[info exists fsc_prevselection] && ([llength $fsc_prevselection] > 0)} {
         set sc_tag [lindex $fsc_prevselection 0]
         if {![info exists shortcuts($sc_tag)]} {
            set sc_tag -1
         }
      }
      if {$sc_tag == -1} {
         set sc_tag [lindex $shortcut_tree 0]
      }
      Tree:selection .fscedit.list set $sc_tag
      Tree:see .fscedit.list $sc_tag

      # display the definition for the selected shortcut
      SelectEditedShortcut

   } else {
      raise .fscedit

      if {$sc_tag != -1} {
         Tree:selection .fscedit.list clear first end
         Tree:selection .fscedit.list set $sc_tag
         Tree:see .fscedit.list $sc_tag
         SelectEditedShortcut
      }
   }
}

##  --------------------------------------------------------------------------
##  Filter shortcut configuration pop-up window
##
proc PopupFilterShortcuts {} {
   global fscedit_sclist fscedit_tree
   global fscedit_desc fscedit_mask fscedit_label fscedit_logic fscedit_tag
   global font_normal text_fg text_bg sctree_font sctree_selfg sctree_selbg
   global pi_attr_labels
   global fscedit_popup

   if {$fscedit_popup == 0} {
      CreateTransientPopup .fscedit "Edit shortcut list"
      set fscedit_popup 1

      # initialize all state variables
      if {[info exists fscedit_desc]} {unset fscedit_desc}
      if {[info exists fscedit_mask]} {unset fscedit_mask}
      if {[info exists fscedit_label]} {unset fscedit_label}
      if {[info exists fscedit_logic]} {set fscedit_logic 0}
      # set invalid index since no item is selected in the listbox yet (must be done by caller)
      set fscedit_tag -1

      ## first column: listbox with all shortcut labels
      scrollbar .fscedit.list_sb -orient vertical -command {.fscedit.list yview} -takefocus 0
      Tree:create .fscedit.list -width 0 -maxheight 25 -minwidth 5 -cursor top_left_arrow \
                                -font $sctree_font -selectbackground $sctree_selbg \
                                -selectforeground $sctree_selfg \
                                -foreground $text_fg -background $text_bg \
                                -selectmode browse -yscrollcommand {.fscedit.list_sb set}
      relief_listbox .fscedit.list
      bind .fscedit.list <Enter> {focus %W}
      bind .fscedit.list <<TreeSelect>> {+ SelectEditedShortcut}
      bind .fscedit.list <<TreeOpenClose>> {ShortcutTree_OpenCloseEvent .fscedit.list fscedit_sclist $fscedit_tree}
      bind .fscedit.list <Control-Key-Up> [concat tkButtonInvoke .fscedit.cmd.updown.up {;} break]
      bind .fscedit.list <Control-Key-Down> [concat tkButtonInvoke .fscedit.cmd.updown.down {;} break]
      bind .fscedit.list <Key-Delete> [list tkButtonInvoke .fscedit.cmd.delete]
      pack .fscedit.list_sb .fscedit.list -side left -pady 10 -fill y

      ## second column: command buttons
      frame  .fscedit.cmd
      frame  .fscedit.cmd.updown
      button .fscedit.cmd.updown.upleft -bitmap "bitmap_ptr_up_left" -command {ShortcutTree_Shift out}
      grid   .fscedit.cmd.updown.upleft -row 0 -column 0 -sticky we
      button .fscedit.cmd.updown.downright -bitmap "bitmap_ptr_down_right" -command {ShortcutTree_Shift in}
      grid   .fscedit.cmd.updown.downright -row 1 -column 0 -sticky we
      button .fscedit.cmd.updown.up -bitmap "bitmap_ptr_up" -command {ShortcutTree_Shift up}
      grid   .fscedit.cmd.updown.up -row 0 -column 1 -sticky we
      button .fscedit.cmd.updown.down -bitmap "bitmap_ptr_down" -command {ShortcutTree_Shift down}
      grid   .fscedit.cmd.updown.down -row 1 -column 1 -sticky we
      grid   columnconfigure .fscedit.cmd.updown 0 -weight 1
      grid   columnconfigure .fscedit.cmd.updown 1 -weight 1
      pack   .fscedit.cmd.updown -side top -anchor nw -fill x
      button .fscedit.cmd.delete -text "Delete" -command ShortcutTree_DeleteSelected -width 10
      pack   .fscedit.cmd.delete -side top -anchor nw
      button .fscedit.cmd.add_folder -text "New folder" -command {ShortcutTree_Add folder} -width 10
      pack   .fscedit.cmd.add_folder -side top -anchor nw
      button .fscedit.cmd.add_sep -text "New separator" -command {ShortcutTree_Add separator} -width 10
      pack   .fscedit.cmd.add_sep -side top -anchor nw

      frame .fscedit.cmd.filt
      button .fscedit.cmd.filt.invoke -text "Invoke" -command InvokeEditedShortcutFilter -width 7
      button .fscedit.cmd.filt.update -text "Update" -command UpdateEditedShortcutFilter -width 7
      pack .fscedit.cmd.filt.invoke .fscedit.cmd.filt.update -side top
      pack .fscedit.cmd.filt -side top -pady 20 -anchor w

      button .fscedit.cmd.save -text "Save" -command SaveEditedShortcuts -width 7
      button .fscedit.cmd.help -text "Help" -command {PopupHelp $helpIndex(Filter shortcuts)} -width 7
      button .fscedit.cmd.abort -text "Abort" -command {AbortEditedShortcuts} -width 7
      pack .fscedit.cmd.abort .fscedit.cmd.help .fscedit.cmd.save -side bottom -anchor sw

      pack .fscedit.cmd -side left -anchor nw -pady 10 -padx 5 -fill y
      bind .fscedit.cmd <Destroy> {+ set fscedit_popup 0}
      bind .fscedit <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]
      bind .fscedit <Key-F1> {PopupHelp $helpIndex(Filter shortcuts)}
      wm protocol .fscedit WM_DELETE_WINDOW AbortEditedShortcuts

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
                                        -indicatoron 1 -underline 0 -direction flush
      config_menubutton .fscedit.flags.mb_mask
      menu  .fscedit.flags.mb_mask.men -tearoff 0
      grid  .fscedit.flags.mb_mask -row 4 -column 1 -sticky we -pady 5

      # fill filter mask menu
      foreach filt {features parental editorial progidx timsel dursel themes \
                    netwops substr vps_pdc piexpire invert_all} {
         .fscedit.flags.mb_mask.men add checkbutton -label $pi_attr_labels($filt) -variable fscedit_mask($filt)
      }

      label .fscedit.flags.lab_logic -text "Combination rule:"
      grid  .fscedit.flags.lab_logic -row 5 -column 0 -sticky w
      menubutton .fscedit.flags.mb_logic -text "Select" -menu .fscedit.flags.mb_logic.men \
                                         -indicatoron 1 -underline 2 -direction flush
      config_menubutton .fscedit.flags.mb_logic
      grid  .fscedit.flags.mb_logic -row 5 -column 1 -sticky we
      menu  .fscedit.flags.mb_logic.men -tearoff 0
      .fscedit.flags.mb_logic.men add radiobutton -label "merge" -variable fscedit_logic -value "merge"
      .fscedit.flags.mb_logic.men add radiobutton -label "logical OR" -variable fscedit_logic -value "or"
      .fscedit.flags.mb_logic.men add radiobutton -label "logical AND" -variable fscedit_logic -value "and"
      pack .fscedit.flags -side left -pady 10 -padx 10 -expand 1 -fill both

   } else {
      raise .fscedit

      # store settings of the currently selected shortcut
      CheckShortcutUpdatePending
   }

   # fill the listbox with all shortcut labels
   ShortcutTree_Fill .fscedit.list {} $fscedit_tree fscedit_sclist 1

   wm resizable .fscedit 1 1
   update
   wm minsize .fscedit [winfo reqwidth .fscedit] [winfo reqheight .fscedit]
}

# "Invoke" command button: apply the filter settings to the main window
proc InvokeEditedShortcutFilter {} {
   global shortcuts shortcut_tree fsc_prevselection
   global fscedit_sclist fscedit_tree

   # store settings of the currently selected shortcut
   CheckShortcutUpdatePending

   set sc_tag [ShortcutTree_Curselection .fscedit.list]
   if {([llength $sc_tag] == 1) && [info exists fscedit_sclist($sc_tag)]} {

      # clear all filter settings
      ResetFilterState
      Tree:selection .all.shortcuts.list clear first end

      # if the shortcut exists in the global list and has the same filters, select it
      if {($sc_tag != -1) && [info exists shortcuts($sc_tag)] &&
          [CompareShortcuts $fscedit_sclist($sc_tag) $shortcuts($sc_tag)]} {
         Tree:selection .all.shortcuts.list set $sc_tag
         set fsc_prevselection $sc_tag
      }

      # copy shortcut into a temporary array to replace the combination logic with "merge"
      # so that filters are loaded into the editable parameter set
      set atmp($sc_tag) [lreplace $fscedit_sclist($sc_tag) $::fsc_logi_idx $::fsc_logi_idx merge]
      SelectShortcuts $sc_tag atmp
      C_PiBox_Refresh
   }
}

# "Update" command button: load the filter settings from the main window into the selected shortcut
proc UpdateEditedShortcutFilter {} {
   global fscedit_sclist fscedit_tree
   global fscedit_tag

   set new_sc [DescribeCurrentFilter]

   # check if any filters are set at all
   if {[llength [lindex $new_sc $::scdesc_mask_idx]] == 0} {
      set answer [tk_messageBox -type okcancel -icon warning -parent .fscedit \
                     -message "Currently no filters selected in the browser. Do you still want to replace this shortcut's filters (with nothing)?"]
      if {[string compare $answer "cancel"] == 0} {
         return
      }
   }

   # store settings of the currently selected shortcut
   CheckShortcutUpdatePending

   set sc_tag [ShortcutTree_Curselection .fscedit.list]
   if {([llength $sc_tag] == 1) && [info exists fscedit_sclist($sc_tag)]} {

      set old_ids [GetShortcutIdentList [lindex $fscedit_sclist($sc_tag) $::fsc_filt_idx]]
      set new_ids [GetShortcutIdentList [lindex $new_sc $::scdesc_filt_idx]]
      if {$old_ids != $new_ids} {
         # different categories -> ask user if he's sure he doesn't want to edit the mask
         set answer [tk_messageBox -icon warning -type yesnocancel -default yes -parent .fscedit \
                            -message "The current settings include different filter categories than the selected shortcut. Do you want to automatically adapt the filter mask?"]
         if {[string compare $answer cancel] == 0} {
            return
         } elseif {[string compare $answer yes] == 0} {
            # replace the mask
            set fscedit_sclist($sc_tag) [lreplace $fscedit_sclist($sc_tag) $::fsc_mask_idx $::fsc_mask_idx \
                                                  [lindex $new_sc $::scdesc_mask_idx]]
         }
      }

      # replace the filter setting of the selected shortcut
      set elem $fscedit_sclist($sc_tag)
      set elem [lreplace $elem $::fsc_filt_idx $::fsc_filt_idx [lindex $new_sc $::scdesc_filt_idx]]
      set elem [lreplace $elem $::fsc_inv_idx $::fsc_inv_idx [lindex $new_sc $::scdesc_inv_idx]]
      set fscedit_sclist($sc_tag) $elem

      # display the new filter setting
      set fscedit_tag -1
      SelectEditedShortcut
   }
}

# "Abort" command button - ask to confirm before changes are lost
proc AbortEditedShortcuts {} {
   global shortcuts shortcut_tree
   global fscedit_sclist fscedit_tree

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
         # check if the shortcut tree was changed
         if {$fscedit_tree != $shortcut_tree} {
            set changed 1
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
   global fscedit_sclist fscedit_tree
   global shortcuts shortcut_tree

   # store settings of the currently selected shortcut
   CheckShortcutUpdatePending

   # copy the temporary array back into the shortcuts array
   unset shortcuts
   array set shortcuts [array get fscedit_sclist]
   set shortcut_tree $fscedit_tree

   # close the popup window
   destroy .fscedit

   # update the shortcut listbox
   ShortcutTree_Fill .all.shortcuts.list {} $shortcut_tree shortcuts 0

   # save shortcuts into the rc/ini & notify other modules
   ApplyShortcutChanges
}

# selection of a shortcut in the listbox - update all displayed info in the popup
proc SelectEditedShortcut {} {
   global fscedit_label fscedit_mask fscedit_logic fscedit_tag
   global fscedit_sclist fscedit_tree

   # store settings of the previously selected shortcut
   CheckShortcutUpdatePending

   set sc_tag [ShortcutTree_Curselection .fscedit.list]
   if {([llength $sc_tag] == 1) && [info exists fscedit_sclist($sc_tag)]} {

      # remember the index of the currently selected shortcut
      set fscedit_tag $sc_tag

      # set label displayed in entry widget
      set fscedit_label [lindex $fscedit_sclist($sc_tag) $::fsc_name_idx]
      .fscedit.flags.ent_name selection range 0 end

      # display description
      .fscedit.flags.desc.tx delete 1.0 end
      .fscedit.flags.desc.tx insert end [ShortcutPrettyPrint [lindex $fscedit_sclist($sc_tag) $::fsc_filt_idx] \
                                                             [lindex $fscedit_sclist($sc_tag) $::fsc_inv_idx]]

      # set combination logic radiobuttons
      set fscedit_logic [lindex $fscedit_sclist($sc_tag) $::fsc_logi_idx]

      # set mask checkbuttons
      if {[info exists fscedit_mask]} {unset fscedit_mask}
      foreach index [lindex $fscedit_sclist($sc_tag) $::fsc_mask_idx] {
         set fscedit_mask($index) 1
      }
   }
}

# Subroutine: Collect shortcut settings from entry and checkbutton widgets
proc GetUpdatedShortcut {new_sc} {
   global fscedit_sclist fscedit_tree
   global fscedit_mask fscedit_logic fscedit_label

   # update filter mask from checkbuttons
   set mask {}
   foreach index [array names fscedit_mask] {
      if {$fscedit_mask($index) != 0} {
         lappend mask $index
      }
   }
   set new_sc [lreplace $new_sc $::fsc_mask_idx $::fsc_mask_idx $mask]

   # update combination logic setting
   set new_sc [lreplace $new_sc $::fsc_logi_idx $::fsc_logi_idx $fscedit_logic]

   # update description label from entry widget
   if {[string length $fscedit_label] > 0} {
      set new_sc [lreplace $new_sc $::fsc_name_idx $::fsc_name_idx $fscedit_label]
   }

   return $new_sc
}

# helper function: store (possibly modified) settings of the selected shortcut
proc CheckShortcutUpdatePending {} {
   global fscedit_sclist fscedit_tree
   global fscedit_tag

   if {$fscedit_tag != -1} {
      set sc_tag $fscedit_tag

      set fscedit_sclist($sc_tag) [GetUpdatedShortcut $fscedit_sclist($sc_tag)]

      # update the label in the shortcut listbox
      set old_sel_tag [ShortcutTree_Curselection .fscedit.list]

      Tree:itemconfigure .fscedit.list $sc_tag \
                         -label [lindex $fscedit_sclist($sc_tag) $::fsc_name_idx]

      if {$old_sel_tag == {}} {
         # put the cursor onto the updated item again
         Tree:selection .fscedit.list set $fscedit_tag
         Tree:see .fscedit.list $fscedit_tag
      }
   }
}

# "Delete" command button
proc ShortcutTree_DeleteSelected {} {
   global fscedit_sclist fscedit_tree
   global fscedit_tag

   set sc_tag [ShortcutTree_Curselection .fscedit.list]
   if {[llength $sc_tag] == 1} {

      set sc_order [Tree:unfold .fscedit.list /]
      set sc_idx [lsearch -exact $sc_order [Tree:labelat .fscedit.list $sc_tag]]

      ShortcutTree_Delete .fscedit.list $sc_tag fscedit_sclist fscedit_tree

      set fscedit_tag -1
      if {$sc_idx != -1} {
         for {set idx $sc_idx} {$idx < [llength $sc_order]} {incr idx} {
            set tag [file tail [lindex $sc_order $idx]]
            if [info exists fscedit_sclist($tag)] {
               set sel_tag $tag
               break
            }
         }
         if {![info exists sel_tag]} {
            for {set idx $sc_idx} {$idx >= 0} {incr idx -1} {
               set tag [file tail [lindex $sc_order $idx]]
               if [info exists fscedit_sclist($tag)] {
                  set sel_tag $tag
                  break
               }
            }
         }
         if {[info exists sel_tag]} {
            Tree:selection .fscedit.list set $sel_tag
            SelectEditedShortcut
         }
      }
   }
}

proc ShortcutTree_ShiftOutRec {w sc_tag sc_arr_ref sc_tree_ref elem_del_ref level} {
   upvar $sc_arr_ref sc_arr
   upvar $sc_tree_ref sc_tree
   upvar $elem_del_ref elem_del

   set idx 0
   foreach elem $sc_tree {
      if {([llength $elem] == 1) ? ($elem == $sc_tag) : ([lindex $elem 0] == $sc_tag)} {
         if {$level > 0} {
            # remove element from directory: caller must re-insert one level up
            set sc_tree [lreplace $sc_tree $idx $idx]
            set elem_del $elem
         } elseif {$idx >= 1} {
            # already at top level: simply exchange element with preceding one
            set elem_2 [lindex $sc_tree [expr $idx - 1]]
            set sc_tree [lreplace $sc_tree [expr $idx - 1] $idx $elem $elem_2]
            set elem_del $sc_tag
         }
         return 1
      } elseif {[llength $elem] > 1} {
         set ltree [lrange $elem 1 end]
         if [ShortcutTree_ShiftOutRec $w $sc_tag sc_arr ltree elem_del [expr $level + 1]] {
            # insert removed element here
            if {[string length $elem_del] > 0} {
               set sc_tree [lreplace $sc_tree $idx $idx $elem_del [concat [lindex $elem 0] $ltree]]
            } else {
               set sc_tree [lreplace $sc_tree $idx $idx [concat [lindex $elem 0] $ltree]]
            }
            set elem_del {}
            return 1
         }
      }
      incr idx
   }
   return 0
}

proc ShortcutTree_ShiftInRec {w sc_tag sc_arr_ref sc_tree_ref} {
   upvar $sc_arr_ref sc_arr
   upvar $sc_tree_ref sc_tree

   set idx 0
   foreach elem $sc_tree {
      if {([llength $elem] == 1) ? ($elem == $sc_tag) : ([lindex $elem 0] == $sc_tag)} {
         if {$idx + 1 < [llength $sc_tree]} {
            set elem_2 [lindex $sc_tree [expr $idx + 1]]
            if {([llength $elem_2] == 1) ?
                 [string match "?dir" [lindex $sc_arr($elem_2) $::fsc_node_idx]] :
                 [string match "?dir" [lindex $sc_arr([lindex $elem_2 0]) $::fsc_node_idx]]} {
               # sub-directory found: remove element and insert it at top of the sub-dir
               set sc_tree [lreplace $sc_tree $idx [expr $idx + 1] \
                                     [linsert $elem_2 1 $elem]]
            } else {
               # not a directory: simply exchange the two
               set sc_tree [lreplace $sc_tree $idx [expr $idx + 1] $elem_2 $elem]
            }
         }
         return 1
      } elseif {[llength $elem] > 1} {
         set ltree [lrange $elem 1 end]
         if [ShortcutTree_ShiftInRec $w $sc_tag sc_arr ltree] {
            set sc_tree [lreplace $sc_tree $idx $idx [concat [lindex $elem 0] $ltree]]
            return 1
         }
      }
      incr idx
   }
   return 0
}

proc ShortcutTree_ShiftUpRec {w sc_tag sc_arr_ref sc_tree_ref} {
   upvar $sc_arr_ref sc_arr
   upvar $sc_tree_ref sc_tree

   set idx 0
   foreach elem $sc_tree {
      if {([llength $elem] == 1) ? ($elem == $sc_tag) : ([lindex $elem 0] == $sc_tag)} {
         if {$idx >= 1} {
            set elem_2 [lindex $sc_tree [expr $idx - 1]]
            set sc_tree [lreplace $sc_tree [expr $idx - 1] $idx $elem $elem_2]
         }
         return 1
      } elseif {[llength $elem] > 1} {
         set ltree [lrange $elem 1 end]
         if [ShortcutTree_ShiftUpRec $w $sc_tag sc_arr ltree] {
            set sc_tree [lreplace $sc_tree $idx $idx [concat [lindex $elem 0] $ltree]]
            return 1
         }
      }
      incr idx
   }
   return 0
}

proc ShortcutTree_ShiftDownRec {w sc_tag sc_arr_ref sc_tree_ref} {
   upvar $sc_arr_ref sc_arr
   upvar $sc_tree_ref sc_tree

   set idx 0
   foreach elem $sc_tree {
      if {([llength $elem] == 1) ? ($elem == $sc_tag) : ([lindex $elem 0] == $sc_tag)} {
         if {$idx + 1 < [llength $sc_tree]} {
            set elem_2 [lindex $sc_tree [expr $idx + 1]]
            set sc_tree [lreplace $sc_tree $idx [expr $idx + 1] $elem_2 $elem]
         }
         return 1
      } elseif {[llength $elem] > 1} {
         set ltree [lrange $elem 1 end]
         if [ShortcutTree_ShiftDownRec $w $sc_tag sc_arr ltree] {
            set sc_tree [lreplace $sc_tree $idx $idx [concat [lindex $elem 0] $ltree]]
            return 1
         }
      }
      incr idx
   }
   return 0
}

# "Down-Arrow" command button
proc ShortcutTree_Shift {mode} {
   global fscedit_sclist fscedit_tree fscedit_tag

   # store settings of the currently selected shortcut
   CheckShortcutUpdatePending

   set sc_tag [ShortcutTree_Curselection .fscedit.list]
   if {([llength $sc_tag] == 1) && [info exists fscedit_sclist($sc_tag)]} {
      set fscedit_tag $sc_tag

      Tree:selection .fscedit.list clear $sc_tag

      switch -exact $mode {
         up {
            ShortcutTree_ShiftUpRec .fscedit.list $sc_tag fscedit_sclist fscedit_tree
         }
         down {
            ShortcutTree_ShiftDownRec .fscedit.list $sc_tag fscedit_sclist fscedit_tree
         }
         in {
            ShortcutTree_ShiftInRec .fscedit.list $sc_tag fscedit_sclist fscedit_tree
         }
         out {
            set elem_del {}
            ShortcutTree_ShiftOutRec .fscedit.list $sc_tag fscedit_sclist fscedit_tree elem_del 0
         }
      }
      ShortcutTree_Fill .fscedit.list {} $fscedit_tree fscedit_sclist 1

      Tree:selection .fscedit.list set $sc_tag
      Tree:see .fscedit.list $sc_tag
   }
}

proc ShortcutTree_DeleteDirRec {w sc_arr_ref sc_tree} {
   upvar $sc_arr_ref sc_arr

   foreach sc_tag $sc_tree {
      if {[llength $sc_tag] == 1} {
         catch {unset sc_arr($sc_tag)}
      } else {
         set dir_tag [lindex $sc_tag 0]
         if { [info exists sc_arr($dir_tag)] &&
             ([string match "?dir" [lindex $sc_arr($dir_tag) $::fsc_node_idx]] == 1)} {
            # use concat so that output is a flat list
            ShortcutTree_DeleteDirRec $w sc_arr [lrange $sc_tag 1 end]
         }
      }
   }
}

proc ShortcutTree_DeleteRec {w sc_tag sc_arr_ref sc_tree_ref} {
   upvar $sc_arr_ref sc_arr
   upvar $sc_tree_ref sc_tree

   set idx 0
   foreach elem $sc_tree {
      if {[llength $elem] == 1} {
         if {$elem == $sc_tag} {
            set sc_tree [lreplace $sc_tree $idx $idx]
            catch {unset sc_arr($sc_tag)}
            return 1
         }
      } else {
         set dir_tag [lindex $elem 0]
         if [info exists sc_arr($dir_tag)] {
            if {$dir_tag == $sc_tag} {
               if {[llength $elem] > 1} {
                  set answer [tk_messageBox -type okcancel -icon warning -parent $w \
                                 -message "Are you sure you want to delete this node inclusing all shortcuts inside?"]
                  if {[string compare $answer "ok"] != 0} {
                     return 1
                  }
                  ShortcutTree_DeleteDirRec $w sc_arr [lrange $elem 1 end]
               }
               set sc_tree [lreplace $sc_tree $idx $idx]
               catch {unset sc_arr($sc_tag)}
               return 1
            } else {
               set ltree [lrange $elem 1 end]
               if [ShortcutTree_DeleteRec $w $sc_tag sc_arr ltree] {
                  set sc_tree [lreplace $sc_tree $idx $idx [concat $dir_tag $ltree]]
                  return 1
               }
            }
         }
      }
      incr idx
   }
   return 0
}

proc ShortcutTree_Delete {w sc_tag sc_arr_ref sc_tree_ref} {
   upvar $sc_arr_ref sc_arr
   upvar $sc_tree_ref sc_tree

   Tree:selection $w clear $sc_tag

   ShortcutTree_DeleteRec $w $sc_tag sc_arr sc_tree

   # fill the widget with the new tree
   ShortcutTree_Fill $w {} $sc_tree sc_arr 1
}

proc ShortcutTree_InsertRec {w new_sc after_sc_tag sc_arr_ref sc_tree_ref} {
   upvar $sc_arr_ref sc_arr
   upvar $sc_tree_ref sc_tree

   set idx 0
   foreach elem $sc_tree {
      if {[llength $elem] == 1} {
         if {$elem == $after_sc_tag} {
            set sc_tree [linsert $sc_tree [expr $idx + 1] $new_sc]
            return 1
         }
      } else {
         set dir_tag [lindex $elem 0]
         if [info exists sc_arr($dir_tag)] {
            if {$dir_tag == $after_sc_tag} {
               set sc_tree [linsert $sc_tree [expr $idx + 1] $new_sc]
               return 1
            } else {
               set ltree [lrange $elem 1 end]
               if [ShortcutTree_InsertRec $w $new_sc $after_sc_tag sc_arr ltree] {
                  set sc_tree [lreplace $sc_tree $idx $idx [concat $dir_tag $ltree]]
                  return 1
               }
            }
         }
      }
      incr idx
   }
   return 0
}

proc ShortcutTree_Add {mode} {
   global fscedit_sclist fscedit_tree fscedit_tag

   # store settings of the currently selected shortcut
   CheckShortcutUpdatePending

   set prev_tag [ShortcutTree_Curselection .fscedit.list]
   if {([llength $prev_tag] == 1) && [info exists fscedit_sclist($prev_tag)]} {

      Tree:selection .fscedit.list clear $prev_tag
      set sc_tag [GenerateShortcutTag]

      if {[string compare $mode folder] == 0} {
         set fscedit_sclist($sc_tag) [list "New folder" {} {} {} merge "+dir"]
      } else {
         set fscedit_sclist($sc_tag) [list {} {} {} {} merge "separator"]
      }
      ShortcutTree_InsertRec .fscedit.list $sc_tag $prev_tag fscedit_sclist fscedit_tree

      ShortcutTree_Fill .fscedit.list {} $fscedit_tree fscedit_sclist 1

      Tree:selection .fscedit.list set $sc_tag
      Tree:see .fscedit.list $sc_tag
      SelectEditedShortcut
   }
}

