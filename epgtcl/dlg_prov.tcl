#
#  Configuration dialogs for provider scan, selection and merging
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
#    Implements configuration dialogs that allow to select a provider from
#    the list or merge providers.
#
#  Author: Tom Zoerner
#
#  $Id: dlg_prov.tcl,v 1.9 2003/03/31 19:17:24 tom Exp tom $
#
set provwin_popup 0
set provmerge_popup 0

set epgscan_popup 0
set epgscan_opt_slow 0
set epgscan_opt_refresh 0
set epgscan_opt_ftable 0

set tzcfg_default {1 60 0}
set timezone_popup 0

#=LOAD=ProvWin_Create
#=LOAD=PopupProviderMerge
#=LOAD=PopupEpgScan
#=LOAD=PopupTimeZone
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Handling of the Provider Selection Pop-up
##
proc ProvWin_Create {} {
   global provwin_popup
   global font_normal entry_disabledforeground
   global provwin_servicename
   global provwin_ailist

   if {$provwin_popup == 0} {

      # build list of available providers
      set provwin_ailist {}
      foreach {cni name} [C_GetProvCnisAndNames] {
         # build list of CNIs
         lappend provwin_ailist $cni
         # build array of provider network names
         set provwin_names($cni) $name
      }
      # sort CNI list according to user preferences
      set provwin_ailist [SortProvList $provwin_ailist]

      if {[llength $provwin_ailist] == 0} {
         # no providers found -> abort
         tk_messageBox -type ok -icon info -message "There are no providers available yet.\nPlease start a provider scan from the Configure menu."
         return
      }

      CreateTransientPopup .provwin "Provider Selection"
      set provwin_popup 1

      # list of providers at the left side of the window
      frame .provwin.n
      frame .provwin.n.b
      scrollbar .provwin.n.b.sb -orient vertical -command {.provwin.n.b.list yview}
      pack .provwin.n.b.sb -side left -fill y
      listbox .provwin.n.b.list -relief ridge -selectmode single -exportselection 0 \
                                -width 12 -height 5 -yscrollcommand {.provwin.n.b.sb set}
      pack .provwin.n.b.list -side left -fill both -expand 1
      pack .provwin.n.b -side left -fill both -expand 1
      bind .provwin.n.b.list <ButtonRelease-1> {+ ProvWin_Select}
      bind .provwin.n.b.list <KeyRelease-space> {+ ProvWin_Select}

      # provider info at the right side of the window
      if {[info exists provwin_servicename]} {unset provwin_servicename}
      frame .provwin.n.info
      frame .provwin.n.info.service
      label .provwin.n.info.service.header -text "Name of service"
      pack  .provwin.n.info.service.header -side top -anchor nw
      entry .provwin.n.info.service.name -state disabled $entry_disabledforeground black \
                                         -textvariable provwin_servicename -width 45
      pack  .provwin.n.info.service.name -side top -anchor nw -fill x
      pack  .provwin.n.info.service -side top -anchor nw -fill x

      frame .provwin.n.info.net
      label .provwin.n.info.net.header -text "List of networks"
      pack .provwin.n.info.net.header -side top -anchor nw
      text .provwin.n.info.net.list -width 45 -height 5 -wrap word -font $font_normal -insertofftime 0
      bindtags .provwin.n.info.net.list {TextReadOnly . all}
      pack .provwin.n.info.net.list -side top -anchor nw -fill both -expand 1
      pack .provwin.n.info.net -side top -anchor nw -fill both -expand 1

      # OI block header and message
      label .provwin.n.info.oiheader -text "OSD header and message"
      pack .provwin.n.info.oiheader -side top -anchor nw
      text .provwin.n.info.oimsg -width 45 -height 6 -wrap word -font $font_normal -insertofftime 0
      bindtags .provwin.n.info.oimsg {TextReadOnly . all}
      pack .provwin.n.info.oimsg -side top -anchor nw -fill both -expand 1
      pack .provwin.n.info -side left -padx 10 -fill both -expand 1
      pack .provwin.n -side top -pady 10 -fill both -expand 1

      # buttons at the bottom of the window
      frame .provwin.cmd
      button .provwin.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration) "Select provider"}
      button .provwin.cmd.abort -text "Abort" -width 5 -command {destroy .provwin}
      button .provwin.cmd.ok -text "Ok" -width 5 -command ProvWin_Exit -default active
      bind .provwin.cmd.ok <Return> {tkButtonInvoke .provwin.cmd.ok}
      bind .provwin.cmd.ok <Escape> {tkButtonInvoke .provwin.cmd.abort}
      pack .provwin.cmd.help .provwin.cmd.abort .provwin.cmd.ok -side left -padx 10
      pack .provwin.cmd -side top -pady 5

      bind .provwin <Key-F1> {PopupHelp $helpIndex(Configuration) "Select provider"}
      bind .provwin.n <Destroy> {+ set provwin_popup 0}
      focus .provwin.cmd.ok

      # fill the listbox with the provider's network names
      foreach cni $provwin_ailist {
         .provwin.n.b.list insert end $provwin_names($cni)
      }

      # select the entry of the currently opened provider (if any)
      set index [lsearch -exact $provwin_ailist [C_GetCurrentDatabaseCni]]
      if {$index >= 0} {
         .provwin.n.b.list selection set $index
         ProvWin_Select
      }

      wm resizable .provwin 1 1
      update
      wm minsize .provwin [winfo reqwidth .provwin] [winfo reqheight .provwin]
   } else {
      raise .provwin
   }
}

