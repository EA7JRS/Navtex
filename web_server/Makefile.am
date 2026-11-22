AUTOMAKE_OPTIONS = subdir-objects
bin_PROGRAMS = navtexwebserver
navtexdir = /opt/Navtex/
navtexwebserver_SOURCES = ../receiver/message_store.c  https_fileserver.c gps.c gps.h
AM_CFLAGS= -I/usr/local/lib  -I/usr/local/include
navtexwebserver_LDADD =  -L/usr/local/lib -lcjson -ldl -lm -lsqlite3  -lmicrohttpd 
navtexwebserver_LDFLAGS =  -pthread 

AM_CPPFLAGS =  -DDATA_FILE=\"/opt/Navtex/Navtex.db\"  -DWEB_ROOT=\"/opt/Navtex/build\" 

EXTRA_DIST = build

# Replace the installed GUI instead of nesting build/build on a reinstall
install-data-hook: 
	$(MKDIR_P) $(DESTDIR)$(navtexdir)
	@rm -rf $(DESTDIR)$(navtexdir)/build
	@cp -r $(srcdir)/build $(DESTDIR)$(navtexdir)/build
