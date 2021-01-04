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
#  $Id: dlg_prov.tcl,v 1.24 2008/10/12 19:18:48 tom Exp tom $
#
set provwin_popup 0
set provmerge_popup 0

set epgscan_popup 0
set epgscan_opt_slow 0
set epgscan_opt_ftable 0

# called by tv application config dialog
proc EpgScan_UpdateTvApp {} {
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
      set tvapp_name [TvAppGetName]
      .epgscan.all.ftable.tab0 configure -text "Load from $tvapp_name"
   }
}

#=LOAD=ProvWin_Create
#=LOAD=PopupProviderMerge
#=LOAD=PopupEpgScan
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Handling of the Provider Selection Pop-up
##
proc ProvWin_Create {} {
   global provwin_popup
   global font_normal entry_disabledforeground
   global provwin_servicename
   global provwin_dir

   if {$provwin_popup == 0} {
      CreateTransientPopup .provwin "Load XMLTV file"
      set provwin_popup 1

      # directory selection is kept across dialog being closed/reopened
      if {![info exists provwin_dir]} {
         set provwin_dir [file normalize "."]
      }

      # entry field for directory
      frame  .provwin.dir
      label  .provwin.dir.prompt -text "Select directory:"
      pack   .provwin.dir.prompt -side left
      frame  .provwin.dir.cfrm -relief sunken -borderwidth 1
      entry  .provwin.dir.cfrm.ent -relief flat -borderwidth 0 -width 30 \
                                   -textvariable provwin_dir -font $::font_fixed
      button .provwin.dir.cfrm.ddb -bitmap bitmap_dropdown -takefocus 0 \
                                   -borderwidth 1 -relief raised -highlightthickness 1 \
                                   -command {ProvWin_ShowDirDropDown .provwin.dir.cfrm.ent ProvWin_SelectTeletextDir}
      bind   .provwin.dir.cfrm.ent <Key-Up> {ProvWin_ShowDirDropDown .provwin.dir.cfrm.ent ProvWin_SelectTeletextDir}
      bind   .provwin.dir.cfrm.ent <Key-Down> {ProvWin_ShowDirDropDown .provwin.dir.cfrm.ent ProvWin_SelectTeletextDir}
      bind   .provwin.dir.cfrm.ent <Return> ProvWin_UpdateList
      pack   .provwin.dir.cfrm.ent -side left -fill x -expand 1
      pack   .provwin.dir.cfrm.ddb -side left -fill y
      pack   .provwin.dir.cfrm -side left -fill x -expand 1

      button .provwin.dir.dlgbut -image $::fileImage -command {
         set dir [tk_chooseDirectory -initialdir $provwin_dir -mustexist 1 -parent .provwin \
                                     -title "Select directory for loading XMLTV files"]
         if {$dir ne ""} {
            set provwin_dir $dir
            ProvWin_UpdateList
         }
      }
      pack  .provwin.dir.dlgbut -side left -padx 5
      pack  .provwin.dir -side top -pady 10 -fill x -expand 1

      # list of providers at the left side of the window
      frame .provwin.n
      frame .provwin.n.b
      scrollbar .provwin.n.b.sb -orient vertical -command {.provwin.n.b.list yview} -takefocus 0
      pack .provwin.n.b.sb -side left -fill y
      listbox .provwin.n.b.list -width 20 -height 5 -selectmode single -exportselection 0 \
                                -yscrollcommand {.provwin.n.b.sb set}
      relief_listbox .provwin.n.b.list
      pack .provwin.n.b.list -side left -fill both -expand 1
      pack .provwin.n.b -side left -fill both -expand 1
      bind .provwin.n.b.list <<ListboxSelect>> ProvWin_Select
      bind .provwin.n.b.list <Double-Button-1> ProvWin_Exit

      # provider info at the right side of the window
      if {[info exists provwin_servicename]} {unset provwin_servicename}
      frame .provwin.n.info
      frame .provwin.n.info.service
      label .provwin.n.info.service.header -text "Name of service"
      pack  .provwin.n.info.service.header -side top -anchor nw
      entry .provwin.n.info.service.name -state disabled $entry_disabledforeground black \
                                         -textvariable provwin_servicename -width 40
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
      button .provwin.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Control) "Load XMLTV file"}
      button .provwin.cmd.abort -text "Abort" -width 5 -command {destroy .provwin}
      button .provwin.cmd.ok -text "Ok" -width 5 -command ProvWin_Exit -default active
      bind .provwin.cmd.ok <Return> {tkButtonInvoke .provwin.cmd.ok}
      bind .provwin <Escape> {tkButtonInvoke .provwin.cmd.abort}
      pack .provwin.cmd.help .provwin.cmd.abort .provwin.cmd.ok -side left -padx 10
      pack .provwin.cmd -side top -pady 5

      bind .provwin <Key-F1> {PopupHelp $helpIndex(Configuration) "Load XMLTV file"}
      bind .provwin.n <Destroy> {+ set provwin_popup 0}
      focus .provwin.cmd.ok

      ProvWin_UpdateList

      wm resizable .provwin 1 1
      update
      wm minsize .provwin [winfo reqwidth .provwin] [winfo reqheight .provwin]
   } else {
      raise .provwin
   }
}

