#
#  Configuration dialog for network names
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
#    Implements a GUI for configuration of network names.
#
#  Author: Tom Zoerner
#
#  $Id: dlg_netname.tcl,v 1.1 2002/10/25 20:16:27 tom Exp tom $
#
set netname_popup 0

##  --------------------------------------------------------------------------
##  Replace provider-supplied network names with user-configured names
##
proc ApplyUserNetnameCfg {name_arr} {
   upvar $name_arr netnames
   global cfnetnames

   foreach cni [array names netnames] {
      if [info exists cfnetnames($cni)] {
         set netnames($cni) $cfnetnames($cni)
      }
   }
}


#=LOAD=NetworkNamingPopup
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Configure individual names for networks
##
proc NetworkNamingPopup {} {
   global cfnetnames prov_selection cfnetwops
   global netname_ailist netname_names netname_idx netname_xawtv netname_automatch
   global netname_prov_cnis netname_prov_names netname_provnets
   global netname_entry
   global netname_popup font_fixed entry_disabledforeground
   global tvapp_name

   if {$netname_popup == 0} {

      # build list of available providers
      set netname_prov_cnis {}
      foreach {cni name} [C_GetProvCnisAndNames] {
         # build list of CNIs
         lappend netname_prov_cnis $cni
         # build array of provider network names
         set netname_prov_names($cni) $name
      }
      # sort provider list according to user preferences
      set netname_prov_cnis [SortProvList $netname_prov_cnis]

      if {[llength $netname_prov_cnis] == 0} {
         # no providers found -> abort
         tk_messageBox -type ok -icon info -message "There are no providers available yet.\nPlease start a provider scan from the Configure menu."
         return
      }

      set netname_ailist {}
      # retrieve all networks from all providers
      foreach prov $netname_prov_cnis {
         array unset tmparr
         set cnilist [C_GetAiNetwopList $prov tmparr]
         foreach cni $cnilist {
            set netname_provnets($prov,$cni) $tmparr($cni)
         }

         if [info exists cfnetwops($prov)] {
            foreach cni [lindex $cfnetwops($prov) 0] {
               if [info exists tmparr($cni)] {
                  if {[lsearch -exact $netname_ailist $cni] == -1} {
                     lappend netname_ailist $cni
                  }
                  unset tmparr($cni)
               }
            }
         }
         foreach cni $cnilist {
            if [info exists tmparr($cni)] {
               if {[lsearch -exact $netname_ailist $cni] == -1} {
                  lappend netname_ailist $cni
               }
            }
         }
      }
      array unset tmparr

      # re-sort CNI list for the merged database
      if {[info exists cfnetwops(0x00FF)] && ([C_GetCurrentDatabaseCni] == 0x00FF)} {
         set tmp {}
         foreach cni [lindex $cfnetwops(0x00FF) 0] {
            set idx [lsearch -exact $netname_ailist $cni]
            if {$idx >= 0} {
               lappend tmp $cni
               set netname_ailist [lreplace $netname_ailist $idx $idx]
            }
         }
         set netname_ailist [concat $tmp $netname_ailist]
      }

      # build array with all names from .xawtv rc file
      set xawtv_list [C_Tvapp_GetStationNames]
      foreach name $xawtv_list {
         set netname_xawtv($name) $name
         regsub -all -- {[^a-zA-Z0-9]*} $name {} tmp
         set netname_xawtv([string tolower $tmp]) $name
      }

      # copy or build an array with the currently configured network names
      set netname_automatch {}
      set xawtv_auto_match 0
      foreach cni $netname_ailist {
         if [info exists cfnetnames($cni)] {
            # network name is already configured by the user
            set netname_names($cni) $cfnetnames($cni)
         } else {
            # no name yet configured -> search best among all providers
            lappend netname_automatch $cni
            foreach prov $netname_prov_cnis {
               if [info exists netname_provnets($prov,$cni)] {
                  # remove all non-alphanumeric characters from the name and make it lower case
                  regsub -all -- {[^a-zA-Z0-9]*} $netname_provnets($prov,$cni) {} name
                  set name [string tolower $name]
                  if [info exists netname_xawtv($netname_provnets($prov,$cni))] {
                     incr xawtv_auto_match
                     set netname_names($cni) $netname_xawtv($netname_provnets($prov,$cni))
                     break
                  } elseif [info exists netname_xawtv($name)] {
                     incr xawtv_auto_match
                     set netname_names($cni) $netname_xawtv($name)
                     break
                  } elseif {![info exists netname_names($cni)]} {
                     set netname_names($cni) $netname_provnets($prov,$cni)
                  }
               }
            }
            if {![info exists netname_names($cni)]} {
               # should never happen
               set netname_names($cni) "undefined"
            }
         }
      }

      CreateTransientPopup .netname "Network name configuration"
      set netname_popup 1

      ## first column: listbox with sorted listing of all netwops
      frame .netname.list
      listbox .netname.list.ailist -exportselection false -height 20 -width 0 -selectmode single -relief ridge -yscrollcommand {.netname.list.sc set}
      pack .netname.list.ailist -anchor nw -side left -fill both -expand 1
      scrollbar .netname.list.sc -orient vertical -command {.netname.list.ailist yview}
      pack .netname.list.sc -side left -fill y
      pack .netname.list -side left -pady 10 -padx 10 -fill both -expand 1
      bind .netname.list.ailist <ButtonPress-1> [list + after idle NetworkNameSelection]
      bind .netname.list.ailist <Key-space>     [list + after idle NetworkNameSelection]

      ## second column: info and commands for the selected network
      frame .netname.cmd
      # first row: entry field
      entry .netname.cmd.myname -textvariable netname_entry -font $font_fixed
      pack .netname.cmd.myname -side top -anchor nw -fill x
      trace variable netname_entry w NetworkNameEdited

      # second row: xawtv selection
      frame .netname.cmd.fx
      label .netname.cmd.fx.lab -text "In $tvapp_name:  "
      pack .netname.cmd.fx.lab -side left -anchor w
      menubutton .netname.cmd.fx.mb -takefocus 1 -relief raised -borderwidth 2
      if [array exists netname_xawtv] {
         menu .netname.cmd.fx.mb.men -tearoff 0
         .netname.cmd.fx.mb configure -menu .netname.cmd.fx.mb.men
      } else {
         .netname.cmd.fx.mb configure -state disabled -text "none"
      }
      pack .netname.cmd.fx.mb -side right -anchor e
      pack .netname.cmd.fx -side top -fill x -pady 5

      # third row: closest match
      frame .netname.cmd.fm
      label .netname.cmd.fm.lab -text "Closest match: "
      pack .netname.cmd.fm.lab -side left -anchor w
      button .netname.cmd.fm.match -command NetworkNameUseMatch
      pack .netname.cmd.fm.match -side left -anchor e -fill x -expand 1
      pack .netname.cmd.fm -side top -fill x -pady 5
      if {![array exists netname_xawtv]} {
         .netname.cmd.fm.match configure -state disabled -text "none"
      }

      # fourth row: network description
      label .netname.cmd.lnetdesc -text "Official network description:"
      pack  .netname.cmd.lnetdesc -side top -anchor nw -pady 5
      entry .netname.cmd.enetdesc -state disabled $entry_disabledforeground black
      pack  .netname.cmd.enetdesc -side top -anchor nw -fill x

      # fifth row: provider name selection
      label .netname.cmd.lprovnams -text "Names used by providers:"
      pack .netname.cmd.lprovnams -side top -anchor nw -pady 5
      listbox .netname.cmd.provnams -exportselection false -height [llength $netname_prov_cnis] -width 0 -selectmode single
      pack .netname.cmd.provnams -side top -anchor n -fill both -expand 1
      bind .netname.cmd.provnams <ButtonPress-1> [list + after idle NetworkNameProvSelection]
      bind .netname.cmd.provnams <Key-space>     [list + after idle NetworkNameProvSelection]

      # bottom row: command buttons
      button .netname.cmd.save -text "Save" -width 7 -command NetworkNamesSave
      button .netname.cmd.abort -text "Abort" -width 7 -command NetworkNamesAbort
      button .netname.cmd.help -text "Help" -width 7 -command {PopupHelp $helpIndex(Configuration) "Network names"}
      pack .netname.cmd.help .netname.cmd.abort .netname.cmd.save -side bottom -anchor sw

      pack .netname.cmd -side left -anchor n -pady 10 -padx 10 -fill both -expand 1

      bind .netname <Key-F1> {PopupHelp $helpIndex(Configuration) "Network names"}
      bind .netname.cmd <Destroy> {+ set netname_popup 0}
      focus .netname.cmd.myname

      if {[array exists netname_xawtv]} {
         # insert xawtv station names to popup menu & measure max. name width
         set mbfont [.netname.cmd.fx.mb cget -font]
         set mbwidth 0
         foreach name $xawtv_list {
            .netname.cmd.fx.mb.men add command -label $name -command [list NetworkNameXawtvSelection $name]
            set mbwidthc [font measure $mbfont $name]
            if {$mbwidthc > $mbwidth} {set mbwidth $mbwidthc}
         }
         # set width of xawtv menu buttons to max. width of all station names
         # convert pixel width to character count
         set mbwidth [expr 1 + $mbwidth / [font measure $mbfont "0"]]
         .netname.cmd.fx.mb configure -width [expr $mbwidth + 2]
         .netname.cmd.fm.match configure -width $mbwidth
      }

      # insert network names from all providers into listbox on the left
      foreach cni $netname_ailist {
         .netname.list.ailist insert end $netname_names($cni)
         if {![NetworkNameIsInXawtv $netname_names($cni)]} {
            .netname.list.ailist itemconfigure end -foreground red -selectforeground red
         }
      }
      # set the cursor onto the first network in the listbox
      set netname_idx -1
      .netname.list.ailist selection set 0
      NetworkNameSelection

      # notify the user if names have been automatically modified
      if [array exists netname_xawtv] {
         set automatch_count $xawtv_auto_match
      } else {
         set automatch_count [llength $netname_automatch]
      }
      if {$automatch_count > 0} {
         if {$automatch_count > 1} {
            set plural "s of $automatch_count new networks have"
         } else {
            set plural " of one new network has"
         }
         update
         tk_messageBox -type ok -icon info -parent .netname \
            -message "The name$plural been automatically selected among the names in the available provider databases. Leave the dialog with 'Save' to keep the new list."
      }

      wm resizable .netname 1 1
      update
      wm minsize .netname [winfo reqwidth .netname] [winfo reqheight .netname]

   } else {
      raise .netname
   }
}

