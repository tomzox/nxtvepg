#
#  Configuration dialog for dumping the database
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
#    Implements a GUI for dumping the database.
#
set dumpdb_pi 1
set dumpdb_xi 1
set dumpdb_ai 1
set dumpdb_ni 1
set dumpdb_oi 1
set dumpdb_mi 1
set dumpdb_li 1
set dumpdb_ti 1
set dumpdb_filename {}
set dumpdb_popup 0

set dumptabs "pi"
set dumptabs_filename {}
set dumptabs_popup 0

set dumphtml_filename {}
set dumphtml_file_append 0
set dumphtml_file_overwrite 0
set dumphtml_sel 1
set dumphtml_sel_count 25
set dumphtml_type 3
set dumphtml_hyperlinks 1
set dumphtml_text_fmt 1
set dumphtml_use_colsel 0
set dumphtml_colsel {}
set dumphtml_popup 0
set htmlcols_popup 0

set dumpxml_filename {}
set dumpxml_format 0
set dumpxml_popup 0

#=CONST= ::xmltv_dtd_5_gmt  0
#=CONST= ::xmltv_dtd_5_ltz  1


# notification by user-defined columns config dialog: columns addition or deletion
proc DumpHtml_ColUpdate {} {
   global dumphtml_colsel
   global htmlcols_ailist htmlcols_selist
   global htmlcols_popup

   if $htmlcols_popup {
      FillColumnSelectionDialog .htmlcols.all htmlcols_ailist htmlcols_selist $dumphtml_colsel 0
   }
}


#=LOAD=PopupDumpDatabase
#=LOAD=PopupDumpDbTabs
#=LOAD=PopupDumpHtml
#=LOAD=PopupDumpXml
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Create dialog to export the database into TAB-separated text file
##
proc PopupDumpDbTabs {} {
   global dumptabs_popup dumptabs_filename dumptabs
   global font_fixed fileImage

   if {$dumptabs_popup == 0} {
      CreateTransientPopup .dumptabs "Export database into text file"
      set dumptabs_popup 1

      frame .dumptabs.all

      frame .dumptabs.all.name
      label .dumptabs.all.name.prompt -text "File name:"
      pack .dumptabs.all.name.prompt -side left
      entry .dumptabs.all.name.filename -textvariable dumptabs_filename -font $font_fixed -width 30
      pack .dumptabs.all.name.filename -side left -padx 5
      bind .dumptabs.all.name.filename <Enter> {SelectTextOnFocus %W}
      bind .dumptabs.all.name.filename <Return> {tkButtonInvoke .dumptabs.all.cmd.apply}
      bind .dumptabs.all.name.filename <Escape> {tkButtonInvoke .dumptabs.all.cmd.clear}
      button .dumptabs.all.name.dlgbut -image $fileImage -command {
         set tmp [tk_getSaveFile -parent .dumptabs \
                     -initialfile [file tail $dumptabs_filename] \
                     -initialdir [file dirname $dumptabs_filename] \
                     -defaultextension .txt -filetypes {{TXT {*.txt}} {all {*.*}}}]
         if {[string length $tmp] > 0} {
            set dumptabs_filename $tmp
         }
         unset tmp
      }
      pack .dumptabs.all.name.dlgbut -side left -padx 5
      pack .dumptabs.all.name -side top -pady 10

      frame .dumptabs.all.opt
      frame .dumptabs.all.opt.one
      radiobutton .dumptabs.all.opt.one.pi -text "Programme Info" -variable dumptabs -value "pi"
      radiobutton .dumptabs.all.opt.one.ai -text "Application Info" -variable dumptabs -value "ai"
      radiobutton .dumptabs.all.opt.one.pdc -text "PDC themes list" -variable dumptabs -value "pdc"
      pack .dumptabs.all.opt.one.pi -side top -anchor nw
      pack .dumptabs.all.opt.one.ai -side top -anchor nw
      pack .dumptabs.all.opt.one.pdc -side top -anchor nw
      pack .dumptabs.all.opt.one -side left -anchor nw -padx 5
      pack .dumptabs.all.opt -side top -pady 10

      frame .dumptabs.all.cmd
      button .dumptabs.all.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Control menu) "Export as text"}
      pack .dumptabs.all.cmd.help -side left -padx 10
      button .dumptabs.all.cmd.clear -text "Dismiss" -width 5 -command {destroy .dumptabs}
      pack .dumptabs.all.cmd.clear -side left -padx 10
      button .dumptabs.all.cmd.apply -text "Export" -width 5 -command DoDbTabsDump -default active
      pack .dumptabs.all.cmd.apply -side left -padx 10
      pack .dumptabs.all.cmd -side top

      pack .dumptabs.all -padx 10 -pady 10
      bind .dumptabs <Key-F1> {PopupHelp $helpIndex(Control menu) "Export as text"}
      bind .dumptabs.all <Destroy> {+ set dumptabs_popup 0}
      focus .dumptabs.all.name.filename
   } else {
      raise .dumptabs
   }
}

