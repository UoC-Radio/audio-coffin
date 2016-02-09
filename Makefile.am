bin_PROGRAMS = acoffin

acoffin_SOURCES = recorder.c gui.c main.c
acoffin_CFLAGS = ${GTK_CFLAGS}
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

