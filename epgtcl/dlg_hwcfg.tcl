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
#  $Id: dlg_hwcfg.tcl,v 1.14 2004/04/02 11:30:17 tom Exp $
#

set hwcf_cardidx 0
set hwcf_input 0
set hwcf_acq_prio 0
set hwcf_slicer_type 0
set hwcf_wdm_stop 0
set hwcf_dsdrv_log 0

set hwcfg_popup 0
set tvcard_popup 0

# Windows only: array indices for tvcardcf()
#=CONST= ::tvcf_chip_idx         0
#=CONST= ::tvcf_card_idx         1
#=CONST= ::tvcf_tuner_idx        2
#=CONST= ::tvcf_pll_idx          3
#=CONST= ::tvcf_idx_count        4

# Used for Windows only: PCI chip IDs (stored at $::tvcf_chip_idx in tvcardcf)
#=CONST= ::pci_id_unknown        0
#=CONST= ::pci_id_brooktree      0x109e
#=CONST= ::pci_id_phlips         0x1131
#=CONST= ::pci_id_conexant       0x14F1

#=LOAD=PopupHardwareConfig
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Video input configuration dialog
##
proc PopupHardwareConfig {} {
   global is_unix netacq_enable
   global hwcf_cardidx hwcf_input hwcf_acq_prio hwcf_slicer_type hwcf_dsdrv_log hwcf_wdm_stop
   global hwcfg_cardidx_sel hwcfg_input_sel hwcfg_prio_sel hwcfg_slicer_sel hwcf_wdm_stop_sel
   global hwcfg_card_list hwcfg_chip_list
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

      set hwcfg_cardidx_sel $hwcf_cardidx
      set hwcfg_input_sel $hwcf_input
      set hwcfg_prio_sel $hwcf_acq_prio
      set hwcfg_slicer_sel $hwcf_slicer_type
      set hwcf_wdm_stop_sel $hwcf_wdm_stop
      # scan PCI bus an retrieve card names (must be called before other driver queries)
      HardwareConfigLoad 0


      frame .hwcfg.opt1
      # create menu to select video input
      frame .hwcfg.opt1.input
      label .hwcfg.opt1.input.curname -text "Video source: "
      menubutton .hwcfg.opt1.input.mb -text "Select" -menu .hwcfg.opt1.input.mb.menu \
                                      -relief raised -borderwidth 2 -indicatoron 1 -underline 0
      menu .hwcfg.opt1.input.mb.menu -tearoff 0 -postcommand {PostDynamicMenu .hwcfg.opt1.input.mb.menu HardwareCreateInputMenu {}}
      pack .hwcfg.opt1.input.curname -side left -padx 10 -anchor w -expand 1
      pack .hwcfg.opt1.input.mb -side left -padx 10 -anchor e
      pack .hwcfg.opt1.input -side top -pady 5 -anchor w -fill x
      pack .hwcfg.opt1 -side top -fill x

      # create menu to select TV card
      frame .hwcfg.opt2
      frame .hwcfg.opt2.card
      label .hwcfg.opt2.card.label -text "TV card: "
      pack .hwcfg.opt2.card.label -side left -padx 10

      menubutton .hwcfg.opt2.card.mb -text "Select" -menu .hwcfg.opt2.card.mb.menu \
                                     -relief raised -borderwidth 2 -indicatoron 1 -underline 1
      menu .hwcfg.opt2.card.mb.menu -tearoff 0
      pack .hwcfg.opt2.card.mb -side right -padx 10 -anchor e
      pack .hwcfg.opt2.card -side top -anchor w -pady 5 -fill x
      pack .hwcfg.opt2 -side top -fill x

      if {!$is_unix} {
         button .hwcfg.tvcardcfg -text "Configure card" -command PopupTvCardConfig
         pack   .hwcfg.tvcardcfg -side top -padx 10 -pady 5
      }

      # create radio buttons to select slicer
      frame .hwcfg.opt3
      label .hwcfg.opt3.lab_slicer -text "Slicer quality:"
      grid  .hwcfg.opt3.lab_slicer -row 0 -column 0 -sticky w
      radiobutton .hwcfg.opt3.slicer_auto -text "automatic" -variable hwcfg_slicer_sel -value 0
      grid        .hwcfg.opt3.slicer_auto -row 0 -column 1 -sticky w
      radiobutton .hwcfg.opt3.slicer_trivial   -text "simple" -variable hwcfg_slicer_sel -value 1
      grid        .hwcfg.opt3.slicer_trivial -row 0 -column 2 -sticky w
      radiobutton .hwcfg.opt3.slicer_zvbi   -text "elaborate" -variable hwcfg_slicer_sel -value 2
      grid        .hwcfg.opt3.slicer_zvbi -row 0 -column 3 -sticky w

      if {!$is_unix} {
         # create radio buttons to select acquisition priority
         label .hwcfg.opt3.lab_prio -text "Priority:"
         grid  .hwcfg.opt3.lab_prio -row 1 -column 0 -sticky w
         radiobutton .hwcfg.opt3.prio_normal -text "normal" -variable hwcfg_prio_sel -value 0
         grid        .hwcfg.opt3.prio_normal -row 1 -column 1 -sticky w
         radiobutton .hwcfg.opt3.prio_high   -text "high" -variable hwcfg_prio_sel -value 1
         grid        .hwcfg.opt3.prio_high   -row 1 -column 2 -sticky w
         radiobutton .hwcfg.opt3.prio_crit   -text "real-time" -variable hwcfg_prio_sel -value 2
         grid        .hwcfg.opt3.prio_crit   -row 1 -column 3 -sticky w

         # create checkbutton to enable DScaler driver debug logging
         checkbutton .hwcfg.opt3.dsdrv_log -text "Driver startup logging into file 'dsdrv.log'" -variable hwcf_dsdrv_log
         grid        .hwcfg.opt3.dsdrv_log -row 2 -column 0 -columnspan 4 -sticky w

         # create checkbutton to allow stopping WDM before dsdrv start
         checkbutton .hwcfg.opt3.wdm_stop  -text "Stop conflicting WDM drivers before start" -variable hwcf_wdm_stop_sel
         grid        .hwcfg.opt3.wdm_stop  -row 3 -column 0 -columnspan 4 -sticky w
      }
      grid columnconfigure .hwcfg.opt3 0 -weight 1
      pack .hwcfg.opt3 -side top -padx 10 -pady 5 -fill x -anchor w

      # add card names list to menu
      if {([llength $hwcfg_card_list] > 1) || \
          ($hwcfg_cardidx_sel >= [llength $hwcfg_card_list])} {
         set idx 0
         foreach name $hwcfg_card_list {
            .hwcfg.opt2.card.mb.menu add radiobutton -variable hwcfg_cardidx_sel -value $idx \
                                                     -label $name -command HardwareConfigCard
            incr idx
         }
      } else {
         .hwcfg.opt2.card.mb configure -state disabled
      }
      # display current card name and input source
      HardwareConfigCard

      # create standard command buttons
      frame .hwcfg.cmd
      button .hwcfg.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration) "TV card input"}
      button .hwcfg.cmd.abort -text "Abort" -width 5 -command {HardwareConfigQuit 0}
      button .hwcfg.cmd.ok -text "Ok" -width 5 -command {HardwareConfigQuit 1} -default active
      pack .hwcfg.cmd.help .hwcfg.cmd.abort .hwcfg.cmd.ok -side left -padx 10
      pack .hwcfg.cmd -side top -pady 10

      bind .hwcfg <Key-F1> {PopupHelp $helpIndex(Configuration) "TV card input"}
      bind .hwcfg <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]
      bind .hwcfg.cmd <Destroy> {+ set hwcfg_popup 0 ; catch {destroy .tvcard}}
      bind .hwcfg.cmd.ok <Return> {tkButtonInvoke .hwcfg.cmd.ok}
      bind .hwcfg.cmd.ok <Escape> {tkButtonInvoke .hwcfg.cmd.abort}
      wm protocol .hwcfg WM_DELETE_WINDOW {HardwareConfigQuit 0}
      focus .hwcfg.cmd.ok

   } else {
      raise .hwcfg
   }
}

