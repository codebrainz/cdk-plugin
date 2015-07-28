plugindir = $(libdir)/geany
plugin_LTLIBRARIES = cdkplugin.la

cdkplugin_la_CFLAGS = $(GEANY_CFLAGS) -I$(top_srcdir) \
	-DCDK_PLUGIN_UI_DIR=\""$(pkgdatadir)/ui"\"
cdkplugin_la_LDFLAGS = $(GEANY_LIBS) -module -avoid-version
cdkplugin_la_LIBADD = libcdk.la
cdkplugin_la_SOURCES = plugin.c

gladeuidir = $(pkgdatadir)/ui
gladeui_DATA = projectpanel.glade

lib_LTLIBRARIES = libcdk.la

libcdk_la_CFLAGS = $(GEANY_CFLAGS) -I$(top_srcdir) -I$(top_builddir)/cdk \
	-DCDK_STYLE_SCHEME_DIR=\""$(pkgdatadir)/style-schemes"\"
libcdk_la_LDFLAGS = $(GEANY_LIBS)
libcdk_la_SOURCES = \
	cdk.h \
	cdkcompleter.c \
	cdkcompleter.h \
	cdkdiagnostics.c \
	cdkdiagnostics.h \
	cdkdocumenthelper.c \
	cdkdocumenthelper.h \
	cdkhighlighter.c \
	cdkhighlighter.h \
	cdkplugin.c \
	cdkplugin.h \
	cdkstyle.c \
	cdkstyle.h \
	cdkstylescheme.c \
	cdkstylescheme.h \
	cdkutils.c \
	cdkutils.h

cdkincludedir = $(includedir)/cdk
cdkinclude_HEADERS = \
	cdk.h \
	cdkcompleter.h \
	cdkdiagnostics.h \
	cdkdocumenthelper.h \
	cdkhighlighter.h \
	cdkplugin.h \
	cdkstyle.h \
	cdkstylescheme.h \
	cdkutils.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = cdk.pc

styleschemedir = $(pkgdatadir)/style-schemes
stylescheme_DATA = default.xml

EXTRA_DIST = cdk.pc.in cdkmarkererror.c cdkmarkerwarning.c
