#
#  Configuration dialog for user-defined entries in the context menu
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
#    Implements a configuration dialog that allows to manage user-defined
#    entries in the context menu in the PI listbox.
#
#  Author: Tom Zoerner
#
#  $Id: shortcuts.tcl,v 1.11 2003/03/19 16:19:16 tom Exp tom $
#
set fsc_name_idx 0
set fsc_mask_idx 1
set fsc_filt_idx 2
set fsc_inv_idx  3
set fsc_logi_idx 4
set fsc_hide_idx 5

##
##  Predefined filter shortcuts
##
proc PreloadShortcuts {} {
   global shortcuts shortcut_order
   global user_language

   if {([string compare -nocase -length 2 $user_language de] == 0) || \
       ([string compare -nocase -length 3 $user_language ger] == 0)} {
      # Germany
      set shortcuts(0)  {Spielfilme themes {theme_class1 16} {} merge 0}
      set shortcuts(1)  {Sport themes {theme_class1 64} {} merge 0}
      set shortcuts(2)  {Serien themes {theme_class1 128} {} merge 0}
      set shortcuts(3)  {Kinder themes {theme_class1 80} {} merge 0}
      set shortcuts(4)  {Shows themes {theme_class1 48} {} merge 0}
      set shortcuts(5)  {News themes {theme_class1 32} {} merge 0}
      set shortcuts(6)  {Sozial themes {theme_class1 37} {} merge 0}
      set shortcuts(7)  {Wissen themes {theme_class1 86} {} merge 0}
      set shortcuts(8)  {Hobby themes {theme_class1 52} {} merge 0}
      set shortcuts(9)  {Musik themes {theme_class1 96} {} merge 0}
      set shortcuts(10) {Kultur themes {theme_class1 112} {} merge 0}
      set shortcuts(11) {Adult themes {theme_class1 24} {} merge 0}
      set shortcut_order [lsort -integer [array names shortcuts]]
   } elseif {[string compare -nocase -length 2 $user_language fr] == 0} {
      # France
      set shortcuts(0)  {Films themes {theme_class1 {16 24}} {} merge 0}
      set shortcuts(1)  {{Films de + 2heures} {themes dursel} {theme_class1 {16 24} dursel {120 1435}} {} merge 0}
      set shortcuts(2)  {{Films pour Adulte} themes {theme_class1 24} {} merge 0}
      set shortcuts(3)  {Sports themes {theme_class1 64} {} merge 0}
      set shortcuts(4)  {Jeunesse themes {theme_class1 80} {} merge 0}
      set shortcuts(5)  {Spectacle/Jeu themes {theme_class1 48} {} merge 0}
      set shortcuts(6)  {Journal themes {theme_class1 32} {} merge 0}
      set shortcuts(7)  {Documentaire themes {theme_class1 38} {} merge 0}
      set shortcuts(8)  {Musique themes {theme_class1 96} {} merge 0}
      set shortcuts(9)  {Religion themes {theme_class1 112} {} merge 0}
      set shortcuts(10) {Variétés themes {theme_class1 50} {} merge 0}
      set shortcuts(11) {Météo substr {substr {{1 0 0 0 Météo}}} {} merge 0}
      set shortcuts(12) {{12 ans et +} parental {parental 5} parental merge 0}
      set shortcuts(13) {{16 ans et +} parental {parental 7} parental merge 0}
      set shortcuts(14) {{45 minutes et +} dursel {dursel {45 1435}} {} merge 0}
      set shortcuts(15) {{3h00 et +} dursel {dursel {180 1435}} {} merge 0}
      set shortcut_order [lsort -integer [array names shortcuts]]
   } else {
      # generic
      set shortcuts(0)  {movies themes {theme_class1 16} {} merge 0}
      set shortcuts(1)  {sports themes {theme_class1 64} {} merge 0}
      set shortcuts(2)  {series themes {theme_class1 128} {} merge 0}
      set shortcuts(3)  {kids themes {theme_class1 80} {} merge 0}
      set shortcuts(4)  {shows themes {theme_class1 48} {} merge 0}
      set shortcuts(5)  {news themes {theme_class1 32} {} merge 0}
      set shortcuts(6)  {social themes {theme_class1 37} {} merge 0}
      set shortcuts(7)  {science themes {theme_class1 86} {} merge 0}
      set shortcuts(8)  {hobbies themes {theme_class1 52} {} merge 0}
      set shortcuts(9)  {music themes {theme_class1 96} {} merge 0}
      set shortcuts(10) {culture themes {theme_class1 112} {} merge 0}
      set shortcuts(11) {adult themes {theme_class1 24} {} merge 0}
      set shortcut_order [lsort -integer [array names shortcuts]]
   }
}

##
##  Generate a new, unique tag
##
proc GenerateShortcutTag {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global shortcuts shortcut_order

   set tag [clock seconds]
   foreach stag [array names shortcuts] {
      if {$tag <= $stag} {
         set tag [expr $stag + 1]
      }
   }
   return $tag
}

