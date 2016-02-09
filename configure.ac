#Prelude
AC_INIT([audio-coffin],[0.6],[mickflemm+audio-coffin@gmail.com])
AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign -Wall -Werror dist-bzip2])

#Check for programs
AC_PROG_CC

#Check for libraries
AC_CHECK_LIB([m],[sin],[LIBM=-lm],
	     AC_MSG_ERROR([Could not find libm]))
AC_SUBST([LIBM])
AC_CHECK_LIB([rt],[shm_open],[LIBRT=-lrt],
	     AC_MSG_ERROR([Could not find librt]))
AC_SUBST([LIBRT])
AC_CHECK_LIB([soxr-lsr],[src_process],[LIBSOXR=-lsoxr-lsr],
	     AC_MSG_ERROR([Could not find libsoxr]))
AC_SUBST([LIBSOXR])
AC_CHECK_LIB([sndfile],[sf_open],[LIBSNDFILE=-lsndfile],
	     AC_MSG_ERROR([Could not find libsndfile]))
AC_SUBST([LIBSNDFILE])
AC_CHECK_LIB([jack],[jack_client_open],[LIBJACK=-ljack],
	     AC_MSG_ERROR([Could not find jack libraries]))
AC_SUBST([LIBJACK])
PKG_CHECK_MODULES([GTK],[gtk+-3.0])

#Check for headers
AC_CHECK_HEADERS([limits.h stdint.h stdlib.h string.h signal.h math.h])
AC_CHECK_HEADER([sndfile.h],[],
		AC_MSG_ERROR([Could not find sndfile.h]))
AC_CHECK_HEADER([soxr-lsr.h],[],
		AC_MSG_ERROR([Could not find soxr-lsr.h]))
AC_CHECK_HEADER([jack/jack.h],[],
		AC_MSG_ERROR([Could not find jack/jack.h]))

#Checks for typedefs, structures, and compiler characteristics.
AC_PREREQ
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

#Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([clock_gettime memset realpath])
#Output files
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
