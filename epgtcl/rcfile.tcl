#
#  RC/INI file handling
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
#    Implements reading and writing the rc (UNIX) or INI (Windows) file.
#    Since version 2.8.0 this module only manages parameters used by
#    the GUI.
#

proc LoadRcFile {filename} {
   global shortcuts shortcut_tree
   global cfnettimes cfnetjoin
   global showNetwopListbox showNetwopListboxLeft showShortcutListbox
   global showTuneTvButton showLayoutButton showStatusLine
   global showColumnHeader showDateScale
   global hideOnMinimize help_winsize help_lang
   global pibox_height pilistbox_cols shortinfo_height
   global pibox_type pinetbox_col_count pinetbox_col_width
   global pinetbox_rows pinetbox_rows_nonl
   global xawtvcf wintvcf ctxmencf ctxmencf_wintv_vcr
   global tunetv_cmd_unix tunetv_cmd_win
   global substr_history
   global dumpdb_filename
   global dumpdb_pi dumpdb_xi dumpdb_ai dumpdb_ni dumpdb_oi dumpdb_mi dumpdb_li dumpdb_ti
   global dumptabs dumptabs_filename
   global dumphtml_filename dumphtml_file_append dumphtml_file_overwrite
   global dumphtml_sel dumphtml_sel_count dumphtml_type
   global dumphtml_hyperlinks dumphtml_text_fmt dumphtml_use_colsel dumphtml_colsel
   global dumpxml_filename dumpxml_format
   global colsel_tabs usercols
   global remgroups remgroup_order reminders rem_msg_cnf_time rem_cmd_cnf_time
   global remlist_winsize rempilist_colsize remsclist_colsize
   global is_unix

   set shortcut_tree {}
   set error 0
   set line_no 0
   set skip_sect 1

   set my_sections [list {TV APP INTERACTION} NETWORKS {DATABASE EXPORT} \
                         GUI REMINDER {FILTER SHORTCUTS}]

   if {[catch {set rcfile [open $filename "r"]} errmsg] == 0} {
      fconfigure $rcfile -encoding "utf-8"

      while {[gets $rcfile line] >= 0} {
         incr line_no
         if {[regexp {^\[(.*)\]$} $line foo sect_name]} {
            # skip sections which are managed by C code
            set skip_sect [expr [lsearch -exact $my_sections $sect_name] == -1]

         } elseif {[string compare $line "___END___"] == 0} {
            set skip_sect 1

         } elseif {!$skip_sect && !$error} {
            # eval the line immediately
            if {[catch $line] != 0} {
               tk_messageBox -type ok -default ok -icon error -message "Syntax error in rc/ini file, line #$line_no: $line"
               set error 1
            }

         } elseif $skip_sect {
            # detect C section version declaration: comes before GUI sections
            if {[regexp {^nxtvepg_version = (0x[0-9a-zA-Z]+)$} $line foo nxtvepg_version]} {
               if {$nxtvepg_version <= 0x030003} {
                  # Prior to 3.0.4 the RC file was using system encoding
                  fconfigure $rcfile -encoding [encoding system]
               }
            }
         }
      }
      close $rcfile

      if {[info exists nxtvepg_version] && ($nxtvepg_version == 0x030003)} {
         # convert themes class
         foreach tag [array names shortcuts] {
            # {Meteo substr {substr {{1 0 0 0 Meteo}}} {} merge 0}
            set ltmp {}
            foreach {ident valist} [lindex $shortcuts($tag) 2] {
               if {[string match $ident theme_class*] == 0} {
                  # TODO convert numbers to names?
                  lappend ltmp $ident $valist

               } else {
                  lappend ltmp $ident $valist
               }
            }
            set shortcuts($tag) [lreplace $shortcuts($tag) 2 2 $ltmp]
         }
      }
   }

   catch {

      # check consistancy of shortcuts
      #set ltmp {}
      #foreach sc_tag $shortcut_order {
      #   if [info exists shortcuts($sc_tag)] {
      #      lappend ltmp $sc_tag
      #   } elseif $is_unix {
      #      puts stderr "Warning: unresolved ref. to shortcut tag $sc_tag"
      #   }
      #}
      #set shortcut_order $ltmp
      # XXX TODO

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

      # build PI netbox row join array from list & check consistancy
      if [info exists pinetbox_rows_nonl_l] {
         array unset pinetbox_rows_nonl
         foreach tag $pinetbox_rows_nonl_l {
            if [info exists colsel_tabs($tag)] {
               set pinetbox_rows_nonl($tag) 1
            } elseif $is_unix {
               puts stderr "Warning: unresolved ref. column '$tag' (by netbox-row-join) in rc file deleted"
            }
         }
      }
   }
}

