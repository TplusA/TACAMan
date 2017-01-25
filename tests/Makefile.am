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

if WITH_CUTTER
TESTS = run_test.sh

if WITH_VALGRIND
TESTS += valgrind.sh
endif

AM_TESTS_ENVIRONMENT = CUTTER="$(CUTTER)" XSLTPROC="$(XSLTPROC)" VALGRIND="$(VALGRIND)"

AM_LDFLAGS = -module -rpath $(libdir) -avoid-version -no-undefined

AM_CPPFLAGS = $(CPPCUTTER_CFLAGS)
AM_CPPFLAGS += -I$(top_srcdir)/src -I$(top_builddir)/src
AM_CPPFLAGS += -I$(top_srcdir)/dbus_interfaces

AM_CFLAGS = $(CWARNINGS)

AM_CXXFLAGS = $(CXXWARNINGS)

LIBS += $(CPPCUTTER_LIBS)

check_LTLIBRARIES = \
    test_dbus_handlers.la \
    test_cachepath.la

test_dbus_handlers_la_SOURCES = \
    test_dbus_handlers.cc mock_expectation.hh \
    mock_messages.hh mock_messages.cc
test_dbus_handlers_la_LIBADD = \
    $(top_builddir)/src/libdbus_handlers.la
test_dbus_handlers_la_CFLAGS = $(TACAMAN_DEPENDENCIES_CFLAGS) $(AM_CFLAGS)
test_dbus_handlers_la_CXXFLAGS = $(TACAMAN_DEPENDENCIES_CFLAGS) $(AM_CXXFLAGS)

test_cachepath_la_SOURCES = \
    test_cachepath.cc mock_expectation.hh \
    mock_messages.hh mock_messages.cc \
    mock_os.hh mock_os.cc
test_cachepath_la_LIBADD = $(top_builddir)/src/libcachepath.la
test_cachepath_la_CFLAGS = $(AM_CFLAGS)
test_cachepath_la_CXXFLAGS = $(AM_CXXFLAGS)

CLEANFILES = test_report.xml test_report_junit.xml valgrind.xml

EXTRA_DIST = cutter2junit.xslt
EXTRA_DIST += cutter-1_2_4.supp

check-local: check-TESTS
	@if $(GREP) -w cutter $(TEST_LOGS); then \
	    echo "Unit tests failed (check log)"; \
	    exit 1; \
	fi
endif
