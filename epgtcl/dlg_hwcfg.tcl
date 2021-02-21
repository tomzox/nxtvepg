#
#  Configuration dialog for TV card
#
#  Copyright (C) 1999-2011, 2020-2021 T. Zoerner
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
set hwcfg_popup 0
set tvcard_popup 0

set hwcf_acq_reenable 0
set hwcfg_drvsrc_sel 0

# Driver type selection
# (note: undef is already replaced by default type at C level)
#=CONST= ::tvcf_drvsrc_undef     -2
#=CONST= ::tvcf_drvsrc_none      -1
# - WIN32 only -
#=CONST= ::tvcf_drvsrc_wdm       0
# - UNIX only -
#=CONST= ::tvcf_drvsrc_v4l       0
#=CONST= ::tvcf_drvsrc_dvb       1

# Causes for acquisition re-enable in "hwcf_acq_reenable":
# 0: don't reenable
# 1: only if quit with 'Ok'
# 2: was temporary disabled, re-enable also if quit with 'Abort'
#=CONST= ::tvcf_acq_disa_none    0
#=CONST= ::tvcf_acq_disa_drv     1
#=CONST= ::tvcf_acq_disa_tmp     2

# Used for return values of proc C_GetHardwareConfig
#=CONST= ::hwcf_ret_cardidx_idx 0
#=CONST= ::hwcf_ret_input_idx 1
#=CONST= ::hwcf_ret_drvsrc_idx 2
#=CONST= ::hwcf_ret_prio_idx 3
#=CONST= ::hwcf_ret_slicer_idx 4

