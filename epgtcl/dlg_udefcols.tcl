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
#  $Id: dlg_udefcols.tcl,v 1.17 2004/12/12 17:26:40 tom Exp tom $
#
# import constants from other modules
#=INCLUDE= "epgtcl/dlg_remind.h"
#=INCLUDE= "epgtcl/shortcuts.h"
#=INCLUDE= "epgtcl/mainwin.h"

#=CONST= ::ucf_type_idx     0
#=CONST= ::ucf_value_idx    1
#=CONST= ::ucf_fmt_idx      2
#=CONST= ::ucf_ctxcache_idx 3
#=CONST= ::ucf_sctag_idx    4

set usercol_popup 0

##
##  Create an example definition
##  - called during start-up if usercols array is empty
##
proc PreloadUserDefinedColumns {} {
   global colsel_tabs usercols

   set colsel_tabs(user_def_0) {80  Example  &user_def  "Compound example"}
   set usercols(0) {{0 Reminder {bold fg_RGBCC0000 ag_RGBFFCCCC} 1 rgp_all} {0 Movie! {bold fg_RGB0068ce} 0 10000} {2 theme {} -1 -1}}
}

# convert an internal color tag into a widget color parameter
proc TextTag2Color {tag} {
   if {([string length $tag] == 6+6) && ([scan $tag {%*[fbac]g_RGB%x%n} foo len] == 2) && ($len == 12)} {
      return "#[string range $tag 6 end]"
   } elseif {[string compare $tag "fg_UNDEF"] == 0} {
      return $::text_fg
   } elseif {[string compare $tag "bg_UNDEF"] == 0} {
      return $::text_bg
      return $::text_fg
   } elseif {[regexp {[fbac]g_} $tag]} {
      return [string range $tag 3 end]
   } else {
      return {}
   }
}

# convert a color to a text tag
proc Color2TextTag {col prefix} {
   if {([string length $col] == 1+6) && ([scan $col "#%x%n" foo len] == 2) && ($len == 7)} {
      return "${prefix}_RGB[string range $col 1 end]"
   } else {
      return "${prefix}_$col"
   }
}

proc UserCols_SetPiBoxTextColors {wid} {
   global pi_bold_font is_unix
   global usercols usercol_count
   global rem_col_fmt

   # collect format tags from all filters in all compound definitions
   foreach id [array names usercols] {
      foreach filt $usercols($id) {
         foreach fmt_tag [lindex $filt $::ucf_fmt_idx] {
            set fmt_cache($fmt_tag) {}
         }
      }
   }
   foreach filt $rem_col_fmt {
      foreach fmt_tag [lindex $filt $::ucf_fmt_idx] {
         set fmt_cache($fmt_tag) {}
      }
   }

   # remove old tags before setting new ones
   set ltmp {}
   foreach fmt_tag [$wid tag names] {
      if {[regexp {^[fbac]g_} $fmt_tag] && \
          ![regexp {^ag_day} $fmt_tag] && \
          ![info exists fmt_cache($fmt_tag)]} {
         lappend ltmp $fmt_tag
      }
   }
   if {[llength $ltmp] > 0} {
      eval [concat $wid tag delete $ltmp]
   }

   # remove non-colors from tag list
   catch {unset fmt_cache(bold)}
   catch {unset fmt_cache(underline)}
   catch {unset fmt_cache(overstrike)}

   # return tags and color identifiers (e.g. "fg_red" -> "#ff0000")
   foreach fmt_tag [array names fmt_cache] {
      set col [TextTag2Color $fmt_tag]
      if {[regexp {[ba]g_} $fmt_tag]} {
         if {[catch {$wid tag configure $fmt_tag -background $col}] && $is_unix} {
            puts stderr "Warning: Failed to load compund attribute color '$fmt_tag': $col"
         }
      } else {
         # catch errors because color names come from config file,
         # i.e. might be corrupt or not exist on the current system
         if {[catch {$wid tag configure $fmt_tag -foreground $col}] && $is_unix} {
            puts stderr "Warning: Failed to load compund attribute color '$fmt_tag': $col"
         }
      }
   }

   # raise background color tags
   for {set wday_idx 0} {$wday_idx < 7} {incr wday_idx} {
      $wid tag raise ag_day$wday_idx
   }
}

# helper function: return group if given tag refers to a reminder group, else -1
proc UserColsDlg_IsReminderPseudoTag {sc_tag} {

   if {[string compare $sc_tag "rgp_all"] == 0} {
      # special case: OR of all reminder groups
      return 0
   } elseif {([scan $sc_tag "rgp_%d%n" gtag len] == 2) && \
             ($len == [string length $sc_tag]) && \
             ($gtag > 0)} {
      return $gtag
   } else {
      # not a reminder
      return -1
   }
}

## ---------------------------------------------------------------------------
##  Load filter cache with shortcuts used in user-defined columns
##
proc UserCols_GetShortcuts {cache_var cache_idx} {
   global shortcuts
   global usercols usercol_count
   global remgroups remgroup_order

   upvar $cache_var cache

   foreach id [array names usercols] {
      set new {}
      foreach filt $usercols($id) {
         set sc_tag [lindex $filt $::ucf_sctag_idx]
         if {[UserColsDlg_IsReminderPseudoTag $sc_tag] != -1} {
            if [info exists cache($sc_tag)] {
               lappend new [lreplace $filt $::ucf_ctxcache_idx $::ucf_ctxcache_idx $cache($sc_tag)]
            }
         } elseif {$sc_tag != -1} {
            if [info exists shortcuts($sc_tag)] {
               if {![info exists cache($sc_tag)]} {
                  set cache($sc_tag) $cache_idx
                  incr cache_idx
               }
               lappend new [lreplace $filt $::ucf_ctxcache_idx $::ucf_ctxcache_idx $cache($sc_tag)]
            }
         } else {
            lappend new [lreplace $filt $::ucf_ctxcache_idx $::ucf_ctxcache_idx -1]
         }
      }
      set usercols($id) $new
   }
   return $cache_idx
}

