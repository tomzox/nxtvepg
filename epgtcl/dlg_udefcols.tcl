#
#  Configuration dialog for user-defined columns
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
#    Implements a GUI for configuring user-defined columns.
#
#  Author: Tom Zoerner
#
#  $Id: dlg_udefcols.tcl,v 1.6 2002/11/30 22:00:55 tom Exp tom $
#
set ucf_type_idx 0
set ucf_value_idx 1
set ucf_fmt_idx 2
set ucf_ctxcache_idx 3
set ucf_sctag_idx 4

set usercol_popup 0

## ---------------------------------------------------------------------------
##  Load filter cache with shortcuts used in user-defined columns
##
proc DownloadUserDefinedColumnFilters {} {
   global ucf_type_idx ucf_value_idx ucf_fmt_idx ucf_ctxcache_idx ucf_sctag_idx
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global shortcuts shortcut_order
   global usercols usercol_count

   set cache_idx 0
   foreach id [array names usercols] {
      set new {}
      foreach filt $usercols($id) {
         set sc_tag [lindex $filt $ucf_sctag_idx]
         if {$sc_tag != -1} {
            if [info exists shortcuts($sc_tag)] {
               if {![info exists cache($sc_tag)]} {
                  set cache($sc_tag) $cache_idx
                  incr cache_idx
               }
               lappend new [lreplace $filt $ucf_ctxcache_idx $ucf_ctxcache_idx $cache($sc_tag)]
            }
         } else {
            lappend new [lreplace $filt $ucf_ctxcache_idx $ucf_ctxcache_idx -1]
         }
      }
      set usercols($id) $new
   }

   # free the old cache and allocate a new one
   C_PiFilter_ContextCacheCtl start $cache_idx

   foreach {sc_tag filt_idx} [array get cache] {
      C_PiFilter_ContextCacheCtl set $filt_idx
      SelectSingleShortcut $sc_tag
   }

   C_PiFilter_ContextCacheCtl done
}

#=LOAD=PopupUserDefinedColumns
#=DYNAMIC=

