#!/usr/bin/wish
#
# I am D. Richard Hipp, the author of this code.  I hereby
# disavow all claims to copyright on this program and release
# it into the public domain. 
#
#                     D. Richard Hipp
#                     January 31, 2001
#
# As an historical record, the original copyright notice is
# reproduced below:
#
# Copyright (C) 1997,1998 D. Richard Hipp
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
# 
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA  02111-1307, USA.
#
# Author contact information:
#   drh@acm.org
#   http://www.hwaci.com/drh/
#
# #Revision: 1.7 #
#
#  nxtvepg $Id: htree.tcl,v 1.11 2020/06/15 09:58:34 tom Exp tom $



bind HTree <Button-1> {
  if {[winfo exists %W]} {
    tkHTreeBeginSelect %W [Tree:labelat %W @%x,%y]
  }
}
bind HTree <Double-1> {
  if {[winfo exists %W]} {
    tkHTreeOpenClose %W [Tree:labelat %W @%x,%y]
  }
}
bind HTree <B1-Motion> {
  set tkPriv(x) %x
  set tkPriv(y) %y
  tkHTreeMotion %W [Tree:labelat %W @%x,%y]
}
bind HTree <Shift-1> {
  tkHTreeBeginExtend %W [Tree:labelat %W @%x,%y]
}
bind HTree <Control-1> {
  tkHTreeBeginToggle %W [Tree:labelat %W @%x,%y]
}
bind HTree <Up> {
  tkHTreeUpDown %W -1
}
bind HTree <Shift-Up> {
  tkHTreeExtendUpDown %W -1
}
bind HTree <Down> {
  tkHTreeUpDown %W 1
}
bind HTree <Shift-Down> {
  tkHTreeExtendUpDown %W 1
}
bind HTree <Left> {
  %W xview scroll -1 units
}
bind HTree <Control-Left> {
  %W xview scroll -1 pages
}
bind HTree <Right> {
  %W xview scroll 1 units
}
bind HTree <Control-Right> {
  %W xview scroll 1 pages
}
bind HTree <Prior> {
  %W yview scroll -1 pages
  Tree:activate %W @0,0
}
bind HTree <Next> {
  %W yview scroll 1 pages
  Tree:activate %W @0,0
}
bind HTree <Control-Prior> {
  %W xview scroll -1 pages
}
bind HTree <Control-Next> {
  %W xview scroll 1 pages
}
bind HTree <Home> {
  %W yview moveto 0
}
bind HTree <End> {
  %W yview moveto 1
}
bind HTree <Control-Home> {
  Tree:activate %W first
  Tree:see %W first
  Tree:selection %W clear first end
  Tree:selection %W set first
  event generate %W <<TreeSelect>>
}
#bind HTree <Shift-Control-Home> {
#  tkHTreeDataExtend %W 0
#}
bind HTree <Control-End> {
  Tree:activate %W end
  Tree:see %W end
  Tree:selection %W clear first end
  Tree:selection %W set end
  event generate %W <<TreeSelect>>
}
#bind HTree <Shift-Control-End> {
#  tkHTreeDataExtend %W [Tree:labelat %W end]
#}
bind HTree <<Copy>> {
  if {[string equal [selection own -displayof %W] "%W"]} {
    clipboard clear -displayof %W
    clipboard append -displayof %W [selection get -displayof %W]
  }
}
bind HTree <Return> {
  if {[winfo exists %W]} {
    tkHTreeOpenClose %W [Tree:labelat %W active]
  }
}
#bind HTree <space> {
#  tkHTreeBeginSelect %W [Tree:labelat %W active]
#}
bind HTree <Select> {
  tkHTreeBeginSelect %W [Tree:labelat %W active]
}
bind HTree <Control-Shift-space> {
  tkHTreeBeginExtend %W [Tree:labelat %W active]
}
bind HTree <Shift-Select> {
  tkHTreeBeginExtend %W [Tree:labelat %W active]
}
#bind HTree <Escape> {
#  tkHTreeCancel %W
#}
bind HTree <Control-slash> {
  tkHTreeSelectAll %W
}
bind HTree <Control-backslash> {
  if {[string compare $Tree(%W:selectmode) "browse"]} {
    Tree:selection %W clear first end
    event generate %W <<TreeSelect>>
  }
}

# Additional Tk bindings that aren't part of the Motif look and feel:

bind HTree <2> {
  global Tree

  if {![info exists Tree(%W:cfmaxwidth)] ||
      ($Tree(%W:max_width) < $Tree(%W:cfmaxwidth))} {
    %W scan mark %x %y
  } else {
    %W scan mark 0 %y
  }
}
bind HTree <B2-Motion> {
  global Tree

  if {![info exists Tree(%W:cfmaxwidth)] ||
      ($Tree(%W:max_width) < $Tree(%W:cfmaxwidth))} {
    %W scan dragto %x %y 2
  } else {
    %W scan dragto 0 %y 2
  }
}

# The MouseWheel will typically only fire on Windows.  However,
# someone could use the "event generate" command to produce one
# on other platforms.

bind HTree <MouseWheel> {
    %W yview scroll [expr {- (%D / 120) * 4}] units
}

if {[string equal "unix" $::tcl_platform(platform)]} {
    # Support for mousewheels on Linux/Unix commonly comes through mapping
    # the wheel to the extended buttons.  If you have a mousewheel, find
    # Linux configuration info at:
    #	http://www.inria.fr/koala/colas/mouse-wheel-scroll/
    bind HTree <4> {
        if {!$::tk_strictMotif} {
            %W yview scroll -5 units
        }
    }
    bind HTree <5> {
        if {!$::tk_strictMotif} {
            %W yview scroll 5 units
        }
    }
}

proc tkHTreeBeginSelect {wid el} {
  global Tree

  if {($el != "") &&
      ![info exists Tree($wid:$el:disabled)]} {
    if {[string equal $Tree($wid:selectmode) "multiple"]} {
	if {[Tree:selection $wid includes $el]} {
          Tree:selection $wid clear $el
	} else {
          Tree:selection $wid set $el
	}
    } else {
      Tree:selection $wid clear first end
      Tree:selection $wid set $el
      Tree:selection $wid anchor $el
      set Tree(prevSel) $el
      set Tree(listboxSelection) {}
      event generate $wid <<TreeSelect>>
    }
  }
}

proc tkHTreeOpenClose {wid el} {
  global Tree

  if [info exists Tree($wid:$el:diropen)] {
    if {$Tree($wid:$el:diropen) == 0} {
      Tree:diropen $wid $el
    } else {
      Tree:dirclose $wid $el
    }
    event generate $wid <<TreeOpenClose>>
  }
}

proc tkHTreeMotion {wid el} {
  global Tree

  if {![info exists Tree(prevSel)] ||
      ![info exists Tree($wid:anchor)] ||
      ($el == $Tree(prevSel)) || ($el == "") ||
      [info exists Tree($wid:$el:disabled)]} {
    return
  }
  set anchor $Tree($wid:anchor)
  switch $Tree($wid:selectmode) {
    browse {
      Tree:selection $wid clear first end
      Tree:selection $wid set $el
      set Tree(prevSel) $el
      event generate $wid <<TreeSelect>>
    }
    extended {
      set i $Tree(prevSel)
      if {[string equal {} $i]} {
        set i $el
        Tree:selection $wid set $el
      }
      if {[Tree:selection $wid includes anchor]} {
        Tree:selection $wid clear $i $el
        Tree:selection $wid set anchor $el
      } else {
        Tree:selection $wid clear $i $el
        Tree:selection $wid clear anchor $el
      }
      if {![info exists Tree(listboxSelection)]} {
        set Tree(listboxSelection) [Tree:curselection $wid]
      }
      set ltree [Tree:unfold $wid /]
      set i_idx [lsearch -exact $ltree $i]
      set el_idx [lsearch -exact $ltree $el]
      set a_idx [lsearch -exact $ltree $anchor]

      while {($i_idx < $el_idx) && ($i_idx < $a_idx)} {
        set i_el [lindex $ltree $i_idx]
        if {[lsearch -exact $Tree(listboxSelection) $i_el] >= 0} {
          Tree:selection $wid set $i_el
        }
        incr i_idx
      }
      while {($i_idx > $el_idx) && ($i_idx > $a_idx)} {
        set i_el [lindex $ltree $i_idx]
        if {[lsearch -exact $Tree(listboxSelection) $i_el] >= 0} {
            Tree:selection $wid set $i_el
        }
        incr i_idx -1
      }
      set Tree(prevSel) $el
      event generate $wid <<TreeSelect>>
    }
  }
}

