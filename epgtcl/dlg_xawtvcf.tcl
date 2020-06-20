#
#  Configuration dialog for TV application interaction
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
#    Implements a GUI for TV application interaction.
#
#  Author: Tom Zoerner
#
#  $Id: dlg_xawtvcf.tcl,v 1.15 2008/08/10 19:14:36 tom Exp tom $
#
set xawtvcf_popup 0

# option defaults
set xawtvcf {tunetv 1 follow 1 dopop 1 poptype 0 duration 7 appcmd ""}
set wintvcf {shm 1 tunetv 1 follow 1 dopop 1}

# callback invoked when a TV app attaches or detaches in the background
proc XawtvConfigShmAttach {attach} {

   # check if the dialog is currently open
   if {[string length [info commands .xawtvcf.connect.lab_tvapp]] > 0} {
      XawtvConfigDisplayShmAttach
   }
}

proc TvAppGetName {} {
   set tvapp_idx [lindex [C_Tvapp_GetConfig] 0]
   if {$tvapp_idx != 0} {
      set tvapp_name [lindex [C_Tvapp_GetTvappList] $tvapp_idx]
   } else {
      set tvapp_name "TV app."
   }
   return $tvapp_name
}


# callback invoked when TV station is changed - only used by vbirec, unused by nxtvepg
proc XawtvConfigShmChannelChange {station freq} {}

