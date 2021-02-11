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

#=CONST= ::tvapp_path_type_none     0
#=CONST= ::tvapp_path_type_dir      1
#=CONST= ::tvapp_path_type_file     2

#=LOAD=XawtvConfigPopup
#=LOAD=TvAppConfigWid_Create
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Creates the Xawtv popup configuration dialog
##
proc XawtvConfigPopup {} {
   global xawtvcf_popup
   global xawtvcf wintvcf xawtv_tmpcf
   global xawtv_poptype_names
   global is_unix font_fixed

   if {$xawtvcf_popup == 0} {
      CreateTransientPopup .xawtvcf "TV Application Interaction Configuration"
      set xawtvcf_popup 1

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
      TvAppConfigWid_Create .xawtvcf.tvapp 1
      pack   .xawtvcf.tvapp -side top -anchor w -fill x -pady 5

      # if "general enable" is off, disable the other buttons
      if {!$is_unix} {
         XawtvConfigGeneralEnable
      }

      frame .xawtvcf.cmd
      button .xawtvcf.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configure menu) "TV application interaction"}
      button .xawtvcf.cmd.abort -text "Abort" -width 5 -command XawtvConfigQuit
      button .xawtvcf.cmd.save -text "Ok" -width 5 -command XawtvConfigSave -default active
      pack .xawtvcf.cmd.help .xawtvcf.cmd.abort .xawtvcf.cmd.save -side left -padx 10
      pack .xawtvcf.cmd -side top -pady 10

      bind .xawtvcf <Key-F1> {PopupHelp $helpIndex(Configure menu) "TV application interaction"}
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
   global xawtv_tmpcf
   global xawtv_poptype_names

   # free memory in temporary variables
   catch {unset xawtv_poptype_names}
   array unset xawtv_tmpcf

   # close the dialog window
   destroy .xawtvcf
}

