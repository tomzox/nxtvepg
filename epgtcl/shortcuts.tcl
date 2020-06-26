#
#  Handling of filter shortcuts
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
#    Implements methods to invoke or undo filter shortcuts.
#
#  Author: Tom Zoerner
#
#  $Id: shortcuts.tcl,v 1.19 2007/12/29 21:11:12 tom Exp tom $
#
#=CONST= ::fsc_name_idx 0
#=CONST= ::fsc_mask_idx 1
#=CONST= ::fsc_filt_idx 2
#=CONST= ::fsc_inv_idx  3
#=CONST= ::fsc_logi_idx 4
#=CONST= ::fsc_node_idx 5

##
##  Predefined filter shortcuts
##
proc PreloadShortcuts {} {
   global shortcuts shortcut_tree
   global user_language

   if {([string compare -nocase -length 2 $user_language de] == 0) || \
       ([string compare -nocase -length 3 $user_language ger] == 0)} {
      # Germany
      set shortcuts(10000) {Spielfilme themes {theme_class1 16} {} merge {}}
      set shortcuts(10010) {Sport themes {theme_class1 64} {} merge {}}
      set shortcuts(10011) {{Ohne Sport} themes {theme_class1 64} theme_class1 and {}}
      set shortcuts(10020) {Serien themes {theme_class1 128} {} merge {}}
      set shortcuts(10030) {Kinder themes {theme_class1 80} {} merge {}}
      set shortcuts(10040) {Shows themes {theme_class1 48} {} merge {}}
      set shortcuts(10050) {News themes {theme_class1 32} {} merge {}}
      set shortcuts(10060) {Sozial themes {theme_class1 37} {} merge {}}
      set shortcuts(10070) {Wissen themes {theme_class1 86} {} merge {}}
      set shortcuts(10080) {Hobby themes {theme_class1 52} {} merge {}}
      set shortcuts(10090) {Musik themes {theme_class1 96} {} merge {}}
      set shortcuts(10100) {Kultur themes {theme_class1 112} {} merge {}}
      set shortcuts(10110) {Adult themes {theme_class1 24} {} merge {}}
      set shortcuts(10120) {Abends timsel {timsel {0 0 1215 1410 -1}} {} merge {}}
      set shortcuts(10130) {{>15 min.} dursel {dursel {16 1435}} {} merge {}}
      set shortcut_tree [lsort -integer [array names shortcuts]]
   } elseif {[string compare -nocase -length 2 $user_language fr] == 0} {
      # France
      set shortcuts(10000) {Films themes {theme_class1 {16 24}} {} merge {}}
      set shortcuts(10010) {{Films de + 2heures} {themes dursel} {theme_class1 {16 24} dursel {120 1435}} {} merge {}}
      set shortcuts(10020) {{Films pour Adulte} themes {theme_class1 24} {} merge {}}
      set shortcuts(10030) {Sports themes {theme_class1 64} {} merge {}}
      set shortcuts(10031) {{pas de sports} themes {theme_class1 64} theme_class1 and {}}
      set shortcuts(10040) {Jeunesse themes {theme_class1 80} {} merge {}}
      set shortcuts(10050) {Spectacle/Jeu themes {theme_class1 48} {} merge {}}
      set shortcuts(10060) {Journal themes {theme_class1 32} {} merge {}}
      set shortcuts(10070) {Documentaire themes {theme_class1 38} {} merge {}}
      set shortcuts(10080) {Musique themes {theme_class1 96} {} merge {}}
      set shortcuts(10090) {Religion themes {theme_class1 112} {} merge {}}
      set shortcuts(10100) {Variétés themes {theme_class1 50} {} merge {}}
      set shortcuts(10110) {Météo substr {substr {{Météo 1 0 0 0 0 0}}} {} merge {}}
      set shortcuts(10120) {{12 ans et +} parental {parental 5} parental merge {}}
      set shortcuts(10130) {{16 ans et +} parental {parental 7} parental merge {}}
      set shortcuts(10140) {{45 minutes et +} dursel {dursel {45 1435}} {} merge {}}
      set shortcuts(10150) {{3h00 et +} dursel {dursel {180 1435}} {} merge {}}
      set shortcut_tree [lsort -integer [array names shortcuts]]
   } else {
      # generic
      set shortcuts(10000) {movies themes {theme_class1 16} {} merge {}}
      set shortcuts(10010) {sports themes {theme_class1 64} {} merge {}}
      set shortcuts(10011) {{no sports} themes {theme_class1 64} theme_class1 and {}}
      set shortcuts(10020) {series themes {theme_class1 128} {} merge {}}
      set shortcuts(10030) {kids themes {theme_class1 80} {} merge {}}
      set shortcuts(10040) {shows themes {theme_class1 48} {} merge {}}
      set shortcuts(10050) {news themes {theme_class1 32} {} merge {}}
      set shortcuts(10060) {social themes {theme_class1 37} {} merge {}}
      set shortcuts(10070) {science themes {theme_class1 86} {} merge {}}
      set shortcuts(10080) {hobbies themes {theme_class1 52} {} merge {}}
      set shortcuts(10090) {music themes {theme_class1 96} {} merge {}}
      set shortcuts(10100) {culture themes {theme_class1 112} {} merge {}}
      set shortcuts(10110) {adult themes {theme_class1 24} {} merge {}}
      set shortcuts(10120) {evening timsel {timsel {0 0 1215 1410 -1}} {} merge {}}
      set shortcuts(10130) {{>15 minutes} dursel {dursel {16 1435}} {} merge {}}
      set shortcut_tree [lsort -integer [array names shortcuts]]
   }
}