##  --------------------------------------------------------------------------
##  Check if shortcut should be deselected after manual filter modification
##
proc CheckShortcutDeselection {} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global shortcuts shortcut_order
   global parental_rating editorial_rating
   global theme_sel theme_class_sel current_theme_class theme_class_count
   global sortcrit_class sortcrit_class_sel
   global series_sel
   global feature_class_count feature_class_mask feature_class_value
   global progidx_first progidx_last filter_progidx
   global substr_stack
   global timsel_enabled timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop timsel_nodate
   global dursel_min dursel_max
   global vpspdc_filt
   global filter_invert
   global fsc_prevselection

   foreach sc_index [.all.shortcuts.list curselection] {
      set sc_tag [lindex $shortcut_order $sc_index]
      set undo 0
      foreach {ident valist} [lindex $shortcuts($sc_tag) $fsc_filt_idx] {
         switch -glob $ident {
            theme_class* {
               scan $ident "theme_class%d" class
               if {$class == $current_theme_class} {
                  foreach theme $valist {
                     if {![info exists theme_sel($theme)] || ($theme_sel($theme) == 0)} {
                        set undo 1
                        break
                     }
                  }
               } elseif {[info exists theme_class_sel($class)]} {
                  foreach theme $valist {
                     if {[lsearch -exact $theme_class_sel($class) $theme] == -1} {
                        set undo 1
                        break
                     }
                  }
               } else {
                  set undo 1
               }
            }
            sortcrit_class* {
               scan $ident "sortcrit_class%d" class
               if {$class == $current_sortcrit_class} {
                  foreach sortcrit $valist {
                     if {![info exists sortcrit_sel($sortcrit)] || ($sortcrit_sel($sortcrit) == 0)} {
                        set undo 1
                        break
                     }
                  }
               } elseif {[info exists sortcrit_class_sel($class)]} {
                  foreach sortcrit $valist {
                     if {[lsearch -exact $sortcrit_class_sel($class) $sortcrit] == -1} {
                        set undo 1
                        break
                     }
                  }
               } else {
                  set undo 1
               }
            }
            series {
               foreach series $valist {
                  if {![info exists series_sel($series)] || ($series_sel($series) == 0)} {
                     set undo 1
                     break
                  }
               }
            }
            netwops {
               if {[.all.shortcuts.netwops selection includes 0]} {
                  set undo 1
               } else {
                  # check if all netwops from the shortcut are still enabled in the filter
                  set selcnis [C_GetNetwopFilterList]
                  foreach cni $valist {
                     if {[lsearch -exact $selcnis $cni] == -1} {
                        set undo 1
                        break
                     }
                  }
               }
            }
            features {
               foreach {mask value} $valist {
                  # search for a matching mask/value pair
                  for {set class 1} {$class <= $feature_class_count} {incr class} {
                     if {($feature_class_mask($class) & $mask) == $mask} {
                        if {($feature_class_value($class) & $mask) == $value} {
                           break
                        }
                     }
                  }
                  if {$class > $feature_class_count} {
                     set undo 1
                     break
                  }
               }
            }
            parental {
               set undo [expr ($valist < $parental_rating) || ($parental_rating == 0)]
            }
            editorial {
               set undo [expr ($valist > $editorial_rating) || ($editorial_rating == 0)]
            }
            progidx {
               set undo [expr ($filter_progidx == 0) || \
                              ($progidx_first > [lindex $valist 0]) || \
                              ($progidx_last < [lindex $valist 1]) ]
            }
            timsel {
               set undo [expr ($timsel_enabled  != 1) || \
                              ($timsel_relative != [lindex $valist 0]) || \
                              ($timsel_absstop  != [lindex $valist 1]) || \
                              ($timsel_start    != [lindex $valist 2]) || \
                              ($timsel_stop     != [lindex $valist 3]) || \
                              ($timsel_nodate ? \
                                ($timsel_date   != -1) : \
                                ($timsel_date   != [lindex $valist 4])) ]
            }
            dursel {
               set undo [expr ($dursel_min != [lindex $valist 0]) || \
                              ($dursel_max != [lindex $valist 1]) ]
            }
            vps_pdc {
               set undo [expr $vpspdc_filt != [lindex $valist 0]]
            }
            substr {
               # check if all substr param sets are still active, i.e. on the stack
               foreach parlist $valist {
                  set found 0
                  foreach stit $substr_stack {
                     if {$parlist == $stit} {
                        set found 1
                        break
                     }
                  }
                  if {$found == 0} {
                     set undo 1
                     break
                  }
               }
            }
         }
         if $undo break
      }

      # check if any filters in the mask are inverted
      set invl [lindex $shortcuts($sc_tag) $fsc_inv_idx]
      foreach {ident valist} [lindex $shortcuts($sc_tag) $fsc_mask_idx] {
         if {[lsearch -exact $invl $ident] == -1} {
            if {[info exists filter_invert($ident)] && ($filter_invert($ident) != 0)} {
               set undo 1
               break
            }
         } else {
            if {![info exists filter_invert($ident)] || ($filter_invert($ident) == 0)} {
               set undo 1
               break
            }
         }
      }

      # check if any filter inversions were undone
      foreach ident $invl {
         if {$filter_invert($ident) == 0} {
            set undo 1
            break
         }
      }

      if $undo {
         # clear the selection in the main window's shortcut listbox
         .all.shortcuts.list selection clear $sc_index

         # remove the shortcut from the list of selected shortcuts
         if {[info exists fsc_prevselection]} {
            set selidx [lsearch -exact $fsc_prevselection $sc_tag]
            if {$selidx != -1} {
               set fsc_prevselection [lreplace $fsc_prevselection $selidx $selidx]
            }
         }
      }
   }
}

