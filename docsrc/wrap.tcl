#!/usr/bin/tclsh
#
# This script processes raw documentation source text into its final form 
# for display.  The processing actions are described below.
#
# Invoke this command as follows:
#
#       tclsh wrap.tcl $(DOC) $(SRC) $(DEST) source1.in source2.in ...
#
# The $(DOC) and $(SRC) values are the names of directories containing
# the documentation source and program source.  $(DEST) is the name of
# of the directory where generated HTML is written.  sourceN.in is the
# input file to be processed.  The output is sourceN.html in the
# local directory.
#
# Changes made to the source files:
#
#     *  An appropriate header is prepended to the file.  
#
#     *  Any <title>...</title> in the input is moved into the prepended
#        header.
#
#     *  An appropriate footer is appended.
#
#     *  Scripts within <tcl>...</tcl> are evaluated.  Output that
#        is emitted from these scripts by "hd_puts" or "hd_resolve"
#        procedures appears in place of the original script.
#
#     *  Hyperlinks within [...] are resolved.
#
# A two-pass algorithm is used.  The first pass collects the names of
# hyperlink targets, requirements text, and other global information.
# The second pass uses the data gathered on the first pass to generate
# the final output.
#
set DOC [lindex $argv 0]
set SRC [lindex $argv 1]
set DEST [lindex $argv 2]
set HOMEDIR [pwd]            ;# Also remember our home directory.

# Load the syntax diagram linkage data
#
source $DOC/art/syntax/syntax_linkage.tcl


# This is the first-pass implementation of procedure that renders
# hyperlinks.  Do not even bother trying to do anything during the
# first pass.  We have to collect keyword information before the
# hyperlinks are meaningful.  
#
proc hd_resolve {text} {
  hd_puts $text
}