# callback for provider selection in listbox: display infos on the right
proc ProvWin_Select {} {
   global provwin_ailist provwin_servicename

   # remove the old service name and netwop list
   set provwin_servicename {}
   .provwin.n.info.net.list configure -state normal
   .provwin.n.info.net.list delete 1.0 end
   .provwin.n.info.oimsg configure -state normal
   .provwin.n.info.oimsg delete 1.0 end

   set index [.provwin.n.b.list curselection]
   if {[string length $index] > 0} {
      set names [C_GetProvServiceInfos [lindex $provwin_ailist $index]]
      # display service name in entry widget
      set provwin_servicename [lindex $names 0]
      # display OI strings in text widget
      .provwin.n.info.oimsg insert end "[lindex $names 1]\n[lindex $names 2]"

      # display all netwops from the AI, separated by commas
      .provwin.n.info.net.list insert end [lindex $names 3]
      if {[llength $names] > 4} {
         foreach netwop [lrange $names 4 end] {
            .provwin.n.info.net.list insert end ", $netwop"
         }
      }
   }
}

# callback for OK button: activate the selected provider
proc ProvWin_Exit {} {
   global provwin_ailist

   set index [.provwin.n.b.list curselection]
   if {[string length $index] > 0} {
      C_ChangeProvider [lindex $provwin_ailist $index]
   }
   destroy .provwin
}