## ---------------------------------------------------------------------------
## Callback for configure menu: create the configuration dialog
##
proc PopupUserDefinedColumns {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global ucf_type_idx ucf_value_idx ucf_fmt_idx ucf_ctxcache_idx ucf_sctag_idx
   global shortcuts shortcut_order
   global usercols colsel_tabs colsel_ailist_predef
   global usercol_scnames usercol_cf_tag usercol_cf_filt_idx
   global usercol_popup text_bg pi_img

   if {$usercol_popup == 0} {
      CreateTransientPopup .usercol "User-defined columns"
      set usercol_popup 1

      frame   .usercol.sel -relief ridge -borderwidth 2
      label   .usercol.sel.lab -text "Currently editing column:"
      pack    .usercol.sel.lab -side left
      entry   .usercol.sel.cur -width 20 -textvariable usercol_cf_lab
      bind    .usercol.sel.cur <Enter> {SelectTextOnFocus %W}
      pack    .usercol.sel.cur -side left -fill x -expand 1
      menubutton .usercol.sel.mb -text "Select column" -direction below -indicatoron 1 -borderwidth 2 -relief raised \
                                 -menu .usercol.sel.mb.men -underline 0
      menu    .usercol.sel.mb.men -tearoff 0
      pack    .usercol.sel.mb -side right
      pack    .usercol.sel -side top -fill x

      # 2nd row: label and header definition
      frame   .usercol.txt
      label   .usercol.txt.lab_head -text "Column header text:"
      pack    .usercol.txt.lab_head -side left -padx 5
      entry   .usercol.txt.ent_head -width 12 -textvariable usercol_cf_head
      bind    .usercol.txt.ent_head <Enter> {SelectTextOnFocus %W}
      pack    .usercol.txt.ent_head -side left -padx 5 -fill x -expand 1
      label   .usercol.txt.lab_lab -text "Header menu:"
      pack    .usercol.txt.lab_lab -side left -padx 5
      menubutton .usercol.txt.hmenu -text "none" -indicatoron 1 -borderwidth 2 -relief raised -width 22 -menu .usercol.txt.hmenu.men
      menu    .usercol.txt.hmenu.men -tearoff 0
      pack    .usercol.txt.hmenu -side left -padx 5
      pack    .usercol.txt -side top -fill x -pady 5

      # 3rd row: shortcut selection & display definition
      frame   .usercol.all

      label   .usercol.all.lab_sel -text "Shortcut selection:"
      grid    .usercol.all.lab_sel -sticky w -row 0 -column 0 -padx 5 -columnspan 2

      frame   .usercol.all.selcmd
      menubutton  .usercol.all.selcmd.scadd -text "add" -indicatoron 1 -borderwidth 2 -relief raised \
                                         -menu .usercol.all.selcmd.scadd.men -underline 0
      menu    .usercol.all.selcmd.scadd.men -tearoff 0
      pack    .usercol.all.selcmd.scadd -side top -fill x -anchor nw
      frame   .usercol.all.selcmd.updown
      button  .usercol.all.selcmd.updown.up -bitmap "bitmap_ptr_up" -command {UserColsDlg_ShiftShortcut 1}
      pack    .usercol.all.selcmd.updown.up -side left -fill x -expand 1
      button  .usercol.all.selcmd.updown.down -bitmap "bitmap_ptr_down" -command {UserColsDlg_ShiftShortcut 0}
      pack    .usercol.all.selcmd.updown.down -side left -fill x -expand 1
      pack    .usercol.all.selcmd.updown -side top -anchor nw -fill x
      button  .usercol.all.selcmd.delsc -text "delete" -command {UserColsDlg_DeleteShortcut}
      pack    .usercol.all.selcmd.delsc -side top -anchor nw
      grid    .usercol.all.selcmd -sticky wen -row 1 -column 0 -padx 5

      ## 3rd row, 2nd column: shortcut list
      frame   .usercol.all.sel
      scrollbar .usercol.all.sel.sb -orient vertical -command [list .usercol.all.sel.selist yview] -takefocus 0
      pack    .usercol.all.sel.sb -side left -fill y
      listbox .usercol.all.sel.selist -exportselection false -height 10 -relief ridge \
                                      -selectmode single -yscrollcommand [list .usercol.all.sel.sb set]
      bind    .usercol.all.sel.selist <<ListboxSelect>> [list after idle [list UserColsDlg_SelectShortcut]]
      bind    .usercol.all.sel.selist <Key-Delete> [list tkButtonInvoke .usercol.all.selcmd.delsc]
      bind    .usercol.all.sel.selist <Control-Key-Up> [concat tkButtonInvoke .usercol.all.selcmd.updown.up {;} break]
      bind    .usercol.all.sel.selist <Control-Key-Down> [concat tkButtonInvoke .usercol.all.selcmd.updown.down {;} break]
      bind    .usercol.all.sel.selist <Enter> {focus %W}
      pack    .usercol.all.sel.selist -side left -fill both -expand 1
      grid    .usercol.all.sel -sticky wens -row 1 -column 1 -padx 5

      ## 3rd row, 3rd column: shortcut display choices

      frame   .usercol.all.disp
      frame   .usercol.all.disp.attr -relief ridge -borderwidth 2
      label   .usercol.all.disp.attr.lab_type -text "Display shortcut match as:"
      grid    .usercol.all.disp.attr.lab_type -sticky w -row 0 -column 0 -columnspan 2
      radiobutton .usercol.all.disp.attr.type_text -text "Text" -variable usercol_cf_type -value 0
      grid    .usercol.all.disp.attr.type_text -sticky w -column 0 -row 1
      entry   .usercol.all.disp.attr.ent_text -textvariable usercol_cf_text
      bind    .usercol.all.disp.attr.ent_text <Enter> {SelectTextOnFocus %W}
      bind    .usercol.all.disp.attr.ent_text <Key-Return> {set usercol_cf_type 0}
      grid    .usercol.all.disp.attr.ent_text -sticky we -column 1 -row 1
      radiobutton .usercol.all.disp.attr.type_image -text "Image" -variable usercol_cf_type -value 1
      grid    .usercol.all.disp.attr.type_image -sticky w -column 0 -row 2
      menubutton  .usercol.all.disp.attr.img -text "Image" -indicatoron 1 -borderwidth 2 -relief raised -menu .usercol.all.disp.attr.img.men -height 20
      menu    .usercol.all.disp.attr.img.men -tearoff 0
      grid    .usercol.all.disp.attr.img -sticky we -column 1 -row 2
      radiobutton .usercol.all.disp.attr.type_attr -text "Attribute" -variable usercol_cf_type -value 2
      grid    .usercol.all.disp.attr.type_attr -sticky w -column 0 -row 3
      menubutton  .usercol.all.disp.attr.att -text "Attribute" -indicatoron 1 -borderwidth 2 -relief raised -menu .usercol.all.disp.attr.att.men
      menu    .usercol.all.disp.attr.att.men -tearoff 0
      grid    .usercol.all.disp.attr.att -sticky we -column 1 -row 3
      grid    columnconfigure .usercol.all.disp.attr 1 -weight 1
      pack    .usercol.all.disp.attr -side top -anchor nw -fill x
      grid    .usercol.all.disp -sticky wen -row 1 -column 2 -padx 5

      frame   .usercol.all.disp.fmt -relief ridge -borderwidth 2
      label   .usercol.all.disp.fmt.lab_fmt -text "Text format:"
      grid    .usercol.all.disp.fmt.lab_fmt -sticky w -row 0 -column 0 -columnspan 2
      checkbutton .usercol.all.disp.fmt.chk_bold -text "bold" -variable usercol_cf_bold
      grid    .usercol.all.disp.fmt.chk_bold -sticky w -row 1 -column 0
      checkbutton .usercol.all.disp.fmt.chk_underline -text "underline" -variable usercol_cf_underline
      grid    .usercol.all.disp.fmt.chk_underline -sticky w -row 2 -column 0
      tk_optionMenu .usercol.all.disp.fmt.mb_color usercol_cf_color black red blue green yellow pink cyan
      grid    .usercol.all.disp.fmt.mb_color -sticky we -row 1 -column 1
      grid    columnconfigure .usercol.all.disp.fmt 0 -weight 1
      grid    columnconfigure .usercol.all.disp.fmt 1 -weight 1
      pack    .usercol.all.disp.fmt -side top -anchor nw -fill x

      pack    .usercol.all.disp.attr -side top -anchor nw -fill x
      grid    columnconfigure .usercol.all 1 -weight 2
      grid    columnconfigure .usercol.all 2 -weight 1
      grid    rowconfigure .usercol.all 1 -weight 1
      pack    .usercol.all -side top -fill both -expand 1

      # 4th row: main control
      frame   .usercol.cmd
      button  .usercol.cmd.help -text "Help" -width 7 -command {PopupHelp $helpIndex(Configuration) "User-defined columns"}
      button  .usercol.cmd.del -text "Delete" -command {UserColsDlg_Delete} -width 7
      button  .usercol.cmd.dismiss -text "Dismiss" -width 7 -command {if [UserColsDlg_CheckDiscard] {destroy .usercol}}
      button  .usercol.cmd.apply -text "Apply" -width 7 -command {UserColsDlg_Apply}
      pack    .usercol.cmd.help .usercol.cmd.del .usercol.cmd.dismiss .usercol.cmd.apply -side left -padx 10
      pack    .usercol.cmd -side top -pady 5

      bind    .usercol <Key-F1> {PopupHelp $helpIndex(Configuration) "User-defined columns"}
      bind    .usercol.cmd <Destroy> {+ set usercol_popup 0}
      #bind    .usercol.cmd.ok <Return> {tkButtonInvoke .usercol.cmd.ok}
      #bind    .usercol.cmd.ok <Escape> {tkButtonInvoke .usercol.cmd.abort}
      bind    .usercol <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]


      # create drop-down menu with all images
      foreach id [lsort [array names pi_img]] {
         .usercol.all.disp.attr.img.men add radiobutton -image [lindex $pi_img($id) 0] \
            -variable usercol_cf_image -value $id \
            -command [concat set usercol_cf_type 1 {;} \
                      .usercol.all.disp.attr.img configure -image [lindex $pi_img($id) 0]]
      }
      .usercol.all.disp.attr.img.men entryconfigure 7 -columnbreak 1
      .usercol.all.disp.attr.img.men entryconfigure 14 -columnbreak 1

      # create drop-down menu with all pre-defined columns
      foreach id $colsel_ailist_predef {
         .usercol.all.disp.attr.att.men add radiobutton -label [lindex $colsel_tabs($id) 3] \
            -variable usercol_cf_attr -value $id \
            -command [concat set usercol_cf_type 2 {;} \
                      [list .usercol.all.disp.attr.att configure -text [lindex $colsel_tabs($id) 3]]]

         if {[string compare [lindex $colsel_tabs($id) 2] none] != 0} {
            .usercol.txt.hmenu.men add radiobutton -label [lindex $colsel_tabs($id) 3] \
               -variable usercol_cf_hmenu -value "&$id" \
               -command [list .usercol.txt.hmenu configure -text [lindex $colsel_tabs($id) 3]]
         }
      }
      .usercol.txt.hmenu.men add separator
      .usercol.txt.hmenu.men add radiobutton -label "Used shortcuts" -variable usercol_cf_hmenu -value "&user_def" \
                                             -command {.usercol.txt.hmenu configure -text "Shortcuts"}
      .usercol.txt.hmenu.men add separator
      .usercol.txt.hmenu.men add radiobutton -label "none" -variable usercol_cf_hmenu -value "none" \
                                             -command {.usercol.txt.hmenu configure -text "none"}

      # create drop-down menu with all currently defined shortcuts
      # - save the shortcut names into a global array
      foreach sc_tag [CompleteShortcutOrder] {
         set usercol_scnames($sc_tag) [lindex $shortcuts($sc_tag) $fsc_name_idx]
         .usercol.all.selcmd.scadd.men add command -label [lindex $shortcuts($sc_tag) $fsc_name_idx] \
            -command [list UserColsDlg_AddShortcut $sc_tag]
      }
      # append menu entry and pseudo-shortcut name for "no match"
      .usercol.all.selcmd.scadd.men add separator
      .usercol.all.selcmd.scadd.men add command -label "*no match*" -command {UserColsDlg_AddShortcut -1}
      set usercol_scnames(-1) "*none of the above*"

      # create drop-down menu with all user-defined columns
      set first_tag [UserColsDlg_FillColumnSelectionMenu]

      # fill dialog with data of the first column
      set usercol_cf_filt_idx -1
      set usercol_cf_tag -1
      if {$first_tag != -1} {
         UserColsDlg_SelectColumn $first_tag
      } else {
         UserColsDlg_NewCol
      }

      wm resizable .usercol 1 1
      update
      wm minsize .usercol [winfo reqwidth .usercol] [winfo reqheight .usercol]
   } else {
      raise .usercol
   }
}