#=LOAD=XawtvConfigPopup
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Creates the Xawtv popup configuration dialog
##
proc XawtvConfigPopup {} {
   global xawtvcf_popup
   global xawtvcf wintvcf xawtv_tmpcf
   global xawtv_tmp_tvapp_list
   global xawtv_poptype_names
   global is_unix fileImage font_fixed

   if {$xawtvcf_popup == 0} {
      CreateTransientPopup .xawtvcf "TV Application Interaction Configuration"
      set xawtvcf_popup 1

      # load TV app config into temporary variables
      set tmpl [C_Tvapp_GetConfig]
      set xawtv_tmpcf(tvapp_idx) [lindex $tmpl 0]
      set xawtv_tmpcf(tvapp_path) [lindex $tmpl 1]

      set xawtv_tmp_tvapp_list [C_Tvapp_GetTvappList]

      # load configuration into temporary array
      if $is_unix {
         foreach {opt val} $xawtvcf { set xawtv_tmpcf($opt) $val }
      } else {
         foreach {opt val} $wintvcf { set xawtv_tmpcf($opt) $val }
      }

      frame .xawtvcf.all
      label .xawtvcf.all.lab_main -text "En-/Disable TV app. interaction features:"
      pack .xawtvcf.all.lab_main -side top -anchor w -pady 5 -padx 5

      if {!$is_unix} {
         .xawtvcf.all configure -borderwidth 1 -relief raised
         checkbutton .xawtvcf.all.shm -text "General enable" -variable xawtv_tmpcf(shm) -command XawtvConfigGeneralEnable
         pack .xawtvcf.all.shm -side top -anchor w -padx 5
      }
      checkbutton .xawtvcf.all.tunetv -text "Tune-TV button" -variable xawtv_tmpcf(tunetv)
      pack .xawtvcf.all.tunetv -side top -anchor w -padx 5
      checkbutton .xawtvcf.all.follow -text "Cursor follows channel changes" -variable xawtv_tmpcf(follow)
      pack .xawtvcf.all.follow -side top -anchor w -padx 5
      checkbutton .xawtvcf.all.dopop -text "Display EPG info in TV app." -variable xawtv_tmpcf(dopop)
      pack .xawtvcf.all.dopop -side top -anchor w -padx 5

      if {$is_unix} {
         .xawtvcf.all.dopop configure -command XawtvConfigPopupSelected

         label .xawtvcf.all.lab_poptype -text "How to display EPG info:"
         pack .xawtvcf.all.lab_poptype -side top -anchor w -pady 5
         menubutton .xawtvcf.all.poptype_mb -menu .xawtvcf.all.poptype_mb.men -indicatoron 1
         config_menubutton .xawtvcf.all.poptype_mb
         pack .xawtvcf.all.poptype_mb -side top -anchor w -fill x

         set xawtv_poptype_names [list "Separate popup" \
                                       "Video overlay" \
                                       "Video overlay, 2 lines" \
                                       "Xawtv window title" \
                                       "Use external application"]

         menu .xawtvcf.all.poptype_mb.men -tearoff 0
         for {set idx 0} {$idx < [llength $xawtv_poptype_names]} {incr idx} {
            .xawtvcf.all.poptype_mb.men add radiobutton -label [lindex $xawtv_poptype_names $idx] \
                                           -variable xawtv_tmpcf(poptype) -value $idx \
                                           -command XawtvConfigPopupSelected
         }

         frame .xawtvcf.all.duration
         scale .xawtvcf.all.duration.s -from 0 -to 60 -orient horizontal -label "Popup duration \[seconds\]" \
                                   -variable xawtv_tmpcf(duration)
         pack .xawtvcf.all.duration.s -side left -fill x -expand 1
         pack .xawtvcf.all.duration -side top -fill x -expand 1 -pady 5

         frame .xawtvcf.all.app
         label .xawtvcf.all.app.lab_cmd -text "Command line: "
         pack  .xawtvcf.all.app.lab_cmd -side left -padx 5
         entry .xawtvcf.all.app.ent_cmd -textvariable xawtv_tmpcf(appcmd) -font $font_fixed -width 33
         pack  .xawtvcf.all.app.ent_cmd -side left -padx 5 -fill x -expand 1
         bind  .xawtvcf.all.app.ent_cmd <Enter> {SelectTextOnFocus %W}
         #pack .xawtvcf.all.app -side top -fill x -expand 1 -pady 5
         pack .xawtvcf.all -side top -fill x

         # set widget states according to initial option settings
         XawtvConfigPopupSelected
      } else {
         pack .xawtvcf.all -side top -fill x
      }

      frame  .xawtvcf.connect -borderwidth 1 -relief raised
      checkbutton  .xawtvcf.connect.lab_tvapp -text "" -disabledforeground black -takefocus 0
      bindtags  .xawtvcf.connect.lab_tvapp {.connect .}
      pack   .xawtvcf.connect.lab_tvapp -side top -anchor w -pady 5 -padx 5
      pack   .xawtvcf.connect -side top -fill x
      # display the name of the currently connected TV app, if any
      XawtvConfigDisplayShmAttach

      # create TV app selection popdown menu and "Test" command button
      frame  .xawtvcf.tvapp -borderwidth 1 -relief raised
      label  .xawtvcf.tvapp.lab -text "Specify from where to load the channel table:" -justify left
      pack   .xawtvcf.tvapp.lab -side top -anchor w -padx 5 -pady 5

      frame  .xawtvcf.tvapp.apptype
      label  .xawtvcf.tvapp.apptype.lab -text "TV application:"
      pack   .xawtvcf.tvapp.apptype.lab -side left -padx 5
      menubutton .xawtvcf.tvapp.apptype.mb -text [lindex $xawtv_tmp_tvapp_list $xawtv_tmpcf(tvapp_idx)] \
                                           -menu .xawtvcf.tvapp.apptype.mb.men \
                                           -justify center -indicatoron 1
      config_menubutton .xawtvcf.tvapp.apptype.mb
      menu   .xawtvcf.tvapp.apptype.mb.men -tearoff 0
      set cmd {.xawtvcf.tvapp.apptype.mb configure -text [lindex $xawtv_tmp_tvapp_list $xawtv_tmpcf(tvapp_idx)]}
      set idx 0
      foreach name $xawtv_tmp_tvapp_list {
         .xawtvcf.tvapp.apptype.mb.men add radiobutton -label $name \
                      -variable xawtv_tmpcf(tvapp_idx) -value $idx \
                      -command XawtvConfigSetTvapp
         incr idx
      }
      pack   .xawtvcf.tvapp.apptype.mb -side left -padx 10 -fill x -expand 1

      button .xawtvcf.tvapp.apptype.load -text "Test" -command XawtvConfigTestPathAndType
      pack   .xawtvcf.tvapp.apptype.load -side right -padx 10 -fill x -expand 1
      pack   .xawtvcf.tvapp.apptype -side top -fill x

      # create entry field and command button to configure TV app directory
      frame  .xawtvcf.tvapp.name
      label  .xawtvcf.tvapp.name.prompt -text "Path:"
      pack   .xawtvcf.tvapp.name.prompt -side left -anchor w
      entry  .xawtvcf.tvapp.name.filename -textvariable xawtv_tmpcf(tvapp_path) -font $font_fixed -width 33
      pack   .xawtvcf.tvapp.name.filename -side left -padx 5 -fill x -expand 1
      bind   .xawtvcf.tvapp.name.filename <Enter> {SelectTextOnFocus %W}
      button .xawtvcf.tvapp.name.dlgbut -image $fileImage -command {
         set tmp [tk_chooseDirectory -parent .xawtvcf \
                     -initialdir $xawtv_tmpcf(tvapp_path) \
                     -mustexist 1]
         if {[string length $tmp] > 0} {
            set xawtv_tmpcf(tvapp_path) $tmp
         }
         unset tmp
      }
      pack   .xawtvcf.tvapp.name.dlgbut -side left -padx 5
      pack   .xawtvcf.tvapp.name -side top -padx 5 -pady 5 -anchor w -fill x -expand 1

      # set state and text of the entry field and button
      XawtvConfigSetTvapp

      pack   .xawtvcf.tvapp -side top -anchor w -fill x -pady 5

      # if "general enable" is off, disable the other buttons
      if {!$is_unix} {
         XawtvConfigGeneralEnable
      }

      frame .xawtvcf.cmd
      button .xawtvcf.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration) "TV application interaction"}
      button .xawtvcf.cmd.abort -text "Abort" -width 5 -command XawtvConfigQuit
      button .xawtvcf.cmd.save -text "Ok" -width 5 -command XawtvConfigSave -default active
      pack .xawtvcf.cmd.help .xawtvcf.cmd.abort .xawtvcf.cmd.save -side left -padx 10
      pack .xawtvcf.cmd -side top -pady 10

      bind .xawtvcf <Key-F1> {PopupHelp $helpIndex(Configuration) "TV application interaction"}
      bind .xawtvcf.cmd <Destroy> {+ set xawtvcf_popup 0}
      bind .xawtvcf.cmd.save <Return> {tkButtonInvoke %W}
      bind .xawtvcf.cmd.save <Escape> {tkButtonInvoke .xawtvcf.cmd.abort}
      wm protocol .xawtvcf WM_DELETE_WINDOW XawtvConfigQuit
      focus .xawtvcf.cmd.save
   } else {
      raise .xawtvcf
   }
}

