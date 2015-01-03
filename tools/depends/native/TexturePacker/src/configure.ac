dnl Process this file with autoconf to produce a configure script.
AC_INIT(TexturePacker, 1.0, me@mail.com)
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_SRCDIR(TexturePacker.cpp)
AM_INIT_AUTOMAKE([foreign])
AC_PROG_CXX

abs_top_srcdir=${abs_top_srcdir=$(cd $srcdir; pwd)}
KODI_SRC_DIR=${KODI_SRC_DIR:-"${abs_top_srcdir}/../../../../.."}

PKG_CHECK_MODULES([PNG], [libpng],
  [INCLUDES="$PNG_CFLAGS"; LIBS=$(pkg-config --silence-errors --static --libs libpng)],
  AC_MSG_ERROR("libpng not found"))

AC_CHECK_HEADER([gif_lib.h],, AC_MSG_ERROR("gif_lib.h not found"))
AC_CHECK_LIB([gif],[main],, AC_MSG_ERROR("libgif not found"))
AC_CHECK_HEADER([jpeglib.h],, AC_MSG_ERROR("jpeglib.h not found"))
AC_CHECK_LIB([jpeg],[main],, AC_MSG_ERROR("libjpeg not found"))
AC_CHECK_HEADER([lzo/lzo1x.h],, AC_MSG_ERROR("lzo/lzo1x.h not found"))
AC_CHECK_LIB([lzo2],[main],, AC_MSG_ERROR("liblzo2 not found"))

AC_SUBST(KODI_SRC_DIR)
AC_SUBST(INCLUDES)
AC_SUBST(EXTRA_DEFINES)

AC_OUTPUT(Makefile)