##
##  Write all config variables into the rc/ini file
##
proc UpdateRcFile {} {
   C_UpdateRcFile
}

##
##  Return string with GUI config data
##  - the string is appended to the rc file by the caller
##
proc GetGuiRcData {} {
   global shortcuts shortcut_tree
   global cfnettimes cfnetjoin
   global showNetwopListbox showNetwopListboxLeft showShortcutListbox
   global showTuneTvButton showLayoutButton showStatusLine
   global showColumnHeader showDateScale
   global hideOnMinimize help_winsize help_lang
   global pibox_height pilistbox_cols shortinfo_height
   global pibox_type pinetbox_col_count pinetbox_col_width
   global pinetbox_rows pinetbox_rows_nonl
   global xawtvcf wintvcf ctxmencf ctxmencf_wintv_vcr
   global tunetv_cmd_unix tunetv_cmd_win
   global substr_history
   global dumpdb_filename
   global dumpdb_pi dumpdb_xi dumpdb_ai dumpdb_ni dumpdb_oi dumpdb_mi dumpdb_li dumpdb_ti
   global dumptabs dumptabs_filename
   global dumphtml_filename dumphtml_file_append dumphtml_file_overwrite
   global dumphtml_sel dumphtml_sel_count dumphtml_type
   global dumphtml_hyperlinks dumphtml_text_fmt dumphtml_use_colsel dumphtml_colsel
   global dumpxml_filename dumpxml_format
   global colsel_tabs usercols colsel_ailist_predef
   global remgroups remgroup_order reminders rem_msg_cnf_time rem_cmd_cnf_time
   global remlist_winsize rempilist_colsize remsclist_colsize
   global EPG_VERSION EPG_VERSION_NO

   set rcfile {}
   #{
      append rcfile {[TV APP INTERACTION]} "\n"
      append rcfile [list set xawtvcf $xawtvcf] "\n"
      append rcfile [list set wintvcf $wintvcf] "\n"
      append rcfile [list set ctxmencf_wintv_vcr $ctxmencf_wintv_vcr] "\n"
      append rcfile "\n"

      append rcfile {[NETWORKS]} "\n"
      # dump network air times
      if [array exists cfnettimes] {
         append rcfile [list array set cfnettimes [array get cfnettimes]] "\n"
      }
      # dump network join lists
      append rcfile [list set cfnetjoin $cfnetjoin] "\n"
      append rcfile "\n"

      append rcfile {[DATABASE EXPORT]} "\n"
      append rcfile [list set dumpdb_filename $dumpdb_filename] "\n"
      append rcfile [list set dumpdb_pi $dumpdb_pi] "\n"
      append rcfile [list set dumpdb_xi $dumpdb_xi] "\n"
      append rcfile [list set dumpdb_ai $dumpdb_ai] "\n"
      append rcfile [list set dumpdb_ni $dumpdb_ni] "\n"
      append rcfile [list set dumpdb_oi $dumpdb_oi] "\n"
      append rcfile [list set dumpdb_mi $dumpdb_mi] "\n"
      append rcfile [list set dumpdb_li $dumpdb_li] "\n"
      append rcfile [list set dumpdb_ti $dumpdb_ti] "\n"

      append rcfile [list set dumptabs_filename $dumptabs_filename] "\n"
      append rcfile [list set dumptabs $dumptabs] "\n"

      append rcfile [list set dumphtml_filename $dumphtml_filename] "\n"
      append rcfile [list set dumphtml_file_append $dumphtml_file_append] "\n"
      append rcfile [list set dumphtml_file_overwrite $dumphtml_file_overwrite] "\n"
      append rcfile [list set dumphtml_sel $dumphtml_sel] "\n"
      append rcfile [list set dumphtml_sel_count $dumphtml_sel_count] "\n"
      append rcfile [list set dumphtml_type $dumphtml_type] "\n"
      append rcfile [list set dumphtml_hyperlinks $dumphtml_hyperlinks] "\n"
      append rcfile [list set dumphtml_text_fmt $dumphtml_text_fmt] "\n"
      append rcfile [list set dumphtml_use_colsel $dumphtml_use_colsel] "\n"
      append rcfile [list set dumphtml_colsel $dumphtml_colsel] "\n"

      append rcfile [list set dumpxml_filename $dumpxml_filename] "\n"
      append rcfile [list set dumpxml_format $dumpxml_format] "\n"
      append rcfile "\n"

      append rcfile {[GUI]} "\n"
      append rcfile [list set nxtvepg_version $EPG_VERSION_NO] "\n"
      append rcfile [list set nxtvepg_version_str $EPG_VERSION] "\n"
      # dump shortcuts & network listbox visibility
      append rcfile [list set showNetwopListbox $showNetwopListbox] "\n"
      append rcfile [list set showNetwopListboxLeft $showNetwopListboxLeft] "\n"
      append rcfile [list set showShortcutListbox $showShortcutListbox] "\n"
      append rcfile [list set showTuneTvButton $showTuneTvButton] "\n"
      append rcfile [list set showLayoutButton $showLayoutButton] "\n"
      append rcfile [list set showStatusLine $showStatusLine] "\n"
      append rcfile [list set showDateScale $showDateScale] "\n"
      append rcfile [list set showColumnHeader $showColumnHeader] "\n"
      append rcfile [list set hideOnMinimize $hideOnMinimize] "\n"

      # dump size of help window, if modified by the user
      if {[info exists help_lang]} {append rcfile [list set help_lang $help_lang] "\n"}
      if {[info exists help_winsize]} {append rcfile [list set help_winsize $help_winsize] "\n"}

      # dump browser listbox configuration
      if {[info exists shortinfo_height]} {append rcfile [list set shortinfo_height $shortinfo_height] "\n"}
      if {[info exists pibox_height]} {append rcfile [list set pibox_height $pibox_height] "\n"}
      if {[info exists pilistbox_cols]} {append rcfile [list set pilistbox_cols $pilistbox_cols] "\n"}
      append rcfile [list set pibox_type $pibox_type] "\n"
      append rcfile [list set pinetbox_col_count $pinetbox_col_count] "\n"
      append rcfile [list set pinetbox_col_width $pinetbox_col_width] "\n"
      append rcfile [list set pinetbox_rows $pinetbox_rows] "\n"
      append rcfile [list set pinetbox_rows_nonl_l [array names pinetbox_rows_nonl]] "\n"

      # dump browser listbox column widths
      set pilistbox_col_widths {}
      foreach col [array names colsel_tabs] {
         if {[string compare -length 9 user_def_ $col] != 0} {
            lappend pilistbox_col_widths $col [lindex $colsel_tabs($col) 0]
         }
      }
      append rcfile [list set pilistbox_col_widths $pilistbox_col_widths] "\n"

      # dump user-defined columns
      foreach col [array names usercols] {
         append rcfile [list set pilistbox_usercol($col) [list $colsel_tabs(user_def_$col) $usercols($col)]] "\n"
      }

      append rcfile [list set ctxmencf $ctxmencf] "\n"
      append rcfile [list set tunetv_cmd_unix $tunetv_cmd_unix] "\n"
      append rcfile [list set tunetv_cmd_win $tunetv_cmd_win] "\n"

      if {[info exists substr_history]} {append rcfile [list set substr_history $substr_history] "\n"}

      append rcfile [list set remlist_winsize $remlist_winsize] "\n"
      append rcfile [list set rempilist_colsize $rempilist_colsize] "\n"
      append rcfile [list set remsclist_colsize $remsclist_colsize] "\n"
      append rcfile "\n"

      append rcfile {[REMINDER]} "\n"
      foreach gtag [array names remgroups] {
         append rcfile [list set remgroups($gtag) $remgroups($gtag)] "\n"
      }
      append rcfile [list set remgroup_order $remgroup_order] "\n"
      append rcfile [list set reminders {}] "\n"
      for {set idx 0} {$idx < [llength $reminders]} {incr idx} {
         append rcfile [list lappend reminders [lindex $reminders $idx]] "\n"
      }
      append rcfile [list set rem_msg_cnf_time $rem_msg_cnf_time] "\n"
      append rcfile [list set rem_cmd_cnf_time $rem_cmd_cnf_time] "\n"
      append rcfile "\n"

      # dump filter shortcuts
      append rcfile {[FILTER SHORTCUTS]} "\n"
      foreach index [lsort -integer [array names shortcuts]] {
         append rcfile [list set shortcuts($index) $shortcuts($index)] "\n"
      }
      append rcfile [list set shortcut_tree $shortcut_tree] "\n"
      append rcfile "\n"
   #}
   return $rcfile
}