# callback for Ok button
proc DoDbTabsDump {} {
   global dumptabs_popup dumptabs_filename dumptabs

   if [DlgDump_CheckOutputFile .dumptabs dumptabs_filename .txt 1 0] {

      C_DumpTabsDatabase $dumptabs_filename $dumptabs

      # save the settings to the rc/ini file
      UpdateRcFile
   }
}

##  --------------------------------------------------------------------------
##  Create dialog to export the database into HTML file
##
proc PopupDumpHtml {} {
   global dumphtml_filename dumphtml_type dumphtml_file_append dumphtml_file_overwrite
   global dumphtml_sel dumphtml_sel_count dumphtml_hyperlinks dumphtml_text_fmt
   global font_fixed fileImage
   global dumphtml_popup

   if {$dumphtml_popup == 0} {
      CreateTransientPopup .dumphtml "Export database into HTML document"
      set dumphtml_popup 1

      frame .dumphtml.opt1 -borderwidth 1 -relief raised
      frame .dumphtml.opt1.name
      label .dumphtml.opt1.name.prompt -text "File name:"
      pack .dumphtml.opt1.name.prompt -side left
      entry .dumphtml.opt1.name.filename -textvariable dumphtml_filename -font $font_fixed -width 30
      pack .dumphtml.opt1.name.filename -side left -padx 5
      bind .dumphtml.opt1.name.filename <Enter> {SelectTextOnFocus %W}
      bind .dumphtml.opt1.name.filename <Return> {tkButtonInvoke .dumphtml.cmd.write}
      bind .dumphtml.opt1.name.filename <Escape> {tkButtonInvoke .dumphtml.cmd.dismiss}
      button .dumphtml.opt1.name.dlgbut -image $fileImage -command {
         set tmp [tk_getSaveFile -parent .dumphtml \
                     -initialfile [file tail $dumphtml_filename] \
                     -initialdir [file dirname $dumphtml_filename] \
                     -defaultextension .html -filetypes {{HTML {*.html *.htm}} {all {*.*}}}]
         if {[string length $tmp] > 0} {
            set dumphtml_filename $tmp
         }
         unset tmp
      }
      pack .dumphtml.opt1.name.dlgbut -side left -padx 5
      pack .dumphtml.opt1.name -side top -pady 10

      checkbutton .dumphtml.opt1.sel0 -text "Append to file (if exists)" -variable dumphtml_file_append -command HtmlDump_ToggleAppend
      checkbutton .dumphtml.opt1.sel1 -text "Overwrite without asking" -variable dumphtml_file_overwrite
      pack .dumphtml.opt1.sel0 .dumphtml.opt1.sel1 -side top -anchor nw
      pack .dumphtml.opt1 -side top -pady 5 -padx 10 -fill x

      frame .dumphtml.opt2 -borderwidth 1 -relief raised
      radiobutton .dumphtml.opt2.chk0 -text "All programmes matching the filters" -variable dumphtml_sel -value 0
      frame .dumphtml.opt2.row1
      radiobutton .dumphtml.opt2.row1.chk1 -text "All matching programmes, but max." -variable dumphtml_sel -value 1
      pack .dumphtml.opt2.row1.chk1 -side left -anchor w
      entry .dumphtml.opt2.row1.ent1 -textvariable dumphtml_sel_count -font $font_fixed -width 5
      pack .dumphtml.opt2.row1.ent1 -side right -anchor e
      radiobutton .dumphtml.opt2.chk2 -text "Selected programme only" -variable dumphtml_sel -value 2
      pack .dumphtml.opt2.chk0 .dumphtml.opt2.row1 .dumphtml.opt2.chk2 -side top -anchor nw
      pack .dumphtml.opt2 -side top -pady 5 -padx 10 -fill x

      frame .dumphtml.opt3 -borderwidth 1 -relief raised
      radiobutton .dumphtml.opt3.sel0 -text "Write titles and descriptions" -variable dumphtml_type -value 3 -command HtmlDump_ToggleType
      radiobutton .dumphtml.opt3.sel1 -text "Write titles only" -variable dumphtml_type -value 1 -command HtmlDump_ToggleType
      radiobutton .dumphtml.opt3.sel2 -text "Write descriptions only" -variable dumphtml_type -value 2 -command HtmlDump_ToggleType
      pack .dumphtml.opt3.sel0 .dumphtml.opt3.sel1 .dumphtml.opt3.sel2 -side top -anchor nw
      pack .dumphtml.opt3 -side top -pady 5 -padx 10 -fill x

      frame .dumphtml.opt4 -borderwidth 1 -relief raised
      frame .dumphtml.opt4.row1
      checkbutton .dumphtml.opt4.row1.chk0 -text "Different columns than main window" -variable dumphtml_use_colsel -command HtmlDump_ToggleColsel
      pack .dumphtml.opt4.row1.chk0 -side left -anchor w
      button .dumphtml.opt4.row1.but0 -text "Configure" -command PopupHtmlColumnSelection
      pack .dumphtml.opt4.row1.but0 -side right -anchor e
      pack .dumphtml.opt4.row1 -side top -anchor nw -fill x
      checkbutton .dumphtml.opt4.chk1 -text "Add hyperlinks to titles" -variable dumphtml_hyperlinks
      pack .dumphtml.opt4.chk1 -side top -anchor nw
      checkbutton .dumphtml.opt4.chk2 -text "Apply text format options in user-defined columns" -variable dumphtml_text_fmt
      pack .dumphtml.opt4.chk2 -side top -anchor nw
      pack .dumphtml.opt4 -side top -pady 5 -padx 10 -fill x

      frame .dumphtml.cmd
      button .dumphtml.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Control menu) "Export as HTML"}
      pack .dumphtml.cmd.help -side left -padx 10
      button .dumphtml.cmd.dismiss -text "Dismiss" -width 5 -command {destroy .dumphtml}
      pack .dumphtml.cmd.dismiss -side left -padx 10
      button .dumphtml.cmd.write -text "Export" -width 5 -command HtmlDump_Start -default active
      pack .dumphtml.cmd.write -side left -padx 10
      pack .dumphtml.cmd -side top -pady 10

      HtmlDump_ToggleType
      HtmlDump_ToggleAppend
      HtmlDump_ToggleColsel

      bind .dumphtml <Key-F1> {PopupHelp $helpIndex(Control menu) "Export as HTML"}
      bind .dumphtml.cmd <Destroy> {+ set dumphtml_popup 0}
      focus .dumphtml.opt1.name.filename
   } else {
      raise .dumphtml
   }
}