# Query the driver for a list of available TV cards
proc HardwareConfigLoad {showDrvErr} {
   global hwcfg_card_list hwcfg_chip_list
   global is_unix

   if $is_unix {
      set hwcfg_card_list [C_HwCfgGetTvCards $showDrvErr]
   } else {

      set hwcfg_card_list {}
      set hwcfg_chip_list {}

      # note: this call loads the driver temporarily for a PCI scan
      # if that fails, card IDs are taken from the config variables, but chip IDs are set to 0
      foreach {chip name} [C_HwCfgGetTvCards $showDrvErr] {
         lappend hwcfg_card_list $name
         lappend hwcfg_chip_list $chip
      }
   }

   # update card type names in the card selection menu
   if {([llength [info commands .hwcfg.opt2.card.mb.menu]] > 0) && \
       ([llength $hwcfg_card_list] > 1)} {
      set idx 0
      foreach name $hwcfg_card_list {
         .hwcfg.opt2.card.mb.menu entryconfigure $idx -label $name
         incr idx
      }
   }
}

# Card index has changed - update card and video input names
proc HardwareConfigCard {} {
   global is_unix
   global hwcfg_input_sel hwcfg_cardidx_sel
   global hwcfg_input_list hwcfg_card_list hwcfg_chip_list
   global tvcardcf

   if {$hwcfg_cardidx_sel < [llength $hwcfg_card_list]} {
      .hwcfg.opt2.card.label configure -text "TV card: [lindex $hwcfg_card_list $hwcfg_cardidx_sel]"

      # get list of video input names directly from the driver
      if {!$is_unix && [info exists tvcardcf($hwcfg_cardidx_sel)]} {
         set card_type [lindex $tvcardcf($hwcfg_cardidx_sel) $::tvcf_card_idx]
      } else {
         set card_type 0
      }
      set hwcfg_input_list [C_HwCfgGetInputList $hwcfg_cardidx_sel $card_type]

      if {$hwcfg_input_sel >= [lindex $hwcfg_input_list $hwcfg_input_sel]} {
         set hwcfg_input_sel 0
      }

      if {[llength $hwcfg_input_list] > 0} {
         .hwcfg.opt1.input.curname configure -text "Video source: [lindex $hwcfg_input_list $hwcfg_input_sel]"
      } else {
         .hwcfg.opt1.input.curname configure -text "Video source: #$hwcfg_input_sel (video device busy)"
      }

      if {!$is_unix} {
         # WDM driver stop is supported for CX23881 only (should not be required for others)
         if {([lindex $hwcfg_chip_list $hwcfg_cardidx_sel] >> 16) == $::pci_id_conexant} {
            .hwcfg.opt3.wdm_stop configure -state normal
         } else {
            .hwcfg.opt3.wdm_stop configure -state disabled
         }
         .hwcfg.tvcardcfg configure -state normal
         catch {destroy .tvcard}
      }
   } else {
      # invalid card index (e.g. card was removed or invalid -card command line parameter
      .hwcfg.opt2.card.label configure -text "TV card: device #[expr $hwcfg_cardidx_sel + 1] not found"

      set hwcfg_input_list "None"
      set hwcfg_input_sel 0
      .hwcfg.opt1.input.curname configure -text "Video source: none (invalid device)"

      if {!$is_unix} {
         .hwcfg.opt3.wdm_stop configure -state disabled
         .hwcfg.tvcardcfg configure -state disabled
      }
   }
}

