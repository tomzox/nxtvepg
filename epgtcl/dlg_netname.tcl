#
#  Configuration dialog for network names
#
#  Copyright (C) 1999-2011, 2020-2023 T. Zoerner
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
set netname_popup 0

# called by tv application config dialog
proc NetworkNaming_UpdateTvApp {} {
   global netname_ailist netname_names netname_idx
   global netname_popup

   if {$netname_popup} {
      # read TV app channel table
      NetworkNameReadTvChanTab

      # insert network names from all providers into listbox on the left
      .netname.list.ailist delete 0 end
      foreach cni $netname_ailist {
         .netname.list.ailist insert end $netname_names($cni)
         if {[NetworkNameIsInXawtv $netname_names($cni)] == 0} {
            .netname.list.ailist itemconfigure end -foreground red -selectforeground red
         }
      }
      # set the cursor onto the first network in the listbox
      set netname_idx -1
      .netname.list.ailist selection set 0
      NetworkNameSelection

      NetworkNameUpdateSimilar
   }
}

#=LOAD=NetworkNamingPopup
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Configure individual names for networks
##
proc NetworkNamingPopup {} {
   global netname_ailist netname_names netname_idx netname_xawtv netname_automatch
   global netname_prov_cnis netname_provnets
   global netname_entry netname_tvsync_state
   global netname_popup font_fixed entry_disabledforeground

   if {$netname_popup == 0} {

      if {![C_IsDatabaseLoaded]} {
         tk_messageBox -type ok -default ok -icon error -parent . \
                       -message "You have to load an XMLTV file before configuring networks."
         return
      }

      # build list of currently used providers
      set netname_prov_cnis [C_GetProviderPath]

      # build array of providers' network names
      foreach prov $netname_prov_cnis {
         array unset tmparr
         set cnilist [C_GetAiNetwopList $prov tmparr ai_names]
         foreach cni $cnilist {
            set netname_provnets($prov,$cni) $tmparr($cni)
         }
      }
      array unset tmparr

      # retrieve list of all networks of the current provider
      set netname_ailist [eval concat [C_GetProvCniConfig]]

      if {[llength $netname_ailist] == 0} {
         # network selection dialog was not used yet
         set netname_ailist [C_GetAiNetwopList "" tmparr ai_names]
      }

      # build array with all names from .xawtv rc file
      # (note: additional entries are generated for name parts separated by "/")
      NetworkNameReadTvChanTab

      # copy or build an array with the currently configured network names
      set netname_automatch {}
      set xawtv_auto_match 0
      array set cfnetnames [C_GetNetwopNames]
      foreach cni $netname_ailist {
         if {[info exists cfnetnames($cni)]} {
            # network name is already configured by the user
            set netname_names($cni) $cfnetnames($cni)
         } else {
            # no name yet configured -> search best among all providers
            lappend netname_automatch $cni
            foreach prov $netname_prov_cnis {
               set key "$prov,$cni"
               if {[info exists netname_provnets($key)]} {
                  # remove non-alphanumeric ASCII characters from the name
                  regsub -all -- {[\0-\/\:-\?\[\\\]\^\_\{\|\}\~]*} $netname_provnets($key) {} name
                  # make it lower-case
                  set name [string tolower $name]
                  if {[info exists netname_xawtv($netname_provnets($key))]} {
                     if {$name ne $netname_provnets($key)} {
                        incr xawtv_auto_match
                     }
                     set netname_names($cni) $netname_xawtv($netname_provnets($key))
                     break
                  } elseif {[info exists netname_xawtv($name)]} {
                     if {$name ne $netname_provnets($key)} {
                        incr xawtv_auto_match
                     }
                     set netname_names($cni) $netname_xawtv($name)
                     break
                  } elseif {![info exists netname_names($cni)]} {
                     set netname_names($cni) $netname_provnets($key)
                  }
               }
            }
            if {![info exists netname_names($cni)]} {
               set netname_names($cni) "?? ($cni)"
            }
         }
      }

      # load special widget libraries
      mclistbox_load

      CreateTransientPopup .netname "Network name configuration"
      set netname_popup 1

      # determine listbox height and check if scrollbar is required
      set lbox_height [llength $netname_ailist]
      set do_scrollbar 0
      if {$lbox_height > 27} {
         set lbox_height 25
         set do_scrollbar 1
      }

      ## first column: listbox with sorted listing of all netwops
      frame .netname.list
      listbox .netname.list.ailist -exportselection false -height $lbox_height -width 0 \
                                   -selectmode single -yscrollcommand {.netname.list.sc set}
      relief_listbox .netname.list.ailist
      pack .netname.list.ailist -anchor nw -side left -fill both -expand 1
      scrollbar .netname.list.sc -orient vertical -command {.netname.list.ailist yview} -takefocus 0
      if {$do_scrollbar} {pack .netname.list.sc -side left -fill y}
      pack .netname.list -side left -pady 10 -padx 10 -fill both -expand 1
      bind .netname.list.ailist <ButtonPress-1> [list + after idle NetworkNameSelection]
      bind .netname.list.ailist <Key-space>     [list + after idle NetworkNameSelection]

      ## second column: info and commands for the selected network
      frame .netname.ctrl
      # first row: entry field
      label .netname.ctrl.lab_myname -text "Enter your preferred name:"
      pack .netname.ctrl.lab_myname -side top -anchor nw
      entry .netname.ctrl.myname -textvariable netname_entry -font $font_fixed -width 40
      bind .netname.ctrl.myname <Key-Return> NetworkName_KeyReturn
      bind .netname.ctrl.myname <Key-Escape> NetworkName_KeyEscape
      pack .netname.ctrl.myname -side top -anchor nw -fill x
      trace variable netname_entry w NetworkNameEdited

      labelframe .netname.ctrl.tv -text "Name synchronization with [TvAppGetName]"
      checkbutton .netname.ctrl.tv.chk_tvsync -text "TV app. name sync status " -variable netname_tvsync_state \
                               -disabledforeground black -takefocus 0 -font $::font_normal
      pack .netname.ctrl.tv.chk_tvsync -side top -anchor nw
      bindtags .netname.ctrl.tv.chk_tvsync {.netname .}

      # second row: xawtv selection
      label .netname.ctrl.tv.lab_tvapp -text "Similar names in TV channel table:" -font $::font_normal
      #pack .netname.ctrl.tv.lab_tvapp -side top -anchor nw
      frame .netname.ctrl.tv.tvl
      listbox .netname.ctrl.tv.tvl.ailist -exportselection false -height 5 -width 0 \
                         -selectmode single -yscrollcommand {.netname.ctrl.tv.tvl.sc set}
      pack .netname.ctrl.tv.tvl.ailist -anchor nw -side left -fill both -expand 1
      scrollbar .netname.ctrl.tv.tvl.sc -orient vertical -command {.netname.ctrl.tv.tvl.ailist yview} -takefocus 0
      pack .netname.ctrl.tv.tvl.sc -side left -fill y
      #pack .netname.ctrl.tv.tvl -side top -fill both -expand 1
      bind .netname.ctrl.tv.tvl.ailist <Double-Button-1> NetworkNameXawtvSelection
      pack .netname.ctrl.tv -side top -fill both -expand 1 -pady 5

      # third row: provider name selection
      labelframe .netname.ctrl.id -text "Original names used in XMLTV file"
      frame .netname.ctrl.id.provnams
      mclistbox::mclistbox .netname.ctrl.id.provnams.lbox -relief sunken -columnrelief flat -labelanchor c \
                                -height 4 -selectmode browse -columnborderwidth 0 \
                                -exportselection 0 -font $::font_normal -labelfont [DeriveFont $::font_normal 0 bold] \
                                -yscrollcommand [list .netname.ctrl.id.provnams.sc set] -fillcolumn title
      .netname.ctrl.id.provnams.lbox column add "provider" -label "Provider" -width 25
      .netname.ctrl.id.provnams.lbox column add "netname" -label "Network name" -width 30
      bind .netname.ctrl.id.provnams.lbox <Double-Button-1> NetworkNameProvSelection
      bind .netname.ctrl.id.provnams.lbox <Key-Return>      NetworkNameProvSelection
      bind .netname.ctrl.id.provnams.lbox <Key-space>       NetworkNameProvSelection
      pack .netname.ctrl.id.provnams.lbox -side left -anchor n -fill both -expand 1
      scrollbar .netname.ctrl.id.provnams.sc -orient vertical -command {.netname.ctrl.id.provnams.lbox yview} -takefocus 0
      pack .netname.ctrl.id.provnams.sc -side left -fill y
      pack .netname.ctrl.id.provnams -side top -fill y -fill both -expand 1
      pack .netname.ctrl.id -side top -fill both -expand 1 -pady 5

      # bottom row: command buttons
      frame .netname.ctrl.cmd
      button .netname.ctrl.cmd.help -text "Help" -width 7 -command {PopupHelp $helpIndex(Configure menu) "Network names"}
      button .netname.ctrl.cmd.abort -text "Abort" -width 7 -command {NetworkNamesQuit 1}
      button .netname.ctrl.cmd.ok -text "Ok" -width 7 -command {NetworkNamesQuit 0} -default active
      bind .netname.ctrl.cmd.ok <Return> {tkButtonInvoke .netname.ctrl.cmd.ok}
      bind .netname.ctrl <Escape> {tkButtonInvoke .netname.ctrl.cmd.abort}
      pack .netname.ctrl.cmd.help .netname.ctrl.cmd.abort .netname.ctrl.cmd.ok -side left -padx 10
      pack .netname.ctrl.cmd -side top -fill x -pady 5

      pack .netname.ctrl -side left -anchor n -pady 10 -padx 10 -fill both -expand 1

      bind .netname <Key-F1> {PopupHelp $helpIndex(Configure menu) "Network names"}
      bind .netname.ctrl <Destroy> {+ set netname_popup 0}
      wm protocol .netname WM_DELETE_WINDOW {NetworkNamesQuit 1}
      focus .netname.ctrl.myname

      # insert network names from all providers into listbox on the left
      foreach cni $netname_ailist {
         .netname.list.ailist insert end $netname_names($cni)
         if {[NetworkNameIsInXawtv $netname_names($cni)] == 0} {
            .netname.list.ailist itemconfigure end -foreground red -selectforeground red
         }
      }
      # set the cursor onto the first network in the listbox
      set netname_idx -1
      .netname.list.ailist selection set 0
      NetworkNameSelection

      # notify the user if names have been automatically modified
      if {[array exists netname_xawtv]} {
         set automatch_count $xawtv_auto_match
      } else {
         set automatch_count 0
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

      if {([string compare $netname_names($cni) $netname_entry] != 0) && \
          ($netname_entry ne "")} {

         # save the selected name
         set netname_names($cni) $netname_entry

         # update the network names listbox
         set sel [.netname.list.ailist curselection]
         .netname.list.ailist delete $netname_idx
         .netname.list.ailist insert $netname_idx $netname_names($cni)
         .netname.list.ailist selection set $sel
         if {[NetworkNameIsInXawtv $netname_names($cni)] == 0} {
            .netname.list.ailist itemconfigure $netname_idx -foreground red -selectforeground red
         }

         # check if the same network was previously auto-matched
         set sel [lsearch -exact $netname_automatch $cni]
         if {$sel != -1} {
            # remove the CNI from the auto list
            set netname_automatch [lreplace $netname_automatch $sel $sel]
         }
      }
   }
}

# callback (variable trace) for change in the entry field
proc NetworkNameEdited {trname trops trcmd} {
   global netname_simi_upd_sched

   if {![info exists netname_simi_upd_sched]} {
      after idle NetworkNameUpdateSimilar
      set netname_simi_upd_sched 1
   }
}

proc NetworkNameUpdateSimilar {} {
   global netname_entry netname_ailist netname_idx netname_names netname_xawtv
   global netname_simi_upd_sched netname_tvsync_state

   catch {unset netname_simi_upd_sched}

   if {[array exists netname_xawtv]} {

      .netname.ctrl.tv.tvl.ailist delete 0 end

      set sync_color "red"
      set sync_text "Does not match any TV channel name"
      if {$netname_idx != -1} {
         if {[string length $netname_entry] > 0} {
            set cni [lindex $netname_ailist $netname_idx]
            set nl {}

            # whitespace and non-alpha compression
            regsub -all -- {[\0-\/\:-\?\[\\\]\^\_\{\|\}\~]*} $netname_entry {} name
            set name [string tolower $name]

            # exact same name
            if {[info exists netname_xawtv($netname_entry)]} {
               if {$netname_xawtv($netname_entry) eq $netname_entry} {
                  set sync_color "green"
                  set sync_text "Identical name in TV channel table"
                  set netname_tvsync_state 1
                  lappend nl $netname_xawtv($netname_entry)
               }
            }
            # exact after whitespace & punctuation compression
            if {[info exists netname_xawtv($name)]} {
               if {[llength $nl] == 0} {
                  set sync_color "yellow"
                  set sync_text "Slightly different name in TV channel table"
                  set netname_tvsync_state 0
               }
               lappend nl $netname_xawtv($name)
            }
            # entry is prefix (1 letter)
            set tmpl [array names netname_xawtv]
            foreach name_idx [lsearch -glob -all $tmpl "${name}*"] {
               lappend nl $netname_xawtv([lindex $tmpl $name_idx])
            }
            # entry is part of the name (at least 3 letters)
            if {[string length $name] >= 3} {
               foreach name_idx [lsearch -glob -all $tmpl "*${name}*"] {
                  lappend nl $netname_xawtv([lindex $tmpl $name_idx])
               }
            }
            # channel name is prefix of entry
            for {set name_part [string replace $name 0 0]} {$name_part ne ""} {set name_part [string replace $name_part 0 0]} {
               if {[info exists netname_xawtv($name_part)]} {
                  lappend nl $netname_xawtv($name_part)
               }
            }
            # channel name is part of the entry
            for {set name_part [string replace $name end end]} {$name_part ne ""} {set name_part [string replace $name_part end end]} {
               if {[info exists netname_xawtv($name_part)]} {
                  lappend nl $netname_xawtv($name_part)
               }
            }
            # beginning of the entry is prefix (1 letter)
            for {set name_part [string replace $name end end]} {[string length $name_part] >= 2} {set name_part [string replace $name_part end end]} {
               foreach name_idx [lsearch -glob -all $tmpl "${name_part}*"] {
                  lappend nl $netname_xawtv([lindex $tmpl $name_idx])
               }
            }

            # unify and display the list of names
            array set tmpa {}
            foreach name $nl {
               if {![info exists tmpa($name)]} {
                  .netname.ctrl.tv.tvl.ailist insert end $name
                  set tmpa($name) 0
               }
            }

         } else {
            # entry field is empty -> display all TV channel names in correct order
            foreach name [C_Tvapp_GetStationNames 1] {
               .netname.ctrl.tv.tvl.ailist insert end $name
            }
         }

      }

      if {$sync_color eq "red"} {
         set netname_tvsync_state 0
      }
      # show the listbox showing TV app channel names in case it was hidden earlier
      pack .netname.ctrl.tv.lab_tvapp -side top -anchor nw
      pack .netname.ctrl.tv.tvl -side top -fill both -expand 1
      pack configure .netname.ctrl.tv -expand 1

   } else {
      set sync_color "#C0C0C0"
      set sync_text "No TV application is configured"
      set netname_tvsync_state 0

      # hide the listbox showing TV app channel names from display
      pack forget .netname.ctrl.tv.lab_tvapp
      pack forget .netname.ctrl.tv.tvl
      pack configure .netname.ctrl.tv -expand 0
   }

   # sync status
   .netname.ctrl.tv.chk_tvsync configure -selectcolor $sync_color -text $sync_text
}

# called by tv application config dialog
proc NetworkNameReadTvChanTab {} {
   global netname_xawtv

   array unset netname_xawtv

   set xawtv_enabled [expr [lindex [C_Tvapp_GetConfig] 0] != 0]
   if {$xawtv_enabled} {
      set xawtv_list [C_Tvapp_GetStationNames 1]
      foreach name $xawtv_list {
         set netname_xawtv($name) $name
         regsub -all -- {[\0-\/\:-\?\[\\\]\^\_\{\|\}\~]*} $name {} tmp
         set netname_xawtv([string tolower $tmp]) $name
      }
   }
}

# callback for "Return" key in name entry field
proc NetworkName_KeyReturn {} {
   global netname_ailist

   # save name of the selected network
   NetworkNameSaveEntry

   # move cursor to the next network
   set sel [.netname.list.ailist curselection]
   if {([string length $sel] > 0) && ($sel + 1 < [llength $netname_ailist])} {
      incr sel

      .netname.list.ailist selection clear 0 end
      .netname.list.ailist selection set $sel
      .netname.list.ailist see $sel
      NetworkNameSelection
   }
}

# callback for "Escape" key in name entry field
proc NetworkName_KeyEscape {} {
   global netname_entry netname_ailist netname_names

   set sel [.netname.list.ailist curselection]
   if {([string length $sel] > 0) && ($sel + 1 < [llength $netname_ailist])} {

      # copy old name into the entry field
      set cni [lindex $netname_ailist $sel]
      set netname_entry $netname_names($cni)

      .netname.ctrl.myname icursor end
   }
}

# callback for selection of a network in the ailist
# -> requires update of the information displayed on the right
proc NetworkNameSelection {} {
   global netname_ailist netname_names netname_idx netname_xawtv
   global netname_prov_cnis netname_provnets netname_provlist
   global netname_entry

   set sel [.netname.list.ailist curselection]
   if {([string length $sel] > 0) && ($sel < [llength $netname_ailist]) && ($sel != $netname_idx)} {
      # save name of the previously selected network, if changed
      NetworkNameSaveEntry

      set netname_idx $sel

      # copy the name of the currently selected network into the entry field
      # note: trace on the assigned variable causes update of "similar" list
      set cni [lindex $netname_ailist $netname_idx]
      set netname_entry $netname_names($cni)
      .netname.ctrl.myname icursor end

      # rebuild the list of provider's network names
      set netname_provlist {}
      .netname.ctrl.id.provnams.lbox delete 0 end
      foreach prov $netname_prov_cnis {
         set key "$prov,$cni"
         if {[info exists netname_provnets($key)]} {
            # the netname_provlist keeps track which providers are listed in the box
            lappend netname_provlist $prov
            .netname.ctrl.id.provnams.lbox insert end \
                [list [file tail $prov] $netname_provnets($key)]
         }
      }
   }
}

# callback for selection of a name in the provider listbox
proc NetworkNameProvSelection {} {
   global netname_ailist netname_names netname_idx
   global netname_prov_cnis netname_provnets netname_provlist
   global netname_entry

   set cni [lindex $netname_ailist $netname_idx]
   set sel [.netname.ctrl.id.provnams.lbox curselection]
   if {([string length $sel] > 0) && ($sel < [llength $netname_provlist])} {
      set prov [lindex $netname_provlist $sel]
      set key "$prov,$cni"
      if {[info exists netname_provnets($key)]} {
         set netname_entry $netname_provnets($key)
      }
   }
}

# callback for selection of a name in the xawtv menu
proc NetworkNameXawtvSelection {} {
   global netname_entry netname_idx

   set sel [.netname.ctrl.tv.tvl.ailist curselection]
   if {[llength $sel] == 1} {
      set netname_entry [.netname.ctrl.tv.tvl.ailist get $sel]
      .netname.ctrl.myname icursor end
   }
}

# helper function which checks for modifications (used for "Abort" button callback)
proc NetworkNames_CheckAbort {} {
   global netname_automatch
   global netname_names

   set cfnetname_list [C_GetNetwopNames]
   set changed 0
   array set cfnetnames $cfnetname_list

   # check if any names have changed (or if there are new CNIs)
   foreach cni [array names netname_names] {
      # ignore automatic name config
      if {[lsearch -exact $netname_automatch $cni] == -1} {
         if {![info exists cfnetnames($cni)] ||
             ([string compare $cfnetnames($cni) $netname_names($cni)] != 0)} {
            set changed 1
            break
         }
      }
   }

   if {$changed} {
      set answer [tk_messageBox -type okcancel -icon warning -parent .netname -message "Discard all changes?"]
      if {[string compare $answer cancel] == 0} {
         return 0
      }
   } elseif {([llength $netname_automatch] > 0) || ([llength $cfnetname_list] == 0)} {
      # failed auto-matches would not have been saved
      set auto 0
      foreach cni $netname_automatch {
         if {[NetworkNameIsInXawtv $netname_names($cni)] > 0} {
            # there's at least one auto-matched name that would get saved
            set auto 1
            break
         }
      }
      if {$auto} {
         set answer [tk_messageBox -type okcancel -icon warning -parent .netname -message "Network names have been configured automatically. Really discard them?"]
         if {[string compare $answer cancel] == 0} {
            return 0
         }
      }
   }
   return 1
}

# "OK" command button
proc NetworkNamesQuit {is_abort} {
   global netname_ailist netname_names netname_idx netname_xawtv netname_automatch
   global netname_prov_cnis netname_provnets netname_provlist
   global netname_entry netname_simi_upd_sched

   # save name of the currently selected network, if changed
   NetworkNameSaveEntry

   # warn about losing changes when doing abort
   if {$is_abort} {
      if {[NetworkNames_CheckAbort] == 0} {
         return
      }
   } else {
      # save names to the rc/ini file
      foreach cni $netname_automatch {
         # do not save failed auto-matches - a new provider's name for the CNI might match
         if {![NetworkNameIsInXawtv $netname_names($cni)]} {
            array unset netname_names $cni
         }
      }
      C_UpdateNetwopNames [array get netname_names]

      # update the network menus with the new names
      UpdateNetwopFilterBar

      # Redraw the PI listbox with the new network names
      C_PiBox_Refresh
   }

   # close the window
   destroy .netname

   # free memory
   foreach var {netname_ailist netname_names netname_idx netname_xawtv netname_automatch
                netname_prov_cnis netname_provnets netname_provlist
                netname_entry netname_simi_upd_sched} {
      catch {unset $var}
   }
}

# check if a user-defined or auto-matched name is equivalent to TV app's channel table
proc NetworkNameIsInXawtv {name} {
   global netname_xawtv netname_names

   if {[array exists netname_xawtv]} {
      set result 0
      # check if the exact or similar (non-alphanums removed) name is known by TV app
      if {[info exists netname_xawtv($name)]} {
         # check if the name in xawtv is exactly the same
         if {[string compare $name $netname_xawtv($name)] == 0} {
            set result 1
         }
      }
   } else {
      # no TV app config file found -> no checking
      set result -1
   }
   return $result
}

