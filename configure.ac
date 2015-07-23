AC_INIT([cdk], [0.1], [https://github.com/codebrainz/geany-cdk/Issues])
AC_CONFIG_MACRO_DIR([build-aux/m4])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
AM_SILENT_RULES([yes])

AM_PROG_AR
LT_INIT([disable-static])

AC_PROG_CC_C99

PKG_CHECK_MODULES([GEANY], [geany gtk+-3.0 glib-2.0 gio-2.0 gmodule-2.0])

AC_CHECK_HEADERS([clang-c/Index.h], [], [
	AC_MSG_ERROR([unable to find the Clang library header (clang-c/Index.h)])
])

AC_CHECK_LIB([clang], [clang_createIndex], [
	LDFLAGS="$LDFLAGS -lclang"
], [
	AC_MSG_ERROR([unable to find the Clang library (libclang)])
])

AC_CONFIG_HEADERS([cdk/config.h])
AC_CONFIG_FILES([
	Makefile
	cdk/Makefile
	cdk/cdk.pc
])
AC_OUTPUT
