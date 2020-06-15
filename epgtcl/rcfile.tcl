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
#    Since version 2.8.0 this module only manages parameters used by
#    the GUI.
#
#  Author: Tom Zoerner
#
#  $Id: rcfile.tcl,v 1.25 2005/01/08 15:17:16 tom Exp tom $
#

proc LoadRcFile {filename } {
   global shortcuts shortcut_tree
   global cfnetwops cfnetnames cfnettimes cfnetjoin
   global showNetwopListbox showNetwopListboxLeft showShortcutListbox
   global showLayoutButton showStatusLine showColumnHeader showDateScale
   global hideOnMinimize menuUserLanguage help_winsize
   global pibox_height pilistbox_cols shortinfo_height
   global pibox_type pinetbox_col_count pinetbox_col_width
   global pinetbox_rows pinetbox_rows_nonl
   global xawtvcf wintvcf ctxmencf ctxmencf_wintv_vcr
   global wintvapp_idx wintvapp_path
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
      while {[gets $rcfile line] >= 0} {
         incr line_no
         if {[regexp {^\[(.*)\]$} $line foo sect_name]} {
            # skip sections which are managed by C code
            set skip_sect [expr [lsearch -exact $my_sections $sect_name] == -1]

         } elseif {[string compare $line "___END___"] == 0} {
            set skip_sect 1

         } elseif {!$skip_sect && ([catch $line] != 0) && !$error} {
            tk_messageBox -type ok -default ok -icon error -message "Syntax error in rc/ini file, line #$line_no: $line"
            set error 1

         } elseif $skip_sect {
            if {[regexp {^set nxtvepg_version (0x[0-9a-zA-Z]+)$} $line foo nxtvepg_version]} {
               # old-style config file detected
               set skip_sect 0
            }
         }
      }
      close $rcfile

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
      if {![info exists nxtvepg_version] || ($nxtvepg_version < 0x020500) || \
          ![info exists nxtvepg_version_str] || [string match "2.5.0pre*" $nxtvepg_version_str]} {
         # starting with 2.5.0pre15 the substr param list was made a list of param lists
         # and the param list was extended by two boolean parameters
         foreach tag [array names shortcuts] {
            set ltmp {}
            foreach {ident valist} [lindex $shortcuts($tag) 2] {
               if {([string compare $ident substr] == 0) && \
                   ([llength $valist] == 5) && \
                   ([llength [lindex $valist 0]] == 1) } {
                  lappend ltmp $ident [list [concat [lrange $valist 4 4] [lrange $valist 0 3] 0 0]]
               } else {
                  lappend ltmp $ident $valist
               }
            }
            set shortcuts($tag) [lreplace $shortcuts($tag) 2 2 $ltmp]
         }
         if {[info exists substr_history] && ([llength $substr_history] > 0) && \
             ([llength [lindex $substr_history 0]] == 5)} { \
            set substr_history {}
         }
      }
      if {[info exists nxtvepg_version] && ($nxtvepg_version == 0x020500)} {
         # fix a bug in a pre-defined shortcut for France in 2.5.0
         foreach tag [array names shortcuts] {
            # {Météo substr {substr {{1 0 0 0 Météo}}} {} merge 0}
            set ltmp {}
            foreach {ident valist} [lindex $shortcuts($tag) 2] {
               if {([string compare $ident substr] == 0) && \
                   ([llength $valist] == 1) && \
                   ([llength [lindex $valist 0]] == 5) } {
                  set ltmp0  [lindex $valist 0]
                  lappend ltmp $ident [list [concat [lindex $ltmp0 4] [lrange $ltmp0 0 3] 0 0]]
                  unset ltmp0
               } else {
                  lappend ltmp $ident $valist
               }
            }
            set shortcuts($tag) [lreplace $shortcuts($tag) 2 2 $ltmp]
         }
      }
      if {[info exists nxtvepg_version] && ($nxtvepg_version < 0x020590)} {
         array set old_cols {red #CC0000 blue #0000CC green #00CC00 \
                             yellow #CCCC00 pink #CC00CC cyan #00CCCC}
         foreach tag [array names pilistbox_usercol] {
            set ltmp {}
            foreach filt [lindex $pilistbox_usercol($tag) 1] {
               set ftmp {}
               foreach fmt [lindex $filt 2] {
                  if { ([string compare $fmt bold] == 0) || \
                       ([string compare $fmt underline] == 0)} {
                     lappend ftmp $fmt
                  } elseif [info exists old_cols($fmt)] {
                     lappend ftmp fg_RGB[string range $old_cols($fmt) 1 end]
                  }
               }
               lappend ltmp [lreplace $filt 2 2 $ftmp]
            }
            set pilistbox_usercol($tag) [lreplace $pilistbox_usercol($tag) 1 1 $ltmp]
         }
         # append reminder "Mark" column to pibox in both layouts
         lappend pilistbox_cols reminder
         set pinetbox_rows [concat reminder $pinetbox_rows]
         lappend pinetbox_rows_nonl_l reminder
      }
      if {[info exists nxtvepg_version] && ($nxtvepg_version < 0x020593)} {
         # "control" element was added to "single PI" reminders in 2.6.0pre3
         set ltmp {}
         foreach elem $reminders {
            lappend ltmp [linsert $elem 1 0]
         }
         set reminders $ltmp
         # added "state" column to "edit reminders" dialog
         set rempilist_colsize {4 10 10 13 24 12}
      }
      if {[info exists nxtvepg_version] && ($nxtvepg_version < 0x020594)} {
         # reminder group config changed from list into array index by random tags
         set remgroup_order {}
         if [info exists remgroups] {
            set ltmp $remgroups
            catch {unset remgroups}
            set gtag 1000
            foreach elem $ltmp {
               set remgroups($gtag) $elem
               lappend remgroup_order $gtag
               incr gtag
            }
         }
         # convert group tags in reminder elements
         set ltmp {}
         foreach elem $reminders {
            if {[lindex $elem 0] < 31} {
               lappend ltmp [lreplace $elem 0 0 [expr [lindex $elem 0] + 1000]]
            } else {
               lappend ltmp [lreplace $elem 0 0 0]
            }
         }
         set reminders $ltmp
         # convert group references in user-defined columns
         foreach tag [array names pilistbox_usercol] {
            set ltmp {}
            foreach filt [lindex $pilistbox_usercol($tag) 1] {
               set sc_tag [lindex $filt 4]
               set grp_tag [UserColsDlg_IsReminderPseudoTag $sc_tag]
               if {$grp_tag > 0} {
                  set sc_tag rgp_[expr $grp_tag + 1000]
               }
               lappend ltmp [lreplace $filt 4 4 $sc_tag]
            }
            set pilistbox_usercol($tag) [lreplace $pilistbox_usercol($tag) 1 1 $ltmp]
         }
      }
      if {[info exists nxtvepg_version] && ($nxtvepg_version < 0x020595)} {
         # new column type was added -> show it (single list layout only)
         if {[lsearch $pilistbox_cols weekcol] == -1} {
            set pilistbox_cols [concat weekcol $pilistbox_cols]
         }
      }
      if {[info exists nxtvepg_version] && ($nxtvepg_version <= 0x020600)} {
         if [info exists shortcut_order] {
            set shortcut_tree {}
            foreach sc_tag $shortcut_order {
               lappend shortcut_tree $sc_tag
            }
            # append sub-directory with hidden shortcuts ($::fsc_hide_idx)
            set tmpl {}
            foreach sc_tag [array names shortcuts] {
               if [lindex $shortcuts($sc_tag) 5] {
                  lappend tmpl $sc_tag
               }
               set shortcuts($sc_tag) [lreplace $shortcuts($sc_tag) 5 5 ""]
               if {[regexp {^-*$} [lindex $shortcuts($sc_tag) 0]] &&
                   ([llength [lindex $shortcuts($sc_tag) 1]] == 0)} {
                  set shortcuts($sc_tag) [list {} {} {} {} "merge" "separator"]
               }
            }
            if {[llength $tmpl] > 0} {
               set sc_tag [GenerateShortcutTag]
               set shortcuts($sc_tag) [list "Hidden" {} {} {} merge "-dir"]
               lappend shortcut_tree [concat $sc_tag $tmpl]
            }
         }
      }
      if {[info exists nxtvepg_version] && ($nxtvepg_version < 0x020792)} {
         set tmpl [list {pi_context.undofilt {} {}} {menu_separator {} {}} \
                        {pi_context.addfilt {} {}} {menu_separator {} {}} \
                        {pi_context.reminder_short {} {}} {menu_separator {} {}}]
         foreach {title cmd} $ctxmencf {
            if [regexp {^!(xawtv|wintv)! *(.*)} $cmd foo type_str cmd_str] {
               lappend tmpl [list "tvapp.$type_str" $title $cmd_str]
            } else {
               if $is_unix {
                  lappend tmpl [list exec.unix $title $cmd]
               } else {
                  lappend tmpl [list exec.win32 $title $cmd]
               }
            }
         }
         set ctxmencf $tmpl
      }
      if {[info exists nxtvepg_version] && ($nxtvepg_version < 0x0207C2)} {
         ConvertV27RcFile 
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
##  Helper procedure to import rc files of version 2.7.x and earlier
##  - note the format of GUI parameters didn't change, so no conversions is needed
##  - this function is only used to pass parameters which are now managed at
##    C level to the C parser function
##  - params are stored in a global string in the format used by the C parser;
##    the string is later read and parsed by C level functions
##
proc ConvertV27RcFile {} {
   # the following code executes in the context of the calling procedure
   # so that it can access it's local variables
   uplevel 1 {
      global rcfileUpgradeStr
      set rcfileUpgradeStr {}

      # dump software version
      append rcfileUpgradeStr {[VERSION]} "\n"
      catch {append rcfileUpgradeStr [concat nxtvepg_version = $nxtvepg_version] "\n"}
      catch {append rcfileUpgradeStr [concat nxtvepg_version_str = $nxtvepg_version_str] "\n"}
      catch {append rcfileUpgradeStr [concat rc_compat_version = $rc_compat_version] "\n"}
      catch {append rcfileUpgradeStr [concat rc_timestamp = $rc_timestamp] "\n"}
      append rcfileUpgradeStr "\n"

      # dump acquisition parameters
      append rcfileUpgradeStr {[ACQUISITION]} "\n"
      catch {append rcfileUpgradeStr [concat acq_mode = $acq_mode] "\n"}
      catch {append rcfileUpgradeStr [concat acq_mode_cnis = $acq_mode_cnis] "\n"}
      catch {append rcfileUpgradeStr [concat prov_freqs = $prov_freqs] "\n"}
      catch {append rcfileUpgradeStr [concat epgscan_opt_ftable = $epgscan_opt_ftable] "\n"}
      append rcfileUpgradeStr "\n"

      # dump provider concat
      append rcfileUpgradeStr {[DATABASE]} "\n"
      catch {append rcfileUpgradeStr [concat piexpire_cutoff = $piexpire_cutoff] "\n"}
      catch {append rcfileUpgradeStr [concat prov_selection = $prov_selection] "\n"}
      catch {append rcfileUpgradeStr [concat prov_merge_cnis = $prov_merge_cnis] "\n"}
      if [info exists prov_merge_cf] {
         foreach {key val} $prov_merge_cf {
            append rcfileUpgradeStr [concat prov_merge_cf$key = $val] "\n"
         }
      }
      catch {append rcfileUpgradeStr [concat prov_merge_netwops = [lindex $cfnetwops(0x00FF) 0]] "\n"}
      append rcfileUpgradeStr "\n"

      append rcfileUpgradeStr {[CLIENT SERVER]} "\n"
      catch {append rcfileUpgradeStr [concat netacq_enable = $netacq_enable] "\n"}
      catch {
         foreach {key val} $netacqcf {
            append rcfileUpgradeStr [concat netacqcf_$key = $val] "\n"
         }
      }
      append rcfileUpgradeStr "\n"

      # dump video input and TV card configuration
      append rcfileUpgradeStr {[TV CARDS]} "\n"
      catch {append rcfileUpgradeStr [concat hwcf_cardidx = $hwcf_cardidx] "\n"}
      catch {append rcfileUpgradeStr [concat hwcf_input = $hwcf_input] "\n"}
      catch {append rcfileUpgradeStr [concat hwcf_acq_prio = $hwcf_acq_prio] "\n"}
      catch {append rcfileUpgradeStr [concat hwcf_slicer_type = $hwcf_slicer_type] "\n"}
      catch {append rcfileUpgradeStr [concat hwcf_wdm_stop = $hwcf_wdm_stop] "\n"}
      catch {append rcfileUpgradeStr [concat tvcardcf_count = [array size tvcardcf]] "\n"}
      catch {
         foreach cardidx [array names tvcardcf] {
            append rcfileUpgradeStr [concat tvcardcf_$cardidx = $tvcardcf($cardidx)] "\n"
         }
      }
      append rcfileUpgradeStr "\n"
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
   global cfnetwops cfnetnames cfnettimes cfnetjoin
   global showNetwopListbox showNetwopListboxLeft showShortcutListbox
   global showLayoutButton showStatusLine showColumnHeader showDateScale
   global hideOnMinimize menuUserLanguage help_winsize
   global pibox_height pilistbox_cols shortinfo_height
   global pibox_type pinetbox_col_count pinetbox_col_width
   global pinetbox_rows pinetbox_rows_nonl
   global xawtvcf wintvcf ctxmencf ctxmencf_wintv_vcr
   global wintvapp_idx wintvapp_path
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
      append rcfile [list set wintvapp_path $wintvapp_path] "\n"
      append rcfile [list set wintvapp_idx $wintvapp_idx] "\n"
      append rcfile [list set xawtvcf $xawtvcf] "\n"
      append rcfile [list set wintvcf $wintvcf] "\n"
      append rcfile [list set ctxmencf_wintv_vcr $ctxmencf_wintv_vcr] "\n"
      append rcfile "\n"

      append rcfile {[NETWORKS]} "\n"
      # dump network selection for all providers
      foreach index [array names cfnetwops] {
         append rcfile [list set cfnetwops($index) $cfnetwops($index)] "\n"
      }
      # dump network names
      if [array exists cfnetnames] {
         append rcfile [list array set cfnetnames [array get cfnetnames]] "\n"
      }
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
      append rcfile [list set showLayoutButton $showLayoutButton] "\n"
      append rcfile [list set showStatusLine $showStatusLine] "\n"
      append rcfile [list set showDateScale $showDateScale] "\n"
      append rcfile [list set showColumnHeader $showColumnHeader] "\n"
      append rcfile [list set hideOnMinimize $hideOnMinimize] "\n"
      append rcfile [list set menuUserLanguage $menuUserLanguage] "\n"

      # dump size of help window, if modified by the user
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

