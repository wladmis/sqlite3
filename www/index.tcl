#
# Run this TCL script to generate HTML for the index.html file.
#
set rcsid {$Id$}

puts {<html>
<head><title>SQLite: An SQL Database Engine In A C Library</title></head>
<body bgcolor=white>
<h1 align=center>SQLite: An SQL Database Engine In A C Library</h1>
<p align=center>}
puts "This page was last modified on [lrange $rcsid 3 4] GMT<br>"
set vers [lindex $argv 0]
puts "The latest SQLite version is <b>$vers</b>"
puts " created on [exec cat last_change] GMT"
puts {</p>}

puts {<h2>Introduction</h2>

<p>SQLite is a C library that implements an SQL database engine.
Programs that link with the SQLite library can have SQL database
access without running a separate RDBMS process.
The distribution comes with a standalone command-line
access program (<a href="sqlite.html">sqlite</a>) that can
be used to administer an SQLite database and which serves as
an example of how to use the SQLite library.</p>

<p>SQLite is <b>not</b> a client library used to connect to a
big database server.  SQLite <b>is</b> the server.  The SQLite
library reads and writes directly to and from the database files
on disk.</p>

<h2>Features</h2>

<p><ul>
<li>Implements a large subset of SQL92.</li>
<li>A complete SQL database (with multiple tables and indices) is
    stored in a single disk file.</li>
<li>Atomic commit and rollback protect data integrity.</li>
<li>Small memory footprint: about 12000 lines of C code.</li>
<li><a href="speed.html">Four times faster</a> than PostgreSQL.
    Twice as fast as SQLite 1.0.</li>
<li>Very simple 
<a href="c_interface.html">C/C++ interface</a> requires the use of only
three functions and one opaque structure.</li>
<li>A TCL interface to the library is included.</li>
<li>A TCL-based test suite provides near 100% code coverage.</li>
<li>Self-contained: no external dependencies.</li>
<li>Built and tested under Linux and Win2K.</li>
<li>Sources are uncopyrighted.  Use for any purpose.</li>
</ul>
</p>
}

puts {<h2>Download</h2>}

puts {<table align="right"hspace="10">
<tr><td align="center" bgcolor="#8ee5ee">
<table border="2"><tr><td align="center">
<a href="sqlite.tar.gz"><big><b>Download SQLite<br>}
puts "version $vers<br>"
puts {Now!
</td></tr></table>
</td></tr>
</table>}


puts {<p>You can download a tarball containing all source
code for SQLite
}
puts "version $vers"
puts {
(including the TCL scripts that generate the
HTML files for this website) at <a href="sqlite.tar.gz">sqlite.tar.gz</a>.}
puts "This is a [file size sqlite.tar.gz] byte download."
set historical [lsort -dict [glob -nocomplain sqlite-*.tar.gz]]
if {$historical!=""} {
  puts {The following historical versions of SQLite are also available:}
  foreach x $historical {
     puts "<a href=\"$x\">$x</a> ([file size $x] bytes)"
  }
}
puts {</p>}

puts {<h2>Current Status</h2>

<p>A <a href="changes.html">change history</a> is available online.
There are currently no <em>known</em> memory leaks or debilitating bugs
in the library.  <a href="http://gcc.gnu.org/onlinedocs/gcov_1.html">Gcov</a>
is used to verify test coverage.</p>

<p>Known bugs:</p>

<ul>
<li><p>
  The LIKE operator is suppose to ignore case. 
  But it only ignores case for 7-bit Latin characters.
  The case of 8-bit iso8859 characters or UTF-8 characters is
  signification.  Hence, <b>'a'&nbsp;LIKE&nbsp;'A'</b> returns
  TRUE but <b>'&aelig;'&nbsp;LIKE&nbsp;'&AElig;'</b> returns FALSE.
</p></li>
</ul>

<h2>Documentation</h2>

<p>The following documentation is currently available:</p>

<p><ul>
<li>Information on the <a href="sqlite.html">sqlite</a>
    command-line utility.</li>
<li>The <a href="lang.html">SQL Language</a> subset understood by SQLite.</li>
<li>The <a href="c_interface.html">C/C++ Interface</a>.</li>
<li>The <a href="tclsqlite.html">Tcl Interface</a>.</li>
<li>The <a href="arch.html">Architecture of the SQLite Library</a> describes
    how the library is put together.</li>
<li>A description of the <a href="opcode.html">virtual machine</a> that
    SQLite uses to access the database.</li>
</ul>
</p>

<p>The SQLite source code is 35% comment.  These comments are
another important source of information. </p>
}

puts {
<table align="right">
<tr><td align="center">
<a href="http://www.yahoogroups.com/subscribe/sqlite">
<img src="http://www.egroups.com/img/ui/join.gif" border=0 /><br />
Click to subscribe to sqlite</a>
</td></tr>
</table>
<a name="mailinglist" />
<h2>Mailing List</h2>
<p>A mailing list has been set up on yahooGroups for discussion of
SQLite design issues or for asking questions about SQLite.</p>
}

puts {<h2>Building From Source</h2>}

puts {
<p>To build sqlite under Unix, just unwrap the tarball, create a separate
build directory, run configure from the build directory and then
type "make".  For example:</p>

<blockquote><pre>
$ tar xzf sqlite.tar.gz      <i> Unpacks into directory named "sqlite" </i>
$ mkdir bld                  <i> Create a separate build directory </i>
$ cd bld
$ ../sqlite/configure
$ make                       <i> Builds "sqlite" and "libsqlite.a" </i>
$ make test                  <i> Optional: run regression tests </i>
</pre></blockquote>

<p>The Win2K version of SQLite was built using the MingW32 cross-compiler
running under Linux.  You have to give the configure script hints to make
this work.  Read the comments at the beginning of the file
<b>configure.in</b> for additional information.  The source code is
general enough that it should be possible to compile SQLite using VC++,
though the author has no desire or motivation to try.
</p>
}

puts {<h2>Command-line Usage Example</h2>

<p>Download the source archive and compile the <b>sqlite</b>
program as described above.  Then type:</p>

<blockquote><pre>
bash$ sqlite ~/newdb              <i>Directory ~/newdb created automatically</i>
sqlite> create table t1(
   ...>    a int,
   ...>    b varchar(20)
   ...>    c text
   ...> );                        <i>End each SQL statement with a ';'</i>
sqlite> insert into t1
   ...> values(1,'hi','y''all');
sqlite> select * from t1;
1|hello|world
sqlite> .mode columns             <i>Special commands begin with '.'</i>
sqlite> .header on                <i>Type ".help" for a list of commands</i>
sqlite> select * from t1;
a      b       c
------ ------- -------
1      hi      y'all
sqlite> .exit
base$
</pre></blockquote>
}
puts {<h2>Related Sites</h2>

<ul>

<li><p>Here is a good <a href="http://w3.one.net/~jhoffman/sqltut.htm">
       tutorial on SQL</a>.</p></li>

<li><p><a href="http://www.postgresql.org/">PostgreSQL</a> is a
       full-blown SQL RDBMS that is also open source.</p></li>

<li><p><a href="http://www.chordate.com/gadfly.html">Gadfly</a> is another
       SQL library, similar to SQLite, except that Gadfly is written
       in Python.</p></li>
</ul>}

puts {
<p><hr /></p>
<p>
<a href="../index.html"><img src="/goback.jpg" border=0 />
More Open Source Software</a> from Hwaci.
</p>

</body></html>}
