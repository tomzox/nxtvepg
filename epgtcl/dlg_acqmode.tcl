#
#  Configuration dialogs for acquisition
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
#  Author: Tom Zoerner
#
#  $Id: dlg_acqmode.tcl,v 1.3 2002/12/08 19:59:00 tom Exp tom $
#
set acqmode_popup 0
set acq_mode "follow-ui"

set netacqcf_popup 0
set netacqcf {remctl 1 do_tcp_ip 0 host localhost ip {} port 7658 max_conn 10 sysloglev 3 fileloglev 0 logname {}}
set netacqcf_loglev_names {"no logging" error warning notice info}
set netacqcf_remctl_names {disabled "local only" "from anywhere"}
set netacqcf_view "both"
set netacq_enable 0

#=LOAD=PopupAcqMode
#=LOAD=PopupNetAcqConfig
#=DYNAMIC=

## ---------------------------------------------------------------------------
## Open popup for acquisition mode selection
## - can not be popped up if /dev/video is busy
##
proc PopupAcqMode {} {
   global acqmode_popup
   global acqmode_ailist acqmode_selist acqmode_names
   global acqmode_sel
   global acq_mode acq_mode_cnis
   global is_unix netacq_enable

   if {$acqmode_popup == 0} {

      if {[C_IsNetAcqActive default]} {
         # warn that params do not affect acquisition running remotely
         set answer [tk_messageBox -type okcancel -icon info -message "Please note that this dialog does not update acquisition parameters on server side."]
         if {[string compare $answer "ok"] != 0} {
            return
         }
      }

      CreateTransientPopup .acqmode "Acquisition mode selection"
      set acqmode_popup 1

      # load list of providers
      set acqmode_ailist {}
      foreach {cni name} [C_GetProvCnisAndNames] {
         lappend acqmode_ailist $cni
         set acqmode_names($cni) $name
      }
      set acqmode_ailist [SortProvList $acqmode_ailist]

      # initialize popup with current settings
      set acqmode_sel $acq_mode

      if {[info exists acq_mode_cnis] && ([llength $acq_mode_cnis] > 0)} {
         set acqmode_selist $acq_mode_cnis
         RemoveObsoleteCnisFromList acqmode_selist $acqmode_ailist
      } else {
         set acqmode_selist $acqmode_ailist
      }

      # checkbuttons for modes
      frame .acqmode.mode
      if $is_unix {
         radiobutton .acqmode.mode.mode0 -text "Passive (no input selection)" -variable acqmode_sel -value "passive" -command UpdateAcqModePopup
         pack .acqmode.mode.mode0 -side top -anchor w
      }
      radiobutton .acqmode.mode.mode1 -text "External (don't touch TV tuner)" -variable acqmode_sel -value "external" -command UpdateAcqModePopup
      radiobutton .acqmode.mode.mode2 -text "Follow browser database" -variable acqmode_sel -value "follow-ui" -command UpdateAcqModePopup
      radiobutton .acqmode.mode.mode3 -text "Manually selected" -variable acqmode_sel -value "cyclic_2" -command UpdateAcqModePopup
      radiobutton .acqmode.mode.mode4 -text "Cyclic: Now->Near->All" -variable acqmode_sel -value "cyclic_012" -command UpdateAcqModePopup
      radiobutton .acqmode.mode.mode5 -text "Cyclic: Now->All" -variable acqmode_sel -value "cyclic_02" -command UpdateAcqModePopup
      radiobutton .acqmode.mode.mode6 -text "Cyclic: Near->All" -variable acqmode_sel -value "cyclic_12" -command UpdateAcqModePopup
      pack .acqmode.mode.mode1 .acqmode.mode.mode2 .acqmode.mode.mode3 .acqmode.mode.mode4 .acqmode.mode.mode5 .acqmode.mode.mode6 -side top -anchor w
      pack .acqmode.mode -side top -pady 10 -padx 10 -anchor w

      # create two listboxes for provider database selection
      frame .acqmode.lb
      UpdateAcqModePopup
      pack .acqmode.lb -side top

      # command buttons at the bottom of the window
      frame .acqmode.cmd
      button .acqmode.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Acquisition modes)}
      button .acqmode.cmd.abort -text "Abort" -width 5 -command {destroy .acqmode}
      button .acqmode.cmd.ok -text "Ok" -width 5 -command {QuitAcqModePopup} -default active
      pack .acqmode.cmd.help .acqmode.cmd.abort .acqmode.cmd.ok -side left -padx 10
      pack .acqmode.cmd -side top -pady 10

      bind .acqmode <Key-F1> {PopupHelp $helpIndex(Acquisition modes)}
      bind .acqmode.cmd <Destroy> {+ set acqmode_popup 0}
      bind .acqmode.cmd.ok <Return> {tkButtonInvoke %W}
      bind .acqmode.cmd.ok <Escape> {tkButtonInvoke .acqmode.cmd.abort}
      focus .acqmode.cmd.ok
   } else {
      raise .acqmode
   }
}

