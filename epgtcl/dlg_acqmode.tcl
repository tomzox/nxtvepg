#
#  Configuration dialogs for acquisition
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
#    Implements configuration dialogs for acquisition.
#
set netacqcf_popup 0
set ttxgrab_popup 0

set netacqcf_loglev_names {"no logging" error warning notice info}
set netacqcf_remctl_names {disabled "local only" "from anywhere"}
set netacqcf_view "both"

# Used for return values of proc C_GetAcqConfig
#=CONST= ::acqcf_ret_mode_idx 0
#=CONST= ::acqcf_ret_start_idx 1

#=LOAD=PopupNetAcqConfig
#=LOAD=PopupTtxGrab
#=LOAD=ProvLoadTeletext
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Creates the remote acquisition configuration dialog
##
proc PopupNetAcqConfig {} {
   global netacqcf_tmpcf
   global netacqcf_remctl_names
   global netacqcf_view
   global fileImage font_normal
   global netacqcf_popup

   if {$netacqcf_popup == 0} {
      CreateTransientPopup .netacqcf "Client/Server Configuration"
      set netacqcf_popup 1

      # load configuration into temporary array
      C_GetNetAcqConfig netacqcf_tmpcf

      frame .netacqcf.view -borderwidth 2 -relief groove
      label .netacqcf.view.view_lab -text "Enable settings for:"
      pack  .netacqcf.view.view_lab -side left -pady 5
      radiobutton .netacqcf.view.clnt_radio -text "client" -variable netacqcf_view -value "client" -command NetAcqChangeView
      radiobutton .netacqcf.view.serv_radio -text "server" -variable netacqcf_view -value "server" -command NetAcqChangeView
      radiobutton .netacqcf.view.both_radio -text "both"   -variable netacqcf_view -value "both" -command NetAcqChangeView
      pack  .netacqcf.view.clnt_radio .netacqcf.view.serv_radio .netacqcf.view.both_radio -side left
      pack  .netacqcf.view -side top -pady 10 -padx 10 -fill x

      set gridrow 0
      frame .netacqcf.all
      label .netacqcf.all.remote_lab -text "Enable remote control:"
      grid  .netacqcf.all.remote_lab -row $gridrow -column 0 -sticky w -padx 5
      menubutton .netacqcf.all.remote_mb -text [lindex $netacqcf_remctl_names $netacqcf_tmpcf(remctl)] \
                      -menu .netacqcf.all.remote_mb.men \
                      -width 22 -justify center -indicatoron 1
      config_menubutton .netacqcf.all.remote_mb
      menu  .netacqcf.all.remote_mb.men -tearoff 0
      set cmd {.netacqcf.all.remote_mb configure -text [lindex $netacqcf_remctl_names $netacqcf_tmpcf(remctl)]}
      set idx 0
      foreach name $netacqcf_remctl_names {
         .netacqcf.all.remote_mb.men add radiobutton -label $name -variable netacqcf_tmpcf(remctl) -value $idx -command $cmd
         incr idx
      }
      grid  .netacqcf.all.remote_mb -row $gridrow -column 1 -sticky we
      incr gridrow
      label .netacqcf.all.tcpip_lab -text "Enable TCP/IP:"
      grid  .netacqcf.all.tcpip_lab -row $gridrow -column 0 -sticky w -padx 5
      checkbutton .netacqcf.all.tcpip_but -text "(depreciated w/o firewall)" -variable netacqcf_tmpcf(do_tcp_ip) -font $font_normal
      grid  .netacqcf.all.tcpip_but -row $gridrow -column 1 -sticky w
      incr gridrow
      label .netacqcf.all.host_lab -text "Server hostname:"
      grid  .netacqcf.all.host_lab -row $gridrow -column 0 -sticky w -padx 5
      entry .netacqcf.all.host_ent -textvariable netacqcf_tmpcf(host) -width 25
      grid  .netacqcf.all.host_ent -row $gridrow -column 1 -sticky we
      incr gridrow
      label .netacqcf.all.port_lab -text "Server TCP port:"
      grid  .netacqcf.all.port_lab -row $gridrow -column 0 -sticky w -padx 5
      entry .netacqcf.all.port_ent -textvariable netacqcf_tmpcf(port) -width 25
      grid  .netacqcf.all.port_ent -row $gridrow -column 1 -sticky we
      incr gridrow
      label .netacqcf.all.ip_lab -text "Bind IP address:"
      grid  .netacqcf.all.ip_lab -row $gridrow -column 0 -sticky w -padx 5
      entry .netacqcf.all.ip_ent -textvariable netacqcf_tmpcf(ip) -width 25
      grid  .netacqcf.all.ip_ent -row $gridrow -column 1 -sticky we
      incr gridrow
      label .netacqcf.all.max_conn_lab -text "Max. connections:"
      grid  .netacqcf.all.max_conn_lab -row $gridrow -column 0 -sticky w -padx 5
      entry .netacqcf.all.max_conn_ent -textvariable netacqcf_tmpcf(max_conn) -width 25
      grid  .netacqcf.all.max_conn_ent -row $gridrow -column 1 -sticky we
      incr gridrow
      label .netacqcf.all.logname_lab -text "Log filename:"
      grid  .netacqcf.all.logname_lab -row $gridrow -column 0 -sticky w -padx 5
      frame .netacqcf.all.logname_frm
      entry .netacqcf.all.logname_frm.ent -textvariable netacqcf_tmpcf(logname) -width 21
      button .netacqcf.all.logname_frm.dlgbut -image $fileImage -command {
         set tmp [tk_getSaveFile -parent .netacqcf \
                     -initialfile [file tail $netacqcf_tmpcf(logname)] \
                     -initialdir [file dirname $netacqcf_tmpcf(logname)]]
         if {[string length $tmp] > 0} {
            set netacqcf_tmpcf(logname) $tmp
         }
         unset tmp
      }
      pack  .netacqcf.all.logname_frm.ent -side left -fill x -expand 1
      pack  .netacqcf.all.logname_frm.dlgbut -side left
      grid  .netacqcf.all.logname_frm -row $gridrow -column 1 -sticky we
      incr gridrow
      label .netacqcf.all.loglev_lab -text "File min. log level:"
      grid  .netacqcf.all.loglev_lab -row $gridrow -column 0 -sticky w -padx 5
      NetAcqCreateLogLevMenu .netacqcf.all.loglev_men fileloglev
      grid  .netacqcf.all.loglev_men -row $gridrow -column 1 -sticky we
      incr gridrow
      label .netacqcf.all.syslev_lab -text "Syslog min. level:"
      grid  .netacqcf.all.syslev_lab -row $gridrow -column 0 -sticky w -padx 5
      NetAcqCreateLogLevMenu .netacqcf.all.syslev_men sysloglev
      grid  .netacqcf.all.syslev_men -row $gridrow -column 1 -sticky we
      incr gridrow
      grid  columnconfigure .netacqcf.all 1 -weight 1
      pack  .netacqcf.all -side top -padx 10 -pady 10 -fill both -expand 1

      # standard dialog buttons: Ok, Abort, Help
      frame .netacqcf.cmd
      button .netacqcf.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configure menu) "Client/Server"}
      button .netacqcf.cmd.abort -text "Abort" -width 5 -command {NetAcqConfigQuit 0}
      button .netacqcf.cmd.save -text "Ok" -width 5 -command {NetAcqConfigQuit 1} -default active
      pack .netacqcf.cmd.help .netacqcf.cmd.abort .netacqcf.cmd.save -side left -padx 10
      pack .netacqcf.cmd -side top -pady 5

      # key bindings
      wm protocol .netacqcf WM_DELETE_WINDOW {NetAcqConfigQuit 0}
      bind .netacqcf.cmd <Destroy> {+ set netacqcf_popup 0}
      bind .netacqcf.cmd.save <Return> {tkButtonInvoke %W}
      bind .netacqcf.cmd.save <Escape> {tkButtonInvoke .netacqcf.cmd.abort}
      bind .netacqcf <Key-F1> {PopupHelp $helpIndex(Configure menu) "Client/Server"}
      focus .netacqcf.cmd.save

      NetAcqChangeView

      wm resizable .netacqcf 1 0
      update
      wm minsize .netacqcf [winfo reqwidth .netacqcf] [winfo reqheight .netacqcf]

   } else {
      raise .netacqcf
   }
}