##
##  Generate a new shortcut tag: value is arbitrary, but unique
##
proc GenerateShortcutTag {} {
   global shortcuts
   global fscedit_sclist

   set tag [C_ClockSeconds]
   foreach stag [array names shortcuts] {
      if {$tag <= $stag} {
         set tag [expr $stag + 1]
      }
   }
   if [info exists fscedit_sclist] {
      foreach stag [array names fscedit_sclist] {
         if {$tag <= $stag} {
            set tag [expr $stag + 1]
         }
      }
   }
   return $tag
}

## ---------------------------------------------------------------------------
##  Load filter const cache
##  - 1. with shortcut combinations used by reminders
##  - 2. with shortcuts used in user-defined columns, and
##
proc DownloadUserDefinedColumnFilters {} {
   array set cache {}

   # need filters for all reminder groups to be able to search for events
   set rem_count [Reminder_GetShortcuts cache]

   # query which shortcuts are required for user-defined columns (list returned in cache array)
   set sc_count [UserCols_GetShortcuts cache $rem_count]

   # free the old cache and allocate a new one with the required number of entries
   C_PiFilter_ContextCacheCtl start [expr $rem_count + $sc_count]

   # download reminder filter contexts
   Reminder_SetShortcuts

   # download shortcut filter context
   foreach {sc_tag filt_idx} [array get cache] {
      if {[UserColsDlg_IsReminderPseudoTag $sc_tag] == -1} {
         C_PiFilter_ContextCacheCtl set $filt_idx
         SelectSingleShortcut $sc_tag
      }
   }

   C_PiFilter_ContextCacheCtl done
}

