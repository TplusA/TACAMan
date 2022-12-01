#
# Copyright (C) 2020, 2021, 2022  T+A elektroakustik GmbH & Co. KG
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

config_h = declare_dependency(
    sources: configure_file(input: '../config.h.meson',
                            output: 'config.h',
                            configuration: config_data),
    include_directories: '.'
)

gnome = import('gnome')

dbus_iface_dir = '../dbus_interfaces'
dbus_iface_defs_includes = include_directories(dbus_iface_dir)

dbus_iface_data = [
    ['de_tahifi_artcache', 'de.tahifi.',       'tdbus',       'Cover Art'],
    ['de_tahifi_debug',    'de.tahifi.Debug.', 'tdbus_debug', 'Debug Levels'],
]

dbus_headers = []
dbus_deps = []
dbus_docs = []

foreach d : dbus_iface_data
    codegen = gnome.gdbus_codegen(d[0],
                                  sources: dbus_iface_dir / d[0] + '.xml',
                                  interface_prefix: d[1],
                                  namespace: d[2])

    dbus_headers += codegen[1]

    dbus_deps += declare_dependency(
        link_with: static_library(
            d[0].split('_')[-1] + '_dbus', codegen,
            dependencies: [glib_deps, config_h],
            c_args: relaxed_dbus_warnings
        ),
        include_directories: dbus_iface_defs_includes
    )

    dbus_docs += custom_target(d[0] + '_docs',
        input: dbus_iface_dir / d[0] + '.xml',
        output: ['@BASENAME@-doc.md', '@BASENAME@-doc.h'],
        command: [
            extract_docs, '-i', '@INPUT@', '-o', '@OUTPUT0@', '-H', '@OUTPUT1@',
            '-c', d[2], '-s', d[1], '-n', d[3]
        ])

    codegen = []
endforeach

cachepath_lib = static_library('cachepath', 'cachepath.cc', dependencies: config_h)

dbus_handlers_lib = static_library('dbus_handlers',
    ['dbus_handlers.cc', 'messages_dbus.c', dbus_headers],
    include_directories: dbus_iface_defs_includes,
    dependencies: [dbus_deps, glib_deps, config_h]
)

executable(
    'tacaman',
    [
        'tacaman.cc', 'artcache.cc', 'artcache_background.cc',
        'converterqueue.cc', 'converterjob.cc', 'formats.cc', 'md5.cc',
        'messages.c', 'messages_glib.c', 'dbus_iface.c', 'backtrace.c', 'os.c',
        dbus_headers, version_info,
    ],
    include_directories: dbus_iface_defs_includes,
    dependencies: [dbus_deps, glib_deps, config_h],
    link_with: [
        cachepath_lib,
        dbus_handlers_lib,
    ],
    install: true
)