proc tkHTreeBeginExtend {wid el} {
  global Tree
  if {[string equal $Tree($wid:selectmode) "extended"]} {
    if {[Tree:selection $wid includes anchor]} {
      tkHTreeMotion $wid $el
    } else {
      # No selection yet; simulate the begin-select operation.
      tkHTreeBeginSelect $wid $el
    }
  }
}

proc tkHTreeBeginToggle {wid el} {
  global Tree

  if {[string equal $Tree($wid:selectmode) "extended"]} {

    set Tree(listboxSelection) [Tree:curselection $wid]
    set Tree(prevSel) $el
    Tree:selection $wid anchor $el
    if {[Tree:selection $wid includes $el]} {
      Tree:selection $wid clear $el
      event generate $wid <<TreeSelect>>
    } else {
      if {![info exists Tree($wid:$el:disabled)]} {
        Tree:selection $wid set $el
        event generate $wid <<TreeSelect>>
      }
    }
  }
}

proc Tree:prevnext {w v direction} {
  global Tree

  set ltree [Tree:unfold $w /]
  set idx [lsearch -exact $ltree $v]
  if {$idx != -1} {
    while 1 {
      incr idx $direction
      if {$idx < 0} {
        # TODO first or last may be disabled
        return [lindex $ltree 0]
      } elseif {$idx >= [llength $ltree]} {
        return [lindex $ltree end]
      } else {
        set el [lindex $ltree $idx]
        if {![info exists Tree($w:$el:disabled)] ||
            ($Tree($w:$el:disabled) == 1)} {
          return [lindex $ltree $idx]
        }
      }
    }
  } else {
    error "Tree:prevnext: Invalid start element '$v'"
  }
  return ""
}

proc tkHTreeUpDown {w amount} {
  global Tree

  set v [Tree:labelat $w active]
  if {$v != ""} {
    set v [Tree:prevnext $w $v $amount]
  } else {
    if {$amount > 0} {
      set v [Tree:labelat $w first]
    } else {
      set v [Tree:labelat $w end]
    }
  }

  if {$v != ""} {
    Tree:activate $w $v
    Tree:see $w active

    switch $Tree($w:selectmode) {
      browse {
        Tree:selection $w clear first end
        Tree:selection $w set active
        event generate $w <<TreeSelect>>
      }
      extended {
        Tree:selection $w clear first end
        Tree:selection $w set active
        Tree:selection $w anchor active
        set Tree(prevSel) [Tree:labelat $w active]
        set Tree(listboxSelection) {}
        event generate $w <<TreeSelect>>
      }
    }
  }
}

proc tkHTreeExtendUpDown {w amount} {
  global Tree

  if {[string compare $Tree($w:selectmode) "extended"]} {
      return
  }

  set active [Tree:labelat $w active]
  if {![info exists Tree(listboxSelection)]} {
    Tree:selection $w set $active
    set Tree(listboxSelection) [Tree:curselection $w]
  }

  set v [Tree:labelat $w active]
  if {$v != ""} {
    set v [Tree:prevnext $w $v $amount]

    Tree:activate $w $v
    Tree:see $w active
    tkHTreeMotion $w [Tree:labelat $w active]
  }
}

proc tkHTreeSelectAll w {
  set mode $Tree($w:selectmode)
  if {[string equal $mode "single"] || [string equal $mode "browse"]} {
    Tree:selection $w clear first end
    Tree:selection $w set active
  } else {
    Tree:selection $w set 0 end
    event generate $w <<TreeSelect>>
  }
}

