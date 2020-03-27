/*
 * Copyright (C) 2017, 2020  T+A elektroakustik GmbH & Co. KG
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

#ifndef DBUS_HANDLERS_H
#define DBUS_HANDLERS_H

#include <gio/gio.h>

#include "de_tahifi_artcache.h"

/*!
 * \addtogroup dbus_handlers DBus handlers for signals
 * \ingroup dbus
 */
/*!@{*/

#ifdef __cplusplus
extern "C" {
#endif

gboolean dbusmethod_cache_get_scaled_image(tdbusArtCacheRead *object,
                                           GDBusMethodInvocation *invocation,
                                           GVariant *stream_key, const char *format,
                                           GVariant *hash, gpointer user_data);

gboolean dbusmethod_cache_add_by_uri(tdbusArtCacheWrite *object,
                                     GDBusMethodInvocation *invocation,
                                     GVariant *stream_key,
                                     guchar image_priority,
                                     const gchar *image_uri,
                                     gpointer user_data);
gboolean dbusmethod_cache_add_by_data(tdbusArtCacheWrite *object,
                                      GDBusMethodInvocation *invocation,
                                      GVariant *stream_key,
                                      guchar image_priority,
                                      GVariant *image_data,
                                      gpointer user_data);

#ifdef __cplusplus
}
#endif

/*!@}*/

#endif /* !DBUS_HANDLERS_H */
