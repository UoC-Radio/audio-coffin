bin_PROGRAMS = acoffin

acoffin_SOURCES = recorder.c gui.c main.c
acoffin_CFLAGS = ${GTK_CFLAGS} -DDATA_PATH='"@datarootdir@/audio-coffin/"'
acoffin_LDADD = ${LIBM} ${LIBRT} ${LIBSOXR} ${LIBSNDFILE} ${LIBJACK} ${GTK_LIBS}

# Also clean up after autoconf
distclean-local:
	-rm -rf autom4te.cache
	-rm -rf build-aux
	-rm aclocal.m4
	-rm configure
	-rm *.in

# Do nothing
test:

# Install images
install-data-local:
	install -d -m 755 ${DESTDIR}@datarootdir@/audio-coffin
	install -d -m 755 ${DESTDIR}@datarootdir@/pixmaps
	install -c -m 644 @srcdir@/images/* ${DESTDIR}@datarootdir@/audio-coffin/
	install -c -m 644 @srcdir@/images/dracula.png ${DESTDIR}@datarootdir@/pixmaps/audio-coffin.png
	install -c -m 644 @srcdir@/audio-coffin.desktop ${DESTDIR}@datarootdir@/applications

uninstall-local:
	rm -rf ${DESTDIR}@datarootdir@/audio-coffin
	rm ${DESTDIR}@datarootdir@/pixmaps/audio-coffin.png
	rm ${DESTDIR}@datarootdir@/applications/audio-coffin.desktop