#
# Create a new tree widget (from a canvas widget)
# - supports all options of the convas widget
#   plus several specific options (see below)
#
proc Tree:create {w args} {
  global Tree

  set Tree($w:/:children) {}
  set Tree($w:/:diropen) 1
  set Tree($w:selection) {}

  # default configuration
  set Tree($w:selectmode) browse
  set Tree($w:selectbackground) gray50
  set Tree($w:selectforeground) black
  set Tree($w:foreground) black
  set Tree($w:font) {Helvetica -12 bold}

  set canvas_args {}
  set char_width [font measure $Tree($w:font) "0"]
  set char_height [font metrics $Tree($w:font) -linespace]
  foreach {op arg} $args {
    switch -exact -- $op {
      -selectmode {set Tree($w:selectmode) $arg}
      -selectbackground {set Tree($w:selectbackground) $arg}
      -selectforeground {set Tree($w:selectforeground) $arg}
      -foreground {set Tree($w:foreground) $arg}
      -font {set Tree($w:font) $arg}
      -maxwidth {set Tree($w:cfmaxwidth) [expr $arg * $char_width]}
      -minwidth {set Tree($w:cfminwidth) [expr $arg * $char_width]}
      -maxheight {set Tree($w:cfmaxheight) [expr $arg * $char_height]}
      -minheight {set Tree($w:cfminheight) [expr $arg * $char_height]}
      -width {
        set arg [expr $arg * $char_width]
        if {($arg == 0) && ![info exists Tree($w:cfmaxwidth)]} {
          set Tree($w:cfmaxwidth) 0
        } elseif {$arg != 0} {
          lappend canvas_args $op $arg
        }
      }
      -height {
        set arg [expr $arg * $char_height]
        if {($arg == 0) && ![info exists Tree($w:cfmaxheight)]} {
          set Tree($w:cfmaxheight) 0
        } elseif {$arg != 0} {
          lappend canvas_args $op $arg
        }
      }
      default {lappend canvas_args $op $arg}
    }
  }

  eval canvas $w -takefocus 1 $canvas_args
  bindtags $w [list $w HTree . all]
  bind $w <Destroy> {Tree:destroyed %W}
  Tree:buildwhenidle $w
}

#
# Internal use only: clean-up after widget was destroyed
#
proc Tree:destroyed {w} {
  global Tree

  if [info exists Tree($w:selectpending)] {
    after cancel $Tree($w:selectpending)
  }
  if [info exists Tree($w:buildpending)] {
    after cancel $Tree($w:buildpending)
  }
  array unset Tree $w:*
}

#
# Pass configuration options to the tree widget
#
proc Tree:configure {w args} {
  # XXX TODO same args as for create -selectmode etc.
  eval $w configure $args
}

#
# Insert a new element $v into the tree $w.
#
proc Tree:insert {w v args} {
  global Tree
  set dir [file dirname $v]
  set n [file tail $v]
  if {![info exists Tree($w:$dir:diropen)]} {
    error "parent item \"$dir\" is missing"
  }
  if {[lsearch -exact $Tree($w:$dir:children) $n] != -1} {
    error "item \"$v\" already exists"
  }
  lappend Tree($w:$dir:children) $n
  set Tree($w:$n:path) $v
  set Tree($w:$v:tags) {}

  foreach {op arg} $args {
    switch -exact -- $op {
      -label {set Tree($w:$v:label) $arg}
      -separator {if $arg {set Tree($w:$v:separator) 1}}
      -state {
         if {[string compare $arg disabled] == 0} {
            set Tree($w:$v:disabled) 2
         } elseif {[string compare $arg noselect] == 0} {
            set Tree($w:$v:disabled) 1
         }
      }
      -image {set Tree($w:$v:icon) $arg}
      -tags {set Tree($w:$v:tags) $arg}
      -diropen {
         set Tree($w:$v:diropen) $arg
         set Tree($w:$v:children) {}
      }
    }
  }
  Tree:buildwhenidle $w
}

#
#  Change parameters for an item node
#
proc Tree:itemconfigure {w v args} {
  global Tree

  set v [Tree:labelat $w $v]
  if {[info exists Tree($w:$v:tags)]} {
    # XXX TODO
    foreach {op arg} $args {
      switch -exact -- $op {
        -label {set Tree($w:$v:label) $arg}
        default {error "Tree:itemconfigure: invalid operator: '$op'"}
      }
    }
    Tree:buildwhenidle $w
  } else {
    error "Tree:itemconfigure: element not found: '$v'"
  }
}

#
#  Returns configuration
#
proc Tree:itemcget {w v op} {
  global Tree

  set v [Tree:labelat $w $v]
  if [info exists Tree($w:$v:tags)] {
    switch -exact -- $op {
      -label {if [info exists Tree($w:$v:label)] {return $Tree($w:$v:label)}}
      -separator {return [info exists Tree($w:$v:separator)]}
      -state {
         if [info exists Tree($w:$v:disabled)] {
            if {$Tree($w:$v:disabled) == 2} {
               return "disabled"
            } elseif {$Tree($w:$v:disabled) == 1} {
               return "noselect"
            } else {
               return "normal"
            }
         }
      }
      -image {
         if [info exists Tree($w:$v:icon)] {
            return $Tree($w:$v:icon)
         } else {
            return ""
         }
      }
      -tags {return $Tree($w:$v:tags)}
      -diropen {
         if [info exists Tree($w:$v:diropen)] {
            return $Tree($w:$v:diropen)
         } else {
            return 0
         }
      }
      default {error "Tree:itemcget: invalid operator: '$op'"}
    }
  } else {
    error "Tree:itemcget: element not found: '$v'"
  }
}

