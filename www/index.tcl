#
# Run this TCL script to generate HTML for the index.html file.
#
set rcsid {$Id$}

puts {<html>
<head><title>SQLite: An SQL Database Engine In A C Library</title></head>
<body bgcolor=white>
<h1 align=center>SQLite: An SQL Database Engine In A C Library</h1>
<p align=center>}
puts "This page was last modified on [lrange $rcsid 3 4] UTC<br>"
set vers [lindex $argv 0]
puts "The latest SQLite version is <b>$vers</b>"
puts " created on [exec cat last_change] UTC"
puts {</p>}

puts {<h2>Introduction</h2>

<p>SQLite is a C library that implements an embeddable SQL database engine.
Programs that link with the SQLite library can have SQL database
access without running a separate RDBMS process.
The distribution comes with a standalone command-line
access program (<a href="sqlite.html">sqlite</a>) that can
be used to administer an SQLite database and which serves as
an example of how to use the SQLite library.</p>

<p>SQLite is <b>not</b> a client library used to connect to a
big database server.  SQLite <b>is</b> the server.  The SQLite
library reads and writes directly to and from the database files
on disk.</p>}

puts {<table align="right"hspace="10">
<tr><td align="center" bgcolor="#8ee5ee">
<table border="2"><tr><td align="center">
<a href="download.html"><big><b>Download<br>SQLite
</td></tr></table>
</td></tr>
</table>}

puts {<h2>Features</h2>

<p><ul>
<li>Implements a large subset of SQL92.</li>
<li>A complete database (with multiple tables and indices) is
    stored in a single disk file.</li>
<li>Atomic commit and rollback protect data integrity.</li>
<li>Small memory footprint: about 14000 lines of C code.</li>
<li><a href="speed.html">Four times faster</a> than PostgreSQL.
    Twice as fast as SQLite 1.0.</li>
<li>Very simple 
<a href="c_interface.html">C/C++ interface</a> requires the use of only
three functions and one opaque structure.</li>
<li><a href="tclsqlite.html">TCL bindings</a> included.</li>
<li>A TCL-based test suite provides near 100% code coverage.</li>
<li>Self-contained: no external dependencies.</li>
<li>Built and tested under Linux and Win2K.</li>
<li>Sources are uncopyrighted.  Use for any purpose.</li>
</ul>
</p>
}

puts {<h2>Current Status</h2>

<p>A <a href="changes.html">change history</a> is available online.
The latest source code is
<a href="download.html">available for download</a>.
There are currently no known memory leaks or bugs
in the library.
SQLite 2.1.7 is currently being used in several mission-critical
applications.  SQLite 2.2.0 is in beta-test.
</p>

<p>
Whenever either of the first two digits in the version number
for SQLite change, it means that the underlying file format
has changed.  See <a href="formatchng.html">formatchng.html</a>
for additional information.
</p>

<h2>Documentation</h2>

<p>The following documentation is currently available:</p>

<p><ul>
<li><a href="faq.html">Frequently Asked Questions</a> are available online.</li>
<li>Information on the <a href="sqlite.html">sqlite</a>
    command-line utility.</li>
<li>The <a href="lang.html">SQL Language</a> subset understood by SQLite.</li>
<li>The <a href="c_interface.html">C/C++ Interface</a>.</li>
<li>The <a href="tclsqlite.html">Tcl Binding</a> to SQLite.</li>
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

puts {<h2>Professional Support and Custom Modifications</h2>}

puts {
<p>
If you would like professional support for SQLite
or if you want custom modifications to SQLite preformed by the
original author, these services are available for a modest fee.
For additional information contact:</p>

<blockquote>
D. Richard Hipp <br />
Hwaci - Applied Software Research <br />
704.948.4565 <br />
<a href="mailto:drh@hwaci.com">drh@hwaci.com</a>
</blockquote>
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
}

puts {<h2>Related Sites</h2>

<ul>

<li><p>An ODBC driver for SQLite can be found at
       <a href="http://www.ch-werner.de/sqliteodbc/">
       http://www.ch-werner.de/sqliteodbc/</a>.</p></li>

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