#=LOAD=PopupHardwareConfig
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Video input configuration dialog
##
proc PopupHardwareConfig {} {
   global is_unix netacq_enable
   global hwcfg_cardidx_sel hwcfg_input_sel hwcfg_drvsrc_sel
   global hwcfg_prio_sel hwcfg_slicer_sel
   global hwcfg_card_list
   global hwcfg_popup

   if {$hwcfg_popup == 0} {
      if {[C_IsNetAcqActive default]} {
         # warn that params do not affect acquisition running remotely
         set answer [tk_messageBox -type okcancel -icon info -message "Please note that this dialog does not update acquisition parameters on server side."]
         if {[string compare $answer "ok"] != 0} {
            return
         }
      }

      CreateTransientPopup .hwcfg "Video input configuration"
      set hwcfg_popup 1

      set hwcfg [C_GetHardwareConfig]
      set hwcfg_cardidx_sel [lindex $hwcfg $::hwcf_ret_cardidx_idx]
      set hwcfg_input_sel [lindex $hwcfg $::hwcf_ret_input_idx]
      set hwcfg_drvsrc_sel [lindex $hwcfg $::hwcf_ret_drvsrc_idx]
      set hwcfg_prio_sel [lindex $hwcfg $::hwcf_ret_prio_idx]
      set hwcfg_slicer_sel [lindex $hwcfg $::hwcf_ret_slicer_idx]

      # create menu to select TV card
      #if {!$is_unix} {
         labelframe .hwcfg.opt2 -text "TV card & driver selection"
      #} else {
      #   frame .hwcfg.opt2
      #}
      frame .hwcfg.opt2.card
      label .hwcfg.opt2.card.label -text "TV card: "
      pack .hwcfg.opt2.card.label -side left -padx 10

      menubutton .hwcfg.opt2.card.mb -text "Select" -menu .hwcfg.opt2.card.mb.menu \
                                     -indicatoron 1 -underline 1
      config_menubutton .hwcfg.opt2.card.mb
      menu .hwcfg.opt2.card.mb.menu -tearoff 0
      pack .hwcfg.opt2.card.mb -side right -padx 10 -anchor e
      pack .hwcfg.opt2.card -side top -anchor w -pady 5 -fill x

      if {$is_unix} {
         frame .hwcfg.opt2.drvsrc
         radiobutton .hwcfg.opt2.drvsrc.src_dvb -text "Digital TV (DVB)" \
                           -variable hwcfg_drvsrc_sel -value $::tvcf_drvsrc_dvb -command HwCfg_DrvTypeChanged
         grid .hwcfg.opt2.drvsrc.src_dvb -row 0 -column 0 -sticky w
         radiobutton .hwcfg.opt2.drvsrc.src_anal -text "Analog TV" \
                           -variable hwcfg_drvsrc_sel -value $::tvcf_drvsrc_v4l -command HwCfg_DrvTypeChanged
         grid .hwcfg.opt2.drvsrc.src_anal -row 1 -column 0 -sticky w
         radiobutton .hwcfg.opt2.drvsrc.src_none -text "None (no device access)" \
                           -variable hwcfg_drvsrc_sel -value $::tvcf_drvsrc_none -command HwCfg_DrvTypeChanged
         grid .hwcfg.opt2.drvsrc.src_none -row 2 -column 0 -sticky w
         pack .hwcfg.opt2.drvsrc -side top -padx 5 -pady 3 -anchor w
         pack .hwcfg.opt2 -side top -fill x -padx 5 -pady 3
      } else {
         frame .hwcfg.opt2.drvsrc
         radiobutton .hwcfg.opt2.drvsrc.src_wdm -text "WDM (card vendor's driver)" \
                           -variable hwcfg_drvsrc_sel -value $::tvcf_drvsrc_wdm -command HwCfg_DrvTypeChanged
         grid .hwcfg.opt2.drvsrc.src_wdm -row 0 -column 0 -sticky w
         radiobutton .hwcfg.opt2.drvsrc.src_none -text "None (no TV card)" \
                           -variable hwcfg_drvsrc_sel -value $::tvcf_drvsrc_none -command HwCfg_DrvTypeChanged
         grid .hwcfg.opt2.drvsrc.src_none -row 2 -column 0 -sticky w
         pack .hwcfg.opt2.drvsrc -side top -padx 5 -pady 3 -anchor w
         pack .hwcfg.opt2 -side top -fill x -padx 5 -pady 3
      }

      # input selection
      if {!$is_unix} {
         labelframe .hwcfg.opt1 -text "Video input connectors"
      } else {
         frame .hwcfg.opt1
      }
      # create menu to select video input
      frame .hwcfg.opt1.input
      label .hwcfg.opt1.input.curname -text "Current input: "
      menubutton .hwcfg.opt1.input.mb -text "Select" -menu .hwcfg.opt1.input.mb.menu \
                                      -indicatoron 1 -underline 0
      config_menubutton .hwcfg.opt1.input.mb
      menu .hwcfg.opt1.input.mb.menu -tearoff 0 -postcommand {PostDynamicMenu .hwcfg.opt1.input.mb.menu HardwareCreateInputMenu {}}
      pack .hwcfg.opt1.input.curname -side left -padx 10 -anchor w -expand 1
      pack .hwcfg.opt1.input.mb -side left -padx 10 -anchor e
      pack .hwcfg.opt1.input -side top -pady 5 -anchor w -fill x
      pack .hwcfg.opt1 -side top -fill x -padx 5 -pady 3

      # create radio buttons to select slicer
      if {!$is_unix} {
         labelframe .hwcfg.opt4 -text "Options"
      } else {
         frame .hwcfg.opt4
      }
      label .hwcfg.opt4.lab_slicer -text "Slicer quality:"
      grid  .hwcfg.opt4.lab_slicer -row 1 -column 0 -sticky w
      radiobutton .hwcfg.opt4.slicer_auto -text "automatic" -variable hwcfg_slicer_sel -value 0
      grid        .hwcfg.opt4.slicer_auto -row 1 -column 1 -sticky w
      radiobutton .hwcfg.opt4.slicer_trivial   -text "simple" -variable hwcfg_slicer_sel -value 1
      grid        .hwcfg.opt4.slicer_trivial -row 1 -column 2 -sticky w
      radiobutton .hwcfg.opt4.slicer_zvbi   -text "elaborate" -variable hwcfg_slicer_sel -value 2
      grid        .hwcfg.opt4.slicer_zvbi -row 1 -column 3 -sticky w

      if {!$is_unix} {
         # create radio buttons to select acquisition priority
         label .hwcfg.opt4.lab_prio -text "Priority:"
         grid  .hwcfg.opt4.lab_prio -row 2 -column 0 -sticky w
         radiobutton .hwcfg.opt4.prio_normal -text "normal" -variable hwcfg_prio_sel -value 0
         grid        .hwcfg.opt4.prio_normal -row 2 -column 1 -sticky w
         radiobutton .hwcfg.opt4.prio_high   -text "high" -variable hwcfg_prio_sel -value 1
         grid        .hwcfg.opt4.prio_high   -row 2 -column 2 -sticky w
         radiobutton .hwcfg.opt4.prio_crit   -text "real-time" -variable hwcfg_prio_sel -value 2
         grid        .hwcfg.opt4.prio_crit   -row 2 -column 3 -sticky w
      }
      grid columnconfigure .hwcfg.opt4 0 -weight 1
      pack .hwcfg.opt4 -side top -fill x -padx 5 -pady 3

      # scan PCI bus and retrieve card names (must be called before other driver queries)
      HwCfg_LoadTvCardList 1

      # display current card name and input source
      HwCfg_TvCardChanged

      # create standard command buttons
      frame .hwcfg.cmd
      button .hwcfg.cmd.help -text "Help" -width 6 -command {PopupHelp $helpIndex(Configure menu) "TV card input"}
      button .hwcfg.cmd.abort -text "Abort" -width 6 -command {HardwareConfigQuit 0}
      button .hwcfg.cmd.ok -text "Ok" -width 6 -command {HardwareConfigQuit 1} -default active
      pack .hwcfg.cmd.help .hwcfg.cmd.abort .hwcfg.cmd.ok -side left -padx 10
      pack .hwcfg.cmd -side top -pady 10

      bind .hwcfg <Key-F1> {PopupHelp $helpIndex(Configure menu) "TV card input"}
      bind .hwcfg <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]
      bind .hwcfg.cmd <Destroy> {+ set hwcfg_popup 0 }
      bind .hwcfg.cmd.ok <Return> {tkButtonInvoke .hwcfg.cmd.ok}
      bind .hwcfg.cmd.ok <Escape> {tkButtonInvoke .hwcfg.cmd.abort}
      wm protocol .hwcfg WM_DELETE_WINDOW {HardwareConfigQuit 0}
      focus .hwcfg.cmd.ok

   } else {
      raise .hwcfg
   }
}