# add or remove database selection popups after mode change
proc UpdateAcqModePopup {} {
   global acqmode_ailist acqmode_selist acqmode_names
   global acqmode_sel

   if {[string compare -length 6 "cyclic" $acqmode_sel] == 0} {
      # manual selection -> add listboxes to allow selection of multiple databases and priority
      if {[string length [info commands .acqmode.lb.ai.ailist]] == 0} {
         # listbox does not yet exist
         foreach widget [info commands .acqmode.lb.*] {
            destroy $widget
         }
         SelBoxCreate .acqmode.lb acqmode_ailist acqmode_selist acqmode_names
      }
   } else {
      foreach widget [info commands .acqmode.lb.*] {
         destroy $widget
      }
      # passive or ui mode -> remove listboxes
      # (the frame is added to trigger the shrink of the window around the remaining widgets)
      frame .acqmode.lb.foo
      pack .acqmode.lb.foo
   }
}

# extract, apply and save the settings
proc QuitAcqModePopup {} {
   global acqmode_ailist acqmode_selist acqmode_names
   global acqmode_sel
   global acq_mode acq_mode_cnis

   if {[string compare -length 6 "cyclic" $acqmode_sel] == 0} {
      if {[llength $acqmode_selist] == 0} {
         tk_messageBox -type ok -default ok -icon error -parent .acqmode -message "You have not selected any providers."
         return
      }
      set acq_mode_cnis $acqmode_selist
   }
   set acq_mode $acqmode_sel

   unset acqmode_ailist acqmode_selist acqmode_sel
   if {[info exists acqmode_names]} {unset acqmode_names}

   C_UpdateAcquisitionMode
   UpdateRcFile

   # close the popup window
   destroy .acqmode
}