##  --------------------------------------------------------------------------
##  Provider merge popup
##
proc PopupProviderMerge {} {
   global provmerge_popup
   global provmerge_ailist provmerge_selist provmerge_names provmerge_cf
   global prov_merge_cnis prov_merge_cf
   global ProvmergeOptLabels
   global cfnetwops

   if {$provmerge_popup == 0} {
      # get CNIs and names of all known providers
      set provmerge_ailist {}
      foreach {cni name} [C_GetProvCnisAndNames] {
         lappend provmerge_ailist $cni
         set provmerge_names($cni) $name
      }
      set provmerge_ailist [SortProvList $provmerge_ailist]
      if {[info exists prov_merge_cnis]} {
         set provmerge_selist {}
         foreach cni $prov_merge_cnis {
            if {[lsearch -exact $provmerge_ailist $cni] != -1} {
               lappend provmerge_selist $cni
            }
         }
      } else {
         set provmerge_selist $provmerge_ailist
      }
      if {[info exists prov_merge_cf]} {
         array set provmerge_cf $prov_merge_cf
      }

      if {[llength $provmerge_ailist] == 0} {
         # no providers found -> abort
         tk_messageBox -type ok -icon info -message "There are no providers available yet.\nPlease start a provider scan from the Configure menu."
         return
      }

      # create the popup window
      CreateTransientPopup .provmerge "Merging provider databases"
      set provmerge_popup 1

      message .provmerge.msg -aspect 800 -text "Select the providers who's databases you want to merge and their priority:"
      pack .provmerge.msg -side top -expand 1 -fill x -pady 5

      # create the two listboxes for database selection
      frame .provmerge.lb
      SelBoxCreate .provmerge.lb provmerge_ailist provmerge_selist provmerge_names
      pack .provmerge.lb -side top

      # create menu for option sub-windows
      array set ProvmergeOptLabels {
         cftitle "Title"
         cfdescr "Description"
         cfthemes "Themes"
         cfseries "Series codes"
         cfsortcrit "Sorting Criteria"
         cfeditorial "Editorial rating"
         cfparental "Parental rating"
         cfsound "Sound format"
         cfformat "Picture format"
         cfrepeat "Repeat flag"
         cfsubt "Subtitle flag"
         cfmisc "misc. features"
         cfvps "VPS/PDC label"
      }
      menubutton .provmerge.mb -menu .provmerge.mb.men -relief raised -borderwidth 1 \
                               -text "Configure" -underline 0
      pack .provmerge.mb -side top
      menu .provmerge.mb.men -tearoff 0
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cftitle} -label $ProvmergeOptLabels(cftitle)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfdescr} -label $ProvmergeOptLabels(cfdescr)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfthemes} -label $ProvmergeOptLabels(cfthemes)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfseries} -label $ProvmergeOptLabels(cfseries)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfsortcrit} -label $ProvmergeOptLabels(cfsortcrit)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfeditorial} -label $ProvmergeOptLabels(cfeditorial)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfparental} -label $ProvmergeOptLabels(cfparental)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfsound} -label $ProvmergeOptLabels(cfsound)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfformat} -label $ProvmergeOptLabels(cfformat)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfrepeat} -label $ProvmergeOptLabels(cfrepeat)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfsubt} -label $ProvmergeOptLabels(cfsubt)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfmisc} -label $ProvmergeOptLabels(cfmisc)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfvps} -label $ProvmergeOptLabels(cfvps)
      .provmerge.mb.men add separator
      .provmerge.mb.men add command -command {ProvMerge_Reset} -label "Reset"

      # create cmd buttons at bottom
      frame .provmerge.cmd
      button .provmerge.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Merged databases)}
      button .provmerge.cmd.abort -text "Abort" -width 5 -command {ProvMerge_Quit Abort}
      button .provmerge.cmd.ok -text "Ok" -width 5 -command {ProvMerge_Quit Ok} -default active
      pack .provmerge.cmd.help .provmerge.cmd.abort .provmerge.cmd.ok -side left -padx 10
      pack .provmerge.cmd -side top -pady 10

      wm protocol .provmerge WM_DELETE_WINDOW {ProvMerge_Quit Abort}
      bind .provmerge.cmd <Destroy> {+ set provmerge_popup 0; ProvMerge_Quit Abort}
      bind .provmerge.cmd.ok <Return> {tkButtonInvoke .provmerge.cmd.ok}
      bind .provmerge.cmd.ok <Escape> {tkButtonInvoke .provmerge.cmd.abort}
      bind .provmerge <Key-F1> {PopupHelp $helpIndex(Merged databases)}
      bind .provmerge <Alt-KeyPress> [bind Menubutton <Alt-KeyPress>]
      focus .provmerge.cmd.ok
   } else {
      raise .provmerge
   }
}

# create menu for option selection
set provmergeopt_popup {}