##  --------------------------------------------------------------------------
##  Set all filters in a shortcut
##  - differences to the normal shortcut selection function:
##    + menu variables are not affected
##    + assumes that all filters in the context were reset before
##    + nor OR-ing between multiple shortcuts possible
##  - MUST NOT be used for the browser filter context; before this function
##    is called a different context must be selected as target
##
proc SelectSingleShortcut {sc_tag} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global shortcuts

   foreach {ident valist} [lindex $shortcuts($sc_tag) $fsc_filt_idx] {
      switch -glob $ident {
         theme_class*   {
            scan $ident "theme_class%d" class
            C_SelectThemes $class $valist
         }
         sortcrit_class*   {
            scan $ident "sortcrit_class%d" class
            C_SelectSortCrits $class $valist
         }
         features {
            C_SelectFeatures $valist
         }
         parental {
            C_SelectParentalRating $valist
         }
         editorial {
            C_SelectEditorialRating $valist
         }
         substr {
            C_SelectSubStr $valist
         }
         progidx {
            set progidx_first [lindex $valist 0]
            set progidx_last  [lindex $valist 1]
            C_SelectProgIdx $progidx_first $progidx_last
         }
         timsel {
            set timsel_relative [lindex $valist 0]
            set timsel_absstop  [lindex $valist 1]
            set timsel_start    [lindex $valist 2]
            set timsel_stop     [lindex $valist 3]
            set timsel_date     [lindex $valist 4]
            set timsel_nodate   [expr $timsel_date < 0]
            C_SelectStartTime $timsel_relative $timsel_absstop $timsel_nodate \
                              $timsel_start $timsel_stop $timsel_date
         }
         dursel {
            set dursel_min [lindex $valist 0]
            set dursel_max [lindex $valist 1]
            C_SelectMinMaxDuration $dursel_min $dursel_max
         }
         vps_pdc {
            set vpspdc_filt [lindex $valist 0]
         }
         series {
            foreach series $valist {
               C_SelectSeries $series 1
            }
         }
         netwops {
            set all {}
            set index 0
            foreach cni [C_GetAiNetwopList 0 {}] {
               if {[lsearch -exact $valist $cni] != -1} {
                  lappend all $index
               }
               incr index
            }
            C_SelectNetwops $all
         }
         default {
         }
      }
   }

   # process list of inverted filter types
   C_InvertFilter [lindex $shortcuts($sc_tag) $fsc_inv_idx]
}

