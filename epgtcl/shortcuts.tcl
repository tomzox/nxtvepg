#
#  Handling of filter shortcuts
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
#    Implements methods to invoke or undo filter shortcuts.
#
#=INCLUDE= "epgtcl/mainwin.h"

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
      set shortcuts(10010) {Abends timsel {timsel {0 0 1215 1410 -1}} {} merge {}}
      set shortcuts(10020) {{Nächste...} progidx {progidx {1 1}} {} merge {}}
      set shortcuts(10030) {{Heute} timsel {timsel {0 1 0 1439 0}} {} merge {}}
      set shortcuts(10040) {{Vergangene...} piexpire {piexpire -1} {} merge {}}
      set shortcuts(10100) {{} {} {} {} merge separator}
      set shortcuts(10110) {{>15 Minuten} dursel {dursel {16 1435}} {} merge {}}
      set shortcuts(10120) {{>80 Minuten} dursel {dursel {80 1435}} {} merge {}}
      set shortcuts(10130) {{Untertitel} features {features {$::PI_FEATURE_SUBTITLE_ANY $::PI_FEATURE_SUBTITLE_ANY}} {} merge {}}
      set shortcuts(10140) {{Zweikanalton} features {features {$::PI_FEATURE_SOUND_MASK $::PI_FEATURE_SOUND_2CHAN}} {} merge {}}
      set shortcuts(10200) {{} {} {} {} merge separator}
      set shortcuts(10210) {Spielfilm substr {substr {{film 0 1 0 0 0 0}}} {} merge {}}
      set shortcuts(10220) {Serie substr {substr {{serie 0 1 0 0 0 0}}} {} merge {}}
      set shortcuts(10230) {tagesschau substr {substr {{Tagesschau 1 0 1 1 0 0}}} {} merge {}}
      set shortcut_tree [lsort -integer [array names shortcuts]]

   } elseif {[string compare -nocase -length 2 $user_language fr] == 0} {
      # France
      set shortcuts(10010) {{le soir} timsel {timsel {0 0 1215 1410 -1}} {} merge {}}
      set shortcuts(10020) {{prochaine} progidx {progidx {1 1}} {} merge {}}
      set shortcuts(10030) {{aujourd'hui} timsel {timsel {0 1 0 1439 0}} {} merge {}}
      set shortcuts(10040) {{expiré} piexpire {piexpire -1} {} merge {}}
      set shortcuts(10100) {{} {} {} {} merge separator}
      set shortcuts(10110) {{45 minutes et +} dursel {dursel {45 1435}} {} merge {}}
      set shortcuts(10120) {{80 minutes et +} dursel {dursel {80 1435}} {} merge {}}
      set shortcuts(10130) {{12 ans et +} parental {parental 11} parental merge {}}
      set shortcuts(10140) {{16 ans et +} parental {parental 15} parental merge {}}
      set shortcuts(10150) {{sous-titre} features {features {$::PI_FEATURE_SUBTITLE_ANY $::PI_FEATURE_SUBTITLE_ANY}} {} merge {}}
      set shortcuts(10200) {{} {} {} {} merge separator}
      set shortcuts(10210) {Films substr {substr {{film 0 1 0 0 0 0}}} {} merge {}}
      set shortcuts(10220) {Série substr {substr {{série 0 1 0 0 0 0}}} {} merge {}}
      set shortcuts(10230) {Météo substr {substr {{Météo 1 0 0 0 0 0}}} {} merge {}}
      set shortcut_tree [lsort -integer [array names shortcuts]]
   } else {
      # generic
      set shortcuts(10010) {{evening} timsel {timsel {0 0 1215 1410 -1}} {} merge {}}
      set shortcuts(10020) {{next up} progidx {progidx {1 1}} {} merge {}}
      set shortcuts(10030) {{today} timsel {timsel {0 1 0 1439 0}} {} merge {}}
      set shortcuts(10040) {{in the past} piexpire {piexpire -1} {} merge {}}
      set shortcuts(10100) {{} {} {} {} merge separator}
      set shortcuts(10110) {{>15 minutes} dursel {dursel {16 1435}} {} merge {}}
      set shortcuts(10120) {{>80 minutes} dursel {dursel {80 1435}} {} merge {}}
      set shortcuts(10130) {{age 12+} parental {parental 11} parental merge {}}
      set shortcuts(10140) {{age 16+} parental {parental 15} parental merge {}}
      set shortcuts(10130) {{subtitled} features {features {$::PI_FEATURE_SUBTITLE_ANY $::PI_FEATURE_SUBTITLE_ANY}} {} merge {}}
      set shortcuts(10140) {{2-channel} features {features {$::PI_FEATURE_SOUND_MASK $::PI_FEATURE_SOUND_2CHAN}} {} merge {}}
      set shortcuts(10200) {{} {} {} {} merge separator}
      set shortcuts(10210) {movies substr {substr {{movie 0 1 0 0 0 0}}} {} merge {}}
      set shortcuts(10220) {series substr {substr {{series 0 1 0 0 0 0}}} {} merge {}}
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
   global feature_class_count feature_class_mask feature_class_value
   global progidx_first progidx_last filter_progidx
   global substr_stack
   global timsel_enabled timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop timsel_datemode
   global dursel_min dursel_max
   global vpspdc_filt
   global piexpire_display piexpire_never
   global filter_invert
   global fsc_prevselection

   set all_theme_names [C_GetAllThemesStrings]

   foreach sc_tag [ShortcutTree_Curselection .all.shortcuts.list] {
      set undo 0
      foreach {ident valist} [lindex $shortcuts($sc_tag) $::fsc_filt_idx] {
         switch -glob $ident {
            theme_class* {
               scan $ident "theme_class%d" class
               if {$class == $current_theme_class} {
                  foreach theme_name $valist {
                     set theme_idx [lsearch -exact $all_theme_names $theme_name]
                     if {($theme_idx >= 0) && ([lsearch -integer $theme_sel $theme_idx] < 0)} {
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
               set undo [expr ($valist < $parental_rating) || ($parental_rating == 0x80)]
            }
            editorial {
               set undo [expr ($valist > $editorial_rating) || ($editorial_rating == 0x80)]
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
               set undo [expr (([lindex $valist 0] == -1) ? \
                               ($piexpire_never == 0) : \
                               ($piexpire_display != [lindex $valist 0]))]
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
            if {![info exists all_theme_names]} {
               set all_theme_names [C_GetAllThemesStrings]
            }
            set theme_idx_list {}
            foreach theme_name $valist {
               set theme_idx [lsearch -exact $all_theme_names $theme_name]
               if {$theme_idx >= 0} {
                  lappend theme_idx_list $theme_idx
               }
            }
            C_SelectThemes [expr {1 << ($class - 1)}] $theme_idx_list
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
            if {[lindex $valist 0] == -1} {
               set piexpire_never 1
            } else {
               set piexpire_display [lindex $valist 0]
            }
         }
         netwops {
            set all {}
            set index 0
            foreach cni [C_GetAiNetwopList "" {}] {
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
   global feature_class_count feature_class_mask feature_class_value
   global progidx_first progidx_last filter_progidx
   global substr_stack substr_pattern substr_grep_title substr_grep_descr
   global substr_match_case substr_match_full
   global timsel_enabled timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop timsel_datemode
   global dursel_min dursel_max dursel_minstr dursel_maxstr
   global vpspdc_filt
   global piexpire_display piexpire_never
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
                     set parental_rating 0xFF
                     C_SelectParentalRating 0
                  }
                  editorial {
                     set editorial_rating 0xFF
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
                     set piexpire_never 0
                     C_SelectExpiredPiDisplay
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
                  if {($rating < $parental_rating) || ($parental_rating == 0x80)} {
                     set parental_rating $rating
                     C_SelectParentalRating $parental_rating
                  }
               }
               editorial {
                  set rating [lindex $valist 0]
                  if {($rating > $editorial_rating) || ($editorial_rating == 0x80)} {
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
                  if {[lindex $valist 0] == -1} {
                     set piexpire_never 1
                  } else {
                     set piexpire_display [lindex $valist 0]
                  }
                  C_SelectExpiredPiDisplay
                  PiExpTime_ExternalChange
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
   set all_theme_names [C_GetAllThemesStrings]
   for {set class 1} {$class <= $theme_class_count} {incr class} {
      if {$class == $current_theme_class} {
         foreach theme $theme_sel {
            set tmp_arr($theme) 1
         }
         if {[info exists tcdesel($class)]} {
            foreach name $tcdesel($class) {
               set idx [lsearch -exact $all_theme_names $name]
               if {$idx >= 0} {
                  set tmp_arr($idx) 0
               }
            }
         }
         if {[info exists tcsel($class)]} {
            foreach name $tcsel($class) {
               set idx [lsearch -exact $all_theme_names $name]
               if {$idx >= 0} {
                  set tmp_arr($idx) 1
               }
            }
         }
         set theme_class_sel($class) {}
         foreach {index value} [array get tmp_arr] {
            if {$value != 0} {
               lappend theme_class_sel($class) $index
            }
         }
         set theme_sel $theme_class_sel($class)
      } else {
         if {[info exists tcsel($class)]} {
            set tmp_list {}
            foreach name $tcsel($class) {
               set idx [lsearch -exact $all_theme_names $name]
               if {$idx >= 0} {
                  lappend tmp_list $idx
               }
            }
            set theme_class_sel($class) $tmp_list
            # XXX TODO: substract tcdesel
         }
      }
      C_SelectThemes [expr {1 << ($class - 1)}] $theme_class_sel($class)
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
      foreach cni [C_GetAiNetwopList "" {}] {
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
   global feature_class_count current_feature_class
   global substr_stack
   global filter_progidx progidx_first progidx_last
   global timsel_enabled timsel_start timsel_stop timsel_date
   global timsel_relative timsel_absstop timsel_datemode
   global dursel_min dursel_max
   global vpspdc_filt
   global piexpire_display piexpire_never
   global filter_invert

   # save the setting of the current theme class into the array
   set theme_class_sel($current_theme_class) $theme_sel

   set all {}
   set mask {}

   # dump all theme classes
   for {set class 1} {$class <= $theme_class_count} {incr class} {
      if {[llength $theme_class_sel($class)] > 0} {
         set theme_names {}
         foreach theme $theme_class_sel($class) {
            lappend theme_names [C_GetThemesString $theme]
         }
         lappend all "theme_class$class" $theme_names
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

   if {$parental_rating != 0xFF} {
      lappend all "parental" $parental_rating
      lappend mask parental
   }
   if {$editorial_rating != 0xFF} {
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

   # dump PI expire time disable or cut-off threshold
   if {$piexpire_never} {
      lappend all "piexpire" -1
      lappend mask "piexpire"
   } elseif {$piexpire_display != 0} {
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
   set netsel_ailist [C_GetAiNetwopList "" netnames]

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
               append out "${not}Theme: $theme\n"
            }
         }
         theme_class* {
            scan $ident "theme_class%d" class
            foreach theme $valist {
               append out "${not}Theme, class ${class}: $theme\n"
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
               if {($mask & $::PI_FEATURE_FMT_WIDE) != 0} {
                  if {($value & $::PI_FEATURE_FMT_WIDE) != 0} {
                     append str_feature "${str_AND}widescreen picture\n"
                  } else {
                     append str_feature "${str_AND}fullscreen picture\n"
                  }
                  set str_AND {   AND }
               }
               if {($mask & $::PI_FEATURE_VIDEO_HD) != 0} {
                  if {($value & $::PI_FEATURE_VIDEO_HD) != 0} {
                     append str_feature "${str_AND}HD video\n"
                  } else {
                     append str_feature "${str_AND}non-HD video\n"
                  }
                  set str_AND {   AND }
               }
               if {($mask & $::PI_FEATURE_VIDEO_BW) != 0} {
                  if {($value & $::PI_FEATURE_VIDEO_BW) != 0} {
                     append str_feature "${str_AND}black & white\n"
                  } else {
                     append str_feature "${str_AND}color\n"
                  }
                  set str_AND {   AND }
               }
               if {($mask & $::PI_FEATURE_VIDEO_NONE) != 0} {
                  if {($value & $::PI_FEATURE_VIDEO_NONE) != 0} {
                     append str_feature "${str_AND}no video (radio)\n"
                  } else {
                     append str_feature "${str_AND}with video\n"
                  }
                  set str_AND {   AND }
               }
               if {($mask & $::PI_FEATURE_SUBTITLE_ANY) != 0} {
                  if {($value & $::PI_FEATURE_SUBTITLE_ANY) != 0} {
                     append str_feature "${str_AND}subtitled\n"
                  } else {
                     append str_feature "${str_AND}no subtitles\n"
                  }
                  set str_AND {   AND }
               }
               if {($mask & $::PI_FEATURE_SUBTITLE_SIGN) != 0} {
                  if {($value & $::PI_FEATURE_SUBTITLE_SIGN) != 0} {
                     append str_feature "${str_AND}deaf-signed\n"
                  } else {
                     append str_feature "${str_AND}not deaf-signed\n"
                  }
                  set str_AND {   AND }
               }
               if {($mask & $::PI_FEATURE_SUBTITLE_OSC) != 0} {
                  if {($value & $::PI_FEATURE_SUBTITLE_OSC) != 0} {
                     append str_feature "${str_AND}on-screen subtitles\n"
                  } else {
                     append str_feature "${str_AND}no on-screen subtitles\n"
                  }
                  set str_AND {   AND }
               }
               if {($mask & $::PI_FEATURE_SUBTITLE_TTX) != 0} {
                  if {($value & $::PI_FEATURE_SUBTITLE_TTX) != 0} {
                     append str_feature "${str_AND}teletext subtitles\n"
                  } else {
                     append str_feature "${str_AND}no teletext subtitles\n"
                  }
                  set str_AND {   AND }
               }
               if {($mask & $::PI_FEATURE_NEW) != 0} {
                  if {($value & $::PI_FEATURE_NEW) != 0} {
                     append str_feature "${str_AND}new\n"
                  } else {
                     append str_feature "${str_AND}not new\n"
                  }
                  set str_AND {   AND }
               }
               if {($mask & $::PI_FEATURE_PREMIERE) != 0} {
                  if {($value & $::PI_FEATURE_PREMIERE) != 0} {
                     append str_feature "${str_AND}premiere\n"
                  } else {
                     append str_feature "${str_AND}not a premiere\n"
                  }
                  set str_AND {   AND }
               }
               if {($mask & $::PI_FEATURE_LAST_REP) != 0} {
                  if {($value & $::PI_FEATURE_LAST_REP) != 0} {
                     append str_feature "${str_AND}last repetition\n"
                  } else {
                     append str_feature "${str_AND}not last repetition\n"
                  }
                  set str_AND {   AND }
               }
               if {($mask & $::PI_FEATURE_REPEAT) != 0} {
                  if {($value & $::PI_FEATURE_REPEAT) != 0} {
                     append str_feature "${str_AND}previously shown\n"
                  } else {
                     append str_feature "${str_AND}not previously shown\n"
                  }
                  set str_AND {   AND }
               }
               if {($mask & $::PI_FEATURE_SOUND_MASK) == $::PI_FEATURE_SOUND_MASK} {
                  set masked_value [expr $value & $::PI_FEATURE_SOUND_MASK]
                  if {$masked_value == $::PI_FEATURE_SOUND_NONE} {
                     set txt "none"
                  } elseif {$masked_value == $::PI_FEATURE_SOUND_MONO} {
                     set txt "mono"
                  } elseif {$masked_value == $::PI_FEATURE_SOUND_STEREO} {
                     set txt "stereo"
                  } elseif {$masked_value == $::PI_FEATURE_SOUND_2CHAN} {
                     set txt "2-channel"
                  } elseif {$masked_value == $::PI_FEATURE_SOUND_SURROUND} {
                     set txt "surround"
                  } elseif {$masked_value == $::PI_FEATURE_SOUND_DOLBY} {
                     set txt "dolby"
                  } else {
                     set txt "invalid"
                  }
                  append str_feature "${str_AND}sound: $txt\n"
                  set str_AND {   AND }
               }

               append out "${not}Feature: $str_feature"
               set first_feature 0
            }
         }
         parental {
            if {$valist == 0x80} {
               append out "${not}Parental rating: all rated programmes\n"
            } elseif {$valist != 0xFF} {
               if {[string length $not] == 0} {
                  append out "Parental rating: ok for age $valist and up\n"
               } else {
                  append out "Parental rating: rated higher than for age $valist\n"
                  append out "                 (i.e. suitable only for age [expr {$valist + 1}] or elder)\n"
               }
            }
         }
         editorial {
            append out "${not}Editorial rating: $valist of 10\n"
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
               append out "${not}Program indices #$start..#$stop after NOW\n"
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
            append out "${not}Duration: from [Motd2HHMM [lindex $valist 0]] to [Motd2HHMM [lindex $valist 1]]\n"
         }
         vps_pdc {
            if {[lindex $valist 0] == 1} {
               append out "${not}VPS/PDC: restrict to programs with VPS/PDC code\n"
            } elseif {[lindex $valist 0] == 2} {
               append out "${not}VPS/PDC: restrict to programs with differing VPS/PDC code\n"
            }
         }
         piexpire {
            if {[lindex $valist 0] == -1} {
               append out "Expire time: show all expired programmes\n"
            } elseif {[lindex $valist 0] < 60} {
               append out "Expire time: include expired programmes up to [lindex $valist 0] minutes\n"
            } else {
               append out "Expire time: include expired programmes up to [expr int([lindex $valist 0]/60)]:[expr [lindex $valist 0]%60] hours\n"
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
               if [lindex $parlist 4] {
                  append out " equal to "
               } else {
                  append out " containing "
               }
               append out "'[lindex $parlist 0]'"
               if [lindex $parlist 3] {
                  append out " (matching case)"
               } elseif [lindex $parlist 4] {
                  append out " (ignoring case)"
               }
               append out "\n"
            }
         }
      }
   }
   return $out
}
