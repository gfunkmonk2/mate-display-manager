NULL =

AM_CPPFLAGS = \
	-I$(top_srcdir)/common		\
	-DDMCONFDIR=\""$(dmconfdir)"\"                  \
	-DDATADIR=\""$(datadir)"\"	\
	-DUIDIR=\""$(uidir)"\" 	\
	-DMATELOCALEDIR=\""$(mdmlocaledir)"\" 	\
	$(MDMSETUP_CFLAGS)		\
	$(NULL)

bin_PROGRAMS =			\
	mdmsetup		\
	$(NULL)

mdmsetup_SOURCES = 		\
	mdmsetup.c 		\
	mdm-user.c 		\
	mdm-user-manager.c	\
	mdm-sessions.c           \
	$(NULL)

mdmsetup_LDADD =		\
	$(MDMSETUP_LIBS)	\
	$(NULL)
        
uidir = $(pkgdatadir)
ui_DATA = 			\
	mdmsetup.ui		\
	$(NULL)

Utilitiesdir = $(datadir)/applications
Utilities_in_files = mdmsetup.desktop.in
Utilities_DATA = $(Utilities_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = 			\
	$(ui_DATA)		\
	$(Utilities_in_files)	\
	$(NULL)