# Save changed name for the currently selected network
proc NetworkNameSaveEntry {} {
   global netname_ailist netname_names netname_idx netname_automatch
   global netname_entry

   if {$netname_idx != -1} {
      set cni [lindex $netname_ailist $netname_idx]

      if {[string compare $netname_names($cni) $netname_entry] != 0} {

         # save the selected name
         set netname_names($cni) $netname_entry

         # update the network names listbox
         set sel [.netname.list.ailist curselection]
         .netname.list.ailist delete $netname_idx
         .netname.list.ailist insert $netname_idx $netname_names($cni)
         .netname.list.ailist selection set $sel
         if {![NetworkNameIsInXawtv $netname_names($cni)]} {
            .netname.list.ailist itemconfigure $netname_idx -foreground red -selectforeground red
         }

         # check if the same network was previously auto-matched
         set sel [lsearch -exact $netname_automatch $cni]
         if {$sel != -1} {
            # remove the CNI from the auto list
            set netname_automatch [concat [lrange $netname_automatch 0 [expr $sel - 1]] \
                                          [lrange $netname_automatch [expr $sel + 1] end]]
         }
      }
   }
}

# callback (variable trace) for change in the entry field
proc NetworkNameEdited {trname trops trcmd} {
   global netname_entry netname_ailist netname_idx netname_names netname_xawtv

   if {[array exists netname_xawtv]} {

      if {$netname_idx != -1} {
         set cni [lindex $netname_ailist $netname_idx]

         regsub -all -- {[^a-zA-Z0-9]*} $netname_entry {} name
         set name [string tolower $name]

         if {[info exists netname_xawtv($netname_entry)]} {
            .netname.cmd.fm.match configure -text $netname_xawtv($netname_entry)
            if {[string compare $netname_xawtv($netname_entry) $netname_entry] == 0} {
               .netname.cmd.fm.match configure -foreground black -activeforeground black
            } else {
               .netname.cmd.fm.match configure -foreground red -activeforeground red
            }
         } elseif {[info exists netname_xawtv($name)]} {
            .netname.cmd.fm.match configure -foreground red -activeforeground red -text $netname_xawtv($name)
         } else {
            .netname.cmd.fm.match configure -foreground red -activeforeground red -text "none"
         }
      }
   }
}