## ---------------------------------------------------------------------------
##  Trigger function for changes in global shortcut list while dialog is open
##
proc UserCols_ShortcutsChanged {} {
   global usercol_popup
   global shortcuts usercol_scnames

   if $usercol_popup {
      # add new shortcuts
      foreach sc_tag [array names shortcuts] {
         set usercol_scnames($sc_tag) [lindex $shortcuts($sc_tag) $::fsc_name_idx]
      }
      # note: obsolete shortcuts not removed here (done upon save)
   }
}

#=LOAD=PopupUserDefinedColumns
#=DYNAMIC=

## ---------------------------------------------------------------------------
## Callback for configure menu: create the configuration dialog
##
proc PopupUserDefinedColumns {} {
   global shortcuts
   global remgroups remgroup_order
   global usercols colsel_tabs colsel_ailist_predef
   global usercol_scnames usercol_cf_tag usercol_cf_filt_idx
   global usercol_popup text_fg text_bg pi_font pi_img win_frm_fg entry_disabledforeground

   if {$usercol_popup == 0} {
      CreateTransientPopup .usercol "Attribute composition & display format"
      set usercol_popup 1

      # 1st row: attribute selection and link to manual pages
      frame   .usercol.head -relief ridge -borderwidth 2
      frame   .usercol.head.sel
      label   .usercol.head.sel.lab -text "Currently editing:"
      pack    .usercol.head.sel.lab -side left
      entry   .usercol.head.sel.cur -width 20 -textvariable usercol_cf_lab
      bind    .usercol.head.sel.cur <Enter> {SelectTextOnFocus %W}
      pack    .usercol.head.sel.cur -side left -fill x -expand 1
      menubutton .usercol.head.sel.mb -text "Select definition" -direction below -indicatoron 1 -borderwidth 2 -relief raised \
                                 -menu .usercol.head.sel.mb.men -underline 0
      menu    .usercol.head.sel.mb.men -tearoff 0
      pack    .usercol.head.sel.mb -side right
      pack    .usercol.head.sel -side top -fill x -padx 5 -pady 5

      label   .usercol.head.lab1 -text "In this dialog you can select text format and colors, or add images to the programme list." -font $::font_normal
      pack    .usercol.head.lab1 -side top -anchor w -padx 5
      button  .usercol.head.lab2 -text "Read the intro for details." -padx 0 -pady 0 \
                      -borderwidth 0 -relief flat -font [DeriveFont $::font_normal 0 underline] \
                      -foreground blue -activeforeground blue -command {PopupHelp $helpIndex(Composite attributes)}
      pack    .usercol.head.lab2 -side top -anchor w -padx 5
      pack    .usercol.head -side top -fill x

      # 2nd row: label and header definition
      frame   .usercol.txt
      label   .usercol.txt.lab_head -text "Column header text:"
      pack    .usercol.txt.lab_head -side left -padx 5
      entry   .usercol.txt.ent_head -width 12 -textvariable usercol_cf_head
      bind    .usercol.txt.ent_head <Enter> {SelectTextOnFocus %W}
      pack    .usercol.txt.ent_head -side left -padx 5 -fill x -expand 1
      label   .usercol.txt.lab_lab -text "Header menu:"
      pack    .usercol.txt.lab_lab -side left -padx 5
      menubutton .usercol.txt.hmenu -text "none" -indicatoron 1 -borderwidth 2 -relief raised -width 22 -menu .usercol.txt.hmenu.men \
                                    -takefocus 1 -highlightthickness 1 -highlightcolor $win_frm_fg
      menu    .usercol.txt.hmenu.men -tearoff 0 -takefocus 1
      pack    .usercol.txt.hmenu -side left -padx 5
      pack    .usercol.txt -side top -fill x -pady 5

      # 3rd row: shortcut selection & display definition
      frame   .usercol.all

      label   .usercol.all.lab_sel -text "Shortcut/reminder selection:"
      grid    .usercol.all.lab_sel -sticky w -row 0 -column 0 -padx 5 -columnspan 2

      frame   .usercol.all.selcmd
      menubutton  .usercol.all.selcmd.scadd -text "Add shortcut" -indicatoron 1 \
                                            -borderwidth 2 -relief raised \
                                            -menu .usercol.all.selcmd.scadd.men -underline 0
      menu    .usercol.all.selcmd.scadd.men -tearoff 0 \
                  -postcommand [list PostDynamicMenu .usercol.all.selcmd.scadd.men UserColsDlg_FillShortcutMenu 0]
      pack    .usercol.all.selcmd.scadd -side top -fill x -anchor nw
      menubutton  .usercol.all.selcmd.remadd -text "Add reminder" -indicatoron 1 \
                                            -borderwidth 2 -relief raised \
                                            -menu .usercol.all.selcmd.remadd.men -underline 0
      menu    .usercol.all.selcmd.remadd.men -tearoff 0 \
                  -postcommand [list PostDynamicMenu .usercol.all.selcmd.remadd.men UserColsDlg_FillReminderMenu 0]
      pack    .usercol.all.selcmd.remadd -side top -fill x -anchor nw
      frame   .usercol.all.selcmd.updown
      button  .usercol.all.selcmd.updown.up -bitmap "bitmap_ptr_up" -command {UserColsDlg_ShiftShortcut 1}
      pack    .usercol.all.selcmd.updown.up -side left -fill x -expand 1
      button  .usercol.all.selcmd.updown.down -bitmap "bitmap_ptr_down" -command {UserColsDlg_ShiftShortcut 0}
      pack    .usercol.all.selcmd.updown.down -side left -fill x -expand 1
      pack    .usercol.all.selcmd.updown -side top -anchor nw -fill x
      button  .usercol.all.selcmd.delsc -text "Delete" -command {UserColsDlg_DeleteShortcut}
      pack    .usercol.all.selcmd.delsc -side top -fill x -anchor nw
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
      label   .usercol.all.disp.attr.lab_type -text "Display match as:"
      grid    .usercol.all.disp.attr.lab_type -sticky w -row 0 -column 0 -columnspan 2
      radiobutton .usercol.all.disp.attr.type_text -text "Text:" -variable usercol_cf_type -value 0
      grid    .usercol.all.disp.attr.type_text -sticky w -column 0 -row 1
      entry   .usercol.all.disp.attr.ent_text -textvariable usercol_cf_text
      bind    .usercol.all.disp.attr.ent_text <Enter> {SelectTextOnFocus %W}
      bind    .usercol.all.disp.attr.ent_text <Key-Return> {set usercol_cf_type 0}
      grid    .usercol.all.disp.attr.ent_text -sticky we -column 1 -row 1
      radiobutton .usercol.all.disp.attr.type_image -text "Image:" -variable usercol_cf_type -value 1
      grid    .usercol.all.disp.attr.type_image -sticky w -column 0 -row 2
      menubutton  .usercol.all.disp.attr.img -text "Image" -indicatoron 1 -borderwidth 2 -relief raised \
                                             -menu .usercol.all.disp.attr.img.men -height 20 \
                                             -takefocus 1 -highlightthickness 1 -highlightcolor $win_frm_fg
      menu    .usercol.all.disp.attr.img.men -tearoff 0
      grid    .usercol.all.disp.attr.img -sticky we -column 1 -row 2
      radiobutton .usercol.all.disp.attr.type_attr -text "Attribute:" -variable usercol_cf_type -value 2
      grid    .usercol.all.disp.attr.type_attr -sticky w -column 0 -row 3
      menubutton  .usercol.all.disp.attr.att -text "Attribute" -indicatoron 1 -borderwidth 2 -relief raised \
                                             -menu .usercol.all.disp.attr.att.men \
                                             -takefocus 1 -highlightthickness 1 -highlightcolor $win_frm_fg
      menu    .usercol.all.disp.attr.att.men -tearoff 0
      grid    .usercol.all.disp.attr.att -sticky we -column 1 -row 3
      grid    columnconfigure .usercol.all.disp.attr 1 -weight 1
      pack    .usercol.all.disp.attr -side top -anchor nw -fill x
      grid    .usercol.all.disp -sticky wen -row 1 -column 2 -padx 5

      frame   .usercol.all.disp.fmt -relief ridge -borderwidth 2
      label   .usercol.all.disp.fmt.lab_fmt -text "Text format:"
      grid    .usercol.all.disp.fmt.lab_fmt -sticky w -row 1 -column 0
      text    .usercol.all.disp.fmt.txtdemo -height 2 -width 1 -font $pi_font \
                                            -background $text_bg -wrap none -relief ridge \
                                            -borderwidth 2 -takefocus 0 -highlightthickness 0 \
                                            -exportselection 0 -insertofftime 0 -spacing1 0 -spacing2 0
      set lh [font metrics $pi_font -linespace]
      .usercol.all.disp.fmt.txtdemo tag configure half_line -font [list Helvetica [expr $lh / -2]]
      .usercol.all.disp.fmt.txtdemo tag configure txt_margin -lmargin1 10 -rmargin 10 -justify center
      .usercol.all.disp.fmt.txtdemo tag configure samplet -font $pi_font
      .usercol.all.disp.fmt.txtdemo tag configure sampleb -background $text_bg
      .usercol.all.disp.fmt.txtdemo tag configure samplec -background $text_bg
      .usercol.all.disp.fmt.txtdemo tag configure sampled -foreground $text_fg
      .usercol.all.disp.fmt.txtdemo tag lower samplec
      .usercol.all.disp.fmt.txtdemo insert 1.0 "\n" half_line
      .usercol.all.disp.fmt.txtdemo insert 2.0 " " {txt_margin samplec} \
                                               "****  " {sampleb samplec sampled} \
                                               "Text sample" {samplet sampleb samplec} \
                                               "  ****" {sampleb samplec sampled} \
                                               "\n" {txt_margin samplec}
      bindtags .usercol.all.disp.fmt.txtdemo {all . .usercol.all.disp.fmt.txtdemo}
      grid    .usercol.all.disp.fmt.txtdemo -sticky news -row 0 -column 1 -rowspan 2 -pady 3
      checkbutton .usercol.all.disp.fmt.chk_bold -text "bold" -variable usercol_cf_bold \
                                                 -command UserColsDlg_UpdateFmtDemoText
      grid    .usercol.all.disp.fmt.chk_bold -sticky w -row 2 -column 0
      checkbutton .usercol.all.disp.fmt.chk_underline -text "underline" -variable usercol_cf_underline \
                                                      -command UserColsDlg_UpdateFmtDemoText
      grid    .usercol.all.disp.fmt.chk_underline -sticky w -row 3 -column 0
      checkbutton .usercol.all.disp.fmt.chk_overstrike -text "overstrike" -variable usercol_cf_overstrike \
                                                      -command UserColsDlg_UpdateFmtDemoText
      grid    .usercol.all.disp.fmt.chk_overstrike -sticky w -row 4 -column 0

      menubutton .usercol.all.disp.fmt.mb_fgcolor -text "Text color" -direction flush -indicatoron 1 \
                                                  -takefocus 1 -highlightthickness 1 -borderwidth 2 -relief raised \
                                                  -menu .usercol.all.disp.fmt.mb_fgcolor.men
      grid    .usercol.all.disp.fmt.mb_fgcolor -sticky we -row 2 -column 1
      UserColsDlg_CreateColorMenu .usercol.all.disp.fmt.mb_fgcolor.men fg \
                  {auto fg_UNDEF black fg_RGB000000 white fg_RGBFFFFFF red fg_RGBCC0000 blue fg_RGB0000CC \
                   green fg_RGB00CC00 yellow fg_RGBCCCC00 pink fg_RGBCC00CC cyan fg_RGB00CCCC}

      menubutton .usercol.all.disp.fmt.mb_agcolor -text "Attribute background" -direction flush -indicatoron 1 \
                                                  -takefocus 1 -highlightthickness 1 -borderwidth 2 -relief raised \
                                                  -menu .usercol.all.disp.fmt.mb_agcolor.men
      grid    .usercol.all.disp.fmt.mb_agcolor -sticky we -row 3 -column 1
      UserColsDlg_CreateColorMenu .usercol.all.disp.fmt.mb_agcolor.men ag \
                              {auto bg_UNDEF black ag_RGB000000 white ag_RGBFFFFFF red ag_RGBFFCCCC blue ag_RGBCCCCFF \
                               green ag_RGBCCFFCC yellow ag_RGBFFFFCC pink ag_RGBFFCCFF cyan ag_RGBCCFFFF}

      menubutton .usercol.all.disp.fmt.mb_bgcolor -text "Column background" -direction flush -indicatoron 1 \
                                                  -takefocus 1 -highlightthickness 1 -borderwidth 2 -relief raised \
                                                  -menu .usercol.all.disp.fmt.mb_bgcolor.men
      grid    .usercol.all.disp.fmt.mb_bgcolor -sticky we -row 4 -column 1
      UserColsDlg_CreateColorMenu .usercol.all.disp.fmt.mb_bgcolor.men bg \
                              {auto bg_UNDEF black bg_RGB000000 white bg_RGBFFFFFF red bg_RGBFFCCCC blue bg_RGBCCCCFF \
                               green bg_RGBCCFFCC yellow bg_RGBFFFFCC pink bg_RGBFFCCFF cyan bg_RGBCCFFFF}

      menubutton .usercol.all.disp.fmt.mb_cgcolor -text "Column text color" -direction flush -indicatoron 1 \
                                                  -takefocus 1 -highlightthickness 1 -borderwidth 2 -relief raised \
                                                  -menu .usercol.all.disp.fmt.mb_cgcolor.men
      grid    .usercol.all.disp.fmt.mb_cgcolor -sticky we -row 5 -column 1
      UserColsDlg_CreateColorMenu .usercol.all.disp.fmt.mb_cgcolor.men cg \
                              {auto fg_UNDEF black cg_RGB000000 white cg_RGBFFFFFF red cg_RGBCC0000 blue cg_RGB0000CC \
                               green cg_RGB00CC00 yellow cg_RGBCCCC00 pink cg_RGBCC00CC cyan cg_RGB00CCCC}

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
      button  .usercol.cmd.help -text "Help" -width 7 -command {PopupHelp $helpIndex(Configuration) "Attribute composition"}
      button  .usercol.cmd.del -text "Delete" -command {UserColsDlg_Delete} -width 7
      button  .usercol.cmd.dismiss -text "Dismiss" -width 7 -command {if [UserColsDlg_CheckDiscard] {destroy .usercol}}
      button  .usercol.cmd.apply -text "Apply" -width 7 -command {UserColsDlg_Apply}
      pack    .usercol.cmd.help .usercol.cmd.del .usercol.cmd.dismiss .usercol.cmd.apply -side left -padx 10
      pack    .usercol.cmd -side top -pady 10

      bind    .usercol <Key-F1> {PopupHelp $helpIndex(Configuration) "Attribute composition"}
      bind    .usercol.cmd <Destroy> {+ set usercol_popup 0}
      #bind    .usercol.cmd.ok <Return> {tkButtonInvoke .usercol.cmd.ok}
      #bind    .usercol.cmd.ok <Escape> {tkButtonInvoke .usercol.cmd.abort}
      bind    .usercol <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]
      wm protocol .usercol WM_DELETE_WINDOW {if [UserColsDlg_CheckDiscard] {destroy .usercol}}


      # create drop-down menu with all images
      foreach id [lsort [array names pi_img]] {
         .usercol.all.disp.attr.img.men add radiobutton -image [lindex $pi_img($id) $::pimg_name_idx] \
            -variable usercol_cf_image -value $id \
            -command [concat set usercol_cf_type 1 {;} \
                      .usercol.all.disp.attr.img configure -image [lindex $pi_img($id) $::pimg_name_idx]]
      }
      .usercol.all.disp.attr.img.men entryconfigure 7 -columnbreak 1
      .usercol.all.disp.attr.img.men entryconfigure 14 -columnbreak 1

      # create drop-down menu with all pre-defined columns
      foreach id $colsel_ailist_predef {
         .usercol.all.disp.attr.att.men add radiobutton -label [lindex $colsel_tabs($id) $::cod_label_idx] \
            -variable usercol_cf_attr -value $id \
            -command [concat set usercol_cf_type 2 {;} \
                      [list .usercol.all.disp.attr.att configure -text [lindex $colsel_tabs($id) $::cod_label_idx]]]

         if {[string compare [lindex $colsel_tabs($id) $::cod_menu_idx] none] != 0} {
            .usercol.txt.hmenu.men add radiobutton -label [lindex $colsel_tabs($id) $::cod_label_idx] \
               -variable usercol_cf_hmenu -value "&$id" \
               -command [list .usercol.txt.hmenu configure -text [lindex $colsel_tabs($id) $::cod_label_idx]]
         }
      }
      .usercol.txt.hmenu.men add separator
      .usercol.txt.hmenu.men add radiobutton -label "Used shortcuts" -variable usercol_cf_hmenu -value "&user_def" \
                                             -command {.usercol.txt.hmenu configure -text "Shortcuts"}
      .usercol.txt.hmenu.men add separator
      .usercol.txt.hmenu.men add radiobutton -label "none" -variable usercol_cf_hmenu -value "none" \
                                             -command {.usercol.txt.hmenu configure -text "none"}

      # load cache variables with shortcut names
      foreach sc_tag [array names shortcuts] {
         set usercol_scnames($sc_tag) [lindex $shortcuts($sc_tag) $::fsc_name_idx]
      }
      foreach gtag $remgroup_order {
         set usercol_scnames(rgp_$gtag) [concat Reminder [lindex $remgroups($gtag) $::rgp_name_idx]]
      }
      set usercol_scnames(rgp_all) "all reminders"
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
   .usercol.head.sel.mb.men delete 0 end

   .usercol.head.sel.mb.men add command -label "Create new definition" -command UserColsDlg_NewCol
   .usercol.head.sel.mb.men add separator

   set ailist [lsort -integer [array names usercols]]
   foreach tag $ailist {
      if [info exists colsel_tabs(user_def_$tag)] {
         set uc_def $colsel_tabs(user_def_$tag)
         .usercol.head.sel.mb.men add command -label "[lindex $uc_def $::cod_label_idx] ([lindex $uc_def $::cod_head_idx])" \
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

# dynamically fill popup menu with all currently defined shortcuts (including "hidden" ones)
proc UserColsDlg_FillShortcutMenu {wid is_stand_alone} {
   global shortcuts shortcut_tree

   ShortcutTree_MenuFill .all.shortcuts.list $wid $shortcut_tree shortcuts \
                         UserColsDlg_AddShortcut UserColsDlg_AddShortcut_StateCb

   # append menu entry and pseudo-shortcut name for "no match"
   $wid add separator
   $wid add command -label "*no match*" -command {UserColsDlg_AddShortcut -1}
}

proc UserColsDlg_AddShortcut_StateCb {sc_tag} {
   global usercol_cf_filt

   if [info exists usercol_cf_filt($sc_tag)] {
      return "disabled"
   } else {
      return "normal"
   }
}

# dynamically fill popup menu with all currently defined reminder groups
proc UserColsDlg_FillReminderMenu {wid is_stand_alone} {
   global remgroups remgroup_order
   global usercol_scnames

   foreach gtag $remgroup_order {
      set elem $remgroups($gtag)
      # update name cache in case new reminders were added since the dialog was opened
      set usercol_scnames(rgp_$gtag) [concat Reminder [lindex $elem $::rgp_name_idx]]

      .usercol.all.selcmd.remadd.men add command -label [lindex $elem $::rgp_name_idx] \
         -command [list UserColsDlg_AddShortcut rgp_$gtag]
   }
   .usercol.all.selcmd.remadd.men add separator
   .usercol.all.selcmd.remadd.men add command -label "All reminders" \
         -command {UserColsDlg_AddShortcut rgp_all}
}

# check if the column definition was changed
proc UserColsDlg_CheckDiscard {} {
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
               if {[lindex $filt $::ucf_sctag_idx] == [lindex $usercol_cf_selist $filt_idx]} {
                  # compare the attributes for this shortcut
                  set new $usercol_cf_filt([lindex $usercol_cf_selist $filt_idx])
                  if {([lindex $filt $::ucf_type_idx] != [lindex $new $::ucf_type_idx]) || \
                      ([string compare [lindex $filt $::ucf_value_idx] [lindex $new $::ucf_value_idx]] != 0) || \
                      ([string compare [lindex $filt $::ucf_fmt_idx] [lindex $new $::ucf_fmt_idx]] != 0)} {
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
         if {([string compare [lindex $old $::cod_head_idx] $usercol_cf_head] != 0) ||
             ([string compare [lindex $old $::cod_menu_idx] $usercol_cf_hmenu] != 0) ||
             ([string compare [lindex $old $::cod_label_idx] $usercol_cf_lab] != 0)} {
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
                        -message "You're about to discard your changes in the current definition."]
         set ok [expr [string compare $answer ok] == 0]
      }
   } else {
      set ok 1
   }
   return $ok
}

# callback for column selection (topmost menubutton)
proc UserColsDlg_SelectColumn {tag} {
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
         set sc_tag [lindex $filt $::ucf_sctag_idx]
         if [info exists usercol_scnames($sc_tag)] {
            lappend usercol_cf_selist $sc_tag
            set usercol_cf_filt($sc_tag) $filt
         }
      }
   }

   # display label and column header text
   if [info exists colsel_tabs(user_def_$usercol_cf_tag)] {
      set uc_def $colsel_tabs(user_def_$usercol_cf_tag)
      set usercol_cf_lab [lindex $uc_def $::cod_label_idx]
      set usercol_cf_head [lindex $uc_def $::cod_head_idx]
      set usercol_cf_hmenu [lindex $uc_def $::cod_menu_idx]
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

   focus .usercol.head.sel.cur
   .usercol.head.sel.cur selection range 0 end

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

# create pop-up menu with all text colors
proc UserColsDlg_CreateColorMenu {wid type col_arr} {
   upvar #0 usercol_cf_${type}color cfg_var

   menu    $wid -tearoff 0
   foreach {colstr coldef} $col_arr {
      $wid add radiobutton -label $colstr -variable usercol_cf_${type}color -value $coldef \
                           -command UserColsDlg_UpdateFmtDemoText
   }
   $wid insert 1 separator
   $wid add separator
   $wid add command -label "other..." -command [list UserColsDlg_PopupUdefColorMenu usercol_cf_${type}color $type]
}

# helper function to invoke color selection sub-dialog for user-defined colors
proc UserColsDlg_PopupUdefColorMenu {col_var type} {
   upvar #0 $col_var colcf

   set tmp [tk_chooseColor -initialcolor [TextTag2Color $colcf] \
                           -parent .usercol -title "Select a color"]
   if {[string length $tmp] > 0} {
      set colcf [Color2TextTag $tmp $type]
      UserColsDlg_UpdateFmtDemoText
   }
}

# apply the text format settings to the demo text entry widget
proc UserColsDlg_UpdateFmtDemoText {} {
   global usercol_cf_bold usercol_cf_underline usercol_cf_overstrike
   global usercol_cf_fgcolor usercol_cf_agcolor usercol_cf_bgcolor usercol_cf_cgcolor
   global text_fg text_bg pi_font pi_bold_font

   if $usercol_cf_bold {
      set demo_font $pi_bold_font
   } else {
      set demo_font $pi_font
   }
   if $usercol_cf_underline {
      if {[string compare [lindex $demo_font 2] "normal"] != 0} {
         lappend demo_font "underline"
      } else {
         set demo_font [lreplace $demo_font 2 2 "underline"]
      }
   }
   if $usercol_cf_overstrike {
      if {[string compare [lindex $demo_font 2] "normal"] != 0} {
         lappend demo_font "overstrike"
      } else {
         set demo_font [lreplace $demo_font 2 2 "overstrike"]
      }
   }

   set fgcol [TextTag2Color $usercol_cf_fgcolor]
   .usercol.all.disp.fmt.txtdemo tag configure samplet -font $demo_font -foreground $fgcol

   if {[string compare $usercol_cf_agcolor bg_UNDEF] != 0} {
      set agcol [TextTag2Color $usercol_cf_agcolor]
      .usercol.all.disp.fmt.txtdemo tag configure sampleb -background $agcol
      .usercol.all.disp.fmt.txtdemo tag raise sampleb
   } else {
      .usercol.all.disp.fmt.txtdemo tag lower sampleb
   }

   set bgcol [TextTag2Color $usercol_cf_bgcolor]
   .usercol.all.disp.fmt.txtdemo tag configure samplec -background $bgcol

   set fgcol [TextTag2Color $usercol_cf_cgcolor]
   .usercol.all.disp.fmt.txtdemo tag configure sampled -foreground $fgcol
}

# helper function: store (possibly modified) settings of the selected shortcut
proc UserColsDlg_UpdateShortcutAttribs {} {
   global usercol_cf_selist usercol_cf_filt usercol_cf_filt_idx
   global usercol_cf_type usercol_cf_text usercol_cf_image usercol_cf_attr
   global usercol_cf_bold usercol_cf_underline usercol_cf_overstrike
   global usercol_cf_fgcolor usercol_cf_agcolor usercol_cf_bgcolor usercol_cf_cgcolor

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
      # build array of text format tags
      # (note: rendering engine expects "bold" first, all-column bg color last
      #        because these require special handling)
      set fmt {}
      if {$usercol_cf_type != 1} {
         if $usercol_cf_bold {lappend fmt bold}
         if $usercol_cf_underline {lappend fmt underline}
         if $usercol_cf_overstrike {lappend fmt overstrike}
         if {[string compare $usercol_cf_fgcolor fg_UNDEF] != 0} {lappend fmt $usercol_cf_fgcolor}
      }
      if {[string compare $usercol_cf_agcolor bg_UNDEF] != 0} {lappend fmt $usercol_cf_agcolor}
      if {[string compare $usercol_cf_bgcolor bg_UNDEF] != 0} {lappend fmt $usercol_cf_bgcolor}
      if {[string compare $usercol_cf_cgcolor fg_UNDEF] != 0} {lappend fmt $usercol_cf_cgcolor}

      set usercol_cf_filt($sc_tag) [list $usercol_cf_type $value $fmt 0 $sc_tag]
   }
}

# callback for changes of selection in shortcut listbox
proc UserColsDlg_SelectShortcut {} {
   global usercol_cf_tag usercol_cf_selist usercol_cf_filt_idx usercol_cf_filt
   global usercol_cf_type usercol_cf_text usercol_cf_image usercol_cf_attr
   global usercol_cf_bold usercol_cf_underline usercol_cf_overstrike
   global usercol_cf_fgcolor usercol_cf_agcolor usercol_cf_bgcolor usercol_cf_cgcolor
   global colsel_tabs pi_img

   set sc_idx [.usercol.all.sel.selist curselection]
   if {[llength $sc_idx] == 1} {
      if {$usercol_cf_filt_idx != $sc_idx} {
         # store settings of the previously selected shortcut
         UserColsDlg_UpdateShortcutAttribs

         # display the settings for the newly selected shortcut
         set usercol_cf_filt_idx $sc_idx
         set filt $usercol_cf_filt([lindex $usercol_cf_selist $sc_idx])

         set usercol_cf_type [lindex $filt $::ucf_type_idx]

         set usercol_cf_text {}
         set usercol_cf_image diamond_black
         set usercol_cf_attr title
         if {$usercol_cf_type == 0} {
            set usercol_cf_text [lindex $filt $::ucf_value_idx]
         } elseif {$usercol_cf_type == 1} {
            set usercol_cf_image [lindex $filt $::ucf_value_idx]
         } elseif {$usercol_cf_type == 2} {
            set usercol_cf_attr [lindex $filt $::ucf_value_idx]
         }
         .usercol.all.disp.attr.img configure -image [lindex $pi_img($usercol_cf_image) $::pimg_name_idx]
         .usercol.all.disp.attr.att configure -text [lindex $colsel_tabs($usercol_cf_attr) $::cod_label_idx]

         set usercol_cf_bold 0
         set usercol_cf_underline 0
         set usercol_cf_overstrike 0
         set usercol_cf_fgcolor fg_UNDEF
         set usercol_cf_agcolor bg_UNDEF
         set usercol_cf_bgcolor bg_UNDEF
         set usercol_cf_cgcolor fg_UNDEF
         foreach fmt [lindex $filt $::ucf_fmt_idx] {
            if {[string compare $fmt bold] == 0} {set usercol_cf_bold 1} \
            elseif {[string compare $fmt underline] == 0} {set usercol_cf_underline 1} \
            elseif {[string compare $fmt overstrike] == 0} {set usercol_cf_overstrike 1} \
            elseif {[string compare -length 3 $fmt "fg_"] == 0} {set usercol_cf_fgcolor $fmt} \
            elseif {[string compare -length 3 $fmt "ag_"] == 0} {set usercol_cf_agcolor $fmt} \
            elseif {[string compare -length 3 $fmt "cg_"] == 0} {set usercol_cf_cgcolor $fmt} \
            else {set usercol_cf_bgcolor $fmt}
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
      set usercol_cf_overstrike 0
      set usercol_cf_fgcolor fg_UNDEF
      set usercol_cf_agcolor bg_UNDEF
      set usercol_cf_bgcolor bg_UNDEF
      set usercol_cf_cgcolor fg_UNDEF
      .usercol.all.disp.attr.img configure -image [lindex $pi_img($usercol_cf_image) $::pimg_name_idx]
      .usercol.all.disp.attr.att configure -text [lindex $colsel_tabs($usercol_cf_attr) $::cod_label_idx]
   }
   UserColsDlg_UpdateFmtDemoText
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
      if {$sc_tag != -1} {
         set disp_text $usercol_scnames($sc_tag)
      } else {
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
   global shortcuts usercols colsel_tabs pilistbox_cols pinetbox_rows pibox_type
   global dumphtml_popup

   # save possible changes in the shortcut attributes
   UserColsDlg_UpdateShortcutAttribs

   if {[llength $usercol_cf_selist] == 0} {
      tk_messageBox -icon error -type ok -default ok -parent .usercol \
                    -message "Cannot save an empty shortcut list.  Add at least one shortcut or *no-match*"
      return
   }
   if {([string length $usercol_cf_lab] == 0) || \
       ([string compare $usercol_cf_lab "*unnamed*"] == 0)} {
      if {[string length $usercol_cf_head] == 0} {
         tk_messageBox -icon error -type ok -default ok -parent .usercol \
                       -message "Please enter a description for this definition in the 'Column header text' field. The text used in the programme list and the attribute selection dialog."
         return
      } else {
         set usercol_cf_lab $usercol_cf_head
      }
   }

   set tmpl {}
   foreach tag $usercol_cf_selist {
      if {($tag == -1) ||
          [info exists shortcuts($tag)] || \
          ([UserColsDlg_IsReminderPseudoTag $tag] != -1)} {
         lappend tmpl $usercol_cf_filt($tag)
      }
   }

   if {[info exists colsel_tabs(user_def_$usercol_cf_tag)]} {
      set old $colsel_tabs(user_def_$usercol_cf_tag)
      set width [lindex $old $::cod_width_idx]
      set head_change [expr ([string compare [lindex $old $::cod_head_idx] $usercol_cf_head] != 0) || \
                            ([string compare [lindex $old $::cod_menu_idx] $usercol_cf_hmenu] != 0) || \
                            ([string compare [lindex $old $::cod_label_idx] $usercol_cf_lab] != 0)]
      set is_new 0
   } else {
      set width [UserColsDlg_CalcDefaultWidth $tmpl]
      set head_change 0
      set is_new 1
   }

   # check if the definition is already part of the current column selection
   if {($dumphtml_popup == 0) || $is_new} {
      if {(($pibox_type == 0) && ([lsearch -exact $pilistbox_cols user_def_$usercol_cf_tag] == -1)) || \
          (($pibox_type != 0) && ([lsearch -exact $pinetbox_rows user_def_$usercol_cf_tag] == -1)) } {

         set answer [tk_messageBox -icon question -type yesnocancel -default yes -parent .usercol \
                        -message "Do you want to append the attribute to the programme list?"]
         if {[string compare $answer yes] == 0} {
            if {$pibox_type == 0} {
               lappend pilistbox_cols user_def_$usercol_cf_tag
               set head_change 1
            } else {
               lappend pinetbox_rows user_def_$usercol_cf_tag
            }
         } elseif {[string compare $answer cancel] == 0} {
            return
         }
      }
   }

   set usercols($usercol_cf_tag) $tmpl

   set colsel_tabs(user_def_$usercol_cf_tag) [list $width $usercol_cf_head $usercol_cf_hmenu $usercol_cf_lab]

   # update this column's entry in the column selection drop-down menu
   UserColsDlg_FillColumnSelectionMenu
   # update this column's entry in the column selection configuration dialogs, if currently open
   PiListboxColsel_ColUpdate
   DumpHtml_ColUpdate
   # fill the shortcut cache with all referenced shortcuts
   DownloadUserDefinedColumnFilters
   if {$head_change && ($pibox_type == 0)} {
      UpdatePiListboxColumns
   } else {
      UpdatePiListboxColumParams
   }
   UpdateListboxColorTags
   C_PiBox_Refresh
   UpdateRcFile
}

# callback for "delete" in main command row -> delete the entire column definition
proc UserColsDlg_Delete {} {
   global usercol_cf_tag usercol_cf_selist
   global usercols colsel_tabs pilistbox_cols pinetbox_rows pibox_type
   global dumphtml_colsel dumphtml_use_colsel

   if [info exists usercols($usercol_cf_tag)] {
      set pi_col_idx [lsearch $pilistbox_cols user_def_$usercol_cf_tag]
      if {$pi_col_idx == -1} {
         set pi_col_idx [lsearch $pinetbox_rows user_def_$usercol_cf_tag]
      }
      set msg "You're about to irrecoverably delete this definition."
      if {$pi_col_idx == -1} {
         if $dumphtml_use_colsel {
            set pi_col_idx [lsearch $dumphtml_colsel user_def_$usercol_cf_tag]
         }
         if {$pi_col_idx == -1} {
            set answer [tk_messageBox -icon question -type okcancel -default ok -parent .usercol \
                           -message "$msg Do you want to proceed?"]
         } else {
            set answer [tk_messageBox -icon question -type okcancel -default ok -parent .usercol \
                           -message "$msg This definition is still referenced by the HTML export configuration. Do you want to proceed?"]
         }
      } else {
         set answer [tk_messageBox -icon question -type okcancel -default ok -parent .usercol \
                        -message "$msg Do you want to proceed and remove this definition from the programme listbox?"]
      }
      if {[string compare $answer ok] == 0} {
         unset usercols($usercol_cf_tag)
         unset colsel_tabs(user_def_$usercol_cf_tag)

         set head_change 0
         set pi_col_idx [lsearch $pilistbox_cols user_def_$usercol_cf_tag]
         if {$pi_col_idx != -1} {
            set pilistbox_cols [lreplace $pilistbox_cols $pi_col_idx $pi_col_idx]
            set head_change 1
         }

         set pi_col_idx [lsearch $pinetbox_rows user_def_$usercol_cf_tag]
         if {$pi_col_idx != -1} {
            set pinetbox_rows [lreplace $pinetbox_rows $pi_col_idx $pi_col_idx]
         }

         set html_col_idx [lsearch $dumphtml_colsel user_def_$usercol_cf_tag]
         if {$html_col_idx != -1} {
            set dumphtml_colsel [lreplace $dumphtml_colsel $html_col_idx $html_col_idx]
         }

         DownloadUserDefinedColumnFilters
         if {$head_change && ($pibox_type == 0)} {
            UpdatePiListboxColumns
         } else {
            UpdatePiListboxColumParams
         }
         UpdateListboxColorTags
         C_PiBox_Refresh
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
proc UserColsDlg_CalcDefaultWidth {filt_list} {
   global usercols colsel_tabs usercol_cf_tag
   global font_normal

   set width 32
   foreach filt $filt_list {
      set type [lindex $filt $::ucf_type_idx]
      if {$type == 0} {
         # this shortcut match is disaplyed with a static text
         if {[lsearch [lindex $filt $::ucf_fmt_idx] bold] == -1} {
            set font $font_normal
         } else {
            set font [DeriveFont $font_normal 0 bold]
         }
         set this_width [font measure $font [lindex $filt $::ucf_value_idx]]
         incr width 5

      } elseif {$type == 2} {
         set this_width [lindex $colsel_tabs([lindex $filt $::ucf_value_idx]) $::cod_width_idx]

      } else {
         set this_width 0
      }
      if {$this_width > $width} {
         set width $this_width
      }
   }
   return $width
}