# callback for "Abort" and "OK" buttons
proc NetAcqConfigQuit {do_save} {
   global netacqcf_tmpcf

   if $do_save {
      # check config for consistancy
      if {[string length $netacqcf_tmpcf(host)] == 0} {
         # hostname must always be given (used only by client though)
         tk_messageBox -type ok -default ok -icon error -parent .netacqcf \
                       -message "You must enter a hostname, at least 'localhost'."
         return
      }
      if {[string compare $netacqcf_tmpcf(host) "localhost"] != 0} {
         # non-local host -> TCP/IP must be enabled
         if {$netacqcf_tmpcf(do_tcp_ip) == 0} {
            tk_messageBox -type ok -default ok -icon error -parent .netacqcf \
                          -message "Cannot use any host but 'localhost' as server when TCP/IP is not enabled."
            return
         }
      }
      if {$netacqcf_tmpcf(do_tcp_ip) && ([string length $netacqcf_tmpcf(port)] == 0)} {
         # TCP/IP enabled -> must give a port name (even if unused by this client b/c server is localhost)
         tk_messageBox -type ok -default ok -icon error -parent .netacqcf \
                       -message "You must enter either a TCP port number or a service name."
         return
      }
      if {$netacqcf_tmpcf(fileloglev) > 0} {
         # when logging to a file is enabled we obviously require a file name
         if {[string length $netacqcf_tmpcf(logname)] == 0} {
            tk_messageBox -type ok -default ok -icon error -parent .netacqcf \
                          -message "If you enable logging into a file, you must enter a file name."
            return
         }
         if {![file isdirectory [file dirname $netacqcf_tmpcf(logname)]]} {
            tk_messageBox -type ok -default ok -icon error -parent .netacqcf \
                          -message "The path given for the log file isn't a directory or doesn't exist."
            return
         }
      }

      # copy temporary variables back into config parameter list
      C_UpdateNetAcqConfig [array get netacqcf_tmpcf]
   }
   # close the dialog window
   destroy .netacqcf
   # free memory allocated for temporary array
   array unset netacqcf_tmpcf
}