# callback for dynamic input source menu
proc HardwareCreateInputMenu {widget dummy} {
   global is_unix
   global hwcfg_input_sel hwcfg_cardidx_sel
   global hwcfg_input_list
   global tvcardcf

   # get list of video input names directly from the driver
   if {!$is_unix && [info exists tvcardcf($hwcfg_cardidx_sel)]} {
      set card_type [lindex $tvcardcf($hwcfg_cardidx_sel) $::tvcf_card_idx]
   } else {
      set card_type 0
   }
   set hwcfg_input_list [C_HwCfgGetInputList $hwcfg_cardidx_sel $card_type]

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
   global hwcf_cardidx hwcf_input hwcf_acq_prio hwcf_slicer_type hwcf_dsdrv_log hwcf_wdm_stop
   global hwcfg_cardidx_sel hwcfg_input_sel hwcfg_prio_sel hwcfg_slicer_sel hwcf_wdm_stop_sel
   global hwcfg_card_list hwcfg_input_list
   global tvcardcf

   # Windows only: check if the user selected a card & tuner model
   if { !$is_unix && ![info exists tvcardcf($hwcfg_cardidx_sel)] } {
      set answer [tk_messageBox -type okcancel -default cancel -icon warning -parent .hwcfg \
                     -message "You haven't configured the selected TV card - acquisition will not be possible!"]
   } else {
      set answer "ok"
   }
   # Windows only: warn about WDM stop option
   if {([string compare $answer "ok"] == 0) &&
       !$is_unix && !$hwcf_wdm_stop && $hwcf_wdm_stop_sel} {
      set answer [tk_messageBox -type okcancel -default cancel -icon warning -parent .hwcfg \
                     -message "WARNING: WDM stop is an experimental feature and should only be used as a last resort.  Press 'Cancel' and then 'Help' for details."]
   }

   if {[string compare $answer "ok"] == 0} {

      if $is_ok {
         # OK button -> save config into the global variables
         set hwcf_cardidx $hwcfg_cardidx_sel
         set hwcf_input $hwcfg_input_sel
         set hwcf_acq_prio $hwcfg_prio_sel
         set hwcf_slicer_type $hwcfg_slicer_sel
         set hwcf_wdm_stop $hwcf_wdm_stop_sel

         if {!$is_unix && $hwcf_dsdrv_log} {
            set msg "To produce a driver startup logfile "
            if $::menuStatusStartAcq {
               append msg "first stop, then re-"
            } else {
               append msg "now, "
            }
            append msg "start acquisition via the 'Control' menu."
            tk_messageBox -type ok -icon info -parent .hwcfg -message $msg
         }

         C_UpdateHardwareConfig
         UpdateRcFile
      }

      # free memory
      unset hwcfg_card_list hwcfg_input_list
      unset hwcfg_cardidx_sel hwcfg_input_sel hwcfg_prio_sel

      # close config dialogs
      destroy .hwcfg
      catch {destroy .tvcard}
   }
}


