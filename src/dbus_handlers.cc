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

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cstring>
#include <cerrno>

#include "dbus_handlers.h"
#include "dbus_handlers.hh"
#include "messages.h"

static void enter_artcache_read_handler(GDBusMethodInvocation *invocation)
{
    static const char iface_name[] = "de.tahifi.ArtCache.Read";

    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s method invocation from '%s': %s",
              iface_name, g_dbus_method_invocation_get_sender(invocation),
              g_dbus_method_invocation_get_method_name(invocation));
}

gboolean dbusmethod_cache_get_scaled_image(tdbusArtCacheRead *object,
                                           GDBusMethodInvocation *invocation,
                                           GVariant *stream_key, GVariant *hash,
                                           gpointer user_data)
{
    enter_artcache_read_handler(invocation);

    auto *data = static_cast<DBus::SignalData *>(user_data);
    log_assert(data != nullptr);

    GVariantBuilder hash_builder;
    g_variant_builder_init(&hash_builder, G_VARIANT_TYPE("ay"));

    GVariantBuilder data_builder;
    g_variant_builder_init(&data_builder, G_VARIANT_TYPE("ay"));

    tdbus_art_cache_read_complete_get_scaled_image_data(object, invocation,
                                                        2, 0,
                                                        g_variant_builder_end(&hash_builder),
                                                        g_variant_builder_end(&data_builder));

    return TRUE;
}

static void enter_artcache_write_handler(GDBusMethodInvocation *invocation)
{
    static const char iface_name[] = "de.tahifi.ArtCache.Write";

    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s method invocation from '%s': %s",
              iface_name, g_dbus_method_invocation_get_sender(invocation),
              g_dbus_method_invocation_get_method_name(invocation));
}

gboolean dbusmethod_cache_add_by_uri(tdbusArtCacheWrite *object,
                                     GDBusMethodInvocation *invocation,
                                     GVariant *stream_key,
                                     guchar image_priority,
                                     const gchar *image_uri,
                                     gpointer user_data)
{
    enter_artcache_write_handler(invocation);

    auto *data = static_cast<DBus::SignalData *>(user_data);
    log_assert(data != nullptr);

    tdbus_art_cache_write_complete_add_image_by_uri(object, invocation);

    return TRUE;
}

gboolean dbusmethod_cache_add_by_data(tdbusArtCacheWrite *object,
                                      GDBusMethodInvocation *invocation,
                                      GVariant *stream_key,
                                      guchar image_priority,
                                      GVariant *image_data,
                                      gpointer user_data)
{
    enter_artcache_write_handler(invocation);

    auto *data = static_cast<DBus::SignalData *>(user_data);
    log_assert(data != nullptr);

    tdbus_art_cache_write_complete_add_image_by_data(object, invocation);

    return TRUE;
}