proc PopupProviderMergeOpt {cfoption} {
   global provmerge_selist provmerge_cf provmerge_names
   global provmerge_popup provmergeopt_popup
   global ProvmergeOptLabels

   if {$provmerge_popup == 1} {
      if {[string length $provmergeopt_popup] == 0} {
         CreateTransientPopup .provmergeopt "Database Selection for $ProvmergeOptLabels($cfoption)" .provmerge
         set provmergeopt_popup $cfoption

         # initialize the result array: default is all databases
         if {![info exists provmerge_cf($cfoption)]} {
            set provmerge_cf($cfoption) $provmerge_selist
         }

         message .provmergeopt.msg -aspect 800 -text "Select the databases from which the [string tolower $ProvmergeOptLabels($cfoption)] shall be extracted:"
         pack .provmergeopt.msg -side top -expand 1 -fill x -pady 5

         # create the two listboxes for database selection
         frame .provmergeopt.lb
         SelBoxCreate .provmergeopt.lb provmerge_selist provmerge_cf($cfoption) provmerge_names
         pack .provmergeopt.lb -side top

         # create cmd buttons at bottom
         frame .provmergeopt.cb
         button .provmergeopt.cb.ok -text "Ok" -width 7 -command {destroy .provmergeopt} -default active
         pack .provmergeopt.cb.ok -side left -padx 10
         pack .provmergeopt.cb -side top -pady 10

         bind .provmergeopt <Key-F1> {PopupHelp $helpIndex(Merged databases)}
         bind .provmergeopt.cb <Destroy> {+ set provmergeopt_popup {}}
         focus .provmergeopt.cb.ok
         bind  .provmergeopt.cb.ok <Return> {tkButtonInvoke .provmergeopt.cb.ok}
         bind  .provmergeopt.cb.ok <Escape> {tkButtonInvoke .provmergeopt.cb.ok}

      } else {
         # the popup is already opened -> just exchange the contents
         wm title .provmergeopt "Database Selection for $ProvmergeOptLabels($cfoption)"

         if {![info exists provmerge_cf($cfoption)]} {
            set provmerge_cf($cfoption) $provmerge_selist
         }

         .provmergeopt.msg configure -text "Select the databases from which the [string tolower $ProvmergeOptLabels($cfoption)] shall be extracted:"

         # create the two listboxes for database selection
         foreach widget [info commands .provmergeopt.lb.*] {
            destroy $widget
         }
         SelBoxCreate .provmergeopt.lb provmerge_selist provmerge_cf($cfoption) provmerge_names

         # make the sure the popup is not obscured by other windows
         raise .provmergeopt
         focus .provmergeopt.cb.ok
      }
   }
}

# Reset the attribute configuration
proc ProvMerge_Reset {} {
   global provmergeopt_popup
   global provmerge_cf

   if {[info exists provmerge_cf]} {
      array unset provmerge_cf
   }

   if {[string length $provmergeopt_popup] != 0} {
      PopupProviderMergeOpt $provmergeopt_popup
   }
}

# close the provider merge popups and free the state variables
proc ProvMerge_Quit {cause} {
   global provmerge_popup provmergeopt_popup
   global provmerge_selist provmerge_cf
   global prov_merge_cnis prov_merge_cf
   global provmerge_names ProvmergeOptLabels

   # check the configuration parameters for consistancy
   if {[string compare $cause "Ok"] == 0} {

      # check if the provider list is empty
      if {[llength $provmerge_selist] == 0} {
         tk_messageBox -type ok -icon error -parent .provmerge -message "You have to add at least one provider from the left to the listbox on the right."
         return
      }

      # compare the new configuration with the one stored in global variables
      set tmp_cf {}
      if {[info exists provmerge_cf]} {
         foreach {name vlist} [array get provmerge_cf] {
            if {[string compare $vlist $provmerge_selist] != 0} {
               foreach cni $vlist {
                  if {[lsearch -exact $provmerge_selist $cni] == -1} {
                     if {[info exists provmerge_names($cni)]} {
                        set provname " ($provmerge_names($cni))"
                     } else {
                        set provname {}
                     }
                     tk_messageBox -type ok -icon error -parent .provmerge -message "The provider list for [string tolower $ProvmergeOptLabels($name)] contains a database$provname that's not in the main selection. Either remove the provider or use 'Reset' to reset the configuration."
                     return
                  }
               }
               lappend tmp_cf $name $vlist
            }
         }
      }
   }

   # close database selection popup (main)
   if {$provmerge_popup} {
      set provmerge_popup 0
      # undo binding to avoid recursive call to this proc
      bind .provmerge.cmd <Destroy> {}
      destroy .provmerge
   }
   # close attribute selection popup (slave)
   if {[string length $provmergeopt_popup] > 0} {
      destroy .provmergeopt
   }
   # free space from arrays
   if {[info exists ProvmergeOptLabels]} {
      unset ProvmergeOptLabels
   }

   # if closed with OK button, start the merge
   if {[string compare $cause "Ok"] == 0} {
      # save the configuration into global variables
      set prov_merge_cnis $provmerge_selist 
      set prov_merge_cf $tmp_cf

      # save the configuration into the rc/ini file
      UpdateRcFile

      # perform the merge and load the new database into the browser
      C_ProvMerge_Start
   }
}

