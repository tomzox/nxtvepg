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
#  $Id: dlg_xawtvcf.tcl,v 1.3 2002/11/10 20:31:01 tom Exp tom $
#
set xawtvcf_popup 0

# option defaults
set xawtvcf {tunetv 1 follow 1 dopop 1 poptype 0 duration 7}
set wintvcf {shm 1 tunetv 1 follow 1 dopop 1}
set wintvapp_idx 0
set wintvapp_path {}

# name of TV app which is configured for channel table
# set generic default here; actual name is set in C module
set tvapp_name "TV app."

# store the TV app name which is used in various dialogs
proc UpdateTvappName {} {
   global tvapp_name
   global wintvapp_idx
   global is_unix

   # note: in UNIX there is only one name, and it's set on C level
   if {!$is_unix} {
      if {$wintvapp_idx > 0} {
         set name_list [C_Tvapp_GetTvappList]
         set tvapp_name [lindex $name_list $wintvapp_idx]
      } else {
         set tvapp_name "TV app."
      }
   }
}

# callback invoked when a TV app attaches or detaches in the background
proc XawtvConfigShmAttach {} {
   global is_unix

   if {!$is_unix} {
      # check if the dialog is currently open
      if {[string length [info commands .xawtvcf.connect.lab_tvapp]] > 0} {
         XawtvConfigDisplayShmAttach
      }
   }
}

#=LOAD=XawtvConfigPopup
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Creates the Xawtv popup configuration dialog
##
proc XawtvConfigPopup {} {
   global xawtvcf_popup
   global xawtvcf wintvcf xawtv_tmpcf
   global tvapp_name wintvapp_idx wintvapp_path xawtv_tmp_tvapp_list
   global is_unix fileImage

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
      label .xawtvcf.all.lab_main -text "En-/Disable $tvapp_name interaction features:"
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
         frame .xawtvcf.all.poptype -borderwidth 2 -relief ridge
         radiobutton .xawtvcf.all.poptype.t0 -text "Separate popup" -variable xawtv_tmpcf(poptype) -value 0 -command XawtvConfigPopupSelected
         radiobutton .xawtvcf.all.poptype.t1 -text "Video overlay (subtitles)" -variable xawtv_tmpcf(poptype) -value 1 -command XawtvConfigPopupSelected
         radiobutton .xawtvcf.all.poptype.t2 -text "Video overlay, 2 lines" -variable xawtv_tmpcf(poptype) -value 2 -command XawtvConfigPopupSelected
         radiobutton .xawtvcf.all.poptype.t3 -text "Xawtv window title" -variable xawtv_tmpcf(poptype) -value 3 -command XawtvConfigPopupSelected
         pack .xawtvcf.all.poptype.t0 .xawtvcf.all.poptype.t1 .xawtvcf.all.poptype.t2 .xawtvcf.all.poptype.t3 -side top -anchor w
         pack .xawtvcf.all.poptype -side top -anchor w -fill x

         frame .xawtvcf.all.duration
         scale .xawtvcf.all.duration.s -from 0 -to 60 -orient horizontal -label "Popup duration \[seconds\]" \
                                   -variable xawtv_tmpcf(duration)
         pack .xawtvcf.all.duration.s -side left -fill x -expand 1
         pack .xawtvcf.all.duration -side top -fill x -expand 1 -pady 5
         pack .xawtvcf.all -side top -fill x

         # set widget states according to initial option settings
         XawtvConfigPopupSelected
      } else {
         pack .xawtvcf.all -side top -fill x

         frame  .xawtvcf.connect -borderwidth 1 -relief raised
         checkbutton  .xawtvcf.connect.lab_tvapp -text "" -disabledforeground black
         bindtags  .xawtvcf.connect.lab_tvapp {.connect .}
         pack   .xawtvcf.connect.lab_tvapp -side top -anchor w -pady 5 -padx 5
         pack   .xawtvcf.connect -side top -fill x
         # display the name of the currently connected TV app, if any
         XawtvConfigDisplayShmAttach
      }

      if {!$is_unix} {
         # load TV app config into temporary variables
         set xawtv_tmpcf(tvapp_idx) $wintvapp_idx
         set xawtv_tmpcf(tvapp_path) $wintvapp_path
         set xawtv_tmp_tvapp_list [C_Tvapp_GetTvappList]
         set xawtv_tmpcf(chk_tvapp_idx) $wintvapp_idx
         set xawtv_tmpcf(chk_tvapp_path) $wintvapp_path

         # create TV app selection popdown menu and "Test" command button
         frame  .xawtvcf.tvapp -borderwidth 1 -relief raised
         label  .xawtvcf.tvapp.lab -text "Specify from where to load the channel table:" -justify left
         pack   .xawtvcf.tvapp.lab -side top -anchor w -padx 5 -pady 5

         frame  .xawtvcf.tvapp.apptype
         label  .xawtvcf.tvapp.apptype.lab -text "TV application:"
         pack   .xawtvcf.tvapp.apptype.lab -side left -padx 5
         menubutton .xawtvcf.tvapp.apptype.mb -text [lindex $xawtv_tmp_tvapp_list $xawtv_tmpcf(tvapp_idx)] \
                         -justify center -relief raised -borderwidth 2 -menu .xawtvcf.tvapp.apptype.mb.men \
                         -indicatoron 1
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

         button .xawtvcf.tvapp.apptype.load -text "Test" \
                   -command {C_Tvapp_TestChanTab $xawtv_tmpcf(tvapp_idx) $xawtv_tmpcf(tvapp_path); \
                             set xawtv_tmpcf(chk_tvapp_idx) $xawtv_tmpcf(tvapp_idx); \
                             set xawtv_tmpcf(chk_tvapp_path) $xawtv_tmpcf(tvapp_path)}
         pack   .xawtvcf.tvapp.apptype.load -side right -padx 10 -fill x -expand 1
         pack   .xawtvcf.tvapp.apptype -side top -fill x

         # create entry field and command button to configure TV app directory
         frame  .xawtvcf.tvapp.name
         label  .xawtvcf.tvapp.name.prompt -text "Path:"
         pack   .xawtvcf.tvapp.name.prompt -side left -anchor w
         entry  .xawtvcf.tvapp.name.filename -textvariable xawtv_tmpcf(tvapp_path) -font {courier -12 normal} -width 33
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

         pack   .xawtvcf.tvapp -side top -anchor w -fill x

         # if "general enable" is off, disable the other buttons
         XawtvConfigGeneralEnable
      }

      frame .xawtvcf.cmd
      button .xawtvcf.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration) "TV application interaction"}
      button .xawtvcf.cmd.abort -text "Abort" -width 5 -command {array unset xawtv_tmpcf; destroy .xawtvcf}
      button .xawtvcf.cmd.save -text "Ok" -width 5 -command XawtvConfigSave -default active
      pack .xawtvcf.cmd.help .xawtvcf.cmd.abort .xawtvcf.cmd.save -side left -padx 10
      pack .xawtvcf.cmd -side top -pady 10

      bind .xawtvcf <Key-F1> {PopupHelp $helpIndex(Configuration) "TV application interaction"}
      bind .xawtvcf.cmd <Destroy> {+ set xawtvcf_popup 0}
      bind .xawtvcf.cmd.save <Return> {tkButtonInvoke %W}
      bind .xawtvcf.cmd.save <Escape> {tkButtonInvoke .xawtvcf.cmd.abort}
      focus .xawtvcf.cmd.save
   } else {
      raise .xawtvcf
   }
}