# This is the second-pass implementation of the procedure that
# renders hyperlinks.  Convert all hyperlinks in $text into 
# appropriate <a href=""> markup.
#
# Links to keywords within the same main file are resolved using
# $::llink() if possible.  All other links and links that could
# not be resolved using $::llink() are resolved using $::glink().
# 
proc hd_resolve_2ndpass {text} {
  regsub -all {\[(.*?)\]} $text \
      "\175; hd_resolve_one \173\\1\175; hd_puts \173" text
  eval "hd_puts \173$text\175"
}
proc hd_resolve_one {x} {
  if {[string is integer $x]} {
    hd_puts \[$x\]
    return
  }
  set x2 [split $x |]
  set kw [string trim [lindex $x2 0]]
  if {[llength $x2]==1} {
    set content $kw
    regsub {\([^)]*\)} $content {} kw
    regsub {=.*} $kw {} kw
    regsub -all {[^a-zA-Z0-9_.#/ -]} $kw {} kw
  } else {
    regsub -all {[^a-zA-Z0-9_.#/ -]} $kw {} kw
    set content [string trim [lindex $x2 1]]
  }
  global hd llink glink backlink
  if {$hd(enable-main)} {
    set fn $hd(fn-main)
    if {$hd(fragment)!=""} {
      set srcurl $fn#$hd(fragment)
    } else {
      set srcurl $fn
    }
    if {[regexp {^[Tt]icket #(\d+)$} $kw all tktid]} {
      set url http://www.sqlite.org/cvstrac/tktview?tn=$tktid
      puts -nonewline $hd(main) \
        "<a href=\"$url\">$content</a>"
    } elseif {[info exists llink($fn:$kw)]} {
      puts -nonewline $hd(main) \
        "<a href=\"$hd(rootpath-main)$llink($fn:$kw)\">$content</a>"
    } elseif {[info exists glink($kw)]} {
      puts -nonewline $hd(main) \
        "<a href=\"$hd(rootpath-main)$glink($kw)\">$content</a>"
    } elseif {[regexp {\.gif$} $kw]} {
      puts -nonewline $hd(main) \
        "<img src=\"$hd(rootpath-main)images/$kw\">"
    } else {
      puts stderr "ERROR: unknown hyperlink target: $kw"
      puts -nonewline $hd(main) "<font color=\"red\">$content</font>"
    }
    lappend backlink($kw) $srcurl
  }
  if {$hd(enable-aux)} {
    if {[regexp {^[Tt]icket #(\d+)$} $kw all tktid]} {
      set url http://www.sqlite.org/cvstrac/tktview?tn=$tktid
      puts -nonewline $hd(aux) \
        "<a href=\"$url\">$content</a>"
    } elseif {[info exists glink($kw)]} {
      puts -nonewline $hd(aux) \
        "<a href=\"$hd(rootpath-aux)$glink($kw)\">$content</a>"
    } elseif {[regexp {\.gif$} $kw]} {
      puts -nonewline $hd(main) \
        "<img src=\"$hd(rootpath-aux)images/$kw\">"
    } else {
      puts stderr "ERROR: unknown hyperlink target: $kw"
      puts -nonewline $hd(aux) "<font color=\"red\">$content</font>"
    }
    lappend backlink($kw) $hd(fn-aux)
  }
}



# Record the fact that all keywords given in the argument list should
# cause a jump to the current location in the current file.
#
# If only the main output file is open, then all references to the
# keyword jump to the main output file.  If both main and aux are
# open then references in the main file jump to the main file and all
# other references jump to the auxiliary file.
#
# This procedure is only active during the first pass when we are
# collecting hyperlink information.  This procedure is redefined to
# be a no-op before the start of the second pass.
#
proc hd_keywords {args} {
  global glink llink hd
  if {$hd(fragment)==""} {
    set lurl $hd(fn-main)
  } else {
    set lurl "#$hd(fragment)"
  }
  set fn $hd(fn-main)
  if {[info exists hd(aux)]} {
    set gurl $hd(fn-aux)
  } else {
    set gurl {}
    if {$hd(fragment)!=""} {
      set lurl $hd(fn-main)#$hd(fragment)
    }
  }
  foreach a $args {
    if {[info exists glink($a)]} {
      puts stderr "WARNING: duplicate keyword \"$a\" - $glink($a) and $lurl"
    }
    if {$gurl==""} {
      set glink($a) $lurl
    } else {
      set glink($a) $gurl
      set llink($fn:$a) $lurl
    }
  }
}

# Start a new fragment in the main file.  Give the new fragment the
# indicated name.  Any keywords defined after this point will refer
# to the fragment, not to the beginning of the file.
#
# Only the main file may have fragments.  Auxiliary files are assumed
# to be small enough that fragments are not helpful.
#
proc hd_fragment {name args} {
  global hd
  set hd(fragment) $name
  puts $hd(main) "<a name=\"$name\"></a>"
  eval hd_keywords $args
}

# Write raw output to both the main file and the auxiliary.  Only write
# to files that are enabled.
#
proc hd_puts {text} {
  global hd
  if {$hd(enable-main)} {
    set fn $hd(fn-main)
    puts -nonewline $hd(main) $text
  }
  if {$hd(enable-aux)} {
    set fn $hd(fn-aux)
    puts -nonewline $hd(aux) $text
  }
  
  # Our pagelink processing based off the globals
  # llink, glink, and backlink generated during hd_resolve
  # processing doesn't catch links outputted directly
  # with hd_puts.  This code attempts to add those links to
  # our pagelink array.
  global pagelink
  set refs [regexp -all -inline {href=\"(.*?)\"} $text]
  foreach {href ref} $refs {
    regsub {#.*} $ref {} ref2
    regsub {http:\/\/www\.sqlite\.org\/} $ref2 {} ref3
    regsub {\.\.\/} $ref3 {} ref4
    if {[regexp {^http} $ref4]} continue
    if {$ref4==""} continue
    if {[regexp {\.html$} $ref4]} {
      lappend pagelink($ref4) $fn
    }
  }
}

# Enable or disable the main output file.
#
proc hd_enable_main {boolean} {
  global hd
  set hd(enable-main) $boolean
}

# Enable or disable the auxiliary output file.
#
proc hd_enable_aux {boolean} {
  global hd
  set hd(enable-aux) $boolean
}
set hd(enable-aux) 0

# Open the main output file.  $filename is relative to $::DEST.  
#
proc hd_open_main {filename} {
  global hd DEST
  hd_close_main
  set hd(fn-main) $filename
  set hd(rootpath-main) [hd_rootpath $filename]
  set hd(main) [open $DEST/$filename w]
  set hd(enable-main) 1
  set hd(fragment) {}
  global pagelink
  lappend pagelink($filename) $filename
}

# If $filename is a path from $::DEST to a file, return a path
# from the directory containing $filename back to the directory $::DEST.
#
proc hd_rootpath {filename} {
  set up {}
  set n [llength [split $filename /]]
  if {$n<=1} {
    return {}
  } else {
    return [string repeat ../ [expr {$n-1}]]
  }
}

# Close the main output file.
#
proc hd_close_main {} {
  global hd
  hd_close_aux
  if {[info exists hd(main)]} {
    puts $hd(main) $hd(footer)
    close $hd(main)
    unset hd(main)
  }
}

# Open the auxiliary output file.
#
# Most documents have only a main file and no auxiliary.  However, some
# large documents are broken up into smaller pieces where each smaller piece
# is an auxiliary file.  There will typically be either many auxiliary files
# or no auxiliary files associated with each main file.
#
proc hd_open_aux {filename} {
  global hd DEST
  hd_close_aux
  set hd(fn-aux) $filename
  set hd(rootpath-aux) [hd_rootpath $filename]
  set hd(aux) [open $DEST/$filename w]
  set hd(enable-aux) 1
  global pagelink
  lappend pagelink($filename) $filename
}

# Close the auxiliary output file
#
proc hd_close_aux {} {
  global hd
  if {[info exists hd(aux)]} {
    puts $hd(aux) $hd(footer)
    close $hd(aux)
    unset hd(aux)
    set hd(enable-aux) 0
    set hd(enable-main) 1
  }
}


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
proc hd_header {title {srcfile {}}} {
  global hd
  set saved_enable $hd(enable-main)
  if {$srcfile==""} {
    set fd $hd(aux)
    set path $hd(rootpath-aux)
  } else {
    set fd $hd(main)
    set path $hd(rootpath-main)
  }
  puts $fd {<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">}
  puts $fd {<html><head>}
  puts $fd "<title>$title</title>"
  putsin4 $fd {<style type="text/css">
    body {
        margin: auto;
        font-family: "Verdana" "sans-serif";
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
        <a href="http://www.sqlite.org/cvstrac/index">Developers</a>
        <a href="${path}support.html">Support</a>
      </div></div></div></div></div>
    </td></tr></table>
  }
  if {[file exists DRAFT]} {
    putsin4 $fd {
      <p align="center"><font size="6" color="red">*** DRAFT ***</font></p>
    }
  }
  if {$srcfile!=""} {
    if {[file exists DRAFT]} {
      set hd(footer) {
        <p align="center"><font size="6" color="red">*** DRAFT ***</font></p>
      }
    } else {
      set hd(footer) {}
    }
    append hd(footer) "<hr><small><i>\n"
    set mtime [file mtime $srcfile]
    set date [clock format $mtime -format {%Y/%m/%d %H:%M:%S UTC} -gmt 1]
    append hd(footer) "This page last modified $date\n"
    append hd(footer) "</i></small></div></body></html>"
  } else {
    set hd(enable-main) $saved_enable
  }
}

# Insert a bubble syntax diagram into the output.
#
proc BubbleDiagram {name {anonymous_flag 0}} {
  global hd

  #if {!$anonymous_flag} {
  #  hd_resolve "<h4>\[$name:\]</h4>"
  #}
  hd_resolve "<h4>\[$name:\]</h4>"
  if {$hd(enable-main)} {
    puts $hd(main) "<blockquote>\
        <img src=\"$hd(rootpath-main)images/syntax/$name.gif\"></img>\
        </blockquote>"
  }
  if {$hd(enable-aux)} {
    puts $hd(aux) "<blockquote>\
        <img src=\"$hd(rootpath-aux)images/syntax/$name.gif\"></img>\
        </blockquote>"
  }
}

# Record a requirement.  This procedure is active only for the first
# pass.  This procedure becomes a no-op for the second pass.  During
# the second pass, requirements listing report generators can use the
# data accumulated during the first pass to construct their reports.
#
# If the "verbatim" argument is true, then the requirement text is
# rendered as is.  In other words, the requirement text is assumed to
# be valid HTML with all hyperlinks already resolved.  If the "verbatim"
# argument is false (the default) then the requirement text is rendered
# using hd_render which will find an expand hyperlinks within the text.
#
# The "comment" argument is non-binding commentary and explanation that
# accompanies the requirement.
#
proc hd_requirement {id text derivedfrom comment} {
  global ALLREQ ALLREQ_DERIVEDFROM ALLREQ_COM
  if {[info exists ALLREQ($id)]} {
    puts stderr "duplicate requirement label: $id"
  }
  set ALLREQ_DERIVEDFROM($id) $derivedfrom
  set ALLREQ($id) $text
  set ALLREQ_COM($id) $comment
}

# Read a block of requirements from an ASCII text file.  Store the
# information obtained in a global variable named by the second parameter.
#
proc hd_read_requirement_file {filename varname} {
  global hd_req_rdr
  hd_reset_requirement_reader
  set in [open $filename]
  while {![eof $in]} {
    set line [gets $in]
    if {[regexp {^(HLR|UNDEF|SYSREQ) +([LHSU]\d+) *(.*)} $line all type rn df]} {
      hd_add_one_requirement $varname
      set hd_req_rdr(rn) $rn
      set hd_req_rdr(derived) $df
    } elseif {[string trim $line]==""} {
      if {$hd_req_rdr(body)==""} {
        set hd_req_rdr(body) $hd_req_rdr(comment)
        set hd_req_rdr(comment) {}
      } else {
        append hd_req_rdr(comment) \n
      }
    } else {
      append hd_req_rdr(comment) $line\n
    }
  }
  hd_add_one_requirement $varname
  close $in
  
}
proc hd_reset_requirement_reader {} {
  global hd_req_rdr
  set hd_req_rdr(rn) {}
  set hd_req_rdr(comment) {}
  set hd_req_rdr(body) {}
  set hd_req_rdr(derived) {}
}
proc hd_add_one_requirement {varname} {
  global hd_req_rdr
  set rn $hd_req_rdr(rn)
  if {$rn!=""} {
    if {$hd_req_rdr(body)==""} {
      set hd_req_rdr(body) $hd_req_rdr(comment)
      set hd_req_rdr(comment) {}
    }
    set b [string trim $hd_req_rdr(body)]
    set c [string trim $hd_req_rdr(comment)]
    set ::${varname}($rn) [list $hd_req_rdr(derived) $b $c]
    lappend ::${varname}(*) $rn
  }
  hd_reset_requirement_reader
}

# First pass.  Process all files.  But do not render hyperlinks.
# Merely collect keyword information so that hyperlinks can be
# correctly rendered on the second pass.
#
foreach infile [lrange $argv 3 end] {
  cd $HOMEDIR
  puts "Processing $infile"
  set fd [open $infile r]
  set in [read $fd]
  close $fd
  set title {No Title}
  regexp {<title>([^\n]*)</title>} $in all title
  regsub {<title>[^\n]*</title>} $in {} in
  set outfile [file root [file tail $infile]].html
  hd_open_main $outfile
  hd_header $title $infile
  regsub -all {<tcl>} $in "\175; eval \173" in
  regsub -all {</tcl>} $in "\175; hd_puts \173" in
  eval "hd_puts \173$in\175"
  cd $::HOMEDIR
  hd_close_main
}

# Second pass.  Process all files again.  This time render hyperlinks
# according to the keyword information collected on the first pass.
#
proc hd_keywords {args} {}
rename hd_resolve {}
rename hd_resolve_2ndpass hd_resolve
proc hd_requirement {args} {}
foreach infile [lrange $argv 3 end] {
  cd $HOMEDIR
  puts "Processing $infile"
  set fd [open $infile r]
  set in [read $fd]
  close $fd
  set title {No Title}
  regexp {<title>([^\n]*)</title>} $in all title
  regsub {<title>[^\n]*</title>} $in {} in
  set outfile [file root [file tail $infile]].html
  hd_open_main $outfile
  hd_header $title $infile
  regsub -all {<tcl>} $in "\175; eval \173" in
  regsub -all {</tcl>} $in "\175; hd_resolve \173" in
  eval "hd_resolve \173$in\175"
  cd $::HOMEDIR
  hd_close_main
}

# Generate a document showing the hyperlink keywords and their
# targets.
#
hd_open_main doc_keyword_crossref.html
hd_header {Keyword Crossreference} $DOC/wrap.tcl
hd_puts "<ul>"
foreach x [lsort -dict [array names glink]] {
  set y $glink($x)
  hd_puts "<li>$x - <a href=\"$y\">$y</a></li>"
  lappend revglink($y) $x
}
hd_puts "</ul>"
hd_close_main

hd_open_main doc_target_crossref.html
hd_header {Target Crossreference} $DOC/wrap.tcl
hd_puts "<ul>"
foreach y [lsort [array names revglink]] {
  hd_puts "<li><a href=\"$y\">$y</a> - [lsort $revglink($y)]</li>"
}
hd_puts "</ul>"
hd_close_main

hd_open_main doc_backlink_crossref.html
hd_header {Backlink Crossreference} $DOC/wrap.tcl
hd_puts "<ul>"
foreach kw [lsort [array names backlink]] {
  hd_puts "<li>$kw -"
  set prev {}
  foreach ref [lsort $backlink($kw)] {
    if {$ref==$prev} continue
    set prev $ref
    hd_puts "  <a href=\"$ref\">$ref</a>"
  }
}
hd_puts "</ul>"
hd_close_main

hd_open_main doc_pagelink_crossref.html
hd_header {Pagelink Crossreference} $DOC/wrap.tcl
hd_puts "<p>Target Page - Which pages reference it.</p>"
hd_puts "<p>Pages matching (news|changes|releaselog|\[0-9]to\[0-9]|^doc_.*_crossref) are skipped.</p>"
hd_puts "<ul>"
foreach y [lsort [array names revglink]] {
  regsub {#.*} $y {} y2
  foreach kw [lsort $revglink($y)] {
    if {[info exists backlink($kw)]} {
      foreach ref [lsort $backlink($kw)] {
        regsub {#.*} $ref {} ref2
        lappend pagelink($y2) $ref2
      }
    }
  }
}
foreach y [lsort [array names pagelink]] {
  if {[regexp {(news|changes|releaselog|[0-9]to[0-9]|^doc_.*_crossref)} $y]} continue
  hd_puts "<li><a href=\"$y\">$y</a> - "
  set prev {}
  foreach ref [lsort $pagelink($y)] {
    if {$ref==$prev} continue
    if {$ref==$y} continue
    if {[regexp {(news|changes|releaselog|[0-9]to[0-9]|^doc_.*_crossref)} $ref]} continue
    hd_puts "<a href=\"$ref\">$ref</a> "
    set prev $ref
  }
  hd_puts "</li>"
}
hd_puts "</ul>"
hd_close_main