# create drop-down menu with all user-defined columns
proc UserColsDlg_FillColumnSelectionMenu {} {
   global usercols colsel_tabs

   # clear the old content
   .usercol.sel.mb.men delete 0 end

   .usercol.sel.mb.men add command -label "Create new column" -command UserColsDlg_NewCol
   .usercol.sel.mb.men add separator

   set ailist [lsort -integer [array names usercols]]
   foreach tag $ailist {
      if [info exists colsel_tabs(user_def_$tag)] {
         set uc_def $colsel_tabs(user_def_$tag)
         .usercol.sel.mb.men add command -label "[lindex $uc_def 3] ([lindex $uc_def 1])" \
                                         -command [concat UserColsDlg_SelectColumn $tag]
      }
   }

   # return the tag fo the first menu item or -1
   if {[llength $ailist] > 0} {
      return [lindex $ailist 0]
   } else {
      return -1
   }
}

# check if the column definition was changed
proc UserColsDlg_CheckDiscard {} {
   global ucf_type_idx ucf_value_idx ucf_fmt_idx ucf_ctxcache_idx ucf_sctag_idx
   global usercol_cf_tag usercol_cf_selist usercol_cf_filt
   global usercol_cf_lab usercol_cf_head usercol_cf_hmenu
   global usercols colsel_tabs

   UserColsDlg_UpdateShortcutAttribs

   if {$usercol_cf_tag != -1} {
      set ok 1

      # check if shortcut count has changed
      if [info exists usercols($usercol_cf_tag)] {
         if {[llength $usercols($usercol_cf_tag)] == [llength $usercol_cf_selist]} {
            set filt_idx 0
            # loop across all shortcuts
            foreach filt $usercols($usercol_cf_tag) {
               # check if the shortcut reference changed
               if {[lindex $filt $ucf_sctag_idx] == [lindex $usercol_cf_selist $filt_idx]} {
                  # compare the attributes for this shortcut
                  set new $usercol_cf_filt([lindex $usercol_cf_selist $filt_idx])
                  if {([lindex $filt $ucf_type_idx] != [lindex $new $ucf_type_idx]) || \
                      ([string compare [lindex $filt $ucf_value_idx] [lindex $new $ucf_value_idx]] != 0) || \
                      ([string compare [lindex $filt $ucf_fmt_idx] [lindex $new $ucf_fmt_idx]] != 0)} {
                     set ok 0
                     break
                  }
               } else {
                  set ok 0
                  break
               }
               incr filt_idx
            }
         } else {
            # number of shortcut differs -> column was changed
            set ok 0
         }
      } elseif {[llength $usercol_cf_selist] > 0} {
         # new, unsaved column with shortcuts assigend
         set ok 0
      }

      # check if label or column header have been changed
      if {[info exists colsel_tabs(user_def_$usercol_cf_tag)]} {
         set old $colsel_tabs(user_def_$usercol_cf_tag)
         if {([string compare [lindex $old 1] $usercol_cf_head] != 0) ||
             ([string compare [lindex $old 2] $usercol_cf_hmenu] != 0) ||
             ([string compare [lindex $old 3] $usercol_cf_lab] != 0)} {
            set ok 0
         }
      } elseif {([string length $usercol_cf_head] > 0) || \
                ([string compare $usercol_cf_hmenu "none"] != 0) || \
                (([string length $usercol_cf_lab] > 0) && \
                 ([string compare $usercol_cf_lab "*unnamed*"] != 0))} {
         # new, unsaved column with header or label assigned
         set ok 0
      }

      if {!$ok} {
         set answer [tk_messageBox -icon warning -type okcancel -default ok -parent .usercol \
                        -message "You're about to discard your changes in the column definition."]
         set ok [expr [string compare $answer ok] == 0]
      }
   } else {
      set ok 1
   }
   return $ok
}