# callback for "View" radio buttons
proc NetAcqChangeView {} {
   global is_unix tcl_platform
   global netacqcf_view

   set view_dst [list \
      .netacqcf.all.remote_lab 1 \
      .netacqcf.all.remote_mb 1 \
      .netacqcf.all.tcpip_lab 1 \
      .netacqcf.all.tcpip_but 1 \
      .netacqcf.all.host_lab 2 \
      .netacqcf.all.host_ent 2 \
      .netacqcf.all.port_lab 3 \
      .netacqcf.all.port_ent 3 \
      .netacqcf.all.ip_lab 1 \
      .netacqcf.all.ip_ent 1 \
      .netacqcf.all.max_conn_lab 1 \
      .netacqcf.all.max_conn_ent 1 \
      .netacqcf.all.logname_lab 1 \
      .netacqcf.all.logname_frm.ent 1 \
      .netacqcf.all.logname_frm.dlgbut 1 \
      .netacqcf.all.loglev_lab 1 \
      .netacqcf.all.loglev_men 1 \
      .netacqcf.all.syslev_lab 1 \
      .netacqcf.all.syslev_men 1 \
   ]

   if {[string compare $netacqcf_view "server"] == 0} {
      set mask 1
   } elseif {[string compare $netacqcf_view "client"] == 0} {
      set mask 2
   } else {
      set mask 3
   }

   set enable_syslog [expr $is_unix || ([string compare $tcl_platform(os) "Windows 95"] != 0)]

   foreach {widg val} $view_dst {
      # disable syslog settings on platforms that don't support it
      if {!$enable_syslog && [string match "*syslev*" $widg]} {
         set val 0
      }

      if {$val & $mask} {
         $widg configure -state normal
      } else {
         $widg configure -state disabled
      }
   }
}

