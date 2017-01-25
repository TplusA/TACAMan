#
# Copyright (C) 2017  T+A elektroakustik GmbH & Co. KG
#
# This file is part of TACAMan.
#
# TACAMan is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 3 as
# published by the Free Software Foundation.
#
# TACAMan is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TACAMan.  If not, see <http://www.gnu.org/licenses/>.
#

ACLOCAL_AMFLAGS = -I ../m4

bin_PROGRAMS = tacaman

tacaman_SOURCES = \
    tacaman.cc \
    messages.h messages.c messages_glib.h messages_glib.c \
    dbus_iface.c dbus_iface.h dbus_iface_deep.h dbus_handlers.hh \
    os.c os.h

DBUS_IFACES = $(top_srcdir)/dbus_interfaces

AM_CPPFLAGS = -DLOCALEDIR=\"$(localedir)\"
AM_CPPFLAGS += -I$(DBUS_IFACES)
AM_CPPFLAGS += $(TACAMAN_DEPENDENCIES_CFLAGS)

AM_CFLAGS = $(CWARNINGS)

AM_CXXFLAGS = $(CXXWARNINGS)

noinst_LTLIBRARIES = \
    libcachepath.la \
    libdbus_handlers.la \
    libartcache_dbus.la \
    libdebug_dbus.la

tacaman_LDADD = $(noinst_LTLIBRARIES) $(TACAMAN_DEPENDENCIES_LIBS)

tacaman_LDFLAGS = $(LTLIBINTL)

libcachepath_la_SOURCES = \
    cachepath.hh cachepath.cc
libcachepath_la_CFLAGS = $(AM_CFLAGS)
libcachepath_la_CXXFLAGS = $(AM_CXXFLAGS)

libdbus_handlers_la_SOURCES = \
    dbus_handlers.h dbus_handlers.hh dbus_handlers.cc \
    dbus_iface_deep.h \
    messages_dbus.h messages_dbus.c
libdbus_handlers_la_CFLAGS = $(AM_CFLAGS)
libdbus_handlers_la_CXXFLAGS = $(AM_CXXFLAGS)

nodist_libdebug_dbus_la_SOURCES = debug_dbus.c debug_dbus.h
libdebug_dbus_la_CFLAGS = $(CRELAXEDWARNINGS)

nodist_libartcache_dbus_la_SOURCES = artcache_dbus.c artcache_dbus.h
libartcache_dbus_la_CFLAGS = $(CRELAXEDWARNINGS)

BUILT_SOURCES = \
    $(nodist_libartcache_dbus_la_SOURCES) \
    de_tahifi_artcache.h \
    de_tahifi_artcache.md \
    artcache_dbus.stamp \
    $(nodist_libdebug_dbus_la_SOURCES) \
    de_tahifi_debug.h \
    de_tahifi_debug.md \
    debug_dbus.stamp

CLEANFILES = $(BUILT_SOURCES)

de_tahifi_artcache.h: artcache_dbus.stamp
de_tahifi_artcache.md: artcache_dbus.stamp
artcache_dbus.c: artcache_dbus.stamp
artcache_dbus.h: artcache_dbus.stamp
artcache_dbus.stamp: $(DBUS_IFACES)/de_tahifi_artcache.xml
	$(GDBUS_CODEGEN) --generate-c-code=artcache_dbus --c-namespace tdbus --interface-prefix de.tahifi. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_artcache.md -H de_tahifi_artcache.h -c tdbus -s de.tahifi. -n 'Cover Art Cache'
	touch $@

de_tahifi_debug.h: debug_dbus.stamp
de_tahifi_debug.md: debug_dbus.stamp
debug_dbus.c: debug_dbus.stamp
debug_dbus.h: debug_dbus.stamp
debug_dbus.stamp: $(DBUS_IFACES)/de_tahifi_debug.xml
	$(GDBUS_CODEGEN) --generate-c-code=debug_dbus --c-namespace tdbus_debug --interface-prefix de.tahifi.Debug. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_debug.md -H de_tahifi_debug.h -c tdbus_debug -s de.tahifi.Debug. -n "Introspection"
	touch $@

