#
#  RC/INI file handling
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
#    Implements reading and writing the rc (UNIX) or INI (Windows) file.
#
#  Author: Tom Zoerner
#
#  $Id: rcfile.tcl,v 1.6 2002/11/30 21:56:05 tom Exp tom $
#
set myrcfile ""

proc LoadRcFile {filename isDefault} {
   global shortcuts shortcut_order
   global prov_selection prov_freqs cfnetwops cfnetnames cfnettimes
   global showNetwopListbox showNetwopListboxLeft showShortcutListbox
   global showStatusLine showColumnHeader
   global hideOnMinimize menuUserLanguage help_winsize
   global prov_merge_cnis prov_merge_cf
   global acq_mode acq_mode_cnis netacq_enable
   global pibox_height pilistbox_cols shortinfo_height
   global hwcfg netacqcf xawtvcf wintvcf ctxmencf ctxmencf_wintv_vcr
   global wintvapp_idx wintvapp_path hwcfg_tvapp_idx hwcfg_tvapp_path
   global substr_history
   global dumpdb_filename
   global dumpdb_pi dumpdb_xi dumpdb_ai dumpdb_ni dumpdb_oi dumpdb_mi dumpdb_li dumpdb_ti
   global dumptabs dumptabs_filename
   global dumphtml_filename dumphtml_file_append dumphtml_file_overwrite
   global dumphtml_sel dumphtml_sel_count dumphtml_type
   global dumphtml_hyperlinks dumphtml_use_colsel dumphtml_colsel
   global dumpxml_filename
   global colsel_tabs usercols
   global EPG_VERSION_NO is_unix
   global myrcfile

   set myrcfile $filename
   set shortcut_order {}
   set error 0
   set line_no 0

   if {[catch {set rcfile [open $filename "r"]} errmsg] == 0} {
      while {[gets $rcfile line] >= 0} {
         incr line_no
         if {([catch $line] != 0) && !$error} {
            tk_messageBox -type ok -default ok -icon error -message "Syntax error in rc/ini file, line #$line_no: $line"
            set error 1
         }
      }
      close $rcfile

      # for backwards compatibility append freq table index to hw cfg
      if {[info exists hwcfg] && ([llength $hwcfg] == 5)} {lappend hwcfg 0}

      if {![info exists nxtvepg_version] || ($nxtvepg_version < 0x000600)} {
         set substr_history {}
         set tag [clock seconds]
         for {set index 0} {$index < $shortcut_count} {incr index} {
            # starting with version 0.5.0 an ID tag was appended
            if {[llength $shortcuts($index)] == 4} {
               lappend shortcuts($index) $tag
               incr tag
            }
            # since version 0.6.0 text search has an additional parameter "match full" and "ignore case" was inverted to "match case"
            set filt [lindex $shortcuts($index) 2]
            set newfilt {}
            foreach {tag val} $filt {
               if {([string compare $tag "substr"] == 0) && ([llength $val] == 4)} {
                  set val [list [lindex $val 0] [lindex $val 1] [expr 1 - [lindex $val 2]] 0 [lindex $val 3]]
               }
               lappend newfilt $tag $val
            }
            set shortcuts($index) [list \
               [lindex $shortcuts($index) 0] \
               [lindex $shortcuts($index) 1] \
               $newfilt \
               [lindex $shortcuts($index) 3] \
               [lindex $shortcuts($index) 4] ]
         }
      }
      if {![info exists nxtvepg_version] || ($nxtvepg_version < 0x020400)} {
         # starting with version 2.4.0 the invert element was added
         # and the tag is used as index
         array set old_sc [array get shortcuts]
         set shortcut_order {}
         unset shortcuts
         for {set index 0} {$index < $shortcut_count} {incr index} {
            if {[info exists old_sc($index)] && ([llength $old_sc($index)] == 5)} {
               set tag [lindex $old_sc($index) 4]
               set shortcuts($tag) [lreplace $old_sc($index) 3 4 {} merge 0]
               lappend shortcut_order $tag
            }
         }
      }
   } elseif {!$isDefault} {
      # warn if rc/ini file could not be loaded
      if $is_unix {
         puts stderr "Warning: reading rc/ini file: $errmsg"
      } else {
         after idle [list tk_messageBox -type ok -default ok -icon warning -message "Warning: rcfile: $errmsg"]
      }
   }

   # build user-defined column definitions from compact lists in rc file
   if {[info exists pilistbox_col_widths]} {
      foreach {col width} $pilistbox_col_widths {
         set colsel_tabs($col) [concat $width [lrange $colsel_tabs($col) 1 end]]
      }
   }
   foreach tag [array names pilistbox_usercol] {
      set usercols($tag) [lindex $pilistbox_usercol($tag) 1]
      set ltmp [lindex $pilistbox_usercol($tag) 0]
      set htyp [lindex $ltmp 2]
      if {([string compare $htyp none] != 0) && \
          ([string compare $htyp "&user_def"] != 0)} {
         if {([string compare -length 1 $htyp "&"] != 0) && \
             [info exists nxtvepg_version] && ($nxtvepg_version <= 0x020400)} {
            # backward compatibility: prepend "&" operator
            set htyp "&$htyp"
            set ltmp [lreplace $ltmp 2 2 $htyp]
         }
         if {[info exists colsel_tabs([string range $htyp 1 end])] == 0} {
            # indirect function reference with unknown column keyword -> remove
            set ltmp [lreplace $ltmp 2 2 none]
         }
      }
      set colsel_tabs(user_def_$tag) $ltmp
   }

   # check consistancy of references from PI column selection to user-defined columns
   set ltmp {}
   foreach col $pilistbox_cols {
      set exists [info exists colsel_tabs($col)]
      if {$exists && ([scan $col "user_def_%d" tag] == 1)} {
         set exists [info exists usercols($tag)]
      }
      if $exists {
         lappend ltmp $col
      } elseif $is_unix {
         puts stderr "Warning: unresolved ref. to user-defined column '$col' in rc file deleted"
      }
   }
   set pilistbox_cols $ltmp

   # check consistancy of references from HTML column selection to user-defined columns
   set ltmp {}
   foreach col $dumphtml_colsel {
      set exists [info exists colsel_tabs($col)]
      if {$exists && ([scan $col "user_def_%d" tag] == 1)} {
         set exists [info exists usercols($tag)]
      }
      if $exists {
         lappend ltmp $col
      } elseif $is_unix {
         puts stderr "Warning: unresolved ref. to user-defined column '$col' in rc file deleted"
      }
   }
   set dumphtml_colsel $ltmp

   if {[info exists EPG_VERSION_NO] && [info exists nxtvepg_version]} {
      if {$nxtvepg_version > $EPG_VERSION_NO} {
         tk_messageBox -type ok -default ok -icon warning -message "The config file is from a newer version of nxtvepg. Some settings may get lost."
      }
   }
}