# create popdown menu to allow choosing log levels (for file logging and syslog)
proc NetAcqCreateLogLevMenu {wmen logtype} {
   global netacqcf_loglev_names
   global netacqcf_tmpcf

   # build callback command for menubutton: set button label to label of selected entry
   # (use "append" b/c some vars need to be evaluated now, others at time of invocation)
   set cmd {}
   append cmd $wmen { configure -text [lindex $netacqcf_loglev_names $netacqcf_tmpcf(} $logtype {)]}

   menubutton $wmen -text [lindex $netacqcf_loglev_names $netacqcf_tmpcf($logtype)] \
                   -width 22 -justify center -relief raised -borderwidth 2 -menu $wmen.men \
                   -indicatoron 1
   config_menubutton $wmen
   menu  $wmen.men -tearoff 0
   set idx 0
   foreach name $netacqcf_loglev_names {
      $wmen.men add radiobutton -label $name -variable netacqcf_tmpcf($logtype) -value $idx -command $cmd
      incr idx
   }
}

#=IF=defined(USE_TTX_GRABBER)
## ---------------------------------------------------------------------------
## Open dialog for teletext grabber configuration
##
proc PopupTtxGrab {} {
   global ttxgrab_tmpcf
   global font_normal font_fixed
   global ttxgrab_popup
   global acqmode_start
   global acqmode_sel

   if {$ttxgrab_popup == 0} {

      # initialize popup with current settings
      C_GetTtxConfig ttxgrab_tmpcf

      CreateTransientPopup .ttxgrab "Teletext grabber configuration"
      set ttxgrab_popup 1

      # convert page numbers to decimal
      set ttxgrab_tmpcf(ovpg) [TtxGrab_Hex2Dec $ttxgrab_tmpcf(ovpg)]
      set ttxgrab_tmpcf(pg_start) [TtxGrab_Hex2Dec $ttxgrab_tmpcf(pg_start)]
      set ttxgrab_tmpcf(pg_end) [TtxGrab_Hex2Dec $ttxgrab_tmpcf(pg_end)]

      # initialize current acq mode settings
      set tmpl [C_GetAcqConfig]
      set acqmode_sel [lindex $tmpl $::acqcf_ret_mode_idx]
      set acqmode_start [lindex $tmpl $::acqcf_ret_start_idx]

      checkbutton .ttxgrab.ena_chk -text "Enable teletext grabber" -variable ttxgrab_tmpcf(enable) \
                                   -command TtxGrab_Enabled
      pack  .ttxgrab.ena_chk -side top -padx 10 -pady 5 -anchor w

      # check-button to disable automatic acq-start upon program start (note "auto enabled" = 0)
      checkbutton .ttxgrab.ena_auto -text "Start acquisition automatically" -variable acqmode_start -onvalue 0 -offvalue 1
      pack .ttxgrab.ena_auto -side top -padx 10 -pady 0 -anchor w

      labelframe .ttxgrab.all -text "Teletext options"
      set gridrow 0
      label .ttxgrab.all.chcnt_lab -text "Number of TV channels to grab from:"
      grid  .ttxgrab.all.chcnt_lab -row $gridrow -column 0 -sticky w -padx 5
      entry .ttxgrab.all.chcnt_ent -textvariable ttxgrab_tmpcf(net_count) -width 4
      grid  .ttxgrab.all.chcnt_ent -row $gridrow -column 1 -sticky we
      incr gridrow
      label .ttxgrab.all.ovpg_lab -text "Overview start page:"
      #grid  .ttxgrab.all.ovpg_lab -row $gridrow -column 0 -sticky w -padx 5
      entry .ttxgrab.all.ovpg_ent -textvariable ttxgrab_tmpcf(ovpg) -width 4
      #grid  .ttxgrab.all.ovpg_ent -row $gridrow -column 1 -sticky we
      incr gridrow
      label .ttxgrab.all.pgrange_lab -text "Capture page range:"
      grid  .ttxgrab.all.pgrange_lab -row $gridrow -column 0 -sticky w -padx 5
      entry .ttxgrab.all.pgstart_ent -textvariable ttxgrab_tmpcf(pg_start) -width 4
      grid  .ttxgrab.all.pgstart_ent -row $gridrow -column 1 -sticky we
      label .ttxgrab.all.pgrange_lab2 -text "-"
      grid  .ttxgrab.all.pgrange_lab2 -row $gridrow -column 2 -sticky w -padx 5
      entry .ttxgrab.all.pgend_ent -textvariable ttxgrab_tmpcf(pg_end) -width 4
      grid  .ttxgrab.all.pgend_ent -row $gridrow -column 3 -sticky we
      incr gridrow
      label .ttxgrab.all.dur_lab -text "Capture duration:"
      grid  .ttxgrab.all.dur_lab -row $gridrow -column 0 -sticky w -padx 5
      entry .ttxgrab.all.dur_ent -textvariable ttxgrab_tmpcf(duration) -width 4
      grid  .ttxgrab.all.dur_ent -row $gridrow -column 1 -sticky we
      label .ttxgrab.all.dur_lab2 -text {[seconds]} -font $font_normal
      grid  .ttxgrab.all.dur_lab2 -row $gridrow -column 2 -columnspan 2 -sticky w -padx 5
      incr gridrow
      label .ttxgrab.all.exp_lab -text "Keep expired programmes:"
      grid  .ttxgrab.all.exp_lab -row $gridrow -column 0 -sticky w -padx 5
      entry .ttxgrab.all.exp_ent -textvariable ttxgrab_tmpcf(pi_expire) -width 4
      grid  .ttxgrab.all.exp_ent -row $gridrow -column 1 -sticky we
      label .ttxgrab.all.exp_lab2 -text {[hours]} -font $font_normal
      grid  .ttxgrab.all.exp_lab2 -row $gridrow -column 2 -columnspan 2 -sticky w -padx 5
      incr gridrow
      pack  .ttxgrab.all -side top -pady 5 -padx 10 -fill x -expand 1

      # Channel table selection
      labelframe .ttxgrab.tvapp -text "TV channel table"
      TvAppConfigWid_Create .ttxgrab.tvapp 0
      pack  .ttxgrab.tvapp -side top -pady 5 -padx 10 -fill x -expand 1

      # checkbuttons for acquisition modes
      labelframe .ttxgrab.mode -text "Acquisition mode"
      #label .ttxgrab.mode.info1 -text "If you have more than one EPG provider, you can\nselect here in which order their data is acquired:" -justify left
      #pack  .ttxgrab.mode.info1 -side top -pady 5 -padx 10 -anchor w
      radiobutton .ttxgrab.mode.mode1 -text "Cyclic: Full" -variable acqmode_sel -value "cyclic_2"
      radiobutton .ttxgrab.mode.mode2 -text "Cyclic: Now->Full" -variable acqmode_sel -value "cyclic_02"
      radiobutton .ttxgrab.mode.mode0 -text "Passive (no tuning or input selection)" -variable acqmode_sel -value "passive"
      pack .ttxgrab.mode.mode1 .ttxgrab.mode.mode2 .ttxgrab.mode.mode0 -side top -anchor w
      pack .ttxgrab.mode -side top -padx 10 -pady 5 -fill x -expand 1

      checkbutton .ttxgrab.keep_ttx -text "Keep teletext raw capture data (for debugging)" -variable ttxgrab_tmpcf(keep_ttx)
      pack  .ttxgrab.keep_ttx -side top -padx 10 -pady 5 -anchor w

      # command buttons at the bottom of the window
      frame .ttxgrab.cmd
      button .ttxgrab.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configure menu) "Teletext grabber"}
      button .ttxgrab.cmd.abort -text "Abort" -width 5 -command {destroy .ttxgrab}
      button .ttxgrab.cmd.ok -text "Ok" -width 5 -command {QuitTtxGrabPopup} -default active
      pack .ttxgrab.cmd.help .ttxgrab.cmd.abort .ttxgrab.cmd.ok -side left -padx 10
      pack .ttxgrab.cmd -side top -pady 10

      bind .ttxgrab <Key-F1> {PopupHelp $helpIndex(Configure menu) "Teletext grabber"}
      bind .ttxgrab.cmd <Destroy> {+ set ttxgrab_popup 0}
      bind .ttxgrab.cmd.ok <Return> {tkButtonInvoke %W}
      bind .ttxgrab.cmd.ok <Escape> {tkButtonInvoke .ttxgrab.cmd.abort}
      focus .ttxgrab.cmd.ok

      # set initial widget state
      TtxGrab_Enabled
   } else {
      raise .ttxgrab
   }
}

