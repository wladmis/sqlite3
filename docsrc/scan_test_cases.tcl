#!/usr/bin/tclsh
#
# This script scans source code and scripts written in C, TCL, and HTML
# looking for comments that indicate that the script provides evidence or
# proof or an implementation for statements in the documentation.  The
# output (written to standard output) is a text file with one record per
# line.  The records are of the following form:
#
#      FILENAME  LINE-NUMBER  TYPE  REQUIREMENT
#
# The TYPE can be one of "testcase", "assert", "evidence", "implementation".
# The REQUIREMENT can be either a requirement number or a prefix of the
# requirement number or the text of the requirement.  If requirement text
# is provided, all HTML markup is removed and all whitespace sequences are
# collapsed into a single space character.
#
#
# The source comments come in several forms.  The most common is a comment
# that betweens at the left margin with "**" or "/*" and with one of the
# following keywords:
#
#     EV:
#     EVIDENCE-OF:
#     IMP:
#     IMPLEMENTATION:
#
# Following the keyword is either a requirement number of the form:
#
#     R-00000-00000-00000-00000-00000-00000-00000-00000
#
# Or a prefix of such a requirement (usually the first two 5-digit groups 
# suffice) or the original text of the requirement.  Original text can 
# continue onto subsequent lines.  The text is terminated by a blank line
# or by the end of the comment.
#
# The second form of the source comments are single-line comments that
# follow these templates:
#
#     /* R-00000-00000... */
#     /* EV: R-00000-00000... */
#     /* IMP: R-00000-00000... */
#
# The comment must contain a requirement number or requirement number
# prefix.  The TYPE of this comment is "assert" if it follows an "assert()"
# macro or "testcase" if it follows a "testcase()" macro, or "evidence" if
# the "EV:" template is used or "implementation" if the "IMP:" template is
# used, otherwise "implementation".
#
#
# COMMAND LINE:
#
# Use as follows:
#
#     tclsh scan_test_cases.tcl -dir DIR DIR/*.test  >>output.txt
#
# The -dir DIR argument specifies the directory name to substitute
# on the FILENAME entries of the output records.
#
##############################################################################
#
set dir .
for {set i 0} {$i<[llength $argv]-1} {incr i} {
  if {[lindex $argv $i]=="-dir"} {
    set dir [lindex $argv [expr {$i+1}]]
    set argv [lreplace $argv $i [expr {$i+1}]]
    break
  }
}

proc output_one_record {} {
  global filename linenumber type requirement
  regsub -all {\s+} [string trim $requirement] { } requirement
  regsub -all {\s?\*/$} $requirement {} requirement
  puts [list $filename $linenumber $type $requirement]
  set linenumber 0
}

foreach sourcefile $argv {
  set filename $dir/[file tail $sourcefile]
  set in [open $sourcefile]
  set lineno 0
  set linenumber 0
  while {![eof $in]} {
    incr lineno
    set line [gets $in]
    if {[regexp {^\s*(/\*|\*\*|#) (EV|EVIDENCE-OF|IMP|IMPLEMENTATION-OF): } \
         $line all mark type]} {
      if {$linenumber>0} output_one_record
      set linenumber $lineno
      if {[string index $type 0]=="E"} {
        set type evidence
      } else {
        set type implementation
      }
      regexp {[^:]+:\s+(.*)$} $line all requirement
      set requirement [string trim $requirement]
      continue
    }
    if {$linenumber>0} {
      if {[regexp {^\s*(\*\*|#)\s+([^\s].*)$} $line all commark tail]} {
        append requirement " [string trim $tail]"
        continue
      }
      output_one_record
    }
    if {[regexp {/\* (EV: |IMP: |)(R-\d[-\d]+\d) \*/} $line all tp rno]} {
      set linenumber $lineno
      if {$tp=="EV: "} {
        set type evidence
      } elseif {$tp=="IMP:"} {
        set type implementation
      } elseif {[regexp {assert\(} $line]} {
        set type assert
      } elseif {[regexp {testcase\(} $line]} {
        set type testcase
      } else {
        set type implementation
      }
      set requirement $rno
      output_one_record
    }
  }
  close $in
}