##  --------------------------------------------------------------------------
##  Check if shortcut should be deselected after manual filter modification
##
proc CheckShortcutDeselection {} {
   global shortcuts
   global parental_rating editorial_rating
   global theme_sel theme_class_sel current_theme_class theme_class_count
   global series_sel
   global feature_class_count feature_class_mask feature_class_value
   global progidx_first progidx_last filter_progidx
   global substr_stack
   global timsel_enabled timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop timsel_datemode
   global dursel_min dursel_max
   global vpspdc_filt
   global piexpire_display
   global filter_invert
   global fsc_prevselection

   foreach sc_tag [ShortcutTree_Curselection .all.shortcuts.list] {
      set undo 0
      foreach {ident valist} [lindex $shortcuts($sc_tag) $::fsc_filt_idx] {
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
                              ($timsel_stop     != [lindex $valist 3]) ]
               if {$undo == 0} {
                  switch -exact $timsel_datemode {
                     rel    {set undo [expr [lindex $valist 4] != $timsel_date]}
                     ignore {set undo [expr [lindex $valist 4] != -1]}
                     wday   {set undo [expr [lindex $valist 4] != -10 - $timsel_date]}
                     mday   {set undo [expr [lindex $valist 4] != -100 - $timsel_date]}
                  }
               }
            }
            dursel {
               set undo [expr ($dursel_min != [lindex $valist 0]) || \
                              ($dursel_max != [lindex $valist 1]) ]
            }
            vps_pdc {
               set undo [expr $vpspdc_filt != [lindex $valist 0]]
            }
            piexpire {
               set undo [expr $piexpire_display != [lindex $valist 0]]
            }
            substr {
               # check if all substr param sets are still active, i.e. on the stack
               array set stack_cache {}
               foreach item $substr_stack {
                  set stack_cache($item) {}
               }
               foreach parlist $valist {
                  if {[info exists stack_cache($parlist)] == 0} {
                     set undo 1
                     break
                  }
               }
               array unset stack_cache
            }
         }
         if $undo break
      }

      # check if any filters in the mask are inverted
      set invl [lindex $shortcuts($sc_tag) $::fsc_inv_idx]
      foreach {ident valist} [lindex $shortcuts($sc_tag) $::fsc_mask_idx] {
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
         if {![info exists filter_invert($ident)] || \
             $filter_invert($ident) == 0} {
            set undo 1
            break
         }
      }

      if $undo {
         # clear the selection in the main window's shortcut listbox
         Tree:selection .all.shortcuts.list clear $sc_tag

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
   global shortcuts

   foreach {ident valist} [lindex $shortcuts($sc_tag) $::fsc_filt_idx] {
      switch -glob $ident {
         theme_class*   {
            scan $ident "theme_class%d" class
            C_SelectThemes $class $valist
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
            set tmp_date        [lindex $valist 4]
            if {$tmp_date <= -100} {
               set timsel_datemode mday
               set timsel_date [expr -100 - [lindex $valist 4]]
            } elseif {$tmp_date <= -10} {
               set timsel_datemode wday
               set timsel_date [expr -10 - [lindex $valist 4]]
            } elseif {$tmp_date < 0} {
               set timsel_datemode ignore
               set timsel_date 0
            } else {
               set timsel_datemode rel
               set timsel_date [lindex $valist 4]
            }
            C_SelectStartTime $timsel_relative $timsel_absstop $timsel_datemode \
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
         piexpire {
            set piexpire_display [lindex $valist 0]
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
   C_InvertFilter [lindex $shortcuts($sc_tag) $::fsc_inv_idx]
}

##  --------------------------------------------------------------------------
##  Callback for button-release on shortcuts listbox
##
proc SelectShortcuts {sc_tag_list shortcuts_arr} {
   global parental_rating editorial_rating
   global theme_sel theme_class_sel current_theme_class theme_class_count
   global series_sel
   global feature_class_count feature_class_mask feature_class_value
   global progidx_first progidx_last filter_progidx
   global substr_stack substr_pattern substr_grep_title substr_grep_descr
   global substr_match_case substr_match_full
   global timsel_enabled timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop timsel_datemode
   global dursel_min dursel_max dursel_minstr dursel_maxstr
   global vpspdc_filt
   global piexpire_display
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

   # reset all filters in the combined mask
   foreach sc_tag $sc_tag_list {
      foreach type [lindex $shortcuts($sc_tag) $::fsc_mask_idx] {
         # reset filter menu state; remember which types were reset
         switch -exact $type {
            themes     {ResetThemes;          set reset(themes) 1}
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
            piexpire   {ResetExpireDelay}
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
      if {[lindex $shortcuts($sc_tag) $::fsc_logi_idx] != "merge"} {
         C_PiFilter_ForkContext remove $sc_tag
      } else {
         foreach {ident valist} [lindex $shortcuts($sc_tag) $::fsc_filt_idx] {
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
                  piexpire {
                     set piexpire_display 0
                     C_SelectExpiredPiDisplay
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

         if {[lsearch -exact [lindex $shortcuts($sc_tag) $::fsc_inv_idx] all] != -1} {
            array unset filter_invert all
         }
      }
   }

   # set all filters in all selected shortcuts (merge)
   foreach sc_tag $sc_tag_list {
      if {[lindex $shortcuts($sc_tag) $::fsc_logi_idx] != "merge"} {
         if {[lsearch $fsc_prevselection $sc_tag] == -1} {
            C_PiFilter_ForkContext [lindex $shortcuts($sc_tag) $::fsc_logi_idx] $sc_tag
            SelectSingleShortcut $sc_tag
            C_PiFilter_ForkContext close
         }
      } else {
         foreach {ident valist} [lindex $shortcuts($sc_tag) $::fsc_filt_idx] {
            switch -glob $ident {
               theme_class*   {
                  scan $ident "theme_class%d" class
                  if {[info exists tcsel($class)]} {
                     set tcsel($class) [concat $tcsel($class) $valist]
                  } else {
                     set tcsel($class) $valist
                  }
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
                  if {[llength $substr_stack] > 0} {
                     array set stack_cache {}
                     set idx 0
                     foreach item $substr_stack {
                        set stack_cache($item) $idx
                        incr idx
                     }
                     # remove identical items from the substr stack
                     foreach parlist $valist {
                        if [info exists stack_cache($parlist)] {
                           set idx $stack_cache($parlist)
                           set substr_stack [lreplace $substr_stack $idx $idx]
                        }
                     }
                     array unset stack_cache
                  }
                  # push set onto the substr stack; new stack is applied at the end
                  set substr_stack [concat $valist $substr_stack]
                  set upd_substr 1
                  # display first element in the test search dialog
                  if {[llength $valist] > 0} {
                     set parlist [lindex $valist 0]
                     set substr_pattern     [lindex $parlist 0]
                     set substr_grep_title  [lindex $parlist 1]
                     set substr_grep_descr  [lindex $parlist 2]
                     set substr_match_case  [lindex $parlist 3]
                     set substr_match_full  [lindex $parlist 4]

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
                  set tmp_date        [lindex $valist 4]
                  if {$tmp_date <= -100} {
                     set timsel_datemode mday
                     set timsel_date [expr -100 - [lindex $valist 4]]
                  } elseif {$tmp_date <= -10} {
                     set timsel_datemode wday
                     set timsel_date [expr -10 - [lindex $valist 4]]
                  } elseif {$tmp_date < 0} {
                     set timsel_datemode ignore
                  } else {
                     set timsel_datemode rel
                     set timsel_date [lindex $valist 4]
                  }
                  C_SelectStartTime $timsel_relative $timsel_absstop $timsel_datemode \
                                    $timsel_start $timsel_stop $timsel_date
                  TimeFilterExternalChange
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
               piexpire {
                  set piexpire_display [lindex $valist 0]
                  C_SelectExpiredPiDisplay
                  PiExpTime_ExternalChange
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

         foreach ident [lindex $shortcuts($sc_tag) $::fsc_inv_idx] {
            set filter_invert($ident) 1
         }
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

   set fsc_prevselection $sc_tag_list
}

##  --------------------------------------------------------------------------
##  Tree widget interface functions

##
##  Toggle shortcut between active / inactive
##  - used for key bindings on 0-9 digit keys in main window
##
proc ToggleShortcut {index} {
   global shortcuts shortcut_tree

   # get flat list of shortcuts (unfold visible parts of tree)
   set sc_tag [lindex [Tree:unfold .all.shortcuts.list /] $index]

   if {! [Tree:selection .all.shortcuts.list includes $sc_tag]} {
      # item is not yet selected -> select it
      Tree:selection .all.shortcuts.list set $sc_tag
   } else {
      # deselect the item
      Tree:selection .all.shortcuts.list clear $sc_tag
   }
   # update filter settings
   ShortcutTree_Invoke
}

##
##  Enable exactly one single shortcut out of a given list
##  - if another shortcut in the list is currently selected, it's deselected
##  - if the single shortcut is already selected, it's deselected
##
proc SelectShortcutFromTagList {tag_list sc_tag} {
   global shortcuts
   global fsc_prevselection

   if [info exists fsc_prevselection] {
      # check if the given shortcut is already selected -> toggle
      if {$sc_tag != {}} {
         set undo [expr [lsearch -exact $fsc_prevselection $sc_tag] != -1]
      } else {
         set undo 0
      }

      # disable shortcuts in the given list, if currently selected
      set sc_tag_list {}
      foreach prev_tag $fsc_prevselection {
         if {[lsearch -exact $tag_list $prev_tag] != -1} {
            # shortcut selected & in the given list -> deselect in listbox
            # (the shortcut's filters are implicitly disabled by not passing the tag to select below)
            Tree:selection .all.shortcuts.list clear $prev_tag
         } else {
            # shortcut is not in the list -> leave it selected
            lappend sc_tag_list $prev_tag
         }
      }
      if {($undo == 0) && ($sc_tag != {})} {
         lappend sc_tag_list $sc_tag
      }

   } else {
      # no shortcuts selected previously -> select the new shortcut
      set sc_tag_list $sc_tag
      set undo 0
   }

   if {$undo == 0} {
      Tree:selection .all.shortcuts.list set $sc_tag
   } else {
      Tree:selection .all.shortcuts.list clear $sc_tag
   }

   SelectShortcuts $sc_tag_list shortcuts
}

##
##  Toggle the shortcut given by the tag on/off
##  - all other shortcuts remain unaffected
##
#proc ToggleShortcutByTag {sc_tag} {
#   global shortcuts
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
#   if {$undo == 0} {
#      Tree:selection .all.shortcuts.list set $sc_tag
#   } else {
#      Tree:selection .all.shortcuts.list clear $sc_tag
#   }
#
#   SelectShortcuts $sc_tag_list shortcuts
#}

proc ShortcutTree_Invoke {} {
   global shortcuts

   set sc_tag_list [ShortcutTree_Curselection .all.shortcuts.list]
   SelectShortcuts $sc_tag_list shortcuts
}

##
##  Feed all elements in the shortcut tree into the tree widget
##  - tree structure is defined by "node" member in shortcut array elements:
##    leafs have type {}; nodes type "+dir" or "-dir" where +/- says if the
##    elements are visible or not; node type "separator" is a horizontal line
##  - calls itself recursively for all sub-nodes
##
proc ShortcutTree_Fill {w dir sc_tags sc_ref edit_mode} {
   upvar $sc_ref sc_arr

   if {$dir == {}} {
      Tree:delitem $w /
   }

   foreach elem $sc_tags {
      if {[llength $elem] == 1} {
         if [info exists sc_arr($elem)] {
            if {[string length [lindex $sc_arr($elem) $::fsc_node_idx]] == 0} {
               Tree:insert $w $dir/$elem -label [lindex $sc_arr($elem) $::fsc_name_idx]
            } elseif {[string compare [lindex $sc_arr($elem) $::fsc_node_idx] separator] == 0} {
               if $edit_mode {
                  Tree:insert $w $dir/$elem -separator 1
               } else {
                  Tree:insert $w $dir/$elem -separator 1 -state disabled
               }
            } elseif {[string match "?dir" [lindex $sc_arr($elem) $::fsc_node_idx]] == 1} {
               set diropen [expr [string compare -length 1 [lindex $sc_arr($elem) $::fsc_node_idx] "+"] == 0]
               if $edit_mode {
                  Tree:insert $w $dir/$elem -label [lindex $sc_arr($elem) $::fsc_name_idx] -diropen $diropen
               } else {
                  Tree:insert $w $dir/$elem -label [lindex $sc_arr($elem) $::fsc_name_idx] -diropen $diropen -state noselect
               }
            } else {
               error "unknown shortcut node type '[lindex $sc_arr($elem) $::fsc_node_idx]' for tag $elem"
            }
         }
      } else {
         set dir_tag [lindex $elem 0]
         if [info exists sc_arr($dir_tag)] {
            set dflag [lindex $sc_arr($dir_tag) $::fsc_node_idx]
            if {[string match "?dir" $dflag] == 1} {
               set diropen [expr [string compare -length 1 $dflag "+"] == 0]
               if $edit_mode {
                  Tree:insert $w $dir/$dir_tag -label [lindex $sc_arr($dir_tag) $::fsc_name_idx] -diropen $diropen
               } else {
                  Tree:insert $w $dir/$dir_tag -label [lindex $sc_arr($dir_tag) $::fsc_name_idx] -diropen $diropen -state noselect
               }
               ShortcutTree_Fill $w $dir/$dir_tag [lrange $elem 1 end] sc_arr $edit_mode
            } else {
               error "unknown shortcut sub-node type '[lindex $sc_arr($elem) $::fsc_node_idx]' for tag $dir_tag"
            }
         }
      }
   }
}

##
##  Hierarchically fill a menu with all shortcuts
##  - invokes a callback proc for each shortcut to determine it's menu entrie's state
##
proc ShortcutTree_MenuFill {wtree wmenu sc_tags sc_ref menu_cmd state_proc} {
   upvar $sc_ref sc_arr

   foreach elem $sc_tags {
      if {[llength $elem] == 1} {
         if {[string length [lindex $sc_arr($elem) $::fsc_node_idx]] == 0} {
            $wmenu add command -label [lindex $sc_arr($elem) $::fsc_name_idx] \
                               -command [concat $menu_cmd $elem] -state [$state_proc $elem]
         } else {
            $wmenu add separator
         }
      } else {
         set dir_tag [lindex $elem 0]
         if [info exists sc_arr($dir_tag)] {
            if {[string match "?dir" [lindex $sc_arr($dir_tag) $::fsc_node_idx]] == 1} {
               $wmenu add cascade -label [lindex $sc_arr($dir_tag) $::fsc_name_idx] -menu ${wmenu}.dir$dir_tag
               menu ${wmenu}.dir$dir_tag -tearoff 0 

               ShortcutTree_MenuFill $wtree ${wmenu}.dir$dir_tag [lrange $elem 1 end] sc_arr $menu_cmd $state_proc
            }
         }
      }
   }
}

##
##  Query which shortcuts are selected in the tree widget
##
proc ShortcutTree_Curselection {w} {

   set ltmp {}
   foreach el [Tree:curselection $w] {
      lappend ltmp [file tail $el]
   }
   return $ltmp
}

##
##  Translate an X,Y coordinate in the tree widget into a shortcut tag
##
proc ShortcutTree_Element {w coo} {
   set w .all.shortcuts.list
   set el [Tree:labelat $w $coo]
   return [file tail $el]
}

##
##  Handler for virtual "TreeOpenClose" event
##  - invoked by tree widget when directory +/- button is clicked
##  - used to track which directories are open in the rc/ini file
##
proc ShortcutTree_OpenCloseEvent {wid sc_arr_ref sc_tree} {
   upvar $sc_arr_ref sc_arr

   foreach elem $sc_tree {
      if {[llength $elem] > 1} {
         # this is a sub-directory
         set dir_tag [lindex $elem 0]
         if {[info exists sc_arr($dir_tag)] &&
             ([string match "?dir" [lindex $sc_arr($dir_tag) $::fsc_node_idx]] == 1)} {
            # query if this directory node is open or closed
            if [Tree:itemcget $wid $dir_tag -diropen] {
               set dflag "+dir"
            } else {
               set dflag "-dir"
            }
            # update open/close status in the node's shortcut array element, if changed
            if {[string compare [lindex $sc_arr($dir_tag) $::fsc_node_idx] $dflag] != 0} {
               set sc_arr($dir_tag) [lreplace $sc_arr($dir_tag) $::fsc_node_idx $::fsc_node_idx $dflag]
            }

            # traverse sub-directory (to process for sub-sub directories)
            ShortcutTree_OpenCloseEvent $wid sc_arr [lrange $elem 1 end]
         }
      }
   }
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
   global series_sel
   global feature_class_count current_feature_class
   global substr_stack
   global filter_progidx progidx_first progidx_last
   global timsel_enabled timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop timsel_datemode
   global dursel_min dursel_max
   global vpspdc_filt
   global piexpire_display
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
      set temp [list $timsel_relative $timsel_absstop $timsel_start $timsel_stop]
      switch -exact $timsel_datemode {
         rel    {lappend temp $timsel_date}
         ignore {lappend temp -1}
         wday   {lappend temp [expr -10 - $timsel_date]}
         mday   {lappend temp [expr -100 - $timsel_date]}
      }
      lappend all "timsel" $temp
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

   # dump PI expire time
   if {$piexpire_display != 0} {
      lappend all "piexpire" $piexpire_display
      lappend mask "piexpire"
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
   # (note: do not use mask or theme classes are missed)
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

# indices into result list returned by proc DescribeCurrentFilter
#=CONST= ::scdesc_mask_idx  0
#=CONST= ::scdesc_filt_idx  1
#=CONST= ::scdesc_inv_idx   2

##  --------------------------------------------------------------------------
##  Generate a text that describes a given filter setting
##
proc ShortcutPrettyPrint {filter inv_list} {

   # fetch CNI list from AI block in database
   set netsel_ailist [C_GetAiNetwopList 0 netnames]

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
            set date [lindex $valist 4]
            if {$date <= -100} {
               set date "monthly, day #[expr -100 - $date]"
            } elseif {$date <= -10} {
               set date "weekly at [GetNameForWeekday [expr -10 - $date] {%A}]"
            } elseif {$date < 0} {
               set date "daily"
            } else {
               if {$date == 0} {
                  set date "today"
               } elseif {$date == 1} {
                  set date "tomorrow"
               } else {
                  set date "in $date days"
               }
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
         piexpire {
            if {[lindex $valist 0] < 60} {
               append out "Expire time: include expired programmes up to [lindex $valist 0] minutes"
            } else {
               append out "Expire time: include expired programmes up to [expr int([lindex $valist 0]/60)]:[expr [lindex $valist 0]%60] hours"
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

