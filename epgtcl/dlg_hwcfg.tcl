#
#  Configuration dialog for TV card
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
#    Implements a GUI for TV card parameters.
#
#  Author: Tom Zoerner
#
#  $Id: dlg_hwcfg.tcl,v 1.4 2002/12/08 19:59:00 tom Exp tom $
#

##  - Video input
##  - Tuner
##  - PLL init 0/1
##  - thread priority normal/high/real-time
##  - card index 0-4
##  - frequency table index (Western Europe/France)
##
set hwcfg_default {0 0 0 0 0 0}
set hwcf_dsdrv_log 0
set hwcfg_popup 0

set hwcfg_tvapp_idx 0
set hwcfg_tvapp_path {}


# save a TV card index that was selected via the command line
proc HardwareConfigUpdateCardIdx {cardidx} {
   global hwcfg hwcfg_default

   if {![info exists hwcfg]} {
      set hwcfg $hwcfg_default
   }
   if {[lindex $hwcfg 4] != $cardidx} {
      set hwcfg [lreplace $hwcfg 4 4 $cardidx]
      UpdateRcFile
   }
}

#=LOAD=PopupHardwareConfig
#=DYNAMIC=

proc PopupHardwareConfig {} {
   global is_unix netacq_enable fileImage win_frm_fg
   global hwcfg_input_sel
   global hwcfg_tuner_sel hwcfg_tuner_list hwcfg_card_list
   global hwcfg_pll_sel hwcfg_prio_sel hwcfg_cardidx_sel hwcfg_ftable_sel
   global hwcfg_popup hwcfg hwcfg_default
   global hwcfg_tmp_tvapp_list hwcfg_tmp_tvapp_idx hwcfg_tmp_tvapp_path
   global hwcfg_chk_tvapp_idx hwcfg_chk_tvapp_path
   global hwcfg_tvapp_path hwcfg_tvapp_idx
   global hwcf_dsdrv_log

   if {$hwcfg_popup == 0} {
      if {[C_IsNetAcqActive default]} {
         # warn that params do not affect acquisition running remotely
         set answer [tk_messageBox -type okcancel -icon info -message "Please note that this dialog does not update acquisition parameters on server side."]
         if {[string compare $answer "ok"] != 0} {
            return
         }
      }

      CreateTransientPopup .hwcfg "TV card input configuration"
      set hwcfg_popup 1

      set hwcfg_tuner_list [C_HwCfgGetTunerList]
      set hwcfg_card_list [C_HwCfgGetTvCardList]
      if {![info exists hwcfg]} {
         set hwcfg $hwcfg_default
      }
      set hwcfg_input_sel [lindex $hwcfg 0]
      set hwcfg_tuner_sel [lindex $hwcfg 1]
      set hwcfg_pll_sel [lindex $hwcfg 2]
      set hwcfg_prio_sel [lindex $hwcfg 3]
      set hwcfg_cardidx_sel [lindex $hwcfg 4]
      set hwcfg_ftable_sel [lindex $hwcfg 5]

      if {!$is_unix} {
         set hwcfg_tmp_tvapp_path $hwcfg_tvapp_path
         set hwcfg_tmp_tvapp_idx $hwcfg_tvapp_idx
         set hwcfg_tmp_tvapp_list [C_Tvapp_GetTvappList]
         set hwcfg_chk_tvapp_idx $hwcfg_tvapp_idx
         set hwcfg_chk_tvapp_path $hwcfg_tvapp_path

         # create TV app selection popdown menu and "Load config" command button
         frame .hwcfg.opt1 -borderwidth 1 -relief raised
         label  .hwcfg.opt1.lab -text "Select a TV application from which the\nTV card configuration can be copied:" -justify left
         pack   .hwcfg.opt1.lab -side top -anchor w -padx 5

         frame  .hwcfg.opt1.apptype
         label  .hwcfg.opt1.apptype.lab -text "TV application:"
         pack   .hwcfg.opt1.apptype.lab -side left -padx 5
         menubutton .hwcfg.opt1.apptype.mb -text [lindex $hwcfg_tmp_tvapp_list $hwcfg_tmp_tvapp_idx] \
                         -justify center -relief raised -borderwidth 2 -menu .hwcfg.opt1.apptype.mb.men \
                         -indicatoron 1 -takefocus 1 -highlightthickness 1 -highlightcolor $win_frm_fg
         menu  .hwcfg.opt1.apptype.mb.men -tearoff 0
         set cmd {.hwcfg.opt1.apptype.mb configure -text [lindex $hwcfg_tmp_tvapp_list $hwcfg_tmp_tvapp_idx]}
         set idx 0
         foreach name $hwcfg_tmp_tvapp_list {
            .hwcfg.opt1.apptype.mb.men add radiobutton -label $name \
                         -variable hwcfg_tmp_tvapp_idx -value $idx \
                         -command HardwareSetTvappCfg
            incr idx
         }
         pack   .hwcfg.opt1.apptype.mb -side left -padx 10 -fill x -expand 1
         button .hwcfg.opt1.apptype.load -text "Load config" -command HardwareConfigLoadFromTvapp
         pack   .hwcfg.opt1.apptype.load -side right -padx 10 -fill x -expand 1
         pack   .hwcfg.opt1.apptype -side top -pady 5 -fill x

         # create entry field and command button to configure TV app directory
         frame .hwcfg.opt1.name
         label .hwcfg.opt1.name.prompt -text "TV app. directory:"
         pack .hwcfg.opt1.name.prompt -side left
         entry .hwcfg.opt1.name.filename -textvariable hwcfg_tmp_tvapp_path -font {courier -12 normal} -width 33
         pack .hwcfg.opt1.name.filename -side left -padx 5
         bind .hwcfg.opt1.name.filename <Enter> {SelectTextOnFocus %W}
         button .hwcfg.opt1.name.dlgbut -image $fileImage -command {
            set tmp [tk_chooseDirectory -parent .hwcfg \
                        -initialdir $hwcfg_tmp_tvapp_path \
                        -mustexist 1]
            if {[string length $tmp] > 0} {
               set hwcfg_tmp_tvapp_path $tmp
            }
            unset tmp
         }
         pack .hwcfg.opt1.name.dlgbut -side left -padx 5
         pack .hwcfg.opt1.name -side top -padx 5 -pady 5
         pack .hwcfg.opt1 -side top -anchor w -fill x
         # set state and text of the entry field and button
         HardwareSetTvappCfg
      }

      frame .hwcfg.opt2 -borderwidth [expr ($is_unix ? 0 : 1)] -relief raised
      # create menu to select video input
      frame .hwcfg.opt2.input
      label .hwcfg.opt2.input.curname -text "Video source: "
      menubutton .hwcfg.opt2.input.mb -text "Configure" -menu .hwcfg.opt2.input.mb.menu -relief raised -borderwidth 1 -indicatoron 1 -underline 0
      menu .hwcfg.opt2.input.mb.menu -tearoff 0 -postcommand {PostDynamicMenu .hwcfg.opt2.input.mb.menu HardwareCreateInputMenu {}}
      pack .hwcfg.opt2.input.curname -side left -padx 10 -anchor w -expand 1
      pack .hwcfg.opt2.input.mb -side left -padx 10 -anchor e
      pack .hwcfg.opt2.input -side top -pady 5 -anchor w -fill x

      if {!$is_unix} {
         # create menu for tuner selection
         frame .hwcfg.opt2.tuner
         label .hwcfg.opt2.tuner.curname -text "Tuner: [lindex $hwcfg_tuner_list $hwcfg_tuner_sel]"
         menubutton .hwcfg.opt2.tuner.mb -text "Configure" -menu .hwcfg.opt2.tuner.mb.menu -relief raised -borderwidth 1 -indicatoron 1 -underline 1
         menu .hwcfg.opt2.tuner.mb.menu -tearoff 0
         set idx 0
         foreach name $hwcfg_tuner_list {
            .hwcfg.opt2.tuner.mb.menu add radiobutton -variable hwcfg_tuner_sel -value $idx -label $name \
                                                 -command {.hwcfg.opt2.tuner.curname configure -text "Tuner: [lindex $hwcfg_tuner_list $hwcfg_tuner_sel]"}
            incr idx
         }
         pack .hwcfg.opt2.tuner.curname -side left -padx 10 -anchor w -expand 1
         pack .hwcfg.opt2.tuner.mb -side left -padx 10 -anchor e
         pack .hwcfg.opt2.tuner -side top -pady 5 -anchor w -fill x

         # create radiobuttons to choose PLL initialization
         frame .hwcfg.opt2.pll
         radiobutton .hwcfg.opt2.pll.pll_none -text "No PLL" -variable hwcfg_pll_sel -value 0
         radiobutton .hwcfg.opt2.pll.pll_28 -text "PLL 28 MHz" -variable hwcfg_pll_sel -value 1
         radiobutton .hwcfg.opt2.pll.pll_35 -text "PLL 35 MHz" -variable hwcfg_pll_sel -value 2
         pack .hwcfg.opt2.pll.pll_none .hwcfg.opt2.pll.pll_28 .hwcfg.opt2.pll.pll_35 -side left
         pack .hwcfg.opt2.pll -side top -padx 10 -anchor w
         pack .hwcfg.opt2 -side top -fill x

         # create menu or checkbuttons to select TV card
         frame .hwcfg.opt3 -borderwidth 1 -relief raised
         frame .hwcfg.opt3.card
         label .hwcfg.opt3.card.label -text "TV card: "
         pack .hwcfg.opt3.card.label -side left -padx 10
         set idx 0
         foreach name $hwcfg_card_list {
            radiobutton .hwcfg.opt3.card.idx$idx -text $name -variable hwcfg_cardidx_sel -value $idx -command HardwareConfigCard
            pack .hwcfg.opt3.card.idx$idx -side left
            incr idx
         }
         pack .hwcfg.opt3.card -side top -anchor w -pady 5

         # create checkbuttons to select acquisition priority
         frame .hwcfg.opt3.prio
         label .hwcfg.opt3.prio.label -text "Priority:"
         radiobutton .hwcfg.opt3.prio.normal -text "normal" -variable hwcfg_prio_sel -value 0
         radiobutton .hwcfg.opt3.prio.high   -text "high" -variable hwcfg_prio_sel -value 1
         radiobutton .hwcfg.opt3.prio.crit   -text "real-time" -variable hwcfg_prio_sel -value 2
         pack .hwcfg.opt3.prio.label -side left -padx 10
         pack .hwcfg.opt3.prio.normal .hwcfg.opt3.prio.high .hwcfg.opt3.prio.crit -side left
         pack .hwcfg.opt3.prio -side top -anchor w
      } else {
         pack .hwcfg.opt2 -side top -fill x
         frame .hwcfg.opt3
      }

      # create checkbuttons to select frequency table
      frame .hwcfg.opt3.ftable
      label .hwcfg.opt3.ftable.label -text "Frequency table:"
      radiobutton .hwcfg.opt3.ftable.tab0 -text "Western Europe" -variable hwcfg_ftable_sel -value 0
      radiobutton .hwcfg.opt3.ftable.tab1 -text "France" -variable hwcfg_ftable_sel -value 1
      pack .hwcfg.opt3.ftable.label -side left -padx 10
      pack .hwcfg.opt3.ftable.tab0 .hwcfg.opt3.ftable.tab1 -side left
      pack .hwcfg.opt3.ftable -side top -anchor w

      # create menu to select TV card
      if {$is_unix} {
         frame .hwcfg.opt3.card
         label .hwcfg.opt3.card.label -text "TV card: "
         pack .hwcfg.opt3.card.label -side left -padx 10

         menubutton .hwcfg.opt3.card.mb -text "Configure" -menu .hwcfg.opt3.card.mb.menu \
                                        -relief raised -borderwidth 1 -indicatoron 1 -underline 2
         menu .hwcfg.opt3.card.mb.menu -tearoff 0
         pack .hwcfg.opt3.card.mb -side right -padx 10 -anchor e
         set idx 0
         foreach name $hwcfg_card_list {
            .hwcfg.opt3.card.mb.menu add radiobutton -variable hwcfg_cardidx_sel -value $idx -label $name -command HardwareConfigCard
            incr idx
         }
         pack .hwcfg.opt3.card -side top -anchor w -pady 5 -fill x
      } else {
         # create checkbutton to enable Bt8x8 driver debug logging
         checkbutton .hwcfg.opt3.dsdrv_log -text "Bt8x8 driver logging into file 'dsdrv.log'" -variable hwcf_dsdrv_log
         pack .hwcfg.opt3.dsdrv_log -side top -anchor w -padx 10
      }
      pack .hwcfg.opt3 -side top -fill x

      # display current card name and input source
      HardwareConfigCard

      # create standard command buttons
      frame .hwcfg.cmd
      button .hwcfg.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration) "TV card input"}
      button .hwcfg.cmd.abort -text "Abort" -width 5 -command {destroy .hwcfg}
      button .hwcfg.cmd.ok -text "Ok" -width 5 -command {HardwareConfigQuit} -default active
      pack .hwcfg.cmd.help .hwcfg.cmd.abort .hwcfg.cmd.ok -side left -padx 10
      pack .hwcfg.cmd -side top -pady 10

      bind .hwcfg <Key-F1> {PopupHelp $helpIndex(Configuration) "TV card input"}
      bind .hwcfg <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]
      bind .hwcfg.cmd <Destroy> {+ set hwcfg_popup 0}
      bind .hwcfg.cmd.ok <Return> {tkButtonInvoke .hwcfg.cmd.ok}
      bind .hwcfg.cmd.ok <Escape> {tkButtonInvoke .hwcfg.cmd.abort}
      focus .hwcfg.cmd.ok

   } else {
      raise .hwcfg
   }
}