# callback for "type" radiobuttons
proc HtmlDump_ToggleType {} {
   global dumphtml_type

   if {$dumphtml_type == 3} {
      # both titles and descriptions are written -> offer hyperlinking
      .dumphtml.opt4.chk1 configure -state normal
   } else {
      .dumphtml.opt4.chk1 configure -state disabled
   }

   if {($dumphtml_type & 1) != 0} {
      # title export enabled -> offer title column selection
      .dumphtml.opt4.row1.chk0 configure -state normal
      .dumphtml.opt4.row1.but0 configure -state normal
   } else {
      .dumphtml.opt4.row1.chk0 configure -state disabled
      .dumphtml.opt4.row1.but0 configure -state disabled
   }
}

# callback for "Append" checkbutton
proc HtmlDump_ToggleAppend {} {
   global dumphtml_file_append

   if $dumphtml_file_append {
      .dumphtml.opt1.sel1 configure -state disabled
   } else {
      .dumphtml.opt1.sel1 configure -state normal
   }
}

# callback for "different columns than main window" checkbutton
proc HtmlDump_ToggleColsel {} {
   global dumphtml_use_colsel dumphtml_colsel
   global pilistbox_cols
   global htmlcols_popup

   if $dumphtml_use_colsel {
      .dumphtml.opt4.row1.but0 configure -state normal
      # when this option is enabled the first time, the main window's column config is copied
      if {[llength $dumphtml_colsel] == 0} {
         set dumphtml_colsel $pilistbox_cols
      }
   } else {
      .dumphtml.opt4.row1.but0 configure -state disable
      if $htmlcols_popup {
         # TODO ask if the changes should be saved
         destroy .htmlcols
      }
   }
}

