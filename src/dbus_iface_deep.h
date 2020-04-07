/*
 * Copyright (C) 2017, 2020  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of TACAMan.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#ifndef DBUS_IFACE_DEEP_H
#define DBUS_IFACE_DEEP_H

#include "dbus_iface.h"
#include "de_tahifi_artcache.h"

#ifdef __cplusplus
extern "C" {
#endif

tdbusArtCacheMonitor *dbus_get_artcache_monitor_iface(void);

#ifdef __cplusplus
}
#endif

#endif /* !DBUS_IFACE_DEEP_H */