##  --------------------------------------------------------------------------
##  WIN32 TV card configuration dialog
##
proc PopupTvCardConfig {} {
   global is_unix netacq_enable fileImage win_frm_fg font_normal
   global hwcfg_cardidx_sel hwcfg_chip_list
   global tvcard_cardtype_names tvcard_cardtype_idxlist
   global tvcard_tuner_names
   global tvcard_cardtyp_sel tvcard_tuner_sel tvcard_pll_sel
   global tvcardcf
   global tvcard_popup

   if {$tvcard_popup == 0} {
      # check if the card was identified by a PCI scan
      if {([llength $hwcfg_chip_list] == 0) || \
          ([lindex $hwcfg_chip_list $hwcfg_cardidx_sel] == $::pci_id_unknown)} {
         # no PCI scan yet -> try again
         HardwareConfigLoad 1

         if {[lindex $hwcfg_chip_list $hwcfg_cardidx_sel] == $::pci_id_unknown} {

            tk_messageBox -type ok -default ok -icon error -parent .hwcfg \
                          -message "PCI scan failed. Close all other video apps, then try again."
            return
         } else {
            # PCI scan successful -> refresh card & input list
            HardwareConfigCard
         }
      }
      # check card index (esp. if 0 cards found)
      if {$hwcfg_cardidx_sel >= [llength $hwcfg_chip_list]} {
         return
      }

      CreateTransientPopup .tvcard "TV card hardware configuration" .hwcfg
      set tvcard_popup 1

      array unset tvcard_cardtype_names
      set idx 0
      foreach name [C_HwCfgGetTvCardList $hwcfg_cardidx_sel] {
         set tvcard_cardtype_names($idx) $name
         incr idx
      }
      set tvcard_cardtype_idxlist [lsort -command SortCardNames [array names tvcard_cardtype_names]]

      array unset tvcard_tuner_names
      set idx 0
      foreach name [C_HwCfgGetTunerList] {
         set tvcard_tuner_names($idx) $name
         incr idx
      }

      if [info exists tvcardcf($hwcfg_cardidx_sel)] {
         set tvcard_cardtyp_sel [lindex $tvcardcf($hwcfg_cardidx_sel) $::tvcf_card_idx]
         set tvcard_tuner_sel [lindex $tvcardcf($hwcfg_cardidx_sel) $::tvcf_tuner_idx]
         set tvcard_pll_sel [lindex $tvcardcf($hwcfg_cardidx_sel) $::tvcf_pll_idx]

         # check parameter validity
         if {![info exists tvcard_cardtype_names($tvcard_cardtyp_sel)]} {
            set tvcard_cardtyp_sel 0
         }
         if {! [info exists tvcard_tuner_names($tvcard_tuner_sel)]} {
            set tvcard_tuner_sel 0
         }
         if {$tvcard_pll_sel > 2} {
            set tvcard_pll_sel 0
         }
      } else {
         set tvcard_cardtyp_sel 0
         set tvcard_tuner_sel 0
         set tvcard_pll_sel 0
      }


      # header
      if {[llength $hwcfg_chip_list] > 1} {
         set title "Configure card #[expr $hwcfg_cardidx_sel + 1] of [llength $hwcfg_chip_list]"
      } else {
         set title "Configure TV card and tuner models"
      }
      frame  .tvcard.opt0 -borderwidth 1 -relief raised
      label  .tvcard.opt0.curcard -text $title -font [DeriveFont $font_normal 2 bold]
      label  .tvcard.opt0.curname -text "Card type: $tvcard_cardtype_names($tvcard_cardtyp_sel)"
      pack   .tvcard.opt0.curcard .tvcard.opt0.curname -side top -pady 5 -padx 5 -fill x -anchor w
      pack   .tvcard.opt0 -side top -fill both -anchor nw

      # create menu for card selection
      frame  .tvcard.opt1 -borderwidth 1 -relief raised
      scrollbar .tvcard.opt1.sv -orient vertical -command {.tvcard.opt1.lnames yview}
      listbox .tvcard.opt1.lnames -selectmode browse -width 40 -height 10 -yscrollcommand {.tvcard.opt1.sv set}
      bind    .tvcard.opt1.lnames <Double-Button-1> TvCardConfigManual
      foreach idx $tvcard_cardtype_idxlist {
         .tvcard.opt1.lnames insert end $tvcard_cardtype_names($idx)
      }
      set tmp [lsearch $tvcard_cardtype_idxlist $tvcard_cardtyp_sel]
      if {$tmp != -1} {
         .tvcard.opt1.lnames selection set $tmp
         .tvcard.opt1.lnames see $tmp
      }
      pack   .tvcard.opt1.sv -side left -fill y
      pack   .tvcard.opt1.lnames -side left -anchor w -expand 1
      frame  .tvcard.opt1.cmd
      button .tvcard.opt1.cmd.auto -text "Autodetect" -width 10 -command TvCardConfigAuto
      button .tvcard.opt1.cmd.manual -text "Pick from list" -width 10 -command TvCardConfigManual
      pack   .tvcard.opt1.cmd.auto .tvcard.opt1.cmd.manual -side top
      pack   .tvcard.opt1.cmd -side left -anchor n
      pack   .tvcard.opt1 -side top -fill x

      # create menu for tuner selection
      frame .tvcard.opt2 -borderwidth 1 -relief raised
      frame .tvcard.opt2.tuner
      label .tvcard.opt2.tuner.curname -text "Tuner type: $tvcard_tuner_names($tvcard_tuner_sel)"
      menubutton .tvcard.opt2.tuner.mb -text "Select" -menu .tvcard.opt2.tuner.mb.menu \
                                       -relief raised -borderwidth 2 -indicatoron 1 -underline 0
      CreateTunerMenu .tvcard.opt2.tuner.mb.menu
      pack .tvcard.opt2.tuner.curname -side left -padx 10 -anchor w -expand 1
      pack .tvcard.opt2.tuner.mb -side left -padx 10 -anchor e
      pack .tvcard.opt2.tuner -side top -pady 5 -anchor w -fill x

      # create radiobuttons to choose PLL initialization
      frame .tvcard.opt2.pll
      radiobutton .tvcard.opt2.pll.pll_none -text "No PLL" -variable tvcard_pll_sel -value 0
      radiobutton .tvcard.opt2.pll.pll_28 -text "PLL 28 MHz" -variable tvcard_pll_sel -value 1
      radiobutton .tvcard.opt2.pll.pll_35 -text "PLL 35 MHz" -variable tvcard_pll_sel -value 2
      pack .tvcard.opt2.pll.pll_none .tvcard.opt2.pll.pll_28 .tvcard.opt2.pll.pll_35 -side left -fill x -expand 1
      pack .tvcard.opt2.pll -side top -padx 10 -fill x -pady 5
      pack .tvcard.opt2 -side top -fill x

      # PLL selection is required for Bt8x8 cards only
      if {([lindex $hwcfg_chip_list $hwcfg_cardidx_sel] >> 16) != $::pci_id_brooktree} {
         .tvcard.opt2.pll.pll_none configure -state disabled
         .tvcard.opt2.pll.pll_28 configure -state disabled
         .tvcard.opt2.pll.pll_35 configure -state disabled
      }

      # create standard command buttons
      frame .tvcard.cmd
      button .tvcard.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration) "TV card input"}
      button .tvcard.cmd.abort -text "Abort" -width 5 -command {destroy .tvcard}
      button .tvcard.cmd.ok -text "Ok" -width 5 -command {TvCardConfigQuit} -default active
      pack .tvcard.cmd.help .tvcard.cmd.abort .tvcard.cmd.ok -side left -padx 10
      pack .tvcard.cmd -side top -pady 10

      bind .tvcard <Key-F1> {PopupHelp $helpIndex(Configuration) "TV card input"}
      bind .tvcard <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]
      bind .tvcard.cmd <Destroy> {+ set tvcard_popup 0}
      bind .tvcard.cmd.ok <Return> {tkButtonInvoke .tvcard.cmd.ok}
      bind .tvcard.cmd.ok <Escape> {tkButtonInvoke .tvcard.cmd.abort}
      focus .tvcard.cmd.ok

   } else {
      raise .tvcard
   }
}