#
# Delete element $v from the tree $w.
#
proc Tree:delitem {w v} {
  global Tree

  if {[string compare $v /] != 0} {
    set v [Tree:labelat $w $v]
  }
  Tree:delitem_rec $w $v

  Tree:buildwhenidle $w
}

proc Tree:delitem_rec {w v} {
  global Tree

  if {[string compare $v /] != 0} {
    if [info exists Tree($w:$v:diropen)] {
      foreach c $Tree($w:$v:children) {
        catch {Tree:delitem_rec $w $v/$c}
      }
    }
    #unset Tree($w:$v:diropen)
    #unset Tree($w:$v:children)
    #catch {unset Tree($w:$v:icon)}
    #catch {unset Tree($w:$v:separator)}
    #catch {unset Tree($w:$v:disabled)}
    #catch {unset Tree($w:$v:label)}
    array unset Tree "$w:$v:*"

    set dir [file dirname $v]
    set n [file tail $v]
    array unset Tree $w:$n:path
    set idx [lsearch -exact $Tree($w:$dir:children) $n]
    if {$idx != -1} {
      set Tree($w:$dir:children) [lreplace $Tree($w:$dir:children) $idx $idx]
    }
  } else {
    array unset Tree "$w:/*"
    set Tree($w:/:children) {}
    set Tree($w:/:diropen) 1
  }
}

#
# Change the selection to the indicated item
#
proc Tree:selection {w cmd {v {}} {v2 {}}} {
  global Tree

  switch -exact -- $cmd {
    anchor {
      set v [Tree:labelat $w $v]
      set Tree($w:anchor) $v
    }
    includes {
      set v [Tree:labelat $w $v]
      return [expr [lsearch -exact $Tree($w:selection) $v] != -1]
    }
    clear {
      set v [Tree:labelat $w $v]
      if {$v != ""} {
        if {$v2 == ""} {
          set vl $v
        } else {
          set v2 [Tree:labelat $w $v2]
          set vl [Tree:lrange $w $v $v2]
        }
        set tmpl {}
        foreach el $Tree($w:selection) {
          if {[lsearch -exact $vl $el] == -1} {
            lappend tmpl $el
          } elseif [info exists Tree($w:$el:tag)] {
             # undo -selectforeground
             $w itemconfigure $Tree($w:$el:tag) -fill $Tree($w:foreground)
          }
        }
        set Tree($w:selection) $tmpl
        Tree:drawselection $w
      }
    }
    set {
      set v [Tree:labelat $w $v]
      if {$v != ""} {
        if {$v2 == ""} {
          set vl $v
        } else {
          set v2 [Tree:labelat $w $v2]
          set vl [Tree:lrange $w $v $v2]
        }
        foreach el $Tree($w:selection) {
          set tmpa($el) 1
        }
        foreach el $vl {
          set tmpa($el) 1
        }
        set tmpl {}
        foreach el [Tree:unfold $w /] {
          if {[info exists tmpa($el)] &&
              ![info exists Tree($w:$el:disabled)]} {
            lappend tmpl $el
          }
        }
        set Tree($w:selection) $tmpl
        Tree:drawselection $w
      }
    }
  }
}

#
# Set activation cursor onto given element
#
proc Tree:activate {w v} {
  global Tree

  # remove mark from the previously marked element
  if {[info exists Tree($w:active_tag)]} {
    $w delete $Tree($w:active_tag)
    catch {unset Tree($w:active_tag)]}
  }

  set v [Tree:labelat $w $v]
  if [info exists Tree($w:$v:tags)] {
     set Tree($w:active) $v

     Tree:drawactive $w
  } else {
    error "Tree:activate: invalid label $v"
  }
}

#
# Mark the given element (underline text)
#
proc Tree:drawactive {w} {
  global Tree

  if [info exists Tree($w:active)] {
    set v $Tree($w:active)
    if [info exists Tree($w:$v:tag)] {
      set bbox [$w bbox $Tree($w:$v:tag)]
      if {[llength $bbox] == 4} {
        set x1 [lindex $bbox 0]
        set x2 [lindex $bbox 2]
        set y2 [expr [lindex $bbox 3] - 1]
        set Tree($w:active_tag) [$w create line $x1 $y2 $x2 $y2 -fill black]
      }
    } else {
      # active element was removed or is invisible
      unset Tree($w:active)
    }
  }
}

