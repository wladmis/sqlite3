#!/usr/bin/tclsh
#
# This script generates the requirements traceability matrix and does
# other processing related to requirements and coverage analysis.
#

# Get a list of source HTML files.
#
set filelist [lsort [glob -nocomplain doc/*.html doc/c3ref/*.html]]
foreach exclude {doc/capi3ref.html doc/changes.html} {
  set i [lsearch $filelist $exclude]
  set filelist [lreplace $filelist $i $i]
}


# Extract requirement text from all files in the list.
#
# Requirements text is text between "^" and "." or between "^(" and ")^".
# Requirement text is normalized by removing all HTML markup, removing
# all whitespace from the beginning and end, and converting all internal
# whitespace sequences into a single space character.
#
# Syntax diagrams are considered their own requirement if they are
# embedded using markup of the following patter:
#
#    <img alt="syntax diagram NAME" src="FILENAME.gif">
#
# Let TEXT be the normalized text of a requirement and let ORIGTEXT be
# the original unnormalized text.  Let FILE be the name of an input file.
# TCL variables constructed are as follows:
#
#    reqtext(TEXT)       Value [list FILE ORIGTEXT]
#    allreq              List of all TEXT values in order seen.
#
# For the special syntax diagram markup, the value of TEXT is
# "syntax diagram NAME" and ORIGTEXT is an appropriate <img> markup.
# In addition, the following variables are set:
#
#    reqimage(TEXT)              Set to FILENAME.gif
#    reqimgfile(FILENAME.gif)    Set to TEXT
#
puts "Scanning documentation for testable statements..."
flush stdout
set allreq {}
foreach file $filelist {
  set in [open $file]
  set x [read $in [file size $file]]
  close $in
  set orig_x $x
  while {[string length $x]>0 && [regsub {^.*?\^} $x {} nx]} {
    set c [string index $nx 0]
    if {$c=="("} {
      regexp {^\((([^<]|<.+?>)*?)\)\^} $nx all req
      regsub {^\((([^<]|<.+?>)*?)\)\^} $nx {} nx
    } else {
      regexp {^([^<]|<.+?>)*?\.} $nx req
      regsub {^([^<]|<.+?>)*?\.} $nx {} nx
    }
    set orig [string trim $req]
    regsub -all {<.+?>} $orig {} req
    regsub -all {\s+} [string trim $req] { } req
    set req [string trim $req]
    set reqtext($req) [list $file $orig]
    lappend allreq $req
    set x $nx
  }
  set x $orig_x
  unset orig_x
  while {[string length $x]>0 
     && [regexp {^(.+?)(<img alt="syntax diagram .*)$} $x all prefix suffix]} {
    set x $suffix
    if {[regexp \
           {<img alt="(syntax diagram [-a-z]+)" src="[./]*([-./a-z]+\.gif)"} \
           $x all name image]} {
      #puts "DIAGRAM: $file $name $image"
      set req $name
      set orig "<img src=\"$image\">"
      set reqtext($req) [list $file $orig]
      set reqimage($req) $image
      set reqimgfile($image) $req
      lappend allreq $req
    }
  }
}

# Compute requirement numbers based on the normalized requirement text.
# Let TEXT be the normalized requirement text and let REQNUM be the
# requirement number in the R-00000-00000-....-00000 format.  Then:
#
#    reqnum(REQNUM)        Set to TEXT.
#    reqtexttonum(TEXT)    Set to REQNUM
#
puts "Computing requirement numbers..."
flush stdout
set out [open ./reqtxt.txt w]
foreach req [array names reqtext] {
  if {[info exists reqimage($req)]} {
    puts $out ^doc/$reqimage($req)
  } else {
    puts $out $req
  }
}
close $out
exec ./md5 reqtxt.txt >reqnum.txt
set in [open ./reqnum.txt]
while {![eof $in]} {
  set rnum [gets $in]
  if {$rnum=="" && [eof $in]} break
  set rtxt [string trim [gets $in]]
  if {![regexp {^R(-\d\d\d\d\d)+$} $rnum]} {
    error "bad requirement number: $rnum"
  }
  if {[regexp {^\^doc/(.*\.gif)$} $rtxt all image]} {
    set rtxt $reqimgfile($image)
  }
  if {![info exists reqtext($rtxt)]} {
    error "unknown requirement text: $rtxt"
  }
  set reqnum($rnum) $rtxt
  set reqtexttonum($rtxt) $rnum
}
close $in
#file delete -force ./reqtxt.txt ./reqnum.txt

# Read the fulfillment database file from evidence.txt file.
# Translate the fourth column in to complete requirement numbers.
#
puts "Mapping requirements to implementation and test evidence..."
flush stdout
set in [open ./evidence.txt r]
while {![eof $in]} {
  set line [gets $in]
  if {[llength $line]<4} continue
  set filename [lindex $line 0]
  set linenumber [lindex $line 1]
  set type [lindex $line 2]
  set rno [lindex $line 3]
  if {[regexp {^(R-\d\d\d\d[-\d]+)(.*)} $rno all rx tx]} {
    set rlist [array names reqnum $rx*]
    if {[llength $rlist]>1} {
      puts stderr "$filename:$linenumber: ambiguous requirement $rno"
      puts stderr "    choices: $rlist"
      continue
    }
    if {[llength $rlist]==0} {
      puts stderr "$filename:$linenumber: no such requirement: $rno"
      continue
    }
    set rno [lindex $rlist 0]
    set tx [string trim $tx]
    if {$tx!="" && $tx!=$reqnum($rno)} {
      puts stderr "$filename:$linenumber: requirement number/text mismatch"
      continue
    }
    lappend fulfill($type,$rno) [list $filename $linenumber]
  } else {
    if {![info exists reqtexttonum($rno)]} {
      puts stderr "$filename:$linenumber: no such requirement ($rno)"
      continue
    }
    set rno $reqtexttonum($rno)
    lappend fulfill($type,$rno) [list $filename $linenumber]
  }
}
close $in


########################################################################
# Header output routine adapted from wrap.tcl.  Keep the two in sync.
#
# hd_putsin4 is like puts except that it removes the first 4 indentation
# characters from each line.  It also does variable substitution in
# the namespace of its calling procedure.
#
proc putsin4 {fd text} {
  regsub -all "\n    " $text \n text
  puts $fd [uplevel 1 [list subst -noback -nocom $text]]
}

# A procedure to write the common header found on every HTML file on
# the SQLite website.
#
proc write_header {path fd title} {
  puts $fd {<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">}
  puts $fd {<html><head>}
  puts $fd "<title>$title</title>"
  putsin4 $fd {<style type="text/css">
    body {
        margin: auto;
        font-family: Verdana, sans-serif;
        padding: 8px 1%;
    }
    
    a { color: #45735f }
    a:visited { color: #734559 }
    
    .logo { position:absolute; margin:3px; }
    .tagline {
      float:right;
      text-align:right;
      font-style:italic;
      width:240px;
      margin:12px;
      margin-top:58px;
    }
    
    .toolbar {
      font-variant: small-caps;
      text-align: center;
      line-height: 1.6em;
      margin: 0;
      padding:1px 8px;
    }
    .toolbar a { color: white; text-decoration: none; padding: 6px 12px; }
    .toolbar a:visited { color: white; }
    .toolbar a:hover { color: #80a796; background: white; }
    
    .content    { margin: 5%; }
    .content dt { font-weight:bold; }
    .content dd { margin-bottom: 25px; margin-left:20%; }
    .content ul { padding:0px; padding-left: 15px; margin:0px; }
    
    /* rounded corners */
    .se  { background: url(${path}images/se.png) 100% 100% no-repeat #80a796}
    .sw  { background: url(${path}images/sw.png) 0% 100% no-repeat }
    .ne  { background: url(${path}images/ne.png) 100% 0% no-repeat }
    .nw  { background: url(${path}images/nw.png) 0% 0% no-repeat }


    /* Text within colored boxes.
    **  everr is red.  evok is green. evnil is white */
    .everr {
      font-family: monospace;
      font-style: normal;
      background: #ffa0a0;
      border-style: solid;
      border-width: 2px;
      border-color: #a00000;
      padding: 0px 5px 0px 5px;
    }
    .evok {
      font-family: monospace;
      font-style: normal;
      background: #a0ffa0;
      border-style: solid;
      border-width: 2px;
      border-color: #00a000;
      padding: 0px 5px 0px 5px;
    }
    .evnil {
      font-family: monospace;
      font-style: normal;
      border-style: solid;
      border-width: 1px;
      padding: 0px 5px 0px 5px;
    }
    .ev {
      font-family: monospace;
      padding: 0px 5px 0px 5px;
    }
    

    </style>
    <meta http-equiv="content-type" content="text/html; charset=UTF-8">
  }
  puts $fd {</head>}
  putsin4 $fd {<body>
    <div><!-- container div to satisfy validator -->
    
    <a href="${path}index.html">
    <img class="logo" src="${path}images/SQLite.gif" alt="SQLite Logo"
     border="0"></a>
    <div><!-- IE hack to prevent disappearing logo--></div>
    <div class="tagline">Small. Fast. Reliable.<br>Choose any three.</div>

    <table width=100% style="clear:both"><tr><td>
      <div class="se"><div class="sw"><div class="ne"><div class="nw">
      <div class="toolbar">
        <a href="${path}about.html">About</a>
        <a href="${path}sitemap.html">Sitemap</a>
        <a href="${path}docs.html">Documentation</a>
        <a href="${path}download.html">Download</a>
        <a href="${path}copyright.html">License</a>
        <a href="${path}news.html">News</a>
        <a href="${path}dev.html">Developers</a>
        <a href="${path}support.html">Support</a>
      </div></div></div></div></div>
    </td></tr></table>
  }
  if {[file exists DRAFT]} {
    putsin4 $fd {
      <p align="center"><font size="6" color="red">*** DRAFT ***</font></p>
    }
  }
}
# End of code copied out of wrap.tcl
##############################################################################

# Generate the requirements traceability matrix.
#
puts "Generating requirements matrix..."
flush stdout
set out [open doc/matrix/matrix.html w]
write_header ../ $out {SQLite Requirements Matrix}
puts $out {<h1 align="center">SQLite Requirements Matrix</h1>}
set prev_filename {}
set indl 0
foreach req $allreq {
  foreach {filename origtxt} $reqtext($req) break
  set reqno $reqtexttonum($req)
  if {$filename!=$prev_filename} {
    regsub {^doc/} $filename {} fn
    if {$indl} {
      puts $out {</dl>}
      set indl 0
    }
    puts $out "<h2>Requirements from <a href=\"$fn\">$fn</a></h2>"
    set prev_filename $filename
  }
  if {!$indl} {
    puts $out "<dl>"
    set indl 1
  }
  puts $out "<dt><a name=\"$reqno\"></a>"
  puts $out "<p><a href=\"$fn#$reqno\">$reqno</a>"
  set cth3 everr
  set ctcl everr
  set cother evnil
  if {[info exists fulfill(evidence,$reqno)]} {
    foreach e $fulfill(evidence,$reqno) {
      if {[regexp {th3/} $e]} {
        set cth3 evok
      } elseif {[regexp {tcltest/} $e]} {
        set ctcl evok
      } else {
        set cother evok
      }
    }
  }
  puts $out "<cite class=$cth3>th3</cite> <cite class=$ctcl>tcl</cite>"
  puts $out "<cite class=$cother>other</cite>"
  puts $out "</p></dt>"
  puts $out "<dd><p>$origtxt</p>"
  foreach {key label proof} {
    evidence {Test evidence} 1 
    assert {Assertions} 1
    testcase {Case checks} 0
    implementation {Implementation} 0
  } {
    if {[info exists fulfill($key,$reqno)]} {
      puts $out "<p><b>$label:</b>"
      foreach e $fulfill($key,$reqno) {
        foreach {filename lineno} $e break
        puts $out "$filename:$lineno"
        if {$proof} {
          if {[regexp {^th3/} $filename]} {set proven_th3($reqno) 1}
          if {[regexp {^tcltest/} $filename]} {set proven_tcl($reqno) 1}
        }
      }
      puts $out </p>
      if {$proof} {
        set proven($reqno) 1
      }
    }
  }
  puts $out "</dd>"
}
if {$indl} {
  puts $out {</dl>}
}
close $out

# Alternative requirements matrix that shows only the requirement numbers
#
set out [open doc/matrix/matrix2.html w]
write_header ../ $out {SQLite Requirements Matrix}
puts $out {<h1 align="center">SQLite Requirements Matrix</h1>}
set prev_filename {}
set inul 0
foreach req $allreq {
  foreach {filename origtxt} $reqtext($req) break
  set reqno $reqtexttonum($req)
  if {$filename!=$prev_filename} {
    regsub {^doc/} $filename {} fn
    if {$inul} {
      puts $out {</ul>}
      set inul 0
    }
    puts $out "<h2>Requirements from <a href=\"$fn\">$fn</a></h2>"
    set prev_filename $filename
  }
  if {!$inul} {
    puts $out "<ul style=ev>"
    set inul 1
  }
  puts $out "<li><a class=ev href=\"$fn#$reqno\">$reqno</a>"
  set cth3 everr
  set ctcl everr
  set cother evnil
  if {[info exists fulfill(evidence,$reqno)]} {
    foreach e $fulfill(evidence,$reqno) {
      if {[regexp {th3/} $e]} {
        set cth3 evok
      } elseif {[regexp {tcltest/} $e]} {
        set ctcl evok
      } else {
        set cother evok
      }
    }
  }
  puts $out "<cite class=$cth3>th3</cite> <cite class=$ctcl>tcl</cite>"
  puts $out "<cite class=$cother>other</cite>"
  puts $out "</li>"
}
if {$inul} {
  puts $out {</ul>}
}
close $out

# Translate documentation to show requirements with links to the matrix.
#
puts "Translating documentation..."
flush stdout
foreach file $filelist {
  set in [open $file]
  set x [read $in [file size $file]]
  close $in
  regsub {^doc/} $file {doc/matrix/} outfile
  if {[regexp / [file dir $file]]} {
    set matrixpath ../matrix.html
  } else {
    set matrixpath matrix.html
  }
  set out {}
  while {[string length $x]>0 && [regexp {^(.*?)\^} $x all prefix]} {
    append out $prefix
    set n [string length $prefix]
    set nx [string range $x [expr {$n+1}] end]
    set c [string index $nx 0]
    if {$c=="("} {
      regexp {^\((([^<]|<.+?>)*?)\)\^} $nx all req
      regsub {^\((([^<]|<.+?>)*?)\)\^} $nx {} nx
    } else {
      regexp {^([^<]|<.+?>)*?\.} $nx req
      regsub {^([^<]|<.+?>)*?\.} $nx {} nx
    }
    set orig [string trim $req]
    regsub -all {<.+?>} $orig {} req
    regsub -all {\s+} [string trim $req] { } req
    set req [string trim $req]
    set rno $reqtexttonum($req)
    set shortrno [string range $rno 0 12]
    append out "<a name=\"$rno\"></a><font color=\"blue\"><b>\n"
    set link "<a href=\"$matrixpath#$rno\" style=\"color: #0000ff\">"
    append out "$link$shortrno</a>:\[</b></font>"
    if {[info exists proven($rno)]} {
      if {[info exists proven_tcl($rno)] && [info exists proven_th3($rno)]} {
        set clr green
      } else {
        set clr orange
      }
    } else {
      set clr red
    }
    append out "<font color=\"$clr\">$orig</font>\n"
    append out "<font color=\"blue\"><b>\]</b></font>\n"
    set x $nx
  }
  append out $x
  set x $out
  set out {}
  while {[string length $x]>0 
     && [regexp {^(.+?)(<img alt="syntax diagram .*)$} $x all prefix suffix]} {
    append out $prefix
    set x $suffix
    if {[regexp \
           {<img alt="(syntax diagram [-a-z]+)" src="[./]*([-./a-z]+\.gif)"} \
           $x all name image]} {
      #puts "DIAGRAM: $file $name $image"
      set req $name
      set rno $reqtexttonum($req)
      set shortrno [string range $rno 0 12]
      append out "<a name=\"$rno\"></a><font color=\"blue\"><b>"
      set link "<a href=\"$matrixpath#$rno\" style=\"color: #0000ff\">"
      append out "$link$shortrno</a>:\[</b></font>\n"
      if {[info exists proven($rno)]} {
        if {[info exists proven_tcl($rno)] && [info exists proven_th3($rno)]} {
          set clr green
        } else {
          set clr orange
        }
      } else {
        set clr red
      }
      append out "<img border=3 style=\"border-color: $clr\" src=\"$image\">"
      append out "<font color=\"blue\"><b>\]</b></font>\n"
      regsub {.+?>} $x {} x
    }
  }
  append out $x
  set outfd [open $outfile w]
  puts -nonewline $outfd $out
  close $outfd
}