##  --------------------------------------------------------------------------
##  Create EPG scan popup-window
##
proc PopupEpgScan {} {
   global is_unix env font_fixed font_normal text_bg
   global prov_freqs
   global tvapp_name
   global hwcf_cardidx tvcardcf
   global epgscan_popup
   global epgscan_opt_slow epgscan_opt_refresh epgscan_opt_ftable

   if {$epgscan_popup == 0} {
      if [C_IsNetAcqActive clear_errors] {
         # acquisition is not running local -> abort
         tk_messageBox -type ok -icon error -message "EPG scan cannot be started: you must disconnect from the acquisition daemon via the Control menu first."
         return
      } elseif {!$is_unix} {
         if {![info exists tvcardcf($hwcf_cardidx)]} {
            # TV card has not been configured yet -> abort
            tk_messageBox -type ok -icon info -message "Before you start the scan, please do configure your card type in the 'TV card input' sub-menu of the Configure menu."
            return
         }
      }

      CreateTransientPopup .epgscan "Scan for Nextview EPG providers"
      set epgscan_popup 1

      frame  .epgscan.cmd
      # control commands
      button .epgscan.cmd.start -text "Start scan" -width 12 -command EpgScan_Start
      button .epgscan.cmd.stop -text "Abort scan" -width 12 -command C_StopEpgScan -state disabled
      button .epgscan.cmd.help -text "Help" -width 12 -command {PopupHelp $helpIndex(Configuration) "Provider scan"}
      button .epgscan.cmd.dismiss -text "Dismiss" -width 12 -command {destroy .epgscan}
      pack .epgscan.cmd.start .epgscan.cmd.stop .epgscan.cmd.help .epgscan.cmd.dismiss -side top -padx 10 -pady 10
      pack .epgscan.cmd -side left

      frame .epgscan.all -relief raised -bd 2
      # progress bar
      frame .epgscan.all.baro -width 140 -height 15 -relief sunken -borderwidth 2
      pack propagate .epgscan.all.baro 0
      frame .epgscan.all.baro.bari -width 0 -height 11 -background blue
      pack propagate .epgscan.all.baro.bari 0
      pack .epgscan.all.baro.bari -padx 2 -pady 2 -side left -anchor w
      pack .epgscan.all.baro -pady 5

      # message window to inform about the scanning state
      frame .epgscan.all.fmsg
      text .epgscan.all.fmsg.msg -width 60 -height 20 -yscrollcommand {.epgscan.all.fmsg.sb set} -wrap none -font $font_fixed
      pack .epgscan.all.fmsg.msg -side left -expand 1 -fill both
      .epgscan.all.fmsg.msg tag configure bold -font [DeriveFont $font_normal 0 bold]
      scrollbar .epgscan.all.fmsg.sb -orient vertical -command {.epgscan.all.fmsg.msg yview}
      pack .epgscan.all.fmsg.sb -side left -fill y
      pack .epgscan.all.fmsg -side top -padx 10 -fill both -expand 1

      # frequency table radio buttons
      frame .epgscan.all.ftable -borderwidth 2 -relief sunken
      label .epgscan.all.ftable.label -text "Channel table:"
      radiobutton .epgscan.all.ftable.tab1 -text "Western Europe" -variable epgscan_opt_ftable -value 1
      radiobutton .epgscan.all.ftable.tab2 -text "France" -variable epgscan_opt_ftable -value 2
      radiobutton .epgscan.all.ftable.tab0 -variable epgscan_opt_ftable -value 0
      pack .epgscan.all.ftable.label -side left -padx 5
      pack .epgscan.all.ftable.tab1 .epgscan.all.ftable.tab2 .epgscan.all.ftable.tab0 -side left -expand 1 -pady 3
      pack .epgscan.all.ftable -side top -padx 10 -fill x

      trace variable tvapp_name w EpgScan_CheckTvAppStatus
      EpgScan_CheckTvAppStatus foo bar bar2

      # mode buttons
      frame   .epgscan.all.opt -borderwidth 2 -relief sunken
      checkbutton .epgscan.all.opt.refresh -text "Refresh only" -variable epgscan_opt_refresh
      checkbutton .epgscan.all.opt.slow -text "Slow" -variable epgscan_opt_slow -command {C_SetEpgScanSpeed $epgscan_opt_slow}
      pack    .epgscan.all.opt.refresh .epgscan.all.opt.slow -side left -padx 5
      if {!$is_unix} {
         button .epgscan.all.opt.cfgtvcard -text "Card setup" -command PopupHardwareConfig
         button .epgscan.all.opt.cfgtvpp -text "Select TV app" -command XawtvConfigPopup
         pack   .epgscan.all.opt.cfgtvpp -side right -pady 3
         pack   .epgscan.all.opt.cfgtvcard -side right -pady 3
      }
      pack   .epgscan.all.opt -side top -padx 10 -fill x


      # check if provider frequencies are available
      UpdateProvFrequency [C_LoadProvFreqsFromDbs]
      if {[llength $prov_freqs] > 0} {
         .epgscan.all.fmsg.msg insert end "To remove obsolete providers or fix database problems\nenable the "
         checkbutton .epgscan.all.fmsg.msg.refresh -text "Refresh only" -variable epgscan_opt_refresh \
                                                   -font $font_fixed -background $text_bg -cursor top_left_arrow
         .epgscan.all.fmsg.msg window create end -window .epgscan.all.fmsg.msg.refresh 
         .epgscan.all.fmsg.msg insert end "option.\n\n"
      } else {
         .epgscan.all.opt.refresh configure -state disabled
      }

      pack .epgscan.all -side top -fill both -expand 1
      bind .epgscan.all <Destroy> EpgScan_Quit
      bind .epgscan <Key-F1> {PopupHelp $helpIndex(Configuration) "Provider scan"}

      .epgscan.all.fmsg.msg insert end "Press the <Start scan> button"

      wm resizable .epgscan 1 1
      update
      wm minsize .epgscan [winfo reqwidth .epgscan] [winfo reqheight .epgscan]
   } else {
      raise .epgscan
   }
}