# callback for "closest match" button
proc NetworkNameUseMatch {} {
   global netname_entry

   set netname_entry [.netname.cmd.fm.match cget -text]
}

# callback for selection of a network in the ailist
# -> requires update of the information displayed on the right
proc NetworkNameSelection {} {
   global netname_ailist netname_names netname_idx netname_xawtv
   global netname_prov_cnis netname_prov_names netname_provnets netname_provlist
   global netname_entry

   set sel [.netname.list.ailist curselection]
   if {([string length $sel] > 0) && ($sel < [llength $netname_ailist]) && ($sel != $netname_idx)} {
      # save name of the previously selected network, if changed
      NetworkNameSaveEntry

      set netname_idx $sel

      # copy the name of the currently selected network into the entry field
      set cni [lindex $netname_ailist $netname_idx]
      set netname_entry $netname_names($cni)

      # display the matched name in the TV app's channel table
      if {[array exists netname_xawtv]} {
         if {[info exists netname_xawtv($netname_names($cni))]} {
            .netname.cmd.fx.mb configure -text $netname_xawtv($netname_names($cni))
         } else {
            .netname.cmd.fx.mb configure -text "select"
         }
      }

      # display description of the network if available
      .netname.cmd.enetdesc configure -state normal
      .netname.cmd.enetdesc delete 0 end
      .netname.cmd.enetdesc insert 0 [C_GetCniDescription $cni]
      .netname.cmd.enetdesc configure -state disabled

      # rebuild the list of provider's network names
      set netname_provlist {}
      .netname.cmd.provnams delete 0 end
      foreach prov $netname_prov_cnis {
         if [info exists netname_provnets($prov,$cni)] {
            # the netname_provlist keeps track which providers are listed in the box
            lappend netname_provlist $prov
            .netname.cmd.provnams insert end "\[$netname_prov_names($prov)\]  $netname_provnets($prov,$cni)"
         }
      }
   }
}

