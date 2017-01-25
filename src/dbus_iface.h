/*
 * Copyright (C) 2017  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of TACAMan.
 *
 * TACAMan is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3 as
 * published by the Free Software Foundation.
 *
 * TACAMan is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TACAMan.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DBUS_IFACE_H
#define DBUS_IFACE_H

#include <stdbool.h>
#include <glib.h>

/*!
 * \addtogroup dbus DBus handling
 */
/*!@{*/

#ifdef __cplusplus
extern "C" {
#endif

int dbus_setup(GMainLoop *loop, bool connect_to_session_bus,
               void *dbus_signal_data_for_dbus_handlers);
void dbus_shutdown(GMainLoop *loop);

#ifdef __cplusplus
}
#endif

/*!@}*/

#endif /* !DBUS_IFACE_H */