##  --------------------------------------------------------------------------
##  Creates the remote acquisition configuration dialog
##
proc PopupNetAcqConfig {} {
   global netacqcf netacqcf_tmpcf
   global netacqcf_remctl_names
   global netacqcf_view
   global fileImage font_normal win_frm_fg
   global netacqcf_popup

   if {$netacqcf_popup == 0} {
      CreateTransientPopup .netacqcf "Client/Server Configuration"
      set netacqcf_popup 1

      # load configuration into temporary array
      foreach {opt val} $netacqcf {
         set netacqcf_tmpcf($opt) $val
      }

      frame .netacqcf.view -borderwidth 2 -relief ridge
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
                      -width 22 -justify center -relief raised -borderwidth 2 -menu .netacqcf.all.remote_mb.men \
                      -indicatoron 1 -takefocus 1 -highlightthickness 1 -highlightcolor $win_frm_fg
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
      button .netacqcf.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Configuration) "Client/Server"}
      button .netacqcf.cmd.abort -text "Abort" -width 5 -command {NetAcqConfigQuit 0}
      button .netacqcf.cmd.save -text "Ok" -width 5 -command {NetAcqConfigQuit 1} -default active
      pack .netacqcf.cmd.help .netacqcf.cmd.abort .netacqcf.cmd.save -side left -padx 10
      pack .netacqcf.cmd -side top -pady 5

      # key bindings
      bind .netacqcf.cmd <Destroy> {+ set netacqcf_popup 0}
      bind .netacqcf.cmd.save <Return> {tkButtonInvoke %W}
      bind .netacqcf.cmd.save <Escape> {tkButtonInvoke .netacqcf.cmd.abort}
      bind .netacqcf <Key-F1> {PopupHelp $helpIndex(Configuration) "Client/Server"}
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
   global netacqcf netacqcf_tmpcf

   if $do_save {
      # check config for consistancy
      if {[string length $netacqcf_tmpcf(host)] == 0} {
         # hostname must always be given (used only by client though)
         tk_messageBox -type ok -default ok -icon error -parent .netacqcf -message "You must enter a hostname, at least 'localhost'."
         return
      }
      if {[string compare $netacqcf_tmpcf(host) "localhost"] != 0} {
         # non-local host -> TCP/IP must be enabled
         if {$netacqcf_tmpcf(do_tcp_ip) == 0} {
            tk_messageBox -type ok -default ok -icon error -parent .netacqcf -message "Cannot use any host but 'localhost' as server when TCP/IP is not enabled."
            return
         }
      }
      if {$netacqcf_tmpcf(do_tcp_ip) && ([string length $netacqcf_tmpcf(port)] == 0)} {
         # TCP/IP enabled -> must give a port name (even if unused by this client b/c server is localhost)
         tk_messageBox -type ok -default ok -icon error -parent .netacqcf -message "You must enter either a TCP port number or a service name."
         return
      }
      if {($netacqcf_tmpcf(fileloglev) > 0) && ([string length $netacqcf_tmpcf(logname)] == 0)} {
         # when logging to a file is enabled we obviously require a file name
         tk_messageBox -type ok -default ok -icon error -parent .netacqcf -message "If you enable logging into a file, you must enter a file name."
         return
      }

      # copy temporary variables back into config parameter list
      set netacqcf [list "remctl" $netacqcf_tmpcf(remctl) \
                         "do_tcp_ip" $netacqcf_tmpcf(do_tcp_ip) \
                         "host" $netacqcf_tmpcf(host) \
                         "port" $netacqcf_tmpcf(port) \
                         "ip" $netacqcf_tmpcf(ip) \
                         "logname" $netacqcf_tmpcf(logname) \
                         "max_conn" $netacqcf_tmpcf(max_conn) \
                         "fileloglev" $netacqcf_tmpcf(fileloglev) \
                         "sysloglev" $netacqcf_tmpcf(sysloglev)]
      UpdateRcFile
      C_UpdateNetAcqConfig
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
   global win_frm_fg
   global netacqcf_loglev_names
   global netacqcf_tmpcf

   # build callback command for menubutton: set button label to label of selected entry
   # (use "append" b/c some vars need to be evaluated now, others at time of invocation)
   set cmd {}
   append cmd $wmen { configure -text [lindex $netacqcf_loglev_names $netacqcf_tmpcf(} $logtype {)]}

   menubutton $wmen -text [lindex $netacqcf_loglev_names $netacqcf_tmpcf($logtype)] \
                   -width 22 -justify center -relief raised -borderwidth 2 -menu $wmen.men \
                   -indicatoron 1 -takefocus 1 -highlightthickness 1 -highlightcolor $win_frm_fg
   menu  $wmen.men -tearoff 0
   set idx 0
   foreach name $netacqcf_loglev_names {
      $wmen.men add radiobutton -label $name -variable netacqcf_tmpcf($logtype) -value $idx -command $cmd
      incr idx
   }
}