# callback for "Start" button
proc EpgScan_Start {} {
   global epgscan_opt_slow epgscan_opt_refresh epgscan_opt_ftable
   global hwcf_input
   global is_unix

   if {[C_IsNetAcqActive clear_errors]} {
      # acquisition is not running local -> abort
      tk_messageBox -type ok -icon info -message "EPG scan cannot be started while in client/server mode."
      return
   }

   UpdateRcFile

   C_StartEpgScan $hwcf_input $epgscan_opt_slow $epgscan_opt_refresh $epgscan_opt_ftable
}

# callback for dialog destruction (including "Dismiss" button)
proc EpgScan_Quit {} {
   global tvapp_name
   global epgscan_popup

   set epgscan_popup 0
   trace vdelete tvapp_name w EpgScan_CheckTvAppStatus

   C_StopEpgScan
}

# called after start or stop of EPG scan to update button states
proc EpgScanButtonControl {is_start} {
   global is_unix env prov_freqs

   if {[string compare $is_start "start"] == 0} {
      # grab input focus to prevent any interference with the scan
      grab .epgscan
      # disable options and command buttons, enable the "Abort" button
      .epgscan.cmd.start configure -state disabled
      .epgscan.cmd.stop configure -state normal
      .epgscan.cmd.help configure -state disabled
      .epgscan.cmd.dismiss configure -state disabled
      .epgscan.all.opt.refresh configure -state disabled
      .epgscan.all.ftable.tab0 configure -state disabled
      .epgscan.all.ftable.tab1 configure -state disabled
      .epgscan.all.ftable.tab2 configure -state disabled
      if {!$is_unix} {
         .epgscan.all.opt.cfgtvcard configure -state disabled
         .epgscan.all.opt.cfgtvpp configure -state disabled
      }
      # destroy all "Remove provider" buttons
      foreach w [info commands .epgscan.all.fmsg.msg.del_prov_*] {
         destroy $w
      }
   } else {
      # check if the popup window still exists
      if {[string length [info commands .epgscan.cmd]] > 0} {
         # release input focus
         grab release .epgscan
         # disable "Abort" button, re-enable others
         .epgscan.cmd.start configure -state normal
         .epgscan.cmd.stop configure -state disabled
         .epgscan.cmd.help configure -state normal
         .epgscan.cmd.dismiss configure -state normal
         if {!$is_unix} {
            .epgscan.all.opt.cfgtvcard configure -state normal
            .epgscan.all.opt.cfgtvpp configure -state normal
         }
         # enable option checkboxes only if they were enabled before the scan
         if {[llength $prov_freqs] > 0} {
            .epgscan.all.opt.refresh configure -state normal
         }
         if {[C_Tvapp_Enabled]} {
            .epgscan.all.ftable.tab0 configure -state normal
         }
         .epgscan.all.ftable.tab1 configure -state normal
         .epgscan.all.ftable.tab2 configure -state normal
         # enable "Remove provider" buttons
         foreach w [info commands .epgscan.all.fmsg.msg.del_prov_*] {
            $w configure -state normal
         }
      }
   }
}