proc ProvWin_NameSortCmp {a b} {
   global provwin_names
   return [string compare -nocase $provwin_names($a) $provwin_names($b)]
}

# callback for directory changes
proc ProvWin_UpdateList {} {
   global provwin_dir provwin_ailist provwin_names

   # build list of available providers
   set provwin_ailist {}
   foreach {cni name} [C_GetProvCnisAndNames $provwin_dir] {
      # build list of CNIs
      lappend provwin_ailist $cni
      # build array of provider network names
      set provwin_names($cni) $name
   }
   # sort CNI list by provider name
   set provwin_ailist [lsort -command ProvWin_NameSortCmp $provwin_ailist]

   if {[llength $provwin_ailist] == 0} {
      # no providers found -> abort
      #tk_messageBox -type ok -icon info -message "There are no providers available yet.\nPlease start a provider scan from the Configure menu."
      #return
      #TODO
   }

   .provwin.n.b.list delete 0 end

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
}

# callback for quick-link for selecting grabber directory
proc ProvWin_SelectTeletextDir {} {
   global provwin_dir
   set provwin_dir [C_GetTtxDbDir]
   ProvWin_UpdateList
}

proc ProvWin_ShowDirDropDown {wid cmd} {
   global provwin_dir

   if {![winfo exists .men_popup]} {
      menu .men_popup -tearoff 0
   } else {
      .men_popup delete 0 end
   }

   C_GetTtxConfig ttxgrab_tmpcf
   if {$ttxgrab_tmpcf(enable)} {
      .men_popup add command -label "Teletext grabber XMLTV directory" -command $cmd
   }

   set rootx [winfo rootx $wid]
   set rooty [expr {[winfo rooty $wid] + [winfo height $wid]}]
   tk_popup .men_popup $rootx $rooty {}
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
   global provmerge_ailist provmerge_selist provwin_names provmerge_cf
   global provwin_dir
   global ProvmergeOptLabels

   if {$provmerge_popup == 0} {
      # directory selection is kept across dialog being closed/reopened
      if {![info exists provwin_dir]} {
         set provwin_dir [file normalize "."]
      }
      set provmerge_ailist {}
      set provmerge_selist {}
      set dropped {}
      foreach cni [C_GetMergeProviderList] {
         set name [C_GetProvServiceName $cni]
         if {$name ne ""} {
            lappend provmerge_selist $cni
            set provwin_names($cni) [C_GetProvServiceName $cni]
         } else {
            # file no longer exists
            lappend dropped $cni
         }
      }
      catch {array set provmerge_cf [C_GetMergeOptions]}

      # create the popup window
      CreateTransientPopup .provmerge "Merge XMLTV files"
      set provmerge_popup 1

      label .provmerge.msg -text "This dialog allows loading and merging multiple XMLTV files. Note the order of\nfiles in the list on the right defines their priority in case of conflicts:"
      pack .provmerge.msg -side top -expand 1 -fill x -pady 5

      # entry field for directory
      frame  .provmerge.dir
      label  .provmerge.dir.prompt -text "Select directory:"
      pack   .provmerge.dir.prompt -side left
      frame  .provmerge.dir.cfrm -relief sunken -borderwidth 1
      entry  .provmerge.dir.cfrm.ent -relief flat -borderwidth 0 -width 30 \
                                   -textvariable provwin_dir -font $::font_fixed
      button .provmerge.dir.cfrm.ddb -bitmap bitmap_dropdown -takefocus 0 \
                                   -borderwidth 1 -relief raised -highlightthickness 1 \
                                   -command {ProvWin_ShowDirDropDown .provmerge.dir.cfrm.ent ProvMerge_SelectTeletextDir}
      bind   .provmerge.dir.cfrm.ent <Key-Up> {ProvWin_ShowDirDropDown .provmerge.dir.cfrm.ent ProvMerge_SelectTeletextDir}
      bind   .provmerge.dir.cfrm.ent <Key-Down> {ProvWin_ShowDirDropDown .provmerge.dir.cfrm.ent ProvMerge_SelectTeletextDir}
      bind   .provmerge.dir.cfrm.ent <Return> ProvMerge_UpdateList
      pack   .provmerge.dir.cfrm.ent -side left -fill x -expand 1
      pack   .provmerge.dir.cfrm.ddb -side left -fill y
      pack   .provmerge.dir.cfrm -side left -fill x -expand 1

      button .provmerge.dir.dlgbut -image $::fileImage -command {
         set dir [tk_chooseDirectory -initialdir $provwin_dir -mustexist 1 -parent .provmerge \
                                     -title "Select directory for loading XMLTV files"]
         if {$dir ne ""} {
            set provwin_dir $dir
            ProvMerge_UpdateList
         }
      }
      pack  .provmerge.dir.dlgbut -side left -padx 5
      pack  .provmerge.dir -side top -pady 5 -fill x -expand 1

      # create the two listboxes for database selection
      frame .provmerge.lb
      SelBoxCreate .provmerge.lb provmerge_ailist provmerge_selist provwin_names 30 1
      pack .provmerge.lb -side top

      # populate the AI list
      ProvMerge_UpdateList

      # create menu for option sub-windows
      array set ProvmergeOptLabels {
         cftitle "Title"
         cfdescr "Description text"
         cfthemes "Theme categories"
         cfeditorial "Editorial rating"
         cfparental "Parental rating"
         cfsound "Sound format"
         cfformat "Picture format"
         cfrepeat "Repeat flag"
         cfsubt "Subtitle flag"
         cfmisc "misc. feature flags"
         cfvps "VPS/PDC label"
      }
      menubutton .provmerge.mb -menu .provmerge.mb.men -text "Configure" -underline 0
      config_menubutton .provmerge.mb
      pack .provmerge.mb -side top
      menu .provmerge.mb.men -tearoff 0
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cftitle} -label $ProvmergeOptLabels(cftitle)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfdescr} -label $ProvmergeOptLabels(cfdescr)
      .provmerge.mb.men add command -command {PopupProviderMergeOpt cfthemes} -label $ProvmergeOptLabels(cfthemes)
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

      wm resizable .provmerge 1 1
      update
      wm minsize .provmerge [winfo reqwidth .provmerge] [winfo reqheight .provmerge]

      if {[llength $dropped] > 0} {
         tk_messageBox -type ok -icon warning -parent .provmerge -message "Formerly used XMLTV files were dropped from the selection as the file no longer exists."
      }
   } else {
      raise .provmerge
   }
}