# callback for "column configuration" button: open dialog
proc PopupHtmlColumnSelection {} {
   global dumphtml_colsel
   global htmlcols_ailist htmlcols_selist colsel_names
   global htmlcols_popup
   global text_fg text_bg

   if {$htmlcols_popup == 0} {
      CreateTransientPopup .htmlcols "HTML Export Columns Selection"
      set htmlcols_popup 1

      FillColumnSelectionDialog .htmlcols.all htmlcols_ailist htmlcols_selist $dumphtml_colsel 1

      message .htmlcols.intromsg -text "Select which types of attributes you want to have\ndisplayed for each TV programme in the HTML document:" \
                                 -aspect 2500 -borderwidth 2 -relief groove \
                                 -foreground $text_fg -background $text_bg -pady 5
      pack .htmlcols.intromsg -side top -fill x -expand 1

      frame .htmlcols.all
      SelBoxCreate .htmlcols.all htmlcols_ailist htmlcols_selist colsel_names 12 0
      .htmlcols.all.ai.ailist configure -width 20
      .htmlcols.all.sel.selist configure -width 20

      button .htmlcols.all.cmd.help -text "Help" -width 7 -command {PopupHelp $helpIndex(Control menu) "Export as HTML"}
      button .htmlcols.all.cmd.abort -text "Abort" -width 7 -command {destroy .htmlcols}
      button .htmlcols.all.cmd.ok -text "Ok" -width 7 -command HtmlDump_ApplyColumnSelection
      pack .htmlcols.all.cmd.help .htmlcols.all.cmd.abort .htmlcols.all.cmd.ok -side bottom -anchor sw
      pack .htmlcols.all -side top

      bind  .htmlcols <Key-F1> {PopupHelp $helpIndex(Control menu) "Export as HTML"}
      bind  .htmlcols.all.cmd <Destroy> {+ set htmlcols_popup 0}
      bind  .htmlcols.all.cmd.ok <Return> {tkButtonInvoke .htmlcols.all.cmd.ok}
      bind  .htmlcols.all.cmd.ok <Escape> {tkButtonInvoke .htmlcols.all.cmd.abort}
      focus .htmlcols.all.cmd.ok
   } else {
      raise .htmlcols
   }
}