##  --------------------------------------------------------------------------
##  Callback for button-release on shortcuts listbox
##
proc SelectShortcuts {sc_tag_list shortcuts_arr} {
   global fsc_mask_idx fsc_filt_idx fsc_name_idx fsc_inv_idx fsc_logi_idx fsc_hide_idx
   global parental_rating editorial_rating
   global theme_sel theme_class_sel current_theme_class theme_class_count
   global sortcrit_class sortcrit_class_sel
   global series_sel
   global feature_class_count feature_class_mask feature_class_value
   global progidx_first progidx_last filter_progidx
   global substr_stack substr_pattern substr_grep_title substr_grep_descr
   global substr_match_case substr_match_full
   global timsel_enabled timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop timsel_nodate
   global dursel_min dursel_max dursel_minstr dursel_maxstr
   global vpspdc_filt
   global fsc_prevselection
   global filter_invert

   upvar $shortcuts_arr shortcuts

   # determine which shortcuts are no longer selected
   if {[info exists fsc_prevselection]} {
      foreach sc_tag $fsc_prevselection {
         if {[info exists shortcuts($sc_tag)]} {
            set deleted($sc_tag) 1
         }
      }
      foreach sc_tag $sc_tag_list {
         if {[info exists deleted($sc_tag)]} {
            unset deleted($sc_tag)
         }
      }
   }
   set fsc_prevselection $sc_tag_list

   # reset all filters in the combined mask
   foreach sc_tag $sc_tag_list {
      foreach type [lindex $shortcuts($sc_tag) $fsc_mask_idx] {
         # reset filter menu state; remember which types were reset
         switch -exact $type {
            themes     {ResetThemes;          set reset(themes) 1}
            sortcrits  {ResetSortCrits;       set reset(sortcrits) 1}
            features   {ResetFeatures;        set reset(features) 1}
            series     {ResetSeries;          set reset(series) 1}
            parental   {ResetParentalRating;  set reset(parental) 1}
            editorial  {ResetEditorialRating; set reset(editorial) 1}
            progidx    {ResetProgIdx;         set reset(progidx) 1}
            timsel     {ResetTimSel;          set reset(timsel) 1}
            dursel     {ResetMinMaxDuration;  set reset(dursel) 1}
            vps_pdc    {ResetVpsPdcFilt;      set reset(vps_pdc) 1}
            substr     {ResetSubstr;          set reset(substr) 1}
            netwops    {ResetNetwops;         set reset(netwops) 1}
            invert_all {array unset filter_invert all}
         }
      }
   }

   # disable masked filters in the filter context
   foreach type [array names reset] {
      C_ResetFilter $type
   }

   # clear all filters of deselected shortcuts
   foreach sc_tag [array names deleted] {
      foreach {ident valist} [lindex $shortcuts($sc_tag) $fsc_filt_idx] {
         if {![info exists reset($ident)]} {

            array unset filter_invert $ident

            switch -glob $ident {
               theme_class*   {
                  scan $ident "theme_class%d" class
                  if {[info exists tcdesel($class)]} {
                     set tcdesel($class) [concat $tcdesel($class) $valist]
                  } else {
                     set tcdesel($class) $valist
                  }
               }
               sortcrit_class*   {
                  scan $ident "sortcrit_class%d" class
                  foreach item $valist {
                     set index [lsearch -exact $sortcrit_class_sel($class) $item]
                     if {$index != -1} {
                        set sortcrit_class_sel($class) [lreplace $sortcrit_class_sel($class) $index $index]
                     }
                  }
               }
               features {
                  foreach {mask value} $valist {
                     for {set class 1} {$class <= $feature_class_count} {incr class} {
                        if {($feature_class_mask($class) == $mask) && ($feature_class_value($class) == $value)} {
                           set feature_class_mask($class)  0
                           set feature_class_value($class) 0
                        }
                     }
                  }
               }
               parental {
                  set parental_rating 0
                  C_SelectParentalRating 0
               }
               editorial {
                  set editorial_rating 0
                  C_SelectEditorialRating 0
               }
               substr {
                  set substr_stack {}
                  set substr_pattern {}
                  C_SelectSubStr {}
               }
               progidx {
                  set filter_progidx 0
                  C_SelectProgIdx
               }
               timsel {
                  set timsel_enabled 0
                  C_SelectStartTime
               }
               dursel {
                  set dursel_min 0
                  set dursel_max 0
                  C_SelectMinMaxDuration 0 0
               }
               vps_pdc {
                  set vpspdc_filt 0
                  C_SelectVpsPdcFilter $vpspdc_filt
               }
               series {
                  foreach index $valist {
                     set series_sel($index) 0
                     C_SelectSeries $index 0
                  }
               }
               netwops {
                  if {[info exists netdesel]} {
                     set netdesel [concat $netdesel $valist]
                  } else {
                     set netdesel $valist
                  }
               }
               default {
               }
            }
         }
      }

      if {[lsearch -exact [lindex $shortcuts($sc_tag) $fsc_inv_idx] all] != -1} {
         array unset filter_invert all
      }
   }

   # set all filters in all selected shortcuts (merge)
   foreach sc_tag $sc_tag_list {
      foreach {ident valist} [lindex $shortcuts($sc_tag) $fsc_filt_idx] {
         switch -glob $ident {
            theme_class*   {
               scan $ident "theme_class%d" class
               if {[info exists tcsel($class)]} {
                  set tcsel($class) [concat $tcsel($class) $valist]
               } else {
                  set tcsel($class) $valist
               }
            }
            sortcrit_class*   {
               scan $ident "sortcrit_class%d" class
               set sortcrit_class_sel($class) [lsort -int [concat $sortcrit_class_sel($class) $valist]]
            }
            features {
               foreach {mask value} $valist {
                  # search the first unused class; if none found, drop the filter
                  for {set class 1} {$class <= $feature_class_count} {incr class} {
                     if {$feature_class_mask($class) == 0} break
                  }
                  if {$class <= $feature_class_count} {
                     set feature_class_mask($class)  $mask
                     set feature_class_value($class) $value
                  }
               }
            }
            parental {
               set rating [lindex $valist 0]
               if {($rating < $parental_rating) || ($parental_rating == 0)} {
                  set parental_rating $rating
                  C_SelectParentalRating $parental_rating
               }
            }
            editorial {
               set rating [lindex $valist 0]
               if {($rating > $editorial_rating) || ($editorial_rating == 0)} {
                  set editorial_rating [lindex $valist 0]
                  C_SelectEditorialRating $editorial_rating
               }
            }
            substr {
               foreach parlist $valist {
                  set substr_pattern     [lindex $parlist 0]
                  set substr_grep_title  [lindex $parlist 1]
                  set substr_grep_descr  [lindex $parlist 2]
                  set substr_match_case  [lindex $parlist 3]
                  set substr_match_full  [lindex $parlist 4]

                  # remove identical items from the substr stack
                  set idx 0
                  foreach item $substr_stack {
                     if {$item == $parlist} {
                        set substr_stack [lreplace $substr_stack $idx $idx]
                        break
                     }
                     incr idx
                  }

                  # push set onto the substr stack; new stack is applied at the end
                  set substr_stack [linsert $substr_stack 0 $parlist]
                  set upd_substr 1
               }
            }
            progidx {
               if {($progidx_first > [lindex $valist 0]) || ($filter_progidx == 0)} {
                  set progidx_first [lindex $valist 0]
               }
               if {($progidx_last < [lindex $valist 1]) || ($filter_progidx == 0)} {
                  set progidx_last  [lindex $valist 1]
               }
               C_SelectProgIdx $progidx_first $progidx_last
               UpdateProgIdxMenuState
            }
            timsel {
               set timsel_enabled  1
               set timsel_relative [lindex $valist 0]
               set timsel_absstop  [lindex $valist 1]
               set timsel_start    [lindex $valist 2]
               set timsel_stop     [lindex $valist 3]
               set timsel_date     [lindex $valist 4]
               set timsel_nodate   [expr $timsel_date < 0]
               C_SelectStartTime $timsel_relative $timsel_absstop $timsel_nodate \
                                 $timsel_start $timsel_stop $timsel_date
            }
            dursel {
               if {$dursel_max == 0} {
                  set dursel_min [lindex $valist 0]
                  set dursel_max [lindex $valist 1]
               } else {
                  if {[lindex $valist 0] < $dursel_min} {set dursel_min [lindex $valist 0]}
                  if {[lindex $valist 1] > $dursel_max} {set dursel_max [lindex $valist 1]}
               }
               C_SelectMinMaxDuration $dursel_min $dursel_max
            }
            vps_pdc {
               set vpspdc_filt [lindex $valist 0]
               C_SelectVpsPdcFilter $vpspdc_filt
            }
            series {
               foreach series $valist {
                  set series_sel($series) 1
                  C_SelectSeries $series 1
               }
            }
            netwops {
               if {[info exists netsel]} {
                  set netsel [concat $netsel $valist]
               } else {
                  set netsel $valist
               }
            }
            default {
            }
         }
      }

      foreach ident [lindex $shortcuts($sc_tag) $fsc_inv_idx] {
         set filter_invert($ident) 1
      }
   }

   # set the collected themes filter
   for {set class 1} {$class < $theme_class_count} {incr class} {
      if {$class == $current_theme_class} {
         if {[info exists tcdesel($class)]} {
            foreach theme $tcdesel($class) {
               set theme_sel($theme) 0
            }
         }
         if {[info exists tcsel($class)]} {
            foreach theme $tcsel($class) {
               set theme_sel($theme) 1
            }
         }
         set theme_class_sel($class) {}
         foreach {index value} [array get theme_sel] {
            if {$value != 0} {
               set theme_class_sel($class) [concat $theme_class_sel($class) $index]
            }
         }
      } else {
         if {[info exists tcsel($class)]} {
            set theme_class_sel($class) $tcsel($class)
            # XXX TODO: substract tcdesel
         }
      }
      C_SelectThemes $class $theme_class_sel($class)
   }

   # unset/set the collected netwop filters
   if {[info exists netdesel] || [info exists netsel]} {
      # get previous netwop filter state
      if {[.all.shortcuts.netwops selection includes 0]} {
         set selcnis {}
      } else {
         set selcnis [C_GetNetwopFilterList]
      }
      # remove deselected CNIs from filter state
      if {[info exists netdesel]} {
         foreach netwop $netdesel {
            set index [lsearch -exact $selcnis $netwop]
            if {$index >= 0} {
               set selcnis [lreplace $selcnis $index $index]
            }
         }
      }
      # append newly selected CNIs
      if {[info exists netsel]} {
         set selcnis [concat $selcnis $netsel]
      }
      # convert CNIs to netwop indices
      set all {}
      set index 0
      foreach cni [C_GetAiNetwopList 0 {}] {
         if {[lsearch -exact $selcnis $cni] != -1} {
            lappend all $index
         }
         incr index
      }
      # set the new filter and menu state
      C_SelectNetwops $all
      UpdateNetwopMenuState $all
   }

   # set or unset the sortcrit filters
   for {set class 1} {$class < $theme_class_count} {incr class} {
      C_SelectSortCrits $class $sortcrit_class_sel($class)
   }

   # set the collected feature filters
   set all {}
   for {set class 1} {$class <= $feature_class_count} {incr class} {
      if {[expr $feature_class_mask($class) != 0]} {
         lappend all $feature_class_mask($class) $feature_class_value($class)
      }
   }
   UpdateFeatureMenuState
   C_SelectFeatures $all

   # disable series filter when all deselected
   if {[info exists series_sel]} {
      set clearSeries 1
      foreach index [array names series_sel] {
         if {$series_sel($index) == 1} {
            set clearSeries 0
            break
         }
      }
      if {$clearSeries} {
         C_ResetFilter series
      }
   }

   # update the sub-string text filter
   if [info exists upd_substr] {
      C_SelectSubStr $substr_stack
   }

   # update the duration filter dialog
   set dursel_minstr [Motd2HHMM $dursel_min]
   set dursel_maxstr [Motd2HHMM $dursel_max]

   # update the invert state
   set all {}
   foreach {key val} [array get filter_invert] {
      if $val {
         lappend all $key
      }
   }
   C_InvertFilter $all

   # finally display the PI selected by the new filter setting
   C_PiBox_Refresh
}