# callback for directory changes
proc ProvMerge_UpdateList {} {
   global provmerge_ailist provwin_names
   global provwin_dir

   set provmerge_ailist {}
   foreach {cni name} [C_GetProvCnisAndNames $provwin_dir] {
      lappend provmerge_ailist $cni
      set provwin_names($cni) $name
   }
   # sort CNI list by provider name
   set provmerge_ailist [lsort -command ProvWin_NameSortCmp $provmerge_ailist]

   SelBoxUpdateList .provmerge.lb provmerge_ailist provmerge_selist provwin_names
}

# callback for quick-link for selecting grabber directory
proc ProvMerge_SelectTeletextDir {} {
   global provwin_dir
   set provwin_dir [C_GetTtxDbDir]
   ProvMerge_UpdateList
}

# create menu for option selection
set provmergeopt_popup {}

proc PopupProviderMergeOpt {cfoption} {
   global provmerge_selist provmerge_cf provwin_names
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
         SelBoxCreate .provmergeopt.lb provmerge_selist provmerge_cf($cfoption) provwin_names 12 0
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
         SelBoxCreate .provmergeopt.lb provmerge_selist provmerge_cf($cfoption) provwin_names 12 0

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
   global provwin_names ProvmergeOptLabels

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
                     if {[info exists provwin_names($cni)]} {
                        set provname " ($provwin_names($cni))"
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

      # perform the merge and load the new database into the browser
      C_ProvMerge_Start $provmerge_selist $tmp_cf
   }
}