# Leave popup with OK button
proc TvCardConfigQuit {} {
   global is_unix
   global tvcard_cardtyp_sel tvcard_tuner_sel tvcard_pll_sel
   global hwcfg_cardidx_sel hwcfg_chip_list
   global tvcardcf
   global hwcfg_popup

   if { ($tvcard_tuner_sel == 0) } {
      set answer [tk_messageBox -type okcancel -default cancel -icon warning -parent .tvcard \
                     -message "You haven't selected a tuner - acquisition will not be possible!"]
   } else {
      set answer "ok"
   }

   if {[string compare $answer "ok"] == 0} {

      set chip [lindex $hwcfg_chip_list $hwcfg_cardidx_sel]

      # save config into the global variable
      # (caution: order implies array indices: tvcf_chip_idx, tvcf_card_idx, tvcf_tuner_idx, tvcf_pll_idx)
      set tvcardcf($hwcfg_cardidx_sel) [list $chip \
                                             $tvcard_cardtyp_sel \
                                             $tvcard_tuner_sel \
                                             $tvcard_pll_sel]

      UpdateRcFile
      C_UpdateHardwareConfig
      destroy .tvcard

      if $hwcfg_popup {
         HardwareConfigLoad 0
         HardwareConfigCard
      }
   }
}

# attempt to auto-detect the card & tuner
proc TvCardConfigAuto {} {
   global menuStatusStartAcq hwcf_cardidx hwcfg_cardidx_sel
   global tvcard_cardtyp_sel tvcard_tuner_sel tvcard_pll_sel
   global tvcard_cardtype_idxlist tvcard_cardtype_names
   global tvcard_tuner_names

   # if acquisition is active for a different card it has to be stopped
   if {$menuStatusStartAcq && ($hwcfg_cardidx_sel != $hwcf_cardidx)} {
      set answer [tk_messageBox -type okcancel -default ok -icon warning -parent .tvcard \
                     -message "Nextview acquisition has to be stopped to allow auto-detection for a different TV card."]
      if {[string compare $answer "ok"] != 0} {
         return
      }
      C_ToggleAcq 0 0
   }

   set tmpl [C_HwCfgQueryCardParams $hwcfg_cardidx_sel -1]
   if {[llength $tmpl] == 3} {

      if {[lindex $tmpl 0] > 0} {
         set tvcard_cardtyp_sel [lindex $tmpl 0]
         .tvcard.opt0.curname configure -text "Card type: $tvcard_cardtype_names($tvcard_cardtyp_sel)"
         set tmp [lsearch $tvcard_cardtype_idxlist $tvcard_cardtyp_sel]
         if {$tmp != -1} {
            .tvcard.opt1.lnames selection clear 0 end
            .tvcard.opt1.lnames selection set $tmp
            .tvcard.opt1.lnames see $tmp
         }

         if {[lindex $tmpl 1] > 0} {
            set tvcard_tuner_sel [lindex $tmpl 1]
         } else {
            tk_messageBox -type ok -default ok -icon warning -parent .tvcard \
                          -message "TV card successfully identified, however TV tuner still unknown - please select a tuner type from the list below."
         }
         set tvcard_pll_sel [lindex $tmpl 2]

         .tvcard.opt2.tuner.curname configure -text "Tuner type: $tvcard_tuner_names($tvcard_tuner_sel)"

      } else {
         tk_messageBox -type ok -default ok -icon warning -parent .tvcard \
                       -message "TV card could not be identified - please select a card type from the list."
      }
   }
}