##
##  Write all config variables into the rc/ini file
##
proc UpdateRcFile {} {
   global shortcuts shortcut_order
   global prov_selection prov_freqs cfnetwops cfnetnames cfnettimes
   global showNetwopListbox showNetwopListboxLeft showShortcutListbox
   global showStatusLine showColumnHeader
   global hideOnMinimize menuUserLanguage help_winsize
   global prov_merge_cnis prov_merge_cf
   global acq_mode acq_mode_cnis netacq_enable
   global pibox_height pilistbox_cols shortinfo_height
   global hwcfg netacqcf xawtvcf wintvcf ctxmencf ctxmencf_wintv_vcr
   global wintvapp_idx wintvapp_path hwcfg_tvapp_idx hwcfg_tvapp_path
   global substr_history
   global dumpdb_filename
   global dumpdb_pi dumpdb_xi dumpdb_ai dumpdb_ni dumpdb_oi dumpdb_mi dumpdb_li dumpdb_ti
   global dumptabs dumptabs_filename
   global dumphtml_filename dumphtml_file_append dumphtml_file_overwrite
   global dumphtml_sel dumphtml_sel_count dumphtml_type
   global dumphtml_hyperlinks dumphtml_use_colsel dumphtml_colsel
   global dumpxml_filename
   global colsel_tabs usercols colsel_ailist_predef
   global EPG_VERSION EPG_VERSION_NO
   global myrcfile

   if {[catch {set rcfile [open $myrcfile "w"]}] == 0} {
      puts $rcfile "#"
      puts $rcfile "# Nextview EPG configuration file"
      puts $rcfile "#"
      puts $rcfile "# This file is automatically generated - do not edit"
      puts $rcfile "# Written at: [clock format [clock seconds] -format %c]"
      puts $rcfile "#"

      # dump software version
      puts $rcfile [list set nxtvepg_version $EPG_VERSION_NO]
      puts $rcfile [list set nxtvepg_version_str $EPG_VERSION]

      # dump filter shortcuts
      foreach index [lsort -integer [array names shortcuts]] {
         puts $rcfile [list set shortcuts($index) $shortcuts($index)]
      }
      puts $rcfile [list set shortcut_order $shortcut_order]

      # dump provider selection order
      puts $rcfile [list set prov_selection $prov_selection]

      # dump provider frequency list
      puts $rcfile [list set prov_freqs $prov_freqs]

      # dump network selection for all providers
      foreach index [array names cfnetwops] {
         puts $rcfile [list set cfnetwops($index) $cfnetwops($index)]
      }
      # dump network names
      if [array exists cfnetnames] {
         puts $rcfile [list array set cfnetnames [array get cfnetnames]]
      }
      # dump network air times
      if [array exists cfnettimes] {
         puts $rcfile [list array set cfnettimes [array get cfnettimes]]
      }

      # dump shortcuts & network listbox visibility
      puts $rcfile [list set showNetwopListbox $showNetwopListbox]
      puts $rcfile [list set showNetwopListboxLeft $showNetwopListboxLeft]
      puts $rcfile [list set showShortcutListbox $showShortcutListbox]
      puts $rcfile [list set showStatusLine $showStatusLine]
      puts $rcfile [list set showColumnHeader $showColumnHeader]
      puts $rcfile [list set hideOnMinimize $hideOnMinimize]
      puts $rcfile [list set menuUserLanguage $menuUserLanguage]

      # dump size of help window, if modified by the user
      if {[info exists help_winsize]} {puts $rcfile [list set help_winsize $help_winsize]}

      # dump provider database merge CNIs and configuration
      if {[info exists prov_merge_cnis]} {puts $rcfile [list set prov_merge_cnis $prov_merge_cnis]}
      if {[info exists prov_merge_cf]} {puts $rcfile [list set prov_merge_cf $prov_merge_cf]}

      # dump browser listbox configuration
      if {[info exists shortinfo_height]} {puts $rcfile [list set shortinfo_height $shortinfo_height]}
      if {[info exists pibox_height]} {puts $rcfile [list set pibox_height $pibox_height]}
      if {[info exists pilistbox_cols]} {puts $rcfile [list set pilistbox_cols $pilistbox_cols]}

      # dump browser listbox column widths
      set pilistbox_col_widths {}
      foreach col [array names colsel_tabs] {
         if {[string compare -length 9 user_def_ $col] != 0} {
            lappend pilistbox_col_widths $col [lindex $colsel_tabs($col) 0]
         }
      }
      puts $rcfile [list set pilistbox_col_widths $pilistbox_col_widths]

      # dump user-defined columns
      foreach col [array names usercols] {
         puts $rcfile [list set pilistbox_usercol($col) [list $colsel_tabs(user_def_$col) $usercols($col)]]
      }

      # dump acquisition mode and provider CNIs
      if {[info exists acq_mode]} { puts $rcfile [list set acq_mode $acq_mode] }
      if {[info exists acq_mode_cnis]} {puts $rcfile [list set acq_mode_cnis $acq_mode_cnis]}
      puts $rcfile [list set netacq_enable $netacq_enable]

      if {[info exists hwcfg]} {puts $rcfile [list set hwcfg $hwcfg]}
      puts $rcfile [list set wintvapp_path $wintvapp_path]
      puts $rcfile [list set wintvapp_idx $wintvapp_idx]
      puts $rcfile [list set hwcfg_tvapp_path $hwcfg_tvapp_path]
      puts $rcfile [list set hwcfg_tvapp_idx $hwcfg_tvapp_idx]

      puts $rcfile [list set netacqcf $netacqcf]
      puts $rcfile [list set xawtvcf $xawtvcf]
      puts $rcfile [list set wintvcf $wintvcf]
      puts $rcfile [list set ctxmencf $ctxmencf]
      puts $rcfile [list set ctxmencf_wintv_vcr $ctxmencf_wintv_vcr]

      if {[info exists substr_history]} {puts $rcfile [list set substr_history $substr_history]}

      puts $rcfile [list set dumpdb_filename $dumpdb_filename]
      puts $rcfile [list set dumpdb_pi $dumpdb_pi]
      puts $rcfile [list set dumpdb_xi $dumpdb_xi]
      puts $rcfile [list set dumpdb_ai $dumpdb_ai]
      puts $rcfile [list set dumpdb_ni $dumpdb_ni]
      puts $rcfile [list set dumpdb_oi $dumpdb_oi]
      puts $rcfile [list set dumpdb_mi $dumpdb_mi]
      puts $rcfile [list set dumpdb_li $dumpdb_li]
      puts $rcfile [list set dumpdb_ti $dumpdb_ti]

      puts $rcfile [list set dumptabs_filename $dumptabs_filename]
      puts $rcfile [list set dumptabs $dumptabs]

      puts $rcfile [list set dumphtml_filename $dumphtml_filename]
      puts $rcfile [list set dumphtml_file_append $dumphtml_file_append]
      puts $rcfile [list set dumphtml_file_overwrite $dumphtml_file_overwrite]
      puts $rcfile [list set dumphtml_sel $dumphtml_sel]
      puts $rcfile [list set dumphtml_sel_count $dumphtml_sel_count]
      puts $rcfile [list set dumphtml_type $dumphtml_type]
      puts $rcfile [list set dumphtml_hyperlinks $dumphtml_hyperlinks]
      puts $rcfile [list set dumphtml_use_colsel $dumphtml_use_colsel]
      puts $rcfile [list set dumphtml_colsel $dumphtml_colsel]

      puts $rcfile [list set dumpxml_filename $dumpxml_filename]

      close $rcfile
   } else {
      tk_messageBox -type ok -default ok -icon error -message "could not write to ini file $myrcfile"
   }
}