proc HtmlDump_ApplyColumnSelection {} {
   global dumphtml_colsel htmlcols_selist

   set dumphtml_colsel $htmlcols_selist
   destroy .htmlcols
}

# callback for "Export" button
proc HtmlDump_Start {} {
   global dumphtml_filename dumphtml_type dumphtml_file_append dumphtml_file_overwrite
   global dumphtml_sel dumphtml_hyperlinks dumphtml_text_fmt
   global dumphtml_use_colsel dumphtml_colsel dumphtml_sel_count
   global pilistbox_cols

   if {$dumphtml_sel == 1} {
      if {([string is integer -strict $dumphtml_sel_count] == 0) || \
          ($dumphtml_sel_count <= 0)} {
         # max count is not a valid number
         tk_messageBox -type ok -default ok -icon error -parent .dumphtml -message "The maximum programme count is not a valid number or <= zero (it must contain digits 0-9 only)"
         return
      }
   }

   # select a column to use for hyperlinks
   set hyperCol [lsearch -exact $pilistbox_cols title]
   if {$hyperCol == -1} {
      set idx 0
      # title column not included as such -> search in user-defined columns for title attribute
      foreach col $pilistbox_cols  {
         if {[regsub {^user_def_} $col {} sc_tag] &&
             [UserCols_IsTitleColumn $sc_tag]} {
            set hyperCol $idx
            break
         }
         incr idx
      }
      if {$hyperCol == -1} {
         set hyperCol 0
      }
   }

   if {[string length $dumphtml_filename] > 0} {
      if [DlgDump_CheckOutputFile .dumphtml dumphtml_filename .html 0 \
                  [expr $dumphtml_file_append || $dumphtml_file_overwrite]] {

         if $dumphtml_use_colsel { set colsel $dumphtml_colsel} \
         else                    { set colsel $pilistbox_cols}

         C_DumpHtml $dumphtml_filename \
                    [expr ($dumphtml_type & 1) != 0] [expr ($dumphtml_type & 2) != 0] \
                    $dumphtml_file_append \
                    [expr $dumphtml_sel == 2] \
                    [expr ($dumphtml_sel == 2) ? 1 : ((($dumphtml_sel == 0) ? -1 : $dumphtml_sel_count))] \
                    [expr ($dumphtml_hyperlinks && ($dumphtml_type == 3)) ? $hyperCol : -1] \
                    $dumphtml_text_fmt \
                    $colsel

         # save the settings to the rc/ini file
         UpdateRcFile
      }

   } else {
      # the file name entry field is still empty -> abort (stdout not allowed here)
      tk_messageBox -type ok -default ok -icon error -parent .dumphtml -message "Please enter a file name."
   }
}