# Card index has changed - update video source name
proc HardwareConfigCard {} {
   global is_unix
   global hwcfg_input_sel hwcfg_input_list
   global hwcfg_cardidx_sel hwcfg_card_list

   set hwcfg_input_list [C_HwCfgGetInputList $hwcfg_cardidx_sel]

   if {$is_unix} {
      .hwcfg.opt3.card.label configure -text "TV card: [lindex $hwcfg_card_list $hwcfg_cardidx_sel]"
   }

   if {$hwcfg_input_sel >= [lindex $hwcfg_input_list $hwcfg_input_sel]} {
      set hwcfg_input_sel 0
   }

   if {[llength $hwcfg_input_list] > 0} {
      .hwcfg.opt2.input.curname configure -text "Video source: [lindex $hwcfg_input_list $hwcfg_input_sel]"
   } else {
      .hwcfg.opt2.input.curname configure -text "Video source: #$hwcfg_input_sel (video device busy)"
   }
}

# callback for dynamic input source menu
proc HardwareCreateInputMenu {widget dummy} {
   global hwcfg_input_sel hwcfg_input_list hwcfg_cardidx_sel

   # get list of source names directly from the driver
   set hwcfg_input_list [C_HwCfgGetInputList $hwcfg_cardidx_sel]

   if {[llength $hwcfg_input_list] > 0} {
      set idx 0
      foreach name $hwcfg_input_list {
         .hwcfg.opt2.input.mb.menu add radiobutton -variable hwcfg_input_sel -value $idx -label $name \
                                              -command {.hwcfg.opt2.input.curname configure -text "Video source: [lindex $hwcfg_input_list $hwcfg_input_sel]"}
         incr idx
      }
   } else {
      .hwcfg.opt2.input.mb.menu add command -label "Video device not available:" -state disabled
      .hwcfg.opt2.input.mb.menu add command -label "cannot switch video source" -state disabled
   }
}