#
# Build flat list of all elements
#
proc Tree:unfold {w v} {
  global Tree
  if {$v=="/"} {
    set vx {}
  } else {
    set vx $v
  }
  set cl {}
  foreach c $Tree($w:$v:children) {
    lappend cl $vx/$c
    if [info exists Tree($w:$vx/$c:diropen)] {
      if {$Tree($w:$vx/$c:diropen)} {
         set cl [concat $cl [Tree:unfold $w $vx/$c]]
      }
    }
  }
  return $cl
}

#
# Return list of all element between and including v1 and v2
# - NOTE: index arguments must already be resolved
#
proc Tree:lrange {w v1 v2} {
  global Tree

  set ltree [Tree:unfold $w /]
  set i1 [lsearch -exact $ltree $v1]
  set i2 [lsearch -exact $ltree $v2]
  if {($i1 != -1) && ($i2 != -1)} {
    if {$i1 > $i2} {
      set tmp $i1
      set i1 $i2
      set i2 $tmp
    }
    return [lrange $ltree $i1 $i2]
  } else {
    error "Tree:lrange: invalid labels $v1 (idx $i1) or $v2 (idx $i2)"
    return {}
  }
}

# 
# Retrieve the current selection
#
proc Tree:curselection w {
  global Tree
  return $Tree($w:selection)
}

#
# Bitmaps used to show which parts of the tree can be opened.
#
set maskdata "#define solid_width 9\n#define solid_height 9"
append maskdata {
  static unsigned char solid_bits[] = {
   0xff, 0x01, 0xff, 0x01, 0xff, 0x01, 0xff, 0x01, 0xff, 0x01, 0xff, 0x01,
   0xff, 0x01, 0xff, 0x01, 0xff, 0x01
  };
}
set data "#define open_width 9\n#define open_height 9"
append data {
  static unsigned char open_bits[] = {
   0xff, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x7d, 0x01, 0x01, 0x01,
   0x01, 0x01, 0x01, 0x01, 0xff, 0x01
  };
}
image create bitmap Tree:openbm -data $data -maskdata $maskdata \
  -foreground black -background white
set data "#define closed_width 9\n#define closed_height 9"
append data {
  static unsigned char closed_bits[] = {
   0xff, 0x01, 0x01, 0x01, 0x11, 0x01, 0x11, 0x01, 0x7d, 0x01, 0x11, 0x01,
   0x11, 0x01, 0x01, 0x01, 0xff, 0x01
  };
}
image create bitmap Tree:closedbm -data $data -maskdata $maskdata \
  -foreground black -background white

# Internal use only.
# Draw the tree on the canvas
proc Tree:build w {
  global Tree
  $w delete all
  if [info exists Tree($w:buildpending)] {
     after cancel $Tree($w:buildpending)
     unset Tree($w:buildpending)
  }
  set yoff 10
  set Tree($w:max_width) 0
  set Tree($w:separator_tags) {}
  Tree:buildlayer $w / 10 yoff
  $w configure -scrollregion [$w bbox all]
  Tree:drawselection $w
  Tree:drawactive $w

  # adjust separator width after all elements are drawn
  foreach tag $Tree($w:separator_tags) {
    eval $w coords $tag [lreplace [$w coords $tag] 2 2 $Tree($w:max_width)]
  }
  unset Tree($w:separator_tags)

  if [info exists Tree($w:see_after_build)] {
    Tree:see $w $Tree($w:see_after_build)
    unset Tree($w:see_after_build)
  }

  if [info exists Tree($w:cfmaxheight)] {
    if {[info exists Tree($w:cfminheight)] && ($yoff < $Tree($w:cfminheight))} {
      $w configure -height $Tree($w:cfminheight)
    } elseif {($yoff <= $Tree($w:cfmaxheight)) || ($Tree($w:cfmaxheight) == 0)} {
      $w configure -height $yoff
    } else {
      $w configure -height $Tree($w:cfmaxheight)
    }
  }
  if [info exists Tree($w:cfmaxwidth)] {
    if {[info exists Tree($w:cfminwidth)] && ($Tree($w:max_width) < $Tree($w:cfminwidth))} {
      $w configure -width $Tree($w:cfminwidth)
    } elseif  {($Tree($w:max_width) <= $Tree($w:cfmaxwidth)) || ($Tree($w:cfmaxwidth) == 0)} {
      $w configure -width $Tree($w:max_width)
    } else {
      $w configure -width $Tree($w:cfmaxwidth)
    }
  }
}