# callback for changes in popup options: adjust state of depending widgets
proc XawtvConfigPopupSelected {} {
   global xawtv_tmpcf
   global xawtv_poptype_names

   if $xawtv_tmpcf(dopop) {
      set state "normal"
   } else {
      set state "disabled"
   }
   .xawtvcf.all.poptype_mb configure -state $state
   .xawtvcf.all.poptype_mb configure -text [lindex $xawtv_poptype_names $xawtv_tmpcf(poptype)]

   if {$xawtv_tmpcf(dopop) && ($xawtv_tmpcf(poptype) < 3)} {
      set state "normal"
      set fg black
   } else {
      set state "disabled"
      set fg [.xawtvcf.all.lab_main cget -disabledforeground]
   }
   .xawtvcf.all.duration.s configure -state $state -foreground $fg

   if {$xawtv_tmpcf(poptype) == 4} {
      pack .xawtvcf.all.app -after .xawtvcf.all.duration -side top -fill x -expand 1 -pady 5
   } else {
      pack forget .xawtvcf.all.app
   }
}

# callback for TV application type popup: disable or enable path entry field
proc XawtvConfigSetTvapp {} {
   global xawtv_tmpcf xawtv_tmp_tvapp_list
   global text_bg

   .xawtvcf.tvapp.apptype.mb configure -text [lindex $xawtv_tmp_tvapp_list $xawtv_tmpcf(tvapp_idx)]

   if {[C_Tvapp_CfgNeedsPath $xawtv_tmpcf(tvapp_idx)] == 0} {
      .xawtvcf.tvapp.name.filename configure -state normal -background #c0c0c0 -textvariable {}
      .xawtvcf.tvapp.name.filename delete 0 end
      .xawtvcf.tvapp.name.filename configure -state disabled
      .xawtvcf.tvapp.name.dlgbut configure -state disabled
   } else {
      .xawtvcf.tvapp.name.filename configure -state normal -background $text_bg -textvariable xawtv_tmpcf(tvapp_path)
      .xawtvcf.tvapp.name.dlgbut configure -state normal
   }
}