# callback for "Remove provider" button in the scan message window
proc EpgScanDeleteProvider {cni} {
   global prov_selection prov_freqs cfnetwops

   if {[string compare [.epgscan.cmd.start cget -state] normal] != 0} {
      return
   }
   set answer [tk_messageBox -type okcancel -icon info -parent .epgscan \
                  -message "You're about to remove the provider's database and all it's configuration information."]
   if {[string compare $answer ok] == 0} {
      # remove the database file
      set err_str [C_RemoveProviderDatabase $cni]

      if {[string length $err_str] > 0} {
         # failed to remove the (existing) file
         tk_messageBox -type ok -icon error -parent .epgscan \
            -message "Failed to remove the database file: $err_str"
      } else {
         #warn prov_merge_cnis
         #warn acq_mode_cnis

         set idx [lsearch -exact $prov_selection $cni]
         if {$idx != -1} {
            set prov_selection [lreplace $prov_selection $idx $idx]
         }
         set idx [lsearch -exact $prov_freqs $cni]
         if {$idx != -1} {
            set prov_freqs [lreplace $prov_freqs $idx [expr $idx + 1]]
         }
         array unset cfnetwops $cni

         UpdateRcFile

         # disable the button so that the user can't invoke it again
         catch [.epgscan.all.fmsg.msg.del_prov_$cni configure -state disabled]
      }
   }
}

# trace callback for variable tvapp_name to update freq. table options
proc EpgScan_CheckTvAppStatus {n1 n1 v} {
   global tvapp_name
   global epgscan_opt_ftable
   global epgscan_popup

   if $epgscan_popup {
      if {![C_Tvapp_Enabled]} {
         .epgscan.all.ftable.tab0 configure -state disabled
         if {$epgscan_opt_ftable == 0} {
            set epgscan_opt_ftable 1
         }
      } else {
         if {[string compare [.epgscan.all.ftable.tab0 cget -state] "disabled"] == 0} {
            .epgscan.all.ftable.tab0 configure -state normal
            set epgscan_opt_ftable 0
         }
      }
      .epgscan.all.ftable.tab0 configure -text "Load from $tvapp_name"
   }
}

