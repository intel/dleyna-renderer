AM_CFLAGS =	$(GLIB_CFLAGS)				\
		$(GIO_CFLAGS)				\
		$(DLEYNA_CORE_CFLAGS)			\
		-I$(top_builddir)/libdleyna/renderer	\
		-include config.h

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
		
libexec_PROGRAMS = dleyna-renderer-service

dleyna_renderer_service_SOURCES =	daemon.c

dleyna_renderer_service_LDADD =	$(GLIB_LIBS)				      			\
				$(GIO_LIBS)				     			\
				$(DLEYNA_CORE_LIBS)			      			\
				$(top_builddir)/libdleyna/renderer/libdleyna-renderer-1.0.la
			
			
dbussessiondir = @DBUS_SESSION_DIR@
dbussession_DATA = com.intel.dleyna-renderer.service

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = dleyna-renderer-service-1.0.pc

CLEANFILES = $(dbussession_DATA) $(pkgconfig_DATA)
DISTCLEANFILES = $(dbussession_DATA) $(pkgconfig_DATA)
			
