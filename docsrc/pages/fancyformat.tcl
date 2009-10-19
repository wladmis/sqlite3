

catch { array unset ::SectionNumbers }
set ::SectionNumbers(1) 0
set ::SectionNumbers(2) 0
set ::SectionNumbers(3) 0
set ::SectionNumbers(fig) 0
catch { set TOC "" }
catch { array unset ::References }

proc H {iLevel zTitle {zName ""} args} {

  set zNumber ""
  for {set i 1} {$i <= 4} {incr i} {
    if {$i < $iLevel} {
      append zNumber "$::SectionNumbers($i)."
    }
    if {$i == $iLevel} {
      append zNumber "[incr ::SectionNumbers($i)]."
    }
    if {$i > $iLevel} {
      set ::SectionNumbers($i) 0
    }
  }
  set zNumber [string range $zNumber 0 end-1]

  if {$zName == ""} {
    set zName "section_[string map {. _} $zNumber]"
  } else {
    set ::References($zName) [list $zNumber $zTitle]
  }

  if {$args != ""} {
    #puts $args
    set ::hd(fragment) $zName
    eval hd_keywords $args
  }

  append ::TOC [subst {
    <div style="margin-left:[expr $iLevel*6]ex">
    <a href="#$zName">${zNumber} $zTitle</a>
    </a></div>
  }]

  return "<h$iLevel id=\"$zName\">$zNumber $zTitle</h$iLevel>\n"
}
proc h1 {args} {uplevel H 1 $args}
proc h2 {args} {uplevel H 2 $args}
proc h3 {args} {uplevel H 3 $args}
proc h4 {args} {uplevel H 4 $args}

proc fancyformat_fragment {name args} {
  global hd
  set hd(fragment) $name
  eval hd_keywords $args
  return "<a name=\"$name\"></a>"
}

proc Figure {zImage zName zCaption} {
  incr ::SectionNumbers(fig)
  set ::References($zName) [list $::SectionNumbers(fig) $zCaption]

  if {[regexp {.*svg} $zImage ]} {
    set fd [open $::DOC/images/$zImage]
    set nLine 0
    while {![eof $fd] && $nLine<30} {
      set line [gets $fd]
      regexp {^ *width="([0123456789]*)} $line dummy iWidth
      regexp {^ *height="([0123456789]*)} $line dummy iHeight
      incr nLine
    }
    close $fd
    incr iWidth
    incr iHeight

    set tag "<object data=\"images/$zImage\" type=\"image/svg+xml\" width=$iWidth height=$iHeight style=\"overflow:hidden\"></object>"
  } else {
    set tag "<img src=\"images/fileformat/$zImage\">"
  }

  subst {
      <center>
      <a name="$zName"></a>
      $tag
      <p><i>Figure $::SectionNumbers(fig) - $zCaption</i>
      </center>
  }
}

proc sort_by_length {lhs rhs} {
  return [expr [string length $lhs] - [string length $rhs]]
}

set ::Random 0
proc randomstring {} {
  incr ::Random
  return [expr $::Random + rand()]
}

proc Ref {no id details} {
  set ::References($id) "\[$no\]"
  return "<tr><td style=\"width:5ex ; vertical-align:top\" id=\"$id\">\[$no\]<td>$details"
}

proc FixReferences {body} {
  if {[info commands hd_resolve_2ndpass] ne ""} return 

  set l [list]
  foreach E [lsort -decr -index 1 -command sort_by_length $::Glossary] {
    # puts $E
    foreach {term anchor} $E {}
    set re [string map {" " [-[:space:]]+} $term]
    set re "${re}s?"

    while { [regexp -nocase $re $body thisterm] } {
      set xxx [randomstring]
      set body [regsub -nocase $re $body $xxx]
      lappend l $xxx "<a class=defnlink href=\"#$anchor\">$thisterm</a>"
    }

    # set body [regsub -all -nocase $re $body "<a class=defnlink href=\"#$anchor\">\\0</a>"]
    # set body [regsub -all -nocase {(defnlink[^<]*) } $body "\\1&20;"]
  }

  foreach R $::Requirements {
    set body [regsub -all "(\[^=\])$R" $body "\\1<a class=reqlink href=#$R>$R</a>"]
  }

  foreach {key value} [array get ::References] {
    foreach {zNumber zTitle} $value {}
    lappend l <cite>$key</cite> "<cite><a href=\"#$key\" title=\"$zTitle\">$zNumber</a></cite>"
  }

  set body [string map $l $body]
}

set ::Glossary {}
proc Glossary {term definition} {
  set anchor [string map {" " _ ' _} $term]
  set anchor "glossary_$anchor"
  lappend ::Glossary [list $term $anchor]
  return "<tr><td class=defn><a name=\"$anchor\"></a>$term <td>$definition"
}

# Procs to generate <table> and <tr> tags. They also give alternating rows
# of the table a grey background, which can make it easier to read.
# 
proc Table {} {
  set ::Stripe 1
  return "<table class=striped>"
}
proc Tr {} {
  set ::Stripe [expr {($::Stripe+1)%2}]
  if {$::Stripe} {
    return "<tr style=\"background-color:#DDDDDD\">"
  } else {
    return "<tr>"
  }
}
proc fancyformat_import_requirement {reqid} {
  lappend ::Requirements $reqid
  set ret "<p class=req id=$reqid><span>[lindex $::ffreq($reqid) 1]</span>"
  if {[llength [lindex $::ffreq($reqid) 0]]} {
    append ret " (P: [lindex $::ffreq($reqid) 0])"
  } 
  if {[info exists ::ffreq_children($reqid)]} {
    append ret " (C: $::ffreq_children($reqid))"
  } 
  append ret "</p>"
}

set ::Requirements [list]

proc Code {txt} {
  set txt [string trim $txt "\n"]
  set out {<div class=codeblock><table width=100%><tr><td><pre>}
  foreach line [split $txt "\n"] {
    if {![string is space $line]} {
      set nSpace [expr {
        [string length $line] - [string length [string trimleft $line]]
      }]
      if {[info exists nMinSpace]==0 || $nSpace<$nMinSpace} {
        set nMinSpace $nSpace
      }
    }
  }
  foreach line [split $txt "\n"] {
    set line [string range $line $nMinSpace end]
    append out "$line\n"
  }
  append out "</table></div>"
  return $out
}

proc fancyformat_document {zTitle lReqfile zBody} {
  unset -nocomplain ::ffreq
  unset -nocomplain ::ffreq_children
  foreach f $lReqfile {
    hd_read_requirement_file $::DOC/req/$f ::ffreq
  }
  foreach req [array names ::ffreq] {
    foreach parent [lindex $::ffreq($req) 0] {
      lappend ::ffreq_children($parent) $req
    }
  }


  set body [subst -novariables $zBody]
  hd_resolve [subst {
    <!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">
    <html>
    <head>
    <link type="text/css" rel="stylesheet" href="images/fileformat/rtdocs.css">
    </head>
    <body>

    <div id=document_title>$zTitle</div>
    <div id=toc_header>Table Of Contents</div>

    <div id=toc>
      $::TOC
    </div id>
    [FixReferences $body]
  }]
}