#X#   # create two listboxes for provider database selection
#X#   frame .acqmode.lb
#X#   UpdateAcqModePopup
#X#   pack .acqmode.lb -side top
#X#
#X## add or remove database selection popups after mode change
#X#proc UpdateAcqModePopup {} {
#X#   global acqmode_ailist acqmode_names
#X#   global acqmode_sel
#X#
#X#   if {[string compare -length 6 "cyclic" $acqmode_sel] == 0} {
#X#      # manual selection -> add listboxes to allow selection of multiple databases and priority
#X#      if {[string length [info commands .acqmode.lb.ai.ailist]] == 0} {
#X#         # listbox does not yet exist
#X#         foreach widget [info commands .acqmode.lb.*] {
#X#            destroy $widget
#X#         }
#X#         SelBoxCreate .acqmode.lb acqmode_ailist acqmode_names \
#X#                      "Available:" "Selected:"
#X#      }
#X#   } else {
#X#      foreach widget [info commands .acqmode.lb.*] {
#X#         destroy $widget
#X#      }
#X#      # passive or ui mode -> remove listboxes
#X#      # (the frame is added to trigger the shrink of the window around the remaining widgets)
#X#      frame .acqmode.lb.foo
#X#      pack .acqmode.lb.foo
#X#   }
#X#}