# callback for column selection (topmost menubutton)
proc UserColsDlg_SelectColumn {tag} {
   global ucf_type_idx ucf_value_idx ucf_fmt_idx ucf_ctxcache_idx ucf_sctag_idx
   global usercols colsel_tabs
   global usercol_scnames
   global usercol_cf_tag usercol_cf_selist usercol_cf_filt_idx usercol_cf_filt
   global usercol_cf_lab usercol_cf_head usercol_cf_hmenu

   if {![UserColsDlg_CheckDiscard]} return;

   set usercol_cf_tag $tag

   # load the column definition into temporary variables
   set usercol_cf_selist {}
   array unset usercol_cf_filt
   if [info exists usercols($usercol_cf_tag)] {
      foreach filt $usercols($usercol_cf_tag) {
         lappend usercol_cf_selist [lindex $filt $ucf_sctag_idx]
         set usercol_cf_filt([lindex $filt $ucf_sctag_idx]) $filt
      }
   }

   # display label and column header text
   if [info exists colsel_tabs(user_def_$usercol_cf_tag)] {
      set uc_def $colsel_tabs(user_def_$usercol_cf_tag)
      set usercol_cf_lab [lindex $uc_def 3]
      set usercol_cf_head [lindex $uc_def 1]
      set usercol_cf_hmenu [lindex $uc_def 2]
   } else {
      set usercol_cf_lab {*unnamed*}
      set usercol_cf_hmenu "none"
      set usercol_cf_head {}
   }

   # display column header menu type
   .usercol.txt.hmenu configure -text "none"
   if {[string compare $usercol_cf_hmenu "&user_def"] == 0} {
      .usercol.txt.hmenu configure -text "Shortcuts"
   } elseif {[string compare -length 1 $usercol_cf_hmenu "&"] == 0} {
      set colref [string range $usercol_cf_hmenu 1 end]
      if {[info exists colsel_tabs($colref)]} {
         .usercol.txt.hmenu configure -text [lindex $colsel_tabs($colref) 3]
      }
   }

   focus .usercol.sel.cur
   .usercol.sel.cur selection range 0 end

   # fill shortcut list
   .usercol.all.sel.selist delete 0 end
   foreach tag $usercol_cf_selist {
      .usercol.all.sel.selist insert end $usercol_scnames($tag)
   }
   # select the first shortcut
   set usercol_cf_filt_idx -1
   if {[llength $usercol_cf_selist] > 0} {
      .usercol.all.sel.selist selection set 0
   }
   UserColsDlg_SelectShortcut
}