##
##  Toggle shortcut between active / inactive
##  - used for key bindings on 0-9 digit keys in main window
##
proc ToggleShortcut {index} {
   if {! [.all.shortcuts.list selection includes $index]} {
      # item is not yet selected -> select it
      .all.shortcuts.list selection set $index
   } else {
      # deselect the item
      .all.shortcuts.list selection clear $index
   }
   # update filter settings
   InvokeSelectedShortcuts
}

##
##  Enable exactly one single shortcut out of a given list
##  - if another shortcut in the list is currently selected, it's deselected
##  - if the single shortcut is already selected, it's deselected
##
proc SelectShortcutFromTagList {tag_list sc_tag} {
   global shortcuts shortcut_order
   global fsc_prevselection

   if [info exists fsc_prevselection] {
      # check if the given shortcut is already selected
      set undo [expr [lsearch -exact $fsc_prevselection $sc_tag] != -1]

      set sc_tag_list {}
      foreach prev_tag $fsc_prevselection {
         if {[lsearch -exact $tag_list $prev_tag] == -1} {
            lappend sc_tag_list $prev_tag
         }
      }
      if {$undo == 0} {
         lappend sc_tag_list $sc_tag
      }

   } else {
      # no shortcuts selected previously -> select the new shortcut
      set sc_tag_list $sc_tag
      set undo 0
   }

   set idx [lsearch -exact $shortcut_order $sc_tag]
   if {$idx != -1} {
      if {$undo == 0} {
         .all.shortcuts.list selection set $idx
      } else {
         .all.shortcuts.list selection clear $idx
      }
   }

   SelectShortcuts $sc_tag_list shortcuts
}