# Leave popup with OK button
proc HardwareConfigQuit {} {
   global is_unix
   global hwcfg_input_sel hwcfg_tuner_sel
   global hwcfg_pll_sel hwcfg_prio_sel hwcfg_cardidx_sel hwcfg_ftable_sel
   global hwcfg hwcfg_default
   global hwcfg_tmp_tvapp_idx hwcfg_tmp_tvapp_path hwcfg_tmp_tvapp_list
   global hwcfg_chk_tvapp_idx hwcfg_chk_tvapp_path
   global hwcfg_tvapp_path hwcfg_tvapp_idx
   global wintvapp_idx wintvapp_path

   if { !$is_unix && ($hwcfg_input_sel == 0) && ($hwcfg_tuner_sel == 0) } {
      set answer [tk_messageBox -type okcancel -default cancel -icon warning -parent .hwcfg \
                     -message "You haven't selected a tuner - acquisition will not be possible!"]
   } elseif { !$is_unix && ($hwcfg_tmp_tvapp_idx != 0) && \
              ($hwcfg_tmp_tvapp_idx != $hwcfg_tvapp_idx) && \
              ($hwcfg_tmp_tvapp_idx != $hwcfg_chk_tvapp_idx)} {
      set answer [tk_messageBox -type okcancel -default cancel -icon warning -parent .hwcfg \
                     -message "You have selected a TV app. but not loaded it's TV card configuration. This setting will have no effect!"]
   } elseif { !$is_unix && ($hwcfg_tmp_tvapp_idx != 0) && \
              ([string compare $hwcfg_tmp_tvapp_path $hwcfg_tvapp_path] != 0) && \
              ([string compare $hwcfg_tmp_tvapp_path $hwcfg_chk_tvapp_path] != 0)} {
      set answer [tk_messageBox -type okcancel -default cancel -icon warning -parent .hwcfg \
                     -message "You have changed the TV app. path but not loaded the TV card configuration. This setting will have no effect!"]
   } else {
      set answer "ok"
   }

   if {[string compare $answer "ok"] == 0} {
      # save config into the global variables
      set hwcfg [list $hwcfg_input_sel $hwcfg_tuner_sel $hwcfg_pll_sel $hwcfg_prio_sel $hwcfg_cardidx_sel $hwcfg_ftable_sel]

      if {!$is_unix} {
         set hwcfg_tvapp_idx $hwcfg_tmp_tvapp_idx
         set hwcfg_tvapp_path $hwcfg_tmp_tvapp_path
         # copy the TV app config for the channel table, if not yet configured
         if {$wintvapp_idx == 0} {
            set wintvapp_idx $hwcfg_tmp_tvapp_idx
            set wintvapp_path $hwcfg_tmp_tvapp_path
            # update the TV app name which is used in dialogs
            UpdateTvappName
         }
      }
      UpdateRcFile
      C_UpdateHardwareConfig
      destroy .hwcfg
   }
}

