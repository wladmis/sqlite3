Name: sqlite3
Version: 3.5.0
Release: alt0

Summary: An Embeddable SQL Database Engine
License: Public Domain
Group: Development/Databases

URL: http://www.sqlite.org/
Source: sqlite-3.5.0.tar
Patch: %name-%version-%release.patch

Requires: lib%name = %version-%release

# Automatically added by buildreq on Fri Aug 17 2007
BuildRequires: gcc-c++ gcc-fortran libreadline-devel tcl-devel

%package -n lib%name
Summary: An Embeddable SQL Database Engine (shared library)
Group: System/Libraries

%package -n lib%name-devel
Summary: An Embeddable SQL Database Engine (header files)
Group: Development/Databases
Requires: lib%name = %version-%release

%package -n lib%name-devel-static
Summary: An Embeddable SQL Database Engine (static library)
Group: Development/Databases
Requires: lib%name-devel = %version-%release

%package tcl
Summary: An Embeddable SQL Database Engine (TCL bindings)
Group: Development/Tcl
Requires: lib%name = %version-%release

%package fts3
Summary: Full Text Search backend for the SQLite database engine
Group: Development/Databases
Requires: lib%name = %version-%release

%package doc
Summary: An Embeddable SQL Database Engine (documentation)
Group: Development/Documentation
Conflicts: %name < 3.3.8-alt1

%package -n lemon
Summary: The Lemon Parser Generator
Group: Development/Other

%description
SQLite is a C library that implements an SQL database engine. 
Programs that link with the SQLite library can have SQL database 
access without running a separate RDBMS process.

%description -n lib%name
SQLite is a C library that implements an SQL database engine. 
Programs that link with the SQLite library can have SQL database 
access without running a separate RDBMS process.

%description -n lib%name-devel
SQLite is a C library that implements an SQL database engine. 
Programs that link with the SQLite library can have SQL database 
access without running a separate RDBMS process.

%description -n lib%name-devel-static
SQLite is a C library that implements an SQL database engine. 
Programs that link with the SQLite library can have SQL database 
access without running a separate RDBMS process.

%description tcl
SQLite is a C library that implements an SQL database engine. 
Programs that link with the SQLite library can have SQL database 
access without running a separate RDBMS process.

%description fts3
SQLite is a C library that implements an SQL database engine.
Programs that link with the SQLite library can have SQL database
access without running a separate RDBMS process.

The fts3 loadable module implements full text search, see
http://www.sqlite.org/cvstrac/wiki?p=FtsOne
http://www.sqlite.org/cvstrac/wiki?p=FtsTwo

%description doc
SQLite is a C library that implements an SQL database engine. 
Programs that link with the SQLite library can have SQL database 
access without running a separate RDBMS process.

%description -n lemon
Lemon is an LALR(1) parser generator for C or C++. It does the same
job as bison and yacc. But lemon is not another bison or yacc
clone. It uses a different grammar syntax which is designed to reduce
the number of coding errors. Lemon also uses a more sophisticated
parsing engine that is faster than yacc and bison and which is both
reentrant and thread-safe. Furthermore, Lemon implements features
that can be used to eliminate resource leaks, making is suitable for
use in long-running programs such as graphical user interfaces or
embedded controllers.

%prep
%setup -q -n sqlite-3.5.0
%patch -p1

%build
autoreconf -i
# maybe add -lhistory; cf. [devel] libreadline add_history
%configure --enable-threadsafe --enable-readline --with-readline-lib=-lreadline
%make_build all libtcl%name.la fts3.la doc

make test

%install
%make_install install tcl_install fts3_install DESTDIR=%buildroot
install -pD -m644 %name.1 %buildroot%_man1dir/%name.1

install -pD -m755 lemon %buildroot%_bindir/lemon
install -pD -m644 lemon.1 %buildroot%_man1dir/lemon.1
install -pD -m644 lempar.c %buildroot%_datadir/lemon/lempar.c

