# This script generates the "docs.html" page that describes various
# sources of documentation available for SQLite.
#
set rcsid {$Id$}
source common.tcl
header {SQLite Documentation}
puts {
<h2>Available Documentation</h2>
<table width="100%" cellpadding="5">
}

proc doc {name url desc} {
  puts {<tr><td valign="top" align="right">}
  regsub -all { +} $name {\&nbsp;} name
  puts "<a href=\"$url\">$name</a></td>"
  puts {<td width="10"></td>}
  puts {<td align="top" align="left">}
  puts $desc
  puts {</td></tr>}
}

doc {SQL Syntax} {lang.html} {
  This document describes the SQL language that is understood by
  SQLite.  
}

doc {Version 2 C/C++ API} {c_interface.html} {
  A description of the C/C++ interface bindings for SQLite through version 
  2.8
}

doc {Tcl API} {tclsqlite.html} {
  A description of the TCL interface bindings for SQLite.
}

doc {Version 2 DataTypes } {datatypes.html} {
  A description of how SQLite version 2 handles SQL datatypes.
}

doc {Release History} {changes.html} {
  A chronology of SQLite releases going back to version 1.0.0
}

doc {Null Handling} {nulls.html} {
  Different SQL database engines handle NULLs in different ways.  The
  SQL standards are ambiguous.  This document describes how SQLite handles
  NULLs in comparison with other SQL database engines.
}

doc {Copyright} {copyright.html} {
  SQLite is in the public domain.  This document describes what that means
  and the implications for contributors.
}

doc {Unsupported SQL} {omitted.html} {
  This page describes features of SQL that SQLite does not support.
}

doc {Speed Comparison} {speed.html} {
  The speed of version 2.7.6 of SQLite is compared against PostgreSQL and
  MySQL.
}

doc {Architecture} {arch.html} {
  An architectural overview of the SQLite library, useful for those who want
  to hack the code.
}

doc {VDBE Tutorial} {vdbe.html} {
  The VDBE is the subsystem within SQLite that does the actual work of
  executing SQL statements.  This page describes the principles of operation
  for the VDBE in SQLite version 2.7.  This is essential reading for anyone
  who want to modify the SQLite sources.
}

doc {VDBE Opcodes} {opcode.html} {
  This document is an automatically generated description of the various
  opcodes that the VDBE understands.  Programmers can use this document as
  a reference to better understand the output of EXPLAIN listings from
  SQLite.
}

puts {</table>}
footer $rcsid
