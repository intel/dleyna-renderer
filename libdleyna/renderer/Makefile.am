libdleyna_rendererincdir = $(includedir)/dleyna-1.0/libdleyna/renderer

DLEYNA_RENDERER_VERSION = 1:2:0

AM_CFLAGS =	$(GLIB_CFLAGS)				\
		$(GIO_CFLAGS)				\
		$(DLEYNA_CORE_CFLAGS)			\
		$(GSSDP_CFLAGS)				\
		$(GUPNP_CFLAGS)				\
		$(GUPNPAV_CFLAGS)			\
		$(GUPNPDLNA_CFLAGS)			\
		$(SOUP_CFLAGS)				\
		-include config.h

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

pkglib_LTLIBRARIES = libdleyna-renderer-1.0.la

libdleyna_rendererinc_HEADERS = 	control-point-renderer.h

libdleyna_renderer_1_0_la_LDFLAGS =	-version-info $(DLEYNA_RENDERER_VERSION) \
					-no-undefined

libdleyna_renderer_1_0_la_SOURCES =	$(libdleyna_rendererinc_HEADERS) \
					async.c				 \
					device.c	 		 \
					host-service.c			 \
					server.c			 \
					task.c		 		 \
					upnp.c

libdleyna_renderer_1_0_la_LIBADD =	$(GLIB_LIBS)		\
					$(GIO_LIBS)		\
					$(DLEYNA_CORE_LIBS)	\
					$(GSSDP_LIBS)		\
					$(GUPNP_LIBS)		\
					$(GUPNPAV_LIBS) 	\
					$(GUPNPDLNA_LIBS) 	\
					$(SOUP_LIBS)

MAINTAINERCLEANFILES =	Makefile.in		\
			aclocal.m4		\
			configure		\
			config.h.in		\
			config.h.in~		\
			build-aux/depcomp	\
			build-aux/compile	\
			build-aux/missing	\
			build-aux/install-sh

sysconf_DATA = dleyna-renderer-service.conf

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = dleyna-renderer-1.0.pc

EXTRA_DIST = 	$(sysconf_DATA)			\
		async.h				\
		device.h			\
		host-service.h			\
		prop-defs.h			\
		server.h			\
		task.h				\
		upnp.h

CLEANFILES = $(pkgconfig_DATA) dleyna-renderer-service.conf
DISTCLEANFILES = $(pkgconfig_DATA) dleyna-renderer-service.conf

maintainer-clean-local:
	rm -rf build-aux