# callback for OK button: save config into variables
proc XawtvConfigSave {} {
   global xawtvcf wintvcf xawtv_tmpcf
   global is_unix

   set tv_app_cfg [TvAppConfigWid_GetResults .xawtvcf]

   if {([lindex $tv_app_cfg 0] != 0)} {
      # silently test the channel table (i.e. don't show diagnostics as with "Test" button)
      set test_result [C_Tvapp_TestChanTab [lindex $tv_app_cfg 0] [lindex $tv_app_cfg 1]]
      set chn_count [lindex $test_result 0]
      set err_msg [lindex $test_result 1]

      if {$chn_count < 0} {
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
   C_Tvapp_UpdateConfig [lindex $tv_app_cfg 0] [lindex $tv_app_cfg 1]
   UpdateRcFile

   # load config vars into the TV interaction module
   C_Tvapp_InitConfig

   XawtvConfigQuit

   # trigger related dialogs, if open
   EpgScan_UpdateTvApp
   NetworkNaming_UpdateTvApp
}

##  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
##  TV application selection & path configuration sub-widget
##

proc TvAppConfigWid_Create {wid with_label} {
   global tv_app_cfg_idx tv_app_cfg_path tv_app_cfg_selist tv_app_prev_idx
   global font_fixed fileImage

   set tv_app_cfg_selist [C_Tvapp_GetTvappList]

   # load TV app config into temporary variables
   set tmpl [C_Tvapp_GetConfig]
   set tv_app_cfg_idx [lindex $tmpl 0]
   set tv_app_cfg_path [lindex $tmpl 1]
   set tv_app_prev_idx $tv_app_cfg_idx

   if {$with_label} {
      label  $wid.lab -text "Specify from where to load the channel table:" -justify left
      pack   $wid.lab -side top -anchor w -padx 5 -pady 5
   }

   frame  $wid.apptype
   label  $wid.apptype.lab -text "TV application:"
   pack   $wid.apptype.lab -side left -padx 5
   menubutton $wid.apptype.mb -text [lindex $tv_app_cfg_selist $tv_app_cfg_idx] \
                                        -menu $wid.apptype.mb.men \
                                        -justify center -indicatoron 1
   config_menubutton $wid.apptype.mb
   menu   $wid.apptype.mb.men -tearoff 0
   set cmd {$wid.apptype.mb configure -text [lindex $tv_app_cfg_selist $tv_app_cfg_idx]}
   set idx 0
   foreach name $tv_app_cfg_selist {
      $wid.apptype.mb.men add radiobutton -label $name \
                   -variable tv_app_cfg_idx -value $idx \
                   -command [list TvAppConfigWid_SetTvapp $wid]
      incr idx
   }
   pack   $wid.apptype.mb -side left -padx 10 -fill x -expand 1

   button $wid.apptype.load -text "Test" -command [list TvAppConfigWid_TestPathAndType $wid $with_label]
   pack   $wid.apptype.load -side right -padx 10 -fill x -expand 1
   pack   $wid.apptype -side top -fill x

   # create entry field and command button to configure TV app directory
   frame  $wid.name
   label  $wid.name.prompt -text "Path:"
   pack   $wid.name.prompt -side left -anchor w
   entry  $wid.name.filename -textvariable tv_app_cfg_path -font $font_fixed -width 33
   pack   $wid.name.filename -side left -padx 5 -fill x -expand 1
   bind   $wid.name.filename <Enter> {SelectTextOnFocus %W}
   button $wid.name.dlgbut -image $fileImage -command [list TvAppConfigWid_OpenTvappPathSel $wid]
   pack   $wid.name.dlgbut -side left -padx 5
   pack   $wid.name -side top -padx 5 -pady 5 -anchor w -fill x -expand 1

   # set state and text of the entry field and button
   TvAppConfigWid_SetTvapp $wid
}

# callback for the directory selection button: open modal file selector dialog
proc TvAppConfigWid_OpenTvappPathSel {wid} {
   global tv_app_cfg_idx tv_app_cfg_path

   if {[C_Tvapp_CfgNeedsPath $tv_app_cfg_idx] == $::tvapp_path_type_file} {
      set tmp [tk_getOpenFile -parent [winfo toplevel $wid] \
                  -title "Select a channel configuration file" \
                  -initialdir $tv_app_cfg_path \
                  -filetypes {{"Channel config" ".conf"} {"any" "*"}}]
   } else {
      set tmp [tk_chooseDirectory -parent [winfo toplevel $wid] \
                  -title "Select TV app's installation path" \
                  -initialdir $tv_app_cfg_path \
                  -mustexist 1]
   }
   if {[string length $tmp] > 0} {
      set tv_app_cfg_path $tmp
   }
}

# callback for TV application type popup: disable or enable path entry field
proc TvAppConfigWid_SetTvapp {wid} {
   global tv_app_cfg_idx tv_app_cfg_path tv_app_cfg_selist tv_app_prev_idx
   global text_bg

   $wid.apptype.mb configure -text [lindex $tv_app_cfg_selist $tv_app_cfg_idx]

   set app_path ""
   if {[C_Tvapp_CfgNeedsPath $tv_app_cfg_idx app_path] == $::tvapp_path_type_none} {
      $wid.name.filename configure -state normal -background #c0c0c0 -textvariable {}
      $wid.name.filename delete 0 end
      $wid.name.filename configure -state disabled
      $wid.name.dlgbut configure -state disabled
   } else {
      $wid.name.filename configure -state normal -background $text_bg -textvariable tv_app_cfg_path
      $wid.name.dlgbut configure -state normal

      if {$tv_app_cfg_idx != $tv_app_prev_idx} {
         set tv_app_cfg_path $app_path
         set tv_app_prev_idx $tv_app_cfg_idx
      }
   }
}

# callback for "Test" button next to TV app type and path selection
proc TvAppConfigWid_TestPathAndType {wid with_netname_hint} {
   global tv_app_cfg_idx tv_app_cfg_path tv_app_cfg_selist

   set name [lindex $tv_app_cfg_selist $tv_app_cfg_idx]

   set test_result [C_Tvapp_TestChanTab $tv_app_cfg_idx $tv_app_cfg_path]
   set chn_count [lindex $test_result 0]
   set err_msg [lindex $test_result 1]

   if {$err_msg ne ""} {
      tk_messageBox -type ok -icon error -parent [winfo toplevel $wid] -message $err_msg
   } elseif {$chn_count > 0} {
      # OK
      if {[llength [C_GetNetwopNames]] > 0} {
         tk_messageBox -type ok -icon info -title "Info" -parent [winfo toplevel $wid] \
                       -message "Test sucessful: found $chn_count TV channels in $name."
      } else {
         set msg "Test sucessful: found $chn_count channels."
         if {$with_netname_hint} {
            append msg " You can now use the network name dialog in the configure menu to synchronize names between nxtvepg and $name"
         }
         tk_messageBox -type ok -icon info -title "Info" -parent [winfo toplevel $wid] -message $msg
      }

   } elseif {$chn_count == 0} {
      # opened ok, but no channels found
      tk_messageBox -type ok -icon warning -title "Warning" -parent [winfo toplevel $wid] \
                    -message "No channels found in the $name channel table!"
   }
}

# widget owner interface for retrieving results
# note the result is shared if multiple dialogs instantiate the widget
proc TvAppConfigWid_GetResults {wid} {
   global tv_app_cfg_idx tv_app_cfg_path tv_app_cfg_selist

   return [list $tv_app_cfg_idx $tv_app_cfg_path]
}