# callback for "new column" button
proc UserColsDlg_NewCol {} {
   global usercols

   # generate a new, unique tag
   set tag [clock seconds]
   while [info exists usercols($tag)] {
      incr tag
   }

   UserColsDlg_SelectColumn $tag
}

# helper function: store (possibly modified) settings of the selected shortcut
proc UserColsDlg_UpdateShortcutAttribs {} {
   global usercol_cf_selist usercol_cf_filt usercol_cf_filt_idx
   global usercol_cf_type usercol_cf_text usercol_cf_image usercol_cf_attr
   global usercol_cf_bold usercol_cf_underline usercol_cf_color

   if {$usercol_cf_filt_idx != -1} {
      set sc_tag [lindex $usercol_cf_selist $usercol_cf_filt_idx]
      if {$usercol_cf_type == 0} {
         set value $usercol_cf_text
      } elseif {$usercol_cf_type == 1} {
         set value $usercol_cf_image
      } elseif {$usercol_cf_type == 2} {
         set value $usercol_cf_attr
      } else {
         set value {}
      }
      set fmt {}
      if $usercol_cf_bold {lappend fmt bold}
      if $usercol_cf_underline {lappend fmt underline}
      if {[string compare $usercol_cf_color black] != 0} {lappend fmt $usercol_cf_color}

      set usercol_cf_filt($sc_tag) [list $usercol_cf_type $value $fmt 0 $sc_tag]
   }
}

