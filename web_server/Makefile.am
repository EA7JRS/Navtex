AUTOMAKE_OPTIONS = subdir-objects
bin_PROGRAMS = navtexwebserver
navtexdir = /opt/Navtex/
navtexwebserver_SOURCES = ../receiver/message_store.c  https_fileserver.c gps.c gps.h nrx.c nrx.h
AM_CFLAGS= -I/usr/local/lib  -I/usr/local/include
navtexwebserver_LDADD =  -L/usr/local/lib -lcjson -ldl -lm -lsqlite3  -lmicrohttpd 
navtexwebserver_LDFLAGS =  -pthread 

AM_CPPFLAGS =  -DDATA_FILE=\"/opt/Navtex/Navtex.db\"  -DWEB_ROOT=\"/opt/Navtex/build\" 

EXTRA_DIST = build

# make check: the rules of a NAVTEX receiver and the NRX sentences, on a database of its own
check_PROGRAMS = test_navtex
TESTS = test_navtex
test_navtex_SOURCES = test_navtex.c nrx.c nrx.h ../receiver/message_store.c
test_navtex_CPPFLAGS = -DDATA_FILE=\"test_navtex.db\" -DWEB_ROOT=\".\"
test_navtex_LDADD = -L/usr/local/lib -lcjson -lm -lsqlite3
test_navtex_LDFLAGS = -pthread
CLEANFILES = test_navtex.db

# Replace the installed GUI instead of nesting build/build on a reinstall
install-data-hook: 
	$(MKDIR_P) $(DESTDIR)$(navtexdir)
	@rm -rf $(DESTDIR)$(navtexdir)/build
	@cp -r $(srcdir)/build $(DESTDIR)$(navtexdir)/build