# callback for "Test" button next to TV app type and path selection
proc XawtvConfigTestPathAndType {} {
   global xawtv_tmpcf xawtv_tmp_tvapp_list

   set name [lindex $xawtv_tmp_tvapp_list $xawtv_tmpcf(tvapp_idx)]
   set chn_count [C_Tvapp_TestChanTab 1 $xawtv_tmpcf(tvapp_idx) $xawtv_tmpcf(tvapp_path)]

   if {$chn_count > 0} {
      # OK
      if {[llength [C_GetNetwopNames]] > 0} {
         tk_messageBox -type ok -icon info -title "Info" -parent .xawtvcf \
                       -message "Test sucessful: found $chn_count names in the $name channel table."
      } else {
         tk_messageBox -type ok -icon info -title "Info" -parent .xawtvcf \
                       -message "Test sucessful: found $chn_count channels. You can now use the network name dialog in the configure menu to synchronize names between nxtvepg and $name"
      }

   } elseif {$chn_count == 0} {
      # opened ok, but no channels found
      tk_messageBox -type ok -icon warning -title "Warning" -parent .xawtvcf \
                    -message "No channels found in the $name channel table!"
   }
}

# callback for general enable: adjust state of depending widgets
proc XawtvConfigGeneralEnable {} {
   global xawtv_tmpcf

   if $xawtv_tmpcf(shm) {
      set state "normal"
   } else {
      set state "disabled"
   }

   .xawtvcf.all.tunetv configure -state $state
   .xawtvcf.all.follow configure -state $state
   .xawtvcf.all.dopop configure -state $state
}

# callback invoked when a TV app attaches or detaches in the background
proc XawtvConfigDisplayShmAttach {} {

   # fetch the attached app's name from shared memory and display it
   set name [C_Tvapp_QueryTvapp]
   if {[string length $name] > 0} {
      .xawtvcf.connect.lab_tvapp configure -text "Connected to: $name" -selectcolor green
      after idle .xawtvcf.connect.lab_tvapp invoke
   } else {
      # no app is currently connected
      .xawtvcf.connect.lab_tvapp configure -text "Connected to: not connected" -selectcolor red
      after idle .xawtvcf.connect.lab_tvapp deselect
   }
}

# clean up temporary variables ans close main window
proc XawtvConfigQuit {} {
   global xawtv_tmp_tvapp_list xawtv_tmpcf
   global xawtv_poptype_names

   # free memory in temporary variables
   catch {unset xawtv_poptype_names}
   unset xawtv_tmp_tvapp_list
   array unset xawtv_tmpcf

   # close the dialog window
   destroy .xawtvcf
}

# callback for OK button: save config into variables
proc XawtvConfigSave {} {
   global xawtvcf wintvcf xawtv_tmpcf
   global is_unix

   set tmpl [C_Tvapp_GetConfig]

   if {($xawtv_tmpcf(tvapp_idx) != 0)} {
      # silently test the channel table (i.e. don't show diagnostics as with "Test" button)
      set test_result [C_Tvapp_TestChanTab 0 $xawtv_tmpcf(tvapp_idx) $xawtv_tmpcf(tvapp_path)]
      if {$test_result < 0} {
         set answer [tk_messageBox -type okcancel -default cancel -icon warning -parent .xawtvcf \
                        -message "Cannot load the TV channel table - You should press 'Cancel' now and then use the 'Test' button to the TV application type and path settings."]
      } elseif {$test_result == 0} {
         set answer [tk_messageBox -type okcancel -default cancel -icon warning -parent .xawtvcf \
                        -message "The selected TV application's channel table is empty. Some features will not work without a frequency table!"]
      } else {
         set answer "ok"
      }
      if {[string compare $answer "cancel"] == 0} return
   }

   if $is_unix {
      set xawtvcf [list "tunetv" $xawtv_tmpcf(tunetv) \
                        "follow" $xawtv_tmpcf(follow) \
                        "dopop" $xawtv_tmpcf(dopop) \
                        "poptype" $xawtv_tmpcf(poptype) \
                        "duration" $xawtv_tmpcf(duration) \
                        "appcmd" $xawtv_tmpcf(appcmd)]
   } else {
      set wintvcf [list "shm" $xawtv_tmpcf(shm) \
                        "tunetv" $xawtv_tmpcf(tunetv) \
                        "follow" $xawtv_tmpcf(follow) \
                        "dopop" $xawtv_tmpcf(dopop)]
   }

   # save config
   C_Tvapp_UpdateConfig $xawtv_tmpcf(tvapp_idx) $xawtv_tmpcf(tvapp_path)
   UpdateRcFile

   # load config vars into the TV interaction module
   C_Tvapp_InitConfig

   XawtvConfigQuit

   # trigger related dialogs, if open
   EpgScan_UpdateTvApp
   NetworkNaming_UpdateTvApp
}

