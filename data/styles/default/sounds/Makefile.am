
soundsdir = $(datadir)/florence/styles/default/sounds
sounds_in_files = sounds.xml.in
# Depend on Makefile so a reconfigure (new prefix / SNDDIR) always rewrites
# sounds.xml. The tarball may still ship a stale Linux /usr/share path.
$(sounds_in_files:.xml.in=.xml): $(sounds_in_files) Makefile
	sed -e "s|\@SNDDIR\@|$(soundsdir)|" $< > $@
sounds_DATA=press.ogg release.ogg return_press.ogg return_release.ogg\
	modifier_press.ogg modifier_release.ogg $(sounds_in_files:.xml.in=.xml)

EXTRA_DIST = $(sounds_DATA) $(sounds_in_files)

