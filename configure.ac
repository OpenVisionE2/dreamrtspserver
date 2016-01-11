AC_INIT([dreamrtspserver],[0.1],[fraxinas@opendreambox.org])
AM_INIT_AUTOMAKE([foreign])

AC_CONFIG_SRCDIR([src/dreamrtspserver.c])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

dnl find a compiler
AC_PROG_CC
AC_PROG_CC_STDC

dnl check if the compiler supports '-c' and '-o' options
AM_PROG_CC_C_O

# Checks for header files.
AC_CHECK_HEADERS([stdio.h stdlib.h fcntl.h string.h getopt.h byteswap.h netinet/in.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Check for libsoup
PKG_CHECK_MODULES(LIBSOUP, [libsoup-2.4 >= 2.42])
AC_SUBST(LIBSOUP_CFLAGS)
AC_SUBST(LIBSOUP_LIBS)

# Check for Gstreamer 1.0
PKG_CHECK_MODULES(GST, [gstreamer-1.0], [])
PKG_CHECK_MODULES(GSTRTSP, [gstreamer-rtsp-1.0], [])
PKG_CHECK_MODULES(GSTRTSPSERVER, [gstreamer-rtsp-server-1.0], [])
PKG_CHECK_MODULES(GSTAPP, [gstreamer-app-1.0 ], [])
PKG_CHECK_MODULES(GIO, [gio-2.0 ], [])

AC_ARG_WITH(upstream,
	AS_HELP_STRING([--with-upstream],[enable mediator upstream feature @<:@default=disable@:>@]),
	[with_upstream="$withval"],[with_upstream="no"])
if test "$with_upstream" = "yes"; then
	AC_DEFINE([HAVE_UPSTREAM],[1],[defined to 1 if upstream is available])
fi
AM_CONDITIONAL(HAVE_UPSTREAM, test "$with_upstream" = "yes")

AC_CONFIG_FILES([
Makefile
src/Makefile
])
AC_OUTPUT

AS_IF([test "$with_upstream" = "yes"],[
echo "  Enabled mediator upstream feature"
])