##
##  Toggle the shortcut given by the tag on/off
##  - all other shortcuts remain unaffected
##
#proc ToggleShortcutByTag {sc_tag} {
#   global shortcuts shortcut_order
#   global fsc_prevselection
#
#   if [info exists fsc_prevselection] {
#      set idx [lsearch -exact $fsc_prevselection $sc_tag]
#      if {$idx == -1} {
#         # shortcut currently not selected -> append it to shortcut list
#         set sc_tag_list [concat $fsc_prevselection $sc_tag]
#         set undo 0
#      } else {
#         # shortcut already selected -> remove it from list
#         set sc_tag_list [lreplace $fsc_prevselection $idx $idx]
#         set undo 1
#      }
#   } else {
#      # no shortcuts selected previously -> select the new shortcut
#      set sc_tag_list $sc_tag
#      set undo 0
#   }
#
#   set idx [lsearch -exact $shortcut_order $sc_tag]
#   if {$idx != -1} {
#      if {$undo == 0} {
#         .all.shortcuts.list selection set $idx
#      } else {
#         .all.shortcuts.list selection clear $idx
#      }
#   }
#
#   SelectShortcuts $sc_tag_list shortcuts
#}

proc InvokeSelectedShortcuts {} {
   global shortcuts shortcut_order

   set sc_tag_list {}
   foreach index [.all.shortcuts.list curselection] {
      lappend sc_tag_list [lindex $shortcut_order $index]
   }
   SelectShortcuts $sc_tag_list shortcuts
}


#=LOAD=DescribeCurrentFilter
#=LOAD=ShortcutPrettyPrint
#=DYNAMIC=