# callback for "Load Config" button
proc HardwareConfigLoadFromTvapp {} {
   global hwcfg_tmp_tvapp_idx hwcfg_tmp_tvapp_path
   global hwcfg_chk_tvapp_idx hwcfg_chk_tvapp_path

   # remember which config was loaded/tested
   set hwcfg_chk_tvapp_idx $hwcfg_tmp_tvapp_idx
   set hwcfg_chk_tvapp_path $hwcfg_tmp_tvapp_path

   # note: enclose cmd in list to avoid that zero-length params vanish due to concat
   uplevel #0 [list C_Tvapp_LoadHwConfig $hwcfg_tmp_tvapp_idx $hwcfg_tmp_tvapp_path]
}

# callback for TV application type popup: disable or enable path entry field
proc HardwareSetTvappCfg {} {
   global hwcfg_tmp_tvapp_list
   global hwcfg_tmp_tvapp_idx hwcfg_tmp_tvapp_path
   global text_bg

   .hwcfg.opt1.apptype.mb configure -text [lindex $hwcfg_tmp_tvapp_list $hwcfg_tmp_tvapp_idx]

   if {[C_Tvapp_CfgNeedsPath $hwcfg_tmp_tvapp_idx] == 0} {
      .hwcfg.opt1.name.filename configure -state normal -background #c0c0c0 -textvariable {}
      .hwcfg.opt1.name.filename delete 0 end
      .hwcfg.opt1.name.filename configure -state disabled
      .hwcfg.opt1.name.dlgbut configure -state disabled
   } else {
      .hwcfg.opt1.name.filename configure -state normal -background $text_bg -textvariable hwcfg_tmp_tvapp_path
      .hwcfg.opt1.name.dlgbut configure -state normal
   }
}