# retrieve tuner & misc. params for manually selected card
proc TvCardConfigManual {} {
   global tvcard_cardtyp_sel tvcard_tuner_sel tvcard_pll_sel
   global tvcard_cardtype_idxlist tvcard_cardtype_names
   global tvcard_tuner_names
   global hwcfg_cardidx_sel

   set sel [.tvcard.opt1.lnames curselection]
   if {[llength $sel] == 1} {
      set tvcard_cardtyp_sel [lindex $tvcard_cardtype_idxlist $sel]

      set tmpl [C_HwCfgQueryCardParams $hwcfg_cardidx_sel $tvcard_cardtyp_sel]

      if {[llength $tmpl] == 3} {
         #set cardtyp [lindex $tmpl 0]
         if {[lindex $tmpl 1] > 0} {
            set tvcard_tuner_sel [lindex $tmpl 1]
         } else {
            tk_messageBox -type ok -default ok -icon warning -parent .tvcard \
                          -message "TV tuner could not be identified - please select a tuner type from the list below."
         }
         set tvcard_pll_sel [lindex $tmpl 2]

         .tvcard.opt0.curname configure -text "Card type: $tvcard_cardtype_names($tvcard_cardtyp_sel)"
         .tvcard.opt2.tuner.curname configure -text "Tuner type: $tvcard_tuner_names($tvcard_tuner_sel)"
      }
   }
}