# callback for changes of selection in shortcut listbox
proc UserColsDlg_SelectShortcut {} {
   global ucf_type_idx ucf_value_idx ucf_fmt_idx ucf_ctxcache_idx ucf_sctag_idx
   global usercol_cf_tag usercol_cf_selist usercol_cf_filt_idx usercol_cf_filt
   global usercol_cf_type usercol_cf_text usercol_cf_image usercol_cf_attr
   global usercol_cf_bold usercol_cf_underline usercol_cf_color
   global colsel_tabs pi_img

   set sc_idx [.usercol.all.sel.selist curselection]
   if {[llength $sc_idx] == 1} {
      if {$usercol_cf_filt_idx != $sc_idx} {
         # store settings of the previously selected shortcut
         UserColsDlg_UpdateShortcutAttribs

         # display the settings for the newly selected shortcut
         set usercol_cf_filt_idx $sc_idx
         set filt $usercol_cf_filt([lindex $usercol_cf_selist $sc_idx])

         set usercol_cf_type [lindex $filt $ucf_type_idx]

         set usercol_cf_text {}
         set usercol_cf_image diamond_black
         set usercol_cf_attr title
         if {$usercol_cf_type == 0} {
            set usercol_cf_text [lindex $filt $ucf_value_idx]
         } elseif {$usercol_cf_type == 1} {
            set usercol_cf_image [lindex $filt $ucf_value_idx]
         } elseif {$usercol_cf_type == 2} {
            set usercol_cf_attr [lindex $filt $ucf_value_idx]
         }
         .usercol.all.disp.attr.img configure -image [lindex $pi_img($usercol_cf_image) 0]
         .usercol.all.disp.attr.att configure -text [lindex $colsel_tabs($usercol_cf_attr) 3]

         set usercol_cf_bold 0
         set usercol_cf_underline 0
         set usercol_cf_color black
         foreach fmt [lindex $filt $ucf_fmt_idx] {
            if {[string compare $fmt bold] == 0} {set usercol_cf_bold 1} \
            elseif {[string compare $fmt underline] == 0} {set usercol_cf_underline 1} \
            else {set usercol_cf_color $fmt}
         }
      }
   } else {
      # none selected or list empty -> clear
      set usercol_cf_type -1
      set usercol_cf_text {}
      set usercol_cf_image diamond_black
      set usercol_cf_attr title
      set usercol_cf_bold 0
      set usercol_cf_underline 0
      set usercol_cf_color black
      .usercol.all.disp.attr.img configure -image [lindex $pi_img($usercol_cf_image) 0]
      .usercol.all.disp.attr.att configure -text [lindex $colsel_tabs($usercol_cf_attr) 3]
   }
}

# callback for "add" menu commands: add a shortcut to the user-defined column
proc UserColsDlg_AddShortcut {sc_tag} {
   global usercol_cf_selist usercol_cf_filt_idx usercol_cf_filt
   global usercol_scnames

   # check if the shortcut is already part of the selection
   if {[lsearch $usercol_cf_selist $sc_tag] == -1} {
      # save possible changes in the shortcut attributes
      UserColsDlg_UpdateShortcutAttribs

      set new_idx [llength $usercol_cf_selist]
      if {($sc_tag != -1) && ($new_idx > 0) && \
          ([lindex $usercol_cf_selist end] == -1) } {
         incr new_idx -1
      }

      # initialize filter match display attribs: initial display type is text = shortcut name
      set disp_text $usercol_scnames($sc_tag)
      if {$sc_tag == -1} {
         set disp_text ""
      }
      set usercol_cf_filt($sc_tag) [list 0 $disp_text {} 0 $sc_tag]
      set usercol_cf_selist [linsert $usercol_cf_selist $new_idx $sc_tag]

      .usercol.all.sel.selist insert $new_idx $usercol_scnames($sc_tag)
      .usercol.all.sel.selist selection clear 0 end
      .usercol.all.sel.selist selection set $new_idx
      UserColsDlg_SelectShortcut
   }
}