# Query the driver for a list of available TV cards
proc HwCfg_LoadTvCardList {showDrvErr} {
   global hwcfg_card_list
   global hwcfg_cardidx_sel hwcfg_drvsrc_sel
   global is_unix

   # note: For WIN32 this call loads the driver temporarily for a PCI scan
   # if that fails, card IDs are taken from the config variables, but chip IDs are set to 0
   if {$hwcfg_drvsrc_sel != $::tvcf_drvsrc_none} {
      set hwcfg_card_list [C_HwCfgScanTvCards $hwcfg_drvsrc_sel $showDrvErr]
   } else {
      set hwcfg_card_list {}
   }

   .hwcfg.opt2.card.mb.menu delete 0 end
   if {([llength $hwcfg_card_list] > 1) || \
       ($hwcfg_cardidx_sel >= [llength $hwcfg_card_list])} {
      set idx 0
      foreach name $hwcfg_card_list {
         .hwcfg.opt2.card.mb.menu add radiobutton -variable hwcfg_cardidx_sel -value $idx \
                                                  -label $name -command HwCfg_TvCardChanged
         incr idx
      }
   } else {
      .hwcfg.opt2.card.mb configure -state disabled
   }
}

# Card index has changed - update card and video input names
proc HwCfg_TvCardChanged {} {
   global is_unix
   global hwcfg_input_sel hwcfg_cardidx_sel hwcfg_drvsrc_sel
   global hwcfg_input_list hwcfg_card_list
   global hwcf_acq_reenable

   if {$hwcfg_cardidx_sel < [llength $hwcfg_card_list]} {
      .hwcfg.opt2.card.label configure -text "TV card: [lindex $hwcfg_card_list $hwcfg_cardidx_sel]"

      # WIN32: retrieve card type from RC file (because input list differs between card types)
      if {!$is_unix} {
         # stop acquisition when selecting a new WDM source (required to query input list)
         set hwcfg [C_GetHardwareConfig]
         if {[C_IsAcqEnabled] &&
             ([lindex $hwcfg $::hwcf_ret_drvsrc_idx] == $::tvcf_drvsrc_wdm) &&
             ([lindex $hwcfg $::hwcf_ret_cardidx_idx] != $hwcfg_cardidx_sel)} {
            C_ToggleAcq 0 0
            set hwcf_acq_reenable $::tvcf_acq_disa_tmp
         }
      }

      # get list of video input names directly from the driver
      set hwcfg_input_list [C_HwCfgGetInputList $hwcfg_cardidx_sel $hwcfg_drvsrc_sel]

      if {$hwcfg_input_sel >= [lindex $hwcfg_input_list $hwcfg_input_sel]} {
         set hwcfg_input_sel 0
      }

      if {[llength $hwcfg_input_list] > 0} {
         .hwcfg.opt1.input.curname configure -text "Video source: [lindex $hwcfg_input_list $hwcfg_input_sel]"
      } else {
         .hwcfg.opt1.input.curname configure -text "Video source: #$hwcfg_input_sel (video device busy)"
      }

   } else {
      # invalid card index (e.g. card was removed or invalid -card command line parameter
      # or (Windows only) driver type set to "none"
      if {$hwcfg_drvsrc_sel != $::tvcf_drvsrc_none} {
         .hwcfg.opt2.card.label configure -text "TV card: device #[expr $hwcfg_cardidx_sel + 1] not found"
      } else {
         .hwcfg.opt2.card.label configure -text "TV card: none"
      }

      set hwcfg_input_list "None"
      set hwcfg_input_sel 0
      if {$hwcfg_drvsrc_sel != $::tvcf_drvsrc_none} {
         .hwcfg.opt1.input.curname configure -text "Video source: none (invalid device)"
      } else {
         .hwcfg.opt1.input.curname configure -text "Video source: none"
      }
   }
}