# callback for selection of a name in the provider listbox
proc NetworkNameProvSelection {} {
   global netname_ailist netname_names netname_idx
   global netname_prov_cnis netname_prov_names netname_provnets netname_provlist
   global netname_entry

   set cni [lindex $netname_ailist $netname_idx]
   set sel [.netname.cmd.provnams curselection]
   if {([string length $sel] > 0) && ($sel < [llength $netname_provlist])} {
      set prov [lindex $netname_provlist $sel]

      if [info exists netname_provnets($prov,$cni)] {
         set netname_entry $netname_provnets($prov,$cni)
         .netname.list.ailist delete $netname_idx
         .netname.list.ailist insert $netname_idx $netname_entry
         .netname.list.ailist selection set $netname_idx
         if {![NetworkNameIsInXawtv $netname_names($cni)]} {
            .netname.list.ailist itemconfigure $netname_idx -foreground red -selectforeground red
         }
      }
   }
}

# callback for selection of a name in the xawtv menu
proc NetworkNameXawtvSelection {name} {
   global netname_entry netname_idx

   set netname_entry $name
   .netname.cmd.fx.mb configure -text $name
   .netname.list.ailist delete $netname_idx
   .netname.list.ailist insert $netname_idx $netname_entry
   .netname.list.ailist selection set $netname_idx
}