# callback for "enable" button
proc TtxGrab_Enabled {} {
   global ttxgrab_tmpcf

   set wlist [list .ttxgrab.ena_auto \
                   .ttxgrab.all.chcnt_ent \
                   .ttxgrab.all.ovpg_ent \
                   .ttxgrab.all.pgstart_ent \
                   .ttxgrab.all.pgend_ent \
                   .ttxgrab.all.dur_ent \
                   .ttxgrab.all.exp_ent \
                   .ttxgrab.mode.mode1 \
                   .ttxgrab.mode.mode2 \
                   .ttxgrab.mode.mode0 \
                   .ttxgrab.keep_ttx]

   if $ttxgrab_tmpcf(enable) {
      foreach wid $wlist {
         $wid configure -state normal
      }
   } else {
      foreach wid $wlist {
         $wid configure -state disabled
      }
   }
}

# helper function to convert page numbers to decimal for display
proc TtxGrab_Hex2Dec {hex} {
   if {$hex < 0x100} {incr hex 0x800}
   set dec [expr ($hex & 0x00F) + \
                 10*(($hex & 0x0F0)>>4) + \
                 100*(($hex & 0xF00)>>8)]
   return $dec;
}

# helper function to convert page numbers to hexadecimal
proc TtxGrab_Dec2Hex {dec} {
   if {$dec > 800} {incr dec -800}
   set hex [expr ($dec % 10) + \
                 16*(($dec/10) % 10) + \
                 256*(($dec/100) % 10)]
   return $hex;
}

proc TtxGrab_PgNoCheck {dec item} {
   if {([regexp {^[1-9][0-9][0-9]$} $dec]) &&
       (($dec >= 100) && ($dec <= 899))} {
      # OK
      set result 1
   } else {
      tk_messageBox -type ok -default ok -icon error -parent .ttxgrab \
                    -message "Invalid $item number: must be a (decimal) number in range 100 to 899"
      set result 0
   }
   return $result
}

proc TtxGrab_IntCheck {int low item} {
   if {([regexp {^[0-9]+$} $int]) &&
       ($int >= $low)} {
      # OK
      set result 1
   } else {
      tk_messageBox -type ok -default ok -icon error -parent .ttxgrab \
                    -message "Invalid $item value: must be a number larger or equal $low"
      set result 0
   }
   return $result
}