##  --------------------------------------------------------------------------
##  Generate a list that describes all current filter settings
##
proc DescribeCurrentFilter {} {
   global feature_class_mask feature_class_value
   global parental_rating editorial_rating
   global theme_class_count current_theme_class theme_sel theme_class_sel
   global sortcrit_class sortcrit_class_sel
   global series_sel
   global feature_class_count current_feature_class
   global substr_stack
   global filter_progidx progidx_first progidx_last
   global timsel_enabled timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop timsel_nodate
   global dursel_min dursel_max
   global vpspdc_filt
   global filter_invert

   # save the setting of the current theme class into the array
   set all {}
   foreach {index value} [array get theme_sel] {
      if {[expr $value != 0]} {
         lappend all $index
      }
   }
   set theme_class_sel($current_theme_class) $all

   set all {}
   set mask {}

   # dump all theme classes
   for {set class 1} {$class <= $theme_class_count} {incr class} {
      if {[string length $theme_class_sel($class)] > 0} {
         lappend all "theme_class$class" $theme_class_sel($class)
         lappend mask themes
      }
   }

   # dump all sortcrit classes
   for {set class 1} {$class <= $theme_class_count} {incr class} {
      if {[string length $sortcrit_class_sel($class)] > 0} {
         lappend all "sortcrit_class$class" $sortcrit_class_sel($class)
         lappend mask sortcrits
      }
   }

   # dump feature filter state
   set temp {}
   for {set class 1} {$class <= $feature_class_count} {incr class} {
      if {[expr $feature_class_mask($class) != 0]} {
         lappend temp $feature_class_mask($class) $feature_class_value($class)
      }
   }
   if {[llength $temp] > 0} {
      lappend all "features" $temp
      lappend mask features
   }

   if {$parental_rating > 0} {
      lappend all "parental" $parental_rating
      lappend mask parental
   }
   if {$editorial_rating > 0} {
      lappend all "editorial" $editorial_rating
      lappend mask editorial
   }

   # dump text substring filter state
   if {[llength $substr_stack] > 0} {
      lappend all "substr" $substr_stack
      lappend mask substr
   }

   # dump program index filter state
   if {$filter_progidx > 0} {
      lappend all "progidx" [list $progidx_first $progidx_last]
      lappend mask progidx
   }

   # dump series array
   set temp {}
   foreach index [array names series_sel] {
      if {$series_sel($index) != 0} {
         lappend temp $index
      }
   }
   if {[string length $temp] > 0} {
      lappend all "series" $temp
      lappend mask series
   }

   # dump start time filter
   if {$timsel_enabled} {
      lappend all "timsel" [list $timsel_relative $timsel_absstop $timsel_start $timsel_stop \
                                 [expr $timsel_nodate ? -1 : $timsel_date]]
      lappend mask timsel
   }

   # dump duration filter
   if {$dursel_max > 0} {
      lappend all "dursel" [list $dursel_min $dursel_max]
      lappend mask dursel
   }

   # dump VPS/PDC filter
   if {$vpspdc_filt != 0} {
      lappend all "vps_pdc" $vpspdc_filt
      lappend mask "vps_pdc"
   }

   # dump CNIs of selected netwops
   # - Upload filters from filter context, so that netwops that are not in the current
   #   netwop bar can be saved too (might have been set through the Navigate menu).
   set temp [C_GetNetwopFilterList]
   if {[llength $temp] > 0} {
      lappend all "netwops" $temp
      lappend mask netwops
   }

   # dump invert flags, but only for filters which are actually used
   # (note: do not use mask or theme and sortcrit classes are missed)
   set inv {}
   foreach {invtyp value} [array get filter_invert] {
      if $value {
         for {set idx 0} {$idx < [llength $all]} {incr idx 2} {
            if {[string compare [lindex $all $idx] $invtyp] == 0} {
               lappend inv $invtyp
               break
            }
         }
      }
   }
   # special case "global invert": add flag to mask
   if {[info exists filter_invert(all)] && $filter_invert(all)} {
      lappend mask invert_all
      lappend inv all
   }

   return [list $mask $all $inv]
}