##
##  Update the provider preference order: move the last selected CNI to the front
##
set prov_selection {}

proc UpdateProvSelection {cni} {
   global prov_selection

   # check if an update is required
   if {($cni != [lindex $prov_selection 0]) && ($cni != 0)} {

      # delete the cni in the old list
      set index 0
      foreach prov $prov_selection {
         if {$prov == $cni} {
            set prov_selection [lreplace $prov_selection $index $index]
            break
         }
         incr index
      }
      # prepend the cni to the selection list
      set prov_selection [linsert $prov_selection 0 $cni]

      # save the new list into the ini/rc file
      UpdateRcFile
   }
}

##
##  Same as above, but with more than one CNI
##  CNI list taken from the merged db (starting with the special CNI 0x00FF)
##
proc UpdateMergedProvSelection {} {
   global prov_selection
   global prov_merge_cnis

   set cni_list [concat 0x00FF $prov_merge_cnis]
   set cni_count [llength $cni_list]

   # compare the given list with the beginning of the old list
   set index 0
   foreach cni $cni_list {
      if {$cni != [lindex $prov_selection $index]} {
         break
      }
      incr index
   }

   # check if an update is required
   if {$index < $cni_count} {

      # delete the given CNIs in the old list
      set new_list {}
      foreach cni $prov_selection {
         if {[lsearch -exact $cni_list $cni] == -1} {
            lappend new_list $cni
         }
      }

      # assemble the new provider selection list
      set prov_selection [concat $cni_list $new_list]

      # save the new list into the ini/rc file
      UpdateRcFile
   }
}

##
##  Update the frequency for a given provider
##
set prov_freqs {}

proc UpdateProvFrequency {cniFreqList} {
   global prov_freqs

   set modified 0

   foreach {cni freq} $cniFreqList {
      # search the list for the given CNI
      set found 0
      set idx 0
      foreach {rc_cni rc_freq} $prov_freqs {
         if {$rc_cni == $cni} {
            if {$rc_freq != $freq} {
               # CNI already in the list with a different frequency -> update
               set prov_freqs [lreplace $prov_freqs $idx [expr $idx + 1] $cni $freq]
               set modified 1
            }
            set found 1
            break
         }
         incr idx 2
      }

      if {$found == 0} {
         # not found in the list -> append new pair to the list
         lappend prov_freqs $cni $freq
         set modified 1
      }
   }

   if $modified {
      # frequency added or modified -> write the rc/ini file
      UpdateRcFile
   }
}

