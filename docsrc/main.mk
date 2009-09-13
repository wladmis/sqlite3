###############################################################################
# The following macros should be defined before this script is
# invoked:
#
# DOC              The toplevel directory of the documentation source tree.
#
# SRC              The toplevel directory of the source code source tree.
#
# NAWK             Nawk compatible awk program.  Older (obsolete?) solaris
#                  systems need this to avoid using the original AT&T AWK.
#
# Once the macros above are defined, the rest of this make script will
# build the SQLite library and testing tools.
################################################################################

all:	doc

sqlite3.h:	$(SRC)/src/sqlite.h.in $(SRC)/manifest.uuid $(SRC)/VERSION
	tclsh $(SRC)/tool/mksqlite3h.tcl $(SRC) | \
	sed 's/^SQLITE_API //' >sqlite3.h

wrap.tcl:	$(DOC)/wrap.tcl
	cp $(DOC)/wrap.tcl .

docdir:
	mkdir -p doc doc/c3ref

doc:	sqlite3.h docdir always
	rm -rf doc/images
	cp -r $(DOC)/images doc
	cp $(SRC)/art/*.gif doc/images
	mkdir doc/images/syntax
	cp $(DOC)/art/syntax/*.gif doc/images/syntax
	cp $(DOC)/rawpages/* doc
	tclsh $(DOC)/wrap.tcl $(DOC) $(SRC) doc $(DOC)/pages/*.in

always:	


clean:	
	rm -rf doc sqlite3.h
