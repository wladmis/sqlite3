###############################################################################
# The following macros should be defined before this script is
# invoked:
#
# DOC              The toplevel directory of the documentation source tree.
#
# SRC              The toplevel directory of the source code source tree.
#
# SQLITE3C         Name of the amalgamation source file
#
# TH3              The toplevel directory for TH3.  May be an empty string.
#
# TCLSH            Name of the TCL command-line shell
#
# NAWK             Nawk compatible awk program.  Older (obsolete?) solaris
#                  systems need this to avoid using the original AT&T AWK.
#
# CC               A C-compiler and arguments for building utility programs
#
# Once the macros above are defined, the rest of this make script will
# build the SQLite library and testing tools.
################################################################################

all:	base evidence matrix doc

sqlite3.h:	$(SRC)/src/sqlite.h.in $(SRC)/manifest.uuid $(SRC)/VERSION
	$(TCLSH) $(SRC)/tool/mksqlite3h.tcl $(SRC) | \
	sed 's/^SQLITE_API //' >sqlite3.h

#wrap.tcl:	$(DOC)/wrap.tcl
#	cp $(DOC)/wrap.tcl .

# Generate the directory into which generated documentation files will
# be written.
#
docdir:
	mkdir -p doc doc/c3ref doc/matrix doc/matrix/c3ref

# This rule generates all documention files from their sources.  The
# special markup on HTML files used to identify testable statements and
# requirements are retained in the HTML and so the HTML generated by
# this rule is not suitable for publication.  This is the first step
# only.
#
base:	sqlite3.h docdir always
	rm -rf doc/images
	cp -r $(DOC)/images doc
	cp $(SRC)/art/*.gif doc/images
	mkdir doc/images/syntax
	cp $(DOC)/art/syntax/*.gif doc/images/syntax
	cp $(DOC)/rawpages/* doc
	$(TCLSH) $(DOC)/wrap.tcl $(DOC) $(SRC) doc $(DOC)/pages/*.in

# Strip the special markup in HTML files that identifies testable statements
# and requirements.
#
doc:	always $(DOC)/remove_carets.sh
	sh $(DOC)/remove_carets.sh doc

# The following rule scans sqlite3.c source text, the text of the TCL
# test cases, and (optionally) the TH3 test case sources looking for
# comments that identify assertions and test cases that provide evidence
# that SQLite behaves as it says it does.  See the comments in 
# scan_test_cases.tcl for additional information.
#
# The output file evidence.txt is used by requirements coverage analysis.
#
SCANNER = $(DOC)/scan_test_cases.tcl

evidence:	
	$(TCLSH) $(SCANNER) -dir src $(SQLITE3C) >evidence.txt
	$(TCLSH) $(SCANNER) -dir tcltest $(SRC)/test/*.test >>evidence.txt
	if test '' != '$(TH3)'; then \
	  $(TCLSH) $(SCANNER) -dir th3 $(TH3)/mkth3.tcl >>evidence.txt; \
	  $(TCLSH) $(SCANNER) -dir th3 $(TH3)/req1/*.test >>evidence.txt; \
	  $(TCLSH) $(SCANNER) -dir th3 $(TH3)/cov1/*.test >>evidence.txt; \
	fi

# Generate the traceability matrix
#
matrix:	md5
	rm -rf doc/matrix/images
	cp -r doc/images doc/matrix
	$(TCLSH) $(DOC)/matrix.tcl

always:	

md5:	$(DOC)/md5.c
	$(CC) -o md5 $(DOC)/md5.c

clean:	
	rm -rf doc sqlite3.h md5 evidence.txt