# Internal use only.
# Build a single layer of the tree on the canvas.
# - indent horizontally by $xoff pixels
# - vertical start offset passed by reference: modified inside
proc Tree:buildlayer {w v xoff yoff_ref} {
  global Tree
  upvar $yoff_ref yoff

  if {$v=="/"} {
    set vx {}
  } else {
    set vx $v
  }
  set start [expr $yoff-10]
  set lh [font metrics $Tree($w:font) -linespace]
  set y $yoff
  foreach c $Tree($w:$v:children) {
    if {[info exists Tree($w:$vx/$c:separator)]} {
      # separator: 10px height
      set y [expr $yoff - 4]
      incr yoff 9
    } else {
      # normal entry: test or image
      set y $yoff
      incr yoff [expr $lh + 2]
    }
    if {[info exists Tree($w:$vx/$c:separator)]} {
      set x [expr $xoff+10]
      set el_width 0
      set j [$w create line $x $y $x $y -fill #707074 -tags $Tree($w:$vx/$c:tags)]
      set Tree($w:tag:$j) $vx/$c
      lappend Tree($w:separator_tags) $j
      set j [$w create line $x [expr $y + 1] $x [expr $y + 1] -fill #ffffff -tags $Tree($w:$vx/$c:tags)]
      set Tree($w:tag:$j) $vx/$c
      lappend Tree($w:separator_tags) $j
    } else {
      set x [expr $xoff+12]
      $w create line $xoff $y [expr $xoff+10] $y -fill gray50 
      if {[info exists Tree($w:$vx/$c:icon)]} {
        set k [$w create image $x $y -image $Tree($w:$vx/$c:icon) -anchor w \
                                     -tags $Tree($w:$vx/$c:tags)]
        incr x [image width $Tree($w:$vx/$c:icon)]
        set Tree($w:tag:$k) $vx/$c
      }
      if {[info exists Tree($w:$vx/$c:label)]} {
        set clabel $Tree($w:$vx/$c:label)
      } else {
        set clabel $c
      }
      set j [$w create text $x $y -text $clabel -font $Tree($w:font) -fill $Tree($w:foreground) \
                                  -anchor w -tags $Tree($w:$vx/$c:tags)]
      set Tree($w:tag:$j) $vx/$c
      set el_width [expr $x + [font measure $Tree($w:font) $clabel]]
    }
    set Tree($w:$vx/$c:tag) $j

    if {$el_width > $Tree($w:max_width)} {
      set Tree($w:max_width) $el_width
    }
    if [info exists Tree($w:$vx/$c:diropen)] {
      if {$Tree($w:$vx/$c:diropen)} {
         set j [$w create image $xoff $y -image Tree:openbm]
         Tree:buildlayer $w $vx/$c [expr $xoff+18] yoff
      } else {
         set j [$w create image $xoff $y -image Tree:closedbm]
      }
      $w bind $j <Button-1> [list tkHTreeOpenClose %W $vx/$c]
    }
  }
  if {$Tree($w:$v:children) > 0 } {
     set j [$w create line $xoff $start $xoff [expr $y+1] -fill gray50]
     $w lower $j
  }
}

#
# Open a branch of a tree
#
proc Tree:diropen {w v} {
  global Tree
  if {[info exists Tree($w:$v:diropen)] && ($Tree($w:$v:diropen) == 0)} {
    set Tree($w:$v:diropen) 1
    Tree:build $w
  }
}

proc Tree:dirclose {w v} {
  global Tree
  if {[info exists Tree($w:$v:diropen)] && ($Tree($w:$v:diropen) == 1)} {
    set Tree($w:$v:diropen) 0
    Tree:build $w
  }
}

# Internal use only.
# Draw the selection highlight
proc Tree:drawselection w {
  global Tree
  catch {$w delete sel sel_frm}
  if [info exists Tree($w:selectpending)] {
     after cancel $Tree($w:selectpending)
     unset Tree($w:selectpending)
  }
  set tmpl {}
  foreach v $Tree($w:selection) {
    if {[info exists Tree($w:$v:tag)]} {
      set bbox [$w bbox $Tree($w:$v:tag)]
      if {[llength $bbox] == 4} {
        set x1 [lindex $bbox 0]
        set y1 [expr [lindex $bbox 1] - 2]
        set x2 [expr $Tree($w:max_width) + 2]
        set y2 [expr [lindex $bbox 3] + 1]
        $w create rectangle [expr $x1 + 1] [expr $y1 + 1] $x2 $y2 -fill $Tree($w:selectbackground) -outline {} -tags sel
        $w create line $x1 $y2 $x1 $y1 $x2 $y1 -fill #ffffff -tags sel_frm
        $w create line $x2 $y1 $x2 $y2 $x1 $y2 -fill #707074 -tags sel_frm
        $w itemconfigure $Tree($w:$v:tag) -fill $Tree($w:selectforeground)
      }
    }
  }
  catch {$w lower sel}
  catch {$w lower sel_frm}
}

# Internal use only
# Call Tree:drawselection then next time we're idle
proc Tree:selectwhenidle w {
  global Tree

  if {![info exists Tree($w:buildpending)]} {
    if {![info exists Tree($w:selectpending)]} {
      set Tree($w:selectpending) [after idle "Tree:drawselection $w"]
    }
  }
}

# Internal use only
# Call Tree:build then next time we're idle
proc Tree:buildwhenidle w {
  global Tree

  if {![info exists Tree($w:buildpending)]} {
    set Tree($w:buildpending) [after idle "Tree:build $w"]
  }
}

#
# Return the full pathname of the label for widget $w that is located
# at real coordinates $x, $y
#
proc Tree:labelat {w idx} {
  global Tree

  if {[scan $idx "@%d,%d" x y] == 2} {
    set x [$w canvasx $x]
    set y1 [$w canvasy $y]
    set y2 [expr [$w canvasy $y] + 2]
    foreach m [$w find overlapping 0 $y1 1000 $y2] {
      if {[info exists Tree($w:tag:$m)]} {
        return $Tree($w:tag:$m)
      }
    }
  } elseif {[string compare $idx "first"] == 0} {
    if {[info exists Tree($w:/:children)] && \
        [llength $Tree($w:/:children)] > 0} {
      return "/[lindex $Tree($w:/:children) 0]"
    }  else {
      return ""
    }

  } elseif {[string compare $idx "end"] == 0} {
    return [lindex [Tree:unfold $w /] end]

  } elseif {[string compare $idx "anchor"] == 0} {
    if [info exists Tree($w:anchor)] {
      return $Tree($w:anchor)
    } else {
      return ""
    }
  } elseif {[string compare $idx "active"] == 0} {
    if [info exists Tree($w:active)] {
      return $Tree($w:active)
    } else {
      return ""
    }
  } elseif {[string compare -length 1 $idx "/"] == 0} {
    if {![info exists Tree($w:$idx:tags)]} {
      error "invalid element path '$idx'"
    }
    return $idx
  } else {
    if {![info exists Tree($w:$idx:path)]} {
      error "invalid element name '$idx'"
    }
    return $Tree($w:$idx:path)
  }
}

#
# Bring the given element into view
#
proc Tree:see {w v} {
  global Tree

  if {![info exists Tree($w:buildpending)]} {
    set v [Tree:labelat $w $v]
    if {[info exists Tree($w:$v:tag)]} {
      set bbox [$w bbox $Tree($w:$v:tag)]
      if {[llength $bbox] == 4} {
        set x1 [lindex $bbox 0]
        set y1 [lindex $bbox 1]
        set x2 [lindex $bbox 2]
        set y2 [lindex $bbox 3]

        #set minx [$w canvasx 0]
        set miny [$w canvasy 0]
        #set maxx [$w canvasx [winfo width $w]]
        set maxy [$w canvasy [winfo height $w]]

        if {$y1 < $miny} {
          $w scan mark 0 0
          $w scan dragto 0 [expr int($miny - $y1) + 3] 1
        } elseif {$y2 > $maxy} {
          $w scan mark 0 [expr int($y2) + 3]
          $w scan dragto 0 [expr int($maxy)] 1
        }
      }
    } else {
      # no error if element exists, but is in a closed directory
      if {![info exists Tree($w:$v:tags)]} {
         error "invalid element name '$v'"
      }
    }
  } else {
    set Tree($w:see_after_build) $v
  }
}

