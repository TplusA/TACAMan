#
# Copyright (C) 2017, 2020  T+A elektroakustik GmbH & Co. KG
#
# This file is part of TACAMan.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
# MA  02110-1301, USA.
#

ACLOCAL_AMFLAGS = -I ../m4

bin_PROGRAMS = tacaman

tacaman_SOURCES = \
    tacaman.cc \
    artcache.hh artcache.cc cachepath.hh cachetypes.hh \
    artcache_background.cc \
    converterqueue.hh converterqueue.cc converterjob.cc \
    pending.hh \
    formats.hh formats.cc \
    md5.cc md5.hh \
    messages.h messages.c messages_glib.h messages_glib.c \
    dbus_iface.c dbus_iface.h dbus_iface_deep.h dbus_handlers.hh \
    backtrace.c backtrace.h os.c os.h os.hh \
    ../dbus_interfaces/de_tahifi_artcache_errors.hh \
    ../dbus_interfaces/de_tahifi_artcache_monitor_errors.h \
    ../dbus_interfaces/de_tahifi_artcache_read_errors.h

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
    md5.hh \
    messages_dbus.h messages_dbus.c
libdbus_handlers_la_CFLAGS = $(AM_CFLAGS)
libdbus_handlers_la_CXXFLAGS = $(AM_CXXFLAGS)

nodist_libdebug_dbus_la_SOURCES = de_tahifi_debug.c de_tahifi_debug.h
libdebug_dbus_la_CFLAGS = $(CRELAXEDWARNINGS)

nodist_libartcache_dbus_la_SOURCES = de_tahifi_artcache.c de_tahifi_artcache.h
libartcache_dbus_la_CFLAGS = $(CRELAXEDWARNINGS)

BUILT_SOURCES = \
    $(nodist_libartcache_dbus_la_SOURCES) \
    de_tahifi_artcache-doc.h \
    de_tahifi_artcache-doc.md \
    de_tahifi_artcache.stamp \
    $(nodist_libdebug_dbus_la_SOURCES) \
    de_tahifi_debug-doc.h \
    de_tahifi_debug-doc.md \
    de_tahifi_debug.stamp

CLEANFILES = $(BUILT_SOURCES)

de_tahifi_artcache-doc.h: de_tahifi_artcache.stamp
de_tahifi_artcache-doc.md: de_tahifi_artcache.stamp
de_tahifi_artcache.c: de_tahifi_artcache.stamp
de_tahifi_artcache.h: de_tahifi_artcache.stamp
de_tahifi_artcache.stamp: $(DBUS_IFACES)/de_tahifi_artcache.xml
	$(GDBUS_CODEGEN) --generate-c-code=de_tahifi_artcache --c-namespace tdbus --interface-prefix de.tahifi. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_artcache-doc.md -H de_tahifi_artcache-doc.h -c tdbus -s de.tahifi. -n "Cover Art Cache"
	touch $@


de_tahifi_debug-doc.h: de_tahifi_debug.stamp
de_tahifi_debug-doc.md: de_tahifi_debug.stamp
de_tahifi_debug.c: de_tahifi_debug.stamp
de_tahifi_debug.h: de_tahifi_debug.stamp
de_tahifi_debug.stamp: $(DBUS_IFACES)/de_tahifi_debug.xml
	$(GDBUS_CODEGEN) --generate-c-code=de_tahifi_debug --c-namespace tdbus_debug --interface-prefix de.tahifi.Debug. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_debug-doc.md -H de_tahifi_debug-doc.h -c tdbus_debug -s de.tahifi.Debug. -n "Introspection"
	touch $@