##  --------------------------------------------------------------------------
##  Create dialog to export the database into TAB-separated text file
##
proc PopupDumpXml {} {
   global dumpxml_popup dumpxml_filename dumpxml_format
   global font_fixed fileImage

   if {$dumpxml_popup == 0} {
      CreateTransientPopup .dumpxml "Export database into XMLTV file"
      set dumpxml_popup 1

      frame  .dumpxml.all
      frame  .dumpxml.all.name
      label  .dumpxml.all.name.prompt -text "File name:"
      pack   .dumpxml.all.name.prompt -side left
      entry  .dumpxml.all.name.filename -textvariable dumpxml_filename -font $font_fixed -width 30
      pack   .dumpxml.all.name.filename -side left -padx 5
      bind   .dumpxml.all.name.filename <Enter> {SelectTextOnFocus %W}
      bind   .dumpxml.all.name.filename <Return> {tkButtonInvoke .dumpxml.all.cmd.ok}
      bind   .dumpxml.all.name.filename <Escape> {tkButtonInvoke .dumpxml.all.cmd.abort}
      button .dumpxml.all.name.dlgbut -image $fileImage -command {
         set tmp [tk_getSaveFile -parent .dumpxml \
                     -initialfile [file tail $dumpxml_filename] \
                     -initialdir [file dirname $dumpxml_filename] \
                     -defaultextension .xml -filetypes {{XML {*.xml}} {all {*.*}}}]
         if {[string length $tmp] > 0} {
            set dumpxml_filename $tmp
         }
         unset tmp
      }
      pack   .dumpxml.all.name.dlgbut -side left -padx 5
      pack   .dumpxml.all.name -side top -pady 10

      frame       .dumpxml.all.xml_fmt
      radiobutton .dumpxml.all.xml_fmt.fmt0 -text "Start/stop times in UTC" -variable dumpxml_format -value $::xmltv_dtd_5_gmt
      radiobutton .dumpxml.all.xml_fmt.fmt1 -text "Start/stop times in local time" -variable dumpxml_format -value $::xmltv_dtd_5_ltz
      pack   .dumpxml.all.xml_fmt.fmt0 .dumpxml.all.xml_fmt.fmt1 -side top -anchor w
      pack   .dumpxml.all.xml_fmt -side top -pady 10 -anchor w

      frame  .dumpxml.all.cmd
      button .dumpxml.all.cmd.help -text "Help" -width 5 -command {PopupHelp $helpIndex(Control menu) "Export as XMLTV"}
      pack   .dumpxml.all.cmd.help -side left -padx 10
      button .dumpxml.all.cmd.abort -text "Abort" -width 5 -command {destroy .dumpxml}
      pack   .dumpxml.all.cmd.abort -side left -padx 10
      button .dumpxml.all.cmd.ok -text "Ok" -width 5 -command DoDbXmlDump -default active
      pack   .dumpxml.all.cmd.ok -side left -padx 10
      pack   .dumpxml.all.cmd -side top

      pack   .dumpxml.all -padx 10 -pady 10
      bind   .dumpxml.all <Destroy> {+ set dumpxml_popup 0}
      bind   .dumpxml <Key-F1> {PopupHelp $helpIndex(Control menu) "Export as XMLTV"}
      focus  .dumpxml.all.name.filename
   } else {
      raise .dumpxml
   }
}

# callback for Ok button
proc DoDbXmlDump {} {
   global dumpxml_popup dumpxml_filename dumpxml_format

   if [DlgDump_CheckOutputFile .dumpxml dumpxml_filename .xml 0 0] {

      C_DumpXml $dumpxml_filename

      # save the settings to the rc/ini file
      UpdateRcFile

      destroy .dumpxml
   }
}

##  --------------------------------------------------------------------------
##  Helper proc for output file handling
##  - error message if a file name was specified (unless stdout is allowed)
##  - append default extension to file name, if none given
##  - warn if the output file already exists (unless suppressed by option)
##
proc DlgDump_CheckOutputFile {wdlg var_filename ext allow_stdout overwrite} {
   upvar $var_filename filename

   set ok 0

   # check if a file name was specified
   if {[string length $filename] > 0} {

      #  append default extension to file name, if none given
      if {([string length $ext] > 0) && (! [string match {*.*} $filename])} {
         append filename $ext
      }

      # check if the output file already exists (unless suppressed by option)
      if {[file exists $filename] && !$overwrite} {

         set answer [tk_messageBox -type okcancel -default ok -icon warning -parent $wdlg \
                                   -message "File '$filename' already exists.\nOverwrite it?"]
         if {[string compare $answer "ok"] == 0} {
            set ok 1
         }
      } else {
         set ok 1
      }
   } else {
      if {! $allow_stdout} {
         # the file name entry field is still empty -> abort (stdout not allowed here)
         tk_messageBox -type ok -default ok -icon error -parent $wdlg \
                       -message "Please enter a file name."
      } else {
         # empty file name stands for "stdout" in this mode
         set ok 1
      }
   }

   return $ok
}