proc HwCfg_DrvTypeChanged {} {
   global is_unix
   global hwcfg_drvsrc_sel

   if {!$is_unix} {
      if {[lsearch -exact [pack slaves .hwcfg] .hwcfg.opt3] != -1} {
         pack forget .hwcfg.opt3 -side top -fill x -padx 5 -pady 3
      }
   }
   HwCfg_LoadTvCardList 1
   HwCfg_TvCardChanged
}

# callback for dynamic input source menu
proc HardwareCreateInputMenu {widget dummy} {
   global is_unix
   global hwcfg_input_sel hwcfg_cardidx_sel hwcfg_drvsrc_sel
   global hwcfg_input_list

   # get list of video input names directly from the driver
   set hwcfg_input_list [C_HwCfgGetInputList $hwcfg_cardidx_sel $hwcfg_drvsrc_sel]

   if {[llength $hwcfg_input_list] > 0} {
      set idx 0
      foreach name $hwcfg_input_list {
         .hwcfg.opt1.input.mb.menu add radiobutton -variable hwcfg_input_sel -value $idx -label $name \
                                                   -command {.hwcfg.opt1.input.curname configure -text "Video source: [lindex $hwcfg_input_list $hwcfg_input_sel]"}
         incr idx
      }
   } else {
      .hwcfg.opt1.input.mb.menu add command -label "Video device not available:" -state disabled
      .hwcfg.opt1.input.mb.menu add command -label "cannot switch video source" -state disabled
   }
}

# Leave popup with OK button
proc HardwareConfigQuit {is_ok} {
   global is_unix
   global hwcfg_cardidx_sel hwcfg_input_sel hwcfg_drvsrc_sel
   global hwcfg_prio_sel hwcfg_slicer_sel
   global hwcfg_card_list hwcfg_input_list
   global hwcf_acq_reenable

   set answer "ok"
   if {$is_ok && ($hwcfg_drvsrc_sel != $::tvcf_drvsrc_none)} {
      # check if the user selected a valid card
      if {$hwcfg_cardidx_sel >= [llength $hwcfg_card_list]} {
         set answer [tk_messageBox -type okcancel -default cancel -icon warning -parent .hwcfg \
                        -message "An invalid TV card is still selected - acquisition cannot be enabled!"]
      }
   }

   if {[string compare $answer "ok"] == 0} {

      if {$is_ok} {
         # OK button -> save config into the global variables

         C_UpdateHardwareConfig $hwcfg_cardidx_sel $hwcfg_input_sel $hwcfg_drvsrc_sel \
                                $hwcfg_prio_sel $hwcfg_slicer_sel

         if {!$is_unix && $hwcf_acq_reenable && ![C_IsAcqEnabled]} {
            set hwcf_acq_reenable $::tvcf_acq_disa_none
            C_ToggleAcq 1 0
         }
      } else {

         if {!$is_unix && ($hwcf_acq_reenable == $::tvcf_acq_disa_tmp) &&
             ![C_IsAcqEnabled]} {
            set hwcf_acq_reenable $::tvcf_acq_disa_none
            C_ToggleAcq 1 0
         }
      }

      # free memory
      unset hwcfg_card_list hwcfg_input_list
      unset hwcfg_cardidx_sel hwcfg_input_sel hwcfg_prio_sel

      # close config dialogs
      destroy .hwcfg
   }
}