# callback for delete button in shortcut selection
proc UserColsDlg_DeleteShortcut {} {
   global usercol_cf_filt_idx usercol_cf_selist usercol_cf_filt

   set sc_idx [.usercol.all.sel.selist curselection]
   if {[llength $sc_idx] == 1} {
      unset usercol_cf_filt([lindex $usercol_cf_selist $sc_idx])
      set usercol_cf_selist [lreplace $usercol_cf_selist $sc_idx $sc_idx]
      .usercol.all.sel.selist delete $sc_idx

      # set the listbox cursor onto the next shortcut
      if {$sc_idx >= [llength $usercol_cf_selist]} {
         set sc_idx [expr [llength $usercol_cf_selist] - 1]
      }
      if {$sc_idx >= 0} {
         .usercol.all.sel.selist selection set $sc_idx
      }
      set usercol_cf_filt_idx -1
      UserColsDlg_SelectShortcut
   }
}

# callback for up/down arrows: change order of shortcuts in listbox
proc UserColsDlg_ShiftShortcut {is_up} {
   global usercol_cf_selist usercol_cf_filt_idx
   global usercol_scnames

   # check if "none of the above" is affected: cannot be moved
   if {$usercol_cf_filt_idx >= 0} {
      if {([lindex $usercol_cf_selist end] != -1) || \
          ($is_up ? ($usercol_cf_filt_idx + 1 < [llength $usercol_cf_selist]) : \
                    ($usercol_cf_filt_idx + 2 < [llength $usercol_cf_selist]))} {

         # save possible changes in the shortcut attributes
         UserColsDlg_UpdateShortcutAttribs

         if $is_up {
            SelBoxShiftUpItem .usercol.all.sel.selist usercol_cf_selist usercol_scnames
         } else {
            SelBoxShiftDownItem .usercol.all.sel.selist usercol_cf_selist usercol_scnames
         }
         set usercol_cf_filt_idx [.usercol.all.sel.selist curselection]
         if {[llength usercol_cf_filt_idx] != 1} {
            set usercol_cf_filt_idx -1
         }
      }
   }
}

# callback for "apply" button
proc UserColsDlg_Apply {} {
   global usercol_cf_tag usercol_cf_selist usercol_cf_filt
   global usercol_cf_lab usercol_cf_head usercol_cf_hmenu
   global usercols colsel_tabs pilistbox_cols

   # save possible changes in the shortcut attributes
   UserColsDlg_UpdateShortcutAttribs

   if {[llength $usercol_cf_selist] == 0} {
      tk_messageBox -icon error -type ok -default ok -parent .usercol \
                    -message "Refusing to save a column definition without shortcuts assigend - the column would always be empty."
      return
   }
   if {([string length $usercol_cf_lab] == 0) || \
       ([string compare $usercol_cf_lab "*unnamed*"] == 0)} {
      if {[string length $usercol_cf_head] == 0} {
         tk_messageBox -icon error -type ok -default ok -parent .usercol \
                       -message "Please enter a description for this column in the 'Column header text' field. The text used in the programme list and the column selection configuration dialog."
         return
      } else {
         set usercol_cf_lab $usercol_cf_head
      }
   }

   set tmpl {}
   foreach tag $usercol_cf_selist {
      lappend tmpl $usercol_cf_filt($tag)
   }
   set usercols($usercol_cf_tag) $tmpl

   if {[info exists colsel_tabs(user_def_$usercol_cf_tag)]} {
      set old $colsel_tabs(user_def_$usercol_cf_tag)
      set width [lindex $old 0]
      set head_change [expr ([string compare [lindex $old 1] $usercol_cf_head] != 0) || \
                            ([string compare [lindex $old 2] $usercol_cf_hmenu] != 0) || \
                            ([string compare [lindex $old 3] $usercol_cf_lab] != 0)]
   } else {
      set width [UserColsDlg_CalcDefaultWidth $usercol_cf_tag]
      set head_change 0

      set answer [tk_messageBox -icon question -type okcancel -default ok -parent .usercol \
                     -message "Do you want to immediately insert the new column to into the programme listbox?"]
      if {[string compare $answer ok] == 0} {
         lappend pilistbox_cols user_def_$usercol_cf_tag
         set head_change 1
      }
   }

   set colsel_tabs(user_def_$usercol_cf_tag) [list $width $usercol_cf_head $usercol_cf_hmenu $usercol_cf_lab]

   # update this column's entry in the column selection drop-down menu
   UserColsDlg_FillColumnSelectionMenu
   # update this column's entry in the column selection configuration dialogs, if currently open
   PiListboxColsel_ColUpdate
   DumpHtml_ColUpdate
   # fill the shortcut cache with all referenced shortcuts
   DownloadUserDefinedColumnFilters
   if $head_change {
      UpdatePiListboxColumns
   }
   C_PiOutput_CfgColumns
   C_RefreshPiListbox
   UpdateRcFile
}