# create a menu cascade with radio buttons for all tuner types
proc CreateTunerMenu {wid} {
   global tvcard_tuner_names

   menu ${wid} -tearoff 0
   menu ${wid}.philips -tearoff 0
   menu ${wid}.temic -tearoff 0
   menu ${wid}.alps -tearoff 0
   menu ${wid}.lg -tearoff 0
   menu ${wid}.misc -tearoff 0
   ${wid} add cascade -menu ${wid}.philips -label "Philips"
   ${wid} add cascade -menu ${wid}.temic -label "Temic"
   ${wid} add cascade -menu ${wid}.alps -label "ALPS"
   ${wid} add cascade -menu ${wid}.lg -label "LG"
   ${wid} add cascade -menu ${wid}.misc -label "other"

   foreach idx [lsort -command SortTunerNames [array names tvcard_tuner_names]] {
      if {[string match -nocase "*philips*" $tvcard_tuner_names($idx)]} {
         set mwid philips
      } elseif {[string match -nocase "*temic*" $tvcard_tuner_names($idx)]} {
         set mwid temic
      } elseif {[string match -nocase "*alps*" $tvcard_tuner_names($idx)]} {
         set mwid alps
      } elseif {[string match -nocase "LG *" $tvcard_tuner_names($idx)]} {
         set mwid lg
      } else {
         set mwid misc
      }
      ${wid}.$mwid add radiobutton -variable tvcard_tuner_sel -value $idx -label $tvcard_tuner_names($idx) \
                                   -command {.tvcard.opt2.tuner.curname configure -text "Tuner: $tvcard_tuner_names($tvcard_tuner_sel)"}
   }
}

proc SortCardNames {a b} {
   global tvcard_cardtype_names

   return [string compare -nocase $tvcard_cardtype_names($a) $tvcard_cardtype_names($b)]
}

proc SortTunerNames {a b} {
   global tvcard_tuner_names

   return [string compare -nocase $tvcard_tuner_names($a) $tvcard_tuner_names($b)]
}

