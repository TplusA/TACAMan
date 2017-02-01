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

SUBDIRS = . src tests

ACLOCAL_AMFLAGS = -I m4

html_DATA = README.html

EXTRA_DIST = README.md

CLEANFILES = README.html

EXTRA_DIST += \
    dbus_interfaces/extract_documentation.py \
    dbus_interfaces/de_tahifi_artcache.xml \
    dbus_interfaces/de_tahifi_debug.xml \
    dbus_interfaces/de_tahifi_artcache_errors.hh \
    dbus_interfaces/de_tahifi_artcache_monitor_errors.h \
    dbus_interfaces/de_tahifi_artcache_read_errors.h

BUILT_SOURCES = versioninfo.h

CLEANFILES += $(BUILT_SOURCES)

versioninfo.h: versioninfo.cache
	(cd $(top_srcdir) && $(AUTOREVISION) -t h -o $(abs_top_builddir)/versioninfo.cache >$(abs_top_builddir)/$@)

.md.html:
	$(MARKDOWN) $< >$@