# callback for changes in popup options: adjust state of depending widgets
proc XawtvConfigPopupSelected {} {
   global xawtv_tmpcf

   if $xawtv_tmpcf(dopop) {
      set state "normal"
   } else {
      set state "disabled"
   }
   .xawtvcf.all.poptype.t0 configure -state $state
   .xawtvcf.all.poptype.t1 configure -state $state
   .xawtvcf.all.poptype.t2 configure -state $state
   .xawtvcf.all.poptype.t3 configure -state $state

   if {$xawtv_tmpcf(dopop) && ($xawtv_tmpcf(poptype) != 3)} {
      set state "normal"
      set fg black
   } else {
      set state "disabled"
      set fg [.xawtvcf.all.lab_main cget -disabledforeground]
   }
   .xawtvcf.all.duration.s configure -state $state -foreground $fg
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
   global is_unix

   if {!$is_unix} {
      # fetch the attached app's name from shared memory and display it
      set name [C_Tvapp_QueryTvapp]
      if {[string length $name] > 0} {
         .xawtvcf.connect.lab_tvapp configure -text "Connected to: $name" -selectcolor green
         .xawtvcf.connect.lab_tvapp invoke
      } else {
         # no app is currently connected
         .xawtvcf.connect.lab_tvapp configure -text "Connected to: not connected" -selectcolor red
         .xawtvcf.connect.lab_tvapp deselect
      }
   }
}

# callback for OK button: save config into variables
proc XawtvConfigSave {} {
   global xawtvcf wintvcf xawtv_tmpcf
   global wintvapp_idx wintvapp_path xawtv_tmp_tvapp_list
   global is_unix

   if {!$is_unix && ($xawtv_tmpcf(tvapp_idx) != 0)} {
      if {($xawtv_tmpcf(tvapp_idx) != $wintvapp_idx) && \
          ($xawtv_tmpcf(tvapp_idx) != $xawtv_tmpcf(chk_tvapp_idx))} {
         set answer [tk_messageBox -type okcancel -default cancel -icon warning -parent .xawtvcf \
                        -message "You have selected a TV app. but not tested if it's channel table can be loaded!"]
      } elseif {([string compare $xawtv_tmpcf(tvapp_path) $wintvapp_path] != 0) && \
                ([string compare $xawtv_tmpcf(tvapp_path) $xawtv_tmpcf(chk_tvapp_path)] != 0)} {
         set answer [tk_messageBox -type okcancel -default cancel -icon warning -parent .xawtvcf \
                        -message "You have changed the TV app. path but not tested if the channel table can be loaded!"]
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
                        "duration" $xawtv_tmpcf(duration)]
   } else {
      set wintvcf [list "shm" $xawtv_tmpcf(shm) \
                        "tunetv" $xawtv_tmpcf(tunetv) \
                        "follow" $xawtv_tmpcf(follow) \
                        "dopop" $xawtv_tmpcf(dopop)]
      set wintvapp_idx $xawtv_tmpcf(tvapp_idx)
      set wintvapp_path $xawtv_tmpcf(tvapp_path)
      unset xawtv_tmp_tvapp_list
   }
   array unset xawtv_tmpcf

   # save options
   UpdateRcFile
   # load config vars into the C module
   C_Tvapp_InitConfig
   # update the TV app name
   UpdateTvappName

   # close the dialog window
   destroy .xawtvcf
}