%define pkgdocdir %_docdir/sqlite-3.3
mkdir -p %buildroot%pkgdocdir
install -p -m644 COPYING doc/*.* %buildroot%pkgdocdir/
mkdir -p %buildroot%_docdir/lemon
mv %buildroot%pkgdocdir/lemon.html %buildroot%_docdir/lemon/

%post -n lib%name -p %post_ldconfig
%postun -n lib%name -p %postun_ldconfig

%files
%_bindir/%name
%_man1dir/%name.*

%files -n lib%name
%_libdir/lib%name.so.?*
%dir %pkgdocdir
%pkgdocdir/COPYING

%files -n lib%name-devel
%_includedir/sqlite3.h
%_includedir/sqlite3ext.h
%_libdir/lib%name.so
#_libdir/lib%name.la
%_libdir/pkgconfig/%name.pc

%files -n lib%name-devel-static
%_libdir/lib%name.a

%files tcl
%_tcllibdir/libtcl%name.so*
#_tcllibdir/libtcl%name.a
#_tcllibdir/libtcl%name.la
%dir %_tcldatadir/sqlite3
%_tcldatadir/sqlite3/pkgIndex.tcl

%files fts3
%dir %_libdir/sqlite3
%_libdir/sqlite3/fts3.so
#_libdir/sqlite3/fts3.a
#_libdir/sqlite3/fts3.la

%files doc
%dir %pkgdocdir
%pkgdocdir/*.*

%files -n lemon
%_bindir/lemon
%_man1dir/lemon.*
%_datadir/lemon/
%dir %_docdir/lemon
%_docdir/lemon/lemon.html

%changelog
* Mon Nov 12 2007 Alexey Tourbin <at@altlinux.ru> 3.5.0-alt0
- 3.4.2 -> 3.5.0

* Fri Aug 17 2007 Alexey Tourbin <at@altlinux.ru> 3.4.2-alt1
- 3.4.1+cvs20070803 -> 3.4.2

* Sun Aug 05 2007 Alexey Tourbin <at@altlinux.ru> 3.4.1-alt2
- 3.4.1+cvs20070720 -> 3.4.1+cvs20070803

* Sat Jul 21 2007 Alexey Tourbin <at@altlinux.ru> 3.4.1-alt1
- 3.3.17+ -> 3.4.1+cvs20070720 (fixes multiple vulnerabilities
  discovered by the Google Security team)
- changed src.rpm packaging to keep separate tarball with cvs snapshot

* Fri Apr 27 2007 Alexey Tourbin <at@altlinux.ru> 3.3.17-alt1
- 3.3.15 -> 3.3.17+

* Tue Apr 10 2007 Alexey Tourbin <at@altlinux.ru> 3.3.15-alt1
- 3.3.14+ -> 3.3.15
- re-enabled exclusive2.test

* Tue Apr 03 2007 Alexey Tourbin <at@altlinux.ru> 3.3.14-alt1
- 3.3.13+ -> 3.3.14+
- disabled -fno-strict-aliasing since the code has been fixed
- disabled exclusive2.test and types3.test

* Tue Feb 20 2007 Alexey Tourbin <at@altlinux.ru> 3.3.13-alt1
- 3.3.12 -> 3.3.13+

* Sun Jan 28 2007 Alexey Tourbin <at@altlinux.ru> 3.3.12-alt1
- 3.3.11 -> 3.3.12

* Tue Jan 23 2007 Alexey Tourbin <at@altlinux.ru> 3.3.11-alt1
- 3.3.10+ -> 3.3.11
- sqlite3-doc: packaged missing html files

* Thu Jan 11 2007 Alexey Tourbin <at@altlinux.ru> 3.3.10-alt1
- 3.3.8+ -> 3.3.10+

* Wed Nov 15 2006 Alexey Tourbin <at@altlinux.ru> 3.3.8-alt2
- 3.3.8+ cvs snapshot
- enabled full text search backend (sqlite3-fts2 package)

* Sat Oct 21 2006 Alexey Tourbin <at@altlinux.ru> 3.3.8-alt1
- imported cvs sources with git-cvsimport; applied my changes
  to the source tree and built with gear
- this release is based on sqlite version 3.3.8
- changed documentation packaging: introduced sqlite3-doc package; moved
  lemon.html to lemon package; added COPYING, which was compiled from
  various sources; sqlite is still public domain, with a few minor issues

* Sun Aug 13 2006 Alexey Tourbin <at@altlinux.ru> 3.3.7-alt1
- 3.3.6 -> 3.3.7

* Wed Jun 07 2006 Alexey Tourbin <at@altlinux.ru> 3.3.6-alt1
- 3.3.5 -> 3.3.6
- compiled with -fno-strict-aliasing (debian #364196)
- linked libtclsqlite3.so with libtcl.so

* Sun Apr 16 2006 Alexey Tourbin <at@altlinux.ru> 3.3.5-alt1
- 3.3.4 -> 3.3.5; sync debian sqlite3_3.3.5-0.1
- urgency=high (for lib%name >= 3.3): hacked sqlite3_prepare() to keep
  old code work; try "sqlite3_prepare nBytes" web search for details
- restricted list of symbols exported by the library;
  introduced symbol versioning
- fixed libtcl%name linkage (eliminated internal lib%name copy)
- improved temporary file handling
- new package: lemon (LALR parser generator)

* Thu Mar 09 2006 Denis Smirnov <mithraen@altlinux.ru> 3.3.4-alt1
- 3.2.6 -> 3.3.4

* Fri Dec 30 2005 ALT QA Team Robot <qa-robot@altlinux.org> 3.2.6-alt1.1
- Rebuilt with libreadline.so.5.

* Sat Sep 24 2005 Alexey Tourbin <at@altlinux.ru> 3.2.6-alt1
- 3.2.5 -> 3.2.6; urgency=medium

* Mon Aug 29 2005 Alexey Tourbin <at@altlinux.ru> 3.2.5-alt1
- 3.2.2 -> 3.2.5

* Fri Jun 24 2005 Alexey Tourbin <at@altlinux.ru> 3.2.2-alt1
- 3.2.1 -> 3.2.2
- enabled thread-safety (--enable-threadsafe configure option)
- removed --enable-utf8 configure option (has no effect)

* Wed Apr 06 2005 Alexey Tourbin <at@altlinux.ru> 3.2.1-alt1
- 3.1.3 -> 3.2.1

* Fri Mar 04 2005 Alexey Tourbin <at@altlinux.ru> 3.1.3-alt1
- 3.0.8 -> 3.1.3

* Fri Nov 26 2004 Alexey Tourbin <at@altlinux.ru> 3.0.8-alt1.1
- fixed tcl bindings (patch by Sergey Bolshakov)

* Thu Oct 21 2004 Alexey Tourbin <at@altlinux.ru> 3.0.8-alt1
- 3.0.6 -> 3.0.8
- alt-makefile.patch merged upstream (sqlite ticket #903 for `mkdir -p',
  sqlite ticket #904 for libdir/lib64)
- added post/postun ldconfig scripts

* Thu Sep 16 2004 Alexey Tourbin <at@altlinux.ru> 3.0.6-alt1
- 3.0.4 -> 3.0.6
- hopefully should build on x86_64

* Fri Aug 13 2004 Alexey Tourbin <at@altlinux.ru> 3.0.4-alt1
- 2.8.13 -> 3.0.4 (beta), renamed to sqlite3

* Fri Jun 04 2004 Denis Smirnov <mithraen@altlinux.ru> 2.8.13-alt4
- Rebuild

* Thu Jun 03 2004 Denis Smirnov <mithraen@altlinux.ru> 2.8.13-alt2
- Fix for correcting update

* Mon May 31 2004 Denis Smirnov <mithraen@altlinux.ru> 2.8.13-alt1
- Some minor packaging fixes
- Tcl binding bugfix

* Sun Apr 11 2004 Denis Smirnov <mithraen@altlinux.ru> 2.8.5-alt3
- Tcl binding build

* Sat Dec 13 2003 Ott Alex <ott@altlinux.ru> 2.8.5-alt2
- Remove .la files

* Sun Jul 27 2003 Ott Alex <ott@altlinux.ru> 2.8.5-alt1
- New version

* Tue Jul 22 2003 Ott Alex <ott@altlinux.ru> 2.8.4-alt1
- Initial build
