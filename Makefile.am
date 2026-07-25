AUTOMAKE_OPTIONS = dejagnu

SUBDIRS = src data po

RUNTESTDEFAULTFLAGS = --tool style --srcdir $srcdir/testsuite

EXTRA_DIST = AUTHORS ChangeLog NEWS README COPYING COPYING-DOCS\
             autogen.sh src/florence.server.in.in \
             docs/florence.1 docs/florence.1.in

# Always install florence(1), even with --without-docs (which skips the
# docs/ subdir and its historical man_MANS path under gnome-doc-utils).
install-data-local:
	$(MKDIR_P) "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DATA) "$(srcdir)/docs/florence.1" \
		"$(DESTDIR)$(mandir)/man1/florence.1"

uninstall-local:
	rm -f "$(DESTDIR)$(mandir)/man1/florence.1"

if WITH_DOCS
   SUBDIRS += docs
   EXTRA_DIST += gnome-doc-utils.make
endif