# save & apply the settings
proc QuitTtxGrabPopup {} {
   global ttxgrab_tmpcf
   global acqmode_start acqmode_sel

   if {$ttxgrab_tmpcf(enable)} {
      if { [TtxGrab_PgNoCheck $ttxgrab_tmpcf(ovpg) "overview page"] &&
           [TtxGrab_PgNoCheck $ttxgrab_tmpcf(pg_start) "page range start"] &&
           [TtxGrab_PgNoCheck $ttxgrab_tmpcf(pg_end) "page range end"] &&
           [TtxGrab_IntCheck $ttxgrab_tmpcf(net_count) 1 "channel count"] &&
           [TtxGrab_IntCheck $ttxgrab_tmpcf(duration) 1 "duration"] &&
           [TtxGrab_IntCheck $ttxgrab_tmpcf(duration) 1 "pi_expire"]} {

         if {$ttxgrab_tmpcf(pg_start) <= $ttxgrab_tmpcf(pg_end)} {
            # check ov page is inside capture range
            #if {($ttxgrab_tmpcf(ovpg) >= $ttxgrab_tmpcf(pg_start)) && \
            #    ($ttxgrab_tmpcf(ovpg) <= $ttxgrab_tmpcf(pg_end))} {

            #} else {
            #   tk_messageBox -type ok -default ok -icon error -parent .ttxgrab \
            #                 -message "Overview page number must lie inside capture page range"
            #   return
            #}

            # check if TV app is configured
            set tv_app_cfg [TvAppConfigWid_GetResults .ttxgrab]
            set test_result [C_Tvapp_TestChanTab [lindex $tv_app_cfg 0] [lindex $tv_app_cfg 1]]
            set chn_count [lindex $test_result 0]
            set err_msg [lindex $test_result 1]

            if {$chn_count <= 0} {
               tk_messageBox -type ok -icon error -parent .ttxgrab \
                             -message [concat "A TV channel table is needed by the Teletext Grabber - " $err_msg]
               return
            } elseif {$chn_count < $ttxgrab_tmpcf(net_count)} {
               set answer [tk_messageBox -type okcancel -default cancel -icon warning -parent .ttxgrab \
                              -message "The selected TV application's channel table has only $chn_count channels which is less than selected for Teletext."]
               if {$answer eq "cancel"} return
            }

            if {![C_CheckTvCardConfig]} {
               set answer [tk_messageBox -type okcancel -default ok -icon info -parent .ttxgrab \
                             -message "Before you can start loading Teletext EPG data, you need to configure your TV card type in the following dialog."]
               if {$answer eq "ok"} {
                  # open the TV card configuration dialog; auto-enable acq after dialog is closed
                  # Note 1 == ::tvcf_acq_disa_drv
                  set ::hwcf_acq_reenable 1
                  PopupHardwareConfig
               }
               return
            }

            # convert page numbers back to hexadecimal
            # ATTN: This has to be done last and followed by destruction of dialog
            set ttxgrab_tmpcf(ovpg) [TtxGrab_Dec2Hex $ttxgrab_tmpcf(ovpg)]
            set ttxgrab_tmpcf(pg_start) [TtxGrab_Dec2Hex $ttxgrab_tmpcf(pg_start)]
            set ttxgrab_tmpcf(pg_end) [TtxGrab_Dec2Hex $ttxgrab_tmpcf(pg_end)]

            # pass params to C level
            C_Tvapp_UpdateConfig [lindex $tv_app_cfg 0] [lindex $tv_app_cfg 1]
            C_UpdateTtxConfig $acqmode_sel $acqmode_start [array get ttxgrab_tmpcf]

         } else {
            tk_messageBox -type ok -default ok -icon error -parent .ttxgrab \
                          -message "Invalid capture range: end must be larger than start page"
            return
         }

      } else {
         # error already reported above
         return
      }
   } else {
      # ignore all changes in the paramete fields except for "enable" flag
      C_UpdateTtxConfig $acqmode_sel $acqmode_start [list "enable" 0]
   }

   if {[C_IsNetAcqActive default]} {
      set tmpl [C_GetAcqConfig]
      set old_acqmode_sel [lindex $tmpl $::acqcf_ret_mode_idx]
      if {$old_acqmode_sel ne $acqmode_sel} {
         # warn that params do not affect acquisition running remotely
         tk_messageBox -type ok -icon info -message "Please note that this does not update the acquisition mode on server side."
      }
   }

   # free memory
   array unset ttxgrab_tmpcf
   unset acqmode_sel acqmode_start

   # close the popup window
   destroy .ttxgrab
}

proc ProvLoadTeletext {} {
   C_GetTtxConfig ttxgrab_tmpcf

   if {$ttxgrab_tmpcf(enable)} {
      C_MergeTtxProviders
   } else {
      set answer [tk_messageBox -type okcancel -default ok -icon question -parent . \
                      -message "Teletext EPG acquisition is currently not enabled. Configure it now?"]
      if {$answer eq "ok"} {
         PopupTtxGrab
      }
   }
}

#=ENDIF=USE_TTX_GRABBER