##  --------------------------------------------------------------------------
##  Create EPG scan popup-window
##
proc PopupEpgScan {} {
   global is_unix env font_fixed font_normal text_fg text_bg
   global epgscan_popup
   global epgscan_opt_slow epgscan_opt_ftable

   if {$epgscan_popup == 0} {
      if [C_IsNetAcqActive clear_errors] {
         # acquisition is not running local -> abort
         tk_messageBox -type ok -icon error -message "EPG scan cannot be started: you must disconnect from the acquisition daemon via the Control menu first."
         return
      } elseif {![C_CheckTvCardConfig]} {
         # TV card has not been configured yet -> abort
         tk_messageBox -type ok -icon info -message "Before you start the scan, please do configure your card type in the 'TV card input' sub-menu of the Configure menu."
         return
      } elseif [C_IsAcqExternal] {
         tk_messageBox -type ok -icon info -message [concat \
                "The provider search cannot be used with an external video input source. " \
                "You must tune EPG provider TV channels manually in the external device " \
                "which is attached to the video input."]
         return
      }

      CreateTransientPopup .epgscan "Scan for Nextview EPG providers"
      set epgscan_popup 1

      frame  .epgscan.cmd
      # control commands
      button .epgscan.cmd.start -text "Start scan" -width 12 -command EpgScan_Start
      button .epgscan.cmd.stop -text "Abort scan" -width 12 -command C_StopEpgScan -state disabled
      button .epgscan.cmd.help -text "Help" -width 12 -command {PopupHelp $helpIndex(Configuration) "TV channel scan"}
      button .epgscan.cmd.ok -text "Ok" -width 12 -command {destroy .epgscan}
      pack .epgscan.cmd.start .epgscan.cmd.stop .epgscan.cmd.help .epgscan.cmd.ok -side top -padx 10 -pady 10
      pack .epgscan.cmd -side left

      frame .epgscan.all -relief raised -borderwidth 1
      # progress bar
      frame .epgscan.all.baro -width 140 -height 15 -relief sunken -borderwidth 1
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
      frame .epgscan.all.ftable -relief sunken -borderwidth 1
      label .epgscan.all.ftable.label -text "Channel table:"
      radiobutton .epgscan.all.ftable.tab1 -text "Western Europe" -variable epgscan_opt_ftable -value 1
      radiobutton .epgscan.all.ftable.tab2 -text "France" -variable epgscan_opt_ftable -value 2
      radiobutton .epgscan.all.ftable.tab0 -variable epgscan_opt_ftable -value 0
      pack .epgscan.all.ftable.label -side left -padx 5
      pack .epgscan.all.ftable.tab1 .epgscan.all.ftable.tab2 .epgscan.all.ftable.tab0 -side left -expand 1 -pady 3
      pack .epgscan.all.ftable -side top -padx 10 -fill x

      EpgScan_UpdateTvApp

      # mode buttons
      frame   .epgscan.all.opt -relief sunken -borderwidth 1
      checkbutton .epgscan.all.opt.slow -text "Slow" -variable epgscan_opt_slow -command {C_SetEpgScanSpeed $epgscan_opt_slow}
      button .epgscan.all.opt.cfgtvcard -text "Card setup" -command PopupHardwareConfig
      button .epgscan.all.opt.cfgtvpp -text "Select TV app" -command XawtvConfigPopup
      pack   .epgscan.all.opt.cfgtvpp -side right -pady 3
      pack   .epgscan.all.opt.cfgtvcard -side right -pady 3
      pack   .epgscan.all.opt -side top -padx 10 -fill x

      pack .epgscan.all -side top -fill both -expand 1
      bind .epgscan.all <Destroy> EpgScan_Quit
      bind .epgscan <Key-F1> {PopupHelp $helpIndex(Configuration) "TV channel scan"}

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
   global is_unix

   if {[C_IsNetAcqActive clear_errors]} {
      # acquisition is not running local -> abort
      tk_messageBox -type ok -icon info -parent .epgscan -message "EPG scan cannot be started while in client/server mode."
      return
   }

   if {$epgscan_opt_ftable != 0} {
      # check $::hwcf_ret_drvsrc_idx for BTDRV_SOURCE_DVB
      set hwcfg [C_GetHardwareConfig]
      if {[lindex $hwcfg 2] == 1} {
         if {![C_Tvapp_Enabled]} {
            set answer [tk_messageBox -type okcancel -icon error -parent .epgscan -message "Scanning physical frequencies is not supported for digital TV cards. Please use \"Select TV app\" for configuring a channel table before starting the EPG scan."]
            if {[string compare $answer ok] == 0} {
               XawtvConfigPopup
            }
            return
         } else {
            tk_messageBox -type ok -icon error -parent .epgscan -message "Scanning physical frequencies is not supported for digital TV cards. Please check option \"Load from TV app\"."
         }
         return
      }
   }

   # clear the message window, including "provider remove" buttons
   .epgscan.all.fmsg.msg delete 1.0 end
   foreach w [info commands .epgscan.all.fmsg.msg.del_prov_*] {
      destroy $w
   }

   C_StartEpgScan $epgscan_opt_slow $epgscan_opt_ftable
}

# callback for dialog destruction (including "OK" button)
proc EpgScan_Quit {} {
   global epgscan_popup

   set epgscan_popup 0

   C_StopEpgScan
}

# called after start or stop of EPG scan to update button states
proc EpgScanButtonControl {is_start} {
   global is_unix env

   if {[string compare $is_start "start"] == 0} {
      # grab input focus to prevent any interference with the scan
      grab .epgscan
      # disable options and command buttons, enable the "Abort" button
      .epgscan.cmd.start configure -state disabled
      .epgscan.cmd.stop configure -state normal
      .epgscan.cmd.help configure -state disabled
      .epgscan.cmd.ok configure -state disabled
      .epgscan.all.ftable.tab0 configure -state disabled
      .epgscan.all.ftable.tab1 configure -state disabled
      .epgscan.all.ftable.tab2 configure -state disabled
      if {!$is_unix} {
         .epgscan.all.opt.cfgtvcard configure -state disabled
         .epgscan.all.opt.cfgtvpp configure -state disabled
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
         .epgscan.cmd.ok configure -state normal
         if {!$is_unix} {
            .epgscan.all.opt.cfgtvcard configure -state normal
            .epgscan.all.opt.cfgtvpp configure -state normal
         }
         # enable option checkboxes only if they were enabled before the scan
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

# called by EPG scan control to add a line to the message window
proc EpgScanAddMessage {msg fmt} {
   .epgscan.all.fmsg.msg insert end $msg $fmt "\n" {}
   .epgscan.all.fmsg.msg see {end linestart - 2 lines}
}