# callback for "delete" in main command row -> delete the entire column definition
proc UserColsDlg_Delete {} {
   global usercol_cf_tag usercol_cf_selist
   global usercols colsel_tabs pilistbox_cols
   global dumphtml_colsel dumphtml_use_colsel

   if [info exists usercols($usercol_cf_tag)] {
      set pi_col_idx [lsearch $pilistbox_cols user_def_$usercol_cf_tag]
      set msg "You're about to irrecoverably delete this column definition."
      if {$pi_col_idx == -1} {
         if $dumphtml_use_colsel {
            set html_col_idx [lsearch $dumphtml_colsel user_def_$usercol_cf_tag]
         }
         if {$html_col_idx == -1} {
            set answer [tk_messageBox -icon question -type okcancel -default ok -parent .usercol \
                           -message "$msg Do you want to proceed?"]
         } else {
            set answer [tk_messageBox -icon question -type okcancel -default ok -parent .usercol \
                           -message "$msg This column is referenced by the HTML export configuration. Do you want to proceed?"]
         }
      } else {
         set answer [tk_messageBox -icon question -type okcancel -default ok -parent .usercol \
                        -message "$msg Do you want to proceed and remove this column from the programme listbox?"]
      }
      if {[string compare $answer ok] == 0} {
         unset usercols($usercol_cf_tag)
         unset colsel_tabs(user_def_$usercol_cf_tag)

         if {$pi_col_idx != -1} {
            set pilistbox_cols [lreplace $pilistbox_cols $pi_col_idx $pi_col_idx]
         }

         set html_col_idx [lsearch $dumphtml_colsel user_def_$usercol_cf_tag]
         if {$html_col_idx != -1} {
            set dumphtml_colsel [lreplace $dumphtml_colsel $html_col_idx $html_col_idx]
         }

         DownloadUserDefinedColumnFilters
         if {$pi_col_idx != -1} {
            UpdatePiListboxColumns
         }
         C_PiOutput_CfgColumns
         C_RefreshPiListbox
         UpdateRcFile

         # update this column's entry in the selection drop-down menu
         UserColsDlg_FillColumnSelectionMenu

         # remove it from the column selection configuration dialogs, if currently open
         PiListboxColsel_ColUpdate
         DumpHtml_ColUpdate

         # remove this column's entry from the selection drop-down menu
         set first_tag [UserColsDlg_FillColumnSelectionMenu]

         # switch to the first user-defined column
         set usercol_cf_filt_idx -1
         set usercol_cf_tag -1
         if {$first_tag != -1} {
            UserColsDlg_SelectColumn $first_tag
         } else {
            UserColsDlg_NewCol
         }
      }
   } else {
      # new, unsaved entry
      if [UserColsDlg_CheckDiscard] {
         # create an empty, new input form
         set usercol_cf_filt_idx -1
         set usercol_cf_tag -1
         UserColsDlg_NewCol
      }
   }
}

# helper function: get default width for a new column
proc UserColsDlg_CalcDefaultWidth {uc_tag} {
   global ucf_type_idx ucf_value_idx ucf_fmt_idx ucf_ctxcache_idx ucf_sctag_idx
   global usercols colsel_tabs usercol_cf_tag
   global font_normal

   set width 32
   foreach filt $usercols($uc_tag) {
      set type [lindex $filt $ucf_type_idx]
      if {$type == 0} {
         # this shortcut match is disaplyed with a static text
         if {[lsearch [lindex $filt $ucf_fmt_idx] bold] == -1} {
            set font $font_normal
         } else {
            set font [DeriveFont $font_normal 0 bold]
         }
         set this_width [font measure $font [lindex $filt $ucf_value_idx]]
         incr width 5

      } elseif {$type == 2} {
         set this_width [lindex $colsel_tabs([lindex $filt $ucf_value_idx]) 0]

      } else {
         set this_width 0
      }
      if {$this_width > $width} {
         set width $this_width
      }
   }
   return $width
}

