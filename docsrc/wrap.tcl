#!/usr/bin/tclsh
#
# This script processes raw page text into its final form for display.
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
#     *  Any <title>...</title> in the input is moved into the prepended
#        header.
#     *  An appropriate footer is appended.
#     *  Scripts within <tcl>...</tcl> are evaluated.  Output that
#        is emitted from these scripts by "puts" appears in place of
#        the original script.
#
set DOC [lindex $argv 0]
set SRC [lindex $argv 1]
set DEST [lindex $argv 2]
set HOMEDIR [pwd]            ;# Also remember our home directory.

# We are going to overload the puts command, so remember the
# original puts command using an alternative name.
rename puts real_puts
proc puts {text} {
  real_puts $::OUT $text
  flush $::OUT
}

# putsin4 is like puts except that it removes the first 4 indentation
# characters from each line.  It also does variable substitution in
# the namespace of its calling procedure.
#
proc putsin4 {text} {
  regsub -all "\n    " $text \n text
  real_puts $::OUT [uplevel 1 [list subst -noback -nocom $text]]
  flush $::OUT
}

# A procedure to write the common header found on every HTML file on
# the SQLite website.
#
proc PutsHeader {title {relpath {}}} {
  puts {<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">}
  puts {<html><head>}
  puts "<title>$title</title>"
  putsin4 {<style type="text/css">
    body {
        max-width: 800px; /* not supported in IE 6 */
        margin: auto;
        font-family: "Verdana" "sans-serif";
        padding: 8px 1%;
    }
    
    a { color: #40534b }
    a:visited { color: #587367 }
    
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
      background-color: #80a796;
      position: relative;
      font-variant: small-caps;
      clear: both;
      text-align: center;
      line-height: 1.6em;
      margin-bottom: 5px;
      padding:1px 8px;
      height:1%; /* IE hack to fix rounded corner positions */
    }
    .toolbar a { color: white; text-decoration: none; padding: 6px 4px; }
    .toolbar a:visited { color: white; }
    .toolbar a:hover { color: #80a796; background: white; }
    
    .content    { margin: 5%; }
    .content dt { font-weight:bold; }
    .content dd { margin-bottom: 25px; margin-left:20%; }
    .content ul { padding:0px; padding-left: 15px; margin:0px; }
    
    /* rounded corners */
    .se,.ne,.nw,.sw { position: absolute; width:8px; height:8px; 
                      font-size:7px; /* IE hack to ensure height=8px */ }
    .se  { background-image: url(${relpath}images/se.png);
           bottom: 0px; right: 0px; }
    .ne  { background-image: url(${relpath}images/ne.png);
           top: 0px;    right: 0px; }
    .sw  { background-image: url(${relpath}images/sw.png);
           bottom: 0px; left: 0px; }
    .nw  { background-image: url(${relpath}images/nw.png);
           top: 0px;    left: 0px; }
    </style>
    <meta http-equiv="content-type" content="text/html; charset=UTF-8">
  }
  puts {</head>}
  putsin4 {<body>
    <div><!-- container div to satisfy validator -->
    
    <img class="logo" src="${relpath}images/SQLite.gif" alt="SQLite Logo">
    <div><!-- IE hack to prevent disappearing logo--></div>
    <div class="tagline">The World's Most Widely Used SQL Database.</div>
    
    <div class="toolbar">
      <a href="${relpath}index.html">Home</a>
      <a href="${relpath}about.html">About</a>
      <a href="${relpath}docs.html">Documentation</a>
      <a href="${relpath}download.html">Download</a>
      <a href="${relpath}copyright.html">License</a>
      <a href="${relpath}press.html">Advocacy</a>
      <a href="${relpath}devhome.html">Developers</a>
      <a href="${relpath}news.html">News</a>
      <a href="${relpath}support.html">Support</a>
      <!-- rounded corners -->
      <div class="ne"></div><div class="se"></div><div class="nw">
      </div><div class="sw"></div>
    </div>
  }
}

# A procedure to write the common footer found at the bottom of
# every HTML file.  $srcfile is the name of the file that is the
# source of the HTML content.  The modification time of this file
# is used to add the "last modified on" line at the bottom of the
# file.
#
proc PutsFooter {srcfile} {
  puts {<hr><small><i>}
  set mtime [file mtime $srcfile]
  set date [clock format $mtime -format {%Y/%m/%d %H:%M:%S UTC} -gmt 1]
  puts "This page last modified $date"
  puts {</i></small></div></body></html>}
}

# The following proc is used to ensure consistent formatting in the 
# HTML generated by lang.tcl and pragma.tcl.
#
proc Syntax {args} {
  puts {<table cellpadding="10" class=pdf_syntax>}
  foreach {rule body} $args {
    puts "<tr><td align=\"right\" valign=\"top\">"
    puts "<i><font color=\"#ff3434\">$rule</font></i>&nbsp;::=</td>"
    regsub -all < $body {%LT} body
    regsub -all > $body {%GT} body
    regsub -all %LT $body {</font></b><i><font color="#ff3434">} body
    regsub -all %GT $body {</font></i><b><font color="#2c2cf0">} body
    regsub -all {[]|[*?]} $body {</font></b>&<b><font color="#2c2cf0">} body
    regsub -all "\n" [string trim $body] "<br>\n" body
    regsub -all "\n  *" $body "\n\\&nbsp;\\&nbsp;\\&nbsp;\\&nbsp;" body
    regsub -all {[|,.*()]} $body {<big>&</big>} body
    regsub -all { = } $body { <big>=</big> } body
    regsub -all {STAR} $body {<big>*</big>} body
    ## These metacharacters must be handled to undo being
    ## treated as SQL punctuation characters above.
    regsub -all {RPPLUS} $body {</font></b>)+<b><font color="#2c2cf0">} body
    regsub -all {LP} $body {</font></b>(<b><font color="#2c2cf0">} body
    regsub -all {RP} $body {</font></b>)<b><font color="#2c2cf0">} body
    ## Place the left-hand side of the rule in the 2nd table column.
    puts "<td><b><font color=\"#2c2cf0\">$body</font></b></td></tr>"
  }
  puts {</table>}
}

# Loop over all input files and process them one by one
#
foreach infile [lrange $argv 3 end] {
  cd $HOMEDIR
  real_puts "Processing $infile"
  set fd [open $infile r]
  set in [read $fd]
  close $fd
  set title {No Title}
  regexp {<title>([^\n]*)</title>} $in all title
  regsub {<title>[^\n]*</title>} $in {} in
  set outfile [file root [file tail $infile]].html
  set ::OUT [open $::DEST/$outfile w]
  PutsHeader $title
  regsub -all {<tcl>} $in "\175; eval \173" in
  regsub -all {</tcl>} $in "\175; puts \173" in
  eval "puts \173$in\175"
  cd $::HOMEDIR
  PutsFooter $infile
  close $::OUT
}