# "Save" command button
proc NetworkNamesSave {} {
   global netname_ailist netname_names netname_idx netname_xawtv netname_automatch
   global netname_prov_cnis netname_prov_names netname_provnets netname_provlist
   global netname_entry
   global cfnetnames

   # save name of the currently selected network, if changed
   NetworkNameSaveEntry

   # save names to the rc/ini file
   array unset cfnetnames
   foreach cni $netname_automatch {
      # do not save failed auto-matches - a new provider's name for the CNI might match
      if {![NetworkNameIsInXawtv $netname_names($cni)]} {
         array unset netname_names $cni
      }
   }
   array set cfnetnames [array get netname_names]
   UpdateRcFile

   # update the network menus with the new names
   UpdateNetwopFilterBar

   # Redraw the PI listbox with the new network names
   C_RefreshPiListbox

   # close the window
   destroy .netname

   # free memory
   foreach var {netname_ailist netname_names netname_idx netname_xawtv netname_automatch
                netname_prov_cnis netname_prov_names netname_provnets netname_provlist
                netname_entry} {
      if [info exists $var] {unset $var}
   }
}

# "Abort" command button
proc NetworkNamesAbort {} {
   global netname_automatch
   global cfnetnames netname_names

   # save name of the currently selected network, if changed
   NetworkNameSaveEntry

   set changed 0
   if [array exists cfnetnames] {
      # check if any names have changed (or if there are new CNIs)
      foreach cni [array names netname_names] {
         # ignore automatic name config
         if {[lsearch -exact $netname_automatch $cni] == -1} {
            if {![info exists cfnetnames($cni)] || ([string compare $cfnetnames($cni) $netname_names($cni)] != 0)} {
               set changed 1
               break
            }
         }
      }
   }

   if $changed {
      set answer [tk_messageBox -type okcancel -icon warning -parent .netname -message "Discard all changes?"]
      if {[string compare $answer cancel] == 0} {
         return
      }
   } elseif {([llength $netname_automatch] > 0) || ![array exists cfnetnames]} {
      # failed auto-matches would not have been saved
      set auto 0
      foreach cni $netname_automatch {
         if [NetworkNameIsInXawtv $netname_names($cni)] {
            # there's at least one auto-matched name that would get saved
            set auto 1
            break
         }
      }
      if $auto {
         set answer [tk_messageBox -type okcancel -icon warning -parent .netname -message "Network names have been configured automatically. Really discard them?"]
         if {[string compare $answer cancel] == 0} {
            return
         }
      }
   }

   # close the popup window
   destroy .netname
}

# check if a user-define or auto-matched name is equivalent to TV app's chhnel table
proc NetworkNameIsInXawtv {name} {
   global netname_xawtv netname_names

   if [array exists netname_xawtv] {
      set result 0
      # check if the exact or similar (non-alphanums removed) name is known by TV app
      if [info exists netname_xawtv($name)] {
         # check if the name in xawtv is exactly the same
         if {[string compare $name $netname_xawtv($name)] == 0} {
            set result 1
         }
      }
   } else {
      # no TV app config file found -> no checking
      set result 1
   }
   return $result
}

