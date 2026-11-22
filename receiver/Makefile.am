bin_PROGRAMS = navtexreceiver
navtexreceiver_SOURCES = capt_sched.c  message_store.c  decoder.C  fir1cpp.C  fir2cpp.C  fir3cpp.C  nav_b_sm.C  nav_sched.C capt_sched.h  decoder.h  fir1cpp.h  fir2cpp.h  fir3cpp.h  message_store.h  nav_b_sm.h  nav_sched.h wav.c wav.h iq_source.h rtl_source.c file_source.c
AM_CFLAGS= -I/usr/local/lib  -I/usr/local/include
navtexreceiver_LDADD =  -L/usr/local/lib $(RECEIVER_LIBS)  -lcjson -ldl -lm -lsqlite3
navtexreceiver_LDFLAGS =  -pthread 
#pkgdata_DATA = Navtex.db

navtexdir = /opt/Navtex/

AM_CPPFLAGS = -DDATA_FILE=\"/opt/Navtex/Navtex.db\"


# Distribute the script with the source package
EXTRA_DIST = generate_db.sh

# Rule to build the empty database
all-local: Navtex.db

Navtex.db:
	@echo "Generating empty SQLite database..."
	./generate_db.sh

# Install the database only when there is none: reinstalling must not wipe
# the stored messages and the configuration. The directory and the database
# belong to the user who ran "sudo make install" (or to root), not to everyone.
install-data-hook: all-local
	$(MKDIR_P) $(DESTDIR)$(navtexdir)
	@if [ -f $(DESTDIR)$(navtexdir)/Navtex.db ]; then \
	  echo "Keeping the existing $(DESTDIR)$(navtexdir)/Navtex.db"; \
	else \
	  $(INSTALL_DATA) Navtex.db $(DESTDIR)$(navtexdir)/Navtex.db; \
	fi
	@chmod 775 $(DESTDIR)$(navtexdir)
	@chmod 664 $(DESTDIR)$(navtexdir)/Navtex.db
	@if [ -n "$$SUDO_USER" ] && [ -z "$(DESTDIR)" ]; then \
	  chown "$$SUDO_USER" $(navtexdir) $(navtexdir)/Navtex.db; \
	fi

CLEANFILES = Navtex.db