## ---------------------------------------------------------------------------
## Time zone configuration popup
##
proc PopupTimeZone {} {
   global entry_disabledforeground
   global tzcfg tzcfg_default
   global tz_auto_sel tz_lto_sel tz_daylight_sel
   global timezone_popup

   if {$timezone_popup == 0} {
      CreateTransientPopup .timezone "Configure time zone"
      set timezone_popup 1

      if {![info exists tzcfg]} {
         set tzcfg $tzcfg_default
      }
      set tz_auto_sel [lindex $tzcfg 0]
      set tz_lto_sel [lindex $tzcfg 1]
      set tz_daylight_sel [lindex $tzcfg 2]

      frame .timezone.f1
      message .timezone.msg -aspect 400 -text \
"All times and dates in Nextview are transmitted in UTC \
(Universal Coordinated Time, formerly known as Greenwhich Mean Time) \
and have to be converted to your local time by adding the local time offset. \
If the offset given below is not correct, switch to manual mode and enter \
the correct value."
      pack .timezone.msg -side top -pady 5 -padx 5
      radiobutton .timezone.f1.auto -text "automatic" -variable tz_auto_sel -value 1 -command {ToggleTimeZoneAuto 1}
      radiobutton .timezone.f1.manu -text "manual" -variable tz_auto_sel -value 0 -command {ToggleTimeZoneAuto 0}
      pack .timezone.f1.auto .timezone.f1.manu -side left -padx 10
      pack .timezone.f1 -side top -pady 10

      frame .timezone.f2 -borderwidth 1 -relief raised
      label .timezone.f2.name -text ""
      frame .timezone.f2.ft
      label .timezone.f2.ft.l1 -text "Local time offset: "
      entry .timezone.f2.ft.entry -width 4 -textvariable tz_lto_sel $entry_disabledforeground black
      bind  .timezone.f2.ft.entry <Enter> {SelectTextOnFocus %W}
      bind  .timezone.f2.ft.entry <Return> {tkButtonInvoke .timezone.cmd.ok}
      label .timezone.f2.ft.l2 -text " minutes"
      pack .timezone.f2.ft.l1 .timezone.f2.ft.entry .timezone.f2.ft.l2 -side left
      checkbutton .timezone.f2.daylight -text "Daylight saving time (+60 minutes)" -variable tz_daylight_sel
      pack .timezone.f2.name .timezone.f2.ft .timezone.f2.daylight -side top -pady 5 -padx 5
      pack .timezone.f2 -side top -pady 10 -fill x

      frame .timezone.cmd
      button .timezone.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration)}
      button .timezone.cmd.abort -text "Abort" -width 5 -command {destroy .timezone}
      button .timezone.cmd.ok -text "Ok" -width 5 -command ApplyTimeZone -default active
      pack .timezone.cmd.help .timezone.cmd.abort .timezone.cmd.ok -side left -padx 10

      bind .timezone <Key-F1> {PopupHelp $helpIndex(Configuration)}
      bind .timezone.cmd <Destroy> {+ set timezone_popup 0}
      focus .timezone.f2.ft.entry

      pack .timezone.cmd -side top -pady 10

      ToggleTimeZoneAuto $tz_auto_sel
   } else {
      raise .timezone
   }
}

# called after auto/manual radio was toggled
proc ToggleTimeZoneAuto {newval} {
   global tz_auto_sel tz_lto_sel tz_daylight_sel

   if {$newval} {
      # fetch default values
      set tzarr [C_GetTimeZone]
      set tz_lto_sel [lindex $tzarr 0]
      set tz_daylight_sel [lindex $tzarr 1]
      .timezone.f2.name configure -text "Time zone name: [lindex $tzarr 2]"

      .timezone.f2.ft.entry configure -state disabled
      .timezone.f2.daylight configure -state disabled
      .timezone.f2.ft.l1 configure -state disabled
      .timezone.f2.ft.l2 configure -state disabled
   } else {
      .timezone.f2.ft.entry configure -state normal
      .timezone.f2.daylight configure -state normal
      .timezone.f2.ft.l1 configure -state normal
      .timezone.f2.ft.l2 configure -state normal
   }
}

# called after the OK button was pressed
proc ApplyTimeZone {} {
   global tz_auto_sel tz_lto_sel tz_daylight_sel
   global tzcfg

   if {$tz_auto_sel == 0} {
      # check if the manually configured LTO is a valid integer
      if {[scan $tz_lto_sel "%d" lto] != 1} {
         tk_messageBox -type ok -default ok -icon error -parent .timezone -message "Invalid input format in LTO '$tz_lto_sel'\nMust be positive or negative decimal integer."
         return
      }
      # check if the value is within bounds of +/- 12 hours
      if {($lto < -12*60) || ($lto > 12*60)} {
         tk_messageBox -type ok -default ok -icon error -parent .timezone -message "Invalid LTO: $lto\nThe maximum absolute value is 12 hours."
         return
      }
      # warn if it's not a half-hour value
      # then the user probably has mistakenly entered hours instead of minutes
      if {($lto % 30) != 0} {
         set answer [tk_messageBox -type okcancel -icon warning -parent .timezone -message "Warning: the given LTO value '$lto minutes' is not a complete hour or half hour.\nMaybe you entered an hour value instead of minutes?\nDo you still want to set this LTO?"]
         if {[string compare $answer "ok"] != 0} {
            return
         }
      }
   }

   set tzcfg [list $tz_auto_sel $tz_lto_sel $tz_daylight_sel]

   destroy .timezone
}