##  --------------------------------------------------------------------------
##  Generate a text that describes a given filter setting
##
proc ShortcutPrettyPrint {filter inv_list} {

   # fetch CNI list from AI block in database
   set netsel_ailist [C_GetAiNetwopList 0 netnames]
   ApplyUserNetnameCfg netnames

   set out {}

   if {[lsearch -exact $inv_list all] != -1} {
      append out "NOT matching: (global invert)\n"
   }

   foreach {ident valist} $filter {
      if {[lsearch -exact $inv_list $ident] != -1} {
         set not {NOT }
      } else {
         set not {}
      }

      switch -glob $ident {
         theme_class1 {
            foreach theme $valist {
               append out "${not}Theme: [C_GetPdcString $theme]\n"
            }
         }
         theme_class* {
            scan $ident "theme_class%d" class
            foreach theme $valist {
               append out "${not}Theme, class ${class}: [C_GetPdcString $theme]\n"
            }
         }
         sortcrit_class* {
            scan $ident "sortcrit_class%d" class
            foreach sortcrit $valist {
               append out "${not}Sort.crit., class ${class}: [format 0x%02X $sortcrit]\n"
            }
         }
         series {
            set titnet_list [C_GetSeriesTitles $valist]
            set title_list {}
            foreach {title netwop} $titnet_list {
               lappend title_list [list $title $netwop]
            }
            foreach title [lsort -command CompareSeriesMenuEntries $title_list] {
               append out "${not}Series: '[lindex $title 0]' on [lindex $title 1]\n"
            }
         }
         netwops {
            foreach cni $valist {
               if [info exists netnames($cni)] {
                  append out "${not}Network: $netnames($cni)\n"
               } else {
                  append out "${not}Network: CNI $cni\n"
               }
            }
         }
         features {
            set first_feature 1
            foreach {mask value} $valist {
               set str_feature {}
               set str_AND {}
               if {($mask & 0x03) == 0x03} {
                  append str_feature [switch -exact [expr $value & 0x03] {
                     0 {format "mono"}
                     1 {format "2-channel"}
                     2 {format "stereo"}
                     3 {format "surround"}
                  }] " sound\n"
                  set str_AND {   AND }
               }
               if {(($mask & 0x0c) == 0x0c) && (($value & 0x0c) == 0)} {
                  append str_feature "${str_AND}fullscreen picture\n"
                  set str_AND {   AND }
               } else {
                  if {$mask & 0x04} {
                     append str_feature "${str_AND}widescreen picture\n"
                     set str_AND {   AND }
                  }
                  if {$mask & 0x08} {
                     append str_feature "${str_AND}PAL+ picture\n"
                     set str_AND {   AND }
                  }
               }
               if {$mask & 0x10} {
                  if {$value & 0x10} {
                     append str_feature "${str_AND}digital\n"
                  } else {
                     append str_feature "${str_AND}analog\n"
                  }
                  set str_AND {   AND }
               }
               if {$mask & 0x20} {
                  if {$value & 0x20} {
                     append str_feature "${str_AND}encrypted\n"
                  } else {
                     append str_feature "${str_AND}not encrypted\n"
                  }
                  set str_AND {   AND }
               }
               if {(($mask & 0xc0) == 0xc0) && (($value & 0xc0) == 0)} {
                  append str_feature "${str_AND}new (i.e. no repeat)\n"
                  set str_AND {   AND }
               } else {
                  if {($mask & 0x40) && ($value & 0x40)} {
                     append str_feature "${str_AND}live transmission\n"
                     set str_AND {   AND }
                  }
                  if {($mask & 0x80) && ($value & 0x80)} {
                     append str_feature "${str_AND}repeat\n"
                     set str_AND {   AND }
                  }
               }
               if {$mask & 0x100} {
                  if {$value & 0x100} {
                     append str_feature "${str_AND}subtitled\n"
                  } else {
                     append str_feature "${str_AND}not subtitled\n"
                  }
                  set str_AND {   AND }
               }
               append out "${not}Feature: $str_feature"
               set first_feature 0
            }
         }
         parental {
            if {$valist == 1} {
               append out "${not}Parental rating: general (all ages)\n"
            } elseif {$valist > 0} {
               if {[string length $not] == 0} {
                  append out "Parental rating: ok for age [expr $valist * 2] and up\n"
               } else {
                  append out "Parental rating: rated higher than for age [expr $valist * 2]\n"
                  append out "                 (i.e. suitable only for age [expr ($valist + 1) * 2] or elder)\n"
               }
            }
         }
         editorial {
            append out "${not}Editorial rating: $valist of 1..7\n"
         }
         progidx {
            set start [lindex $valist 0]
            set stop  [lindex $valist 1]
            if {($start == 0) && ($stop == 0)} {
               append out "${not}Program running NOW\n"
            } elseif {($start == 0) && ($stop == 1)} {
               append out "${not}Program running NOW or NEXT\n"
            } elseif {($start == 1) && ($stop == 1)} {
               append out "${not}Program running NEXT\n"
            } else {
               append out "${not}Program indices #$start..#$stop\n"
            }
         }
         timsel {
            if {[lindex $valist 4] == -1} {
               set date "daily"
            } elseif {[lindex $valist 4] == 0} {
               set date "today"
            } elseif {[lindex $valist 4] == 1} {
               set date "tomorrow"
            } else {
               set date "in [lindex $valist 4] days"
            }
            if {[lindex $valist 0] == 0} {
               if {[lindex $valist 1] == 0} {
                  append out "${not}Time span: $date from [Motd2HHMM [lindex $valist 2]] til [Motd2HHMM [lindex $valist 3]]\n"
               } else {
                  append out "${not}Time span: $date from [Motd2HHMM [lindex $valist 2]] til midnight\n"
               }
            } else {
               if {[lindex $valist 1] == 0} {
                  append out "${not}Time span: $date from NOW til [Motd2HHMM [lindex $valist 3]]\n"
               } else {
                  append out "${not}Time span: $date from NOW til midnight\n"
               }
            }
         }
         dursel {
            append out "${not}Duration: from [Motd2HHMM [lindex $valist 0]] to [Motd2HHMM [lindex $valist 1]]"
         }
         vps_pdc {
            if {[lindex $valist 0] == 1} {
               append out "${not}VPS/PDC: restrict to programs with VPS/PDC code"
            } elseif {[lindex $valist 0] == 2} {
               append out "${not}VPS/PDC: restrict to programs with differing VPS/PDC code"
            }
         }
         substr {
            foreach parlist $valist {
               set grep_title [lindex $parlist 1]
               set grep_descr [lindex $parlist 2]
               if {$grep_title && !$grep_descr} {
                  append out "${not}Title"
               } elseif {!$grep_title && $grep_descr} {
                  append out "${not}Description"
               } else {
                  append out "${not}Title or Description"
               }
               append out " containing '[lindex $parlist 0]'"
               if {[lindex $parlist 3] && [lindex $parlist 4]} {
                  append out " (match case & complete)"
               } elseif [lindex $parlist 3] {
                  append out " (match case)"
               } elseif [lindex $parlist 4] {
                  append out " (match complete)"
               }
               append out "\n"
            }
         }
      }
   }
   return $out
}


