#
# Run this TCL script to generate HTML for the index.html file.
#
set rcsid {$Id$}

puts {<html>
<head><title>SQLite: An SQL Database Engine Built Atop GDBM</title></head>
<body bgcolor=white>
<h1 align=center>SQLite: An SQL Database Engine Built Atop
<a href="http://www.gnu.org/software/gdbm/gdbm.html">GDBM</a></h1>
<p align=center>}
puts "This page was last modified on [lrange $rcsid 3 4] GMT<br>"
puts "The SQLite source code was last modifed on [exec cat last_change] GMT"
puts {</p>}

if 0 {
puts {
<h2>News</h2>
<p>
The SQLite code base is being called "beta" only because it is
relatively new.  It appears to be stable and usable.
Most of the SQL language is now implemented and working.  
The regression test suite
provides good coverage, according to
<a href="http://gcc.gnu.org/onlinedocs/gcov_1.html">gcov</a>.
There are currently no known errors in the code.</p>

<p>If you find bugs or missing features, please submit a comment
to the <a href="#mailinglist">SQLite mailing list</a>.</p>
}
}

puts {<h2>Introduction</h2>

<p>SQLite is an SQL database engine built on top of the
<a href="http://www.gnu.org/software/gdbm/gdbm.html">GDBM library</a>.
SQLite includes a standalone command-line
access program (<a href="sqlite.html">sqlite</a>)
and a C library (<a href="c_interface.html">libsqlite.a</a>)
that can be linked
with a C/C++ program to provide SQL database access without
an separate RDBMS.</p>

<h2>Features</h2>

<p><ul>
<li>Implements most of SQL92.</li>
<li>A database is just a directory of GDBM files.</li>
<li>Unlimited length records.</li>
<li>Import and export data from 
<a href="http://www.postgresql.org/">PostgreSQL</a>.</li>
<li>Very simple 
<a href="c_interface.html">C/C++ interface</a> requires the use of only
three functions and one opaque structure.</li>
<li>A <a href="http://dev.scriptics.com/">Tcl</a> interface is
included.</li>
<li>Command-line access program <a href="sqlite.html">sqlite</a> uses
the <a href="http://www.google.com/search?q=gnu+readline+library">GNU
Readline library</a></li>
<li>A Tcl-based test suite provides near 100% code coverage</li>
<li>7500+ lines of C code.  No external dependencies other than GDBM.</li>
<li>Built and tested under Linux (RedHat 6.0).  Should work under any Unix and
probably also under Windows95/98/NT/2000.</li>
</ul>
</p>

<h2>Current Status</h2>

<p>A <a href="changes.html">change history</a> is available online.
There are currently no <em>known</em> bugs or memory leaks
in the library.  <a href="http://gcc.gnu.org/onlinedocs/gcov_1.html">Gcov</a>
is used to verify test coverage.  The test suite currently exercises
all code except for a few areas which are unreachable or which are
only reached when <tt>malloc()</tt> fails.  The code has been tested
for memory leaks and is found to be clean.</p>

<p>
Among the SQL features that SQLite does not currently implement are:</p>

<p>
<ul>
<li>outer joins</li>
<li>constraints are parsed but are not enforced</li>
<li>no support for transactions or rollback</li>
</ul>
</p>

<h2>Documentation</h2>

<p>The following documentation is currently available:</p>

<p><ul>
<li>Information on the <a href="sqlite.html">sqlite</a>
    command-line utility.</li>
<li>The <a href="lang.html">SQL Language</a> subset understood by SQLite.</li>
<li>The <a href="c_interface.html">C/C++ Interface</a>.</li>
<li>The <a href="fileformat.html">file format</a> used by SQLite databases.</li>
<li>The <a href="arch.html">Architecture of the SQLite Library</a> describes
    how the library is put together.</li>
<li>A description of the <a href="opcode.html">virtual machine</a> that
    SQLite uses to access the database.</li>
<li>Instructions for building 
    <a href="crosscompile.html">SQLite for Win98/NT</a> using the
    MinGW cross-compiler.  There are also instructions on
    <a href="mingw.html">building MinGW</a> in case you don't already have
    a copy.</li>
</ul>
</p>

<p>The SQLite source code is 35% comment.  These comments are
another important source of information. </p>
}

puts {
<a name="mailinglist" />
<h2>Mailing List</h2>
<p>A mailing list has been set up on eGroups for discussion of
SQLite design issues or for asking questions about SQLite.</p>
<center>
<a href="http://www.egroups.com/subscribe/sqlite">
<img src="http://www.egroups.com/img/ui/join.gif" border=0 /><br />
Click to subscribe to sqlite</a>
</center>}

puts {<h2>Download</h2>

<p>You can download a tarball containing all source
code for SQLite (including the TCL scripts that generate the
HTML files for this website) at <a href="sqlite.tar.gz">sqlite.tar.gz</a>.}
puts "This is a [file size sqlite.tar.gz] byte download.  The
tarball was last modified at [clock format [file mtime sqlite.tar.gz]]"
puts {</p>

<p>To build sqlite, just unwrap the tarball, create a separate
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
}

puts {<h2>Command-line Usage Example</h2>

<p>Download the source archive and compile the <b>sqlite</b>
program as described above.  The type:</p>

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
<li><p>The canonical site for GDBM is
       <a href="http://www.gnu.org/software/gdbm/gdbm.html">
       http://www.gnu.org/software/gdbm/gdbm.html</a></p></li>

<li><p>Someday, we would like to port SQLite to work with
       the Berkeley DB library in addition to GDBM.  For information
       about the Berkeley DB library, see
       <a href="http://www.sleepycat.com/">http://www.sleepycat.com/</a>
       </p></li>

<li><p>Here is a good <a href="http://w3.one.net/~jhoffman/sqltut.htm">
       tutorial on SQL</a>.</p></li>

<li><p><a href="http://www.postgresql.org/">PostgreSQL</a> is a
       full-blown SQL RDBMS that is also open source.</p></li>

<li><p><a href="http://www.chordate.com/gadfly.html">Gadfly</a> is another
       SQL library, similar to SQLite, except that Gadfly is written
       in Python.</p></li>

<li><p><a href="http://www.vogel-nest.de/tcl/qgdbm.html">Qgdbm</a> is
       a wrapper around 
       <a href="http://www.vogel-nest.de/tcl/tclgdbm.html">tclgdbm</a>
       that provides SQL-like access to GDBM files.</p></li>
</ul>}

puts {
<p><hr /></p>
<p>
<a href="../index.html"><img src="/goback.jpg" border=0 />
More Open Source Software</a> from Hwaci.
</p>

</body></html>}
